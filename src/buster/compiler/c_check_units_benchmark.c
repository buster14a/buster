// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// benchmark-check-units [--executable PATH] [--units N] [--functions N] [--lanes N,N...] [--runs N] [-- OPTION...]
// (#1128): one command of several translation units by each lane count, so that a regression where the lanes stop
// scaling shows. The command compiles a program of synth's shape (check_units_test_synth: --units, 64 by default, of
// --functions, 200) with -c and the options after `--` (the CI step's frozen headers and predefines; their paths
// absolute, as the processes run in the program's directory), as whole processes of --executable (this compiler by
// default), by each of --lanes' counts (1,2,4,8 by default), --runs times each (5 by default) after a warm-up, in
// time-runs' rotated order (profile_time_runs, which prints each run and each count's medians). Then each count against
// the first's: how many times faster its wall time is, and how many times more system time it took. Lanes that
// serialize on the kernel (#1128: the address space's lock, which arena reservations, commits and discards take) raise
// the system time, where the wall time on a busy machine may hide it. Fails if a process fails or the program can't be
// written.

#define BUSTER_CHECK_UNITS_BENCHMARK_LANE_COUNTS 8

static int run_check_units_benchmark(int argc, char** argv)
{
    CheckUnitsTestTally tally = { .random = 0x9e3779b97f4a7c15ull };
    const char* executable = "/proc/self/exe";
    uint32_t units = 64;
    uint32_t functions = 200;
    uint64_t runs = 5;
    uint32_t lanes[BUSTER_CHECK_UNITS_BENCHMARK_LANE_COUNTS] = { 1, 2, 4, 8 };
    uint32_t lane_count = 4;
    char** options = 0;
    uint32_t option_count = 0;
    bool parsed = true;
    for (int i = 0; parsed && options == 0 && i < argc; i += 1)
    {
        if (strcmp(argv[i], "--") == 0)
        {
            options = argv + i + 1;
            option_count = (uint32_t)(argc - i - 1);
        }
        else if (strcmp(argv[i], "--executable") == 0 && i + 1 < argc)
        {
            executable = argv[++i];
        }
        else if (strcmp(argv[i], "--units") == 0 && i + 1 < argc)
        {
            units = (uint32_t)strtoul(argv[++i], 0, 0);
        }
        else if (strcmp(argv[i], "--functions") == 0 && i + 1 < argc)
        {
            functions = (uint32_t)strtoul(argv[++i], 0, 0);
        }
        else if (strcmp(argv[i], "--runs") == 0 && i + 1 < argc)
        {
            runs = strtoull(argv[++i], 0, 0);
        }
        else if (strcmp(argv[i], "--lanes") == 0 && i + 1 < argc)
        {
            char* at = argv[++i];
            lane_count = 0;
            while (parsed && *at != 0)
            {
                char* end = at;
                const unsigned long value = strtoul(at, &end, 10);
                parsed = end != at && value != 0 && value <= 4096 && lane_count < BUSTER_CHECK_UNITS_BENCHMARK_LANE_COUNTS && (*end == 0 || *end == ',');
                lanes[lane_count] = (uint32_t)value;
                lane_count += parsed;
                at = *end == ',' ? end + 1 : end;
            }
            parsed = parsed && lane_count != 0;
        }
        else
        {
            parsed = false;
        }
    }
    parsed = parsed && units != 0 && units <= 4096 && functions != 0 && runs != 0;
    if (!parsed)
    {
        fprintf(stderr, "usage: benchmark-check-units [--executable PATH] [--units N] [--functions N] [--lanes N,N...] [--runs N] [-- OPTION...]\n");
    }
    char resolved[PATH_MAX];
    const ssize_t resolved_length = parsed && strcmp(executable, "/proc/self/exe") == 0 ? readlink(executable, resolved, sizeof(resolved) - 1) : -1;
    if (resolved_length > 0)
    {
        resolved[resolved_length] = 0;
        executable = resolved;
    }
    char absolute[PATH_MAX];
    if (parsed && executable[0] != '/' && realpath(executable, absolute) != NULL)
    {
        executable = absolute;
    }
    // The program, in a directory of its own that the processes run in, so that their arguments are the units' names and
    // their objects land beside them.
    char directory[PATH_MAX];
    snprintf(directory, sizeof(directory), "%s/buster-benchmark-check-units-XXXXXX", oracle_temporary_directory());
    const bool made = parsed && mkdtemp(directory) != NULL;
    const bool written = made && check_units_test_synth(&tally, directory, units, functions);
    // Each list: --lanes N, the options, -c, unit_0.c ... unit_{units - 1}.c, the names shared by every list.
    const uint64_t list_length = (uint64_t)units + option_count + 3;
    Arena arena = written ? arena_reserve(64ull << 20) : (Arena){ 0 };
    char** names = BUSTER_ARENA_PUSH_ARRAY(&arena, char*, units);
    char** arguments = BUSTER_ARENA_PUSH_ARRAY(&arena, char*, lane_count * list_length);
    uint64_t* starts = BUSTER_ARENA_PUSH_ARRAY(&arena, uint64_t, lane_count);
    uint64_t* counts = BUSTER_ARENA_PUSH_ARRAY(&arena, uint64_t, lane_count);
    uint64_t* medians = BUSTER_ARENA_PUSH_ARRAY(&arena, uint64_t, (uint64_t)lane_count * BUSTER_PROFILE_TIME_FIELDS);
    int* statuses = BUSTER_ARENA_PUSH_ARRAY(&arena, int, lane_count);
    char* lane_text = BUSTER_ARENA_PUSH_ARRAY(&arena, char, (uint64_t)lane_count * 16);
    bool ready = names != 0 && arguments != 0 && starts != 0 && counts != 0 && medians != 0 && statuses != 0 && lane_text != 0;
    for (uint32_t u = 0; ready && u < units; u += 1)
    {
        names[u] = arena_push(&arena, 32, 1);
        ready = names[u] != 0;
        if (ready)
        {
            snprintf(names[u], 32, "unit_%u.c", u);
        }
    }
    for (uint32_t l = 0; ready && l < lane_count; l += 1)
    {
        snprintf(lane_text + 16 * l, 16, "%u", lanes[l]);
        starts[l] = l * list_length;
        counts[l] = list_length;
        char** list = arguments + starts[l];
        list[0] = "--lanes";
        list[1] = lane_text + 16 * l;
        for (uint32_t o = 0; o < option_count; o += 1)
        {
            list[2 + o] = options[o];
        }
        list[2 + option_count] = "-c";
        for (uint32_t u = 0; u < units; u += 1)
        {
            list[3 + option_count + u] = names[u];
        }
    }
    const int saved_directory = ready ? open(".", O_RDONLY | O_DIRECTORY | O_CLOEXEC) : -1;
    ready = ready && saved_directory >= 0 && chdir(directory) == 0;
    uint64_t failures = !ready;
    if (ready)
    {
        printf("check units: %u units of %u functions (synth's shape), -c by --lanes", units, functions);
        for (uint32_t l = 0; l < lane_count; l += 1)
        {
            printf("%s%u", l == 0 ? " " : ", ", lanes[l]);
        }
        printf(", %llu runs each after a warm-up, as processes of %s\n", (unsigned long long)runs, executable);
        fflush(stdout);
        const char* const executables[] = { executable };
        const uint64_t zero[] = { 0 };
        const char* const build_names[] = { "units" };
        const ProfileTimeRuns t = {
            .executables = executables,
            .builds = zero,
            .placements = zero,
            .executable_count = 1,
            .build_names = build_names,
            .build_count = 1,
            .arguments = arguments,
            .starts = starts,
            .counts = counts,
            .list_count = lane_count,
            .runs = runs,
            .warmups = 1,
            .output = STDOUT_FILENO,
            .medians = medians,
            .statuses = statuses,
        };
        failures = profile_time_runs(&arena, &t);
        // Every process the same as its count's first, which must have compiled the units: time-runs takes a status
        // that stays the same, a failure among them, for a pass.
        for (uint32_t l = 0; l < lane_count; l += 1)
        {
            const int status = statuses[profile_time_slot_index(&t, 0, 0, l)];
            const bool compiled = status != -1 && WIFEXITED(status) && WEXITSTATUS(status) == 0;
            failures += !compiled;
            if (!compiled)
            {
                fprintf(stderr, "benchmark-check-units: --lanes %u's command failed (status %d)\n", lanes[l], status != -1 && WIFEXITED(status) ? WEXITSTATUS(status) : -1);
            }
        }
        // Against the first count's, one lane's by default.
        const uint64_t* first = medians + profile_time_slot_index(&t, 0, 0, 0) * BUSTER_PROFILE_TIME_FIELDS;
        for (uint32_t l = 0; failures == 0 && l < lane_count; l += 1)
        {
            const uint64_t* row = medians + profile_time_slot_index(&t, 0, 0, l) * BUSTER_PROFILE_TIME_FIELDS;
            printf("check units, --lanes %u: %.3f ms wall (%.2fx --lanes %u's speed), %.3f ms user, %.3f ms system (%.2fx --lanes %u's), %llu minor faults\n", lanes[l],
                   (double)row[0] / 1e6, row[0] != 0 ? (double)first[0] / (double)row[0] : 0.0, lanes[0], (double)row[1] / 1e3, (double)row[2] / 1e3,
                   first[2] != 0 ? (double)row[2] / (double)first[2] : 0.0, lanes[0], (unsigned long long)row[4]);
        }
    }
    if (saved_directory >= 0)
    {
        failures += fchdir(saved_directory) != 0;
        close(saved_directory);
    }
    arena_release(&arena);
    if (made)
    {
        char* remove[] = { "rm", "-rf", directory, NULL };
        check_units_test_run("/", remove, "/dev/null", "/dev/null", false);
    }
    const bool passed = parsed && written && failures == 0;
    if (parsed && !passed)
    {
        fprintf(stderr, "benchmark-check-units: %s\n", !written ? "can't write the program" : "a process failed or its times couldn't be taken");
    }
    return passed ? 0 : 1;
}
