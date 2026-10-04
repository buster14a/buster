/* Independent control/worker roots, real journal/cache/codec/daemon tests.
 * Fixed recipe result bytes and manager observations are fixtures. None of
 * these tests contact SSH, a benchmark host, systemd or a cloud account.
 */
#ifdef __linux__
BUSTER_GLOBAL_LOCAL BqRequest bq_test_offhost_request(u32 number)
{
    BqRequest original = bq_test_real_request(number), request;
    String8 fields[BQ_FIELD_COUNT] = {S8(BQ_EXPORT_PRINCIPAL), bq_field(&original, 1), bq_field(&original, 2),
                                     bq_field(&original, 3), bq_field(&original, 4)};
    BQ_CHECK(bq_request_make(fields, &request) == BQ_OK);
    return request;
}

BUSTER_GLOBAL_LOCAL void bq_test_offhost_remove(char const* path)
{
    int root = open(path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    BQ_CHECK(root >= 0 && bq_remove_workspace_payload(root));
    if (root >= 0) close(root);
    BQ_CHECK(rmdir(path) == 0);
}

BUSTER_GLOBAL_LOCAL BqError bq_test_offhost_message(BqOffhostControl* control, u32 operation,
                                                   BqOffhostIdentity const* identity, void const* body, u32 size)
{
    BqSessionPacket request, response;
    bq_session_packet(&request, operation, BQ_OK, identity, body, size);
    BqError error = bq_control_session(control, &request, &response);
    BQ_CHECK(bq_session_valid(&response, true) && bq_u32(response.bytes + 20) == (u32)error);
    return error;
}

BUSTER_GLOBAL_LOCAL void bq_test_offhost_codec(void)
{
    BqOffhostIdentity identity = {.id = 123, .token = 999};
    bq_digest("control", 7, identity.control);
    bq_digest(BQ_OFFHOST_MACHINE, sizeof(BQ_OFFHOST_MACHINE) - 1, identity.machine);
    BqSessionPacket packet, changed;
    bq_session_packet(&packet, BQ_SESSION_QUIET, BQ_OK, &identity, NULL, 0);
    BQ_CHECK(bq_session_valid(&packet, false) && !bq_session_valid(&packet, true));
    for (u32 i = 0; i < 24; i += 1)
    {
        changed = packet;
        changed.bytes[i] ^= 0x80;
        BQ_CHECK(!bq_session_valid(&changed, false));
    }
    changed = packet;
    changed.size -= 1;
    BQ_CHECK(!bq_session_valid(&changed, false));
    changed = packet;
    bq_put32(changed.bytes + 16, BQ_SESSION_BODY + 1);
    BQ_CHECK(!bq_session_valid(&changed, false));
    int stream[2];
    BQ_CHECK(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, stream) == 0);
    u64 deadline = bq_worker_deadline(bq_worker_monotonic_milliseconds(), 1000);
    BQ_CHECK(bq_session_stream(stream[0], &packet, true, deadline) == BQ_OK &&
             bq_session_stream(stream[1], &changed, false, deadline) == BQ_OK &&
             packet.size == changed.size && !memcmp(packet.bytes, changed.bytes, packet.size));
    close(stream[0]); close(stream[1]);
    BqFixture local;
    if (bq_test_begin(&local))
    {
        BqRequest request = bq_test_offhost_request(700);
        BQ_CHECK(bq_assigned_import(&local.queue, &request, 900000, 700000) == BQ_OK);
        BQ_CHECK(local.queue.state.sequence == 1 && local.queue.state.active_id == 900000 &&
                 local.queue.state.jobs[0].token == 700000 && local.queue.state.journal_schema == BQ_SCHEMA_ASSIGNED);
        u64 bytes = local.queue.bytes;
        BQ_CHECK(bq_assigned_import(&local.queue, &request, 900000, 700000) == BQ_OK && local.queue.bytes == bytes);
        BQ_CHECK(bq_assigned_import(&local.queue, &request, 900000, 700001) == BQ_CONFLICT);
        bq_close(&local.queue);
        BQ_CHECK(bq_open(&local.queue, local.path) == BQ_OK && local.queue.needs_reconciliation &&
                 local.queue.state.jobs[0].id == 900000 && local.queue.state.jobs[0].token == 700000);
        bq_test_end(&local);
    }
}

