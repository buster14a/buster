/* Linux-only deadline fixtures, included by tests.c.
 * bq_test_worker_deadlines checks capture/launcher deadlines and owned cleanup.
 * bq_test_worker_group_reaping retains live and zombie group members explicitly;
 * no verdict depends on how promptly PID 1 reaps an orphaned shell descendant.
 */
#define BQ_TEST_WORKER_TIMEOUT_MILLISECONDS 20u
#define BQ_TEST_WORKER_BOUND_MILLISECONDS 1000u

BUSTER_GLOBAL_LOCAL bool bq_test_worker_probe_locked(char const* path);

BUSTER_GLOBAL_LOCAL volatile sig_atomic_t bq_test_alarm_count;
BUSTER_GLOBAL_LOCAL volatile sig_atomic_t bq_test_worker_cancel_delivery_count;

BUSTER_GLOBAL_LOCAL void bq_test_alarm_handler(int signal_number)
{
    (void)signal_number;
    bq_test_alarm_count += 1;
}

BUSTER_GLOBAL_LOCAL void bq_test_worker_cancel_delivery_handler(int signal_number)
{
    (void)signal_number;
    bq_test_worker_cancel_delivery_count += 1;
}

BUSTER_GLOBAL_LOCAL void bq_test_worker_pending_cancel_delivery(void)
{
    struct sigaction action = {0}, prior_term = {0}, prior_interrupt = {0};
    sigset_t both, prior_mask;
    sigemptyset(&action.sa_mask);
    action.sa_handler = bq_test_worker_cancel_delivery_handler;
    sigemptyset(&both);
    sigaddset(&both, SIGTERM);
    sigaddset(&both, SIGINT);
    bool mask_saved = sigprocmask(SIG_SETMASK, NULL, &prior_mask) == 0;
    bool term_installed = sigaction(SIGTERM, &action, &prior_term) == 0;
    bool interrupt_installed = term_installed && sigaction(SIGINT, &action, &prior_interrupt) == 0;
    bool ready = mask_saved && interrupt_installed && sigprocmask(SIG_UNBLOCK, &both, NULL) == 0;
    BQ_CHECK(ready);
    if (ready)
    {
        int signals[] = {SIGTERM, SIGINT};
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(signals); index += 1)
        {
            sigset_t one, pending, remaining;
            sigemptyset(&one);
            bool blocked = sigaddset(&one, signals[index]) == 0 && sigprocmask(SIG_BLOCK, &one, NULL) == 0;
            bq_worker_cancel_signal = 0;
            bq_worker_shutdown_signal = 0;
            bq_test_worker_cancel_delivery_count = 0;
            bool raised = blocked && raise(signals[index]) == 0 && sigpending(&pending) == 0 &&
                          sigismember(&pending, signals[index]) == 1;
            BQ_CHECK(raised);
            BqError consumed = raised ? bq_worker_consume_pending_cancel(&pending) : BQ_IO;
            bool drained = sigpending(&remaining) == 0 && sigismember(&remaining, signals[index]) == 0;
            BQ_CHECK(consumed == BQ_OK && drained && bq_worker_cancel_signal && bq_worker_shutdown_signal);
            bool unblocked = sigprocmask(SIG_UNBLOCK, &one, NULL) == 0;
            BQ_CHECK(unblocked && bq_test_worker_cancel_delivery_count == 0);
        }
    }
    if (mask_saved) BQ_CHECK(sigprocmask(SIG_SETMASK, &prior_mask, NULL) == 0);
    if (interrupt_installed) BQ_CHECK(sigaction(SIGINT, &prior_interrupt, NULL) == 0);
    if (term_installed) BQ_CHECK(sigaction(SIGTERM, &prior_term, NULL) == 0);
    bq_worker_cancel_signal = 0;
    bq_worker_shutdown_signal = 0;
}

