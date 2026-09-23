/* Real socketpair/pidfd tests of the production phase protocol. Fixture
 * processes exercise ordering and durability, not performance qualification. */
#ifndef BUSTER_BENCH_SERVICE_PHASE_CHANNEL_TESTS_H
#define BUSTER_BENCH_SERVICE_PHASE_CHANNEL_TESTS_H

BUSTER_GLOBAL_LOCAL void bq_test_phase_run(unsigned defect, char const* driver);
BUSTER_GLOBAL_LOCAL void bq_test_phase_prelaunch_deadline(void);
BUSTER_GLOBAL_LOCAL void bq_test_phase_finalization_deadline(void);

BUSTER_GLOBAL_LOCAL void bq_test_phase_packets(void)
{
    int pair[2] = {-1, -1};
    BQ_CHECK(socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair) == 0);
    BqPhaseChannel sender, receiver;
    BQ_CHECK(bq_phase_init(&sender, pair[0], 7, 9));
    BQ_CHECK(bq_phase_init(&receiver, pair[1], 7, 9));
    BQ_CHECK(fcntl(pair[0], F_GETFD) & FD_CLOEXEC);
    unsigned char message[BQ_PHASE_MESSAGE_BYTES], copy[BQ_PHASE_MESSAGE_BYTES + 1];
    BQ_CHECK(bq_phase_make(&sender, 1, message) && bq_phase_check(&receiver, message));
    for (unsigned byte = 0; byte < sizeof(message); ++byte)
    {
        memcpy(copy, message, sizeof(message));
        copy[byte] ^= 0x80;
        /* The timestamp may still describe a valid earlier instant; all other
         * bytes have a unique encoding and must be rejected. */
        if (byte < 32 || byte >= 40) BQ_CHECK(!bq_phase_check(&receiver, copy));
    }
    memcpy(copy, message, sizeof(message));
    bq_phase_put(copy + 32, UINT64_MAX);
    BQ_CHECK(!bq_phase_check(&receiver, copy));
    bq_phase_put(copy + 32, 0);
    BQ_CHECK(!bq_phase_check(&receiver, copy));
    memcpy(copy, message, sizeof(message));
    for (unsigned length = BQ_PHASE_MESSAGE_BYTES - 1; length <= BQ_PHASE_MESSAGE_BYTES + 1; ++length)
    {
        BQ_CHECK(send(pair[0], copy, length, MSG_NOSIGNAL) == (ssize_t)length);
        BQ_CHECK(bq_phase_receive(pair[1], message) == (length == BQ_PHASE_MESSAGE_BYTES));
    }
    int pipe_fd[2] = {-1, -1};
    BQ_CHECK(pipe2(pipe_fd, O_CLOEXEC | O_NONBLOCK) == 0);
    unsigned char ancillary[CMSG_SPACE(sizeof(int))] = {0};
    struct iovec vector = {copy, BQ_PHASE_MESSAGE_BYTES};
    struct msghdr packet = {.msg_iov = &vector, .msg_iovlen = 1,
                            .msg_control = ancillary, .msg_controllen = sizeof(ancillary)};
    struct cmsghdr* header = CMSG_FIRSTHDR(&packet);
    header->cmsg_level = SOL_SOCKET;
    header->cmsg_type = SCM_RIGHTS;
    header->cmsg_len = CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(header), &pipe_fd[1], sizeof(int));
    BQ_CHECK(sendmsg(pair[0], &packet, MSG_NOSIGNAL) == BQ_PHASE_MESSAGE_BYTES);
    close(pipe_fd[1]);
    BQ_CHECK(!bq_phase_receive(pair[1], message));
    char byte = 0;
    BQ_CHECK(read(pipe_fd[0], &byte, 1) == 0); /* No leaked ancillary writer. */
    close(pipe_fd[0]);
    close(pair[1]);
    BQ_CHECK(!bq_phase_exchange(&sender, 1) && sender.failed);
    BQ_CHECK(!bq_phase_make(&sender, 1, message));
    close(pair[0]);
    bq_test_phase_run(9, NULL);
    bq_test_phase_prelaunch_deadline();
    bq_test_phase_finalization_deadline();
}

BUSTER_GLOBAL_LOCAL u32 bq_test_phase_deadline_clock_calls;

