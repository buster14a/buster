/* #881 PR 1 to PR 3 fixture: the retirement producer behind bq_worker_unit.
 * Included by the preparation test runner after retirement_unit_campaign_tests.h.
 *
 * The attempts are the real unit-oracle fixture's (retirement_unit_oracle_tests.h),
 * whose matched builds freeze the stand-in compilers
 * (retirement_stand_in_compiler.h) and whose candidate snapshot carries what
 * they copy. A reference attempt's projection pins B's stand-in required
 * checks, a row plan over the validator's timed partition
 * (bq_prep_worker_unit_plan_text: one batch template per timed object group,
 * whose metrics leaf names its row, and one for the untimed object groups),
 * the reviewed budget record, the untimed-command contract
 * (bq_prep_worker_unit_untimed_text) and lane D's frozen campaign values, all
 * installed under recipes/. The gate takes an observation of that plan
 * through its test seam whose artifacts, objects and code facts are the
 * stand-in outputs' own (bq_prep_worker_unit_observe), so real launches
 * reproduce them; it is re-addressed to each producer attempt (the plan's
 * commands are logical, so only the job and token differ). The test process
 * plays the coordinator (bq_prep_worker_unit_drive): it hands the lease and
 * the phase channel to a forked worker-unit through
 * bq_worker_lease_handoff_send, resumes the paused unit, acknowledges every
 * phase message in order and reads the channel to EOF. The unit runs
 * bq_worker_unit_pinned with the fixture's complete profile and roots; it
 * reports its BqError as its exit status, or 200 if it was left with a child
 * (a keeper it did not stop, a producer it did not reap) or a changed
 * descriptor count.
 *
 * Covered: bq_retirement_profile_complete over each pin, lane D's campaign
 * values and the status line; the compiled blocked profile, a blocked status
 * and a missing pin refused before the handoff, with no keeper directory and
 * no retirement directory (bq_prep_worker_unit_refused); the producer through
 * the ready record, which the coordinator replay accepts, then SETTLING (the
 * untimed batches with real launches), MEASURING (the store-based bind, the
 * documents sized before lane E's store plan and written at exactly those
 * sizes, the untimed streams published, attach), A/A (each runtime launch
 * running lane B's `./{{output}}` program, retained from the stage's own
 * compile and copied into a fresh step directory; the runtime rows compile the host
 * program BQ_PREP_ORACLE_PROGRAM, which prints the reference oracle's
 * output), the fixture admission, the post-A/A document, the freeze, A/B and
 * READY: all five documents at the sizes retained before timing
 * (bq_prep_worker_unit_sized), the post-sample record with every launch
 * (bq_prep_worker_unit_post_sample) and every stream kind published
 * (bq_prep_worker_unit_result). Failure retention (campaign-failure.txt)
 * on a failing untimed launch (job 85), a SIGTERM during A/A (job 86), the
 * deadline expiring during A/A (job 87, whose launches cannot fit the time
 * left) and a detached (setsid) sleeper left by a timed compile (job 88),
 * found by the descendant check after that launch; each with the keeper
 * stopped and nothing left running. SIGTERM to
 * the unit during the hanging generate of job
 * 63, forwarded to the producer's self-pipe, ending cancelled with the stage
 * gone; the failing generate of job 67; the producer killed during the
 * hanging generate of job 64; the detached (setsid, double-fork) grandchild
 * job 73's generate leaves, which only the producer's subreaper exposes:
 * the gate refuses and the producer sweeps it (BQ_CLEANUP_FAILED, no ready
 * record, the grandchild gone); and a SIGTERM raised in the unit's teardown
 * window after job 30's failing generate
 * (bq_retirement_worker_unit_test_late_term), consumed and reported as
 * cancelled instead of ending the unit before its keeper stops. Jobs 30, 63,
 * 64, 67 and 73 select those driver behaviours in
 * retirement_matched_build_fixture.c.
 *
 * #881 PR 4: the channel is BQPHASE2, and the successful unit sends
 * RETIREMENT_READY with exactly the published record's digest.
 * bq_prep_worker_unit_coordinator then checks the coordinator's side:
 * bq_retirement_coordinator_replay accepts that digest and refuses another,
 * a flipped record byte and the compiled profile; bq_worker_retirement_finalize
 * (bq_worker_finish's hook) needs the digests, the replay and the journalled
 * authority, stops at the deadline, reloads the digests in recovery and holds
 * a durable success, and passes the smoke recipe;
 * and bq_retirement_request_valid_pinned, bq_worker_finalization_recipe and
 * bq_worker_recipe_launchable admit the recipe only through the complete
 * seams, also while queue.c's test seam makes the queue's own predicate
 * admit (#881 N1; the result binding likewise in bq_prep_worker_unit_bound).
 * bq_prep_worker_unit_campaign_order checks that the campaign's
 * SETTLING follows RETIREMENT_READY on a BQPHASE2 channel.
 *
 * #881 PR 3: job 82 reaches MEASURED through the real coordinator path
 * (bq_prep_worker_unit_coordinate: the queue's active job, the lease and
 * channel handed to the forked unit, bq_worker_phase_join acknowledging every
 * phase and handing the authority off). The composition pins its adapter
 * (this runner's `retirement-replay` mode) and the binding context the Python
 * fixture emits (bq_prep_worker_unit_composition,
 * retirement_binding_context_fixture.py). Checked: the phase receipts, the
 * finalization (replay, journalled authority and its derivation), every
 * composer output, the binding and admission receipt, the manifest and bundle
 * (bq_prep_worker_unit_result, bq_prep_worker_unit_bound); the exported
 * result for the Python checks, whose binding the #511 validator accepts
 * (bq_prep_worker_unit_export); over one shared replay, the derivation's
 * refusals of a forged plan digest, another ready record, a changed stage
 * fact, another binding digest, a flipped context, and scratch copies with a
 * changed untimed file, a record byte, every checked record line forged
 * consistently, and a binding relinked or with another measurement harness
 * (bq_prep_worker_unit_derivation, BqPrepWorkerUnitForge,
 * bq_prep_worker_unit_forged), and the binding writer's refusals
 * (bq_prep_worker_unit_binding_refusals); the 39 evidence files the binding
 * context names published at their digests and sealed, and the publication's
 * refusals of swapped held binaries, a changed or missing installed file and
 * an unmappable path (bq_prep_worker_unit_evidence_refusals); one table of
 * binding paths on which the producer's result-root names and lane F's
 * evidence_name agree (bq_prep_worker_unit_evidence_names); a
 * tampered manifest or bundle digest
 * and a missing context chain (bq_prep_worker_unit_result_refusals). Each
 * section's wall time is printed (bq_prep_test_timing).
 *
 * #881 recovery L2 (bq_prep_worker_unit_recovery): job 82 recovered by the
 * real bq_worker_recover after a reboot. A handoff that timed out after the
 * copy, a COMPLETE handoff without worker-phase-4, a worker-phase-4 over an
 * INCOMPLETE handoff, a cancelled job's timed-out handoff and a durable
 * success in the queue without worker-phase-4 are classified
 * (bq_retirement_coordinator_handoff_class), poisoned and held twice: never
 * succeeded or rewritten, the next reservation refused, the evidence kept,
 * the reconciliation refusing the retirement recipe, and the poison refusing
 * a durable success after the records are restored. The intact handoff
 * (complete) and an emptied one (absent) are not held. In the live run
 * (bq_prep_worker_unit_live_handoff), a MEASURED handoff that fails in
 * bq_worker_phase_accept is held by bq_worker_finish the same way. The
 * leftovers, poison-record and reconciliation/export guards without the
 * producer are in tests.c (bq_test_retirement_poison_hold,
 * bq_test_retirement_poison_reconcile).
 *
 * #881 production generators: retirement_required_checks.py over the
 * reference projection's plan emits exactly the installed authority, which
 * the importer accepts (bq_prep_worker_unit_generated_checks), and
 * retirement_binding_context.py over the fixture's test record with the
 * reference attempt's held binaries emits a context the binding check
 * accepts and whose swapped, missing or moved-sentinel variants it refuses
 * (bq_prep_worker_unit_production_context). Both run before job 82, so a
 * campaign failure cannot skip them. */
#ifndef BUSTER_BENCH_SERVICE_RETIREMENT_WORKER_UNIT_TESTS_H
#define BUSTER_BENCH_SERVICE_RETIREMENT_WORKER_UNIT_TESTS_H

#define BQ_PREP_WORKER_UNIT_REFERENCE 81u
#define BQ_PREP_WORKER_UNIT_SUCCESS 82u
#define BQ_PREP_WORKER_UNIT_CANCEL 63u
#define BQ_PREP_WORKER_UNIT_CRASH 64u
#define BQ_PREP_WORKER_UNIT_FAILURE 67u
#define BQ_PREP_WORKER_UNIT_ESCAPE 73u
/* Job 30 (token 40) also fails its generate. */
#define BQ_PREP_WORKER_UNIT_LATE_TERM 30u
/* The stand-in compilers' job behaviours (retirement_stand_in_compiler.h). */
#define BQ_PREP_WORKER_UNIT_UNTIMED_FAILURE 85u
#define BQ_PREP_WORKER_UNIT_AA_TERMINATE 86u
#define BQ_PREP_WORKER_UNIT_AA_DEADLINE 87u
#define BQ_PREP_WORKER_UNIT_AA_ESCAPE 88u
/* The evidence files the fixture's binding context names
 * (retirement_binding_context_fixture.py): all 39 of the #511 record. */
#define BQ_PREP_WORKER_UNIT_EVIDENCE 39u
/* The unit's exit status when it kept a child or a descriptor. */
#define BQ_PREP_WORKER_UNIT_UNCLEAN 200
#define BQ_PREP_WORKER_UNIT_MILLISECONDS 300000u
/* The fixture plan's template timeout: every launch needs this much time
 * left before the job deadline. */
#define BQ_PREP_WORKER_UNIT_TIMEOUT_SECONDS 5u
/* The untimed contract's batch target word (every fixture batch writes the
 * one metrics record in A's candidate snapshot). */
#define BQ_PREP_WORKER_UNIT_UNTIMED_TARGET BQ_RETIREMENT_ROW_BATCH_TARGET

/* The #437 A/A admission receipt stand-in the fixture build's producer
 * presents (retirement_worker_campaign.c declares it under
 * BQ_RETIREMENT_UNIT_CAMPAIGN_FIXTURE_AA): the validator's AA_SCHEMA keys in
 * sorted order with test identities, over the driver's family and the
 * campaign's CPU; its identities are the ones the fixture's binding context
 * binds (retirement_binding_context_fixture.py). It is test data, not a #426
 * decision. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_worker_campaign_fixture_receipt(BqRetirementUnitCampaign const* driver,
    TpRetirementCampaign const* campaign, char receipt[BQ_RETIREMENT_UNIT_CAMPAIGN_AA_RECEIPT_BYTES_MAX],
    u32* length, char digest[SHA256_HEX_CAPACITY])
{
    int written = driver && campaign ? snprintf(receipt, BQ_RETIREMENT_UNIT_CAMPAIGN_AA_RECEIPT_BYTES_MAX,
        "{\"admitted\":true,\"baseline_source_commit\":\"%040d\",\"baseline_source_tree\":\"%040d\","
        "\"family_sha256\":\"%s\",\"lease_protocol\":\"server-authoritative-supervisor-lease-v1\","
        "\"logical_cpu\":%d,\"machine_id\":\"fixture-machine\",\"native_only\":true,"
        "\"native_target\":\"x86_64-unknown-linux-gnu\",\"profile_id\":\"fixture-profile\","
        "\"profile_version\":\"fixture-profile-v1\","
        "\"schema\":\"" BQ_RETIREMENT_UNIT_CAMPAIGN_AA_SCHEMA "\",\"service_id\":\"fixture-service\",\"version\":1}",
        1, 2, driver->family_sha256, campaign->cpu) : -1;
    bool ok = written > 0 && written < (int)BQ_RETIREMENT_UNIT_CAMPAIGN_AA_RECEIPT_BYTES_MAX;
    if (ok) bq_digest(receipt, (u32)written, (char8*)digest);
    *length = ok ? (u32)written : 0;
    return ok;
}

/* The next line of the #619 series whose manifest is `manifest` (its shards
 * are the leaves it names beside it, read in order), or NULL at its end. */
BUSTER_GLOBAL_LOCAL char* bq_prep_worker_unit_series_line(FILE* manifest, FILE** shard, char* line, size_t capacity)
{
    char* result = NULL;
    bool done = false;
    while (!done)
    {
        if (*shard && fgets(line, (int)capacity, *shard))
        {
            result = line;
            done = true;
        }
        else
        {
            char entry[512], leaf[256];
            if (*shard) fclose(*shard);
            *shard = NULL;
            while (!*shard && fgets(entry, sizeof(entry), manifest))
                if (sscanf(entry, "shard=%*u offset=%*u bytes=%*u sha256=%*s path=%255s", leaf) == 1)
                    *shard = fopen(leaf, "rb");
            done = !*shard;
        }
    }
    return result;
}

/* The worker-unit fixture's composer adapter (this runner's
 * `retirement-replay` mode, executed by the composer from its hashed
 * descriptor): one structurally valid #619 result per series member. It
 * stands in for the reviewed `bench_throughput retirement-replay` adapter,
 * which the installed service pins instead; its bounds are test data, not a
 * statistics result. Exit 0 only when every declared member was written. */
BUSTER_GLOBAL_LOCAL int bq_prep_worker_unit_adapter(char const* input_path, char const* output_path)
{
    FILE* input = fopen(input_path, "rb");
    FILE* output = input ? fopen(output_path, "wb") : NULL;
    FILE* shard = NULL;
    char line[4096];
    unsigned members = 0, bootstrap = 0, cells = 0, resamples = 0, pairs = 0, version = 0, total = 0;
    unsigned long long seed = 0;
    bool ok = input && output && bq_prep_worker_unit_series_line(input, &shard, line, sizeof(line)) &&
              sscanf(line, "version=%u seed=%llu bootstrap_members=%u cell_members=%u pairs=%u resamples=%u frozen=1 "
                     "members=%u", &version, &seed, &bootstrap, &cells, &pairs, &resamples, &total) == 7;
    if (ok) fputs("{\"schema\":\"buster-native-retirement-statistics-replay-v1\",\"version\":1,\"members\":[", output);
    while (ok && bq_prep_worker_unit_series_line(input, &shard, line, sizeof(line)))
    {
        char name[TP_RETIREMENT_COMPOSE_MEMBER_BYTES];
        unsigned metric = 0, kind = 0, family = 0, member_cells = 0, member_pairs = 0, member_resamples = 0;
        double limit = 0.0;
        if (sscanf(line, "member=%127s metric=%u kind=%u family=%u cells=%u pairs=%u resamples=%u limit=%lf", name,
                   &metric, &kind, &family, &member_cells, &member_pairs, &member_resamples, &limit) == 8)
        {
            double alpha = 0.05 / (2.0 * 3.0 * 2.0 * (double)(kind ? cells : bootstrap));
            fprintf(output, "%s{\"member\":\"%s\",\"metric\":%u,\"kind\":%u,\"family_index\":%u,\"outcome\":\"pass\","
                    "\"valid\":true,\"resampled\":%s,\"resamples\":%u,\"tail_alpha\":%.17g,"
                    "\"round\":[{\"estimate\":1.0,\"lower\":0.99,\"upper\":1.001},"
                    "{\"estimate\":1.0,\"lower\":0.99,\"upper\":1.001}],"
                    "\"pooled\":{\"estimate\":1.0,\"lower\":0.995,\"upper\":1.0005}}",
                    members++ ? "," : "", name, metric, kind, family, kind ? "false" : "true", member_resamples, alpha);
        }
    }
    if (ok) fputs("]}\n", output);
    if (output && fclose(output) != 0) ok = false;
    if (shard) fclose(shard);
    if (input) fclose(input);
    int status = ok && members == total ? 0 : 2;
    return status;
}

typedef enum BqPrepWorkerUnitMode
{
    BQ_PREP_WORKER_UNIT_RUN,
    BQ_PREP_WORKER_UNIT_TERMINATE,
    BQ_PREP_WORKER_UNIT_KILL_PRODUCER,
    /* SIGTERM to the unit inside A/A: once the first second-label launch
     * left the stand-in's marker, at most a few seconds after MEASURING. */
    BQ_PREP_WORKER_UNIT_TERMINATE_MEASURING,
} BqPrepWorkerUnitMode;

/* The bound on the MEASURING-to-SIGTERM wait when no marker appears: past
 * the bind, documents and store plan, inside A/A (whose second-label
 * launches sleep a second for that job). The marker normally ends it within
 * one 50 ms poll of that launch's start. */
#define BQ_PREP_WORKER_UNIT_TERMINATE_DELAY_MS 4000u

typedef struct BqPrepWorkerUnitRun
{
    int status;
    u32 messages, preparing, ready;
    /* Each acknowledged message's phase, in order. */
    u32 phases[8];
    /* The digest the RETIREMENT_READY packet carried (#881 PR 4). */
    char ready_sha256[SHA256_HEX_CAPACITY];
    bool eof;
    /* The hanging generate's driver and the broker CLI above it. */
    pid_t driver, broker;
    /* When MEASURING was acknowledged (monotonic milliseconds), and the
     * handoff time. */
    u64 measuring_ms, started_ms;
} BqPrepWorkerUnitRun;

/* A process's parent, from /proc/<pid>/stat. */
BUSTER_GLOBAL_LOCAL pid_t bq_prep_worker_unit_parent(pid_t pid)
{
    char path[64], text[512];
    int length = snprintf(path, sizeof(path), "/proc/%ld/stat", (long)pid);
    char* state = length > 0 && (size_t)length < sizeof(path) && bq_prep_test_read_text(path, text, sizeof(text)) ?
                  strrchr(text, ')') : NULL;
    long parent = state && state[1] == ' ' && state[2] && state[3] == ' ' ? strtol(state + 4, NULL, 10) : 0;
    return (pid_t)parent;
}

/* The pid the hanging generate of attempt recorded, or 0. */
BUSTER_GLOBAL_LOCAL pid_t bq_prep_worker_unit_driver(BqPrepOracleFixture const* fixture, BqJob const* job)
{
    char path[256], text[32];
    int length = snprintf(path, sizeof(path), "%s/job-%" PRIu64 "-attempt-%" PRIu64 "/base/build/matched-build/pid",
                          fixture->workspaces, (uint64_t)job->id, (uint64_t)job->token);
    u32 used = length > 0 && (size_t)length < sizeof(path) ? bq_prep_test_read_text(path, text, sizeof(text)) : 0;
    long pid = used && text[used - 1] == '\n' ? strtol(text, NULL, 10) : 0;
    return (pid_t)pid;
}

/* Whether the keeper's socket for job is gone (a stopped keeper unlinks it). */
BUSTER_GLOBAL_LOCAL bool bq_prep_worker_unit_keeper_gone(BqPrepOracleFixture const* fixture, BqJob const* job)
{
    char path[256];
    struct stat info = {0};
    int length = snprintf(path, sizeof(path), "%s/results/.lease-return/%" PRIu64 "-%" PRIu64, fixture->workspaces,
                          (uint64_t)job->id, (uint64_t)job->token);
    bool gone = length > 0 && (size_t)length < sizeof(path) && lstat(path, &info) != 0 && errno == ENOENT;
    return gone;
}

/* The campaign's stream kinds by store-path prefix and suffix: 0 untimed
 * records, 1 untimed metrics, then per stage transcripts, metrics, row
 * samples and batch samples (A/A 2..5, A/B 6..9). A/B sample prefixes end in
 * the first index digit. */
#define BQ_PREP_WORKER_UNIT_STREAM_KINDS 10u
BUSTER_GLOBAL_LOCAL char const* const bq_prep_worker_unit_stream_prefixes[BQ_PREP_WORKER_UNIT_STREAM_KINDS] = {
    BQ_RETIREMENT_WORKER_UNTIMED_PATH, "retirement-metrics-untimed-", "retirement-execution-aa-",
    "retirement-metrics-aa-", "retirement-samples-aa-", "retirement-batches-aa-", "retirement-execution-ab-",
    "retirement-metrics-ab-", "retirement-samples-0", "retirement-batches-0"};
BUSTER_GLOBAL_LOCAL char const* const bq_prep_worker_unit_stream_suffixes[BQ_PREP_WORKER_UNIT_STREAM_KINDS] = {
    "", ".txt", ".jsonl", ".txt", ".jsonl", ".jsonl", ".jsonl", ".txt", ".jsonl", ".jsonl"};

BUSTER_GLOBAL_LOCAL u32 bq_prep_worker_unit_stream_kind(char const* name)
{
    u32 kind = BQ_PREP_WORKER_UNIT_STREAM_KINDS;
    for (u32 index = 0; kind == BQ_PREP_WORKER_UNIT_STREAM_KINDS && index < BQ_PREP_WORKER_UNIT_STREAM_KINDS;
         index += 1)
        if (!strncmp(name, bq_prep_worker_unit_stream_prefixes[index], strlen(bq_prep_worker_unit_stream_prefixes[index])))
            kind = index;
    return kind;
}

/* What a finished unit left in its result root (lane E's store root): each
 * workflow document's size (0 when absent), the published untimed records and
 * untimed metrics shards, every entry, the post-sample record's size, the
 * stream kinds published (each entry a stream the attempt staged under the
 * same name with the same size) and how many entries are none of these. */
typedef struct BqPrepWorkerUnitResult
{
    u64 document_bytes[BQ_RETIREMENT_UNIT_CAMPAIGN_DOCUMENTS];
    u64 record_bytes;
    u32 untimed_records, untimed_metrics, entries, published_kinds, unmatched;
    /* (#881 PR 3) Composition's entries: the composer's outputs, the
     * producer's binding and admission receipt, the coordinator's
     * worker-phase-N receipts and the manifest and bundle; outputs has one bit
     * per bq_prep_worker_unit_outputs name present. */
    u32 composed, outputs;
    /* The binding context's evidence files (BQ_RETIREMENT_WORKER_EVIDENCE_PREFIX). */
    u32 evidence;
} BqPrepWorkerUnitResult;

/* The result root's entries composition adds, by exact name, then by prefix. */
#define BQ_PREP_WORKER_UNIT_OUTPUTS 11u
BUSTER_GLOBAL_LOCAL char const* const bq_prep_worker_unit_outputs[BQ_PREP_WORKER_UNIT_OUTPUTS] = {
    TP_RETIREMENT_COMPOSE_CODE_PATH, TP_RETIREMENT_COMPOSE_SERIES_PATH, TP_RETIREMENT_COMPOSE_REPLAY_PATH,
    TP_RETIREMENT_COMPOSE_BUNDLE_PATH, TP_RETIREMENT_EXECUTION_RECEIPT_PATH, TP_RETIREMENT_RETAINED_MANIFEST_PATH,
    BQ_RETIREMENT_WORKER_SEALED_PATH, BQ_RETIREMENT_WORKER_BINDING_PATH, BQ_RETIREMENT_WORKER_ADMISSION_PATH,
    "native-retirement-performance-v1.manifest", "native-retirement-performance-v1.bundle"};
BUSTER_GLOBAL_LOCAL char const* const bq_prep_worker_unit_output_prefixes[] = {TP_RETIREMENT_COMPOSE_SERIES_SHARD_PREFIX,
    "retirement-result-rows-", "retirement-result-batches-", "worker-phase-"};

BUSTER_GLOBAL_LOCAL bool bq_prep_worker_unit_composed_entry(char const* name, u32* outputs)
{
    bool known = false;
    for (u32 index = 0; index < BQ_PREP_WORKER_UNIT_OUTPUTS; index += 1)
        if (!strcmp(name, bq_prep_worker_unit_outputs[index]))
        {
            known = true;
            *outputs |= 1u << index;
        }
    for (u32 index = 0; !known && index < BUSTER_ARRAY_LENGTH(bq_prep_worker_unit_output_prefixes); index += 1)
        known = !strncmp(name, bq_prep_worker_unit_output_prefixes[index],
                         strlen(bq_prep_worker_unit_output_prefixes[index]));
    return known;
}