BUSTER_GLOBAL_LOCAL void bq_test_worker_coordinator_lease_continuity(void)
{
    char root[] = "/tmp/buster-coordinator-lease-XXXXXX";
    char lease_path[128] = {0};
    int pair[2] = {-1, -1};
    int received = -1;
    int path_length = -1;
    BqWorkerLease coordinator = {.descriptor = -1};
    bool root_created = mkdtemp(root) != NULL;
    bool rooted = root_created;
    BQ_CHECK(root_created);
    if (rooted)
    {
        path_length = snprintf(lease_path, sizeof(lease_path), "%s/host.lock", root);
        rooted = path_length > 0 && (size_t)path_length < sizeof(lease_path) &&
                 bq_worker_lease_acquire(lease_path, &coordinator) == 0;
    }
    bool paired = rooted && socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair) == 0;
    BQ_CHECK(paired);
    if (paired)
    {
        char marker = 1;
        char sent_control[CMSG_SPACE(sizeof(int))] = {0};
        struct iovec sent_vector = {&marker, sizeof(marker)};
        struct msghdr sent_message = {0};
        sent_message.msg_iov = &sent_vector;
        sent_message.msg_iovlen = 1;
        sent_message.msg_control = sent_control;
        sent_message.msg_controllen = sizeof(sent_control);
        struct cmsghdr* sent_header = CMSG_FIRSTHDR(&sent_message);
        bool header_ready = sent_header != NULL;
        if (header_ready)
        {
            sent_header->cmsg_level = SOL_SOCKET;
            sent_header->cmsg_type = SCM_RIGHTS;
            sent_header->cmsg_len = CMSG_LEN(sizeof(coordinator.descriptor));
            memcpy(CMSG_DATA(sent_header), &coordinator.descriptor, sizeof(coordinator.descriptor));
        }
        ssize_t sent = header_ready ? sendmsg(pair[0], &sent_message, MSG_NOSIGNAL) : -1;
        char received_control[CMSG_SPACE(sizeof(int))] = {0};
        char received_marker = 0;
        struct iovec received_vector = {&received_marker, sizeof(received_marker)};
        struct msghdr received_message = {0};
        received_message.msg_iov = &received_vector;
        received_message.msg_iovlen = 1;
        received_message.msg_control = received_control;
        received_message.msg_controllen = sizeof(received_control);
        ssize_t count = sent == sizeof(marker) ? recvmsg(pair[1], &received_message, MSG_CMSG_CLOEXEC) : -1;
        bool transferred = count == sizeof(received_marker) &&
                           !(received_message.msg_flags & (MSG_TRUNC | MSG_CTRUNC)) && received_marker == marker;
        for (struct cmsghdr* header = count >= 0 ? CMSG_FIRSTHDR(&received_message) : NULL;
             header; header = CMSG_NXTHDR(&received_message, header))
        {
            if (header->cmsg_level == SOL_SOCKET && header->cmsg_type == SCM_RIGHTS &&
                header->cmsg_len == CMSG_LEN(sizeof(received)) && received < 0)
            {
                memcpy(&received, CMSG_DATA(header), sizeof(received));
            }
            else transferred = false;
        }
        transferred = transferred && received >= 3;
        BQ_CHECK(transferred && bq_test_worker_probe_locked(lease_path));
        if (received >= 0)
        {
            close(received);
            received = -1;
        }
        /* The transient worker has closed its received descriptor. The
         * coordinator's copy must still exclude a second worker until final
         * cleanup completes and the coordinator releases its own reference. */
        BQ_CHECK(transferred && bq_test_worker_probe_locked(lease_path));
    }
    if (pair[0] >= 0) close(pair[0]);
    if (pair[1] >= 0) close(pair[1]);
    bq_worker_lease_release(&coordinator);
    bool released = rooted && !bq_test_worker_probe_locked(lease_path);
    BQ_CHECK(released);
    if (received >= 0) close(received);
    if (path_length > 0 && (size_t)path_length < sizeof(lease_path)) unlink(lease_path);
    if (root_created) rmdir(root);
}

BUSTER_GLOBAL_LOCAL void bq_test_worker_process_state(pid_t pid)
{
    if (pid > 1)
    {
        char path[64], text[1024];
        snprintf(path, sizeof(path), "/proc/%ld/stat", (long)pid);
        FILE* file = fopen(path, "r");
        if (file)
        {
            if (fgets(text, sizeof(text), file)) fprintf(stderr, "WORKER_DEADLINE process=%s", text);
            fclose(file);
        }
        else
        {
            fprintf(stderr, "WORKER_DEADLINE pid=%ld proc_errno=%d\n", (long)pid, errno);
        }
    }
}

BUSTER_GLOBAL_LOCAL void bq_test_worker_capture_result(char const* name, BqError error, BqError expected,
                                                         u64 before, u64 after, char const* output)
{
    u32 failures = bq_test_failures;
    pid_t pid = bq_worker_test_exec_pid;
    BQ_CHECK(error == expected);
    BQ_CHECK(after >= before && after - before < BQ_TEST_WORKER_BOUND_MILLISECONDS);
    BQ_CHECK(pid > 1);
    int group_result = -1, group_error = 0, child_result = -1, child_error = 0;
    if (pid > 1)
    {
        errno = 0;
        group_result = kill(-pid, 0);
        group_error = errno;
        BQ_CHECK(group_result == -1 && group_error == ESRCH);
        errno = 0;
        siginfo_t info = {0};
        child_result = waitid(P_PID, (id_t)pid, &info, WEXITED | WNOHANG | WNOWAIT);
        child_error = errno;
        BQ_CHECK(child_result == -1 && child_error == ECHILD);
    }
    if (failures != bq_test_failures)
    {
        fprintf(stderr, "WORKER_DEADLINE case=%s error=%d expected=%d elapsed=%" PRIu64
                " pid=%ld output=[%s] group_result=%d group_errno=%d child_result=%d child_errno=%d\n",
                name, error, expected, (uint64_t)(after - before), (long)pid, output,
                group_result, group_error, child_result, child_error);
        bq_test_worker_process_state(pid);
    }
    if (pid > 1 && child_result == 0)
    {
        /* Keep evidence before reaping, then bound cleanup of a child that a
         * regressed exec_capture left running or waitable. It still owns pid.
         */
        kill(-pid, SIGKILL);
        kill(pid, SIGKILL);
        int status = 0;
        BQ_CHECK(bq_worker_waitpid_until(pid, &status,
            bq_worker_deadline(bq_worker_monotonic_milliseconds(), BQ_TEST_WORKER_BOUND_MILLISECONDS)) == BQ_OK);
    }
}

