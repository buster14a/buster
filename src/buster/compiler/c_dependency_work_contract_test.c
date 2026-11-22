// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Standalone admission contracts for #2041, using the real driver from #1314. These fixtures exercise the admission
// operation, not compilation throughput: each distinct source/object/destination must be admitted, while a conflict
// at the end of the protected set must be rejected with its diagnostic. No fixture or counter is a compiler input.

#define _GNU_SOURCE
#include <stdlib.h>
#include <sys/stat.h>

#include "work_contract.h"
#include "host_isa.h"
#include "lane.c"

// Count the filesystem calls actually made, independently of the driver's optional probe parameter. Function-like
// macros leave `struct stat` alone. Both wrappers and the macro replacements exist in this standalone test only.
static uint64_t dependency_work_probes;

static int dependency_work_stat(const char* path, struct stat* status)
{
    dependency_work_probes += 1;
    return stat(path, status);
}

static int dependency_work_lstat(const char* path, struct stat* status)
{
    dependency_work_probes += 1;
    return lstat(path, status);
}

#define stat(path, status) dependency_work_stat((path), (status))
#define lstat(path, status) dependency_work_lstat((path), (status))
#ifdef BUSTER_WORK_DEPENDENCY_SOURCE
#include BUSTER_WORK_DEPENDENCY_SOURCE
#else
#include "c_dependency_driver.c"
#endif
#undef lstat
#undef stat

// Capture the real diagnostic too: replacing refusal by success, or suppressing the diagnostic, must fail even if
// it saves all the measured work. Restore stderr before reporting the contract; fixture failures are never skips.
static bool dependency_work_refuses(const DependencyOptions* options, char** sources, uint32_t count, Arena* arena)
{
    FILE* errors = tmpfile();
    const int saved = dup(STDERR_FILENO);
    const bool flushed = fflush(stderr) == 0;
    bool valid = errors != NULL && saved >= 0 && flushed;
    if (valid)
    {
        valid = dup2(fileno(errors), STDERR_FILENO) == STDERR_FILENO;
    }
    if (valid)
    {
        arena_pop_to(arena, 0);
        const bool admitted = dependency_check_destinations(options, sources, count, NULL, true, false, arena);
        const bool written = fflush(stderr) == 0;
        char message[256] = { 0 };
        rewind(errors);
        const size_t length = fread(message, 1, sizeof(message) - 1, errors);
        valid = !admitted && written && length != 0 && !ferror(errors) &&
                strstr(message, "error: dependency output is empty, unavailable, or aliases an input or primary output") != NULL;
    }
    if (saved >= 0)
    {
        valid = dup2(saved, STDERR_FILENO) == STDERR_FILENO && valid;
        valid = close(saved) == 0 && valid;
    }
    if (errors != NULL)
    {
        valid = fclose(errors) == 0 && valid;
    }
    return valid;
}

static bool dependency_work_write(const char* path)
{
    FILE* output = fopen(path, "wb");
    bool valid = output != NULL;
    if (output != NULL)
    {
        valid = fputs("protected\n", output) >= 0;
        valid = fclose(output) == 0 && valid;
    }
    return valid;
}

static bool dependency_work_intact(const char* path)
{
    FILE* input = fopen(path, "rb");
    bool valid = input != NULL;
    if (input != NULL)
    {
        char bytes[16] = { 0 };
        const size_t length = fread(bytes, 1, sizeof(bytes), input);
        valid = length == 10 && memcmp(bytes, "protected\n", 10) == 0 && !ferror(input);
        valid = fclose(input) == 0 && valid;
    }
    return valid;
}

static bool dependency_work_semantics(char** sources, uint32_t count, Arena* arena)
{
    DependencyOptions options = { .enabled = true };
    char destination[32];
    char object[32];
    char parent_alias[32];
    snprintf(destination, sizeof(destination), "unit-%04u.d", count - 1);
    snprintf(object, sizeof(object), "unit-%04u.o", count - 1);
    snprintf(parent_alias, sizeof(parent_alias), "./unit-%04u.d", count - 1);
    const char* const last = sources[count - 1];

    // Existing inode aliases at the final destination and final protected source/output, well past the first
    // destination's streaming check. An implementation which drops indexed lookups cannot pass these controls.
    bool source_ready = dependency_work_write(last) && link(last, destination) == 0;
    const bool source_refused = source_ready && dependency_work_refuses(&options, sources, count, arena);
    const bool source_intact = source_ready && dependency_work_intact(last);
    unlink(destination);
    unlink(last);

    bool object_ready = dependency_work_write(object) && link(object, destination) == 0;
    const bool object_refused = object_ready && dependency_work_refuses(&options, sources, count, arena);
    const bool object_intact = object_ready && dependency_work_intact(object);
    unlink(destination);
    unlink(object);

    // A nonexistent input with a different spelling still aliases its destination by parent identity and basename.
    sources[count - 1] = parent_alias;
    const bool parent_refused = dependency_work_refuses(&options, sources, count, arena);
    sources[count - 1] = (char*)last;

    const bool dangling_ready = symlink("missing-target", destination) == 0;
    const bool dangling_refused = dangling_ready && dependency_work_refuses(&options, sources, count, arena);
    unlink(destination);

    // Shared stdout has no protected filesystem destination and must remain a successful, allocation-free case.
    options.path = "-";
    arena_pop_to(arena, 0);
    const uint64_t before = dependency_work_probes;
    const bool stdout_admitted = dependency_check_destinations(&options, sources, count, NULL, true, false, arena);
    const bool valid = source_ready && source_refused && source_intact && object_ready && object_refused && object_intact &&
                       parent_refused && dangling_ready && dangling_refused && stdout_admitted && arena->length == 0 && dependency_work_probes == before;
    return valid;
}

