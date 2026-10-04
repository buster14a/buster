/* Off-host daemon and installed worker-initiated SSH bridge for #437.
 * bq_control_serve is the sole journal/cache writer. bq_worker_stdio runs as
 * the dedicated forced-command SSH account and bridges only the worker socket.
 * bq_offhost_worker_agent owns local custody, the physical lease and fixed SSH
 * process. It never reconnects or sends ordinary messages during execution.
 */
#ifdef __linux__
BUSTER_GLOBAL_LOCAL BqError bq_offhost_connect(char const* socket_path, int* output)
{
    *output = -1;
    int fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    size_t length = strlen(socket_path);
    BqError error = fd < 0 || length >= sizeof(address.sun_path) ? BQ_IO : BQ_OK;
    if (error == BQ_OK)
    {
        memcpy(address.sun_path, socket_path, length + 1);
        if (connect(fd, (struct sockaddr*)&address, sizeof(address)) != 0 || !bq_transport_peer_allowed(fd)) error = BQ_IO;
    }
    if (error == BQ_OK) *output = fd;
    else if (fd >= 0) close(fd);
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_control_serve(char const* root, char const* public_path, char const* worker_path, char const machine[SHA256_HEX_CAPACITY])
{
    BqOffhostControl control = {.queue = {.directory_fd = -1, .lock_fd = -1, .journal_fd = -1}};
    BqTransportEndpoint public = {.listener = -1, .parent = -1};
    BqTransportEndpoint worker = {.listener = -1, .parent = -1};
    BqError error = !strcmp(public_path, worker_path) ? BQ_BAD_REQUEST : bq_control_open(&control, root, machine);
    if (error == BQ_OK) error = bq_transport_endpoint_open(public_path, &public);
    if (error == BQ_OK) error = bq_transport_endpoint_open(worker_path, &worker);
    struct sigaction action = {.sa_handler = bq_transport_stop_handler}, old_term = {0}, old_interrupt = {0};
    bool signals = false;
    if (error == BQ_OK)
    {
        sigemptyset(&action.sa_mask);
        bq_transport_stop_signal = 0;
        signals = sigaction(SIGTERM, &action, &old_term) == 0;
        if (!signals || sigaction(SIGINT, &action, &old_interrupt) != 0) error = BQ_IO;
    }
    int session = -1;
    while (error == BQ_OK && !bq_transport_stop_signal)
    {
        struct pollfd waiting[3] = {{public.listener, POLLIN, 0}, {worker.listener, POLLIN, 0}, {session, POLLIN, 0}};
        int ready = poll(waiting, 3, 1000);
        if (ready < 0 && errno != EINTR) error = BQ_IO;
        if (ready > 0 && (waiting[0].revents & POLLIN))
        {
            int client = accept4(public.listener, NULL, NULL, SOCK_CLOEXEC);
            if (client >= 0)
            {
                u8 request[BQ_CONTROL_CAP];
                u32 size = 0;
                BqPacket response = {0};
                BqError admission = bq_transport_peer_allowed(client) ? bq_transport_receive(client, request, &size) : BQ_EXPORT_UNAUTHORIZED;
                if (admission == BQ_OK) admission = bq_control_public(&control, request, size, &response);
                else bq_transport_peer_error(request, size, &response, admission);
                bq_transport_send(client, response.bytes, response.size);
                close(client);
                if (control.queue.poisoned) error = BQ_IO;
            }
        }
        if (ready > 0 && (waiting[1].revents & POLLIN))
        {
            int candidate = accept4(worker.listener, NULL, NULL, SOCK_CLOEXEC | SOCK_NONBLOCK);
            if (candidate >= 0)
            {
                if (!bq_transport_peer_allowed(candidate) || session >= 0) close(candidate);
                else { session = candidate; control.cancel_sent = false; }
            }
        }
        if (ready > 0 && session >= 0 && (waiting[2].revents & (POLLIN | POLLHUP | POLLERR)))
        {
            BqSessionPacket request = {0}, response = {0};
            BqError admission = bq_session_receive(session, &request);
            if (admission == BQ_OK)
            {
                admission = bq_control_session(&control, &request, &response);
                if (bq_session_send(session, &response) != BQ_OK) admission = BQ_IO;
            }
            if (admission != BQ_OK)
            {
                close(session);
                session = -1;
                if (control.quiet) control.channel_lost = true;
            }
            if (control.queue.poisoned) error = BQ_IO;
        }
        if (session >= 0 && control.identity.id && control.quiet && !control.cancel_sent)
        {
            BqError intent = bq_offhost_marked(&control.queue, "cancel-intent", &control.identity);
            if (intent == BQ_OK)
            {
                BqSessionPacket cancel;
                bq_session_packet(&cancel, BQ_SESSION_CANCEL, BQ_OK, &control.identity, NULL, 0);
                if (bq_session_send(session, &cancel) == BQ_OK) control.cancel_sent = true;
                else { close(session); session = -1; }
            }
            else if (intent != BQ_NOT_FOUND) error = intent;
        }
    }
    if (session >= 0) close(session);
    if (signals)
    {
        if (sigaction(SIGINT, &old_interrupt, NULL) != 0 || sigaction(SIGTERM, &old_term, NULL) != 0) error = BQ_IO;
    }
    BqError worker_close = bq_transport_endpoint_close(&worker);
    BqError public_close = bq_transport_endpoint_close(&public);
    if (error == BQ_OK && (worker_close != BQ_OK || public_close != BQ_OK)) error = BQ_IO;
    bq_close(&control.queue);
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_worker_stdio(int input, int output, char const* socket_path)
{
    int session = -1;
    struct sigaction ignored = {.sa_handler = SIG_IGN}, previous = {0};
    sigemptyset(&ignored.sa_mask);
    BqError error = sigaction(SIGPIPE, &ignored, &previous) == 0 ? bq_offhost_connect(socket_path, &session) : BQ_IO;
    while (error == BQ_OK)
    {
        /* No inactivity timer, keepalive, heartbeat or reconnect while quiet.
         * A partial frame has a bounded deadline once its first byte arrives. */
        struct pollfd waiting[2] = {{input, POLLIN, 0}, {session, POLLIN, 0}};
        int ready = poll(waiting, 2, -1);
        if (ready < 0 && errno == EINTR) continue;
        if (ready < 0) error = BQ_IO;
        for (u32 i = 0; error == BQ_OK && i < 2; i += 1)
        {
            if (waiting[i].revents & (POLLIN | POLLHUP | POLLERR))
            {
                BqSessionPacket packet = {0};
                u64 deadline = bq_worker_deadline(bq_worker_monotonic_milliseconds(), BQ_EXPORT_READ_MILLISECONDS);
                if (i == 0)
                {
                    error = bq_session_stream(input, &packet, false, deadline);
                    if (error == BQ_OK && !bq_session_valid(&packet, false)) error = BQ_BAD_REQUEST;
                    if (error == BQ_OK) error = bq_session_send(session, &packet);
                }
                else
                {
                    error = bq_session_receive(session, &packet);
                    if (error == BQ_OK) error = bq_session_stream(output, &packet, true, deadline);
                }
            }
        }
    }
    if (session >= 0) close(session);
    sigaction(SIGPIPE, &previous, NULL);
    return error;
}

typedef struct BqOffhostSsh
{
    pid_t process;
    int input;
    int output;
    bool cancel_pending;
} BqOffhostSsh;

BUSTER_GLOBAL_LOCAL void bq_offhost_ssh_close(BqOffhostSsh* ssh)
{
    if (ssh->input >= 0) close(ssh->input);
    if (ssh->output >= 0) close(ssh->output);
    if (ssh->process > 0)
    {
        kill(ssh->process, SIGKILL);
        int status = 0;
        bq_worker_waitpid_until(ssh->process, &status, bq_worker_deadline(bq_worker_monotonic_milliseconds(), 5000));
    }
    *ssh = (BqOffhostSsh){.input = -1, .output = -1};
}

BUSTER_GLOBAL_LOCAL BqError bq_offhost_ssh_open(BqOffhostSsh* ssh)
{
    *ssh = (BqOffhostSsh){.input = -1, .output = -1};
    char leaf[BQ_PATH_CAP + 1];
    int parent = bq_worker_open_lease_parent(BQ_OFFHOST_SSH_CONFIG, leaf);
    int config = parent >= 0 ? openat(parent, leaf, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC) : -1;
    struct stat info = {0}, directory = {0};
    BqError error = parent < 0 || fstat(parent, &directory) != 0 || directory.st_uid != 0 ||
        (directory.st_mode & 022) || config < 0 || fstat(config, &info) != 0 || !S_ISREG(info.st_mode) ||
        info.st_nlink != 1 || info.st_uid != 0 || (info.st_mode & 022) ? BQ_CONFIGURATION_MISMATCH : BQ_OK;
    if (config >= 0) close(config);
    if (parent >= 0) close(parent);
    int incoming[2] = {-1, -1}, outgoing[2] = {-1, -1};
    if (error == BQ_OK && (pipe2(incoming, O_CLOEXEC) != 0 || pipe2(outgoing, O_CLOEXEC) != 0)) error = BQ_IO;
    pid_t child = error == BQ_OK ? fork() : -1;
    if (error == BQ_OK && child < 0) error = BQ_IO;
    if (child == 0)
    {
        if (dup2(incoming[0], STDIN_FILENO) < 0 || dup2(outgoing[1], STDOUT_FILENO) < 0) _exit(126);
        close(incoming[0]); close(incoming[1]); close(outgoing[0]); close(outgoing[1]);
        char* const arguments[] = {"ssh", "-F", BQ_OFFHOST_SSH_CONFIG, "-T", "-oBatchMode=yes",
            "-oServerAliveInterval=0", "-oServerAliveCountMax=0", "-oTCPKeepAlive=no", "-oRekeyLimit=1G 0",
            "-oConnectTimeout=10",
            "buster-control", "/usr/local/libexec/buster-bench-service", "worker-stdio", NULL};
        execv("/usr/bin/ssh", arguments);
        _exit(127);
    }
    if (error == BQ_OK)
    {
        ssh->process = child;
        ssh->input = incoming[1];
        ssh->output = outgoing[0];
        incoming[1] = outgoing[0] = -1;
    }
    for (u32 i = 0; i < 2; i += 1)
    {
        if (incoming[i] >= 0) close(incoming[i]);
        if (outgoing[i] >= 0) close(outgoing[i]);
    }
    return error;
}

BUSTER_GLOBAL_LOCAL bool bq_offhost_cancel_identity(BqSessionPacket const* packet, BqOffhostIdentity const* identity)
{
    BqOffhostIdentity incoming = bq_session_identity(packet);
    bool ok = bq_session_valid(packet, false) && bq_u32(packet->bytes + 12) == BQ_SESSION_CANCEL &&
              packet->size == BQ_SESSION_HEADER && incoming.id == identity->id && incoming.token == identity->token &&
              !memcmp(incoming.control, identity->control, 64) && !memcmp(incoming.machine, identity->machine, 64);
    return ok;
}

BUSTER_GLOBAL_LOCAL BqError bq_offhost_roundtrip(BqOffhostSsh* ssh, BqSessionPacket const* request,
                                               BqSessionPacket* response, pid_t execution)
{
    BqSessionPacket outgoing = *request;
    BqOffhostIdentity identity = bq_session_identity(request);
    u64 deadline = bq_worker_deadline(bq_worker_monotonic_milliseconds(), BQ_EXPORT_READ_MILLISECONDS);
    BqError error = bq_session_stream(ssh->input, &outgoing, true, deadline);
    bool received = false;
    while (error == BQ_OK && !received)
    {
        error = bq_session_stream(ssh->output, response, false, deadline);
        if (error == BQ_OK && bq_offhost_cancel_identity(response, &identity))
        {
            if (execution > 0 && kill(execution, SIGTERM) != 0 && errno != ESRCH) error = BQ_IO;
            else if (!execution) ssh->cancel_pending = true;
        }
        else if (error == BQ_OK)
        {
            received = true;
            if (!bq_session_valid(response, true) || bq_u32(response->bytes + 12) != (bq_u32(request->bytes + 12) | BQ_SESSION_REPLY) ||
                memcmp(response->bytes + 104, request->bytes + 104, 64) ||
                (bq_u32(request->bytes + 12) != BQ_SESSION_HELLO &&
                 memcmp(response->bytes + 24, request->bytes + 24, 144))) error = BQ_BAD_REQUEST;
            else error = (BqError)bq_u32(response->bytes + 20);
        }
    }
    return error;
}

/* A verified committed local CAS needs no remote read. In particular, retrying
 * a lost quiet ACK cannot begin ordinary transport after quiet admission. */
BUSTER_GLOBAL_LOCAL BqError bq_offhost_native_cached(BqQueue* queue, String8 identity, bool* cached)
{
    *cached = false;
    char name[65] = {0}, program[65], manifest[BQ_NATIVE_MANIFEST_CAP], digest[65];
    u32 length = 0;
    u64 size = 0;
    BqError error = identity.length == 64 && bq_native_hex((u8 const*)identity.pointer) ? BQ_OK : BQ_BAD_REQUEST;
    if (error == BQ_OK) memcpy(name, identity.pointer, 64);
    int store = error == BQ_OK ? bq_native_store(queue) : -1;
    if (error == BQ_OK && store < 0) error = BQ_IO;
    int directory = error == BQ_OK ? openat(store, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC) : -1;
    if (error == BQ_OK && directory < 0 && errno != ENOENT) error = BQ_SOURCE_MISMATCH;
    struct stat info = {0};
    if (directory >= 0)
    {
        bool valid = fstat(directory, &info) == 0 && info.st_uid == geteuid() &&
                     (info.st_mode & 07777) == 0500 &&
                     bq_native_description(directory, name, manifest, &length, program, &size);
        int input = valid ? openat(directory, "program", O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC) : -1;
        valid = valid && bq_native_file(input, (off_t)size, 0400) && bq_native_elf(input, size, 0) &&
                bq_native_digest_fd(input, 0, size, digest, -1) && !memcmp(digest, program, 64);
        if (input >= 0) close(input);
        if (valid) *cached = true;
        else error = BQ_SOURCE_MISMATCH;
    }
    if (directory >= 0) close(directory);
    if (store >= 0) close(store);
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_offhost_worker_native(BqQueue* queue, BqOffhostSsh* ssh, BqOffhostIdentity const* identity)
{
    BqJob* job = bq_job(&queue->state, identity->id);
    BqError error = !job || job->token != identity->token ? BQ_CONFLICT : BQ_OK;
    bool native = error == BQ_OK && bq_recipe_native(bq_request_recipe(&job->request));
    bool cached = false;
    if (native) error = bq_offhost_native_cached(queue, bq_field(&job->request, 3), &cached);
    if (error == BQ_OK && native && !cached)
    {
        BqSessionPacket request, response;
        u8 cursor_body[8];
        bq_put64(cursor_body, UINT64_MAX);
        bq_session_packet(&request, BQ_SESSION_NATIVE_READ, BQ_OK, identity, cursor_body, sizeof(cursor_body));
        error = bq_offhost_roundtrip(ssh, &request, &response, 0);
        u8 metadata[72];
        u64 size = 0, cursor = 0;
        char installed[65];
        if (error == BQ_OK && response.size != BQ_SESSION_HEADER + sizeof(metadata)) error = BQ_BAD_REQUEST;
        if (error == BQ_OK)
        {
            memcpy(metadata, response.bytes + BQ_SESSION_HEADER, sizeof(metadata));
            size = bq_u64(metadata + 64);
            char manifest[BQ_NATIVE_MANIFEST_CAP], digest[65];
            if (bq_native_manifest(manifest, metadata, size, digest) <= 0 ||
                memcmp(digest, bq_field(&job->request, 3).pointer, 64)) error = BQ_SOURCE_MISMATCH;
            if (error == BQ_OK) error = bq_native_upload(queue, 1, metadata, sizeof(metadata), installed, &cursor);
        }
        while (error == BQ_OK && cursor < size)
        {
            bq_put64(cursor_body, cursor);
            bq_session_packet(&request, BQ_SESSION_NATIVE_READ, BQ_OK, identity, cursor_body, sizeof(cursor_body));
            error = bq_offhost_roundtrip(ssh, &request, &response, 0);
            u32 amount = response.size >= BQ_SESSION_HEADER ? response.size - BQ_SESSION_HEADER : 0;
            u8 const* body = response.bytes + BQ_SESSION_HEADER;
            if (error == BQ_OK && (amount <= 80 || amount > BQ_CONTROL_BODY || memcmp(body, metadata, 72) ||
                bq_u64(body + 72) != cursor || amount - 80 > size - cursor)) error = BQ_SOURCE_MISMATCH;
            u64 previous = cursor;
            if (error == BQ_OK) error = bq_native_upload(queue, 2, body, amount, installed, &cursor);
            if (error == BQ_OK && cursor != previous + amount - 80) error = BQ_INVALID_TRANSITION;
        }
        if (error == BQ_OK) error = bq_native_upload(queue, 3, metadata, sizeof(metadata), installed, &cursor);
        if (error == BQ_OK && (cursor != size || memcmp(installed, bq_field(&job->request, 3).pointer, 64))) error = BQ_SOURCE_MISMATCH;
    }
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_offhost_worker_upload(BqQueue* queue, BqOffhostSsh* ssh, BqOffhostIdentity const* identity)
{
    BqJob* job = bq_job(&queue->state, identity->id);
    BqError error = !job || job->token != identity->token || job->phase != BQ_FINISHED || !job->result_bound ?
                    BQ_RECONCILIATION_REQUIRED : bq_export_prepare(queue, job);
    char name[80];
    bq_export_name(name, identity->id, identity->token, false);
    int fd = error == BQ_OK ? openat(queue->directory_fd, name, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC) : -1;
    struct stat info = {0};
    if (error == BQ_OK && (fd < 0 || fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) || info.st_size < 0 || (u64)info.st_size < BQ_EXPORT_DATA_OFFSET ||
        (u64)info.st_size > BQ_EXPORT_DATA_OFFSET + BQ_EXPORT_TOTAL_CAP)) error = BQ_EXPORT_CORRUPT;
    u8 receipt[BQ_EXPORT_RECEIPT_CAP];
    if (error == BQ_OK && !bq_read(fd, receipt, sizeof(receipt), 0)) error = BQ_IO;
    BqSessionPacket request, response;
    if (error == BQ_OK)
    {
        bq_session_packet(&request, BQ_SESSION_RESULT, BQ_OK, identity, receipt, sizeof(receipt));
        error = bq_offhost_roundtrip(ssh, &request, &response, 0);
    }
    for (u64 cursor = 0; error == BQ_OK && cursor < (u64)info.st_size;)
    {
        u8 body[BQ_SESSION_BODY];
        u32 count = (u64)info.st_size - cursor < BQ_EXPORT_CHUNK_CAP ? (u32)((u64)info.st_size - cursor) : BQ_EXPORT_CHUNK_CAP;
        bq_put64(body, cursor);
        if (!bq_read(fd, body + 8, count, cursor)) error = BQ_IO;
        if (error == BQ_OK)
        {
            bq_session_packet(&request, BQ_SESSION_CHUNK, BQ_OK, identity, body, count + 8);
            error = bq_offhost_roundtrip(ssh, &request, &response, 0);
        }
        cursor += count;
    }
    if (fd >= 0) close(fd);
    if (error == BQ_OK)
    {
        bq_session_packet(&request, BQ_SESSION_COMPLETE, BQ_OK, identity, NULL, 0);
        error = bq_offhost_roundtrip(ssh, &request, &response, 0);
    }
    if (error == BQ_OK)
    {
        /* Local receipt proves we received cache-commit, never that an EOF did. */
        error = bq_offhost_mark(queue, "cache-ack", identity);
        if (error == BQ_OK)
        {
            bq_session_packet(&request, BQ_SESSION_TERMINAL_ACK, BQ_OK, identity, NULL, 0);
            error = bq_offhost_roundtrip(ssh, &request, &response, 0);
        }
        if (error == BQ_OK) error = bq_offhost_mark(queue, "terminal-ack", identity);
    }
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_offhost_deliver(BqQueue* queue, BqOffhostSsh* ssh, BqOffhostIdentity const* identity)
{
    BqJob* job = bq_job(&queue->state, identity->id);
    BqError error = !job || job->token != identity->token || job->phase != BQ_FINISHED ||
                    !job->result_bound || queue->needs_reconciliation ? BQ_RECONCILIATION_REQUIRED :
                    bq_worker_result_binding_validate(job);
    BqSessionPacket request, response;
            while (error == BQ_OK)
            {
                if (ssh->input < 0)
                {
                    error = bq_offhost_ssh_open(ssh);
                    if (error == BQ_OK)
                    {
                        bq_session_packet(&request, BQ_SESSION_HELLO, BQ_OK, identity, NULL, 0);
                        error = bq_offhost_roundtrip(ssh, &request, &response, 0);
                        if (error == BQ_OK && response.size == BQ_SESSION_HEADER)
                        {
                            error = bq_offhost_mark(queue, "terminal-ack", identity);
                            break;
                        }
                    }
                }
                if (error == BQ_OK) error = bq_offhost_worker_upload(queue, ssh, identity);
                if (error == BQ_IO || error == BQ_EXPORT_TIMEOUT)
                {
                    bq_offhost_ssh_close(ssh);
                    error = BQ_OK;
                    poll(NULL, 0, 1000);
                }
                else break;
            }
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_offhost_custodian(int channel, char const* root, BqOffhostIdentity const* identity, BqError run)
{
    u8 completed[24] = {0};
    bq_put64(completed, identity->id);
    bq_put64(completed + 8, identity->token);
    bq_put32(completed + 16, (u32)run);
    bool sent = send(channel, completed, sizeof(completed), MSG_NOSIGNAL) == sizeof(completed);
    bool acknowledged = false, orphaned = !sent;
    BqError error = BQ_OK;
    while (error == BQ_OK && !acknowledged && !orphaned)
    {
        struct pollfd waiting = {channel, POLLIN, 0};
        int ready = poll(&waiting, 1, -1);
        if (ready < 0 && errno == EINTR) continue;
        u8 receipt[24] = {0};
        ssize_t size = ready > 0 ? recv(channel, receipt, sizeof(receipt), MSG_TRUNC) : -1;
        orphaned = size <= 0;
        acknowledged = size == sizeof(receipt) && !memcmp(receipt, completed, 16) && !bq_u64(receipt + 16);
        if (size > 0 && !acknowledged) error = BQ_CONFLICT;
    }
    close(channel);
    if (orphaned && error == BQ_OK)
    {
        /* The execution child retains the original OFD until the control ACK.
         * Parent loss transfers delivery ownership to it, never to a new job. */
        BqQueue queue = {.directory_fd = -1, .lock_fd = -1, .journal_fd = -1};
        do
        {
            error = bq_open(&queue, root);
            if (error == BQ_BUSY) poll(NULL, 0, 1000);
        } while (error == BQ_BUSY);
        BqOffhostSsh ssh = {.input = -1, .output = -1};
        if (error == BQ_OK) error = bq_offhost_deliver(&queue, &ssh, identity);
        bq_offhost_ssh_close(&ssh);
        bq_close(&queue);
    }
    if (error != BQ_OK)
    {
        fprintf(stderr, "offhost lease custodian quarantined: %s\n", bq_error_name(error));
        fflush(stderr);
        for (;;) poll(NULL, 0, 1000);
    }
    return error;
}

BUSTER_GLOBAL_LOCAL bool bq_offhost_binary_digest(int fd, u64 size, char digest[SHA256_HEX_CAPACITY])
{
    bool ok = size > 0 && size <= BQ_WORKER_BUNDLE_FILE_CAP;
    Sha256 hash;
    sha256_init(&hash);
    for (u64 offset = 0; ok && offset < size;)
    {
        u8 bytes[BQ_EXPORT_CHUNK_CAP];
        u32 count = size - offset < sizeof(bytes) ? (u32)(size - offset) : sizeof(bytes);
        ok = bq_read(fd, bytes, count, offset);
        if (ok) sha256_add(&hash, bytes, count);
        offset += count;
    }
    if (ok) sha256_finish_hex(&hash, digest);
    return ok;
}

BUSTER_GLOBAL_LOCAL BqError bq_offhost_recover_custody(BqQueue* queue, BqWorkerConfig const* installed,
                                                     BqOffhostIdentity const* identity, BqWorkerLease* lease)
{
    BqJob* pending = bq_job(&queue->state, identity->id);
    BqError entered = bq_offhost_marked(queue, "execution-entered", identity);
    BqError custody = bq_offhost_marked(queue, "lease-custody", identity);
    BqError error = !pending || pending->token != identity->token ? BQ_CONFLICT :
                    entered != BQ_OK && entered != BQ_NOT_FOUND ? entered :
                    custody != BQ_OK && custody != BQ_NOT_FOUND ? custody : BQ_OK;
    bool uncertain = entered == BQ_OK || custody == BQ_OK;
    if (error == BQ_OK && uncertain && pending->phase != BQ_FINISHED)
    {
        /* A new process has no proof of continuity of the previous OFD. First
         * recover the actual installed unit/cgroup/cleanup locally, never SSH. */
        BqWorkerConfig recovery = *installed;
        recovery.assigned_attempt = true;
        recovery.retained_lease_descriptor = -1;
        queue->needs_reconciliation = true;
        u64 id = identity->id;
        error = bq_worker_run(queue, &recovery, &id);
        if (installed->quarantine->descriptor >= 0)
        {
            lease->descriptor = installed->quarantine->descriptor;
            installed->quarantine->descriptor = -1;
        }
    }
    if (error == BQ_OK && uncertain && lease->descriptor < 0)
    {
        char path[BQ_PATH_CAP + 1];
        if (!bq_worker_text(installed->lease_file, path, sizeof(path)) || bq_worker_lease_acquire(path, lease) != 0)
            error = BQ_BUSY;
    }
    if (error == BQ_OK && uncertain)
    {
        /* Whole-unit stop/reboot can kill every custodian. Reacquisition is
         * not continuity: retain the new lease and a durable gap quarantine.
         * Clearing it requires an operator-reviewed recovery, no timed retry. */
        queue->needs_reconciliation = true;
        error = bq_offhost_mark(queue, "lease-gap", identity);
        if (error == BQ_OK) error = BQ_RECONCILIATION_REQUIRED;
    }
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_offhost_worker_agent(char const* root, BqWorkerConfig const* installed)
{
    BqQueue queue = {.directory_fd = -1, .lock_fd = -1, .journal_fd = -1};
    BqWorkerLease lease = {.descriptor = -1};
    BqOffhostSsh ssh = {.input = -1, .output = -1};
    BqOffhostIdentity identity = {0};
    struct sigaction ignored = {.sa_handler = SIG_IGN}, previous_pipe = {0};
    sigemptyset(&ignored.sa_mask);
    BqError error = sigaction(SIGPIPE, &ignored, &previous_pipe) == 0 ? bq_offhost_operator_identity(identity.machine) : BQ_IO;
    if (error == BQ_OK) error = bq_open(&queue, root);
    if (error == BQ_OK && queue.state.journal_schema && queue.state.journal_schema != BQ_SCHEMA_ASSIGNED) error = BQ_CONFIGURATION_MISMATCH;
    for (u32 i = 0; error == BQ_OK && i < queue.state.job_count; i += 1)
    {
        BqJob* job = queue.state.jobs + i;
        char name[96];
        bq_offhost_name(name, "assignment", job->id, job->token);
        BqSessionPacket assignment = {.size = BQ_SESSION_HEADER + job->request.size};
        error = bq_offhost_read(&queue, name, assignment.bytes, assignment.size);
        if (error == BQ_OK)
        {
            BqOffhostIdentity existing = bq_session_identity(&assignment);
            BqError ack = bq_offhost_marked(&queue, "terminal-ack", &existing);
            if (ack == BQ_NOT_FOUND)
            {
                if (identity.id) error = BQ_CONFLICT;
                else identity = existing;
            }
            else if (ack != BQ_OK) error = ack;
        }
    }
    if (error == BQ_OK && identity.id) error = bq_offhost_recover_custody(&queue, installed, &identity, &lease);
    int completion = -1;
    pid_t custodian = -1;
    while (error == BQ_OK)
    {
        BqSessionPacket request, response;
        error = bq_offhost_ssh_open(&ssh);
        if (error == BQ_OK)
        {
            bq_session_packet(&request, BQ_SESSION_HELLO, BQ_OK, &identity, NULL, 0);
            error = bq_offhost_roundtrip(&ssh, &request, &response, 0);
        }
        if (error == BQ_NOT_FOUND)
        {
            error = BQ_OK;
            bq_offhost_ssh_close(&ssh);
            poll(NULL, 0, 1000);
            continue;
        }
        if (error != BQ_OK) break;
        BqOffhostIdentity assigned = bq_session_identity(&response);
        BqRequest work = {.size = response.size - BQ_SESSION_HEADER};
        if (!work.size && identity.id)
        {
            error = bq_offhost_mark(&queue, "terminal-ack", &identity);
            if (error == BQ_OK)
            {
                identity.id = identity.token = 0;
                bq_worker_lease_release(&lease);
                bq_offhost_ssh_close(&ssh);
                continue;
            }
        }
        if (error == BQ_OK && (!assigned.id || !assigned.token || work.size > BQ_REQUEST_CAP)) error = BQ_BAD_REQUEST;
        if (error == BQ_OK)
        {
            memcpy(work.bytes, response.bytes + BQ_SESSION_HEADER, work.size);
            error = bq_assigned_import(&queue, &work, assigned.id, assigned.token);
        }
        if (error == BQ_OK)
        {
            identity = assigned;
            char name[96];
            bq_offhost_name(name, "assignment", identity.id, identity.token);
            error = bq_offhost_record(&queue, name, response.bytes, response.size);
        }
        char lease_path[BQ_PATH_CAP + 1];
        if (error == BQ_OK && lease.descriptor < 0)
        {
            if (!bq_worker_text(installed->lease_file, lease_path, sizeof(lease_path)) ||
                bq_worker_lease_acquire(lease_path, &lease) != 0) error = BQ_BUSY;
        }
        if (error == BQ_OK)
        {
            char custody_name[96];
            bq_offhost_name(custody_name, "worker-custody", identity.id, identity.token);
            u8 custody[129];
            error = bq_offhost_read(&queue, custody_name, custody, 128u);
            if (error == BQ_NOT_FOUND)
            {
                int binary = open("/usr/local/libexec/buster-bench-service", O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
                struct stat info = {0};
                char boot_path[BQ_PATH_CAP + 1], boot[BQ_WORKER_BOOT_CAP];
                error = binary < 0 || fstat(binary, &info) != 0 || !S_ISREG(info.st_mode) || info.st_uid != 0 ||
                    (info.st_mode & 022) || !bq_offhost_binary_digest(binary, (u64)info.st_size, (char*)custody) ||
                    !bq_worker_text(installed->boot_id_file, boot_path, sizeof(boot_path)) ||
                    !bq_worker_read_regular(boot_path, boot, sizeof(boot)) || !bq_worker_boot_valid(boot) ?
                    BQ_CONFIGURATION_MISMATCH : BQ_OK;
                if (binary >= 0) close(binary);
                if (error == BQ_OK)
                {
                    bq_digest(boot, (u32)strlen(boot), (char*)custody + 64);
                    error = bq_offhost_record(&queue, custody_name, custody, 128u);
                }
            }
            bq_session_packet(&request, BQ_SESSION_CUSTODY, BQ_OK, &identity, custody, 128u);
            if (error == BQ_OK) error = bq_offhost_roundtrip(&ssh, &request, &response, 0);
        }
        BqJob* job = bq_job(&queue.state, identity.id);
        if (error == BQ_OK && job && job->phase != BQ_FINISHED)
        {
            error = bq_offhost_worker_native(&queue, &ssh, &identity);
            if (error == BQ_OK) error = bq_offhost_mark(&queue, "lease-custody", &identity);
            bq_session_packet(&request, BQ_SESSION_QUIET, BQ_OK, &identity, NULL, 0);
            if (error == BQ_OK) error = bq_offhost_roundtrip(&ssh, &request, &response, 0);
            BqError started = bq_offhost_marked(&queue, "execution-entered", &identity);
            if (error == BQ_OK && started != BQ_OK && started != BQ_NOT_FOUND) error = started;
            bool recovery = started == BQ_OK;
            if (error == BQ_OK) error = bq_offhost_mark(&queue, "execution-entered", &identity);
            if (error == BQ_OK && ssh.cancel_pending) error = bq_cancel(&queue, identity.id);
            if (error == BQ_OK)
            {
                queue.needs_reconciliation = recovery;
                sigset_t blocked, previous;
                sigemptyset(&blocked);
                sigaddset(&blocked, SIGTERM);
                sigaddset(&blocked, SIGINT);
                bool masked = sigprocmask(SIG_BLOCK, &blocked, &previous) == 0;
                int channels[2] = {-1, -1};
                bool channel = masked && socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, channels) == 0;
                pid_t execution = channel ? fork() : -1;
                if (execution < 0)
                {
                    error = BQ_IO;
                    if (masked) sigprocmask(SIG_SETMASK, &previous, NULL);
                    if (channels[0] >= 0) close(channels[0]);
                    if (channels[1] >= 0) close(channels[1]);
                }
                else if (execution == 0)
                {
                    close(channels[0]);
                    close(ssh.input); close(ssh.output);
                    BqWorkerConfig config = *installed;
                    config.assigned_attempt = true;
                    config.retained_lease_descriptor = lease.descriptor;
                    if (ssh.cancel_pending) bq_worker_transport_stop_signal = 1;
                    u64 id = identity.id;
                    BqError run = bq_worker_run(&queue, &config, &id);
                    bq_close(&queue);
                    BqError delivered = bq_offhost_custodian(channels[1], root, &identity, run);
                    _exit(delivered == BQ_OK ? 0 : 1);
                }
                else
                {
                    close(channels[1]);
                    completion = channels[0];
                    custodian = execution;
                    sigprocmask(SIG_SETMASK, &previous, NULL);
                    bq_close(&queue);
                    bool finished = false;
                    while (!finished)
                    {
                        int status = 0;
                        pid_t waited = waitpid(execution, &status, WNOHANG);
                        if (waited == execution) { error = BQ_WORKER_INTERRUPTED; finished = true; custodian = -1; }
                        else if (waited < 0 && errno != EINTR) { error = BQ_IO; finished = true; }
                        else
                        {
                            struct pollfd waiting[2] = {{ssh.output, POLLIN, 0}, {completion, POLLIN, 0}};
                            int ready = poll(waiting, 2, 1000);
                            if (ready > 0 && (waiting[1].revents & (POLLIN | POLLHUP | POLLERR)))
                            {
                                u8 done[24];
                                ssize_t amount = recv(completion, done, sizeof(done), MSG_TRUNC);
                                if (amount != sizeof(done) || bq_u64(done) != identity.id || bq_u64(done + 8) != identity.token)
                                    error = BQ_WORKER_MISMATCH;
                                finished = true;
                            }
                            if (ready > 0 && (waiting[0].revents & (POLLIN | POLLHUP | POLLERR)))
                            {
                                BqSessionPacket exceptional = {0};
                                BqError read = bq_session_stream(ssh.output, &exceptional, false,
                                    bq_worker_deadline(bq_worker_monotonic_milliseconds(), BQ_EXPORT_READ_MILLISECONDS));
                                if (read == BQ_OK && bq_offhost_cancel_identity(&exceptional, &identity))
                                {
                                    if (!finished) kill(execution, SIGTERM);
                                }
                                else
                                {
                                    /* Channel loss never terminates or duplicates admitted work. */
                                    bq_offhost_ssh_close(&ssh);
                                }
                            }
                        }
                    }
                    BqError reopened = bq_open(&queue, root);
                    if (error == BQ_OK) error = reopened;
                }
            }
        }
        if (error == BQ_OK) error = bq_offhost_deliver(&queue, &ssh, &identity);
        if (error == BQ_OK)
        {
            if (completion >= 0)
            {
                u8 receipt[24] = {0};
                bq_put64(receipt, identity.id);
                bq_put64(receipt + 8, identity.token);
                if (send(completion, receipt, sizeof(receipt), MSG_NOSIGNAL) != sizeof(receipt)) error = BQ_IO;
                close(completion);
                completion = -1;
                int status = 0;
                if (custodian > 0 && bq_worker_waitpid_until(custodian, &status,
                    bq_worker_deadline(bq_worker_monotonic_milliseconds(), 5000)) != BQ_OK) error = BQ_IO;
                custodian = -1;
            }
            if (error == BQ_OK) bq_worker_lease_release(&lease);
            identity.id = identity.token = 0;
            bq_offhost_ssh_close(&ssh);
        }
    }
    bq_offhost_ssh_close(&ssh);
    if (completion >= 0) close(completion);
    if (lease.descriptor >= 0)
    {
        /* Preserve quarantine in the live coordinator. Installed restart must
         * reconcile its durable ledger before any new assignment is accepted. */
        bq_worker_quarantine.descriptor = lease.descriptor;
        snprintf(bq_worker_quarantine.lease_path, sizeof(bq_worker_quarantine.lease_path), "%.*s",
                 (int)installed->lease_file.length, installed->lease_file.pointer);
        lease.descriptor = -1;
    }
    bq_close(&queue);
    if (bq_worker_quarantine.descriptor >= 0)
    {
        fprintf(stderr, "offhost worker quarantined: %s; retained lease requires operator reconciliation\n", bq_error_name(error));
        fflush(stderr);
        /* Installed process stays alive holding the same OFD after uncertainty.
         * No new network activity or assignment is attempted in quarantine. */
        for (;;) poll(NULL, 0, 1000);
    }
    sigaction(SIGPIPE, &previous_pipe, NULL);
    return error;
}
#else
BUSTER_GLOBAL_LOCAL BqError bq_control_serve(char const* root, char const* public_path, char const* worker_path, char const machine[SHA256_HEX_CAPACITY])
{
    (void)root; (void)public_path; (void)worker_path; (void)machine;
    return BQ_UNSUPPORTED;
}
BUSTER_GLOBAL_LOCAL BqError bq_worker_stdio(int input, int output, char const* socket_path)
{
    (void)input; (void)output; (void)socket_path;
    return BQ_UNSUPPORTED;
}
BUSTER_GLOBAL_LOCAL BqError bq_offhost_worker_agent(char const* root, BqWorkerConfig const* config)
{
    (void)root; (void)config;
    return BQ_UNSUPPORTED;
}
#endif
