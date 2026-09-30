/* Real socketpair/pidfd tests of the production phase protocol. Fixture
 * processes exercise ordering and durability, not performance qualification.
 *
 * #881 PR 4 (BQPHASE2): bq_test_phase_v2_packets covers the digest codec,
 * every version-2 phase in its only order, each wrong size, the other
 * version, an unknown or out-of-order phase, a missing, zero or unwanted
 * digest, and the BQPHASE1 layout byte for byte. bq_test_phase_ack_window
 * covers the acknowledgement window. bq_test_phase_retirement runs a stub
 * producer through the coordinator's bq_worker_phase_join: it keeps the
 * ready digest, hands off a real receipt authority before acknowledging
 * MEASURED, and refuses a digest naming no authority, an execution deadline
 * reached before or during the handoff, a BQPHASE1 packet and a zero ready
 * digest. */
#ifndef BUSTER_BENCH_SERVICE_PHASE_CHANNEL_TESTS_H
#define BUSTER_BENCH_SERVICE_PHASE_CHANNEL_TESTS_H
#include <signal.h>
#include <sys/time.h>
#include "retirement_coordinator_fixture.h"

BUSTER_GLOBAL_LOCAL void bq_test_phase_run(unsigned defect, char const* driver);
BUSTER_GLOBAL_LOCAL void bq_test_phase_prelaunch_deadline(void);
BUSTER_GLOBAL_LOCAL void bq_test_phase_finalization_deadline(void);
BUSTER_GLOBAL_LOCAL void bq_test_phase_v2_packets(void);
BUSTER_GLOBAL_LOCAL void bq_test_phase_ack_window(void);
BUSTER_GLOBAL_LOCAL void bq_test_phase_retirement(unsigned defect);
BUSTER_GLOBAL_LOCAL u32 bq_test_phase_deadline_clock_calls;
BUSTER_GLOBAL_LOCAL u64 bq_test_phase_expired_clock(BqWorkerBackend* backend);

BUSTER_GLOBAL_LOCAL int bq_test_phase_ack_child(int descriptor)
{
    unsigned char message[BQ_PHASE_MESSAGE_BYTES] = {0};
    ssize_t count = recv(descriptor, message, sizeof(message), 0);
    int ok = count == BQ_PHASE_MESSAGE_BYTES && !memcmp(message, "BQPHASE1", 8) &&
             bq_phase_get(message + 24) == BQ_PHASE_PREPARING && bq_phase_get(message + 40) == 0;
    if (ok)
    {
        bq_phase_put(message + 40, 1);
        ok = send(descriptor, message, sizeof(message), MSG_NOSIGNAL) == BQ_PHASE_MESSAGE_BYTES;
    }
    return ok;
}

BUSTER_GLOBAL_LOCAL void bq_test_phase_exchange_until_success(void)
{
    int pair[2] = {-1, -1};
    bool paired = socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair) == 0;
    BQ_CHECK(paired);
    pid_t child = paired ? fork() : -1;
    BQ_CHECK(child != -1);
    if (child == 0)
    {
        close(pair[0]);
        int ok = bq_test_phase_ack_child(pair[1]);
        close(pair[1]);
        _exit(ok ? 0 : 1);
    }
    if (child > 0) close(pair[1]);
    else if (pair[1] >= 0) close(pair[1]);
    BqPhaseChannel channel = {.descriptor = -1};
    bool initialized = child > 0 && bq_phase_init(&channel, pair[0], 7, 9);
    uint64_t now = bq_phase_clock();
    uint64_t deadline = now && UINT64_C(1000000000) <= UINT64_MAX - now ?
                        now + UINT64_C(1000000000) : UINT64_MAX;
    bool exchanged = initialized && bq_phase_exchange_until(&channel, BQ_PHASE_PREPARING, deadline);
    int status = 0;
    BqError joined = child > 0 ? bq_worker_waitpid_until(child, &status,
        bq_worker_deadline(bq_worker_monotonic_milliseconds(), 1000)) : BQ_BAD_REQUEST;
    if (joined != BQ_OK && child > 0)
    {
        kill(child, SIGKILL);
        joined = bq_worker_waitpid_until(child, &status,
            bq_worker_deadline(bq_worker_monotonic_milliseconds(), 1000));
    }
    BQ_CHECK(exchanged && channel.sequence == BQ_PHASE_PREPARING && joined == BQ_OK &&
             WIFEXITED(status) && WEXITSTATUS(status) == 0);
    if (pair[0] >= 0) close(pair[0]);
}

BUSTER_GLOBAL_LOCAL volatile sig_atomic_t bq_test_phase_alarm_count;

BUSTER_GLOBAL_LOCAL void bq_test_phase_alarm_handler(int signal_number)
{
    if (signal_number == SIGALRM) bq_test_phase_alarm_count += 1;
}

