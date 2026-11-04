// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// #1239: original expression/barrier fixtures compared bit for bit with Clang -O0. Every target compiles on every
// host; execution is gated by the instructions these scalar fixtures use, and skipped execution is reported.

static void check_test_contract_options(CheckTest* test)
{
    static const struct
    {
        const char* flags[5];
        uint8_t expected;
    } cases[] = {
        { { NULL }, BUSTER_PREDEFINES_CONTRACT_ON },
        { { "-ffp-contract=off", NULL }, BUSTER_PREDEFINES_CONTRACT_OFF },
        { { "-ffp-contract=on", NULL }, BUSTER_PREDEFINES_CONTRACT_ON },
        { { "-ffp-contract=fast", NULL }, BUSTER_PREDEFINES_CONTRACT_FAST },
        { { "-ffp-contract=fast-honor-pragmas", NULL }, BUSTER_PREDEFINES_CONTRACT_FAST_HONOR_PRAGMAS },
        { { "-ffp-contract=off", "-ffast-math", "-fno-fast-math", NULL }, BUSTER_PREDEFINES_CONTRACT_OFF },
        { { "-ffast-math", "-ffp-contract=off", "-fno-fast-math", NULL }, BUSTER_PREDEFINES_CONTRACT_OFF },
        { { "-ffp-contract=on", "-ffast-math", NULL }, BUSTER_PREDEFINES_CONTRACT_FAST },
        { { "-ffp-contract=off", "-funsafe-math-optimizations", "-fno-unsafe-math-optimizations", NULL }, BUSTER_PREDEFINES_CONTRACT_OFF },
        { { "-ffp-contract=off", "-ffp-model=precise", NULL }, BUSTER_PREDEFINES_CONTRACT_ON },
        { { "-ffp-contract=on", "-ffp-model=strict", NULL }, BUSTER_PREDEFINES_CONTRACT_OFF },
        { { "-ffp-contract=on", "-ffp-model=fast", NULL }, BUSTER_PREDEFINES_CONTRACT_FAST },
        { { "-ffp-contract=off", "-Ofast", "-O0", NULL }, BUSTER_PREDEFINES_CONTRACT_OFF },
        { { "-Ofast", "-ffp-contract=off", NULL }, BUSTER_PREDEFINES_CONTRACT_OFF },
    };
    for (uint32_t row = 0; row < BUSTER_ARRAY_LENGTH(cases); row += 1)
    {
        uint8_t floats[8];
        PredefinesOptions options = { .floats = floats, .float_capacity = BUSTER_ARRAY_LENGTH(floats) };
        bool parsed = true;
        for (uint32_t flag = 0; cases[row].flags[flag] != NULL; flag += 1)
        {
            parsed &= predefines_option(&options, cases[row].flags[flag], NULL) == PREDEFINES_PARSE_TAKEN;
        }
        char label[96];
        snprintf(label, sizeof(label), "contraction option ordering %u", row);
        check_test_expect(test, parsed && predefines_fp_contract(&options) == cases[row].expected, label);
    }
}

static bool check_test_contract_command(CheckTest* test, char* const* arguments, const char* label)
{
    char* text = NULL;
    const int status = check_test_driver_spawn(arguments, &text);
    check_test_expect(test, status == 0, label);
    if (status != 0 && text != NULL)
    {
        fprintf(stderr, "%s: %s\n", label, text);
    }
    free(text);
    return status == 0;
}