BUSTER_GLOBAL_LOCAL bool bq_test_worker_wait_zombie(pid_t pid, u64 deadline)
{
    bool ok = true, exited = false;
    while (ok && !exited)
    {
        siginfo_t info = {0};
        int result = waitid(P_PID, (id_t)pid, &info, WEXITED | WNOHANG | WNOWAIT);
        if (result < 0 && errno != EINTR) ok = false;
        else if (result == 0 && info.si_pid == pid)
        {
            exited = true;
            ok = info.si_code == CLD_KILLED && info.si_status == SIGKILL;
        }
        else
        {
            u64 now = bq_worker_monotonic_milliseconds();
            if (now >= deadline) ok = false;
            else
            {
                u64 next = bq_worker_deadline(now, 1);
                if (next > deadline) next = deadline;
                ok = bq_worker_sleep_until(next) == BQ_OK;
            }
        }
    }
    return ok && exited;
}

BUSTER_GLOBAL_LOCAL void bq_test_worker_group_reaping(void)
{
    /* Both members are our direct children. Reap the leader first: the helper
     * must still reject a live member, then a deliberately unreaped zombie.
     * This is the same group-membership condition as the orphaned sleep in
     * #697, without transferring cleanup responsibility to the host's init.
     */
    u32 failures = bq_test_failures;
    pid_t leader = fork();
    if (!leader) for (;;) pause();
    bool grouped = leader > 1 && setpgid(leader, leader) == 0;
    BQ_CHECK(grouped);
    pid_t member = grouped ? fork() : -1;
    if (!member) for (;;) pause();
    grouped = grouped && member > 1 && setpgid(member, leader) == 0;
    BQ_CHECK(grouped);
    bool leader_reaped = false;
    int status = 0;
    if (grouped)
    {
        BQ_CHECK(kill(leader, SIGKILL) == 0);
        leader_reaped = bq_worker_waitpid_until(leader, &status,
            bq_worker_deadline(bq_worker_monotonic_milliseconds(), BQ_TEST_WORKER_BOUND_MILLISECONDS)) == BQ_OK;
        BQ_CHECK(leader_reaped);
        u64 before = bq_worker_monotonic_milliseconds();
        BQ_CHECK(bq_worker_process_group_absent(leader,
            bq_worker_deadline(before, BQ_TEST_WORKER_TIMEOUT_MILLISECONDS)) == BQ_CLEANUP_FAILED);
        u64 after = bq_worker_monotonic_milliseconds();
        BQ_CHECK(after >= bq_worker_deadline(before, BQ_TEST_WORKER_TIMEOUT_MILLISECONDS) &&
                 after - before < BQ_TEST_WORKER_BOUND_MILLISECONDS);

        BQ_CHECK(kill(member, SIGKILL) == 0);
        bool zombie = bq_test_worker_wait_zombie(member,
            bq_worker_deadline(bq_worker_monotonic_milliseconds(), BQ_TEST_WORKER_BOUND_MILLISECONDS));
        BQ_CHECK(zombie);
        if (zombie)
        {
            before = bq_worker_monotonic_milliseconds();
            BQ_CHECK(bq_worker_process_group_absent(leader,
                bq_worker_deadline(before, BQ_TEST_WORKER_TIMEOUT_MILLISECONDS)) == BQ_CLEANUP_FAILED);
            after = bq_worker_monotonic_milliseconds();
            BQ_CHECK(after >= bq_worker_deadline(before, BQ_TEST_WORKER_TIMEOUT_MILLISECONDS) &&
                     after - before < BQ_TEST_WORKER_BOUND_MILLISECONDS);
        }
    }
    if (failures != bq_test_failures)
    {
        bq_test_worker_process_state(leader);
        bq_test_worker_process_state(member);
    }
    /* Failed setup/assertions do not bypass bounded cleanup of owned PIDs. */
    if (leader > 1 && !leader_reaped)
    {
        kill(leader, SIGKILL);
        BQ_CHECK(bq_worker_waitpid_until(leader, &status,
            bq_worker_deadline(bq_worker_monotonic_milliseconds(), BQ_TEST_WORKER_BOUND_MILLISECONDS)) == BQ_OK);
    }
    if (member > 1)
    {
        kill(member, SIGKILL);
        BQ_CHECK(bq_worker_waitpid_until(member, &status,
            bq_worker_deadline(bq_worker_monotonic_milliseconds(), BQ_TEST_WORKER_BOUND_MILLISECONDS)) == BQ_OK);
    }
    if (leader > 1)
    {
        BQ_CHECK(bq_worker_process_group_absent(leader,
            bq_worker_deadline(bq_worker_monotonic_milliseconds(), BQ_TEST_WORKER_TIMEOUT_MILLISECONDS)) == BQ_OK);
    }
}