BUSTER_GLOBAL_LOCAL void bq_test_phase_exchange_until_failures(void)
{
    int expired_pair[2] = {-1, -1};
    bool expired_paired = socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, expired_pair) == 0;
    BQ_CHECK(expired_paired);
    BqPhaseChannel expired = {.descriptor = -1};
    uint64_t expired_at = bq_phase_clock();
    bool expired_ready = expired_paired && bq_phase_init(&expired, expired_pair[0], 7, 9);
    bool rejected_expired = expired_ready &&
        !bq_phase_exchange_until(&expired, BQ_PHASE_PREPARING, expired_at);
    unsigned char packet[BQ_PHASE_MESSAGE_BYTES] = {0};
    errno = 0;
    ssize_t absent = expired_paired ? recv(expired_pair[1], packet, sizeof(packet), MSG_DONTWAIT) : -1;
    BQ_CHECK(rejected_expired && expired.failed && absent < 0 &&
             (errno == EAGAIN || errno == EWOULDBLOCK));
    if (expired_pair[0] >= 0) close(expired_pair[0]);
    if (expired_pair[1] >= 0) close(expired_pair[1]);

    int pair[2] = {-1, -1};
    bool paired = socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair) == 0;
    BQ_CHECK(paired);
    BqPhaseChannel channel = {.descriptor = -1};
    bool initialized = paired && bq_phase_init(&channel, pair[0], 7, 9);
    struct sigaction action = {0}, prior_action = {0};
    struct itimerval timer = {0}, prior_timer = {0};
    sigemptyset(&action.sa_mask);
    action.sa_handler = bq_test_phase_alarm_handler;
    bool timer_saved = getitimer(ITIMER_REAL, &prior_timer) == 0;
    bool action_installed = timer_saved && sigaction(SIGALRM, &action, &prior_action) == 0;
    timer.it_value.tv_usec = 10000;
    timer.it_interval.tv_usec = 10000;
    bool timer_started = action_installed && setitimer(ITIMER_REAL, &timer, NULL) == 0;
    uint64_t start = bq_phase_clock();
    uint64_t deadline = start && UINT64_C(200000000) <= UINT64_MAX - start ?
                        start + UINT64_C(200000000) : UINT64_MAX;
    bq_test_phase_alarm_count = 0;
    bool timed_out = timer_started && initialized &&
        !bq_phase_exchange_until(&channel, BQ_PHASE_PREPARING, deadline);
    uint64_t end = bq_phase_clock();
    uint64_t elapsed = end >= start ? end - start : 0;
    ssize_t request = paired ? recv(pair[1], packet, sizeof(packet), MSG_DONTWAIT) : -1;
    BQ_CHECK(timed_out && channel.failed && channel.sequence == 0 && bq_test_phase_alarm_count >= 2 &&
             elapsed >= UINT64_C(150000000) && elapsed < UINT64_C(2000000000) &&
             request == BQ_PHASE_MESSAGE_BYTES && bq_phase_get(packet + 24) == BQ_PHASE_PREPARING &&
             bq_phase_get(packet + 40) == 0);
    struct itimerval cleared = {0};
    if (timer_started) BQ_CHECK(setitimer(ITIMER_REAL, &cleared, NULL) == 0);
    if (timer_saved) BQ_CHECK(setitimer(ITIMER_REAL, &prior_timer, NULL) == 0);
    if (action_installed) BQ_CHECK(sigaction(SIGALRM, &prior_action, NULL) == 0);
    if (pair[0] >= 0) close(pair[0]);
    if (pair[1] >= 0) close(pair[1]);
}

BUSTER_GLOBAL_LOCAL void bq_test_phase_packets(void)
{
    int pair[2] = {-1, -1};
    BQ_CHECK(socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair) == 0);
    BqPhaseChannel sender, receiver;
    BQ_CHECK(bq_phase_init(&sender, pair[0], 7, 9));
    BQ_CHECK(bq_phase_init(&receiver, pair[1], 7, 9));
    BQ_CHECK(fcntl(pair[0], F_GETFD) & FD_CLOEXEC);
    unsigned char message[BQ_PHASE_MESSAGE_BYTES], copy[BQ_PHASE_MESSAGE_BYTES + 1];
    BQ_CHECK(bq_phase_make(&sender, 1, message) && bq_phase_check(&receiver, message));
    for (unsigned byte = 0; byte < sizeof(message); ++byte)
    {
        memcpy(copy, message, sizeof(message));
        copy[byte] ^= 0x80;
        /* The timestamp may still describe a valid earlier instant; all other
         * bytes have a unique encoding and must be rejected. */
        if (byte < 32 || byte >= 40) BQ_CHECK(!bq_phase_check(&receiver, copy));
    }
    memcpy(copy, message, sizeof(message));
    bq_phase_put(copy + 32, UINT64_MAX);
    BQ_CHECK(!bq_phase_check(&receiver, copy));
    bq_phase_put(copy + 32, 0);
    BQ_CHECK(!bq_phase_check(&receiver, copy));
    memcpy(copy, message, sizeof(message));
    for (unsigned length = BQ_PHASE_MESSAGE_BYTES - 1; length <= BQ_PHASE_MESSAGE_BYTES + 1; ++length)
    {
        BQ_CHECK(send(pair[0], copy, length, MSG_NOSIGNAL) == (ssize_t)length);
        BQ_CHECK(bq_phase_receive(pair[1], message) == (length == BQ_PHASE_MESSAGE_BYTES));
    }
    int pipe_fd[2] = {-1, -1};
    BQ_CHECK(pipe2(pipe_fd, O_CLOEXEC | O_NONBLOCK) == 0);
    unsigned char ancillary[CMSG_SPACE(sizeof(int))] = {0};
    struct iovec vector = {copy, BQ_PHASE_MESSAGE_BYTES};
    struct msghdr packet = {.msg_iov = &vector, .msg_iovlen = 1,
                            .msg_control = ancillary, .msg_controllen = sizeof(ancillary)};
    struct cmsghdr* header = CMSG_FIRSTHDR(&packet);
    header->cmsg_level = SOL_SOCKET;
    header->cmsg_type = SCM_RIGHTS;
    header->cmsg_len = CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(header), &pipe_fd[1], sizeof(int));
    BQ_CHECK(sendmsg(pair[0], &packet, MSG_NOSIGNAL) == BQ_PHASE_MESSAGE_BYTES);
    close(pipe_fd[1]);
    BQ_CHECK(!bq_phase_receive(pair[1], message));
    char byte = 0;
    BQ_CHECK(read(pipe_fd[0], &byte, 1) == 0); /* No leaked ancillary writer. */
    close(pipe_fd[0]);
    close(pair[1]);
    BQ_CHECK(!bq_phase_exchange(&sender, 1) && sender.failed);
    BQ_CHECK(!bq_phase_make(&sender, 1, message));
    close(pair[0]);
    bq_test_phase_run(9, NULL);
    bq_test_phase_prelaunch_deadline();
    bq_test_phase_finalization_deadline();
    bq_test_phase_exchange_until_success();
    bq_test_phase_exchange_until_failures();
    bq_test_phase_v2_packets();
    bq_test_phase_ack_window();
    for (unsigned defect = 0; defect <= 9; ++defect) bq_test_phase_retirement(defect);
}

#define BQ_TEST_PHASE_READY_HEX "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
#define BQ_TEST_PHASE_OTHER_HEX "fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210"

/* Every other phase value (0 to 6) is refused by make on this channel. */
BUSTER_GLOBAL_LOCAL bool bq_test_phase_only_next(BqPhaseChannel const* channel, unsigned next,
                                                 unsigned char const* digest)
{
    unsigned char scratch[BQ_PHASE_MESSAGE_CAP];
    bool only = true;
    for (unsigned other = 0; other <= BQ_PHASE_RETIREMENT_READY + 1; ++other)
        if (other != next)
            only = only && !bq_phase_make_digest(channel, other,
                                                 bq_phase_digest_carried(channel->version, other) ? digest : NULL, scratch);
    return only;
}

