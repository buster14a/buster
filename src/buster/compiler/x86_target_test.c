// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Target policy tests. CPUID inputs are synthetic as well as live; Clang is an independent preprocessor oracle.
// The encoder's random-record generator supplies valid operands, not ISA expectations.
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>

#include "buster/compiler/x86_target_policy_test.c"

// The backend unit owns the selector fixtures, so this call does not duplicate them in the frontend unit.
bool run_x86_target_selection_tests(void);

static bool x86_target_test_cpuid(void)
{
    X86CpuId cpu = { .xcr0 = 0xe6, .znver5 = true };
    for (uint32_t i = 0; i < sizeof(cpu.words) / sizeof(cpu.words[0]); i += 1)
    {
        for (uint32_t j = 0; j < 4; j += 1)
        {
            cpu.words[i][j] = UINT32_MAX;
        }
    }
    const uint64_t all = (UINT64_C(1) << X86_FEATURE_COUNT) - 1;
    bool passed = x86_target_from_cpuid(&cpu).features == all;
    const uint64_t disabled[] = { 0, 2, 4, 6, 0x26, 0x66, 0xa6 };
    for (uint32_t i = 0; i < sizeof(disabled) / sizeof(disabled[0]); i += 1)
    {
        cpu.xcr0 = disabled[i];
        const X86Target target = x86_target_from_cpuid(&cpu);
        passed &= (target.features & BUSTER_X86_FEATURE(AVX512F)) == 0;
        passed &= ((target.features & BUSTER_X86_FEATURE(AVX)) != 0) == ((disabled[i] & 6) == 6);
        passed &= ((target.features & BUSTER_X86_FEATURE(XSAVE)) != 0) == ((disabled[i] & 6) == 6);
        passed &= (target.features & (BUSTER_X86_FEATURE(BMI) | BUSTER_X86_FEATURE(BMI2))) == (BUSTER_X86_FEATURE(BMI) | BUSTER_X86_FEATURE(BMI2));
    }
    cpu.xcr0 = 0xe6;
    for (uint32_t bit = 26; bit <= 28; bit += 1)
    {
        cpu.words[0][2] = UINT32_MAX ^ (1u << bit);
        const X86Target target = x86_target_from_cpuid(&cpu);
        passed &= (target.features & (BUSTER_X86_FEATURE(AVX) | BUSTER_X86_FEATURE(AVX2) | BUSTER_X86_FEATURE(FMA) | BUSTER_X86_FEATURE(AVX512F) | BUSTER_X86_FEATURE(XSAVE) | BUSTER_X86_FEATURE(XSAVEC) | BUSTER_X86_FEATURE(XSAVEOPT) | BUSTER_X86_FEATURE(XSAVES))) == 0;
    }
    cpu = (X86CpuId){0};
    passed &= x86_target_from_cpuid(&cpu).features == 0;
    cpu.words[6][0] = 1u << 20;
    passed &= x86_target_from_cpuid(&cpu).features == BUSTER_X86_FEATURE(PREFETCHI);
    cpu.words[6][0] = 0;
    cpu.words[2][3] = 1u << 14;
    passed &= x86_target_from_cpuid(&cpu).features == BUSTER_X86_FEATURE(PREFETCHI);
    X86Target unchanged = { .features = 123 };
    passed &= !x86_target_parse("x86-64-v5", 9, &unchanged) && unchanged.features == 123;
    passed &= !x86_target_parse("", 0, &unchanged) && unchanged.features == 123;
    passed &= !x86_target_parse("znver5-extra", 12, &unchanged) && unchanged.features == 123;
    passed &= x86_target_parse("znver5-extra", 6, &unchanged) && unchanged.architecture == X86_ARCH_ZNVER5;
    const X86Target live = x86_target_native();
    printf("target live native: features=%#llx, znver5=%u (CPUID with OS-enabled XCR0 state)\n", (unsigned long long)live.features, live.native_znver5);
    printf("target CPUID, XCR0 and bounded names: %s\n", passed ? "pass" : "FAIL");
    return passed;
}