BUSTER_GLOBAL_LOCAL void bq_test_offhost_assigned_worker(void)
{
    BqWorkerFixture fixture;
    if (bq_test_worker_begin(&fixture, BQ_WORKER_SUCCEEDED, false))
    {
        BqQueue* queue = &fixture.material.queue.queue;
        BqRequest request = bq_test_offhost_request(701);
        BqWorkerLease retained = {.descriptor = -1};
        BQ_CHECK(bq_assigned_import(queue, &request, 1, 2) == BQ_OK);
        BQ_CHECK(bq_worker_lease_acquire(fixture.lease, &retained) == 0);
        fixture.config.assigned_attempt = true;
        fixture.config.retained_lease_descriptor = retained.descriptor;
        u64 id = 1;
        BQ_CHECK(bq_worker_run(queue, &fixture.config, &id) == BQ_OK && id == 1);
        BqJob* job = bq_job(&queue->state, id);
        BQ_CHECK(job && job->token == 2 && job->phase == BQ_FINISHED && job->outcome == BQ_SUCCEEDED &&
                 fixture.fake.starts == 1 && bq_test_worker_probe_locked(fixture.lease));
        bq_close(queue);
        BQ_CHECK(bq_open(queue, fixture.material.queue.path) == BQ_OK && !queue->needs_reconciliation);
        BQ_CHECK(bq_test_worker_probe_locked(fixture.lease));
        bq_worker_lease_release(&retained);
        BQ_CHECK(!bq_test_worker_probe_locked(fixture.lease));
        bq_test_worker_end(&fixture);
    }
}

