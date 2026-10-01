/* Private supervisor/recipe phase protocol. The authenticated lease handoff
 * supplies the connected socket; no pathname, environment or request opens it.
 * The recipe waits for each durable acknowledgement before proceeding. Children
 * must never inherit this socket. This protocol proves supervisor quiescence.
 * Workload admission and the harness's own quiet behavior remain separate
 * gates.
 *
 * Two fixed-size versions exist. The channel's owner chooses one at
 * bq_phase_init_version; a packet never negotiates it:
 *   BQPHASE1 (48 bytes, the smoke recipe, unchanged): magic, job, attempt,
 *     phase, monotonic time, ack, each little-endian at 0/8/16/24/32/40. It
 *     carries no retirement receipt or acceptance authority.
 *   BQPHASE2 (80 bytes, the retirement recipe only, #881 PR 4): the same
 *     48-byte head under magic BQPHASE2, then a 32-byte SHA-256 digest at
 *     BQ_PHASE_DIGEST_OFFSET. The sequence is PREPARING, RETIREMENT_READY
 *     (the ready record's digest, sent right after bq_retirement_unit_ready),
 *     SETTLING, MEASURING, AA_MEASURED (#1021: the A/A stage's canonical
 *     row digest, sent once its sample shards are published, between A/A
 *     and A/B), MEASURED (the receipt authority's digest). Only
 *     RETIREMENT_READY, AA_MEASURED and MEASURED carry a digest, which must
 *     be nonzero; the other phases carry 32 zero bytes. The unit and the
 *     coordinator share a UID, so these digests never travel through files.
 *
 * Map: bq_phase_bytes (size per version), bq_phase_next (the only successor
 * of a sequence), bq_phase_digest_carried, bq_phase_digest_parse and
 * bq_phase_digest_format (strict lowercase hex), bq_phase_make_digest and
 * bq_phase_make (build), bq_phase_receive_sized and bq_phase_receive
 * (exact-size receive, ancillary descriptors closed), bq_phase_check (strict
 * parse against the channel), bq_phase_ack_deadline (the acknowledgement
 * window), bq_phase_exchange_digest_until, bq_phase_exchange_until and
 * bq_phase_exchange.
 */
#ifndef BUSTER_BENCH_SERVICE_PHASE_CHANNEL_H
#define BUSTER_BENCH_SERVICE_PHASE_CHANNEL_H
#ifdef __linux__
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define BQ_PHASE_MESSAGE_BYTES 48u
#define BQ_PHASE_V2_MESSAGE_BYTES 80u
#define BQ_PHASE_DIGEST_OFFSET 48u
#define BQ_PHASE_DIGEST_BYTES 32u
/* A buffer that holds a packet of either version. */
#define BQ_PHASE_MESSAGE_CAP BQ_PHASE_V2_MESSAGE_BYTES
#define BQ_PHASE_VERSION_1 1u
#define BQ_PHASE_VERSION_2 2u
#define BQ_PHASE_ACK_MILLISECONDS 5000u
#define BQ_PHASE_PREPARING 1u
#define BQ_PHASE_SETTLING 2u
#define BQ_PHASE_MEASURING 3u
#define BQ_PHASE_MEASURED 4u
/* Version 2 only, between PREPARING and SETTLING: the ready record's digest. */
#define BQ_PHASE_RETIREMENT_READY 5u
/* Version 2 only, between MEASURING and MEASURED (#1021): the A/A stage's
 * canonical row digest (lane D's raw numeric digest: the A/A sample shards'
 * `row-round-pair` then `group-round-pair` lines). The coordinator rehashes
 * the published A/A sample shards and journals it before acknowledging, and
 * A/B may start only after that acknowledgement. */
#define BQ_PHASE_AA_MEASURED 6u

typedef struct BqPhaseChannel
{
    int descriptor;
    unsigned sequence;
    int failed;
    uint64_t job, attempt, last_time;
    /* BQ_PHASE_VERSION_1 or _2 once initialized; 0 accepts no packet. */
    unsigned version;
} BqPhaseChannel;

static inline uint64_t bq_phase_clock(void)
{
    struct timespec time = {0};
    int ok = clock_gettime(CLOCK_MONOTONIC, &time) == 0 && time.tv_sec >= 0 &&
             (uint64_t)time.tv_sec <= (UINT64_MAX - (uint64_t)time.tv_nsec) / 1000000000;
    uint64_t result = ok ? (uint64_t)time.tv_sec * 1000000000 + (uint64_t)time.tv_nsec : 0;
    return result;
}

/* Convert the supervisor's absolute CLOCK_MONOTONIC millisecond deadline to
 * this channel's nanosecond domain without wrapping on hostile input. */
