/* Versioned local control codec, not a network server or authentication layer.
 * Requests and ordinary replies are capped at 536 bytes, independent of queue
 * size. Authenticated export replies alone allow one fixed 64 KiB chunk.
 * Both human CLI commands and raw protocol requests enter bq_dispatch.
 * bq_public_response_valid checks successful typed replies before rendering.
 */
#include "queue.h"
#define BQ_CONTROL_HEADER 24u
#define BQ_CONTROL_BODY 512u
#define BQ_CONTROL_CAP (BQ_CONTROL_HEADER + BQ_CONTROL_BODY)
#define BQ_LOG_PAGE 4u
#define BQ_PACKET_CAP (BQ_CONTROL_HEADER + BQ_EXPORT_BODY_CAP)

BUSTER_GLOBAL_LOCAL BqWorkerQuarantine bq_worker_quarantine = {.descriptor = -1};

typedef enum BqOperation
{
    BQ_OP_CAPABILITIES = 1, BQ_OP_SUBMIT, BQ_OP_STATUS, BQ_OP_RESULT,
    BQ_OP_CANCEL, BQ_OP_LOGS, BQ_OP_FAKE_RUN, BQ_OP_FAKE_RECONCILE,
    BQ_OP_MATERIALIZE, BQ_OP_WORKSPACE_RECONCILE, BQ_OP_WORKER_RUN,
    BQ_OP_SUBMIT_EXCLUSIVE, BQ_OP_EXPORT
} BqOperation;

typedef struct BqPacket
{
    u32 size;
    u8 bytes[BQ_PACKET_CAP];
} BqPacket;

BUSTER_GLOBAL_LOCAL char const bq_capabilities_v1[] =
    "schema=1 executor=fake-only repository=buster pending=8 lifetime-jobs=64\n"
    "recipes=fake-success-v1,fake-failure-v1 workload=fake-steps-v1\n"
    "profile=unmeasured toolchain=none oracle=fake-v1 validity=not-evaluated\n"
    "retention=journal-lifetime transport=none authentication=none\n"
#ifdef _WIN32
    "storage=unsupported-on-windows\n";
#else
    "storage=private-local-posix-directory\n";
#endif

BUSTER_GLOBAL_LOCAL char const bq_capabilities_v2[] =
    "schema=2 journal=3 materialization-journal=2 legacy-journal=1 executor=supervisor pending=8 jobs=64\n"
    "local-recipes=fake-success-v1,fake-failure-v1 service-recipes=validate-buster-v1 "
    "blocked-recipes=native-retirement-performance-v1\n"
    "profile=smoke validity=not-evaluated materialization=read-only workspace=per-attempt\n"
    "worker=fixed-systemd-service dispatch=fixed-registry admission=idle-only-atomic "
    "retirement=blocked export=1 "
#ifdef __linux__
    "transport=unix-seqpacket authentication=peer-uid-gid\n"
#else
    "transport=unsupported authentication=none\n"
#endif
#ifdef _WIN32
    "storage=unsupported-on-windows\n";
#else
    "storage=private-local-posix-directory\n";
#endif

BUSTER_GLOBAL_LOCAL void bq_packet_schema(BqPacket* packet, u32 schema, u32 operation, u64 correlation, u8 const* body, u32 size)
{
    *packet = (BqPacket){0};
    if (size <= BQ_CONTROL_BODY || (operation == (BQ_OP_EXPORT | 0x80000000u) && size <= BQ_EXPORT_BODY_CAP))
    {
        packet->size = BQ_CONTROL_HEADER + size;
        memcpy(packet->bytes, "BQP1", 4);
        bq_put32(packet->bytes + 4, schema);
        bq_put32(packet->bytes + 8, operation);
        bq_put32(packet->bytes + 12, size);
        bq_put64(packet->bytes + 16, correlation);
        if (size)
        {
            memcpy(packet->bytes + BQ_CONTROL_HEADER, body, size);
        }
    }
}

BUSTER_GLOBAL_LOCAL void bq_packet(BqPacket* packet, u32 operation, u64 correlation, u8 const* body, u32 size)
{
    bq_packet_schema(packet, BQ_CONTROL_SCHEMA, operation, correlation, body, size);
}

