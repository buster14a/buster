/* #881 PR 1 and PR 2 fixture: the retirement producer behind bq_worker_unit.
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
 * sizes, the untimed streams published, attach) and the A/A compiler
 * campaign, stopping at the first A/A runtime launch: lane B's runtime
 * template must be `./{{output}}` (retirement_row_plan.c), which lane D's
 * canonical launch layout refuses (args[0] must be the binary slot), so no
 * fixture reaches A/B or READY (bq_prep_worker_unit_campaign_checks records
 * that stop with its coordinates). Failure retention (campaign-failure.txt)
 * on a failing untimed launch (job 85), a SIGTERM during A/A (job 86) and the
 * deadline expiring during A/A (job 87, whose launches cannot fit the time
 * left), each with the keeper stopped and nothing left running. SIGTERM to
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
 * a flipped record byte and the compiled profile; bq_worker_retirement_replay
 * (bq_worker_finish's hook) needs both digests and passes the smoke recipe;
 * and bq_retirement_request_valid_pinned, bq_worker_finalization_recipe and
 * bq_worker_recipe_launchable admit the recipe only through the complete
 * seams. bq_prep_worker_unit_campaign_order checks that the campaign's
 * SETTLING follows RETIREMENT_READY on a BQPHASE2 channel. */
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
 * campaign's CPU. It is test data, not a #426 decision. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_worker_campaign_fixture_receipt(BqRetirementUnitCampaign const* driver,
    TpRetirementCampaign const* campaign, char receipt[BQ_RETIREMENT_UNIT_CAMPAIGN_AA_RECEIPT_BYTES_MAX],
    u32* length, char digest[SHA256_HEX_CAPACITY])
{
    int written = driver && campaign ? snprintf(receipt, BQ_RETIREMENT_UNIT_CAMPAIGN_AA_RECEIPT_BYTES_MAX,
        "{\"admitted\":true,\"baseline_source_commit\":\"%040d\",\"baseline_source_tree\":\"%040d\","
        "\"family_sha256\":\"%s\",\"lease_protocol\":\"server-authoritative-supervisor-lease-v1\","
        "\"logical_cpu\":%d,\"machine_id\":\"fixture-machine\",\"native_only\":true,"
        "\"native_target\":\"x86_64-unknown-linux-gnu\",\"profile_id\":\"fixture-profile\",\"profile_version\":1,"
        "\"schema\":\"" BQ_RETIREMENT_UNIT_CAMPAIGN_AA_SCHEMA "\",\"service_id\":\"fixture-service\",\"version\":1}",
        1, 2, driver->family_sha256, campaign->cpu) : -1;
    bool ok = written > 0 && written < (int)BQ_RETIREMENT_UNIT_CAMPAIGN_AA_RECEIPT_BYTES_MAX;
    if (ok) bq_digest(receipt, (u32)written, (char8*)digest);
    *length = ok ? (u32)written : 0;
    return ok;
}

typedef enum BqPrepWorkerUnitMode
{
    BQ_PREP_WORKER_UNIT_RUN,
    BQ_PREP_WORKER_UNIT_TERMINATE,
    BQ_PREP_WORKER_UNIT_KILL_PRODUCER,
    /* SIGTERM to the unit a few seconds after MEASURING is acknowledged. */
    BQ_PREP_WORKER_UNIT_TERMINATE_MEASURING,
} BqPrepWorkerUnitMode;

/* The MEASURING-to-SIGTERM delay: past the bind, documents and store plan,
 * inside A/A (whose second-label launches sleep a second for that job). */
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

/* What a finished unit left in its result root (lane E's store root): each
 * workflow document's size (0 when absent), the published untimed records and
 * untimed metrics shards, and every entry. */
typedef struct BqPrepWorkerUnitResult
{
    u64 document_bytes[BQ_RETIREMENT_UNIT_CAMPAIGN_DOCUMENTS];
    u32 untimed_records, untimed_metrics, entries;
} BqPrepWorkerUnitResult;

BUSTER_GLOBAL_LOCAL BqPrepWorkerUnitResult bq_prep_worker_unit_result(char const* result_root)
{
    BqPrepWorkerUnitResult result = {0};
    int directory = open(result_root, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
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
    }
    if (listing) closedir(listing);
    return result;
}

