// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Policy-owned checks included by x86_target_test.c. Enumerate every subset of every distinct requirement mask,
// every single-feature removal from each profile, and unknown rows even with every feature bit set. The independent
// named-form expectations and external compiler/assembler checks establish ISA facts; the matrix checks the kernels.

static bool x86_target_test_matrix(void)
{
    uint8_t scalar[X86_FORM_COUNT];
    uint8_t vector[X86_FORM_COUNT];
    uint64_t masks[X86_FORM_COUNT];
    uint32_t mask_count = 0;
    uint32_t configurations = 0;
    uint32_t known = 0;
    const uint64_t features = (UINT64_C(1) << X86_FEATURE_COUNT) - 1;
    bool passed = true;
    uint8_t indexed[X86_FORM_COUNT] = {0};
    passed &= x86_target_requirement(X86_FORM_COUNT) == 0;
    passed &= x86_target_requirement(UINT32_MAX) == 0;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(x86_target_classified_forms); i += 1)
    {
        const uint16_t form = x86_target_classified_forms[i];
        passed &= form < X86_FORM_COUNT;
        if (form < X86_FORM_COUNT)
        {
            passed &= indexed[form] == 0 && x86_target_requirement(form) != 0;
            passed &= x86_target_requirement_slots[form] == i + 1;
            passed &= x86_target_classified_requirements[i] == x86_target_requirement(form);
            indexed[form] = 1;
        }
    }
    for (uint32_t form = 0; form < X86_FORM_COUNT; form += 1)
    {
        const uint64_t row = x86_target_requirement(form);
        const uint32_t compact = x86_target_compact_requirements[form];
        const uint64_t restored = (compact & BUSTER_X86_TARGET_COMPACT_LOW) | (uint64_t)((compact >> (X86_FEATURE_AES + 1)) & 1) << X86_FEATURE_PCLMUL | (compact & BUSTER_X86_TARGET_COMPACT_KNOWN ? BUSTER_X86_TARGET_KNOWN : 0);
        passed &= row == restored;
        passed &= indexed[form] == (row != 0);
        passed &= (x86_target_requirement_slots[form] != 0) == (row != 0);
        passed &= x86_target_requirement_slots[form] <= BUSTER_X86_TARGET_CLASSIFIED_COUNT;
        passed &= row == 0 || (row & ~(features | BUSTER_X86_TARGET_KNOWN)) == 0;
        passed &= row == 0 || (row & BUSTER_X86_TARGET_KNOWN) != 0;
        known += row != 0;
        const uint64_t required = row & features;
        bool found = false;
        for (uint32_t m = 0; m < mask_count; m += 1)
        {
            found |= masks[m] == required;
        }
        if (!found)
        {
            masks[mask_count++] = required;
        }
    }
    for (uint32_t m = 0; m < mask_count; m += 1)
    {
        const uint64_t required = masks[m];
        uint64_t subset = required;
        do
        {
            const X86Target target = { .features = subset };
            x86_target_forms_scalar(&target, scalar);
            x86_target_forms(&target, vector);
            passed &= memcmp(scalar, vector, sizeof(scalar)) == 0;
            for (uint32_t form = 0; form < X86_FORM_COUNT; form += 1)
            {
                const uint64_t row = x86_target_requirement(form);
                const bool expected = row != 0 && ((row & features) & ~subset) == 0;
                passed &= (vector[form] != 0) == expected;
            }
            configurations += 1;
            subset = (subset - 1) & required;
        }
        while (subset != required);
    }
    static const char* const profiles[] = { "x86-64", "x86-64-v2", "x86-64-v3", "x86-64-v4", "znver5", "native" };
    // Every raw feature bit alone catches projection aliases, particularly BF16's bit at PCLMUL's compact slot.
    for (uint32_t bit = 0; bit < 64; bit += 1)
    {
        const X86Target target = { .features = UINT64_C(1) << bit };
        x86_target_forms_scalar(&target, scalar);
        x86_target_forms(&target, vector);
        passed &= memcmp(scalar, vector, sizeof(scalar)) == 0;
        configurations += 1;
    }
    uint64_t random = 180;
    for (uint32_t p = 0; p < BUSTER_ARRAY_LENGTH(profiles); p += 1)
    {
        X86Target target;
        passed &= x86_target_parse(profiles[p], strlen(profiles[p]), &target);
        const uint64_t original = target.features;
        for (uint32_t f = 0; f <= X86_FEATURE_COUNT; f += 1)
        {
            target.features = f == X86_FEATURE_COUNT ? original : original & ~(UINT64_C(1) << f);
            x86_target_forms_scalar(&target, scalar);
            x86_target_forms(&target, vector);
            passed &= memcmp(scalar, vector, sizeof(scalar)) == 0;
            configurations += 1;
        }
        // Reserved target bits, including the table's known marker, must not enable or disable a form.
        // Use the scalar result without those bits as the reference, and a deliberately unaligned output.
        const uint64_t reserved = ~(features | BUSTER_X86_TARGET_KNOWN);
        const uint64_t extra[] = { BUSTER_X86_TARGET_KNOWN, reserved, reserved | BUSTER_X86_TARGET_KNOWN };
        target.features = original;
        x86_target_forms_scalar(&target, scalar);
        for (uint32_t r = 0; r < BUSTER_ARRAY_LENGTH(extra); r += 1)
        {
            uint8_t guarded[X86_FORM_COUNT + 2] __attribute__((aligned(64)));
            memset(guarded, 0xa5, sizeof(guarded));
            target.features = original | extra[r];
            x86_target_forms(&target, guarded + 1);
            passed &= guarded[0] == 0xa5 && guarded[X86_FORM_COUNT + 1] == 0xa5;
            passed &= memcmp(scalar, guarded + 1, sizeof(scalar)) == 0;
            configurations += 1;
        }
        for (uint32_t form = 1; form < X86_FORM_COUNT; form += 1)
        {
            X86Instruction records[4] = {
                { .form = X86_FORM_NOP },
                { .form = X86_FORM_NOP },
                { .form = (uint16_t)form },
                { .form = X86_FORM_NOP },
            };
            if (vector[form])
            {
                records[2] = x86_random_instruction(&random, (uint16_t)form, false);
            }
            passed &= x86_target_validate(vector, records, 4) == (vector[form] ? 4u : 2u);
        }
    }
    // No target, including a malformed one with reserved bits, can admit an unclassified or non-64-bit form.
    const X86Target all = { .features = UINT64_MAX };
    x86_target_forms(&all, vector);
    for (uint32_t form = 0; form < X86_FORM_COUNT; form += 1)
    {
        passed &= (vector[form] != 0) == (x86_target_requirement(form) != 0);
        passed &= form < BUSTER_X86_FORM_LEGACY_MODES || vector[form] == 0;
    }
    union
    {
        X86Target target;
        uint8_t allowed[X86_FORM_COUNT];
    } overlap;
    overlap.target = all;
    x86_target_forms_scalar(&overlap.target, overlap.allowed);
    passed &= memcmp(overlap.allowed, vector, sizeof(vector)) == 0;
    overlap.target = all;
    x86_target_forms(&overlap.target, overlap.allowed);
    passed &= memcmp(overlap.allowed, vector, sizeof(vector)) == 0;
    printf("target feature matrix: %s (%u configurations, %u classified / %u forms, first-failure negatives at six profiles; overlapping target/output)\n", passed ? "pass" : "FAIL", configurations, known, X86_FORM_COUNT);
    return passed;
}

