/* #881 PR 1 fixture: the retirement producer behind bq_worker_unit.
 * Included by the preparation test runner after retirement_unit_campaign_tests.h.
 *
 * The attempts are the real unit-oracle fixture's (retirement_unit_oracle_tests.h):
 * a reference attempt's projection pins B's stand-in required checks and a
 * row plan (as bq_prep_test_unit_ready does), and the gate takes an honest
 * synthetic observation of that plan through its test seam, re-addressed to
 * each producer attempt (the plan's commands are logical, so only the job and
 * token differ). The test process plays the coordinator
 * (bq_prep_worker_unit_drive): it hands the lease and the phase channel to a
 * forked worker-unit through bq_worker_lease_handoff_send, resumes the paused
 * unit, acknowledges PREPARING and reads the channel to EOF. The unit runs
 * bq_worker_unit_pinned with the fixture's complete profile and roots; it
 * reports its BqError as its exit status, or 200 if it was left with a child
 * (a keeper it did not stop, a producer it did not reap) or a changed
 * descriptor count.
 *
 * Covered: bq_retirement_profile_complete over each pin and the status line;
 * the compiled blocked profile, a blocked status and a missing pin refused
 * before the handoff, with no keeper directory and no retirement directory
 * (bq_prep_worker_unit_refused); the producer through the ready record, which
 * the coordinator replay accepts, ending in the documented not-yet-wired
 * failure (the gate's no-children check passes, so the keeper is not the
 * producer's child); SIGTERM to the unit during the hanging generate of job
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
#include "retirement_coordinator_fixture.h"

#define BQ_PREP_WORKER_UNIT_REFERENCE 81u
#define BQ_PREP_WORKER_UNIT_SUCCESS 82u
#define BQ_PREP_WORKER_UNIT_CANCEL 63u
#define BQ_PREP_WORKER_UNIT_CRASH 64u
#define BQ_PREP_WORKER_UNIT_FAILURE 67u
#define BQ_PREP_WORKER_UNIT_ESCAPE 73u
/* Job 30 (token 40) also fails its generate. */
#define BQ_PREP_WORKER_UNIT_LATE_TERM 30u
/* The unit's exit status when it kept a child or a descriptor. */
#define BQ_PREP_WORKER_UNIT_UNCLEAN 200
#define BQ_PREP_WORKER_UNIT_MILLISECONDS 300000u

typedef enum BqPrepWorkerUnitMode
{
    BQ_PREP_WORKER_UNIT_RUN,
    BQ_PREP_WORKER_UNIT_TERMINATE,
    BQ_PREP_WORKER_UNIT_KILL_PRODUCER,
} BqPrepWorkerUnitMode;

typedef struct BqPrepWorkerUnitRun
{
    int status;
    u32 messages, preparing, ready;
    /* The digest the RETIREMENT_READY packet carried (#881 PR 4). */
    char ready_sha256[SHA256_HEX_CAPACITY];
    bool eof;
    /* The hanging generate's driver and the broker CLI above it. */
    pid_t driver, broker;
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

/* Plays the coordinator for one retirement worker-unit of attempt. mode
 * TERMINATE sends SIGTERM to the unit once the hanging generate recorded its
 * pid; KILL_PRODUCER then kills the producer (the broker CLI's parent). */
BUSTER_GLOBAL_LOCAL BqPrepWorkerUnitRun bq_prep_worker_unit_drive(BqPrepOracleFixture* fixture,
    BqPrepUnitAttempt const* attempt, BqRetirementWorkerUnitSeams const* seams, BqPrepWorkerUnitMode mode)
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
    u64 deadline = bq_worker_monotonic_milliseconds() + BQ_PREP_WORKER_UNIT_MILLISECONDS;
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
            run.messages += open_channel;
            run.preparing += open_channel && bq_phase_get(message + 24) == BQ_PHASE_PREPARING;
            if (open_channel && bq_phase_get(message + 24) == BQ_PHASE_RETIREMENT_READY)
            {
                run.ready += 1;
                bq_phase_digest_format(message + BQ_PHASE_DIGEST_OFFSET, run.ready_sha256);
            }
            bq_phase_put(message + 40, 1u);
            if (open_channel)
                open_channel = send(phase, message, BQ_PHASE_V2_MESSAGE_BYTES, MSG_NOSIGNAL) == BQ_PHASE_V2_MESSAGE_BYTES;
        }
        pid_t driver = acted ? 0 : bq_prep_worker_unit_driver(fixture, &attempt->job);
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