static void check_test_driver_contract(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_contract_options(test);
    static const char* const targets[] = { "-march=x86-64", "-march=x86-64-v2", "-march=x86-64-v3", "-march=x86-64-v4", "-march=znver5" };
    static const char* const modes[] = { NULL, "-ffp-contract=on", "-ffp-contract=off", "-ffp-contract=fast", "-ffp-contract=fast-honor-pragmas" };
    static const char source[] = "tests/driver/fp-contract.c";
    const uint64_t features = x86_target_native().features;
    char oracle[256];
    char object[256];
    char result[256];
    char saved[256];
    char label[256];
    snprintf(oracle, sizeof(oracle), "%s/contract-clang", directory);
    snprintf(object, sizeof(object), "%s/contract.o", directory);
    snprintf(result, sizeof(result), "%s/contract-result", directory);
    snprintf(saved, sizeof(saved), "%s/contract-on.o", directory);
    for (uint32_t target = 0; executable != NULL && target < BUSTER_ARRAY_LENGTH(targets); target += 1)
    {
        const uint64_t required = target < 2 ? 0 : BUSTER_X86_FEATURE(AVX) | BUSTER_X86_FEATURE(FMA) |
                                                  (target >= 3 ? BUSTER_X86_FEATURE(AVX512F) : 0);
        const bool native = (features & required) == required;
        if (!native)
        {
            printf("fp-contract: %s: compile all paths; native execution skipped (host instruction features)\n", targets[target]);
        }
        for (uint32_t mode = 0; mode < BUSTER_ARRAY_LENGTH(modes); mode += 1)
        {
            char* oracle_arguments[] = { (char*)clang, "-O0", (char*)targets[target], (char*)source, "-o", oracle, (char*)modes[mode], NULL };
            snprintf(label, sizeof(label), "contraction oracle %s %s", targets[target], mode != 0 ? modes[mode] : "default");
            const bool compiled = check_test_contract_command(test, oracle_arguments, label);
            char* expected = NULL;
            char* run_oracle[] = { oracle, NULL };
            const bool observed = compiled && (!native || check_test_driver_spawn_stream(run_oracle, &expected, true) == 0);
            check_test_expect(test, observed, label);
            if (observed && native && target >= 2 && mode <= 1)
            {
                check_test_expect(test, expected != NULL && strncmp(expected, "0 0 a8800000 b970000000000000 0\n", 32) == 0,
                                  "the oracle's cancellation witness distinguishes fused from separately rounded arithmetic");
            }
            for (uint32_t path = 0; observed && path < 4; path += 1)
            {
                const bool direct = (path & 1) != 0;
                const bool scalar = (path & 2) != 0;
                char* arguments[16] = { (char*)executable, "--verify", (char*)targets[target] };
                uint32_t count = 3;
                if (modes[mode] != NULL)
                {
                    arguments[count++] = (char*)modes[mode];
                }
                if (scalar)
                {
                    arguments[count++] = "--scalar";
                }
                if (!direct)
                {
                    arguments[count++] = "-c";
                }
                arguments[count++] = (char*)source;
                arguments[count++] = "-o";
                arguments[count++] = direct ? result : target < 2 && mode == 1 && path == 0 ? saved : object;
                arguments[count] = NULL;
                snprintf(label, sizeof(label), "contraction %s %s %s %s", targets[target], mode != 0 ? modes[mode] : "default",
                         scalar ? "scalar" : "vector", direct ? "direct" : "object");
                bool built = check_test_contract_command(test, arguments, label);
                if (built && target < 2 && mode == 2 && path == 0)
                {
                    check_test_expect(test, check_test_driver_same_files(saved, object), "contraction on/off leaves no-FMA object bytes unchanged");
                }
                if (built && !direct)
                {
                    char* link[] = { (char*)clang, arguments[count - 1], "-o", result, NULL };
                    built = check_test_contract_command(test, link, label);
                }
                if (built && native)
                {
                    char* actual = NULL;
                    char* run[] = { result, NULL };
                    const int status = check_test_driver_spawn_stream(run, &actual, true);
                    const bool equal = status == 0 && actual != NULL && expected != NULL && strcmp(actual, expected) == 0;
                    check_test_expect(test, equal, label);
                    if (!equal && actual != NULL && expected != NULL)
                    {
                        uint32_t byte = 0;
                        while (actual[byte] != 0 && actual[byte] == expected[byte])
                        {
                            byte += 1;
                        }
                        fprintf(stderr, "%s: first difference at byte %u: got %.100s, expected %.100s\n", label, byte, actual + byte, expected + byte);
                    }
                    free(actual);
                }
                unlink(result);
                unlink(object);
            }
            free(expected);
            unlink(oracle);
        }
        unlink(saved);
    }
    if (executable != NULL)
    {
        char* invalid[] = { (char*)executable, "-c", "-ffp-contract=invalid", (char*)source, "-o", object, NULL };
        char* text = NULL;
        const int status = check_test_driver_spawn(invalid, &text);
        check_test_expect(test, status == 1 && text != NULL && strstr(text, "unsupported argument 'invalid' to option '-ffp-contract='") != NULL &&
                               access(object, F_OK) != 0, "invalid contraction mode fails before object publication");
        free(text);
    }
}
