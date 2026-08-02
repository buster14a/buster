#include <buster/lib/time.h>
#include <buster/lib/os.h>
#include <buster/lib/system_headers.h>

TimeDataType timestamp_take(void)
{
#if defined(__linux__) || defined(__APPLE__)
    struct timespec ts;
    int result = clock_gettime(CLOCK_MONOTONIC, &ts);
    BUSTER_CHECK(result == 0);
    BUSTER_CT_CHECK(sizeof(TimeDataType) >= sizeof(struct timespec));
    TimeDataType timestamp = {0};
    memcpy(&timestamp, &ts, sizeof(ts));
    return timestamp;
#elif defined(_WIN32)
    LARGE_INTEGER c;
    BOOL result = QueryPerformanceCounter(&c);
    BUSTER_CHECK(result);
    return (TimeDataType)c.QuadPart;
#endif
}

u64 timestamp_ns_between(TimeDataType start, TimeDataType end)
{
#if defined(__linux__) || defined(__APPLE__)
    struct timespec start_ts;
    struct timespec end_ts;
    memcpy(&start_ts, &start, sizeof(start_ts));
    memcpy(&end_ts, &end, sizeof(end_ts));
    s64 second_diff = end_ts.tv_sec - start_ts.tv_sec;
    s64 ns_diff = end_ts.tv_nsec - start_ts.tv_nsec;

    u64 result = (u64)second_diff * 1000000000ULL + (u64)ns_diff;
    return result;
#elif defined(_WIN32)
    // Split the conversion so ticks * 1e9 cannot overflow 64 bits (a 10 MHz
    // counter would overflow after ~30 minutes); see os_now_microseconds.
    u64 ticks = end - start;
    u64 frequency = os_state.frequency;
    u64 whole_seconds = ticks / frequency;
    u64 remainder_ticks = ticks % frequency;
    u64 ns = whole_seconds * (u64)(1000 * 1000 * 1000) + (remainder_ticks * (u64)(1000 * 1000 * 1000)) / frequency;
    return ns;
#endif
}
