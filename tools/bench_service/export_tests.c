/* Deterministic native export round trips and fail-closed boundary tests.
 * Included after worker fixture definitions; all paths are disposable fixtures.
 */
#ifdef __linux__
BUSTER_GLOBAL_LOCAL void bq_test_export_request(BqPacket* packet, BqJob const* job, u64 cursor, char const* receipt)
{
    u8 body[BQ_EXPORT_REQUEST_CAP] = {0};
    bq_put64(body, job->id);
    bq_put64(body + 8, job->token);
    memcpy(body + 16, job->result_full_digest, 64);
    bq_put64(body + 80, cursor);
    if (receipt) memcpy(body + 88, receipt, 64);
    bq_packet(packet, BQ_OP_EXPORT, 878, body, sizeof(body));
}

BUSTER_GLOBAL_LOCAL void bq_test_export_socket(BqQueue* queue, char const* queue_path, BqJob const* job,
                                                char const* archive, char const* root, char const* receipt)
{
    int probe = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    if (probe < 0)
    {
        BQ_CHECK(errno == EPERM || errno == EAFNOSUPPORT || errno == ENOSYS);
        printf("EXPORT_TRANSPORT_TEST result=unavailable-socket-policy\n");
    }
    else
    {
        close(probe);
        char socket_path[BQ_PATH_CAP + 1], downloaded[BQ_PATH_CAP + 64], replay[BQ_PATH_CAP + 64];
        char id[32], token[32];
        snprintf(socket_path, sizeof(socket_path), "%s/control.sock", queue_path);
        snprintf(downloaded, sizeof(downloaded), "%s/downloaded.bq", root);
        snprintf(replay, sizeof(replay), "%s/download-replay", root);
        snprintf(id, sizeof(id), "%llu", (unsigned long long)job->id);
        snprintf(token, sizeof(token), "%llu", (unsigned long long)job->token);
        BqJob saved = *job;
        u64 sequence = queue->state.sequence;
        bq_close(queue);
        pid_t child = fork();
        BQ_CHECK(child >= 0);
        if (child == 0) _exit(bq_transport_serve(queue_path, socket_path, NULL) == BQ_OK ? 0 : 1);
        if (child > 0)
        {
            struct stat info = {0};
            bool ready = false;
            for (u32 i = 0; !ready && i < 200; i += 1)
            {
                ready = lstat(socket_path, &info) == 0 && S_ISSOCK(info.st_mode);
                if (!ready) usleep(10000);
            }
            BQ_CHECK(ready);
            FILE* output = fopen(downloaded, "wb");
            FILE* diagnostics = tmpfile();
            FILE* input = tmpfile();
            char* arguments[] = {"bench_service", "client", socket_path, "export", id, token, (char*)saved.result_full_digest};
            BQ_CHECK(output && diagnostics && input && bq_cli(7, arguments, input, output, diagnostics) == 0);
            if (output) fclose(output);
            if (diagnostics) fclose(diagnostics);
            if (input) fclose(input);
            char original_digest[SHA256_HEX_CAPACITY], downloaded_digest[SHA256_HEX_CAPACITY];
            BQ_CHECK(bq_test_file_sha256(archive, original_digest) && bq_test_file_sha256(downloaded, downloaded_digest) &&
                     !memcmp(original_digest, downloaded_digest, sizeof(original_digest)) &&
                     bq_export_unpack(downloaded, replay, receipt) == BQ_OK);
            BqPacket request, response;
            bq_test_export_request(&request, &saved, 0, receipt);
            /* Send then disconnect before receiving: no cursor/queue mutation. */
            int client = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
            struct sockaddr_un address = {.sun_family = AF_UNIX};
            size_t length = strlen(socket_path);
            bool short_path = length < sizeof(address.sun_path);
            if (short_path) memcpy(address.sun_path, socket_path, length + 1);
            BQ_CHECK(client >= 0 && short_path && connect(client, (struct sockaddr*)&address,
                     (socklen_t)(offsetof(struct sockaddr_un, sun_path) + length + 1)) == 0 &&
                     bq_transport_send(client, request.bytes, request.size) == BQ_OK);
            if (client >= 0) close(client);
            BQ_CHECK(bq_transport_request(socket_path, &request, &response) == BQ_OK &&
                     bq_public_response_valid(&request, &response));
            BQ_CHECK(kill(child, SIGTERM) == 0);
            int status = 0;
            BQ_CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
        }
        BQ_CHECK(bq_open(queue, queue_path) == BQ_OK && queue->state.sequence == sequence);
    }
}