BUSTER_GLOBAL_LOCAL void bq_test_phase_v2_packets(void)
{
    unsigned char ready[BQ_PHASE_DIGEST_BYTES], other[BQ_PHASE_DIGEST_BYTES], scratch[BQ_PHASE_DIGEST_BYTES];
    char formatted[2 * BQ_PHASE_DIGEST_BYTES + 1];
    BQ_CHECK(bq_phase_digest_parse(BQ_TEST_PHASE_READY_HEX, ready) &&
             bq_phase_digest_parse(BQ_TEST_PHASE_OTHER_HEX, other) && ready[0] == 0x01 && ready[31] == 0xef);
    bq_phase_digest_format(ready, formatted);
    BQ_CHECK(!strcmp(formatted, BQ_TEST_PHASE_READY_HEX));
    /* Upper case, a short or long spelling, a non-digit and the zero digest. */
    char const* refused[] = {"", "0123456789ABCDEF0123456789abcdef0123456789abcdef0123456789abcdef",
                             "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcde",
                             "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0",
                             "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdeg",
                             "0000000000000000000000000000000000000000000000000000000000000000"};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(refused); index += 1)
    {
        memset(scratch, 0x5a, sizeof(scratch));
        bool parsed = bq_phase_digest_parse(refused[index], scratch);
        unsigned char any = 0;
        for (u32 byte = 0; byte < sizeof(scratch); byte += 1) any |= scratch[byte];
        BQ_CHECK(!parsed && !any);
    }
    BQ_CHECK(!bq_phase_digest_parse(NULL, scratch));

    int pair[2] = {-1, -1};
    BQ_CHECK(socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair) == 0);
    BqPhaseChannel sender, receiver, smoke, invalid;
    BQ_CHECK(bq_phase_init_version(&sender, pair[0], 7, 9, BQ_PHASE_VERSION_2) && sender.version == 2 &&
             bq_phase_init_version(&receiver, pair[1], 7, 9, BQ_PHASE_VERSION_2) &&
             bq_phase_init(&smoke, pair[1], 7, 9) && smoke.version == BQ_PHASE_VERSION_1);
    BQ_CHECK(!bq_phase_init_version(&invalid, pair[0], 7, 9, 0) && invalid.failed &&
             !bq_phase_init_version(&invalid, pair[0], 7, 9, 3) && invalid.failed);
    unsigned const order[] = {BQ_PHASE_PREPARING, BQ_PHASE_RETIREMENT_READY, BQ_PHASE_SETTLING, BQ_PHASE_MEASURING,
                              BQ_PHASE_MEASURED};
    unsigned char message[BQ_PHASE_MESSAGE_CAP], copy[BQ_PHASE_MESSAGE_CAP + 1];
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(order); index += 1)
    {
        unsigned phase = order[index];
        bool carried = phase == BQ_PHASE_RETIREMENT_READY || phase == BQ_PHASE_MEASURED;
        unsigned char const* digest = phase == BQ_PHASE_RETIREMENT_READY ? ready : phase == BQ_PHASE_MEASURED ? other : NULL;
        BQ_CHECK(bq_phase_next(BQ_PHASE_VERSION_2, sender.sequence) == phase &&
                 bq_phase_digest_carried(BQ_PHASE_VERSION_2, phase) == carried &&
                 !bq_phase_digest_carried(BQ_PHASE_VERSION_1, phase));
        /* Out of order, a missing digest, an unwanted one or a zero one. */
        memset(scratch, 0, sizeof(scratch));
        BQ_CHECK(bq_test_phase_only_next(&sender, phase, ready) &&
                 !bq_phase_make_digest(&sender, phase, carried ? NULL : ready, message) &&
                 (!carried || !bq_phase_make_digest(&sender, phase, scratch, message)));
        memset(message, 0xa5, sizeof(message));
        BQ_CHECK(bq_phase_make_digest(&sender, phase, digest, message) && bq_phase_check(&receiver, message));
        BQ_CHECK(!memcmp(message, "BQPHASE2", 8) && bq_phase_get(message + 8) == 7 &&
                 bq_phase_get(message + 16) == 9 && bq_phase_get(message + 24) == phase &&
                 bq_phase_get(message + 40) == 0);
        memset(scratch, 0, sizeof(scratch));
        BQ_CHECK(!memcmp(message + BQ_PHASE_DIGEST_OFFSET, digest ? digest : scratch, BQ_PHASE_DIGEST_BYTES));
        /* Every byte but the timestamp has one encoding; a carried digest
         * may be any nonzero value, but never zero. */
        for (unsigned byte = 0; byte < BQ_PHASE_V2_MESSAGE_BYTES; ++byte)
        {
            memcpy(copy, message, BQ_PHASE_V2_MESSAGE_BYTES);
            copy[byte] ^= 0x80;
            if ((byte < 32 || byte >= 40) && (!carried || byte < BQ_PHASE_DIGEST_OFFSET))
                BQ_CHECK(!bq_phase_check(&receiver, copy));
        }
        memcpy(copy, message, BQ_PHASE_V2_MESSAGE_BYTES);
        memset(copy + BQ_PHASE_DIGEST_OFFSET, 0, BQ_PHASE_DIGEST_BYTES);
        if (carried) BQ_CHECK(!bq_phase_check(&receiver, copy));
        /* Unknown phases and the other version's magic. */
        memcpy(copy, message, BQ_PHASE_V2_MESSAGE_BYTES);
        bq_phase_put(copy + 24, BQ_PHASE_RETIREMENT_READY + 1);
        BQ_CHECK(!bq_phase_check(&receiver, copy));
        bq_phase_put(copy + 24, 0);
        BQ_CHECK(!bq_phase_check(&receiver, copy));
        memcpy(copy, message, BQ_PHASE_V2_MESSAGE_BYTES);
        memcpy(copy, "BQPHASE1", 8);
        BQ_CHECK(!bq_phase_check(&receiver, copy));
        /* On the wire, only exactly 80 bytes are received. */
        for (unsigned length = BQ_PHASE_V2_MESSAGE_BYTES - 1; length <= BQ_PHASE_V2_MESSAGE_BYTES + 1; ++length)
        {
            memcpy(copy, message, BQ_PHASE_V2_MESSAGE_BYTES);
            copy[BQ_PHASE_V2_MESSAGE_BYTES] = 0;
            BQ_CHECK(send(pair[0], copy, length, MSG_NOSIGNAL) == (ssize_t)length);
            BQ_CHECK(bq_phase_receive_sized(pair[1], copy, BQ_PHASE_V2_MESSAGE_BYTES) ==
                     (length == BQ_PHASE_V2_MESSAGE_BYTES));
        }
        BQ_CHECK(send(pair[0], message, BQ_PHASE_MESSAGE_BYTES, MSG_NOSIGNAL) == BQ_PHASE_MESSAGE_BYTES &&
                 !bq_phase_receive_sized(pair[1], copy, BQ_PHASE_V2_MESSAGE_BYTES));
        BQ_CHECK(send(pair[0], message, BQ_PHASE_V2_MESSAGE_BYTES, MSG_NOSIGNAL) == BQ_PHASE_V2_MESSAGE_BYTES &&
                 !bq_phase_receive(pair[1], copy));
        /* As if acknowledged: both ends advance. */
        sender.sequence = receiver.sequence = phase;
        sender.last_time = receiver.last_time = bq_phase_get(message + 32);
    }
    /* After MEASURED nothing follows, and a replay is refused. */
    BQ_CHECK(bq_phase_next(BQ_PHASE_VERSION_2, BQ_PHASE_MEASURED) == 0 && bq_test_phase_only_next(&sender, 0, ready) &&
             !bq_phase_check(&receiver, message));
    /* The v2 packet is refused by a BQPHASE1 channel, and v1 knows no
     * RETIREMENT_READY and no digest. */
    BqPhaseChannel fresh;
    BQ_CHECK(bq_phase_init_version(&fresh, pair[0], 7, 9, BQ_PHASE_VERSION_2) &&
             bq_phase_make_digest(&fresh, BQ_PHASE_PREPARING, NULL, message) && !bq_phase_check(&smoke, message));
    BQ_CHECK(!bq_phase_make_digest(&smoke, BQ_PHASE_PREPARING, ready, message) &&
             bq_phase_next(BQ_PHASE_VERSION_1, BQ_PHASE_PREPARING) == BQ_PHASE_SETTLING &&
             bq_phase_next(BQ_PHASE_VERSION_2, BQ_PHASE_PREPARING) == BQ_PHASE_RETIREMENT_READY &&
             bq_phase_next(0, 0) == 0 && bq_phase_bytes(0) == 0 && bq_phase_bytes(3) == 0);
    /* BQPHASE1 is unchanged: 48 bytes, nothing written past them, the same
     * layout, and a v2 channel refuses it. */
    memset(message, 0xa5, sizeof(message));
    BQ_CHECK(bq_phase_make(&smoke, BQ_PHASE_PREPARING, message) && bq_phase_bytes(BQ_PHASE_VERSION_1) == 48);
    unsigned char expected[BQ_PHASE_MESSAGE_BYTES];
    memcpy(expected, "BQPHASE1", 8);
    bq_phase_put(expected + 8, 7);
    bq_phase_put(expected + 16, 9);
    bq_phase_put(expected + 24, BQ_PHASE_PREPARING);
    bq_phase_put(expected + 32, bq_phase_get(message + 32));
    bq_phase_put(expected + 40, 0);
    bool untouched = true;
    for (unsigned byte = BQ_PHASE_MESSAGE_BYTES; byte < sizeof(message); ++byte) untouched = untouched && message[byte] == 0xa5;
    BQ_CHECK(!memcmp(message, expected, sizeof(expected)) && untouched);
    BQ_CHECK(bq_phase_init_version(&fresh, pair[1], 7, 9, BQ_PHASE_VERSION_2) && !bq_phase_check(&fresh, message));
    BqPhaseChannel smoke_receiver;
    BQ_CHECK(bq_phase_init(&smoke_receiver, pair[1], 7, 9) && bq_phase_check(&smoke_receiver, message));
    close(pair[0]);
    close(pair[1]);
}

