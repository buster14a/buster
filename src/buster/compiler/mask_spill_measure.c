// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// measure-mask-spills (#179): what spilling an AVX-512 opmask register costs, to memory against to a general register,
// the choice #179 leaves to a measurement. Include after x86_zen5_measure.c, whose counters and median it takes, and
// x86_target.c, whose x86_target_native tells whether the host runs the programs.
//
// Each program is a loop of BUSTER_MASK_SPILL_STEPS steps an iteration. A step adds k0 (1) to one of its chains' masks
// with kadd of the width, and first, by the program's strategy, spills and reloads that mask: nothing (the control),
// kmov to its stack slot and back, or kmov to its general register and back. The chains are one (the step's cost is a
// round trip's latency, the spill's and the reload's with the kadd's) or seven, k1 to k7, independent (throughput: as
// a compiler spills a mask under pressure or saves masks around a call, with work between the store and the reload).
// The mix adds a vpaddd of a ZMM register to each step, four independent chains of them, as vector code has around its
// masks: kmov k, r takes a vector pipe, kmov k, m a load and a pipe, kmov r, k and kmov m, k none of the vector ones
// that the adds need (docs/x86-64-encoder.md's tables, which #179's design quotes). Every width: kmovb and kaddb
// (AVX512DQ), kmovw (AVX512F) with kaddw (DQ), kmovd and kmovq with kaddd and kaddq (AVX512BW).
//
// A program is `void program(uint64_t iterations, uint64_t output[7])`: it leaves each mask's final value in its
// output word, which test-mask-spills checks against the chains' arithmetic (mask_spill_expected). The measurement
// reports core cycles per step, the median of --passes interleaved passes, the programs of a width and chain count in
// rotating order; requires the approved 9700X unless --any-cpu, which labels the results with the CPU it ran on.

#define BUSTER_MASK_SPILL_STEPS 84
#define BUSTER_MASK_SPILL_CODE_SIZE 16384
#define BUSTER_MASK_SPILL_RECORDS 512
#define BUSTER_MASK_SPILL_CHAINS 7
#define BUSTER_MASK_SPILL_VECTORS 4
#define BUSTER_MASK_SPILL_MAX_PASSES 31
#define BUSTER_MASK_SPILL_MAX_ITERATIONS 10000000
// The frame: a slot per chain, 8 bytes each, rsp kept 16-byte aligned after the six saved registers.
#define BUSTER_MASK_SPILL_FRAME 72
_Static_assert(BUSTER_MASK_SPILL_STEPS % BUSTER_MASK_SPILL_CHAINS == 0 && BUSTER_MASK_SPILL_STEPS % BUSTER_MASK_SPILL_VECTORS == 0, "every chain takes the same steps");
_Static_assert(8 * BUSTER_MASK_SPILL_CHAINS <= BUSTER_MASK_SPILL_FRAME, "a slot per chain");
// The most records a program has: six saves, the frame, the counter, two a mask, the vector registers' zeros, up to 63
// no-ops before the loop, four a step, the loop's two, the masks' stores, vzeroupper, the frame, six restores, ret.
_Static_assert(6 + 2 + 2 * (BUSTER_MASK_SPILL_CHAINS + 1) + BUSTER_MASK_SPILL_VECTORS + 1 + 63 + 4 * BUSTER_MASK_SPILL_STEPS + 2 + BUSTER_MASK_SPILL_CHAINS + 2 + 6 + 1 <= BUSTER_MASK_SPILL_RECORDS,
               "every program's records fit");

typedef enum MaskSpillStrategy
{
    MASK_SPILL_NONE,
    MASK_SPILL_MEMORY,
    MASK_SPILL_GENERAL,
    MASK_SPILL_STRATEGIES,
} MaskSpillStrategy;

typedef struct MaskSpillShape MaskSpillShape;
struct MaskSpillShape
{
    // 1, 2, 4 or 8 bytes.
    uint8_t width;
    // 1 or BUSTER_MASK_SPILL_CHAINS.
    uint8_t chains;
    bool mix;
    MaskSpillStrategy strategy;
};

typedef struct MaskSpillForms MaskSpillForms;
struct MaskSpillForms
{
    uint16_t store;
    uint16_t load;
    uint16_t to_general;
    uint16_t from_general;
    uint16_t add;
};

