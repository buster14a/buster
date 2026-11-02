// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #939's cost on real C (benchmark-tier1-certificate): publishing and checking the certificate of every allocation
// tier1_cfg.c makes of the functions -c compiles of a set of files, per IR instruction, beside the allocation it
// certifies and tier1_cfg_verify, and the certificates' sizes. Include after tier1_cfg_source_benchmark.c, whose corpus
// it takes, and tier1_certificate_cfg.c.
//
//   build/compiler benchmark-tier1-certificate [--only VARIANT] [preprocessor options] file...
//
// The corpus is benchmark-tier1-cfg-source's, with each function's vector-path allocation kept: every one must pass
// tier1_cfg_verify, and its certificate is published and proved once outside the time, its sizes summed; a refusal
// fails the benchmark, which then reports no time. The variants, each a run over every function after a warm-up, in
// BUSTER_TIER0_BENCHMARK_PASSES passes whose order rotates (--only times one alone, back to back):
//
// - allocate: tier1_cfg_allocate, the allocation the certificate is of;
// - publish: tier1_certificate_cfg, the program published from the input and the allocation (the allocator's share);
// - check: tier1_certificate_check of the programs published before the time (the checker alone);
// - certify: tier1_certificate_cfg_check, both, as Tier1EmitOptions.certify runs them;
// - verify: tier1_cfg_verify, the independent structural checker, for comparison.
//
// Each is reported per IR instruction (time, cycles, instructions, IPC) and in cycles per certificate item: the source
// operands, edits, facts and renames the checker's work is linear in. The checker is scalar: no vector path yet, so no
// --after-scalar state applies. Run it under the 9700X's lock pinned to one CPU (taskset -c 2 ...).

#define BUSTER_TIER1_CERT_BENCH_ALLOCATE 0
#define BUSTER_TIER1_CERT_BENCH_PUBLISH 1
#define BUSTER_TIER1_CERT_BENCH_CHECK 2
#define BUSTER_TIER1_CERT_BENCH_CERTIFY 3
#define BUSTER_TIER1_CERT_BENCH_VERIFY 4
#define BUSTER_TIER1_CERT_BENCH_VARIANTS 5

static const char* const tier1_cert_bench_names[BUSTER_TIER1_CERT_BENCH_VARIANTS] = {
    "allocate",
    "publish",
    "check",
    "certify",
    "verify",
};

// The published programs, each in the kept arena, and one scratch buffer as large as the largest needs.
typedef struct Tier1CertBench Tier1CertBench;
struct Tier1CertBench
{
    const Tier1CfgSourceCorpus* corpus;
    Tier1CertProgram* programs;
    void* buffer;
    size_t buffer_bytes;
    // The certificates' sizes, summed (and the largest program's and scratch's bytes).
    uint64_t values;
    uint64_t blocks;
    uint64_t unreachable_blocks;
    uint64_t edges;
    uint64_t positions;
    uint64_t sources;
    uint64_t operands;
    uint64_t transitions;
    uint64_t split_moves;
    uint64_t edge_moves;
    uint64_t facts;
    uint64_t recipe_facts;
    uint64_t renames;
    uint64_t locations;
    uint64_t units;
    uint64_t program_bytes;
    uint64_t program_bytes_max;
    uint64_t scratch_bytes;
    uint64_t scratch_bytes_max;
    uint64_t unit_reads;
    uint64_t unit_writes;
    uint64_t checked_instructions;
    uint64_t checked_edits;
    uint64_t checked_facts;
};

static Tier0BenchmarkReading tier1_cert_bench_run(uint32_t variant, const Tier1CertBench* bench, Arena* output, Arena* scratch, Tier0BenchmarkCounters counters)
{
    const Tier1CfgSourceCorpus* corpus = bench->corpus;
    uint32_t failures = 0;
    const uint64_t output_mark = output->length;
    const uint64_t scratch_mark = scratch->length;
    const Tier0BenchmarkReading begin = tier0_benchmark_read(counters);
    for (uint32_t f = 0; f < corpus->count; f += 1)
    {
        if (variant == BUSTER_TIER1_CERT_BENCH_ALLOCATE)
        {
            Tier1CfgAllocation result;
            failures += tier1_cfg_allocate(&corpus->problems[f], output, scratch, &result) != TIER1_CFG_OK;
        }
        else if (variant == BUSTER_TIER1_CERT_BENCH_PUBLISH)
        {
            Tier1CertProgram program;
            failures += tier1_certificate_cfg(&corpus->problems[f], &corpus->allocations[f], output, scratch, &program, 0) != TIER1_CERT_OK;
        }
        else if (variant == BUSTER_TIER1_CERT_BENCH_CHECK)
        {
            Tier1CertReport report;
            failures += tier1_certificate_check(&bench->programs[f], bench->buffer, bench->buffer_bytes, &report) != TIER1_CERT_OK;
        }
        else if (variant == BUSTER_TIER1_CERT_BENCH_CERTIFY)
        {
            Tier1CertReport report;
            failures += tier1_certificate_cfg_check(&corpus->problems[f], &corpus->allocations[f], output, scratch, &report, 0) != TIER1_CERT_OK;
        }
        else
        {
            Tier1CfgCheck check;
            failures += !tier1_cfg_verify(&corpus->problems[f], &corpus->allocations[f], scratch, &check);
        }
        arena_pop_to(output, output_mark);
        arena_pop_to(scratch, scratch_mark);
    }
    const Tier0BenchmarkReading end = tier0_benchmark_read(counters);
    const Tier0BenchmarkReading taken = {
        .nanoseconds = end.nanoseconds - begin.nanoseconds,
        .cycles = end.cycles - begin.cycles,
        .instructions = end.instructions - begin.instructions,
        .throttle = end.throttle - begin.throttle,
        .failures = failures,
    };
    return taken;
}

