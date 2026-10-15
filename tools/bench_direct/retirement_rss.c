// Native child-RSS observer for the fixed AST retirement profile (#4058).
// Exec this small image before forking: the compiler inherits this image's
// current residence, not Python/perf's historical high-water mark. The outer
// AST leg's native owner retains timeout and descendant cleanup authority;
// this helper keeps the inherited group and waits only for its exact child.
// Map: rss_transfer, rss_number, rss_hex, rss_observe, main.
// The 256-byte little-endian packet is decoded after the existing perf/wall
// span. No JSON formatting, compiler instrumentation or counter toggling runs
// here. The observer's fixed startup/fork/wait work remains in that span.
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <sys/wait.h>

enum { RSS_PACKET_BYTES = 256, RSS_BINDING_BYTES = 32, RSS_DIGEST_BYTES = 64 };

static int rss_transfer(int fd, unsigned char* bytes, size_t count, int writing)
{
    size_t at = 0;
    int valid = 1;
    while (valid && at < count)
    {
        ssize_t amount = writing ? write(fd, bytes + at, count - at) : read(fd, bytes + at, count - at);
        if (amount > 0) at += (size_t)amount;
        else if (amount < 0 && errno == EINTR) { }
        else valid = 0;
    }
    return valid;
}

static void rss_number(unsigned char* bytes, uint64_t number, unsigned count)
{
    for (unsigned i = 0; i < count; ++i) bytes[i] = (unsigned char)(number >> (i * 8));
}

static uint64_t rss_read_number(unsigned char const* bytes, unsigned count)
{
    uint64_t number = 0;
    for (unsigned i = 0; i < count; ++i) number |= (uint64_t)bytes[i] << (i * 8);
    return number;
}

static int rss_hex(char const* text, size_t count)
{
    int valid = strlen(text) == count;
    for (size_t i = 0; valid && i < count; ++i)
        valid = (text[i] >= '0' && text[i] <= '9') || (text[i] >= 'a' && text[i] <= 'f');
    return valid;
}

