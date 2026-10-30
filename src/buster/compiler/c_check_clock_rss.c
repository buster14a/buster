// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Linux's current address-space high-water mark, not getrusage's peak across exec (#865).
// VmHWM is approximate kernel RSS accounting, not an exact census or a sample of current VmRSS.
// Read only at resource marks with --time-stages; no polling thread, allocations or default-path work.
// This bounded OS-text adapter is scalar; it does not process the compiler's source/token streams.

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#define BUSTER_CHECK_RSS_STATUS_CAPACITY 8192

// Exactly one complete VmHWM line, with an unsigned KiB count. Missing, malformed, duplicate and
// overflowing fields are unavailable, never a fallback to the inherited getrusage high-water mark.
static bool check_clock_peak_parse(const char* text, size_t length, uint64_t* peak_kb)
{
    uint64_t value = 0;
    uint32_t matches = 0;
    size_t at = 0;
    bool valid = text != NULL;
    while (valid && at < length)
    {
        const char* newline = memchr(text + at, '\n', length - at);
        const size_t end = newline != NULL ? (size_t)(newline - text) : length;
        if (end - at >= 6 && memcmp(text + at, "VmHWM:", 6) == 0)
        {
            matches += 1;
            size_t p = at + 6;
            while (p < end && (text[p] == ' ' || text[p] == '\t'))
            {
                p += 1;
            }
            const size_t first = p;
            value = 0;
            while (valid && p < end && text[p] >= '0' && text[p] <= '9')
            {
                const uint64_t digit = (uint64_t)(text[p] - '0');
                valid = value <= (UINT64_MAX - digit) / 10;
                value = valid ? value * 10 + digit : 0;
                p += 1;
            }
            const bool digits = p != first;
            const size_t separator = p;
            while (p < end && (text[p] == ' ' || text[p] == '\t'))
            {
                p += 1;
            }
            valid = valid && digits && p != separator && end - p >= 2 && memcmp(text + p, "kB", 2) == 0;
            p += valid ? 2 : 0;
            while (p < end && (text[p] == ' ' || text[p] == '\t'))
            {
                p += 1;
            }
            valid = valid && p == end && newline != NULL;
        }
        at = end + (end < length);
    }
    valid = valid && matches == 1;
    *peak_kb = valid ? value : 0;
    return valid;
}

// `path` is /proc/self/status in production; the parameter also lets tests exercise failed and
// truncated reads without changing the process's mount namespace. A too-large status is unavailable.
static bool check_clock_peak_read(const char* path, uint64_t* peak_kb)
{
    char text[BUSTER_CHECK_RSS_STATUS_CAPACITY];
    size_t used = 0;
    const int file = open(path, O_RDONLY | O_CLOEXEC);
    bool valid = file != -1;
    ssize_t got = 1;
    while (valid && got != 0 && used < sizeof(text))
    {
        got = read(file, text + used, sizeof(text) - used);
        if (got > 0)
        {
            used += (size_t)got;
        }
        else
        {
            valid = got == 0 || errno == EINTR;
        }
    }
    *peak_kb = 0;
    valid = valid && got == 0 && check_clock_peak_parse(text, used, peak_kb);
    if (file != -1)
    {
        close(file);
    }
    return valid;
}
