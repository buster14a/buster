/* Off-host session codec and immutable custody records for #437.
 * bq_session_valid checks bounded authenticated frames, bq_session_stream
 * frames fixed SSH pipes, and bq_offhost_record publishes no-replace records.
 * SSH authenticates the installed worker account; no request selects a host,
 * path, command or account. A session is not a lease or an execution proof.
 */
#define BQ_SESSION_HEADER 168u
#define BQ_SESSION_BODY (BQ_EXPORT_CHUNK_CAP + 8u)
#define BQ_SESSION_CAP (BQ_SESSION_HEADER + BQ_SESSION_BODY)
#define BQ_SESSION_VERSION 1u
#define BQ_SESSION_REPLY 0x80000000u
#define BQ_OFFHOST_WORKER_SOCKET "/run/buster-bench/worker.sock"
#define BQ_OFFHOST_CONTROL_ROOT "/var/lib/buster-bench-control/queue"
#define BQ_OFFHOST_SSH_CONFIG "/etc/buster-bench/worker-ssh.conf"
#define BQ_OFFHOST_MACHINE "buster-fixed-benchmark-worker-v1"

typedef enum BqSessionOperation
{
    BQ_SESSION_HELLO = 1, BQ_SESSION_CUSTODY, BQ_SESSION_QUIET,
    BQ_SESSION_RESULT, BQ_SESSION_CHUNK, BQ_SESSION_COMPLETE,
    BQ_SESSION_TERMINAL_ACK, BQ_SESSION_CANCEL
} BqSessionOperation;

typedef struct BqSessionPacket
{
    u32 size;
    u8 bytes[BQ_SESSION_CAP];
} BqSessionPacket;

typedef struct BqOffhostIdentity
{
    char control[SHA256_HEX_CAPACITY];
    char machine[SHA256_HEX_CAPACITY];
    u64 id;
    u64 token;
} BqOffhostIdentity;

BUSTER_GLOBAL_LOCAL void bq_session_packet(BqSessionPacket* packet, u32 operation, BqError error,
                                           BqOffhostIdentity const* identity, void const* body, u32 size)
{
    *packet = (BqSessionPacket){0};
    if (size <= BQ_SESSION_BODY)
    {
        memcpy(packet->bytes, "BQSSH001", 8);
        bq_put32(packet->bytes + 8, BQ_SESSION_VERSION);
        bq_put32(packet->bytes + 12, operation);
        bq_put32(packet->bytes + 16, size);
        bq_put32(packet->bytes + 20, (u32)error);
        if (identity)
        {
            bq_put64(packet->bytes + 24, identity->id);
            bq_put64(packet->bytes + 32, identity->token);
            memcpy(packet->bytes + 40, identity->control, 64);
            memcpy(packet->bytes + 104, identity->machine, 64);
        }
        if (size) memcpy(packet->bytes + BQ_SESSION_HEADER, body, size);
        packet->size = BQ_SESSION_HEADER + size;
    }
}

BUSTER_GLOBAL_LOCAL bool bq_session_valid(BqSessionPacket const* packet, bool reply)
{
    u32 operation = packet->size >= BQ_SESSION_HEADER ? bq_u32(packet->bytes + 12) : 0;
    bool valid = packet->size >= BQ_SESSION_HEADER && packet->size <= BQ_SESSION_CAP &&
                 !memcmp(packet->bytes, "BQSSH001", 8) && bq_u32(packet->bytes + 8) == BQ_SESSION_VERSION &&
                 bq_u32(packet->bytes + 16) == packet->size - BQ_SESSION_HEADER &&
                 (operation & BQ_SESSION_REPLY) == (reply ? BQ_SESSION_REPLY : 0) &&
                 (operation & ~BQ_SESSION_REPLY) >= BQ_SESSION_HELLO &&
                 (operation & ~BQ_SESSION_REPLY) <= BQ_SESSION_CANCEL &&
                 bq_u32(packet->bytes + 20) <= BQ_EXPORT_TIMEOUT && (reply || !bq_u32(packet->bytes + 20)) &&
                 bq_result_digest_valid(packet->bytes + 104);
    bool control_empty = true;
    for (u32 i = 0; i < 64; i += 1) control_empty = control_empty && packet->bytes[40 + i] == 0;
    valid = valid && (bq_result_digest_valid(packet->bytes + 40) ||
                     (control_empty && (operation & ~BQ_SESSION_REPLY) == BQ_SESSION_HELLO));
    return valid;
}

