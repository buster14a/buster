// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Private request-transmission checks for #2761, included by oracle_cache_test.c. Small socket buffers force partial
// writes. A parent watchdog kills the test's process group if a blocking-send regression defeats the request limit.

typedef enum OracleTestServerPeer
{
    ORACLE_TEST_SERVER_PARK,
    ORACLE_TEST_SERVER_INTERRUPT,
    ORACLE_TEST_SERVER_DRAIN,
    ORACLE_TEST_SERVER_TRICKLE,
} OracleTestServerPeer;

static volatile sig_atomic_t oracle_test_server_signals;

static void oracle_test_server_interrupt(int signal)
{
    (void)signal;
    oracle_test_server_signals += 1;
}

// A socket whose peer either never reads or drains the known padding in small pieces. The server owns the peer just
// as it owns its real executable; stop must close the socket and reap it. Its path and restart count are retained.
static bool oracle_test_server_peer(OracleServer* server, OracleTestServerPeer mode, size_t bytes)
{
    int sockets[2] = {
        -1,
        -1,
    };
    const int room = 4096;
    const bool paired = socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sockets) == 0 &&
                        setsockopt(sockets[0], SOL_SOCKET, SO_SNDBUF, &room, sizeof(room)) == 0;
    const pid_t caller = getpid();
    const pid_t peer = paired ? fork() : -1;
    if (peer == 0)
    {
        close(sockets[0]);
        const struct timespec delay = {
            .tv_sec = 0,
            .tv_nsec = mode == ORACLE_TEST_SERVER_DRAIN ? 1000000 : 10000000,
        };
        size_t received = 0;
        bool valid = true;
        while (valid && (mode < ORACLE_TEST_SERVER_DRAIN || received < bytes))
        {
            nanosleep(&delay, 0);
            if (mode != ORACLE_TEST_SERVER_PARK)
            {
                kill(caller, SIGUSR1);
            }
            if (mode >= ORACLE_TEST_SERVER_DRAIN)
            {
                char chunk[1024];
                const size_t maximum = mode == ORACLE_TEST_SERVER_TRICKLE ? 256 : sizeof(chunk);
                const size_t wanted = bytes - received < maximum ? bytes - received : maximum;
                ssize_t got = -1;
                do
                {
                    got = read(sockets[1], chunk, wanted);
                }
                while (got < 0 && errno == EINTR);
                valid = got > 0;
                for (ssize_t i = 0; valid && i < got; i += 1)
                {
                    valid = chunk[i] == 'p';
                }
                received += got > 0 ? (size_t)got : 0;
            }
        }
        close(sockets[1]);
        _exit(valid && received == bytes ? 0 : 1);
    }
    if (sockets[1] >= 0)
    {
        close(sockets[1]);
    }
    if (peer > 0)
    {
        server->owner = caller;
        server->pid = peer;
        server->socket = sockets[0];
    }
    else if (sockets[0] >= 0)
    {
        close(sockets[0]);
    }
    return peer > 0;
}

static bool oracle_test_server_reaped(pid_t peer)
{
    int status = 0;
    const pid_t waited = peer > 0 ? waitpid(peer, &status, WNOHANG) : 0;
    return waited == -1 && errno == ECHILD;
}