// Publishes and proves every function's certificate once, outside the time, into `kept`, summing the sizes; the
// largest scratch the checker needs, which one buffer then holds. False, with the function named, on any refusal.
static bool tier1_cert_bench_prepare(Tier1CertBench* bench, Arena* kept, Arena* scratch)
{
    const Tier1CfgSourceCorpus* corpus = bench->corpus;
    bench->programs = arena_push_array(kept, (uint64_t)corpus->count + 1, sizeof(Tier1CertProgram), _Alignof(Tier1CertProgram));
    bool valid = bench->programs != 0;
    for (uint32_t f = 0; valid && f < corpus->count; f += 1)
    {
        Tier1CertCfgStats stats = { 0 };
        Tier1CertReport report = { 0 };
        const Tier1CertError published = tier1_certificate_cfg(&corpus->problems[f], &corpus->allocations[f], kept, scratch, &bench->programs[f], &stats);
        const size_t size = published == TIER1_CERT_OK ? tier1_certificate_scratch_size(&bench->programs[f]) : 0;
        const uint64_t mark = scratch->length;
        void* buffer = size != 0 ? arena_push(scratch, size, 64) : 0;
        const Tier1CertError checked = buffer != 0 ? tier1_certificate_check(&bench->programs[f], buffer, size, &report) : published != TIER1_CERT_OK ? published : TIER1_CERT_CAPACITY;
        arena_pop_to(scratch, mark);
        valid = checked == TIER1_CERT_OK;
        if (!valid)
        {
            printf("benchmark-tier1-certificate: function %u's certificate failed: error %u (block %u edge %u instruction %u; publication block %u value %u position %u)\n", f, (unsigned)checked, report.block, report.edge, report.instruction, stats.block, stats.value, stats.position);
        }
        bench->values += corpus->problems[f].value_count;
        bench->blocks += stats.blocks;
        bench->unreachable_blocks += stats.unreachable_blocks;
        bench->edges += stats.edges;
        bench->positions += stats.positions;
        bench->sources += stats.sources;
        bench->operands += stats.operands;
        bench->transitions += stats.transitions;
        bench->split_moves += stats.split_moves;
        bench->edge_moves += stats.edge_moves;
        bench->facts += stats.facts;
        bench->recipe_facts += stats.recipe_facts;
        bench->renames += stats.renames;
        bench->locations += stats.locations;
        bench->units += stats.units;
        bench->program_bytes += stats.bytes;
        bench->program_bytes_max = stats.bytes > bench->program_bytes_max ? stats.bytes : bench->program_bytes_max;
        bench->scratch_bytes += report.scratch_bytes;
        bench->scratch_bytes_max = report.scratch_bytes > bench->scratch_bytes_max ? report.scratch_bytes : bench->scratch_bytes_max;
        bench->buffer_bytes = size > bench->buffer_bytes ? size : bench->buffer_bytes;
        bench->unit_reads += report.unit_reads;
        bench->unit_writes += report.unit_writes;
        bench->checked_instructions += report.instructions;
        bench->checked_edits += report.edits;
        bench->checked_facts += report.facts;
    }
    bench->buffer = valid ? arena_push(kept, bench->buffer_bytes + 1, 64) : 0;
    return valid && bench->buffer != 0;
}

