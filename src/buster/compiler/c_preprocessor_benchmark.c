// SPDX-License-Identifier: (Apache-2.0 WITH LLVM-exception) AND MIT
// Original material Copyright (c) 2026 David Gonzalez Martin.
// Counter/timing/sort helpers adapted through c_lexer_benchmark.c retain MIT and the upstream notice:
// Copyright (c) 2023 Niles Salter. See THIRD_PARTY_NOTICES.md and LICENSES/Accelerated-Zig-Parser.txt.
//
// The preprocessor benchmark: preprocesses translation units from scratch, as `-E` does without printing, with the
// vectorized kernels and with their scalar twins, and reports per token what each costs.
//
// Usage: build/compiler benchmark-preprocessor [--clang] [--only vectorized|scalar] [-E's options] file...
//
// Each pass preprocesses every file once with each implementation in turn, each run from nothing: a new intern table
// and preprocessor, so that every file it includes is read, lexed, refined and given its directive table, as a
// compile of one translation unit does. After a warm-up pass come BUSTER_PP_BENCHMARK_PASSES passes; it reports the
// medians of wall time and of user-space cycles and instructions (perf_event_open; zero when unavailable), per input
// token (the tokens of every file the runs prepared) and per output token, the share of the time each phase took
// (PPPhase: reading, lexing, refining and directive tables; the rest is the main loop with its directives), and what
// a run did (PPStatistics). With --clang it also times `clang -E -P` with the same options (but --predefines, which is
// ours) writing to /dev/null, a process per file, the median of as many runs, for scale. --only runs one
// implementation (for profiles). Run it pinned to one CPU (taskset -c 2 ...). The exit status is 1 if the arguments
// are invalid or a run fails.
//
// Layout: the clock and the counters, the passes, and run_preprocessor_benchmark.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// OS-dependent includes
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <linux/perf_event.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>

#define BUSTER_ARRAY_LENGTH(x) (sizeof(x) / sizeof((x)[0]))

#include "buster/compiler/c_lexer.c"
#include "buster/compiler/lane.c"
#include "buster/compiler/c_intern.c"
#include "buster/compiler/c_refine.c"
#include "buster/compiler/c_preprocessor.c"

#define BUSTER_PP_BENCHMARK_PASSES 21
#define BUSTER_PP_BENCHMARK_IMPLEMENTATIONS 2

typedef struct PPBenchmarkSample PPBenchmarkSample;
struct PPBenchmarkSample
{
    PPStatistics statistics;
    uint64_t nanoseconds;
    uint64_t cycles;
    uint64_t instructions;
    uint64_t ticks[PP_PHASE_COUNT + 1]; // The phases', and the whole runs'.
};

static int pp_benchmark_open_counter(uint64_t config)
{
    struct perf_event_attr attributes;
    memset(&attributes, 0, sizeof(attributes));
    attributes.size = sizeof(attributes);
    attributes.type = PERF_TYPE_HARDWARE;
    attributes.config = config;
    attributes.exclude_kernel = 1;
    attributes.exclude_hv = 1;
    return (int)syscall(SYS_perf_event_open, &attributes, 0, -1, -1, PERF_FLAG_FD_CLOEXEC);
}

static uint64_t pp_benchmark_read_counter(int fd)
{
    uint64_t value = 0;
    if (fd >= 0 && read(fd, &value, sizeof(value)) != sizeof(value))
    {
        value = 0;
    }
    return value;
}

static uint64_t pp_benchmark_nanoseconds(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec;
}

static void pp_benchmark_sort(uint64_t* values, uint64_t count)
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

