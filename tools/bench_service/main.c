/* Service and control entry points. No caller-selected commands or shell execution.
 * bq_client_arguments owns typed requests; gateway fixes socket/principal/recipe.
 * bq_cli owns dispatch and diagnostics; bq_response_write prints bounded receipts.
 * Tests include this entry point, as the existing throughput tests do.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS 1
#endif
#include "queue.c"
#include "exclusive_admission.c"
#include "workspace.c"
#include "worker_linux.c"
#include "export.c"
#include "protocol.c"
#include "transport.c"
#include "export_client.c"
#include <inttypes.h>
#include <limits.h>

BUSTER_GLOBAL_LOCAL bool bq_decimal(char const* text, bool nonzero, u64* value)
{
    String8 input = string_from_pointer(text);
    IntegerParsingU64 parsed = string8_parse_u64_decimal(input);
    bool ok = parsed.status == INTEGER_PARSING_SUCCESS && parsed.length == input.length && (!nonzero || parsed.value != 0);
    *value = ok ? parsed.value : 0;
    return ok;
}

/* The gateway is a fixed request encoder, not a privilege transition. The
 * installed executable still needs the service's authenticated UID/GID. */
#define BQ_GATEWAY_SOCKET "/run/buster-bench/control.sock"

BUSTER_GLOBAL_LOCAL bool bq_client_arguments(int argc, char** argv, bool gateway, BqPacket* request, u32* operation)
{
    u8 body[BQ_CONTROL_BODY] = {0};
    u32 size = 0;
    u64 id = 0, after = 0;
    bool valid = false;
    *request = (BqPacket){0};
    *operation = 0;
    if (argc == 1 && !strcmp(argv[0], "capabilities"))
    {
        *operation = BQ_OP_CAPABILITIES;
        valid = true;
    }
    else if (argc == (gateway ? 4 : 6) && !strcmp(argv[0], "submit"))
    {
        BqRequest submission;
        String8 fields[BQ_FIELD_COUNT];
        if (gateway)
        {
            fields[0] = S8("github-actions");
            fields[1] = string_from_pointer(argv[1]);
            fields[2] = S8("validate-buster-v1");
            fields[3] = string_from_pointer(argv[2]);
            fields[4] = string_from_pointer(argv[3]);
        }
        else
        {
            for (u32 i = 0; i < BQ_FIELD_COUNT; i += 1)
                fields[i] = string_from_pointer(argv[1 + i]);
        }
        valid = bq_request_make(fields, &submission) == BQ_OK && bq_recipe_service(bq_request_recipe(&submission));
        *operation = BQ_OP_SUBMIT;
        if (valid)
        {
            size = submission.size;
            memcpy(body, submission.bytes, size);
        }
    }
    else if ((argc == 4 || argc == 5 || argc == 6) &&
             ((!strcmp(argv[0], "export") && (argc == 4 || argc == 5)) ||
              (!strcmp(argv[0], "export-chunk") && argc == 6)))
    {
        u64 token = 0, cursor = UINT64_MAX;
        BqRecipe expected_recipe = argc == 5 ? bq_recipe_from_name(string_from_pointer(argv[4])) : BQ_RECIPE_UNKNOWN;
        valid = bq_decimal(argv[1], true, &id) && bq_decimal(argv[2], true, &token) && strlen(argv[3]) == 64 &&
                bq_result_digest_valid((u8 const*)argv[3]) &&
                (argc != 5 || expected_recipe == BQ_RECIPE_VALIDATE_BUSTER ||
                 expected_recipe == BQ_RECIPE_NATIVE_RETIREMENT_BLOCKED);
        if (valid && argc == 6) valid = bq_decimal(argv[4], false, &cursor) && cursor != UINT64_MAX &&
                                     cursor % BQ_EXPORT_CHUNK_CAP == 0 && strlen(argv[5]) == 64 &&
                                     bq_result_digest_valid((u8 const*)argv[5]);
        *operation = BQ_OP_EXPORT;
        if (valid)
        {
            bq_put64(body, id);
            bq_put64(body + 8, token);
            memcpy(body + 16, argv[3], 64);
            bq_put64(body + 80, cursor);
            if (argc == 6) memcpy(body + 88, argv[5], 64);
            if (argc == 5) bq_put32(body + 88, (u32)expected_recipe);
            size = BQ_EXPORT_REQUEST_CAP;
        }
    }
    else if (argc == 2 && (!strcmp(argv[0], "status") || !strcmp(argv[0], "result") || !strcmp(argv[0], "cancel")))
    {
        *operation = !strcmp(argv[0], "status") ? BQ_OP_STATUS : !strcmp(argv[0], "result") ? BQ_OP_RESULT : BQ_OP_CANCEL;
        valid = bq_decimal(argv[1], true, &id);
        bq_put64(body, id);
        size = 8;
    }
    else if ((argc == 2 || argc == 3) && !strcmp(argv[0], "logs"))
    {
        *operation = BQ_OP_LOGS;
        valid = bq_decimal(argv[1], true, &id) && (argc == 2 || bq_decimal(argv[2], false, &after));
        bq_put64(body, id);
        bq_put64(body + 8, after);
        size = 16;
    }
    if (valid) bq_packet(request, *operation, 1, body, size);
    return valid;
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

/* Only locally dispatched or validated typed responses reach this formatter. */
BUSTER_GLOBAL_LOCAL bool bq_response_write(u32 operation, BqPacket const* response, FILE* output)
{
    bool written = true;
    u8 const* data = response->bytes + BQ_CONTROL_HEADER;
    if (operation == BQ_OP_CAPABILITIES)
    {
        written = fwrite(data + 4, 1, response->size - BQ_CONTROL_HEADER - 4, output) == response->size - BQ_CONTROL_HEADER - 4;
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
                          " pending=%u retained=%u request-sha256=%.64s failure=%s\n",
                          (uint64_t)bq_u64(data + 4), (uint64_t)bq_u64(data + 12), (uint64_t)bq_u64(data + 20),
                          bq_phase_name(bq_u32(data + 28)), bq_outcome_name(bq_u32(data + 32)), bq_u32(data + 40),
                          bq_u32(data + 44), bq_u32(data + 48), bq_u32(data + 52), (char const*)data + 56,
                          bq_error_name((BqError)bq_u32(data + 120))) >= 0;
        if (written)
        {
            bool bound = response->size == BQ_CONTROL_CAP;
            written = fprintf(output, "result-bound=%u statistical-decision=not-evaluated\n", bound ? 1u : 0u) >= 0;
            if (written && bound)
            {
                written = fprintf(output, "result-root=%.*s\nmanifest-sha256=%.64s\nbundle-sha256=%.64s\n"
                                  "full-result-sha256=%.64s\n", (int)bq_u32(data + 124), (char const*)data + 128,
                                  (char const*)data + 320, (char const*)data + 384, (char const*)data + 448) >= 0;
            }
        }
    }
    return written;
}

