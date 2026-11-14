// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #1239's original scalar contraction fixture, compiled through the real driver. The existing program oracle pins
// -ffp-contract=off; this separate matrix exercises the command-line policy and lexical overrides against Clang -O0.
// Every configuration shares one source and oracle across both backends, both readers and object/direct emission.

// Print the first differing source-result line, keeping a matrix failure bounded even though the fixture checks
// hundreds of representations. A status failure's diagnostics are printed separately by the caller.
static void check_test_driver_fp_contract_difference(const char* expected, const char* actual)
{
    const char* left = expected;
    const char* right = actual;
    bool different = false;
    while (!different && *left != 0 && *right != 0)
    {
        const char* left_end = strchr(left, '\n');
        const char* right_end = strchr(right, '\n');
        const size_t left_size = left_end != NULL ? (size_t)(left_end - left) : strlen(left);
        const size_t right_size = right_end != NULL ? (size_t)(right_end - right) : strlen(right);
        different = left_size != right_size || memcmp(left, right, left_size) != 0;
        if (!different)
        {
            left += left_size + (left_end != NULL);
            right += right_size + (right_end != NULL);
        }
    }
    fprintf(stderr, "FP contraction first difference:\n  Clang: %.160s\n  Buster: %.160s\n", left, right);
}

static void check_test_driver_fp_contract(CheckTest* test, const char* clang, const char* driver, const char* directory)
{
    const char* const executable = driver != NULL ? driver : "/proc/self/exe";
    const char* const source = "tests/driver/fp_contract.c";
    const uint64_t fma_features = BUSTER_X86_FEATURE(AVX) | BUSTER_X86_FEATURE(AVX2) | BUSTER_X86_FEATURE(FMA);
    const uint64_t znver5_features = fma_features | BUSTER_X86_FEATURE(AVX512F) | BUSTER_X86_FEATURE(AVX512VL);
    const X86Target native = x86_target_native();
    const struct
    {
        const char* name;
        const char* march;
        const char* first;
        const char* second;
        uint64_t required;
        bool fused;
    } configurations[] = {
        { "x86-64 unfused compatibility", "-march=x86-64", NULL, NULL, 0, false },
        { "x86-64-v2 unfused compatibility", "-march=x86-64-v2", NULL, NULL, 0, false },
        { "x86-64-v3 default", "-march=x86-64-v3", NULL, NULL, fma_features, true },
        { "znver5 default", "-march=znver5", NULL, NULL, znver5_features, true },
        { "off", "-march=x86-64-v3", "-ffp-contract=off", NULL, fma_features, false },
        { "on", "-march=x86-64-v3", "-ffp-contract=on", NULL, fma_features, true },
        // Clang -O0 grants the wider permission with fast, but does not perform its later SSA contraction pass.
        { "fast", "-march=x86-64-v3", "-ffp-contract=fast", NULL, fma_features, false },
        { "fast-honor-pragmas", "-march=x86-64-v3", "-ffp-contract=fast-honor-pragmas", NULL, fma_features, false },
        { "off then on", "-march=x86-64-v3", "-ffp-contract=off", "-ffp-contract=on", fma_features, true },
        { "on then off", "-march=x86-64-v3", "-ffp-contract=on", "-ffp-contract=off", fma_features, false },
        // Scalar EVEX FMA requires AVX512F alone; copying its arguments must not accidentally require AVX512VL.
        { "AVX512F without VL", "-march=x86-64-v3", "-mavx512f", "-mno-avx512vl", fma_features | BUSTER_X86_FEATURE(AVX512F), true },
    };
    const bool available = access(source, R_OK) == 0;
    check_test_expect(test, available, "FP contraction external fixture is available (run from the repository root)");
    char object[512];
    char unfused[512];
    char binary[512];
    char reference[512];
    snprintf(object, sizeof(object), "%s/fp-contract.o", directory);
    snprintf(unfused, sizeof(unfused), "%s/fp-contract-off.o", directory);
    snprintf(binary, sizeof(binary), "%s/fp-contract-buster", directory);
    snprintf(reference, sizeof(reference), "%s/fp-contract-clang", directory);
    for (uint32_t configuration = 0; available && configuration < BUSTER_ARRAY_LENGTH(configurations); configuration += 1)
    {
        const bool run_native = (native.features & configurations[configuration].required) == configurations[configuration].required;
        char* oracle_arguments[16] = { (char*)clang, "-O0", "-std=gnu17", "-w", (char*)configurations[configuration].march };
        uint32_t oracle_count = 5;
        // Without FMA, retain Buster's existing unfused arithmetic, including its exceptional-value signs. Clang's
        // default moves a negation into the product before expanding fmuladd even on these targets, so use its off
        // mode as this exact-bit compatibility control. Also compare Buster's default and off objects below.
        if (configurations[configuration].required == 0)
        {
            oracle_arguments[oracle_count++] = "-ffp-contract=off";
        }
        if (configurations[configuration].first != NULL)
        {
            oracle_arguments[oracle_count++] = (char*)configurations[configuration].first;
        }
        if (configurations[configuration].second != NULL)
        {
            oracle_arguments[oracle_count++] = (char*)configurations[configuration].second;
        }
        oracle_arguments[oracle_count++] = (char*)source;
        oracle_arguments[oracle_count++] = "-lm";
        oracle_arguments[oracle_count++] = "-o";
        oracle_arguments[oracle_count++] = reference;
        char* diagnostic = NULL;
        const int oracle_status = check_test_driver_spawn(oracle_arguments, &diagnostic);
        char description[512];
        snprintf(description, sizeof(description), "FP contraction, %s: Clang -O0 fixture builds", configurations[configuration].name);
        check_test_expect(test, oracle_status == 0, description);
        if (oracle_status != 0 && diagnostic != NULL)
        {
            fprintf(stderr, "%s", diagnostic);
        }
        free(diagnostic);
        diagnostic = NULL;
        char* expected = NULL;
        char* oracle_run[] = { reference, NULL };
        const bool referenced = oracle_status == 0 && (!run_native || check_test_driver_spawn_stream(oracle_run, &expected, true) == 0);
        if (run_native)
        {
            // Independent exact-product control: one rounding produces the exact negative power of two, while the
            // rounded product is 1 and its separate addition produces positive zero.
            const char* first = configurations[configuration].fused ? "shape 0 0 a8800000 b970000000000000 0 0 0\n" :
                                                                     "shape 0 0 00000000 0000000000000000 0 0 0\n";
            const bool exact = referenced && expected != NULL && strncmp(expected, first, strlen(first)) == 0;
            check_test_expect(test, exact, "FP contraction oracle distinguishes one rounding from two by independently known exact bits");
        }
        else
        {
            printf("  FP contraction, %s: native execution skipped (host features); both tiers and output paths still compile and link\n",
                   configurations[configuration].name);
        }
        for (uint32_t route = 0; referenced && route < 8; route += 1)
        {
            const bool direct = (route & 1) != 0;
            const bool scalar = (route & 2) != 0;
            const bool tier1 = (route & 4) != 0;
            char* arguments[28] = { (char*)executable, "-std=gnu17", "-w", (char*)configurations[configuration].march };
            uint32_t count = 4;
            if (configurations[configuration].first != NULL)
            {
                arguments[count++] = (char*)configurations[configuration].first;
            }
            if (configurations[configuration].second != NULL)
            {
                arguments[count++] = (char*)configurations[configuration].second;
            }
            if (scalar)
            {
                arguments[count++] = "--scalar";
                arguments[count++] = "--deferred-bodies";
            }
            arguments[count++] = "--lanes";
            arguments[count++] = scalar ? "3" : "1";
            arguments[count++] = "--check-transitions";
            if (tier1)
            {
                arguments[count++] = "--tier1";
                arguments[count++] = "--tier1-strict";
                arguments[count++] = "--tier1-report";
                arguments[count++] = "--tier1-refusals";
                arguments[count++] = "--verify";
            }
            if (!direct)
            {
                arguments[count++] = "-c";
            }
            arguments[count++] = (char*)source;
            arguments[count++] = "-o";
            arguments[count++] = direct ? binary : object;
            if (direct)
            {
                arguments[count++] = "-lm";
            }
            int status = check_test_driver_spawn(arguments, &diagnostic);
            if (tier1)
            {
                const bool taken = status == 0 && diagnostic != NULL && strstr(diagnostic, "fallbacks=0\t") != NULL &&
                                   strstr(diagnostic, "tier1-refused") == NULL;
                check_test_expect(test, taken, "FP contraction fixture is compiled by Tier 1 with no silent fallback");
            }
            if (status == 0 && !direct && configurations[configuration].required == 0)
            {
                char* control[32] = { 0 };
                memcpy(control, arguments, count * sizeof(*control));
                control[count] = "-ffp-contract=off";
                control[count + 1] = "-o";
                control[count + 2] = unfused;
                char* control_diagnostic = NULL;
                const int control_status = check_test_driver_spawn(control, &control_diagnostic);
                check_test_expect(test, control_status == 0 && check_test_driver_same_files(object, unfused),
                                  "non-FMA target's default and explicit off compile byte-identical objects");
                if (control_status != 0 && control_diagnostic != NULL)
                {
                    fprintf(stderr, "%s", control_diagnostic);
                }
                free(control_diagnostic);
                unlink(unfused);
            }
            if (status == 0 && !direct)
            {
                free(diagnostic);
                diagnostic = NULL;
                char* link[] = { (char*)clang, object, "-lm", "-o", binary, NULL };
                status = check_test_driver_spawn(link, &diagnostic);
            }
            snprintf(description, sizeof(description), "FP contraction, %s, Tier %u, %s reader, %s: builds and links",
                     configurations[configuration].name, tier1 ? 1 : 0, scalar ? "scalar/deferred" : "vector", direct ? "direct" : "object");
            check_test_expect(test, status == 0, description);
            if (status != 0 && diagnostic != NULL)
            {
                fprintf(stderr, "%s", diagnostic);
            }
            free(diagnostic);
            diagnostic = NULL;
            if (status == 0 && run_native)
            {
                char* actual = NULL;
                char* run[] = { binary, NULL };
                const int ran = check_test_driver_spawn_stream(run, &actual, true);
                const bool same = ran == 0 && actual != NULL && expected != NULL && strcmp(actual, expected) == 0;
                snprintf(description, sizeof(description), "FP contraction, %s, Tier %u, %s reader, %s: exact bits, scopes, effects and environment match Clang -O0",
                         configurations[configuration].name, tier1 ? 1 : 0, scalar ? "scalar/deferred" : "vector", direct ? "direct" : "object");
                check_test_expect(test, same, description);
                if (!same && actual != NULL && expected != NULL)
                {
                    check_test_driver_fp_contract_difference(expected, actual);
                }
                free(actual);
            }
            unlink(object);
            unlink(binary);
        }
        free(expected);
        unlink(reference);
    }
}
