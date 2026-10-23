// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// benchmark-gnu: original measurement helpers for the retained GNU syntax interface. The actual input is a
// preprocessed translation unit. Collection includes candidate discovery and variable group decoding; build adds
// exact sizing/allocation. The pipeline rows include token conversion, parsing, postorder and optional metadata.
// Preprocessing is outside the timing. This is not semantic checking, generated-code or whole-compiler acceptance.

#include <linux/perf_event.h>
#include <sys/syscall.h>

#define BUSTER_GNU_BENCHMARK_PASSES 21
#define BUSTER_GNU_BENCHMARK_PATHS 6

typedef struct GnuBenchmarkSample GnuBenchmarkSample;
struct GnuBenchmarkSample
{
    uint64_t nanoseconds;
    uint64_t cycles;
};

static uint64_t gnu_benchmark_clock(void)
{
    struct timespec value;
    clock_gettime(CLOCK_MONOTONIC_RAW, &value);
    return (uint64_t)value.tv_sec * UINT64_C(1000000000) + (uint64_t)value.tv_nsec;
}

// The generic collector may hoist vector constants even on its scalar branch. Condition with dependent integer
// work instead, keeping the result live and checking elapsed time between bounded chunks.
static uint64_t gnu_benchmark_scalar_work(uint64_t value)
{
#pragma clang loop vectorize(disable) interleave(disable)
    for (uint32_t i = 0; i < 1024; i += 1)
    {
        value = (value << 1) + value + (value >> 17) + i;
    }
    return value;
}

static uint64_t gnu_benchmark_median(const GnuBenchmarkSample* samples, bool cycles)
{
    uint64_t values[BUSTER_GNU_BENCHMARK_PASSES];
    for (uint32_t i = 0; i < BUSTER_GNU_BENCHMARK_PASSES; i += 1)
    {
        const uint64_t value = cycles ? samples[i].cycles : samples[i].nanoseconds;
        uint32_t at = i;
        while (at != 0 && values[at - 1] > value)
        {
            values[at] = values[at - 1];
            at -= 1;
        }
        values[at] = value;
    }
    return values[BUSTER_GNU_BENCHMARK_PASSES / 2];
}

static bool gnu_benchmark_equal(const GnuSyntax* a, const GnuSyntax* b)
{
    bool result = a->group_count == b->group_count && a->attribute_count == b->attribute_count && a->asm_label_count == b->asm_label_count;
    for (uint32_t i = 0; result && i < a->group_count; i += 1)
    {
        const GnuAttributeGroup* x = &a->groups[i];
        const GnuAttributeGroup* y = &b->groups[i];
        result = x->keyword == y->keyword && x->close == y->close && x->parent == y->parent && x->role == y->role &&
                 x->attribute_begin == y->attribute_begin && x->attribute_count == y->attribute_count;
    }
    for (uint32_t i = 0; result && i < a->attribute_count; i += 1)
    {
        const GnuAttribute* x = &a->attributes[i];
        const GnuAttribute* y = &b->attributes[i];
        result = x->name == y->name && x->argument_begin == y->argument_begin && x->argument_end == y->argument_end && x->group == y->group;
    }
    for (uint32_t i = 0; result && i < a->asm_label_count; i += 1)
    {
        const GnuAsmLabel* x = &a->asm_labels[i];
        const GnuAsmLabel* y = &b->asm_labels[i];
        result = x->keyword == y->keyword && x->string_begin == y->string_begin && x->string_end == y->string_end && x->parent == y->parent;
    }
    return result;
}