BUSTER_GLOBAL_LOCAL void bq_test_export(bool success)
{
    BqWorkerFixture fixture;
    if (bq_test_worker_begin(&fixture, BQ_WORKER_SUCCEEDED, false))
    {
        BqQueue* queue = &fixture.material.queue.queue;
        BqRequest original = bq_test_real_request(878), submission;
        String8 fields[BQ_FIELD_COUNT] = {S8(BQ_EXPORT_PRINCIPAL), bq_field(&original, 1), bq_field(&original, 2),
                                         bq_field(&original, 3), bq_field(&original, 4)};
        BQ_CHECK(bq_request_make(fields, &submission) == BQ_OK);
        u64 id = 0, token = 0;
        BQ_CHECK(bq_submit(queue, &submission, &id) == BQ_OK &&
                 bq_materialize(queue, fixture.config.installed_root, fixture.config.workspace_root, &id, &token) == BQ_OK);
        BqJob* job = bq_job(&queue->state, id);
        BqWorkerFinalization finalization = {.config = &fixture.config, .result_directory = -1};
        BQ_CHECK(job != NULL);
        if (!success) BQ_CHECK(bq_worker_result_open(&fixture.config, job, &finalization, true) == BQ_OK);
        BqOutcome outcome = success ? BQ_SUCCEEDED : BQ_FAILED;
        if (success) BQ_CHECK(bq_test_worker_make_success_result(&fixture, job, &finalization));
        else
        {
            u8 bytes[BQ_EXPORT_CHUNK_CAP * 2 + 57];
            for (u32 i = 0; i < sizeof(bytes); i += 1) bytes[i] = (u8)(i * 13);
            int payload = openat(finalization.result_directory, "payload.bin", O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0400);
            BQ_CHECK(payload >= 0 && write(payload, bytes, sizeof(bytes)) == sizeof(bytes));
            if (payload >= 0) close(payload);
            BQ_CHECK(mkdirat(finalization.result_directory, "empty", 0700) == 0);
            BQ_CHECK(bq_worker_result_evidence(job, outcome, BQ_WORKER_FAILED, &finalization) == BQ_OK &&
                     bq_worker_result_failure_artifacts(job, outcome, BQ_WORKER_FAILED, &finalization) == BQ_OK);
        }
        for (BqPhase phase = BQ_SETTLING; phase <= BQ_CLEANING; phase = (BqPhase)(phase + 1))
        {
            BQ_CHECK(bq_real_advance(queue, job, phase, phase >= BQ_FINALIZING ? outcome : BQ_NO_OUTCOME) == BQ_OK);
            job = bq_job(&queue->state, id);
        }
        BQ_CHECK(bq_result_bind(queue, job, string_from_pointer(finalization.result_root), finalization.result_digest,
                               finalization.bundle_digest, finalization.full_digest) == BQ_OK);
        job = bq_job(&queue->state, id);
        BqPacket request, response, repeated;
        bq_test_export_request(&request, job, UINT64_MAX, NULL);
        BQ_CHECK(bq_transport_dispatch(queue, request.bytes, request.size, &response) == BQ_EXPORT_NOT_FINALIZED);
        BQ_CHECK(bq_real_advance(queue, job, BQ_FINISHED, outcome) == BQ_OK);
        job = bq_job(&queue->state, id);
        BQ_CHECK(job && bq_worker_result_binding_validate(job) == BQ_OK);
        u64 sequence = queue->state.sequence, journal_bytes = queue->bytes;
        if (!success)
        {
            bq_export_test_stall = true;
            BQ_CHECK(bq_transport_dispatch(queue, request.bytes, request.size, &response) == BQ_EXPORT_TIMEOUT);
            bq_export_test_stall = false;
            BQ_CHECK(queue->state.sequence == sequence && queue->bytes == journal_bytes &&
                     bq_worker_result_binding_validate(job) == BQ_OK);
        }
        BqError prepared = bq_transport_dispatch(queue, request.bytes, request.size, &response);
        if (prepared != BQ_OK) fprintf(stderr, "EXPORT_TEST prepare=%s\n", bq_error_name(prepared));
        BQ_CHECK(prepared == BQ_OK && bq_public_response_valid(&request, &response));
        u64 full_index_checks = bq_export_index_full_checks;
        BQ_CHECK(bq_transport_dispatch(queue, request.bytes, request.size, &repeated) == BQ_OK &&
                 response.size == repeated.size && !memcmp(response.bytes, repeated.bytes, response.size) &&
                 bq_export_index_full_checks == full_index_checks);
        char published[80], pending[80];
        BQ_CHECK(bq_export_name(published, id, token, false) && bq_export_name(pending, id, token, true) &&
                 linkat(queue->directory_fd, published, queue->directory_fd, pending, 0) == 0 &&
                 bq_transport_dispatch(queue, request.bytes, request.size, &repeated) == BQ_OK &&
                 response.size == repeated.size && !memcmp(response.bytes, repeated.bytes, response.size));
        u8 receipt[BQ_EXPORT_RECEIPT_CAP];
        memcpy(receipt, response.bytes + BQ_CONTROL_HEADER + BQ_EXPORT_REPLY_HEADER, sizeof(receipt));
        char digest[SHA256_HEX_CAPACITY];
        bq_digest(receipt, sizeof(receipt), digest);
        u64 total = bq_u64(receipt + 24);
        BQ_CHECK(total > 0 && total <= BQ_EXPORT_TOTAL_CAP);
        char archive[BQ_PATH_CAP + 64], replay[BQ_PATH_CAP + 64];
        snprintf(archive, sizeof(archive), "%s/export.bq", fixture.root);
        snprintf(replay, sizeof(replay), "%s/replayed", fixture.root);
        FILE* file = fopen(archive, "wb");
        BQ_CHECK(file && fwrite(receipt, 1, sizeof(receipt), file) == sizeof(receipt));
        for (u64 cursor = 0; file && cursor < total;)
        {
            bq_test_export_request(&request, job, cursor, digest);
            BQ_CHECK(bq_transport_dispatch(queue, request.bytes, request.size, &response) == BQ_OK &&
                     bq_public_response_valid(&request, &response));
            /* Recovery of a pending export may replace spool metadata once;
             * the immediately repeated read must reuse its verified index. */
            full_index_checks = bq_export_index_full_checks;
            BQ_CHECK(bq_transport_dispatch(queue, request.bytes, request.size, &repeated) == BQ_OK &&
                     response.size == repeated.size && !memcmp(response.bytes, repeated.bytes, response.size) &&
                     bq_export_index_full_checks == full_index_checks);
            u8 const* body = response.bytes + BQ_CONTROL_HEADER;
            u32 count = bq_u32(body + 44);
            BQ_CHECK(count > 0 && count <= BQ_EXPORT_CHUNK_CAP && response.size <= BQ_PACKET_CAP);
            if (!count || count > BQ_EXPORT_CHUNK_CAP) break;
            BQ_CHECK(fwrite(body + BQ_EXPORT_REPLY_HEADER, 1, count, file) == count);
            cursor += count;
            /* No response state is committed: an identical cursor survives a
             * dropped response and a real close/replay of the queue journal. */
            bq_close(queue);
            BQ_CHECK(bq_open(queue, fixture.material.queue.path) == BQ_OK);
            job = bq_job(&queue->state, id);
        }
        if (file) BQ_CHECK(fclose(file) == 0);
        BQ_CHECK(bq_export_unpack(archive, replay, digest) == BQ_OK);
        int replay_root = open(replay, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        BQ_CHECK(replay_root >= 0 && bq_worker_result_binding_validate_at(job, replay_root) == BQ_OK);
        if (!success) BQ_CHECK(faccessat(replay_root, "empty", F_OK, 0) == 0);
        if (replay_root >= 0) close(replay_root);
        if (success) bq_test_export_socket(queue, fixture.material.queue.path, job, archive, fixture.root, digest);
        job = bq_job(&queue->state, id);
        /* The public absence response cannot disclose a foreign job. */
        bq_test_export_request(&request, job, 0, digest);
        BqJob* authorized = NULL;
        BQ_CHECK(bq_export_authorize(queue, request.bytes + BQ_CONTROL_HEADER, S8("other-principal"), &authorized) == BQ_NOT_FOUND && !authorized);
        bq_put64(request.bytes + BQ_CONTROL_HEADER, UINT64_MAX);
        BQ_CHECK(bq_transport_dispatch(queue, request.bytes, request.size, &response) == BQ_NOT_FOUND);
        bq_test_export_request(&request, job, 0, digest);
        bq_put64(request.bytes + BQ_CONTROL_HEADER + 8, token + 1);
        BQ_CHECK(bq_transport_dispatch(queue, request.bytes, request.size, &response) == BQ_CONFLICT);
        bq_test_export_request(&request, job, 0, digest);
        request.bytes[BQ_CONTROL_HEADER + 16] ^= 1;
        BQ_CHECK(bq_transport_dispatch(queue, request.bytes, request.size, &response) != BQ_OK);
        bq_test_export_request(&request, job, 1, digest);
        BQ_CHECK(bq_transport_dispatch(queue, request.bytes, request.size, &response) == BQ_BAD_REQUEST);
        bq_test_export_request(&request, job, total, digest);
        BQ_CHECK(bq_transport_dispatch(queue, request.bytes, request.size, &response) == BQ_BAD_REQUEST);
        bq_test_export_request(&request, job, 0, digest);
        request.bytes[BQ_CONTROL_HEADER + 88] = digest[0] == 'a' ? 'b' : 'a';
        BQ_CHECK(bq_transport_dispatch(queue, request.bytes, request.size, &response) == BQ_CONFLICT);
        bq_test_export_request(&request, job, 0, digest);
        BQ_CHECK(bq_transport_public_request(request.bytes, request.size - 1) == BQ_BAD_REQUEST);
        BQ_CHECK(queue->state.sequence == sequence && queue->bytes == journal_bytes && bq_worker_result_binding_validate(job) == BQ_OK);
        /* A truncated download has no successful independent replay. */
        BQ_CHECK(truncate(archive, (off_t)(sizeof(receipt) + total - 1)) == 0 &&
                 bq_export_unpack(archive, replay, digest) == BQ_EXPORT_CORRUPT);
        char sealed[80];
        BQ_CHECK(bq_export_name(sealed, id, token, false));
        int spool = openat(queue->directory_fd, sealed, O_RDWR | O_CLOEXEC);
        if (spool < 0)
        {
            BQ_CHECK(fchmodat(queue->directory_fd, sealed, 0600, 0) == 0);
            spool = openat(queue->directory_fd, sealed, O_RDWR | O_CLOEXEC);
        }
        BQ_CHECK(spool >= 0);
        if (spool >= 0)
        {
            u8 saved = 0;
            BQ_CHECK(pread(spool, &saved, 1, BQ_EXPORT_DATA_OFFSET) == 1 && fchmod(spool, 0400) == 0);
            bq_export_test_mutation_fd = spool;
            BQ_CHECK(bq_transport_dispatch(queue, request.bytes, request.size, &response) == BQ_EXPORT_CORRUPT);
            bq_export_test_mutation_fd = -1;
            BQ_CHECK(pwrite(spool, &saved, 1, BQ_EXPORT_DATA_OFFSET) == 1 &&
                     bq_transport_dispatch(queue, request.bytes, request.size, &response) == BQ_OK);
            u8 changed = 0xff;
            BQ_CHECK(pwrite(spool, &changed, 1, BQ_EXPORT_DATA_OFFSET) == 1 && fchmod(spool, 0400) == 0);
            BQ_CHECK(bq_transport_dispatch(queue, request.bytes, request.size, &response) == BQ_EXPORT_CORRUPT);
            close(spool);
        }
        BQ_CHECK(queue->state.sequence == sequence && queue->bytes == journal_bytes && bq_worker_result_binding_validate(job) == BQ_OK);
        BQ_CHECK(unlinkat(queue->directory_fd, sealed, 0) == 0);
        BQ_CHECK(bq_transport_dispatch(queue, request.bytes, request.size, &response) == BQ_EXPORT_MISSING);
        bool bound = job->result_bound;
        job->result_bound = false;
        BQ_CHECK(bq_export_authorize(queue, request.bytes + BQ_CONTROL_HEADER, S8(BQ_EXPORT_PRINCIPAL), &authorized) == BQ_EXPORT_INVALID);
        BqOutcome saved_outcome = job->outcome;
        job->outcome = BQ_INTERRUPTED;
        BQ_CHECK(bq_export_authorize(queue, request.bytes + BQ_CONTROL_HEADER, S8(BQ_EXPORT_PRINCIPAL), &authorized) == BQ_EXPORT_INTERRUPTED);
        job->outcome = saved_outcome;
        job->result_bound = bound;
        if (finalization.result_directory >= 0) close(finalization.result_directory);
        bq_test_worker_end(&fixture);
    }
}

BUSTER_GLOBAL_LOCAL void bq_test_export_inventory(void)
{
    char path[BQ_PATH_CAP + 1] = "/tmp/bq-export-paths-XXXXXX";
    BQ_CHECK(bq_test_mkdtemp_physical(path, sizeof(path)));
    int root = open(path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    BqExportInventory* inventory = mmap(NULL, sizeof(*inventory), PROT_READ | PROT_WRITE,
                                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    u64 deadline = bq_worker_deadline(bq_worker_monotonic_milliseconds(), BQ_EXPORT_PREPARE_MILLISECONDS);
    BQ_CHECK(root >= 0 && inventory != MAP_FAILED);
    if (root >= 0 && inventory != MAP_FAILED)
    {
        int file = openat(root, "regular", O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
        BQ_CHECK(file >= 0 && write(file, "a", 1) == 1 && bq_export_inventory(root, inventory, deadline) == BQ_OK &&
                 inventory->files == 1 && inventory->bytes == 1 && bq_export_inventory_unchanged(root, inventory));
        BQ_CHECK(pwrite(file, "b", 1, 0) == 1 && !bq_export_inventory_unchanged(root, inventory));
        BQ_CHECK(bq_export_inventory(root, inventory, deadline) == BQ_OK);
        BQ_CHECK(renameat(root, "regular", root, "replacement") == 0 && !bq_export_inventory_unchanged(root, inventory));
        BQ_CHECK(renameat(root, "replacement", root, "regular") == 0);
        BQ_CHECK(linkat(root, "regular", root, "alias", 0) == 0 && bq_export_inventory(root, inventory, deadline) == BQ_EXPORT_CORRUPT);
        BQ_CHECK(unlinkat(root, "alias", 0) == 0);
        BQ_CHECK(ftruncate(file, (off_t)BQ_WORKER_BUNDLE_FILE_CAP + 1) == 0 &&
                 bq_export_inventory(root, inventory, deadline) == BQ_EXPORT_OVERSIZED);
        BQ_CHECK(ftruncate(file, 1) == 0);
        BQ_CHECK(symlinkat("../outside", root, "alias") == 0 && bq_export_inventory(root, inventory, deadline) == BQ_EXPORT_CORRUPT);
        BQ_CHECK(unlinkat(root, "alias", 0) == 0);
        BQ_CHECK(mkfifoat(root, "fifo", 0600) == 0 && bq_export_inventory(root, inventory, deadline) == BQ_EXPORT_CORRUPT);
        BQ_CHECK(unlinkat(root, "fifo", 0) == 0);
        u8 byte = 0;
        BQ_CHECK(bq_export_io(file, &byte, 1, 0, false, 0) == BQ_EXPORT_TIMEOUT);
        if (file >= 0) close(file);
        BQ_CHECK(unlinkat(root, "regular", 0) == 0);
        int directory = fcntl(root, F_DUPFD_CLOEXEC, 3);
        for (u32 i = 0; directory >= 0 && i < 100; i += 1)
        {
            BQ_CHECK(mkdirat(directory, "a", 0700) == 0);
            int child = openat(directory, "a", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            close(directory);
            directory = child;
        }
        if (directory >= 0) close(directory);
        BQ_CHECK(bq_export_inventory(root, inventory, deadline) == BQ_EXPORT_OVERSIZED);
        BQ_CHECK(bq_remove_workspace_payload(root));
        for (u32 i = 0; i <= BQ_WORKER_BUNDLE_ENTRY_CAP; i += 1)
        {
            char name[32];
            snprintf(name, sizeof(name), "file-%u", i);
            int fd = openat(root, name, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0400);
            BQ_CHECK(fd >= 0);
            if (fd >= 0) close(fd);
        }
        BQ_CHECK(bq_export_inventory(root, inventory, deadline) == BQ_EXPORT_OVERSIZED);
        /* The generic worker cleanup intentionally has the same entry cap. */
        for (u32 i = 0; i <= BQ_WORKER_BUNDLE_ENTRY_CAP; i += 1)
        {
            char name[32];
            snprintf(name, sizeof(name), "file-%u", i);
            BQ_CHECK(unlinkat(root, name, 0) == 0);
        }
        BQ_CHECK(!bq_worker_bundle_path_valid("../escape") && !bq_worker_bundle_path_valid("a/../escape") &&
                 !bq_worker_bundle_path_valid("/absolute") && !bq_worker_bundle_path_valid("a//b"));
    }
    if (inventory != MAP_FAILED) munmap(inventory, sizeof(*inventory));
    if (root >= 0) close(root);
    BQ_CHECK(rmdir(path) == 0);
}
#endif
