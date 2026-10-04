/* Control-host writer and original sealed-result cache for #437.
 * bq_control_open owns the global FIFO. bq_control_session applies authenticated
 * worker custody/quiet/transfer/ACK messages. bq_control_public serves only
 * local journal/cache bytes. No control function executes work or polls a host.
 * Missing ACKs retain the assignment indefinitely; time is never ownership.
 */
typedef struct BqOffhostControl
{
    BqQueue queue;
    BqOffhostIdentity identity;
    bool quiet;
    bool cancel_sent;
    bool channel_lost;
    u64 worker_messages;
} BqOffhostControl;

#ifdef __linux__
BUSTER_GLOBAL_LOCAL BqError bq_control_assignment(BqOffhostControl* control, BqSessionPacket* packet)
{
    BqJob* job = bq_job(&control->queue.state, control->identity.id);
    BqError error = !job || job->token != control->identity.token ? BQ_CONFLICT : BQ_OK;
    if (error == BQ_OK)
    {
        bq_session_packet(packet, BQ_SESSION_HELLO | BQ_SESSION_REPLY, BQ_OK, &control->identity,
                          job->request.bytes, job->request.size);
        char name[96];
        bq_offhost_name(name, "assignment", job->id, job->token);
        error = bq_offhost_record(&control->queue, name, packet->bytes, packet->size);
        if (error == BQ_OK) error = bq_offhost_observe(&control->queue, "assignment", &control->identity);
    }
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_control_open(BqOffhostControl* control, char const* root, char const machine[SHA256_HEX_CAPACITY])
{
    *control = (BqOffhostControl){.queue = {.directory_fd = -1, .lock_fd = -1, .journal_fd = -1}};
    BqError error = bq_open(&control->queue, root);
    if (error == BQ_OK && !bq_transport_queue_admissible(&control->queue)) error = BQ_UNSUPPORTED;
    if (error == BQ_OK)
    {
        error = bq_offhost_read(&control->queue, "offhost-control-instance", control->identity.control, 64);
        if (error == BQ_NOT_FOUND && control->queue.state.job_count) error = BQ_CONFIGURATION_MISMATCH;
        if (error == BQ_NOT_FOUND)
        {
            u8 entropy[32];
            int random = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
            error = random < 0 || !bq_read(random, entropy, sizeof(entropy), 0) ? BQ_IO : BQ_OK;
            if (random >= 0) close(random);
            if (error == BQ_OK)
            {
                bq_digest(entropy, sizeof(entropy), control->identity.control);
                error = bq_offhost_record(&control->queue, "offhost-control-instance", control->identity.control, 64);
            }
        }
        if (error == BQ_OK && !bq_result_digest_valid((u8*)control->identity.control)) error = BQ_CORRUPT;
        memcpy(control->identity.machine, machine, 65);
    }
    for (u32 i = 0; error == BQ_OK && i < control->queue.state.job_count; i += 1)
    {
        BqJob* job = control->queue.state.jobs + i;
        if (job->token)
        {
            BqOffhostIdentity identity = control->identity;
            identity.id = job->id;
            identity.token = job->token;
            BqError ack = bq_offhost_marked(&control->queue, "terminal-ack", &identity);
            if (ack == BQ_NOT_FOUND)
            {
                if (control->identity.id) error = BQ_CONFLICT;
                else control->identity = identity;
            }
            else if (ack != BQ_OK) error = ack;
        }
    }
    if (error == BQ_OK && control->identity.id)
    {
        BqSessionPacket assignment;
        error = bq_control_assignment(control, &assignment);
        BqError quiet = error == BQ_OK ? bq_offhost_marked(&control->queue, "quiet", &control->identity) : error;
        if (quiet != BQ_OK && quiet != BQ_NOT_FOUND) error = quiet;
        control->quiet = quiet == BQ_OK;
    }
    if (error != BQ_OK) bq_close(&control->queue);
    return error;
}

BUSTER_GLOBAL_LOCAL bool bq_control_cache_path(BqQueue* queue, BqJob const* job, char path[BQ_PATH_CAP + 1])
{
    int size = snprintf(path, BQ_PATH_CAP + 1, "%s/offhost-cache-%llu-%llu", queue->directory_path,
                        (unsigned long long)job->id, (unsigned long long)job->token);
    bool ok = size > 0 && size <= (int)BQ_PATH_CAP;
    return ok;
}

BUSTER_GLOBAL_LOCAL BqError bq_control_cached_job(BqQueue* queue, BqJob* job, u8 const receipt[BQ_EXPORT_RECEIPT_CAP])
{
    char path[BQ_PATH_CAP + 1];
    BqError error = bq_control_cache_path(queue, job, path) ? BQ_OK : BQ_BAD_REQUEST;
    int root = error == BQ_OK ? bq_open_absolute_directory(string_from_pointer(path)) : -1;
    if (error == BQ_OK && root < 0) error = BQ_EXPORT_MISSING;
    if (error == BQ_OK && receipt)
    {
        memcpy(job->result_manifest_digest, receipt + 112, 64);
        memcpy(job->result_bundle_digest, receipt + 176, 64);
        memcpy(job->result_full_digest, receipt + 240, 64);
        job->result_bound = true;
        BqRecipeFiles files;
        bool valid = bq_recipe_files(bq_request_recipe(&job->request), &files);
        int fd = valid ? openat(root, files.manifest, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC) : -1;
        struct stat info = {0};
        char text[BQ_WORKER_RESULT_CAP + 1] = {0};
        valid = fd >= 0 && fstat(fd, &info) == 0 && S_ISREG(info.st_mode) &&
                info.st_size > 0 && info.st_size <= BQ_WORKER_RESULT_CAP && bq_read(fd, (u8*)text, (u32)info.st_size, 0);
        char const* key = valid ? strstr(text, "\nresult-root=") : NULL;
        char const* end = key ? strchr(key + 13, '\n') : NULL;
        valid = key && end && end > key + 13 && end - key - 13 <= BQ_PATH_CAP;
        if (valid)
        {
            memset(job->result_root, 0, sizeof(job->result_root));
            memcpy(job->result_root, key + 13, (size_t)(end - key - 13));
        }
        if (fd >= 0) close(fd);
        if (!valid) error = BQ_EXPORT_INVALID;
    }
    if (error == BQ_OK) error = bq_worker_result_binding_validate_at(job, root);
    if (root >= 0) close(root);
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_control_receipt(BqOffhostControl* control, u8 const* receipt)
{
    BqJob* job = bq_job(&control->queue.state, control->identity.id);
    BqError error = !job || !bq_export_receipt_valid(receipt) || bq_u64(receipt + 8) != job->id ||
                    bq_u64(receipt + 16) != job->token || memcmp(receipt + 48, job->digest, 64) ||
                    bq_u32(receipt + 992) != job->request.size ||
                    memcmp(receipt + 672, job->request.bytes, job->request.size) ? BQ_EXPORT_CORRUPT : BQ_OK;
    if (error == BQ_OK)
    {
        char name[96];
        bq_offhost_name(name, "receipt", job->id, job->token);
        error = bq_offhost_record(&control->queue, name, receipt, BQ_EXPORT_RECEIPT_CAP);
    }
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_control_transfer(BqOffhostControl* control, u8 const* body, u32 size)
{
    char name[96], receipt_name[96];
    bq_offhost_name(name, "upload", control->identity.id, control->identity.token);
    bq_offhost_name(receipt_name, "receipt", control->identity.id, control->identity.token);
    u8 receipt[BQ_EXPORT_RECEIPT_CAP];
    BqError error = size <= 8 || size > BQ_SESSION_BODY ? BQ_BAD_REQUEST :
                    bq_offhost_read(&control->queue, receipt_name, receipt, sizeof(receipt));
    u64 cursor = size >= 8 ? bq_u64(body) : 0;
    u64 total = error == BQ_OK ? BQ_EXPORT_DATA_OFFSET + bq_u64(receipt + 24) : 0;
    u32 count = size > 8 ? size - 8 : 0;
    if (error == BQ_OK && (cursor > total || count > total - cursor || cursor % BQ_EXPORT_CHUNK_CAP ||
                          (count != BQ_EXPORT_CHUNK_CAP && cursor + count != total))) error = BQ_BAD_REQUEST;
    char sealed[80];
    bq_export_name(sealed, control->identity.id, control->identity.token, false);
    int fd = error == BQ_OK ? openat(control->queue.directory_fd, sealed, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC) : -1;
    bool published = fd >= 0;
    if (error == BQ_OK && fd < 0 && errno == ENOENT)
        fd = openat(control->queue.directory_fd, name, O_RDWR | O_CREAT | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC, 0600);
    struct stat info = {0};
    if (error == BQ_OK && (fd < 0 || fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) || info.st_uid != geteuid() ||
                          info.st_nlink != 1 || (info.st_mode & 0777) != (published ? 0400 : 0600) || info.st_size < 0)) error = BQ_EXPORT_CORRUPT;
    if (error == BQ_OK && (u64)info.st_size < cursor) error = BQ_INVALID_TRANSITION;
    if (error == BQ_OK && (u64)info.st_size > cursor)
    {
        u8 previous[BQ_EXPORT_CHUNK_CAP];
        if ((u64)info.st_size < cursor + count || !bq_read(fd, previous, count, cursor) ||
            memcmp(previous, body + 8, count)) error = BQ_CONFLICT;
    }
    else if (error == BQ_OK)
    {
        error = bq_export_io(fd, (void*)(body + 8), count, cursor, true,
                  bq_worker_deadline(bq_worker_monotonic_milliseconds(), BQ_EXPORT_READ_MILLISECONDS));
        if (error == BQ_OK && (fsync(fd) != 0 || fsync(control->queue.directory_fd) != 0)) error = BQ_IO;
    }
    if (fd >= 0) close(fd);
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_control_publish_cache(BqOffhostControl* control)
{
    BqQueue* queue = &control->queue;
    BqJob* job = bq_job(&queue->state, control->identity.id);
    char receipt_name[96], upload[96], sealed[80], offline[96];
    bq_offhost_name(receipt_name, "receipt", control->identity.id, control->identity.token);
    bq_offhost_name(upload, "upload", control->identity.id, control->identity.token);
    bq_offhost_name(offline, "archive", control->identity.id, control->identity.token);
    bq_export_name(sealed, control->identity.id, control->identity.token, false);
    u8 receipt[BQ_EXPORT_RECEIPT_CAP];
    BqError error = job ? bq_offhost_read(queue, receipt_name, receipt, sizeof(receipt)) : BQ_NOT_FOUND;
    if (error == BQ_OK) error = bq_control_receipt(control, receipt);
    int input = error == BQ_OK ? openat(queue->directory_fd, upload, O_RDWR | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC) : -1;
    bool from_upload = input >= 0;
    if (error == BQ_OK && input < 0 && errno == ENOENT)
        input = openat(queue->directory_fd, sealed, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    struct stat info = {0};
    u64 total = error == BQ_OK ? bq_u64(receipt + 24) : 0;
    if (error == BQ_OK && (input < 0 || fstat(input, &info) != 0 || !S_ISREG(info.st_mode) ||
        info.st_uid != geteuid() || info.st_nlink < 1 || info.st_nlink > 2 ||
        (u64)info.st_size != BQ_EXPORT_DATA_OFFSET + total)) error = BQ_EXPORT_CORRUPT;
    u64 deadline = bq_worker_deadline(bq_worker_monotonic_milliseconds(), BQ_EXPORT_PREPARE_MILLISECONDS);
    if (error == BQ_OK)
    {
        u8 original[BQ_EXPORT_RECEIPT_CAP];
        if (!bq_read(input, original, sizeof(original), 0) || memcmp(original, receipt, sizeof(original))) error = BQ_EXPORT_CORRUPT;
    }
    if (error == BQ_OK && from_upload)
    {
        if (fchmod(input, 0400) != 0 || fsync(input) != 0) error = BQ_IO;
        else if (linkat(queue->directory_fd, upload, queue->directory_fd, sealed, 0) != 0 && errno != EEXIST) error = BQ_IO;
        struct stat target = {0};
        if (error == BQ_OK && (fstatat(queue->directory_fd, sealed, &target, AT_SYMLINK_NOFOLLOW) != 0 ||
            target.st_dev != info.st_dev || target.st_ino != info.st_ino)) error = BQ_CONFLICT;
        if (error == BQ_OK && (unlinkat(queue->directory_fd, upload, 0) != 0 || fsync(queue->directory_fd) != 0)) error = BQ_IO;
    }
    if (input >= 0) close(input);
    BqJob terminal = job ? *job : (BqJob){0};
    terminal.phase = BQ_FINISHED;
    terminal.outcome = error == BQ_OK ? (BqOutcome)bq_u32(receipt + 1012) : BQ_NO_OUTCOME;
    terminal.validity = error == BQ_OK ? (BqValidity)bq_u32(receipt + 1016) : BQ_NOT_EVALUATED;
    if (error == BQ_OK)
    {
        terminal.result_bound = true;
        memcpy(terminal.result_manifest_digest, receipt + 112, 64);
        memcpy(terminal.result_bundle_digest, receipt + 176, 64);
        memcpy(terminal.result_full_digest, receipt + 240, 64);
        u8 output[BQ_EXPORT_BODY_CAP];
        u32 count = 0;
        char digest[SHA256_HEX_CAPACITY];
        bq_digest(receipt, sizeof(receipt), digest);
        error = bq_export_read(queue, &terminal, UINT64_MAX, (u8*)digest, output, &count);
        for (u64 cursor = 0; error == BQ_OK && cursor < total; cursor += BQ_EXPORT_CHUNK_CAP)
            error = bq_export_read(queue, &terminal, cursor, (u8*)digest, output, &count);
    }
    char cache[BQ_PATH_CAP + 1], archive[BQ_PATH_CAP + 1];
    if (error == BQ_OK && (!bq_control_cache_path(queue, &terminal, cache) ||
        snprintf(archive, sizeof(archive), "%s/%s", queue->directory_path, offline) >= (int)sizeof(archive))) error = BQ_BAD_REQUEST;
    if (error == BQ_OK)
    {
        BqError cached = bq_control_cached_job(queue, &terminal, receipt);
        if (cached == BQ_EXPORT_MISSING)
        {
            int source = openat(queue->directory_fd, sealed, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
            int destination = openat(queue->directory_fd, offline, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0400);
            if (source < 0 || destination < 0) error = BQ_EXPORT_CORRUPT;
            if (error == BQ_OK) error = bq_export_io(destination, receipt, sizeof(receipt), 0, true, deadline);
            for (u64 cursor = 0; error == BQ_OK && cursor < total;)
            {
                u8 bytes[BQ_EXPORT_CHUNK_CAP];
                u32 count = total - cursor < sizeof(bytes) ? (u32)(total - cursor) : sizeof(bytes);
                error = bq_export_io(source, bytes, count, BQ_EXPORT_DATA_OFFSET + cursor, false, deadline);
                if (error == BQ_OK) error = bq_export_io(destination, bytes, count, sizeof(receipt) + cursor, true, deadline);
                cursor += count;
            }
            if (error == BQ_OK && (fsync(destination) != 0 || fsync(queue->directory_fd) != 0)) error = BQ_IO;
            if (source >= 0) close(source);
            if (destination >= 0) close(destination);
            char digest[SHA256_HEX_CAPACITY];
            bq_digest(receipt, sizeof(receipt), digest);
            if (error == BQ_OK) error = bq_export_unpack(archive, cache, digest);
            if (error == BQ_OK) error = bq_control_cached_job(queue, &terminal, receipt);
        }
        else error = cached;
    }
    if (error == BQ_OK && job->phase != BQ_FINISHED)
    {
        if (terminal.outcome == BQ_CANCELLED && !job->cancel_requested) error = bq_cancel(queue, job->id);
        if (terminal.outcome == BQ_SUCCEEDED)
        {
            for (u32 phase = job->phase + 1; error == BQ_OK && phase <= BQ_CLEANING; phase += 1)
                error = bq_real_advance(queue, job, (BqPhase)phase, phase >= BQ_FINALIZING ? BQ_SUCCEEDED : BQ_NO_OUTCOME);
        }
        else if (job->phase < BQ_CLEANING) error = bq_real_advance(queue, job, BQ_CLEANING, terminal.outcome);
        if (error == BQ_OK && !job->result_bound)
            error = bq_result_bind(queue, job, string_from_pointer(terminal.result_root), terminal.result_manifest_digest,
                                  terminal.result_bundle_digest, terminal.result_full_digest);
        if (error == BQ_OK) error = bq_real_advance(queue, job, BQ_FINISHED, terminal.outcome);
    }
    if (error == BQ_OK && (job->outcome != terminal.outcome || !job->result_bound ||
        memcmp(job->result_full_digest, terminal.result_full_digest, 64))) error = BQ_CONFLICT;
    if (error == BQ_OK)
    {
        BqError intent = bq_offhost_marked(queue, "cancel-intent", &control->identity);
        if (intent == BQ_OK) error = bq_offhost_mark(queue,
            terminal.outcome == BQ_CANCELLED ? "cancel-applied" : "cancel-too-late", &control->identity);
        else if (intent != BQ_NOT_FOUND) error = intent;
        if (error == BQ_OK) error = bq_offhost_mark(queue, "cache-committed", &control->identity);
        if (error == BQ_OK) error = bq_offhost_observe(queue, "cache", &control->identity);
        queue->needs_reconciliation = false;
    }
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_control_session(BqOffhostControl* control, BqSessionPacket const* request,
                                              BqSessionPacket* response)
{
    BqError error = bq_session_valid(request, false) ? BQ_OK : BQ_BAD_REQUEST;
    u32 operation = request->size >= BQ_SESSION_HEADER ? bq_u32(request->bytes + 12) : BQ_SESSION_HELLO;
    BqOffhostIdentity incoming = bq_session_identity(request);
    u32 size = request->size >= BQ_SESSION_HEADER ? request->size - BQ_SESSION_HEADER : 0;
    u8 const* body = request->bytes + BQ_SESSION_HEADER;
    if (error == BQ_OK && memcmp(incoming.machine, control->identity.machine, 64)) error = BQ_EXPORT_UNAUTHORIZED;
    if (error == BQ_OK && operation != BQ_SESSION_HELLO &&
        (incoming.id != control->identity.id || incoming.token != control->identity.token ||
         memcmp(incoming.control, control->identity.control, 64))) error = BQ_CONFLICT;
    if (error == BQ_OK && operation == BQ_SESSION_HELLO)
    {
        if (size || (incoming.control[0] && memcmp(incoming.control, control->identity.control, 64))) error = BQ_CONFLICT;
        else if (incoming.id && (incoming.id != control->identity.id || incoming.token != control->identity.token))
        {
            error = bq_offhost_marked(&control->queue, "terminal-ack", &incoming);
            if (error == BQ_OK) bq_session_packet(response, operation | BQ_SESSION_REPLY, BQ_OK, &incoming, NULL, 0);
        }
        else
        {
            BqError custody = control->identity.id ? bq_offhost_marked(&control->queue, "custody", &control->identity) : BQ_NOT_FOUND;
            if (!incoming.id && custody == BQ_OK) error = BQ_RECONCILIATION_REQUIRED;
            else if (custody != BQ_OK && custody != BQ_NOT_FOUND) error = custody;
            if (error == BQ_OK && !control->identity.id)
            {
                if (control->queue.needs_reconciliation) error = BQ_RECONCILIATION_REQUIRED;
                else error = bq_reserve(&control->queue, &control->identity.id, &control->identity.token);
            }
            if (error == BQ_OK) error = bq_control_assignment(control, response);
        }
    }
    else if (error == BQ_OK && operation == BQ_SESSION_CUSTODY && size == 128 &&
             bq_result_digest_valid(body) && bq_result_digest_valid(body + 64))
    {
        char name[96];
        bq_offhost_name(name, "worker-custody", incoming.id, incoming.token);
        error = bq_offhost_record(&control->queue, name, body, size);
        if (error == BQ_OK) error = bq_offhost_mark(&control->queue, "custody", &incoming);
        if (error == BQ_OK) error = bq_offhost_observe(&control->queue, "custody", &incoming);
    }
    else if (error == BQ_OK && operation == BQ_SESSION_QUIET && !size)
    {
        error = bq_offhost_marked(&control->queue, "custody", &incoming);
        if (error == BQ_OK) error = bq_offhost_mark(&control->queue, "quiet", &incoming);
        if (error == BQ_OK) error = bq_offhost_observe(&control->queue, "quiet", &incoming);
        if (error == BQ_OK) control->quiet = true;
    }
    else if (error == BQ_OK && operation == BQ_SESSION_RESULT && size == BQ_EXPORT_RECEIPT_CAP)
    {
        error = bq_offhost_marked(&control->queue, "quiet", &incoming);
        if (error == BQ_OK) error = bq_control_receipt(control, body);
        if (error == BQ_OK) error = bq_offhost_observe(&control->queue, "receipt", &incoming);
    }
    else if (error == BQ_OK && operation == BQ_SESSION_CHUNK)
        error = bq_control_transfer(control, body, size);
    else if (error == BQ_OK && operation == BQ_SESSION_COMPLETE && !size)
        error = bq_control_publish_cache(control);
    else if (error == BQ_OK && operation == BQ_SESSION_TERMINAL_ACK && !size)
    {
        error = bq_offhost_marked(&control->queue, "cache-committed", &incoming);
        if (error == BQ_OK) error = bq_offhost_mark(&control->queue, "terminal-ack", &incoming);
        if (error == BQ_OK) error = bq_offhost_observe(&control->queue, "ack", &incoming);
        if (error == BQ_OK)
        {
            control->identity.id = control->identity.token = 0;
            control->quiet = false;
            control->cancel_sent = false;
            control->channel_lost = false;
        }
    }
    else if (error == BQ_OK) error = BQ_BAD_REQUEST;
    if (operation != BQ_SESSION_HELLO || error != BQ_OK)
        bq_session_packet(response, operation | BQ_SESSION_REPLY, error,
                          operation == BQ_SESSION_HELLO ? &control->identity : &incoming, NULL, 0);
    control->worker_messages += 1;
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_control_observation(BqOffhostControl* control, BqJob* job, BqPacket* response)
{
    BqOffhostIdentity identity = control->identity;
    identity.id = job->id;
    identity.token = job->token;
    u64 timestamp = 0;
    BqError error = bq_offhost_observed(&control->queue, &identity, &timestamp);
    if (error == BQ_OK && !timestamp && job->token)
    {
        identity.token = 0;
        error = bq_offhost_observed(&control->queue, &identity, &timestamp);
        identity.token = job->token;
    }
    u32 flags = 0, cancellation = 0;
    if (job->phase != BQ_FINISHED && job->token)
    {
        BqError quiet = bq_offhost_marked(&control->queue, "quiet", &identity);
        if (quiet == BQ_OK) flags |= BQ_OBSERVATION_STALE | BQ_OBSERVATION_PAUSED | BQ_OBSERVATION_LIFECYCLE_UNAVAILABLE;
        else if (quiet != BQ_NOT_FOUND) error = quiet;
        if (control->queue.needs_reconciliation) flags |= BQ_OBSERVATION_RECONCILIATION;
        if (control->channel_lost) flags |= BQ_OBSERVATION_RECONCILIATION | BQ_OBSERVATION_QUARANTINE;
    }
    char const* decisions[] = {"cancel-intent", "cancel-applied", "cancel-too-late"};
    for (u32 i = 0; error == BQ_OK && job->token && i < BUSTER_ARRAY_LENGTH(decisions); i += 1)
    {
        BqError found = bq_offhost_marked(&control->queue, decisions[i], &identity);
        if (found == BQ_OK) cancellation = i + 1;
        else if (found != BQ_NOT_FOUND) error = found;
    }
    if (error == BQ_OK)
    {
        u8* observation = response->bytes + response->size;
        memset(observation, 0, BQ_OBSERVATION_SIZE);
        memcpy(observation, "BQOBS001", 8);
        bq_put64(observation + 8, timestamp);
        bq_put32(observation + 16, flags);
        bq_put32(observation + 20, cancellation);
        response->size += BQ_OBSERVATION_SIZE;
        bq_put32(response->bytes + 12, response->size - BQ_CONTROL_HEADER);
    }
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_control_public(BqOffhostControl* control, u8 const* request, u32 size, BqPacket* response)
{
    BqQueue* queue = &control->queue;
    BqError error = bq_transport_public_request(request, size);
    u32 operation = size >= BQ_CONTROL_HEADER ? bq_u32(request + 8) : 0;
    if (error == BQ_OK && operation == BQ_OP_CAPABILITIES)
    {
        char const capabilities[] = "schema=2 executor=off-host-control-unqualified pending=8 lifetime-jobs=512\n"
            "recipes=validate-buster-v1,zen5-calibration-v1 cache=control-local-original-sealed\n"
            "worker=initiated-fixed-ssh quiet=whole-job no-heartbeat lease=no-expiry\n"
            "lifecycle=last-observed posthoc-phases cancellation=durable-intent-until-worker-outcome\n"
            "qualification=local-fixtures-only native-remote=unsupported observation=BQOBS001-utc-ms\n";
        u8 output[BQ_CONTROL_BODY] = {0};
        memcpy(output + 4, capabilities, sizeof(capabilities) - 1);
        bq_packet(response, operation | 0x80000000u, bq_u64(request + 16), output, 4 + sizeof(capabilities) - 1);
    }
    else if (error == BQ_OK && (operation == BQ_OP_STATUS || operation == BQ_OP_RESULT || operation == BQ_OP_CANCEL) &&
             size == BQ_CONTROL_HEADER + 8)
    {
        BqJob* job = bq_job(&queue->state, bq_u64(request + BQ_CONTROL_HEADER));
        error = job && string_equal(bq_field(&job->request, 0), S8(BQ_EXPORT_PRINCIPAL)) && bq_recipe_service(bq_request_recipe(&job->request)) ? BQ_OK : BQ_EXPORT_UNAUTHORIZED;
        BqOffhostIdentity identity = control->identity;
        if (job) { identity.id = job->id; identity.token = job->token; }
        BqError intent = error == BQ_OK && job->token ? bq_offhost_marked(queue, "cancel-intent", &identity) : BQ_NOT_FOUND;
        if (error == BQ_OK && intent != BQ_OK && intent != BQ_NOT_FOUND) error = intent;
        if (error == BQ_OK && operation == BQ_OP_CANCEL && job->phase != BQ_FINISHED)
        {
            if (job->phase == BQ_QUEUED) error = bq_cancel(queue, job->id);
            else error = bq_offhost_mark(queue, "cancel-intent", &identity);
            if (error == BQ_OK) intent = BQ_OK;
        }
        if (error == BQ_OK && job->result_bound) error = bq_control_cached_job(queue, job, NULL);
        if (error == BQ_OK)
        {
            u8 output[BQ_CONTROL_BODY] = {0};
            bq_put64(output + 4, job->id);
            bq_put64(output + 12, job->token);
            bq_put32(output + 28, (u32)job->phase);
            bq_put32(output + 32, (u32)job->outcome);
            bq_put32(output + 36, (u32)job->validity);
            bq_put32(output + 40, job->cancel_requested || intent == BQ_OK);
            memcpy(output + 56, job->digest, 64);
            u32 output_size = 124;
            if (job->result_bound)
            {
                bq_put32(output + 124, (u32)strlen(job->result_root));
                memcpy(output + 128, job->result_root, strlen(job->result_root));
                memcpy(output + 320, job->result_manifest_digest, 64);
                memcpy(output + 384, job->result_bundle_digest, 64);
                memcpy(output + 448, job->result_full_digest, 64);
                output_size = sizeof(output);
            }
            bq_packet(response, operation | 0x80000000u, bq_u64(request + 16), output, output_size);
        }
    }
    else if (error == BQ_OK && operation == BQ_OP_EXPORT)
    {
        u8 const* body = request + BQ_CONTROL_HEADER;
        BqJob* job = NULL;
        error = bq_export_authorize(queue, body, S8(BQ_EXPORT_PRINCIPAL), &job);
        if (error == BQ_OK) error = bq_control_cached_job(queue, job, NULL);
        u8 output[BQ_EXPORT_BODY_CAP];
        u32 output_size = 0;
        if (error == BQ_OK) error = bq_export_read(queue, job, bq_u64(body + 80), body + 88, output, &output_size);
        if (error == BQ_OK) bq_packet(response, operation | 0x80000000u, bq_u64(request + 16), output, output_size);
    }
    else if (error == BQ_OK && (operation == BQ_OP_STATUS || operation == BQ_OP_RESULT || operation == BQ_OP_CANCEL)) error = BQ_BAD_REQUEST;
    else if (error == BQ_OK) error = bq_transport_dispatch(queue, request, size, response);
    if (error == BQ_OK && operation != BQ_OP_CAPABILITIES && operation != BQ_OP_EXPORT)
    {
        u64 id = operation == BQ_OP_LOGS ? bq_u64(request + BQ_CONTROL_HEADER) : bq_u64(response->bytes + BQ_CONTROL_HEADER + 4);
        BqJob* job = bq_job(&queue->state, id);
        if (job)
        {
            BqOffhostIdentity identity = control->identity;
            identity.id = job->id;
            identity.token = job->token;
            if (operation == BQ_OP_SUBMIT || operation == BQ_OP_SUBMIT_EXCLUSIVE)
                error = bq_offhost_observe(queue, "submit", &identity);
            if (error == BQ_OK) error = bq_control_observation(control, job, response);
        }
    }
    if (error != BQ_OK) bq_transport_peer_error(request, size, response, error);
    return error;
}
#endif
