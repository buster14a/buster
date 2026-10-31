// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// benchmark-tier1-pipeline (#140): the run time of the pipeline test's fixtures (tier1_pipeline_test.c), each compiled
// four ways: by Tier 1 (its typed stream lowered, promoted, optimized, selected and emitted, general control flow
// through the CFG consumer), by Tier 0 (tier0_compile of the same typed stream), and by clang at -O0 and -O2 from the
// fixtures' C formulas, as shared objects loaded in place. Every compile's results on the fixtures' 32 inputs must be the
// typed interpreter's before anything is timed. Then each fixture's four functions are timed in 21 passes, a pass calling
// one compile's function on every input `--repeat` times, the compiles' order rotating by pass; the report gives each
// one's median in ns per call. These are bounded fixtures, kernels rather than programs: run time on a corpus needs Tier
// 1 end to end (#138). Include after tier1_pipeline_test.c and tier0_test.c.
//
//   build/compiler benchmark-tier1-pipeline [--clang PATH] [--repeat N] [--seed N] [--dump DIRECTORY]
//
// --dump writes each fixture's Tier 1 and Tier 0 entry functions' bytes, to the end of their mappings, to DIRECTORY as
// tier1-K.bin and tier0-K.bin, for objdump -D -b binary -m i386:x86-64.
// Run it under the 9700X's lock, pinned to one CPU (taskset -c 2 ...).

#include <dlfcn.h>
#include <math.h>

#define BUSTER_TIER1_PIPELINE_BENCHMARK_PASSES 21
#define BUSTER_TIER1_PIPELINE_BENCHMARK_COMPILES 4

typedef uint64_t (*Tier1PipelineEntry)(uint64_t, uint64_t, uint64_t);

static const char* const tier1_pipeline_benchmark_names[BUSTER_TIER1_PIPELINE_BENCHMARK_COMPILES] = {
    "tier1",
    "tier0",
    "clang-O0",
    "clang-O2",
};

static Tier1PipelineEntry tier1_pipeline_benchmark_entry(const void* address)
{
    Tier1PipelineEntry entry;
    memcpy(&entry, &address, sizeof(entry));
    return entry;
}

// Every input `repeat` times; the sum keeps the calls.
static __attribute__((noinline, no_sanitize("function"))) uint64_t tier1_pipeline_benchmark_calls(Tier1PipelineEntry entry, const uint64_t inputs[BUSTER_TIER1_PIPELINE_INPUTS][3], uint32_t repeat)
{
    uint64_t sum = 0;
    for (uint32_t r = 0; r < repeat; r += 1)
    {
        for (uint32_t i = 0; i < BUSTER_TIER1_PIPELINE_INPUTS; i += 1)
        {
            sum += entry(inputs[i][0], inputs[i][1], inputs[i][2]);
        }
    }
    return sum;
}

static __attribute__((no_sanitize("function"))) bool tier1_pipeline_benchmark_agrees(Tier1PipelineTest* test, Tier1PipelineEntry entry)
{
    bool agrees = true;
    for (uint32_t i = 0; i < BUSTER_TIER1_PIPELINE_INPUTS; i += 1)
    {
        agrees = agrees && entry(test->inputs[i][0], test->inputs[i][1], test->inputs[i][2]) == test->expected[test->fixture][i];
    }
    return agrees;
}