static void oracle_test_server_writes(OracleTest* test, const char* padding, size_t bytes)
{
    int sockets[2] = {
        -1,
        -1,
    };
    const bool paired = socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sockets) == 0;
    char byte = 0;
    const bool expired = paired && !oracle_server_send(sockets[0], "x", 1, oracle_now());
    const ssize_t got = paired ? recv(sockets[1], &byte, 1, MSG_DONTWAIT) : 0;
    oracle_test_expect(test, expired && got == -1 && (errno == EAGAIN || errno == EWOULDBLOCK),
                       "server transmission: an expired deadline writes nothing to a writable socket");
    if (sockets[1] >= 0)
    {
        close(sockets[1]);
    }
    oracle_test_expect(test, paired && !oracle_server_send(sockets[0], "x", 1, oracle_now() + 1000000000),
                       "server transmission: a closed peer fails without SIGPIPE");
    if (sockets[0] >= 0)
    {
        close(sockets[0]);
    }

    OracleServer server = { .socket = -1 };
    oracle_test_server_signals = 0;
    const bool draining = oracle_test_server_peer(&server, ORACLE_TEST_SERVER_DRAIN, bytes);
    const bool sent = draining && oracle_server_send(server.socket, padding, bytes, oracle_now() + 5000000000ull);
    int status = 0;
    pid_t waited = -1;
    if (sent)
    {
        do
        {
            waited = waitpid(server.pid, &status, 0);
        }
        while (waited < 0 && errno == EINTR);
    }
    oracle_test_expect(test, sent && waited == server.pid && WIFEXITED(status) && WEXITSTATUS(status) == 0 && oracle_test_server_signals != 0,
                       "server transmission: partial writes and interrupted waits preserve all bytes");
    if (waited == server.pid)
    {
        server.pid = 0;
    }
    oracle_server_stop(&server);

    oracle_test_server_signals = 0;
    const bool trickling = oracle_test_server_peer(&server, ORACLE_TEST_SERVER_TRICKLE, bytes);
    const uint64_t begun = oracle_now();
    const bool ended = trickling && !oracle_server_send(server.socket, padding, bytes, begun + 100000000);
    const uint64_t elapsed = oracle_now() - begun;
    oracle_test_expect(test, ended && elapsed >= 90000000 && elapsed < 3000000000ull && oracle_test_server_signals != 0,
                       "server transmission: repeated progress and EINTR do not reset the absolute deadline");
    const pid_t peer = server.pid;
    oracle_server_stop(&server);
    oracle_test_expect(test, trickling && oracle_test_server_reaped(peer), "server transmission: the partial-request peer is reaped");
}

static void oracle_test_server_requests(OracleTest* test, const char* path, const char* input, char* padding)
{
    memcpy(padding, "ORACLE_TEST_PADDING=", sizeof("ORACLE_TEST_PADDING=") - 1);
    char* short_arguments[] = {
        test->helper,
        "cat",
        0,
    };
    char* long_arguments[] = {
        test->helper,
        "cat",
        padding,
        0,
    };
    char* empty_environment[] = { 0 };
    char* long_environment[] = {
        padding,
        0,
    };
    OracleServer server = {
        .path = path,
        .seconds = 1,
        .socket = -1,
    };
    for (uint32_t environment = 0; environment < 2; environment += 1)
    {
        oracle_test_server_signals = 0;
        const bool ready = oracle_test_server_peer(&server, environment == 0 ? ORACLE_TEST_SERVER_PARK : ORACLE_TEST_SERVER_INTERRUPT, 0);
        const pid_t peer = server.pid;
        char* output = 0;
        char* errors = 0;
        uint64_t output_length = 0;
        uint64_t errors_length = 0;
        const uint64_t begun = oracle_now();
        const int status = ready ? oracle_server_run(&server, environment == 0 ? long_arguments : short_arguments, environment == 0 ? empty_environment : long_environment,
                                                     test->base, input, 3, 1, &output, &output_length, &errors, &errors_length) : 0;
        const uint64_t elapsed = oracle_now() - begun;
        oracle_test_expect(test, ready && status == -1 && server.stalled && server.socket == -1 && server.pid == 0 && output == 0 && errors == 0 &&
                                 output_length == 0 && errors_length == 0 && elapsed >= 900000000 && elapsed < 5000000000ull && oracle_test_server_reaped(peer) &&
                                 (environment == 0 || oracle_test_server_signals != 0),
                           environment == 0 ? "server transmission: a blocked argument times out, clears captures and reaps its peer" :
                                              "server transmission: a blocked environment times out despite repeated EINTR");
        free(output);
        free(errors);
        const uint32_t starts = server.starts;
        const int again = ready ? oracle_server_run(&server, short_arguments, empty_environment, test->base, input, 3, 1,
                                                    &output, &output_length, &errors, &errors_length) : -1;
        oracle_test_expect(test, again == 0 && !server.stalled && server.starts == starts + 1 && output != 0 && output_length == 13 &&
                                 strcmp(output, "served bytes\n") == 0 && errors != 0 && errors_length == 0,
                           "server transmission: the next request starts a fresh server and preserves its reply");
        free(output);
        free(errors);
        oracle_server_stop(&server);
    }

    OracleCache off;
    oracle_cache_open(&off, 0, 0);
    oracle_serve(path, test->helper, false);
    OracleRun run = { 0 };
    bool ran = oracle_test_serve(&off, short_arguments, input, 0, 0, &run) && run.served && oracle_test_served_text(&run, "served bytes\n");
    oracle_run_release(&run);
    oracle_server_stop(&oracle_serving.server);
    oracle_serving.server.seconds = 1;
    const bool attached = ran && oracle_test_server_peer(&oracle_serving.server, ORACLE_TEST_SERVER_PARK, 0);
    const pid_t peer = oracle_serving.server.pid;
    ran = attached && oracle_test_serve(&off, short_arguments, input, long_environment, 0, &run);
    oracle_test_expect(test, ran && !run.served && oracle_test_served_text(&run, "served bytes\n") && oracle_serving.counts->unserved == 1 &&
                             oracle_serving.counts->stalls == 1 && oracle_test_server_reaped(peer),
                       "server transmission: an unanswered write falls back to the ordinary child and counts one stall");
    oracle_run_release(&run);
    ran = ran && oracle_test_serve(&off, short_arguments, input, 0, 0, &run);
    oracle_test_expect(test, ran && run.served && oracle_test_served_text(&run, "served bytes\n") && oracle_serving.counts->served == 2 &&
                             oracle_serving.counts->unserved == 1 && oracle_serve_end("oracle transmission test"),
                       "server transmission: serving recovers after the ordinary-child fallback");
    oracle_run_release(&run);
    oracle_serve(0, 0, false);
    oracle_cache_close(&off);
}