BUSTER_GLOBAL_LOCAL u64 bq_test_phase_expired_clock(BqWorkerBackend* backend)
{
    BqWorkerFake* fake = backend->context;
    bq_test_phase_deadline_clock_calls += 1;
    if (bq_test_phase_deadline_clock_calls == 2) fake->elapsed = 3600000;
    return fake->elapsed;
}

/* Model materialization consuming the entire fixed runtime without waiting an
 * hour: no unit is launched, failure is durable, and admission is released. */
BUSTER_GLOBAL_LOCAL void bq_test_phase_prelaunch_deadline(void)
{
    BqWorkerFixture fixture;
    if (bq_test_worker_begin(&fixture, BQ_WORKER_SUCCEEDED, false))
    {
        BqQueue* queue = &fixture.material.queue.queue;
        BqRequest request = bq_test_real_request(218);
        u64 id = 0;
        bq_test_phase_deadline_clock_calls = 0;
        fixture.backend.clock = bq_test_phase_expired_clock;
        BQ_CHECK(bq_submit(queue, &request, &id) == BQ_OK);
        BQ_CHECK(bq_worker_run(queue, &fixture.config, &id) == BQ_WORKER_TIMEOUT);
        BqJob* job = bq_job(&queue->state, id);
        BQ_CHECK(job && job->phase == BQ_FINISHED && job->outcome == BQ_FAILED &&
                 bq_failure_evidence(queue, job) == BQ_WORKER_TIMEOUT &&
                 fixture.fake.starts == 0 && !queue->needs_reconciliation &&
                 !queue->state.active_id && !bq_test_worker_probe_locked(fixture.lease));
        BqRequest second = bq_test_real_request(219);
        u64 next = 0, token = 0;
        BQ_CHECK(bq_submit(queue, &second, &next) == BQ_OK &&
                 bq_reserve(queue, &next, &token) == BQ_OK && next != id);
        bq_test_worker_end(&fixture);
    }
}

/* Validation can cross the same fixed deadline even after the child has
 * exited cleanly. A valid preexisting bundle cannot turn that into success. */
BUSTER_GLOBAL_LOCAL void bq_test_phase_finalization_deadline(void)
{
    for (unsigned delayed = 0; delayed < 2; delayed += 1)
    {
        BqWorkerFixture fixture;
        if (bq_test_worker_begin(&fixture, BQ_WORKER_SUCCEEDED, false))
        {
            BqQueue* queue = &fixture.material.queue.queue;
            BqRequest request = bq_test_real_request(220 + delayed);
            u64 id = 0, token = 0;
            BQ_CHECK(bq_submit(queue, &request, &id) == BQ_OK &&
                     bq_materialize(queue, fixture.config.installed_root,
                                    fixture.config.workspace_root, &id, &token) == BQ_OK);
            BqJob* job = bq_job(&queue->state, id);
            fixture.config.production_path = true;
            BqWorkerFinalization finalization = {.config = &fixture.config, .result_directory = -1,
                                                 .execution_deadline = 1000};
            bool artifact = job && bq_test_worker_make_success_result(&fixture, job, &finalization);
            fixture.fake.elapsed = delayed ? 0 : 1000;
            bq_test_phase_deadline_clock_calls = 0;
            fixture.backend.clock = delayed ? bq_test_phase_expired_clock : fixture.backend.clock;
            BQ_CHECK(artifact && bq_worker_finish(queue, &fixture.config, job,
                                                   BQ_SUCCEEDED, BQ_NOT_FOUND, &finalization) == BQ_OK);
            BQ_CHECK(bq_worker_finalization_restore(&finalization) == BQ_OK);
            job = bq_job(&queue->state, id);
            BQ_CHECK(job && job->phase == BQ_FINISHED && job->outcome == BQ_FAILED &&
                     bq_failure_evidence(queue, job) == BQ_WORKER_TIMEOUT &&
                     !queue->state.active_id && !queue->needs_reconciliation);
            BqRequest next_request = bq_test_real_request(222 + delayed);
            u64 next = 0, next_token = 0;
            BQ_CHECK(bq_submit(queue, &next_request, &next) == BQ_OK &&
                     bq_reserve(queue, &next, &next_token) == BQ_OK && next != id);
            if (finalization.result_directory >= 0) close(finalization.result_directory);
            bq_test_worker_end(&fixture);
        }
    }
}