// The fixture's Tier 1 code, as the pipeline test makes it for the vector path with rematerialization, but with every
// caller-saved register and the red zone: its mapping and the entry's start, or MAP_FAILED.
static uint8_t* tier1_pipeline_benchmark_tier1(Tier1PipelineTest* test, const Tier1PipelineFixture* fixture, uint64_t* mapped, uint64_t* entry)
{
    test->vector = true;
    test->rematerialize = true;
    tier1_pipeline_reset(test);
    const Tier1EmitOptions options = {
        .register_mask = tier1_liveness_register_mask(),
        .vector = true,
        .rematerialize = true,
    };
    Tier1EmitResult bodies[BUSTER_TIER1_PIPELINE_FUNCTIONS];
    bool ok = fixture->program.function_count <= BUSTER_TIER1_PIPELINE_FUNCTIONS;
    for (uint32_t f = 0; ok && f < fixture->program.function_count; f += 1)
    {
        Tier0Functions functions;
        IrFunction function;
        IrDominance dominance;
        Tier1SelectPlan plan;
        uint32_t node = BUSTER_IR_NONE;
        ok = tier1_pipeline_upstream(test, &fixture->program, f, &functions, &function, &dominance);
        ok = ok && tier1_select_plan(&function, &dominance, &test->arenas[TIER1_PIPELINE_SELECTED], &test->arenas[TIER1_PIPELINE_SCRATCH], true, &plan, &node) == TIER1_SELECT_OK;
        ok = ok && tier1_emit_function(&plan, &options, test->allowed, &test->arenas[TIER1_PIPELINE_EMITTED], &test->arenas[TIER1_PIPELINE_SCRATCH], &bodies[f], &node) == TIER1_EMIT_OK;
    }
    uint64_t starts[BUSTER_TIER1_PIPELINE_FUNCTIONS] = { 0 };
    uint8_t* machine = ok ? tier1_pipeline_load(test, &fixture->program, bodies, mapped, starts) : MAP_FAILED;
    *entry = starts[fixture->program.function_count - 1];
    return machine;
}

// The fixture's Tier 0 code, by tier0_test.c's builder, its calls resolved: its mapping and the entry's start, or
// MAP_FAILED.
static uint8_t* tier1_pipeline_benchmark_tier0(Tier1PipelineTest* test, const Tier1PipelineFixture* fixture, Tier0TestCode* code, uint64_t* mapped, uint64_t* entry)
{
    tier1_pipeline_reset(test);
    Tier0Functions functions;
    Tier0TestTally tally = { 0 };
    bool ok = tier0_program_functions(&fixture->program, &test->tables, &test->arenas[TIER1_PIPELINE_ABI], &functions);
    ok = ok && tier0_test_build(&tally, &functions, &test->arenas[TIER1_PIPELINE_SCRATCH], code, test->seed);
    *mapped = ((uint64_t)code->size + 4095) & ~UINT64_C(4095);
    uint8_t* region = ok ? mmap(0, *mapped, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) : MAP_FAILED;
    ok = region != MAP_FAILED;
    if (ok)
    {
        memcpy(region, code->code, code->size);
    }
    // A fixture has no globals: every fixup is a call of one of its functions, symbol f + 1.
    for (uint32_t x = 0; ok && x < code->fixup_count; x += 1)
    {
        const SymbolId target = code->targets[x];
        ok = code->kinds[x] == BUSTER_LINK_FIXUP_CALL && target != 0 && target <= fixture->program.function_count;
        const int64_t value = ok ? (int64_t)code->offsets[target - 1] - (int64_t)code->ends[x] : 0;
        ok = ok && value >= INT32_MIN && value <= INT32_MAX;
        const int32_t field = (int32_t)value;
        if (ok)
        {
            memcpy(region + code->fields[x], &field, sizeof(field));
        }
    }
    ok = ok && mprotect(region, *mapped, PROT_READ | PROT_EXEC) == 0;
    if (!ok && region != MAP_FAILED)
    {
        munmap(region, *mapped);
    }
    *entry = code->offsets[fixture->program.function_count - 1];
    return ok ? region : MAP_FAILED;
}