/* The scalar head of a canonical tp-retirement-campaign-budget-v2 record, in
 * bq_worker_budget_keys order: a 2 h 24 min 0.5 s ceiling, then every fixed
 * phase (38.8 s in all, settling and export counted per collection stage).
 * The service unit does not link the throughput encoder, so the fixture
 * spells the canonical text: these lines, the three remaining scalars and
 * both bound tables, exactly as tp_retirement_budget_encode lays them out. */
typedef struct BqTestWorkerBudget
{
    u64 values[BUSTER_ARRAY_LENGTH(bq_worker_budget_keys)];
} BqTestWorkerBudget;

BUSTER_GLOBAL_LOCAL BqTestWorkerBudget bq_test_worker_retirement_budget(void)
{
    BqTestWorkerBudget budget = {{UINT64_C(8640500000000), 1000000000, 2000000000, 3000000000, 3000000000,
        4000000000, 500000000, 600000000, 700000000, 800000000, 900000000, 1000000000, UINT64_C(20000000000)}};
    return budget;
}

/* Write `budget` as a canonical record and a one-line profile pinning its
 * digest. */
BUSTER_GLOBAL_LOCAL bool bq_test_worker_retirement_record(BqTestWorkerBudget const* budget,
                                                          char record[BQ_WORKER_BUDGET_BYTES], u64* size,
                                                          char profile[128])
{
    int length = snprintf(record, BQ_WORKER_BUDGET_BYTES, "%s\nderivation=%s\n", BQ_WORKER_BUDGET_SCHEMA,
        "fixed+stages*(settling+export)+sum_g(stages*2*(W+R*P)*timed(kind_g,stage_g,n_g))"
        "+U*stages*2*(W+R*P)*runtime+sum_u(4*untimed(kind_u,stage_u,n_u));"
        "object:first batch class with max_inputs>=n;singleton:its stage bound,never a one-input batch;"
        "untimed:separate tables measured on the slowest untimed target");
    u64 used = length > 0 && length < (int)BQ_WORKER_BUDGET_BYTES ? (u64)length : BQ_WORKER_BUDGET_BYTES;
    for (u32 index = 0; used < BQ_WORKER_BUDGET_BYTES && index < BUSTER_ARRAY_LENGTH(bq_worker_budget_keys);
         index += 1)
    {
        length = snprintf(record + used, BQ_WORKER_BUDGET_BYTES - used, "%s=%" PRIu64 "\n",
                          bq_worker_budget_keys[index], (uint64_t)budget->values[index]);
        used = length > 0 && (u64)length < BQ_WORKER_BUDGET_BYTES - used ? used + (u64)length :
               BQ_WORKER_BUDGET_BYTES;
    }
    if (used < BQ_WORKER_BUDGET_BYTES)
    {
        length = snprintf(record + used, BQ_WORKER_BUDGET_BYTES - used, "%s",
                          "runtime-process-ns=50000000\nmetrics-header-bytes=4096\nmetrics-input-bytes=16384\n"
                          "batch=1:40000000\nbatch=1024:2000000000\nsingleton=link:45000000\n"
                          "singleton=self-host-stage1:900000000\nuntimed-batch=1:50000000\n"
                          "untimed-batch=1024:2500000000\nuntimed-singleton=link:60000000\n"
                          "untimed-singleton=self-host-stage1:1200000000\n");
        used = length > 0 && (u64)length < BQ_WORKER_BUDGET_BYTES - used ? used + (u64)length :
               BQ_WORKER_BUDGET_BYTES;
    }
    bool ok = used < BQ_WORKER_BUDGET_BYTES;
    char digest[SHA256_HEX_CAPACITY] = {0};
    if (ok)
    {
        bq_digest(record, (u32)used, (char8*)digest);
        length = snprintf(profile, 128, "campaign-budget-sha256=%s\n", digest);
        ok = length > 0 && length < 128;
    }
    *size = ok ? used : 0;
    return ok;
}

