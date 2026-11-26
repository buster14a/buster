// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The checker driver's persistent-oracle failure contract (#2421). A forked protocol peer answers one request or
// deliberately loses it, without LLVM libraries or an intermittent compiler crash. A real clang child independently
// supplies the expected status and selected stream. Private to the checker driver's owning unity unit.

typedef enum CheckRunModeReply
{
    CHECK_RUN_MODE_UNAVAILABLE,
    CHECK_RUN_MODE_EOF,
    CHECK_RUN_MODE_PARTIAL,
    CHECK_RUN_MODE_STALL,
    CHECK_RUN_MODE_ANSWER,
} CheckRunModeReply;

// Consume one request string without keeping its bytes. The protocol peer reads the complete request before failing,
// so the client observes an unavailable reply rather than relying on the race between send and server termination.
static bool check_test_run_mode_string(int socket, uint64_t deadline)
{
    uint32_t length = 0;
    bool read = oracle_server_receive(socket, &length, sizeof(length), deadline);
    uint8_t bytes[256];
    while (read && length != 0)
    {
        const uint32_t count = length < sizeof(bytes) ? length : (uint32_t)sizeof(bytes);
        read = oracle_server_receive(socket, bytes, count, deadline);
        length -= count;
    }
    return read;
}

static void check_test_run_mode_reply(int socket, CheckRunModeReply reply, int32_t status, const char* text)
{
    const uint64_t deadline = oracle_now() + UINT64_C(5000000000);
    uint32_t header[3] = { 0 };
    bool read = oracle_server_receive(socket, header, sizeof(header), deadline) && header[0] != 0 &&
                (header[1] == 1 || header[1] == 2) && check_test_run_mode_string(socket, deadline);
    const uint64_t strings = (uint64_t)header[0] + header[2];
    for (uint64_t i = 0; read && i < strings; i += 1)
    {
        read = check_test_run_mode_string(socket, deadline);
    }
    if (read && reply == CHECK_RUN_MODE_STALL)
    {
        // Only the client's request deadline releases this peer. oracle_server_stop kills and reaps it.
        for (;;)
        {
            pause();
        }
    }
    if (read && (reply == CHECK_RUN_MODE_PARTIAL || reply == CHECK_RUN_MODE_ANSWER))
    {
        const uint32_t length = (uint32_t)strlen(text) + (reply == CHECK_RUN_MODE_PARTIAL ? 1u : 0u);
        const uint32_t sent = reply == CHECK_RUN_MODE_PARTIAL ? (length - 1) / 2 : length;
        read = oracle_server_send(socket, &status, sizeof(status), deadline) && oracle_server_send(socket, &length, sizeof(length), deadline) &&
               oracle_server_send(socket, text, sent, deadline);
    }
    close(socket);
    _exit(read ? 0 : 1);
}

static bool check_test_run_mode_peer(CheckRunModeReply reply, int32_t status, const char* text)
{
    int sockets[2] = { -1, -1 };
    const bool paired = socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sockets) == 0;
    fflush(NULL);
    const pid_t child = paired ? fork() : -1;
    if (child == 0)
    {
        close(sockets[0]);
        check_test_run_mode_reply(sockets[1], reply, status, text);
    }
    if (sockets[1] >= 0)
    {
        close(sockets[1]);
    }
    if (child > 0)
    {
        OracleServer* server = &check_driver_run_modes.server;
        server->socket = sockets[0];
        server->pid = child;
        server->owner = getpid();
        server->starts = 1;
    }
    else if (sockets[0] >= 0)
    {
        close(sockets[0]);
    }
    return child > 0;
}

// A reusable source command gets the same current-limit snapshot as a new compiler process. It must not inherit a
// prior command's snapshot or leave its own behind: resource-limit tests also run in this process and its children.
#if !BUSTER_ARENA_REGIONS
static void check_test_run_mode_limits(CheckTest* test, const char* source)
{
    const ArenaLimits saved = arena_limits;
    for (uint32_t rejected = 0; rejected < 2; rejected += 1)
    {
        char* const arguments[] = { "/proc/self/exe", "-fsyntax-only", "-fno-caret-diagnostics", (char*)source,
                                    rejected != 0 ? "-DBUSTER_RUN_MODE_REJECT" : NULL, NULL };
        char* expected = NULL;
        const int expected_status = check_test_driver_child(arguments, &expected, false, true);
        const bool ready = expected_status == (int)rejected && expected != NULL;
        check_test_expect(test, ready, "run modes: the fresh compiler supplies the current-limit result");
        for (uint32_t fixed = 0; ready && fixed < 2; fixed += 1)
        {
            // The deliberately stale one-page share cannot compile this unit. Each command must read the live
            // process limits before reserving, even where the caller already had a fixed snapshot.
            const ArenaLimits prior = {
                .fixed = fixed != 0,
                .address_share = BUSTER_ARENA_PAGE_BYTES,
                .commit_unbounded = false,
            };
            arena_limits = prior;
            char* actual = NULL;
            int status = -1;
            const bool taken = check_test_driver_run_in_process(arguments, &actual, false, &status);
            check_test_expect(test, taken && status == expected_status && actual != NULL && strcmp(actual, expected) == 0,
                              "run modes: each reusable command reads current limits, as its fresh child does");
            check_test_expect(test, arena_limits.fixed == prior.fixed && arena_limits.address_share == prior.address_share &&
                                   arena_limits.commit_unbounded == prior.commit_unbounded,
                              "run modes: a successful or rejected command restores the caller's limit policy");
            arena_limits = saved;
            free(actual);
        }
        free(expected);
    }
    char* const response[] = { "/proc/self/exe", "@run-mode-response", NULL };
    char* text = NULL;
    int status = -1;
    const bool taken = check_test_driver_run_in_process(response, &text, false, &status);
    check_test_expect(test, !taken && text == NULL && arena_limits.fixed == saved.fixed && arena_limits.address_share == saved.address_share &&
                           arena_limits.commit_unbounded == saved.commit_unbounded,
                      "run modes: a response-file command left to a child restores the caller's limit policy");
    free(text);
    arena_limits = saved;
}
#endif