BUSTER_GLOBAL_LOCAL void bq_test_phase_run(unsigned defect, char const* driver)
{
    BqWorkerFixture fixture;
    if (bq_test_worker_begin(&fixture, BQ_WORKER_SUCCEEDED, false))
    {
        BqQueue* queue = &fixture.material.queue.queue;
        BqRequest request = bq_test_real_request(200 + defect);
        u64 id = 0, token = 0;
        BQ_CHECK(bq_submit(queue, &request, &id) == BQ_OK &&
                 bq_materialize(queue, fixture.config.installed_root, fixture.config.workspace_root, &id, &token) == BQ_OK);
        BqJob* job = bq_job(&queue->state, id);
        BqWorkerFinalization finalization = {.config = &fixture.config, .result_directory = -1};
        BQ_CHECK(bq_worker_result_open(&fixture.config, job, &finalization, true) == BQ_OK);
        if (defect == 7)
            BQ_CHECK(bq_worker_result_control_publish(&finalization, "worker-phase-1", "existing\n", 9, 0400) == BQ_OK);
        if (defect == 8)
        {
            char record[48];
            BQ_CHECK(bq_record_name(record, "worker-phase-1", id) &&
                     bq_record_write(queue, record, (u8 const*)"existing\n", 9, false) == BQ_OK);
        }
        int pair[2] = {-1, -1};
        BQ_CHECK(socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair) == 0);
        BqPhaseChannel phases;
        BQ_CHECK(bq_phase_init(&phases, pair[0], id, token));
        struct sigaction cancel = {.sa_handler = bq_worker_cancel_handler}, prior = {0};
        sigemptyset(&cancel.sa_mask);
        BQ_CHECK(sigaction(SIGUSR1, &cancel, &prior) == 0);
        pid_t parent = getpid();
        pid_t child = fork();
        if (child == 0)
        {
            close(pair[0]);
            if (defect == 6)
            {
                char job_text[32], token_text[32], phase_text[32];
                snprintf(job_text, sizeof(job_text), "%" PRIu64, (uint64_t)id);
                snprintf(token_text, sizeof(token_text), "%" PRIu64, (uint64_t)token);
                snprintf(phase_text, sizeof(phase_text), "%d", pair[1]);
                int flags = fcntl(pair[1], F_GETFD);
                if (flags < 0 || fcntl(pair[1], F_SETFD, flags & ~FD_CLOEXEC) != 0) _exit(125);
                char* arguments[] = {(char*)driver, (char*)"bench_service_recipe_self_test", job_text, token_text,
                    fixture.material.workspaces,
                    (char*)"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
                    (char*)"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
                    finalization.result_root, phase_text, NULL};
                execv(driver, arguments);
                _exit(126);
            }
            BqPhaseChannel client;
            bool ok = bq_phase_init(&client, pair[1], id, token) && (fcntl(pair[1], F_GETFD) & FD_CLOEXEC);
            for (unsigned phase = 1; ok && phase <= 4; ++phase)
            {
                if (defect == 7 || defect == 8)
                {
                    unsigned char pending[BQ_PHASE_MESSAGE_BYTES];
                    struct pollfd waiting = {.fd = pair[1], .events = POLLIN};
                    ok = bq_phase_make(&client, phase, pending) &&
                         send(pair[1], pending, sizeof(pending), MSG_NOSIGNAL) == sizeof(pending) &&
                         poll(&waiting, 1, 100) == 0;
                    break;
                }
                if (phase == 4 && defect >= 1 && defect <= 3)
                {
                    if (defect == 3) kill(parent, SIGUSR1);
                    if (defect >= 2) while (true) pause();
                    break;
                }
                if ((defect == 4 && phase == 1) || (defect == 5 && phase == 2))
                {
                    unsigned char bad[BQ_PHASE_MESSAGE_BYTES];
                    ok = bq_phase_make(&client, phase, bad);
                    bq_phase_put(bad + (defect == 4 ? 16 : 24), defect == 4 ? token + 1 : 1);
                    ok = ok && send(pair[1], bad, sizeof(bad), MSG_NOSIGNAL) == sizeof(bad);
                    break;
                }
                ok = bq_phase_exchange(&client, phase);
                char name[48], record[48];
                unsigned char retained[512];
                u32 size = 0;
                snprintf(name, sizeof(name), "worker-phase-%u", phase);
                struct stat info = {0};
                ok = ok && fstatat(finalization.result_directory, name, &info, AT_SYMLINK_NOFOLLOW) == 0 &&
                     S_ISREG(info.st_mode) && (info.st_mode & 0777) == 0400 && info.st_nlink == 1 &&
                     bq_record_name(record, name, id) &&
                     bq_record_read(queue, record, retained, sizeof(retained), &size) == BQ_OK && size > 0;
            }
            if (ok && defect == 9)
            {
                /* A worker may still be sealing after MEASURED. A replayed
                 * final packet during that window must poison the attempt. */
                unsigned char duplicate[BQ_PHASE_MESSAGE_BYTES] = {0};
                memcpy(duplicate, "BQPHASE1", 8);
                bq_phase_put(duplicate + 8, id);
                bq_phase_put(duplicate + 16, token);
                bq_phase_put(duplicate + 24, BQ_PHASE_MEASURED);
                bq_phase_put(duplicate + 32, client.last_time);
                ok = send(pair[1], duplicate, sizeof(duplicate), MSG_NOSIGNAL) == sizeof(duplicate);
            }
            close(pair[1]);
            _exit(ok ? 0 : 1);
        }
        BQ_CHECK(child > 0);
        close(pair[1]);
        BqSystemdContext context = {.pid = child};
        int status = 0;
        u64 before = bq_worker_monotonic_milliseconds();
        BqError result = bq_worker_phase_join(queue, &context, &phases, &status,
            bq_worker_deadline(before, defect == 2 ? 1000 : defect == 6 ? 30000 : 3000), &finalization);
        BqError expected = (defect == 7 || defect == 8) ? BQ_IO : defect == 0 || defect == 6 ? BQ_OK : defect == 2 ? BQ_WORKER_TIMEOUT :
                           defect == 3 ? BQ_WORKER_CANCEL_SIGNAL : BQ_WORKER_MISMATCH;
        BQ_CHECK(result == expected);
        BQ_CHECK(bq_worker_monotonic_milliseconds() - before < (defect == 6 ? 30000u : 3000u));
        if (context.pid > 0)
        {
            if (defect == 2 || defect == 3) kill(child, SIGKILL);
            BQ_CHECK(waitpid(child, &status, 0) == child);
        }
        if (defect != 2 && defect != 3) BQ_CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
        BQ_CHECK(sigaction(SIGUSR1, &prior, NULL) == 0);
        bq_worker_cancel_signal = 0;
        bq_worker_shutdown_signal = 0;
        close(pair[0]);
        if (defect == 0 || defect == 6)
        {
            BQ_CHECK(bq_worker_phases_validate(queue, job, &finalization) == BQ_OK);
            BQ_CHECK(unlinkat(finalization.result_directory, "worker-phase-3", 0) == 0);
            BQ_CHECK(bq_worker_phases_validate(queue, job, &finalization) != BQ_OK);
            BQ_CHECK(bq_worker_result_control_publish(&finalization, "worker-phase-3", "replaced\n", 9, 0400) == BQ_OK);
            BQ_CHECK(bq_worker_phases_validate(queue, job, &finalization) != BQ_OK);
        }
        close(finalization.result_directory);
        BqPhase expected_phase = (defect == 4 || defect == 5 || defect == 7 || defect == 8) ? BQ_PREPARING : BQ_MEASURING;
        BQ_CHECK(bq_job(&queue->state, id)->phase == expected_phase);
        /* Even a completed exchange is not a performance verdict or cleanup
         * proof. A restart remains fenced until ordinary recovery completes. */
        bq_close(queue);
        BQ_CHECK(bq_open(queue, fixture.material.queue.path) == BQ_OK);
        BQ_CHECK(queue->needs_reconciliation && bq_job(&queue->state, id)->phase == expected_phase &&
                 bq_job(&queue->state, id)->outcome == BQ_NO_OUTCOME);
        u64 next_id = 0, next_token = 0;
        BQ_CHECK(bq_reserve(queue, &next_id, &next_token) == BQ_RECONCILIATION_REQUIRED);
        bq_test_worker_end(&fixture);
    }
}
#endif