BUSTER_GLOBAL_LOCAL int bq_cli(int argc, char** argv, FILE* input, FILE* output, FILE* diagnostics)
{
    BqPacket request = {0};
    BqPacket response = {0};
    BqQueue queue = {.directory_fd = -1, .lock_fd = -1, .journal_fd = -1};
    BqError error = BQ_BAD_REQUEST;
    u32 operation = 0;
    u8 body[BQ_CONTROL_BODY] = {0};
    u32 body_size = 0;
    bool raw = false;
    bool remote = false;
    bool typed_remote = false;
    char const* socket_path = NULL;
    bool serve = false;
    bool valid = false;
    bool handled = false;
    bool simple_diagnostic = false;
    u64 id = 0;
    u64 argument = 0;
    u64 attempt = 0;
    if (argc == 5 && !strcmp(argv[1], "unpack-export"))
    {
        valid = true;
        error = bq_export_unpack(argv[2], argv[3], argv[4]);
        handled = true;
        simple_diagnostic = true;
    }
    else if (argc == 10 && !strcmp(argv[1], "worker-unit"))
    {
        valid = bq_decimal(argv[3], true, &argument) && bq_decimal(argv[4], true, &attempt);
        error = valid ? bq_worker_unit(string_from_pointer(argv[2]), string_from_pointer(argv[3]),
                                        string_from_pointer(argv[4]), string_from_pointer(argv[5]),
                                        string_from_pointer(argv[6]), string_from_pointer(argv[7]),
                                        string_from_pointer(argv[8]), string_from_pointer(argv[9])) : BQ_BAD_REQUEST;
        handled = true;
        simple_diagnostic = true;
    }
    else if (argc == 2 && !strcmp(argv[1], "capabilities"))
    {
        operation = BQ_OP_CAPABILITIES;
        valid = true;
    }
    else if (argc == 3 && !strcmp(argv[1], "rpc"))
    {
        remote = true;
        valid = true;
    }
    else if (argc >= 4 && !strcmp(argv[1], "client"))
    {
        typed_remote = true;
        socket_path = argv[2];
        valid = bq_client_arguments(argc - 3, argv + 3, false, &request, &operation);
    }
    else if (argc >= 3 && !strcmp(argv[1], "gateway"))
    {
        typed_remote = true;
        socket_path = BQ_GATEWAY_SOCKET;
        valid = bq_client_arguments(argc - 2, argv + 2, true, &request, &operation);
        if (valid && operation == BQ_OP_SUBMIT)
        {
            operation = BQ_OP_SUBMIT_EXCLUSIVE;
            bq_put32(request.bytes + 8, operation);
        }
    }
    else if (argc == 8 && !strcmp(argv[1], "serve"))
    {
        u64 cpu = 0;
        String8 installed = string_from_pointer(argv[4]);
        String8 workspace = string_from_pointer(argv[5]);
        String8 lease = string_from_pointer(argv[6]);
        serve = true;
        valid = bq_decimal(argv[7], false, &cpu) && cpu <= UINT32_MAX && argv[2][0] == '/' &&
                installed.length <= BQ_PATH_CAP && workspace.length <= BQ_PATH_CAP && lease.length <= BQ_PATH_CAP;
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
        else if (argc == 5 && !strcmp(argv[1], "materialize"))
        {
            String8 installed = string_from_pointer(argv[3]);
            String8 workspace = string_from_pointer(argv[4]);
            operation = BQ_OP_MATERIALIZE;
            valid = installed.length <= BQ_PATH_CAP && workspace.length <= BQ_PATH_CAP;
            if (valid)
            {
                bq_put32(body, (u32)installed.length);
                bq_put32(body + 4, (u32)workspace.length);
                memcpy(body + 8, installed.pointer, (size_t)installed.length);
                memcpy(body + 8 + installed.length, workspace.pointer, (size_t)workspace.length);
                body_size = 8 + (u32)installed.length + (u32)workspace.length;
            }
        }
        else if (argc == 6 && !strcmp(argv[1], "workspace-reconcile"))
        {
            String8 workspace = string_from_pointer(argv[3]);
            operation = BQ_OP_WORKSPACE_RECONCILE;
            valid = bq_decimal(argv[4], true, &id) && bq_decimal(argv[5], true, &argument) && workspace.length <= BQ_PATH_CAP;
            if (valid)
            {
                bq_put64(body, id);
                bq_put64(body + 8, argument);
                bq_put32(body + 16, (u32)workspace.length);
                memcpy(body + 20, workspace.pointer, (size_t)workspace.length);
                body_size = 20 + (u32)workspace.length;
            }
        }
        else if (argc == 7 && !strcmp(argv[1], "worker-run"))
        {
            String8 installed = string_from_pointer(argv[3]);
            String8 workspace = string_from_pointer(argv[4]);
            String8 lease = string_from_pointer(argv[5]);
            operation = BQ_OP_WORKER_RUN;
            valid = bq_decimal(argv[6], false, &argument) && argument <= UINT32_MAX &&
                    installed.length <= BQ_PATH_CAP && workspace.length <= BQ_PATH_CAP && lease.length <= BQ_PATH_CAP &&
                    installed.length + workspace.length + lease.length <= BQ_CONTROL_BODY - 16;
            if (valid)
            {
                bq_put32(body, (u32)installed.length);
                bq_put32(body + 4, (u32)workspace.length);
                bq_put32(body + 8, (u32)lease.length);
                bq_put32(body + 12, (u32)argument);
                memcpy(body + 16, installed.pointer, (size_t)installed.length);
                memcpy(body + 16 + installed.length, workspace.pointer, (size_t)workspace.length);
                memcpy(body + 16 + installed.length + workspace.length, lease.pointer, (size_t)lease.length);
                body_size = 16 + (u32)installed.length + (u32)workspace.length + (u32)lease.length;
            }
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
    if (!handled && remote && valid)
    {
        error = bq_transport_client(argv[2], input, output);
        if (fflush(output) != 0)
        {
            error = BQ_IO;
        }
        handled = true;
        simple_diagnostic = true;
    }
    if (!handled && typed_remote && valid)
    {
        bool download = operation == BQ_OP_EXPORT && bq_u64(request.bytes + BQ_CONTROL_HEADER + 80) == UINT64_MAX;
        if (download) error = bq_export_download(socket_path, &request, output, diagnostics);
        else error = bq_transport_request(socket_path, &request, &response);
        if (operation == BQ_OP_EXPORT) raw = true;
        if (!download && error == BQ_OK && !bq_public_response_valid(&request, &response))
        {
            response = (BqPacket){0};
            error = BQ_BAD_REQUEST;
        }
        handled = true;
        simple_diagnostic = true;
    }
    if (!handled && serve && valid)
    {
        u64 cpu = 0;
        bq_decimal(argv[7], false, &cpu);
        BqWorkerConfig config = {
            .installed_root = string_from_pointer(argv[4]),
            .workspace_root = string_from_pointer(argv[5]),
            .lease_file = string_from_pointer(argv[6]),
            .boot_id_file = S8("/proc/sys/kernel/random/boot_id"),
            .cgroup_root = S8("/sys/fs/cgroup"),
            .limits = {(u32)cpu, 8ull * 1024 * 1024 * 1024, 0, 256, 60ull * 60 * 1000000},
            .quarantine = &bq_worker_quarantine,
            .queue_root = string_from_pointer(argv[2]),
            .production_path = true,
        };
        error = bq_transport_serve(argv[2], argv[3], &config);
        handled = true;
        simple_diagnostic = true;
    }
    if (!handled && valid)
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
    else if (error == BQ_OK && response.size)
    {
        written = bq_response_write(operation, &response, output);
    }
    if (fflush(output) != 0)
    {
        written = false;
    }
    if (!written)
    {
        error = BQ_IO;
    }
    if (simple_diagnostic && error != BQ_OK)
    {
        fprintf(diagnostics, "bench_service: %s\n", bq_error_name(error));
    }
    else if (!simple_diagnostic && error != BQ_OK)
    {
        fprintf(diagnostics, "bench_service: %s; io-uncertain requires retry/reopen, never rollback\n", bq_error_name(error));
        if (!valid)
        {
            fprintf(diagnostics, "commands: capabilities | submit DIR PRINCIPAL KEY RECIPE BASE_SHA CANDIDATE_SHA | "
                    "status/result/cancel DIR JOB | logs DIR JOB [AFTER_SEQUENCE] | fake-run DIR | "
                    "fake-reconcile DIR JOB TOKEN | materialize DIR INSTALLED_ROOT WORKSPACE_ROOT | "
                    "workspace-reconcile DIR WORKSPACE_ROOT JOB TOKEN | "
                    "worker-run DIR INSTALLED_ROOT WORKSPACE_ROOT LEASE_FILE CPU | protocol DIR | rpc SOCKET | "
                    "client SOCKET capabilities/submit/status/result/cancel/logs ... | "
                    "gateway capabilities | gateway submit KEY BASE_SHA CANDIDATE_SHA | "
                    "gateway status/result/cancel JOB | gateway logs JOB [AFTER_SEQUENCE] | "
                    "gateway export JOB TOKEN FULL_SHA [EXPECTED_RECIPE] | gateway export-chunk JOB TOKEN FULL_SHA CURSOR RECEIPT_SHA | "
                    "unpack-export ARCHIVE NEW_ABSOLUTE_DIRECTORY RECEIPT_SHA | "
                    "serve DIR SOCKET INSTALLED_ROOT WORKSPACE_ROOT LEASE_FILE CPU\n");
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