static bool tier1_cert_bench_measure(const Tier1CertBench* bench, Arena* output, Arena* scratch, uint32_t only)
{
    const Tier0BenchmarkCounters counters = {
        .cycles = tier0_benchmark_counter(PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES),
        .instructions = tier0_benchmark_counter(PERF_TYPE_HARDWARE, PERF_COUNT_HW_INSTRUCTIONS),
        .throttle = tier0_benchmark_counter(PERF_TYPE_RAW, BUSTER_TIER0_BENCHMARK_THROTTLE_EVENT),
    };
    uint32_t failed = BUSTER_TIER1_CERT_BENCH_VARIANTS;
    // The warm-up, which also checks that every variant passes every function.
    for (uint32_t v = 0; v < BUSTER_TIER1_CERT_BENCH_VARIANTS && failed == BUSTER_TIER1_CERT_BENCH_VARIANTS; v += 1)
    {
        failed = (only == BUSTER_TIER1_CERT_BENCH_VARIANTS || v == only) && tier1_cert_bench_run(v, bench, output, scratch, counters).failures != 0 ? v : failed;
    }
    double values[BUSTER_TIER1_CERT_BENCH_VARIANTS][3][BUSTER_TIER0_BENCHMARK_PASSES];
    memset(values, 0, sizeof(values));
    for (uint32_t pass = 0; pass < BUSTER_TIER0_BENCHMARK_PASSES && failed == BUSTER_TIER1_CERT_BENCH_VARIANTS; pass += 1)
    {
        for (uint32_t k = 0; k < BUSTER_TIER1_CERT_BENCH_VARIANTS && failed == BUSTER_TIER1_CERT_BENCH_VARIANTS; k += 1)
        {
            // The order rotates by pass, so that no variant always follows the same one.
            const uint32_t v = (k + pass) % BUSTER_TIER1_CERT_BENCH_VARIANTS;
            if (only == BUSTER_TIER1_CERT_BENCH_VARIANTS || v == only)
            {
                const Tier0BenchmarkReading taken = tier1_cert_bench_run(v, bench, output, scratch, counters);
                failed = taken.failures != 0 ? v : failed;
                values[v][0][pass] = (double)taken.nanoseconds;
                values[v][1][pass] = (double)taken.cycles;
                values[v][2][pass] = (double)taken.instructions;
            }
        }
    }
    if (failed != BUSTER_TIER1_CERT_BENCH_VARIANTS)
    {
        printf("benchmark-tier1-certificate: a function's %s failed, so its runs measured less than the corpus: no report\n", tier1_cert_bench_names[failed]);
    }
    else
    {
        const double r = (double)bench->corpus->instructions;
        const double items = (double)(bench->operands + bench->transitions + bench->split_moves + bench->edge_moves + bench->facts + bench->renames);
        printf("%-10s %12s %12s %12s %8s %12s\n", "variant", "ns/instr", "cyc/instr", "ins/instr", "IPC", "cyc/item");
        for (uint32_t v = 0; v < BUSTER_TIER1_CERT_BENCH_VARIANTS; v += 1)
        {
            if (only == BUSTER_TIER1_CERT_BENCH_VARIANTS || v == only)
            {
                const double ns = tier0_benchmark_median(values[v][0]);
                const double cycles = tier0_benchmark_median(values[v][1]);
                const double instructions = tier0_benchmark_median(values[v][2]);
                printf("%-10s %12.2f %12.2f %12.2f %8.2f %12.2f\n", tier1_cert_bench_names[v], ns / r, cycles / r, instructions / r, cycles != 0 ? instructions / cycles : 0.0, items != 0 ? cycles / items : 0.0);
            }
        }
    }
    const int descriptors[] = {
        counters.cycles,
        counters.instructions,
        counters.throttle,
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(descriptors); i += 1)
    {
        if (descriptors[i] >= 0)
        {
            close(descriptors[i]);
        }
    }
    return failed == BUSTER_TIER1_CERT_BENCH_VARIANTS;
}