static int rss_observe(char const* output, char const* nonce, char const* binary, char const* argv_digest, char** argv)
{
    int result = 125;
    int fd = open(output, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    struct stat record;
    int valid = fd >= 0 && fstat(fd, &record) == 0 && S_ISREG(record.st_mode) && record.st_nlink == 1;
    int launch[2] = {-1, -1};
    struct sigaction children = {0};
    children.sa_handler = SIG_DFL;
    valid = valid && sigemptyset(&children.sa_mask) == 0 && sigaction(SIGCHLD, &children, 0) == 0;
    valid = valid && pipe2(launch, O_CLOEXEC) == 0;
    pid_t child = valid ? fork() : -1;
    if (child == 0)
    {
        close(fd);
        close(launch[0]);
        unsigned char before[16] = {0};
        struct rusage usage;
        int observed = getrusage(RUSAGE_SELF, &usage) == 0 && usage.ru_maxrss > 0;
        if (observed) rss_number(before, (uint64_t)usage.ru_maxrss, 8);
        rss_number(before + 8, (uint64_t)getpgrp(), 4);
        rss_number(before + 12, observed ? 0 : (uint64_t)(errno ? errno : EIO), 4);
        int sent = rss_transfer(launch[1], before, sizeof(before), 1);
        if (sent && observed)
        {
            execv(argv[0], argv);
            unsigned char failed[4];
            rss_number(failed, (uint64_t)errno, 4);
            (void)rss_transfer(launch[1], failed, sizeof(failed), 1);
        }
        _exit(sent && observed ? 127 : 125);
    }
    if (launch[1] >= 0) close(launch[1]);
    struct rusage usage = {0};
    int status = 0;
    pid_t waited = -1;
    int child_handle = -1;
    int child_owned = 0;
    if (child > 0)
    {
        child_handle = (int)syscall(SYS_pidfd_open, child, 0);
        child_owned = child_handle >= 0 && fcntl(child_handle, F_SETFD, FD_CLOEXEC) == 0;
        // SIGCHLD is default and no wait has occurred: a failed pidfd open
        // still leaves this exact child owned. Refuse rather than proceed
        // without a stable handle for ambiguous wait failures.
        if (!child_owned) (void)kill(child, SIGKILL);
        do { waited = wait4(child, &status, 0, &usage); } while (waited < 0 && errno == EINTR);
        if (waited < 0 && child_owned)
        {
            // wait4 can consume a child before reporting EFAULT. A pidfd
            // cannot accidentally signal a recycled numeric PID there.
            (void)syscall(SYS_pidfd_send_signal, child_handle, SIGKILL, 0, 0);
            pid_t cleaned;
            do { cleaned = wait4(child, &status, 0, &usage); } while (cleaned < 0 && errno == EINTR);
        }
    }
    if (child_handle >= 0) close(child_handle);
    unsigned char before[20] = {0};
    size_t count = 0;
    int complete = child_owned && waited == child;
    while (complete && count < sizeof(before))
    {
        ssize_t amount = read(launch[0], before + count, sizeof(before) - count);
        if (amount > 0) count += (size_t)amount;
        else if (amount == 0) break;
        else if (errno != EINTR) complete = 0;
    }
    if (launch[0] >= 0) close(launch[0]);
    if (complete && (count == 16 || count == 20))
    {
        unsigned char packet[RSS_PACKET_BYTES] = {0};
        memcpy(packet, "BSTRSS1", 7);
        rss_number(packet + 8, RSS_PACKET_BYTES, 4);
        rss_number(packet + 12, 1, 4);
        rss_number(packet + 16, (uint64_t)getpid(), 4);
        rss_number(packet + 20, (uint64_t)child, 4);
        rss_number(packet + 24, (uint64_t)waited, 4);
        rss_number(packet + 28, (uint64_t)getpgrp(), 4);
        memcpy(packet + 32, before + 8, 4);
        rss_number(packet + 36, (uint64_t)(unsigned)status, 4);
        uint64_t floor_error = rss_read_number(before + 12, 4);
        uint64_t exec_error = count == 20 ? rss_read_number(before + 16, 4) : 0;
        rss_number(packet + 40, floor_error ? floor_error : exec_error, 4);
        rss_number(packet + 44, floor_error ? 2 : exec_error ? 3 : 0, 4);
        rss_number(packet + 48, usage.ru_maxrss > 0 ? (uint64_t)usage.ru_maxrss : 0, 8);
        memcpy(packet + 56, before, 8);
        memcpy(packet + 64, nonce, RSS_BINDING_BYTES);
        memcpy(packet + 96, binary, RSS_DIGEST_BYTES);
        memcpy(packet + 160, argv_digest, RSS_DIGEST_BYTES);
        valid = rss_transfer(fd, packet, sizeof(packet), 1);
        if (valid && !floor_error && !exec_error)
            result = WIFEXITED(status) ? WEXITSTATUS(status) : WIFSIGNALED(status) ? 128 + WTERMSIG(status) : 125;
    }
    if (fd >= 0 && close(fd) != 0) result = 125;
    if (result == 125) fputs("retirement-rss: collector unavailable or failed\n", stderr);
    return result;
}
#endif

int main(int argc, char** argv)
{
    int result = 125;
#ifdef __linux__
    int valid = argc >= 11 && strcmp(argv[1], "--output") == 0 && strcmp(argv[3], "--nonce") == 0 &&
                strcmp(argv[5], "--binary-sha256") == 0 && strcmp(argv[7], "--argv-sha256") == 0 && strcmp(argv[9], "--") == 0;
    valid = valid && argv[10][0] == '/' && rss_hex(argv[4], RSS_BINDING_BYTES) &&
            rss_hex(argv[6], RSS_DIGEST_BYTES) && rss_hex(argv[8], RSS_DIGEST_BYTES);
    if (valid) result = rss_observe(argv[2], argv[4], argv[6], argv[8], argv + 10);
    else fputs("retirement-rss: expected exclusive output, nonce, binary/argv SHA256 and compiler argv\n", stderr);
#else
    (void)argc;
    (void)argv;
    fputs("retirement-rss: Linux wait4 observation is unavailable\n", stderr);
#endif
    return result;
}