// Independent ISA expectations for every FMA row, including every 132/213/231 order and vector width. Cumulative
// profiles alone cannot distinguish AVX from FMA, or AVX512F from AVX512VL. These facts do not read the policy rows.
static bool x86_target_test_fma_masks(void)
{
    static const struct
    {
        uint16_t form;
        uint8_t kind;
    } cases[] = {
#define BUSTER_X86_TARGET_TEST_FMA(name, ...) { X86_FORM_##name, 0 },
        BUSTER_X86_FMA_PACKED_FORMS(BUSTER_X86_TARGET_TEST_FMA, VFMADD, "vfmadd", 0x98)
        BUSTER_X86_FMA_SCALAR_FORMS(BUSTER_X86_TARGET_TEST_FMA, VFMADD, "vfmadd", 0x98)
        BUSTER_X86_FMA_PACKED_FORMS(BUSTER_X86_TARGET_TEST_FMA, VFMSUB, "vfmsub", 0x9a)
        BUSTER_X86_FMA_SCALAR_FORMS(BUSTER_X86_TARGET_TEST_FMA, VFMSUB, "vfmsub", 0x9a)
        BUSTER_X86_FMA_PACKED_FORMS(BUSTER_X86_TARGET_TEST_FMA, VFNMADD, "vfnmadd", 0x9c)
        BUSTER_X86_FMA_SCALAR_FORMS(BUSTER_X86_TARGET_TEST_FMA, VFNMADD, "vfnmadd", 0x9c)
        BUSTER_X86_FMA_PACKED_FORMS(BUSTER_X86_TARGET_TEST_FMA, VFNMSUB, "vfnmsub", 0x9e)
        BUSTER_X86_FMA_SCALAR_FORMS(BUSTER_X86_TARGET_TEST_FMA, VFNMSUB, "vfnmsub", 0x9e)
        BUSTER_X86_FMA_PACKED_FORMS(BUSTER_X86_TARGET_TEST_FMA, VFMADDSUB, "vfmaddsub", 0x96)
        BUSTER_X86_FMA_PACKED_FORMS(BUSTER_X86_TARGET_TEST_FMA, VFMSUBADD, "vfmsubadd", 0x97)
#undef BUSTER_X86_TARGET_TEST_FMA
#define BUSTER_X86_TARGET_TEST_FMA(name, ...) { X86_FORM_##name, 1 },
        BUSTER_X86_AVX512_FMA_PACKED_FORMS(BUSTER_X86_TARGET_TEST_FMA, VFMADD, "vfmadd", 0x98)
        BUSTER_X86_AVX512_FMA_SCALAR_FORMS(BUSTER_X86_TARGET_TEST_FMA, VFMADD, "vfmadd", 0x98)
        BUSTER_X86_AVX512_FMA_PACKED_FORMS(BUSTER_X86_TARGET_TEST_FMA, VFMSUB, "vfmsub", 0x9a)
        BUSTER_X86_AVX512_FMA_SCALAR_FORMS(BUSTER_X86_TARGET_TEST_FMA, VFMSUB, "vfmsub", 0x9a)
        BUSTER_X86_AVX512_FMA_PACKED_FORMS(BUSTER_X86_TARGET_TEST_FMA, VFNMADD, "vfnmadd", 0x9c)
        BUSTER_X86_AVX512_FMA_SCALAR_FORMS(BUSTER_X86_TARGET_TEST_FMA, VFNMADD, "vfnmadd", 0x9c)
        BUSTER_X86_AVX512_FMA_PACKED_FORMS(BUSTER_X86_TARGET_TEST_FMA, VFNMSUB, "vfnmsub", 0x9e)
        BUSTER_X86_AVX512_FMA_SCALAR_FORMS(BUSTER_X86_TARGET_TEST_FMA, VFNMSUB, "vfnmsub", 0x9e)
        BUSTER_X86_AVX512_FMA_PACKED_FORMS(BUSTER_X86_TARGET_TEST_FMA, VFMADDSUB, "vfmaddsub", 0x96)
        BUSTER_X86_AVX512_FMA_PACKED_FORMS(BUSTER_X86_TARGET_TEST_FMA, VFMSUBADD, "vfmsubadd", 0x97)
#undef BUSTER_X86_TARGET_TEST_FMA
#define BUSTER_X86_TARGET_TEST_FMA(name, ...) { X86_FORM_##name, 2 },
        BUSTER_X86_AVX512VL_FMA_PACKED_FORMS(BUSTER_X86_TARGET_TEST_FMA, VFMADD, "vfmadd", 0x98)
        BUSTER_X86_AVX512VL_FMA_PACKED_FORMS(BUSTER_X86_TARGET_TEST_FMA, VFMSUB, "vfmsub", 0x9a)
        BUSTER_X86_AVX512VL_FMA_PACKED_FORMS(BUSTER_X86_TARGET_TEST_FMA, VFNMADD, "vfnmadd", 0x9c)
        BUSTER_X86_AVX512VL_FMA_PACKED_FORMS(BUSTER_X86_TARGET_TEST_FMA, VFNMSUB, "vfnmsub", 0x9e)
        BUSTER_X86_AVX512VL_FMA_PACKED_FORMS(BUSTER_X86_TARGET_TEST_FMA, VFMADDSUB, "vfmaddsub", 0x96)
        BUSTER_X86_AVX512VL_FMA_PACKED_FORMS(BUSTER_X86_TARGET_TEST_FMA, VFMSUBADD, "vfmsubadd", 0x97)
#undef BUSTER_X86_TARGET_TEST_FMA
    };
    const uint64_t required[] = {
        BUSTER_X86_FEATURE(AVX) | BUSTER_X86_FEATURE(FMA),
        BUSTER_X86_FEATURE(AVX512F),
        BUSTER_X86_FEATURE(AVX512F) | BUSTER_X86_FEATURE(AVX512VL),
    };
    bool passed = true;
    uint32_t checks = 0;
    uint8_t allowed[X86_FORM_COUNT];
    for (uint32_t kind = 0; kind < BUSTER_ARRAY_LENGTH(required); kind += 1)
    {
        uint64_t subset = required[kind];
        do
        {
            const X86Target target = { .features = subset };
            x86_target_forms(&target, allowed);
            for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(cases); c += 1)
            {
                if (cases[c].kind == kind)
                {
                    passed &= (allowed[cases[c].form] != 0) == (subset == required[kind]);
                    checks += 1;
                }
            }
            subset = (subset - 1) & required[kind];
        }
        while (subset != required[kind]);
    }
    printf("independent FMA minimal-feature subsets: %s (%u forms, %u checks)\n", passed ? "pass" : "FAIL", (unsigned)BUSTER_ARRAY_LENGTH(cases), checks);
    return passed;
}

