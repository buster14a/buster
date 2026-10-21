// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception Copyright (c) 2026 David Gonzalez Martin. See LICENSE and
// LICENSING.md. The floors' benchmark (docs/object-writer-and-linker.md, "The floors"): what the machine and the kernel
// charge a linker however it works: creating its output, writing one file from several threads, the small calls it
// makes per input file, faulting and copying memory, and a process's teardown. It takes over the floor programs of the
// document's appendix. Usage: build/compiler benchmark-floors [--directory PATH] [--cpu N] [--only
// files|threads|calls|memory|teardown] Run it under the 9700X's lock without taskset: it pins itself to --cpu (CPU 2 by
// default), and a write by N threads to the first N CPUs this process may use (0 to 7 on the 9700X, one per core).
// Every figure is a median, the sections in this order (--only runs one):
//   - files: the output written from memory into --directory (TMPDIR or /tmp by default) each way a linker can write it
//     (FloorsBenchmarkMethod), at each of floors_benchmark_sizes: BUSTER_FLOORS_BENCHMARK_RUNS runs, or from
//     BUSTER_FLOORS_BENCHMARK_LARGE_BYTES BUSTER_FLOORS_BENCHMARK_LARGE_RUNS runs with a syncfs after each, outside the
//     time. The file is there, written the first way, before a size's first run, and every way's time takes in its
//     open and its close.
//   - threads: each of floors_benchmark_parallel_sizes written by 1, 2, 4 and 8 threads, each a contiguous share, by
//     pwrite and through a shared mapping: the open and the sizing on the first thread, then from the threads' release
//     (they wait at a sync once the file is open: starting them isn't timed) until the last has written, and the unmap
//     and the close; BUSTER_FLOORS_BENCHMARK_LARGE_RUNS runs with a syncfs after each.
//   - calls: what a linker pays per input file, on this process's libc.so.6: open and close, stat, mmap and munmap
//     without a touch, the same touching every page, and pread of the whole file into memory already touched.
//   - memory: BUSTER_FLOORS_BENCHMARK_FAULT_BYTES of anonymous memory touched a page at a time for the first time, with
//     base pages and with MADV_HUGEPAGE; then memcpy, non-temporal stores and memset at each of
//     floors_benchmark_copy_sizes, BUSTER_FLOORS_BENCHMARK_COPY_BYTES a run.
//   - teardown: a child, on another CPU when this process may use one, touches each of
//     floors_benchmark_teardown_sizes of anonymous memory, with base pages and with MADV_HUGEPAGE, sends this process
//     the time through a pipe and exits: the time from then to waitpid's return, which a linker that forks hides from
//     its caller.
// The exit status is 1 if the arguments are invalid or a call fails.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// OS-dependent includes
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <immintrin.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/syscall.h>

typedef struct String8 String8;
struct String8
{
    char* pointer;
    uint64_t length;
};

#ifndef BUSTER_ARRAY_LENGTH
#define BUSTER_ARRAY_LENGTH(x) (sizeof(x) / sizeof((x)[0]))
#endif

#include "buster/compiler/lane.c"

#define BUSTER_FLOORS_BENCHMARK_PATH_BYTES 4096
// The runs of a figure, and from BUSTER_FLOORS_BENCHMARK_LARGE_BYTES of output the large runs, with a syncfs after
// each.
#define BUSTER_FLOORS_BENCHMARK_RUNS 15
#define BUSTER_FLOORS_BENCHMARK_LARGE_RUNS 5
#define BUSTER_FLOORS_BENCHMARK_LARGE_BYTES (64ull << 20)
// The output's bytes, written from memory: the largest size.
#define BUSTER_FLOORS_BENCHMARK_SOURCE_BYTES (512ull << 20)
// The calls timed together for a figure of the small calls, and of those that touch the whole file.
#define BUSTER_FLOORS_BENCHMARK_CALLS 1000
#define BUSTER_FLOORS_BENCHMARK_FILE_CALLS 100
#define BUSTER_FLOORS_BENCHMARK_FAULT_BYTES (256ull << 20)
#define BUSTER_FLOORS_BENCHMARK_COPY_BYTES (1ull << 30)
#define BUSTER_FLOORS_BENCHMARK_MAX_THREADS 8
#define BUSTER_FLOORS_BENCHMARK_PAGE_BYTES 4096
// splitmix64's increment: the output's bytes are its multiples, which no page of zeros could stand for.
#define BUSTER_FLOORS_BENCHMARK_GOLDEN_RATIO 0x9E3779B97F4A7C15ull

static const uint64_t floors_benchmark_sizes[] = {
    16ull << 10,
    256ull << 10,
    4ull << 20,
    64ull << 20,
    512ull << 20,
};

static const uint64_t floors_benchmark_parallel_sizes[] = {
    64ull << 20,
    512ull << 20,
};

static const uint32_t floors_benchmark_thread_counts[] = {
    1,
    2,
    4,
    8,
};

static const uint64_t floors_benchmark_copy_sizes[] = {
    256ull << 10,
    4ull << 20,
    16ull << 20,
    256ull << 20,
};