/* The acknowledgement window: the 5 s cap under the caller's deadline, except
 * a version-2 MEASURED, bounded by the deadline alone; and a v2 MEASURED that
 * is never acknowledged fails at that deadline. */
BUSTER_GLOBAL_LOCAL void bq_test_phase_ack_window(void)
{
    uint64_t const second = UINT64_C(1000000000), start = UINT64_C(100) * second;
    BqPhaseChannel smoke = {.version = BQ_PHASE_VERSION_1}, retirement = {.version = BQ_PHASE_VERSION_2};
    BQ_CHECK(bq_phase_ack_deadline(&smoke, BQ_PHASE_MEASURED, start, start + 3600 * second) == start + 5 * second &&
             bq_phase_ack_deadline(&smoke, BQ_PHASE_MEASURED, start, start + second) == start + second &&
             bq_phase_ack_deadline(&retirement, BQ_PHASE_MEASURED, start, start + 3600 * second) ==
                 start + 3600 * second &&
             bq_phase_ack_deadline(&retirement, BQ_PHASE_RETIREMENT_READY, start, start + 3600 * second) ==
                 start + 5 * second &&
             bq_phase_ack_deadline(&retirement, BQ_PHASE_SETTLING, start, start + second) == start + second &&
             bq_phase_ack_deadline(NULL, BQ_PHASE_MEASURED, start, start + 3600 * second) == start + 5 * second);
    int pair[2] = {-1, -1};
    BQ_CHECK(socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair) == 0);
    BqPhaseChannel channel;
    BQ_CHECK(bq_phase_init_version(&channel, pair[0], 7, 9, BQ_PHASE_VERSION_2));
    channel.sequence = BQ_PHASE_MEASURING;
    unsigned char packet[BQ_PHASE_MESSAGE_CAP] = {0};
    /* A missing or malformed digest sends nothing. */
    uint64_t now = bq_phase_clock();
    BQ_CHECK(!bq_phase_exchange_until(&channel, BQ_PHASE_MEASURED, now + second) && channel.failed);
    channel.failed = 0;
    BQ_CHECK(!bq_phase_exchange_digest_until(&channel, BQ_PHASE_MEASURED, "not-a-digest", now + second) &&
             channel.failed);
    errno = 0;
    BQ_CHECK(recv(pair[1], packet, sizeof(packet), MSG_DONTWAIT) < 0 && (errno == EAGAIN || errno == EWOULDBLOCK));
    channel.failed = 0;
    uint64_t begin = bq_phase_clock();
    BQ_CHECK(!bq_phase_exchange_digest_until(&channel, BQ_PHASE_MEASURED, BQ_TEST_PHASE_OTHER_HEX,
                                             begin + UINT64_C(300000000)) && channel.failed &&
             channel.sequence == BQ_PHASE_MEASURING);
    uint64_t elapsed = bq_phase_clock() - begin;
    unsigned char digest[BQ_PHASE_DIGEST_BYTES];
    BQ_CHECK(elapsed >= UINT64_C(250000000) && elapsed < 2 * second &&
             recv(pair[1], packet, sizeof(packet) + 1, MSG_DONTWAIT) == BQ_PHASE_V2_MESSAGE_BYTES &&
             bq_phase_get(packet + 24) == BQ_PHASE_MEASURED && bq_phase_digest_parse(BQ_TEST_PHASE_OTHER_HEX, digest) &&
             !memcmp(packet + BQ_PHASE_DIGEST_OFFSET, digest, sizeof(digest)));
    close(pair[0]);
    close(pair[1]);
}

