/* Local one-shot CLI. No shell commands, remote execution, timers or listeners.
 * Tests include this entry point, as the existing throughput tests do.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS 1
#endif
#include "queue.c"
#include "protocol.c"
#include <inttypes.h>

BUSTER_GLOBAL_LOCAL bool bq_decimal(char const* text, bool nonzero, u64* value)
{
    String8 input = string_from_pointer(text);
    IntegerParsingU64 parsed = string8_parse_u64_decimal(input);
    bool ok = parsed.status == INTEGER_PARSING_SUCCESS && parsed.length == input.length && (!nonzero || parsed.value != 0);
    *value = ok ? parsed.value : 0;
    return ok;
}

BUSTER_GLOBAL_LOCAL char const* bq_phase_name(u32 phase)
{
    char const* names[] = {"queued", "reserved", "preparing", "settling", "measuring", "finalizing", "cleaning", "finished"};
    char const* result = phase < sizeof(names) / sizeof(names[0]) ? names[phase] : "unknown";
    return result;
}

BUSTER_GLOBAL_LOCAL char const* bq_outcome_name(u32 outcome)
{
    char const* names[] = {"none", "succeeded", "failed", "cancelled", "interrupted"};
    char const* result = outcome < sizeof(names) / sizeof(names[0]) ? names[outcome] : "unknown";
    return result;
}

BUSTER_GLOBAL_LOCAL int bq_cli(int argc, char** argv, FILE* input, FILE* output, FILE* diagnostics)
{
    BqPacket request = {0};
    BqPacket response = {0};
    BqQueue queue = {.directory_fd = -1, .lock_fd = -1, .journal_fd = -1};
    BqError error = BQ_BAD_REQUEST;
    u32 operation = 0;
    u8 body[BQ_REQUEST_CAP] = {0};
    u32 body_size = 0;
    bool raw = false;
    bool valid = false;
    u64 id = 0;
    u64 argument = 0;
    if (argc == 2 && !strcmp(argv[1], "capabilities"))
    {
        operation = BQ_OP_CAPABILITIES;
        valid = true;
    }
    else if (argc >= 3)
    {
        if (argc == 8 && !strcmp(argv[1], "submit"))
        {
            BqRequest submission;
            String8 fields[BQ_FIELD_COUNT];
            for (u32 i = 0; i < BQ_FIELD_COUNT; i += 1)
            {
                fields[i] = string_from_pointer(argv[3 + i]);
            }
            valid = bq_request_make(fields, &submission) == BQ_OK;
            operation = BQ_OP_SUBMIT;
            if (valid)
            {
                body_size = submission.size;
                memcpy(body, submission.bytes, body_size);
            }
        }
        else if (argc == 4 && (!strcmp(argv[1], "status") || !strcmp(argv[1], "result") || !strcmp(argv[1], "cancel")))
        {
            operation = !strcmp(argv[1], "status") ? BQ_OP_STATUS : !strcmp(argv[1], "result") ? BQ_OP_RESULT : BQ_OP_CANCEL;
            valid = bq_decimal(argv[3], true, &id);
            bq_put64(body, id);
            body_size = 8;
        }
        else if ((argc == 4 || argc == 5) && !strcmp(argv[1], "logs"))
        {
            operation = BQ_OP_LOGS;
            valid = bq_decimal(argv[3], true, &id) && (argc == 4 || bq_decimal(argv[4], false, &argument));
            bq_put64(body, id);
            bq_put64(body + 8, argument);
            body_size = 16;
        }
        else if (argc == 5 && !strcmp(argv[1], "fake-reconcile"))
        {
            operation = BQ_OP_FAKE_RECONCILE;
            valid = bq_decimal(argv[3], true, &id) && bq_decimal(argv[4], true, &argument);
            bq_put64(body, id);
            bq_put64(body + 8, argument);
            body_size = 16;
        }
        else if (argc == 3 && !strcmp(argv[1], "fake-run"))
        {
            operation = BQ_OP_FAKE_RUN;
            valid = true;
        }
        else if (argc == 3 && !strcmp(argv[1], "protocol"))
        {
            raw = true;
            valid = true;
        }
    }
    if (valid)
    {
        error = operation == BQ_OP_CAPABILITIES ? BQ_OK : bq_open(&queue, argv[2]);
        if (error == BQ_OK)
        {
            if (raw)
            {
                /* One frame plus EOF. One extra byte detects oversize/trailing
                 * input without an allocation based on untrusted lengths. */
                u8 bytes[BQ_CONTROL_CAP + 1];
                size_t count = fread(bytes, 1, sizeof(bytes), input);
                error = ferror(input) ? BQ_IO : bq_dispatch(&queue, bytes, (u32)count, &response);
            }
            else
            {
                bq_packet(&request, operation, 1, body, body_size);
                error = bq_dispatch(&queue, request.bytes, request.size, &response);
            }
        }
    }
    bool written = true;
    if (raw && response.size)
    {
        written = fwrite(response.bytes, 1, response.size, output) == response.size;
    }
    else if (error == BQ_OK)
    {
        u8 const* data = response.bytes + BQ_CONTROL_HEADER;
        if (operation == BQ_OP_CAPABILITIES)
        {
            written = fwrite(data + 4, 1, response.size - BQ_CONTROL_HEADER - 4, output) == response.size - BQ_CONTROL_HEADER - 4;
        }
        else if (operation == BQ_OP_LOGS)
        {
            for (u32 i = 0; written && i < bq_u32(data + 4); i += 1)
            {
                u8 const* event = data + 20 + i * 32;
                written = fprintf(output, "sequence=%" PRIu64 " job=%" PRIu64 " event=%u phase=%s outcome=%s validity=not-evaluated\n",
                                  (uint64_t)bq_u64(event), (uint64_t)bq_u64(event + 8), bq_u32(event + 16),
                                  bq_phase_name(bq_u32(event + 20)), bq_outcome_name(bq_u32(event + 24))) >= 0;
            }
            if (written)
            {
                written = fprintf(output, "next=%" PRIu64 " more=%u\n", (uint64_t)bq_u64(data + 8), bq_u32(data + 16)) >= 0;
            }
        }
        else
        {
            written = fprintf(output, "job=%" PRIu64 " token=%" PRIu64 " sequence=%" PRIu64
                              " phase=%s outcome=%s validity=not-evaluated cancel-requested=%u reconciliation=%u"
                              " pending=%u retained=%u request-sha256=%.64s\n",
                              (uint64_t)bq_u64(data + 4), (uint64_t)bq_u64(data + 12), (uint64_t)bq_u64(data + 20),
                              bq_phase_name(bq_u32(data + 28)), bq_outcome_name(bq_u32(data + 32)), bq_u32(data + 40),
                              bq_u32(data + 44), bq_u32(data + 48), bq_u32(data + 52), (char const*)data + 56) >= 0;
        }
    }
    if (fflush(output) != 0)
    {
        written = false;
    }
    if (!written)
    {
        error = BQ_IO;
    }
    if (error != BQ_OK)
    {
        fprintf(diagnostics, "bench_service: %s; io-uncertain requires retry/reopen, never rollback\n", bq_error_name(error));
        if (!valid)
        {
            fprintf(diagnostics, "commands: capabilities | submit DIR PRINCIPAL KEY RECIPE BASE_SHA CANDIDATE_SHA | "
                    "status/result/cancel DIR JOB | logs DIR JOB [AFTER_SEQUENCE] | fake-run DIR | "
                    "fake-reconcile DIR JOB TOKEN | protocol DIR\n");
        }
    }
    bq_close(&queue);
    int result = error == BQ_OK ? 0 : 1;
    return result;
}

int main(int argc, char** argv)
{
    int result = bq_cli(argc, argv, stdin, stdout, stderr);
    return result;
}
