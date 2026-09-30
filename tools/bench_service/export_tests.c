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

/* #1024: the chunk-index cache must never outlive the spool it verified.
 * With the cache warm, a self-consistent rewrite (chunk bytes and their index
 * digest changed together, so the per-chunk check alone would pass) must
 * fail, both when its metadata change forces a full check and, forced here,
 * when metadata is unchanged and the cached page digest must catch it. A
 * byte-identical spool on a new inode must be rechecked in full before the
 * cache is reused. The named spool is restored to the caller's `spool` inode
 * before returning. */
BUSTER_GLOBAL_LOCAL void bq_test_export_cached_spool(BqQueue* queue, int spool, char const* sealed,
                                                     BqPacket const* request, u64 total)
{
    BqPacket response;
    u64 checks = bq_export_index_full_checks;
    BQ_CHECK(bq_transport_dispatch(queue, request->bytes, request->size, &response) == BQ_OK &&
             bq_export_index_full_checks == checks);
    u8 original[BQ_EXPORT_CHUNK_CAP], forged[BQ_EXPORT_CHUNK_CAP];
    char original_index[64], forged_index[SHA256_HEX_CAPACITY];
    u32 count = (u32)(total < BQ_EXPORT_CHUNK_CAP ? total : BQ_EXPORT_CHUNK_CAP);
    bool read = pread(spool, original, count, BQ_EXPORT_DATA_OFFSET) == (ssize_t)count &&
                pread(spool, original_index, 64, BQ_EXPORT_RECEIPT_CAP) == 64;
    BQ_CHECK(read && count > 0);
    if (read && count > 0)
    {
        memcpy(forged, original, count);
        forged[0] ^= 0x5a;
        bq_digest(forged, count, forged_index);
        BQ_CHECK(pwrite(spool, forged, count, BQ_EXPORT_DATA_OFFSET) == (ssize_t)count &&
                 pwrite(spool, forged_index, 64, BQ_EXPORT_RECEIPT_CAP) == 64 && fchmod(spool, 0400) == 0);
        /* Whatever the filesystem's timestamp granularity, the rewrite fails. */
        BQ_CHECK(bq_transport_dispatch(queue, request->bytes, request->size, &response) == BQ_EXPORT_CORRUPT);
        BQ_CHECK(pwrite(spool, original, count, BQ_EXPORT_DATA_OFFSET) == (ssize_t)count &&
                 pwrite(spool, original_index, 64, BQ_EXPORT_RECEIPT_CAP) == 64 && fchmod(spool, 0400) == 0 &&
                 bq_transport_dispatch(queue, request->bytes, request->size, &response) == BQ_OK);
        /* Deterministically take the cached path: pretend the rewrite left
         * every inode timestamp unchanged. The rehashed index page catches it
         * without a full check. */
        BQ_CHECK(pwrite(spool, forged, count, BQ_EXPORT_DATA_OFFSET) == (ssize_t)count &&
                 pwrite(spool, forged_index, 64, BQ_EXPORT_RECEIPT_CAP) == 64 && fchmod(spool, 0400) == 0 &&
                 fstat(spool, &bq_export_index_cache.identity) == 0 && bq_export_index_cache.valid);
        checks = bq_export_index_full_checks;
        BQ_CHECK(bq_transport_dispatch(queue, request->bytes, request->size, &response) == BQ_EXPORT_CORRUPT &&
                 bq_export_index_full_checks == checks && !bq_export_index_cache.valid);
        BQ_CHECK(pwrite(spool, original, count, BQ_EXPORT_DATA_OFFSET) == (ssize_t)count &&
                 pwrite(spool, original_index, 64, BQ_EXPORT_RECEIPT_CAP) == 64 && fchmod(spool, 0400) == 0 &&
                 bq_transport_dispatch(queue, request->bytes, request->size, &response) == BQ_OK &&
                 bq_export_index_full_checks == checks + 1);
    }
    /* Replace the name with a byte-identical copy on a new inode. */
    struct stat info = {0};
    BQ_CHECK(fstat(spool, &info) == 0 && renameat(queue->directory_fd, sealed, queue->directory_fd,
                                                   "export-cache-original.hold") == 0);
    int copy = openat(queue->directory_fd, sealed, O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    BQ_CHECK(copy >= 0);
    for (u64 offset = 0; copy >= 0 && offset < (u64)info.st_size;)
    {
        u8 bytes[BQ_EXPORT_CHUNK_CAP];
        u64 remaining = (u64)info.st_size - offset;
        size_t chunk = remaining < sizeof(bytes) ? (size_t)remaining : sizeof(bytes);
        bool moved = pread(spool, bytes, chunk, (off_t)offset) == (ssize_t)chunk &&
                     pwrite(copy, bytes, chunk, (off_t)offset) == (ssize_t)chunk;
        BQ_CHECK(moved);
        offset = moved ? offset + chunk : (u64)info.st_size;
    }
    if (copy >= 0)
    {
        BQ_CHECK(fchmod(copy, 0400) == 0 && close(copy) == 0);
        checks = bq_export_index_full_checks;
        BQ_CHECK(bq_transport_dispatch(queue, request->bytes, request->size, &response) == BQ_OK &&
                 bq_export_index_full_checks == checks + 1);
        BQ_CHECK(bq_transport_dispatch(queue, request->bytes, request->size, &response) == BQ_OK &&
                 bq_export_index_full_checks == checks + 1);
        BQ_CHECK(unlinkat(queue->directory_fd, sealed, 0) == 0);
    }
    BQ_CHECK(renameat(queue->directory_fd, "export-cache-original.hold", queue->directory_fd, sealed) == 0);
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
        BQ_CHECK(bq_transport_response_wait(request.bytes, request.size) == (int)BQ_EXPORT_PREPARE_MILLISECONDS + 5000);
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
        char id_text[32], token_text[32];
        snprintf(id_text, sizeof(id_text), "%llu", (unsigned long long)id);
        snprintf(token_text, sizeof(token_text), "%llu", (unsigned long long)token);
        char* hinted[] = {"export", id_text, token_text, job->result_full_digest, "validate-buster-v1"};
        BqPacket typed;
        u32 operation = 0;
        BQ_CHECK(bq_client_arguments(5, hinted, true, &typed, &operation) && operation == BQ_OP_EXPORT &&
                 bq_u32(typed.bytes + BQ_CONTROL_HEADER + 88) == BQ_RECIPE_VALIDATE_BUSTER &&
                 bq_transport_public_request(typed.bytes, typed.size) == BQ_OK &&
                 bq_transport_response_wait(typed.bytes, typed.size) == (int)BQ_EXPORT_PREPARE_MILLISECONDS + 5000);
        hinted[4] = "fake-success-v1";
        BQ_CHECK(!bq_client_arguments(5, hinted, true, &typed, &operation));
        hinted[4] = "native-retirement-performance-v1";
        BQ_CHECK(bq_client_arguments(5, hinted, true, &typed, &operation) &&
                 bq_transport_response_wait(typed.bytes, typed.size) ==
                     (int)BQ_EXPORT_RETIREMENT_PREPARE_MILLISECONDS + 5000 &&
                 bq_transport_dispatch(queue, typed.bytes, typed.size, &response) == BQ_CONFLICT &&
                 queue->state.sequence == sequence && queue->bytes == journal_bytes);
        bq_put32(typed.bytes + BQ_CONTROL_HEADER + 88, BQ_RECIPE_FAKE_SUCCESS);
        BQ_CHECK(bq_transport_public_request(typed.bytes, typed.size) == BQ_BAD_REQUEST);
        bq_put32(typed.bytes + BQ_CONTROL_HEADER + 88, BQ_RECIPE_VALIDATE_BUSTER);
        typed.bytes[BQ_CONTROL_HEADER + 92] = 1;
        BQ_CHECK(bq_transport_public_request(typed.bytes, typed.size) == BQ_BAD_REQUEST);
        typed.bytes[BQ_CONTROL_HEADER + 92] = 0;
        BQ_CHECK(bq_transport_dispatch(queue, typed.bytes, typed.size, &response) == BQ_OK &&
                 bq_public_response_valid(&typed, &response));
        bq_put32(typed.bytes + BQ_CONTROL_HEADER + 88, BQ_RECIPE_NATIVE_RETIREMENT_BLOCKED);
        BQ_CHECK(!bq_public_response_valid(&typed, &response));
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
        u8 oversized[BQ_EXPORT_RECEIPT_CAP];
        memcpy(oversized, receipt, sizeof(oversized));
        bq_put64(oversized + 24, BQ_EXPORT_TOTAL_CAP + 1);
        BQ_CHECK(bq_export_receipt_recipe(receipt) == BQ_RECIPE_VALIDATE_BUSTER &&
                 !bq_export_receipt_valid(oversized));
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
            BQ_CHECK(bq_transport_response_wait(request.bytes, request.size) == BQ_TRANSPORT_CLIENT_MILLISECONDS);
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
            bq_test_export_cached_spool(queue, spool, sealed, &request, total);
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
        /* #881 recovery L2: a bound, finished result with a poison record
         * never exports; without the record it is authorized again. */
        BQ_CHECK(bq_export_authorize(queue, request.bytes + BQ_CONTROL_HEADER, S8(BQ_EXPORT_PRINCIPAL), &authorized) == BQ_OK &&
                 bq_retirement_poison_write(queue, job, false, false, "incomplete") == BQ_OK);
        BQ_CHECK(bq_export_authorize(queue, request.bytes + BQ_CONTROL_HEADER, S8(BQ_EXPORT_PRINCIPAL), &authorized) ==
                 BQ_EXPORT_INVALID && !authorized);
        char poison[48];
        BQ_CHECK(bq_record_name(poison, BQ_RETIREMENT_POISON_RECORD, id) && unlinkat(queue->directory_fd, poison, 0) == 0 &&
                 bq_export_authorize(queue, request.bytes + BQ_CONTROL_HEADER, S8(BQ_EXPORT_PRINCIPAL), &authorized) == BQ_OK);
        if (finalization.result_directory >= 0) close(finalization.result_directory);
        bq_test_worker_end(&fixture);
    }
}

