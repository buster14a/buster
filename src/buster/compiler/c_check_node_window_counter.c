// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #3196. One pinned user-mode hardware event for one family's window sweeps in measure-node-machine.
// Pinning one event avoids multiplexing. The synchronous reader runs on the command's thread.
#ifndef BUSTER_C_CHECK_NODE_WINDOW_COUNTER_INCLUDED
#define BUSTER_C_CHECK_NODE_WINDOW_COUNTER_INCLUDED

#include <inttypes.h>
#include <linux/perf_event.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <x86intrin.h>

enum
{
    NODE_WINDOW_TESTS,
    NODE_WINDOW_CONTEXTS,
    NODE_WINDOW_ROWS,
    NODE_WINDOW_FAMILIES
};

static const char* const node_window_families[] = { "tests", "contexts", "rows" };
static const char* const node_window_parts[NODE_WINDOW_FAMILIES][7] = {
    { "bytes", "loop" },
    { "loop", "targets", "expressions" },
    { "reads", "shapes", "lists", "write", "values", "expressions", "summaries" },
};

typedef struct NodeWindowEvent NodeWindowEvent;
struct NodeWindowEvent
{
    const char* name;
    uint32_t type;
    uint64_t config;
};

static const NodeWindowEvent node_window_events[] = {
    { "cycles", PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES },
    { "instructions", PERF_TYPE_HARDWARE, PERF_COUNT_HW_INSTRUCTIONS },
    { "branches", PERF_TYPE_HARDWARE, PERF_COUNT_HW_BRANCH_INSTRUCTIONS },
    { "branch-misses", PERF_TYPE_HARDWARE, PERF_COUNT_HW_BRANCH_MISSES },
    { "frontend-stalls", PERF_TYPE_HARDWARE, PERF_COUNT_HW_STALLED_CYCLES_FRONTEND },
    { "l1d-misses", PERF_TYPE_HW_CACHE, PERF_COUNT_HW_CACHE_L1D | (PERF_COUNT_HW_CACHE_RESULT_MISS << 16) },
    { "l1i-misses", PERF_TYPE_HW_CACHE, PERF_COUNT_HW_CACHE_L1I | (PERF_COUNT_HW_CACHE_RESULT_MISS << 16) },
};

typedef struct NodeWindowCounter NodeWindowCounter;
struct NodeWindowCounter
{
    uint32_t family;
    const NodeWindowEvent* event;
    int descriptor;
    volatile struct perf_event_mmap_page* page;
    size_t page_size;
    bool failed;
    uint64_t overhead;
    uint64_t chunks;
    uint64_t records;
    uint64_t sums[2][BUSTER_ARRAY_LENGTH(node_window_parts[0])];
};

static _Thread_local NodeWindowCounter* node_window_counting;

static bool node_window_select(NodeWindowCounter* counter, const char* family, const char* event)
{
    counter->family = NODE_WINDOW_FAMILIES;
    counter->event = NULL;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(node_window_families); i += 1)
    {
        if (strcmp(family, node_window_families[i]) == 0)
        {
            counter->family = i;
        }
    }
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(node_window_events); i += 1)
    {
        if (strcmp(event, node_window_events[i].name) == 0)
        {
            counter->event = &node_window_events[i];
        }
    }
    return counter->family < NODE_WINDOW_FAMILIES && counter->event != NULL;
}

// Linux's perf mmap sequence counter protects the offset, hardware index and width across reschedules.
// Fences on both sides keep the sweep's instructions between its two reads. A failed pinned event
// invalidates the measurement, including a read returning EOF when the event cannot be scheduled.
static __attribute__((noinline)) uint64_t node_window_pmu(NodeWindowCounter* counter)
{
    uint64_t result = 0;
    volatile struct perf_event_mmap_page* page = counter->page;
    _mm_lfence();
    if (page != NULL && page->cap_user_rdpmc)
    {
        uint32_t sequence;
        uint32_t index;
        do
        {
            sequence = page->lock;
            __atomic_signal_fence(__ATOMIC_SEQ_CST);
            index = page->index;
            const uint32_t shift = 64u - page->pmc_width;
            const uint64_t offset = (uint64_t)page->offset;
            const uint64_t raw = index != 0 ? __rdpmc((int)index - 1) : 0;
            result = index != 0 ? offset + (uint64_t)((int64_t)(raw << shift) >> shift) : offset;
            __atomic_signal_fence(__ATOMIC_SEQ_CST);
        } while ((sequence & 1u) != 0 || sequence != page->lock);
        counter->failed |= index == 0;
    }
    else if (read(counter->descriptor, &result, sizeof(result)) != (ssize_t)sizeof(result))
    {
        counter->failed = true;
    }
    _mm_lfence();
    return result;
}