static int run_gnu_benchmark(int argc, char** argv)
{
    static const char* const names[BUSTER_GNU_BENCHMARK_PATHS] = {
        "collect-scalar",
        "collect-vector",
        "build-scalar",
        "build-vector",
        "parser-only",
        "parser-with-gnu",
    };
    uint32_t only = BUSTER_GNU_BENCHMARK_PATHS;
    uint32_t after_scalar = 0;
    char** rest = malloc(((size_t)argc + 1) * sizeof(*rest));
    int rest_count = 0;
    bool valid = rest != 0 && argc >= 1;
    for (int i = 1; valid && i < argc; i += 1)
    {
        if (strcmp(argv[i], "--only") == 0 && i + 1 < argc)
        {
            only = BUSTER_GNU_BENCHMARK_PATHS;
            i += 1;
            for (uint32_t path = 0; path < BUSTER_GNU_BENCHMARK_PATHS; path += 1)
            {
                if (strcmp(argv[i], names[path]) == 0)
                {
                    only = path;
                }
            }
            valid = only != BUSTER_GNU_BENCHMARK_PATHS;
        }
        else if (strcmp(argv[i], "--after-scalar") == 0 && i + 1 < argc)
        {
            char* end = 0;
            errno = 0;
            const unsigned long value = strtoul(argv[++i], &end, 10);
            valid = errno == 0 && end != argv[i] && *end == 0 && value <= 1000;
            after_scalar = valid ? (uint32_t)value : 0;
        }
        else
        {
            rest[rest_count++] = argv[i];
        }
    }
    Arena options_arena = arena_reserve(1ull << 30);
    PPOptions options = { 0 };
    const char* input = 0;
    const char* output = 0;
    bool markers = false;
    valid = valid && options_arena.base != 0 && pp_parse_options(rest_count, rest, &options, &options_arena, &input, &output, &markers);
    static InternTable table;
    static Preprocessor pp;
    static Parser reference;
    // Each pipeline keeps its own preceding parse, so metadata teardown is charged to the metadata path.
    static Parser pipeline[2];
    const ParseOptions parse_options = { .extension_metadata = true };
    const ParseOptions plain_options = { 0 };
    valid = valid && intern_initialize(&table) && pp_initialize(&pp, &options, &table) &&
            parse_initialize(&reference, &parse_options, options.language) && parse_initialize(&pipeline[0], &plain_options, options.language) &&
            parse_initialize(&pipeline[1], &parse_options, options.language);
    valid = valid && pp_run(&pp, input) && parse_translation_unit(&reference, &pp);
    Arena arena = { 0 };
    GnuSyntax collected = { 0 };
    if (valid)
    {
        arena = arena_reserve(reference.gnu_arena.length + 64);
        valid = arena.base != 0 && gnu_syntax_build(&reference.tokens, &reference.tree, &arena, &collected, true) == GNU_SYNTAX_OK &&
                gnu_benchmark_equal(&collected, &reference.gnu);
    }
    const uint32_t tokens = reference.tokens.count;
    GnuBenchmarkSample samples[BUSTER_GNU_BENCHMARK_PATHS][BUSTER_GNU_BENCHMARK_PASSES] = { 0 };
    struct perf_event_attr counter = {
        .type = PERF_TYPE_HARDWARE,
        .size = sizeof(counter),
        .config = PERF_COUNT_HW_CPU_CYCLES,
        .exclude_kernel = 1,
        .exclude_hv = 1,
    };
    const int cycles = valid ? (int)syscall(SYS_perf_event_open, &counter, 0, -1, -1, 0) : -1;
    bool have_cycles = cycles >= 0;
    volatile uint64_t consumed = 0;
    for (uint32_t pass = 0; valid && pass <= BUSTER_GNU_BENCHMARK_PASSES; pass += 1)
    {
        for (uint32_t offset = 0; valid && offset < BUSTER_GNU_BENCHMARK_PATHS; offset += 1)
        {
            const uint32_t path = (offset + pass) % BUSTER_GNU_BENCHMARK_PATHS;
            if (only != BUSTER_GNU_BENCHMARK_PATHS && path != only)
            {
                continue;
            }
            if (after_scalar != 0 && (path == 1 || path == 3 || path == 5))
            {
                const uint64_t until = gnu_benchmark_clock() + (uint64_t)after_scalar * 1000000;
                uint64_t work = consumed + 1;
                do
                {
                    work = gnu_benchmark_scalar_work(work);
                } while (gnu_benchmark_clock() < until);
                consumed += work;
            }
            const uint32_t repetitions = path < 4 && tokens < 65536 ? 65536 / (tokens != 0 ? tokens : 1) : 1;
            uint64_t before_cycles = 0;
            uint64_t after_cycles = 0;
            have_cycles = have_cycles && read(cycles, &before_cycles, sizeof(before_cycles)) == sizeof(before_cycles);
            const uint64_t before = gnu_benchmark_clock();
            for (uint32_t repeat = 0; valid && repeat < repetitions; repeat += 1)
            {
                if (path < 2)
                {
                    valid = gnu_syntax_collect(&reference.tokens, &reference.tree, &collected, path == 0, false) == GNU_SYNTAX_OK;
                }
                else if (path < 4)
                {
                    arena_pop_to(&arena, 0);
                    valid = gnu_syntax_build(&reference.tokens, &reference.tree, &arena, &collected, path == 2) == GNU_SYNTAX_OK;
                }
                else
                {
                    valid = parse_translation_unit(&pipeline[path - 4], &pp);
                }
                consumed += path < 4 ? collected.attribute_count + collected.asm_label_count : pipeline[path - 4].tokens.count;
            }
            const uint64_t elapsed = gnu_benchmark_clock() - before;
            have_cycles = have_cycles && read(cycles, &after_cycles, sizeof(after_cycles)) == sizeof(after_cycles);
            if (valid)
            {
                valid = path < 4 ? gnu_benchmark_equal(&collected, &reference.gnu) : path == 4 || gnu_benchmark_equal(&pipeline[1].gnu, &reference.gnu);
            }
            if (pass != 0)
            {
                samples[path][pass - 1] = (GnuBenchmarkSample){
                    .nanoseconds = elapsed / repetitions,
                    .cycles = have_cycles ? (after_cycles - before_cycles) / repetitions : 0,
                };
            }
        }
    }
    if (valid)
    {
        printf("GNU syntax: %s, %u tokens, %u groups, %u attributes, %u asm labels; %llu retained metadata bytes\n", input, tokens,
               reference.gnu.group_count, reference.gnu.attribute_count, reference.gnu.asm_label_count, (unsigned long long)reference.gnu_arena.length);
        printf("Parser scalar fallback: %llu tokens, %.3f%%; whole unit: %s\n", (unsigned long long)reference.statistics.fallback_tokens,
               tokens != 0 ? 100.0 * (double)reference.statistics.fallback_tokens / tokens : 0.0, reference.statistics.whole ? "yes" : "no");
        printf("21 rotating passes; preprocessing excluded; after-scalar=%u ms; CPU cycles %s\n", after_scalar, have_cycles ? "available" : "unavailable");
        printf("%-20s %12s %14s %14s\n", "path", "ns/token", "Mtokens/s", "cycles/token");
        for (uint32_t path = 0; path < BUSTER_GNU_BENCHMARK_PATHS; path += 1)
        {
            if (only == BUSTER_GNU_BENCHMARK_PATHS || path == only)
            {
                const uint64_t nanoseconds = gnu_benchmark_median(samples[path], false);
                const double denominator = tokens != 0 ? (double)tokens : 1.0;
                printf("%-20s %12.3f %14.3f ", names[path], (double)nanoseconds / denominator,
                       nanoseconds != 0 ? (double)tokens * 1000.0 / nanoseconds : 0.0);
                if (have_cycles)
                {
                    printf("%14.3f\n", (double)gnu_benchmark_median(samples[path], true) / denominator);
                }
                else
                {
                    printf("%14s\n", "unavailable");
                }
            }
        }
    }
    else
    {
        fprintf(stderr, "benchmark-gnu: invalid arguments, input, allocation failure or metadata mismatch\n");
        fprintf(stderr, "usage: benchmark-gnu [--only collect-scalar|collect-vector|build-scalar|build-vector|parser-only|parser-with-gnu] [--after-scalar MS] [-E's options] file.c\n");
        if (reference.fatal != PARSE_MESSAGE_NONE || reference.errors != 0)
        {
            parse_print_diagnostics(&reference, &pp);
        }
    }
    if (cycles >= 0)
    {
        close(cycles);
    }
    arena_release(&arena);
    parse_release(&pipeline[0]);
    parse_release(&pipeline[1]);
    parse_release(&reference);
    pp_release(&pp);
    intern_release(&table);
    arena_release(&options_arena);
    free(rest);
    return valid ? 0 : 1;
}