// The fixtures' C functions, buster_fixture_K, compiled by clang at -O0 and -O2 into shared objects in `directory` and
// loaded: the two handles, or false.
static bool tier1_pipeline_benchmark_clang(const char* clang, const char* directory, void** handles)
{
    char source[4096];
    snprintf(source, sizeof(source), "%s/fixtures.c", directory);
    FILE* file = fopen(source, "w");
    bool ok = file != 0;
    if (ok)
    {
        fputs(tier1_pipeline_prelude, file);
        for (uint32_t k = 0; k < BUSTER_TIER1_PIPELINE_CASES; k += 1)
        {
            fprintf(file, "uint64_t buster_fixture_%u(uint64_t a, uint64_t b, uint64_t c)\n{\n    uint64_t result = 0;\n    %s\n    return result;\n}\n", k, tier1_pipeline_formulas[k]);
        }
        ok = !ferror(file);
        ok = fclose(file) == 0 && ok;
    }
    for (uint32_t level = 0; ok && level < 2; level += 1)
    {
        char library[4096];
        snprintf(library, sizeof(library), "%s/fixtures-O%u.so", directory, level * 2);
        char* const compile[] = {
            (char*)clang,
            "-std=c17",
            level == 0 ? "-O0" : "-O2",
            "-march=x86-64",
            "-fPIC",
            "-shared",
            "-Wall",
            "-Werror",
            source,
            "-o",
            library,
            0,
        };
        const Tier1PipelineProcess compiled = tier1_pipeline_process(compile, 0, 60000);
        ok = tier1_pipeline_process_ok(&compiled);
        handles[level] = ok ? dlopen(library, RTLD_NOW | RTLD_LOCAL) : 0;
        ok = handles[level] != 0;
    }
    return ok;
}

static double tier1_pipeline_benchmark_now(void)
{
    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC_RAW, &time);
    return (double)time.tv_sec * 1e9 + (double)time.tv_nsec;
}

static int tier1_pipeline_benchmark_compare(const void* left, const void* right)
{
    const double a = *(const double*)left;
    const double b = *(const double*)right;
    return (a > b) - (a < b);
}