static void oracle_test_server_transport(OracleTest* test, const char* path, const char* input, bool ready)
{
    int reports[2] = {
        -1,
        -1,
    };
    const bool piped = ready && pipe(reports) == 0 && fcntl(reports[0], F_SETFD, FD_CLOEXEC) == 0 && fcntl(reports[1], F_SETFD, FD_CLOEXEC) == 0;
    fflush(0);
    const pid_t child = piped ? fork() : -1;
    if (child == 0)
    {
        close(reports[0]);
        const bool grouped = setpgid(0, 0) == 0;
        struct sigaction action = {
            .sa_handler = oracle_test_server_interrupt,
        };
        sigemptyset(&action.sa_mask);
        const bool signals = sigaction(SIGUSR1, &action, 0) == 0;
        const size_t bytes = 65536;
        char* padding = malloc(bytes + 1);
        test->checks = 0;
        test->failures = 0;
        oracle_test_expect(test, grouped && signals && padding != 0, "server transmission: prepare isolated socket tests");
        if (grouped && signals && padding != 0)
        {
            memset(padding, 'p', bytes);
            padding[bytes] = 0;
            oracle_test_server_writes(test, padding, bytes);
            oracle_test_server_requests(test, path, input, padding);
        }
        free(padding);
        const uint32_t report[2] = {
            test->checks,
            test->failures,
        };
        const bool written = write(reports[1], report, sizeof(report)) == (ssize_t)sizeof(report);
        close(reports[1]);
        fflush(0);
        _exit(written ? 0 : 1);
    }
    if (reports[1] >= 0)
    {
        close(reports[1]);
    }
    if (child > 0)
    {
        setpgid(child, child);
    }
    const uint64_t deadline = oracle_now() + 15000000000ull;
    int status = 0;
    pid_t waited = 0;
    while (child > 0 && waited == 0 && oracle_now() < deadline)
    {
        waited = waitpid(child, &status, WNOHANG);
        if (waited < 0 && errno == EINTR)
        {
            waited = 0;
        }
        if (waited == 0)
        {
            const struct timespec delay = {
                .tv_sec = 0,
                .tv_nsec = 10000000,
            };
            nanosleep(&delay, 0);
        }
    }
    const bool finished = child > 0 && waited == child && WIFEXITED(status) && WEXITSTATUS(status) == 0;
    if (child > 0)
    {
        // This also removes any peer left behind by a failed assertion or an interrupted worker.
        kill(-child, SIGKILL);
        if (waited != child)
        {
            kill(child, SIGKILL);
            do
            {
                waited = waitpid(child, &status, 0);
            }
            while (waited < 0 && errno == EINTR);
        }
    }
    uint32_t report[2] = { 0 };
    const bool reported = finished && read(reports[0], report, sizeof(report)) == (ssize_t)sizeof(report);
    if (reports[0] >= 0)
    {
        close(reports[0]);
    }
    oracle_test_expect(test, reported && report[0] != 0, "server transmission: request limits finish before the independent watchdog");
    if (reported)
    {
        test->checks += report[0];
        test->failures += report[1];
    }
}