static const MaskSpillForms mask_spill_forms_table[4] = {
    { X86_FORM_KMOVB_M8_K, X86_FORM_KMOVB_K_M8, X86_FORM_KMOVB_R32_K, X86_FORM_KMOVB_K_R32, X86_FORM_KADDB_K_K_K },
    { X86_FORM_KMOVW_M16_K, X86_FORM_KMOVW_K_M16, X86_FORM_KMOVW_R32_K, X86_FORM_KMOVW_K_R32, X86_FORM_KADDW_K_K_K },
    { X86_FORM_KMOVD_M32_K, X86_FORM_KMOVD_K_M32, X86_FORM_KMOVD_R32_K, X86_FORM_KMOVD_K_R32, X86_FORM_KADDD_K_K_K },
    { X86_FORM_KMOVQ_M64_K, X86_FORM_KMOVQ_K_M64, X86_FORM_KMOVQ_R64_K, X86_FORM_KMOVQ_K_R64, X86_FORM_KADDQ_K_K_K },
};

// The general register chain c (0 to 6) spills to: caller-saved ones the program doesn't otherwise use.
static const uint8_t mask_spill_general[BUSTER_MASK_SPILL_CHAINS] = {
    X86_REGISTER_RAX,
    X86_REGISTER_RCX,
    X86_REGISTER_RDX,
    X86_REGISTER_R8,
    X86_REGISTER_R9,
    X86_REGISTER_R10,
    X86_REGISTER_R11,
};

static const MaskSpillForms* mask_spill_forms(uint32_t width)
{
    return &mask_spill_forms_table[width == 8 ? 3 : width == 4 ? 2 : width == 2 ? 1 : 0];
}

// Mask k (1 to 7) after `iterations` of a program of `shape`: its start, k, plus the steps of its chain, at the width.
static uint64_t mask_spill_expected(const MaskSpillShape* shape, uint32_t k, uint64_t iterations)
{
    const uint64_t steps = k <= shape->chains ? (uint64_t)(BUSTER_MASK_SPILL_STEPS / shape->chains) * iterations : 0;
    const uint64_t every = shape->width == 8 ? UINT64_MAX : (UINT64_C(1) << (8 * shape->width)) - 1;
    return (k + steps) & every;
}