/* Derive the limit of `budget`'s own correctly pinned record. */
BUSTER_GLOBAL_LOCAL BqError bq_test_worker_runtime_of(BqTestWorkerBudget const* budget, u64* runtime)
{
    char record[BQ_WORKER_BUDGET_BYTES], profile[128];
    u64 size = 0;
    BqError error = bq_test_worker_retirement_record(budget, record, &size, profile) ?
        bq_worker_retirement_runtime(string_from_pointer(profile), (String8){(char8*)record, size}, runtime) :
        BQ_IO;
    return error;
}

/* #881-C: the retirement unit limit and deadline come from the authenticated
 * budget ceiling, and every absent, unpinned, mismatched, zero, too-small,
 * oversized or fixed-phase-starved record is refused. The coordinator argv
 * carries the same value the broker self-test installs as a property
 * (bq_broker_runtime_self_test), and the deadline reaches the lease message. */
BUSTER_GLOBAL_LOCAL void bq_test_worker_retirement_runtime(void)
{
    BqTestWorkerBudget budget = bq_test_worker_retirement_budget();
    char record[BQ_WORKER_BUDGET_BYTES], profile[128];
    u64 size = 0, runtime = 1;
    bool built = bq_test_worker_retirement_record(&budget, record, &size, profile);
    BQ_CHECK(built);
    String8 record_text = {(char8*)record, size};
    String8 profile_text = string_from_pointer(profile);
    /* Budget -> limit, rounded down to whole seconds under the ceiling. */
    BQ_CHECK(built && bq_worker_retirement_runtime(profile_text, record_text, &runtime) == BQ_OK &&
             runtime == UINT64_C(8640000000));
    /* Limit -> absolute deadline from lease acquisition -> lease message. */
    u64 start = bq_worker_monotonic_milliseconds();
    u64 deadline = 0, deadline_ns = 0;
    BQ_CHECK(bq_worker_execution_deadline(start, runtime, &deadline) && deadline == start + UINT64_C(8640000) &&
             bq_phase_deadline_from_milliseconds(deadline, &deadline_ns));
    BqWorkerLeaseMessage message;
    char const* digest = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    BQ_CHECK(bq_worker_lease_message_make(&message, BQ_WORKER_LEASE_RESPONSE, "/tmp/host.lock", 1, 2, 3, 4,
                                          digest, deadline_ns) &&
             message.execution_deadline_ns == deadline_ns &&
             bq_worker_lease_message_matches(&message, BQ_WORKER_LEASE_RESPONSE, "/tmp/host.lock", 1, 2, 3, 4,
                                             digest, deadline_ns) &&
             !bq_worker_lease_message_matches(&message, BQ_WORKER_LEASE_RESPONSE, "/tmp/host.lock", 1, 2, 3, 4,
                                              digest, deadline_ns - 1));
    /* Limit -> typed broker argv; smoke keeps its six values. */
    char const* arguments[8];
    char runtime_text[32];
    u32 count = 0;
    char const* base = "1111111111111111111111111111111111111111";
    char const* candidate = "2222222222222222222222222222222222222222";
    BQ_CHECK(bq_worker_outer_arguments(BQ_RECIPE_NATIVE_RETIREMENT_BLOCKED, runtime, "1", "2", base, candidate,
                                       runtime_text, arguments, &count) && count == 7 &&
             !strcmp(arguments[0], BQ_SYSTEMD_BROKER) && !strcmp(arguments[1], BQ_SYSTEMD_RETIREMENT_OUTER_VERB) &&
             !strcmp(arguments[1], "start-retirement-outer") && !strcmp(arguments[2], "1") &&
             !strcmp(arguments[3], "2") && !strcmp(arguments[4], base) && !strcmp(arguments[5], candidate) &&
             !strcmp(arguments[6], "8640000000") && arguments[7] == NULL);
    BQ_CHECK(bq_worker_outer_arguments(BQ_RECIPE_VALIDATE_BUSTER, BQ_SYSTEMD_SMOKE_RUNTIME_USEC, "1", "2", base,
                                       candidate, runtime_text, arguments, &count) && count == 6 &&
             !strcmp(arguments[1], "start-outer") && arguments[6] == NULL);
    u64 const refused_limits[] = {0, UINT64_C(59000000), UINT64_C(8640000001),
                                  BQ_SYSTEMD_RETIREMENT_RUNTIME_MAX_USEC + BQ_SYSTEMD_USEC_PER_SECOND, UINT64_MAX};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(refused_limits); index += 1)
        BQ_CHECK(!bq_worker_outer_arguments(BQ_RECIPE_NATIVE_RETIREMENT_BLOCKED, refused_limits[index], "1", "2",
                                            base, candidate, runtime_text, arguments, &count) &&
                 count == 0 && arguments[0] == NULL);
    BQ_CHECK(!bq_worker_outer_arguments(BQ_RECIPE_UNKNOWN, runtime, "1", "2", base, candidate, runtime_text,
                                        arguments, &count) && count == 0);
    /* The manager's readback of the same limit parses to the same value. */
    u64 observed = 0;
    BQ_CHECK(bq_worker_duration("2h 24min", &observed) && observed == runtime &&
             bq_worker_duration("8640000000", &observed) && observed == runtime &&
             bq_worker_duration("1h", &observed) && observed == BQ_SYSTEMD_SMOKE_RUNTIME_USEC &&
             bq_worker_duration("10s", &observed) && observed == UINT64_C(10000000) &&
             !bq_worker_duration("infinity", &observed) && observed == 0 &&
             !bq_worker_duration("2h 24min 0.5s", &observed));
    /* Absent record, unpinned (blocked) profile, empty profile. */
    BQ_CHECK(bq_worker_retirement_runtime(profile_text, (String8){0}, &runtime) == BQ_RECIPE_MISMATCH &&
             runtime == 0);
    BQ_CHECK(bq_worker_retirement_runtime(bq_recipe_profile(BQ_RECIPE_NATIVE_RETIREMENT_BLOCKED), record_text,
                                          &runtime) == BQ_RECIPE_MISMATCH && runtime == 0);
    BQ_CHECK(bq_worker_retirement_runtime((String8){0}, record_text, &runtime) == BQ_RECIPE_MISMATCH);
    BQ_CHECK(bq_worker_retirement_runtime(profile_text, record_text, NULL) == BQ_RECIPE_MISMATCH);
    /* A record the pin does not name, a truncated or extended record. */
    BqTestWorkerBudget other = budget;
    other.values[12] += 1;
    char other_record[BQ_WORKER_BUDGET_BYTES], other_profile[128];
    u64 other_size = 0;
    BQ_CHECK(bq_test_worker_retirement_record(&other, other_record, &other_size, other_profile) &&
             bq_worker_retirement_runtime(profile_text, (String8){(char8*)other_record, other_size}, &runtime) ==
                 BQ_RECIPE_MISMATCH &&
             bq_worker_retirement_runtime(string_from_pointer(other_profile), record_text, &runtime) ==
                 BQ_RECIPE_MISMATCH &&
             bq_worker_retirement_runtime(string_from_pointer(other_profile),
                                          (String8){(char8*)other_record, other_size}, &runtime) == BQ_OK);
    BQ_CHECK(bq_worker_retirement_runtime(profile_text, (String8){(char8*)record, size - 1}, &runtime) ==
             BQ_RECIPE_MISMATCH);
    char spaced[BQ_WORKER_BUDGET_BYTES + 1];
    memcpy(spaced, record, (size_t)size);
    spaced[size] = '\n';
    BQ_CHECK(bq_worker_retirement_runtime(profile_text, (String8){(char8*)spaced, size + 1}, &runtime) ==
             BQ_RECIPE_MISMATCH);
    /* Even correctly pinned: another schema, a leading zero or a reordered
     * scalar is not the canonical record this worker reads. */
    char const* const substitutions[][2] = {
        {"schema=tp-retirement-campaign-budget-v2", "schema=tp-retirement-campaign-budget-v1"},
        {"reservation-ns=1000000000", "reservation-ns=01000000000"},
        {"reservation-ns=1000000000\nmaterialization-ns=2000000000",
         "materialization-ns=2000000000\nreservation-ns=1000000000"}};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(substitutions); index += 1)
    {
        char changed[BQ_WORKER_BUDGET_BYTES + 8], changed_profile[128], changed_digest[SHA256_HEX_CAPACITY];
        char const* found = strstr(record, substitutions[index][0]);
        u64 old_length = strlen(substitutions[index][0]), new_length = strlen(substitutions[index][1]);
        u64 prefix = found ? (u64)(found - record) : 0;
        BQ_CHECK(found != NULL);
        if (found)
        {
            memcpy(changed, record, (size_t)prefix);
            memcpy(changed + prefix, substitutions[index][1], (size_t)new_length);
            memcpy(changed + prefix + new_length, found + old_length, (size_t)(size - prefix - old_length));
            u64 changed_size = size - old_length + new_length;
            bq_digest(changed, (u32)changed_size, (char8*)changed_digest);
            snprintf(changed_profile, sizeof(changed_profile), "campaign-budget-sha256=%s\n", changed_digest);
            BQ_CHECK(bq_worker_retirement_runtime(string_from_pointer(changed_profile),
                                                  (String8){(char8*)changed, changed_size}, &runtime) ==
                     BQ_RECIPE_MISMATCH && runtime == 0);
        }
    }
    /* Zero, under-a-minute, over-range and fixed-phase-starved ceilings, and
     * a zero fixed-phase bound, are refused even when correctly pinned. */
    u64 const ceilings[] = {0, UINT64_C(59999999999),
                            BQ_SYSTEMD_RETIREMENT_RUNTIME_MAX_USEC * 1000 + UINT64_C(1000000000), UINT64_MAX};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(ceilings); index += 1)
    {
        BqTestWorkerBudget changed = budget;
        changed.values[0] = ceilings[index];
        BQ_CHECK(bq_test_worker_runtime_of(&changed, &runtime) == BQ_RECIPE_MISMATCH && runtime == 0);
    }
    BqTestWorkerBudget starved = budget;
    starved.values[12] = UINT64_C(60000000000);
    starved.values[0] = UINT64_C(70000000000);
    BQ_CHECK(bq_test_worker_runtime_of(&starved, &runtime) == BQ_RECIPE_MISMATCH && runtime == 0);
    starved.values[0] = UINT64_C(78800000000);
    BQ_CHECK(bq_test_worker_runtime_of(&starved, &runtime) == BQ_OK && runtime == UINT64_C(78000000));
    starved.values[0] = UINT64_C(78799999999);
    BQ_CHECK(bq_test_worker_runtime_of(&starved, &runtime) == BQ_RECIPE_MISMATCH);
    for (u32 index = 1; index < BUSTER_ARRAY_LENGTH(budget.values); index += 1)
    {
        BqTestWorkerBudget changed = budget;
        changed.values[index] = 0;
        BQ_CHECK(bq_test_worker_runtime_of(&changed, &runtime) == BQ_RECIPE_MISMATCH);
    }
    /* The range edges themselves are accepted. */
    u64 const edges[] = {UINT64_C(60999999999), BQ_SYSTEMD_RETIREMENT_RUNTIME_MAX_USEC * 1000};
    u64 const edge_limits[] = {BQ_SYSTEMD_RETIREMENT_RUNTIME_MIN_USEC, BQ_SYSTEMD_RETIREMENT_RUNTIME_MAX_USEC};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(edges); index += 1)
    {
        BqTestWorkerBudget changed = budget;
        changed.values[0] = edges[index];
        BQ_CHECK(bq_test_worker_runtime_of(&changed, &runtime) == BQ_OK && runtime == edge_limits[index]);
    }
}

