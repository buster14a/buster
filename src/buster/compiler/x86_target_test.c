// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Target policy tests. CPUID inputs are synthetic as well as live; Clang is an independent preprocessor oracle.
// The encoder's random-record generator supplies valid operands, not ISA expectations.

static bool x86_target_test_cpuid(void)
{
    X86CpuId cpu = { .xcr0 = 0xe6, .znver5 = true };
    for (uint32_t i = 0; i < 6; i += 1)
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
        passed &= (target.features & (BUSTER_X86_FEATURE(BMI) | BUSTER_X86_FEATURE(BMI2))) == (BUSTER_X86_FEATURE(BMI) | BUSTER_X86_FEATURE(BMI2));
    }
    cpu.xcr0 = 0xe6;
    for (uint32_t bit = 26; bit <= 28; bit += 1)
    {
        cpu.words[0][2] = UINT32_MAX ^ (1u << bit);
        const X86Target target = x86_target_from_cpuid(&cpu);
        passed &= (target.features & (BUSTER_X86_FEATURE(AVX) | BUSTER_X86_FEATURE(AVX2) | BUSTER_X86_FEATURE(FMA) | BUSTER_X86_FEATURE(AVX512F))) == 0;
    }
    cpu = (X86CpuId){0};
    passed &= x86_target_from_cpuid(&cpu).features == 0;
    X86Target unchanged = { .features = 123 };
    passed &= !x86_target_parse("x86-64-v5", 9, &unchanged) && unchanged.features == 123;
    passed &= !x86_target_parse("", 0, &unchanged) && unchanged.features == 123;
    passed &= !x86_target_parse("znver5-extra", 12, &unchanged) && unchanged.features == 123;
    passed &= x86_target_parse("znver5-extra", 6, &unchanged) && unchanged.architecture == X86_ARCH_ZNVER5;
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
        { X86_FORM_VPADDD_Y_Y_YM256, 2 },
        { X86_FORM_KMOVW_K_K, 3 },
        { X86_FORM_KMOVB_K_K, 3 },
        { X86_FORM_KMOVD_K_K, 3 },
        { X86_FORM_KMOVQ_K_K, 3 },
        { X86_FORM_AESENC_X_XM128, 4 }, // v4 does not imply the optional crypto extension.
        { X86_FORM_PUSHP_R64, 255 }, // APX is not implied by AVX-512.
        { X86_FORM_ANDN_R64_R64_RM64_EVEX, 255 },
        { X86_FORM_NONE, 255 },
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
    static const char* const invalid[] = { "-march=", "-march=x86-64-v5", "-march=znver6", "-mtune=invalid", "-mavx2" };
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
    passed &= x86_target_test_pages();
    char directory[] = "/tmp/buster-target-XXXXXX";
    if (mkdtemp(directory) != 0)
    {
        passed &= x86_target_test_preprocessor(directory);
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

static int run_x86_target_benchmark(void)
{
    X86Target target;
    const bool parsed = x86_target_parse("znver5", 6, &target);
    uint8_t scalar[X86_FORM_COUNT];
    uint8_t vector[X86_FORM_COUNT];
    x86_target_forms_scalar(&target, scalar);
    x86_target_forms(&target, vector);
    const bool passed = parsed && memcmp(scalar, vector, sizeof(scalar)) == 0;
    uint64_t times[2][21];
    const uint32_t iterations = 10000;
    for (uint32_t pass = 0; passed && pass < 21; pass += 1)
    {
        for (uint32_t order = 0; order < 2; order += 1)
        {
            const uint32_t variant = (order + pass) & 1;
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
            times[variant][pass] = x86_target_clock() - start;
        }
    }
    for (uint32_t variant = 0; passed && variant < 2; variant += 1)
    {
        for (uint32_t i = 1; i < 21; i += 1)
        {
            const uint64_t value = times[variant][i];
            uint32_t j = i;
            while (j != 0 && times[variant][j - 1] > value)
            {
                times[variant][j] = times[variant][j - 1];
                j -= 1;
            }
            times[variant][j] = value;
        }
        printf("target form table (%u rows), %s: %.2f ns/table, 21 interleaved medians\n", X86_FORM_COUNT, variant == 0 ? "scalar source" : "AVX-512", (double)times[variant][10] / iterations);
    }
    return passed && memcmp(scalar, vector, sizeof(scalar)) == 0 ? 0 : 1;
}
