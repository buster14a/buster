/* Linux-only deadline fixtures, included by tests.c.
 * bq_test_worker_deadlines checks capture/launcher deadlines and owned cleanup.
 * bq_test_worker_group_reaping retains live and zombie group members explicitly;
 * no verdict depends on how promptly PID 1 reaps an orphaned shell descendant.
 */
#define BQ_TEST_WORKER_TIMEOUT_MILLISECONDS 20u
#define BQ_TEST_WORKER_BOUND_MILLISECONDS 1000u

BUSTER_GLOBAL_LOCAL bool bq_test_worker_probe_locked(char const* path);

BUSTER_GLOBAL_LOCAL volatile sig_atomic_t bq_test_alarm_count;

BUSTER_GLOBAL_LOCAL void bq_test_alarm_handler(int signal_number)
{
    (void)signal_number;
    bq_test_alarm_count += 1;
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

BUSTER_GLOBAL_LOCAL void bq_test_worker_deadlines(void)
{
    bq_test_worker_coordinator_lease_continuity();
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
             bq_worker_deadline(before, BQ_TEST_WORKER_TIMEOUT_MILLISECONDS)) == BQ_WORKER_TIMEOUT);
    after = bq_worker_monotonic_milliseconds();
    BQ_CHECK(after >= before && after - before < BQ_TEST_WORKER_BOUND_MILLISECONDS);
    if (context.pid > 0)
    {
        BQ_CHECK(backend.cleanup_launcher(&backend,
                 bq_worker_deadline(bq_worker_monotonic_milliseconds(), BQ_TEST_WORKER_BOUND_MILLISECONDS)) == BQ_OK && context.pid < 0);
    }
    bq_test_worker_group_reaping();
}
