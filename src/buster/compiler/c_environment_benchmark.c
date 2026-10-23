// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Synthetic environment handoff benchmark. The complete paths include index allocation/construction and queries;
// the type paths include TypeId dependency walking and completion comparisons. Event/source extraction, parsing,
// type construction and the future checker are not timed. This is not corpus or whole-compiler acceptance.
// Uses the original clock, counter record and median helpers in c_bindings_benchmark.c.

static bool c_environment_benchmark_queries(const CEnvironment* environment, const uint32_t* keys, const uint32_t* points, uint32_t count, uint32_t* results, bool scalar)
{
    bool valid = true;
    if (scalar)
    {
        for (uint32_t i = 0; i < count; i += 1)
        {
            const uint32_t key = keys[i];
            results[i] = c_environment_lookup_key(environment, key, points[i]);
        }
    }
    else
    {
        valid = c_environment_lookup_batch(environment, keys, points, count, results);
    }
    return valid;
}

static int run_c_environment_benchmark(int argc, char** argv)
{
    const char* const paths[] = { "index", "scalar", "vector", "full-scalar", "full-vector", "types-scalar", "types-vector" };
    uint32_t only = 7;
    uint32_t names = 4096;
    uint32_t queries = 65536;
    uint32_t history = 4;
    uint32_t after_scalar = 0;
    bool valid = true;
    for (int i = 0; valid && i < argc; i += 1)
    {
        if (strcmp(argv[i], "--only") == 0 && i + 1 < argc)
        {
            only = 7;
            i += 1;
            for (uint32_t path = 0; path < 7; path += 1)
            {
                if (strcmp(argv[i], paths[path]) == 0)
                {
                    only = path;
                }
            }
            valid = only < 7;
        }
        else if (i + 1 < argc && (strcmp(argv[i], "--names") == 0 || strcmp(argv[i], "--queries") == 0 || strcmp(argv[i], "--history") == 0 || strcmp(argv[i], "--after-scalar") == 0))
        {
            uint32_t* output = strcmp(argv[i], "--names") == 0 ? &names : strcmp(argv[i], "--queries") == 0 ? &queries : strcmp(argv[i], "--history") == 0 ? &history : &after_scalar;
            const uint32_t limit = output == &after_scalar ? 1000 : output == &history ? 128 : 1000000;
            char* end = 0;
            errno = 0;
            const unsigned long value = strtoul(argv[++i], &end, 10);
            valid = errno == 0 && end != argv[i] && *end == 0 && value <= limit && (output == &after_scalar || value >= (output == &names ? 2 : 1));
            *output = (uint32_t)value;
        }
        else
        {
            valid = false;
        }
    }
    valid &= (uint64_t)names * history <= 1000000;
    Arena arena = arena_reserve(256ull << 20);
    Arena scratch = arena_reserve(64ull << 20);
    CBindings bindings = { 0 };
    if (valid)
    {
        valid = c_bindings_initialize(&bindings, &arena, names, names * history);
    }
    for (uint32_t generation = 0; valid && generation < history; generation += 1)
    {
        for (uint32_t name = 1; valid && name < names; name += 1)
        {
            const uint32_t point = generation * names + name;
            valid = c_bindings_declare(&bindings, C_BINDING_ORDINARY, name, C_BINDING_OBJECT, point - 1, point, 0) != 0;
        }
    }
    uint32_t* keys = valid ? arena_push_array(&arena, queries, 4, 64) : 0;
    uint32_t* points = keys != 0 ? arena_push_array(&arena, queries, 4, 64) : 0;
    uint32_t* expected = points != 0 ? arena_push_array(&arena, queries, 4, 64) : 0;
    uint32_t* output = expected != 0 ? arena_push_array(&arena, queries, 4, 64) : 0;
    TypeId* requested = output != 0 ? arena_push_array(&arena, queries, 4, 64) : 0;
    uint8_t* complete = requested != 0 ? arena_push_array(&arena, queries, 1, 64) : 0;
    uint8_t* type_expected = complete != 0 ? arena_push_array(&arena, queries, 1, 64) : 0;
    valid = type_expected != 0;
    uint64_t random = 113;
    for (uint32_t i = 0; valid && i < queries; i += 1)
    {
        random = random * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
        keys[i] = 1 + (uint32_t)(random >> 32) % (names - 1);
        points[i] = (uint32_t)random % (names * history + 1);
    }
    CEnvironment environment;
    const uint64_t before_index = arena.length;
    valid = valid && c_environment_build(&environment, &bindings, &arena, &scratch);
    const uint64_t index_bytes = arena.length - before_index;
    valid = valid && c_environment_benchmark_queries(&environment, keys, points, queries, expected, true);
    TypeTestTable world = { 0 };
    valid = valid && type_test_table_open(&world, 64);
    uint32_t completions[65];
    for (uint32_t i = 0; i < 65; i += 1)
    {
        completions[i] = UINT32_MAX;
    }
    if (valid)
    {
        TypeTable* table = &world.table;
        const TypeId record = type_tag(table, BUSTER_TYPE_KIND_STRUCT, 1);
        const TypeMember member =
        {
            .name = 2,
            .type = BUSTER_TYPE_INT,
            .bit_width = BUSTER_TYPE_NOT_BIT_FIELD,
        };
        valid = type_complete_record(table, record, &member, 1, (TypeRecordAttributes){ 0 });
        completions[table->types.index[record >> BUSTER_TYPE_INDEX_SHIFT]] = names * history / 2;
        const TypeId array = type_array(table, record, 4);
        const TypeId pointer = type_pointer(table, record);
        for (uint32_t i = 0; valid && i < queries; i += 1)
        {
            requested[i] = i % 3 == 0 ? record : i % 3 == 1 ? array : pointer;
        }
        c_environment_types_scalar(table, completions, 65, requested, points, queries, type_expected);
    }
    struct perf_event_attr attribute =
    {
        .type = PERF_TYPE_HARDWARE,
        .size = sizeof(struct perf_event_attr),
        .config = PERF_COUNT_HW_CPU_CYCLES,
        .disabled = 1,
        .exclude_kernel = 1,
        .exclude_hv = 1,
        .read_format = PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING,
    };
    const int counter = valid ? (int)syscall(SYS_perf_event_open, &attribute, 0, -1, -1, PERF_FLAG_FD_CLOEXEC) : -1;
    bool cycles_available = counter >= 0;
    CBindingSample samples[7][BUSTER_C_BINDING_BENCHMARK_PASSES] = { 0 };
    for (uint32_t pass = 0; valid && pass <= BUSTER_C_BINDING_BENCHMARK_PASSES; pass += 1)
    {
        for (uint32_t step = 0; valid && step < 7; step += 1)
        {
            const uint32_t path = pass % 2 ? 6 - step : step;
            if (only == 7 || only == path)
            {
                const uint64_t until = c_binding_benchmark_time() + (uint64_t)after_scalar * 1000000;
                while (after_scalar != 0 && c_binding_benchmark_time() < until)
                {
                    valid &= c_environment_benchmark_queries(&environment, keys, points, queries, output, true);
                    __asm__ volatile("" : : "r"(output) : "memory");
                }
                const uint64_t mark = arena.length;
                CEnvironment built = { 0 };
                if (counter >= 0)
                {
                    cycles_available &= ioctl(counter, PERF_EVENT_IOC_RESET, 0) == 0 && ioctl(counter, PERF_EVENT_IOC_ENABLE, 0) == 0;
                }
                const uint64_t begin = c_binding_benchmark_time();
                if (path == 0 || path == 3 || path == 4)
                {
                    valid = c_environment_build(&built, &bindings, &arena, &scratch);
                }
                if (path >= 1 && path <= 4 && valid)
                {
                    valid = c_environment_benchmark_queries(path >= 3 ? &built : &environment, keys, points, queries, output, path == 1 || path == 3);
                }
                if (path == 5)
                {
                    c_environment_types_scalar(&world.table, completions, 65, requested, points, queries, complete);
                }
                if (path == 6)
                {
                    c_environment_types(&world.table, completions, 65, requested, points, queries, complete);
                }
                const uint64_t elapsed = c_binding_benchmark_time() - begin;
                uint64_t counts[3] = { 0 };
                if (counter >= 0)
                {
                    cycles_available &= ioctl(counter, PERF_EVENT_IOC_DISABLE, 0) == 0 && read(counter, counts, sizeof(counts)) == sizeof(counts);
                    cycles_available &= counts[1] == counts[2] && counts[2] != 0;
                }
                if (pass != 0)
                {
                    samples[path][pass - 1] = (CBindingSample)
                    {
                        .nanoseconds = elapsed,
                        .cycles = counts[0],
                    };
                }
                if (path == 0 && valid)
                {
                    valid = built.count == environment.count && memcmp(built.offsets, environment.offsets, ((uint64_t)environment.keys + 1) * 4) == 0 &&
                            memcmp(built.points, environment.points, (uint64_t)environment.count * 4) == 0 && memcmp(built.handles, environment.handles, (uint64_t)environment.count * 4) == 0;
                }
                else if (path < 5 && valid)
                {
                    valid = memcmp(output, expected, (uint64_t)queries * 4) == 0;
                }
                else if (valid)
                {
                    valid = memcmp(complete, type_expected, queries) == 0;
                }
                arena_pop_to(&arena, mark);
            }
        }
    }
    if (valid)
    {
        printf("environments synthetic: %u declarations, %u names, %u queries, %u passes, after-scalar %u ms\n", bindings.count, names, queries, BUSTER_C_BINDING_BENCHMARK_PASSES, after_scalar);
        printf("  retained index %llu bytes; construction scratch %llu bytes plus alignment; query scratch bounded on stack\n", (unsigned long long)index_bytes, (unsigned long long)names * 8);
        for (uint32_t path = 0; path < 7; path += 1)
        {
            if (only == 7 || only == path)
            {
                const uint32_t items = path == 0 ? bindings.count : queries;
                printf("  %s: %.3f ns/%s", paths[path], (double)c_binding_benchmark_median(samples[path], false) / items, path == 0 ? "declaration" : "query");
                if (cycles_available)
                {
                    printf(", %.3f cycles/%s\n", (double)c_binding_benchmark_median(samples[path], true) / items, path == 0 ? "declaration" : "query");
                }
                else
                {
                    printf(", cycles unavailable\n");
                }
            }
        }
    }
    else
    {
        fprintf(stderr, "benchmark-environments: invalid options, allocation failure or disagreement\n");
    }
    if (counter >= 0)
    {
        close(counter);
    }
    type_test_table_close(&world);
    arena_release(&scratch);
    arena_release(&arena);
    return !valid;
}
