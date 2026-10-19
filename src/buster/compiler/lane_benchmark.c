// The lane benchmark: what lane_sync, lane_broadcast and lane_run cost on groups of 1 to 8 lanes, each lane on a CPU of
// its own, how fast lanes run after waiting at a sync, and what an arena's first touches cost.
//
// Usage: build/compiler benchmark-lanes [--lanes N] [--cpus LIST] [--rounds N]
//
// For each lane count from 1 to --lanes, lane i is pinned to the i-th CPU of --cpus: numbers and ranges separated by
// commas, such as 0-3,8. By default they are the CPUs this process may run on, in order; on the 9700X the first eight,
// 0 to 7, are its eight cores, since CPU 10 is offline and 8 to 15 are the SMT siblings of 0 to 7. --lanes is
// BUSTER_LANE_BENCHMARK_DEFAULT_LANES by default, or as many as there are CPUs if that is fewer. Each figure is the median of BUSTER_LANE_BENCHMARK_PASSES passes after a warm-up pass:
//   - sync: back-to-back lane_sync calls, --rounds per pass (BUSTER_LANE_BENCHMARK_DEFAULT_ROUNDS by default), timed by
//     lane 0 and divided by the rounds: what a sync costs when every lane arrives at once;
//   - broadcast: the same for lane_broadcast from lane 0;
//   - pthread: the same for pthread_barrier_wait, the RAD Debugger's barrier on Linux, over a sixteenth of the rounds
//     (BUSTER_LANE_BENCHMARK_PTHREAD_SHARE): it sleeps at every wait, so its rounds take far longer;
//   - wake: a sync at which lane 0 arrives late, from its arrival until the last lane has left: first
//     BUSTER_LANE_BENCHMARK_MWAITX_LATE_NANOSECONDS late, while the others wait in mwaitx, then
//     BUSTER_LANE_BENCHMARK_FUTEX_LATE_NANOSECONDS late, once they sleep on the futex; and how many times the lanes slept
//     per pass of the second;
//   - start: lane_run of an entry point that returns at once, without pinning, from the call until it returns.
// Then, on the largest group, what waiting costs afterwards. Each lane first measures its full speed at a busy loop (a
// dependent multiply and add per iteration) while every lane runs it. Then for each of lane_benchmark_waits, every pass,
// after every lane has worked for BUSTER_LANE_BENCHMARK_SETTLE_NANOSECONDS: lane 0 works alone for the wait while the
// others wait for it at a sync, and then every lane does BUSTER_LANE_BENCHMARK_WORK_NANOSECONDS of full-speed work. The
// figures are the wake, as above, lane 0's speed while alone, and how much longer than at full speed the slowest and the
// median waiting lane took for their work.
// Then, on the first CPU, the arenas: reserving and releasing a scratch arena's 64 GB, and in fresh arenas the first
// touch of the page at the start, of all the base pages before BUSTER_ARENA_SMALL_BYTES, and of the first huge page
// after them. The exit status is 1 if the arguments are invalid, a group can't be started or pinned, or a broadcast
// gets the wrong value.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// OS-dependent includes
#include <time.h>
#include <unistd.h>
#include <sys/syscall.h>

#include "buster/compiler/lane.c"

