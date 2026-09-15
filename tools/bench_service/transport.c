/* Authenticated local control service for #437.
 *
 * The queue remains a single-writer object.  This boundary owns one queue
 * handle, accepts one bounded seqpacket per connection, authenticates the
 * peer's Unix credentials, and dispatches only public queue operations.  The
 * installed roots and lease are service configuration; they never cross the
 * client protocol.  A real recipe is started only from that fixed
 * configuration and remains fail-closed until the build-driver handoff adds
 * its reviewed executor.
 */
#ifdef __linux__
#include <stddef.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>

#define BQ_TRANSPORT_BACKLOG 8
#define BQ_TRANSPORT_POLL_MILLISECONDS 100
#define BQ_TRANSPORT_IO_MILLISECONDS 1000
#define BQ_TRANSPORT_CLIENT_MILLISECONDS 30000

typedef struct BqTransportEndpoint
{
    int listener;
    int parent;
    char leaf[BQ_PATH_CAP + 1];
    dev_t device;
    ino_t inode;
} BqTransportEndpoint;

BUSTER_GLOBAL_LOCAL volatile sig_atomic_t bq_transport_stop_signal;
BUSTER_GLOBAL_LOCAL BqError bq_transport_endpoint_close(BqTransportEndpoint* endpoint);

BUSTER_GLOBAL_LOCAL bool bq_transport_socket_path(char const* text, char output[BQ_PATH_CAP + 1])
{
    String8 path = string_from_pointer(text);
    bool ok = bq_string_path(path, output);
    if (ok)
    {
        char* slash = strrchr(output, '/');
        ok = slash && slash[1] && strlen(output) < sizeof(((struct sockaddr_un*)0)->sun_path);
    }
    return ok;
}

BUSTER_GLOBAL_LOCAL void bq_transport_stop_handler(int signal_number)
{
    (void)signal_number;
    bq_transport_stop_signal = 1;
    bq_worker_transport_stop_signal = 1;
}

BUSTER_GLOBAL_LOCAL bool bq_transport_peer_allowed(int client)
{
    struct ucred peer = {0};
    socklen_t size = sizeof(peer);
    bool ok = getsockopt(client, SOL_SOCKET, SO_PEERCRED, &peer, &size) == 0 && size == sizeof(peer) &&
              peer.uid == geteuid() && peer.gid == getegid();
    return ok;
}