BUSTER_GLOBAL_LOCAL BqOffhostIdentity bq_session_identity(BqSessionPacket const* packet)
{
    BqOffhostIdentity identity = {.id = bq_u64(packet->bytes + 24), .token = bq_u64(packet->bytes + 32)};
    memcpy(identity.control, packet->bytes + 40, 64);
    memcpy(identity.machine, packet->bytes + 104, 64);
    return identity;
}

#ifdef __linux__
BUSTER_GLOBAL_LOCAL BqError bq_offhost_record(BqQueue* queue, char const* name, void const* bytes, u32 size)
{
    BqError error = !name || !size || size > BQ_SESSION_BODY || queue->poisoned ? BQ_BAD_REQUEST : BQ_OK;
    int fd = error == BQ_OK ? openat(queue->directory_fd, name,
             O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0400) : -1;
    if (error == BQ_OK && fd < 0 && errno == EEXIST)
    {
        fd = openat(queue->directory_fd, name, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
        struct stat info = {0};
        u8 previous[BQ_SESSION_BODY];
        error = fd < 0 || fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) || info.st_uid != geteuid() ||
                info.st_nlink != 1 || (info.st_mode & 0777) != 0400 || info.st_size != size ? BQ_CORRUPT : BQ_OK;
        if (error == BQ_OK && (!bq_read(fd, previous, size, 0) || memcmp(previous, bytes, size))) error = BQ_CONFLICT;
    }
    else if (error == BQ_OK)
    {
        u32 done = 0;
        while (error == BQ_OK && done < size)
        {
            ssize_t count = write(fd, (u8 const*)bytes + done, size - done);
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) error = BQ_IO;
            else done += (u32)count;
        }
        if (error == BQ_OK && (fsync(fd) != 0 || fsync(queue->directory_fd) != 0)) error = BQ_IO;
    }
    if (fd >= 0) close(fd);
    if (error == BQ_IO) queue->poisoned = true;
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_offhost_read(BqQueue* queue, char const* name, void* bytes, u32 size)
{
    int fd = openat(queue->directory_fd, name, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    struct stat info = {0};
    BqError error = fd < 0 ? errno == ENOENT ? BQ_NOT_FOUND : BQ_IO :
                    fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) || info.st_nlink != 1 ||
                    info.st_uid != geteuid() || (info.st_mode & 0777) != 0400 || info.st_size != size ? BQ_CORRUPT : BQ_OK;
    if (error == BQ_OK && !bq_read(fd, bytes, size, 0)) error = BQ_IO;
    if (fd >= 0) close(fd);
    return error;
}

BUSTER_GLOBAL_LOCAL bool bq_offhost_name(char name[96], char const* prefix, u64 id, u64 token)
{
    int count = snprintf(name, 96, "offhost-%s-%llu-%llu", prefix, (unsigned long long)id, (unsigned long long)token);
    bool ok = count > 0 && count < 96;
    return ok;
}