static bool x86_target_test_compiler_macros(void)
{
    static const char* const profiles[] = { "x86-64", "x86-64-v2", "x86-64-v3", "x86-64-v4", "znver5", "native" };
    static const char* const compilers[] = { "clang", "gcc" };
    bool passed = true;
    uint32_t checks = 0;
    for (uint32_t p = 0; p < BUSTER_ARRAY_LENGTH(profiles); p += 1)
    {
        char march[64];
        snprintf(march, sizeof(march), "-march=%s", profiles[p]);
        char* arguments[] = { march };
        X86Target target;
        passed &= x86_target_parse(profiles[p], strlen(profiles[p]), &target);
        for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(compilers); c += 1)
        {
            PPClangOutput output = pp_test_clang_process(compilers[c], arguments, 1, "/dev/null", true);
            passed &= output.status == PP_CLANG_ACCEPTED;
            for (uint32_t f = 0; output.status == PP_CLANG_ACCEPTED && f < X86_FEATURE_COUNT; f += 1)
            {
                const char* macro = x86_features[f].macro;
                if (macro != 0)
                {
                    char line[128];
                    snprintf(line, sizeof(line), "#define %s 1\n", macro);
                    bool expected = (target.features & (UINT64_C(1) << f)) != 0;
                    // GCC has no INVPCID/RDPRU feature macros. Its named znver5 profile also omits SHSTK,
                    // while its native resolver reports the actual 9700X shadow-stack capability.
                    if (c == 1 && (f == X86_FEATURE_INVPCID || f == X86_FEATURE_RDPRU || (f == X86_FEATURE_SHSTK && target.architecture == X86_ARCH_ZNVER5)))
                    {
                        expected = false;
                    }
                    const bool present = strstr(output.text, line) != 0;
                    if (present != expected)
                    {
                        fprintf(stderr, "target macro: %s %s %s: expected %u, got %u\n", compilers[c], march, macro, expected, present);
                    }
                    passed &= present == expected;
                    checks += 1;
                }
            }
            free(output.text);
            free(output.diagnostics);
        }
    }
    printf("target feature macros against Clang/GCC, including real native CPUID: %s (%u checks)\n", passed ? "pass" : "FAIL", checks);
    return passed;
}

