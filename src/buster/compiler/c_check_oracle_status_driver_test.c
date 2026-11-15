// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The persistent oracle's int32 protocol status represents a process exit code, not a wait status (#2740). Malformed
// values must fail the request rather than wrap into a valid compiler result. Uses the checker's private protocol peer.

static void check_test_driver_oracle_status(CheckTest* test)
{
    const CheckDriverRunModes saved = check_driver_run_modes;
    static const int32_t statuses[] = { INT32_MIN, -1, 0, 1, 127, 255, 256, INT32_MAX };
    static const char reply[] = "complete server stream\n";
    char* const arguments[] = { "protocol-status-test", NULL };
    char* const environment[] = { NULL };
    for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(statuses); c += 1)
    {
        for (uint32_t streams = 1; streams <= 2; streams += 1)
        {
            check_driver_run_modes = (CheckDriverRunModes){
                .server = {
                    .socket = -1,
                },
            };
            const int32_t expected = statuses[c];
            const bool valid = expected >= 0 && expected <= UINT8_MAX;
            const bool peer = check_test_run_mode_peer(CHECK_RUN_MODE_ANSWER, expected, reply);
            char* output = NULL;
            char* errors = NULL;
            uint64_t output_length = 0;
            uint64_t errors_length = 0;
            OracleServer* server = &check_driver_run_modes.server;
            const int status = peer ? oracle_server_run(server, arguments, environment, ".", NULL, streams, 1,
                                                        &output, &output_length, &errors, &errors_length) : -1;
            const char* text = streams == 1 ? output : errors;
            const uint64_t length = streams == 1 ? output_length : errors_length;
            const bool captured = valid && text != NULL && length == sizeof(reply) - 1 && strcmp(text, reply) == 0;
            const bool discarded = !valid && output == NULL && errors == NULL && output_length == 0 && errors_length == 0 &&
                                   server->socket < 0 && server->pid == 0;
            char description[256];
            snprintf(description, sizeof(description), "oracle protocol: status %d, stream %u is retained only inside the process exit range", expected, streams);
            check_test_expect(test, peer && status == (valid ? expected : -1) && (valid ? captured : discarded) && !server->stalled, description);
            free(output);
            free(errors);
            oracle_server_stop(server);
        }
    }
    check_driver_run_modes = saved;
}