static __attribute__((unused)) int run_tier1_certificate_benchmark(int count, char** arguments)
{
    uint32_t only = BUSTER_TIER1_CERT_BENCH_VARIANTS;
    char** options = calloc((size_t)count + 1, sizeof(char*));
    char** files = calloc((size_t)count + 1, sizeof(char*));
    uint32_t option_count = 0;
    uint32_t file_count = 0;
    bool valid = options != 0 && files != 0;
    for (int i = 0; valid && i < count; i += 1)
    {
        if (strcmp(arguments[i], "--only") == 0)
        {
            valid = i + 1 < count;
            for (uint32_t v = 0; valid && v < BUSTER_TIER1_CERT_BENCH_VARIANTS; v += 1)
            {
                only = strcmp(arguments[i + 1], tier1_cert_bench_names[v]) == 0 ? v : only;
            }
            valid = valid && only != BUSTER_TIER1_CERT_BENCH_VARIANTS;
            i += 1;
        }
        else if (arguments[i][0] != '-')
        {
            files[file_count] = arguments[i];
            file_count += 1;
        }
        else if (strcmp(arguments[i], "--no-default-config") != 0)
        {
            options[option_count] = arguments[i];
            option_count += 1;
            if (pp_option_takes_value(arguments[i]))
            {
                valid = i + 1 < count;
                options[option_count] = valid ? arguments[i + 1] : 0;
                option_count += 1;
                i += 1;
            }
        }
    }
    valid = valid && file_count != 0;
    Arena lowered = arena_reserve(UINT64_C(1) << 36);
    Arena table = arena_reserve(UINT64_C(1) << 32);
    Arena kept = arena_reserve(UINT64_C(1) << 35);
    Arena ir = arena_reserve(UINT64_C(1) << 34);
    Arena output = arena_reserve(UINT64_C(1) << 34);
    Arena scratch = arena_reserve(UINT64_C(1) << 34);
    Tier1CfgSourceCorpus corpus = {
        .capacity = BUSTER_TIER0_BENCHMARK_MAX_FUNCTIONS,
    };
    valid = valid && lowered.base != 0 && table.base != 0 && kept.base != 0 && ir.base != 0 && output.base != 0 && scratch.base != 0;
    corpus.problems = valid ? arena_push_array(&kept, corpus.capacity, sizeof(Tier1CfgInput), _Alignof(Tier1CfgInput)) : 0;
    corpus.intervals = valid ? arena_push_array(&kept, corpus.capacity, sizeof(Tier1AllocationInput), _Alignof(Tier1AllocationInput)) : 0;
    corpus.allocations = valid ? arena_push_array(&kept, corpus.capacity, sizeof(Tier1CfgAllocation), _Alignof(Tier1CfgAllocation)) : 0;
    valid = valid && corpus.problems != 0 && corpus.intervals != 0 && corpus.allocations != 0;
    bool joined = valid;
    for (uint32_t i = 0; joined && i < file_count; i += 1)
    {
        joined = tier1_cfg_source_corpus_file(&corpus, files[i], options, option_count, &lowered, &table, &kept, &ir, &output, &scratch);
        fflush(stdout);
    }
    Tier1CertBench bench = {
        .corpus = &corpus,
    };
    bool measured = false;
    if (valid && joined && corpus.count != 0 && corpus.instructions != 0 && tier1_cert_bench_prepare(&bench, &kept, &scratch))
    {
        printf("benchmark-tier1-certificate: files=%" PRIu64 " functions=%u left_out=%" PRIu64 " instructions=%" PRIu64 " values=%" PRIu64 " blocks=%" PRIu64 " unreachable_blocks=%" PRIu64 " edges=%" PRIu64 "\n", corpus.files, corpus.count, corpus.left_out, corpus.instructions, bench.values, bench.blocks, bench.unreachable_blocks, bench.edges);
        printf("benchmark-tier1-certificate: source positions=%" PRIu64 " records=%" PRIu64 " operands=%" PRIu64 "; edits transitions=%" PRIu64 " split_moves=%" PRIu64 " edge_moves=%" PRIu64 "; certificate facts=%" PRIu64 " recipe_facts=%" PRIu64 " renames=%" PRIu64 "\n", bench.positions, bench.sources, bench.operands, bench.transitions, bench.split_moves, bench.edge_moves, bench.facts, bench.recipe_facts, bench.renames);
        printf("benchmark-tier1-certificate: storage locations=%" PRIu64 " units=%" PRIu64 "; program_bytes=%" PRIu64 " (largest %" PRIu64 ") checker_scratch_bytes=%" PRIu64 " (largest %" PRIu64 "); checker unit_reads=%" PRIu64 " unit_writes=%" PRIu64 " instructions=%" PRIu64 " edits=%" PRIu64 " facts=%" PRIu64 "\n", bench.locations, bench.units, bench.program_bytes, bench.program_bytes_max, bench.scratch_bytes, bench.scratch_bytes_max, bench.unit_reads, bench.unit_writes, bench.checked_instructions, bench.checked_edits, bench.checked_facts);
        printf("benchmark-tier1-certificate: per IR instruction: facts=%.3f edits=%.3f operands=%.3f program_bytes=%.1f\n", (double)bench.facts / (double)corpus.instructions, (double)(bench.transitions + bench.split_moves + bench.edge_moves) / (double)corpus.instructions, (double)bench.operands / (double)corpus.instructions, (double)bench.program_bytes / (double)corpus.instructions);
        fflush(stdout);
        measured = tier1_cert_bench_measure(&bench, &output, &scratch, only);
    }
    else if (!valid)
    {
        fprintf(stderr, "usage: compiler benchmark-tier1-certificate [--only allocate|publish|check|certify|verify] [preprocessor options] file...\n");
    }
    arena_release(&scratch);
    arena_release(&output);
    arena_release(&ir);
    arena_release(&kept);
    arena_release(&table);
    arena_release(&lowered);
    free(files);
    free(options);
    return measured ? 0 : 1;
}