/* The coordinator's own facts in the retirement fixture: its A digest and the
 * seams' row-plan pin. */
#define BQ_TEST_PHASE_PREPARATION_HEX "a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0"
#define BQ_TEST_PHASE_ROW_PLAN_HEX "b1b1b1b1b1b1b1b1b1b1b1b1b1b1b1b1b1b1b1b1b1b1b1b1b1b1b1b1b1b1b1b1"

/* The number of authority copies and journals in the queue-private root, which
 * this also removes. */
BUSTER_GLOBAL_LOCAL u32 bq_test_phase_queue_authority_drain(int queue_directory)
{
    int root = openat(queue_directory, BQ_RETIREMENT_COORDINATOR_QUEUE_AUTHORITY, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    DIR* stream = root >= 0 ? fdopendir(dup(root)) : NULL;
    u32 found = 0;
    for (struct dirent* entry = stream ? readdir(stream) : NULL; entry; entry = readdir(stream))
    {
        if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, ".."))
        {
            found += !strncmp(entry->d_name, "authority-", 10);
            unlinkat(root, entry->d_name, 0);
        }
    }
    if (stream) closedir(stream);
    if (root >= 0)
    {
        close(root);
        unlinkat(queue_directory, BQ_RETIREMENT_COORDINATOR_QUEUE_AUTHORITY, AT_REMOVEDIR);
    }
    return found;
}

/* A stub retirement producer on a BQPHASE2 channel against the coordinator's
 * bq_worker_phase_join. defect: 0 success; 1 a MEASURED digest naming no
 * authority; 2 the execution deadline already reached at MEASURED; 3 reached
 * during the handoff; 4 a BQPHASE1 packet; 5 a zero ready digest; and a
 * self-consistent authority whose context chain names 6 another ready digest,
 * 7 another A digest or 8 another row plan, or 9 has no chain. */
