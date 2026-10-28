// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// benchmark-streaming [--units N] [--functions N] [--seed N] [--runs N] [--chunk-bytes N] [--order callees|callers]
//                     [--lanes N] [--cpus A,B] [--directory PATH] [--keep]: streaming.c's schedules against each other
// and against the direct path's writer, on one program of `--units` generated units (400) of `--functions` functions
// each (32): from the start of the write to the file closed, with Tier 0 compiling every function in it. Each run
// writes the program by every available variant, in an order that rotates from run to run, into `--directory` (TMPDIR or /tmp):
//   - at the end, streamed, and streamed by a writer lane (on the CPUs of `--cpus`, or the process's), each with its
//     time to the last function compiled and after it;
//   - the direct path: the same compiled code as a Link, written by executable_write on one lane and on `--lanes` (8),
//     each with its compiling, the Link's building and the write.
// Before the runs, every available variant's file must run and print the same 8 bytes, and the schedules' files must be the same.
// It reports the medians of `--runs` runs (11). Run it under the 9700X's lock with taskset -c 2 for the one-lane
// variants; parallel variants need enough allowed CPUs (--cpus 2,3 and taskset -c 2,3 for two lanes).
// main.c includes this file after streaming_test.c, whose runner it takes.

#define BUSTER_STREAM_BENCHMARK_VARIANTS 5

// A variant's times in ms: the whole, to the last function compiled, then (the direct path) building the Link, then
// the rest; and the schedules' rest in parts: .text's last chunks, pairs and DWARF units, the tail built, written, the
// headers and the close (with the lanes' end).
#define BUSTER_STREAM_BENCHMARK_TIMES 9

typedef struct StreamBenchmarkCpus StreamBenchmarkCpus;
struct StreamBenchmarkCpus
{
    uint32_t allowed_count;
    uint32_t lanes;
    uint32_t writer[2];
    bool writer_available;
};

static bool stream_benchmark_cpu_allowed(const uint64_t* allowed, uint32_t cpu)
{
    return cpu < BUSTER_LANE_CPU_MASK_WORDS * 64 && (allowed[cpu / 64] >> (cpu % 64) & 1) != 0;
}

// --cpus is exactly two bounded decimal CPU IDs. Reject a missing second number, trailing text and overflow.
static bool stream_benchmark_parse_cpus(const char* text, uint32_t* cpus)
{
    bool result = true;
    for (uint32_t part = 0; result && part < 2; part += 1)
    {
        uint32_t cpu = 0;
        const char* begin = text;
        while (result && *text >= '0' && *text <= '9')
        {
            cpu = cpu * 10 + (uint32_t)(*text - '0');
            text += 1;
            result = cpu < BUSTER_LANE_CPU_MASK_WORDS * 64;
        }
        result = result && text != begin && *text == (part == 0 ? ',' : 0);
        cpus[part] = cpu;
        text += result && part == 0;
    }
    return result;
}

// Select only within the inherited affinity. Explicit writer CPUs may not widen a taskset/cpuset placement.
// The CI caller chooses one CPU per physical core; the benchmark itself counts the allowed logical CPUs.
static StreamBenchmarkCpus stream_benchmark_cpus(const uint64_t* allowed, uint32_t requested, const uint32_t* writer)
{
    StreamBenchmarkCpus result = { .writer = { BUSTER_LANE_CPU_MASK_WORDS * 64, BUSTER_LANE_CPU_MASK_WORDS * 64 } };
    for (uint32_t word = 0; word < BUSTER_LANE_CPU_MASK_WORDS; word += 1)
    {
        for (uint64_t bits = allowed[word]; bits != 0; bits &= bits - 1)
        {
            if (result.allowed_count < 2)
            {
                result.writer[result.allowed_count] = word * 64 + (uint32_t)__builtin_ctzll(bits);
            }
            result.allowed_count += 1;
        }
    }
    result.lanes = requested < result.allowed_count ? requested : result.allowed_count;
    if (writer != 0)
    {
        result.writer[0] = writer[0];
        result.writer[1] = writer[1];
    }
    result.writer_available = result.writer[0] != result.writer[1] && stream_benchmark_cpu_allowed(allowed, result.writer[0]) &&
                              stream_benchmark_cpu_allowed(allowed, result.writer[1]);
    return result;
}

