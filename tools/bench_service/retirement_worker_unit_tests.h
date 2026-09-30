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
 * gone; the failing generate of job 67; and the producer killed during the
 * hanging generate of job 64. Jobs 63, 64 and 67 select those driver
 * behaviours in retirement_matched_build_fixture.c. */
#ifndef BUSTER_BENCH_SERVICE_RETIREMENT_WORKER_UNIT_TESTS_H
#define BUSTER_BENCH_SERVICE_RETIREMENT_WORKER_UNIT_TESTS_H

#define BQ_PREP_WORKER_UNIT_REFERENCE 81u
#define BQ_PREP_WORKER_UNIT_SUCCESS 82u
#define BQ_PREP_WORKER_UNIT_CANCEL 63u
#define BQ_PREP_WORKER_UNIT_CRASH 64u
#define BQ_PREP_WORKER_UNIT_FAILURE 67u
/* The unit's exit status when it kept a child or a descriptor. */
#define BQ_PREP_WORKER_UNIT_UNCLEAN 200
#define BQ_PREP_WORKER_UNIT_MILLISECONDS 300000u

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
} BqPrepWorkerUnitMode;

typedef struct BqPrepWorkerUnitRun
{
    int status;
    u32 messages, preparing;
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
    /* The supervisor side: acknowledge every phase message until EOF. */
    bool acted = mode == BQ_PREP_WORKER_UNIT_RUN, open_channel = ok;
    while (open_channel && bq_worker_monotonic_milliseconds() < deadline)
    {
        struct pollfd waiting = {phase, POLLIN, 0};
        if (poll(&waiting, 1, 50) > 0)
        {
            unsigned char message[BQ_PHASE_MESSAGE_BYTES] = {0};
            ssize_t count = recv(phase, message, sizeof(message), 0);
            open_channel = count == BQ_PHASE_MESSAGE_BYTES;
            run.eof = count == 0;
            run.messages += open_channel;
            run.preparing += open_channel && bq_phase_get(message + 24) == BQ_PHASE_PREPARING;
            bq_phase_put(message + 40, 1u);
            if (open_channel) open_channel = send(phase, message, sizeof(message), MSG_NOSIGNAL) == BQ_PHASE_MESSAGE_BYTES;
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

/* The compiled blocked profile, a blocked status and a missing pin are
 * refused before the lease handoff: no keeper directory, no retirement
 * directory, no child. */
BUSTER_GLOBAL_LOCAL void bq_prep_worker_unit_refused(BqPrepOracleFixture* fixture, BqPrepUnitAttempt const* attempt,
    BqRetirementWorkerUnitSeams const* seams)
{
    char job_text[24], token_text[24], blocked[4096], missing[4096];
    snprintf(job_text, sizeof(job_text), "%" PRIu64, (uint64_t)attempt->job.id);
    snprintf(token_text, sizeof(token_text), "%" PRIu64, (uint64_t)attempt->job.token);
    int length = snprintf(blocked, sizeof(blocked), "%.*sstatus=blocked\n", (int)seams->profile.length,
                          (char const*)seams->profile.pointer);
    char const* pin = strstr(fixture->profile, "row-plan-sha256=");
    char const* end = pin ? strchr(pin, '\n') : NULL;
    int missing_length = end ? snprintf(missing, sizeof(missing), "%.*s%s", (int)(pin - fixture->profile),
                                        fixture->profile, end + 1) : -1;
    BqRetirementWorkerUnitSeams refused[2] = {*seams, *seams};
    refused[0].profile = string_from_pointer(blocked);
    refused[1].profile = string_from_pointer(missing);
    BQ_PREP_CHECK(length > 0 && (size_t)length < sizeof(blocked) && missing_length > 0 &&
                  (size_t)missing_length < sizeof(missing));
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
    char const* statuses[] = {"status=blocked\n", "status=ready\n", "status=ready\nstatus=ready\n"};
    bool expected[] = {false, true, false};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(statuses); index += 1)
    {
        int length = snprintf(changed, sizeof(changed), "%s%s", complete, statuses[index]);
        BQ_PREP_CHECK(length > 0 && (size_t)length < sizeof(changed) &&
                      bq_retirement_profile_complete(string_from_pointer(changed)) == expected[index]);
    }
}

BUSTER_GLOBAL_LOCAL void bq_prep_test_worker_unit(void)
{
    u32 descriptors = bq_prep_test_open_descriptors();
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
             strlen(fixture->profile) + strlen(pin) + strlen(row_pin) + strlen(budget_pin) < sizeof(fixture->profile);
        if (ok)
        {
            strcat(fixture->profile, pin);
            strcat(fixture->profile, row_pin);
            strcat(fixture->profile, budget_pin);
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
        if (run.status != BQ_RETIREMENT_WORKER_UNIT_UNCOMPOSED)
            fprintf(stderr, "RETIREMENT_PREP worker-unit success exited %d after %u messages\n", run.status,
                    run.messages);
        BQ_PREP_CHECK(run.status == BQ_RETIREMENT_WORKER_UNIT_UNCOMPOSED && run.messages == 1 && run.preparing == 1 &&
                      run.eof && bq_prep_worker_unit_keeper_gone(fixture, &attempt->job));
        BQ_PREP_CHECK(bq_prep_worker_unit_ready(attempt->attempt, digest) &&
                      bq_retirement_unit_replay_pinned(attempt->store, fixture->workspaces_fd, fixture->installed_fd,
                          attempt->job.id, attempt->job.token, string_from_pointer(fixture->workspaces), seams.profile,
                          S8("self-test"), fixture->driver, fixture->toolchain_root, fixture->broker,
                          fixture->workspaces, attempt->digest, digest) == BQ_OK);
        BQ_PREP_CHECK(bq_prep_test_unit_attempt_close(attempt));
    }

    /* (c) and (d): SIGTERM to the unit, a failing stage and a killed
     * producer, each during the build. */
    u64 const jobs[] = {BQ_PREP_WORKER_UNIT_CANCEL, BQ_PREP_WORKER_UNIT_FAILURE, BQ_PREP_WORKER_UNIT_CRASH};
    BqPrepWorkerUnitMode const modes[] = {BQ_PREP_WORKER_UNIT_TERMINATE, BQ_PREP_WORKER_UNIT_RUN,
                                          BQ_PREP_WORKER_UNIT_KILL_PRODUCER};
    int const expected[] = {BQ_WORKER_CANCEL_SIGNAL, BQ_WORKER_FAILED, BQ_CLEANUP_FAILED};
    for (u32 index = 0; ok && index < BUSTER_ARRAY_LENGTH(jobs); index += 1)
    {
        bool begun = bq_prep_test_unit_attempt(&fixture->queue, &fixture->job, jobs[index], fixture->installed_fd,
                                               fixture->workspaces_fd, &fixture->preparation, fixture->profile,
                                               fixture->toolchain_root, attempt);
        BQ_PREP_CHECK(begun);
        observed.job_id = attempt->job.id;
        observed.attempt_token = attempt->job.token;
        BqPrepWorkerUnitRun run = begun ? bq_prep_worker_unit_drive(fixture, attempt, &seams, modes[index]) :
                                          (BqPrepWorkerUnitRun){.status = -1};
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
        if (begun) BQ_PREP_CHECK(bq_prep_test_unit_attempt_close(attempt));
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