#define BUSTER_LANE_BENCHMARK_DEFAULT_LANES 8
#define BUSTER_LANE_BENCHMARK_MAX_LANES 64
#define BUSTER_LANE_BENCHMARK_DEFAULT_ROUNDS 20000
#define BUSTER_LANE_BENCHMARK_PASSES 21
// The pthread barrier's passes have rounds / BUSTER_LANE_BENCHMARK_PTHREAD_SHARE rounds.
#define BUSTER_LANE_BENCHMARK_PTHREAD_SHARE 16
// How late lane 0 comes to the wake passes' syncs: well within the BUSTER_LANE_MWAITX_TICKS (1 ms on the 9700X) that
// the other lanes wait in mwaitx, and well past them, once they sleep on the futex.
#define BUSTER_LANE_BENCHMARK_MWAITX_LATE_NANOSECONDS 200000
#define BUSTER_LANE_BENCHMARK_FUTEX_LATE_NANOSECONDS 3000000
// After a wait: how long every lane works before each pass, so that it starts at full speed, and how much full-speed
// work each does after the wait.
#define BUSTER_LANE_BENCHMARK_SETTLE_NANOSECONDS 2000000
#define BUSTER_LANE_BENCHMARK_WORK_NANOSECONDS 1000000
// The busy loop's full speed: each lane works this long with every lane working, then takes the best of
// BUSTER_LANE_BENCHMARK_CALIBRATION_RUNS runs of BUSTER_LANE_BENCHMARK_CALIBRATION_ITERATIONS iterations (0.7 ms on
// the 9700X).
#define BUSTER_LANE_BENCHMARK_CALIBRATION_NANOSECONDS 20000000
#define BUSTER_LANE_BENCHMARK_CALIBRATION_RUNS 8
#define BUSTER_LANE_BENCHMARK_CALIBRATION_ITERATIONS 1000000
// The busy loop's iteration: an LCG step, a multiply and an add that depend on the last.
#define BUSTER_LANE_BENCHMARK_LCG_MULTIPLIER 6364136223846793005ull
#define BUSTER_LANE_BENCHMARK_LCG_INCREMENT 1442695040888963407ull

// How long lane 0 works alone in the passes after a wait while the other lanes wait for it, in nanoseconds: within the
// mwaitx time, around it, and past it.
static const uint64_t lane_benchmark_waits[] = { 200000, 1000000, 2000000, 5000000 };
#define BUSTER_LANE_BENCHMARK_WAIT_COUNT (sizeof(lane_benchmark_waits) / sizeof(lane_benchmark_waits[0]))

typedef struct LaneBenchmark LaneBenchmark;
struct LaneBenchmark
{
    uint64_t rounds;
    pthread_barrier_t barrier;
    // Each pass's time in nanoseconds: of all its syncs, broadcasts or pthread barrier waits, or of its one wake.
    uint64_t sync[BUSTER_LANE_BENCHMARK_PASSES];
    uint64_t broadcast[BUSTER_LANE_BENCHMARK_PASSES];
    uint64_t pthread[BUSTER_LANE_BENCHMARK_PASSES];
    uint64_t wake_mwaitx[BUSTER_LANE_BENCHMARK_PASSES];
    uint64_t wake_futex[BUSTER_LANE_BENCHMARK_PASSES];
    // When each lane left the late sync, how many times each slept in the wake passes and how many wrong values each
    // got from broadcasts; each written by its lane.
    uint64_t left[BUSTER_LANE_BENCHMARK_MAX_LANES];
    uint64_t sleeps[BUSTER_LANE_BENCHMARK_MAX_LANES];
    uint64_t wrong[BUSTER_LANE_BENCHMARK_MAX_LANES];
};

// What waiting costs afterwards, on the largest group: per wait and pass, the wake, lane 0's time alone, and the slowest
// and median waiting lane's time for their work, each against its time at full speed (in thousandths, 1000 being full
// speed).
typedef struct LaneBenchmarkAfter LaneBenchmarkAfter;
struct LaneBenchmarkAfter
{
    uint64_t wake[BUSTER_LANE_BENCHMARK_WAIT_COUNT][BUSTER_LANE_BENCHMARK_PASSES];
    uint64_t alone[BUSTER_LANE_BENCHMARK_WAIT_COUNT][BUSTER_LANE_BENCHMARK_PASSES];
    uint64_t slowest[BUSTER_LANE_BENCHMARK_WAIT_COUNT][BUSTER_LANE_BENCHMARK_PASSES];
    uint64_t median[BUSTER_LANE_BENCHMARK_WAIT_COUNT][BUSTER_LANE_BENCHMARK_PASSES];
    // Each lane's, written by its lane: when it arrived (lane 0) or left the sync, and its work's time against full speed.
    uint64_t arrival;
    uint64_t left[BUSTER_LANE_BENCHMARK_MAX_LANES];
    uint64_t work[BUSTER_LANE_BENCHMARK_MAX_LANES];
};