typedef struct StreamBenchmarkSample StreamBenchmarkSample;
struct StreamBenchmarkSample
{
    double times[BUSTER_STREAM_BENCHMARK_TIMES];
};

static int stream_benchmark_compare(const void* left, const void* right)
{
    const double a = *(const double*)left;
    const double b = *(const double*)right;
    return (a > b) - (a < b);
}

static double stream_benchmark_median(double* values, uint32_t count)
{
    qsort(values, count, sizeof(double), stream_benchmark_compare);
    return count == 0 ? 0 : count % 2 == 1 ? values[count / 2] : (values[count / 2 - 1] + values[count / 2]) / 2;
}

// Runs `path` and reads the 8 bytes it prints into `printed`.
static bool stream_benchmark_output(const char* directory, const char* path, uint64_t* printed)
{
    char output[PATH_MAX];
    char errors[PATH_MAX];
    snprintf(output, sizeof(output), "%s/run.out", directory);
    snprintf(errors, sizeof(errors), "%s/run.err", directory);
    char* arguments[] = { (char*)path, 0 };
    FILE* file = object_test_run(arguments, output, errors) == 0 ? fopen(output, "rb") : 0;
    const bool read = file != 0 && fread(printed, 1, 8, file) == 8;
    if (file != 0)
    {
        fclose(file);
    }
    return read;
}

static bool stream_benchmark_variant(const StreamProgram* program, uint32_t variant, const char* directory, StreamOrder order, uint64_t chunk_bytes, uint32_t lanes,
                                     const uint32_t* cpus, StreamBenchmarkSample* sample, char* path, uint64_t* chunks_streamed)
{
    bool result = false;
    snprintf(path, PATH_MAX, "%s/variant-%u", directory, variant);
    if (variant < STREAM_SCHEDULES)
    {
        const StreamOptions options = {
            .path = { .pointer = path, .length = strlen(path) },
            .schedule = (StreamSchedule)variant,
            .order = order,
            .chunk_bytes = chunk_bytes,
            .cpus = variant == STREAM_SCHEDULE_WRITER ? cpus : 0,
        };
        StreamResult written;
        result = stream_write(program, &options, &written);
        const uint64_t marks[BUSTER_STREAM_BENCHMARK_TIMES + 1] = {
            written.start, written.closed, written.start, written.compiled, written.compiled, written.compiled, written.flushed, written.tail_built,
            written.tail_written, written.headers,
        };
        sample->times[0] = (double)(marks[1] - marks[0]) / 1e6;
        sample->times[1] = (double)(marks[3] - marks[2]) / 1e6;
        sample->times[2] = 0;
        sample->times[3] = (double)(written.closed - written.compiled) / 1e6;
        for (uint32_t t = 4; t + 1 < BUSTER_STREAM_BENCHMARK_TIMES; t += 1)
        {
            sample->times[t] = (double)(marks[t + 2] - marks[t + 1]) / 1e6;
        }
        sample->times[BUSTER_STREAM_BENCHMARK_TIMES - 1] = (double)(written.closed - written.headers) / 1e6;
        *chunks_streamed = written.chunks_streamed;
    }
    else
    {
        StreamLinkResult written;
        result = stream_write_link(program, (String8){ .pointer = path, .length = strlen(path) }, order, variant == STREAM_SCHEDULES ? 1 : lanes, &written);
        memset(sample, 0, sizeof(*sample));
        sample->times[0] = (double)(written.closed - written.start) / 1e6;
        sample->times[1] = (double)(written.compiled - written.start) / 1e6;
        sample->times[2] = (double)(written.linked - written.compiled) / 1e6;
        sample->times[3] = (double)(written.closed - written.linked) / 1e6;
        *chunks_streamed = 0;
    }
    return result;
}

