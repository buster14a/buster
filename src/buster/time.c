#pragma once
#include <buster/time.h>

BUSTER_IMPL TimeDataType timestamp_take()
{
#if defined(__linux__) || defined(__APPLE__)
    struct timespec ts;
    let result = clock_gettime(CLOCK_MONOTONIC, &ts);
    BUSTER_CHECK(result == 0);
    return *(u128*)&ts;
#elif defined(_WIN32)
    LARGE_INTEGER c;
    BOOL result = QueryPerformanceCounter(&c);
    BUSTER_CHECK(result);
    return c.QuadPart;
#endif
}

BUSTER_IMPL u64 timestamp_ns_between(TimeDataType start, TimeDataType end)
{
#if defined(__linux__) || defined(__APPLE__)
    let start_ts = *(struct timespec*)&start;
    let end_ts = *(struct timespec*)&end;
    let second_diff = end_ts.tv_sec - start_ts.tv_sec;
    let ns_diff = end_ts.tv_nsec - start_ts.tv_nsec;

    let result = (u64)second_diff * 1000000000ULL + (u64)ns_diff;
    return result;
#elif defined(_WIN32)
    let ns = (f64)((end - start) * 1000 * 1000 * 1000) / frequency;
    return ns;
#endif
}