BUSTER_GLOBAL_LOCAL void bq_test_phase_retirement(unsigned defect)
{
    BqWorkerFixture fixture;
    if (bq_test_worker_begin(&fixture, BQ_WORKER_SUCCEEDED, false))
    {
        BqQueue* queue = &fixture.material.queue.queue;
        BqRequest request = bq_test_real_request(230 + defect);
        u64 id = 0, token = 0;
        BQ_CHECK(bq_submit(queue, &request, &id) == BQ_OK &&
                 bq_materialize(queue, fixture.config.installed_root, fixture.config.workspace_root, &id, &token) == BQ_OK);
        BqJob* job = bq_job(&queue->state, id);
        BqRetirementWorkerUnitSeams seams = {.profile = S8("row-plan-sha256=" BQ_TEST_PHASE_ROW_PLAN_HEX "\n")};
        BqWorkerFinalization finalization = {.config = &fixture.config, .result_directory = -1,
                                             .execution_deadline = 1000, .phase_version = BQ_PHASE_VERSION_2,
                                             .retirement = &seams};
        memcpy(finalization.retirement_preparation_sha256, BQ_TEST_PHASE_PREPARATION_HEX, SHA256_HEX_CAPACITY);
        BQ_CHECK(bq_worker_result_open(&fixture.config, job, &finalization, true) == BQ_OK);
        char name[64], authority_sha256[SHA256_HEX_CAPACITY] = {0};
        int workspaces = open(fixture.material.workspaces, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        int attempt = workspaces >= 0 && bq_workspace_name(name, id, token) ?
                      openat(workspaces, name, O_RDONLY | O_DIRECTORY | O_CLOEXEC) : -1;
        BQ_CHECK(attempt >= 0 && bq_coordinator_fixture_authority(finalization.result_directory, attempt, id, token,
                     defect == 7 ? BQ_TEST_PHASE_OTHER_HEX : BQ_TEST_PHASE_PREPARATION_HEX,
                     defect == 6 ? BQ_TEST_PHASE_OTHER_HEX : BQ_TEST_PHASE_READY_HEX,
                     defect == 8 ? BQ_TEST_PHASE_OTHER_HEX : BQ_TEST_PHASE_ROW_PLAN_HEX, defect != 9, authority_sha256));
        int pair[2] = {-1, -1};
        BQ_CHECK(socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair) == 0);
        BqPhaseChannel phases;
        BQ_CHECK(bq_phase_init_version(&phases, pair[0], id, token, BQ_PHASE_VERSION_2));
        pid_t child = fork();
        if (child == 0)
        {
            close(pair[0]);
            BqPhaseChannel client;
            bool ok = bq_phase_init_version(&client, pair[1], id, token, BQ_PHASE_VERSION_2);
            unsigned const order[] = {BQ_PHASE_PREPARING, BQ_PHASE_RETIREMENT_READY, BQ_PHASE_SETTLING,
                                      BQ_PHASE_MEASURING, BQ_PHASE_MEASURED};
            for (u32 index = 0; ok && index < BUSTER_ARRAY_LENGTH(order); index += 1)
            {
                unsigned phase = order[index];
                char const* digest = phase == BQ_PHASE_RETIREMENT_READY ? BQ_TEST_PHASE_READY_HEX :
                                     phase == BQ_PHASE_MEASURED ? (defect == 1 ? BQ_TEST_PHASE_OTHER_HEX : authority_sha256) :
                                     NULL;
                if ((defect == 4 && phase == BQ_PHASE_PREPARING) || (defect == 5 && phase == BQ_PHASE_RETIREMENT_READY))
                {
                    /* A BQPHASE1 packet, or a READY whose digest is zero. */
                    unsigned char bad[BQ_PHASE_MESSAGE_CAP] = {0}, nonzero[BQ_PHASE_DIGEST_BYTES] = {1};
                    BqPhaseChannel smoke = client;
                    smoke.version = BQ_PHASE_VERSION_1;
                    ok = defect == 4 ? bq_phase_make(&smoke, phase, bad) : bq_phase_make_digest(&client, phase, nonzero, bad);
                    if (defect == 5) memset(bad + BQ_PHASE_DIGEST_OFFSET, 0, BQ_PHASE_DIGEST_BYTES);
                    unsigned size = defect == 4 ? BQ_PHASE_MESSAGE_BYTES : BQ_PHASE_V2_MESSAGE_BYTES;
                    ok = ok && send(pair[1], bad, size, MSG_NOSIGNAL) == (ssize_t)size;
                    break;
                }
                ok = bq_phase_exchange_digest_until(&client, phase, digest, bq_phase_clock() + UINT64_C(3000000000));
            }
            close(pair[1]);
            _exit(ok ? 0 : 1);
        }
        BQ_CHECK(child > 0);
        close(pair[1]);
        if (defect == 2) fixture.fake.elapsed = 1000;
        bq_test_phase_deadline_clock_calls = 0;
        if (defect == 3) fixture.backend.clock = bq_test_phase_expired_clock;
        BqSystemdContext context = {.pid = child};
        int status = 0;
        BqError result = bq_worker_phase_join(queue, &context, &phases, &status,
                                              bq_worker_deadline(bq_worker_monotonic_milliseconds(), 5000), &finalization);
        BqError expected = defect == 0 ? BQ_OK : defect == 2 || defect == 3 ? BQ_WORKER_TIMEOUT : BQ_WORKER_MISMATCH;
        if (result != expected) fprintf(stderr, "BQ_TEST phase retirement defect %u: %d\n", defect, (int)result);
        BQ_CHECK(result == expected);
        if (context.pid > 0)
        {
            kill(child, SIGKILL);
            BQ_CHECK(waitpid(child, &status, 0) == child);
        }
        else BQ_CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
        close(pair[0]);
        job = bq_job(&queue->state, id);
        /* The ready digest is kept once acknowledged; the authority only after
         * its handoff and within the deadline. */
        bool ready_kept = !strcmp(finalization.retirement_ready_sha256, BQ_TEST_PHASE_READY_HEX);
        BQ_CHECK(ready_kept == (defect != 4 && defect != 5) &&
                 (finalization.retirement_authority_sha256[0] != 0) == (defect == 0) &&
                 (defect != 0 || !strcmp(finalization.retirement_authority_sha256, authority_sha256)));
        /* The durable records recovery reloads: READY's once acknowledged,
         * MEASURED's only after a completed handoff. */
        char durable[SHA256_HEX_CAPACITY] = {0};
        BQ_CHECK(bq_worker_phase_record_digest(queue, job, BQ_PHASE_RETIREMENT_READY, durable) == ready_kept &&
                 (!ready_kept || !strcmp(durable, BQ_TEST_PHASE_READY_HEX)));
        BQ_CHECK(bq_worker_phase_record_digest(queue, job, BQ_PHASE_MEASURED, durable) == (defect == 0) &&
                 (defect != 0 || !strcmp(durable, authority_sha256)));
        /* A record is read only as this attempt's. */
        BqJob other = *job;
        other.token += 1;
        BQ_CHECK(!bq_worker_phase_record_digest(queue, &other, BQ_PHASE_RETIREMENT_READY, durable) && !durable[0]);
        if (defect == 0)
        {
            /* The finalization check: the journalled authority, bound to the
             * coordinator's facts, and nothing else. */
            String8 root = fixture.config.workspace_root;
            BQ_CHECK(bq_retirement_coordinator_authority_complete(finalization.result_directory, root,
                         queue->directory_fd, id, token, seams.profile, BQ_TEST_PHASE_PREPARATION_HEX,
                         BQ_TEST_PHASE_READY_HEX, authority_sha256) == BQ_OK);
            BQ_CHECK(bq_retirement_coordinator_authority_complete(finalization.result_directory, root,
                         queue->directory_fd, id, token, seams.profile, BQ_TEST_PHASE_PREPARATION_HEX,
                         BQ_TEST_PHASE_READY_HEX, BQ_TEST_PHASE_OTHER_HEX) != BQ_OK &&
                     bq_retirement_coordinator_authority_complete(finalization.result_directory, root,
                         queue->directory_fd, id, token, seams.profile, BQ_TEST_PHASE_PREPARATION_HEX,
                         BQ_TEST_PHASE_OTHER_HEX, authority_sha256) != BQ_OK &&
                     bq_retirement_coordinator_authority_complete(finalization.result_directory, root,
                         queue->directory_fd, id, token, seams.profile, BQ_TEST_PHASE_OTHER_HEX,
                         BQ_TEST_PHASE_READY_HEX, authority_sha256) != BQ_OK);
            char body[512] = {0};
            u32 size = 0;
            BQ_CHECK(job && job->phase == BQ_MEASURING && bq_worker_phases_validate(queue, job, &finalization) == BQ_OK &&
                     bq_worker_result_control_read(finalization.result_directory, "worker-phase-5", body,
                                                   sizeof(body) - 1, &size) &&
                     strstr(body, "protocol=BQPHASE2\n") &&
                     strstr(body, "digest-sha256=" BQ_TEST_PHASE_READY_HEX "\n"));
            BQ_CHECK(unlinkat(finalization.result_directory, "worker-phase-5", 0) == 0 &&
                     bq_worker_phases_validate(queue, job, &finalization) != BQ_OK);
        }
        /* The copy and its journal exist only when the handoff ran; a refused
         * chain leaves not even the queue-private root. */
        struct stat info = {0};
        bool rooted = fstatat(queue->directory_fd, BQ_RETIREMENT_COORDINATOR_QUEUE_AUTHORITY, &info, 0) == 0;
        u32 copies = bq_test_phase_queue_authority_drain(queue->directory_fd);
        BQ_CHECK(copies == (defect == 0 || defect == 3 ? 2u : 0u) && (defect < 6 || !rooted));
        if (attempt >= 0) close(attempt);
        if (workspaces >= 0) close(workspaces);
        close(finalization.result_directory);
        bq_worker_cancel_signal = 0;
        bq_test_worker_end(&fixture);
    }
}