static const uint64_t floors_benchmark_teardown_sizes[] = {
    64ull << 20,
    1ull << 30,
    4ull << 30,
};

// The ways to write the output.
typedef enum FloorsBenchmarkMethod
{
    // unlink, open with O_CREAT | O_TRUNC, pwrite until done, close.
    FLOORS_BENCHMARK_NEW_FILE,
    // The same without the unlink.
    FLOORS_BENCHMARK_TRUNCATE,
    // open of the file there, pwrite, ftruncate to the same size, close.
    FLOORS_BENCHMARK_IN_PLACE,
    // open of another name with O_CREAT | O_TRUNC, pwrite, close, rename over the file.
    FLOORS_BENCHMARK_RENAME,
    // unlink, open with O_CREAT, ftruncate, mmap with MAP_SHARED, memcpy, munmap, close.
    FLOORS_BENCHMARK_MAP,
    // The same with fallocate in place of ftruncate, and MADV_HUGEPAGE on the mapping.
    FLOORS_BENCHMARK_MAP_FALLOCATE,
    // The mapping without the unlink.
    FLOORS_BENCHMARK_MAP_IN_PLACE,
    FLOORS_BENCHMARK_METHODS,
} FloorsBenchmarkMethod;

static const char* const floors_benchmark_method_names[FLOORS_BENCHMARK_METHODS] = {
    [FLOORS_BENCHMARK_NEW_FILE] = "new file",
    [FLOORS_BENCHMARK_TRUNCATE] = "O_TRUNC",
    [FLOORS_BENCHMARK_IN_PLACE] = "in place",
    [FLOORS_BENCHMARK_RENAME] = "temporary file",
    [FLOORS_BENCHMARK_MAP] = "mmap",
    [FLOORS_BENCHMARK_MAP_FALLOCATE] = "mmap, fallocate",
    [FLOORS_BENCHMARK_MAP_IN_PLACE] = "mmap in place",
};

// The ways several threads write one file: by pwrite into a new file or in place, or through a shared mapping.
static const FloorsBenchmarkMethod floors_benchmark_parallel_methods[] = {
    FLOORS_BENCHMARK_NEW_FILE,
    FLOORS_BENCHMARK_IN_PLACE,
    FLOORS_BENCHMARK_MAP,
    FLOORS_BENCHMARK_MAP_FALLOCATE,
    FLOORS_BENCHMARK_MAP_IN_PLACE,
};

static uint64_t floors_benchmark_now(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec;
}

static int floors_benchmark_compare(const void* a, const void* b)
{
    const uint64_t x = *(const uint64_t*)a;
    const uint64_t y = *(const uint64_t*)b;
    return (x > y) - (x < y);
}

// The median of the values, in nanoseconds; sorts them.
static uint64_t floors_benchmark_median(uint64_t* values, uint64_t count)
{
    qsort(values, count, sizeof(uint64_t), floors_benchmark_compare);
    return values[count / 2];
}

