// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #939's cost of the certificate of the CFG consumer's code on real C (benchmark-tier1-code-certificate): decoding every
// function's emitted records into its certificate (tier1_certificate_emitted.c) and checking it, per IR instruction and
// per record, beside the emission it certifies, with the certificates' sizes. Include after
// tier1_select_source_benchmark.c, whose corpus it takes (each function lowered, promoted and optimized as the C driver's
// --tier1 takes it), and tier1_emit.c.
//
//   build/compiler benchmark-tier1-code-certificate [--only VARIANT] [preprocessor options] file...
//
// Before the time, each function is planned by the vector path and emitted as --tier1 --verify emits it (every form
// allowed): a function the CFG consumer doesn't take, or whose code's certificate the decoding doesn't cover yet, is
// left out and counted, by reason; a refusal, or a step out of memory or past a limit, fails the benchmark, which then
// reports no time (tier1_code_cert_bench_disposition). The variants, each a run over the functions kept, after a
// warm-up, in BUSTER_TIER0_BENCHMARK_PASSES passes whose order rotates (--only times one alone, back to back):
//
// - emit: tier1_emit_function without the certificates, the code the certificate is of;
// - decode: tier1_certificate_emitted, the program decoded from the records;
// - check: tier1_certificate_check of the programs decoded before the time;
// - certify: tier1_certificate_emitted_check, both, as Tier1EmitOptions.certify runs them.
//
// The checker is scalar, and so is the decoding: no --after-scalar state applies. Run it under the 9700X's lock pinned
// to one CPU (taskset -c 2 ...).

#define BUSTER_TIER1_CODE_CERT_EMIT 0
#define BUSTER_TIER1_CODE_CERT_DECODE 1
#define BUSTER_TIER1_CODE_CERT_CHECK 2
#define BUSTER_TIER1_CODE_CERT_CERTIFY 3
#define BUSTER_TIER1_CODE_CERT_VARIANTS 4

static const char* const tier1_code_cert_bench_names[BUSTER_TIER1_CODE_CERT_VARIANTS] = {
    "emit",
    "decode",
    "check",
    "certify",
};

// The functions kept: each one's plan and emission, its decoded program, and the scratch the largest check needs; and
// the certificates' sizes, summed.
// Why the preparation failed, if it did: a function's code certificate refused it, or a step ran out of room. Either
// leaves no report: the time would be of a smaller corpus than the file's.
typedef enum Tier1CodeCertBenchFailure
{
    TIER1_CODE_CERT_BENCH_PREPARED,
    TIER1_CODE_CERT_BENCH_REFUSED,
    TIER1_CODE_CERT_BENCH_EXHAUSTED,
} Tier1CodeCertBenchFailure;

typedef struct Tier1CodeCertBench Tier1CodeCertBench;
struct Tier1CodeCertBench
{
    Tier1SelectPlan* plans;
    Tier1EmitResult* emitted;
    Tier1CertProgram* programs;
    uint8_t* allowed;
    void* buffer;
    size_t buffer_bytes;
    uint32_t count;
    uint64_t instructions;
    uint64_t records;
    uint64_t edits;
    uint64_t edge_edits;
    uint64_t originals;
    uint64_t sources;
    uint64_t facts;
    uint64_t locations;
    uint64_t units;
    uint64_t program_bytes;
    uint64_t program_bytes_max;
    uint64_t unit_reads;
    uint64_t unit_writes;
    // The corpus's functions the benchmark leaves out: not the CFG consumer's, its emission's failures, and those whose
    // code's shape the decoding doesn't take, by Tier1CertEmittedReason.
    uint64_t straight;
    uint64_t unemitted;
    uint64_t uncertified[TIER1_CERT_EMITTED_REASON_COUNT];
    Tier1CodeCertBenchFailure failure;
};