// The program's records into `code` (BUSTER_MASK_SPILL_CODE_SIZE bytes, writable), encoded by both encoders into the
// same bytes; its body's instruction count (the loop's, without its dec and jne) in *body.
static bool mask_spill_encode(const MaskSpillShape* shape, uint8_t* code, uint32_t* length, uint32_t* body)
{
    const uint8_t saved[] = { X86_REGISTER_RBX, X86_REGISTER_RBP, X86_REGISTER_R12, X86_REGISTER_R13, X86_REGISTER_R14, X86_REGISTER_R15 };
    const MaskSpillForms* forms = mask_spill_forms(shape->width);
    X86Instruction records[BUSTER_MASK_SPILL_RECORDS];
    uint32_t offsets[BUSTER_MASK_SPILL_RECORDS];
    uint32_t count = 0;
    for (uint32_t i = 0; i < sizeof(saved); i += 1)
    {
        records[count++] = (X86Instruction){ .form = X86_FORM_PUSH_R64, .rm = saved[i] };
    }
    records[count++] = (X86Instruction){ .form = X86_FORM_SUB_RM64_IMM8, .rm = X86_REGISTER_RSP, .immediate = BUSTER_MASK_SPILL_FRAME };
    records[count++] = (X86Instruction){ .form = X86_FORM_MOV_R64_RM64, .reg = X86_REGISTER_R12, .rm = X86_REGISTER_RDI };
    // k0 = 1, the chains' step; k1 to k7 start at their numbers; the vector chains and their addend at zero.
    for (uint32_t k = 0; k <= BUSTER_MASK_SPILL_CHAINS; k += 1)
    {
        records[count++] = (X86Instruction){ .form = X86_FORM_MOV_R64_IMM64, .rm = X86_REGISTER_RAX, .immediate = k == 0 ? 1 : (int32_t)k };
        records[count++] = (X86Instruction){ .form = X86_FORM_KMOVQ_K_R64, .reg = (uint8_t)k, .rm = X86_REGISTER_RAX };
    }
    for (uint32_t v = 0; shape->mix && v <= BUSTER_MASK_SPILL_VECTORS; v += 1)
    {
        const uint8_t zmm = v == BUSTER_MASK_SPILL_VECTORS ? 15 : (uint8_t)(v + 1);
        records[count++] = (X86Instruction){ .form = X86_FORM_VPXORD_Z_Z_ZM512, .reg = zmm, .vvvv = zmm, .rm = zmm };
    }
    // The loop starts on a 64-byte boundary, so that the programs' loops are placed alike.
    uint8_t prefix[512];
    const int64_t prefix_length = x86_encode_scalar(records, count, prefix, sizeof(prefix), 0, 0);
    bool valid = prefix_length > 0;
    for (uint32_t i = 0; valid && i < (64 - (uint64_t)prefix_length % 64) % 64; i += 1)
    {
        records[count++] = (X86Instruction){ .form = X86_FORM_NOP };
    }
    const uint32_t loop = count;
    for (uint32_t step = 0; valid && step < BUSTER_MASK_SPILL_STEPS; step += 1)
    {
        const uint32_t chain = step % shape->chains;
        const uint8_t k = (uint8_t)(chain + 1);
        if (shape->strategy == MASK_SPILL_MEMORY)
        {
            records[count++] = (X86Instruction){ .form = forms->store, .reg = k, .rm = X86_REGISTER_RSP, .mode = BUSTER_X86_MEMORY_BASE, .displacement = (int32_t)(8 * chain) };
            records[count++] = (X86Instruction){ .form = forms->load, .reg = k, .rm = X86_REGISTER_RSP, .mode = BUSTER_X86_MEMORY_BASE, .displacement = (int32_t)(8 * chain) };
        }
        else if (shape->strategy == MASK_SPILL_GENERAL)
        {
            records[count++] = (X86Instruction){ .form = forms->to_general, .reg = mask_spill_general[chain], .rm = k };
            records[count++] = (X86Instruction){ .form = forms->from_general, .reg = k, .rm = mask_spill_general[chain] };
        }
        records[count++] = (X86Instruction){ .form = forms->add, .reg = k, .vvvv = k, .rm = 0 };
        if (shape->mix)
        {
            const uint8_t zmm = (uint8_t)(step % BUSTER_MASK_SPILL_VECTORS + 1);
            records[count++] = (X86Instruction){ .form = X86_FORM_VPADDD_Z_Z_ZM512, .reg = zmm, .vvvv = zmm, .rm = 15 };
        }
    }
    *body = count - loop;
    records[count++] = (X86Instruction){ .form = X86_FORM_DEC_RM64, .rm = X86_REGISTER_R12 };
    const uint32_t branch = count;
    records[count++] = (X86Instruction){ .form = X86_FORM_JNE_REL32 };
    for (uint32_t k = 1; k <= BUSTER_MASK_SPILL_CHAINS; k += 1)
    {
        records[count++] = (X86Instruction){ .form = X86_FORM_KMOVQ_M64_K, .reg = (uint8_t)k, .rm = X86_REGISTER_RSI, .mode = BUSTER_X86_MEMORY_BASE, .displacement = (int32_t)(8 * (k - 1)) };
    }
    records[count++] = (X86Instruction){ .form = X86_FORM_VZEROUPPER };
    records[count++] = (X86Instruction){ .form = X86_FORM_ADD_RM64_IMM8, .rm = X86_REGISTER_RSP, .immediate = BUSTER_MASK_SPILL_FRAME };
    for (uint32_t i = sizeof(saved); i > 0; i -= 1)
    {
        records[count++] = (X86Instruction){ .form = X86_FORM_POP_R64, .rm = saved[i - 1] };
    }
    records[count++] = (X86Instruction){ .form = X86_FORM_RET };
    valid = valid && count <= BUSTER_MASK_SPILL_RECORDS && x86_validate(records, count) == count;
    int64_t encoded = valid ? x86_encode_scalar(records, count, code, BUSTER_MASK_SPILL_CODE_SIZE, offsets, 0) : -1;
    valid = valid && encoded > 0 && offsets[loop] % 64 == 0;
    if (valid)
    {
        // The loop's branch, back to its first step, which the first encoding measured.
        records[branch].displacement = (int32_t)offsets[loop] - (int32_t)offsets[branch + 1];
        encoded = x86_encode_scalar(records, count, code, BUSTER_MASK_SPILL_CODE_SIZE, offsets, 0);
        uint8_t vector[BUSTER_MASK_SPILL_CODE_SIZE];
        const int64_t vector_length = x86_encode(records, count, vector, sizeof(vector), 0, 0);
        valid = encoded > 0 && vector_length == encoded && memcmp(vector, code, (size_t)encoded) == 0;
    }
    *length = valid ? (uint32_t)encoded : 0;
    return valid;
}