// One pass of one implementation over the files: each preprocessed from nothing. Adds the files' input and output
// tokens to `tokens` (the first time).
static bool pp_benchmark_pass(const PPOptions* base, bool scalar, char** files, int file_count, int cycles_fd, int instructions_fd, PPBenchmarkSample* sample,
                              uint64_t* input_tokens, uint64_t* output_tokens)
{
    static InternTable table;
    static Preprocessor pp;
    PPOptions options = *base;
    options.scalar = scalar;
    options.timing = true;
    bool result = true;
    *sample = (PPBenchmarkSample){ 0 };
    *input_tokens = 0;
    *output_tokens = 0;
    for (int f = 0; result && f < file_count; f += 1)
    {
        const uint64_t nanoseconds = pp_benchmark_nanoseconds();
        const uint64_t cycles = pp_benchmark_read_counter(cycles_fd);
        const uint64_t instructions = pp_benchmark_read_counter(instructions_fd);
        const uint64_t ticks = __rdtsc();
        result = intern_initialize(&table) && pp_initialize(&pp, &options, &table) && pp_run(&pp, files[f]);
        sample->ticks[PP_PHASE_COUNT] += __rdtsc() - ticks;
        sample->instructions += pp_benchmark_read_counter(instructions_fd) - instructions;
        sample->cycles += pp_benchmark_read_counter(cycles_fd) - cycles;
        sample->nanoseconds += pp_benchmark_nanoseconds() - nanoseconds;
        for (uint32_t phase = 0; phase < PP_PHASE_COUNT; phase += 1)
        {
            sample->ticks[phase] += pp.ticks[phase];
        }
        *input_tokens += pp.input_tokens;
        *output_tokens += pp.output.count;
        const uint64_t* from = (const uint64_t*)&pp.statistics;
        uint64_t* to = (uint64_t*)&sample->statistics;
        const uint64_t longest = sample->statistics.longest_condition;
        for (uint32_t k = 0; k < sizeof(PPStatistics) / sizeof(uint64_t); k += 1)
        {
            to[k] += from[k];
        }
        sample->statistics.longest_condition = pp.statistics.longest_condition > longest ? pp.statistics.longest_condition : longest;
        pp_release(&pp);
        intern_release(&table);
    }
    return result;
}

// The wall time of `clang -E -P` with the options on each file, writing to /dev/null.
static uint64_t pp_benchmark_clang(char** arguments, int argument_count, char** files, int file_count)
{
    uint64_t result = 0;
    for (int f = 0; f < file_count; f += 1)
    {
        const uint64_t start = pp_benchmark_nanoseconds();
        const pid_t child = fork();
        if (child == 0)
        {
            char* argv[256];
            int n = 0;
            argv[n++] = "clang";
            for (int i = 0; i < argument_count && n < 245; i += 1)
            {
                argv[n++] = arguments[i];
            }
            argv[n++] = "-E";
            argv[n++] = "-P";
            argv[n++] = "-o";
            argv[n++] = "/dev/null";
            argv[n++] = files[f];
            argv[n] = 0;
            unsetenv("CPATH");
            unsetenv("C_INCLUDE_PATH");
            execvp("clang", argv);
            _exit(127);
        }
        int status = 0;
        waitpid(child, &status, 0);
        result += pp_benchmark_nanoseconds() - start;
    }
    return result;
}

