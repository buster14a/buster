// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// test-mask-spills (#179): measure-mask-spills' programs, built for every width, chain count, mix and strategy. Each
// must encode by both encoders into the same bytes and pass x86_validate, have its loop's instructions as the step's
// recipe counts them (a kadd, the spill's two kmovs, the mix's vpaddd), differ from every other program, and, where the
// host has AVX512F, BW and DQ, leave each mask's value as the chains' arithmetic has it (mask_spill_expected) after
// 1, 3 and 1,000 iterations: a spill or a reload of the wrong width or register shows in the masks' values. Include
// after mask_spill_measure.c.

typedef struct MaskSpillTest MaskSpillTest;
struct MaskSpillTest
{
    uint64_t checks;
    uint64_t failures;
    uint64_t programs;
    uint64_t runs;
};

static void mask_spill_test_expect(MaskSpillTest* test, bool condition, const char* message)
{
    test->checks += 1;
    if (!condition)
    {
        test->failures += 1;
        if (test->failures <= 20)
        {
            printf("test-mask-spills: %s\n", message);
        }
    }
}

static int run_mask_spill_tests(void)
{
    MaskSpillTest test = { 0 };
    const bool initialized = x86_encoder_initialize();
    mask_spill_test_expect(&test, initialized, "the encoder initializes");
    const X86Target host = x86_target_native();
    const uint64_t needed = BUSTER_X86_FEATURE(AVX512F) | BUSTER_X86_FEATURE(AVX512BW) | BUSTER_X86_FEATURE(AVX512DQ);
    const bool native = (host.features & needed) == needed;
    const uint8_t widths[] = { 1, 2, 4, 8 };
    const uint8_t chain_counts[] = { 1, BUSTER_MASK_SPILL_CHAINS };
    const uint64_t iteration_counts[] = { 1, 3, 1000 };
    uint64_t hashes[BUSTER_ARRAY_LENGTH(widths) * BUSTER_ARRAY_LENGTH(chain_counts) * 2 * MASK_SPILL_STRATEGIES] = { 0 };
    uint32_t hashed = 0;
    for (uint32_t w = 0; initialized && w < BUSTER_ARRAY_LENGTH(widths); w += 1)
    {
        for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(chain_counts); c += 1)
        {
            for (uint32_t mix = 0; mix < 2; mix += 1)
            {
                for (uint32_t s = 0; s < MASK_SPILL_STRATEGIES; s += 1)
                {
                    const MaskSpillShape shape = { .width = widths[w], .chains = chain_counts[c], .mix = mix != 0, .strategy = (MaskSpillStrategy)s };
                    X86CostProgram program;
                    const bool built = mask_spill_program(&shape, &program);
                    mask_spill_test_expect(&test, built, "a program encodes by both encoders into the same bytes and validates");
                    const uint32_t per_step = 1 + (s != MASK_SPILL_NONE ? 2 : 0) + (mix != 0 ? 1 : 0);
                    mask_spill_test_expect(&test, !built || program.body_count == BUSTER_MASK_SPILL_STEPS * per_step, "a loop's instructions are its steps'");
                    bool distinct = true;
                    for (uint32_t h = 0; built && h < hashed; h += 1)
                    {
                        distinct &= hashes[h] != program.hash;
                    }
                    mask_spill_test_expect(&test, distinct, "every program differs from the others");
                    if (built)
                    {
                        hashes[hashed] = program.hash;
                        hashed += 1;
                        test.programs += 1;
                    }
                    for (uint32_t r = 0; built && native && r < BUSTER_ARRAY_LENGTH(iteration_counts); r += 1)
                    {
                        uint64_t output[BUSTER_MASK_SPILL_CHAINS];
                        memset(output, 0xa5, sizeof(output));
                        mask_spill_execute(&program, iteration_counts[r], output);
                        bool same = true;
                        for (uint32_t k = 1; k <= BUSTER_MASK_SPILL_CHAINS; k += 1)
                        {
                            same &= output[k - 1] == mask_spill_expected(&shape, k, iteration_counts[r]);
                        }
                        if (!same && test.failures < 20)
                        {
                            printf("test-mask-spills: width %u, %u chains, mix %u, %s, %lu iterations: k1 = %016lx, expected %016lx\n", widths[w], chain_counts[c], mix, mask_spill_strategy_name((MaskSpillStrategy)s),
                                   iteration_counts[r], output[0], mask_spill_expected(&shape, 1, iteration_counts[r]));
                        }
                        mask_spill_test_expect(&test, same, "each mask ends at its chain's value");
                        test.runs += 1;
                    }
                    if (built)
                    {
                        munmap(program.code, BUSTER_MASK_SPILL_CODE_SIZE);
                    }
                }
            }
        }
    }
    mask_spill_test_expect(&test, test.programs == BUSTER_ARRAY_LENGTH(hashes), "every shape's program");
    mask_spill_test_expect(&test, !native || test.runs == BUSTER_ARRAY_LENGTH(hashes) * BUSTER_ARRAY_LENGTH(iteration_counts), "every program runs where the host can");
    printf("test-mask-spills: %lu checks, %lu failures; %lu programs, %lu native runs%s\n", test.checks, test.failures, test.programs, test.runs, native ? "" : " (the host lacks AVX512F, BW or DQ: none ran)");
    return test.failures == 0 ? 0 : 1;
}