typedef struct LaneBenchmarkArenas LaneBenchmarkArenas;
struct LaneBenchmarkArenas
{
    uint64_t reserve[BUSTER_LANE_BENCHMARK_PASSES];
    uint64_t first_page[BUSTER_LANE_BENCHMARK_PASSES];
    uint64_t small_pages[BUSTER_LANE_BENCHMARK_PASSES];
    uint64_t huge_page[BUSTER_LANE_BENCHMARK_PASSES];
};

static uint64_t lane_benchmark_now(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec;
}

static void lane_benchmark_sort(uint64_t* values, uint64_t count)
{
    for (uint64_t i = 1; i < count; i += 1)
    {
        const uint64_t value = values[i];
        uint64_t j = i;
        while (j > 0 && values[j - 1] > value)
        {
            values[j] = values[j - 1];
            j -= 1;
        }
        values[j] = value;
    }
}

static double lane_benchmark_median(const uint64_t* values)
{
    uint64_t sorted[BUSTER_LANE_BENCHMARK_PASSES];
    memcpy(sorted, values, sizeof(sorted));
    lane_benchmark_sort(sorted, BUSTER_LANE_BENCHMARK_PASSES);
    return (double)sorted[BUSTER_LANE_BENCHMARK_PASSES / 2];
}

// The entry point of the measured groups.
static void lane_benchmark_main(void* argument)
{
    LaneBenchmark* benchmark = argument;
    const uint32_t index = lane_index();
    const uint32_t lanes = lane_count();
    for (uint64_t pass = 0; pass <= BUSTER_LANE_BENCHMARK_PASSES; pass += 1)
    {
        lane_sync();
        const uint64_t begin = lane_benchmark_now();
        for (uint64_t round = 0; round < benchmark->rounds; round += 1)
        {
            lane_sync();
        }
        const uint64_t end = lane_benchmark_now();
        if (index == 0 && pass > 0)
        {
            benchmark->sync[pass - 1] = end - begin;
        }
    }

    uint64_t wrong = 0;
    for (uint64_t pass = 0; pass <= BUSTER_LANE_BENCHMARK_PASSES; pass += 1)
    {
        lane_sync();
        const uint64_t begin = lane_benchmark_now();
        for (uint64_t round = 0; round < benchmark->rounds; round += 1)
        {
            wrong += lane_broadcast(index == 0 ? round : ~round, 0) != round ? 1 : 0;
        }
        const uint64_t end = lane_benchmark_now();
        if (index == 0 && pass > 0)
        {
            benchmark->broadcast[pass - 1] = end - begin;
        }
    }

    for (uint64_t pass = 0; pass <= BUSTER_LANE_BENCHMARK_PASSES; pass += 1)
    {
        lane_sync();
        const uint64_t begin = lane_benchmark_now();
        for (uint64_t round = 0; round < benchmark->rounds / BUSTER_LANE_BENCHMARK_PTHREAD_SHARE; round += 1)
        {
            pthread_barrier_wait(&benchmark->barrier);
        }
        const uint64_t end = lane_benchmark_now();
        if (index == 0 && pass > 0)
        {
            benchmark->pthread[pass - 1] = end - begin;
        }
    }

    uint64_t sleeps = 0;
    for (uint32_t futex = 0; futex < 2; futex += 1)
    {
        const uint64_t late = futex != 0 ? BUSTER_LANE_BENCHMARK_FUTEX_LATE_NANOSECONDS : BUSTER_LANE_BENCHMARK_MWAITX_LATE_NANOSECONDS;
        for (uint64_t pass = 0; pass <= BUSTER_LANE_BENCHMARK_PASSES; pass += 1)
        {
            lane_sync();
            uint64_t arrival = 0;
            if (index == 0)
            {
                // Lane 0 spins rather than sleeps, so that it arrives on time.
                const uint64_t until = lane_benchmark_now() + late;
                while (lane_benchmark_now() < until)
                {
                    _mm_pause();
                }
                arrival = lane_benchmark_now();
            }
            const uint64_t slept = thread_context.sleeps;
            lane_sync();
            benchmark->left[index] = lane_benchmark_now();
            sleeps += pass > 0 && futex != 0 ? thread_context.sleeps - slept : 0;
            lane_sync();
            if (index == 0 && pass > 0)
            {
                uint64_t last = 0;
                for (uint32_t lane = 0; lane < lanes; lane += 1)
                {
                    last = benchmark->left[lane] > last ? benchmark->left[lane] : last;
                }
                uint64_t* wake = futex != 0 ? benchmark->wake_futex : benchmark->wake_mwaitx;
                wake[pass - 1] = last - arrival;
            }
        }
    }
    benchmark->sleeps[index] = sleeps;
    benchmark->wrong[index] = wrong;
}