static bool x86_target_test_forms(void)
{
    static const char* const profiles[] = { "x86-64", "x86-64-v2", "x86-64-v3", "x86-64-v4", "znver5", "native" };
    static const struct
    {
        uint16_t form;
        uint8_t first;
    } cases[] = {
        { X86_FORM_ADD_RM64_R64, 0 },
        { X86_FORM_ADDSD_X_XM64, 0 },
        { X86_FORM_CMPSS_X_XM32_IMM8, 0 },
        { X86_FORM_CMPSD_X_XM64_IMM8, 0 },
        { X86_FORM_MOVD_X_RM32, 0 },
        { X86_FORM_MOVQ_X_RM64, 0 },
        { X86_FORM_XCHG_RM64_R64, 0 },
        { X86_FORM_LOCK_XADD_M64_R64, 0 },
        { X86_FORM_LOCK_BTC_M64_R64, 0 },
        { X86_FORM_FLD_M80, 0 },
        { X86_FORM_FSTP_M80, 0 },
        { X86_FORM_FISTP_M64, 0 },
        { X86_FORM_FUCOMIP_ST0_ST, 0 },
        { X86_FORM_FCMOVNU_ST0_ST, 0 },
        { X86_FORM_FISTTP_M16, 1 },
        { X86_FORM_FISTTP_M32, 1 },
        { X86_FORM_FISTTP_M64, 1 },
        { X86_FORM_HADDPS_X_XM128, 1 },
        { X86_FORM_PABSB_X_XM128, 1 },
        { X86_FORM_PCMPISTRI_X_XM128_IMM8, 1 },
        { X86_FORM_FNSTCW_M16, 0 },
        { X86_FORM_STMXCSR_M32, 0 },
        { X86_FORM_LOCK_CMPXCHG_M8_R8, 0 },
        { X86_FORM_LOCK_CMPXCHG_M16_R16, 0 },
        { X86_FORM_LOCK_CMPXCHG_M32_R32, 0 },
        { X86_FORM_LOCK_CMPXCHG_M64_R64, 0 },
        { X86_FORM_POPCNT_R64_RM64, 1 },
        { X86_FORM_LAHF, 1 },
        { X86_FORM_PSHUFB_X_XM128, 1 },
        { X86_FORM_ROUNDSD_X_XM64_IMM8, 1 },
        { X86_FORM_CMPXCHG16B_M128, 1 },
        { X86_FORM_LZCNT_R64_RM64, 2 },
        { X86_FORM_TZCNT_R64_RM64, 2 },
        { X86_FORM_ANDN_R64_R64_RM64, 2 },
        { X86_FORM_SHLX_R64_RM64_R64, 2 },
        { X86_FORM_VADDSS_X_X_XM32, 2 },
        { X86_FORM_VMOVSS_X_X_X, 2 },
        { X86_FORM_VCVTTSD2SI_R64_XM64, 2 },
        { X86_FORM_VFMSUB132SS_X_X_XM32, 2 },
        { X86_FORM_VFNMSUB231PD_Y_Y_YM256, 2 },
        { X86_FORM_VMULSS_X_X_XM32_EVEX, 3 },
        { X86_FORM_VFMADDSUB132PS_Z_Z_ZM512, 3 },
        { X86_FORM_VPADDD_Y_Y_YM256, 2 },
        { X86_FORM_KMOVW_K_K, 3 },
        { X86_FORM_KMOVB_K_K, 3 },
        { X86_FORM_KMOVD_K_K, 3 },
        { X86_FORM_KMOVQ_K_K, 3 },
        { X86_FORM_AESENC_X_XM128, 4 }, // v4 does not imply the optional crypto extension.
        { X86_FORM_PUSHP_R64, 255 }, // APX is not implied by AVX-512.
        { X86_FORM_ANDN_R64_R64_RM64_EVEX, 255 },
        { X86_FORM_NONE, 255 },
        { X86_FORM_FEMMS, 255 }, // Obsolete 3DNow! is not part of a supported profile.
    };
    uint8_t scalar[X86_FORM_COUNT];
    uint8_t vector[X86_FORM_COUNT];
    uint64_t random = 180;
    bool passed = x86_encoder_initialize();
    uint32_t accepted = 0;
    for (uint32_t p = 0; p < sizeof(profiles) / sizeof(profiles[0]); p += 1)
    {
        X86Target target;
        passed &= x86_target_parse(profiles[p], strlen(profiles[p]), &target);
        x86_target_forms_scalar(&target, scalar);
        x86_target_forms(&target, vector);
        passed &= memcmp(scalar, vector, sizeof(scalar)) == 0;
        const X86Instruction rounds_control =
        {
            .rm = X86_REGISTER_RSP,
            .mode = BUSTER_X86_MEMORY_BASE,
            .form = X86_FORM_FNSTCW_M16,
        };
        // The baseline control-word read stays available at all profiles, including detected native.
        passed &= scalar[X86_FORM_FNSTCW_M16] != 0 && vector[X86_FORM_FNSTCW_M16] != 0;
        passed &= x86_target_validate(vector, &rounds_control, 1) == 1;
        const X86Instruction sse_control =
        {
            .rm = X86_REGISTER_RSP,
            .mode = BUSTER_X86_MEMORY_BASE,
            .form = X86_FORM_STMXCSR_M32,
        };
        passed &= scalar[X86_FORM_STMXCSR_M32] != 0 && vector[X86_FORM_STMXCSR_M32] != 0;
        passed &= x86_target_validate(vector, &sse_control, 1) == 1;
        const uint16_t scalar_cas[] = { X86_FORM_LOCK_CMPXCHG_M8_R8, X86_FORM_LOCK_CMPXCHG_M16_R16, X86_FORM_LOCK_CMPXCHG_M32_R32, X86_FORM_LOCK_CMPXCHG_M64_R64 };
        for (uint32_t c = 0; c < sizeof(scalar_cas) / sizeof(scalar_cas[0]); c += 1)
        {
            const X86Instruction compare =
            {
                .reg = X86_REGISTER_RDX,
                .rm = X86_REGISTER_RDI,
                .mode = BUSTER_X86_MEMORY_BASE,
                .form = scalar_cas[c],
            };
            passed &= scalar[scalar_cas[c]] != 0 && vector[scalar_cas[c]] != 0;
            passed &= x86_target_validate(vector, &compare, 1) == 1;
        }
        for (uint32_t c = 0; p < 5 && c < sizeof(cases) / sizeof(cases[0]); c += 1)
        {
            if ((scalar[cases[c].form] != 0) != (p >= cases[c].first))
            {
                printf("target %s: unexpected eligibility for form %u\n", profiles[p], cases[c].form);
                passed = false;
            }
        }
        for (uint32_t form = 0; form <= UINT16_MAX; form += 1)
        {
            const uint32_t number = form & BUSTER_X86_FORM_NUMBER;
            const bool expected = number < BUSTER_X86_FORM_TEMPLATES && scalar[number % X86_FORM_COUNT] != 0;
            passed &= x86_target_form_allowed(vector, form) == expected;
        }
        for (uint32_t form = 1; form < X86_FORM_COUNT; form += 1)
        {
            if (scalar[form])
            {
                X86Instruction instruction = x86_random_instruction(&random, (uint16_t)form, false);
                passed &= x86_target_validate(vector, &instruction, 1) == 1;
                instruction.form |= BUSTER_X86_FORM_FIXUP;
                passed &= x86_target_validate(vector, &instruction, 1) == 1;
                accepted += 1;
            }
        }
        passed &= !x86_target_form_allowed(vector, UINT32_MAX);
        passed &= x86_target_validate(vector, 0, 0) == 0;
    }
    // Individual feature controls catch mistakes concealed by the cumulative v4 profile.
    X86Target partial = { .features = BUSTER_X86_FEATURE(AVX512F) };
    x86_target_forms(&partial, vector);
    passed &= vector[X86_FORM_KMOVW_K_K] && !vector[X86_FORM_KMOVB_K_K] && !vector[X86_FORM_KMOVQ_K_K];
    partial.features |= BUSTER_X86_FEATURE(AVX512DQ);
    x86_target_forms(&partial, vector);
    passed &= vector[X86_FORM_KMOVB_K_K] && !vector[X86_FORM_KMOVD_K_K];
    passed &= vector[X86_FORM_VADDSS_X_X_XM32_EVEX] && !vector[X86_FORM_VADDPS_X_X_XM128_EVEX];
    partial.features = BUSTER_X86_FEATURE(BMI);
    x86_target_forms(&partial, vector);
    passed &= vector[X86_FORM_ANDN_R64_R64_RM64] && !vector[X86_FORM_BZHI_R64_RM64_R64];
    partial.features = BUSTER_X86_FEATURE(BMI2);
    x86_target_forms(&partial, vector);
    passed &= !vector[X86_FORM_ANDN_R64_R64_RM64] && vector[X86_FORM_BZHI_R64_RM64_R64];
    partial.features = BUSTER_X86_FEATURE(AVX);
    x86_target_forms(&partial, vector);
    passed &= vector[X86_FORM_VPADDD_X_X_XM128] && !vector[X86_FORM_VPADDD_Y_Y_YM256];
    const X86Instruction invalid[] = {
        { .form = X86_FORM_NOP },
        { .form = X86_FORM_MOV_R64_RM64, .reg = 255 },
    };
    passed &= x86_target_validate(vector, invalid, 2) == 1;
    printf("target forms: %s (%u accepted random records, all 65536 form values at six targets)\n", passed ? "pass" : "FAIL", accepted);
    return passed;
}