BUSTER_GLOBAL_LOCAL BqError bq_transport_endpoint_open(char const* socket_path, BqTransportEndpoint* endpoint)
{
    *endpoint = (BqTransportEndpoint){.listener = -1, .parent = -1};
    char path[BQ_PATH_CAP + 1];
    BqError error = bq_transport_socket_path(socket_path, path) ? BQ_OK : BQ_BAD_REQUEST;
    if (error == BQ_OK)
    {
        endpoint->parent = bq_worker_open_lease_parent(path, endpoint->leaf);
        error = endpoint->parent >= 0 ? BQ_OK : BQ_CONFIGURATION_MISMATCH;
    }
    struct stat existing = {0};
    if (error == BQ_OK)
    {
        errno = 0;
        bool present = fstatat(endpoint->parent, endpoint->leaf, &existing, AT_SYMLINK_NOFOLLOW) == 0;
        error = present ? BQ_BUSY : errno == ENOENT ? BQ_OK : BQ_IO;
    }
    if (error == BQ_OK)
    {
        endpoint->listener = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
        error = endpoint->listener >= 0 ? BQ_OK : BQ_IO;
    }
    struct sockaddr_un address = {0};
    if (error == BQ_OK)
    {
        size_t length = strlen(path);
        address.sun_family = AF_UNIX;
        memcpy(address.sun_path, path, length + 1);
        socklen_t address_size = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + length + 1);
        mode_t prior_umask = umask(0077);
        int bound = bind(endpoint->listener, (struct sockaddr*)&address, address_size);
        int saved_errno = errno;
        umask(prior_umask);
        errno = saved_errno;
        error = bound == 0 ? BQ_OK : BQ_IO;
    }
    if (error == BQ_OK)
    {
        struct stat bound = {0};
        if (fstatat(endpoint->parent, endpoint->leaf, &bound, AT_SYMLINK_NOFOLLOW) == 0 && S_ISSOCK(bound.st_mode))
        {
            endpoint->device = bound.st_dev;
            endpoint->inode = bound.st_ino;
        }
        else
        {
            error = BQ_IO;
        }
    }
    if (error == BQ_OK)
    {
        error = listen(endpoint->listener, BQ_TRANSPORT_BACKLOG) == 0 ? BQ_OK : BQ_IO;
    }
    if (error == BQ_OK)
    {
        struct stat bound = {0};
        error = fstatat(endpoint->parent, endpoint->leaf, &bound, AT_SYMLINK_NOFOLLOW) == 0 && S_ISSOCK(bound.st_mode) &&
                bound.st_uid == geteuid() && (bound.st_mode & 077) == 0 && bound.st_dev == endpoint->device &&
                bound.st_ino == endpoint->inode ? BQ_OK : BQ_CONFIGURATION_MISMATCH;
    }
    if (error != BQ_OK)
    {
        BqError cleanup = bq_transport_endpoint_close(endpoint);
        if (cleanup != BQ_OK) error = cleanup;
    }
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_transport_endpoint_close(BqTransportEndpoint* endpoint)
{
    BqError error = BQ_OK;
    if (endpoint->listener >= 0)
    {
        if (close(endpoint->listener) != 0) error = BQ_IO;
    }
    if (endpoint->parent >= 0)
    {
        bool owned = false;
        bool absent = true;
        if (endpoint->device && endpoint->inode)
        {
            struct stat info = {0};
            errno = 0;
            int inspected = fstatat(endpoint->parent, endpoint->leaf, &info, AT_SYMLINK_NOFOLLOW);
            absent = inspected != 0 && errno == ENOENT;
            owned = inspected == 0 && S_ISSOCK(info.st_mode) && info.st_dev == endpoint->device && info.st_ino == endpoint->inode;
            if (owned && unlinkat(endpoint->parent, endpoint->leaf, 0) != 0)
            {
                error = BQ_IO;
            }
            else if (owned && fsync(endpoint->parent) != 0)
            {
                error = BQ_IO;
            }
            else if (!owned && !absent && error == BQ_OK)
            {
                error = BQ_CONFIGURATION_MISMATCH;
            }
        }
        if (close(endpoint->parent) != 0 && error == BQ_OK) error = BQ_IO;
    }
    endpoint->listener = endpoint->parent = -1;
    endpoint->device = endpoint->inode = 0;
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_transport_public_operation(u32 operation)
{
    bool public_operation = operation == BQ_OP_CAPABILITIES || operation == BQ_OP_SUBMIT ||
                            operation == BQ_OP_STATUS || operation == BQ_OP_RESULT || operation == BQ_OP_CANCEL ||
                            operation == BQ_OP_LOGS;
    BqError error = public_operation ? BQ_OK : BQ_BAD_REQUEST;
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_transport_public_request(u8 const* input, u32 size)
{
    BqError error = size >= BQ_CONTROL_HEADER && size <= BQ_CONTROL_CAP && !memcmp(input, "BQP1", 4) ? BQ_OK : BQ_BAD_REQUEST;
    u32 operation = error == BQ_OK ? bq_u32(input + 8) : 0;
    u32 schema = error == BQ_OK ? bq_u32(input + 4) : 0;
    u32 length = error == BQ_OK ? bq_u32(input + 12) : 0;
    if (error == BQ_OK && (schema != BQ_CONTROL_SCHEMA || bq_transport_public_operation(operation) != BQ_OK))
    {
        error = BQ_BAD_REQUEST;
    }
    if (error == BQ_OK && operation == BQ_OP_SUBMIT)
    {
        BqRequest request = {.size = length};
        if (schema != BQ_CONTROL_SCHEMA || length != size - BQ_CONTROL_HEADER || length > BQ_REQUEST_CAP)
        {
            error = BQ_BAD_REQUEST;
        }
        else
        {
            memcpy(request.bytes, input + BQ_CONTROL_HEADER, length);
            error = !bq_request_valid(&request) ? BQ_BAD_REQUEST : !bq_recipe_real(&request) ? BQ_UNSUPPORTED : BQ_OK;
        }
    }
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_transport_wait_for(int descriptor, short events, int milliseconds)
{
    struct pollfd waiting = {descriptor, events, 0};
    int result = poll(&waiting, 1, milliseconds);
    BqError error = result > 0 && (waiting.revents & (events | POLLERR | POLLHUP)) ? BQ_OK : BQ_IO;
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_transport_wait(int descriptor, short events)
{
    BqError error = bq_transport_wait_for(descriptor, events, BQ_TRANSPORT_IO_MILLISECONDS);
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_transport_receive_timeout(int client, u8 bytes[BQ_CONTROL_CAP], u32* size, int milliseconds)
{
    char packet[BQ_CONTROL_CAP + 1];
    BqError wait_error = bq_transport_wait_for(client, POLLIN, milliseconds);
    ssize_t received = wait_error == BQ_OK ? recv(client, packet, sizeof(packet), MSG_TRUNC | MSG_DONTWAIT) : -1;
    BqError error = wait_error != BQ_OK ? wait_error : received < 0 ? BQ_IO :
                    received > BQ_CONTROL_CAP ? BQ_BAD_REQUEST : received < BQ_CONTROL_HEADER ? BQ_BAD_REQUEST : BQ_OK;
    if (error == BQ_OK)
    {
        memcpy(bytes, packet, (size_t)received);
        *size = (u32)received;
    }
    else
    {
        *size = 0;
    }
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_transport_receive(int client, u8 bytes[BQ_CONTROL_CAP], u32* size)
{
    BqError error = bq_transport_receive_timeout(client, bytes, size, BQ_TRANSPORT_IO_MILLISECONDS);
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_transport_send(int client, u8 const* bytes, u32 size)
{
    BqError wait_error = bq_transport_wait(client, POLLOUT);
    ssize_t sent = wait_error == BQ_OK ? send(client, bytes, size, MSG_NOSIGNAL | MSG_DONTWAIT) : -1;
    BqError error = wait_error != BQ_OK ? wait_error : sent == (ssize_t)size ? BQ_OK : BQ_IO;
    return error;
}

BUSTER_GLOBAL_LOCAL bool bq_transport_response_valid(BqPacket const* request, BqPacket const* response, u32 received)
{
    bool shaped = request && response && received >= BQ_CONTROL_HEADER + 4 && received <= BQ_CONTROL_CAP &&
                  !memcmp(response->bytes, "BQP1", 4) &&
                  bq_u32(response->bytes + 12) == received - BQ_CONTROL_HEADER;
    bool request_bindable = request && request->size >= BQ_CONTROL_HEADER &&
                            !memcmp(request->bytes, "BQP1", 4) &&
                            (bq_u32(request->bytes + 4) == 1 || bq_u32(request->bytes + 4) == BQ_CONTROL_SCHEMA);
    return shaped && (!request_bindable ||
                      (bq_u32(response->bytes + 4) == bq_u32(request->bytes + 4) &&
                       bq_u32(response->bytes + 8) == (bq_u32(request->bytes + 8) | 0x80000000u) &&
                       bq_u64(response->bytes + 16) == bq_u64(request->bytes + 16)));
}

BUSTER_GLOBAL_LOCAL bool bq_transport_typed_response_valid(BqPacket const* request, BqPacket const* response)
{
    if (!request || !response || !bq_transport_response_valid(request, response, response->size))
    {
        return false;
    }
    u32 operation = bq_u32(request->bytes + 8);
    u32 body_size = response->size - BQ_CONTROL_HEADER;
    u8 const* body = response->bytes + BQ_CONTROL_HEADER;
    BqError error = (BqError)bq_u32(body);
    if (error != BQ_OK)
    {
        return body_size == 124;
    }
    if (operation == BQ_OP_CAPABILITIES)
    {
        u32 capabilities_size = (u32)sizeof(bq_capabilities_v2) - 1;
        return body_size == 4 + capabilities_size && !memcmp(body + 4, bq_capabilities_v2, capabilities_size);
    }
    if (operation == BQ_OP_LOGS)
    {
        if (body_size < 20) return false;
        u32 count = bq_u32(body + 4);
        if (count > BQ_LOG_PAGE || body_size != 20 + count * 32) return false;
        u64 requested_job = bq_u64(request->bytes + BQ_CONTROL_HEADER);
        for (u32 index = 0; index < count; index += 1)
        {
            if (bq_u64(body + 20 + index * 32 + 8) != requested_job) return false;
        }
        return true;
    }
    if (operation != BQ_OP_SUBMIT && operation != BQ_OP_STATUS && operation != BQ_OP_RESULT && operation != BQ_OP_CANCEL)
    {
        return false;
    }
    bool result_body = body_size == BQ_CONTROL_BODY;
    if (body_size != 124 && !result_body) return false;
    if (operation == BQ_OP_RESULT && !result_body) return false;
    if ((operation == BQ_OP_STATUS || operation == BQ_OP_RESULT || operation == BQ_OP_CANCEL) &&
        bq_u64(body + 4) != bq_u64(request->bytes + BQ_CONTROL_HEADER))
    {
        return false;
    }
    if (operation == BQ_OP_SUBMIT)
    {
        char8 request_digest[SHA256_HEX_CAPACITY];
        bq_digest(request->bytes + BQ_CONTROL_HEADER, request->size - BQ_CONTROL_HEADER, request_digest);
        if (!bq_u64(body + 4) || memcmp(body + 56, request_digest, SHA256_HEX_CAPACITY - 1)) return false;
    }
    if (result_body)
    {
        u32 path_length = bq_u32(body + 124);
        if (!bq_result_path_valid(body + 128, path_length) ||
            !bq_result_digest_valid(body + 320) || !bq_result_digest_valid(body + 384) ||
            !bq_result_digest_valid(body + 448))
        {
            return false;
        }
    }
    return true;
}

BUSTER_GLOBAL_LOCAL BqError bq_transport_peer_error(u8 const* input, u32 size, BqPacket* response, BqError error)
{
    u32 operation = size >= BQ_CONTROL_HEADER && !memcmp(input, "BQP1", 4) ? bq_u32(input + 8) : 0;
    u32 schema = size >= BQ_CONTROL_HEADER && !memcmp(input, "BQP1", 4) &&
                 (bq_u32(input + 4) == 1 || bq_u32(input + 4) == BQ_CONTROL_SCHEMA) ? bq_u32(input + 4) : BQ_CONTROL_SCHEMA;
    u64 correlation = size >= BQ_CONTROL_HEADER && !memcmp(input, "BQP1", 4) ? bq_u64(input + 16) : 0;
    u8 body[124] = {0};
    bq_put32(body, (u32)error);
    bq_packet_schema(response, schema, operation | 0x80000000u, correlation, body, schema == 1 ? 120 : 124);
    return error;
}

BUSTER_GLOBAL_LOCAL bool bq_transport_has_real_job(BqQueue const* queue)
{
    bool found = false;
    bool inspected = false;
    for (u32 i = 0; !inspected && i < queue->state.job_count; i += 1)
    {
        BqJob const* job = queue->state.jobs + i;
        if (job->phase == BQ_QUEUED)
        {
            inspected = true;
            found = bq_recipe_real(&job->request);
        }
    }
    return found;
}

BUSTER_GLOBAL_LOCAL bool bq_transport_queue_admissible(BqQueue const* queue)
{
    bool admissible = true;
    for (u32 i = 0; admissible && i < queue->state.job_count; i += 1)
    {
        BqJob const* job = queue->state.jobs + i;
        admissible = job->phase == BQ_FINISHED || bq_recipe_real(&job->request);
    }
    return admissible;
}

BUSTER_GLOBAL_LOCAL BqError bq_transport_worker_once(BqQueue* queue, BqWorkerConfig const* config)
{
    BqError error = BQ_NOT_FOUND;
    BqJob* active = queue->state.active_id ? bq_job(&queue->state, queue->state.active_id) : NULL;
    bool active_real = active && bq_recipe_real(&active->request);
    if (config && !bq_transport_stop_signal &&
        ((active_real && queue->needs_reconciliation) || (!queue->state.active_id && bq_transport_has_real_job(queue))))
    {
        u64 id = queue->state.active_id;
        for (u32 i = 0; !id && i < queue->state.job_count; i += 1)
        {
            BqJob* candidate = queue->state.jobs + i;
            if (candidate->phase == BQ_QUEUED && bq_recipe_real(&candidate->request))
            {
                id = candidate->id;
            }
        }
        error = bq_worker_run(queue, config, &id);
        if (bq_worker_shutdown_signal)
        {
            bq_transport_stop_signal = 1;
        }
    }
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_transport_exchange(char const* socket_path, BqPacket const* request,
                                                   BqPacket* response)
{
    BqError error = BQ_UNSUPPORTED;
    int client = -1;
    if (request && response && request->size >= BQ_CONTROL_HEADER && request->size <= BQ_CONTROL_CAP &&
        bq_transport_socket_path(socket_path, (char[BQ_PATH_CAP + 1]){0}))
    {
        client = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
        if (client >= 0)
        {
            char path[BQ_PATH_CAP + 1];
            bq_transport_socket_path(socket_path, path);
            struct sockaddr_un address = {0};
            size_t length = strlen(path);
            address.sun_family = AF_UNIX;
            memcpy(address.sun_path, path, length + 1);
            socklen_t address_size = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + length + 1);
            if (connect(client, (struct sockaddr*)&address, address_size) == 0 &&
                bq_transport_send(client, request->bytes, request->size) == BQ_OK)
            {
                u32 received = 0;
                BqError receive_error = bq_transport_receive_timeout(client, response->bytes, &received,
                                                                      BQ_TRANSPORT_CLIENT_MILLISECONDS);
                bool response_valid = receive_error == BQ_OK &&
                                      bq_transport_response_valid(request, response, received);
                if (response_valid)
                {
                    response->size = received;
                    error = (BqError)bq_u32(response->bytes + BQ_CONTROL_HEADER);
                }
                else
                {
                    response->size = 0;
                    error = receive_error == BQ_OK ? BQ_BAD_REQUEST : receive_error;
                }
            }
            else
            {
                error = BQ_IO;
            }
            close(client);
            client = -1;
        }
        else
        {
            error = BQ_IO;
        }
    }
    if (client >= 0)
    {
        close(client);
    }
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_transport_client(char const* socket_path, FILE* input, FILE* output)
{
    BqPacket request = {0}, response = {0};
    u8 bytes[BQ_CONTROL_CAP + 1];
    size_t count = fread(bytes, 1, sizeof(bytes), input);
    BqError error = ferror(input) || count < BQ_CONTROL_HEADER || count > BQ_CONTROL_CAP ? BQ_BAD_REQUEST : BQ_OK;
    if (error == BQ_OK)
    {
        request.size = (u32)count;
        memcpy(request.bytes, bytes, count);
        error = bq_transport_exchange(socket_path, &request, &response);
        if (response.size && fwrite(response.bytes, 1, response.size, output) != response.size) error = BQ_IO;
    }
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_transport_serve(char const* state_path, char const* socket_path,
                                                BqWorkerConfig const* config)
{
    BqQueue queue = {.directory_fd = -1, .lock_fd = -1, .journal_fd = -1};
    BqTransportEndpoint endpoint = {.listener = -1, .parent = -1};
    BqError error = bq_open(&queue, state_path);
    if (error == BQ_OK && !bq_transport_queue_admissible(&queue))
    {
        error = BQ_UNSUPPORTED;
    }
    if (error == BQ_OK)
    {
        error = bq_transport_endpoint_open(socket_path, &endpoint);
    }
    struct sigaction action = {0}, old_term = {0}, old_interrupt = {0};
    bool term_installed = false;
    bool interrupt_installed = false;
    if (error == BQ_OK)
    {
        action.sa_handler = bq_transport_stop_handler;
        sigemptyset(&action.sa_mask);
        bq_transport_stop_signal = 0;
        term_installed = sigaction(SIGTERM, &action, &old_term) == 0;
        interrupt_installed = term_installed && sigaction(SIGINT, &action, &old_interrupt) == 0;
        if (!interrupt_installed)
        {
            error = BQ_IO;
        }
    }
    if (error == BQ_OK)
    {
        bq_transport_worker_once(&queue, config);
        if (queue.poisoned) error = BQ_IO;
    }
    while (error == BQ_OK && !bq_transport_stop_signal)
    {
        /* Retry durable queued work on the idle tick.  worker_once is
         * synchronous: once it owns the lease, this loop does not poll or
         * process client traffic until the whole job has cleaned up. */
        bq_transport_worker_once(&queue, config);
        if (queue.poisoned)
        {
            error = BQ_IO;
            break;
        }
        struct pollfd waiting = {endpoint.listener, POLLIN, 0};
        int ready = poll(&waiting, 1, BQ_TRANSPORT_POLL_MILLISECONDS);
        if (ready < 0 && errno != EINTR)
        {
            error = BQ_IO;
        }
        else if (ready > 0 && (waiting.revents & POLLIN))
        {
            int client = accept4(endpoint.listener, NULL, NULL, SOCK_CLOEXEC);
            if (client < 0)
            {
                error = errno == EINTR ? BQ_OK : BQ_IO;
            }
            else
            {
                u8 request[BQ_CONTROL_CAP];
                u32 request_size = 0;
                BqPacket response = {0};
                BqError request_error = bq_transport_receive(client, request, &request_size);
                if (request_error == BQ_OK && !bq_transport_peer_allowed(client))
                {
                    request_error = BQ_BAD_REQUEST;
                }
                if (request_error == BQ_OK)
                {
                    request_error = bq_transport_public_request(request, request_size);
                }
                if (request_error == BQ_OK)
                {
                    BqError dispatch_error = bq_dispatch(&queue, request, request_size, &response);
                    if (dispatch_error == BQ_IO && queue.poisoned)
                    {
                        error = BQ_IO;
                    }
                }
                else
                {
                    bq_transport_peer_error(request, request_size, &response, request_error);
                }
                bq_transport_send(client, response.bytes, response.size);
                close(client);
                if (error == BQ_OK)
                {
                    bq_transport_worker_once(&queue, config);
                    if (queue.poisoned)
                    {
                        error = BQ_IO;
                    }
                }
            }
        }
    }
    if (interrupt_installed && sigaction(SIGINT, &old_interrupt, NULL) != 0)
    {
        error = BQ_IO;
    }
    if (term_installed && sigaction(SIGTERM, &old_term, NULL) != 0)
    {
        error = BQ_IO;
    }
    if (queue.directory_fd >= 0)
    {
        BqError cleanup = bq_transport_endpoint_close(&endpoint);
        if (error == BQ_OK && cleanup != BQ_OK) error = cleanup;
    }
    bq_close(&queue);
    return error;
}

#else

BqError bq_transport_client(char const* socket_path, FILE* input, FILE* output)
{
    (void)socket_path;
    (void)input;
    (void)output;
    return BQ_UNSUPPORTED;
}

BqError bq_transport_serve(char const* state_path, char const* socket_path, BqWorkerConfig const* config)
{
    (void)state_path;
    (void)socket_path;
    (void)config;
    return BQ_UNSUPPORTED;
}

#endif