// `iterations` steps of the busy loop. The empty asm keeps Clang from folding or vectorizing the chain.
static uint64_t lane_benchmark_work(uint64_t x, uint64_t iterations)
{
    for (uint64_t i = 0; i < iterations; i += 1)
    {
        x = x * BUSTER_LANE_BENCHMARK_LCG_MULTIPLIER + BUSTER_LANE_BENCHMARK_LCG_INCREMENT;
        __asm__("" : "+r"(x));
    }
    return x;
}

// The busy loop until `until`, in steps of about 30 us.
static uint64_t lane_benchmark_work_until(uint64_t x, uint64_t until)
{
    while (lane_benchmark_now() < until)
    {
        x = lane_benchmark_work(x, 1u << 15);
    }
    return x;
}

// The entry point of the passes after a wait.
static void lane_benchmark_after_wait(void* argument)
{
    LaneBenchmarkAfter* after = argument;
    const uint32_t index = lane_index();
    const uint32_t lanes = lane_count();
    uint64_t x = index + 1;
    lane_sync();
    x = lane_benchmark_work_until(x, lane_benchmark_now() + BUSTER_LANE_BENCHMARK_CALIBRATION_NANOSECONDS);
    uint64_t best = UINT64_MAX;
    for (uint32_t run = 0; run < BUSTER_LANE_BENCHMARK_CALIBRATION_RUNS; run += 1)
    {
        const uint64_t begin = lane_benchmark_now();
        x = lane_benchmark_work(x, BUSTER_LANE_BENCHMARK_CALIBRATION_ITERATIONS);
        const uint64_t end = lane_benchmark_now();
        best = end - begin < best ? end - begin : best;
    }
    const double nanoseconds_per_iteration = (double)best / BUSTER_LANE_BENCHMARK_CALIBRATION_ITERATIONS;
    const uint64_t work_iterations = (uint64_t)(BUSTER_LANE_BENCHMARK_WORK_NANOSECONDS / nanoseconds_per_iteration);
    const double work_nanoseconds = (double)work_iterations * nanoseconds_per_iteration;
    for (uint32_t wait = 0; wait < BUSTER_LANE_BENCHMARK_WAIT_COUNT; wait += 1)
    {
        const uint64_t alone_iterations = (uint64_t)((double)lane_benchmark_waits[wait] / nanoseconds_per_iteration);
        for (uint64_t pass = 0; pass <= BUSTER_LANE_BENCHMARK_PASSES; pass += 1)
        {
            lane_sync();
            x = lane_benchmark_work_until(x, lane_benchmark_now() + BUSTER_LANE_BENCHMARK_SETTLE_NANOSECONDS);
            lane_sync();
            uint64_t alone = 0;
            if (index == 0)
            {
                const uint64_t begin = lane_benchmark_now();
                x = lane_benchmark_work(x, alone_iterations);
                after->arrival = lane_benchmark_now();
                alone = (uint64_t)(1000.0 * (double)(after->arrival - begin) / ((double)alone_iterations * nanoseconds_per_iteration));
            }
            lane_sync();
            const uint64_t left = lane_benchmark_now();
            x = lane_benchmark_work(x, work_iterations);
            const uint64_t done = lane_benchmark_now();
            after->left[index] = left;
            after->work[index] = (uint64_t)(1000.0 * (double)(done - left) / work_nanoseconds);
            lane_sync();
            if (index == 0 && pass > 0)
            {
                uint64_t last = 0;
                uint64_t works[BUSTER_LANE_BENCHMARK_MAX_LANES];
                for (uint32_t lane = 1; lane < lanes; lane += 1)
                {
                    last = after->left[lane] > last ? after->left[lane] : last;
                    works[lane - 1] = after->work[lane];
                }
                lane_benchmark_sort(works, lanes - 1);
                after->wake[wait][pass - 1] = last - after->arrival;
                after->alone[wait][pass - 1] = alone;
                after->slowest[wait][pass - 1] = works[lanes - 2];
                after->median[wait][pass - 1] = works[(lanes - 2) / 2];
            }
        }
    }
}