// A function's planning and emission as the preparation takes them: a refusal of its code, or a selection or emission
// out of memory or past a limit (the code certificate's capacity is TIER1_EMIT_MEMORY), fails the preparation; any other
// failure (an unsupported function, or one the emitter doesn't take) leaves the function out, counted.
static Tier1CodeCertBenchFailure tier1_code_cert_bench_disposition(Tier1SelectError selected, Tier1EmitError emitted)
{
    const bool exhausted = selected == TIER1_SELECT_ERROR_MEMORY || selected == TIER1_SELECT_ERROR_LIMIT || emitted == TIER1_EMIT_MEMORY || emitted == TIER1_EMIT_LIMIT;
    return exhausted ? TIER1_CODE_CERT_BENCH_EXHAUSTED : emitted == TIER1_EMIT_CERTIFICATE ? TIER1_CODE_CERT_BENCH_REFUSED : TIER1_CODE_CERT_BENCH_PREPARED;
}

static Tier1EmitOptions tier1_code_cert_bench_options(bool certify)
{
    const Tier1EmitOptions options = {
        .register_mask = tier1_liveness_register_mask(),
        .vector = true,
        .rematerialize = true,
        .certify = certify,
    };
    return options;
}

static Tier0BenchmarkReading tier1_code_cert_bench_run(uint32_t variant, const Tier1CodeCertBench* bench, Arena* output, Arena* scratch, Tier0BenchmarkCounters counters)
{
    uint32_t failures = 0;
    const uint64_t output_mark = output->length;
    const Tier1EmitOptions options = tier1_code_cert_bench_options(false);
    const Tier0BenchmarkReading begin = tier0_benchmark_read(counters);
    for (uint32_t f = 0; f < bench->count; f += 1)
    {
        if (variant == BUSTER_TIER1_CODE_CERT_EMIT)
        {
            Tier1EmitResult emitted = { 0 };
            uint32_t node = BUSTER_IR_NONE;
            failures += tier1_emit_function(&bench->plans[f], &options, bench->allowed, output, scratch, &emitted, &node) != TIER1_EMIT_OK;
        }
        else if (variant == BUSTER_TIER1_CODE_CERT_DECODE)
        {
            Tier1CertProgram program = { 0 };
            Tier1CertEmittedStats stats = { 0 };
            failures += tier1_certificate_emitted(&bench->plans[f], &bench->emitted[f], output, scratch, &program, &stats) != TIER1_CERT_EMITTED_OK;
        }
        else if (variant == BUSTER_TIER1_CODE_CERT_CHECK)
        {
            Tier1CertReport report = { 0 };
            failures += tier1_certificate_check(&bench->programs[f], bench->buffer, bench->buffer_bytes, &report) != TIER1_CERT_OK;
        }
        else
        {
            Tier1CertReport report = { 0 };
            Tier1CertEmittedStats stats = { 0 };
            failures += tier1_certificate_emitted_check(&bench->plans[f], &bench->emitted[f], output, scratch, &report, &stats) != TIER1_CERT_EMITTED_OK;
        }
        arena_pop_to(output, output_mark);
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

// Plans and emits each of the corpus's functions as --tier1 --verify does, and keeps those whose code is certified,
// with their decoded programs and sizes. It stops at the first failure (tier1_code_cert_bench_disposition, or the
// decoding's refusal or capacity), which Tier1CodeCertBench.failure says.
static bool tier1_code_cert_bench_prepare(Tier1CodeCertBench* bench, const Tier1SelectSourceCorpus* corpus, Arena* kept, Arena* programs, Arena* scratch)
{
    const Tier1EmitOptions options = tier1_code_cert_bench_options(true);
    bool valid = true;
    for (uint32_t f = 0; valid && f < corpus->count; f += 1)
    {
        const uint64_t mark = kept->length;
        Tier1SelectPlan plan = { 0 };
        uint32_t node = BUSTER_IR_NONE;
        const Tier1SelectError selected = tier1_select_plan_verified(&corpus->functions[f], &corpus->dominance[f], 0, 0, kept, scratch, true, &plan, &node);
        bool ok = selected == TIER1_SELECT_OK;
        Tier1EmitResult emitted = { 0 };
        const Tier1EmitError error = ok ? tier1_emit_function(&plan, &options, bench->allowed, kept, scratch, &emitted, &node) : TIER1_EMIT_INVALID;
        ok = ok && error == TIER1_EMIT_OK;
        bench->failure = tier1_code_cert_bench_disposition(selected, error);
        valid = bench->failure == TIER1_CODE_CERT_BENCH_PREPARED;
        bench->unemitted += !ok && valid;
        bench->straight += ok && !emitted.code_checked;
        const bool certified = ok && emitted.code_checked && emitted.code_reason == TIER1_CERT_EMITTED_OK;
        bench->uncertified[emitted.code_reason < TIER1_CERT_EMITTED_REASON_COUNT ? emitted.code_reason : 0] += ok && emitted.code_checked && !certified;
        Tier1CertProgram program = { 0 };
        Tier1CertEmittedStats stats = { 0 };
        const uint64_t program_mark = programs->length;
        if (certified && valid)
        {
            const Tier1CertEmittedReason decoded = tier1_certificate_emitted(&plan, &emitted, programs, scratch, &program, &stats);
            bench->failure = decoded == TIER1_CERT_EMITTED_OK ? bench->failure : decoded == TIER1_CERT_EMITTED_CAPACITY ? TIER1_CODE_CERT_BENCH_EXHAUSTED : TIER1_CODE_CERT_BENCH_REFUSED;
            valid = decoded == TIER1_CERT_EMITTED_OK;
            const size_t size = valid ? tier1_certificate_scratch_size(&program) : 0;
            bench->buffer_bytes = size > bench->buffer_bytes ? size : bench->buffer_bytes;
            bench->program_bytes_max = stats.bytes > bench->program_bytes_max ? stats.bytes : bench->program_bytes_max;
            bench->plans[bench->count] = plan;
            bench->emitted[bench->count] = emitted;
            bench->programs[bench->count] = program;
            bench->count += valid;
            bench->instructions += valid ? corpus->functions[f].instruction_count : 0;
            bench->records += stats.records;
            bench->edits += stats.edits;
            bench->edge_edits += stats.edge_edits;
            bench->originals += stats.originals;
            bench->sources += stats.sources;
            bench->facts += stats.facts;
            bench->locations += stats.locations;
            bench->units += stats.units;
            bench->program_bytes += stats.bytes;
        }
        if (!certified || !valid)
        {
            arena_pop_to(programs, program_mark);
            arena_pop_to(kept, mark);
        }
    }
    return valid;
}

static bool tier1_code_cert_bench_measure(Tier1CodeCertBench* bench, Arena* output, Arena* scratch, uint32_t only)
{
    const Tier0BenchmarkCounters counters = {
        .cycles = tier0_benchmark_counter(PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES),
        .instructions = tier0_benchmark_counter(PERF_TYPE_HARDWARE, PERF_COUNT_HW_INSTRUCTIONS),
        .throttle = tier0_benchmark_counter(PERF_TYPE_RAW, BUSTER_TIER0_BENCHMARK_THROTTLE_EVENT),
    };
    // The checks' scratch, and their reads and writes, once outside the time.
    bench->buffer = bench->buffer_bytes != 0 ? arena_push(scratch, bench->buffer_bytes, 64) : 0;
    uint32_t failed = bench->buffer != 0 ? BUSTER_TIER1_CODE_CERT_VARIANTS : BUSTER_TIER1_CODE_CERT_CHECK;
    for (uint32_t f = 0; failed == BUSTER_TIER1_CODE_CERT_VARIANTS && f < bench->count; f += 1)
    {
        Tier1CertReport report = { 0 };
        failed = tier1_certificate_check(&bench->programs[f], bench->buffer, bench->buffer_bytes, &report) == TIER1_CERT_OK ? failed : BUSTER_TIER1_CODE_CERT_CHECK;
        bench->unit_reads += report.unit_reads;
        bench->unit_writes += report.unit_writes;
    }
    for (uint32_t v = 0; v < BUSTER_TIER1_CODE_CERT_VARIANTS && failed == BUSTER_TIER1_CODE_CERT_VARIANTS; v += 1)
    {
        failed = (only == BUSTER_TIER1_CODE_CERT_VARIANTS || v == only) && tier1_code_cert_bench_run(v, bench, output, scratch, counters).failures != 0 ? v : failed;
    }
    double values[BUSTER_TIER1_CODE_CERT_VARIANTS][4][BUSTER_TIER0_BENCHMARK_PASSES];
    memset(values, 0, sizeof(values));
    for (uint32_t pass = 0; pass < BUSTER_TIER0_BENCHMARK_PASSES && failed == BUSTER_TIER1_CODE_CERT_VARIANTS; pass += 1)
    {
        for (uint32_t k = 0; k < BUSTER_TIER1_CODE_CERT_VARIANTS && failed == BUSTER_TIER1_CODE_CERT_VARIANTS; k += 1)
        {
            // The variants' order rotates by pass.
            const uint32_t v = (k + pass) % BUSTER_TIER1_CODE_CERT_VARIANTS;
            if (only == BUSTER_TIER1_CODE_CERT_VARIANTS || v == only)
            {
                const Tier0BenchmarkReading taken = tier1_code_cert_bench_run(v, bench, output, scratch, counters);
                failed = taken.failures != 0 ? v : failed;
                values[v][0][pass] = (double)taken.nanoseconds;
                values[v][1][pass] = (double)taken.cycles;
                values[v][2][pass] = (double)taken.instructions;
                values[v][3][pass] = (double)taken.throttle;
            }
        }
    }
    if (failed != BUSTER_TIER1_CODE_CERT_VARIANTS)
    {
        printf("benchmark-tier1-code-certificate: a function's %s failed, so its runs measured less than the corpus: no report\n", tier1_code_cert_bench_names[failed]);
    }
    else
    {
        printf("%-10s %12s %12s %12s %12s %8s %12s\n", "variant", "ns/instr", "cyc/instr", "ins/instr", "cyc/record", "IPC", "thr/instr");
        for (uint32_t v = 0; v < BUSTER_TIER1_CODE_CERT_VARIANTS; v += 1)
        {
            if (only == BUSTER_TIER1_CODE_CERT_VARIANTS || v == only)
            {
                const double ns = tier0_benchmark_median(values[v][0]);
                const double cycles = tier0_benchmark_median(values[v][1]);
                const double instructions = tier0_benchmark_median(values[v][2]);
                const double throttle = tier0_benchmark_median(values[v][3]);
                const double r = (double)bench->instructions;
                printf("%-10s %12.2f %12.2f %12.2f %12.2f %8.2f %12.3f\n", tier1_code_cert_bench_names[v], ns / r, cycles / r, instructions / r, cycles / (double)bench->records, cycles != 0 ? instructions / cycles : 0.0, throttle / r);
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
    return failed == BUSTER_TIER1_CODE_CERT_VARIANTS;
}

static __attribute__((unused)) int run_tier1_code_certificate_benchmark(int count, char** arguments)
{
    uint32_t only = BUSTER_TIER1_CODE_CERT_VARIANTS;
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
            for (uint32_t v = 0; valid && v < BUSTER_TIER1_CODE_CERT_VARIANTS; v += 1)
            {
                only = strcmp(arguments[i + 1], tier1_code_cert_bench_names[v]) == 0 ? v : only;
            }
            valid = valid && only != BUSTER_TIER1_CODE_CERT_VARIANTS;
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
    Arena kept[3] = {
        arena_reserve(UINT64_C(1) << 36),
        arena_reserve(UINT64_C(1) << 36),
        arena_reserve(UINT64_C(1) << 36),
    };
    Arena emitted = arena_reserve(UINT64_C(1) << 36);
    Arena programs = arena_reserve(UINT64_C(1) << 36);
    Arena output = arena_reserve(UINT64_C(1) << 34);
    Arena scratch = arena_reserve(UINT64_C(1) << 34);
    Tier1SelectSourceCorpus corpus = {
        .capacity = BUSTER_TIER0_BENCHMARK_MAX_FUNCTIONS,
    };
    Tier1CodeCertBench bench = { 0 };
    valid = valid && lowered.base != 0 && table.base != 0 && kept[0].base != 0 && kept[1].base != 0 && kept[2].base != 0 && emitted.base != 0 && programs.base != 0 && output.base != 0 && scratch.base != 0;
    corpus.functions = valid ? arena_push_array(&kept[2], corpus.capacity, sizeof(IrFunction), _Alignof(IrFunction)) : 0;
    corpus.dominance = valid ? arena_push_array(&kept[2], corpus.capacity, sizeof(IrDominance), _Alignof(IrDominance)) : 0;
    bench.plans = valid ? arena_push_array(&programs, corpus.capacity, sizeof(Tier1SelectPlan), _Alignof(Tier1SelectPlan)) : 0;
    bench.emitted = valid ? arena_push_array(&programs, corpus.capacity, sizeof(Tier1EmitResult), _Alignof(Tier1EmitResult)) : 0;
    bench.programs = valid ? arena_push_array(&programs, corpus.capacity, sizeof(Tier1CertProgram), _Alignof(Tier1CertProgram)) : 0;
    bench.allowed = valid ? arena_push(&programs, X86_FORM_COUNT, 1) : 0;
    valid = valid && corpus.functions != 0 && corpus.dominance != 0 && bench.plans != 0 && bench.emitted != 0 && bench.programs != 0 && bench.allowed != 0;
    if (valid)
    {
        // Every form: the benchmark's code is the host's, as --tier1's on its own target.
        memset(bench.allowed, 1, X86_FORM_COUNT);
    }
    bool joined = valid;
    for (uint32_t i = 0; joined && i < file_count; i += 1)
    {
        joined = tier1_select_source_corpus_file(&corpus, files[i], options, option_count, &lowered, &table, kept, &output, &scratch);
        fflush(stdout);
    }
    bool measured = false;
    if (valid && joined)
    {
        const bool prepared = tier1_code_cert_bench_prepare(&bench, &corpus, &emitted, &programs, &scratch);
        printf("benchmark-tier1-code-certificate: files=%" PRIu64 " functions=%" PRIu64 " selected=%u certified=%u straight=%" PRIu64 " unemitted=%" PRIu64, corpus.files, corpus.seen, corpus.count, bench.count, bench.straight, bench.unemitted);
        for (uint32_t r = 1; r < TIER1_CERT_EMITTED_REASON_COUNT; r += 1)
        {
            if (bench.uncertified[r] != 0)
            {
                printf(" uncertified.%s=%" PRIu64, tier1_cert_emitted_reason_name((Tier1CertEmittedReason)r), bench.uncertified[r]);
            }
        }
        printf("\n");
        printf("benchmark-tier1-code-certificate: instructions=%" PRIu64 " records=%" PRIu64 " edits=%" PRIu64 " edge_edits=%" PRIu64 " originals=%" PRIu64 " sources=%" PRIu64 " facts=%" PRIu64 " locations=%" PRIu64 " units=%" PRIu64
               " program_bytes=%" PRIu64 " program_bytes_max=%" PRIu64 " scratch_bytes_max=%zu\n",
               bench.instructions, bench.records, bench.edits, bench.edge_edits, bench.originals, bench.sources, bench.facts, bench.locations, bench.units, bench.program_bytes, bench.program_bytes_max, bench.buffer_bytes);
        measured = prepared && bench.count != 0 && bench.instructions != 0 && tier1_code_cert_bench_measure(&bench, &output, &scratch, only);
        printf("benchmark-tier1-code-certificate: unit_reads=%" PRIu64 " unit_writes=%" PRIu64 "\n", bench.unit_reads, bench.unit_writes);
        if (!prepared)
        {
            printf("benchmark-tier1-code-certificate: %s: no report\n", bench.failure == TIER1_CODE_CERT_BENCH_EXHAUSTED ? "a function's preparation ran out of room" : "a function's code certificate refused it");
        }
    }
    else if (!valid)
    {
        fprintf(stderr, "usage: compiler benchmark-tier1-code-certificate [--only emit|decode|check|certify] [preprocessor options] file...\n");
    }
    arena_release(&scratch);
    arena_release(&output);
    arena_release(&programs);
    arena_release(&emitted);
    for (uint32_t k = 0; k < 3; k += 1)
    {
        arena_release(&kept[k]);
    }
    arena_release(&table);
    arena_release(&lowered);
    free(files);
    free(options);
    return measured ? 0 : 1;
}