// GAS's architecture gate is independent of the policy rows. Every classified form gets valid operands from the
// encoder's printer, on one source line. A Zen 5 control must first assemble every line, proving its operands and
// spelling with zero refusals. Restricted CPUs use different error messages for unavailable encodings, operands,
// masks and vector operations. With identical instruction-only input and only -march changing, an error placed on
// an instruction is an architecture-dependent refusal after the control. Every other diagnostic still fails.
static bool x86_target_test_gas(const char* directory)
{
    // GAS 2.47's dual FMA/AVX512F templates admit these explicit EVEX forms without AVX512VL. Keep the
    // architectural policy strict: the independent minimal-feature tests above require F and VL for every row.
    // Count this external-oracle gap only where GAS accepts it; a corrected assembler follows the normal check.
    static const uint8_t gas_vl_fma[X86_FORM_COUNT] = {
#define BUSTER_X86_TARGET_GAS_VL(name, ...) [X86_FORM_##name] = 1,
        BUSTER_X86_AVX512VL_FMA_PACKED_FORMS(BUSTER_X86_TARGET_GAS_VL, VFMADD, "vfmadd", 0x98)
        BUSTER_X86_AVX512VL_FMA_PACKED_FORMS(BUSTER_X86_TARGET_GAS_VL, VFMSUB, "vfmsub", 0x9a)
        BUSTER_X86_AVX512VL_FMA_PACKED_FORMS(BUSTER_X86_TARGET_GAS_VL, VFNMADD, "vfnmadd", 0x9c)
        BUSTER_X86_AVX512VL_FMA_PACKED_FORMS(BUSTER_X86_TARGET_GAS_VL, VFNMSUB, "vfnmsub", 0x9e)
        BUSTER_X86_AVX512VL_FMA_PACKED_FORMS(BUSTER_X86_TARGET_GAS_VL, VFMADDSUB, "vfmaddsub", 0x96)
        BUSTER_X86_AVX512VL_FMA_PACKED_FORMS(BUSTER_X86_TARGET_GAS_VL, VFMSUBADD, "vfmsubadd", 0x97)
#undef BUSTER_X86_TARGET_GAS_VL
    };
    static const struct
    {
        const char* profile;
        const char* cpu;
        uint64_t disabled;
    } configurations[] = {
        { "znver5", "znver5", 0 }, // Syntax/operand control, before any negative architecture case.
        { "x86-64", "generic64", 0 },
        // GAS tracks FISTTP separately from SSE3, without an extension option. Nocona supplies both; remove MONITOR.
        { "x86-64-v2", "nocona+nomonitor+sse3+ssse3+sse4.1+sse4.2+popcnt+cx16+lahf_sahf", 0 },
        { "x86-64-v3", "nocona+nomonitor+sse3+ssse3+sse4.1+sse4.2+popcnt+cx16+lahf_sahf+avx+avx2+bmi+bmi2+f16c+fma+lzcnt+movbe+xsave", 0 },
        { "x86-64-v4", "nocona+nomonitor+sse3+ssse3+sse4.1+sse4.2+popcnt+cx16+lahf_sahf+avx+avx2+bmi+bmi2+f16c+fma+lzcnt+movbe+xsave+avx512f+avx512bw+avx512cd+avx512dq+avx512vl", 0 },
        { "znver5", "znver5+nofma", BUSTER_X86_FEATURE(FMA) },
        { "znver5", "znver5+noavx512vl", BUSTER_X86_FEATURE(AVX512VL) },
        { "znver5", "znver5+noavx512bw", BUSTER_X86_FEATURE(AVX512BW) },
        { "znver5", "znver5+noavx512dq", BUSTER_X86_FEATURE(AVX512DQ) },
        { "znver5", "znver5+nobmi2", BUSTER_X86_FEATURE(BMI2) },
        { "znver5", "znver5+nopopcnt", BUSTER_X86_FEATURE(POPCNT) },
    };
    char source[4096];
    snprintf(source, sizeof(source), "%s/forms.s", directory);
    FILE* file = fopen(source, "w");
    bool passed = file != 0;
    uint16_t forms[X86_FORM_COUNT];
    uint32_t count = 0;
    uint64_t random = 180;
    if (file != 0)
    {
        fputs(".intel_syntax noprefix\n.text\n", file);
        for (uint32_t form = 1; form < X86_FORM_COUNT; form += 1)
        {
            if (x86_target_requirement(form) != 0)
            {
                X86Instruction instruction = x86_random_instruction(&random, (uint16_t)form, true);
                // This oracle checks canonical forms. Optional FS/GS prefixes have no separate ISA requirement,
                // and GAS warns when one has no effect (LEA). Flagged records are covered by the raw-form tests.
                instruction.form = (uint16_t)form;
                // The printer spells absolute memory with DS even without a form override. LEA ignores that
                // segment too; use a RIP-relative operand with the same ISA requirement for the control.
                if ((form == X86_FORM_LEA_R16_M || form == X86_FORM_LEA_R32_M || form == X86_FORM_LEA_R64_M) && instruction.mode == BUSTER_X86_MEMORY_ABSOLUTE)
                {
                    instruction.mode = BUSTER_X86_MEMORY_RIP;
                }
                passed &= x86_validate(&instruction, 1) == 1;
                x86_print_instruction(file, &instruction, 0);
                fputc('\n', file);
                forms[count++] = (uint16_t)form;
            }
        }
        passed &= ferror(file) == 0;
        passed &= fclose(file) == 0;
    }
    uint64_t checks = 0;
    uint32_t gaps = 0;
    for (uint32_t c = 0; passed && c < BUSTER_ARRAY_LENGTH(configurations); c += 1)
    {
        char march[512];
        snprintf(march, sizeof(march), "-march=%s", configurations[c].cpu);
        FILE* errors = tmpfile();
        fflush(0);
        const pid_t child = errors != 0 ? fork() : -1;
        if (child == 0)
        {
            if (setenv("LC_ALL", "C", 1) != 0 || dup2(fileno(errors), STDERR_FILENO) < 0)
            {
                _exit(126);
            }
            execlp("as", "as", "--64", march, "-o", "/dev/null", source, (char*)0);
            _exit(127);
        }
        int status = 0;
        pid_t waited;
        do
        {
            waited = child > 0 ? waitpid(child, &status, 0) : -1;
        }
        while (child > 0 && waited < 0 && errno == EINTR);
        char* diagnostics = 0;
        size_t length = 0;
        bool ready = child > 0 && waited == child && WIFEXITED(status) && WEXITSTATUS(status) <= 1 && errors != 0 && pp_test_read_output(errors, &diagnostics, &length);
        uint8_t rejected[X86_FORM_COUNT] = {0};
        uint32_t refusals = 0;
        char* cursor = diagnostics;
        while (ready && cursor != 0 && *cursor != 0)
        {
            char* end = strchr(cursor, '\n');
            if (end != 0)
            {
                *end = 0;
            }
            if (strstr(cursor, "Error:") != 0)
            {
                const size_t prefix = strlen(source);
                char* number = strncmp(cursor, source, prefix) == 0 && cursor[prefix] == ':' ? cursor + prefix + 1 : 0;
                char* after = 0;
                const unsigned long line = number != 0 ? strtoul(number, &after, 10) : 0;
                const bool placed = line >= 3 && line - 3 < count && after != number && after != 0 && *after == ':';
                // GAS also uses its ordinary error category for an internal assembler failure.
                const bool internal = placed && strncmp(after, ": Error: internal error", sizeof(": Error: internal error") - 1) == 0;
                const bool refusal = placed && c != 0 && !internal && strncmp(after, ": Error: ", sizeof(": Error: ") - 1) == 0;
                ready &= refusal;
                if (refusal)
                {
                    rejected[line - 3] = 1;
                    refusals += 1;
                }
                else
                {
                    fprintf(stderr, "target GAS %s: unexpected diagnostic: %s\n", march, cursor);
                }
            }
            else if (*cursor != 0)
            {
                const size_t prefix = strlen(source);
                const bool header = strncmp(cursor, source, prefix) == 0 && strcmp(cursor + prefix, ": Assembler messages:") == 0;
                if (!header)
                {
                    fprintf(stderr, "target GAS %s: unexpected diagnostic: %s\n", march, cursor);
                    ready = false;
                }
            }
            cursor = end != 0 ? end + 1 : 0;
        }
        ready &= WEXITSTATUS(status) == (refusals != 0 ? 1 : 0);
        ready &= c != 0 || refusals == 0;
        X86Target target = {0};
        ready &= x86_target_parse(configurations[c].profile, strlen(configurations[c].profile), &target);
        target.features &= ~configurations[c].disabled;
        uint8_t allowed[X86_FORM_COUNT];
        x86_target_forms(&target, allowed);
        for (uint32_t i = 0; ready && i < count; i += 1)
        {
            const bool gap = configurations[c].disabled == BUSTER_X86_FEATURE(AVX512VL) && gas_vl_fma[forms[i]] && !rejected[i];
            if (gap)
            {
                passed &= allowed[forms[i]] == 0;
                gaps += 1;
            }
            else if ((allowed[forms[i]] != 0) == (rejected[i] != 0))
            {
                fprintf(stderr, "target GAS %s: form %u (%s), policy %u, oracle %u\n", march, forms[i], x86_form_descriptions[forms[i]].mnemonic, allowed[forms[i]], !rejected[i]);
                passed = false;
            }
            checks += !gap;
        }
        passed &= ready;
        if (errors != 0)
        {
            fclose(errors);
        }
        free(diagnostics);
    }
    unlink(source);
    printf("target forms against GAS architecture gate: %s (%llu comparisons, %u explicit GAS AVX512VL/FMA gaps; independent feature removals)\n", passed ? "pass" : "FAIL", (unsigned long long)checks, gaps);
    return passed;
}

