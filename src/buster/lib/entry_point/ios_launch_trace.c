// Native iOS launch observations, including before arenas/thread context exist.
// buster_ios_launch_trace owns fixed-size stderr records; wall time correlates
// with host receipt, monotonic/process CPU time attributes app work. No clocks
// here participate in launch admission or alter the launcher's deadline.
#include <buster/lib/entry_point.h>
#include <buster/lib/system_headers.h>
#include <stdio.h>
#include <stdlib.h>

void buster_ios_launch_trace(String8 stage)
{
    const char* enabled = getenv("BUSTER_IOS_LAUNCH_TRACE");
    if (enabled && enabled[0] == '1' && enabled[1] == 0 && stage.length <= 64)
    {
        struct timespec monotonic = {0};
        struct timespec wall = {0};
        struct rusage usage = {0};
        int monotonic_status = clock_gettime(CLOCK_MONOTONIC, &monotonic);
        int wall_status = clock_gettime(CLOCK_REALTIME, &wall);
        int cpu_status = getrusage(RUSAGE_SELF, &usage);
        u64 monotonic_us = (u64)monotonic.tv_sec * 1000000 + (u64)monotonic.tv_nsec / 1000;
        u64 wall_us = (u64)wall.tv_sec * 1000000 + (u64)wall.tv_nsec / 1000;
        u64 cpu_us = ((u64)usage.ru_utime.tv_sec + (u64)usage.ru_stime.tv_sec) * 1000000 +
                     (u64)usage.ru_utime.tv_usec + (u64)usage.ru_stime.tv_usec;
        char record[512];
        int length = snprintf(record, sizeof(record),
            "BUSTER_IOS_LAUNCH_V1 stage=%.*s pid=%ld monotonic_us=%llu wall_us=%llu process_cpu_us=%llu monotonic_status=%d wall_status=%d cpu_status=%d\n",
            (int)stage.length, (const char*)stage.pointer, (long)getpid(),
            (unsigned long long)monotonic_us, (unsigned long long)wall_us, (unsigned long long)cpu_us,
            monotonic_status, wall_status, cpu_status);
        if (length > 0 && (size_t)length < sizeof(record))
        {
            // One direct write avoids stdio buffering and runtime allocation.
            // A failed/partial write leaves unavailable evidence, never success.
            ssize_t written = write(STDERR_FILENO, record, (size_t)length);
            BUSTER_UNUSED(written);
        }
    }
}