BUSTER_GLOBAL_LOCAL BqPrepWorkerUnitResult bq_prep_worker_unit_result(char const* result_root, int attempt)
{
    BqPrepWorkerUnitResult result = {0};
    int directory = open(result_root, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    int staged = openat(attempt, BQ_RETIREMENT_WORKER_CAMPAIGN_DIRECTORY "/streams",
                        O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    struct stat record = {0};
    if (directory >= 0 && fstatat(directory, BQ_RETIREMENT_UNIT_CAMPAIGN_RECORD, &record, AT_SYMLINK_NOFOLLOW) == 0)
        result.record_bytes = (u64)record.st_size;
    for (u32 index = 0; directory >= 0 && index < BQ_RETIREMENT_UNIT_CAMPAIGN_DOCUMENTS; index += 1)
    {
        struct stat info = {0};
        if (fstatat(directory, bq_retirement_unit_campaign_document_paths[index], &info, AT_SYMLINK_NOFOLLOW) == 0)
            result.document_bytes[index] = (u64)info.st_size;
    }
    DIR* listing = directory >= 0 ? fdopendir(directory) : NULL;
    if (!listing && directory >= 0) close(directory);
    for (struct dirent* entry = listing ? readdir(listing) : NULL; entry; entry = readdir(listing))
    {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        char expected[TP_RETIREMENT_STORE_PATH_BYTES + 1];
        result.entries += 1;
        result.untimed_records += !strcmp(entry->d_name, BQ_RETIREMENT_WORKER_UNTIMED_PATH);
        /* Published shards are numbered from 0000 with no gap. */
        for (u32 index = 0; index < 16; index += 1)
            if (tp_retirement_metrics_shard_path(expected, TP_RETIREMENT_UNTIMED_METRICS_TAG, index) &&
                !strcmp(entry->d_name, expected))
                result.untimed_metrics |= 1u << index;
        bool known = !strcmp(entry->d_name, BQ_RETIREMENT_UNIT_CAMPAIGN_RECORD);
        for (u32 index = 0; index < BQ_RETIREMENT_UNIT_CAMPAIGN_DOCUMENTS; index += 1)
            known = known || !strcmp(entry->d_name, bq_retirement_unit_campaign_document_paths[index]);
        bool composed = !known && bq_prep_worker_unit_composed_entry(entry->d_name, &result.outputs);
        result.composed += composed;
        known = known || composed;
        bool evidence = !known && !strncmp(entry->d_name, BQ_RETIREMENT_WORKER_EVIDENCE_PREFIX,
                                           strlen(BQ_RETIREMENT_WORKER_EVIDENCE_PREFIX));
        result.evidence += evidence;
        known = known || evidence;
        u32 kind = known ? BQ_PREP_WORKER_UNIT_STREAM_KINDS : bq_prep_worker_unit_stream_kind(entry->d_name);
        struct stat published = {0}, copy = {0};
        bool stream = kind < BQ_PREP_WORKER_UNIT_STREAM_KINDS && staged >= 0 &&
                      fstatat(dirfd(listing), entry->d_name, &published, AT_SYMLINK_NOFOLLOW) == 0 &&
                      fstatat(staged, entry->d_name, &copy, AT_SYMLINK_NOFOLLOW) == 0 &&
                      S_ISREG(published.st_mode) && published.st_size == copy.st_size;
        if (stream) result.published_kinds |= 1u << kind;
        result.unmatched += !known && !stream;
    }
    if (listing) closedir(listing);
    if (staged >= 0) close(staged);
    return result;
}

/* Plays the coordinator for one retirement worker-unit of attempt with an
 * execution deadline `milliseconds` after the handoff. mode TERMINATE sends
 * SIGTERM to the unit once the hanging generate recorded its pid;
 * KILL_PRODUCER then kills the producer (the broker CLI's parent);
 * TERMINATE_MEASURING sends SIGTERM to the unit once the stand-in's A/A
 * marker exists (BQ_RETIREMENT_STAND_IN_AA_MARKER), or at the latest
 * BQ_PREP_WORKER_UNIT_TERMINATE_DELAY_MS after it acknowledged MEASURING.
 * result, when given, receives what the unit left in its result root. */
BUSTER_GLOBAL_LOCAL BqPrepWorkerUnitRun bq_prep_worker_unit_drive(BqPrepOracleFixture* fixture,
    BqPrepUnitAttempt const* attempt, BqRetirementWorkerUnitSeams const* seams, BqPrepWorkerUnitMode mode,
    u32 milliseconds, BqPrepWorkerUnitResult* result)
{
    BqPrepWorkerUnitRun run = {.status = -1};
    char root[] = "/tmp/bq-worker-unit-XXXXXX";
    char lease_path[128], result_root[128], job_text[24], token_text[24];
    BqWorkerLease lease = {.descriptor = -1};
    BqWorkerLeaseHandoff handoff = {.listener = -1, .parent = -1};
    bool made = mkdtemp(root) != NULL;
    int lengths[4] = {snprintf(lease_path, sizeof(lease_path), "%s/host.lock", root),
                      snprintf(result_root, sizeof(result_root), "%s/result", root),
                      snprintf(job_text, sizeof(job_text), "%" PRIu64, (uint64_t)attempt->job.id),
                      snprintf(token_text, sizeof(token_text), "%" PRIu64, (uint64_t)attempt->job.token)};
    bool ok = made && lengths[0] > 0 && (size_t)lengths[0] < sizeof(lease_path) && lengths[1] > 0 &&
              (size_t)lengths[1] < sizeof(result_root) && lengths[2] > 0 && lengths[3] > 0 &&
              mkdir(result_root, 0700) == 0;
    int result_directory = ok ? open(result_root, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    ok = ok && result_directory >= 0 && bq_worker_lease_acquire(lease_path, &lease) == 0 &&
         bq_worker_lease_handoff_open(result_root, result_directory, &handoff);
    int subreaper = 0;
    bool reaping = ok && mode == BQ_PREP_WORKER_UNIT_KILL_PRODUCER && prctl(PR_GET_CHILD_SUBREAPER, &subreaper) == 0 &&
                   prctl(PR_SET_CHILD_SUBREAPER, 1) == 0;
    pid_t unit = ok ? fork() : -1;
    if (unit == 0)
    {
        close(handoff.listener);
        close(handoff.parent);
        close(result_directory);
        close(lease.descriptor);
        u32 before = bq_prep_test_open_descriptors();
        BqError error = bq_worker_unit_pinned(string_from_pointer(lease_path), string_from_pointer(job_text),
            string_from_pointer(token_text), S8("native-retirement-performance-v1"),
            string_from_pointer(fixture->workspaces), bq_field(&attempt->job.request, 3),
            bq_field(&attempt->job.request, 4), string_from_pointer(result_root), seams);
        bool clean = bq_prep_test_live_children() == 0 && bq_prep_test_open_descriptors() == before;
        _exit(clean ? (int)error : BQ_PREP_WORKER_UNIT_UNCLEAN);
    }
    int phase = -1;
    run.started_ms = bq_worker_monotonic_milliseconds();
    u64 deadline = run.started_ms + milliseconds;
    ok = ok && unit > 0 && bq_worker_lease_handoff_send(&handoff, lease.descriptor, lease_path, attempt->job.id,
        attempt->job.token, attempt->digest, deadline, &phase) == BQ_OK;
    /* The coordinator waits past the execution deadline by the unit's stop
     * budget before it gives up on EOF. */
    deadline += 2u * 10000u;
    int status = 0;
    pid_t waited = 0;
    u64 pause = bq_worker_monotonic_milliseconds() + 10000u;
    while (ok && !waited && bq_worker_monotonic_milliseconds() < pause)
    {
        waited = waitpid(unit, &status, WUNTRACED | WNOHANG);
        if (!waited) poll(NULL, 0, 5);
    }
    ok = ok && waited == unit && WIFSTOPPED(status) && kill(unit, SIGCONT) == 0;
    /* The supervisor side: acknowledge every BQPHASE2 message until EOF,
     * keeping the ready digest. */
    bool acted = mode == BQ_PREP_WORKER_UNIT_RUN, open_channel = ok;
    while (open_channel && bq_worker_monotonic_milliseconds() < deadline)
    {
        struct pollfd waiting = {phase, POLLIN, 0};
        if (poll(&waiting, 1, 50) > 0)
        {
            unsigned char message[BQ_PHASE_MESSAGE_CAP + 1] = {0};
            ssize_t count = recv(phase, message, sizeof(message), 0);
            open_channel = count == BQ_PHASE_V2_MESSAGE_BYTES && !memcmp(message, "BQPHASE2", 8);
            run.eof = count == 0;
            u64 sent = open_channel ? bq_phase_get(message + 24) : 0;
            if (open_channel && run.messages < BUSTER_ARRAY_LENGTH(run.phases)) run.phases[run.messages] = (u32)sent;
            run.messages += open_channel;
            run.preparing += sent == BQ_PHASE_PREPARING;
            if (sent == BQ_PHASE_RETIREMENT_READY)
            {
                run.ready += 1;
                bq_phase_digest_format(message + BQ_PHASE_DIGEST_OFFSET, run.ready_sha256);
            }
            bq_phase_put(message + 40, 1u);
            if (open_channel)
                open_channel = send(phase, message, BQ_PHASE_V2_MESSAGE_BYTES, MSG_NOSIGNAL) == BQ_PHASE_V2_MESSAGE_BYTES;
            if (open_channel && sent == BQ_PHASE_MEASURING) run.measuring_ms = bq_worker_monotonic_milliseconds();
        }
        struct stat marker;
        if (mode == BQ_PREP_WORKER_UNIT_TERMINATE_MEASURING && !acted && run.measuring_ms &&
            (fstatat(attempt->attempt, BQ_RETIREMENT_WORKER_CAMPAIGN_DIRECTORY "/work/"
                     BQ_RETIREMENT_STAND_IN_AA_MARKER, &marker, AT_SYMLINK_NOFOLLOW) == 0 ||
             bq_worker_monotonic_milliseconds() >= run.measuring_ms + BQ_PREP_WORKER_UNIT_TERMINATE_DELAY_MS))
            acted = kill(unit, SIGTERM) == 0;
        pid_t driver = acted || mode == BQ_PREP_WORKER_UNIT_TERMINATE_MEASURING ? 0 :
                       bq_prep_worker_unit_driver(fixture, &attempt->job);
        pid_t broker = driver > 0 ? bq_prep_worker_unit_parent(driver) : 0;
        pid_t producer = broker > 0 ? bq_prep_worker_unit_parent(broker) : 0;
        if (producer > 0 && bq_prep_worker_unit_parent(producer) == unit)
        {
            run.driver = driver;
            run.broker = broker;
            acted = kill(mode == BQ_PREP_WORKER_UNIT_TERMINATE ? unit : producer,
                         mode == BQ_PREP_WORKER_UNIT_TERMINATE ? SIGTERM : SIGKILL) == 0;
        }
    }
    /* No EOF by the deadline: the unit is stopped and the run fails. */
    if (unit > 0 && !run.eof) kill(unit, SIGKILL);
    if (unit > 0)
    {
        while (waitpid(unit, &status, 0) < 0 && errno == EINTR) {}
        run.status = acted && WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }
    /* A killed producer's broker and stage are orphans; they were reparented
     * here, so stop and reap them. */
    if (reaping)
    {
        if (run.driver > 0) kill(run.driver, SIGKILL);
        if (run.broker > 0) kill(run.broker, SIGKILL);
        u64 bound = bq_worker_monotonic_milliseconds() + 10000u;
        bool children = true;
        while (children && bq_worker_monotonic_milliseconds() < bound)
        {
            pid_t reaped = waitpid(-1, NULL, WNOHANG);
            children = reaped >= 0 || errno != ECHILD;
            if (reaped == 0) poll(NULL, 0, 5);
        }
        prctl(PR_SET_CHILD_SUBREAPER, subreaper);
    }
    if (phase >= 0) close(phase);
    bq_worker_lease_handoff_close(&handoff);
    bq_worker_lease_release(&lease);
    if (result_directory >= 0) close(result_directory);
    if (result && ok) *result = bq_prep_worker_unit_result(result_root, attempt->attempt);
    if (made) bq_prep_test_cleanup(root);
    return run;
}

/* The one ready-<digest> entry of attempt's sealed retirement-ready/. */
BUSTER_GLOBAL_LOCAL bool bq_prep_worker_unit_ready(int attempt, char digest[SHA256_HEX_CAPACITY])
{
    int directory = openat(attempt, BQ_RETIREMENT_UNIT_READY_DIRECTORY, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    DIR* listing = directory >= 0 ? fdopendir(directory) : NULL;
    if (!listing && directory >= 0) close(directory);
    u32 found = 0;
    bool named = true;
    for (struct dirent* entry = listing ? readdir(listing) : NULL; entry; entry = readdir(listing))
    {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        found += 1;
        named = named && strlen(entry->d_name) == 70 && !strncmp(entry->d_name, "ready-", 6) &&
                bq_retirement_hex(string_from_pointer(entry->d_name + 6), 64);
        if (named) memcpy(digest, entry->d_name + 6, SHA256_HEX_CAPACITY);
    }
    if (listing) closedir(listing);
    return listing && found == 1 && named;
}

/* complete without its status lines, then status (which may be empty). */
BUSTER_GLOBAL_LOCAL bool bq_prep_worker_unit_status(char const* complete, char const* status, char* out,
    size_t capacity)
{
    size_t used = 0;
    bool ok = capacity > 0;
    char const* line = complete;
    while (ok && line && *line)
    {
        char const* end = strchr(line, '\n');
        size_t length = end ? (size_t)(end - line) + 1u : strlen(line);
        if (strncmp(line, "status=", 7))
        {
            ok = used + length < capacity;
            if (ok) memcpy(out + used, line, length);
            used += ok ? length : 0;
        }
        line = end ? end + 1 : NULL;
    }
    size_t tail = strlen(status);
    ok = ok && used + tail < capacity;
    if (ok)
    {
        memcpy(out + used, status, tail + 1u);
    }
    return ok;
}

/* The compiled blocked profile, a blocked status and a missing pin are
 * refused before the lease handoff: no keeper directory, no retirement
 * directory, no child. */
BUSTER_GLOBAL_LOCAL void bq_prep_worker_unit_refused(BqPrepOracleFixture* fixture, BqPrepUnitAttempt const* attempt,
    BqRetirementWorkerUnitSeams const* seams)
{
    char job_text[24], token_text[24], blocked[4096], missing[4096];
    snprintf(job_text, sizeof(job_text), "%" PRIu64, (uint64_t)attempt->job.id);
    snprintf(token_text, sizeof(token_text), "%" PRIu64, (uint64_t)attempt->job.token);
    int length = bq_prep_worker_unit_status(fixture->profile, "status=blocked\n", blocked, sizeof(blocked)) ? 1 : -1;
    char const* pin = strstr(fixture->profile, "row-plan-sha256=");
    char const* end = pin ? strchr(pin, '\n') : NULL;
    int missing_length = end ? snprintf(missing, sizeof(missing), "%.*s%s", (int)(pin - fixture->profile),
                                        fixture->profile, end + 1) : -1;
    BqRetirementWorkerUnitSeams refused[2] = {*seams, *seams};
    refused[0].profile = string_from_pointer(blocked);
    refused[1].profile = string_from_pointer(missing);
    BQ_PREP_CHECK(length > 0 && missing_length > 0 && (size_t)missing_length < sizeof(missing));
    String8 lease = S8("/tmp/bq-worker-unit-absent/host.lock"), result = S8("/tmp/bq-worker-unit-absent/result");
    String8 workspaces = string_from_pointer(fixture->workspaces);
    BQ_PREP_CHECK(bq_worker_unit(lease, string_from_pointer(job_text), string_from_pointer(token_text),
                                 S8("native-retirement-performance-v1"), workspaces, bq_field(&attempt->job.request, 3),
                                 bq_field(&attempt->job.request, 4), result) == BQ_BAD_REQUEST);
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(refused); index += 1)
        BQ_PREP_CHECK(bq_worker_unit_pinned(lease, string_from_pointer(job_text), string_from_pointer(token_text),
                      S8("native-retirement-performance-v1"), workspaces, bq_field(&attempt->job.request, 3),
                      bq_field(&attempt->job.request, 4), result, refused + index) == BQ_BAD_REQUEST);
    struct stat info = {0};
    char keepers[192];
    int keepers_length = snprintf(keepers, sizeof(keepers), "%s/results/.lease-return", fixture->workspaces);
    BQ_PREP_CHECK(keepers_length > 0 && (size_t)keepers_length < sizeof(keepers) &&
                  lstat(keepers, &info) != 0 && errno == ENOENT &&
                  fstatat(attempt->attempt, BQ_RETIREMENT_UNIT_EVIDENCE_DIRECTORY, &info, AT_SYMLINK_NOFOLLOW) != 0 &&
                  errno == ENOENT && bq_prep_test_live_children() == 0);
}

/* The coordinator's backend clock in the finalization checks: mode 0 is
 * always before the 1000 ms test deadline, mode 1 always past it, and mode 2
 * before it on the first reading and past it afterwards. */
BUSTER_GLOBAL_LOCAL u32 bq_prep_worker_unit_clock_mode, bq_prep_worker_unit_clock_calls;

BUSTER_GLOBAL_LOCAL u64 bq_prep_worker_unit_clock(BqWorkerBackend* backend)
{
    (void)backend;
    bq_prep_worker_unit_clock_calls += 1;
    u64 now = bq_prep_worker_unit_clock_mode == 1 || (bq_prep_worker_unit_clock_mode == 2 &&
              bq_prep_worker_unit_clock_calls > 1) ? 5000u : 0u;
    return now;
}

/* A durable queue record of a BQPHASE2 phase carrying digest, as
 * bq_worker_phase_accept writes it. */
BUSTER_GLOBAL_LOCAL bool bq_prep_worker_unit_record(BqQueue* queue, BqJob const* job, unsigned phase,
                                                    char const digest[SHA256_HEX_CAPACITY])
{
    char name[48], record[48], body[512];
    snprintf(name, sizeof(name), "worker-phase-%u", phase);
    int length = snprintf(body, sizeof(body),
        "schema=1\nprotocol=BQPHASE2\njob-id=%" PRIu64 "\nattempt-token=%" PRIu64 "\nrequest-sha256=%s\nphase=%u"
        "\nrecipe-request-monotonic-ns=1\nsupervisor-observed-monotonic-ns=2\ndigest-sha256=%s\n",
        (uint64_t)job->id, (uint64_t)job->token, job->digest, phase, digest);
    bool ok = length > 0 && (size_t)length < sizeof(body) && bq_record_name(record, name, job->id) &&
              bq_record_write(queue, record, (u8 const*)body, (u32)length, false) == BQ_OK;
    return ok;
}

/* #881 PR 4: on the worker-unit's BQPHASE2 channel the in-unit campaign
 * (PR 2) starts SETTLING only after RETIREMENT_READY carried the digest; a
 * begin right after PREPARING is refused without touching the channel. */
BUSTER_GLOBAL_LOCAL void bq_prep_worker_unit_campaign_order(void)
{
    int pair[2] = {-1, -1}, cancel[2] = {-1, -1};
    char root[] = "/tmp/bq-worker-unit-order-XXXXXX";
    bool ok = socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair) == 0 &&
              pipe2(cancel, O_CLOEXEC | O_NONBLOCK) == 0 && mkdtemp(root) != NULL;
    pid_t peer = ok ? fork() : -1;
    if (peer == 0)
    {
        close(pair[0]);
        bool answered = true;
        for (u32 index = 0; answered && index < 3; index += 1)
        {
            unsigned char message[BQ_PHASE_MESSAGE_CAP] = {0};
            answered = recv(pair[1], message, sizeof(message), 0) == BQ_PHASE_V2_MESSAGE_BYTES;
            bq_phase_put(message + 40, 1u);
            answered = answered && send(pair[1], message, BQ_PHASE_V2_MESSAGE_BYTES, MSG_NOSIGNAL) ==
                                   BQ_PHASE_V2_MESSAGE_BYTES;
        }
        close(pair[1]);
        _exit(answered ? 0 : 1);
    }
    if (pair[1] >= 0) close(pair[1]);
    int work = ok ? open(root, O_RDONLY | O_DIRECTORY | O_CLOEXEC) : -1;
    int logs = work >= 3 && mkdirat(work, "logs", 0700) == 0 ?
               openat(work, "logs", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    BqPhaseChannel phases = {.descriptor = -1, .failed = 1};
    u64 deadline = bq_phase_clock() + 60000000000ull;
    ok = ok && peer > 0 && logs >= 3 && bq_phase_init_version(&phases, pair[0], 1, 2, BQ_PHASE_VERSION_2) &&
         bq_phase_exchange(&phases, BQ_PHASE_PREPARING);
    BqRetirementUnitCampaign early = {0}, driver = {0};
    BQ_PREP_CHECK(ok && !bq_retirement_unit_campaign_begin(&early, &phases, cancel[0], deadline, work, logs) &&
                  phases.sequence == BQ_PHASE_PREPARING && !phases.failed);
    BQ_PREP_CHECK(bq_phase_exchange_digest_until(&phases, BQ_PHASE_RETIREMENT_READY,
                      "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef", deadline) &&
                  bq_retirement_unit_campaign_begin(&driver, &phases, cancel[0], deadline, work, logs) &&
                  phases.sequence == BQ_PHASE_SETTLING && driver.step == BQ_RETIREMENT_UNIT_CAMPAIGN_SETTLING);
    int status = 0;
    if (pair[0] >= 0) close(pair[0]);
    if (peer > 0) BQ_PREP_CHECK(waitpid(peer, &status, 0) == peer && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    if (logs >= 0) close(logs);
    if (work >= 0)
    {
        unlinkat(work, "logs", AT_REMOVEDIR);
        close(work);
        rmdir(root);
    }
    for (u32 side = 0; side < 2; side += 1)
        if (cancel[side] >= 0) close(cancel[side]);
}

/* bq_retirement_profile_complete over the fixture's complete profile: every
 * one of its digest pins (the fixture pins exactly the integration keys) is
 * required, and a pin that is not a digest is not a pin. */
BUSTER_GLOBAL_LOCAL void bq_prep_worker_unit_profiles(char const* complete)
{
    char changed[4096];
    BQ_PREP_CHECK(bq_retirement_profile_complete(string_from_pointer(complete)) &&
                  !bq_retirement_profile_complete(bq_recipe_profile(BQ_RECIPE_NATIVE_RETIREMENT_BLOCKED)) &&
                  !bq_retirement_profile_complete((String8){0}));
    u32 pins = 0;
    char const* line = complete;
    while (line && *line)
    {
        char const* end = strchr(line, '\n');
        char const* key = strstr(line, "-sha256=");
        if (end && key && key < end)
        {
            pins += 1;
            int length = snprintf(changed, sizeof(changed), "%.*s%s", (int)(line - complete), complete, end + 1);
            BQ_PREP_CHECK(length > 0 && (size_t)length < sizeof(changed) &&
                          !bq_retirement_profile_complete(string_from_pointer(changed)));
            int prefix = (int)(key - complete) + 8;
            length = snprintf(changed, sizeof(changed), "%.*sX%s", prefix, complete, end);
            BQ_PREP_CHECK(length > 0 && (size_t)length < sizeof(changed) &&
                          !bq_retirement_profile_complete(string_from_pointer(changed)));
        }
        line = end ? end + 1 : NULL;
    }
    BQ_PREP_CHECK(pins == BUSTER_ARRAY_LENGTH(bq_retirement_worker_unit_pins));
    /* Lane D's four frozen campaign values are required too, each canonical
     * and in range. */
    static char const* const values[] = {BQ_RETIREMENT_UNIT_CAMPAIGN_SEED_KEY, BQ_RETIREMENT_UNIT_CAMPAIGN_PAIRS_KEY,
        BQ_RETIREMENT_UNIT_CAMPAIGN_RESAMPLES_KEY, BQ_RETIREMENT_UNIT_CAMPAIGN_BOOTSTRAP_KEY};
    static char const* const invalid[] = {"0", "61", "10", "0"};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(values); index += 1)
    {
        char const* at = strstr(complete, values[index]);
        char const* end = at ? strchr(at, '\n') : NULL;
        int length = end ? snprintf(changed, sizeof(changed), "%.*s%s", (int)(at - complete), complete, end + 1) : -1;
        BQ_PREP_CHECK(length > 0 && (size_t)length < sizeof(changed) &&
                      !bq_retirement_profile_complete(string_from_pointer(changed)));
        length = end ? snprintf(changed, sizeof(changed), "%.*s%s%s%s", (int)(at - complete), complete, values[index],
                                invalid[index], end) : -1;
        BQ_PREP_CHECK(length > 0 && (size_t)length < sizeof(changed) &&
                      !bq_retirement_profile_complete(string_from_pointer(changed)));
    }
    /* Exactly one status line, whose value is exactly the admitting one. */
    char const* statuses[] = {"status=admitted\n", "", "status=blocked\n", "status=blocked-pending\n",
                              "status=admitted \n", "status=admitted\r\n", "status=Admitted\n", "status=\n",
                              "status=admitted\nstatus=admitted\n", "status=admitted\nstatus=blocked\n"};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(statuses); index += 1)
        BQ_PREP_CHECK(bq_prep_worker_unit_status(complete, statuses[index], changed, sizeof(changed)) &&
                      bq_retirement_profile_complete(string_from_pointer(changed)) == (index == 0));
}

/* The worker-unit fixture's row plan: bq_prep_campaign_plan_text's shape
 * (the validator's timed partition; template 0 compiles one row, template 1
 * runs its output; each timed object group its own batch template 2 + g)
 * with this fixture's short timeouts, each object group's metrics leaf
 * b<row>.metrics (the stand-in names r<row>.o from it, so every group must
 * have one member), one more batch template, the untimed contract's
 * (*untimed_template), and last the runtime rows' compile template, whose
 * stand-in mode writes the executable the runtime step runs. text is
 * malloc'd. */
BUSTER_GLOBAL_LOCAL bool bq_prep_worker_unit_plan_text(BqRetirementDocumentPopulation const* population,
    BqRetirementDocumentPartition const* partition, BqRetirementProjection const* projection,
    char const cpu_model[SHA256_HEX_CAPACITY], u32 cpu, TpRetirementCampaignBudget const* budget,
    BqRetirementRowText* text, u32* untimed_template)
{
    BqRetirementPrepared const* prepared = &projection->prepared;
    u32 native = prepared->native_target, timeout = BQ_PREP_WORKER_UNIT_TIMEOUT_SECONDS;
    *text = (BqRetirementRowText){.ok = population->count == prepared->rows};
    *untimed_template = 2u + partition->object_groups;
    bq_retirement_row_text(text, "BQ-RETIREMENT-ROW-PLAN-V1\nsupport=%s\ncensus=%s\npopulation=%s\nnative-target=%u\n"
        "cpu=%s %u\ntemplates=%u\n", prepared->support_sha256, prepared->census_sha256, projection->population_sha256,
        native, cpu_model, cpu, 4u + partition->object_groups);
    bq_retirement_row_text(text, "template=0 compile %u 1024\nargv=7\narg={{binary}}\narg=compile\narg={{source:1}}\n"
        "arg={{fixture}}\narg={{output}}\narg={{metrics}}\narg=--label={{label}}\nenvironment=2\nenv=LC_ALL=C\n"
        "env=PATH=/usr/bin:/bin\n", timeout);
    bq_retirement_row_text(text, "template=1 runtime %u 1024\nargv=1\narg=./{{output}}\nenvironment=0\n", timeout);
    for (u32 batch = 0; text->ok && batch <= partition->object_groups; batch += 1)
    {
        char group[32];
        snprintf(group, sizeof(group), batch < partition->object_groups ? "--batch-group=%u" : "--untimed", batch);
        bq_retirement_row_text(text, "template=%u batch %u 1024\nargv=7\narg={{binary}}\narg=batch\narg=%s\n"
            "arg={{source:1}}\narg=@{{inputs}}\narg={{metrics}}\narg=--label={{label}}\nenvironment=2\nenv=LC_ALL=C\n"
            "env=PATH=/usr/bin:/bin\n", 2u + batch, timeout, group);
    }
    u32 program = 3u + partition->object_groups;
    bq_retirement_row_text(text, "template=%u compile %u 1024\nargv=7\narg={{binary}}\narg=program\narg={{source:1}}\n"
        "arg={{fixture}}\narg={{output}}\narg={{metrics}}\narg=--label={{label}}\nenvironment=2\nenv=LC_ALL=C\n"
        "env=PATH=/usr/bin:/bin\n", program, timeout);
    bq_retirement_row_text(text, "rows=%u\n", prepared->rows);
    for (u32 index = 0; text->ok && index < prepared->rows; index += 1)
    {
        BqRetirementTrustedRow const* row = projection->rows + index;
        String8 fixture = bq_retirement_document_value(population, index, BQ_RETIREMENT_DOCUMENT_FIXTURE);
        bool compile = row->compiler_eligible != 0, runtime = bq_retirement_row_native_runtime(row, native);
        char compiled[16];
        snprintf(compiled, sizeof(compiled), "%u", runtime ? program : 0u);
        text->ok = !compile || fixture.length;
        bq_retirement_row_text(text, "row=%u %s %s %.*s\n", index,
            bq_retirement_row_timed_object(row, native) ? "batch" : compile ? compiled : "-", runtime ? "1" : "-",
            compile ? (int)fixture.length : 1, compile ? (char const*)fixture.pointer : "-");
    }
    bq_retirement_row_text(text, "groups=%u\n", partition->object_groups);
    for (u32 group = 0, batch = 0; text->ok && group < partition->count; group += 1)
    {
        if (!partition->object[group]) continue;
        u32 row = partition->rows[partition->first[group]];
        uint64_t bound = 0;
        text->ok = partition->first[group + 1] - partition->first[group] == 1 &&
                   tp_retirement_budget_metrics_bytes(budget, 1, &bound);
        String8 fixture = bq_retirement_document_value(population, row, BQ_RETIREMENT_DOCUMENT_FIXTURE);
        bq_retirement_row_text(text, "group=%u %u none b%u.metrics %" PRIu64 " 0 1\ninput=%u 1 ok driver.none r%u.o %.*s\n",
                               batch, 2u + batch, row, bound, row, row, (int)fixture.length,
                               (char const*)fixture.pointer);
        batch += 1;
    }
    return text->ok;
}

/* The untimed-command contract over the untimed partition for the plan
 * whose authority is plan_sha256: every untimed object group on the plan's
 * untimed batch template, with the batch target word, allocator none, the
 * metrics leaf u<row>.metrics and the member's object r<row>.o. */
BUSTER_GLOBAL_LOCAL bool bq_prep_worker_unit_untimed_text(BqRetirementDocumentPartition const* untimed,
    char const plan_sha256[SHA256_HEX_CAPACITY], u32 template_index, BqRetirementRowText* text)
{
    *text = (BqRetirementRowText){.ok = true};
    bq_retirement_row_text(text, "BQ-RETIREMENT-UNTIMED-COMMANDS-V1\nrow-plan=%s\ngroups=%u\n", plan_sha256,
                           untimed->object_groups);
    for (u32 group = 0; text->ok && group < untimed->count; group += 1)
    {
        if (!untimed->object[group]) continue;
        u32 row = untimed->rows[untimed->first[group]];
        text->ok = untimed->first[group + 1] - untimed->first[group] == 1;
        bq_retirement_row_text(text, "group=%u %u %s none u%u.metrics 1\ninput=%u r%u.o\n", group, template_index,
                               BQ_PREP_WORKER_UNIT_UNTIMED_TARGET, row, row, row);
    }
    return text->ok;
}

/* Installs (or replaces) recipes/<name> and returns its `key=<digest>` pin. */
BUSTER_GLOBAL_LOCAL bool bq_prep_worker_unit_install(char const* recipes, char const* name, char const* bytes,
    u32 length, char const* key, char pin[128])
{
    char path[256], digest[SHA256_HEX_CAPACITY] = {0};
    int named = snprintf(path, sizeof(path), "%s/%s", recipes, name);
    bool ok = named > 0 && (size_t)named < sizeof(path) && length && chmod(recipes, 0700) == 0 &&
              (unlink(path) == 0 || errno == ENOENT) && bq_prep_test_write_bytes(path, bytes, length) &&
              chmod(recipes, 0500) == 0;
    if (ok) bq_digest(bytes, length, (char8*)digest);
    int written = ok ? snprintf(pin, 128, "%s%s\n", key, digest) : -1;
    return ok && written > 0 && written < 128;
}

/* A stand-in output's artifact digest and code facts, read back through
 * lane D's own artifact reader from a private copy. */
BUSTER_GLOBAL_LOCAL bool bq_prep_worker_unit_output_side(u32 index, TpRetirementCodeSide* side)
{
    char path[] = "/tmp/bq-worker-unit-code-XXXXXX";
    u32 length = 0;
    char* bytes = bq_prep_test_oracle_output(index, &length);
    int file = bytes ? mkstemp(path) : -1;
    *side = (TpRetirementCodeSide){0};
    bool ok = file >= 0 && bq_write_all(file, (u8 const*)bytes, length) && lseek(file, 0, SEEK_SET) == 0 &&
              tp_retirement_code_observe(file, file, side);
    if (file >= 0)
    {
        unlink(path);
        close(file);
    }
    free(bytes);
    return ok;
}

/* The observation an honest producer would make of plan with the stand-in
 * compilers: every compile writes A's candidate snapshot's object (a single
 * compile's .out, a batch member's .o: the same bytes; a runtime row's
 * compile the .program executable), with its real
 * artifact digest and, for a row with a code obligation, the code section
 * lane D's reader parses; diagnostics are empty and runtime outputs the
 * independent oracle's (bq_row_test_observe's shape). */
BUSTER_GLOBAL_LOCAL bool bq_prep_worker_unit_observe(BqRetirementRowPlan const* plan, BqRetirementRowObserved* observed)
{
    TpRetirementCodeSide object = {0}, single = {0}, program = {0};
    bool ok = bq_prep_worker_unit_output_side(2, &object) && bq_prep_worker_unit_output_side(3, &single) &&
              bq_prep_worker_unit_output_side(BQ_PREP_ORACLE_PROGRAM, &program) &&
              !strcmp(object.artifact_sha256, single.artifact_sha256) && bq_row_test_observe(plan, observed);
    char empty[SHA256_HEX_CAPACITY];
    bq_digest("", 0, (char8*)empty);
    for (u32 index = 0; ok && index < plan->row_count; index += 1)
    {
        BqRetirementTrustedRow const* row = plan->completed + index;
        BqRetirementRowPlanRow const* planned = plan->rows + index;
        BqRetirementRowFact* fact = observed->facts + index;
        bool batch = planned->compile == BQ_RETIREMENT_ROW_PLAN_BATCH;
        TpRetirementCodeSide const* output = batch ? &object :
                                             planned->runtime != BQ_RETIREMENT_ROW_PLAN_NONE ? &program : &single;
        for (u32 side = 0; planned->compile != BQ_RETIREMENT_ROW_PLAN_NONE && side < 2; side += 1)
        {
            BqRetirementObservedSide* facts = fact->side + side;
            memcpy(facts->artifact_sha256, output->artifact_sha256, SHA256_HEX_CAPACITY);
            memcpy(facts->diagnostic_sha256, empty, SHA256_HEX_CAPACITY);
            if (row->compiler_eligible && row->code_obligation)
            {
                memcpy(facts->code_sha256, output->code_sha256, SHA256_HEX_CAPACITY);
                facts->code_bytes = output->code_bytes;
            }
            if (batch)
            {
                memcpy(observed->object_sha256[planned->input][side], output->artifact_sha256, SHA256_HEX_CAPACITY);
                memcpy(observed->diagnostic_sha256[planned->input][side], empty, SHA256_HEX_CAPACITY);
            }
        }
        fact->code_eligible = fact->compiler_eligible && row->code_obligation && fact->side[0].code_bytes > 0;
    }
    return ok;
}

/* The fixture's campaign authorities for the reference projection, each
 * installed under recipes/ and pinned: the row plan (row_plan receives its
 * import), the reviewed budget record, the untimed-command contract and
 * lane D's frozen values (seed 7, 60 pairs, 100000 resamples, the derived
 * family's bootstrap count); *timed_groups is the campaign's group count.
 * The pins are appended to profile. */
BUSTER_GLOBAL_LOCAL bool bq_prep_worker_unit_authorities(BqPrepOracleFixture* fixture,
    BqRetirementProjection const* projection, BqJob const* job, BqRetirementRowPlan* row_plan, char* pins,
    size_t capacity, u32* timed_groups)
{
    BqRetirementDocumentPopulation population = {0};
    BqRetirementDocumentPartition timed = {0}, untimed = {0};
    BqRetirementDocumentFamily family = {0};
    TpRetirementCampaignBudget budget = bq_campaign_service_budget();
    char cpu_model[SHA256_HEX_CAPACITY] = {0}, row_pin[128] = {0}, budget_pin[128] = {0}, untimed_pin[128] = {0};
    char budget_text[TP_RETIREMENT_BUDGET_BYTES];
    size_t budget_bytes = tp_retirement_budget_encode(&budget, budget_text, sizeof(budget_text));
    u32 cpu = 0, template_index = 0;
    BqRetirementRowText plan = {0}, contract = {0};
    String8 profile = string_from_pointer(fixture->profile);
    bool ok = budget_bytes && bq_row_test_cpu(&cpu, cpu_model) &&
              bq_retirement_documents_population(fixture->installed_fd, profile, projection->rows,
                  projection->prepared.rows, projection->prepared.native_target, &population) == BQ_OK &&
              bq_retirement_documents_partition(&population, 0, &timed) &&
              bq_retirement_documents_partition(&population, 1, &untimed) &&
              bq_retirement_documents_family(&population, &timed, &family) &&
              bq_prep_worker_unit_plan_text(&population, &timed, projection, cpu_model, cpu, &budget, &plan,
                                            &template_index) &&
              bq_prep_worker_unit_install(fixture->recipes, BQ_RETIREMENT_ROW_PLAN_NAME, plan.bytes, (u32)plan.length,
                                          "row-plan-sha256=", row_pin) &&
              bq_prep_worker_unit_install(fixture->recipes, BQ_RETIREMENT_WORKER_BUDGET_NAME, budget_text,
                                          (u32)budget_bytes, BQ_RETIREMENT_WORKER_BUDGET_PIN, budget_pin);
    size_t used = strlen(pins);
    int length = ok ? snprintf(pins + used, capacity - used, "%s%s", row_pin, budget_pin) : -1;
    ok = ok && length > 0 && (size_t)length < capacity - used;
    used += ok ? (size_t)length : 0;
    char combined[4096];
    length = ok ? snprintf(combined, sizeof(combined), "%s%s", fixture->profile, pins) : -1;
    ok = ok && length > 0 && (size_t)length < sizeof(combined) &&
         bq_retirement_row_plan_import_profile(fixture->installed_fd, string_from_pointer(combined), job, projection,
                                               row_plan) == BQ_OK &&
         bq_prep_worker_unit_untimed_text(&untimed, row_plan->authority_sha256, template_index, &contract) &&
         bq_prep_worker_unit_install(fixture->recipes, BQ_RETIREMENT_WORKER_UNTIMED_NAME, contract.bytes,
                                     (u32)contract.length, BQ_RETIREMENT_WORKER_UNTIMED_PIN, untimed_pin);
    length = ok ? snprintf(pins + used, capacity - used, "%s" BQ_RETIREMENT_UNIT_CAMPAIGN_SEED_KEY "7\n"
                           BQ_RETIREMENT_UNIT_CAMPAIGN_PAIRS_KEY "60\n" BQ_RETIREMENT_UNIT_CAMPAIGN_RESAMPLES_KEY
                           "100000\n" BQ_RETIREMENT_UNIT_CAMPAIGN_BOOTSTRAP_KEY "%u\n", untimed_pin,
                           family.bootstrap_members) : -1;
    ok = ok && length > 0 && (size_t)length < capacity - used;
    *timed_groups = ok ? timed.count : 0;
    free(plan.bytes);
    free(contract.bytes);
    bq_retirement_documents_partition_release(&timed);
    bq_retirement_documents_partition_release(&untimed);
    bq_retirement_documents_population_release(&population);
    return ok;
}

/* The retained campaign failure of attempt (retirement-campaign/
 * campaign-failure.txt): the fields the fixture checks, each from its
 * `key=` line. */
typedef struct BqPrepWorkerUnitFailure
{
    long long result, reason, step, stage, launched, kind, status, exit, cancelled, sequence, log_bytes, after,
        purpose;
    bool present;
} BqPrepWorkerUnitFailure;

BUSTER_GLOBAL_LOCAL BqPrepWorkerUnitFailure bq_prep_worker_unit_failure(int attempt)
{
    BqPrepWorkerUnitFailure failure = {0};
    char text[BQ_RETIREMENT_WORKER_FAILURE_BYTES + 1] = {0};
    int directory = openat(attempt, BQ_RETIREMENT_WORKER_CAMPAIGN_DIRECTORY, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    u32 length = directory >= 0 ? bq_prep_test_read_at(directory, BQ_RETIREMENT_WORKER_FAILURE_NAME, text,
                                                       BQ_RETIREMENT_WORKER_FAILURE_BYTES) : 0;
    if (directory >= 0) close(directory);
    static char const* const keys[] = {"\nresult=", "\nreason=", "\nstep=", "\nstage=", "\nlaunched=", "\nkind=",
                                       "\nstatus=", "\nexit=", "\ncancelled=", "\nsequence=", "\nlog-bytes=",
                                       "\nafter=", "\npurpose="};
    long long* values[] = {&failure.result, &failure.reason, &failure.step, &failure.stage, &failure.launched,
                           &failure.kind, &failure.status, &failure.exit, &failure.cancelled, &failure.sequence,
                           &failure.log_bytes, &failure.after, &failure.purpose};
    failure.present = length && !strncmp(text, BQ_RETIREMENT_WORKER_FAILURE_HEADER,
                                         strlen(BQ_RETIREMENT_WORKER_FAILURE_HEADER));
    for (u32 index = 0; failure.present && index < BUSTER_ARRAY_LENGTH(keys); index += 1)
    {
        char const* at = strstr(text, keys[index]);
        failure.present = at != NULL;
        if (at) *values[index] = strtoll(at + strlen(keys[index]), NULL, 10);
    }
    return failure;
}

/* The staged streams of attempt: each kind's files
 * (bq_prep_worker_unit_stream_prefixes) numbered contiguously from 0000
 * (zero-padded, so their names sort in index order), counts[kind] receiving
 * how many. */
BUSTER_GLOBAL_LOCAL bool bq_prep_worker_unit_streams(int attempt, u32 counts[BQ_PREP_WORKER_UNIT_STREAM_KINDS])
{
    char const* const* prefixes = bq_prep_worker_unit_stream_prefixes;
    char const* const* suffixes = bq_prep_worker_unit_stream_suffixes;
    u64 seen[BQ_PREP_WORKER_UNIT_STREAM_KINDS] = {0};
    memset(counts, 0, BQ_PREP_WORKER_UNIT_STREAM_KINDS * sizeof(*counts));
    char path[64];
    int length = snprintf(path, sizeof(path), BQ_RETIREMENT_WORKER_CAMPAIGN_DIRECTORY "/streams");
    int directory = length > 0 ? openat(attempt, path, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    DIR* listing = directory >= 0 ? fdopendir(directory) : NULL;
    if (!listing && directory >= 0) close(directory);
    bool ok = listing != NULL;
    for (struct dirent* entry = listing ? readdir(listing) : NULL; ok && entry; entry = readdir(listing))
    {
        /* The post-sample record is staged beside the streams. */
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..") ||
            !strcmp(entry->d_name, BQ_RETIREMENT_UNIT_CAMPAIGN_RECORD))
            continue;
        u32 kind = bq_prep_worker_unit_stream_kind(entry->d_name);
        size_t name = strlen(entry->d_name);
        size_t prefix = kind < BQ_PREP_WORKER_UNIT_STREAM_KINDS ? strlen(prefixes[kind]) : 0;
        /* A/B sample prefixes end in the first index digit. */
        size_t digits = kind >= 8 && kind < BQ_PREP_WORKER_UNIT_STREAM_KINDS ? prefix - 1u : prefix;
        char const* suffix = kind < BQ_PREP_WORKER_UNIT_STREAM_KINDS ? suffixes[kind] : "";
        ok = kind < BQ_PREP_WORKER_UNIT_STREAM_KINDS &&
             (kind == 0 ? name == prefix :
              name == digits + 4u + strlen(suffix) && !strcmp(entry->d_name + digits + 4u, suffix));
        u32 index = 0;
        for (u32 digit = 0; ok && kind && digit < 4; digit += 1)
        {
            char c = entry->d_name[digits + digit];
            ok = c >= '0' && c <= '9';
            index = index * 10u + (u32)(c - '0');
        }
        ok = ok && index < 64 && !(seen[kind] & (UINT64_C(1) << index));
        if (ok) seen[kind] |= UINT64_C(1) << index;
    }
    if (listing) closedir(listing);
    for (u32 kind = 0; ok && kind < BQ_PREP_WORKER_UNIT_STREAM_KINDS; kind += 1)
    {
        while (counts[kind] < 64 && (seen[kind] & (UINT64_C(1) << counts[kind]))) counts[kind] += 1;
        ok = counts[kind] < 64 && seen[kind] == (counts[kind] ? (UINT64_C(1) << counts[kind]) - 1u : 0);
    }
    return ok;
}

/* A retirement run through the campaign: the unit exited `status` after the
 * first `phases` BQPHASE2 phases in order (PREPARING, RETIREMENT_READY,
 * SETTLING, MEASURING) and EOF, with its keeper stopped; RETIREMENT_READY
 * carried exactly the published record's digest, which the coordinator replay
 * accepts; the campaign kept its failure. */
BUSTER_GLOBAL_LOCAL void bq_prep_worker_unit_campaign_checks(BqPrepOracleFixture* fixture,
    BqPrepUnitAttempt const* attempt, BqRetirementWorkerUnitSeams const* seams, BqPrepWorkerUnitRun const* run,
    int status, u32 phases, BqPrepWorkerUnitFailure* failure)
{
    static u32 const order[] = {BQ_PHASE_PREPARING, BQ_PHASE_RETIREMENT_READY, BQ_PHASE_SETTLING, BQ_PHASE_MEASURING};
    char digest[SHA256_HEX_CAPACITY] = {0};
    bool ordered = run->messages == phases && phases <= BUSTER_ARRAY_LENGTH(order) && run->preparing == 1 &&
                   run->ready == 1;
    for (u32 index = 0; ordered && index < phases; index += 1) ordered = run->phases[index] == order[index];
    if (run->status != status || !ordered)
        fprintf(stderr, "RETIREMENT_PREP worker-unit job %" PRIu64 " exited %d after %u messages\n",
                (uint64_t)attempt->job.id, run->status, run->messages);
    BQ_PREP_CHECK(run->status == status && ordered && run->eof && bq_prep_worker_unit_keeper_gone(fixture, &attempt->job));
    BQ_PREP_CHECK(bq_prep_worker_unit_ready(attempt->attempt, digest) && !strcmp(digest, run->ready_sha256) &&
                  bq_retirement_unit_replay_pinned(attempt->store, fixture->workspaces_fd, fixture->installed_fd,
                      attempt->job.id, attempt->job.token, string_from_pointer(fixture->workspaces), seams->profile,
                      S8("self-test"), fixture->driver, fixture->toolchain_root, fixture->broker,
                      fixture->workspaces, attempt->digest, digest) == BQ_OK);
    *failure = bq_prep_worker_unit_failure(attempt->attempt);
    BQ_PREP_CHECK(status == BQ_OK ? !failure->present :
                  failure->present && failure->result == status);
}

/* How many runtime step directories (`runtime-*`) attempt's campaign work
 * directory still holds, or UINT32_MAX when it cannot be listed. */
BUSTER_GLOBAL_LOCAL u32 bq_prep_worker_unit_steps_left(int attempt)
{
    int directory = openat(attempt, BQ_RETIREMENT_WORKER_CAMPAIGN_DIRECTORY "/work",
                           O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    DIR* listing = directory >= 0 ? fdopendir(directory) : NULL;
    if (!listing && directory >= 0) close(directory);
    u32 left = listing ? 0 : UINT32_MAX;
    for (struct dirent* entry = listing ? readdir(listing) : NULL; entry; entry = readdir(listing))
        left += !strncmp(entry->d_name, "runtime-", 8);
    if (listing) closedir(listing);
    return left;
}

/* The staged post-sample record of attempt: its header, the A/A admission,
 * post-A/A binding and post-sample digests present, and the untimed, A/A and
 * A/B launch counts. */
BUSTER_GLOBAL_LOCAL bool bq_prep_worker_unit_post_sample(int attempt, u64 launches[3])
{
    char text[BQ_RETIREMENT_UNIT_CAMPAIGN_RECORD_BYTES_MAX + 1] = {0};
    int directory = openat(attempt, BQ_RETIREMENT_WORKER_CAMPAIGN_DIRECTORY "/streams",
                           O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    u32 length = directory >= 0 ? bq_prep_test_read_at(directory, BQ_RETIREMENT_UNIT_CAMPAIGN_RECORD, text,
                                                       BQ_RETIREMENT_UNIT_CAMPAIGN_RECORD_BYTES_MAX) : 0;
    if (directory >= 0) close(directory);
    bool ok = length && !strncmp(text, BQ_RETIREMENT_UNIT_CAMPAIGN_RECORD_HEADER,
                                 strlen(BQ_RETIREMENT_UNIT_CAMPAIGN_RECORD_HEADER));
    static char const* const digests[] = {"\naa-admission=", "\npost-aa=", "\npost-aa-binding=", "\npost-sample=",
                                          "\nlog-aa=", "\nlog-ab="};
    for (u32 index = 0; ok && index < BUSTER_ARRAY_LENGTH(digests); index += 1)
    {
        char const* at = strstr(text, digests[index]);
        char const* value = at ? at + strlen(digests[index]) : NULL;
        ok = value && strlen(value) > 64 && value[64] == '\n';
        for (u32 digit = 0; ok && digit < 64; digit += 1)
            ok = (value[digit] >= '0' && value[digit] <= '9') || (value[digit] >= 'a' && value[digit] <= 'f');
    }
    char const* counts = ok ? strstr(text, "\nlaunches=") : NULL;
    unsigned long long values[3] = {0};
    ok = counts && sscanf(counts, "\nlaunches=%llu,%llu,%llu\n", &values[0], &values[1], &values[2]) == 3;
    for (u32 index = 0; index < 3; index += 1) launches[index] = ok ? values[index] : 0;
    return ok;
}

/* The document sizes the campaign retained before timing
 * (BQ_RETIREMENT_WORKER_SIZED_NAME) are exactly `written`, all nonzero. */
BUSTER_GLOBAL_LOCAL bool bq_prep_worker_unit_sized(int attempt, u64 const written[BQ_RETIREMENT_UNIT_CAMPAIGN_DOCUMENTS])
{
    char text[1024] = {0}, expected[1024];
    int directory = openat(attempt, BQ_RETIREMENT_WORKER_CAMPAIGN_DIRECTORY, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    u32 length = directory >= 0 ? bq_prep_test_read_at(directory, BQ_RETIREMENT_WORKER_SIZED_NAME, text,
                                                       sizeof(text) - 1u) : 0;
    if (directory >= 0) close(directory);
    int used = snprintf(expected, sizeof(expected), "%s", BQ_RETIREMENT_WORKER_SIZED_HEADER);
    bool ok = length > 0;
    for (u32 index = 0; ok && index < BQ_RETIREMENT_UNIT_CAMPAIGN_DOCUMENTS; index += 1)
    {
        int line = snprintf(expected + used, sizeof(expected) - (size_t)used, "%s %" PRIu64 "\n",
                            bq_retirement_unit_campaign_document_paths[index], (uint64_t)written[index]);
        ok = written[index] && line > 0 && (size_t)line < sizeof(expected) - (size_t)used;
        used += ok ? line : 0;
    }
    ok = ok && (size_t)used == length && !memcmp(text, expected, length);
    return ok;
}

/* ---------------------------------------------------- #881 PR 3: composition */

/* This runner's own path, the fixture's composer adapter (its
 * `retirement-replay` mode). */
BUSTER_GLOBAL_LOCAL char bq_prep_worker_unit_self[4096];

/* A whole small file into a new malloc'd buffer (NUL-terminated). */
BUSTER_GLOBAL_LOCAL char* bq_prep_worker_unit_slurp(int directory, char const* name, u32* length)
{
    int file = openat(directory, name, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    struct stat info = {0};
    bool ok = file >= 0 && fstat(file, &info) == 0 && S_ISREG(info.st_mode) && info.st_size > 0 &&
              info.st_size < (1 << 26);
    char* bytes = ok ? malloc((size_t)info.st_size + 1u) : NULL;
    u32 used = 0;
    ok = ok && bytes && bq_read_file(file, (u8*)bytes, (u32)info.st_size, &used) && used == (u32)info.st_size;
    if (file >= 0) close(file);
    if (ok) bytes[used] = 0;
    else
    {
        free(bytes);
        bytes = NULL;
    }
    *length = ok ? used : 0;
    return bytes;
}

/* The composition's authorities for the reference projection, pinned and
 * appended to pins: the composer adapter (this runner) and the binding
 * context the Python fixture emits over the installed census with the
 * reference's held binaries (job 81's frozen trusted-build outputs, whose
 * bytes every producer attempt holds), the profile's contract pin, lane D's
 * frozen values and the campaign CPU (retirement_binding_context_fixture.py),
 * installed under recipes/ beside the evidence directory it writes
 * (BQ_RETIREMENT_WORKER_EVIDENCE_NAME: every file the context names except
 * the two binaries, which the producer copies from its held descriptors). */
BUSTER_GLOBAL_LOCAL bool bq_prep_worker_unit_composition(BqPrepOracleFixture* fixture,
    BqRetirementProjection const* projection, BqJob const* reference, char* pins, size_t capacity)
{
    char adapter[SHA256_HEX_CAPACITY] = {0}, contract[SHA256_HEX_CAPACITY] = {0}, output[256], pin[128] = {0};
    char evidence[256], binaries[2][256], workspace[64] = {0}, cpu_text[16], cpu_model[SHA256_HEX_CAPACITY] = {0};
    u32 cpu = 0;
    ssize_t self = readlink("/proc/self/exe", bq_prep_worker_unit_self, sizeof(bq_prep_worker_unit_self) - 1u);
    bool ok = self > 0 && (size_t)self < sizeof(bq_prep_worker_unit_self) - 1u &&
              bq_workspace_name(workspace, reference->id, reference->token) && bq_row_test_cpu(&cpu, cpu_model);
    if (ok) bq_prep_worker_unit_self[self] = 0;
    int named[5] = {snprintf(output, sizeof(output), "%s/binding-context.fixture", fixture->workspaces),
                    snprintf(evidence, sizeof(evidence), "%s/" BQ_RETIREMENT_WORKER_EVIDENCE_NAME, fixture->recipes),
                    snprintf(binaries[0], sizeof(binaries[0]), "%s/%s/" BQ_RETIREMENT_BUILD_WORK_DIRECTORY
                             "/trusted-build/base-ide", fixture->workspaces, ok ? workspace : ""),
                    snprintf(binaries[1], sizeof(binaries[1]), "%s/%s/" BQ_RETIREMENT_BUILD_WORK_DIRECTORY
                             "/trusted-build/candidate-ide", fixture->workspaces, ok ? workspace : ""),
                    snprintf(cpu_text, sizeof(cpu_text), "%u", cpu)};
    ok = ok && named[0] > 0 && (size_t)named[0] < sizeof(output) && named[1] > 0 &&
         (size_t)named[1] < sizeof(evidence) &&
         named[2] > 0 && (size_t)named[2] < sizeof(binaries[0]) && named[3] > 0 &&
         (size_t)named[3] < sizeof(binaries[1]) && named[4] > 0 && (size_t)named[4] < sizeof(cpu_text) &&
         bq_prep_test_file_sha(bq_prep_worker_unit_self, adapter) &&
         bq_retirement_profile_sha(string_from_pointer(fixture->profile), S8("contract-sha256="), contract);
    char* emit[] = {"python3", "-W", "error", "tools/bench_service/retirement_binding_context_fixture.py",
                    fixture->census, output, evidence, binaries[0], binaries[1],
                    (char*)projection->prepared.binary_sha256[0], (char*)projection->prepared.binary_sha256[1],
                    contract, "7", "60", "100000", cpu_text, NULL};
    ok = ok && chmod(fixture->recipes, 0700) == 0 && bq_prep_test_run(emit);
    ok = chmod(fixture->recipes, 0500) == 0 && ok;
    int workspaces = ok ? open(fixture->workspaces, O_RDONLY | O_DIRECTORY | O_CLOEXEC) : -1;
    u32 length = 0;
    char* bytes = workspaces >= 0 ? bq_prep_worker_unit_slurp(workspaces, "binding-context.fixture", &length) : NULL;
    if (workspaces >= 0) close(workspaces);
    ok = ok && bytes && unlink(output) == 0 &&
         bq_prep_worker_unit_install(fixture->recipes, BQ_RETIREMENT_WORKER_BINDING_CONTEXT_NAME, bytes, length,
                                     BQ_RETIREMENT_WORKER_BINDING_CONTEXT_PIN, pin);
    free(bytes);
    size_t used = strlen(pins);
    int appended = ok ? snprintf(pins + used, capacity - used, "adapter-sha256=%s\n%s", adapter, pin) : -1;
    ok = ok && appended > 0 && (size_t)appended < capacity - used;
    return ok;
}

/* text as a JSON string into out (the checks' scripts are printable ASCII,
 * so only quotes and backslashes are escaped); its length, or 0. */
BUSTER_GLOBAL_LOCAL u32 bq_prep_worker_unit_json_text(char* out, u32 capacity, char const* text)
{
    u32 used = 0;
    bool ok = capacity > 2u;
    if (ok) out[used++] = '"';
    for (size_t index = 0; ok && text[index]; index += 1)
    {
        ok = used + 3u < capacity && text[index] >= 0x20 && text[index] <= 0x7e;
        if (ok && (text[index] == '"' || text[index] == '\\')) out[used++] = '\\';
        if (ok) out[used++] = text[index];
    }
    ok = ok && used + 2u < capacity;
    if (ok)
    {
        out[used++] = '"';
        out[used] = 0;
    }
    return ok ? used : 0;
}

/* The reviewed check plan (retirement_required_checks.py's PLAN) that
 * describes exactly the authority bq_check_test_authority composes for these
 * specs over projection, naming commit as A's candidate. Its length, or 0. */
BUSTER_GLOBAL_LOCAL u32 bq_prep_worker_unit_checks_plan(char* text, u32 capacity,
    BqRetirementProjection const* projection, char const* commit, char const* tree, BqCheckTestSpec const* specs,
    u32 count)
{
    BqRetirementPrepared const* prepared = &projection->prepared;
    u32 eligible = 0;
    for (u32 row = 0; row < prepared->rows; row += 1) eligible += projection->rows[row].compiler_eligible != 0;
    int used = snprintf(text, capacity, "{\"schema\":\"bq-retirement-required-checks-plan-v1\",\"projection\":{"
        "\"support_sha256\":\"%s\",\"census_sha256\":\"%s\",\"population_sha256\":\"%s\",\"native_target\":%u,"
        "\"rows\":%u,\"object_rows\":%u,\"eligible_rows\":%u},\"candidate\":{\"commit\":\"%s\",\"tree\":\"%s\"},"
        "\"tools\":[\"sh\"],\"checks\":[", prepared->support_sha256, prepared->census_sha256,
        projection->population_sha256, prepared->native_target, prepared->rows, prepared->object_rows, eligible,
        commit, tree);
    bool ok = used > 0 && (u32)used < capacity;
    for (u32 index = 0; ok && index < count; index += 1)
    {
        BqCheckTestSpec const* spec = specs + index;
        char script[1024], output[64], output_sha256[SHA256_HEX_CAPACITY];
        int output_length = snprintf(output, sizeof(output), "check-%u ok\n", index);
        bq_digest(output, (u32)output_length, (char8*)output_sha256);
        int line = snprintf(text + used, capacity - (u32)used, "%s{\"kind\":\"%s\",\"target\":%u,\"rows\":%u,"
                            "\"evidence\":\"%s\",\"timeout_seconds\":%u,\"memory_mib\":1024,"
                            "\"configuration\":\"fixture %s %u\",", index ? "," : "", spec->kind, spec->target,
                            spec->rows, spec->evidence, spec->timeout, spec->kind, spec->target);
        ok = line > 0 && (u32)line < capacity - (u32)used;
        used += ok ? line : 0;
        ok = ok && (!spec->script || bq_prep_worker_unit_json_text(script, sizeof(script), spec->script));
        line = !ok ? -1 : !spec->script ? snprintf(text + used, capacity - (u32)used, "\"argv\":[],\"environment\":[]}") :
               snprintf(text + used, capacity - (u32)used, "\"stdout_sha256\":\"%s\",\"argv\":[\"{{tool:0}}\","
                        "\"-c\",%s,\"check-%u\",\"{{binary:%u}}\",\"{{source:0}}\",\"{{binary:%u}}\",\"{{source:1}}\","
                        "\"{{tool:0}}\"],\"environment\":[\"LC_ALL=C\",\"PATH=/usr/bin:/bin\",\"WORK={{work}}\"]}",
                        output_sha256, script, index, spec->binary, 1u - spec->binary);
        ok = line > 0 && (u32)line < capacity - (u32)used;
        used += ok ? line : 0;
    }
    ok = ok && (u32)used + 3u < capacity;
    if (ok) used += snprintf(text + used, capacity - (u32)used, "]}\n");
    return ok ? (u32)used : 0;
}

/* Runs argv with its standard error in the new file `path` (removed
 * afterwards): whether it exited 1 and its message holds `reason`. */
BUSTER_GLOBAL_LOCAL bool bq_prep_worker_unit_refused_with(char* const argv[], char const* path, char const* reason)
{
    int file = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    pid_t child = file >= 0 ? fork() : -1;
    if (child == 0)
    {
        if (dup2(file, 2) == 2) execvp(argv[0], argv);
        _exit(127);
    }
    int status = 0;
    pid_t waited = -1;
    do { if (child > 0) waited = waitpid(child, &status, 0); }
    while (waited < 0 && errno == EINTR);
    char text[4096] = {0};
    bool ok = child > 0 && waited == child && WIFEXITED(status) && WEXITSTATUS(status) == 1 &&
              bq_prep_test_read_text(path, text, sizeof(text)) && strstr(text, reason);
    if (file >= 0)
    {
        close(file);
        unlink(path);
    }
    return ok;
}

/* The production required-check generator (retirement_required_checks.py)
 * over the reference projection (#1020 step 9): a plan of the specs
 * bq_check_test_install pinned, the installed shell tool and the hosted
 * acceptance record. Its bytes must be exactly the installed authority,
 * which the importer accepts for the reference attempt under their own pin;
 * a plan naming another candidate commit than the record's is refused by the
 * generator for exactly that reason. */
BUSTER_GLOBAL_LOCAL void bq_prep_worker_unit_generated_checks(BqPrepOracleFixture* fixture,
    BqPrepOracleAttempt* reference, BqCheckTestSpec const* specs, u32 count)
{
    BqRetirementProjection const* projection = &reference->projection;
    BqRetirementPreparation const* preparation = &reference->attempt.unit.preparation;
    char plan_path[256], output[256], tools[256], record[256], refusal[256];
    /* A revision is 40 or 64 hex digits. */
    char other[72];
    int lengths[5] = {snprintf(plan_path, sizeof(plan_path), "%s/required-checks.plan", fixture->workspaces),
                      snprintf(refusal, sizeof(refusal), "%s/required-checks.refusal", fixture->workspaces),
                      snprintf(output, sizeof(output), "%s/required-checks.generated", fixture->workspaces),
                      snprintf(tools, sizeof(tools), "%s/" BQ_RETIREMENT_CHECK_TOOLS_DIRECTORY, fixture->recipes),
                      snprintf(record, sizeof(record), "%s/" BQ_RETIREMENT_HOSTED_ACCEPTANCE_NAME, fixture->recipes)};
    bool ok = true;
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(lengths); index += 1)
        ok = ok && lengths[index] > 0 && (size_t)lengths[index] < sizeof(plan_path);
    char* plan = malloc(BQ_CHECK_TEST_AUTHORITY_CAP);
    char* emit[] = {"python3", "-W", "error", "tools/bench_service/retirement_required_checks.py", "--plan", plan_path,
                    "--checks-dir", tools, "--hosted-record", record, "--output", output, NULL};
    /* Refused first: another candidate commit than the record names. */
    snprintf(other, sizeof(other), "%s", preparation->subjects[1].commit);
    other[0] = other[0] == 'a' ? 'b' : 'a';
    u32 length = ok && plan ? bq_prep_worker_unit_checks_plan(plan, BQ_CHECK_TEST_AUTHORITY_CAP, projection, other,
                                                              preparation->subjects[1].tree, specs, count) : 0;
    BQ_PREP_CHECK(length && bq_prep_test_write_bytes(plan_path, plan, length) &&
                  bq_prep_worker_unit_refused_with(emit, refusal,
                      "does not open with its header and the candidate commit and tree") &&
                  access(output, F_OK) != 0 && unlink(plan_path) == 0);
    length = ok && plan ? bq_prep_worker_unit_checks_plan(plan, BQ_CHECK_TEST_AUTHORITY_CAP, projection,
                                                          preparation->subjects[1].commit,
                                                          preparation->subjects[1].tree, specs, count) : 0;
    ok = length && bq_prep_test_write_bytes(plan_path, plan, length) && bq_prep_test_run(emit);
    BQ_PREP_CHECK(ok);
    int workspaces = open(fixture->workspaces, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    int recipes = open(fixture->recipes, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    u32 generated_length = 0, installed_length = 0;
    char* generated = ok && workspaces >= 0 ? bq_prep_worker_unit_slurp(workspaces, "required-checks.generated",
                                                                        &generated_length) : NULL;
    char* installed = ok && recipes >= 0 ? bq_prep_worker_unit_slurp(recipes, BQ_RETIREMENT_REQUIRED_CHECKS_NAME,
                                                                     &installed_length) : NULL;
    BQ_PREP_CHECK(generated && installed && generated_length == installed_length &&
                  !memcmp(generated, installed, installed_length));
    char digest[SHA256_HEX_CAPACITY] = {0}, profile[128];
    if (generated) bq_digest(generated, generated_length, (char8*)digest);
    snprintf(profile, sizeof(profile), "required-checks-sha256=%s\n", digest);
    BqRetirementRequiredChecks checks = {.hosted = -1};
    BQ_PREP_CHECK(generated && bq_retirement_required_checks_import_profile(fixture->installed_fd,
                      string_from_pointer(profile), &reference->attempt.unit.job, preparation, projection, &checks) ==
                      BQ_OK && checks.count == count && checks.tool_count == 1);
    if (checks.owned) BQ_PREP_CHECK(bq_retirement_required_checks_release(&checks));
    if (workspaces >= 0) close(workspaces);
    if (recipes >= 0) close(recipes);
    BQ_PREP_CHECK(unlink(plan_path) == 0 && (!generated || unlink(output) == 0));
    free(generated);
    free(installed);
    free(plan);
}

/* One retirement worker-unit driven by the real coordinator path (#881 PR 4,
 * now reaching MEASURED): the attempt is the queue's active preparing job,
 * the lease and phase channel are handed to a forked worker-unit, and
 * bq_worker_phase_join acknowledges every BQPHASE2 phase, keeps the ready
 * digest and hands the authority off before MEASURED is acknowledged. The
 * result root, its finalization state and the queue state before the
 * attempt are kept for the checks (bq_prep_worker_unit_composed_close). */
typedef struct BqPrepWorkerUnitComposed
{
    char root[32], result_root[128];
    int result_directory;
    BqWorkerConfig config;
    BqWorkerFinalization finalization;
    BqPhaseChannel phases;
    BqState* saved;
    BqError joined;
    int status;
    bool made;
} BqPrepWorkerUnitComposed;

BUSTER_GLOBAL_LOCAL void bq_prep_worker_unit_coordinate(BqPrepOracleFixture* fixture, BqPrepUnitAttempt const* attempt,
    BqRetirementWorkerUnitSeams const* seams, u32 milliseconds, BqPrepWorkerUnitComposed* composed)
{
    *composed = (BqPrepWorkerUnitComposed){.result_directory = -1, .joined = BQ_IO, .status = -1,
                                           .phases = {.descriptor = -1, .failed = 1}};
    snprintf(composed->root, sizeof(composed->root), "/tmp/bq-worker-unit-XXXXXX");
    char lease_path[128], job_text[24], token_text[24];
    BqWorkerLease lease = {.descriptor = -1};
    BqWorkerLeaseHandoff handoff = {.listener = -1, .parent = -1};
    BqQueue* queue = &fixture->queue;
    composed->made = mkdtemp(composed->root) != NULL;
    int lengths[4] = {snprintf(lease_path, sizeof(lease_path), "%s/host.lock", composed->root),
                      snprintf(composed->result_root, sizeof(composed->result_root), "%s/result", composed->root),
                      snprintf(job_text, sizeof(job_text), "%" PRIu64, (uint64_t)attempt->job.id),
                      snprintf(token_text, sizeof(token_text), "%" PRIu64, (uint64_t)attempt->job.token)};
    bool ok = composed->made && lengths[0] > 0 && (size_t)lengths[0] < sizeof(lease_path) && lengths[1] > 0 &&
              (size_t)lengths[1] < sizeof(composed->result_root) && lengths[2] > 0 && lengths[3] > 0 &&
              mkdir(composed->result_root, 0700) == 0;
    composed->result_directory = ok ? open(composed->result_root, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    struct stat info = {0};
    ok = ok && composed->result_directory >= 0 && fstat(composed->result_directory, &info) == 0 &&
         bq_worker_lease_acquire(lease_path, &lease) == 0 &&
         bq_worker_lease_handoff_open(composed->result_root, composed->result_directory, &handoff);
    /* The queue holds the attempt as its active, preparing job. */
    composed->saved = ok ? malloc(sizeof(*composed->saved)) : NULL;
    ok = ok && composed->saved && queue->state.job_count < BQ_JOB_CAP;
    if (ok)
    {
        memcpy(composed->saved, &queue->state, sizeof(queue->state));
        BqJob* job = queue->state.jobs + queue->state.job_count++;
        *job = attempt->job;
        job->phase = BQ_PREPARING;
        job->outcome = BQ_NO_OUTCOME;
        job->cancel_requested = false;
        job->result_bound = false;
        queue->state.active_id = job->id;
    }
    composed->config = (BqWorkerConfig){.workspace_root = string_from_pointer(fixture->workspaces)};
    BqWorkerFinalization* finalization = &composed->finalization;
    *finalization = (BqWorkerFinalization){.config = &composed->config, .result_directory = composed->result_directory,
        .result_device = info.st_dev, .result_inode = info.st_ino, .phase_version = BQ_PHASE_VERSION_2,
        .retirement = seams};
    snprintf(finalization->result_root, sizeof(finalization->result_root), "%s", composed->result_root);
    memcpy(finalization->retirement_preparation_sha256, attempt->digest, SHA256_HEX_CAPACITY);
    ok = ok && bq_recipe_files(BQ_RECIPE_NATIVE_RETIREMENT_BLOCKED, &finalization->recipe);
    pid_t unit = ok ? fork() : -1;
    if (unit == 0)
    {
        close(handoff.listener);
        close(handoff.parent);
        close(composed->result_directory);
        close(lease.descriptor);
        u32 before = bq_prep_test_open_descriptors();
        BqError error = bq_worker_unit_pinned(string_from_pointer(lease_path), string_from_pointer(job_text),
            string_from_pointer(token_text), S8("native-retirement-performance-v1"),
            string_from_pointer(fixture->workspaces), bq_field(&attempt->job.request, 3),
            bq_field(&attempt->job.request, 4), string_from_pointer(composed->result_root), seams);
        bool clean = bq_prep_test_live_children() == 0 && bq_prep_test_open_descriptors() == before;
        _exit(clean ? (int)error : BQ_PREP_WORKER_UNIT_UNCLEAN);
    }
    int phase = -1;
    u64 deadline = bq_worker_monotonic_milliseconds() + milliseconds;
    finalization->execution_deadline = deadline;
    ok = ok && unit > 0 && bq_worker_lease_handoff_send(&handoff, lease.descriptor, lease_path, attempt->job.id,
        attempt->job.token, attempt->digest, deadline, &phase) == BQ_OK;
    int status = 0;
    pid_t waited = 0;
    u64 pause = bq_worker_monotonic_milliseconds() + 10000u;
    while (ok && !waited && bq_worker_monotonic_milliseconds() < pause)
    {
        waited = waitpid(unit, &status, WUNTRACED | WNOHANG);
        if (!waited) poll(NULL, 0, 5);
    }
    ok = ok && waited == unit && WIFSTOPPED(status) && kill(unit, SIGCONT) == 0 &&
         bq_phase_init_version(&composed->phases, phase, attempt->job.id, attempt->job.token, BQ_PHASE_VERSION_2);
    BqSystemdContext context = {.pid = unit};
    if (ok)
        composed->joined = bq_worker_phase_join(queue, &context, &composed->phases, &status, deadline + 2u * 10000u,
                                                finalization);
    /* A unit the join did not reap is stopped here. */
    bool reaped = ok && context.pid < 0;
    if (unit > 0 && !reaped)
    {
        kill(unit, SIGKILL);
        while (waitpid(unit, &status, 0) < 0 && errno == EINTR) {}
    }
    composed->status = unit > 0 && WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    if (phase >= 0) close(phase);
    composed->phases.descriptor = -1;
    bq_worker_lease_handoff_close(&handoff);
    bq_worker_lease_release(&lease);
}

/* The queue state before the attempt, the result root removed. */
BUSTER_GLOBAL_LOCAL void bq_prep_worker_unit_composed_close(BqPrepOracleFixture* fixture,
    BqPrepWorkerUnitComposed* composed)
{
    if (composed->saved) memcpy(&fixture->queue.state, composed->saved, sizeof(fixture->queue.state));
    free(composed->saved);
    composed->saved = NULL;
    if (composed->result_directory >= 0) close(composed->result_directory);
    composed->result_directory = -1;
    if (composed->made) bq_prep_test_cleanup(composed->root);
    composed->made = false;
}

/* The attempt's retirement-authority/ path. */
BUSTER_GLOBAL_LOCAL bool bq_prep_worker_unit_authority_path(BqPrepOracleFixture const* fixture, BqJob const* job,
    char path[256])
{
    char name[64];
    int length = bq_workspace_name(name, job->id, job->token) ?
                 snprintf(path, 256, "%s/%s/" BQ_RETIREMENT_UNIT_AUTHORITY_DIRECTORY, fixture->workspaces, name) : -1;
    return length > 0 && length < 256;
}

/* Every regular file of `from` copied into the new directory `to`. */
BUSTER_GLOBAL_LOCAL bool bq_prep_worker_unit_copy_tree(char const* from, char const* to)
{
    DIR* listing = opendir(from);
    bool ok = listing && mkdir(to, 0700) == 0;
    for (struct dirent* entry = listing && ok ? readdir(listing) : NULL; ok && entry; entry = readdir(listing))
    {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        char source[512], target[512];
        int lengths[2] = {snprintf(source, sizeof(source), "%s/%s", from, entry->d_name),
                          snprintf(target, sizeof(target), "%s/%s", to, entry->d_name)};
        ok = lengths[0] > 0 && (size_t)lengths[0] < sizeof(source) && lengths[1] > 0 &&
             (size_t)lengths[1] < sizeof(target) && bq_prep_test_copy_file(source, target);
    }
    if (listing) closedir(listing);
    return ok;
}

/* The composed job-82 result for the Python checks
 * (retirement_compose_test.py, retirement_export_replay_real_test.py):
 * `retirement-worker-unit-result/` beside this runner, replaced, holding
 * `result/` (the result root, with every evidence file the binding names),
 * `authority/` (the producer's authority and context chain) and
 * `stand-in-batch.metrics` (the one metrics record every stand-in batch
 * writes, whose repetition the Python checks waive by digest). */
BUSTER_GLOBAL_LOCAL bool bq_prep_worker_unit_export(BqPrepOracleFixture const* fixture,
    BqPrepWorkerUnitComposed const* composed, BqJob const* job)
{
    char directory[4096], result[4200], authority[4200], source[256];
    size_t length = strlen(bq_prep_worker_unit_self);
    char const* slash = length ? strrchr(bq_prep_worker_unit_self, '/') : NULL;
    int named = slash ? snprintf(directory, sizeof(directory), "%.*s/retirement-worker-unit-result",
                                 (int)(slash - bq_prep_worker_unit_self), bq_prep_worker_unit_self) : -1;
    bool ok = named > 0 && (size_t)named < sizeof(directory) &&
              bq_prep_worker_unit_authority_path(fixture, job, source);
    struct stat info = {0};
    if (ok && lstat(directory, &info) == 0) bq_prep_test_cleanup(directory);
    snprintf(result, sizeof(result), "%s/result", directory);
    snprintf(authority, sizeof(authority), "%s/authority", directory);
    ok = ok && mkdir(directory, 0700) == 0 && bq_prep_worker_unit_copy_tree(composed->result_root, result) &&
         bq_prep_worker_unit_copy_tree(source, authority);
    char metrics_path[4200];
    u32 metrics_length = 0;
    char* metrics = ok ? bq_prep_test_oracle_output(0, &metrics_length) : NULL;
    snprintf(metrics_path, sizeof(metrics_path), "%s/stand-in-batch.metrics", directory);
    ok = ok && metrics && bq_prep_test_write_bytes(metrics_path, metrics, metrics_length);
    free(metrics);
    /* The #511 validator accepts the written binding structurally here; the
     * Python checks run it over the evidence (WorkerUnitEvidenceTests). */
    char binding[4300];
    int bound = snprintf(binding, sizeof(binding), "%s/" BQ_RETIREMENT_WORKER_BINDING_PATH, result);
    char* validate[] = {"python3", "-W", "error", "tools/native_retirement_performance_binding.py", binding, NULL};
    ok = ok && bound > 0 && (size_t)bound < sizeof(binding) && bq_prep_test_run(validate);
    return ok;
}

/* A scratch copy of the installed evidence directory at `directory` (made
 * here, files 0444, itself 0500) holding every installed file `list` names:
 * `changed` (when not NULL) with its last byte flipped, `dropped` (when not
 * NULL) left out. */
BUSTER_GLOBAL_LOCAL bool bq_prep_worker_unit_evidence_copy(int installed, BqRetirementWorkerEvidenceList const* list,
    char const* directory, char const* changed, char const* dropped)
{
    bool ok = mkdir(directory, 0700) == 0;
    for (u32 index = 0; ok && index < list->count; index += 1)
    {
        BqRetirementWorkerEvidence const* item = &list->items[index];
        if (item->held || (dropped && !strcmp(item->stored, dropped))) continue;
        u32 length = 0;
        char* bytes = bq_prep_worker_unit_slurp(installed, item->stored, &length);
        char path[512];
        int named = snprintf(path, sizeof(path), "%s/%s", directory, item->stored);
        ok = bytes && length && named > 0 && (size_t)named < sizeof(path);
        if (ok && changed && !strcmp(item->stored, changed)) bytes[length - 1u] ^= 1;
        ok = ok && bq_prep_test_write_bytes(path, bytes, length) && chmod(path, 0444) == 0;
        free(bytes);
    }
    ok = ok && chmod(directory, 0500) == 0;
    return ok;
}

/* One publication of `list` from `evidence` (a directory path) into a new
 * result directory: its result, and whether it left the directory empty. */
BUSTER_GLOBAL_LOCAL BqError bq_prep_worker_unit_evidence_run(BqRetirementWorkerEvidenceList const* list,
    char const* evidence, int const held[2], bool* empty)
{
    char root[] = "/tmp/bq-worker-unit-evidence-result-XXXXXX";
    bool made = mkdtemp(root) != NULL;
    int result_root = made ? open(root, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    int directory = open(evidence, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    BqError result = result_root >= 0 && directory >= 0 ?
                     bq_retirement_worker_evidence_publish(list, directory, held, result_root) : BQ_IO;
    DIR* listing = empty ? opendir(root) : NULL;
    u32 entries = 0;
    for (struct dirent* entry = listing ? readdir(listing) : NULL; entry; entry = readdir(listing))
        entries += strcmp(entry->d_name, ".") && strcmp(entry->d_name, "..");
    if (listing) closedir(listing);
    if (empty) *empty = listing && !entries;
    /* A complete publication wrote every listed file at its listed digest. */
    for (u32 index = 0; result == BQ_OK && index < list->count; index += 1)
    {
        char path[512], digest[SHA256_HEX_CAPACITY] = {0};
        int named = snprintf(path, sizeof(path), "%s/%s", root, list->items[index].stored);
        if (!(named > 0 && (size_t)named < sizeof(path) && bq_prep_test_file_sha(path, digest) &&
              !strcmp(digest, list->items[index].sha256)))
            result = BQ_CORRUPT;
    }
    if (directory >= 0) close(directory);
    if (result_root >= 0) close(result_root);
    if (made) bq_prep_test_cleanup(root);
    return result;
}

/* The installed context with `count` more requested-work items (canonical:
 * each item's keys and its artifact's keys in sorted order), each a distinct
 * binding path, inserted at the head of requested_work.items. */
BUSTER_GLOBAL_LOCAL bool bq_prep_worker_unit_evidence_grow(char const* installed, u32 length, u32 count,
    BqRetirementWorkerBindingContext* grown)
{
    static char const marker[] = "\nrequested_work={\"closure\":";
    char const* line = strstr(installed, marker);
    char const* items = line ? strstr(line, "\"items\":[") : NULL;
    char const* end = line ? strchr(line + 1, '\n') : NULL;
    bool ok = items && end && items < end;
    size_t at = ok ? (size_t)(items - installed) + strlen("\"items\":[") : 0;
    char item[256];
    size_t item_length = 0;
    grown->bytes = ok ? malloc(length + (size_t)count * sizeof(item) + 1u) : NULL;
    ok = ok && grown->bytes;
    size_t used = at;
    if (ok) memcpy(grown->bytes, installed, at);
    for (u32 index = 0; ok && index < count; index += 1)
    {
        int written = snprintf(item, sizeof(item), "{\"artifact\":{\"bytes\":1,\"path\":\""
                               "work/extra-%02u.closure\",\"sha256\":\"%064u\"},"
                               "\"kind\":\"workloads\",\"name\":\"extra-%02u\"},", index, 0u, index);
        ok = written > 0 && (size_t)written < sizeof(item);
        item_length = ok ? (size_t)written : 0;
        if (ok) memcpy(grown->bytes + used, item, item_length);
        used += item_length;
    }
    if (ok)
    {
        memcpy(grown->bytes + used, installed + at, length - at + 1u);
        grown->length = (u32)(used + length - at);
    }
    return ok;
}

/* (#881 P4) The evidence the binding context names: the installed context
 * lists all 39 files of the #511 record under their validator names, the
 * store plan's measure gives the same totals, and the publication from the
 * installed directory and job 81's frozen binaries (the held pair's bytes)
 * writes each at its listed digest. Refused, with no evidence file written
 * at all: swapped held binaries (BQ_SOURCE_MISMATCH), an installed file with
 * one byte changed and a missing one (BQ_RECIPE_MISMATCH). Refused at
 * listing: binding paths without an unambiguous result-root name
 * (bq_retirement_worker_evidence_map) and requested-work items beyond
 * BQ_RETIREMENT_WORKER_EVIDENCE_CAP (the cap itself lists). Each listed
 * file's result-root name is its binding path mapped by lane F's rule. */
BUSTER_GLOBAL_LOCAL void bq_prep_worker_unit_evidence_refusals(BqPrepOracleFixture* fixture,
    BqRetirementWorkerUnitSeams const* seams, BqJob const* reference)
{
    int recipes = open(fixture->recipes, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    u32 length = 0;
    char* installed = recipes >= 0 ? bq_prep_worker_unit_slurp(recipes, BQ_RETIREMENT_WORKER_BINDING_CONTEXT_NAME,
                                                               &length) : NULL;
    if (recipes >= 0) close(recipes);
    Arena* arena = arena_create((ArenaCreation){.reserved_size = UINT64_C(1) << 30, .flags = {.no_pool = 1}});
    BqRetirementWorkerBindingContext context = {.bytes = installed ? malloc(length + 1u) : NULL, .length = length};
    if (context.bytes) memcpy(context.bytes, installed, length + 1u);
    BqRetirementWorkerEvidenceList* list = calloc(1, sizeof(*list));
    u32 entries = 0;
    u64 bytes = 0;
    bool listed = arena && list && context.bytes && bq_retirement_worker_binding_parse(arena, &context) &&
                  bq_retirement_worker_evidence_list(&context, list);
    BQ_PREP_CHECK(listed && list->count == BQ_PREP_WORKER_UNIT_EVIDENCE && list->items[0].held == 0 &&
                  !strcmp(list->items[0].name, "contract.source") &&
                  !strcmp(list->items[0].path, "docs/native-retirement-performance-contract.md") &&
                  !strcmp(list->items[0].stored, BQ_RETIREMENT_WORKER_EVIDENCE_PREFIX
                          "docs--native-retirement-performance-contract.md") &&
                  !strcmp(list->items[1].name, "support.files[0]") &&
                  !strcmp(list->items[list->count - 1u].name, "workflow.records.admission"));
    BQ_PREP_CHECK(arena && bq_retirement_worker_evidence_measure(arena, fixture->installed_fd, seams->profile, &entries,
                                                                 &bytes) == BQ_OK &&
                  listed && entries == list->count && bytes == list->bytes);
    /* The held binaries, in the gate's order. */
    char workspace[64] = {0}, paths[2][256], evidence[256], scratch[] = "/tmp/bq-worker-unit-evidence-XXXXXX";
    int held[2] = {-1, -1};
    u32 binaries[2] = {BQ_PREP_WORKER_UNIT_EVIDENCE, BQ_PREP_WORKER_UNIT_EVIDENCE};
    bool opened = bq_workspace_name(workspace, reference->id, reference->token);
    for (u32 side = 0; opened && side < 2; side += 1)
    {
        int named = snprintf(paths[side], sizeof(paths[side]), "%s/%s/" BQ_RETIREMENT_BUILD_WORK_DIRECTORY
                             "/trusted-build/%s", fixture->workspaces, workspace, side ? "candidate-ide" : "base-ide");
        held[side] = named > 0 && (size_t)named < sizeof(paths[side]) ?
                     open(paths[side], O_RDONLY | O_CLOEXEC | O_NOFOLLOW) : -1;
        opened = held[side] >= 0;
    }
    for (u32 index = 0; listed && index < list->count; index += 1)
        if (list->items[index].held) binaries[list->items[index].held - 1u] = index;
    int named = snprintf(evidence, sizeof(evidence), "%s/" BQ_RETIREMENT_WORKER_EVIDENCE_NAME, fixture->recipes);
    bool ready = listed && opened && named > 0 && (size_t)named < sizeof(evidence) &&
                 binaries[0] < list->count && binaries[1] < list->count;
    BQ_PREP_CHECK(ready);
    int directory = ready ? bq_retirement_worker_evidence_open(fixture->installed_fd) : -1;
    BQ_PREP_CHECK(directory >= 0);
    if (directory >= 0) close(directory);
    bool unwritten = false;
    BQ_PREP_CHECK(ready && bq_prep_worker_unit_evidence_run(list, evidence, held, NULL) == BQ_OK);
    int swapped[2] = {held[1], held[0]};
    BQ_PREP_CHECK(ready && bq_prep_worker_unit_evidence_run(list, evidence, swapped, &unwritten) ==
                           BQ_SOURCE_MISMATCH && unwritten);
    /* An installed receipt changed by one byte, then one missing. */
    char const* receipt = NULL;
    for (u32 index = 0; ready && index < list->count; index += 1)
        if (!strcmp(list->items[index].name, "execution.service.recipe")) receipt = list->items[index].stored;
    int installed_evidence = ready ? open(evidence, O_RDONLY | O_DIRECTORY | O_CLOEXEC) : -1;
    for (u32 mode = 0; receipt && installed_evidence >= 0 && mode < 2; mode += 1)
    {
        memcpy(scratch + strlen(scratch) - 6u, "XXXXXX", 6);
        bool made = mkdtemp(scratch) != NULL && rmdir(scratch) == 0;
        unwritten = false;
        BQ_PREP_CHECK(made &&
                      bq_prep_worker_unit_evidence_copy(installed_evidence, list, scratch, mode ? NULL : receipt,
                                                        mode ? receipt : NULL) &&
                      bq_prep_worker_unit_evidence_run(list, scratch, held, &unwritten) == BQ_RECIPE_MISMATCH &&
                      unwritten);
        if (made && chmod(scratch, 0700) == 0) bq_prep_test_cleanup(scratch);
    }
    BQ_PREP_CHECK(receipt && installed_evidence >= 0);
    if (installed_evidence >= 0) close(installed_evidence);
    /* Binding paths without an unambiguous result-root name refuse at
     * listing: a segment containing `--`, an empty or `..` segment, a byte
     * outside the mapped set, and a single segment (lane F reads one where
     * it is). (A leading zero never parses: canonical JSON has none.) */
    static char const* const finds[] = {"\"measurement/harness\"", "\"measurement/harness\"",
                                        "\"measurement/harness\"", "\"measurement/harness\"",
                                        "\"measurement/harness\""};
    static char const* const replacements[] = {"\"measurement/har--ss\"", "\"measurement//arness\"",
                                               "\"measurement/../ness\"", "\"measurement/harn$ss\"",
                                               "\"retirement-harness1\""};
    for (u32 index = 0; installed && index < BUSTER_ARRAY_LENGTH(finds); index += 1)
    {
        Arena* scratch_arena = arena_create((ArenaCreation){.reserved_size = UINT64_C(1) << 30,
                                                             .flags = {.no_pool = 1}});
        BqRetirementWorkerBindingContext edited = {.bytes = malloc(length + 1u), .length = length};
        bool copied = scratch_arena && edited.bytes;
        if (copied) memcpy(edited.bytes, installed, length + 1u);
        char* at = copied ? strstr((char*)edited.bytes, finds[index]) : NULL;
        if (at) memcpy(at, replacements[index], strlen(replacements[index]));
        BQ_PREP_CHECK(at && strlen(finds[index]) == strlen(replacements[index]) &&
                      bq_retirement_worker_binding_parse(scratch_arena, &edited) &&
                      !bq_retirement_worker_evidence_list(&edited, list));
        bq_retirement_worker_binding_release(&edited);
        if (scratch_arena) arena_destroy(scratch_arena, 1);
    }
    /* Requested-work items up to the cap list; one more refuses. */
    u32 extra = BQ_RETIREMENT_WORKER_EVIDENCE_CAP - BQ_PREP_WORKER_UNIT_EVIDENCE;
    for (u32 count = extra; installed && count <= extra + 1u; count += 1)
    {
        Arena* scratch_arena = arena_create((ArenaCreation){.reserved_size = UINT64_C(1) << 30,
                                                             .flags = {.no_pool = 1}});
        BqRetirementWorkerBindingContext grown = {0};
        bool made = scratch_arena && bq_prep_worker_unit_evidence_grow(installed, length, count, &grown);
        bool listed_grown = made && bq_retirement_worker_binding_parse(scratch_arena, &grown) &&
                            bq_retirement_worker_evidence_list(&grown, list);
        BQ_PREP_CHECK(made && listed_grown == (count == extra) &&
                      (!listed_grown || list->count == BQ_RETIREMENT_WORKER_EVIDENCE_CAP));
        bq_retirement_worker_binding_release(&grown);
        if (scratch_arena) arena_destroy(scratch_arena, 1);
    }
    for (u32 side = 0; side < 2; side += 1)
        if (held[side] >= 0) close(held[side]);
    bq_retirement_worker_binding_release(&context);
    free(list);
    free(installed);
    if (arena) arena_destroy(arena, 1);
}

/* (#881 P4) One table of binding paths through both result-root name rules:
 * the producer's bq_retirement_worker_evidence_map and lane F's
 * evidence_name (tools/bench_service/retirement_lane_f.py, imported by
 * python3 over the same paths as arguments, one name or `-` per line). The
 * two must agree on every row, and every row's outcome is the pinned one:
 * nested paths map (a `retirement-` or other reserved first segment
 * included, and `..b` or `.github`, which are not `.` or `..`); a single
 * segment, reserved or not, has no flat name; nor has an empty, `.` or `..`
 * segment, an in-segment `--`, a segment beginning or ending with `-` (the
 * pairs `a-/b`, `a/-b` and `a--b/c`, `a/b--c` would otherwise share one
 * name), a byte outside [A-Za-z0-9._-] and `/`, or a mapped name over
 * BQ_RETIREMENT_WORKER_EVIDENCE_PATH_CAP (128 bytes maps, 129 does not; a
 * 128- or 129-byte path never fits). */
BUSTER_GLOBAL_LOCAL void bq_prep_worker_unit_evidence_names(void)
{
    typedef struct BqPrepWorkerUnitName
    {
        char const* path;
        char const* name;
    } BqPrepWorkerUnitName;
    BqPrepWorkerUnitName table[] = {
        {"docs/native-retirement-performance-contract.md",
         BQ_RETIREMENT_WORKER_EVIDENCE_PREFIX "docs--native-retirement-performance-contract.md"},
        {"tools/throughput/retirement_stats.h",
         BQ_RETIREMENT_WORKER_EVIDENCE_PREFIX "tools--throughput--retirement_stats.h"},
        {"a-b/c-d", BQ_RETIREMENT_WORKER_EVIDENCE_PREFIX "a-b--c-d"},
        {"a/..b", BQ_RETIREMENT_WORKER_EVIDENCE_PREFIX "a--..b"},
        {".github/x", BQ_RETIREMENT_WORKER_EVIDENCE_PREFIX ".github--x"},
        {"retirement-x/y", BQ_RETIREMENT_WORKER_EVIDENCE_PREFIX "retirement-x--y"},
        {"worker-phase-x/y", BQ_RETIREMENT_WORKER_EVIDENCE_PREFIX "worker-phase-x--y"},
        {"contract.md", NULL},
        {"retirement-oracle.json", NULL},
        {"retirement-evidence-docs--contract.md", NULL},
        {"worker-phase-ready", NULL},
        {"unit-campaign-x", NULL},
        {"native-retirement-performance-v1.bundle", NULL},
        {"", NULL},
        {"/", NULL},
        {"/a", NULL},
        {"a/", NULL},
        {"a//b", NULL},
        {"./a", NULL},
        {"a/./b", NULL},
        {"a/../b", NULL},
        {"../a", NULL},
        {"a/.", NULL},
        {"a--b/c", NULL},
        {"a/b--c", NULL},
        {"a/b---c", NULL},
        {"a-/b", NULL},
        {"a/-b", NULL},
        {"-a/b", NULL},
        {"a/b-", NULL},
        {"a/-", NULL},
        {"a/b$c", NULL},
        {"a/b c", NULL},
        {"a\\b/c", NULL},
        {"a/\xc3\xa9", NULL},
        /* The long rows, generated below: "a/" then `b` bytes, for paths
         * of 107 and 108 bytes (mapped names of 128 and 129) and of 128 and
         * 129 bytes. */
        {NULL, NULL}, {NULL, NULL}, {NULL, NULL}, {NULL, NULL},
    };
    enum { BQ_PREP_WORKER_UNIT_NAMES = BUSTER_ARRAY_LENGTH(table) };
    static u32 const long_paths[] = {107u, 108u, 128u, 129u};
    char paths[BUSTER_ARRAY_LENGTH(long_paths)][BQ_RETIREMENT_WORKER_EVIDENCE_PATH_CAP + 2];
    char fitting[BQ_RETIREMENT_WORKER_EVIDENCE_PATH_CAP + 1];
    for (u32 row = 0; row < BUSTER_ARRAY_LENGTH(long_paths); row += 1)
    {
        memset(paths[row], 'b', long_paths[row]);
        memcpy(paths[row], "a/", 2);
        paths[row][long_paths[row]] = 0;
        table[BQ_PREP_WORKER_UNIT_NAMES - BUSTER_ARRAY_LENGTH(long_paths) + row].path = paths[row];
    }
    /* Only the 107-byte path's name (the prefix, "a--", 105 `b`) fits. */
    int fitted = snprintf(fitting, sizeof(fitting), "%sa--%s", BQ_RETIREMENT_WORKER_EVIDENCE_PREFIX, paths[0] + 2);
    BQ_PREP_CHECK(fitted == (int)BQ_RETIREMENT_WORKER_EVIDENCE_PATH_CAP);
    table[BQ_PREP_WORKER_UNIT_NAMES - BUSTER_ARRAY_LENGTH(long_paths)].name = fitting;
    static char script[] =
        "import sys\n"
        "sys.path.insert(0, 'tools/bench_service')\n"
        "import retirement_lane_f as lane_f\n"
        "with open(sys.argv[1], 'x') as out:\n"
        "    for path in sys.argv[2:]:\n"
        "        out.write((lane_f.evidence_name(path) or '-') + '\\n')\n";
    char directory[] = "/tmp/bq-worker-unit-names-XXXXXX", output[64];
    bool made = mkdtemp(directory) != NULL;
    int named = made ? snprintf(output, sizeof(output), "%s/names", directory) : -1;
    /* The seven leading arguments, one per path and the terminating NULL. */
    char* argv[7 + BQ_PREP_WORKER_UNIT_NAMES + 1] = {"python3", "-B", "-W", "error", "-c", script, output};
    for (u32 index = 0; index < BQ_PREP_WORKER_UNIT_NAMES; index += 1)
        argv[7u + index] = (char*)table[index].path;
    argv[7u + BQ_PREP_WORKER_UNIT_NAMES] = NULL;
    bool ran = made && named > 0 && (size_t)named < sizeof(output) && bq_prep_test_run(argv);
    if (!ran) fprintf(stderr, "evidence names: lane F's evidence_name did not run (%s)\n", made ? output : directory);
    int opened = ran ? open(directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC) : -1;
    u32 length = 0;
    char* lane_f = opened >= 0 ? bq_prep_worker_unit_slurp(opened, "names", &length) : NULL;
    if (opened >= 0) close(opened);
    BQ_PREP_CHECK(lane_f != NULL);
    char* line = lane_f;
    for (u32 index = 0; lane_f && index < BQ_PREP_WORKER_UNIT_NAMES; index += 1)
    {
        char stored[BQ_RETIREMENT_WORKER_EVIDENCE_PATH_CAP + 1] = {0};
        bool mapped = bq_retirement_worker_evidence_map(table[index].path, (u32)strlen(table[index].path), stored);
        char* end = line ? strchr(line, '\n') : NULL;
        if (end) *end = 0;
        bool pinned = table[index].name ? mapped && !strcmp(stored, table[index].name) : !mapped;
        bool agreed = end && (mapped ? !strcmp(line, stored) : !strcmp(line, "-"));
        if (!pinned || !agreed)
            fprintf(stderr, "evidence name row %u (%s): C %s, lane F %s\n", index, table[index].path,
                    mapped ? stored : "-", end ? line : "(missing)");
        BQ_PREP_CHECK(pinned && agreed);
        line = end ? end + 1 : NULL;
    }
    BQ_PREP_CHECK(line && *line == 0);
    free(lane_f);
    if (named > 0 && (size_t)named < sizeof(output)) unlink(output);
    if (made) rmdir(directory);
}

/* The digest with its first hex digit changed. */
BUSTER_GLOBAL_LOCAL void bq_prep_worker_unit_flip(char digest[SHA256_HEX_CAPACITY])
{
    digest[0] = digest[0] == '0' ? '1' : '0';
}

/* `name` copied from `from` into `to` (mode 0600) with the first occurrence
 * of `find` replaced by `replace` (both NULL: unchanged); digest receives the
 * copy's SHA-256. */
BUSTER_GLOBAL_LOCAL bool bq_prep_worker_unit_copy_edit(int from, int to, char const* name, char const* find,
    char const* replace, char digest[SHA256_HEX_CAPACITY])
{
    u32 length = 0;
    char* bytes = bq_prep_worker_unit_slurp(from, name, &length);
    char* at = bytes && find ? strstr(bytes, find) : NULL;
    size_t find_length = find ? strlen(find) : 0, replace_length = replace ? strlen(replace) : 0;
    bool ok = bytes && (!find || at);
    size_t total = ok ? (size_t)length - find_length + replace_length : 0;
    char* edited = ok ? malloc(total + 1u) : NULL;
    ok = ok && edited;
    if (ok)
    {
        size_t prefix = at ? (size_t)(at - bytes) : length;
        memcpy(edited, bytes, prefix);
        if (at)
        {
            memcpy(edited + prefix, replace, replace_length);
            memcpy(edited + prefix + replace_length, at + find_length, (size_t)length - prefix - find_length);
        }
    }
    int file = ok ? openat(to, name, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600) : -1;
    ok = ok && file >= 0 && bq_write_all(file, (u8 const*)edited, (u32)total);
    if (file >= 0 && close(file) != 0) ok = false;
    if (ok) bq_digest(edited, (u32)total, (char8*)digest);
    free(edited);
    free(bytes);
    return ok;
}

/* The final context of a binding over a raw numeric digest. */
BUSTER_GLOBAL_LOCAL bool bq_prep_worker_unit_context(int directory, char const* raw, char digest[SHA256_HEX_CAPACITY])
{
    Arena* arena = arena_create((ArenaCreation){.reserved_size = UINT64_C(1) << 30, .flags = {.no_pool = 1}});
    u32 length = 0;
    char* binding = bq_prep_worker_unit_slurp(directory, BQ_RETIREMENT_WORKER_BINDING_PATH, &length);
    char* context = NULL;
    size_t context_length = 0;
    bool ok = arena && binding && tp_retirement_compose_execution_context((unsigned char const*)binding, length, raw,
                                                                          arena, &context, &context_length);
    if (ok) bq_digest(context, (u32)context_length, (char8*)digest);
    free(binding);
    if (arena) arena_destroy(arena, 1);
    return ok;
}

/* One scratch edit of the composed result for the derivation: `find`
 * replaced by `replace` in one copied file; `relist` moves the record's
 * retained line and the authority's retained digest to the edited record (a
 * consistent forgery of the record), `plan` moves the authority's plan to
 * the edited record's execution-plan value, `rebind` moves the chain's
 * binding digest to the edited binding, and `recontext` the authority's
 * context to the one recomputed over it. */
typedef struct BqPrepWorkerUnitForge
{
    u32 file;
    char find[96], replace[96];
    bool relist, plan, rebind, recontext;
} BqPrepWorkerUnitForge;

/* Every result-root file the derivation reads, the retained manifest last
 * (it lists the record's copy). */
enum { BQ_PREP_FORGE_UNTIMED, BQ_PREP_FORGE_RECORD, BQ_PREP_FORGE_BINDING, BQ_PREP_FORGE_ADMISSION,
       BQ_PREP_FORGE_RETAINED, BQ_PREP_FORGE_FILES };

/* The derivation over a scratch result directory of copies with `forge`
 * applied (NULL: unedited). */
BUSTER_GLOBAL_LOCAL BqError bq_prep_worker_unit_forged(BqRetirementWorkerUnitSeams const* seams,
    BqRetirementUnitReplayed const* replayed, BqJob const* job, char const* ready, int result,
    TpRetirementReceiptAuthority const* trusted, BqRetirementContextChainCarried const* carried,
    BqPrepWorkerUnitForge const* forge)
{
    static char const* const files[BQ_PREP_FORGE_FILES] = {BQ_RETIREMENT_WORKER_UNTIMED_PATH,
        BQ_RETIREMENT_UNIT_CAMPAIGN_RECORD, BQ_RETIREMENT_WORKER_BINDING_PATH, BQ_RETIREMENT_WORKER_ADMISSION_PATH,
        TP_RETIREMENT_RETAINED_MANIFEST_PATH};
    char scratch[] = "/tmp/bq-worker-unit-derive-XXXXXX";
    bool made = mkdtemp(scratch) != NULL;
    int copy = made ? open(scratch, O_RDONLY | O_DIRECTORY | O_CLOEXEC) : -1;
    char digests[BQ_PREP_FORGE_FILES][SHA256_HEX_CAPACITY] = {{0}}, record_digest[SHA256_HEX_CAPACITY] = {0};
    char listed[80] = {0}, relisted[80] = {0};
    u32 length = 0;
    char* record = bq_prep_worker_unit_slurp(result, BQ_RETIREMENT_UNIT_CAMPAIGN_RECORD, &length);
    if (record) bq_digest(record, length, (char8*)record_digest);
    bool ok = copy >= 0 && record;
    for (u32 index = 0; ok && index < BQ_PREP_FORGE_RETAINED; index += 1)
    {
        bool edited = forge && forge->file == index;
        ok = bq_prep_worker_unit_copy_edit(result, copy, files[index], edited ? forge->find : NULL,
                                           edited ? forge->replace : NULL, digests[index]);
    }
    bool relist = forge && forge->relist;
    snprintf(listed, sizeof(listed), " %s ", record_digest);
    snprintf(relisted, sizeof(relisted), " %s ", digests[BQ_PREP_FORGE_RECORD]);
    ok = ok && bq_prep_worker_unit_copy_edit(result, copy, files[BQ_PREP_FORGE_RETAINED], relist ? listed : NULL,
                                             relist ? relisted : NULL, digests[BQ_PREP_FORGE_RETAINED]);
    TpRetirementReceiptAuthority edited = *trusted;
    BqRetirementContextChainCarried linked = *carried;
    if (relist) memcpy(edited.retained_sha256, digests[BQ_PREP_FORGE_RETAINED], SHA256_HEX_CAPACITY);
    char const* value = forge && forge->plan ? strchr(forge->replace, '=') : NULL;
    if (value) memcpy(edited.plan_sha256, value + 1, 64);
    if (forge && forge->rebind) memcpy(linked.binding, digests[BQ_PREP_FORGE_BINDING], SHA256_HEX_CAPACITY);
    if (forge && forge->recontext)
        ok = ok && bq_prep_worker_unit_context(copy, carried->stages[1].raw, edited.context_sha256);
    BqError derived = ok ? bq_retirement_coordinator_derive(seams, replayed, job->id, job->token, ready, &edited, &linked,
                                                            copy) : BQ_IO;
    free(record);
    if (copy >= 0) close(copy);
    if (made) bq_prep_test_cleanup(scratch);
    return derived;
}

/* A record-line forgery: the first character of `key`'s value flipped. */
BUSTER_GLOBAL_LOCAL bool bq_prep_worker_unit_forge_line(char const* record, char const* key, bool plan,
    BqPrepWorkerUnitForge* forge)
{
    char pattern[48];
    int named = snprintf(pattern, sizeof(pattern), "\n%s", key);
    char const* found = named > 0 && record ? strstr(record, pattern) : NULL;
    char const* end = found ? strchr(found + 1, '\n') : NULL;
    size_t span = end ? (size_t)(end - found) : 0;
    bool ok = span && span < sizeof(forge->find) && (size_t)named < span;
    *forge = (BqPrepWorkerUnitForge){.file = BQ_PREP_FORGE_RECORD, .relist = true, .plan = plan};
    if (ok)
    {
        memcpy(forge->find, found, span);
        memcpy(forge->replace, found, span);
        bq_prep_worker_unit_flip(forge->replace + named);
    }
    return ok;
}

/* Review items M1 of #1961 and of #1964, over the composed job: the
 * coordinator's derivation (bq_retirement_coordinator_derive) accepts the
 * genuine authority and chain and refuses, each through one check alone:
 *   an authority whose plan differs from the recomputed execution-plan
 *   document; a ready digest other than the coordinator's (the pre-sample
 *   context no longer binds it); a carried stage fact that does not give
 *   the carried post-sample context; a carried binding digest that is not
 *   the result's binding; an authority context that is not the binding's
 *   execution context;
 * and, over scratch copies of every result-root file it reads (which the
 * unedited copy derives): an untimed record stream other than the one the
 * store sealed; an admission receipt other than the one the record names; a
 * record byte no line check reads (its
 * retained line is stale); every checked line of the post-sample record
 * forged consistently (the record, its retained line and the authority's
 * retained digest), including its execution-plan line with the authority's
 * plan following (so only the recomputed document refuses it) and without
 * it (so only the record's line check refuses it); a binding linked to
 * another post-A/A document, or with another measurement harness, each with
 * the chain's binding digest and the authority's context recomputed over it
 * (refused because the coordinator derives the context from its own
 * rendering); and a binding with another measurement harness whose chain
 * digest follows but whose authority context stays the genuine one (so only
 * the byte comparison with that rendering refuses it); and a pinned binding
 * context with another campaign seed, installed under its own pin, with the
 * result's binding rendered from it (so only the coordinator's own check of
 * the context refuses it). */
BUSTER_GLOBAL_LOCAL void bq_prep_worker_unit_derivation(BqPrepOracleFixture* fixture, BqPrepUnitAttempt const* attempt,
    BqRetirementWorkerUnitSeams const* seams, BqPrepWorkerUnitComposed const* composed,
    BqRetirementUnitReplayed const* replayed)
{
    String8 workspaces = string_from_pointer(fixture->workspaces);
    BqWorkerFinalization const* finalization = &composed->finalization;
    u64 job = attempt->job.id, token = attempt->job.token;
    TpRetirementReceiptAuthority trusted = {0};
    BqRetirementContextChainCarried carried = {0};
    BQ_PREP_CHECK(bq_retirement_coordinator_authority_complete(composed->result_directory, workspaces,
                      fixture->queue.directory_fd, job, token, seams->profile, attempt->digest,
                      finalization->retirement_ready_sha256, finalization->retirement_authority_sha256, &trusted,
                      &carried) == BQ_OK);
    char const* ready = finalization->retirement_ready_sha256;
    int result = composed->result_directory;
    BQ_PREP_CHECK(bq_retirement_coordinator_derive(seams, replayed, job, token, ready, &trusted, &carried, result) ==
                  BQ_OK);
    TpRetirementReceiptAuthority forged = trusted;
    bq_prep_worker_unit_flip(forged.plan_sha256);
    BQ_PREP_CHECK(bq_retirement_coordinator_derive(seams, replayed, job, token, ready, &forged, &carried, result) ==
                  BQ_WORKER_MISMATCH);
    char other[SHA256_HEX_CAPACITY];
    memcpy(other, ready, SHA256_HEX_CAPACITY);
    bq_prep_worker_unit_flip(other);
    BQ_PREP_CHECK(bq_retirement_coordinator_derive(seams, replayed, job, token, other, &trusted, &carried, result) ==
                  BQ_WORKER_MISMATCH);
    BqRetirementContextChainCarried changed = carried;
    changed.stages[0].invocations += 1u;
    BQ_PREP_CHECK(bq_retirement_coordinator_derive(seams, replayed, job, token, ready, &trusted, &changed, result) ==
                  BQ_WORKER_MISMATCH);
    changed = carried;
    bq_prep_worker_unit_flip(changed.binding);
    BQ_PREP_CHECK(bq_retirement_coordinator_derive(seams, replayed, job, token, ready, &trusted, &changed, result) ==
                  BQ_WORKER_MISMATCH);
    forged = trusted;
    bq_prep_worker_unit_flip(forged.context_sha256);
    BQ_PREP_CHECK(bq_retirement_coordinator_derive(seams, replayed, job, token, ready, &forged, &carried, result) ==
                  BQ_WORKER_MISMATCH);
    /* The scratch edits: unedited first (which derives). */
    u32 length = 0, bound = 0;
    char* record = bq_prep_worker_unit_slurp(result, BQ_RETIREMENT_UNIT_CAMPAIGN_RECORD, &length);
    char* binding = bq_prep_worker_unit_slurp(result, BQ_RETIREMENT_WORKER_BINDING_PATH, &bound);
    char const* post = record ? strstr(record, "\npost-aa-binding=") : NULL;
    char const* harness = binding ? strstr(binding, "\"harness_source_commit\":\"") : NULL;
    BQ_PREP_CHECK(record && post && harness);
    static char const* const lines[] = {"job=", "attempt=", "plan=", "pre-sample=", "post-sample=", "log-untimed=",
        "log-aa=", "log-ab=", "pre-sample-plan=", "result-input-plan=", "post-aa-binding=", "aa-admission=", "family=",
        "timed-rows=", "execution-plan="};
    BqPrepWorkerUnitForge forges[3u + BUSTER_ARRAY_LENGTH(lines) + 4u];
    u32 count = 0;
    forges[count++] = (BqPrepWorkerUnitForge){.file = BQ_PREP_FORGE_UNTIMED, .find = "\"", .replace = "'"};
    /* Another admission receipt than the one the record and binding name. */
    forges[count++] = (BqPrepWorkerUnitForge){.file = BQ_PREP_FORGE_ADMISSION, .find = "\"admitted\":true",
                                              .replace = "\"admitted\":TRUE"};
    forges[count++] = (BqPrepWorkerUnitForge){.file = BQ_PREP_FORGE_RECORD, .find = "\nlaunches=",
                                              .replace = "\nLaunches="};
    bool prepared = record && post && harness;
    for (u32 index = 0; prepared && index < BUSTER_ARRAY_LENGTH(lines); index += 1)
    {
        bool plan = !strcmp(lines[index], "execution-plan=");
        prepared = bq_prep_worker_unit_forge_line(record, lines[index], plan, &forges[count]);
        count += prepared;
    }
    if (prepared)
    {
        prepared = bq_prep_worker_unit_forge_line(record, "execution-plan=", false, &forges[count]);
        count += prepared;
        BqPrepWorkerUnitForge* link = &forges[count++];
        *link = (BqPrepWorkerUnitForge){.file = BQ_PREP_FORGE_BINDING, .rebind = true, .recontext = true};
        snprintf(link->find, sizeof(link->find), "%.64s", post + 17);
        snprintf(link->replace, sizeof(link->replace), "%.64s", post + 17);
        bq_prep_worker_unit_flip(link->replace);
        BqPrepWorkerUnitForge* measurement = &forges[count++];
        *measurement = (BqPrepWorkerUnitForge){.file = BQ_PREP_FORGE_BINDING, .rebind = true, .recontext = true};
        snprintf(measurement->find, sizeof(measurement->find), "%.30s", harness);
        snprintf(measurement->replace, sizeof(measurement->replace), "%.30s", harness);
        bq_prep_worker_unit_flip(measurement->replace + 25);
        forges[count] = *measurement;
        forges[count++].recontext = false;
    }
    BQ_PREP_CHECK(prepared && count == BUSTER_ARRAY_LENGTH(forges));
    BqJob const* queued = &attempt->job;
    BQ_PREP_CHECK(bq_prep_worker_unit_forged(seams, replayed, queued, ready, result, &trusted, &carried, NULL) == BQ_OK);
    for (u32 index = 0; prepared && index < count; index += 1)
    {
        BqError derived = bq_prep_worker_unit_forged(seams, replayed, queued, ready, result, &trusted, &carried,
                                                     &forges[index]);
        if (derived != BQ_WORKER_MISMATCH)
            fprintf(stderr, "RETIREMENT_PREP derivation forge %u (%s): %d\n", index, forges[index].find, (int)derived);
        BQ_PREP_CHECK(derived == BQ_WORKER_MISMATCH);
    }
    /* A pinned binding context the replayed plan refuses (another campaign
     * seed), installed under its own pin, with the result's binding rendered
     * from it (the context's rules section is the binding's verbatim) and the
     * chain and authority following: the coordinator's rendering then equals
     * the result, so only its own check of the context refuses it. */
    BqRetirementUnitCampaignPins pins = {0};
    char seed[32], other_seed[32], pin[128] = {0}, profile[sizeof(fixture->profile)];
    u32 context_length = 0;
    int recipes = open(fixture->recipes, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    char* context = recipes >= 0 ? bq_prep_worker_unit_slurp(recipes, BQ_RETIREMENT_WORKER_BINDING_CONTEXT_NAME,
                                                             &context_length) : NULL;
    if (recipes >= 0) close(recipes);
    bool pinned = context && bq_retirement_unit_campaign_pins(seams->profile, &pins);
    snprintf(seed, sizeof(seed), "\"seed\":%" PRIu64 ",", (uint64_t)pins.seed);
    snprintf(other_seed, sizeof(other_seed), "\"seed\":%" PRIu64 ",", (uint64_t)(pins.seed ^ 1u));
    char* rules = pinned ? strstr(context, "\nrules=") : NULL;
    char* at = rules ? strstr(rules, seed) : NULL;
    char* key = strstr(fixture->profile, BQ_RETIREMENT_WORKER_BINDING_CONTEXT_PIN);
    pinned = at && key && strlen(seed) == strlen(other_seed) && binding && strstr(binding, seed);
    if (pinned) memcpy(at, other_seed, strlen(other_seed));
    pinned = pinned && bq_prep_worker_unit_install(fixture->recipes, BQ_RETIREMENT_WORKER_BINDING_CONTEXT_NAME,
                                                   context, context_length, BQ_RETIREMENT_WORKER_BINDING_CONTEXT_PIN,
                                                   pin);
    if (pinned)
    {
        size_t offset = (size_t)(key - fixture->profile) + strlen(BQ_RETIREMENT_WORKER_BINDING_CONTEXT_PIN);
        memcpy(profile, fixture->profile, sizeof(profile));
        memcpy(profile + offset, pin + strlen(BQ_RETIREMENT_WORKER_BINDING_CONTEXT_PIN), 64);
        BqRetirementWorkerUnitSeams reseeded = *seams;
        reseeded.profile = string_from_pointer(profile);
        BqPrepWorkerUnitForge rendered = {.file = BQ_PREP_FORGE_BINDING, .rebind = true, .recontext = true};
        snprintf(rendered.find, sizeof(rendered.find), "%s", seed);
        snprintf(rendered.replace, sizeof(rendered.replace), "%s", other_seed);
        BqError derived = bq_prep_worker_unit_forged(&reseeded, replayed, queued, ready, result, &trusted, &carried,
                                                     &rendered);
        if (derived != BQ_WORKER_MISMATCH) fprintf(stderr, "RETIREMENT_PREP derivation reseeded context: %d\n", derived);
        BQ_PREP_CHECK(derived == BQ_WORKER_MISMATCH);
        memcpy(at, seed, strlen(seed));
    }
    /* The genuine context again, under its original pin. */
    char restored[128] = {0};
    BQ_PREP_CHECK(pinned && bq_prep_worker_unit_install(fixture->recipes, BQ_RETIREMENT_WORKER_BINDING_CONTEXT_NAME,
                      context, context_length, BQ_RETIREMENT_WORKER_BINDING_CONTEXT_PIN, restored) &&
                  !memcmp(restored + strlen(BQ_RETIREMENT_WORKER_BINDING_CONTEXT_PIN),
                          key + strlen(BQ_RETIREMENT_WORKER_BINDING_CONTEXT_PIN), 64));
    free(context);
    free(binding);
    free(record);
}

/* A held binary's bytes into the new file `path` (mode 0600). */
BUSTER_GLOBAL_LOCAL bool bq_prep_worker_unit_copy_held(int descriptor, char const* path)
{
    int file = descriptor >= 0 ? open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600) : -1;
    char buffer[65536];
    off_t offset = 0;
    ssize_t got = 1;
    bool ok = file >= 0;
    while (ok && got > 0)
    {
        got = pread(descriptor, buffer, sizeof(buffer), offset);
        ok = got >= 0 && (got == 0 || bq_write_all(file, (u8 const*)buffer, (u32)got));
        offset += ok ? (off_t)got : 0;
    }
    ok = ok && offset > 0;
    if (file >= 0 && close(file) != 0) ok = false;
    return ok;
}

/* Whether bytes (length, NUL-terminated) with `edit` applied parse as a
 * context and pass the check. edit 0 keeps them; 1 swaps the measurement and
 * population lines; 2 drops the provenance line; 3 moves the admission
 * sentinel out of execution.host to execution's own first member (still
 * canonical, since aa_admission_receipt sorts before host). */
BUSTER_GLOBAL_LOCAL bool bq_prep_worker_unit_context_accepted(char const* bytes, u32 length, u32 edit,
    String8 profile, BqRetirementCorrectness const* gate, char const family_sha256[SHA256_HEX_CAPACITY],
    BqRetirementUnitCampaignPins const* pins, TpRetirementPlan const* plan)
{
    Arena* arena = arena_create((ArenaCreation){.reserved_size = UINT64_C(1) << 30, .flags = {.no_pool = 1}});
    BqRetirementWorkerBindingContext context = {.bytes = malloc(length + 1u), .length = 0};
    char const* measurement = strstr(bytes, "\nmeasurement=");
    char const* population = measurement ? strstr(measurement, "\npopulation=") : NULL;
    char const* producer = population ? strstr(population, "\nproducer=") : NULL;
    char const* provenance = producer ? strstr(producer, "\nprovenance=") : NULL;
    char const* requested = provenance ? strstr(provenance, "\nrequested_work=") : NULL;
    char const* host = strstr(bytes, "\nexecution={\"host\":{" BQ_RETIREMENT_WORKER_ADMISSION_SENTINEL ",");
    size_t sentinel = strlen(BQ_RETIREMENT_WORKER_ADMISSION_SENTINEL);
    bool ok = arena && context.bytes && requested && host;
    u8* out = context.bytes;
    if (ok && edit == 0) memcpy(out, bytes, length);
    if (ok && edit == 1)
    {
        size_t head = (size_t)(measurement - bytes), first = (size_t)(population - measurement);
        size_t second = (size_t)(producer - population);
        memcpy(out, bytes, head);
        memcpy(out + head, population, second);
        memcpy(out + head + second, measurement, first);
        memcpy(out + head + second + first, producer, length - (size_t)(producer - bytes));
    }
    if (ok && edit == 2)
    {
        size_t head = (size_t)(provenance - bytes);
        memcpy(out, bytes, head);
        memcpy(out + head, requested, length - (size_t)(requested - bytes));
    }
    if (ok && edit == 3)
    {
        /* "\nexecution={" SENTINEL ",\"host\":{" rest-after-sentinel-comma */
        size_t head = (size_t)(host - bytes) + strlen("\nexecution={");
        size_t at = head + strlen("\"host\":{");
        memcpy(out, bytes, head);
        memcpy(out + head, bytes + at, sentinel);
        out[head + sentinel] = ',';
        memcpy(out + head + sentinel + 1u, "\"host\":{", strlen("\"host\":{"));
        memcpy(out + at + sentinel + 1u, bytes + at + sentinel + 1u, length - (at + sentinel + 1u));
    }
    context.length = edit == 2 ? (u32)(length - (size_t)(requested - provenance)) : length;
    ok = ok && bq_retirement_worker_binding_parse(arena, &context) &&
         bq_retirement_worker_binding_check(&context, profile, gate, family_sha256, pins, plan);
    bq_retirement_worker_binding_release(&context);
    if (arena) arena_destroy(arena, 1);
    return ok;
}

/* The production context generator (retirement_binding_context.py) over the
 * preparation fixture's test record as its inputs (the fixture's
 * --production-inputs over the installed census, with the reference
 * attempt's two held binaries copied out and lane D's frozen campaign values
 * from the completed profile), before any campaign runs: the context parses
 * and passes bq_retirement_worker_binding_check against the inputs' profile
 * pins (the fixture profile's census pins and a contract pin of the record's
 * contract source), the reference projection's rows and binaries, and the
 * plan's values (the pinned seed, pairs and resamples and the derived
 * family's member counts). It is refused with two sections swapped, with a
 * section dropped and with the admission sentinel moved out of
 * execution.host. Run twice, the generator emits the same bytes. */
BUSTER_GLOBAL_LOCAL void bq_prep_worker_unit_production_context(BqPrepOracleFixture* fixture,
    BqPrepOracleAttempt const* reference)
{
    String8 fixture_profile = string_from_pointer(fixture->profile);
    BqRetirementCorrectness gate = {.prepared = reference->projection.prepared,
                                    .trusted_rows = reference->projection.rows};
    BqRetirementDocumentPopulation population = {0};
    BqRetirementDocumentPartition timed = {0};
    BqRetirementDocumentFamily family = {0};
    BqRetirementUnitCampaignPins pins = {0};
    bool derived = bq_retirement_documents_population(fixture->installed_fd, fixture_profile, gate.trusted_rows,
                       gate.prepared.rows, gate.prepared.native_target, &population) == BQ_OK &&
                   bq_retirement_documents_partition(&population, 0, &timed) &&
                   bq_retirement_documents_family(&population, &timed, &family) &&
                   bq_retirement_unit_campaign_pins(fixture_profile, &pins) &&
                   family.bootstrap_members == pins.bootstrap_members;
    TpRetirementPlan plan = {.seed = pins.seed, .version = TP_RETIREMENT_STATISTICS_VERSION,
                             .bootstrap_members_per_scope = family.bootstrap_members,
                             .cell_members_per_scope = family.cell_members, .pairs_per_round = pins.pairs,
                             .resamples = pins.resamples, .frozen_before_samples = 1};
    BQ_PREP_CHECK(derived);
    BqRetirementHeldBinaries const* held = &reference->built.binaries;
    char root[] = "/tmp/bq-production-context-XXXXXX";
    char paths[8][96], values[3][24];
    static char const* const names[8] = {"baseline", "candidate", "inputs", "inputs/inputs.json", "inputs/evidence",
                                         "inputs/binaries.record", "inputs/profile", "context"};
    bool ok = derived && mkdtemp(root) != NULL;
    for (u32 index = 0; ok && index < BUSTER_ARRAY_LENGTH(names); index += 1)
    {
        int named = snprintf(paths[index], sizeof(paths[index]), "%s/%s", root, names[index]);
        ok = named > 0 && (size_t)named < sizeof(paths[index]);
    }
    snprintf(values[0], sizeof(values[0]), "%" PRIu64, (uint64_t)pins.seed);
    snprintf(values[1], sizeof(values[1]), "%u", pins.pairs);
    snprintf(values[2], sizeof(values[2]), "%u", pins.resamples);
    char* inputs[] = {"python3", "-W", "error", "tools/bench_service/retirement_binding_context_fixture.py",
                      "--production-inputs", paths[2], fixture->census, paths[0], paths[1], values[0], values[1],
                      values[2], NULL};
    char* generate[] = {"python3", "-W", "error", "tools/bench_service/retirement_binding_context.py", "--inputs",
                        paths[3], "--evidence-root", paths[4], "--census", fixture->census, "--binaries-record",
                        paths[5], "--profile", paths[6], "--output", paths[7], NULL};
    ok = ok && bq_prep_worker_unit_copy_held(held->descriptors[0], paths[0]) &&
         bq_prep_worker_unit_copy_held(held->descriptors[1], paths[1]) && mkdir(paths[2], 0700) == 0 &&
         bq_prep_test_run(inputs) && bq_prep_test_run(generate);
    BQ_PREP_CHECK(ok);
    int directory = ok ? open(root, O_RDONLY | O_DIRECTORY | O_CLOEXEC) : -1;
    u32 length = 0, profile_length = 0, again_length = 0;
    char* bytes = directory >= 0 ? bq_prep_worker_unit_slurp(directory, "context", &length) : NULL;
    char* profile = directory >= 0 ? bq_prep_worker_unit_slurp(directory, "inputs/profile", &profile_length) : NULL;
    /* Deterministic: a second run emits the same bytes. */
    ok = bytes && profile && unlink(paths[7]) == 0 && bq_prep_test_run(generate);
    char* again = ok ? bq_prep_worker_unit_slurp(directory, "context", &again_length) : NULL;
    BQ_PREP_CHECK(again && again_length == length && !memcmp(again, bytes, length));
    for (u32 edit = 0; bytes && profile && edit < 4; edit += 1)
    {
        bool accepted = bq_prep_worker_unit_context_accepted(bytes, length, edit, string_from_pointer(profile), &gate,
                                                             family.sha256, &pins, &plan);
        if (accepted != (edit == 0)) fprintf(stderr, "RETIREMENT_PREP production context edit %u accepted %d\n", edit,
                                             (int)accepted);
        BQ_PREP_CHECK(accepted == (edit == 0));
    }
    if (directory >= 0) close(directory);
    free(again);
    free(profile);
    free(bytes);
    if (derived && access(root, F_OK) == 0) bq_prep_test_cleanup(root);
    bq_retirement_documents_partition_release(&timed);
    bq_retirement_documents_population_release(&population);
}

/* The binding writer's context check (bq_retirement_worker_binding_check)
 * accepts the installed context and refuses, each alone: a pinned support
 * file at another digest, another contract, another candidate or baseline
 * binary, another statistical family or required-row count, another
 * campaign seed, pairs per round or bootstrap member count, and an
 * admission receipt that is not the sentinel or is not execution's first
 * member. A non-canonical section is refused by the parse. */
BUSTER_GLOBAL_LOCAL void bq_prep_worker_unit_binding_refusals(BqPrepOracleFixture* fixture,
    BqRetirementWorkerUnitSeams const* seams, BqRetirementUnitReplayed const* replayed)
{
    BqRetirementCorrectness const* gate = &replayed->gate.correctness;
    String8 profile = seams->profile;
    BqRetirementDocumentPopulation population = {0};
    BqRetirementDocumentPartition timed = {0};
    BqRetirementDocumentFamily family = {0};
    BqRetirementUnitCampaignPins pins = {0};
    TpRetirementPlan plan = {0};
    char support[SHA256_HEX_CAPACITY] = {0}, contract[SHA256_HEX_CAPACITY] = {0};
    int recipes = open(fixture->recipes, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    u32 length = 0;
    char* installed = recipes >= 0 ? bq_prep_worker_unit_slurp(recipes, BQ_RETIREMENT_WORKER_BINDING_CONTEXT_NAME,
                                                               &length) : NULL;
    if (recipes >= 0) close(recipes);
    bool ready = installed && bq_retirement_documents_population(fixture->installed_fd, profile, gate->trusted_rows,
                     gate->prepared.rows, gate->prepared.native_target, &population) == BQ_OK &&
                 bq_retirement_documents_partition(&population, 0, &timed) &&
                 bq_retirement_documents_family(&population, &timed, &family) &&
                 bq_retirement_unit_campaign_pins(profile, &pins) && bq_retirement_unit_campaign_plan(gate, &pins, &plan) &&
                 bq_retirement_profile_sha(profile, S8("support-declaration-sha256="), support) &&
                 bq_retirement_profile_sha(profile, S8("contract-sha256="), contract);
    BQ_PREP_CHECK(ready);
    char const* candidate = gate->prepared.binary_sha256[1];
    char const* baseline = gate->prepared.binary_sha256[0];
    /* Integer fields keep their digit count with the low bit flipped. */
    char seed[32], other_seed[32], rows[48], other_rows[48], pairs[40], other_pairs[40], members[48], other_members[48];
    snprintf(seed, sizeof(seed), "\"seed\":%" PRIu64 ",", (uint64_t)pins.seed);
    snprintf(other_seed, sizeof(other_seed), "\"seed\":%" PRIu64 ",", (uint64_t)pins.seed + 1u);
    snprintf(rows, sizeof(rows), "\"required_row_count\":%u,", gate->prepared.rows);
    snprintf(other_rows, sizeof(other_rows), "\"required_row_count\":%u,", gate->prepared.rows ^ 1u);
    snprintf(pairs, sizeof(pairs), "\"pairs_per_round\":%u,", pins.pairs);
    snprintf(other_pairs, sizeof(other_pairs), "\"pairs_per_round\":%u,", pins.pairs ^ 1u);
    snprintf(members, sizeof(members), "\"bootstrap_members_per_scope\":%u,", plan.bootstrap_members_per_scope);
    snprintf(other_members, sizeof(other_members), "\"bootstrap_members_per_scope\":%u,",
             plan.bootstrap_members_per_scope ^ 1u);
    /* (line key, digest or text to replace, its replacement) */
    char changed[10][80];
    char const* finds[] = {NULL, support, contract, candidate, family.sha256, seed, "\"sha256\":\"0000", baseline,
                           rows, pairs, members};
    char const* lines[] = {NULL, "support=", "contract=", "subjects=", "population=", "rules=", "execution=",
                           "subjects=", "population=", "rules=", "rules="};
    for (u32 index = 0; index < 4; index += 1)
    {
        snprintf(changed[index], sizeof(changed[index]), "%s", finds[index + 1]);
        bq_prep_worker_unit_flip(changed[index]);
    }
    snprintf(changed[4], sizeof(changed[4]), "%s", other_seed);
    snprintf(changed[5], sizeof(changed[5]), "\"sha256\":\"1000");
    snprintf(changed[6], sizeof(changed[6]), "%s", baseline);
    bq_prep_worker_unit_flip(changed[6]);
    snprintf(changed[7], sizeof(changed[7]), "%s", other_rows);
    snprintf(changed[8], sizeof(changed[8]), "%s", other_pairs);
    snprintf(changed[9], sizeof(changed[9]), "%s", other_members);
    for (u32 index = 0; ready && index < BUSTER_ARRAY_LENGTH(finds); index += 1)
    {
        Arena* arena = arena_create((ArenaCreation){.reserved_size = UINT64_C(1) << 30, .flags = {.no_pool = 1}});
        BqRetirementWorkerBindingContext context = {.bytes = malloc(length + 1u), .length = length};
        bool copied = arena && context.bytes;
        if (copied) memcpy(context.bytes, installed, length + 1u);
        char* line = copied && lines[index] ? strstr((char*)context.bytes, lines[index]) : NULL;
        char* at = line ? strstr(line, finds[index]) : NULL;
        char* end = line ? strchr(line, '\n') : NULL;
        copied = copied && (!index || (at && end && at < end));
        if (copied && index) memcpy(at, changed[index - 1], strlen(finds[index]));
        bool accepted = copied && bq_retirement_worker_binding_parse(arena, &context) &&
                        bq_retirement_worker_binding_check(&context, profile, gate, family.sha256, &pins, &plan);
        BQ_PREP_CHECK(copied && accepted == (index == 0));
        bq_retirement_worker_binding_release(&context);
        if (arena) arena_destroy(arena, 1);
    }
    /* A section re-encoded non-canonically (a space after its first colon). */
    Arena* arena = arena_create((ArenaCreation){.reserved_size = UINT64_C(1) << 30, .flags = {.no_pool = 1}});
    BqRetirementWorkerBindingContext spaced = {.bytes = malloc(length + 2u), .length = length + 1u};
    char* colon = spaced.bytes ? strstr(installed, "contract={\"source\":") : NULL;
    bool spaced_ok = arena && colon;
    if (spaced_ok)
    {
        size_t at = (size_t)(colon - installed) + strlen("contract={\"source\":");
        memcpy(spaced.bytes, installed, at);
        spaced.bytes[at] = ' ';
        memcpy(spaced.bytes + at + 1u, installed + at, length - at + 1u);
    }
    BQ_PREP_CHECK(spaced_ok && !bq_retirement_worker_binding_parse(arena, &spaced));
    bq_retirement_worker_binding_release(&spaced);
    if (arena) arena_destroy(arena, 1);
    /* Canonical, with the sentinel once and every receipt field in place, but
     * with another execution key sorted before host: the sentinel is no
     * longer the section's first member, which the writer splices at. */
    static char const leading[] = "\"aaa\":1,";
    arena = arena_create((ArenaCreation){.reserved_size = UINT64_C(1) << 30, .flags = {.no_pool = 1}});
    BqRetirementWorkerBindingContext shifted = {.bytes = malloc(length + sizeof(leading)),
                                                .length = length + (u32)sizeof(leading) - 1u};
    char* section = shifted.bytes ? strstr(installed, "\nexecution={") : NULL;
    bool shifted_ok = arena && section;
    if (shifted_ok)
    {
        size_t at = (size_t)(section - installed) + strlen("\nexecution={");
        memcpy(shifted.bytes, installed, at);
        memcpy(shifted.bytes + at, leading, sizeof(leading) - 1u);
        memcpy(shifted.bytes + at + sizeof(leading) - 1u, installed + at, length - at + 1u);
    }
    BQ_PREP_CHECK(shifted_ok && bq_retirement_worker_binding_parse(arena, &shifted) &&
                  !bq_retirement_worker_binding_check(&shifted, profile, gate, family.sha256, &pins, &plan));
    bq_retirement_worker_binding_release(&shifted);
    if (arena) arena_destroy(arena, 1);
    free(installed);
    bq_retirement_documents_partition_release(&timed);
    bq_retirement_documents_population_release(&population);
}

/* The retirement result's manifest and context chain at finalization: the
 * coordinator's re-formatted manifest refuses a manifest whose ready digest
 * or bundle line was changed (on a fresh, unbound validation), and the
 * journalled authority finalization requires
 * (bq_retirement_coordinator_authority_complete) is refused without its
 * context chain; both pass again restored. The end-to-end finalization is
 * checked once, over the genuine result, by the caller. */
BUSTER_GLOBAL_LOCAL void bq_prep_worker_unit_result_refusals(BqPrepOracleFixture* fixture,
    BqPrepUnitAttempt const* attempt, BqPrepWorkerUnitComposed* composed)
{
    BqQueue* queue = &fixture->queue;
    BqJob const* job = bq_job(&queue->state, attempt->job.id);
    char const* manifest = composed->finalization.recipe.manifest;
    u32 length = 0;
    char* bytes = bq_prep_worker_unit_slurp(composed->result_directory, manifest, &length);
    char const* keys[] = {"\nready-sha256=", "\nbundle-sha256="};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(keys); index += 1)
    {
        char* at = bytes ? strstr(bytes, keys[index]) : NULL;
        bool ok = job && at && fchmodat(composed->result_directory, manifest, 0600, 0) == 0;
        size_t digit = at ? (size_t)(at - bytes) + strlen(keys[index]) : 0;
        char saved = ok ? bytes[digit] : 0;
        if (ok) bytes[digit] = saved == '0' ? '1' : '0';
        int file = ok ? openat(composed->result_directory, manifest, O_WRONLY | O_TRUNC | O_CLOEXEC | O_NOFOLLOW) : -1;
        ok = ok && file >= 0 && bq_write_all(file, (u8 const*)bytes, length);
        if (file >= 0) close(file);
        /* Read-only again, as the producer left it, so only the content
         * differs from the genuine manifest. */
        ok = ok && fchmodat(composed->result_directory, manifest, 0400, 0) == 0;
        BqWorkerFinalization fresh = composed->finalization;
        fresh.result_bound = false;
        BQ_PREP_CHECK(ok && bq_worker_result_validate(&composed->config, job, &fresh) == BQ_CONFIGURATION_MISMATCH);
        if (ok) bytes[digit] = saved;
        ok = ok && fchmodat(composed->result_directory, manifest, 0600, 0) == 0;
        file = ok ? openat(composed->result_directory, manifest, O_WRONLY | O_TRUNC | O_CLOEXEC | O_NOFOLLOW) : -1;
        ok = ok && file >= 0 && bq_write_all(file, (u8 const*)bytes, length);
        if (file >= 0) close(file);
        fresh = composed->finalization;
        BQ_PREP_CHECK(ok && fchmodat(composed->result_directory, manifest, 0400, 0) == 0 &&
                      bq_worker_result_validate(&composed->config, job, &fresh) == BQ_OK);
    }
    free(bytes);
    char authority[256], name[TP_RETIREMENT_STORE_PATH_BYTES + 1];
    bool named = job && bq_prep_worker_unit_authority_path(fixture, job, authority) &&
                 bq_retirement_context_chain_name(name, job->id, job->token);
    int directory = named ? open(authority, O_RDONLY | O_DIRECTORY | O_CLOEXEC) : -1;
    BqWorkerFinalization fresh = composed->finalization;
    BqRetirementWorkerUnitSeams const* seams = fresh.retirement;
    bool moved = directory >= 0 && seams && renameat(directory, name, directory, "moved-chain") == 0;
    /* The journalled authority is refused without its chain (before the
     * derivation, which would also lack the carried values). */
    BQ_PREP_CHECK(moved && bq_retirement_coordinator_authority_complete(composed->result_directory,
                      string_from_pointer(fixture->workspaces), queue->directory_fd, job->id, job->token,
                      seams->profile, attempt->digest, fresh.retirement_ready_sha256,
                      fresh.retirement_authority_sha256, NULL, NULL) == BQ_WORKER_MISMATCH);
    BQ_PREP_CHECK(moved && renameat(directory, "moved-chain", directory, name) == 0 &&
                  bq_retirement_coordinator_authority_complete(composed->result_directory,
                      string_from_pointer(fixture->workspaces), queue->directory_fd, job->id, job->token,
                      seams->profile, attempt->digest, fresh.retirement_ready_sha256,
                      fresh.retirement_authority_sha256, NULL, NULL) == BQ_OK);
    if (directory >= 0) close(directory);
}

/* #881 PR 4, the coordinator's side over the attempt the producer composed.
 * The replay at finalization accepts the digest the channel carried and
 * refuses another digest and a changed record. bq_worker_retirement_finalize
 * (bq_worker_finish's hook) requires the ready digest, the replay, the
 * journalled authority the packet named and (#881 PR 3) its derivation,
 * stops at the execution deadline before or after the replay, reloads every
 * digest from the durable queue records in recovery, and a durable success
 * whose READY record was tampered with is held, never rewritten. The
 * request, finalization and launch gates refuse the recipe under the
 * compiled or no profile and admit it only through the complete seams. */
BUSTER_GLOBAL_LOCAL void bq_prep_worker_unit_coordinator(BqPrepOracleFixture* fixture, BqPrepUnitAttempt const* attempt,
    BqRetirementWorkerUnitSeams const* seams, BqPrepWorkerUnitComposed const* composed)
{
    String8 workspaces = string_from_pointer(fixture->workspaces);
    u64 job = attempt->job.id, token = attempt->job.token;
    char const* channel_digest = composed->finalization.retirement_ready_sha256;
    char const* authority = composed->finalization.retirement_authority_sha256;
    char tampered[SHA256_HEX_CAPACITY], record[80], other_authority[SHA256_HEX_CAPACITY];
    memcpy(tampered, channel_digest, SHA256_HEX_CAPACITY);
    bq_prep_worker_unit_flip(tampered);
    memcpy(other_authority, authority, SHA256_HEX_CAPACITY);
    bq_prep_worker_unit_flip(other_authority);
    snprintf(record, sizeof(record), "ready-%s", channel_digest);
    BQ_PREP_CHECK(bq_retirement_coordinator_replay(seams, workspaces, job, token, attempt->digest, channel_digest, NULL) ==
                  BQ_OK);
    BQ_PREP_CHECK(bq_retirement_coordinator_replay(seams, workspaces, job, token, attempt->digest, tampered, NULL) !=
                  BQ_OK);
    BQ_PREP_CHECK(bq_prep_test_flip_sealed(attempt->attempt, BQ_RETIREMENT_UNIT_READY_DIRECTORY, record, 0500, 0400) &&
                  bq_retirement_coordinator_replay(seams, workspaces, job, token, attempt->digest, channel_digest,
                                                   NULL) != BQ_OK &&
                  bq_prep_test_flip_sealed(attempt->attempt, BQ_RETIREMENT_UNIT_READY_DIRECTORY, record, 0500, 0400) &&
                  bq_retirement_coordinator_replay(seams, workspaces, job, token, attempt->digest, channel_digest,
                                                   NULL) == BQ_OK);
    /* The compiled profile cannot replay even the true record. */
    BqRetirementWorkerUnitSeams installed = bq_retirement_worker_unit_installed();
    installed.installed_root = seams->installed_root;
    installed.broker_workspaces = seams->broker_workspaces;
    BQ_PREP_CHECK(bq_retirement_coordinator_replay(&installed, workspaces, job, token, attempt->digest,
                                                   channel_digest, NULL) != BQ_OK);
    /* The finish hook over the composed result. */
    BqQueue* queue = &fixture->queue;
    BqJob const* queued = bq_job(&queue->state, job);
    BqWorkerBackend clock = {.clock = bq_prep_worker_unit_clock};
    BqWorkerConfig config = {.workspace_root = workspaces, .backend = &clock};
    BqWorkerFinalization finalization = {.config = &config, .result_directory = composed->result_directory,
                                         .retirement = seams};
    memcpy(finalization.retirement_preparation_sha256, attempt->digest, SHA256_HEX_CAPACITY);
    memcpy(finalization.retirement_authority_sha256, authority, SHA256_HEX_CAPACITY);
    bq_prep_worker_unit_clock_mode = 0;
    /* A missing ready digest is reloaded from the durable READY record. */
    BQ_PREP_CHECK(queued && bq_worker_retirement_finalize(queue, queued, &finalization) == BQ_OK &&
                  !strcmp(finalization.retirement_ready_sha256, channel_digest));
    memcpy(finalization.retirement_ready_sha256, tampered, SHA256_HEX_CAPACITY);
    BQ_PREP_CHECK(bq_worker_retirement_finalize(queue, queued, &finalization) != BQ_OK);
    memcpy(finalization.retirement_ready_sha256, channel_digest, SHA256_HEX_CAPACITY);
    BQ_PREP_CHECK(bq_worker_retirement_finalize(queue, queued, &finalization) == BQ_OK);
    /* A changed record fails the replay even with every digest and the
     * authority intact. */
    BQ_PREP_CHECK(bq_prep_test_flip_sealed(attempt->attempt, BQ_RETIREMENT_UNIT_READY_DIRECTORY, record, 0500, 0400) &&
                  bq_worker_retirement_finalize(queue, queued, &finalization) != BQ_OK &&
                  bq_prep_test_flip_sealed(attempt->attempt, BQ_RETIREMENT_UNIT_READY_DIRECTORY, record, 0500, 0400) &&
                  bq_worker_retirement_finalize(queue, queued, &finalization) == BQ_OK);
    /* The authority the packet named must be the journalled one. */
    memcpy(finalization.retirement_authority_sha256, other_authority, SHA256_HEX_CAPACITY);
    BQ_PREP_CHECK(bq_worker_retirement_finalize(queue, queued, &finalization) == BQ_WORKER_MISMATCH);
    memcpy(finalization.retirement_authority_sha256, authority, SHA256_HEX_CAPACITY);
    /* The execution deadline (backend clock) before the replay, which then
     * never runs (a tampered digest would fail it), and after it. */
    finalization.execution_deadline = 1000;
    bq_prep_worker_unit_clock_mode = 1;
    memcpy(finalization.retirement_ready_sha256, tampered, SHA256_HEX_CAPACITY);
    BQ_PREP_CHECK(bq_worker_retirement_finalize(queue, queued, &finalization) == BQ_WORKER_TIMEOUT);
    bq_prep_worker_unit_clock_mode = 2;
    bq_prep_worker_unit_clock_calls = 0;
    memcpy(finalization.retirement_ready_sha256, channel_digest, SHA256_HEX_CAPACITY);
    BQ_PREP_CHECK(bq_worker_retirement_finalize(queue, queued, &finalization) == BQ_WORKER_TIMEOUT);
    finalization.execution_deadline = 0;
    bq_prep_worker_unit_clock_mode = 0;
    BqJob smoke = attempt->job;
    String8 fields[BQ_FIELD_COUNT] = {S8("fixture"), S8("coordinator"), S8("validate-buster-v1"),
                                      bq_field(&attempt->job.request, 3), bq_field(&attempt->job.request, 4)};
    BQ_PREP_CHECK(bq_request_make(fields, &smoke.request) == BQ_OK);
    BqWorkerFinalization plain_smoke = {.config = &config, .result_directory = -1, .retirement = seams};
    BQ_PREP_CHECK(bq_worker_retirement_finalize(queue, &smoke, &plain_smoke) == BQ_OK);
    /* Recovery: a fresh finalization reloads every digest from the durable
     * queue records the coordinator wrote and derives again. */
    BqWorkerFinalization recovered = {.config = &config, .result_directory = composed->result_directory,
                                      .retirement = seams};
    BQ_PREP_CHECK(queued && bq_worker_retirement_finalize(queue, queued, &recovered) == BQ_OK &&
                  !strcmp(recovered.retirement_preparation_sha256, attempt->digest) &&
                  !strcmp(recovered.retirement_ready_sha256, channel_digest) &&
                  !strcmp(recovered.retirement_authority_sha256, authority));
    /* A tampered durable READY record: a success that was already durable is
     * held for reconciliation, never rewritten as a failure. The genuine
     * record is restored afterwards. */
    char name[48];
    u8 genuine[512];
    u32 genuine_bytes = 0;
    BQ_PREP_CHECK(bq_record_name(name, "worker-phase-5", job) &&
                  bq_record_read(queue, name, genuine, sizeof(genuine), &genuine_bytes) == BQ_OK &&
                  unlinkat(queue->directory_fd, name, 0) == 0 &&
                  bq_prep_worker_unit_record(queue, &attempt->job, BQ_PHASE_RETIREMENT_READY, tampered));
    BqJob durable = attempt->job;
    durable.phase = BQ_FINALIZING;
    durable.outcome = BQ_SUCCEEDED;
    BqWorkerConfig production = config;
    production.production_path = true;
    BqWorkerFinalization restarted = {.config = &production, .result_directory = composed->result_directory,
                                      .retirement = seams};
    snprintf(restarted.result_root, sizeof(restarted.result_root), "%s", composed->result_root);
    queue->needs_reconciliation = false;
    BQ_PREP_CHECK(bq_worker_finish(queue, &production, &durable, BQ_SUCCEEDED, BQ_NOT_FOUND, &restarted) != BQ_OK &&
                  bq_failure_evidence(queue, &durable) == BQ_NOT_FOUND && durable.outcome == BQ_SUCCEEDED &&
                  durable.phase == BQ_FINALIZING && queue->needs_reconciliation);
    queue->needs_reconciliation = false;
    BQ_PREP_CHECK(unlinkat(queue->directory_fd, name, 0) == 0 &&
                  bq_record_write(queue, name, genuine, genuine_bytes, false) == BQ_OK);
    /* The gates: the compiled profile, a missing seam and a blocked status
     * refuse; the complete seams admit; the queue's own gate never does. */
    char blocked[4096];
    BQ_PREP_CHECK(bq_prep_worker_unit_status(fixture->profile, "status=blocked\n", blocked, sizeof(blocked)));
    BqRetirementWorkerUnitSeams refused = *seams;
    refused.profile = string_from_pointer(blocked);
    BqRequest const* request = &attempt->job.request;
    BQ_PREP_CHECK(!bq_request_valid(request) && !bq_retirement_request_valid_pinned(request, installed.profile) &&
                  !bq_retirement_request_valid_pinned(request, refused.profile) &&
                  bq_retirement_request_valid_pinned(request, seams->profile) &&
                  bq_retirement_request_valid_pinned(&smoke.request, installed.profile));
    BqRequest malformed = *request;
    malformed.size -= 1;
    BQ_PREP_CHECK(!bq_retirement_request_valid_pinned(&malformed, seams->profile));
    BqRetirementWorkerUnitSeams const* const gated[] = {NULL, &installed, &refused, seams};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(gated); index += 1)
    {
        BqWorkerFinalization gate = {.result_directory = -1, .retirement = gated[index]};
        bool admitted = bq_worker_finalization_recipe(&attempt->job, &gate);
        /* The launch gate on a bound retirement recipe decides by itself. */
        BqWorkerFinalization launch = {.result_directory = -1, .retirement = gated[index]};
        BQ_PREP_CHECK(bq_recipe_files(BQ_RECIPE_NATIVE_RETIREMENT_BLOCKED, &launch.recipe));
        BQ_PREP_CHECK(admitted == (index == 3) && bq_worker_recipe_launchable(&launch) == (index == 3) &&
                      (index != 3 || !strcmp(gate.recipe.name, "native-retirement-performance-v1")));
        BqWorkerFinalization plain = {.result_directory = -1, .retirement = gated[index]};
        BQ_PREP_CHECK(bq_worker_finalization_recipe(&smoke, &plain) && bq_worker_recipe_launchable(&plain));
    }
    /* #881 N1: with the queue's own predicate admitting (queue.c's test seam
     * stands an admitted but pin-less profile in for the compiled one), the
     * gates still decide by the seams' profile alone: the compiled, the
     * stand-in's installed seams and a blocked status refuse; only the
     * complete seams admit. */
    char admitting[BQ_RECIPE_PROFILE_CAP];
    BQ_PREP_CHECK(bq_prep_worker_unit_status((char const*)installed.profile.pointer, "status=admitted\n", admitting,
                                             sizeof(admitting)));
    bq_retirement_profile_test_override = string_from_pointer(admitting);
    BqRetirementWorkerUnitSeams stand_in = bq_retirement_worker_unit_installed();
    BQ_PREP_CHECK(bq_recipe_retirement_admitted() && bq_request_valid(request) &&
                  !bq_retirement_profile_complete(stand_in.profile) &&
                  !bq_retirement_request_valid_pinned(request, stand_in.profile) &&
                  !bq_retirement_request_valid_pinned(request, installed.profile) &&
                  !bq_retirement_request_valid_pinned(request, refused.profile) &&
                  bq_retirement_request_valid_pinned(request, seams->profile));
    BqRetirementWorkerUnitSeams const* const admitting_gated[] = {NULL, &installed, &stand_in, &refused, seams};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(admitting_gated); index += 1)
    {
        bool complete = index == BUSTER_ARRAY_LENGTH(admitting_gated) - 1;
        BqWorkerFinalization gate = {.result_directory = -1, .retirement = admitting_gated[index]};
        BqWorkerFinalization launch = {.result_directory = -1, .retirement = admitting_gated[index]};
        BQ_PREP_CHECK(bq_recipe_files(BQ_RECIPE_NATIVE_RETIREMENT_BLOCKED, &launch.recipe));
        BQ_PREP_CHECK(bq_worker_finalization_recipe(&attempt->job, &gate) == complete &&
                      bq_worker_recipe_launchable(&launch) == complete);
    }
    bq_retirement_profile_test_override = (String8){0};
    BQ_PREP_CHECK(!bq_request_valid(request));
}

/* The composed job's result through the coordinator's remaining gates: the
 * retirement manifest (bq_worker_result_validate), then the queue binding
 * (bq_worker_result_binding_validate_pinned), which admits the retirement
 * recipe only through the complete seams: the unpinned entry, the compiled
 * profile and a blocked status refuse the same bound result. */
BUSTER_GLOBAL_LOCAL void bq_prep_worker_unit_bound(BqPrepOracleFixture* fixture, BqPrepUnitAttempt const* attempt,
    BqRetirementWorkerUnitSeams const* seams, BqPrepWorkerUnitComposed* composed)
{
    BqJob const* queued = bq_job(&fixture->queue.state, attempt->job.id);
    BqWorkerFinalization* finalization = &composed->finalization;
    BQ_PREP_CHECK(queued && bq_worker_result_validate(&composed->config, queued, finalization) == BQ_OK &&
                  finalization->result_bound);
    BqJob bound = queued ? *queued : attempt->job;
    bound.result_bound = true;
    snprintf(bound.result_root, sizeof(bound.result_root), "%s", composed->result_root);
    memcpy(bound.result_manifest_digest, finalization->result_digest, SHA256_HEX_CAPACITY);
    memcpy(bound.result_bundle_digest, finalization->bundle_digest, SHA256_HEX_CAPACITY);
    memcpy(bound.result_full_digest, finalization->full_digest, SHA256_HEX_CAPACITY);
    char blocked[4096];
    BqRetirementWorkerUnitSeams refused = *seams, installed = bq_retirement_worker_unit_installed();
    BQ_PREP_CHECK(bq_prep_worker_unit_status(fixture->profile, "status=blocked\n", blocked, sizeof(blocked)));
    refused.profile = string_from_pointer(blocked);
    int directory = composed->result_directory;
    BQ_PREP_CHECK(bq_worker_result_binding_validate_pinned(&bound, directory, seams) == BQ_OK &&
                  bq_worker_result_binding_validate_at(&bound, directory) == BQ_CONFIGURATION_MISMATCH &&
                  bq_worker_result_binding_validate_pinned(&bound, directory, &installed) == BQ_CONFIGURATION_MISMATCH &&
                  bq_worker_result_binding_validate_pinned(&bound, directory, &refused) == BQ_CONFIGURATION_MISMATCH);
    BqJob other = bound;
    bq_prep_worker_unit_flip(other.result_full_digest);
    BQ_PREP_CHECK(bq_worker_result_binding_validate_pinned(&other, directory, seams) == BQ_CONFIGURATION_MISMATCH);
    /* #881 N1: the queue's predicate admitting does not open the binding for
     * an incomplete or blocked seam profile. */
    char admitting[BQ_RECIPE_PROFILE_CAP];
    BQ_PREP_CHECK(bq_prep_worker_unit_status((char const*)installed.profile.pointer, "status=admitted\n", admitting,
                                             sizeof(admitting)));
    bq_retirement_profile_test_override = string_from_pointer(admitting);
    BqRetirementWorkerUnitSeams stand_in = bq_retirement_worker_unit_installed();
    BQ_PREP_CHECK(bq_recipe_retirement_admitted() &&
                  bq_worker_result_binding_validate_pinned(&bound, directory, &stand_in) == BQ_CONFIGURATION_MISMATCH &&
                  bq_worker_result_binding_validate_pinned(&bound, directory, &installed) == BQ_CONFIGURATION_MISMATCH &&
                  bq_worker_result_binding_validate_pinned(&bound, directory, &refused) == BQ_CONFIGURATION_MISMATCH &&
                  bq_worker_result_binding_validate_pinned(&bound, directory, seams) == BQ_OK);
    bq_retirement_profile_test_override = (String8){0};
}

/* One entry of the queue directory or its retirement-authority/ moved aside
 * (hide) or back. */
BUSTER_GLOBAL_LOCAL bool bq_prep_worker_unit_aside(int directory, char const* name, bool hide)
{
    char aside[80];
    int length = snprintf(aside, sizeof(aside), "aside-%s", name);
    bool ok = length > 0 && (size_t)length < sizeof(aside) &&
              (hide ? renameat(directory, name, directory, aside) : renameat(directory, aside, directory, name)) == 0;
    return ok;
}

/* The records one recovery case leaves (the poison record and any failure
 * record), removed so the next case starts from the composed attempt. */
BUSTER_GLOBAL_LOCAL bool bq_prep_worker_unit_recovery_reset(BqQueue* queue, u64 job)
{
    char poison[48], failure[48];
    bool ok = bq_record_name(poison, BQ_RETIREMENT_POISON_RECORD, job) && bq_record_name(failure, "failure", job) &&
              unlinkat(queue->directory_fd, poison, 0) == 0 &&
              (unlinkat(queue->directory_fd, failure, 0) == 0 || errno == ENOENT);
    return ok;
}

/* One recovery case: which of worker-phase-4 and the journal stay in place,
 * the queue job's phase, outcome and cancellation, the class and store state
 * expected, and the failure record's reason (BQ_NOT_FOUND: none). */
typedef struct BqPrepRecoveryCase
{
    bool measured, journalled, cancelled;
    BqPhase phase;
    BqOutcome outcome;
    BqRetirementHandoffClass handoff;
    TpRetirementAuthorityState state;
    BqError reason;
} BqPrepRecoveryCase;

/* #881 recovery L2 over job 82's composed attempt: the queue's own job,
 * recovered after a reboot (another boot id, so no unit to stop) by
 * bq_worker_recover itself. Each case is classified
 * (bq_retirement_coordinator_handoff_class), poisoned and held twice:
 * recovery returns BQ_RECONCILIATION_REQUIRED with the job's phase and
 * outcome unchanged (never succeeded, a durable success never rewritten),
 * the failure record written only for an uncancelled job without a durable
 * outcome, the queue needing reconciliation and refusing the next
 * reservation, and the attempt workspace and queue-private copy kept:
 * - a handoff that timed out after the copy (no journal, no worker-phase-4);
 * - a COMPLETE handoff whose worker-phase-4 is missing;
 * - a worker-phase-4 over an INCOMPLETE handoff;
 * - the timed-out handoff of a cancelled job;
 * - a durable success (FINALIZING, SUCCEEDED) in the queue without
 *   worker-phase-4, which the reconciliation cannot release while the recipe
 *   is not a real one (BQ_UNSUPPORTED: the queue stays wedged).
 * The poison outlives the restored records: the finalization refuses a
 * durable success until the record is removed. The intact handoff classifies
 * COMPLETE and the handoff with nothing left ABSENT; neither is held. */
BUSTER_GLOBAL_LOCAL void bq_prep_worker_unit_recovery(BqPrepOracleFixture* fixture, BqPrepUnitAttempt const* attempt,
    BqRetirementWorkerUnitSeams const* seams, BqPrepWorkerUnitComposed const* composed)
{
    BqQueue* queue = &fixture->queue;
    u64 id = attempt->job.id, token = attempt->job.token;
    BqJob* job = bq_job(&queue->state, id);
    char measured[48], worker[48], unit[BQ_WORKER_UNIT_CAP], workspace[64];
    char copy[TP_RETIREMENT_STORE_PATH_BYTES + 1], journal[TP_RETIREMENT_STORE_PATH_BYTES + 1];
    char const* digest = composed->finalization.retirement_authority_sha256;
    snprintf(copy, sizeof(copy), "authority-job-%" PRIu64 "-%" PRIu64 ".txt", (uint64_t)id, (uint64_t)token);
    snprintf(journal, sizeof(journal), "authority-job-%" PRIu64 "-%" PRIu64 ".journal", (uint64_t)id, (uint64_t)token);
    int authority = openat(queue->directory_fd, BQ_RETIREMENT_COORDINATOR_QUEUE_AUTHORITY,
                           O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    bool ok = job && job->phase == BQ_MEASURING && authority >= 0 &&
              bq_record_name(measured, "worker-phase-4", id) && bq_record_name(worker, "worker", id) &&
              bq_worker_unit_name(unit, id, token) &&
              bq_workspace_name(workspace, id, token) &&
              bq_worker_record_write(queue, job, "00000000-0000-4000-8000-000000000001", unit) == BQ_OK;
    BQ_PREP_CHECK(ok);
    BqWorkerBackend clock = {.clock = bq_prep_worker_unit_clock};
    BqWorkerConfig config = {.workspace_root = string_from_pointer(fixture->workspaces), .backend = &clock};
    bq_prep_worker_unit_clock_mode = 0;
    BqPrepRecoveryCase const cases[] = {
        {false, false, false, BQ_MEASURING, BQ_NO_OUTCOME, BQ_RETIREMENT_HANDOFF_INCOMPLETE,
         TP_RETIREMENT_AUTHORITY_INCOMPLETE, BQ_BOOT_INTERRUPTED},
        {false, true, false, BQ_MEASURING, BQ_NO_OUTCOME, BQ_RETIREMENT_HANDOFF_INCONSISTENT,
         TP_RETIREMENT_AUTHORITY_COMPLETE, BQ_WORKER_MISMATCH},
        {true, false, false, BQ_MEASURING, BQ_NO_OUTCOME, BQ_RETIREMENT_HANDOFF_INCONSISTENT,
         TP_RETIREMENT_AUTHORITY_INCOMPLETE, BQ_WORKER_MISMATCH},
        {false, false, true, BQ_MEASURING, BQ_NO_OUTCOME, BQ_RETIREMENT_HANDOFF_INCOMPLETE,
         TP_RETIREMENT_AUTHORITY_INCOMPLETE, BQ_NOT_FOUND},
        {false, true, false, BQ_FINALIZING, BQ_SUCCEEDED, BQ_RETIREMENT_HANDOFF_INCONSISTENT,
         TP_RETIREMENT_AUTHORITY_COMPLETE, BQ_NOT_FOUND},
    };
    for (u32 index = 0; ok && index < BUSTER_ARRAY_LENGTH(cases); index += 1)
    {
        BqPrepRecoveryCase const* current = cases + index;
        ok = (current->measured || bq_prep_worker_unit_aside(queue->directory_fd, measured, true)) &&
             (current->journalled || bq_prep_worker_unit_aside(authority, journal, true));
        BQ_PREP_CHECK(ok);
        job->phase = current->phase;
        job->outcome = current->outcome;
        job->cancel_requested = current->cancelled;
        TpRetirementAuthorityState state = TP_RETIREMENT_AUTHORITY_ABSENT;
        BQ_PREP_CHECK(bq_retirement_coordinator_handoff_class(composed->result_directory, queue->directory_fd, id,
                          token, current->measured, current->measured ? digest : "", &state) == current->handoff &&
                      state == current->state);
        /* Recovery twice: the second finds the poison record. */
        for (u32 pass = 0; ok && pass < 2; pass += 1)
        {
            BqWorkerLease lease = {.descriptor = open("/dev/null", O_RDONLY | O_CLOEXEC)};
            BqWorkerFinalization recovered = {.config = &config, .result_directory = dup(composed->result_directory),
                                              .retirement = seams};
            queue->needs_reconciliation = true;
            BqError error = bq_worker_recover(queue, &config, &clock, "/nonexistent/host.lock",
                                              "00000000-0000-4000-8000-000000000002", job, &lease, &recovered);
            job = bq_job(&queue->state, id);
            u64 next = 0, next_token = 0;
            struct stat info = {0};
            BQ_PREP_CHECK(error == BQ_RECONCILIATION_REQUIRED && job && job->phase == current->phase &&
                          job->outcome == current->outcome && queue->state.active_id == id &&
                          queue->needs_reconciliation && recovered.retirement_held &&
                          bq_failure_evidence(queue, job) == current->reason && bq_retirement_poisoned(queue, job));
            BQ_PREP_CHECK(bq_reserve(queue, &next, &next_token) == BQ_RECONCILIATION_REQUIRED && !next &&
                          fstatat(fixture->workspaces_fd, workspace, &info, AT_SYMLINK_NOFOLLOW) == 0 &&
                          fstatat(authority, copy, &info, AT_SYMLINK_NOFOLLOW) == 0);
            if (lease.descriptor >= 0) close(lease.descriptor);
            if (recovered.result_directory >= 0) close(recovered.result_directory);
            ok = job != NULL;
        }
        /* No operator path releases a held retirement job while the recipe
         * is not a real one: the reconciliation refuses and changes nothing. */
        if (ok && current->phase == BQ_FINALIZING)
            BQ_PREP_CHECK(bq_workspace_reconcile(queue, string_from_pointer(fixture->workspaces), id, token) ==
                          BQ_UNSUPPORTED && job->phase == BQ_FINALIZING && job->outcome == BQ_SUCCEEDED &&
                          queue->needs_reconciliation);
        ok = ok && (current->measured || bq_prep_worker_unit_aside(queue->directory_fd, measured, false)) &&
             (current->journalled || bq_prep_worker_unit_aside(authority, journal, false));
        BQ_PREP_CHECK(ok);
        if (job)
        {
            job->phase = BQ_MEASURING;
            job->outcome = BQ_NO_OUTCOME;
            job->cancel_requested = false;
        }
        /* The records are whole again, but the poison stays: no durable
         * success can finalize until the record is gone. */
        BqJob durable = job ? *job : attempt->job;
        durable.phase = BQ_FINALIZING;
        durable.outcome = BQ_SUCCEEDED;
        BqWorkerFinalization restarted = {.config = &config, .result_directory = composed->result_directory,
                                          .retirement = seams};
        BQ_PREP_CHECK(bq_worker_retirement_finalize(queue, &durable, &restarted) == BQ_WORKER_MISMATCH);
        ok = ok && bq_prep_worker_unit_recovery_reset(queue, id);
        BQ_PREP_CHECK(ok);
        /* Once (each replays): without the record the same success finalizes. */
        BqWorkerFinalization clean = {.config = &config, .result_directory = composed->result_directory,
                                      .retirement = seams};
        if (!index) BQ_PREP_CHECK(bq_worker_retirement_finalize(queue, &durable, &clean) == BQ_OK);
    }
    /* The intact handoff, then one that left nothing: neither is held, and
     * nothing is written. */
    for (u32 index = 0; ok && index < 2; index += 1)
    {
        bool emptied = index == 1;
        ok = !emptied || (bq_prep_worker_unit_aside(queue->directory_fd, measured, true) &&
                          bq_prep_worker_unit_aside(authority, journal, true) &&
                          bq_prep_worker_unit_aside(authority, copy, true));
        TpRetirementAuthorityState state = TP_RETIREMENT_AUTHORITY_INVALID;
        BqWorkerFinalization recovered = {.config = &config, .result_directory = composed->result_directory,
                                          .retirement = seams};
        bool held = true;
        BQ_PREP_CHECK(ok && bq_retirement_coordinator_handoff_class(composed->result_directory, queue->directory_fd,
                          id, token, !emptied, emptied ? "" : digest, &state) ==
                      (emptied ? BQ_RETIREMENT_HANDOFF_ABSENT : BQ_RETIREMENT_HANDOFF_COMPLETE) &&
                      state == (emptied ? TP_RETIREMENT_AUTHORITY_ABSENT : TP_RETIREMENT_AUTHORITY_COMPLETE));
        BQ_PREP_CHECK(bq_worker_retirement_handoff_hold(queue, job, &recovered, BQ_BOOT_INTERRUPTED, &held) == BQ_OK &&
                      !held && !recovered.retirement_held && !bq_retirement_poisoned(queue, job) &&
                      bq_failure_evidence(queue, job) == BQ_NOT_FOUND);
        ok = ok && (!emptied || (bq_prep_worker_unit_aside(queue->directory_fd, measured, false) &&
                                 bq_prep_worker_unit_aside(authority, journal, false) &&
                                 bq_prep_worker_unit_aside(authority, copy, false)));
        BQ_PREP_CHECK(ok);
    }
    queue->needs_reconciliation = false;
    BQ_PREP_CHECK(unlinkat(queue->directory_fd, worker, 0) == 0);
    if (authority >= 0) close(authority);
}

/* #881 recovery L2 in the live run: job 82's MEASURED packet delivered again
 * through bq_worker_phase_accept while worker-phase-4 is missing. The
 * coordinator's handoff refuses (its copy and journal already exist), so no
 * record is written and the channel fails, as a handoff that failed after its
 * copy would. The live run then finishes the job failed (bq_worker_finish),
 * which classifies the handoff (COMPLETE without worker-phase-4:
 * inconsistent) and holds it before any transition: poisoned, a
 * worker-mismatch failure record, the job still MEASURING, the workspace and
 * copy kept and the next reservation refused. The run's failure retention
 * (bq_worker_failure_retain) then publishes, binds, advances and records
 * nothing more for it, and, when a failed stop skipped the finish, classifies
 * and holds the job itself before anything is published. */
BUSTER_GLOBAL_LOCAL void bq_prep_worker_unit_live_handoff(BqPrepOracleFixture* fixture,
    BqPrepUnitAttempt const* attempt, BqRetirementWorkerUnitSeams const* seams,
    BqPrepWorkerUnitComposed const* composed)
{
    BqQueue* queue = &fixture->queue;
    u64 id = attempt->job.id, token = attempt->job.token;
    BqJob* job = bq_job(&queue->state, id);
    char measured[48], workspace[64];
    unsigned char digest[BQ_PHASE_DIGEST_BYTES], message[BQ_PHASE_MESSAGE_CAP];
    int pair[2] = {-1, -1};
    BqPhaseChannel channel = {.descriptor = -1, .failed = 1};
    bool ok = job && job->phase == BQ_MEASURING && bq_record_name(measured, "worker-phase-4", id) &&
              bq_workspace_name(workspace, id, token) &&
              bq_phase_digest_parse(composed->finalization.retirement_authority_sha256, digest) &&
              socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair) == 0 &&
              bq_phase_init_version(&channel, pair[0], id, token, BQ_PHASE_VERSION_2) &&
              bq_prep_worker_unit_aside(queue->directory_fd, measured, true);
    channel.sequence = BQ_PHASE_MEASURING;
    ok = ok && bq_phase_make_digest(&channel, BQ_PHASE_MEASURED, digest, message);
    BQ_PREP_CHECK(ok);
    BqWorkerBackend clock = {.clock = bq_prep_worker_unit_clock};
    BqWorkerConfig config = {.workspace_root = string_from_pointer(fixture->workspaces), .backend = &clock};
    BqWorkerFinalization finalization = {.config = &config, .result_directory = composed->result_directory,
                                         .result_device = composed->finalization.result_device,
                                         .result_inode = composed->finalization.result_inode,
                                         .phase_version = BQ_PHASE_VERSION_2, .retirement = seams};
    memcpy(finalization.result_root, composed->finalization.result_root, sizeof(finalization.result_root));
    memcpy(finalization.retirement_preparation_sha256, attempt->digest, SHA256_HEX_CAPACITY);
    memcpy(finalization.retirement_ready_sha256, composed->finalization.retirement_ready_sha256, SHA256_HEX_CAPACITY);
    bq_prep_worker_unit_clock_mode = 0;
    u8 bytes[512];
    u32 size = 0;
    BQ_PREP_CHECK(ok && bq_worker_phase_accept(queue, &channel, message, &finalization) == BQ_WORKER_MISMATCH &&
                  channel.failed && bq_record_read(queue, measured, bytes, sizeof(bytes), &size) == BQ_NOT_FOUND);
    queue->needs_reconciliation = false;
    BqError finished = ok ? bq_worker_finish(queue, &config, job, BQ_FAILED, BQ_WORKER_MISMATCH, &finalization) :
                       BQ_IO;
    job = bq_job(&queue->state, id);
    u64 next = 0, next_token = 0;
    struct stat info = {0};
    bool inconsistent = false;
    BQ_PREP_CHECK(finished == BQ_RECONCILIATION_REQUIRED && finalization.retirement_held && job &&
                  job->phase == BQ_MEASURING && job->outcome == BQ_NO_OUTCOME && queue->needs_reconciliation &&
                  bq_retirement_poison_read(queue, job, &inconsistent) == BQ_OK && inconsistent &&
                  bq_failure_evidence(queue, job) == BQ_WORKER_MISMATCH);
    BQ_PREP_CHECK(bq_reserve(queue, &next, &next_token) == BQ_RECONCILIATION_REQUIRED && !next &&
                  fstatat(fixture->workspaces_fd, workspace, &info, AT_SYMLINK_NOFOLLOW) == 0);
    /* The run's failure retention after that finish: the held job publishes,
     * binds, advances and records nothing more (its failure record stays the
     * hold's). The outcome record would be the first publication. */
    BqRecipeFiles files = {0};
    u64 sequence = queue->state.sequence;
    ok = ok && bq_recipe_files(BQ_RECIPE_NATIVE_RETIREMENT_BLOCKED, &files) &&
         fstatat(composed->result_directory, files.outcome, &info, AT_SYMLINK_NOFOLLOW) != 0 && errno == ENOENT;
    BQ_PREP_CHECK(ok && job &&
                  bq_worker_failure_retain(queue, job, &finalization, BQ_RECONCILIATION_REQUIRED, false, true) ==
                  BQ_RECONCILIATION_REQUIRED && finalization.retirement_held && !job->result_bound &&
                  !finalization.result_bound && job->phase == BQ_MEASURING && queue->state.sequence == sequence &&
                  bq_failure_evidence(queue, job) == BQ_WORKER_MISMATCH && queue->needs_reconciliation &&
                  fstatat(composed->result_directory, files.outcome, &info, AT_SYMLINK_NOFOLLOW) != 0 &&
                  errno == ENOENT);
    /* A failed stop skips the finish: the retention itself classifies and
     * holds the job before anything is published. */
    ok = ok && bq_prep_worker_unit_recovery_reset(queue, id);
    BqWorkerFinalization stopped = finalization;
    stopped.retirement_held = false;
    queue->needs_reconciliation = false;
    BQ_PREP_CHECK(ok && job &&
                  bq_worker_failure_retain(queue, job, &stopped, BQ_CLEANUP_FAILED, false, true) == BQ_CLEANUP_FAILED &&
                  stopped.retirement_held && !job->result_bound && !stopped.result_bound &&
                  job->phase == BQ_MEASURING && queue->state.sequence == sequence &&
                  bq_retirement_poison_read(queue, job, &inconsistent) == BQ_OK && inconsistent &&
                  bq_failure_evidence(queue, job) == BQ_WORKER_MISMATCH && queue->needs_reconciliation &&
                  fstatat(composed->result_directory, files.outcome, &info, AT_SYMLINK_NOFOLLOW) != 0 &&
                  errno == ENOENT);
    queue->needs_reconciliation = false;
    BQ_PREP_CHECK(bq_prep_worker_unit_aside(queue->directory_fd, measured, false) &&
                  bq_prep_worker_unit_recovery_reset(queue, id));
    if (pair[0] >= 0) close(pair[0]);
    if (pair[1] >= 0) close(pair[1]);
}

/* The record generators regenerate the authorities below
 * (retirement_records_tests.h, included after this file). */
BUSTER_GLOBAL_LOCAL void bq_prep_records_test(BqPrepOracleFixture* fixture, BqRetirementProjection const* projection,
    BqJob const* job, BqRetirementRowPlan const* row_plan);

BUSTER_GLOBAL_LOCAL void bq_prep_test_worker_unit(void)
{
    u32 descriptors = bq_prep_test_open_descriptors();
    bq_prep_worker_unit_campaign_order();
    BqPrepOracleFixture* fixture = calloc(1, sizeof(*fixture));
    BqPrepOracleAttempt* reference = calloc(1, sizeof(*reference));
    BqPrepUnitAttempt* attempt = calloc(1, sizeof(*attempt));
    int cancel[2] = {-1, -1};
    bool ok = fixture && reference && attempt && pipe2(cancel, O_CLOEXEC | O_NONBLOCK) == 0 &&
              bq_prep_test_oracle_setup(fixture);
    /* Only a begun attempt holds descriptors (a zeroed one names fd 0). */
    bool begun = ok;
    bool referenced = begun && bq_prep_test_oracle_attempt(fixture, BQ_PREP_WORKER_UNIT_REFERENCE, cancel[0], reference);
    /* The oracle records each reference row's independent output, which the
     * observation must carry. */
    BqRetirementUnitOracle oracle = {0};
    referenced = referenced && bq_retirement_unit_oracle_pinned(&reference->attempt.unit, &reference->projection,
        fixture->workspaces_fd, string_from_pointer(fixture->profile), cancel[0],
        bq_phase_clock() + 300000000000ull, &oracle) == BQ_OK;
    if (oracle.owned) BQ_PREP_CHECK(bq_retirement_unit_oracle_release(&oracle));
    BQ_PREP_CHECK(ok && referenced);
    /* The reference projection's required checks and the campaign
     * authorities, pinned, then the admitting status, which completes the
     * profile. */
    BqRetirementProjection const* projection = referenced ? &reference->projection : NULL;
    BqRetirementRowPlan row_plan = {0};
    BqRetirementRowObserved observed = {0};
    u32 timed_groups = 0;
    if (referenced)
    {
        u32 eligible = 0;
        for (u32 row = 0; row < projection->prepared.rows; row += 1) eligible += projection->rows[row].compiler_eligible;
        BqCheckTestSpec specs[BQ_CHECK_TEST_CHECKS];
        bq_check_test_specs(specs, projection->prepared.object_rows, eligible, projection->prepared.native_target);
        char pins[2048] = {0};
        ok = bq_check_test_install(fixture->recipes, &reference->attempt.unit.preparation, projection, specs,
                                   BQ_CHECK_TEST_CHECKS, 0, pins, sizeof(pins)) &&
             bq_prep_worker_unit_authorities(fixture, projection, &reference->attempt.unit.job, &row_plan, pins,
                                             sizeof(pins), &timed_groups) &&
             bq_prep_worker_unit_composition(fixture, projection, &reference->attempt.unit.job, pins, sizeof(pins)) &&
             strlen(fixture->profile) + strlen(pins) + strlen(BQ_RETIREMENT_PROFILE_ADMITTED_STATUS "\n") <
             sizeof(fixture->profile);
        if (ok)
        {
            strcat(fixture->profile, pins);
            strcat(fixture->profile, BQ_RETIREMENT_PROFILE_ADMITTED_STATUS "\n");
        }
        ok = ok && bq_prep_worker_unit_observe(&row_plan, &observed);
        BQ_PREP_CHECK(ok);
        if (ok)
        {
            bq_prep_records_test(fixture, projection, &reference->attempt.unit.job, &row_plan);
            bq_prep_test_timing("worker-unit-records");
        }
        /* The production generators' round trips, before and independent of
         * job 82. */
        if (ok) bq_prep_worker_unit_generated_checks(fixture, reference, specs, BQ_CHECK_TEST_CHECKS);
        if (ok) bq_prep_worker_unit_production_context(fixture, reference);
        bq_prep_test_timing("worker-unit-generators");
    }
    ok = ok && mkdirat(fixture->workspaces_fd, "results", 0700) == 0;
    BqRetirementWorkerUnitSeams seams = {
        .profile = string_from_pointer(fixture ? fixture->profile : ""), .census_profile = S8("self-test"),
        .installed_root = fixture ? fixture->installed : NULL, .driver = fixture ? fixture->driver : NULL,
        .toolchain_root = fixture ? fixture->toolchain_root : NULL, .broker = fixture ? fixture->broker : NULL,
        .broker_workspaces = fixture ? fixture->workspaces : NULL, .candidate_uid = geteuid(),
        .adapter = bq_prep_worker_unit_self, .supplied = &observed};
    if (ok) bq_prep_worker_unit_profiles(fixture->profile);
    bq_prep_test_timing("worker-unit-setup");

    /* (b) and (a): refused with the compiled profile, then, through the real
     * coordinator path, the ready record, SETTLING, MEASURING, A/A (every
     * runtime launch on the program retained from the stage's own compile),
     * the fixture admission, the post-A/A document, the freeze, A/B, READY,
     * composition, the authority and MEASURED with the complete one (#881
     * PR 3). */
    u64 measuring_ms = 0;
    ok = ok && bq_prep_test_unit_attempt(&fixture->queue, &fixture->job, BQ_PREP_WORKER_UNIT_SUCCESS,
                                         fixture->installed_fd, fixture->workspaces_fd, &fixture->preparation,
                                         fixture->profile, fixture->toolchain_root, attempt);
    BQ_PREP_CHECK(ok);
    if (ok)
    {
        bq_prep_worker_unit_refused(fixture, attempt, &seams);
        observed.job_id = attempt->job.id;
        observed.attempt_token = attempt->job.token;
        BqPrepWorkerUnitComposed* composed = calloc(1, sizeof(*composed));
        u64 started = bq_worker_monotonic_milliseconds();
        if (composed) bq_prep_worker_unit_coordinate(fixture, attempt, &seams, BQ_PREP_WORKER_UNIT_MILLISECONDS, composed);
        bq_prep_test_timing("job-82-measured");
        BqPrepWorkerUnitFailure failure = bq_prep_worker_unit_failure(attempt->attempt);
        if (failure.present)
            fprintf(stderr, "RETIREMENT_PREP worker-unit campaign stopped at stage %lld step %lld sequence %lld "
                    "kind %lld reason %lld launched %lld status %lld exit %lld after %lld purpose %lld result %lld\n",
                    failure.stage, failure.step, failure.sequence, failure.kind, failure.reason, failure.launched,
                    failure.status, failure.exit, failure.after, failure.purpose, failure.result);
        if (composed && (composed->joined != BQ_OK || composed->status != BQ_OK))
            fprintf(stderr, "RETIREMENT_PREP worker-unit job %" PRIu64 " joined %d exited %d\n",
                    (uint64_t)attempt->job.id, (int)composed->joined, composed->status);
        /* MEASURED: the coordinator acknowledged every BQPHASE2 phase in
         * order (each durable record equal to its result-root receipt), kept
         * the ready digest the published record has, and handed off the
         * authority MEASURED named, whose journalled copy is complete; the
         * unit exited 0 with its keeper stopped and no failure retained. */
        BqJob const* queued = composed ? bq_job(&fixture->queue.state, attempt->job.id) : NULL;
        char digest[SHA256_HEX_CAPACITY] = {0};
        BQ_PREP_CHECK(composed && composed->joined == BQ_OK && composed->status == BQ_OK && !failure.present &&
                      composed->phases.sequence == BQ_PHASE_MEASURED && queued && queued->phase == BQ_MEASURING &&
                      bq_worker_phases_validate(&fixture->queue, queued, &composed->finalization) == BQ_OK &&
                      bq_prep_worker_unit_keeper_gone(fixture, &attempt->job) &&
                      bq_prep_worker_unit_ready(attempt->attempt, digest) &&
                      !strcmp(digest, composed->finalization.retirement_ready_sha256) &&
                      composed->finalization.retirement_authority_sha256[0]);
        bool measured = composed && composed->joined == BQ_OK && queued;
        /* The finalization: the replay, the journalled authority and its
         * derivation (M1). */
        BQ_PREP_CHECK(measured && bq_worker_retirement_finalize(&fixture->queue, queued, &composed->finalization) ==
                                  BQ_OK);
        bq_prep_test_timing("job-82-finalized");
        /* The result root: all five documents at exactly the sizes retained
         * before timing, the post-sample record and every stream kind of
         * both stages published unchanged, every composer output, the
         * binding and admission receipt, the 39 evidence files the binding
         * context names, the five worker-phase receipts and the manifest and
         * bundle, and nothing else. */
        BqPrepWorkerUnitResult result = measured ? bq_prep_worker_unit_result(composed->result_root, attempt->attempt) :
                                        (BqPrepWorkerUnitResult){0};
        u32 counts[BQ_PREP_WORKER_UNIT_STREAM_KINDS];
        bool documented = true;
        for (u32 index = 0; index < BQ_RETIREMENT_UNIT_CAMPAIGN_DOCUMENTS; index += 1)
            documented = documented && result.document_bytes[index];
        BQ_PREP_CHECK(documented && bq_prep_worker_unit_sized(attempt->attempt, result.document_bytes));
        BQ_PREP_CHECK(result.record_bytes && result.untimed_records == 1 && result.untimed_metrics == 1u &&
                      result.published_kinds == (1u << BQ_PREP_WORKER_UNIT_STREAM_KINDS) - 1u && !result.unmatched &&
                      result.outputs == (1u << BQ_PREP_WORKER_UNIT_OUTPUTS) - 1u &&
                      result.evidence == BQ_PREP_WORKER_UNIT_EVIDENCE);
        BQ_PREP_CHECK(bq_prep_worker_unit_streams(attempt->attempt, counts) && counts[0] == 1 && counts[1] >= 1 &&
                      counts[2] >= 1 && counts[3] >= 1 && counts[4] >= 1 && counts[5] >= 1 && counts[6] >= 1 &&
                      counts[7] >= 1 && counts[8] >= 1 && counts[9] >= 1);
        /* The post-sample record: the admission, post-A/A and post-sample
         * digests, and every A/A and A/B launch, each stage 2 x (rounds x
         * pairs + warmups) per group and per runtime row (both reference
         * rows run their program). */
        u64 launches[3] = {0}, per_unit = 2u * (TP_RETIREMENT_ROUNDS * 60u + TP_RETIREMENT_WARMUPS);
        BQ_PREP_CHECK(bq_prep_worker_unit_post_sample(attempt->attempt, launches) && launches[0] &&
                      launches[1] == ((u64)timed_groups + BQ_PREP_ORACLE_REFERENCES) * per_unit &&
                      launches[2] == launches[1]);
        /* Every successful runtime launch retired its step directory. */
        BQ_PREP_CHECK(bq_prep_worker_unit_steps_left(attempt->attempt) == 0);
        /* When MEASURING was acknowledged, from the coordinator's record. */
        char record_name[48], body[512] = {0};
        u32 size = 0;
        char const* observed_at = measured && bq_record_name(record_name, "worker-phase-3", attempt->job.id) &&
                                  bq_record_read(&fixture->queue, record_name, (u8*)body, sizeof(body) - 1u, &size) ==
                                  BQ_OK ? strstr(body, "supervisor-observed-monotonic-ns=") : NULL;
        u64 measuring_ns = observed_at ? strtoull(observed_at + 33, NULL, 10) : 0;
        measuring_ms = measuring_ns / 1000000u > started ? measuring_ns / 1000000u - started : 0;
        if (measured)
        {
            /* The coordinator's remaining gates over the composed result, the
             * output the Python checks read, the derivation's refusals, the
             * binding writer's refusals and the result's refusals. */
            bq_prep_worker_unit_bound(fixture, attempt, &seams, composed);
            BQ_PREP_CHECK(bq_prep_worker_unit_export(fixture, composed, &attempt->job));
            bq_prep_test_timing("job-82-bound-exported");
            /* One replay, shared by every derivation and binding refusal. */
            BqRetirementUnitReplayed replayed = {0};
            BQ_PREP_CHECK(bq_retirement_coordinator_replay(&seams, string_from_pointer(fixture->workspaces),
                              attempt->job.id, attempt->job.token, attempt->digest,
                              composed->finalization.retirement_ready_sha256, &replayed) == BQ_OK);
            if (replayed.kept)
            {
                bq_prep_worker_unit_derivation(fixture, attempt, &seams, composed, &replayed);
                bq_prep_test_timing("job-82-derivation");
                bq_prep_worker_unit_binding_refusals(fixture, &seams, &replayed);
                bq_prep_test_timing("job-82-binding-refusals");
                bq_prep_worker_unit_evidence_refusals(fixture, &seams, &reference->attempt.unit.job);
                bq_prep_test_timing("job-82-evidence-refusals");
            }
            BQ_PREP_CHECK(bq_retirement_unit_replayed_release(&replayed));
            bq_prep_worker_unit_evidence_names();
            bq_prep_test_timing("job-82-evidence-names");
            bq_prep_worker_unit_result_refusals(fixture, attempt, composed);
            bq_prep_test_timing("job-82-result-refusals");
            /* #881 PR 4: the coordinator's side over the digests the channel
             * carried. */
            bq_prep_worker_unit_coordinator(fixture, attempt, &seams, composed);
            bq_prep_test_timing("job-82-coordinator");
            /* Recovery L2: incomplete and inconsistent handoffs held. */
            bq_prep_worker_unit_recovery(fixture, attempt, &seams, composed);
            bq_prep_worker_unit_live_handoff(fixture, attempt, &seams, composed);
            bq_prep_test_timing("job-82-recovery");
        }
        if (composed) bq_prep_worker_unit_composed_close(fixture, composed);
        free(composed);
        BQ_PREP_CHECK(bq_prep_test_unit_attempt_close(attempt));
    }

    /* Failure retention inside the campaign: a failing untimed launch
     * (SETTLING, job 85's stand-ins exit 9), SIGTERM during A/A (job 86) and
     * the job deadline expiring during A/A (job 87: no launch fits the time
     * left). Jobs 86 and 87 sleep in every A/A second-label launch, so A/A
     * outlasts both. Job 88's first second-label compile leaves a detached
     * (setsid) sleeper, which the check after that very launch finds (the
     * producer is its subreaper), kills and fails with, long before the
     * campaign's end. */
    u64 const campaign_jobs[] = {BQ_PREP_WORKER_UNIT_UNTIMED_FAILURE, BQ_PREP_WORKER_UNIT_AA_TERMINATE,
                                 BQ_PREP_WORKER_UNIT_AA_DEADLINE, BQ_PREP_WORKER_UNIT_AA_ESCAPE};
    BqPrepWorkerUnitMode const campaign_modes[] = {BQ_PREP_WORKER_UNIT_RUN, BQ_PREP_WORKER_UNIT_TERMINATE_MEASURING,
                                                   BQ_PREP_WORKER_UNIT_RUN, BQ_PREP_WORKER_UNIT_RUN};
    int const campaign_expected[] = {BQ_WORKER_FAILED, BQ_WORKER_CANCEL_SIGNAL, BQ_WORKER_TIMEOUT, BQ_WORKER_FAILED};
    u64 compile_launches = (u64)timed_groups * 2u * (TP_RETIREMENT_ROUNDS * 60u + TP_RETIREMENT_WARMUPS);
    /* Job 87's deadline leaves the success run's time to MEASURING, a margin
     * and one launch timeout: A/A starts and then no launch fits. */
    u64 margin = measuring_ms / 2u > 5000u ? measuring_ms / 2u : 5000u;
    u32 deadline_ms = (u32)(measuring_ms + margin + BQ_PREP_WORKER_UNIT_TIMEOUT_SECONDS * 1000u);
    for (u32 index = 0; ok && measuring_ms && index < BUSTER_ARRAY_LENGTH(campaign_jobs); index += 1)
    {
        bool started = bq_prep_test_unit_attempt(&fixture->queue, &fixture->job, campaign_jobs[index],
                                                 fixture->installed_fd, fixture->workspaces_fd, &fixture->preparation,
                                                 fixture->profile, fixture->toolchain_root, attempt);
        BQ_PREP_CHECK(started);
        observed.job_id = attempt->job.id;
        observed.attempt_token = attempt->job.token;
        BqPrepWorkerUnitRun run = started ? bq_prep_worker_unit_drive(fixture, attempt, &seams, campaign_modes[index],
            campaign_jobs[index] == BQ_PREP_WORKER_UNIT_AA_DEADLINE ? deadline_ms : BQ_PREP_WORKER_UNIT_MILLISECONDS,
            NULL) : (BqPrepWorkerUnitRun){.status = -1};
        BqPrepWorkerUnitFailure failure = {0};
        bq_prep_worker_unit_campaign_checks(fixture, attempt, &seams, &run, campaign_expected[index], index ? 4 : 3,
                                            &failure);
        if (!index)
            BQ_PREP_CHECK(failure.reason == BQ_RETIREMENT_UNIT_CAMPAIGN_STOP_LAUNCH && failure.stage == 0 &&
                          failure.launched == 1 && failure.exit == 9 && failure.sequence == 0 && failure.log_bytes >= 0 &&
                          failure.step == BQ_RETIREMENT_UNIT_CAMPAIGN_SETTLING);
        else if (index == 1)
            BQ_PREP_CHECK(failure.stage == 1 && failure.step == BQ_RETIREMENT_UNIT_CAMPAIGN_BOUND &&
                          (failure.reason == BQ_RETIREMENT_UNIT_CAMPAIGN_STOP_CANCELLED || failure.cancelled == 1));
        else if (index == 2)
            BQ_PREP_CHECK(failure.reason == BQ_RETIREMENT_UNIT_CAMPAIGN_STOP_DEADLINE && failure.stage == 1 &&
                          !failure.launched && failure.step == BQ_RETIREMENT_UNIT_CAMPAIGN_BOUND);
        else
        {
            /* Found after the launch that left it: a finished A/A compiler
             * launch, early in the cursor, and the sleeper is gone. */
            char text[32];
            u32 used = bq_prep_test_read_at(attempt->attempt, BQ_RETIREMENT_WORKER_CAMPAIGN_DIRECTORY
                                                 "/work/escaped.pid", text, sizeof(text));
            pid_t escaped = used ? (pid_t)strtol(text, NULL, 10) : 0;
            bool gone = escaped > 1 && kill(escaped, 0) != 0 && errno == ESRCH;
            if (escaped > 1 && !gone) kill(escaped, SIGKILL);
            BQ_PREP_CHECK(failure.reason == BQ_RETIREMENT_UNIT_CAMPAIGN_STOP_LAUNCH && failure.stage == 1 &&
                          failure.launched == 1 && failure.after == BQ_RETIREMENT_UNIT_CAMPAIGN_AFTER_DESCENDANTS &&
                          failure.status == TP_RETIREMENT_MEASUREMENT_COMPLETE && !failure.kind &&
                          failure.sequence < (long long)compile_launches && gone);
        }
        if (started) BQ_PREP_CHECK(bq_prep_test_unit_attempt_close(attempt));
        char section[32];
        snprintf(section, sizeof(section), "job-%" PRIu64, (uint64_t)campaign_jobs[index]);
        bq_prep_test_timing(section);
    }

    /* (c) and (d): SIGTERM to the unit, a failing stage and a killed
     * producer, each during the build; a detached grandchild left by a
     * generate, which only the producer's subreaper sees (it fails the gate
     * and is swept); and a SIGTERM raised in the unit's teardown window
     * (after a failing stage), which is consumed and reported. */
    u64 const jobs[] = {BQ_PREP_WORKER_UNIT_CANCEL, BQ_PREP_WORKER_UNIT_FAILURE, BQ_PREP_WORKER_UNIT_CRASH,
                        BQ_PREP_WORKER_UNIT_ESCAPE, BQ_PREP_WORKER_UNIT_LATE_TERM};
    BqPrepWorkerUnitMode const modes[] = {BQ_PREP_WORKER_UNIT_TERMINATE, BQ_PREP_WORKER_UNIT_RUN,
                                          BQ_PREP_WORKER_UNIT_KILL_PRODUCER, BQ_PREP_WORKER_UNIT_RUN,
                                          BQ_PREP_WORKER_UNIT_RUN};
    int const expected[] = {BQ_WORKER_CANCEL_SIGNAL, BQ_WORKER_FAILED, BQ_CLEANUP_FAILED, BQ_CLEANUP_FAILED,
                            BQ_WORKER_CANCEL_SIGNAL};
    for (u32 index = 0; ok && index < BUSTER_ARRAY_LENGTH(jobs); index += 1)
    {
        bool started = bq_prep_test_unit_attempt(&fixture->queue, &fixture->job, jobs[index], fixture->installed_fd,
                                                 fixture->workspaces_fd, &fixture->preparation, fixture->profile,
                                                 fixture->toolchain_root, attempt);
        BQ_PREP_CHECK(started);
        observed.job_id = attempt->job.id;
        observed.attempt_token = attempt->job.token;
        bq_retirement_worker_unit_test_late_term = jobs[index] == BQ_PREP_WORKER_UNIT_LATE_TERM;
        BqPrepWorkerUnitRun run = started ? bq_prep_worker_unit_drive(fixture, attempt, &seams, modes[index],
                                                                      BQ_PREP_WORKER_UNIT_MILLISECONDS, NULL) :
                                            (BqPrepWorkerUnitRun){.status = -1};
        bq_retirement_worker_unit_test_late_term = false;
        struct stat info = {0};
        bool staged = modes[index] == BQ_PREP_WORKER_UNIT_RUN || run.driver > 0;
        if (run.status != expected[index]) fprintf(stderr, "RETIREMENT_PREP worker-unit job %" PRIu64 " exited %d\n",
                                                   (uint64_t)jobs[index], run.status);
        BQ_PREP_CHECK(run.status == expected[index] && staged && run.messages == 1 && run.preparing == 1 && run.eof &&
                      bq_prep_worker_unit_keeper_gone(fixture, &attempt->job) &&
                      fstatat(attempt->attempt, BQ_RETIREMENT_UNIT_READY_DIRECTORY, &info, AT_SYMLINK_NOFOLLOW) != 0 &&
                      errno == ENOENT);
        /* The cancelled stage was killed and proven gone by the build. */
        if (modes[index] == BQ_PREP_WORKER_UNIT_TERMINATE)
            BQ_PREP_CHECK(run.driver > 0 && kill(run.driver, 0) != 0 && errno == ESRCH);
        /* The escaped grandchild was reparented to the producer and killed
         * there; one left alive (under init) is stopped here. */
        if (jobs[index] == BQ_PREP_WORKER_UNIT_ESCAPE)
        {
            char path[256], text[32];
            int length = snprintf(path, sizeof(path), "%s/job-73-escaped.pid", fixture->workspaces);
            u32 used = length > 0 && (size_t)length < sizeof(path) ? bq_prep_test_read_text(path, text, sizeof(text)) : 0;
            pid_t escaped = used ? (pid_t)strtol(text, NULL, 10) : 0;
            bool gone = escaped > 1 && kill(escaped, 0) != 0 && errno == ESRCH;
            BQ_PREP_CHECK(gone);
            if (escaped > 1 && !gone) kill(escaped, SIGKILL);
        }
        if (started) BQ_PREP_CHECK(bq_prep_test_unit_attempt_close(attempt));
        char section[32];
        snprintf(section, sizeof(section), "job-%" PRIu64, (uint64_t)jobs[index]);
        bq_prep_test_timing(section);
    }
    bq_retirement_row_observed_release(&observed);
    if (row_plan.owned) BQ_PREP_CHECK(bq_retirement_row_plan_release(&row_plan));
    if (begun) BQ_PREP_CHECK(bq_prep_test_oracle_attempt_close(reference));
    if (fixture) bq_prep_test_oracle_teardown(fixture);
    for (u32 side = 0; side < 2; side += 1)
        if (cancel[side] >= 0) close(cancel[side]);
    free(attempt);
    free(reference);
    free(fixture);
    BQ_PREP_CHECK(bq_prep_test_open_descriptors() == descriptors && bq_prep_test_live_children() == 0);
}

/* A consistent campaign capacity (tp_retirement_campaign_store_preflight's
 * identities) whose payload is `metrics_shards` metrics shards plus seven
 * other payload files: two stages of one transcript shard and two sample
 * shards (one row, one batch) each, and the untimed record. */
#define BQ_PREP_WORKER_PLAN_OTHER_PAYLOAD 7u
BUSTER_GLOBAL_LOCAL TpRetirementCampaignCapacity bq_prep_worker_plan_capacity(u64 metrics_shards)
{
    TpRetirementCampaignCapacity capacity = {0};
    capacity.row_samples_per_stage = 1;
    capacity.sample_shards_per_stage = 2;
    capacity.transcript_shards_per_stage = 1;
    capacity.metrics_shards_per_stage_upper_bound = 1;
    capacity.total_transcript_shards = 2;
    capacity.total_sample_shards = 4;
    capacity.total_metrics_shards_upper_bound = metrics_shards;
    capacity.total_shard_files = 6u + metrics_shards;
    capacity.untimed_record_files = 1;
    capacity.total_payload_files = capacity.total_shard_files + 1u;
    capacity.total_transcript_bytes_upper_bound = 4096;
    capacity.total_sample_bytes_upper_bound = 4096;
    capacity.total_metrics_bytes_upper_bound = 4096;
    capacity.untimed_record_bytes_upper_bound = 4096;
    capacity.total_payload_bytes_upper_bound = 4u * 4096u;
    return capacity;
}

/* Lane E's store plan over a fresh store at root. */
BUSTER_GLOBAL_LOCAL bool bq_prep_worker_plan(int root, TpRetirementStoredFile* files,
    TpRetirementCampaignCapacity const* capacity, TpRetirementComposeLayout const* layout,
    BqRetirementWorkerDeclaration const* declared, u32 evidence_entries, u64 evidence_bytes,
    TpRetirementCampaignStorePlan* plan)
{
    TpRetirementStore store = {.root = -1};
    TpRetirementFamilyCounts family = {0};
    bool ok = tp_retirement_store_open(&store, root, files, TP_RETIREMENT_STORE_FILES) &&
              bq_retirement_worker_store_plan(&store, capacity, layout, 60u, 1u, declared, evidence_entries,
                                              evidence_bytes, plan, &family);
    tp_retirement_store_close(&store);
    return ok;
}

/* The worker campaign's retained declaration and store plan
 * (bq_retirement_worker_declaration, bq_retirement_worker_store_plan): the
 * declaration holds the A/A shard groups (reserved) and D's two constant
 * entries, and bq_retirement_unit_handoff_retained_matches accepts the
 * handoff's entries only field for field at their declared position; the
 * plan reserves the result root's three control files and five BQPHASE2
 * worker-phase-N receipts (BQ_RETIREMENT_WORKER_RESULT_ENTRIES) beside D's
 * five documents, so the last payload file that fits leaves no entry and one
 * more is refused; the binding context's evidence files take one entry each
 * (the same campaign with them no longer fits): the cap's worth is planned
 * and one more is refused. */
BUSTER_GLOBAL_LOCAL void bq_prep_test_worker_store_plan(void)
{
    TpRetirementCampaignCapacity capacity = bq_prep_worker_plan_capacity(1);
    u64 documents[BQ_RETIREMENT_UNIT_CAMPAIGN_DOCUMENTS] = {100, 200, 300, 400, 500};
    BqRetirementWorkerDeclaration declared = {0}, refused = {0};
    BQ_PREP_CHECK(bq_retirement_worker_declaration(&capacity, documents, &declared) &&
                  declared.declaration.retained == declared.retained &&
                  declared.declaration.retained_count == BQ_RETIREMENT_WORKER_RETAINED &&
                  declared.declaration.prior_entries == BQ_RETIREMENT_UNIT_CAMPAIGN_DOCUMENTS &&
                  declared.declaration.prior_bytes == 1500u);
    for (u32 index = 0; index < 4u; index += 1) BQ_PREP_CHECK(declared.retained[index].reserved == 1);
    documents[2] = 0;
    BQ_PREP_CHECK(!bq_retirement_worker_declaration(&capacity, documents, &refused));
    documents[2] = 300;

    /* The retained-declaration equality: D's entries sit after the four
     * shard groups. */
    BqRetirementUnitHandoff* handoff = calloc(1, sizeof(*handoff));
    BQ_PREP_CHECK(handoff != NULL);
    if (handoff)
    {
        for (u32 index = 0; index < BQ_RETIREMENT_UNIT_HANDOFF_RETAINED; index += 1)
            handoff->retained[index] = bq_retirement_unit_handoff_declared[index];
        TpRetirementComposeRetained* retained = declared.retained;
        u32 first = BQ_RETIREMENT_WORKER_RETAINED - BQ_RETIREMENT_UNIT_HANDOFF_RETAINED;
        BQ_PREP_CHECK(bq_retirement_unit_handoff_retained_matches(handoff, retained, BQ_RETIREMENT_WORKER_RETAINED,
                                                                  first));
        BQ_PREP_CHECK(!bq_retirement_unit_handoff_retained_matches(handoff, retained, BQ_RETIREMENT_WORKER_RETAINED,
                                                                   first - 1u) &&
                      !bq_retirement_unit_handoff_retained_matches(handoff, retained,
                                                                   BQ_RETIREMENT_WORKER_RETAINED - 1u, first) &&
                      !bq_retirement_unit_handoff_retained_matches(handoff, retained, BQ_RETIREMENT_WORKER_RETAINED,
                                                                   BQ_RETIREMENT_WORKER_RETAINED + 1u) &&
                      !bq_retirement_unit_handoff_retained_matches(NULL, retained, BQ_RETIREMENT_WORKER_RETAINED,
                                                                   first));
        /* Each field of each entry, changed alone, refuses. */
        for (u32 entry = 0; entry < BQ_RETIREMENT_UNIT_HANDOFF_RETAINED; entry += 1)
        {
            for (u32 field = 0; field < 6u; field += 1)
            {
                TpRetirementComposeRetained saved = retained[first + entry];
                TpRetirementComposeRetained* changed = &retained[first + entry];
                if (field == 0) changed->kind = "unitother";
                else if (field == 1) changed->prefix = "retirement-other-";
                else if (field == 2) changed->suffix = changed->suffix ? NULL : ".txt";
                else if (field == 3) changed->files_max += 1u;
                else if (field == 4) changed->reserved = !changed->reserved;
                else changed->bytes_max += 1u;
                BQ_PREP_CHECK(!bq_retirement_unit_handoff_retained_matches(handoff, retained,
                                                                           BQ_RETIREMENT_WORKER_RETAINED, first));
                retained[first + entry] = saved;
            }
        }
        BQ_PREP_CHECK(bq_retirement_unit_handoff_retained_matches(handoff, retained, BQ_RETIREMENT_WORKER_RETAINED,
                                                                  first));
    }
    free(handoff);

    /* The store plan over a two-row layout (a runtime singleton and an
     * object group). */
    char const* values[TP_RETIREMENT_COMPOSE_DIMENSIONS] = {"x86_64-unknown-linux-gnu", "baseline", "none",
                                                           "direct-ssa", "0", "link"};
    TpRetirementComposeRow rows[2];
    unsigned kinds[2] = {TP_RETIREMENT_GROUP_SINGLETON, TP_RETIREMENT_GROUP_OBJECT};
    for (u32 row = 0; row < 2u; row += 1)
        rows[row] = (TpRetirementComposeRow){row, row, row == 0, {values[0], values[1], values[2], values[3],
                                                                  values[4], row ? "object" : values[5]}};
    TpRetirementComposeLayout layout = {rows, kinds, 2, 2, 2, 0};
    TpRetirementStoredFile* files = calloc(TP_RETIREMENT_STORE_FILES, sizeof(*files));
    char root_path[] = "/tmp/bq-retirement-worker-plan-XXXXXX";
    bool made = files && mkdtemp(root_path) != NULL;
    int root = made ? open(root_path, O_RDONLY | O_DIRECTORY | O_CLOEXEC) : -1;
    TpRetirementCampaignStorePlan plan = {0};
    u64 payload = BQ_PREP_WORKER_PLAN_OTHER_PAYLOAD + 1u;
    bool planned = root >= 3 && bq_prep_worker_plan(root, files, &capacity, &layout, &declared, 0, 0, &plan);
    BQ_PREP_CHECK(planned &&
                  plan.external_entries == BQ_RETIREMENT_WORKER_RESULT_ENTRIES + BQ_RETIREMENT_UNIT_CAMPAIGN_DOCUMENTS &&
                  plan.entries == plan.owned_files + plan.external_entries && plan.entries <= BQ_WORKER_BUNDLE_ENTRY_CAP &&
                  plan.owned_files > payload);
    /* The largest campaign that fits fills the store exactly, the control
     * entries included; one more payload file is refused. */
    u64 control = planned ? plan.owned_files - payload : 0;
    u64 fitting = TP_RETIREMENT_STORE_FILES - control - plan.external_entries - BQ_PREP_WORKER_PLAN_OTHER_PAYLOAD;
    TpRetirementCampaignCapacity full = bq_prep_worker_plan_capacity(fitting);
    TpRetirementCampaignCapacity over = bq_prep_worker_plan_capacity(fitting + 1u);
    BQ_PREP_CHECK(planned && bq_prep_worker_plan(root, files, &full, &layout, &declared, 0, 0, &plan) &&
                  plan.entries == BQ_WORKER_BUNDLE_ENTRY_CAP && plan.remaining_entries == 0);
    BQ_PREP_CHECK(planned && !bq_prep_worker_plan(root, files, &over, &layout, &declared, 0, 0, &plan) &&
                  !plan.entries);
    /* Evidence entries are external entries of the plan: 39 of them (the
     * fixture's) leave room for 39 fewer payload files; exactly
     * BQ_RETIREMENT_WORKER_EVIDENCE_CAP plan and one more refuses. */
    u32 const evidence_counts[] = {39u, BQ_RETIREMENT_WORKER_EVIDENCE_CAP};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(evidence_counts); index += 1)
    {
        u32 count = evidence_counts[index];
        TpRetirementCampaignCapacity evidenced = bq_prep_worker_plan_capacity(fitting - count);
        TpRetirementCampaignCapacity crowded = bq_prep_worker_plan_capacity(fitting - count + 1u);
        BQ_PREP_CHECK(planned &&
                      bq_prep_worker_plan(root, files, &evidenced, &layout, &declared, count, count * 4096u, &plan) &&
                      plan.entries == BQ_WORKER_BUNDLE_ENTRY_CAP &&
                      plan.external_entries ==
                          BQ_RETIREMENT_WORKER_RESULT_ENTRIES + BQ_RETIREMENT_UNIT_CAMPAIGN_DOCUMENTS + count);
        BQ_PREP_CHECK(planned &&
                      !bq_prep_worker_plan(root, files, &crowded, &layout, &declared, count, count * 4096u, &plan));
    }
    BQ_PREP_CHECK(planned && !bq_prep_worker_plan(root, files, &capacity, &layout, &declared,
                                                  BQ_RETIREMENT_WORKER_EVIDENCE_CAP + 1u, 4096u, &plan));
    if (root >= 0) BQ_PREP_CHECK(close(root) == 0);
    if (made) BQ_PREP_CHECK(rmdir(root_path) == 0);
    free(files);
}

#endif
