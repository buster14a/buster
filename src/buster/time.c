#include <buster/time.h>
#include <buster/os.h>
#include <buster/system_headers.h>

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
    u64 ns = (u64)((f64)((end - start) * 1000 * 1000 * 1000) / (f64)os_state.frequency);
    return ns;
#endif
}