BUSTER_GLOBAL_LOCAL bool bq_public_response_valid(BqPacket const* request, BqPacket const* response)
{
    bool valid = request && response && request->size >= BQ_CONTROL_HEADER && request->size <= BQ_CONTROL_CAP &&
                 response->size >= BQ_CONTROL_HEADER + 4 && response->size <= BQ_PACKET_CAP;
    if (valid)
    {
        valid = !memcmp(request->bytes, "BQP1", 4) && !memcmp(response->bytes, "BQP1", 4) &&
                bq_u32(request->bytes + 4) == BQ_CONTROL_SCHEMA && bq_u32(response->bytes + 4) == BQ_CONTROL_SCHEMA &&
                bq_u32(request->bytes + 12) == request->size - BQ_CONTROL_HEADER &&
                bq_u32(response->bytes + 12) == response->size - BQ_CONTROL_HEADER &&
                bq_u32(response->bytes + 8) == (bq_u32(request->bytes + 8) | 0x80000000u) &&
                bq_u64(response->bytes + 16) == bq_u64(request->bytes + 16) &&
                bq_u32(response->bytes + BQ_CONTROL_HEADER) == BQ_OK;
    }
    if (valid)
    {
        u32 operation = bq_u32(request->bytes + 8);
        u32 length = response->size - BQ_CONTROL_HEADER;
        u8 const* data = response->bytes + BQ_CONTROL_HEADER;
        u8 const* arguments = request->bytes + BQ_CONTROL_HEADER;
        if (operation == BQ_OP_EXPORT)
        {
            valid = request->size == BQ_CONTROL_HEADER + BQ_EXPORT_REQUEST_CAP && length >= BQ_EXPORT_REPLY_HEADER;
            if (valid)
            {
                u64 cursor = bq_u64(arguments + 80), next = bq_u64(data + 28), total = bq_u64(data + 36);
                u32 count = bq_u32(data + 44);
                valid = bq_u64(data + 4) == bq_u64(arguments) && bq_u64(data + 12) == bq_u64(arguments + 8) &&
                        bq_u64(data + 20) == cursor && total && total <= BQ_EXPORT_TOTAL_CAP &&
                        bq_result_digest_valid(data + 48) && length == BQ_EXPORT_REPLY_HEADER + count;
                if (valid && cursor == UINT64_MAX)
                {
                    valid = count == BQ_EXPORT_RECEIPT_CAP && !next;
#ifdef __linux__
                    char digest[SHA256_HEX_CAPACITY];
                    if (valid) bq_digest(data + BQ_EXPORT_REPLY_HEADER, count, digest);
                    valid = valid && bq_export_receipt_valid(data + BQ_EXPORT_REPLY_HEADER) &&
                            !memcmp(digest, data + 48, 64) &&
                            !memcmp(arguments + 16, data + BQ_EXPORT_REPLY_HEADER + 240, 64) &&
                            bq_u64(data + BQ_EXPORT_REPLY_HEADER + 8) == bq_u64(arguments) &&
                            bq_u64(data + BQ_EXPORT_REPLY_HEADER + 16) == bq_u64(arguments + 8) &&
                            bq_u64(data + BQ_EXPORT_REPLY_HEADER + 24) == total;
#endif
                }
                else if (valid)
                {
                    valid = cursor < total && cursor % BQ_EXPORT_CHUNK_CAP == 0 && count > 0 &&
                            count == (total - cursor < BQ_EXPORT_CHUNK_CAP ? total - cursor : BQ_EXPORT_CHUNK_CAP) &&
                            next == cursor + count && !memcmp(arguments + 88, data + 48, 64);
                }
            }
        }
        else if (response->size > BQ_CONTROL_CAP)
        {
            valid = false;
        }
        else if (operation == BQ_OP_CAPABILITIES)
        {
            valid = request->size == BQ_CONTROL_HEADER && length > 4;
            for (u32 i = 4; valid && i < length; i += 1)
                valid = data[i] == '\n' || (data[i] >= 0x20 && data[i] <= 0x7e);
        }
        else if (operation == BQ_OP_LOGS)
        {
            valid = request->size == BQ_CONTROL_HEADER + 16 && length >= 20;
            if (valid)
            {
                u32 count = bq_u32(data + 4);
                u64 next = bq_u64(arguments + 8);
                valid = count <= BQ_LOG_PAGE && length == 20 + count * 32 && bq_u32(data + 16) <= 1 &&
                        (!bq_u32(data + 16) || count == BQ_LOG_PAGE);
                for (u32 i = 0; valid && i < count; i += 1)
                {
                    u8 const* event = data + 20 + i * 32;
                    valid = bq_u64(event) > next && bq_u64(event + 8) == bq_u64(arguments) &&
                            bq_u32(event + 16) >= BQ_SUBMIT && bq_u32(event + 16) <= BQ_RESULT_BIND &&
                            bq_u32(event + 20) <= BQ_FINISHED && bq_u32(event + 24) <= BQ_INTERRUPTED &&
                            bq_u32(event + 28) == BQ_NOT_EVALUATED;
                    next = bq_u64(event);
                }
                valid = valid && bq_u64(data + 8) == next;
            }
        }
        else if ((operation >= BQ_OP_SUBMIT && operation <= BQ_OP_CANCEL) || operation == BQ_OP_SUBMIT_EXCLUSIVE)
        {
            bool submission = operation == BQ_OP_SUBMIT || operation == BQ_OP_SUBMIT_EXCLUSIVE;
            valid = (length == 124 || length == BQ_CONTROL_BODY) &&
                    (submission || request->size == BQ_CONTROL_HEADER + 8);
            if (valid)
            {
                valid = bq_u64(data + 4) != 0 &&
                        (submission || bq_u64(data + 4) == bq_u64(arguments)) &&
                        bq_u32(data + 28) <= BQ_FINISHED && bq_u32(data + 32) <= BQ_INTERRUPTED &&
                        bq_u32(data + 36) == BQ_NOT_EVALUATED && bq_u32(data + 40) <= 1 &&
                        bq_u32(data + 44) <= 1 && bq_u32(data + 48) <= BQ_PENDING_CAP &&
                        bq_u32(data + 52) <= BQ_JOB_CAP && bq_u32(data + 48) <= bq_u32(data + 52) &&
                        bq_result_digest_valid(data + 56) && bq_u32(data + 120) <= BQ_WORKER_CANCEL_SIGNAL;
                if (valid && length == BQ_CONTROL_BODY)
                {
                    u32 root_length = bq_u32(data + 124);
                    valid = (operation == BQ_OP_STATUS || operation == BQ_OP_RESULT) && bq_u64(data + 12) != 0 &&
                            bq_result_path_valid(data + 128, root_length) &&
                            bq_result_digest_valid(data + 320) && bq_result_digest_valid(data + 384) &&
                            bq_result_digest_valid(data + 448);
                    for (u32 i = root_length; valid && i < BQ_PATH_CAP; i += 1)
                        valid = data[128 + i] == 0;
                }
            }
        }
        else
        {
            valid = false;
        }
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL BqError bq_dispatch(BqQueue* queue, u8 const* input, u32 size, BqPacket* response)
{
    u32 operation = 0;
    u32 schema = BQ_CONTROL_SCHEMA;
    u64 correlation = 0;
    u64 id = 0;
    u8 output[BQ_CONTROL_BODY] = {0};
    u32 output_size = 124;
    BqError error = BQ_BAD_REQUEST;
    if (size >= BQ_CONTROL_HEADER && size <= BQ_CONTROL_CAP && !memcmp(input, "BQP1", 4) &&
        (bq_u32(input + 4) == 1 || bq_u32(input + 4) == BQ_CONTROL_SCHEMA) && bq_u32(input + 12) == size - BQ_CONTROL_HEADER)
    {
        schema = bq_u32(input + 4);
        output_size = schema == 1 ? 120 : 124;
        operation = bq_u32(input + 8);
        correlation = bq_u64(input + 16);
        u32 length = size - BQ_CONTROL_HEADER;
        u8 const* body = input + BQ_CONTROL_HEADER;
        if (operation == BQ_OP_CAPABILITIES && !length)
        {
            char const* capabilities = schema == 1 ? bq_capabilities_v1 : bq_capabilities_v2;
            u32 capabilities_size = schema == 1 ? (u32)sizeof(bq_capabilities_v1) - 1 : (u32)sizeof(bq_capabilities_v2) - 1;
            if (capabilities_size <= BQ_CONTROL_BODY - 4)
            {
                error = BQ_OK;
                output_size = 4 + capabilities_size;
                memcpy(output + 4, capabilities, capabilities_size);
            }
        }
        else if (queue->poisoned || queue->journal_fd < 0)
        {
            error = BQ_IO;
        }
        else if ((operation == BQ_OP_SUBMIT || operation == BQ_OP_SUBMIT_EXCLUSIVE) && length <= BQ_REQUEST_CAP)
        {
            BqRequest request = {.size = length};
            memcpy(request.bytes, body, length);
            if (schema == 1 && bq_recipe_real(&request))
            {
                error = BQ_BAD_REQUEST;
            }
            else
            {
                error = operation == BQ_OP_SUBMIT_EXCLUSIVE ? bq_submit_exclusive(queue, &request, &id) :
                                                              bq_submit(queue, &request, &id);
            }
        }
        else if ((operation == BQ_OP_STATUS || operation == BQ_OP_RESULT || operation == BQ_OP_CANCEL) && length == 8)
        {
            id = bq_u64(body);
            BqJob* job = bq_job(&queue->state, id);
            error = !job ? BQ_NOT_FOUND : schema == 1 && bq_recipe_real(&job->request) ? BQ_UNSUPPORTED : BQ_OK;
            if (error == BQ_OK && job && job->result_bound && (operation == BQ_OP_STATUS || operation == BQ_OP_RESULT))
            {
                error = bq_worker_result_binding_validate(job);
                if (error == BQ_OK && schema == BQ_CONTROL_SCHEMA) output_size = BQ_CONTROL_BODY;
            }
            if (error == BQ_OK && operation == BQ_OP_CANCEL)
            {
                error = bq_cancel(queue, id);
            }
        }
        else if (operation == BQ_OP_FAKE_RUN && !length)
        {
            error = bq_fake_run(queue, &id);
        }
        else if (operation == BQ_OP_FAKE_RECONCILE && length == 16)
        {
            id = bq_u64(body);
            error = bq_fake_reconcile(queue, id, bq_u64(body + 8));
        }
        else if (schema == BQ_CONTROL_SCHEMA && operation == BQ_OP_MATERIALIZE && length >= 8)
        {
            u32 installed_length = bq_u32(body);
            u32 workspace_length = bq_u32(body + 4);
            if (installed_length <= BQ_PATH_CAP && workspace_length <= BQ_PATH_CAP &&
                installed_length + workspace_length == length - 8)
            {
                String8 installed = {(char8*)body + 8, installed_length};
                String8 workspace = {(char8*)body + 8 + installed_length, workspace_length};
                u64 token = 0;
                error = bq_materialize(queue, installed, workspace, &id, &token);
            }
        }
        else if (schema == BQ_CONTROL_SCHEMA && operation == BQ_OP_WORKSPACE_RECONCILE && length >= 20)
        {
            id = bq_u64(body);
            u64 token = bq_u64(body + 8);
            u32 workspace_length = bq_u32(body + 16);
            if (workspace_length <= BQ_PATH_CAP && workspace_length == length - 20)
            {
                String8 workspace = {(char8*)body + 20, workspace_length};
                error = bq_workspace_reconcile(queue, workspace, id, token);
            }
        }
        else if (schema == BQ_CONTROL_SCHEMA && operation == BQ_OP_WORKER_RUN && length >= 16)
        {
            u32 installed_length = bq_u32(body);
            u32 workspace_length = bq_u32(body + 4);
            u32 lease_length = bq_u32(body + 8);
            u32 cpu = bq_u32(body + 12);
            if (installed_length <= BQ_PATH_CAP && workspace_length <= BQ_PATH_CAP && lease_length <= BQ_PATH_CAP &&
                installed_length + workspace_length + lease_length == length - 16)
            {
                BqWorkerConfig config = {
                    .installed_root = {(char8*)body + 16, installed_length},
                    .workspace_root = {(char8*)body + 16 + installed_length, workspace_length},
                    .lease_file = {(char8*)body + 16 + installed_length + workspace_length, lease_length},
                    .boot_id_file = S8("/proc/sys/kernel/random/boot_id"),
                    .cgroup_root = S8("/sys/fs/cgroup"),
                    .limits = {cpu, 8ull * 1024 * 1024 * 1024, 0, 256, 60ull * 60 * 1000000},
                    .quarantine = &bq_worker_quarantine,
                    .queue_root = string_from_pointer(queue->directory_path),
                    .production_path = true
                };
                error = bq_worker_run(queue, &config, &id);
            }
        }
        else if (operation == BQ_OP_LOGS && length == 16)
        {
            id = bq_u64(body);
            u64 after = bq_u64(body + 8);
            BqJob* job = bq_job(&queue->state, id);
            error = !job ? BQ_NOT_FOUND : schema == 1 && bq_recipe_real(&job->request) ? BQ_UNSUPPORTED :
                    after > queue->state.sequence ? BQ_BAD_REQUEST : BQ_OK;
            if (error == BQ_OK)
            {
                u32 count = 0;
                u64 next = after;
                bool more = false;
                for (u32 i = 0; i < queue->state.event_count; i += 1)
                {
                    BqEvent const* event = queue->state.events + i;
                    if (event->job_id == id && event->sequence > after)
                    {
                        if (count == BQ_LOG_PAGE)
                        {
                            more = true;
                        }
                        else
                        {
                            u8* record = output + 20 + count * 32;
                            bq_put64(record, event->sequence);
                            bq_put64(record + 8, event->job_id);
                            bq_put32(record + 16, (u32)event->kind);
                            bq_put32(record + 20, (u32)event->phase);
                            bq_put32(record + 24, (u32)event->outcome);
                            bq_put32(record + 28, BQ_NOT_EVALUATED);
                            next = event->sequence;
                            count += 1;
                        }
                    }
                }
                bq_put32(output + 4, count);
                bq_put64(output + 8, next);
                bq_put32(output + 16, more);
                output_size = 20 + count * 32;
            }
        }
    }
    if (output_size == 120 || output_size == 124 || output_size == BQ_CONTROL_BODY)
    {
        BqJob const* job = bq_job(&queue->state, id);
        bq_put64(output + 4, job ? job->id : 0);
        bq_put64(output + 12, job ? job->token : 0);
        bq_put64(output + 20, queue->state.sequence);
        bq_put32(output + 28, job ? (u32)job->phase : 0);
        bq_put32(output + 32, job ? (u32)job->outcome : 0);
        bq_put32(output + 36, job ? (u32)job->validity : BQ_NOT_EVALUATED);
        bq_put32(output + 40, job && job->cancel_requested);
        bq_put32(output + 44, queue->needs_reconciliation);
        bq_put32(output + 48, bq_pending(&queue->state));
        bq_put32(output + 52, queue->state.job_count);
        if (job)
        {
            memcpy(output + 56, job->digest, 64);
        }
        if (output_size == 124 || output_size == BQ_CONTROL_BODY)
        {
            BqError failure = job ? bq_failure_evidence(queue, job) : BQ_NOT_FOUND;
            bq_put32(output + 120, failure != BQ_NOT_FOUND && failure != BQ_UNSUPPORTED ? (u32)failure : 0);
            if (error == BQ_OK && (operation == BQ_OP_STATUS || operation == BQ_OP_RESULT) &&
                (failure == BQ_CORRUPT || failure == BQ_IO))
            {
                error = failure;
            }
        }
        if (output_size == BQ_CONTROL_BODY && job && job->result_bound)
        {
            bq_put32(output + 124, (u32)strlen(job->result_root));
            memcpy(output + 128, job->result_root, strlen(job->result_root));
            memcpy(output + 320, job->result_manifest_digest, 64);
            memcpy(output + 384, job->result_bundle_digest, 64);
            memcpy(output + 448, job->result_full_digest, 64);
        }
    }
    bq_put32(output, (u32)error);
    bq_packet_schema(response, schema, operation | 0x80000000u, correlation, output, output_size);
    return error;
}
