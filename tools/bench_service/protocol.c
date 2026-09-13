/* Versioned local control codec, not a network server or authentication layer.
 * Requests and responses are capped at 536 bytes, independent of queue size.
 * Both human CLI commands and raw protocol requests enter bq_dispatch.
 */
#include "queue.h"
#define BQ_CONTROL_HEADER 24u
#define BQ_CONTROL_BODY 512u
#define BQ_CONTROL_CAP (BQ_CONTROL_HEADER + BQ_CONTROL_BODY)
#define BQ_LOG_PAGE 4u

typedef enum BqOperation
{
    BQ_OP_CAPABILITIES = 1, BQ_OP_SUBMIT, BQ_OP_STATUS, BQ_OP_RESULT,
    BQ_OP_CANCEL, BQ_OP_LOGS, BQ_OP_FAKE_RUN, BQ_OP_FAKE_RECONCILE
} BqOperation;

typedef struct BqPacket
{
    u32 size;
    u8 bytes[BQ_CONTROL_CAP];
} BqPacket;

BUSTER_GLOBAL_LOCAL char const bq_capabilities[] =
    "schema=1 executor=fake-only repository=buster pending=8 lifetime-jobs=64\n"
    "recipes=fake-success-v1,fake-failure-v1 workload=fake-steps-v1\n"
    "profile=unmeasured toolchain=none oracle=fake-v1 validity=not-evaluated\n"
    "retention=journal-lifetime transport=none authentication=none\n"
#ifdef _WIN32
    "storage=unsupported-on-windows\n";
#else
    "storage=private-local-posix-directory\n";
#endif

BUSTER_GLOBAL_LOCAL void bq_packet(BqPacket* packet, u32 operation, u64 correlation, u8 const* body, u32 size)
{
    *packet = (BqPacket){0};
    if (size <= BQ_CONTROL_BODY)
    {
        packet->size = BQ_CONTROL_HEADER + size;
        memcpy(packet->bytes, "BQP1", 4);
        bq_put32(packet->bytes + 4, BQ_SCHEMA);
        bq_put32(packet->bytes + 8, operation);
        bq_put32(packet->bytes + 12, size);
        bq_put64(packet->bytes + 16, correlation);
        if (size)
        {
            memcpy(packet->bytes + BQ_CONTROL_HEADER, body, size);
        }
    }
}

BUSTER_GLOBAL_LOCAL BqError bq_dispatch(BqQueue* queue, u8 const* input, u32 size, BqPacket* response)
{
    u32 operation = 0;
    u64 correlation = 0;
    u64 id = 0;
    u8 output[BQ_CONTROL_BODY] = {0};
    u32 output_size = 120;
    BqError error = BQ_BAD_REQUEST;
    if (size >= BQ_CONTROL_HEADER && size <= BQ_CONTROL_CAP && !memcmp(input, "BQP1", 4) &&
        bq_u32(input + 4) == BQ_SCHEMA && bq_u32(input + 12) == size - BQ_CONTROL_HEADER)
    {
        operation = bq_u32(input + 8);
        correlation = bq_u64(input + 16);
        u32 length = size - BQ_CONTROL_HEADER;
        u8 const* body = input + BQ_CONTROL_HEADER;
        if (operation == BQ_OP_CAPABILITIES && !length)
        {
            error = BQ_OK;
            output_size = 4 + (u32)sizeof(bq_capabilities) - 1;
            memcpy(output + 4, bq_capabilities, sizeof(bq_capabilities) - 1);
        }
        else if (queue->poisoned || queue->journal_fd < 0)
        {
            error = BQ_IO;
        }
        else if (operation == BQ_OP_SUBMIT && length <= BQ_REQUEST_CAP)
        {
            BqRequest request = {.size = length};
            memcpy(request.bytes, body, length);
            error = bq_submit(queue, &request, &id);
        }
        else if ((operation == BQ_OP_STATUS || operation == BQ_OP_RESULT || operation == BQ_OP_CANCEL) && length == 8)
        {
            id = bq_u64(body);
            error = bq_job(&queue->state, id) ? BQ_OK : BQ_NOT_FOUND;
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
        else if (operation == BQ_OP_LOGS && length == 16)
        {
            id = bq_u64(body);
            u64 after = bq_u64(body + 8);
            error = !bq_job(&queue->state, id) ? BQ_NOT_FOUND : after > queue->state.sequence ? BQ_BAD_REQUEST : BQ_OK;
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
    if (output_size == 120)
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
    }
    bq_put32(output, (u32)error);
    bq_packet(response, operation | 0x80000000u, correlation, output, output_size);
    return error;
}