static bool x86_target_test_pages(void)
{
    const size_t page = (size_t)sysconf(_SC_PAGESIZE);
    const size_t bytes = ((X86_FORM_COUNT + page - 1) / page) * page;
    uint8_t* mapping = mmap(0, bytes + 2 * page, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    bool passed = mapping != MAP_FAILED;
    if (passed)
    {
        passed = mprotect(mapping + page, bytes, PROT_READ | PROT_WRITE) == 0;
        if (passed)
        {
            X86Target target;
            passed &= x86_target_parse("x86-64-v4", 9, &target);
            uint8_t expected[X86_FORM_COUNT];
            x86_target_forms_scalar(&target, expected);
            uint8_t* begin = mapping + page;
            uint8_t* end = begin + bytes;
            x86_target_forms(&target, begin);
            passed &= memcmp(begin, expected, sizeof(expected)) == 0;
            x86_target_forms(&target, end - X86_FORM_COUNT);
            passed &= memcmp(end - X86_FORM_COUNT, expected, sizeof(expected)) == 0;
            const X86Instruction instruction = { .form = X86_FORM_NOP };
            for (uint32_t n = 0; n <= 32; n += 1)
            {
                X86Instruction* records = (X86Instruction*)(end - n * sizeof(instruction));
                for (uint32_t i = 0; i < n; i += 1)
                {
                    records[i] = instruction;
                }
                passed &= x86_target_validate(expected, records, n) == n;
            }
        }
        munmap(mapping, bytes + 2 * page);
    }
    printf("target table and record page edges: %s\n", passed ? "pass" : "FAIL");
    return passed;
}

static bool x86_target_test_preprocessor(const char* directory)
{
    static const char* const profiles[] = { "x86-64", "x86-64-v2", "x86-64-v3", "x86-64-v4", "znver5", "native" };
    bool passed = pp_test_predefines(directory);
    for (uint32_t p = 0; p < sizeof(profiles) / sizeof(profiles[0]); p += 1)
    {
        X86Target target;
        passed &= x86_target_parse(profiles[p], strlen(profiles[p]), &target);
        char source[4096];
        char expected[4096];
        char options[128];
        uint32_t source_length = 0;
        uint32_t expected_length = 0;
        for (uint32_t i = 0; i < sizeof(x86_target_builtins) / sizeof(x86_target_builtins[0]); i += 1)
        {
            const char* name = x86_target_builtins[i].name;
            source_length += (uint32_t)sprintf(source + source_length, "__has_builtin(%s)\n", name);
            expected_length += (uint32_t)sprintf(expected + expected_length, "%d\n", x86_target_has_builtin(&target, (const uint8_t*)name, (uint32_t)strlen(name)));
        }
        snprintf(options, sizeof(options), "-march=%s", profiles[p]);
        const PPTestCase test = {
            .name = profiles[p],
            .source = source,
            .standard = "gnu17",
            .expected = expected,
            .options = options,
        };
        passed &= pp_test_case(&test, directory, true);
    }
    const PPTestCase order = {
        .name = "last march and independent tune",
        .source = "#ifdef __AVX2__\nwrong\n#else\nbaseline\n#endif\n",
        .standard = "gnu17",
        .expected = "baseline\n",
        .options = "-march=x86-64-v4 -mtune=znver5 -march=x86-64",
    };
    passed &= pp_test_case(&order, directory, true);
    // Names clang doesn't know, and an output mode buster doesn't follow (c_predefines.c takes clang's CPUs and -m switches).
    static const char* const invalid[] = { "-march=", "-march=x86-64-v5", "-march=znver9", "-mtune=invalid", "-m32" };
    for (uint32_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i += 1)
    {
        Arena arena = arena_reserve(1ull << 20);
        PPOptions options;
        const char* input;
        const char* output;
        bool markers;
        char* arguments[] = { (char*)invalid[i], "/dev/null" };
        passed &= !pp_parse_options(2, arguments, &options, &arena, &input, &output, &markers);
        arena_release(&arena);
    }
    printf("target builtins and option order against clang: %s\n", passed ? "pass" : "FAIL");
    return passed;
}

static int run_x86_target_tests(void)
{
    bool passed = x86_target_test_cpuid();
    passed &= x86_target_test_forms();
    passed &= x86_target_test_matrix();
    passed &= x86_target_test_fma_masks();
    passed &= x86_target_test_pages();
    passed &= x86_target_test_compiler_macros();
    passed &= run_x86_target_selection_tests();
    char directory[] = "/tmp/buster-target-XXXXXX";
    if (mkdtemp(directory) != 0)
    {
        passed &= x86_target_test_preprocessor(directory);
        passed &= x86_target_test_gas(directory);
        char path[4096];
        snprintf(path, sizeof(path), "%s/case.c", directory);
        unlink(path);
        rmdir(directory);
    }
    else
    {
        passed = false;
    }
    return passed ? 0 : 1;
}

static __attribute__((noinline, aligned(64))) void x86_target_benchmark_scalar(const X86Target* target, uint8_t* allowed)
{
    x86_target_forms_scalar(target, allowed);
}

static __attribute__((noinline, aligned(64))) void x86_target_benchmark_vector(const X86Target* target, uint8_t* allowed)
{
    x86_target_forms(target, allowed);
}

static uint64_t x86_target_clock(void)
{
    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC_RAW, &time);
    return (uint64_t)time.tv_sec * 1000000000 + (uint64_t)time.tv_nsec;
}