static int run_streaming_benchmark(int argc, char** argv)
{
    uint32_t units = 400;
    uint32_t functions = 32;
    uint64_t seed = 88;
    uint32_t runs = 11;
    uint64_t chunk_bytes = BUSTER_STREAM_CHUNK_BYTES;
    uint32_t lanes = 8;
    StreamOrder order = STREAM_ORDER_CALLEES;
    uint32_t cpu_list[2] = { 0, 0 };
    const uint32_t* cpus = 0;
    const char* base = getenv("TMPDIR");
    base = base != 0 && base[0] != 0 ? base : "/tmp";
    bool keep = false;
    bool valid = true;
    for (int i = 0; valid && i < argc; i += 1)
    {
        const bool value = i + 1 < argc;
        if (strcmp(argv[i], "--units") == 0 && value)
        {
            i += 1;
            units = (uint32_t)strtoul(argv[i], 0, 10);
            valid = units != 0 && units <= BUSTER_STREAM_MAX_UNITS;
        }
        else if (strcmp(argv[i], "--functions") == 0 && value)
        {
            i += 1;
            functions = (uint32_t)strtoul(argv[i], 0, 10);
            valid = functions != 0 && functions <= BUSTER_TIER0_GENERATE_MAX_FUNCTIONS;
        }
        else if (strcmp(argv[i], "--seed") == 0 && value)
        {
            i += 1;
            seed = strtoull(argv[i], 0, 10);
        }
        else if (strcmp(argv[i], "--runs") == 0 && value)
        {
            i += 1;
            runs = (uint32_t)strtoul(argv[i], 0, 10);
            valid = runs != 0 && runs <= 1001;
        }
        else if (strcmp(argv[i], "--chunk-bytes") == 0 && value)
        {
            i += 1;
            chunk_bytes = strtoull(argv[i], 0, 10);
            valid = chunk_bytes >= 4096 && (chunk_bytes & (chunk_bytes - 1)) == 0;
        }
        else if (strcmp(argv[i], "--lanes") == 0 && value)
        {
            i += 1;
            lanes = (uint32_t)strtoul(argv[i], 0, 10);
            valid = lanes != 0 && lanes <= BUSTER_LANE_MAX_COUNT;
        }
        else if (strcmp(argv[i], "--order") == 0 && value)
        {
            i += 1;
            order = strcmp(argv[i], "callers") == 0 ? STREAM_ORDER_CALLERS : STREAM_ORDER_CALLEES;
            valid = strcmp(argv[i], "callers") == 0 || strcmp(argv[i], "callees") == 0;
        }
        else if (strcmp(argv[i], "--cpus") == 0 && value)
        {
            i += 1;
            valid = stream_benchmark_parse_cpus(argv[i], cpu_list);
            cpus = cpu_list;
        }
        else if (strcmp(argv[i], "--directory") == 0 && value)
        {
            i += 1;
            base = argv[i];
        }
        else
        {
            keep = keep || strcmp(argv[i], "--keep") == 0;
            valid = strcmp(argv[i], "--keep") == 0;
        }
    }
    if (!valid)
    {
        fprintf(stderr, "usage: benchmark-streaming [--units N] [--functions N] [--seed N] [--runs N] [--chunk-bytes N] [--order callees|callers] "
                        "[--lanes N] [--cpus A,B] [--directory PATH] [--keep]\n");
    }
    uint64_t allowed[BUSTER_LANE_CPU_MASK_WORDS] = { 0 };
    const bool known = valid && syscall(SYS_sched_getaffinity, 0, sizeof(allowed), allowed) > 0;
    const StreamBenchmarkCpus placement = stream_benchmark_cpus(allowed, lanes, cpus);
    if (valid && (!known || placement.allowed_count == 0))
    {
        fprintf(stderr, "benchmark-streaming: cannot read a nonempty allowed CPU set\n");
        valid = false;
    }
    if (valid)
    {
        printf("benchmark-streaming: requested lanes=%u, selected lanes=%u, allowed CPUs=%u; ", lanes, placement.lanes, placement.allowed_count);
        if (placement.writer_available)
        {
            printf("writer CPUs=%u,%u\n", placement.writer[0], placement.writer[1]);
        }
        else
        {
            printf("writer unavailable\n");
        }
    }
    lanes = placement.lanes;
    cpus = placement.writer;
    const bool enabled[BUSTER_STREAM_BENCHMARK_VARIANTS] = { true, true, placement.writer_available, true, lanes > 1 };
    char directory[PATH_MAX];
    snprintf(directory, sizeof(directory), "%s/buster-streaming-benchmark-XXXXXX", base);
    valid = valid && x86_encoder_initialize() && x86_semantics_initialize() && tier0_initialize() && unwind_initialize() && mkdtemp(directory) != 0;
    Tier0Tables tables;
    memset(&tables, 0, sizeof(tables));
    valid = valid && tier0_tables_open(&tables);
    Arena arena = arena_reserve(1ull << 38);
    Arena scratch = arena_reserve(1ull << 32);
    valid = valid && arena.base != 0 && scratch.base != 0;
    // Programs shaped like compiled C's, as benchmark-backend's.
    const Tier0ProgramOptions options = {
        .functions = functions,
        .statements = 24,
        .expression_nodes = 12,
        .calls = true,
        .loops = true,
        .floating = true,
        .pointers = true,
        .defined = true,
    };
    StreamProgram program;
    memset(&program, 0, sizeof(program));
    valid = valid && stream_program_generate(&program, &arena, &scratch, &tables, seed, units, options);
    // The variants' files: each must run and print the same, and the schedules' must be the same bytes.
    static const char* const names[BUSTER_STREAM_BENCHMARK_VARIANTS] = { "at the end", "streamed", "streamed, writer lane", "direct path, 1 lane", "direct path, lanes" };
    StreamResult reference;
    memset(&reference, 0, sizeof(reference));
    if (valid)
    {
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/reference", directory);
        const StreamOptions options_end = {
            .path = { .pointer = path, .length = strlen(path) },
            .schedule = STREAM_SCHEDULE_END,
            .order = order,
            .chunk_bytes = chunk_bytes,
        };
        valid = stream_write(&program, &options_end, &reference);
        uint64_t expected = 0;
        valid = valid && stream_benchmark_output(directory, path, &expected);
        Arena files = arena_reserve(1ull << 34);
        const String8 reference_bytes = valid ? object_test_read_file(&files, path) : (String8){ 0 };
        for (uint32_t variant = 0; valid && variant < BUSTER_STREAM_BENCHMARK_VARIANTS; variant += 1)
        {
            if (!enabled[variant])
            {
                continue;
            }
            char variant_path[PATH_MAX];
            StreamBenchmarkSample sample;
            uint64_t streamed = 0;
            uint64_t printed = 0;
            valid = stream_benchmark_variant(&program, variant, directory, order, chunk_bytes, lanes, cpus, &sample, variant_path, &streamed) &&
                    stream_benchmark_output(directory, variant_path, &printed) && printed == expected;
            if (valid && variant < STREAM_SCHEDULES)
            {
                const String8 bytes = object_test_read_file(&files, variant_path);
                valid = bytes.pointer != 0 && bytes.length == reference_bytes.length && memcmp(bytes.pointer, reference_bytes.pointer, bytes.length) == 0;
            }
            if (!valid)
            {
                printf("benchmark-streaming: %s's file differs or doesn't run\n", names[variant]);
            }
        }
        arena_release(&files);
    }
    if (valid)
    {
        printf("benchmark-streaming: %u units of %u functions, %u functions, %" PRIu64 " typed entries, %" PRIu64 " records; .text %" PRIu64 " bytes, file %" PRIu64
               " bytes, %" PRIu64 " chunks of %" PRIu64 " bytes, %" PRIu64 " fields (%" PRIu64 " forward, %s first)\n",
               units, functions, program.function_count - 1, program.entries, reference.records, reference.text_size, reference.file_size, reference.chunks, chunk_bytes,
               reference.fixups, reference.forward_fixups, order == STREAM_ORDER_CALLEES ? "callees" : "callers");
        double* samples = BUSTER_ARENA_PUSH_ARRAY(&arena, double, (uint64_t)BUSTER_STREAM_BENCHMARK_VARIANTS * BUSTER_STREAM_BENCHMARK_TIMES * runs);
        uint64_t streamed[BUSTER_STREAM_BENCHMARK_VARIANTS] = { 0 };
        valid = samples != 0;
        for (uint32_t run = 0; valid && run < runs; run += 1)
        {
            for (uint32_t step = 0; valid && step < BUSTER_STREAM_BENCHMARK_VARIANTS; step += 1)
            {
                const uint32_t variant = (run + step) % BUSTER_STREAM_BENCHMARK_VARIANTS;
                if (!enabled[variant])
                {
                    continue;
                }
                char path[PATH_MAX];
                StreamBenchmarkSample sample;
                valid = stream_benchmark_variant(&program, variant, directory, order, chunk_bytes, lanes, cpus, &sample, path, &streamed[variant]);
                double* row = samples + ((uint64_t)variant * BUSTER_STREAM_BENCHMARK_TIMES) * runs;
                for (uint32_t t = 0; t < BUSTER_STREAM_BENCHMARK_TIMES; t += 1)
                {
                    row[(uint64_t)t * runs + run] = sample.times[t];
                }
            }
        }
        if (valid)
        {
            printf("  %-24s %9s %9s %9s %9s %8s | %9s %9s %9s %9s %9s\n", "variant", "total ms", "compile", "link", "after", "streamed", "last text", "tail", "tail write",
                   "headers", "close");
            for (uint32_t variant = 0; variant < BUSTER_STREAM_BENCHMARK_VARIANTS; variant += 1)
            {
                double* row = samples + ((uint64_t)variant * BUSTER_STREAM_BENCHMARK_TIMES) * runs;
                char label[64];
                snprintf(label, sizeof(label), variant == BUSTER_STREAM_BENCHMARK_VARIANTS - 1 ? "%s (%u)" : "%s", names[variant], lanes);
                if (!enabled[variant])
                {
                    printf("  %-24s not run (%s)\n", label, variant == STREAM_SCHEDULE_WRITER ? "two distinct allowed writer CPUs required" : "multiple selected lanes required");
                    continue;
                }
                double medians[BUSTER_STREAM_BENCHMARK_TIMES];
                for (uint32_t t = 0; t < BUSTER_STREAM_BENCHMARK_TIMES; t += 1)
                {
                    medians[t] = stream_benchmark_median(row + (uint64_t)t * runs, runs);
                }
                printf("  %-24s %9.3f %9.3f %9.3f %9.3f %8" PRIu64, label, medians[0], medians[1], medians[2], medians[3], streamed[variant]);
                if (variant < STREAM_SCHEDULES)
                {
                    printf(" | %9.3f %9.3f %9.3f %9.3f %9.3f", medians[4], medians[5], medians[6], medians[7], medians[8]);
                }
                printf("\n");
            }
        }
    }
    if (!keep && directory[0] != 0 && valid)
    {
        char* remove[] = { "rm", "-r", directory, 0 };
        object_test_run(remove, "/dev/null", "/dev/null");
    }
    tier0_tables_close(&tables);
    arena_release(&scratch);
    arena_release(&arena);
    return valid ? 0 : 1;
}
