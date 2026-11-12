// Copyright (c) 2026 David Gonzalez Martin
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// test-x86-zen5 and benchmark-x86-zen5 (#181): exact scalar/vector lookups over every byte ID, length and guarded tail;
// each record's operand kind (zero idioms, LEA's shapes, memory with and without an index) and what doesn't change it
// (immediates, decorations, segment overrides, fixups); the table's completeness (every case accounts for every
// column); and the harness: every case's fixtures resolve to the case, validate, encode alike by both encoders, and run
// where the host's target allows them. These tests check no timing: measure-x86-zen5 --check reproduces the table.

// Every byte ID of every column, every count through 256, with synthetic distinct entries in all 256 slots so that a
// swapped bank or lane shows; then tails that end at a guard page.
static bool x86_zen5_test_lookup(void)
{
    uint8_t ids[256];
    uint16_t scalar[256];
    uint16_t vector[256];
    static uint16_t saved[X86_COST_COLUMN_COUNT][BUSTER_X86_COST_CAPACITY];
    memcpy(saved, x86_zen5_columns, sizeof(saved));
    for (uint32_t column = 0; column < X86_COST_COLUMN_COUNT; column += 1)
    {
        for (uint32_t id = 0; id < BUSTER_X86_COST_CAPACITY; id += 1)
        {
            x86_zen5_columns[column][id] = (uint16_t)(column * 1000 + id * 7 + 1);
        }
    }
    bool valid = true;
    for (uint32_t i = 0; i < 256; i += 1)
    {
        ids[i] = (uint8_t)(i * 37 + 11);
    }
    for (uint32_t column = 0; column < X86_COST_COLUMN_COUNT; column += 1)
    {
        for (uint32_t count = 0; count <= 256; count += 1)
        {
            memset(scalar, 0x5a, sizeof(scalar));
            memset(vector, 0x5a, sizeof(vector));
            x86_zen5_lookup_scalar(ids, count, (X86CostColumn)column, scalar);
            x86_zen5_lookup(ids, count, (X86CostColumn)column, vector);
            valid &= memcmp(scalar, vector, sizeof(scalar)) == 0;
            for (uint32_t i = 0; i < count; i += 1)
            {
                valid &= vector[i] == x86_zen5_columns[column][ids[i]];
            }
        }
    }
    const long page = sysconf(_SC_PAGESIZE);
    uint8_t* input = mmap(0, page * 3, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    uint8_t* output = mmap(0, page * 3, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    valid &= input != MAP_FAILED && output != MAP_FAILED;
    if (input != MAP_FAILED && output != MAP_FAILED)
    {
        const bool writable = mprotect(input + page, page, PROT_READ | PROT_WRITE) == 0 && mprotect(output + page, page, PROT_READ | PROT_WRITE) == 0;
        valid &= writable;
        for (uint32_t column = 0; writable && column < X86_COST_COLUMN_COUNT; column += 1)
        {
            for (uint32_t count = 0; count <= 256; count += 1)
            {
                uint8_t* source = input + 2 * page - count;
                uint16_t* destination = (uint16_t*)(output + 2 * page - count * 2);
                memcpy(source, ids, count);
                x86_zen5_lookup_scalar(source, count, (X86CostColumn)column, scalar);
                x86_zen5_lookup(source, count, (X86CostColumn)column, destination);
                valid &= memcmp(scalar, destination, count * 2) == 0;
            }
        }
    }
    if (input != MAP_FAILED)
    {
        munmap(input, page * 3);
    }
    if (output != MAP_FAILED)
    {
        munmap(output, page * 3);
    }
    memcpy(x86_zen5_columns, saved, sizeof(saved));
    printf("Zen 5 columns: distinct entries, every byte ID, every tail through 256, guarded slices: %s\n", valid ? "pass" : "FAIL");
    return valid;
}

static bool x86_zen5_test_expect(bool condition, const char* what)
{
    if (!condition)
    {
        printf("  failed: %s\n", what);
    }
    return condition;
}

// The ID of `record`, as the case (form, operand kind) whose row it should take.
static bool x86_zen5_test_kind(X86Instruction record, uint32_t form, uint32_t operands, const char* what)
{
    const uint32_t c = x86_zen5_case_find(form, operands);
    return x86_zen5_test_expect(c != X86_COST_CASE_NONE && x86_zen5_id(&record) == x86_zen5_case_ids[c], what);
}

static bool x86_zen5_test_cases(void)
{
    bool valid = x86_zen5_test_expect(x86_zen5_id_count > 1 && x86_zen5_id_count <= BUSTER_X86_COST_CAPACITY, "the rows fit the byte IDs");
    // Every case has its dense ID, and its form's ID for its kind is it.
    for (uint32_t c = 1; c < X86_COST_CASE_COUNT; c += 1)
    {
        const X86CostCase* entry = &x86_zen5_cases[c];
        valid &= x86_zen5_test_expect(x86_zen5_case_ids[c] != 0 && x86_zen5_form_ids[entry->form][entry->operands] == x86_zen5_case_ids[c], entry->name);
    }
    // Zero idioms: two sources one register; another register is the ordinary case.
    valid &= x86_zen5_test_kind((X86Instruction){ .form = X86_FORM_XOR_RM32_R32, .reg = 3, .rm = 3 }, X86_FORM_XOR_RM32_R32, X86_COST_ZERO, "xor r32, r32 is a zero idiom");
    valid &= x86_zen5_test_kind((X86Instruction){ .form = X86_FORM_XOR_RM32_R32, .reg = 3, .rm = 2 }, X86_FORM_XOR_RM32_R32, X86_COST_REGISTER, "xor of two registers");
    valid &= x86_zen5_test_kind((X86Instruction){ .form = X86_FORM_VPXORD_X_X_XM128, .reg = 0, .vvvv = 5, .rm = 5 }, X86_FORM_VPXORD_X_X_XM128, X86_COST_ZERO,
                                "vpxord's vvvv and rm one register");
    valid &= x86_zen5_test_kind((X86Instruction){ .form = X86_FORM_VPXORD_X_X_XM128, .reg = 5, .vvvv = 5, .rm = 4 }, X86_FORM_VPXORD_X_X_XM128, X86_COST_REGISTER,
                                "vpxord's destination is no source");
    // LEA's shapes: RBP and R13 force a displacement, a third component.
    X86Instruction lea = { .form = X86_FORM_LEA_R64_M, .reg = 0, .rm = 3, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 16 };
    valid &= x86_zen5_test_kind(lea, X86_FORM_LEA_R64_M, X86_COST_LEA_BASE, "lea [base + disp]");
    lea.mode = BUSTER_X86_MEMORY_RIP;
    valid &= x86_zen5_test_kind(lea, X86_FORM_LEA_R64_M, X86_COST_LEA_BASE, "lea [rip + disp]");
    lea = (X86Instruction){ .form = X86_FORM_LEA_R64_M, .reg = 0, .rm = 3, .index = 2, .mode = BUSTER_X86_MEMORY_BASE_INDEX(0) };
    valid &= x86_zen5_test_kind(lea, X86_FORM_LEA_R64_M, X86_COST_LEA_INDEX, "lea [base + index]");
    lea.rm = X86_REGISTER_RBP;
    valid &= x86_zen5_test_kind(lea, X86_FORM_LEA_R64_M, X86_COST_LEA_THREE, "lea [rbp + index]");
    lea.rm = X86_REGISTER_R13;
    valid &= x86_zen5_test_kind(lea, X86_FORM_LEA_R64_M, X86_COST_LEA_THREE, "lea [r13 + index]");
    lea.rm = 3;
    lea.mode = BUSTER_X86_MEMORY_BASE_INDEX(2);
    valid &= x86_zen5_test_kind(lea, X86_FORM_LEA_R64_M, X86_COST_LEA_SCALED, "lea [base + index * 4]");
    lea.displacement = -8;
    valid &= x86_zen5_test_kind(lea, X86_FORM_LEA_R64_M, X86_COST_LEA_THREE, "lea [base + index * 4 + disp]");
    lea.mode = BUSTER_X86_MEMORY_INDEX(2);
    valid &= x86_zen5_test_expect(x86_zen5_id(&lea) == 0, "lea [index * 4 + disp] has no row");
    // Memory kinds; what doesn't change a case.
    X86Instruction load = { .form = X86_FORM_MOV_R64_RM64, .reg = 0, .rm = X86_REGISTER_RSP, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 24 };
    valid &= x86_zen5_test_kind(load, X86_FORM_MOV_R64_RM64, X86_COST_MEMORY, "a load from [rsp + disp]");
    load.mode = BUSTER_X86_MEMORY_RIP;
    valid &= x86_zen5_test_kind(load, X86_FORM_MOV_R64_RM64, X86_COST_MEMORY, "a RIP-relative load");
    load.form |= BUSTER_X86_FORM_FIXUP;
    valid &= x86_zen5_test_kind(load, X86_FORM_MOV_R64_RM64, X86_COST_MEMORY, "a fixup mark");
    load.form = X86_FORM_MOV_R64_RM64 + BUSTER_X86_FORM_FS;
    load.mode = BUSTER_X86_MEMORY_BASE;
    valid &= x86_zen5_test_kind(load, X86_FORM_MOV_R64_RM64, X86_COST_MEMORY, "an FS override");
    load.form = X86_FORM_MOV_R64_RM64;
    load.mode = BUSTER_X86_MEMORY_BASE_INDEX(3);
    load.index = 2;
    valid &= x86_zen5_test_kind(load, X86_FORM_MOV_R64_RM64, X86_COST_MEMORY_INDEX, "a load with an index");
    load.mode = BUSTER_X86_MEMORY_INDEX(3);
    valid &= x86_zen5_test_expect(x86_zen5_id(&load) == 0, "a load with an index and no base has no row");
    load.mode = BUSTER_X86_MEMORY_BASE | BUSTER_X86_MODE_ADDRESS_SIZE;
    valid &= x86_zen5_test_expect(x86_zen5_id(&load) == 0, "a 32-bit address has no row");
    valid &= x86_zen5_test_kind((X86Instruction){ .form = X86_FORM_SHL_RM64_IMM8, .rm = 2, .immediate = 0 }, X86_FORM_SHL_RM64_IMM8, X86_COST_REGISTER, "shl by 0");
    valid &= x86_zen5_test_kind((X86Instruction){ .form = X86_FORM_SHL_RM64_IMM8, .rm = 2, .immediate = 63 }, X86_FORM_SHL_RM64_IMM8, X86_COST_REGISTER, "shl by 63");
    valid &= x86_zen5_test_kind((X86Instruction){ .form = X86_FORM_VPADDD_Z_Z_ZM512, .reg = 1, .vvvv = 2, .rm = 3, .decorations = 4 | BUSTER_X86_DECORATION_ZEROING },
                                X86_FORM_VPADDD_Z_Z_ZM512, X86_COST_REGISTER, "an opmask and zeroing");
    // A merging opmask: its own kind, which a form without such a row doesn't have.
    valid &= x86_zen5_test_kind((X86Instruction){ .form = X86_FORM_VPADDD_Z_Z_ZM512, .reg = 1, .vvvv = 2, .rm = 3, .decorations = 4 }, X86_FORM_VPADDD_Z_Z_ZM512,
                                X86_COST_MASKED, "a merging opmask");
    valid &= x86_zen5_test_kind((X86Instruction){ .form = X86_FORM_VMOVDQU32_ZM512_Z, .reg = 1, .rm = X86_REGISTER_RSP, .mode = BUSTER_X86_MEMORY_BASE, .decorations = 4 },
                                X86_FORM_VMOVDQU32_ZM512_Z, X86_COST_MASKED_MEMORY, "a masked store");
    // Any opmask on memory: a zeroing masked load is the masked memory kind; merging into its destination has no row.
    X86Instruction masked_load = { .form = X86_FORM_VMOVDQU8_Z_ZM512, .reg = 1, .rm = X86_REGISTER_RSP, .mode = BUSTER_X86_MEMORY_BASE,
                                   .decorations = 4 | BUSTER_X86_DECORATION_ZEROING };
    valid &= x86_zen5_test_kind(masked_load, X86_FORM_VMOVDQU8_Z_ZM512, X86_COST_MASKED_MEMORY, "a zeroing masked load");
    masked_load.decorations = 4;
    valid &= x86_zen5_test_expect(x86_zen5_id(&masked_load) == 0, "a merging masked load has no row");
    masked_load.decorations = 4 | BUSTER_X86_DECORATION_ZEROING;
    masked_load.mode = BUSTER_X86_MEMORY_BASE_INDEX(0);
    masked_load.index = 2;
    valid &= x86_zen5_test_expect(x86_zen5_id(&masked_load) == 0, "a masked load with an index has no row");
    valid &= x86_zen5_test_kind((X86Instruction){ .form = X86_FORM_VMOVDQU32_Z_ZM512, .reg = 1, .rm = 2, .decorations = 4 | BUSTER_X86_DECORATION_ZEROING },
                                X86_FORM_VMOVDQU32_Z_ZM512, X86_COST_REGISTER, "a zeroing masked register move");
    const X86Instruction unmasked_row = { .form = X86_FORM_VPANDQ_Z_Z_ZM512, .reg = 1, .vvvv = 2, .rm = 3, .decorations = 4 };
    valid &= x86_zen5_test_expect(x86_zen5_id(&unmasked_row) == 0, "a merging opmask without a masked row has no case");
    const X86Instruction unknown[] = {
        { .form = X86_FORM_NONE },
        { .form = UINT16_MAX },
        { .form = X86_FORM_RDRAND_R64 },
    };
    valid &= x86_zen5_test_expect(x86_zen5_missing(unknown, BUSTER_ARRAY_LENGTH(unknown), 0) == BUSTER_ARRAY_LENGTH(unknown), "forms without rows are missing");
    uint64_t parsed = 123;
    valid &= !x86_zen5_number("18446744073709551616", 1, BUSTER_X86_COST_MAX_ITERATIONS, &parsed) && parsed == 123;
    valid &= !x86_zen5_number("-1", 1, BUSTER_X86_COST_MAX_ITERATIONS, &parsed);
    valid &= !x86_zen5_number("", 1, BUSTER_X86_COST_MAX_ITERATIONS, &parsed);
    valid &= !x86_zen5_number("0", 1, BUSTER_X86_COST_MAX_ITERATIONS, &parsed);
    valid &= !x86_zen5_number("1x", 1, BUSTER_X86_COST_MAX_ITERATIONS, &parsed);
    valid &= x86_zen5_number("10000000", 1, BUSTER_X86_COST_MAX_ITERATIONS, &parsed) && parsed == 10000000;
    valid &= !x86_zen5_number("10000001", 1, BUSTER_X86_COST_MAX_ITERATIONS, &parsed);
    printf("Zen 5 operand kinds: %u cases in %u IDs, %s\n", X86_COST_CASE_COUNT - 1, x86_zen5_id_count - 1, valid ? "pass" : "FAIL");
    return valid;
}

// Every case accounts for every column: measured, derived or absent, as measure-x86-zen5 --table writes them. A
// column measured or derived holds a value; an absent or unknown one doesn't. A throughput, retired ops and the pipes
// exist for every case that runs.
static bool x86_zen5_test_table(void)
{
    bool valid = true;
    uint32_t incomplete = 0;
    const char* first = 0;
    for (uint32_t c = 1; c < X86_COST_CASE_COUNT; c += 1)
    {
        const X86CostCase* entry = &x86_zen5_cases[c];
        const uint32_t accounted = (uint32_t)entry->measured | entry->derived | entry->absent;
        bool complete = accounted == BUSTER_X86_COST_ALL_COLUMNS;
        for (uint32_t column = 0; column < X86_COST_COLUMN_COUNT; column += 1)
        {
            const bool known = ((entry->measured | entry->derived) & (1u << column)) != 0;
            complete &= known == (entry->values[column] != BUSTER_X86_COST_UNKNOWN);
        }
        X86CostShape shape;
        x86_zen5_shape(entry->form, entry->operands, &shape);
        const uint32_t always = (1u << X86_COST_THROUGHPUT) | (1u << X86_COST_RETIRED_OPS) | (1u << X86_COST_PIPES);
        complete &= shape.special == X86_COST_TRAP || (entry->absent & always) == 0;
        first = !complete && first == 0 ? entry->name : first;
        incomplete += !complete;
    }
    valid = incomplete == 0;
    printf("Zen 5 table: %u cases, %u incomplete%s%s: %s\n", X86_COST_CASE_COUNT - 1, incomplete, first != 0 ? ", the first " : "", first != 0 ? first : "", valid ? "pass" : "FAIL");
    return valid;
}

// Every case's fixtures and the references: built, their targets the case, validated and encoded alike, and run (twice
// through the loop) where the host allows every record.
static bool x86_zen5_test_programs(void)
{
    static X86CostHost host;
    x86_zen5_host(&host);
    bool valid = true;
    uint32_t built = 0;
    uint32_t ran = 0;
    X86CostShape shape;
    x86_zen5_shape(X86_FORM_NOP, X86_COST_REGISTER, &shape);
    x86_zen5_fill(&shape);
    for (uint32_t r = X86_COST_REFERENCE_EMPTY; valid && r < X86_COST_REFERENCE_COUNT; r += 1)
    {
        X86CostProgram program = { 0 };
        valid = x86_zen5_program(&shape, X86_COST_INDEPENDENT, r, &host, &program);
        if (valid && program.runnable)
        {
            x86_zen5_execute(&program, 2);
            ran += 1;
        }
        built += valid;
        x86_zen5_release(&program, 1);
    }
    for (uint32_t c = 1; valid && c < X86_COST_CASE_COUNT; c += 1)
    {
        const X86CostCase* entry = &x86_zen5_cases[c];
        x86_zen5_shape(entry->form, entry->operands, &shape);
        valid = shape.special == X86_COST_TRAP ? shape.experiments == 0 : (shape.experiments & (1u << X86_COST_INDEPENDENT)) != 0;
        x86_zen5_fill(&shape);
        for (uint32_t e = 0; valid && e < X86_COST_EXPERIMENT_COUNT; e += 1)
        {
            if (shape.experiments & (1u << e))
            {
                X86CostProgram program = { 0 };
                valid = x86_zen5_program_targets(&shape, (X86CostExperiment)e) && x86_zen5_program(&shape, (X86CostExperiment)e, X86_COST_REFERENCE_NONE, &host, &program);
                if (valid && program.runnable)
                {
                    x86_zen5_execute(&program, 2);
                    ran += 1;
                }
                built += valid;
                x86_zen5_release(&program, 1);
                if (!valid)
                {
                    printf("  failed harness %s experiment %s\n", entry->name, x86_zen5_experiment_names[e]);
                }
            }
        }
        if (!valid && shape.experiments == 0)
        {
            printf("  failed harness %s: no experiment\n", entry->name);
        }
    }
    printf("Zen 5 harness: %u programs built, their targets the cases, validated and encoded alike; %u run on this host: %s\n", built, ran, valid ? "pass" : "FAIL");
    return valid;
}

static int run_x86_zen5_tests(void)
{
    bool valid = x86_encoder_initialize() && x86_semantics_initialize() && x86_zen5_initialize();
    valid &= x86_zen5_test_lookup();
    valid &= x86_zen5_test_cases();
    valid &= x86_zen5_test_programs();
    valid &= x86_zen5_test_table();
    return valid ? 0 : 1;
}

static uint64_t x86_zen5_nanoseconds(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC_RAW, &now);
    return (uint64_t)now.tv_sec * 1000000000 + now.tv_nsec;
}

static int run_x86_zen5_benchmark(int argc, char** argv)
{
    enum { count = 65536, repeats = 1024, passes = 21 };
    static uint8_t ids[count];
    static uint16_t scalar[count];
    static uint16_t vector[count];
    uint64_t times[2][passes];
    uint64_t core_cycles[2][passes];
    X86CostCounters counters = { .fd = { -1, -1, -1, -1, -1 } };
    bool valid = x86_zen5_initialize();
    uint64_t conditioning = 0;
    int only = -1;
    for (int i = 0; valid && i < argc; i += 1)
    {
        if (strcmp(argv[i], "--after-scalar") == 0 && i + 1 < argc)
        {
            valid = x86_zen5_number(argv[++i], 1, 1000, &conditioning);
        }
        else if (strcmp(argv[i], "--only") == 0 && i + 1 < argc)
        {
            i += 1;
            only = strcmp(argv[i], "scalar") == 0 ? 0 : strcmp(argv[i], "vector") == 0 ? 1 : -1;
            valid = only >= 0;
        }
        else
        {
            valid = false;
        }
    }
    // IDs over every row, as a selection's records would carry them.
    for (uint32_t i = 0; i < count; i += 1)
    {
        ids[i] = (uint8_t)((i * 37u + i / 17) % x86_zen5_id_count);
    }
    x86_zen5_lookup_scalar(ids, count, X86_COST_THROUGHPUT, scalar);
    x86_zen5_lookup(ids, count, X86_COST_THROUGHPUT, vector);
    valid &= memcmp(scalar, vector, sizeof(scalar)) == 0;
    if (valid)
    {
        valid = x86_zen5_counters(&counters);
        for (uint32_t pass = 0; valid && pass < passes; pass += 1)
        {
            for (uint32_t j = 0; valid && j < 2; j += 1)
            {
                const uint32_t implementation = (pass + j) & 1;
                if (only >= 0 && implementation != (uint32_t)only)
                {
                    continue;
                }
                const uint64_t until = x86_zen5_nanoseconds() + conditioning * 1000000;
                while (x86_zen5_nanoseconds() < until)
                {
                    x86_zen5_lookup_scalar(ids, count, X86_COST_THROUGHPUT, scalar);
                    __asm__ volatile("" : : "m"(scalar) : "memory");
                }
                valid = ioctl(counters.fd[0], PERF_EVENT_IOC_RESET, PERF_IOC_FLAG_GROUP) == 0;
                valid &= ioctl(counters.fd[0], PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP) == 0;
                const uint64_t start = x86_zen5_nanoseconds();
                for (uint32_t repeat = 0; repeat < repeats; repeat += 1)
                {
                    if (implementation == 0)
                    {
                        x86_zen5_lookup_scalar(ids, count, X86_COST_THROUGHPUT, scalar);
                    }
                    else
                    {
                        x86_zen5_lookup(ids, count, X86_COST_THROUGHPUT, vector);
                    }
                    __asm__ volatile("" : : "m"(scalar), "m"(vector) : "memory");
                }
                times[implementation][pass] = x86_zen5_nanoseconds() - start;
                X86CostSample sample = { 0 };
                valid &= ioctl(counters.fd[0], PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP) == 0;
                valid &= read(counters.fd[0], &sample, sizeof(sample)) == sizeof(sample);
                valid &= sample.count == BUSTER_X86_COST_COUNTERS && sample.running != 0 && sample.enabled == sample.running && sample.values[0] != 0;
                core_cycles[implementation][pass] = sample.values[0];
            }
        }
        for (uint32_t implementation = 0; valid && implementation < 2; implementation += 1)
        {
            if (only >= 0 && implementation != (uint32_t)only)
            {
                continue;
            }
            const uint64_t median = x86_zen5_median(times[implementation], passes);
            const uint64_t cycles = x86_zen5_median(core_cycles[implementation], passes);
            const double denominator = (double)count * repeats;
            printf("Zen 5 %s lookup: %.4f ns/ID [%.4f, %.4f], %.4f cycles/ID, %u passes, after-scalar %lu ms; excludes record-to-ID resolution\n",
                   implementation == 0 ? "scalar" : "AVX-512", median / denominator, times[implementation][0] / denominator,
                   times[implementation][passes - 1] / denominator, cycles / denominator, passes, conditioning);
        }
        valid &= memcmp(scalar, vector, sizeof(scalar)) == 0;
    }
    x86_zen5_counters_close(&counters);
    if (!valid)
    {
        fprintf(stderr, "benchmark-x86-zen5 failed: check options, lookup agreement and perf counters\n");
    }
    return valid ? 0 : 1;
}