BUSTER_GLOBAL_LOCAL u32 bq_test_phase_deadline_clock_calls;

BUSTER_GLOBAL_LOCAL u64 bq_test_phase_expired_clock(BqWorkerBackend* backend)
{
    BqWorkerFake* fake = backend->context;
    bq_test_phase_deadline_clock_calls += 1;
    if (bq_test_phase_deadline_clock_calls == 2) fake->elapsed = 3600000;
    return fake->elapsed;
}

/* Model materialization consuming the entire fixed runtime without waiting an
 * hour: no unit is launched, failure is durable, and admission is released. */
BUSTER_GLOBAL_LOCAL void bq_test_phase_prelaunch_deadline(void)
{
    BqWorkerFixture fixture;
    if (bq_test_worker_begin(&fixture, BQ_WORKER_SUCCEEDED, false))
    {
        BqQueue* queue = &fixture.material.queue.queue;
        BqRequest request = bq_test_real_request(218);
        u64 id = 0;
        bq_test_phase_deadline_clock_calls = 0;
        fixture.backend.clock = bq_test_phase_expired_clock;
        BQ_CHECK(bq_submit(queue, &request, &id) == BQ_OK);
        BQ_CHECK(bq_worker_run(queue, &fixture.config, &id) == BQ_WORKER_TIMEOUT);
        BqJob* job = bq_job(&queue->state, id);
        BQ_CHECK(job && job->phase == BQ_FINISHED && job->outcome == BQ_FAILED &&
                 bq_failure_evidence(queue, job) == BQ_WORKER_TIMEOUT &&
                 fixture.fake.starts == 0 && !queue->needs_reconciliation &&
                 !queue->state.active_id && !bq_test_worker_probe_locked(fixture.lease));
        BqRequest second = bq_test_real_request(219);
        u64 next = 0, token = 0;
        BQ_CHECK(bq_submit(queue, &second, &next) == BQ_OK &&
                 bq_reserve(queue, &next, &token) == BQ_OK && next != id);
        bq_test_worker_end(&fixture);
    }
}

/* Validation can cross the same fixed deadline even after the child has
 * exited cleanly. A valid preexisting bundle cannot turn that into success. */
BUSTER_GLOBAL_LOCAL void bq_test_phase_finalization_deadline(void)
{
    for (unsigned delayed = 0; delayed < 2; delayed += 1)
    {
        BqWorkerFixture fixture;
        if (bq_test_worker_begin(&fixture, BQ_WORKER_SUCCEEDED, false))
        {
            BqQueue* queue = &fixture.material.queue.queue;
            BqRequest request = bq_test_real_request(220 + delayed);
            u64 id = 0, token = 0;
            BQ_CHECK(bq_submit(queue, &request, &id) == BQ_OK &&
                     bq_materialize(queue, fixture.config.installed_root,
                                    fixture.config.workspace_root, &id, &token) == BQ_OK);
            BqJob* job = bq_job(&queue->state, id);
            fixture.config.production_path = true;
            BqWorkerFinalization finalization = {.config = &fixture.config, .result_directory = -1,
                                                 .execution_deadline = 1000};
            bool artifact = job && bq_test_worker_make_success_result(&fixture, job, &finalization);
            fixture.fake.elapsed = delayed ? 0 : 1000;
            bq_test_phase_deadline_clock_calls = 0;
            fixture.backend.clock = delayed ? bq_test_phase_expired_clock : fixture.backend.clock;
            BQ_CHECK(artifact && bq_worker_finish(queue, &fixture.config, job,
                                                   BQ_SUCCEEDED, BQ_NOT_FOUND, &finalization) == BQ_OK);
            BQ_CHECK(bq_worker_finalization_restore(&finalization) == BQ_OK);
            job = bq_job(&queue->state, id);
            BQ_CHECK(job && job->phase == BQ_FINISHED && job->outcome == BQ_FAILED &&
                     bq_failure_evidence(queue, job) == BQ_WORKER_TIMEOUT &&
                     !queue->state.active_id && !queue->needs_reconciliation);
            BqRequest next_request = bq_test_real_request(222 + delayed);
            u64 next = 0, next_token = 0;
            BQ_CHECK(bq_submit(queue, &next_request, &next) == BQ_OK &&
                     bq_reserve(queue, &next, &next_token) == BQ_OK && next != id);
            if (finalization.result_directory >= 0) close(finalization.result_directory);
            bq_test_worker_end(&fixture);
        }
    }
}

