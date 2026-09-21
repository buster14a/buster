/* Private supervisor/recipe phase protocol. The authenticated lease handoff
 * supplies the connected socket; no pathname, environment or request opens it.
 * The recipe waits for each durable acknowledgement before proceeding. Children
 * must never inherit this socket. This protocol proves supervisor quiescence;
 * workload admission and the harness's own quiet behavior remain separate gates.
 */
#ifndef BUSTER_BENCH_SERVICE_PHASE_CHANNEL_H
#define BUSTER_BENCH_SERVICE_PHASE_CHANNEL_H
#ifdef __linux__
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define BQ_PHASE_MESSAGE_BYTES 48u
#define BQ_PHASE_ACK_MILLISECONDS 5000u
#define BQ_PHASE_PREPARING 1u
#define BQ_PHASE_SETTLING 2u
#define BQ_PHASE_MEASURING 3u
#define BQ_PHASE_MEASURED 4u

typedef struct BqPhaseChannel
{
    int descriptor;
    unsigned sequence;
    int failed;
    uint64_t job, attempt, last_time;
} BqPhaseChannel;

static inline uint64_t bq_phase_clock(void)
{
    struct timespec time = {0};
    int ok = clock_gettime(CLOCK_MONOTONIC, &time) == 0 && time.tv_sec >= 0 &&
             (uint64_t)time.tv_sec <= (UINT64_MAX - (uint64_t)time.tv_nsec) / 1000000000;
    uint64_t result = ok ? (uint64_t)time.tv_sec * 1000000000 + (uint64_t)time.tv_nsec : 0;
    return result;
}

static inline void bq_phase_put(unsigned char* bytes, uint64_t value)
{
    for (unsigned i = 0; i < 8; ++i) bytes[i] = (unsigned char)(value >> (i * 8));
}

static inline uint64_t bq_phase_get(unsigned char const* bytes)
{
    uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i) value |= (uint64_t)bytes[i] << (i * 8);
    return value;
}

static inline int bq_phase_init(BqPhaseChannel* channel, int descriptor, uint64_t job, uint64_t attempt)
{
    int type = 0;
    socklen_t size = sizeof(type);
    int flags = descriptor >= 3 ? fcntl(descriptor, F_GETFD) : -1;
    int ok = channel && job && attempt && flags >= 0 &&
             getsockopt(descriptor, SOL_SOCKET, SO_TYPE, &type, &size) == 0 &&
             size == sizeof(type) && type == SOCK_SEQPACKET &&
             fcntl(descriptor, F_SETFD, flags | FD_CLOEXEC) == 0;
    if (channel) *channel = (BqPhaseChannel){.descriptor = descriptor, .job = job, .attempt = attempt, .failed = !ok};
    return ok;
}

static inline int bq_phase_make(BqPhaseChannel const* channel, unsigned phase,
                                unsigned char message[BQ_PHASE_MESSAGE_BYTES])
{
    uint64_t now = bq_phase_clock();
    int ok = channel && !channel->failed && phase == channel->sequence + 1 &&
             phase >= BQ_PHASE_PREPARING && phase <= BQ_PHASE_MEASURED && now > channel->last_time;
    if (ok)
    {
        memcpy(message, "BQPHASE1", 8);
        bq_phase_put(message + 8, channel->job);
        bq_phase_put(message + 16, channel->attempt);
        bq_phase_put(message + 24, phase);
        bq_phase_put(message + 32, now);
        bq_phase_put(message + 40, 0);
    }
    return ok;
}

/* Reject oversized packets and ancillary descriptors, closing every delivered
 * fd even when the packet is malformed. MSG_CMSG_CLOEXEC also covers rejection. */
static inline int bq_phase_receive(int descriptor, unsigned char bytes[BQ_PHASE_MESSAGE_BYTES])
{
    unsigned char control[CMSG_SPACE(sizeof(int) * 16)] = {0};
    struct iovec vector = {bytes, BQ_PHASE_MESSAGE_BYTES};
    struct msghdr message = {0};
    message.msg_iov = &vector;
    message.msg_iovlen = 1;
    message.msg_control = control;
    message.msg_controllen = sizeof(control);
    ssize_t count = recvmsg(descriptor, &message, MSG_DONTWAIT | MSG_CMSG_CLOEXEC);
    int ok = count == BQ_PHASE_MESSAGE_BYTES && !(message.msg_flags & (MSG_TRUNC | MSG_CTRUNC));
    for (struct cmsghdr* header = count >= 0 ? CMSG_FIRSTHDR(&message) : NULL;
         header; header = CMSG_NXTHDR(&message, header))
    {
        ok = 0;
        if (header->cmsg_level == SOL_SOCKET && header->cmsg_type == SCM_RIGHTS &&
            header->cmsg_len >= CMSG_LEN(0))
        {
            size_t descriptors = (header->cmsg_len - CMSG_LEN(0)) / sizeof(int);
            for (size_t i = 0; i < descriptors; ++i)
            {
                int received = -1;
                memcpy(&received, (unsigned char*)CMSG_DATA(header) + i * sizeof(int), sizeof(int));
                if (received >= 0) close(received);
            }
        }
    }
    return ok;
}

static inline int bq_phase_check(BqPhaseChannel const* channel, unsigned char const* message)
{
    uint64_t phase = bq_phase_get(message + 24), time = bq_phase_get(message + 32);
    uint64_t now = bq_phase_clock();
    int ok = channel && !channel->failed && !memcmp(message, "BQPHASE1", 8) &&
             bq_phase_get(message + 8) == channel->job && bq_phase_get(message + 16) == channel->attempt &&
             phase == channel->sequence + 1 && phase <= BQ_PHASE_MEASURED &&
             time > channel->last_time && now && time <= now && bq_phase_get(message + 40) == 0;
    return ok;
}

static inline int bq_phase_exchange(BqPhaseChannel* channel, unsigned phase)
{
    unsigned char request[BQ_PHASE_MESSAGE_BYTES] = {0}, response[BQ_PHASE_MESSAGE_BYTES] = {0};
    int ok = bq_phase_make(channel, phase, request);
    if (ok) ok = send(channel->descriptor, request, sizeof(request), MSG_NOSIGNAL | MSG_DONTWAIT) == sizeof(request);
    struct pollfd wait = {.fd = channel ? channel->descriptor : -1, .events = POLLIN};
    if (ok) ok = poll(&wait, 1, BQ_PHASE_ACK_MILLISECONDS) > 0 && (wait.revents & POLLIN);
    if (ok) ok = bq_phase_receive(channel->descriptor, response);
    if (ok)
    {
        bq_phase_put(request + 40, 1);
        ok = !memcmp(request, response, sizeof(request));
    }
    if (ok)
    {
        channel->sequence = phase;
        channel->last_time = bq_phase_get(request + 32);
    }
    else if (channel) channel->failed = 1;
    return ok;
}
#endif
#endif