/* Plays the coordinator for one retirement worker-unit of attempt with an
 * execution deadline `milliseconds` after the handoff. mode TERMINATE sends
 * SIGTERM to the unit once the hanging generate recorded its pid;
 * KILL_PRODUCER then kills the producer (the broker CLI's parent);
 * TERMINATE_MEASURING sends SIGTERM to the unit
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
        if (mode == BQ_PREP_WORKER_UNIT_TERMINATE_MEASURING && !acted && run.measuring_ms &&
            bq_worker_monotonic_milliseconds() >= run.measuring_ms + BQ_PREP_WORKER_UNIT_TERMINATE_DELAY_MS)
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
    if (result && ok) *result = bq_prep_worker_unit_result(result_root);
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

/* #881 PR 4, the coordinator's side over the attempt the producer just ran.
 * The replay at finalization accepts the digest the channel carried and
 * refuses another digest and a changed record; bq_worker_retirement_replay
 * (bq_worker_finish's hook) requires the digest and the seams; and the
 * request, finalization and launch gates refuse the recipe under the
 * compiled or no profile and admit it only through the complete seams. */
BUSTER_GLOBAL_LOCAL void bq_prep_worker_unit_coordinator(BqPrepOracleFixture* fixture, BqPrepUnitAttempt const* attempt,
    BqRetirementWorkerUnitSeams const* seams, char const channel_digest[SHA256_HEX_CAPACITY])
{
    String8 workspaces = string_from_pointer(fixture->workspaces);
    u64 job = attempt->job.id, token = attempt->job.token;
    char tampered[SHA256_HEX_CAPACITY], record[80];
    memcpy(tampered, channel_digest, SHA256_HEX_CAPACITY);
    tampered[0] = tampered[0] == '0' ? '1' : '0';
    snprintf(record, sizeof(record), "ready-%s", channel_digest);
    BQ_PREP_CHECK(bq_retirement_coordinator_replay(seams, workspaces, job, token, attempt->digest, channel_digest) ==
                  BQ_OK);
    BQ_PREP_CHECK(bq_retirement_coordinator_replay(seams, workspaces, job, token, attempt->digest, tampered) != BQ_OK);
    BQ_PREP_CHECK(bq_prep_test_flip_sealed(attempt->attempt, BQ_RETIREMENT_UNIT_READY_DIRECTORY, record, 0500, 0400) &&
                  bq_retirement_coordinator_replay(seams, workspaces, job, token, attempt->digest, channel_digest) !=
                      BQ_OK &&
                  bq_prep_test_flip_sealed(attempt->attempt, BQ_RETIREMENT_UNIT_READY_DIRECTORY, record, 0500, 0400) &&
                  bq_retirement_coordinator_replay(seams, workspaces, job, token, attempt->digest, channel_digest) ==
                      BQ_OK);
    /* The compiled profile cannot replay even the true record. */
    BqRetirementWorkerUnitSeams installed = bq_retirement_worker_unit_installed();
    installed.installed_root = seams->installed_root;
    installed.broker_workspaces = seams->broker_workspaces;
    BQ_PREP_CHECK(bq_retirement_coordinator_replay(&installed, workspaces, job, token, attempt->digest,
                                                   channel_digest) != BQ_OK);
    /* The finish hook: only a retirement job is replayed, with both digests. */
    BqWorkerConfig config = {.workspace_root = workspaces};
    BqWorkerFinalization finalization = {.config = &config, .result_directory = -1, .retirement = seams};
    memcpy(finalization.retirement_preparation_sha256, attempt->digest, SHA256_HEX_CAPACITY);
    BQ_PREP_CHECK(bq_worker_retirement_replay(&attempt->job, &finalization) == BQ_WORKER_MISMATCH);
    memcpy(finalization.retirement_ready_sha256, tampered, SHA256_HEX_CAPACITY);
    BQ_PREP_CHECK(bq_worker_retirement_replay(&attempt->job, &finalization) != BQ_OK);
    memcpy(finalization.retirement_ready_sha256, channel_digest, SHA256_HEX_CAPACITY);
    BQ_PREP_CHECK(bq_worker_retirement_replay(&attempt->job, &finalization) == BQ_OK);
    BqJob smoke = attempt->job;
    String8 fields[BQ_FIELD_COUNT] = {S8("fixture"), S8("coordinator"), S8("validate-buster-v1"),
                                      bq_field(&attempt->job.request, 3), bq_field(&attempt->job.request, 4)};
    BQ_PREP_CHECK(bq_request_make(fields, &smoke.request) == BQ_OK);
    finalization.retirement_ready_sha256[0] = 0;
    BQ_PREP_CHECK(bq_worker_retirement_replay(&smoke, &finalization) == BQ_OK);
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
 * have one member) and one more batch template, the untimed contract's
 * (*untimed_template). text is malloc'd. */
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
        native, cpu_model, cpu, 3u + partition->object_groups);
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
    bq_retirement_row_text(text, "rows=%u\n", prepared->rows);
    for (u32 index = 0; text->ok && index < prepared->rows; index += 1)
    {
        BqRetirementTrustedRow const* row = projection->rows + index;
        String8 fixture = bq_retirement_document_value(population, index, BQ_RETIREMENT_DOCUMENT_FIXTURE);
        bool compile = row->compiler_eligible != 0;
        text->ok = !compile || fixture.length;
        bq_retirement_row_text(text, "row=%u %s %s %.*s\n", index,
            bq_retirement_row_timed_object(row, native) ? "batch" : compile ? "0" : "-",
            bq_retirement_row_native_runtime(row, native) ? "1" : "-", compile ? (int)fixture.length : 1,
            compile ? (char const*)fixture.pointer : "-");
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
 * compile's .out, a batch member's .o: the same bytes), with its real
 * artifact digest and, for a row with a code obligation, the code section
 * lane D's reader parses; diagnostics are empty and runtime outputs the
 * independent oracle's (bq_row_test_observe's shape). */