static void lane_benchmark_empty(void* argument)
{
    (void)argument;
}

// The arenas' first touches, on the first CPU.
static void lane_benchmark_arenas(void* argument)
{
    LaneBenchmarkArenas* arenas = argument;
    for (uint64_t pass = 0; pass <= BUSTER_LANE_BENCHMARK_PASSES; pass += 1)
    {
        const uint64_t reserve_begin = lane_benchmark_now();
        Arena untouched = arena_reserve(BUSTER_SCRATCH_ARENA_CAPACITY);
        arena_release(&untouched);
        const uint64_t reserve_end = lane_benchmark_now();

        Arena arena = arena_reserve(BUSTER_SCRATCH_ARENA_CAPACITY);
        volatile uint8_t* base = arena.base;
        const uint64_t begin = lane_benchmark_now();
        base[0] = 1;
        const uint64_t first_page = lane_benchmark_now();
        for (uint64_t offset = BUSTER_ARENA_PAGE_BYTES; offset < BUSTER_ARENA_SMALL_BYTES; offset += BUSTER_ARENA_PAGE_BYTES)
        {
            base[offset] = 1;
        }
        const uint64_t small_pages = lane_benchmark_now();
        base[BUSTER_ARENA_SMALL_BYTES] = 1;
        const uint64_t huge_page = lane_benchmark_now();
        arena_release(&arena);
        if (pass > 0)
        {
            arenas->reserve[pass - 1] = reserve_end - reserve_begin;
            arenas->first_page[pass - 1] = first_page - begin;
            arenas->small_pages[pass - 1] = small_pages - begin;
            arenas->huge_page[pass - 1] = huge_page - small_pages;
        }
    }
}

static bool lane_benchmark_parse(const char* text, uint64_t* value)
{
    char* end = 0;
    const unsigned long long parsed = strtoull(text, &end, 10);
    const bool result = text[0] >= '0' && text[0] <= '9' && end != text && *end == 0;
    *value = result ? (uint64_t)parsed : 0;
    return result;
}

// Parses a CPU list such as "0-3,8" into cpus. Returns how many CPUs it names, or 0 if it is invalid or names more than
// BUSTER_LANE_BENCHMARK_MAX_LANES.
static uint32_t lane_benchmark_parse_cpus(const char* text, uint32_t* cpus)
{
    uint32_t count = 0;
    bool valid = true;
    const char* p = text;
    while (valid && *p != 0)
    {
        char* end = 0;
        const unsigned long first = strtoul(p, &end, 10);
        unsigned long last = first;
        valid = *p >= '0' && *p <= '9' && end != p;
        p = end;
        if (valid && *p == '-')
        {
            last = strtoul(p + 1, &end, 10);
            valid = p[1] >= '0' && p[1] <= '9' && end != p + 1 && last >= first;
            p = end;
        }
        for (unsigned long cpu = first; valid && cpu <= last; cpu += 1)
        {
            valid = count < BUSTER_LANE_BENCHMARK_MAX_LANES && cpu < BUSTER_LANE_CPU_MASK_WORDS * 64;
            if (valid)
            {
                cpus[count] = (uint32_t)cpu;
                count += 1;
            }
        }
        if (valid && *p == ',')
        {
            p += 1;
            valid = *p != 0;
        }
        else
        {
            valid = valid && *p == 0;
        }
    }
    return valid ? count : 0;
}