// Compare cached admission with the direct, uncached predicate for every row. This also catches a future
// syntax predicate gaining an input that was omitted from the cache key, including false-positive admission.
static bool x86_target_test_matcher_admission(void)
{
    Arena arena = arena_reserve(1ull << 20);
    X86MatchIndex* index = BUSTER_ARENA_PUSH_ARRAY(&arena, X86MatchIndex, 1);
    bool passed = index != 0 && x86_semantics_initialize();
    static const char* const profiles[] = { "x86-64", "x86-64-v2", "x86-64-v3", "x86-64-v4", "znver5", "native" };
    uint32_t configurations = 0;
    uint8_t allowed[X86_FORM_COUNT];
    uint8_t seen[X86_FORM_COUNT];
    for (uint32_t p = 0; index != 0 && p <= BUSTER_ARRAY_LENGTH(profiles); p += 1)
    {
        X86Target target = { .features = UINT64_MAX };
        if (p < BUSTER_ARRAY_LENGTH(profiles))
        {
            passed &= x86_target_parse(profiles[p], strlen(profiles[p]), &target);
        }
        const uint64_t original = target.features;
        for (uint32_t bit = 0; bit <= X86_FEATURE_COUNT; bit += 1)
        {
            target.features = bit == X86_FEATURE_COUNT ? original : original & ~(UINT64_C(1) << bit);
            x86_target_forms_scalar(&target, allowed);
            x86_match_initialize(index, &target);
            memset(seen, 0, sizeof(seen));
            const uint32_t count = index->begin[BUSTER_X86_MATCH_BUCKETS];
            passed &= count <= X86_FORM_COUNT;
            for (uint32_t at = 0; at < count && at < X86_FORM_COUNT; at += 1)
            {
                const uint32_t form = index->forms[at];
                passed &= form < X86_FORM_COUNT;
                if (form < X86_FORM_COUNT)
                {
                    passed &= seen[form] == 0;
                    seen[form] = 1;
                }
            }
            for (uint32_t form = 0; form < X86_FORM_COUNT; form += 1)
            {
                uint8_t roles[4];
                const X86FormDescription* row = &x86_form_descriptions[form];
                const bool expected = form < BUSTER_X86_FORM_LEGACY_MODES && allowed[form] && x86_match_supported(row) && x86_match_roles(row->syntax, roles) <= 4;
                passed &= seen[form] == expected;
            }
            configurations += 1;
        }
    }
    printf("target matcher admission: %s (%u configurations, every form against uncached syntax and scalar ISA)\n", passed ? "pass" : "FAIL", configurations);
    arena_release(&arena);
    return passed;
}