BUSTER_GLOBAL_LOCAL void bq_test_phase_run(unsigned defect, char const* driver)
{
    BqWorkerFixture fixture;
    if (bq_test_worker_begin(&fixture, BQ_WORKER_SUCCEEDED, false))
    {
        BqQueue* queue = &fixture.material.queue.queue;
        BqRequest request = bq_test_real_request(200 + defect);
        u64 id = 0, token = 0;
        BQ_CHECK(bq_submit(queue, &request, &id) == BQ_OK &&
                 bq_materialize(queue, fixture.config.installed_root, fixture.config.workspace_root, &id, &token) == BQ_OK);
        BqJob* job = bq_job(&queue->state, id);
        BqWorkerFinalization finalization = {.config = &fixture.config, .result_directory = -1};
        BQ_CHECK(bq_worker_result_open(&fixture.config, job, &finalization, true) == BQ_OK);
        if (defect == 7)
            BQ_CHECK(bq_worker_result_control_publish(&finalization, "worker-phase-1", "existing\n", 9, 0400) == BQ_OK);
        if (defect == 8)
        {
            char record[48];
            BQ_CHECK(bq_record_name(record, "worker-phase-1", id) &&
                     bq_record_write(queue, record, (u8 const*)"existing\n", 9, false) == BQ_OK);
        }
        int pair[2] = {-1, -1};
        BQ_CHECK(socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair) == 0);
        BqPhaseChannel phases;
        BQ_CHECK(bq_phase_init(&phases, pair[0], id, token));
        struct sigaction cancel = {.sa_handler = bq_worker_cancel_handler}, prior = {0};
        sigemptyset(&cancel.sa_mask);
        BQ_CHECK(sigaction(SIGUSR1, &cancel, &prior) == 0);
        pid_t parent = getpid();
        pid_t child = fork();
        if (child == 0)
        {
            close(pair[0]);
            if (defect == 6)
            {
                char job_text[32], token_text[32], phase_text[32];
                snprintf(job_text, sizeof(job_text), "%" PRIu64, (uint64_t)id);
                snprintf(token_text, sizeof(token_text), "%" PRIu64, (uint64_t)token);
                snprintf(phase_text, sizeof(phase_text), "%d", pair[1]);
                int flags = fcntl(pair[1], F_GETFD);
                if (flags < 0 || fcntl(pair[1], F_SETFD, flags & ~FD_CLOEXEC) != 0) _exit(125);
                char* arguments[] = {(char*)driver, (char*)"bench_service_recipe_self_test", job_text, token_text,
                    fixture.material.workspaces,
                    (char*)"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
                    (char*)"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
                    finalization.result_root, phase_text, NULL};
                execv(driver, arguments);
                _exit(126);
            }
            BqPhaseChannel client;
            bool ok = bq_phase_init(&client, pair[1], id, token) && (fcntl(pair[1], F_GETFD) & FD_CLOEXEC);
            for (unsigned phase = 1; ok && phase <= 4; ++phase)
            {
                if (defect == 7 || defect == 8)
                {
                    unsigned char pending[BQ_PHASE_MESSAGE_BYTES];
                    struct pollfd waiting = {.fd = pair[1], .events = POLLIN};
                    ok = bq_phase_make(&client, phase, pending) &&
                         send(pair[1], pending, sizeof(pending), MSG_NOSIGNAL) == sizeof(pending) &&
                         poll(&waiting, 1, 100) == 0;
                    break;
                }
                if (phase == 4 && defect >= 1 && defect <= 3)
                {
                    if (defect == 3) kill(parent, SIGUSR1);
                    if (defect >= 2) while (true) pause();
                    break;
                }
                if ((defect == 4 && phase == 1) || (defect == 5 && phase == 2))
                {
                    unsigned char bad[BQ_PHASE_MESSAGE_BYTES];
                    ok = bq_phase_make(&client, phase, bad);
                    bq_phase_put(bad + (defect == 4 ? 16 : 24), defect == 4 ? token + 1 : 1);
                    ok = ok && send(pair[1], bad, sizeof(bad), MSG_NOSIGNAL) == sizeof(bad);
                    break;
                }
                ok = bq_phase_exchange(&client, phase);
                char name[48], record[48];
                unsigned char retained[512];
                u32 size = 0;
                snprintf(name, sizeof(name), "worker-phase-%u", phase);
                struct stat info = {0};
                ok = ok && fstatat(finalization.result_directory, name, &info, AT_SYMLINK_NOFOLLOW) == 0 &&
                     S_ISREG(info.st_mode) && (info.st_mode & 0777) == 0400 && info.st_nlink == 1 &&
                     bq_record_name(record, name, id) &&
                     bq_record_read(queue, record, retained, sizeof(retained), &size) == BQ_OK && size > 0;
            }
            if (ok && defect == 9)
            {
                /* A worker may still be sealing after MEASURED. A replayed
                 * final packet during that window must poison the attempt. */
                unsigned char duplicate[BQ_PHASE_MESSAGE_BYTES] = {0};
                memcpy(duplicate, "BQPHASE1", 8);
                bq_phase_put(duplicate + 8, id);
                bq_phase_put(duplicate + 16, token);
                bq_phase_put(duplicate + 24, BQ_PHASE_MEASURED);
                bq_phase_put(duplicate + 32, client.last_time);
                ok = send(pair[1], duplicate, sizeof(duplicate), MSG_NOSIGNAL) == sizeof(duplicate);
            }
            close(pair[1]);
            _exit(ok ? 0 : 1);
        }
        BQ_CHECK(child > 0);
        close(pair[1]);
        BqSystemdContext context = {.pid = child};
        int status = 0;
        u64 before = bq_worker_monotonic_milliseconds();
        BqError result = bq_worker_phase_join(queue, &context, &phases, &status,
            bq_worker_deadline(before, defect == 2 ? 1000 : defect == 6 ? 30000 : 3000), &finalization);
        BqError expected = (defect == 7 || defect == 8) ? BQ_IO : defect == 0 || defect == 6 ? BQ_OK : defect == 2 ? BQ_WORKER_TIMEOUT :
                           defect == 3 ? BQ_WORKER_CANCEL_SIGNAL : BQ_WORKER_MISMATCH;
        BQ_CHECK(result == expected);
        BQ_CHECK(bq_worker_monotonic_milliseconds() - before < (defect == 6 ? 30000u : 3000u));
        if (context.pid > 0)
        {
            if (defect == 2 || defect == 3) kill(child, SIGKILL);
            BQ_CHECK(waitpid(child, &status, 0) == child);
        }
        if (defect != 2 && defect != 3) BQ_CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
        BQ_CHECK(sigaction(SIGUSR1, &prior, NULL) == 0);
        bq_worker_cancel_signal = 0;
        bq_worker_shutdown_signal = 0;
        close(pair[0]);
        if (defect == 0 || defect == 6)
        {
            BQ_CHECK(bq_worker_phases_validate(queue, job, &finalization) == BQ_OK);
            BQ_CHECK(unlinkat(finalization.result_directory, "worker-phase-3", 0) == 0);
            BQ_CHECK(bq_worker_phases_validate(queue, job, &finalization) != BQ_OK);
            BQ_CHECK(bq_worker_result_control_publish(&finalization, "worker-phase-3", "replaced\n", 9, 0400) == BQ_OK);
            BQ_CHECK(bq_worker_phases_validate(queue, job, &finalization) != BQ_OK);
        }
        close(finalization.result_directory);
        BqPhase expected_phase = (defect == 4 || defect == 5 || defect == 7 || defect == 8) ? BQ_PREPARING : BQ_MEASURING;
        BQ_CHECK(bq_job(&queue->state, id)->phase == expected_phase);
        /* Even a completed exchange is not a performance verdict or cleanup
         * proof. A restart remains fenced until ordinary recovery completes. */
        bq_close(queue);
        BQ_CHECK(bq_open(queue, fixture.material.queue.path) == BQ_OK);
        BQ_CHECK(queue->needs_reconciliation && bq_job(&queue->state, id)->phase == expected_phase &&
                 bq_job(&queue->state, id)->outcome == BQ_NO_OUTCOME);
        u64 next_id = 0, next_token = 0;
        BQ_CHECK(bq_reserve(queue, &next_id, &next_token) == BQ_RECONCILIATION_REQUIRED);
        bq_test_worker_end(&fixture);
    }
}
#endif