BUSTER_GLOBAL_LOCAL void bq_test_worker_deadlines(void)
{
    bq_test_worker_pending_cancel_delivery();
    bq_test_worker_coordinator_lease_continuity();
    bq_test_worker_retirement_runtime();
    u64 absolute = 0, absolute_nanoseconds = 0;
    BQ_CHECK(!bq_worker_execution_deadline(1, 0, &absolute));
    BQ_CHECK(!bq_worker_execution_deadline(UINT64_MAX, 1, &absolute));
    BQ_CHECK(!bq_worker_execution_deadline(0, 1000, NULL));
    BQ_CHECK(bq_worker_execution_deadline(200, 1, &absolute) && absolute == 201);
    BQ_CHECK(bq_worker_execution_deadline(200, 1001, &absolute) && absolute == 202);
    BQ_CHECK(bq_worker_execution_deadline(0, 60ull * 60 * 1000000, &absolute) && absolute == 3600000);
    BQ_CHECK(bq_phase_deadline_from_milliseconds(201, &absolute_nanoseconds) &&
             absolute_nanoseconds == 201000000ull);
    u64 largest_milliseconds = UINT64_MAX / 1000000ull;
    BQ_CHECK(bq_phase_deadline_from_milliseconds(largest_milliseconds, &absolute_nanoseconds) &&
             absolute_nanoseconds == largest_milliseconds * 1000000ull);
    BQ_CHECK(!bq_phase_deadline_from_milliseconds(largest_milliseconds + 1, &absolute_nanoseconds) &&
             absolute_nanoseconds == 0);
    BQ_CHECK(!bq_phase_deadline_from_milliseconds(1, NULL));

    struct sigaction action = {0}, prior = {0};
    action.sa_handler = bq_test_alarm_handler;
    sigemptyset(&action.sa_mask);
    struct itimerval timer = {{0, 100}, {0, 100}}, stopped = {{0, 0}, {0, 0}};
    u64 before = bq_worker_monotonic_milliseconds();
    BQ_CHECK(sigaction(SIGALRM, &action, &prior) == 0 && setitimer(ITIMER_REAL, &timer, NULL) == 0);
    BQ_CHECK(bq_worker_sleep_until(bq_worker_deadline(before, 10)) == BQ_OK);
    u64 after = bq_worker_monotonic_milliseconds();
    BQ_CHECK(setitimer(ITIMER_REAL, &stopped, NULL) == 0 && sigaction(SIGALRM, &prior, NULL) == 0 &&
             bq_test_alarm_count > 0 && after >= before + 10 && after - before < BQ_TEST_WORKER_BOUND_MILLISECONDS);

    char output[64];
    int status = 0;
    /* exec keeps the timed process owned by exec_capture. A separate group
     * fixture below tests live/zombie members without depending on PID 1.
     * The fork result identifies the group even when startup exhausts 20 ms
     * before the shell can emit its PID; stdout is not a readiness handshake.
     */
    char const* blocked_pipe[] = {"/bin/sh", "-c", "echo $$; exec /bin/sleep 10", NULL};
    before = bq_worker_monotonic_milliseconds();
    BqError error = bq_worker_exec_capture(blocked_pipe, output, sizeof(output), &status,
                                          BQ_TEST_WORKER_TIMEOUT_MILLISECONDS);
    after = bq_worker_monotonic_milliseconds();
    bq_test_worker_capture_result("blocked-pipe", error, BQ_IO, before, after, output);
    char const* blocked_wait[] = {"/bin/sh", "-c", "exec 1>&- 2>&-; exec /bin/sleep 10", NULL};
    before = bq_worker_monotonic_milliseconds();
    error = bq_worker_exec_capture(blocked_wait, output, sizeof(output), &status,
                                   BQ_TEST_WORKER_TIMEOUT_MILLISECONDS);
    after = bq_worker_monotonic_milliseconds();
    bq_test_worker_capture_result("blocked-wait", error, BQ_IO, before, after, output);

    bq_worker_test_stop_before_exec = true;
    before = bq_worker_monotonic_milliseconds();
    error = bq_worker_exec_capture(blocked_pipe, output, sizeof(output), &status,
                                   BQ_TEST_WORKER_TIMEOUT_MILLISECONDS);
    after = bq_worker_monotonic_milliseconds();
    bq_worker_test_stop_before_exec = false;
    BQ_CHECK(output[0] == 0);
    bq_test_worker_capture_result("timeout-before-output", error, BQ_IO, before, after, output);

    bq_worker_test_setpgid_failure = true;
    char const* harmless[] = {"/bin/true", NULL};
    before = bq_worker_monotonic_milliseconds();
    error = bq_worker_exec_capture(harmless, output, sizeof(output), &status, BQ_TEST_WORKER_BOUND_MILLISECONDS);
    after = bq_worker_monotonic_milliseconds();
    bq_worker_test_setpgid_failure = false;
    bq_test_worker_capture_result("setpgid-failure", error, BQ_CLEANUP_FAILED, before, after, output);

    BqSystemdContext context = {0};
    context.pid = fork();
    if (!context.pid) for (;;) pause();
    BqWorkerBackend backend = {&context, NULL, NULL, NULL, bq_systemd_join, bq_systemd_cleanup_launcher,
                               bq_systemd_delay, bq_systemd_clock};
    before = bq_worker_monotonic_milliseconds();
    BQ_CHECK(context.pid > 0 && backend.join(&backend, &status,
             bq_worker_deadline(before, BQ_TEST_WORKER_TIMEOUT_MILLISECONDS), true) == BQ_WORKER_TIMEOUT);
    after = bq_worker_monotonic_milliseconds();
    BQ_CHECK(after >= before && after - before < BQ_TEST_WORKER_BOUND_MILLISECONDS);
    /* #880 attempt P: a pending cancellation interrupts only a cancellable
     * join. The cleanup join must still reap a launcher that exits later. */
    bq_worker_cancel_signal = 1;
    before = bq_worker_monotonic_milliseconds();
    BQ_CHECK(context.pid > 0 && backend.join(&backend, &status,
             bq_worker_deadline(before, BQ_TEST_WORKER_TIMEOUT_MILLISECONDS), true) == BQ_WORKER_CANCEL_SIGNAL &&
             context.pid > 0);
    after = bq_worker_monotonic_milliseconds();
    BQ_CHECK(after >= before && after - before < BQ_TEST_WORKER_BOUND_MILLISECONDS);
    if (context.pid > 0)
    {
        BQ_CHECK(backend.cleanup_launcher(&backend,
                 bq_worker_deadline(bq_worker_monotonic_milliseconds(), BQ_TEST_WORKER_BOUND_MILLISECONDS)) == BQ_OK && context.pid < 0);
    }
    if (context.pid > 0)
    {
        kill(context.pid, SIGKILL);
        waitpid(context.pid, NULL, 0);
    }
    context.pid = fork();
    if (!context.pid)
    {
        struct timespec pause_before_exit = {0, 200 * 1000 * 1000};
        nanosleep(&pause_before_exit, NULL);
        _exit(7);
    }
    status = 0;
    BQ_CHECK(context.pid > 0 && backend.join(&backend, &status,
             bq_worker_deadline(bq_worker_monotonic_milliseconds(), BQ_TEST_WORKER_BOUND_MILLISECONDS), false) == BQ_OK &&
             context.pid < 0 && WIFEXITED(status) && WEXITSTATUS(status) == 7);
    bq_worker_cancel_signal = 0;
    bq_test_worker_group_reaping();
}
