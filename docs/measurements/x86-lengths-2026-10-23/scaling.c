// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Standalone independent-function scaling evidence. Compile this same source with either checkout's src include path.
// Each worker owns an identical, independently allocated generated pool. Setup, scalar/reference checks, and five
// warmup passes precede the start barrier. Wall time includes restoring the branches, sizing, encoding, checking lengths,
// and sampling output; it excludes setup and teardown. This is a synthetic pipeline, not a complete compiler benchmark.
// CPU selection uses the process's initial allowed mask. One worker prefers CPU 2; multiple workers take the first
// allowed CPUs. The coordinator takes the first allowed CPU, which it may briefly share with a worker at the barriers.

#define _GNU_SOURCE
#include <errno.h>
#include <sched.h>
#include <pthread.h>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#include "buster/compiler/branch_sizing_benchmark.c"
#pragma clang diagnostic pop

#define BUSTER_SCALING_WORKERS 8
#define BUSTER_SCALING_WARMUP 5
#define BUSTER_SCALING_MAX_PASSES 1000000

typedef struct ScalingRun ScalingRun;
struct ScalingRun
{
    pthread_barrier_t barrier;
    const BranchSizingBenchmarkRow* row;
    uint64_t passes;
    // Workers wait here until creation succeeds for all of them. A failed creation releases them without a barrier.
    int launch;
    bool valid;
};

typedef struct ScalingWorker ScalingWorker;
struct ScalingWorker
{
    _Alignas(128) ScalingRun* run;
    uint32_t cpu;
    const char* failure;
    uint64_t records;
    uint64_t functions;
    uint64_t bytes;
    uint64_t checksum;
    uint64_t reference_checksum;
};

// Before timing, compare the complete encoded bytes and offsets against the scalar encoder. The benchmark's own check
// already compares every function's vector/scalar sizing result, modified records, and offsets.
static bool scaling_reference(Arena* arena, BranchSizingBenchmarkPool* pool, uint64_t* checksum)
{
    const uint64_t mark = arena->length;
    uint8_t* reference = arena_push(arena, BUSTER_X86_ENCODER_CAPACITY(pool->largest), 64);
    uint32_t* offsets = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, pool->largest);
    bool result = reference != 0 && offsets != 0;
    uint64_t hash = 14695981039346656037ull;
    for (uint64_t group = 0; result && group < pool->groups; group += 1)
    {
        branch_sizing_benchmark_prepare(pool, group, true);
        for (uint64_t f = pool->group_starts[group]; result && f < pool->group_starts[group + 1]; f += 1)
        {
            const X86Instruction* records = pool->records + pool->record_starts[f];
            const uint64_t count = pool->record_starts[f + 1] - pool->record_starts[f];
            const int64_t scalar = x86_encode_scalar(records, count, reference, BUSTER_X86_ENCODER_CAPACITY(count), offsets, 0);
            const int64_t vector = x86_encode(records, count, pool->encoded, BUSTER_X86_ENCODER_CAPACITY(count), pool->offsets, 0);
            result = scalar >= 0 && vector == scalar && memcmp(reference, pool->encoded, (uint64_t)scalar) == 0 &&
                     memcmp(offsets, pool->offsets, count * sizeof(uint32_t)) == 0;
            for (uint64_t b = 0; result && b < (uint64_t)scalar; b += 1)
            {
                hash = (hash ^ reference[b]) * 1099511628211ull;
            }
        }
    }
    *checksum = hash;
    arena_pop_to(arena, mark);
    return result;
}

static bool scaling_pass(BranchSizingBenchmarkPool* pool, uint64_t expected_bytes, uint64_t* checksum)
{
    bool result = true;
    uint64_t bytes = 0;
    uint64_t hash = *checksum;
    for (uint64_t group = 0; result && group < pool->groups; group += 1)
    {
        hash += branch_sizing_benchmark_prepare(pool, group, false);
        for (uint64_t f = pool->group_starts[group]; result && f < pool->group_starts[group + 1]; f += 1)
        {
            X86Instruction* records = pool->records + pool->record_starts[f];
            const uint64_t count = pool->record_starts[f + 1] - pool->record_starts[f];
            const uint32_t* branches = pool->branches + pool->branch_starts[f];
            const uint32_t* targets = pool->targets + pool->branch_starts[f];
            const uint64_t branch_count = pool->branch_starts[f + 1] - pool->branch_starts[f];
            const BranchSizing sized = size_branches(records, count, branches, targets, branch_count, pool->offsets);
            result = sized.error == BRANCH_SIZING_OK;
            if (result)
            {
                const int64_t length = x86_encode(records, count, pool->encoded, BUSTER_X86_ENCODER_CAPACITY(count), pool->offsets, 0);
                result = length >= 0 && (uint64_t)length == sized.length;
                if (result)
                {
                    bytes += (uint64_t)length;
                    // Make all stores observable without adding a byte-by-byte hash to the timed pipeline.
                    __asm__ volatile("" : : "r"(pool->encoded), "r"(pool->offsets) : "memory");
                    hash = hash * 1099511628211ull + (uint64_t)length;
                    if (length != 0)
                    {
                        hash ^= (uint64_t)pool->encoded[0] | ((uint64_t)pool->encoded[(uint64_t)length / 2] << 8) |
                                ((uint64_t)pool->encoded[(uint64_t)length - 1] << 16);
                    }
                    if (count != 0)
                    {
                        hash += pool->offsets[count - 1];
                    }
                }
            }
        }
    }
    *checksum = hash;
    result = result && bytes == expected_bytes;
    return result;
}