static void check_test_driver_run_modes(CheckTest* test, const char* clang, const char* directory)
{
    const CheckDriverRunModes saved = check_driver_run_modes;
    const char* source = "tests/driver/run_modes.c";
    char missing[PATH_MAX];
    snprintf(missing, sizeof(missing), "%s/no-clang-server", directory);
    const bool available = access(source, R_OK) == 0;
    check_test_expect(test, available, "run modes: runtime fixture is available");
#if !BUSTER_ARENA_REGIONS
    if (available)
    {
        check_test_run_mode_limits(test, source);
    }
#endif
    for (uint32_t rejected = 0; available && rejected < 2; rejected += 1)
    {
        for (uint32_t stream = 0; stream < 2; stream += 1)
        {
            const bool stdout_capture = stream != 0;
            char* const arguments[] = { (char*)clang, "-E", "-P", "-fno-caret-diagnostics", (char*)source,
                                        rejected != 0 ? "-DBUSTER_RUN_MODE_REJECT" : NULL, NULL };
            char* expected = NULL;
            const int expected_status = check_test_driver_child(arguments, &expected, stdout_capture, true);
            const bool ready = expected_status == (int)rejected && expected != NULL;
            check_test_expect(test, ready, "run modes: independent clang accepts or rejects the fixture as requested");
            for (uint32_t differential = 0; ready && differential < 2; differential += 1)
            {
                for (uint32_t c = CHECK_RUN_MODE_UNAVAILABLE; c <= CHECK_RUN_MODE_ANSWER; c += 1)
                {
                    const CheckRunModeReply reply = (CheckRunModeReply)c;
                    // One timeout proves the deadline/fallback boundary; the other cases cover both streams and statuses.
                    if (reply != CHECK_RUN_MODE_STALL || (rejected != 0 && !stdout_capture && differential != 0))
                    {
                        check_driver_run_modes = (CheckDriverRunModes){
                            .differential = differential != 0,
                            .server = {
                                .path = missing,
                                .seconds = 1,
                                .socket = -1,
                            },
                            .clang = clang,
                        };
                        const bool peer = reply == CHECK_RUN_MODE_UNAVAILABLE || check_test_run_mode_peer(reply, expected_status, expected);
                        char* actual = NULL;
                        const int status = peer ? check_test_driver_spawn_stream(arguments, &actual, stdout_capture) : -1;
                        const bool answer = reply == CHECK_RUN_MODE_ANSWER;
                        const uint64_t children = answer && differential == 0 ? 0 : 1;
                        const bool stopped = answer || (check_driver_run_modes.server.socket < 0 && check_driver_run_modes.server.pid == 0);
                        const bool same = status == expected_status && actual != NULL && strcmp(actual, expected) == 0;
                        char description[256];
                        snprintf(description, sizeof(description), "run modes: reply %u, rejected %u, stdout %u, differential %u keeps child status/output and compares only answers",
                                 c, rejected, stream, differential);
                        check_test_expect(test, peer && same && stopped && check_driver_run_modes.differences == 0 &&
                                              check_driver_run_modes.served_runs == (uint64_t)answer && check_driver_run_modes.child_runs == children &&
                                              check_driver_run_modes.stalls == (uint64_t)(reply == CHECK_RUN_MODE_STALL), description);
                        free(actual);
                        oracle_server_stop(&check_driver_run_modes.server);
                    }
                }
            }
            // A complete, nonnegative answer still participates in differential validation. Losing the reply and
            // receiving a wrong status or stream are different cases; neither mismatch may be hidden by fallback.
            for (uint32_t mismatch = 0; ready && mismatch < 2; mismatch += 1)
            {
                check_driver_run_modes = (CheckDriverRunModes){
                    .differential = true,
                    .server = {
                        .path = missing,
                        .socket = -1,
                    },
                    .clang = clang,
                    // Keep intentional mismatch output out of the test log, while checking the exact increment.
                    .differences = 8,
                };
                const int reply_status = mismatch == 0 ? 1 - expected_status : expected_status;
                const char* reply_text = mismatch == 0 ? expected : "different server output\n";
                const bool peer = check_test_run_mode_peer(CHECK_RUN_MODE_ANSWER, reply_status, reply_text);
                char* actual = NULL;
                const int status = peer ? check_test_driver_spawn_stream(arguments, &actual, stdout_capture) : -1;
                check_test_expect(test, peer && status == expected_status && actual != NULL && strcmp(actual, expected) == 0 &&
                                      check_driver_run_modes.differences == 9 && check_driver_run_modes.served_runs == 1,
                                  "run modes: a complete wrong status or stream remains a differential failure");
                free(actual);
                oracle_server_stop(&check_driver_run_modes.server);
            }
            free(expected);
        }
    }
    check_driver_run_modes = saved;
}
