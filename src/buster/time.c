#pragma once
#include <buster/time.h>
#include <buster/system_headers.h>

TimeDataType timestamp_take(void)
{
#if defined(__linux__) || defined(__APPLE__)
    struct timespec ts;
    int result = clock_gettime(CLOCK_MONOTONIC, &ts);
    BUSTER_CHECK(result == 0);
    return *(u128*)&ts;
#elif defined(_WIN32)
    LARGE_INTEGER c;
    BOOL result = QueryPerformanceCounter(&c);
    BUSTER_CHECK(result);
    return c.QuadPart;
#endif
}

u64 timestamp_ns_between(TimeDataType start, TimeDataType end)
{
#if defined(__linux__) || defined(__APPLE__)
    struct timespec start_ts = *(struct timespec*)&start;
    struct timespec end_ts = *(struct timespec*)&end;
    s64 second_diff = end_ts.tv_sec - start_ts.tv_sec;
    s64 ns_diff = end_ts.tv_nsec - start_ts.tv_nsec;

    u64 result = (u64)second_diff * 1000000000ULL + (u64)ns_diff;
    return result;
#elif defined(_WIN32)
    let ns = (f64)((end - start) * 1000 * 1000 * 1000) / frequency;
    return ns;
#endif
}