static void* scaling_worker(void* argument)
{
    ScalingWorker* worker = argument;
    ScalingRun* run = worker->run;
    int launch = 0;
    while (launch == 0)
    {
        launch = __atomic_load_n(&run->launch, __ATOMIC_ACQUIRE);
        _mm_pause();
    }
    if (launch > 0)
    {
        cpu_set_t cpus;
        CPU_ZERO(&cpus);
        CPU_SET(worker->cpu, &cpus);
        worker->failure = pthread_setaffinity_np(pthread_self(), sizeof(cpus), &cpus) == 0 ? 0 : "CPU affinity";
        Arena arena = { 0 };
        BranchSizingBenchmarkPool pool = { 0 };
        BranchSizingBenchmarkStatistics statistics = { 0 };
        if (worker->failure == 0)
        {
            arena = arena_reserve(BUSTER_BRANCH_SIZING_BENCHMARK_ARENA_BYTES);
            pool = branch_sizing_benchmark_pool(&arena, run->row, 1);
            worker->failure = pool.valid ? 0 : "pool allocation";
        }
        if (worker->failure == 0)
        {
            const bool checked = branch_sizing_benchmark_check(&arena, &pool, BUSTER_BRANCH_SIZING_ROUNDS, &statistics) &&
                                 scaling_reference(&arena, &pool, &worker->reference_checksum);
            worker->failure = checked ? 0 : "scalar/reference check";
        }
        worker->records = pool.records_total;
        worker->functions = pool.functions;
        worker->bytes = statistics.bytes;
        uint64_t checksum = 0;
        for (uint32_t pass = 0; worker->failure == 0 && pass < BUSTER_SCALING_WARMUP; pass += 1)
        {
            worker->failure = scaling_pass(&pool, statistics.bytes, &checksum) ? 0 : "warmup pass";
        }
        pthread_barrier_wait(&run->barrier);
        pthread_barrier_wait(&run->barrier);
        checksum = 0;
        for (uint64_t pass = 0; run->valid && worker->failure == 0 && pass < run->passes; pass += 1)
        {
            worker->failure = scaling_pass(&pool, statistics.bytes, &checksum) ? 0 : "timed pass";
        }
        worker->checksum = checksum;
        pthread_barrier_wait(&run->barrier);
        scratch_release();
        arena_release(&arena);
    }
    return 0;
}

static bool scaling_parse(const char* text, uint64_t* value)
{
    char* end = 0;
    errno = 0;
    const unsigned long long parsed = strtoull(text, &end, 10);
    const bool result = text[0] >= '0' && text[0] <= '9' && end != text && *end == 0 && errno != ERANGE;
    *value = result ? parsed : *value;
    return result;
}