// The CPUs this process may run on, in order, up to BUSTER_LANE_BENCHMARK_MAX_LANES of them.
static uint32_t lane_benchmark_allowed_cpus(uint32_t* cpus)
{
    uint64_t mask[BUSTER_LANE_CPU_MASK_WORDS] = { 0 };
    uint32_t count = 0;
    if (syscall(SYS_sched_getaffinity, 0, sizeof(mask), mask) > 0)
    {
        for (uint32_t cpu = 0; cpu < BUSTER_LANE_CPU_MASK_WORDS * 64 && count < BUSTER_LANE_BENCHMARK_MAX_LANES; cpu += 1)
        {
            if ((mask[cpu / 64] >> (cpu % 64)) & 1)
            {
                cpus[count] = cpu;
                count += 1;
            }
        }
    }
    return count;
}

int run_lane_benchmark(int argc, char** argv)
{
    uint64_t lanes = 0;
    uint64_t rounds = BUSTER_LANE_BENCHMARK_DEFAULT_ROUNDS;
    uint32_t cpus[BUSTER_LANE_BENCHMARK_MAX_LANES];
    uint32_t cpu_count = 0;
    bool valid = true;
    for (int i = 1; i < argc; i += 1)
    {
        if (strcmp(argv[i], "--lanes") == 0 && i + 1 < argc)
        {
            valid = valid && lane_benchmark_parse(argv[i + 1], &lanes) && lanes != 0;
            i += 1;
        }
        else if (strcmp(argv[i], "--rounds") == 0 && i + 1 < argc)
        {
            valid = valid && lane_benchmark_parse(argv[i + 1], &rounds);
            i += 1;
        }
        else if (strcmp(argv[i], "--cpus") == 0 && i + 1 < argc)
        {
            cpu_count = lane_benchmark_parse_cpus(argv[i + 1], cpus);
            valid = valid && cpu_count != 0;
            i += 1;
        }
        else
        {
            valid = false;
        }
    }
    cpu_count = cpu_count != 0 ? cpu_count : lane_benchmark_allowed_cpus(cpus);
    lanes = lanes != 0 ? lanes : (cpu_count < BUSTER_LANE_BENCHMARK_DEFAULT_LANES ? cpu_count : BUSTER_LANE_BENCHMARK_DEFAULT_LANES);

    int result = 1;
    if (!valid || lanes == 0 || lanes > BUSTER_LANE_BENCHMARK_MAX_LANES || rounds < BUSTER_LANE_BENCHMARK_PTHREAD_SHARE)
    {
        fprintf(stderr, "usage: %s [--lanes N] [--cpus LIST] [--rounds N]\n", argv[0]);
    }
    else if (cpu_count < lanes)
    {
        fprintf(stderr, "%lu lanes need as many CPUs, and there are %u to pin them to\n", (unsigned long)lanes, cpu_count);
    }
    else
    {
        printf("lanes pinned to CPUs");
        for (uint32_t i = 0; i < lanes; i += 1)
        {
            printf(" %u", cpus[i]);
        }
        printf(" in order; medians of %d passes, of %lu rounds for sync and broadcast and %lu for pthread, and of waking from mwaitx (%lu us late) and the futex "
               "(%lu us late):\n",
               BUSTER_LANE_BENCHMARK_PASSES, (unsigned long)rounds, (unsigned long)(rounds / BUSTER_LANE_BENCHMARK_PTHREAD_SHARE),
               (unsigned long)(BUSTER_LANE_BENCHMARK_MWAITX_LATE_NANOSECONDS / 1000), (unsigned long)(BUSTER_LANE_BENCHMARK_FUTEX_LATE_NANOSECONDS / 1000));
        printf("  lanes        sync   broadcast     pthread   wake mwaitx   wake futex  slept       start\n");
        static LaneBenchmark benchmark;
        bool passed = true;
        for (uint32_t count = 1; passed && count <= lanes; count += 1)
        {
            memset(&benchmark, 0, sizeof(benchmark));
            benchmark.rounds = rounds;
            passed = pthread_barrier_init(&benchmark.barrier, 0, count) == 0 && lane_run(count, lane_benchmark_main, &benchmark, cpus);
            pthread_barrier_destroy(&benchmark.barrier);
            uint64_t start[BUSTER_LANE_BENCHMARK_PASSES];
            for (uint64_t pass = 0; passed && pass <= BUSTER_LANE_BENCHMARK_PASSES; pass += 1)
            {
                const uint64_t begin = lane_benchmark_now();
                passed = lane_run(count, lane_benchmark_empty, 0, 0);
                const uint64_t end = lane_benchmark_now();
                if (pass > 0)
                {
                    start[pass - 1] = end - begin;
                }
            }
            uint64_t sleeps = 0;
            uint64_t wrong = 0;
            for (uint32_t lane = 0; lane < count; lane += 1)
            {
                sleeps += benchmark.sleeps[lane];
                wrong += benchmark.wrong[lane];
            }
            if (!passed)
            {
                fprintf(stderr, "a group of %u lanes couldn't be started or pinned\n", count);
            }
            else if (wrong != 0)
            {
                fprintf(stderr, "%u lanes: %lu broadcasts got the wrong value\n", count, (unsigned long)wrong);
                passed = false;
            }
            else
            {
                printf("  %5u  %7.1f ns  %7.1f ns  %7.1f ns  %9.2f us  %8.2f us  %5.1f  %7.1f us\n", count, lane_benchmark_median(benchmark.sync) / (double)rounds,
                       lane_benchmark_median(benchmark.broadcast) / (double)rounds,
                       lane_benchmark_median(benchmark.pthread) / (double)(rounds / BUSTER_LANE_BENCHMARK_PTHREAD_SHARE), lane_benchmark_median(benchmark.wake_mwaitx) / 1e3,
                       lane_benchmark_median(benchmark.wake_futex) / 1e3, (double)sleeps / BUSTER_LANE_BENCHMARK_PASSES, lane_benchmark_median(start) / 1e3);
            }
            fflush(stdout);
        }

        static LaneBenchmarkAfter after;
        if (passed && lanes > 1)
        {
            passed = lane_run((uint32_t)lanes, lane_benchmark_after_wait, &after, cpus);
            if (!passed)
            {
                fprintf(stderr, "a group of %lu lanes couldn't be started or pinned\n", (unsigned long)lanes);
            }
        }
        if (passed && lanes > 1)
        {
            printf("after waiting, %lu lanes: lane 0 works alone for the wait while the others wait for it, then every lane works for %lu us; medians of %d passes, "
                   "against full speed:\n",
                   (unsigned long)lanes, (unsigned long)(BUSTER_LANE_BENCHMARK_WORK_NANOSECONDS / 1000), BUSTER_LANE_BENCHMARK_PASSES);
            printf("      wait       wake  lane 0 alone  slowest after  median after\n");
            for (uint32_t wait = 0; wait < BUSTER_LANE_BENCHMARK_WAIT_COUNT; wait += 1)
            {
                printf("  %5lu us  %6.2f us  %11.1f%%  %12.1f%%  %11.1f%%\n", (unsigned long)(lane_benchmark_waits[wait] / 1000), lane_benchmark_median(after.wake[wait]) / 1e3,
                       lane_benchmark_median(after.alone[wait]) / 10.0 - 100.0, lane_benchmark_median(after.slowest[wait]) / 10.0 - 100.0,
                       lane_benchmark_median(after.median[wait]) / 10.0 - 100.0);
            }
            fflush(stdout);
        }

        static LaneBenchmarkArenas arenas;
        if (passed && !lane_run(1, lane_benchmark_arenas, &arenas, cpus))
        {
            fprintf(stderr, "the arenas' lane couldn't be pinned to CPU %u\n", cpus[0]);
            passed = false;
        }
        if (passed)
        {
            printf("arenas on CPU %u: reserving and releasing %lu GB %.2f us; first touch of the first page %.2f us, of the first %lu KB (base pages) %.2f us, of "
                   "the huge page after them %.2f us\n",
                   cpus[0], (unsigned long)(BUSTER_SCRATCH_ARENA_CAPACITY >> 30), lane_benchmark_median(arenas.reserve) / 1e3, lane_benchmark_median(arenas.first_page) / 1e3,
                   (unsigned long)(BUSTER_ARENA_SMALL_BYTES >> 10), lane_benchmark_median(arenas.small_pages) / 1e3, lane_benchmark_median(arenas.huge_page) / 1e3);
        }
        result = passed ? 0 : 1;
    }
    return result;
}