/* #881 PR 4, the coordinator's side over the attempt the producer just ran.
 * The replay at finalization accepts the digest the channel carried and
 * refuses another digest and a changed record. bq_worker_retirement_finalize
 * (bq_worker_finish's hook) requires the ready digest, the replay and the
 * journalled authority the packet named, stops at the execution deadline
 * before or after the replay, reloads every digest from the durable queue
 * records in recovery, and a durable success whose READY record was tampered
 * with is held, never rewritten. The request, finalization and launch gates
 * refuse the recipe under the compiled or no profile and admit it only
 * through the complete seams. */
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
    /* The finish hook over a real handoff: a result directory holding a
     * receipt, the producer's authority and its chain bound to this attempt's
     * A digest and the channel's ready digest, copied and journalled into the
     * queue-private root. */
    char result_root[] = "/tmp/bq-worker-unit-result-XXXXXX", row_plan[SHA256_HEX_CAPACITY] = {0};
    char authority[SHA256_HEX_CAPACITY] = {0}, other_authority[SHA256_HEX_CAPACITY];
    bool made = mkdtemp(result_root) != NULL;
    int result = made ? open(result_root, O_RDONLY | O_DIRECTORY | O_CLOEXEC) : -1;
    BqQueue* queue = &fixture->queue;
    BQ_PREP_CHECK(result >= 0 && bq_retirement_profile_sha(seams->profile, S8("row-plan-sha256="), row_plan) &&
                  bq_coordinator_fixture_authority(result, attempt->attempt, job, token, attempt->digest,
                                                   channel_digest, row_plan, true, authority) &&
                  bq_retirement_coordinator_handoff(result, workspaces, queue->directory_fd, job, token,
                      seams->profile, attempt->digest, channel_digest, authority) == BQ_OK);
    memcpy(other_authority, authority, SHA256_HEX_CAPACITY);
    other_authority[0] = other_authority[0] == '0' ? '1' : '0';
    BqWorkerBackend clock = {.clock = bq_prep_worker_unit_clock};
    BqWorkerConfig config = {.workspace_root = workspaces, .backend = &clock};
    BqWorkerFinalization finalization = {.config = &config, .result_directory = result, .retirement = seams};
    memcpy(finalization.retirement_preparation_sha256, attempt->digest, SHA256_HEX_CAPACITY);
    memcpy(finalization.retirement_authority_sha256, authority, SHA256_HEX_CAPACITY);
    bq_prep_worker_unit_clock_mode = 0;
    /* No durable READY record yet, so the missing digest cannot be reloaded. */
    BQ_PREP_CHECK(bq_worker_retirement_finalize(queue, &attempt->job, &finalization) == BQ_WORKER_MISMATCH);
    memcpy(finalization.retirement_ready_sha256, tampered, SHA256_HEX_CAPACITY);
    BQ_PREP_CHECK(bq_worker_retirement_finalize(queue, &attempt->job, &finalization) != BQ_OK);
    memcpy(finalization.retirement_ready_sha256, channel_digest, SHA256_HEX_CAPACITY);
    BQ_PREP_CHECK(bq_worker_retirement_finalize(queue, &attempt->job, &finalization) == BQ_OK);
    /* A changed record fails the replay even with every digest and the
     * authority intact. */
    BQ_PREP_CHECK(bq_prep_test_flip_sealed(attempt->attempt, BQ_RETIREMENT_UNIT_READY_DIRECTORY, record, 0500, 0400) &&
                  bq_worker_retirement_finalize(queue, &attempt->job, &finalization) != BQ_OK &&
                  bq_prep_test_flip_sealed(attempt->attempt, BQ_RETIREMENT_UNIT_READY_DIRECTORY, record, 0500, 0400) &&
                  bq_worker_retirement_finalize(queue, &attempt->job, &finalization) == BQ_OK);
    /* The authority the packet named must be the journalled one. */
    memcpy(finalization.retirement_authority_sha256, other_authority, SHA256_HEX_CAPACITY);
    BQ_PREP_CHECK(bq_worker_retirement_finalize(queue, &attempt->job, &finalization) == BQ_WORKER_MISMATCH);
    memcpy(finalization.retirement_authority_sha256, authority, SHA256_HEX_CAPACITY);
    /* The execution deadline (backend clock) before the replay, which then
     * never runs (a tampered digest would fail it), and after it. */
    finalization.execution_deadline = 1000;
    bq_prep_worker_unit_clock_mode = 1;
    memcpy(finalization.retirement_ready_sha256, tampered, SHA256_HEX_CAPACITY);
    BQ_PREP_CHECK(bq_worker_retirement_finalize(queue, &attempt->job, &finalization) == BQ_WORKER_TIMEOUT);
    bq_prep_worker_unit_clock_mode = 2;
    bq_prep_worker_unit_clock_calls = 0;
    memcpy(finalization.retirement_ready_sha256, channel_digest, SHA256_HEX_CAPACITY);
    BQ_PREP_CHECK(bq_worker_retirement_finalize(queue, &attempt->job, &finalization) == BQ_WORKER_TIMEOUT);
    finalization.execution_deadline = 0;
    bq_prep_worker_unit_clock_mode = 0;
    BqJob smoke = attempt->job;
    String8 fields[BQ_FIELD_COUNT] = {S8("fixture"), S8("coordinator"), S8("validate-buster-v1"),
                                      bq_field(&attempt->job.request, 3), bq_field(&attempt->job.request, 4)};
    BQ_PREP_CHECK(bq_request_make(fields, &smoke.request) == BQ_OK);
    BqWorkerFinalization plain_smoke = {.config = &config, .result_directory = -1, .retirement = seams};
    BQ_PREP_CHECK(bq_worker_retirement_finalize(queue, &smoke, &plain_smoke) == BQ_OK);
    /* Recovery: a fresh finalization reloads every digest from the durable
     * queue records and replays. */
    BQ_PREP_CHECK(bq_prep_worker_unit_record(queue, &attempt->job, BQ_PHASE_RETIREMENT_READY, channel_digest) &&
                  bq_prep_worker_unit_record(queue, &attempt->job, BQ_PHASE_MEASURED, authority));
    BqWorkerFinalization recovered = {.config = &config, .result_directory = result, .retirement = seams};
    BQ_PREP_CHECK(bq_worker_retirement_finalize(queue, &attempt->job, &recovered) == BQ_OK &&
                  !strcmp(recovered.retirement_preparation_sha256, attempt->digest) &&
                  !strcmp(recovered.retirement_ready_sha256, channel_digest) &&
                  !strcmp(recovered.retirement_authority_sha256, authority));
    /* A tampered durable READY record: a success that was already durable is
     * held for reconciliation, never rewritten as a failure. */
    char name[48];
    BQ_PREP_CHECK(bq_record_name(name, "worker-phase-5", job) && unlinkat(queue->directory_fd, name, 0) == 0 &&
                  bq_prep_worker_unit_record(queue, &attempt->job, BQ_PHASE_RETIREMENT_READY, tampered));
    BqJob durable = attempt->job;
    durable.phase = BQ_FINALIZING;
    durable.outcome = BQ_SUCCEEDED;
    BqWorkerConfig production = config;
    production.production_path = true;
    BqWorkerFinalization restarted = {.config = &production, .result_directory = result, .retirement = seams};
    snprintf(restarted.result_root, sizeof(restarted.result_root), "%s", result_root);
    queue->needs_reconciliation = false;
    BQ_PREP_CHECK(bq_worker_finish(queue, &production, &durable, BQ_SUCCEEDED, BQ_NOT_FOUND, &restarted) != BQ_OK &&
                  bq_failure_evidence(queue, &durable) == BQ_NOT_FOUND && durable.outcome == BQ_SUCCEEDED &&
                  durable.phase == BQ_FINALIZING && queue->needs_reconciliation);
    queue->needs_reconciliation = false;
    if (result >= 0) close(result);
    if (made) bq_prep_test_cleanup(result_root);
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
    /* Exactly one status line, whose value is exactly the admitting one. */
    char const* statuses[] = {"status=admitted\n", "", "status=blocked\n", "status=blocked-pending\n",
                              "status=admitted \n", "status=admitted\r\n", "status=Admitted\n", "status=\n",
                              "status=admitted\nstatus=admitted\n", "status=admitted\nstatus=blocked\n"};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(statuses); index += 1)
        BQ_PREP_CHECK(bq_prep_worker_unit_status(complete, statuses[index], changed, sizeof(changed)) &&
                      bq_retirement_profile_complete(string_from_pointer(changed)) == (index == 0));
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
    /* The reference projection's required checks and row plan, pinned, and
     * the reviewed campaign budget's pin, which completes the profile. */
    BqRetirementProjection const* projection = referenced ? &reference->projection : NULL;
    BqRetirementRowPlan row_plan = {0};
    BqRetirementRowObserved observed = {0};
    char* plan_text = referenced ? malloc(BQ_ROW_TEST_PLAN_CAP) : NULL;
    if (referenced)
    {
        u32 eligible = 0;
        for (u32 row = 0; row < projection->prepared.rows; row += 1) eligible += projection->rows[row].compiler_eligible;
        BqCheckTestSpec specs[BQ_CHECK_TEST_CHECKS];
        bq_check_test_specs(specs, projection->prepared.object_rows, eligible, projection->prepared.native_target);
        char pin[256] = {0}, row_pin[128] = {0}, cpu_model[SHA256_HEX_CAPACITY] = {0};
        char budget_sha256[SHA256_HEX_CAPACITY] = {0}, budget_pin[128] = {0};
        TpRetirementCampaignBudget budget = bq_campaign_service_budget();
        u32 cpu = 0;
        u32 plan_length = plan_text && bq_row_test_cpu(&cpu, cpu_model) ?
                          bq_row_test_plan(plan_text, BQ_ROW_TEST_PLAN_CAP, projection, cpu_model, cpu, 0) : 0;
        int budget_length = tp_retirement_budget_digest(&budget, budget_sha256) ?
                            snprintf(budget_pin, sizeof(budget_pin), "campaign-budget-sha256=%s\n", budget_sha256) : -1;
        ok = bq_check_test_install(fixture->recipes, &reference->attempt.unit.preparation, projection, specs,
                                   BQ_CHECK_TEST_CHECKS, 0, pin, sizeof(pin)) &&
             plan_length && bq_row_test_install(fixture->recipes, plan_text, plan_length, row_pin) &&
             budget_length > 0 && (size_t)budget_length < sizeof(budget_pin) &&
             strlen(fixture->profile) + strlen(pin) + strlen(row_pin) + strlen(budget_pin) +
             strlen(BQ_RETIREMENT_PROFILE_ADMITTED_STATUS "\n") < sizeof(fixture->profile);
        if (ok)
        {
            strcat(fixture->profile, pin);
            strcat(fixture->profile, row_pin);
            strcat(fixture->profile, budget_pin);
            strcat(fixture->profile, BQ_RETIREMENT_PROFILE_ADMITTED_STATUS "\n");
        }
        ok = ok && bq_retirement_row_plan_import_profile(fixture->installed_fd, string_from_pointer(fixture->profile),
                                                         &reference->attempt.unit.job, projection, &row_plan) == BQ_OK &&
             bq_row_test_observe(&row_plan, &observed);
        BQ_PREP_CHECK(ok);
    }
    free(plan_text);
    ok = ok && mkdirat(fixture->workspaces_fd, "results", 0700) == 0;
    BqRetirementWorkerUnitSeams seams = {
        .profile = string_from_pointer(fixture ? fixture->profile : ""), .census_profile = S8("self-test"),
        .installed_root = fixture ? fixture->installed : NULL, .driver = fixture ? fixture->driver : NULL,
        .toolchain_root = fixture ? fixture->toolchain_root : NULL, .broker = fixture ? fixture->broker : NULL,
        .broker_workspaces = fixture ? fixture->workspaces : NULL, .candidate_uid = geteuid(), .supplied = &observed};
    if (ok) bq_prep_worker_unit_profiles(fixture->profile);

    /* (b) and (a): refused with the compiled profile, then through the ready
     * record with the complete one. */
    ok = ok && bq_prep_test_unit_attempt(&fixture->queue, &fixture->job, BQ_PREP_WORKER_UNIT_SUCCESS,
                                         fixture->installed_fd, fixture->workspaces_fd, &fixture->preparation,
                                         fixture->profile, fixture->toolchain_root, attempt);
    BQ_PREP_CHECK(ok);
    if (ok)
    {
        bq_prep_worker_unit_refused(fixture, attempt, &seams);
        observed.job_id = attempt->job.id;
        observed.attempt_token = attempt->job.token;
        BqPrepWorkerUnitRun run = bq_prep_worker_unit_drive(fixture, attempt, &seams, BQ_PREP_WORKER_UNIT_RUN);
        char digest[SHA256_HEX_CAPACITY] = {0};
        if (run.status != BQ_RETIREMENT_WORKER_UNIT_NOT_WIRED)
            fprintf(stderr, "RETIREMENT_PREP worker-unit success exited %d after %u messages\n", run.status,
                    run.messages);
        BQ_PREP_CHECK(run.status == BQ_RETIREMENT_WORKER_UNIT_NOT_WIRED && run.messages == 2 && run.preparing == 1 &&
                      run.ready == 1 && run.eof && bq_prep_worker_unit_keeper_gone(fixture, &attempt->job));
        /* The channel carried exactly the published record's digest. */
        BQ_PREP_CHECK(bq_prep_worker_unit_ready(attempt->attempt, digest) && !strcmp(digest, run.ready_sha256) &&
                      bq_retirement_unit_replay_pinned(attempt->store, fixture->workspaces_fd, fixture->installed_fd,
                          attempt->job.id, attempt->job.token, string_from_pointer(fixture->workspaces), seams.profile,
                          S8("self-test"), fixture->driver, fixture->toolchain_root, fixture->broker,
                          fixture->workspaces, attempt->digest, digest) == BQ_OK);
        bq_prep_worker_unit_coordinator(fixture, attempt, &seams, run.ready_sha256);
        BQ_PREP_CHECK(bq_prep_test_unit_attempt_close(attempt));
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
        BqPrepWorkerUnitRun run = started ? bq_prep_worker_unit_drive(fixture, attempt, &seams, modes[index]) :
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

#endif