BUSTER_GLOBAL_LOCAL BqError bq_offhost_mark(BqQueue* queue, char const* prefix, BqOffhostIdentity const* identity)
{
    char name[96];
    BqSessionPacket packet;
    bq_session_packet(&packet, BQ_SESSION_CUSTODY, BQ_OK, identity, NULL, 0);
    BqError error = bq_offhost_name(name, prefix, identity->id, identity->token) ?
                    bq_offhost_record(queue, name, packet.bytes, packet.size) : BQ_BAD_REQUEST;
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_offhost_marked(BqQueue* queue, char const* prefix, BqOffhostIdentity const* identity)
{
    char name[96];
    BqSessionPacket packet, expected;
    bq_session_packet(&expected, BQ_SESSION_CUSTODY, BQ_OK, identity, NULL, 0);
    BqError error = bq_offhost_name(name, prefix, identity->id, identity->token) ?
                    bq_offhost_read(queue, name, packet.bytes, expected.size) : BQ_BAD_REQUEST;
    if (error == BQ_OK && memcmp(packet.bytes, expected.bytes, expected.size)) error = BQ_CONFLICT;
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_offhost_observe(BqQueue* queue, char const* stage, BqOffhostIdentity const* identity)
{
    char prefix[64], name[96];
    int length = snprintf(prefix, sizeof(prefix), "observed-%s", stage);
    u8 record[8];
    BqError error = length <= 0 || length >= (int)sizeof(prefix) ||
                    !bq_offhost_name(name, prefix, identity->id, identity->token) ? BQ_BAD_REQUEST :
                    bq_offhost_read(queue, name, record, sizeof(record));
    if (error == BQ_NOT_FOUND)
    {
        struct timespec now;
        error = clock_gettime(CLOCK_REALTIME, &now) != 0 || now.tv_sec <= 0 ? BQ_IO : BQ_OK;
        if (error == BQ_OK)
        {
            bq_put64(record, (u64)now.tv_sec * 1000 + (u64)now.tv_nsec / 1000000);
            error = bq_offhost_record(queue, name, record, sizeof(record));
        }
    }
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_offhost_observed(BqQueue* queue, BqOffhostIdentity const* identity, u64* timestamp)
{
    *timestamp = 0;
    char const* stages[] = {"submit", "assignment", "custody", "quiet", "receipt", "cache", "ack"};
    BqError error = BQ_OK;
    for (u32 i = 0; error == BQ_OK && i < BUSTER_ARRAY_LENGTH(stages); i += 1)
    {
        char prefix[64], name[96];
        snprintf(prefix, sizeof(prefix), "observed-%s", stages[i]);
        bq_offhost_name(name, prefix, identity->id, identity->token);
        u8 record[8];
        BqError read = bq_offhost_read(queue, name, record, sizeof(record));
        if (read == BQ_OK) *timestamp = bq_u64(record);
        else if (read != BQ_NOT_FOUND) error = read;
    }
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_offhost_operator_identity(char machine[SHA256_HEX_CAPACITY])
{
    char leaf[BQ_PATH_CAP + 1];
    int parent = bq_worker_open_lease_parent("/etc/buster-bench/worker-id.sha256", leaf);
    int fd = parent >= 0 ? openat(parent, leaf, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC) : -1;
    struct stat info = {0};
    BqError error = fd < 0 || fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) || info.st_uid != 0 ||
        info.st_nlink != 1 || (info.st_mode & 022) || info.st_size != 64 || !bq_read(fd, (u8*)machine, 64, 0) ||
        !bq_result_digest_valid((u8*)machine) ? BQ_CONFIGURATION_MISMATCH : BQ_OK;
    machine[64] = 0;
    if (fd >= 0) close(fd);
    if (parent >= 0) close(parent);
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_session_io(int fd, void* bytes, u32 size, bool writing, u64 deadline)
{
    BqError error = BQ_OK;
    u32 done = 0;
    while (error == BQ_OK && done < size)
    {
        int remaining = bq_worker_remaining(deadline);
        struct pollfd wait = {fd, writing ? POLLOUT : POLLIN, 0};
        int ready = remaining ? poll(&wait, 1, remaining) : 0;
        if (ready < 0 && errno == EINTR) continue;
        if (ready <= 0) error = ready == 0 ? BQ_EXPORT_TIMEOUT : BQ_IO;
        else
        {
            ssize_t count = writing ? write(fd, (u8*)bytes + done, size - done) : read(fd, (u8*)bytes + done, size - done);
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) error = BQ_IO;
            else done += (u32)count;
        }
    }
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_session_stream(int fd, BqSessionPacket* packet, bool writing, u64 deadline)
{
    BqError error = BQ_OK;
    if (writing) error = packet->size >= BQ_SESSION_HEADER && packet->size <= BQ_SESSION_CAP ?
                          bq_session_io(fd, packet->bytes, packet->size, true, deadline) : BQ_BAD_REQUEST;
    else
    {
        packet->size = 0;
        error = bq_session_io(fd, packet->bytes, BQ_SESSION_HEADER, false, deadline);
        u32 size = error == BQ_OK ? bq_u32(packet->bytes + 16) : 0;
        if (error == BQ_OK && size > BQ_SESSION_BODY) error = BQ_BAD_REQUEST;
        if (error == BQ_OK && size) error = bq_session_io(fd, packet->bytes + BQ_SESSION_HEADER, size, false, deadline);
        if (error == BQ_OK) packet->size = BQ_SESSION_HEADER + size;
    }
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_session_receive(int fd, BqSessionPacket* packet)
{
    ssize_t count = recv(fd, packet->bytes, sizeof(packet->bytes), MSG_TRUNC | MSG_DONTWAIT);
    BqError error = count >= BQ_SESSION_HEADER && count <= BQ_SESSION_CAP ? BQ_OK : BQ_BAD_REQUEST;
    packet->size = error == BQ_OK ? (u32)count : 0;
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_session_send(int fd, BqSessionPacket const* packet)
{
    ssize_t count = send(fd, packet->bytes, packet->size, MSG_NOSIGNAL | MSG_DONTWAIT);
    BqError error = count == packet->size ? BQ_OK : BQ_IO;
    return error;
}
#endif