// `size` bytes of anonymous memory, with `advice` (MADV_HUGEPAGE or MADV_NOHUGEPAGE), or 0.
static uint8_t* floors_benchmark_map(uint64_t size, int advice)
{
    uint8_t* bytes = mmap(0, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    uint8_t* result = bytes != MAP_FAILED ? bytes : 0;
    if (result != 0)
    {
        madvise(result, size, advice);
    }
    return result;
}

// Flushes the file system of `directory`, outside the time, so that one run's dirty pages don't slow the next.
static void floors_benchmark_sync(const char* directory)
{
    const int file = open(directory, O_RDONLY | O_CLOEXEC);
    if (file >= 0)
    {
        syscall(SYS_syncfs, file);
        close(file);
    }
}

// Writes bytes `begin` to `end` of `bytes` at the same offsets of `file`, by pwrite until done. False if one failed.
static bool floors_benchmark_pwrite(int file, const uint8_t* bytes, uint64_t begin, uint64_t end)
{
    uint64_t done = begin;
    bool result = true;
    while (result && done < end)
    {
        const ssize_t written = pwrite(file, bytes + done, end - done, (off_t)done);
        result = written > 0;
        done += result ? (uint64_t)written : 0;
    }
    return result;
}

// Sizes the open `file` for `size` bytes, as `method` does: fallocate or ftruncate.
static bool floors_benchmark_size(int file, FloorsBenchmarkMethod method, uint64_t size)
{
    const long sized = method == FLOORS_BENCHMARK_MAP_FALLOCATE ? syscall(SYS_fallocate, file, 0, 0, (off_t)size) : ftruncate(file, (off_t)size);
    return sized == 0;
}

// Writes `size` bytes of `source` into the file at `path` by `method` (`temporary` is the rename's name). False if a
// call failed.
static bool floors_benchmark_write(FloorsBenchmarkMethod method, const char* path, const char* temporary, const uint8_t* source, uint64_t size)
{
    const bool mapped = method == FLOORS_BENCHMARK_MAP || method == FLOORS_BENCHMARK_MAP_FALLOCATE || method == FLOORS_BENCHMARK_MAP_IN_PLACE;
    if (method == FLOORS_BENCHMARK_NEW_FILE || method == FLOORS_BENCHMARK_MAP || method == FLOORS_BENCHMARK_MAP_FALLOCATE)
    {
        unlink(path);
    }
    const char* target = method == FLOORS_BENCHMARK_RENAME ? temporary : path;
    const bool truncated = method == FLOORS_BENCHMARK_NEW_FILE || method == FLOORS_BENCHMARK_TRUNCATE || method == FLOORS_BENCHMARK_RENAME;
    const int flags = (mapped ? O_RDWR : O_WRONLY) | O_CREAT | (truncated ? O_TRUNC : 0) | O_CLOEXEC;
    const int file = open(target, flags, 0755);
    bool result = file >= 0;
    if (mapped)
    {
        result = result && floors_benchmark_size(file, method, size);
        uint8_t* map = result ? mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, file, 0) : MAP_FAILED;
        result = map != MAP_FAILED;
        if (result && method == FLOORS_BENCHMARK_MAP_FALLOCATE)
        {
            madvise(map, size, MADV_HUGEPAGE);
        }
        if (result)
        {
            memcpy(map, source, size);
            munmap(map, size);
        }
    }
    else
    {
        result = result && floors_benchmark_pwrite(file, source, 0, size);
        result = result && (method != FLOORS_BENCHMARK_IN_PLACE || ftruncate(file, (off_t)size) == 0);
    }
    if (file >= 0)
    {
        close(file);
    }
    result = result && (method != FLOORS_BENCHMARK_RENAME || rename(temporary, path) == 0);
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// Files

static bool floors_benchmark_files(const char* directory, const uint8_t* source)
{
    char path[BUSTER_FLOORS_BENCHMARK_PATH_BYTES];
    char temporary[BUSTER_FLOORS_BENCHMARK_PATH_BYTES];
    snprintf(path, sizeof(path), "%s/buster-floors-output", directory);
    snprintf(temporary, sizeof(temporary), "%s/buster-floors-output.temporary", directory);
    printf("Creating the output in %s, us (GB/s):\n  %-18s", directory, "");
    for (uint32_t s = 0; s < BUSTER_ARRAY_LENGTH(floors_benchmark_sizes); s += 1)
    {
        printf(" %17llu KB", (unsigned long long)(floors_benchmark_sizes[s] >> 10));
    }
    printf("\n");
    bool result = true;
    for (uint32_t method = 0; result && method < FLOORS_BENCHMARK_METHODS; method += 1)
    {
        printf("  %-18s", floors_benchmark_method_names[method]);
        for (uint32_t s = 0; result && s < BUSTER_ARRAY_LENGTH(floors_benchmark_sizes); s += 1)
        {
            const uint64_t size = floors_benchmark_sizes[s];
            const bool large = size >= BUSTER_FLOORS_BENCHMARK_LARGE_BYTES;
            const uint32_t runs = large ? BUSTER_FLOORS_BENCHMARK_LARGE_RUNS : BUSTER_FLOORS_BENCHMARK_RUNS;
            uint64_t times[BUSTER_FLOORS_BENCHMARK_RUNS];
            unlink(path);
            result = floors_benchmark_write(FLOORS_BENCHMARK_NEW_FILE, path, temporary, source, size);
            floors_benchmark_sync(directory);
            for (uint32_t run = 0; result && run < runs; run += 1)
            {
                const uint64_t start = floors_benchmark_now();
                result = floors_benchmark_write(method, path, temporary, source, size);
                times[run] = floors_benchmark_now() - start;
                if (large)
                {
                    floors_benchmark_sync(directory);
                }
            }
            const uint64_t median = result ? floors_benchmark_median(times, runs) : 0;
            printf(" %10.1f (%5.2f)", (double)median / 1e3, result ? (double)size / (double)median : 0.0);
            fflush(stdout);
        }
        printf("\n");
    }
    if (!result)
    {
        printf("  a write into %s failed: %s\n", directory, strerror(errno));
    }
    unlink(path);
    unlink(temporary);
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// Threads

typedef struct FloorsBenchmarkParallel FloorsBenchmarkParallel;
struct FloorsBenchmarkParallel
{
    FloorsBenchmarkMethod method;
    const char* directory;
    const char* path;
    const uint8_t* source;
    uint64_t size;
    // The first lane's file and mapping, of which each lane writes its share.
    int file;
    uint8_t* map;
    // Each lane's run: whether it wrote its share, and its errno if not. Every lane reads all of them after a sync, so
    // that the lanes stop together and no lane's failure is lost.
    bool wrote[BUSTER_FLOORS_BENCHMARK_MAX_THREADS];
    int errors[BUSTER_FLOORS_BENCHMARK_MAX_THREADS];
    // What the first lane measured, whether every run succeeded, and if not, the errno of the first lane that failed.
    uint64_t times[BUSTER_FLOORS_BENCHMARK_LARGE_RUNS];
    bool succeeded;
    int error;
};

// The lanes' entry point: the runs of one way, size and lane count. The first lane opens and sizes the file (and maps
// it), every lane writes its share, and the first unmaps and closes it, then flushes the file system outside the time.
static void floors_benchmark_parallel_main(void* argument)
{
    FloorsBenchmarkParallel* parallel = argument;
    const bool first = lane_index() == 0;
    const uint64_t begin = parallel->size * lane_index() / lane_count();
    const uint64_t end = parallel->size * (lane_index() + 1) / lane_count();
    const FloorsBenchmarkMethod method = parallel->method;
    const bool mapped = method == FLOORS_BENCHMARK_MAP || method == FLOORS_BENCHMARK_MAP_FALLOCATE || method == FLOORS_BENCHMARK_MAP_IN_PLACE;
    bool succeeded = true;
    int error = 0;
    for (uint32_t run = 0; succeeded && run < BUSTER_FLOORS_BENCHMARK_LARGE_RUNS; run += 1)
    {
        uint64_t start = 0;
        uint64_t opened = 0;
        bool ready = false;
        int failure = 0;
        if (first)
        {
            if (method == FLOORS_BENCHMARK_NEW_FILE || method == FLOORS_BENCHMARK_MAP || method == FLOORS_BENCHMARK_MAP_FALLOCATE)
            {
                unlink(parallel->path);
            }
            start = floors_benchmark_now();
            parallel->file = open(parallel->path, O_RDWR | O_CREAT | O_CLOEXEC, 0755);
            ready = parallel->file >= 0 && floors_benchmark_size(parallel->file, method, parallel->size);
            parallel->map = ready && mapped ? mmap(0, parallel->size, PROT_READ | PROT_WRITE, MAP_SHARED, parallel->file, 0) : MAP_FAILED;
            ready = ready && (!mapped || parallel->map != MAP_FAILED);
            failure = ready ? 0 : errno;
            if (ready && method == FLOORS_BENCHMARK_MAP_FALLOCATE)
            {
                madvise(parallel->map, parallel->size, MADV_HUGEPAGE);
            }
            opened = floors_benchmark_now();
        }
        ready = lane_broadcast(ready ? 1 : 0, 0) != 0;
        const uint64_t released = first ? floors_benchmark_now() : 0;
        bool wrote = ready;
        if (ready && mapped)
        {
            memcpy(parallel->map + begin, parallel->source + begin, end - begin);
        }
        else if (ready)
        {
            wrote = floors_benchmark_pwrite(parallel->file, parallel->source, begin, end);
            failure = wrote ? 0 : errno;
        }
        // A lane rewrites its slot only after the next run's broadcast, which every lane reaches after reading them.
        parallel->wrote[lane_index()] = wrote;
        parallel->errors[lane_index()] = failure;
        lane_sync();
        for (uint32_t lane = 0; lane < lane_count(); lane += 1)
        {
            error = succeeded && !parallel->wrote[lane] ? parallel->errors[lane] : error;
            succeeded = succeeded && parallel->wrote[lane];
        }
        if (first)
        {
            if (parallel->map != MAP_FAILED)
            {
                munmap(parallel->map, parallel->size);
            }
            if (parallel->file >= 0)
            {
                close(parallel->file);
            }
            parallel->times[run] = (opened - start) + (floors_benchmark_now() - released);
            floors_benchmark_sync(parallel->directory);
        }
    }
    if (first)
    {
        parallel->succeeded = succeeded;
        parallel->error = error;
    }
}

static bool floors_benchmark_threads(const char* directory, const uint8_t* source, const uint32_t* cpus, uint32_t cpu_count)
{
    char path[BUSTER_FLOORS_BENCHMARK_PATH_BYTES];
    snprintf(path, sizeof(path), "%s/buster-floors-output", directory);
    bool result = true;
    // The errno of a lane that failed, which isn't this thread's.
    int lane_error = 0;
    for (uint32_t s = 0; result && s < BUSTER_ARRAY_LENGTH(floors_benchmark_parallel_sizes); s += 1)
    {
        const uint64_t size = floors_benchmark_parallel_sizes[s];
        printf("One file of %llu MB from several threads in %s, ms (GB/s):\n  %-18s", (unsigned long long)(size >> 20), directory, "");
        for (uint32_t t = 0; t < BUSTER_ARRAY_LENGTH(floors_benchmark_thread_counts); t += 1)
        {
            printf(" %8u thread%s", floors_benchmark_thread_counts[t], floors_benchmark_thread_counts[t] == 1 ? " " : "s");
        }
        printf("\n");
        for (uint32_t m = 0; result && m < BUSTER_ARRAY_LENGTH(floors_benchmark_parallel_methods); m += 1)
        {
            const FloorsBenchmarkMethod method = floors_benchmark_parallel_methods[m];
            printf("  %-18s", floors_benchmark_method_names[method]);
            // More threads than CPUs to pin them to aren't run.
            for (uint32_t t = 0; result && t < BUSTER_ARRAY_LENGTH(floors_benchmark_thread_counts); t += 1)
            {
                const uint32_t threads = floors_benchmark_thread_counts[t];
                FloorsBenchmarkParallel parallel = {
                    .method = method,
                    .directory = directory,
                    .path = path,
                    .source = source,
                    .size = size,
                    .file = -1,
                    .map = MAP_FAILED,
                };
                const bool runs = threads <= cpu_count;
                if (runs)
                {
                    unlink(path);
                    result = floors_benchmark_write(FLOORS_BENCHMARK_NEW_FILE, path, path, source, size);
                    floors_benchmark_sync(directory);
                    result = result && lane_run(threads, floors_benchmark_parallel_main, &parallel, cpus) && parallel.succeeded;
                    lane_error = parallel.error;
                }
                const uint64_t median = runs && result ? floors_benchmark_median(parallel.times, BUSTER_FLOORS_BENCHMARK_LARGE_RUNS) : 0;
                if (runs)
                {
                    printf(" %7.2f (%5.2f)", (double)median / 1e6, result ? (double)size / (double)median : 0.0);
                }
                else
                {
                    printf(" %15s", "-");
                }
                fflush(stdout);
            }
            printf("\n");
        }
    }
    if (!result)
    {
        printf("  a write from several threads failed: %s\n", strerror(lane_error != 0 ? lane_error : errno));
    }
    unlink(path);
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// Calls

// The path of this process's libc.so.6, from /proc/self/maps, into `path`. False if it isn't there.
static bool floors_benchmark_libc(char* path, uint64_t capacity)
{
    FILE* maps = fopen("/proc/self/maps", "r");
    char line[BUSTER_FLOORS_BENCHMARK_PATH_BYTES];
    bool result = false;
    while (!result && maps != 0 && fgets(line, sizeof(line), maps) != 0)
    {
        const char* name = strchr(line, '/');
        const char* newline = name != 0 ? strchr(name, '\n') : 0;
        const uint64_t length = newline != 0 ? (uint64_t)(newline - name) : 0;
        const uint64_t suffix = sizeof("/libc.so.6") - 1;
        result = length >= suffix && length < capacity && memcmp(name + length - suffix, "/libc.so.6", suffix) == 0;
        if (result)
        {
            memcpy(path, name, length);
            path[length] = 0;
        }
    }
    if (maps != 0)
    {
        fclose(maps);
    }
    return result;
}

static bool floors_benchmark_calls(uint8_t* buffer, uint64_t buffer_size)
{
    char libc[BUSTER_FLOORS_BENCHMARK_PATH_BYTES];
    bool result = floors_benchmark_libc(libc, sizeof(libc));
    const int file = result ? open(libc, O_RDONLY | O_CLOEXEC) : -1;
    struct stat status;
    result = file >= 0 && fstat(file, &status) == 0 && (uint64_t)status.st_size <= buffer_size;
    const uint64_t size = result ? (uint64_t)status.st_size : 0;
    uint64_t times[5][BUSTER_FLOORS_BENCHMARK_RUNS];
    volatile uint8_t sink = 0;
    for (uint32_t run = 0; result && run < BUSTER_FLOORS_BENCHMARK_RUNS; run += 1)
    {
        uint64_t start = floors_benchmark_now();
        for (uint32_t i = 0; i < BUSTER_FLOORS_BENCHMARK_CALLS; i += 1)
        {
            const int opened = open(libc, O_RDONLY | O_CLOEXEC);
            result = result && opened >= 0;
            close(opened);
        }
        times[0][run] = floors_benchmark_now() - start;
        start = floors_benchmark_now();
        for (uint32_t i = 0; i < BUSTER_FLOORS_BENCHMARK_CALLS; i += 1)
        {
            struct stat found;
            result = result && stat(libc, &found) == 0;
        }
        times[1][run] = floors_benchmark_now() - start;
        start = floors_benchmark_now();
        for (uint32_t i = 0; i < BUSTER_FLOORS_BENCHMARK_CALLS; i += 1)
        {
            void* mapped = mmap(0, size, PROT_READ, MAP_PRIVATE, file, 0);
            result = result && mapped != MAP_FAILED;
            munmap(mapped, size);
        }
        times[2][run] = floors_benchmark_now() - start;
        start = floors_benchmark_now();
        for (uint32_t i = 0; i < BUSTER_FLOORS_BENCHMARK_FILE_CALLS; i += 1)
        {
            const uint8_t* mapped = mmap(0, size, PROT_READ, MAP_PRIVATE, file, 0);
            result = result && mapped != MAP_FAILED;
            for (uint64_t offset = 0; result && offset < size; offset += BUSTER_FLOORS_BENCHMARK_PAGE_BYTES)
            {
                sink += mapped[offset];
            }
            munmap((void*)mapped, size);
        }
        times[3][run] = floors_benchmark_now() - start;
        start = floors_benchmark_now();
        for (uint32_t i = 0; i < BUSTER_FLOORS_BENCHMARK_FILE_CALLS; i += 1)
        {
            result = result && pread(file, buffer, size, 0) == (ssize_t)size;
        }
        times[4][run] = floors_benchmark_now() - start;
    }
    if (file >= 0)
    {
        close(file);
    }
    if (result)
    {
        const double pages = (double)((size + BUSTER_FLOORS_BENCHMARK_PAGE_BYTES - 1) / BUSTER_FLOORS_BENCHMARK_PAGE_BYTES);
        const double calls = BUSTER_FLOORS_BENCHMARK_CALLS;
        const double file_calls = BUSTER_FLOORS_BENCHMARK_FILE_CALLS;
        printf("The calls a linker makes per input file, on %s (%llu bytes), us each:\n", libc, (unsigned long long)size);
        printf("  open and close                        %8.2f\n", (double)floors_benchmark_median(times[0], BUSTER_FLOORS_BENCHMARK_RUNS) / calls / 1e3);
        printf("  stat                                  %8.2f\n", (double)floors_benchmark_median(times[1], BUSTER_FLOORS_BENCHMARK_RUNS) / calls / 1e3);
        printf("  mmap and munmap, untouched            %8.2f\n", (double)floors_benchmark_median(times[2], BUSTER_FLOORS_BENCHMARK_RUNS) / calls / 1e3);
        const double touched = (double)floors_benchmark_median(times[3], BUSTER_FLOORS_BENCHMARK_RUNS) / file_calls;
        printf("  mmap, every page touched, and munmap  %8.2f (%.1f ns a page)\n", touched / 1e3, touched / pages);
        const double read = (double)floors_benchmark_median(times[4], BUSTER_FLOORS_BENCHMARK_RUNS) / file_calls;
        printf("  pread of the whole file               %8.2f (%.2f GB/s)\n", read / 1e3, (double)size / read);
    }
    else
    {
        printf("  the calls on libc.so.6 failed (%s)\n", strerror(errno));
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// Memory

// Copies `size` bytes, a multiple of 256, by non-temporal stores 256 bytes at a time; both are 64-byte aligned.
static void floors_benchmark_stream(uint8_t* destination, const uint8_t* source, uint64_t size)
{
    for (uint64_t offset = 0; offset < size; offset += 256)
    {
        const __m512i a = _mm512_load_si512(source + offset);
        const __m512i b = _mm512_load_si512(source + offset + 64);
        const __m512i c = _mm512_load_si512(source + offset + 128);
        const __m512i d = _mm512_load_si512(source + offset + 192);
        _mm512_stream_si512((void*)(destination + offset), a);
        _mm512_stream_si512((void*)(destination + offset + 64), b);
        _mm512_stream_si512((void*)(destination + offset + 128), c);
        _mm512_stream_si512((void*)(destination + offset + 192), d);
    }
    _mm_sfence();
}

static bool floors_benchmark_memory(const uint8_t* source)
{
    bool result = true;
    printf("Memory:\n");
    for (uint32_t huge = 0; result && huge < 2; huge += 1)
    {
        uint64_t times[BUSTER_FLOORS_BENCHMARK_LARGE_RUNS];
        for (uint32_t run = 0; result && run < BUSTER_FLOORS_BENCHMARK_LARGE_RUNS; run += 1)
        {
            uint8_t* bytes = floors_benchmark_map(BUSTER_FLOORS_BENCHMARK_FAULT_BYTES, huge != 0 ? MADV_HUGEPAGE : MADV_NOHUGEPAGE);
            result = bytes != 0;
            const uint64_t start = floors_benchmark_now();
            for (uint64_t offset = 0; result && offset < BUSTER_FLOORS_BENCHMARK_FAULT_BYTES; offset += BUSTER_FLOORS_BENCHMARK_PAGE_BYTES)
            {
                bytes[offset] = 1;
            }
            times[run] = floors_benchmark_now() - start;
            if (bytes != 0)
            {
                munmap(bytes, BUSTER_FLOORS_BENCHMARK_FAULT_BYTES);
            }
        }
        const uint64_t median = result ? floors_benchmark_median(times, BUSTER_FLOORS_BENCHMARK_LARGE_RUNS) : 0;
        const double pages = (double)(BUSTER_FLOORS_BENCHMARK_FAULT_BYTES / BUSTER_FLOORS_BENCHMARK_PAGE_BYTES);
        printf("  first touch of %llu MB, %-13s %8.2f ms (%.1f ns a 4 KB page, %.2f GB/s)\n", (unsigned long long)(BUSTER_FLOORS_BENCHMARK_FAULT_BYTES >> 20),
               huge != 0 ? "MADV_HUGEPAGE:" : "base pages:", (double)median / 1e6, (double)median / pages,
               (double)BUSTER_FLOORS_BENCHMARK_FAULT_BYTES / (double)median);
    }
    const uint64_t largest = floors_benchmark_copy_sizes[BUSTER_ARRAY_LENGTH(floors_benchmark_copy_sizes) - 1];
    uint8_t* destination = result ? floors_benchmark_map(largest, MADV_HUGEPAGE) : 0;
    result = destination != 0;
    if (result)
    {
        memset(destination, 0, largest);
    }
    static const char* const copy_names[] = {
        "memcpy",
        "non-temporal stores",
        "memset",
    };
    for (uint32_t way = 0; result && way < BUSTER_ARRAY_LENGTH(copy_names); way += 1)
    {
        printf("  %-20s", copy_names[way]);
        for (uint32_t s = 0; s < BUSTER_ARRAY_LENGTH(floors_benchmark_copy_sizes); s += 1)
        {
            const uint64_t size = floors_benchmark_copy_sizes[s];
            const uint64_t repeats = BUSTER_FLOORS_BENCHMARK_COPY_BYTES / size;
            uint64_t times[BUSTER_FLOORS_BENCHMARK_RUNS];
            for (uint32_t run = 0; run < BUSTER_FLOORS_BENCHMARK_RUNS; run += 1)
            {
                const uint64_t start = floors_benchmark_now();
                for (uint64_t repeat = 0; repeat < repeats; repeat += 1)
                {
                    if (way == 0)
                    {
                        memcpy(destination, source, size);
                    }
                    else if (way == 1)
                    {
                        floors_benchmark_stream(destination, source, size);
                    }
                    else
                    {
                        memset(destination, (int)repeat, size);
                    }
                    // Each copy happens: the compiler mustn't merge the repeats.
                    __asm__ volatile("" ::: "memory");
                }
                times[run] = floors_benchmark_now() - start;
            }
            const uint64_t median = floors_benchmark_median(times, BUSTER_FLOORS_BENCHMARK_RUNS);
            printf(" %8llu KB %6.2f GB/s", (unsigned long long)(size >> 10), (double)(size * repeats) / (double)median);
        }
        printf("\n");
    }
    if (destination != 0)
    {
        munmap(destination, largest);
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// Teardown

// The child runs on `other_cpu`, another CPU than this process's `cpu` when it may use one, as a linker's caller waits
// beside it. Its message is the time it sent it: on one CPU, the child may tear down its memory and exit before this
// process reads the message.
static bool floors_benchmark_teardown(uint32_t cpu, uint32_t other_cpu)
{
    bool result = true;
    printf("Teardown, from a child's last message to waitpid's return, the child on CPU %u%s, ms:\n", other_cpu, other_cpu == cpu ? " (this process's too)" : "");
    for (uint32_t huge = 0; result && huge < 2; huge += 1)
    {
        printf("  %-14s", huge != 0 ? "MADV_HUGEPAGE" : "base pages");
        for (uint32_t s = 0; result && s < BUSTER_ARRAY_LENGTH(floors_benchmark_teardown_sizes); s += 1)
        {
            const uint64_t size = floors_benchmark_teardown_sizes[s];
            uint64_t times[BUSTER_FLOORS_BENCHMARK_LARGE_RUNS];
            for (uint32_t run = 0; result && run < BUSTER_FLOORS_BENCHMARK_LARGE_RUNS; run += 1)
            {
                int pipes[2] = { -1, -1 };
                result = pipe(pipes) == 0;
                const pid_t child = result ? fork() : -1;
                if (child == 0)
                {
                    close(pipes[0]);
                    lane_pin(other_cpu);
                    uint8_t* bytes = floors_benchmark_map(size, huge != 0 ? MADV_HUGEPAGE : MADV_NOHUGEPAGE);
                    for (uint64_t offset = 0; bytes != 0 && offset < size; offset += BUSTER_FLOORS_BENCHMARK_PAGE_BYTES)
                    {
                        bytes[offset] = 1;
                    }
                    // 0 if it had no memory.
                    const uint64_t sent = bytes != 0 ? floors_benchmark_now() : 0;
                    const ssize_t told = write(pipes[1], &sent, sizeof(sent));
                    _exit(told == sizeof(sent) && sent != 0 ? 0 : 1);
                }
                // Without this process's write end, the read sees the end of file if the child dies before its message.
                if (pipes[1] >= 0)
                {
                    close(pipes[1]);
                }
                uint64_t sent = 0;
                result = child > 0 && read(pipes[0], &sent, sizeof(sent)) == sizeof(sent) && sent != 0;
                int status = 0;
                result = child > 0 && waitpid(child, &status, 0) == child && result && WIFEXITED(status) && WEXITSTATUS(status) == 0;
                times[run] = floors_benchmark_now() - sent;
                if (pipes[0] >= 0)
                {
                    close(pipes[0]);
                }
            }
            const uint64_t median = result ? floors_benchmark_median(times, BUSTER_FLOORS_BENCHMARK_LARGE_RUNS) : 0;
            printf(" %6llu MB %8.2f", (unsigned long long)(size >> 20), (double)median / 1e6);
            fflush(stdout);
        }
        printf("\n");
    }
    if (!result)
    {
        printf("  a child couldn't touch its memory, or died before its message or its exit\n");
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The command

typedef enum FloorsBenchmarkSection
{
    FLOORS_BENCHMARK_FILES,
    FLOORS_BENCHMARK_THREADS,
    FLOORS_BENCHMARK_CALLS,
    FLOORS_BENCHMARK_MEMORY,
    FLOORS_BENCHMARK_TEARDOWN,
    FLOORS_BENCHMARK_SECTIONS,
} FloorsBenchmarkSection;

static const char* const floors_benchmark_section_names[FLOORS_BENCHMARK_SECTIONS] = {
    [FLOORS_BENCHMARK_FILES] = "files",
    [FLOORS_BENCHMARK_THREADS] = "threads",
    [FLOORS_BENCHMARK_CALLS] = "calls",
    [FLOORS_BENCHMARK_MEMORY] = "memory",
    [FLOORS_BENCHMARK_TEARDOWN] = "teardown",
};

int run_floors_benchmark(int argc, char** argv);

int run_floors_benchmark(int argc, char** argv)
{
    const char* directory = getenv("TMPDIR") != 0 ? getenv("TMPDIR") : "/tmp";
    uint32_t sections = (1u << FLOORS_BENCHMARK_SECTIONS) - 1;
    unsigned long long cpu = 2;
    bool valid = true;
    for (int i = 1; valid && i < argc; i += 1)
    {
        const bool has_value = i + 1 < argc;
        if (strcmp(argv[i], "--directory") == 0 && has_value)
        {
            directory = argv[i + 1];
            i += 1;
        }
        else if (strcmp(argv[i], "--cpu") == 0 && has_value)
        {
            char* end = 0;
            cpu = strtoull(argv[i + 1], &end, 10);
            valid = argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9' && *end == 0 && cpu < BUSTER_LANE_CPU_MASK_WORDS * 64;
            i += 1;
        }
        else if (strcmp(argv[i], "--only") == 0 && has_value)
        {
            sections = 0;
            for (uint32_t section = 0; section < FLOORS_BENCHMARK_SECTIONS; section += 1)
            {
                sections |= strcmp(argv[i + 1], floors_benchmark_section_names[section]) == 0 ? 1u << section : 0;
            }
            valid = sections != 0;
            i += 1;
        }
        else
        {
            valid = false;
        }
    }
    // The CPUs this process may use, in order: the threads' writes take the first ones.
    uint64_t allowed[BUSTER_LANE_CPU_MASK_WORDS] = { 0 };
    uint32_t cpus[BUSTER_FLOORS_BENCHMARK_MAX_THREADS] = { 0 };
    uint32_t cpu_count = 0;
    const bool known = valid && syscall(SYS_sched_getaffinity, 0, sizeof(allowed), allowed) > 0;
    for (uint32_t c = 0; known && c < BUSTER_LANE_CPU_MASK_WORDS * 64; c += 1)
    {
        const bool usable = (allowed[c / 64] >> (c % 64) & 1) != 0 && cpu_count < BUSTER_FLOORS_BENCHMARK_MAX_THREADS;
        cpus[usable ? cpu_count : 0] = usable ? c : cpus[0];
        cpu_count += usable ? 1 : 0;
    }
    // Only valid arguments have --cpu in range.
    const bool pin = known && lane_pin((uint32_t)cpu);
    // The output's bytes, in memory touched beforehand, which the copies read too.
    const bool sourced = (sections & ~(1u << FLOORS_BENCHMARK_TEARDOWN)) != 0;
    uint8_t* source = pin && sourced ? floors_benchmark_map(BUSTER_FLOORS_BENCHMARK_SOURCE_BYTES, MADV_HUGEPAGE) : 0;
    for (uint64_t offset = 0; source != 0 && offset < BUSTER_FLOORS_BENCHMARK_SOURCE_BYTES; offset += 8)
    {
        const uint64_t value = offset * BUSTER_FLOORS_BENCHMARK_GOLDEN_RATIO;
        memcpy(source + offset, &value, 8);
    }
    bool result = pin && (!sourced || source != 0);
    if (!valid)
    {
        fprintf(stderr, "usage: benchmark-floors [--directory PATH] [--cpu N] [--only files|threads|calls|memory|teardown]\n");
    }
    else if (!result)
    {
        fprintf(stderr, "benchmark-floors: can't run on CPU %llu, or no memory for the output's %llu MB\n", cpu,
                (unsigned long long)(BUSTER_FLOORS_BENCHMARK_SOURCE_BYTES >> 20));
    }
    if (result && (sections >> FLOORS_BENCHMARK_FILES & 1) != 0)
    {
        result = floors_benchmark_files(directory, source);
    }
    if (result && (sections >> FLOORS_BENCHMARK_THREADS & 1) != 0)
    {
        result = floors_benchmark_threads(directory, source, cpus, cpu_count);
    }
    if (result && (sections >> FLOORS_BENCHMARK_CALLS & 1) != 0)
    {
        result = floors_benchmark_calls(source, BUSTER_FLOORS_BENCHMARK_SOURCE_BYTES);
    }
    if (result && (sections >> FLOORS_BENCHMARK_MEMORY & 1) != 0)
    {
        result = floors_benchmark_memory(source);
    }
    // A child forked with the output's bytes mapped would tear them down too.
    if (source != 0)
    {
        munmap(source, BUSTER_FLOORS_BENCHMARK_SOURCE_BYTES);
    }
    // The first CPU this process may use but --cpu, for the teardown's children, or --cpu if there is none.
    uint32_t other_cpu = (uint32_t)cpu;
    for (uint32_t c = 0; c < cpu_count; c += 1)
    {
        other_cpu = other_cpu == cpu && cpus[c] != cpu ? cpus[c] : other_cpu;
    }
    if (result && (sections >> FLOORS_BENCHMARK_TEARDOWN & 1) != 0)
    {
        result = floors_benchmark_teardown((uint32_t)cpu, other_cpu);
    }
    return result ? 0 : 1;
}