BUSTER_GLOBAL_LOCAL void bq_test_offhost_cache(bool cancel_intent)
{
    char root[BQ_PATH_CAP + 1] = "/tmp/buster-offhost-control-XXXXXX";
    BQ_CHECK(bq_test_mkdtemp_physical(root, sizeof(root)));
    char machine[SHA256_HEX_CAPACITY];
    bq_digest(BQ_OFFHOST_MACHINE, sizeof(BQ_OFFHOST_MACHINE) - 1, machine);
    BqOffhostControl control;
    BQ_CHECK(bq_control_open(&control, root, machine) == BQ_OK);
    u8 custody[129];
    bq_digest("fixture-install", 15, (char*)custody);
    bq_digest("fixture-boot", 12, (char*)custody + 64);
    BqWorkerFixture worker;
    if (bq_test_worker_begin(&worker, BQ_WORKER_SUCCEEDED, false))
    {
        BqRequest work = bq_test_offhost_request(cancel_intent ? 703 : 702);
        u64 id = 0;
        BQ_CHECK(bq_submit(&control.queue, &work, &id) == BQ_OK && id == 1);
        BqOffhostIdentity empty = control.identity;
        BQ_CHECK(bq_test_offhost_message(&control, BQ_SESSION_HELLO, &empty, NULL, 0) == BQ_OK);
        BqOffhostIdentity identity = control.identity;
        BQ_CHECK(identity.id == 1 && identity.token == 2);
        BQ_CHECK(bq_test_offhost_message(&control, BQ_SESSION_QUIET, &identity, NULL, 0) == BQ_NOT_FOUND);
        BQ_CHECK(bq_assigned_import(&worker.material.queue.queue, &work, id, identity.token) == BQ_OK);
        BQ_CHECK(bq_test_offhost_message(&control, BQ_SESSION_CUSTODY, &identity, custody, 128u) == BQ_OK);
        BQ_CHECK(bq_test_offhost_message(&control, BQ_SESSION_HELLO, &empty, NULL, 0) == BQ_RECONCILIATION_REQUIRED);
        BQ_CHECK(bq_test_offhost_message(&control, BQ_SESSION_HELLO, &identity, NULL, 0) == BQ_OK);
        BQ_CHECK(bq_test_offhost_message(&control, BQ_SESSION_QUIET, &identity, NULL, 0) == BQ_OK && control.quiet);
        BqOffhostIdentity wrong = identity;
        wrong.token += 1;
        BQ_CHECK(bq_test_offhost_message(&control, BQ_SESSION_CUSTODY, &wrong, custody, 128u) == BQ_CONFLICT);
        u8 body[8];
        bq_put64(body, id);
        BqPacket request, response;
        if (cancel_intent)
        {
            bq_packet(&request, BQ_OP_CANCEL, 3, body, sizeof(body));
            BQ_CHECK(bq_control_public(&control, request.bytes, request.size, &response) == BQ_OK &&
                     bq_u32(response.bytes + BQ_CONTROL_HEADER + 40) == 1 && !control.queue.state.jobs[0].cancel_requested);
        }
        bq_close(&control.queue);
        BQ_CHECK(bq_control_open(&control, root, machine) == BQ_OK && control.identity.id == id && control.quiet);
        BQ_CHECK(bq_test_offhost_message(&control, BQ_SESSION_HELLO, &identity, NULL, 0) == BQ_OK);
        BqWorkerLease retained = {.descriptor = -1};
        BQ_CHECK(bq_worker_lease_acquire(worker.lease, &retained) == 0);
        u64 token = identity.token;
        BqQueue* local = &worker.material.queue.queue;
        BQ_CHECK(bq_materialize_reserved(local, worker.config.installed_root, worker.config.workspace_root, &id, &token) == BQ_OK);
        BqJob* job = bq_job(&local->state, id);
        BqWorkerFinalization finalization = {.config = &worker.config, .result_directory = -1};
        BQ_CHECK(bq_test_worker_make_success_result(&worker, job, &finalization));
        for (BqPhase phase = BQ_SETTLING; phase <= BQ_CLEANING; phase = (BqPhase)(phase + 1))
            BQ_CHECK(bq_real_advance(local, job, phase, phase >= BQ_FINALIZING ? BQ_SUCCEEDED : BQ_NO_OUTCOME) == BQ_OK);
        BQ_CHECK(bq_result_bind(local, job, string_from_pointer(finalization.result_root), finalization.result_digest,
                 finalization.bundle_digest, finalization.full_digest) == BQ_OK);
        BQ_CHECK(bq_real_advance(local, job, BQ_FINISHED, BQ_SUCCEEDED) == BQ_OK);
        BQ_CHECK(bq_export_prepare(local, job) == BQ_OK);
        char sealed[80];
        bq_export_name(sealed, id, token, false);
        int archive = openat(local->directory_fd, sealed, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
        struct stat info = {0};
        u8 receipt[BQ_EXPORT_RECEIPT_CAP];
        BQ_CHECK(archive >= 0 && fstat(archive, &info) == 0 && bq_read(archive, receipt, sizeof(receipt), 0));
        receipt[48] ^= 1;
        BQ_CHECK(bq_test_offhost_message(&control, BQ_SESSION_RESULT, &identity, receipt, sizeof(receipt)) == BQ_EXPORT_CORRUPT);
        receipt[48] ^= 1;
        BQ_CHECK(bq_test_offhost_message(&control, BQ_SESSION_RESULT, &identity, receipt, sizeof(receipt)) == BQ_OK);
        u8 chunk[BQ_SESSION_BODY];
        bq_put64(chunk, BQ_EXPORT_CHUNK_CAP);
        memset(chunk + 8, 0, BQ_EXPORT_CHUNK_CAP);
        BQ_CHECK(bq_test_offhost_message(&control, BQ_SESSION_CHUNK, &identity, chunk, sizeof(chunk)) == BQ_INVALID_TRANSITION);
        for (u64 cursor = 0; cursor < (u64)info.st_size; cursor += BQ_EXPORT_CHUNK_CAP)
        {
            u32 count = (u64)info.st_size - cursor < BQ_EXPORT_CHUNK_CAP ? (u32)((u64)info.st_size - cursor) : BQ_EXPORT_CHUNK_CAP;
            bq_put64(chunk, cursor);
            BQ_CHECK(bq_read(archive, chunk + 8, count, cursor));
            BQ_CHECK(bq_test_offhost_message(&control, BQ_SESSION_CHUNK, &identity, chunk, count + 8) == BQ_OK);
            BQ_CHECK(bq_test_offhost_message(&control, BQ_SESSION_CHUNK, &identity, chunk, count + 8) == BQ_OK);
            if (!cursor)
            {
                chunk[8] ^= 1;
                BQ_CHECK(bq_test_offhost_message(&control, BQ_SESSION_CHUNK, &identity, chunk, count + 8) == BQ_CONFLICT);
            }
        }
        close(archive);
        BQ_CHECK(bq_test_offhost_message(&control, BQ_SESSION_COMPLETE, &identity, NULL, 0) == BQ_OK);
        BQ_CHECK(bq_test_worker_probe_locked(worker.lease));
        bq_close(&control.queue);
        BQ_CHECK(bq_control_open(&control, root, machine) == BQ_OK && control.identity.id == id);
        BQ_CHECK(bq_test_offhost_message(&control, BQ_SESSION_COMPLETE, &identity, NULL, 0) == BQ_OK);
        bq_packet(&request, BQ_OP_RESULT, 4, body, sizeof(body));
        BQ_CHECK(bq_control_public(&control, request.bytes, request.size, &response) == BQ_OK &&
                 bq_public_response_valid(&request, &response) && bq_u32(response.bytes + BQ_CONTROL_HEADER + 32) == BQ_SUCCEEDED);
        BQ_CHECK(!strcmp(control.queue.state.jobs[0].result_root, job->result_root));
        if (cancel_intent) BQ_CHECK(bq_offhost_marked(&control.queue, "cancel-too-late", &identity) == BQ_OK);
        BQ_CHECK(bq_test_offhost_message(&control, BQ_SESSION_TERMINAL_ACK, &identity, NULL, 0) == BQ_OK && !control.identity.id);
        BQ_CHECK(bq_test_offhost_message(&control, BQ_SESSION_HELLO, &identity, NULL, 0) == BQ_OK);
        bq_worker_lease_release(&retained);
        BQ_CHECK(!bq_test_worker_probe_locked(worker.lease));
        if (finalization.result_directory >= 0) close(finalization.result_directory);
        BQ_CHECK(unlinkat(local->directory_fd, sealed, 0) == 0);
        bq_test_worker_end(&worker);
        bq_packet(&request, BQ_OP_RESULT, 4, body, sizeof(body));
        BQ_CHECK(bq_control_public(&control, request.bytes, request.size, &response) == BQ_OK);
    }
    bq_close(&control.queue);
    bq_test_offhost_remove(root);
}

BUSTER_GLOBAL_LOCAL void bq_test_offhost_quiet_load(void)
{
    char root[BQ_PATH_CAP + 1] = "/tmp/buster-offhost-daemon-XXXXXX";
    BQ_CHECK(bq_test_mkdtemp_physical(root, sizeof(root)));
    char public[BQ_PATH_CAP + 1], worker[BQ_PATH_CAP + 1];
    snprintf(public, sizeof(public), "%s/public.sock", root);
    snprintf(worker, sizeof(worker), "%s/worker.sock", root);
    char machine[SHA256_HEX_CAPACITY];
    bq_digest(BQ_OFFHOST_MACHINE, sizeof(BQ_OFFHOST_MACHINE) - 1, machine);
    pid_t daemon = fork();
    BQ_CHECK(daemon >= 0);
    if (daemon == 0) _exit(bq_control_serve(root, public, worker, machine) == BQ_OK ? 0 : 1);
    bool ready = false;
    for (u32 i = 0; daemon > 0 && !ready && i < 100; i += 1)
    {
        struct stat info;
        ready = lstat(worker, &info) == 0 && S_ISSOCK(info.st_mode);
        if (!ready) poll(NULL, 0, 10);
    }
    BQ_CHECK(ready);
    BqPacket request, response;
    BqRequest work = bq_test_offhost_request(800);
    bq_packet(&request, BQ_OP_SUBMIT, 1, work.bytes, work.size);
    BQ_CHECK(bq_transport_request(public, &request, &response) == BQ_OK && bq_public_response_valid(&request, &response));
    int session = -1;
    BQ_CHECK(bq_offhost_connect(worker, &session) == BQ_OK);
    if (session >= 0)
    {
        BqOffhostIdentity identity = {0};
        bq_digest(BQ_OFFHOST_MACHINE, sizeof(BQ_OFFHOST_MACHINE) - 1, identity.machine);
        BqSessionPacket packet, reply;
        bq_session_packet(&packet, BQ_SESSION_HELLO, BQ_OK, &identity, NULL, 0);
        BQ_CHECK(bq_session_send(session, &packet) == BQ_OK && bq_transport_wait_for(session, POLLIN, 1000) == BQ_OK &&
                 bq_session_receive(session, &reply) == BQ_OK && bq_session_valid(&reply, true));
        identity = bq_session_identity(&reply);
        for (u32 op = BQ_SESSION_CUSTODY; op <= BQ_SESSION_QUIET; op += 1)
        {
            u8 custody[129];
            bq_digest("fixture-install", 15, (char*)custody);
            bq_digest("fixture-boot", 12, (char*)custody + 64);
            bq_session_packet(&packet, op, BQ_OK, &identity, op == BQ_SESSION_CUSTODY ? custody : NULL,
                              op == BQ_SESSION_CUSTODY ? 128u : 0);
            BQ_CHECK(bq_session_send(session, &packet) == BQ_OK && bq_transport_wait_for(session, POLLIN, 1000) == BQ_OK &&
                     bq_session_receive(session, &reply) == BQ_OK && !bq_u32(reply.bytes + 20));
        }
        u32 accepted = 0, full = 0;
        for (u32 i = 0; i < 100; i += 1)
        {
            work = bq_test_offhost_request(801 + i);
            bq_packet(&request, BQ_OP_SUBMIT, 10 + i, work.bytes, work.size);
            BqError error = bq_transport_request(public, &request, &response);
            BQ_CHECK(error == BQ_OK || error == BQ_FULL);
            if (error == BQ_OK) { accepted += 1; BQ_CHECK(bq_public_response_valid(&request, &response)); }
            if (error == BQ_FULL) full += 1;
        }
        BQ_CHECK(accepted == BQ_PENDING_CAP - 1 && accepted + full == 100);
        u8 body[8];
        bq_put64(body, identity.id);
        for (u32 i = 0; i < 1000; i += 1)
        {
            bq_packet(&request, i % 2 ? BQ_OP_STATUS : BQ_OP_RESULT, 200 + i, body, sizeof(body));
            BQ_CHECK(bq_transport_request(public, &request, &response) == BQ_OK &&
                     bq_public_response_valid(&request, &response) &&
                     bq_u32(response.bytes + BQ_CONTROL_HEADER + 28) == BQ_RESERVED);
        }
        struct pollfd dormant = {session, POLLIN, 0};
        BQ_CHECK(poll(&dormant, 1, 0) == 0);
        bq_packet(&request, BQ_OP_CANCEL, 1201, body, sizeof(body));
        BQ_CHECK(bq_transport_request(public, &request, &response) == BQ_OK);
        BQ_CHECK(bq_transport_wait_for(session, POLLIN, 1000) == BQ_OK && bq_session_receive(session, &reply) == BQ_OK &&
                 bq_offhost_cancel_identity(&reply, &identity));
        close(session);
        printf("OFFHOST_QUIET_LOAD submits=100 accepted=%u full=%u reads=1000 ordinary-worker-messages=0 cancellation=exceptional\n", accepted, full);
    }
    if (daemon > 0)
    {
        int status = 0;
        kill(daemon, SIGTERM);
        BQ_CHECK(waitpid(daemon, &status, 0) == daemon && WIFEXITED(status) && !WEXITSTATUS(status));
    }
    bq_test_offhost_remove(root);
}
#endif