static int dependency_work_contract_tests(void)
{
    const uint32_t sizes[] = { 256, 512, 1024 };
    char directory[] = "/tmp/buster-work-dependency-XXXXXX";
    const int saved_directory = open(".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    const bool created = mkdtemp(directory) != NULL;
    const bool entered = created && chdir(directory) == 0;
    Arena arena = arena_reserve(UINT64_C(2) << 20);
    char names[1024][32];
    char* sources[1024];
    for (uint32_t i = 0; i < 1024; i += 1)
    {
        snprintf(names[i], sizeof(names[i]), "unit-%04u.c", i);
        sources[i] = names[i];
    }
    const bool ready = saved_directory >= 0 && entered && arena.base != NULL;
    uint64_t previous_probes = 0;
    uint64_t previous_comparisons = 0;
    uint64_t previous_bytes = 0;
    uint32_t failures = 0;
    for (uint32_t i = 0; i < 3; i += 1)
    {
        const uint32_t count = sizes[i];
        uint64_t driver_probes = 0;
        uint64_t comparisons = 0;
        dependency_work_probes = 0;
        arena_pop_to(&arena, 0);
        const DependencyOptions options = { .enabled = true };
        const bool admitted = ready && dependency_check_destinations_counted(&options, sources, count, NULL, true, false, &arena, &driver_probes, &comparisons);
        const uint64_t probes = dependency_work_probes;
        const uint64_t bytes = arena.length;
        const bool controls = ready && dependency_work_semantics(sources, count, &arena);
        const bool semantic = admitted && controls && probes != 0 && driver_probes == probes && comparisons != 0;
        uint32_t levels = 0;
        for (uint32_t remaining = count * 2; remaining > 1; remaining = (remaining + 1) / 2)
        {
            levels += 1;
        }
        // The source shape has bounded-length names: two new names, two protected identities, and three pointer
        // columns per identity. Keep the established driver's slack for names and the three alignment boundaries.
        const uint64_t storage_bound = (uint64_t)count * (80 + 2 * sizeof(DependencyIdentity) + 6 * sizeof(const DependencyIdentity*)) + 3 * _Alignof(DependencyIdentity);
        const WorkContract records[] = {
            {
                .fixture = "dependency-distinct",
                .metric = "filesystem-probes",
                .input = count,
                .actual = probes,
                .bound = 9 * (uint64_t)count,
                .semantic = semantic,
            },
            {
                .fixture = "dependency-distinct",
                .metric = "identity-comparisons",
                .input = count,
                .actual = comparisons,
                .bound = (uint64_t)count * (32 + 20 * levels),
                .semantic = semantic,
            },
            {
                .fixture = "dependency-distinct",
                .metric = "arena-bytes",
                .input = count,
                .actual = bytes,
                .bound = storage_bound,
                .semantic = semantic,
            },
        };
        for (uint32_t record = 0; record < 3; record += 1)
        {
            failures += !work_contract_report(stdout, records + record);
        }
        if (i != 0)
        {
            const WorkContract growth[] = {
                {
                    .fixture = "dependency-doubling",
                    .metric = "filesystem-probes",
                    .input = count,
                    .actual = probes,
                    .bound = 2 * previous_probes,
                    .semantic = semantic && previous_probes != 0,
                },
                {
                    .fixture = "dependency-doubling",
                    .metric = "identity-comparisons",
                    .input = count,
                    .actual = comparisons,
                    .bound = 3 * previous_comparisons,
                    .semantic = semantic && previous_comparisons != 0,
                },
                {
                    .fixture = "dependency-doubling",
                    .metric = "arena-bytes",
                    .input = count,
                    .actual = bytes,
                    .bound = 2 * previous_bytes + 3 * _Alignof(DependencyIdentity),
                    .semantic = semantic && previous_bytes != 0,
                },
            };
            for (uint32_t record = 0; record < 3; record += 1)
            {
                failures += !work_contract_report(stdout, growth + record);
            }
        }
        previous_probes = probes;
        previous_comparisons = comparisons;
        previous_bytes = bytes;
    }
    arena_release(&arena);
    if (saved_directory >= 0)
    {
        failures += fchdir(saved_directory) != 0;
        failures += close(saved_directory) != 0;
    }
    if (created)
    {
        failures += rmdir(directory) != 0;
    }
    return failures == 0 ? 0 : 1;
}