static double x86_target_benchmark_median(double values[21])
{
    for (uint32_t i = 1; i < 21; i += 1)
    {
        const double value = values[i];
        uint32_t j = i;
        while (j != 0 && values[j - 1] > value)
        {
            values[j] = values[j - 1];
            j -= 1;
        }
        values[j] = value;
    }
    return values[10];
}

static int run_x86_target_benchmark(int count, char** arguments)
{
    int only = -1;
    uint32_t after_predefines = 0;
    uint32_t iterations = 10000;
    bool valid = true;
    for (int i = 0; i < count && valid; i += 1)
    {
        if (strcmp(arguments[i], "--only") == 0 && i + 1 < count)
        {
            i += 1;
            only = strcmp(arguments[i], "scalar") == 0 ? 0 : strcmp(arguments[i], "avx512") == 0 ? 1 : -1;
            valid = only >= 0;
        }
        else if ((strcmp(arguments[i], "--after-predefines") == 0 || strcmp(arguments[i], "--iterations") == 0) && i + 1 < count)
        {
            const bool conditioning = strcmp(arguments[i], "--after-predefines") == 0;
            char* end;
            i += 1;
            const unsigned long value = strtoul(arguments[i], &end, 10);
            valid = end != arguments[i] && *end == 0 && value != 0 && value <= (conditioning ? 300 : 10000000);
            if (conditioning)
            {
                after_predefines = (uint32_t)value;
            }
            else
            {
                iterations = (uint32_t)value;
            }
        }
        else
        {
            valid = false;
        }
    }
    X86Target target;
    const bool parsed = x86_target_parse("znver5", 6, &target);
    uint8_t scalar[X86_FORM_COUNT];
    uint8_t vector[X86_FORM_COUNT];
    x86_target_forms_scalar(&target, scalar);
    x86_target_forms(&target, vector);
    const bool passed = valid && parsed && memcmp(scalar, vector, sizeof(scalar)) == 0;
    double times[2][21];
    double cycles[2][21];
    struct perf_event_attr event = {0};
    event.size = sizeof(event);
    event.type = PERF_TYPE_HARDWARE;
    event.config = PERF_COUNT_HW_CPU_CYCLES;
    event.disabled = 1;
    event.exclude_kernel = 1;
    event.exclude_hv = 1;
    event.read_format = PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING;
    const int fd = (int)syscall(SYS_perf_event_open, &event, 0, -1, -1, PERF_FLAG_FD_CLOEXEC);
    bool counters = fd >= 0;
    char predefines[BUSTER_X86_TARGET_PREDEFINES_BYTES];
    for (uint32_t pass = 0; passed && pass < 21; pass += 1)
    {
        for (uint32_t order = 0; order < 2; order += 1)
        {
            const uint32_t variant = (order + pass) & 1;
            if (only >= 0 && variant != (uint32_t)only)
            {
                continue;
            }
            if (after_predefines != 0)
            {
                const uint64_t until = x86_target_clock() + (uint64_t)after_predefines * 1000000;
                do
                {
                    x86_target_predefines(&target, predefines);
                    __asm__ volatile("" : : "r"(predefines) : "memory");
                } while (x86_target_clock() < until);
            }
            uint64_t before[3] = {0};
            if (counters)
            {
                counters = read(fd, before, sizeof(before)) == sizeof(before) && ioctl(fd, PERF_EVENT_IOC_ENABLE, 0) == 0;
            }
            const uint64_t start = x86_target_clock();
            if (variant == 0)
            {
                for (uint32_t i = 0; i < iterations; i += 1)
                {
                    x86_target_benchmark_scalar(&target, scalar);
                    __asm__ volatile("" : : "r"(scalar) : "memory");
                }
            }
            else
            {
                for (uint32_t i = 0; i < iterations; i += 1)
                {
                    x86_target_benchmark_vector(&target, vector);
                    __asm__ volatile("" : : "r"(vector) : "memory");
                }
            }
            times[variant][pass] = (double)(x86_target_clock() - start) / iterations;
            uint64_t readout[3] = {0};
            if (counters)
            {
                counters = ioctl(fd, PERF_EVENT_IOC_DISABLE, 0) == 0 && read(fd, readout, sizeof(readout)) == sizeof(readout) && readout[2] > before[2];
            }
            cycles[variant][pass] = counters ? (double)(readout[0] - before[0]) * (readout[1] - before[1]) / (readout[2] - before[2]) / iterations : 0;
        }
    }
    for (uint32_t variant = 0; passed && variant < 2; variant += 1)
    {
        if (only < 0 || variant == (uint32_t)only)
        {
            printf("target form table (%u rows), %s: %.2f ns/table, %.2f cycles/table; 21 medians, %u tables/sample, preceding predefines %u ms\n", X86_FORM_COUNT, variant == 0 ? "scalar source" : "AVX-512", x86_target_benchmark_median(times[variant]), counters ? x86_target_benchmark_median(cycles[variant]) : 0, iterations, after_predefines);
        }
    }
    if (!counters)
    {
        printf("target benchmark: hardware cycles unavailable\n");
    }
    if (fd >= 0)
    {
        close(fd);
    }
    if (!valid)
    {
        fprintf(stderr, "usage: benchmark-x86-target [--only scalar|avx512] [--after-predefines MS] [--iterations N]\n");
    }
    return passed && memcmp(scalar, vector, sizeof(scalar)) == 0 ? 0 : 1;
}