static inline bool bq_phase_deadline_from_milliseconds(uint64_t deadline_milliseconds,
                                                       uint64_t* deadline_nanoseconds)
{
    bool ok = deadline_nanoseconds && deadline_milliseconds <= UINT64_MAX / UINT64_C(1000000);
    if (deadline_nanoseconds)
        *deadline_nanoseconds = ok ? deadline_milliseconds * UINT64_C(1000000) : 0;
    return ok;
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

static inline unsigned bq_phase_bytes(unsigned version)
{
    unsigned bytes = version == BQ_PHASE_VERSION_2 ? BQ_PHASE_V2_MESSAGE_BYTES :
                     version == BQ_PHASE_VERSION_1 ? BQ_PHASE_MESSAGE_BYTES : 0;
    return bytes;
}

/* The only phase that may follow `sequence` (0 before PREPARING), or 0. */
static inline unsigned bq_phase_next(unsigned version, unsigned sequence)
{
    unsigned next = 0;
    if (version == BQ_PHASE_VERSION_1 && sequence < BQ_PHASE_MEASURED) next = sequence + 1;
    else if (version == BQ_PHASE_VERSION_2)
        next = sequence == 0 ? BQ_PHASE_PREPARING : sequence == BQ_PHASE_PREPARING ? BQ_PHASE_RETIREMENT_READY :
               sequence == BQ_PHASE_RETIREMENT_READY ? BQ_PHASE_SETTLING :
               sequence == BQ_PHASE_SETTLING ? BQ_PHASE_MEASURING :
               sequence == BQ_PHASE_MEASURING ? BQ_PHASE_AA_MEASURED :
               sequence == BQ_PHASE_AA_MEASURED ? BQ_PHASE_MEASURED : 0;
    return next;
}

static inline bool bq_phase_digest_carried(unsigned version, unsigned phase)
{
    bool carried = version == BQ_PHASE_VERSION_2 &&
                   (phase == BQ_PHASE_RETIREMENT_READY || phase == BQ_PHASE_AA_MEASURED || phase == BQ_PHASE_MEASURED);
    return carried;
}

/* Exactly 64 lowercase hex digits to 32 bytes. Any other spelling and the
 * all-zero digest, which names no record, are refused. */
static inline bool bq_phase_digest_parse(char const* hex, unsigned char digest[BQ_PHASE_DIGEST_BYTES])
{
    bool ok = hex && digest && strnlen(hex, 2 * BQ_PHASE_DIGEST_BYTES + 1) == 2 * BQ_PHASE_DIGEST_BYTES;
    unsigned char any = 0;
    for (unsigned i = 0; ok && i < 2 * BQ_PHASE_DIGEST_BYTES; ++i)
    {
        char c = hex[i];
        unsigned value = c >= '0' && c <= '9' ? (unsigned)(c - '0') :
                         c >= 'a' && c <= 'f' ? (unsigned)(c - 'a' + 10) : 16u;
        ok = value < 16u;
        if (ok && i % 2 == 0) digest[i / 2] = (unsigned char)(value << 4);
        else if (ok) digest[i / 2] = (unsigned char)(digest[i / 2] | value);
        if (ok && i % 2) any |= digest[i / 2];
    }
    ok = ok && any != 0;
    if (!ok && digest) memset(digest, 0, BQ_PHASE_DIGEST_BYTES);
    return ok;
}

static inline void bq_phase_digest_format(unsigned char const digest[BQ_PHASE_DIGEST_BYTES],
                                          char hex[2 * BQ_PHASE_DIGEST_BYTES + 1])
{
    static char const digits[] = "0123456789abcdef";
    for (unsigned i = 0; i < BQ_PHASE_DIGEST_BYTES; ++i)
    {
        hex[2 * i] = digits[digest[i] >> 4];
        hex[2 * i + 1] = digits[digest[i] & 15u];
    }
    hex[2 * BQ_PHASE_DIGEST_BYTES] = 0;
}

/* Version 1 has no digest field. In version 2 a carried digest is nonzero
 * and every other phase carries 32 zero bytes. */
static inline bool bq_phase_digest_valid(unsigned char const* message, unsigned version, unsigned phase)
{
    unsigned char any = 0;
    for (unsigned i = 0; version == BQ_PHASE_VERSION_2 && i < BQ_PHASE_DIGEST_BYTES; ++i)
        any |= message[BQ_PHASE_DIGEST_OFFSET + i];
    bool valid = version == BQ_PHASE_VERSION_1 ||
                 (version == BQ_PHASE_VERSION_2 && (bq_phase_digest_carried(version, phase) ? any != 0 : any == 0));
    return valid;
}

static inline int bq_phase_init_version(BqPhaseChannel* channel, int descriptor, uint64_t job, uint64_t attempt,
                                        unsigned version)
{
    int type = 0;
    socklen_t size = sizeof(type);
    int flags = descriptor >= 3 ? fcntl(descriptor, F_GETFD) : -1;
    int ok = channel && job && attempt && flags >= 0 && bq_phase_bytes(version) &&
             getsockopt(descriptor, SOL_SOCKET, SO_TYPE, &type, &size) == 0 &&
             size == sizeof(type) && type == SOCK_SEQPACKET &&
             fcntl(descriptor, F_SETFD, flags | FD_CLOEXEC) == 0;
    if (channel)
        *channel = (BqPhaseChannel){.descriptor = descriptor, .job = job, .attempt = attempt, .failed = !ok,
                                    .version = version};
    return ok;
}

static inline int bq_phase_init(BqPhaseChannel* channel, int descriptor, uint64_t job, uint64_t attempt)
{
    int ok = bq_phase_init_version(channel, descriptor, job, attempt, BQ_PHASE_VERSION_1);
    return ok;
}

/* message holds bq_phase_bytes(channel->version) bytes. digest (32 bytes) is
 * required exactly for the phases that carry one. */
static inline int bq_phase_make_digest(BqPhaseChannel const* channel, unsigned phase,
                                       unsigned char const* digest, unsigned char* message)
{
    uint64_t now = bq_phase_clock();
    unsigned version = channel ? channel->version : 0;
    unsigned char any = 0;
    for (unsigned i = 0; digest && i < BQ_PHASE_DIGEST_BYTES; ++i) any |= digest[i];
    int ok = channel && message && !channel->failed && phase && phase == bq_phase_next(version, channel->sequence) &&
             now > channel->last_time && (bq_phase_digest_carried(version, phase) ? digest && any : !digest);
    if (ok)
    {
        memcpy(message, version == BQ_PHASE_VERSION_2 ? "BQPHASE2" : "BQPHASE1", 8);
        bq_phase_put(message + 8, channel->job);
        bq_phase_put(message + 16, channel->attempt);
        bq_phase_put(message + 24, phase);
        bq_phase_put(message + 32, now);
        bq_phase_put(message + 40, 0);
        if (version == BQ_PHASE_VERSION_2 && digest)
            memcpy(message + BQ_PHASE_DIGEST_OFFSET, digest, BQ_PHASE_DIGEST_BYTES);
        else if (version == BQ_PHASE_VERSION_2) memset(message + BQ_PHASE_DIGEST_OFFSET, 0, BQ_PHASE_DIGEST_BYTES);
    }
    return ok;
}

static inline int bq_phase_make(BqPhaseChannel const* channel, unsigned phase, unsigned char* message)
{
    int ok = bq_phase_make_digest(channel, phase, NULL, message);
    return ok;
}

/* Reject every packet but one of exactly `size` bytes, and ancillary
 * descriptors, closing every delivered fd even when the packet is malformed.
 * MSG_CMSG_CLOEXEC also covers rejection. */
static inline int bq_phase_receive_sized(int descriptor, unsigned char* bytes, unsigned size)
{
    unsigned char control[CMSG_SPACE(sizeof(int) * 16)] = {0};
    struct iovec vector = {bytes, size};
    struct msghdr message = {0};
    message.msg_iov = &vector;
    message.msg_iovlen = 1;
    message.msg_control = control;
    message.msg_controllen = sizeof(control);
    ssize_t count = size ? recvmsg(descriptor, &message, MSG_DONTWAIT | MSG_CMSG_CLOEXEC) : -1;
    int ok = size && count == (ssize_t)size && !(message.msg_flags & (MSG_TRUNC | MSG_CTRUNC));
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

static inline int bq_phase_receive(int descriptor, unsigned char bytes[BQ_PHASE_MESSAGE_BYTES])
{
    int ok = bq_phase_receive_sized(descriptor, bytes, BQ_PHASE_MESSAGE_BYTES);
    return ok;
}

/* message holds bq_phase_bytes(channel->version) bytes, a size that
 * bq_phase_receive_sized proved. It must carry the channel's magic, job and
 * attempt, the channel's only next phase, an increasing time no later than
 * now, no ack, and the digest rule of its version. */
static inline int bq_phase_check(BqPhaseChannel const* channel, unsigned char const* message)
{
    uint64_t phase = bq_phase_get(message + 24), time = bq_phase_get(message + 32);
    uint64_t now = bq_phase_clock();
    unsigned version = channel ? channel->version : 0;
    unsigned next = channel ? bq_phase_next(version, channel->sequence) : 0;
    int ok = channel && !channel->failed && next &&
             !memcmp(message, version == BQ_PHASE_VERSION_2 ? "BQPHASE2" : "BQPHASE1", 8) &&
             bq_phase_get(message + 8) == channel->job && bq_phase_get(message + 16) == channel->attempt &&
             phase == next && time > channel->last_time && now && time <= now && bq_phase_get(message + 40) == 0 &&
             bq_phase_digest_valid(message, version, next);
    return ok;
}

/* The acknowledgement deadline of one exchange that starts at `start`: the
 * 5 s per-ack cap under the caller's deadline, except for a version-2
 * AA_MEASURED or MEASURED. Before acknowledging AA_MEASURED the coordinator
 * rehashes every published A/A sample shard; before MEASURED it copies and
 * journals the receipt authority (tp_retirement_store_authority_handoff),
 * which reopens every retained file. Those waits are therefore bounded only
 * by the caller's deadline, the job's remaining execution budget. The smoke
 * recipe keeps the cap. */
static inline uint64_t bq_phase_ack_deadline(BqPhaseChannel const* channel, unsigned phase, uint64_t start,
                                             uint64_t deadline_ns)
{
    uint64_t ack_budget = (uint64_t)BQ_PHASE_ACK_MILLISECONDS * UINT64_C(1000000);
    uint64_t capped = start && ack_budget <= UINT64_MAX - start ? start + ack_budget : UINT64_MAX;
    bool whole = channel && channel->version == BQ_PHASE_VERSION_2 &&
                 (phase == BQ_PHASE_AA_MEASURED || phase == BQ_PHASE_MEASURED);
    uint64_t result = whole || deadline_ns < capped ? deadline_ns : capped;
    return result;
}

/* The caller's absolute monotonic deadline bounds the acknowledgement wait
 * (bq_phase_ack_deadline). digest_hex is the carried digest in lowercase hex.
 * It is required exactly for the phases that carry one. */
static inline int bq_phase_exchange_digest_until(BqPhaseChannel* channel, unsigned phase, char const* digest_hex,
                                                 uint64_t deadline_ns)
{
    unsigned char request[BQ_PHASE_MESSAGE_CAP] = {0}, response[BQ_PHASE_MESSAGE_CAP] = {0};
    unsigned char digest[BQ_PHASE_DIGEST_BYTES] = {0};
    unsigned size = channel ? bq_phase_bytes(channel->version) : 0;
    uint64_t start = bq_phase_clock();
    uint64_t ack_deadline = bq_phase_ack_deadline(channel, phase, start, deadline_ns);
    bool parsed = !digest_hex || bq_phase_digest_parse(digest_hex, digest);
    int ok = start && deadline_ns > start && size && parsed &&
             bq_phase_make_digest(channel, phase, digest_hex ? digest : NULL, request);
    if (ok) ok = bq_phase_clock() < ack_deadline;
    if (ok) ok = send(channel->descriptor, request, size, MSG_NOSIGNAL | MSG_DONTWAIT) == (ssize_t)size;
    struct pollfd wait = {.fd = channel ? channel->descriptor : -1, .events = POLLIN};
    bool acknowledged = false;
    while (ok && !acknowledged)
    {
        uint64_t now = bq_phase_clock();
        if (!now) ok = false;
        uint64_t remaining = now < ack_deadline ? ack_deadline - now : 0;
        uint64_t timeout_ms = remaining / UINT64_C(1000000) +
                              (remaining % UINT64_C(1000000) != 0);
        int timeout = timeout_ms > INT_MAX ? INT_MAX : (int)timeout_ms;
        int ready = ok && timeout ? poll(&wait, 1, timeout) : 0;
        if (ready < 0 && errno != EINTR) ok = false;
        else if (ready > 0)
        {
            acknowledged = (wait.revents & POLLIN) != 0;
            if (!acknowledged) ok = false;
        }
        else if (ok && !timeout) ok = false;
    }
    if (ok) ok = bq_phase_receive_sized(channel->descriptor, response, size);
    if (ok) ok = bq_phase_clock() < ack_deadline;
    if (ok)
    {
        bq_phase_put(request + 40, 1);
        ok = !memcmp(request, response, size);
    }
    if (ok)
    {
        channel->sequence = phase;
        channel->last_time = bq_phase_get(request + 32);
    }
    else if (channel) channel->failed = 1;
    return ok;
}

static inline int bq_phase_exchange_until(BqPhaseChannel* channel, unsigned phase, uint64_t deadline_ns)
{
    int ok = bq_phase_exchange_digest_until(channel, phase, NULL, deadline_ns);
    return ok;
}

static inline int bq_phase_exchange(BqPhaseChannel* channel, unsigned phase)
{
    uint64_t now = bq_phase_clock();
    uint64_t budget = (uint64_t)BQ_PHASE_ACK_MILLISECONDS * UINT64_C(1000000);
    uint64_t deadline = now ? (budget <= UINT64_MAX - now ? now + budget : UINT64_MAX) : 0;
    int ok = bq_phase_exchange_until(channel, phase, deadline);
    return ok;
}
#endif
#endif