// The program of `shape` in an executable mapping of its own (program->code, BUSTER_MASK_SPILL_CODE_SIZE bytes), or
// false with nothing mapped.
static bool mask_spill_program(const MaskSpillShape* shape, X86CostProgram* program)
{
    memset(program, 0, sizeof(*program));
    uint8_t* code = mmap(0, BUSTER_MASK_SPILL_CODE_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    bool valid = code != MAP_FAILED;
    uint32_t length = 0;
    uint32_t body = 0;
    valid = valid && mask_spill_encode(shape, code, &length, &body);
    valid = valid && mprotect(code, BUSTER_MASK_SPILL_CODE_SIZE, PROT_READ | PROT_EXEC) == 0;
    if (valid)
    {
        program->code = code;
        program->length = length;
        program->body_count = body;
        program->hash = UINT64_C(14695981039346656037);
        for (uint32_t i = 0; i < length; i += 1)
        {
            program->hash = (program->hash ^ code[i]) * UINT64_C(1099511628211);
        }
    }
    else if (code != MAP_FAILED)
    {
        munmap(code, BUSTER_MASK_SPILL_CODE_SIZE);
    }
    return valid;
}

// JIT execution is an external ABI boundary, as x86_zen5_execute's: generated code has no function-type metadata.
static __attribute__((no_sanitize("function"))) void mask_spill_execute(const X86CostProgram* program, uint64_t iterations, uint64_t* output)
{
    ((void (*)(uint64_t, uint64_t*))program->code)(iterations, output);
}

// One sample of the counters around a run, checked as x86_zen5_sample checks its: the retired instructions must be the
// loop's (and less than 4,096 more), every counter running the whole time.
static bool mask_spill_sample(const X86CostProgram* program, uint64_t iterations, const X86CostCounters* counters, X86CostSample* sample)
{
    uint64_t output[BUSTER_MASK_SPILL_CHAINS] = { 0 };
    bool valid = ioctl(counters->fd[0], PERF_EVENT_IOC_RESET, PERF_IOC_FLAG_GROUP) == 0;
    valid &= ioctl(counters->fd[0], PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP) == 0;
    if (valid)
    {
        mask_spill_execute(program, iterations, output);
    }
    valid &= ioctl(counters->fd[0], PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP) == 0;
    valid &= read(counters->fd[0], sample, sizeof(*sample)) == sizeof(*sample);
    valid &= sample->count == 3 && sample->running != 0 && sample->enabled == sample->running;
    const uint64_t expected = iterations * (program->body_count + 2);
    valid &= sample->values[1] >= expected && sample->values[1] - expected < 4096 && sample->values[0] != 0;
    return valid;
}

static const char* mask_spill_strategy_name(MaskSpillStrategy strategy)
{
    return strategy == MASK_SPILL_MEMORY ? "memory" : strategy == MASK_SPILL_GENERAL ? "general" : "none";
}

static int run_mask_spill_measure(int argc, char** argv)
{
    uint64_t iterations = 20000;
    uint64_t passes = 21;
    bool any_cpu = false;
    bool valid = true;
    for (int i = 0; i < argc && valid; i += 1)
    {
        if (strcmp(argv[i], "--iterations") == 0 && i + 1 < argc)
        {
            valid = x86_zen5_number(argv[++i], 1, BUSTER_MASK_SPILL_MAX_ITERATIONS, &iterations);
        }
        else if (strcmp(argv[i], "--passes") == 0 && i + 1 < argc)
        {
            valid = x86_zen5_number(argv[++i], 3, BUSTER_MASK_SPILL_MAX_PASSES, &passes) && (passes & 1) != 0;
        }
        else if (strcmp(argv[i], "--any-cpu") == 0)
        {
            any_cpu = true;
        }
        else
        {
            valid = false;
        }
    }
    uint32_t eax, ebx, ecx, edx;
    __cpuid(1, eax, ebx, ecx, edx);
    const uint32_t signature = eax;
    char brand[49] = { 0 };
    for (uint32_t i = 0; i < 3; i += 1)
    {
        uint32_t words[4];
        __cpuid(0x80000002 + i, words[0], words[1], words[2], words[3]);
        memcpy(brand + 16 * i, words, 16);
    }
    const bool approved = signature == 0xb40f40 && strstr(brand, "AMD Ryzen 7 9700X") != 0;
    const X86Target host = x86_target_native();
    const uint64_t needed = BUSTER_X86_FEATURE(AVX512F) | BUSTER_X86_FEATURE(AVX512BW) | BUSTER_X86_FEATURE(AVX512DQ);
    if (valid && !approved && !any_cpu)
    {
        fprintf(stderr, "measure-mask-spills requires the approved 9700X (or --any-cpu), got %s (CPUID %08x)\n", brand, signature);
        valid = false;
    }
    if (valid && (host.features & needed) != needed)
    {
        fprintf(stderr, "measure-mask-spills needs AVX512F, AVX512BW and AVX512DQ\n");
        valid = false;
    }
    X86CostCounters counters = { .fd = { -1, -1, -1 } };
    valid = valid && x86_encoder_initialize() && x86_zen5_counters(&counters);
    if (valid)
    {
        printf("# CPU=%s CPUID=%08x approved=%s compiler=%s iterations=%lu passes=%lu steps=%u\n", brand, signature, approved ? "yes" : "no", __clang_version__, iterations, passes,
               BUSTER_MASK_SPILL_STEPS);
        printf("# core cycles per step, the median of the passes; a step is a kadd of its chain's mask, after its spill and reload by the strategy\n");
        printf("width\tchains\tmix\tstrategy\tcycles\tmin\tmax\textra\tcode_hash\n");
    }
    const uint8_t widths[] = { 1, 2, 4, 8 };
    const uint8_t chain_counts[] = { 1, BUSTER_MASK_SPILL_CHAINS };
    for (uint32_t w = 0; valid && w < BUSTER_ARRAY_LENGTH(widths); w += 1)
    {
        for (uint32_t c = 0; valid && c < BUSTER_ARRAY_LENGTH(chain_counts); c += 1)
        {
            for (uint32_t mix = 0; valid && mix < 2; mix += 1)
            {
                X86CostProgram programs[MASK_SPILL_STRATEGIES] = { 0 };
                uint64_t cycles[MASK_SPILL_STRATEGIES][BUSTER_MASK_SPILL_MAX_PASSES] = { 0 };
                for (uint32_t s = 0; valid && s < MASK_SPILL_STRATEGIES; s += 1)
                {
                    const MaskSpillShape shape = { .width = widths[w], .chains = chain_counts[c], .mix = mix != 0, .strategy = (MaskSpillStrategy)s };
                    valid = mask_spill_program(&shape, &programs[s]);
                    if (valid)
                    {
                        uint64_t output[BUSTER_MASK_SPILL_CHAINS] = { 0 };
                        mask_spill_execute(&programs[s], iterations, output);
                    }
                }
                for (uint32_t pass = 0; valid && pass < passes; pass += 1)
                {
                    for (uint32_t j = 0; valid && j < MASK_SPILL_STRATEGIES; j += 1)
                    {
                        const uint32_t s = (pass + j) % MASK_SPILL_STRATEGIES;
                        X86CostSample sample = { 0 };
                        valid = mask_spill_sample(&programs[s], iterations, &counters, &sample);
                        cycles[s][pass] = sample.values[0];
                    }
                }
                const double denominator = (double)iterations * BUSTER_MASK_SPILL_STEPS;
                uint64_t control = 0;
                for (uint32_t s = 0; valid && s < MASK_SPILL_STRATEGIES; s += 1)
                {
                    const uint64_t median = x86_zen5_median(cycles[s], (uint32_t)passes);
                    control = s == MASK_SPILL_NONE ? median : control;
                    printf("%u\t%u\t%s\t%s\t%.4f\t%.4f\t%.4f\t%.4f\t%016lx\n", widths[w], chain_counts[c], mix != 0 ? "vector" : "none", mask_spill_strategy_name((MaskSpillStrategy)s),
                           median / denominator, cycles[s][0] / denominator, cycles[s][passes - 1] / denominator, ((double)median - (double)control) / denominator, programs[s].hash);
                }
                for (uint32_t s = 0; s < MASK_SPILL_STRATEGIES; s += 1)
                {
                    if (programs[s].code != 0)
                    {
                        munmap(programs[s].code, BUSTER_MASK_SPILL_CODE_SIZE);
                    }
                }
            }
        }
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
        fprintf(stderr, "measure-mask-spills failed: check options, CPU, perf permissions and pinned counters; no missing counter is treated as zero\n");
    }
    return valid ? 0 : 1;
}
