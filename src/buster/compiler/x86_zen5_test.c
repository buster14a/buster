// Copyright (c) 2026 David Gonzalez Martin
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Exact scalar/vector agreement, bounded reads/writes, fail-closed operand cases, and executable harness checks.
// These tests deliberately do not assert that a table's own fixtures prove coverage of a future compiler emitter.

static bool x86_zen5_test_lookup(void)
{
    uint8_t ids[256];
    uint16_t scalar[256];
    uint16_t vector[256];
    uint16_t saved[X86_COST_COLUMN_COUNT][BUSTER_X86_COST_CAPACITY];
    memcpy(saved, x86_zen5_columns, sizeof(saved));
    // Real costs share many values. Distinct synthetic entries catch swapped lanes/banks even where two real
    // instructions happen to have the same measured cost. Restore the actual model before the case checks.
    for (uint32_t column = 0; column < X86_COST_COLUMN_COUNT; column += 1)
    {
        for (uint32_t id = 1; id < BUSTER_X86_COST_CAPACITY; id += 1)
        {
            x86_zen5_columns[column][id] = column * 1000 + id * 7;
        }
    }
    bool valid = true;
    for (uint32_t i = 0; i < 256; i += 1)
    {
        ids[i] = i;
    }
    for (uint32_t column = 0; column < X86_COST_COLUMN_COUNT; column += 1)
    {
        for (uint32_t count = 0; count <= 256; count += 1)
        {
            memset(scalar, 0x5a, sizeof(scalar));
            memset(vector, 0x5a, sizeof(vector));
            x86_zen5_lookup_scalar(ids, count, column, scalar);
            x86_zen5_lookup(ids, count, column, vector);
            valid &= memcmp(scalar, vector, sizeof(scalar)) == 0;
            for (uint32_t i = 0; i < count; i += 1)
            {
                valid &= vector[i] == x86_zen5_columns[column][i < X86_COST_COUNT ? i : 0];
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
        if (writable)
        {
            for (uint32_t column = 0; column < X86_COST_COLUMN_COUNT; column += 1)
            {
                for (uint32_t count = 0; count <= 256; count += 1)
                {
                    uint8_t* source = input + 2 * page - count;
                    uint16_t* destination = (uint16_t*)(output + 2 * page - count * 2);
                    memcpy(source, ids, count);
                    x86_zen5_lookup_scalar(source, count, column, scalar);
                    x86_zen5_lookup(source, count, column, destination);
                    valid &= memcmp(scalar, destination, count * 2) == 0;
                }
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

static bool x86_zen5_test_cases(void)
{
    bool valid = true;
    for (uint32_t id = 1; id < X86_COST_COUNT; id += 1)
    {
        X86Instruction instruction = x86_zen5_fixture(id, X86_REGISTER_RAX, X86_REGISTER_R14, false);
        valid &= x86_validate(&instruction, 1) == 1 && x86_zen5_id(&instruction) == id;
        valid &= x86_zen5_missing(&instruction, 1, 1u << X86_COST_PIPES) == 1;
        valid &= x86_zen5_missing(&instruction, 1, 1u << X86_COST_EXECUTED_UOPS) == 1;
        for (uint32_t column = 0; column < X86_COST_COLUMN_COUNT; column += 1)
        {
            valid &= ((x86_zen5_measured_fields[id] & (1u << column)) != 0) == (x86_zen5_columns[column][id] != BUSTER_X86_COST_UNKNOWN);
        }
        instruction.form += BUSTER_X86_FORM_FS;
        valid &= x86_zen5_id(&instruction) == 0;
        instruction.form = x86_zen5_cases[id].form | BUSTER_X86_FORM_FIXUP;
        valid &= x86_zen5_id(&instruction) == 0;
    }
    const X86Instruction unsupported[] = {
        { .form = X86_FORM_NONE },
        { .form = UINT16_MAX },
        { .form = X86_FORM_ADD_RM64_R64, .mode = BUSTER_X86_MEMORY_BASE },
        { .form = X86_FORM_SUB_RM64_R64, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RAX },
        { .form = X86_FORM_SHL_RM64_IMM8, .immediate = 0 },
        { .form = X86_FORM_SHL_RM64_IMM8, .immediate = 64 },
        { .form = X86_FORM_LEA_R64_M, .mode = BUSTER_X86_MEMORY_RIP },
        { .form = X86_FORM_MOV_R64_RM64, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 1 },
        { .form = X86_FORM_MOV_R64_IMM64, .immediate = 4660, .displacement = 1 },
        { .form = X86_FORM_IMUL_RM64 },
    };
    valid &= x86_zen5_missing(unsupported, sizeof(unsupported) / sizeof(unsupported[0]), 1u << X86_COST_THROUGHPUT) == sizeof(unsupported) / sizeof(unsupported[0]);
    X86Instruction zero = { .form = X86_FORM_XOR_RM64_R64, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RAX };
    valid &= x86_zen5_id(&zero) == X86_COST_XOR_RM64_R64_ZERO;
    zero.reg = X86_REGISTER_RDX;
    valid &= x86_zen5_id(&zero) == X86_COST_XOR_RM64_R64_REGISTER;
    uint64_t parsed = 123;
    valid &= !x86_zen5_number("18446744073709551616", 1, 10000000, &parsed) && parsed == 123;
    valid &= !x86_zen5_number("-1", 1, 10000000, &parsed);
    valid &= !x86_zen5_number("", 1, 10000000, &parsed);
    valid &= !x86_zen5_number("0", 1, 10000000, &parsed);
    valid &= !x86_zen5_number("1x", 1, 10000000, &parsed);
    valid &= x86_zen5_number("10000000", 1, 10000000, &parsed) && parsed == 10000000;
    printf("Zen 5 operand cases and unknown coverage: %u cases, %s\n", X86_COST_COUNT - 1, valid ? "pass" : "FAIL");
    return valid;
}

static bool x86_zen5_test_programs(void)
{
    bool valid = true;
    for (uint32_t id = 0; valid && id < X86_COST_COUNT; id += 1)
    {
        for (uint32_t experiment = 0; valid && experiment < X86_COST_EXPERIMENT_COUNT; experiment += 1)
        {
            X86CostProgram program = {0};
            valid = x86_zen5_program(id, experiment, &program);
            if (valid)
            {
                x86_zen5_execute(&program, 2);
                const uint32_t expected = id == 0 ? 0 : BUSTER_X86_COST_BLOCK * (experiment >= X86_COST_MIX_ADD ? 2 : 1);
                valid &= program.body_count == expected;
                munmap(program.code, BUSTER_X86_COST_CODE_SIZE);
            }
            if (!valid)
            {
                printf("  failed harness %s experiment %u\n", x86_zen5_cases[id].name, experiment);
            }
        }
    }
    printf("Zen 5 harness: all generated loops validated, encoders agree, execute/return: %s\n", valid ? "pass" : "FAIL");
    return valid;
}

static int run_x86_zen5_tests(void)
{
    bool valid = x86_encoder_initialize() && x86_zen5_initialize();
    valid &= x86_zen5_test_lookup();
    valid &= x86_zen5_test_cases();
    valid &= x86_zen5_test_programs();
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
    uint8_t ids[count];
    uint16_t scalar[count];
    uint16_t vector[count];
    uint64_t times[2][passes];
    uint64_t core_cycles[2][passes];
    X86CostCounters counters = { .fd = { -1, -1, -1 } };
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
    for (uint32_t i = 0; i < count; i += 1)
    {
        ids[i] = (i * 37u + i / 17) % X86_COST_COUNT;
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
                if (only >= 0 && implementation != only)
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
                X86CostSample sample = {0};
                valid &= ioctl(counters.fd[0], PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP) == 0;
                valid &= read(counters.fd[0], &sample, sizeof(sample)) == sizeof(sample);
                valid &= sample.count == 3 && sample.running != 0 && sample.enabled == sample.running && sample.values[0] != 0;
                core_cycles[implementation][pass] = sample.values[0];
            }
        }
        for (uint32_t implementation = 0; valid && implementation < 2; implementation += 1)
        {
            if (only >= 0 && implementation != only)
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
    for (uint32_t i = 0; i < 3; i += 1)
    {
        if (counters.fd[i] >= 0)
        {
            close(counters.fd[i]);
        }
    }
    if (!valid)
    {
        fprintf(stderr, "benchmark-x86-zen5 failed: check options, lookup agreement and perf counters\n");
    }
    return valid ? 0 : 1;
}