static uint64_t scaling_time(void)
{
    struct timespec now = { 0 };
    const int error = clock_gettime(CLOCK_MONOTONIC_RAW, &now);
    const uint64_t result = error == 0 ? (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec : 0;
    return result;
}

int main(int argc, char** argv)
{
    uint64_t count = 1;
    uint64_t passes = 200;
    const char* row_name = argc > 3 ? argv[3] : "tier0-mix";
    bool valid = argc <= 4 && (argc < 2 || scaling_parse(argv[1], &count)) && (argc < 3 || scaling_parse(argv[2], &passes));
    valid = valid && count >= 1 && count <= BUSTER_SCALING_WORKERS && passes >= 1 && passes <= BUSTER_SCALING_MAX_PASSES;
    ScalingRun run = {
        .passes = passes,
    };
    for (uint64_t r = 0; r < sizeof(branch_sizing_benchmark_rows) / sizeof(branch_sizing_benchmark_rows[0]); r += 1)
    {
        if (strcmp(row_name, branch_sizing_benchmark_rows[r].name) == 0)
        {
            run.row = &branch_sizing_benchmark_rows[r];
        }
    }
    valid = valid && run.row != 0;
    if (!valid)
    {
        fprintf(stderr, "usage: %s [workers 1..8] [passes 1..1000000] [benchmark row; default tier0-mix]\n", argv[0]);
    }
    if (valid)
    {
        valid = x86_encoder_initialize();
        if (!valid)
        {
            fprintf(stderr, "scaling: encoder initialization failed\n");
        }
    }
    // Save the job's whole allowed set before pinning the coordinator: worker threads inherit its narrower affinity.
    uint32_t worker_cpus[BUSTER_SCALING_WORKERS] = { 0 };
    uint32_t coordinator_cpu = 0;
    cpu_set_t allowed;
    CPU_ZERO(&allowed);
    if (valid)
    {
        valid = sched_getaffinity(0, sizeof(allowed), &allowed) == 0;
        if (!valid)
        {
            fprintf(stderr, "scaling: cannot read the allowed CPUs: %s\n", strerror(errno));
        }
    }
    if (valid)
    {
        uint32_t available = 0;
        for (uint32_t cpu = 0; cpu < CPU_SETSIZE; cpu += 1)
        {
            if (CPU_ISSET(cpu, &allowed))
            {
                if (available < BUSTER_SCALING_WORKERS)
                {
                    worker_cpus[available] = cpu;
                }
                available += 1;
            }
        }
        valid = available >= count;
        if (!valid)
        {
            fprintf(stderr, "scaling: %llu workers need distinct CPUs, but only %u are allowed\n", (unsigned long long)count, available);
        }
        else
        {
            coordinator_cpu = worker_cpus[0];
            if (count == 1 && CPU_ISSET(2, &allowed))
            {
                worker_cpus[0] = 2;
            }
            cpu_set_t cpus;
            CPU_ZERO(&cpus);
            CPU_SET(coordinator_cpu, &cpus);
            valid = pthread_setaffinity_np(pthread_self(), sizeof(cpus), &cpus) == 0;
            if (!valid)
            {
                fprintf(stderr, "scaling: coordinator CPU %u is unavailable\n", coordinator_cpu);
            }
        }
    }
    const bool barrier_created = valid && pthread_barrier_init(&run.barrier, 0, (unsigned int)count + 1) == 0;
    if (valid && !barrier_created)
    {
        fprintf(stderr, "scaling: barrier initialization failed\n");
    }
    valid = valid && barrier_created;
    pthread_t threads[BUSTER_SCALING_WORKERS];
    ScalingWorker workers[BUSTER_SCALING_WORKERS] = { 0 };
    uint64_t created = 0;
    while (valid && created < count)
    {
        workers[created].run = &run;
        workers[created].cpu = worker_cpus[created];
        const int error = pthread_create(&threads[created], 0, scaling_worker, &workers[created]);
        valid = error == 0;
        created += valid;
        if (!valid)
        {
            fprintf(stderr, "scaling: pthread_create failed: %s\n", strerror(error));
        }
    }
    __atomic_store_n(&run.launch, valid ? 1 : -1, __ATOMIC_RELEASE);
    uint64_t elapsed = 0;
    if (valid)
    {
        pthread_barrier_wait(&run.barrier);
        for (uint64_t w = 0; w < count; w += 1)
        {
            valid = valid && workers[w].failure == 0 && workers[w].records == workers[0].records && workers[w].bytes == workers[0].bytes &&
                    workers[w].reference_checksum == workers[0].reference_checksum;
        }
        run.valid = valid;
        const uint64_t begin = scaling_time();
        pthread_barrier_wait(&run.barrier);
        pthread_barrier_wait(&run.barrier);
        const uint64_t end = scaling_time();
        valid = valid && begin != 0 && end > begin;
        elapsed = end - begin;
    }
    for (uint64_t w = 0; w < created; w += 1)
    {
        const int error = pthread_join(threads[w], 0);
        valid = valid && error == 0 && workers[w].failure == 0 && workers[w].checksum == workers[0].checksum;
        if (error != 0 || workers[w].failure != 0)
        {
            fprintf(stderr, "scaling: worker %llu on CPU %u failed: %s\n", (unsigned long long)w, workers[w].cpu,
                    error != 0 ? strerror(error) : workers[w].failure);
        }
    }
    if (barrier_created)
    {
        pthread_barrier_destroy(&run.barrier);
    }
    if (valid)
    {
        const uint64_t records = workers[0].records * count * passes;
        uint64_t checksum = 0;
        for (uint64_t w = 0; w < count; w += 1)
        {
            checksum += workers[w].checksum * (w + 1);
        }
        printf("row=%s workers=%llu passes=%llu seed=1 warmup=%u cpu_first=%u cpu_last=%u functions_per_worker=%llu records_per_worker=%llu "
               "encoded_bytes_per_worker=%llu wall_ns=%llu records=%llu records/s=%.3f ns/record=%.6f checksum=%016llx reference=%016llx "
               "coordinator_cpu=%u worker_cpus=",
               row_name, (unsigned long long)count, (unsigned long long)passes, BUSTER_SCALING_WARMUP, workers[0].cpu, workers[count - 1].cpu,
               (unsigned long long)workers[0].functions, (unsigned long long)workers[0].records, (unsigned long long)workers[0].bytes,
               (unsigned long long)elapsed, (unsigned long long)records, (double)records * 1000000000.0 / (double)elapsed,
               (double)elapsed / (double)records, (unsigned long long)checksum, (unsigned long long)workers[0].reference_checksum, coordinator_cpu);
        for (uint64_t w = 0; w < count; w += 1)
        {
            printf("%s%u", w == 0 ? "" : ",", workers[w].cpu);
        }
        putchar('\n');
    }
    else
    {
        fprintf(stderr, "scaling: no valid measurement\n");
    }
    return valid ? 0 : 1;
}