static __attribute__((unused)) int run_tier1_pipeline_benchmark(int count, char** arguments)
{
    const char* clang = "clang";
    const char* dump = 0;
    uint32_t repeat = 2000;
    uint64_t seed = UINT64_C(0xb138134177);
    bool valid = true;
    for (int i = 0; valid && i < count; i += 1)
    {
        char* end = 0;
        if (strcmp(arguments[i], "--clang") == 0 && i + 1 < count)
        {
            clang = arguments[i + 1];
            i += 1;
        }
        else if (strcmp(arguments[i], "--repeat") == 0 && i + 1 < count)
        {
            const unsigned long long value = strtoull(arguments[i + 1], &end, 10);
            valid = end != arguments[i + 1] && *end == 0 && value != 0 && value <= 1000000;
            repeat = (uint32_t)value;
            i += 1;
        }
        else if (strcmp(arguments[i], "--dump") == 0 && i + 1 < count)
        {
            dump = arguments[i + 1];
            i += 1;
        }
        else if (strcmp(arguments[i], "--seed") == 0 && i + 1 < count)
        {
            seed = strtoull(arguments[i + 1], &end, 0);
            valid = end != arguments[i + 1] && *end == 0;
            i += 1;
        }
        else
        {
            valid = false;
        }
    }
    int result = 1;
    Tier1PipelineTest test = {
        .seed = seed,
        .allowed = malloc(X86_FORM_COUNT),
    };
    Tier0TestCode* tier0_code = calloc(1, sizeof(Tier0TestCode));
    uint8_t* tier0_bytes = malloc(BUSTER_TIER0_TEST_CODE_BYTES);
    const char* temporary = getenv("TMPDIR");
    temporary = temporary != 0 && temporary[0] != 0 ? temporary : "/tmp";
    char directory[4096];
    snprintf(directory, sizeof(directory), "%s/buster-tier1-pipeline-benchmark-XXXXXX", temporary);
    void* handles[2] = { 0, 0 };
    bool ready = valid && test.allowed != 0 && tier0_code != 0 && tier0_bytes != 0 && x86_encoder_initialize() && x86_semantics_initialize() && x86_zen5_initialize() && tier0_initialize() && tier0_tables_open(&test.tables);
    for (uint32_t a = 0; a < BUSTER_TIER1_PIPELINE_ARENAS; a += 1)
    {
        test.arenas[a] = arena_reserve(8u << 20);
        ready = test.arenas[a].base != 0 && ready;
    }
    const bool made = ready && mkdtemp(directory) != 0;
    ready = made && tier1_pipeline_benchmark_clang(clang, directory, handles);
    if (!valid)
    {
        fprintf(stderr, "usage: compiler benchmark-tier1-pipeline [--clang PATH] [--repeat N] [--seed N] [--dump DIRECTORY]\n");
    }
    else if (!ready)
    {
        fprintf(stderr, "benchmark-tier1-pipeline: setup failed (tables, arenas, or clang's shared objects in %s)\n", directory);
    }
    else
    {
        const X86Target target = {
            .architecture = X86_ARCH_BASELINE,
        };
        x86_target_forms(&target, test.allowed);
        tier1_pipeline_inputs(&test, seed);
        tier0_code->code = tier0_bytes;
        printf("benchmark-tier1-pipeline: fixtures' kernels, not programs; ns per call, medians of %u passes of %u calls each\n", BUSTER_TIER1_PIPELINE_BENCHMARK_PASSES, repeat * BUSTER_TIER1_PIPELINE_INPUTS);
        printf("fixture,shape,tier1,tier0,clang-O0,clang-O2\n");
        double logs[BUSTER_TIER1_PIPELINE_BENCHMARK_COMPILES] = { 0, 0, 0, 0 };
        bool measured = true;
        uint64_t sink = 0;
        for (uint32_t k = 0; measured && k < BUSTER_TIER1_PIPELINE_CASES; k += 1)
        {
            const uint32_t shape = tier1_pipeline_shapes[k];
            test.fixture = k;
            Tier1PipelineFixture fixture;
            Tier1PipelineFixture original;
            tier1_pipeline_fixture(&fixture, shape);
            tier1_pipeline_reset(&test);
            measured = tier1_pipeline_freeze(&test, &fixture, &original);
            uint64_t tier1_mapped = 0;
            uint64_t tier1_entry = 0;
            uint64_t tier0_mapped = 0;
            uint64_t tier0_entry = 0;
            uint8_t* tier1_machine = measured ? tier1_pipeline_benchmark_tier1(&test, &fixture, &tier1_mapped, &tier1_entry) : MAP_FAILED;
            uint8_t* tier0_machine = measured ? tier1_pipeline_benchmark_tier0(&test, &fixture, tier0_code, &tier0_mapped, &tier0_entry) : MAP_FAILED;
            char name[64];
            snprintf(name, sizeof(name), "buster_fixture_%u", k);
            void* clang_o0 = dlsym(handles[0], name);
            void* clang_o2 = dlsym(handles[1], name);
            const Tier1PipelineEntry entries[BUSTER_TIER1_PIPELINE_BENCHMARK_COMPILES] = {
                tier1_machine != MAP_FAILED ? tier1_pipeline_benchmark_entry(tier1_machine + tier1_entry) : 0,
                tier0_machine != MAP_FAILED ? tier1_pipeline_benchmark_entry(tier0_machine + tier0_entry) : 0,
                clang_o0 != 0 ? tier1_pipeline_benchmark_entry(clang_o0) : 0,
                clang_o2 != 0 ? tier1_pipeline_benchmark_entry(clang_o2) : 0,
            };
            for (uint32_t c = 0; measured && c < BUSTER_TIER1_PIPELINE_BENCHMARK_COMPILES; c += 1)
            {
                measured = entries[c] != 0 && tier1_pipeline_benchmark_agrees(&test, entries[c]);
                if (!measured)
                {
                    fprintf(stderr, "benchmark-tier1-pipeline: fixture %u's %s code is missing or disagrees with the typed interpreter\n", k, tier1_pipeline_benchmark_names[c]);
                }
            }
            double samples[BUSTER_TIER1_PIPELINE_BENCHMARK_COMPILES][BUSTER_TIER1_PIPELINE_BENCHMARK_PASSES];
            for (uint32_t c = 0; measured && c < BUSTER_TIER1_PIPELINE_BENCHMARK_COMPILES; c += 1)
            {
                sink += tier1_pipeline_benchmark_calls(entries[c], test.inputs, repeat);
            }
            for (uint32_t pass = 0; measured && pass < BUSTER_TIER1_PIPELINE_BENCHMARK_PASSES; pass += 1)
            {
                for (uint32_t turn = 0; turn < BUSTER_TIER1_PIPELINE_BENCHMARK_COMPILES; turn += 1)
                {
                    const uint32_t c = (turn + pass) % BUSTER_TIER1_PIPELINE_BENCHMARK_COMPILES;
                    const double begin = tier1_pipeline_benchmark_now();
                    sink += tier1_pipeline_benchmark_calls(entries[c], test.inputs, repeat);
                    samples[c][pass] = (tier1_pipeline_benchmark_now() - begin) / ((double)repeat * BUSTER_TIER1_PIPELINE_INPUTS);
                }
            }
            if (measured)
            {
                printf("%u,%u", k, shape);
                for (uint32_t c = 0; c < BUSTER_TIER1_PIPELINE_BENCHMARK_COMPILES; c += 1)
                {
                    qsort(samples[c], BUSTER_TIER1_PIPELINE_BENCHMARK_PASSES, sizeof(double), tier1_pipeline_benchmark_compare);
                    const double median = samples[c][BUSTER_TIER1_PIPELINE_BENCHMARK_PASSES / 2];
                    logs[c] += log(median);
                    printf(",%.3f", median);
                }
                printf("\n");
            }
            for (uint32_t c = 0; dump != 0 && c < 2; c += 1)
            {
                // The entry function's bytes to its mapping's end, for objdump -D -b binary -m i386:x86-64.
                char path[4200];
                snprintf(path, sizeof(path), "%s/%s-%u.bin", dump, tier1_pipeline_benchmark_names[c], k);
                FILE* out = (c == 0 ? tier1_machine : tier0_machine) != MAP_FAILED ? fopen(path, "wb") : 0;
                if (out != 0)
                {
                    const uint8_t* bytes = c == 0 ? tier1_machine + tier1_entry : tier0_machine + tier0_entry;
                    fwrite(bytes, 1, (c == 0 ? tier1_mapped - tier1_entry : tier0_mapped - tier0_entry), out);
                    fclose(out);
                }
            }
            if (tier1_machine != MAP_FAILED)
            {
                munmap(tier1_machine, tier1_mapped);
            }
            if (tier0_machine != MAP_FAILED)
            {
                munmap(tier0_machine, tier0_mapped);
            }
        }
        if (measured)
        {
            printf("geometric-mean");
            for (uint32_t c = 0; c < BUSTER_TIER1_PIPELINE_BENCHMARK_COMPILES; c += 1)
            {
                printf(",%s=%.3f", tier1_pipeline_benchmark_names[c], exp(logs[c] / BUSTER_TIER1_PIPELINE_CASES));
            }
            printf("\nbenchmark-tier1-pipeline: checksum %" PRIu64 "\n", sink);
        }
        result = measured ? 0 : 1;
    }
    for (uint32_t level = 0; level < 2; level += 1)
    {
        if (handles[level] != 0)
        {
            dlclose(handles[level]);
        }
    }
    if (made)
    {
        char path[4200];
        snprintf(path, sizeof(path), "%s/fixtures.c", directory);
        unlink(path);
        snprintf(path, sizeof(path), "%s/fixtures-O0.so", directory);
        unlink(path);
        snprintf(path, sizeof(path), "%s/fixtures-O2.so", directory);
        unlink(path);
        rmdir(directory);
    }
    for (uint32_t a = 0; a < BUSTER_TIER1_PIPELINE_ARENAS; a += 1)
    {
        arena_release(&test.arenas[a]);
    }
    tier0_tables_close(&test.tables);
    free(tier0_bytes);
    free(tier0_code);
    free(test.allowed);
    return result;
}