static inline uint64_t node_window_stamp(NodeWindowCounter* counter)
{
    return counter != NULL ? node_window_pmu(counter) : 0;
}

// Subtract the empty interval once per measured interval, after summing over chunks in the report.
static void node_window_collect(NodeWindowCounter* counter, unsigned turn, const uint64_t* points, uint32_t parts, uint32_t records)
{
    counter->chunks += turn == 0;
    counter->records += turn == 0 ? records : 0;
    for (uint32_t i = 0; i < parts; i += 1)
    {
        counter->failed |= points[i + 1] < points[i];
        counter->sums[turn][i] += points[i + 1] - points[i];
    }
}

// c_check_source_benchmark.c, in the checker unity unit.
static uint64_t check_source_median(const uint64_t* values, uint32_t count);

static bool node_window_pmu_open(NodeWindowCounter* counter)
{
    const struct perf_event_attr attribute = {
        .size = sizeof(attribute),
        .type = counter->event->type,
        .config = counter->event->config,
        .pinned = 1,
        .exclude_kernel = 1,
        .exclude_hv = 1,
    };
    counter->descriptor = (int)syscall(SYS_perf_event_open, &attribute, 0, -1, -1, PERF_FLAG_FD_CLOEXEC);
    if (counter->descriptor >= 0)
    {
        counter->page_size = (size_t)sysconf(_SC_PAGESIZE);
        void* page = mmap(NULL, counter->page_size, PROT_READ, MAP_SHARED, counter->descriptor, 0);
        counter->page = page != MAP_FAILED ? page : NULL;
        uint64_t samples[33];
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(samples); i += 1)
        {
            const uint64_t start = node_window_pmu(counter);
            (void)__rdtsc();
            samples[i] = node_window_pmu(counter) - start;
        }
        counter->overhead = check_source_median(samples, BUSTER_ARRAY_LENGTH(samples));
    }
    return counter->descriptor >= 0 && !counter->failed;
}

static void node_window_pmu_close(NodeWindowCounter* counter)
{
    if (counter->page != NULL)
    {
        munmap((void*)counter->page, counter->page_size);
    }
    if (counter->descriptor >= 0)
    {
        close(counter->descriptor);
    }
}

static void node_window_pmu_report(const NodeWindowCounter* counter, bool summaries)
{
    if (counter->chunks == 0)
    {
        printf("node-window-counter: family=%s event=%s chunks=0 records=0\n", node_window_families[counter->family], counter->event->name);
    }
    for (uint32_t part = 0; counter->chunks != 0 && part < BUSTER_ARRAY_LENGTH(node_window_parts[0]) && node_window_parts[counter->family][part] != NULL; part += 1)
    {
        if (counter->family != NODE_WINDOW_ROWS || part != 6 || summaries)
        {
            for (uint32_t turn = 0; turn < 2; turn += 1)
            {
                const uint64_t raw = counter->sums[turn][part];
                const uint64_t empty = counter->chunks * counter->overhead;
                const double adjusted = (double)raw - (double)empty;
                printf("node-window-counter: family=%s sweep=%s event=%s run=%s chunks=%" PRIu64 " records=%" PRIu64 " raw=%" PRIu64
                       " empty-per-chunk=%" PRIu64 " per-chunk=%.3f per-record=%.6f\n",
                       node_window_families[counter->family], node_window_parts[counter->family][part], counter->event->name, turn == 0 ? "first" : "second",
                       counter->chunks, counter->records, raw, counter->overhead, adjusted / (double)counter->chunks,
                       counter->records != 0 ? adjusted / (double)counter->records : 0.0);
            }
        }
    }
}

#endif