BUSTER_GLOBAL_LOCAL void bq_test_export_inventory(void)
{
    BqRecipeFiles smoke = {0}, retirement = {0};
    BQ_CHECK(bq_recipe_files(BQ_RECIPE_VALIDATE_BUSTER, &smoke) &&
             bq_recipe_files(BQ_RECIPE_NATIVE_RETIREMENT_BLOCKED, &retirement) &&
             bq_worker_bundle_total_cap(&smoke) == BQ_WORKER_BUNDLE_TOTAL_CAP &&
             bq_worker_bundle_total_cap(&retirement) == BQ_WORKER_RETIREMENT_BUNDLE_TOTAL_CAP &&
             bq_export_total_cap(BQ_RECIPE_VALIDATE_BUSTER) == BQ_EXPORT_TOTAL_CAP &&
             bq_export_data_offset(BQ_RECIPE_VALIDATE_BUSTER) == BQ_EXPORT_DATA_OFFSET &&
             bq_export_total_cap(BQ_RECIPE_NATIVE_RETIREMENT_BLOCKED) > UINT64_C(8720640) * 180 &&
             bq_export_data_offset(BQ_RECIPE_NATIVE_RETIREMENT_BLOCKED) > BQ_EXPORT_DATA_OFFSET &&
             bq_export_prepare_milliseconds(BQ_RECIPE_NATIVE_RETIREMENT_BLOCKED) >
                 BQ_EXPORT_PREPARE_MILLISECONDS);
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
        BQ_CHECK(file >= 0 && write(file, "a", 1) == 1 && bq_export_inventory(root, inventory, BQ_RECIPE_VALIDATE_BUSTER, deadline) == BQ_OK &&
                 inventory->files == 1 && inventory->bytes == 1 && bq_export_inventory_unchanged(root, inventory));
        BQ_CHECK(pwrite(file, "b", 1, 0) == 1 && !bq_export_inventory_unchanged(root, inventory));
        BQ_CHECK(bq_export_inventory(root, inventory, BQ_RECIPE_VALIDATE_BUSTER, deadline) == BQ_OK);
        BQ_CHECK(renameat(root, "regular", root, "replacement") == 0 && !bq_export_inventory_unchanged(root, inventory));
        BQ_CHECK(renameat(root, "replacement", root, "regular") == 0);
        BQ_CHECK(linkat(root, "regular", root, "alias", 0) == 0 && bq_export_inventory(root, inventory, BQ_RECIPE_VALIDATE_BUSTER, deadline) == BQ_EXPORT_CORRUPT);
        BQ_CHECK(unlinkat(root, "alias", 0) == 0);
        /* A full 64 MiB A1 metrics or transcript shard is exactly one
         * admissible entry: only a larger file is oversized. */
        BQ_CHECK(ftruncate(file, (off_t)BQ_WORKER_BUNDLE_FILE_CAP) == 0 &&
                 bq_export_inventory(root, inventory, BQ_RECIPE_NATIVE_RETIREMENT_BLOCKED, deadline) == BQ_OK &&
                 inventory->files == 1 && inventory->bytes == BQ_WORKER_BUNDLE_FILE_CAP);
        BQ_CHECK(ftruncate(file, (off_t)BQ_WORKER_BUNDLE_FILE_CAP + 1) == 0 &&
                 bq_export_inventory(root, inventory, BQ_RECIPE_VALIDATE_BUSTER, deadline) == BQ_EXPORT_OVERSIZED);
        BQ_CHECK(ftruncate(file, 1) == 0);
        BQ_CHECK(symlinkat("../outside", root, "alias") == 0 && bq_export_inventory(root, inventory, BQ_RECIPE_VALIDATE_BUSTER, deadline) == BQ_EXPORT_CORRUPT);
        BQ_CHECK(unlinkat(root, "alias", 0) == 0);
        BQ_CHECK(mkfifoat(root, "fifo", 0600) == 0 && bq_export_inventory(root, inventory, BQ_RECIPE_VALIDATE_BUSTER, deadline) == BQ_EXPORT_CORRUPT);
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
        BQ_CHECK(bq_export_inventory(root, inventory, BQ_RECIPE_VALIDATE_BUSTER, deadline) == BQ_EXPORT_OVERSIZED);
        BQ_CHECK(bq_remove_workspace_payload(root));
        for (u32 i = 0; i <= BQ_WORKER_BUNDLE_ENTRY_CAP; i += 1)
        {
            char name[32];
            snprintf(name, sizeof(name), "file-%u", i);
            int fd = openat(root, name, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0400);
            BQ_CHECK(fd >= 0);
            if (fd >= 0) close(fd);
        }
        BQ_CHECK(bq_export_inventory(root, inventory, BQ_RECIPE_VALIDATE_BUSTER, deadline) == BQ_EXPORT_OVERSIZED);
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

/* A sparse fixture file of `size` bytes (no buffer of that size exists)
 * whose last byte is `tail`, so a digest covers the whole length. */
BUSTER_GLOBAL_LOCAL bool bq_test_evidence_sparse(int root, char const* name, u64 size, char tail)
{
    int file = openat(root, name, O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    bool ok = file >= 0 && size && ftruncate(file, (off_t)size) == 0 &&
              pwrite(file, &tail, 1, (off_t)(size - 1)) == 1 && fchmod(file, 0400) == 0;
    if (file >= 0 && close(file) != 0) ok = false;
    return ok;
}

/* Rewrite the last byte of a sealed fixture in place: same inode and size. */
BUSTER_GLOBAL_LOCAL bool bq_test_evidence_tail(int root, char const* name, char tail)
{
    struct stat info = {0};
    bool ok = fchmodat(root, name, 0600, 0) == 0;
    int file = ok ? openat(root, name, O_WRONLY | O_NOFOLLOW | O_CLOEXEC) : -1;
    ok = file >= 0 && fstat(file, &info) == 0 && info.st_size > 0 &&
         pwrite(file, &tail, 1, info.st_size - 1) == 1 && fchmod(file, 0400) == 0;
    if (file >= 0 && close(file) != 0) ok = false;
    return ok;
}

/* The process's peak resident set in bytes (monotonic). */
BUSTER_GLOBAL_LOCAL u64 bq_test_peak_rss(void)
{
    struct rusage usage = {0};
    u64 result = getrusage(RUSAGE_SELF, &usage) == 0 ? (u64)usage.ru_maxrss * 1024u : UINT64_MAX;
    return result;
}

/* A retirement export archive with one file entry `path` of `size` sparse
 * bytes, and its receipt's digest. The receipt is well formed for the
 * admitted retirement recipe; the archive carries no result binding, so an
 * archive whose entries all unpack still ends BQ_EXPORT_INVALID. */
BUSTER_GLOBAL_LOCAL bool bq_test_evidence_archive(char const* archive, char const* path, u64 size,
                                                  char receipt_digest[SHA256_HEX_CAPACITY])
{
    BqRequest request = {0};
    u8 receipt[BQ_EXPORT_RECEIPT_CAP] = {0};
    u32 length = (u32)strlen(path);
    u64 total = 16u + length + size;
    bool ok = bq_test_retirement_request(BQ_EXPORT_PRINCIPAL, "evidence-cap", &request) && bq_request_valid(&request);
    int file = ok ? open(archive, O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600) : -1;
    ok = ok && file >= 0;
    if (ok)
    {
        u8 header[16] = {0};
        char tail = 'z';
        bq_put32(header, 2);
        bq_put32(header + 4, length);
        bq_put64(header + 8, size);
        ok = pwrite(file, header, sizeof(header), BQ_EXPORT_RECEIPT_CAP) == (ssize_t)sizeof(header) &&
             pwrite(file, path, length, BQ_EXPORT_RECEIPT_CAP + sizeof(header)) == (ssize_t)length &&
             ftruncate(file, (off_t)(BQ_EXPORT_RECEIPT_CAP + total)) == 0 &&
             pwrite(file, &tail, 1, (off_t)(BQ_EXPORT_RECEIPT_CAP + total - 1)) == 1;
    }
    Sha256 hash;
    sha256_init(&hash);
    for (u64 offset = 0; ok && offset < total;)
    {
        u8 bytes[BQ_EXPORT_CHUNK_CAP];
        u64 count = total - offset < sizeof(bytes) ? total - offset : sizeof(bytes);
        ok = pread(file, bytes, (size_t)count, (off_t)(BQ_EXPORT_RECEIPT_CAP + offset)) == (ssize_t)count;
        if (ok) sha256_add(&hash, bytes, count);
        offset += count;
    }
    if (ok)
    {
        String8 principal = bq_field(&request, 0), recipe = bq_field(&request, 2);
        String8 profile = bq_recipe_profile(BQ_RECIPE_NATIVE_RETIREMENT_BLOCKED);
        char digest[SHA256_HEX_CAPACITY];
        memcpy(receipt, "BQEXP001", 8);
        bq_put64(receipt + 8, 1);
        bq_put64(receipt + 16, 1);
        bq_put64(receipt + 24, total);
        bq_put64(receipt + 32, size);
        bq_put32(receipt + 40, 1);
        bq_put32(receipt + 44, 1);
        for (u32 offset = 48; offset < 496; offset += 64) memset(receipt + offset, '0', 64);
        bq_request_digest(&request, digest);
        memcpy(receipt + 48, digest, 64);
        sha256_finish_hex(&hash, (char8*)digest);
        memcpy(receipt + 304, digest, 64);
        memcpy(receipt + 496, principal.pointer, (size_t)principal.length);
        memcpy(receipt + 560, recipe.pointer, (size_t)recipe.length);
        bq_digest(profile.pointer, (u32)profile.length, digest);
        memcpy(receipt + 608, digest, 64);
        memcpy(receipt + 672, request.bytes, request.size);
        bq_put32(receipt + 992, request.size);
        bq_put32(receipt + 1012, BQ_SUCCEEDED);
        bq_put32(receipt + 1016, BQ_VALID);
        bq_put32(receipt + 1020, BQ_FINISHED);
        ok = bq_export_receipt_valid(receipt) && bq_export_receipt_recipe(receipt) == BQ_RECIPE_NATIVE_RETIREMENT_BLOCKED &&
             pwrite(file, receipt, sizeof(receipt), 0) == (ssize_t)sizeof(receipt);
        bq_digest(receipt, sizeof(receipt), receipt_digest);
    }
    if (file >= 0 && close(file) != 0) ok = false;
    return ok;
}

/* Unpack `archive` into a new `replay`; returns the result and whether the
 * entry `name` was created there with `size` bytes. The replay directory and
 * the archive are removed afterwards. */
BUSTER_GLOBAL_LOCAL BqError bq_test_evidence_unpack(char const* archive, char const* replay, char const* receipt,
                                                    char const* name, u64 size, bool* created)
{
    BqError error = bq_export_unpack(archive, replay, receipt);
    int root = open(replay, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    struct stat info = {0};
    *created = root >= 0 && fstatat(root, name, &info, AT_SYMLINK_NOFOLLOW) == 0 && S_ISREG(info.st_mode) &&
               (u64)info.st_size == size;
    BQ_CHECK(root >= 0 && bq_remove_workspace_payload(root));
    if (root >= 0) close(root);
    BQ_CHECK(rmdir(replay) == 0 && unlink(archive) == 0);
    return error;
}

/* #1880: a retirement evidence entry (BQ_WORKER_RETIREMENT_EVIDENCE_PREFIX,
 * flat) may reach BQ_WORKER_RETIREMENT_EVIDENCE_FILE_CAP through the
 * producer's index, the worker's bundle validation, export and unpack; one
 * byte more, any other file over BQ_WORKER_BUNDLE_FILE_CAP, and an evidence
 * name under another recipe are still refused, and a changed byte of a large
 * entry fails its digest. The fixtures are sparse and every reader streams,
 * so the peak resident set grows by far less than one entry. */
BUSTER_GLOBAL_LOCAL void bq_test_export_evidence_cap(void)
{
    u64 const cap = BQ_WORKER_RETIREMENT_EVIDENCE_FILE_CAP;
    char const* evidence = BQ_WORKER_RETIREMENT_EVIDENCE_PREFIX "toolchain--bin--clang";
    char const* other = "retirement-other.bin";
    BQ_CHECK(bq_worker_bundle_file_cap(true, evidence) == cap && cap > BQ_WORKER_BUNDLE_FILE_CAP &&
             bq_worker_bundle_file_cap(false, evidence) == BQ_WORKER_BUNDLE_FILE_CAP &&
             bq_worker_bundle_file_cap(true, BQ_WORKER_RETIREMENT_EVIDENCE_PREFIX) == BQ_WORKER_BUNDLE_FILE_CAP &&
             bq_worker_bundle_file_cap(true, "nested/retirement-evidence-x") == BQ_WORKER_BUNDLE_FILE_CAP &&
             bq_worker_bundle_file_cap(true, "retirement-evidence-x/y") == BQ_WORKER_BUNDLE_FILE_CAP &&
             bq_worker_bundle_file_cap(true, other) == BQ_WORKER_BUNDLE_FILE_CAP &&
             bq_worker_bundle_file_cap(true, NULL) == BQ_WORKER_BUNDLE_FILE_CAP);
    /* Index lines: the size is checked against the entry's own cap. */
    char line[BQ_WORKER_BUNDLE_LINE_CAP];
    BqWorkerBundleEntry entry;
    char const* zeros = "0000000000000000000000000000000000000000000000000000000000000000";
    snprintf(line, sizeof(line), "%s %" PRIu64 " %s", zeros, cap, evidence);
    BQ_CHECK(bq_worker_bundle_entry_parse(line, true, &entry) && entry.size == cap &&
             !bq_worker_bundle_entry_parse(line, false, &entry));
    snprintf(line, sizeof(line), "%s %" PRIu64 " %s", zeros, cap + 1, evidence);
    BQ_CHECK(!bq_worker_bundle_entry_parse(line, true, &entry));
    snprintf(line, sizeof(line), "%s %" PRIu64 " %s", zeros, (u64)BQ_WORKER_BUNDLE_FILE_CAP + 1, other);
    BQ_CHECK(!bq_worker_bundle_entry_parse(line, true, &entry));

    BqRecipeFiles retirement = {0};
    char path[BQ_PATH_CAP + 1] = "/tmp/bq-evidence-cap-XXXXXX";
    BQ_CHECK(bq_recipe_files(BQ_RECIPE_NATIVE_RETIREMENT_BLOCKED, &retirement) &&
             bq_test_mkdtemp_physical(path, sizeof(path)));
    int root = open(path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    BqExportInventory* inventory = mmap(NULL, sizeof(*inventory), PROT_READ | PROT_WRITE,
                                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    Arena* arena = arena_create((ArenaCreation){.reserved_size = UINT64_C(1) << 30, .flags = {.no_pool = 1}});
    u64 deadline = bq_worker_deadline(bq_worker_monotonic_milliseconds(), BQ_EXPORT_PREPARE_MILLISECONDS);
    u64 peak = bq_test_peak_rss();
    BQ_CHECK(root >= 0 && inventory != MAP_FAILED && arena);
    if (root >= 0 && inventory != MAP_FAILED && arena)
    {
        /* Export inventory: at the cap only for retirement, never above it. */
        BQ_CHECK(bq_test_evidence_sparse(root, evidence, cap, 'a') &&
                 bq_export_inventory(root, inventory, BQ_RECIPE_NATIVE_RETIREMENT_BLOCKED, deadline) == BQ_OK &&
                 inventory->files == 1 && inventory->bytes == cap &&
                 bq_export_inventory(root, inventory, BQ_RECIPE_VALIDATE_BUSTER, deadline) == BQ_EXPORT_OVERSIZED);
        BQ_CHECK(fchmodat(root, evidence, 0600, 0) == 0);
        int grow = openat(root, evidence, O_WRONLY | O_NOFOLLOW | O_CLOEXEC);
        BQ_CHECK(grow >= 0 && ftruncate(grow, (off_t)cap + 1) == 0 &&
                 bq_export_inventory(root, inventory, BQ_RECIPE_NATIVE_RETIREMENT_BLOCKED, deadline) ==
                     BQ_EXPORT_OVERSIZED);
        BQ_CHECK(grow >= 0 && ftruncate(grow, (off_t)cap) == 0 && pwrite(grow, "a", 1, (off_t)cap - 1) == 1 &&
                 fchmod(grow, 0400) == 0);
        if (grow >= 0) close(grow);
        BQ_CHECK(bq_test_evidence_sparse(root, other, BQ_WORKER_BUNDLE_FILE_CAP + 1, 'b') &&
                 bq_export_inventory(root, inventory, BQ_RECIPE_NATIVE_RETIREMENT_BLOCKED, deadline) ==
                     BQ_EXPORT_OVERSIZED);
        /* The producer's index and the worker's validation: a non-evidence
         * file over the ordinary cap refuses the index and writes nothing. */
        char digest[SHA256_HEX_CAPACITY] = {0};
        struct stat absent = {0};
        BQ_CHECK(!bq_retirement_worker_bundle_write(arena, root, &retirement, digest) &&
                 fstatat(root, retirement.bundle, &absent, AT_SYMLINK_NOFOLLOW) != 0 && errno == ENOENT);
        BQ_CHECK(unlinkat(root, other, 0) == 0 && bq_test_evidence_sparse(root, other, BQ_WORKER_BUNDLE_FILE_CAP, 'b'));
        char full[SHA256_HEX_CAPACITY] = {0};
        BQ_CHECK(bq_retirement_worker_bundle_write(arena, root, &retirement, digest) &&
                 bq_worker_bundle_validate_recipe(root, &retirement, digest, full) == BQ_OK && full[0]);
        /* One changed byte of the large entry, same inode and size. */
        BQ_CHECK(bq_test_evidence_tail(root, evidence, 'c') &&
                 bq_worker_bundle_validate_recipe(root, &retirement, digest, full) == BQ_CONFIGURATION_MISMATCH);
        BQ_CHECK(bq_test_evidence_tail(root, evidence, 'a') &&
                 bq_worker_bundle_validate_recipe(root, &retirement, digest, full) == BQ_OK);
        /* One byte over the evidence cap refuses the index. */
        BQ_CHECK(unlinkat(root, retirement.bundle, 0) == 0 && unlinkat(root, evidence, 0) == 0 &&
                 bq_test_evidence_sparse(root, evidence, cap + 1, 'a') &&
                 !bq_retirement_worker_bundle_write(arena, root, &retirement, digest) &&
                 fstatat(root, retirement.bundle, &absent, AT_SYMLINK_NOFOLLOW) != 0 && errno == ENOENT);
        BQ_CHECK(bq_remove_workspace_payload(root));

        /* Unpack: the receipt's recipe decides; each entry is refused before
         * anything is created for it. */
        char archive[BQ_PATH_CAP + 64], replay[BQ_PATH_CAP + 64], receipt[SHA256_HEX_CAPACITY] = {0};
        snprintf(archive, sizeof(archive), "%s/evidence.bqexport", path);
        snprintf(replay, sizeof(replay), "%s/replay", path);
        bool created = false;
        bq_test_retirement_admit(true);
        BQ_CHECK(bq_test_evidence_archive(archive, evidence, cap, receipt) &&
                 bq_test_evidence_unpack(archive, replay, receipt, evidence, cap, &created) == BQ_EXPORT_INVALID &&
                 created);
        BQ_CHECK(bq_test_evidence_archive(archive, evidence, cap + 1, receipt) &&
                 bq_test_evidence_unpack(archive, replay, receipt, evidence, cap + 1, &created) == BQ_EXPORT_CORRUPT &&
                 !created);
        BQ_CHECK(bq_test_evidence_archive(archive, other, BQ_WORKER_BUNDLE_FILE_CAP + 1, receipt) &&
                 bq_test_evidence_unpack(archive, replay, receipt, other, BQ_WORKER_BUNDLE_FILE_CAP + 1, &created) ==
                     BQ_EXPORT_CORRUPT && !created);
        bq_test_retirement_admit(false);
        u64 grown = bq_test_peak_rss() - peak;
        if (grown >= BQ_WORKER_BUNDLE_FILE_CAP) fprintf(stderr, "EVIDENCE_CAP_TEST peak_rss_growth=%" PRIu64 "\n", grown);
        BQ_CHECK(grown < BQ_WORKER_BUNDLE_FILE_CAP);
    }
    if (arena) arena_destroy(arena, 1);
    if (inventory != MAP_FAILED) munmap(inventory, sizeof(*inventory));
    if (root >= 0) close(root);
    BQ_CHECK(rmdir(path) == 0);
}
#endif