int run_preprocessor_benchmark(int argc, char** argv)
{
    int result = 1;
    bool with_clang = false;
    uint32_t only = 3;
    char** passed = calloc((size_t)argc + 1, sizeof(char*));
    char** clang_passed = calloc((size_t)argc + 1, sizeof(char*));
    char** files = calloc((size_t)argc + 1, sizeof(char*));
    int passed_count = 0;
    int clang_passed_count = 0;
    int file_count = 0;
    for (int i = 1; passed != 0 && clang_passed != 0 && files != 0 && i < argc; i += 1)
    {
        const char* argument = argv[i];
        const bool takes_value = strcmp(argument, "-I") == 0 || strcmp(argument, "-D") == 0 || strcmp(argument, "-U") == 0 || strcmp(argument, "-isystem") == 0 ||
                                 strcmp(argument, "-iquote") == 0 || strcmp(argument, "-idirafter") == 0 || strcmp(argument, "-include") == 0 ||
                                 strcmp(argument, "--predefines") == 0;
        // --predefines (clang's predefines for the options, from `clang -dM -E`) is ours alone.
        const bool ours = strcmp(argument, "--predefines") == 0;
        if (strcmp(argument, "--clang") == 0)
        {
            with_clang = true;
        }
        else if (strcmp(argument, "--only") == 0 && i + 1 < argc)
        {
            i += 1;
            only = strcmp(argv[i], "vectorized") == 0 ? 1 : strcmp(argv[i], "scalar") == 0 ? 2 : 0;
        }
        else if (argument[0] == '-')
        {
            passed[passed_count++] = argv[i];
            if (!ours)
            {
                clang_passed[clang_passed_count++] = argv[i];
            }
            if (takes_value && i + 1 < argc)
            {
                i += 1;
                passed[passed_count++] = argv[i];
                if (!ours)
                {
                    clang_passed[clang_passed_count++] = argv[i];
                }
            }
        }
        else
        {
            files[file_count++] = argv[i];
        }
    }
    Arena arena = arena_reserve(1ull << 30);
    PPOptions options;
    const char* input;
    const char* output;
    bool markers;
    char** with_input = calloc((size_t)passed_count + 2, sizeof(char*));
    if (with_input != 0 && file_count != 0)
    {
        memcpy(with_input, passed, (size_t)passed_count * sizeof(char*));
        with_input[passed_count] = files[0];
    }
    if (file_count == 0 || with_input == 0 || only == 0 || !pp_parse_options(passed_count + 1, with_input, &options, &arena, &input, &output, &markers))
    {
        fprintf(stderr, "usage: %s [--clang] [--only vectorized|scalar] [-E's options] file...\n", argv[0]);
    }
    else
    {
        const int cycles_fd = pp_benchmark_open_counter(PERF_COUNT_HW_CPU_CYCLES);
        const int instructions_fd = pp_benchmark_open_counter(PERF_COUNT_HW_INSTRUCTIONS);
        if (cycles_fd < 0 || instructions_fd < 0)
        {
            fprintf(stderr, "perf_event_open failed (see /proc/sys/kernel/perf_event_paranoid): cycles and instructions read as 0\n");
        }
        static PPBenchmarkSample samples[BUSTER_PP_BENCHMARK_IMPLEMENTATIONS][BUSTER_PP_BENCHMARK_PASSES];
        uint64_t clang_times[BUSTER_PP_BENCHMARK_PASSES];
        uint64_t input_tokens = 0;
        uint64_t output_tokens = 0;
        bool ran = true;
        for (uint32_t pass = 0; ran && pass <= BUSTER_PP_BENCHMARK_PASSES; pass += 1)
        {
            for (uint32_t implementation = 0; ran && implementation < BUSTER_PP_BENCHMARK_IMPLEMENTATIONS; implementation += 1)
            {
                if (((only >> implementation) & 1) == 0)
                {
                    continue;
                }
                PPBenchmarkSample sample;
                ran = pp_benchmark_pass(&options, implementation == 1, files, file_count, cycles_fd, instructions_fd, &sample, &input_tokens, &output_tokens);
                if (pass > 0)
                {
                    samples[implementation][pass - 1] = sample;
                }
            }
            if (with_clang && pass > 0)
            {
                clang_times[pass - 1] = pp_benchmark_clang(clang_passed, clang_passed_count, files, file_count);
            }
        }
        if (!ran)
        {
            fprintf(stderr, "a run failed\n");
        }
        else
        {
            static const char* const names[BUSTER_PP_BENCHMARK_IMPLEMENTATIONS] = { "vectorized", "scalar" };
            static const char* const phase_names[PP_PHASE_COUNT + 1] = { "read", "lex", "refine", "directives", "main loop" };
            printf("%d files, %lu input tokens, %lu output tokens; %d passes, medians:\n", file_count, (unsigned long)input_tokens, (unsigned long)output_tokens,
                   BUSTER_PP_BENCHMARK_PASSES);
            for (uint32_t implementation = 0; implementation < BUSTER_PP_BENCHMARK_IMPLEMENTATIONS; implementation += 1)
            {
                if (((only >> implementation) & 1) == 0)
                {
                    continue;
                }
                uint64_t nanoseconds[BUSTER_PP_BENCHMARK_PASSES];
                uint64_t cycles[BUSTER_PP_BENCHMARK_PASSES];
                uint64_t instructions[BUSTER_PP_BENCHMARK_PASSES];
                uint64_t phases[PP_PHASE_COUNT + 1][BUSTER_PP_BENCHMARK_PASSES];
                for (uint32_t pass = 0; pass < BUSTER_PP_BENCHMARK_PASSES; pass += 1)
                {
                    const PPBenchmarkSample* sample = &samples[implementation][pass];
                    nanoseconds[pass] = sample->nanoseconds;
                    cycles[pass] = sample->cycles;
                    instructions[pass] = sample->instructions;
                    uint64_t phase_sum = 0;
                    for (uint32_t phase = 0; phase < PP_PHASE_COUNT; phase += 1)
                    {
                        phases[phase][pass] = sample->ticks[phase];
                        phase_sum += sample->ticks[phase];
                    }
                    phases[PP_PHASE_COUNT][pass] = sample->ticks[PP_PHASE_COUNT] > phase_sum ? sample->ticks[PP_PHASE_COUNT] - phase_sum : 0;
                }
                pp_benchmark_sort(nanoseconds, BUSTER_PP_BENCHMARK_PASSES);
                pp_benchmark_sort(cycles, BUSTER_PP_BENCHMARK_PASSES);
                pp_benchmark_sort(instructions, BUSTER_PP_BENCHMARK_PASSES);
                const double median_cycles = (double)cycles[BUSTER_PP_BENCHMARK_PASSES / 2];
                const double median_instructions = (double)instructions[BUSTER_PP_BENCHMARK_PASSES / 2];
                printf("  %-10s %8.3f ms  %6.2f cycles/input token  %6.2f cycles/output token  %6.2f instructions/input token  IPC %.2f\n", names[implementation],
                       (double)nanoseconds[BUSTER_PP_BENCHMARK_PASSES / 2] / 1e6, median_cycles / (double)input_tokens, median_cycles / (double)output_tokens,
                       median_instructions / (double)input_tokens, median_cycles != 0 ? median_instructions / median_cycles : 0.0);
                uint64_t total = 0;
                uint64_t medians[PP_PHASE_COUNT + 1];
                // The phases are timed with the time stamp counter: its ticks per nanosecond, over the passes' whole runs.
                uint64_t run_ticks = 0;
                uint64_t run_nanoseconds = 0;
                for (uint32_t pass = 0; pass < BUSTER_PP_BENCHMARK_PASSES; pass += 1)
                {
                    run_ticks += samples[implementation][pass].ticks[PP_PHASE_COUNT];
                    run_nanoseconds += samples[implementation][pass].nanoseconds;
                }
                const double milliseconds_per_tick = run_ticks != 0 ? (double)run_nanoseconds / (double)run_ticks / 1e6 : 0.0;
                for (uint32_t phase = 0; phase <= PP_PHASE_COUNT; phase += 1)
                {
                    pp_benchmark_sort(phases[phase], BUSTER_PP_BENCHMARK_PASSES);
                    medians[phase] = phases[phase][BUSTER_PP_BENCHMARK_PASSES / 2];
                    total += medians[phase];
                }
                printf("             ");
                for (uint32_t phase = 0; phase <= PP_PHASE_COUNT; phase += 1)
                {
                    printf("  %s %.1f%%", phase_names[phase], total != 0 ? 100.0 * (double)medians[phase] / (double)total : 0.0);
                }
                printf("\n             ");
                for (uint32_t phase = 0; phase <= PP_PHASE_COUNT; phase += 1)
                {
                    printf("  %s %.3f ms", phase_names[phase], (double)medians[phase] * milliseconds_per_tick);
                }
                printf("\n");
            }
            const PPStatistics* statistics = &samples[only == 2 ? 1 : 0][0].statistics;
            printf("  per pass: %lu directives, %lu file runs (%lu tokens), %lu buffer runs (%lu tokens), %lu object-like expansions copied out and %lu "
                   "buffered, %lu invocations (%lu arguments, %lu expanded first), %lu pastes, %lu builtins, %lu includes skipped\n",
                   (unsigned long)statistics->directives, (unsigned long)statistics->file_runs, (unsigned long)statistics->file_run_tokens,
                   (unsigned long)statistics->buffer_runs, (unsigned long)statistics->buffer_run_tokens, (unsigned long)statistics->direct_expansions,
                   (unsigned long)statistics->buffered_expansions, (unsigned long)statistics->invocations, (unsigned long)statistics->arguments,
                   (unsigned long)statistics->expanded_arguments, (unsigned long)statistics->pastes, (unsigned long)statistics->builtins, (unsigned long)statistics->skipped_includes);
            printf("  #if and #elif: %lu lines of %.1f tokens on average after expansion, %lu of more than 32, the longest %lu\n",
                   (unsigned long)statistics->conditions, statistics->conditions != 0 ? (double)statistics->condition_tokens / (double)statistics->conditions : 0.0,
                   (unsigned long)statistics->long_conditions, (unsigned long)statistics->longest_condition);
            if (with_clang)
            {
                pp_benchmark_sort(clang_times, BUSTER_PP_BENCHMARK_PASSES);
                printf("  clang -E   %8.3f ms (a process per file, writing to /dev/null)\n", (double)clang_times[BUSTER_PP_BENCHMARK_PASSES / 2] / 1e6);
            }
            result = 0;
        }
        if (cycles_fd >= 0)
        {
            close(cycles_fd);
        }
        if (instructions_fd >= 0)
        {
            close(instructions_fd);
        }
    }
    free(with_input);
    free(passed);
    free(clang_passed);
    free(files);
    arena_release(&arena);
    return result;
}