BUSTER_GLOBAL_LOCAL bool bq_prep_worker_unit_observe(BqRetirementRowPlan const* plan, BqRetirementRowObserved* observed)
{
    TpRetirementCodeSide object = {0}, program = {0};
    bool ok = bq_prep_worker_unit_output_side(2, &object) && bq_prep_worker_unit_output_side(3, &program) &&
              !strcmp(object.artifact_sha256, program.artifact_sha256) && bq_row_test_observe(plan, observed);
    char empty[SHA256_HEX_CAPACITY];
    bq_digest("", 0, (char8*)empty);
    for (u32 index = 0; ok && index < plan->row_count; index += 1)
    {
        BqRetirementTrustedRow const* row = plan->completed + index;
        BqRetirementRowPlanRow const* planned = plan->rows + index;
        BqRetirementRowFact* fact = observed->facts + index;
        bool batch = planned->compile == BQ_RETIREMENT_ROW_PLAN_BATCH;
        TpRetirementCodeSide const* output = batch ? &object : &program;
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
    long long result, reason, step, stage, launched, kind, status, exit, cancelled, sequence, log_bytes;
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
                                       "\nstatus=", "\nexit=", "\ncancelled=", "\nsequence=", "\nlog-bytes="};
    long long* values[] = {&failure.result, &failure.reason, &failure.step, &failure.stage, &failure.launched,
                           &failure.kind, &failure.status, &failure.exit, &failure.cancelled, &failure.sequence,
                           &failure.log_bytes};
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

/* The staged streams of attempt: each kind's files numbered contiguously
 * from 0000 (zero-padded, so their names sort in index order), counts[kind]
 * receiving how many: 0 untimed records, 1 untimed metrics, then per stage
 * transcripts, metrics, row samples and batch samples (A/A 2..5, A/B 6..9). */
#define BQ_PREP_WORKER_UNIT_STREAM_KINDS 10u
BUSTER_GLOBAL_LOCAL bool bq_prep_worker_unit_streams(int attempt, u32 counts[BQ_PREP_WORKER_UNIT_STREAM_KINDS])
{
    static char const* const prefixes[BQ_PREP_WORKER_UNIT_STREAM_KINDS] = {BQ_RETIREMENT_WORKER_UNTIMED_PATH,
        "retirement-metrics-untimed-", "retirement-execution-aa-", "retirement-metrics-aa-", "retirement-samples-aa-",
        "retirement-batches-aa-", "retirement-execution-ab-", "retirement-metrics-ab-", "retirement-samples-0",
        "retirement-batches-0"};
    static char const* const suffixes[BQ_PREP_WORKER_UNIT_STREAM_KINDS] = {"", ".txt", ".jsonl", ".txt", ".jsonl",
        ".jsonl", ".jsonl", ".txt", ".jsonl", ".jsonl"};
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
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        u32 kind = BQ_PREP_WORKER_UNIT_STREAM_KINDS;
        for (u32 index = 0; kind == BQ_PREP_WORKER_UNIT_STREAM_KINDS && index < BQ_PREP_WORKER_UNIT_STREAM_KINDS;
             index += 1)
            if (!strncmp(entry->d_name, prefixes[index], strlen(prefixes[index]))) kind = index;
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
    BQ_PREP_CHECK(failure->present && failure->result == status);
}

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
        char pins[1024] = {0};
        ok = bq_check_test_install(fixture->recipes, &reference->attempt.unit.preparation, projection, specs,
                                   BQ_CHECK_TEST_CHECKS, 0, pins, sizeof(pins)) &&
             bq_prep_worker_unit_authorities(fixture, projection, &reference->attempt.unit.job, &row_plan, pins,
                                             sizeof(pins), &timed_groups) &&
             strlen(fixture->profile) + strlen(pins) + strlen(BQ_RETIREMENT_PROFILE_ADMITTED_STATUS "\n") <
             sizeof(fixture->profile);
        if (ok)
        {
            strcat(fixture->profile, pins);
            strcat(fixture->profile, BQ_RETIREMENT_PROFILE_ADMITTED_STATUS "\n");
        }
        ok = ok && bq_prep_worker_unit_observe(&row_plan, &observed);
        BQ_PREP_CHECK(ok);
    }
    ok = ok && mkdirat(fixture->workspaces_fd, "results", 0700) == 0;
    BqRetirementWorkerUnitSeams seams = {
        .profile = string_from_pointer(fixture ? fixture->profile : ""), .census_profile = S8("self-test"),
        .installed_root = fixture ? fixture->installed : NULL, .driver = fixture ? fixture->driver : NULL,
        .toolchain_root = fixture ? fixture->toolchain_root : NULL, .broker = fixture ? fixture->broker : NULL,
        .broker_workspaces = fixture ? fixture->workspaces : NULL, .candidate_uid = geteuid(), .supplied = &observed};
    if (ok) bq_prep_worker_unit_profiles(fixture->profile);

    /* (b) and (a): refused with the compiled profile, then through the ready
     * record, SETTLING and MEASURING into A/A with the complete one. The
     * A/A compiler campaign runs every group's 2 x (2 x pairs + warmups)
     * launches per round; its first runtime launch is where it stops. */
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
        BqPrepWorkerUnitResult result = {0};
        BqPrepWorkerUnitRun run = bq_prep_worker_unit_drive(fixture, attempt, &seams, BQ_PREP_WORKER_UNIT_RUN,
                                                            BQ_PREP_WORKER_UNIT_MILLISECONDS, &result);
        BqPrepWorkerUnitFailure failure = {0};
        bq_prep_worker_unit_campaign_checks(fixture, attempt, &seams, &run, BQ_WORKER_FAILED, 4, &failure);
        u64 compiles = (u64)timed_groups * 2u * (TP_RETIREMENT_ROUNDS * 60u + TP_RETIREMENT_WARMUPS);
        if (failure.sequence != (long long)compiles || failure.stage != 1)
            fprintf(stderr, "RETIREMENT_PREP worker-unit campaign stopped at stage %lld step %lld sequence %lld "
                    "(expected %" PRIu64 ") kind %lld reason %lld launched %lld status %lld exit %lld\n", failure.stage,
                    failure.step, failure.sequence, (uint64_t)compiles, failure.kind, failure.reason, failure.launched,
                    failure.status, failure.exit);
        /* Every A/A compiler launch ran; the first runtime launch was refused
         * before any child (lane B's ./{{output}} runtime argv is not lane
         * D's binary slot). */
        BQ_PREP_CHECK(failure.stage == 1 && failure.kind == 1 && !failure.launched &&
                      failure.status == TP_RETIREMENT_MEASUREMENT_PLAN_INVALID && failure.sequence == (long long)compiles &&
                      failure.step == BQ_RETIREMENT_UNIT_CAMPAIGN_BOUND);
        /* The four pre-A/A documents in the result store, the untimed streams
         * published into it, and every stream staged in index order. */
        u32 counts[BQ_PREP_WORKER_UNIT_STREAM_KINDS];
        BQ_PREP_CHECK(result.document_bytes[BQ_RETIREMENT_UNIT_CAMPAIGN_ORACLE] &&
                      result.document_bytes[BQ_RETIREMENT_UNIT_CAMPAIGN_EXECUTION_PLAN] &&
                      result.document_bytes[BQ_RETIREMENT_UNIT_CAMPAIGN_RESULT_INPUT_PLAN] &&
                      result.document_bytes[BQ_RETIREMENT_UNIT_CAMPAIGN_PRE_SAMPLE] &&
                      !result.document_bytes[BQ_RETIREMENT_UNIT_CAMPAIGN_POST_AA] && result.untimed_records == 1 &&
                      result.untimed_metrics == 1u && result.entries == 6);
        BQ_PREP_CHECK(bq_prep_worker_unit_streams(attempt->attempt, counts) && counts[0] == 1 && counts[1] >= 1 &&
                      counts[2] >= 1 && counts[3] >= 1 && counts[4] >= 1 && counts[5] >= 1 && counts[6] >= 1 &&
                      counts[7] >= 1 && counts[8] >= 1 && counts[9] >= 1);
        measuring_ms = run.measuring_ms > run.started_ms ? run.measuring_ms - run.started_ms : 0;
        /* #881 PR 4: the coordinator's side over the ready digest the
         * channel carried. */
        bq_prep_worker_unit_coordinator(fixture, attempt, &seams, run.ready_sha256);
        BQ_PREP_CHECK(bq_prep_test_unit_attempt_close(attempt));
    }

    /* Failure retention inside the campaign: a failing untimed launch
     * (SETTLING, job 85's stand-ins exit 9), SIGTERM during A/A (job 86) and
     * the job deadline expiring during A/A (job 87: no launch fits the time
     * left). Jobs 86 and 87 sleep in every A/A second-label launch, so A/A
     * outlasts both. */
    u64 const campaign_jobs[] = {BQ_PREP_WORKER_UNIT_UNTIMED_FAILURE, BQ_PREP_WORKER_UNIT_AA_TERMINATE,
                                 BQ_PREP_WORKER_UNIT_AA_DEADLINE};
    BqPrepWorkerUnitMode const campaign_modes[] = {BQ_PREP_WORKER_UNIT_RUN, BQ_PREP_WORKER_UNIT_TERMINATE_MEASURING,
                                                   BQ_PREP_WORKER_UNIT_RUN};
    int const campaign_expected[] = {BQ_WORKER_FAILED, BQ_WORKER_CANCEL_SIGNAL, BQ_WORKER_TIMEOUT};
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
        else
            BQ_PREP_CHECK(failure.reason == BQ_RETIREMENT_UNIT_CAMPAIGN_STOP_DEADLINE && failure.stage == 1 &&
                          !failure.launched && failure.step == BQ_RETIREMENT_UNIT_CAMPAIGN_BOUND);
        if (started) BQ_PREP_CHECK(bq_prep_test_unit_attempt_close(attempt));
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
    BqRetirementWorkerDeclaration const* declared, TpRetirementCampaignStorePlan* plan)
{
    TpRetirementStore store = {.root = -1};
    TpRetirementFamilyCounts family = {0};
    bool ok = tp_retirement_store_open(&store, root, files, TP_RETIREMENT_STORE_FILES) &&
              bq_retirement_worker_store_plan(&store, capacity, layout, 60u, 1u, declared, plan, &family);
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
 * more is refused. */
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
    bool planned = root >= 3 && bq_prep_worker_plan(root, files, &capacity, &layout, &declared, &plan);
    BQ_PREP_CHECK(planned &&
                  plan.external_entries == 3u + 5u + BQ_RETIREMENT_UNIT_CAMPAIGN_DOCUMENTS &&
                  plan.entries == plan.owned_files + plan.external_entries && plan.entries <= BQ_WORKER_BUNDLE_ENTRY_CAP &&
                  plan.owned_files > payload);
    /* The largest campaign that fits fills the store exactly, the control
     * entries included; one more payload file is refused. */
    u64 control = planned ? plan.owned_files - payload : 0;
    u64 fitting = TP_RETIREMENT_STORE_FILES - control - plan.external_entries - BQ_PREP_WORKER_PLAN_OTHER_PAYLOAD;
    TpRetirementCampaignCapacity full = bq_prep_worker_plan_capacity(fitting);
    TpRetirementCampaignCapacity over = bq_prep_worker_plan_capacity(fitting + 1u);
    BQ_PREP_CHECK(planned && bq_prep_worker_plan(root, files, &full, &layout, &declared, &plan) &&
                  plan.entries == BQ_WORKER_BUNDLE_ENTRY_CAP && plan.remaining_entries == 0);
    BQ_PREP_CHECK(planned && !bq_prep_worker_plan(root, files, &over, &layout, &declared, &plan) && !plan.entries);
    if (root >= 0) BQ_PREP_CHECK(close(root) == 0);
    if (made) BQ_PREP_CHECK(rmdir(root_path) == 0);
    free(files);
}

#endif
