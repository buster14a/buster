// Copyright (c) 2026 David Gonzalez Martin
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Original instruction microbenchmarks for #181. Encoder records generate bounded W^X mappings. Twelve independent
// chains, matched six-chain controls, two dependency paths, and mixes with six ADD/IMUL chains measure throughput
// and possible contention; the twelve chains again with a never-taken jo after each instruction measure whether the
// pair fuses (one retired macro-op), as a flag-setting instruction and a conditional branch may, for the cases whose
// OF the fixture keeps clear (x86_zen5_branch_case). A mix can be dependency-limited; compare it with the matched
// six-chain observations.
// PMCx0C1 is RETIRED MACRO-OPS, not executed micro-ops. Neither that counter nor a mixed result identifies a pipe.
// Only measure-x86-zen5 requires a 9700X; construction/encoding tests never label another CPU's results as Zen 5.

#include <cpuid.h>
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>

#define BUSTER_X86_COST_BLOCK 240
#define BUSTER_X86_COST_CODE_SIZE 32768
#define BUSTER_X86_COST_RECORDS 640
#define BUSTER_X86_COST_MAX_PASSES 31
// The most loop iterations a call runs (`--iterations`); each call sets its registers again.
#define BUSTER_X86_COST_MAX_ITERATIONS 10000000
_Static_assert(2 * BUSTER_X86_COST_BLOCK + 96 <= BUSTER_X86_COST_RECORDS, "records include the largest body, alignment padding and saved-register wrapper");
_Static_assert(BUSTER_X86_COST_BLOCK % 12 == 0, "source chains remain connected across the loop back edge");

typedef enum X86CostExperiment
{
    X86_COST_INDEPENDENT,
    X86_COST_INDEPENDENT_SIX,
    X86_COST_CHAIN_DESTINATION,
    X86_COST_CHAIN_SOURCE,
    X86_COST_MIX_ADD,
    X86_COST_MIX_IMUL,
    X86_COST_BRANCH,
    X86_COST_EXPERIMENT_COUNT,
} X86CostExperiment;

typedef struct X86CostProgram X86CostProgram;
struct X86CostProgram
{
    uint8_t* code;
    uint32_t length;
    uint32_t body_count;
    uint64_t hash;
};

static X86Instruction x86_zen5_fixture(uint8_t id, uint8_t destination, uint8_t source, bool source_path)
{
    const X86CostCase* entry = &x86_zen5_cases[id];
    const X86FormDescription* form = &x86_form_descriptions[entry->form];
    X86Instruction instruction = { .form = entry->form, .immediate = entry->immediate };
    if (entry->operands == X86_COST_L1_LOAD)
    {
        instruction.reg = destination;
        instruction.rm = source;
        instruction.mode = BUSTER_X86_MEMORY_BASE;
    }
    else if (entry->operands == X86_COST_LEA_SIMPLE || entry->operands == X86_COST_LEA_SCALED)
    {
        instruction.reg = destination;
        instruction.rm = source_path ? X86_REGISTER_R14 : destination;
        instruction.index = source_path ? source : X86_REGISTER_R14;
        instruction.mode = BUSTER_X86_MEMORY_BASE_INDEX(entry->operands == X86_COST_LEA_SCALED ? 3 : 0);
        instruction.displacement = entry->operands == X86_COST_LEA_SCALED ? 4660 : 0;
    }
    else if (form->syntax == X86_SYNTAX_REG_RM || form->syntax == X86_SYNTAX_REG_RM_IMM)
    {
        instruction.reg = destination;
        instruction.rm = form->immediate_bytes != 0 && !source_path ? destination : source;
    }
    else
    {
        instruction.rm = destination;
        instruction.reg = form->reg != BUSTER_X86_OPERAND_NONE ? source : 0;
        if (entry->operands == X86_COST_ZERO)
        {
            instruction.reg = destination;
        }
    }
    return instruction;
}

static bool x86_zen5_source_path(uint8_t id)
{
    const X86CostCase* entry = &x86_zen5_cases[id];
    const X86FormDescription* form = &x86_form_descriptions[entry->form];
    return entry->operands != X86_COST_ZERO && (form->reg != BUSTER_X86_OPERAND_NONE || entry->operands == X86_COST_L1_LOAD);
}

// The most a register's chain moves in a call of the twelve chains: a step per BUSTER_X86_COST_BLOCK / 12 instructions
// an iteration, from 3, its value at the call's start.
#define BUSTER_X86_COST_CHAIN_STEPS ((uint64_t)(BUSTER_X86_COST_BLOCK / 12) * BUSTER_X86_COST_MAX_ITERATIONS)
_Static_assert(3 + 3 * BUSTER_X86_COST_CHAIN_STEPS <= INT32_MAX, "an add or sub of 3, an inc or a dec never overflows a 32-bit chain");

// Whether the branch experiment's jo after the case's instruction is never taken: the instruction defines OF, and the
// chain's values keep it clear. The logic operations clear it; an add or sub of 3 (R14's value, or the imm8), an inc
// and a dec stay inside the range of the chain's width (an add of 4660 doesn't at 32 bits); neg's values, 3 and -3,
// are never the most negative; shr and sar by one of a nonnegative value clear it. The multiplies overflow, the shifts
// by 3 leave OF undefined, shl by one shifts the chain's bits into the sign, and not, lea and mov write no flags (the
// jo would read what came before them): those cases don't run the experiment.
static bool x86_zen5_branch_case(uint8_t id)
{
    const X86CostCase* entry = &x86_zen5_cases[id];
    const uint64_t step = entry->immediate > 0 ? (uint64_t)entry->immediate : 3;
    bool result = false;
    switch (entry->form)
    {
    case X86_FORM_AND_RM32_R32:
    case X86_FORM_AND_RM64_R64:
    case X86_FORM_OR_RM32_R32:
    case X86_FORM_OR_RM64_R64:
    case X86_FORM_XOR_RM32_R32:
    case X86_FORM_XOR_RM64_R64:
    case X86_FORM_AND_RM32_IMM8:
    case X86_FORM_AND_RM64_IMM8:
    case X86_FORM_OR_RM32_IMM8:
    case X86_FORM_OR_RM64_IMM8:
    case X86_FORM_XOR_RM32_IMM8:
    case X86_FORM_XOR_RM64_IMM8:
    case X86_FORM_NEG_RM32:
    case X86_FORM_NEG_RM64:
    case X86_FORM_SHR_RM32_1:
    case X86_FORM_SHR_RM64_1:
    case X86_FORM_SAR_RM32_1:
    case X86_FORM_SAR_RM64_1:
        result = true;
        break;
    case X86_FORM_ADD_RM32_R32:
    case X86_FORM_ADD_R32_RM32:
    case X86_FORM_SUB_RM32_R32:
    case X86_FORM_ADD_RM32_IMM8:
    case X86_FORM_ADD_RM32_IMM32:
    case X86_FORM_SUB_RM32_IMM8:
        result = 3 + step * BUSTER_X86_COST_CHAIN_STEPS <= INT32_MAX;
        break;
    case X86_FORM_INC_RM32:
    case X86_FORM_DEC_RM32:
        result = 3 + BUSTER_X86_COST_CHAIN_STEPS <= INT32_MAX;
        break;
    case X86_FORM_ADD_RM64_R64:
    case X86_FORM_ADD_R64_RM64:
    case X86_FORM_SUB_RM64_R64:
    case X86_FORM_ADD_RM64_IMM8:
    case X86_FORM_ADD_RM64_IMM32:
    case X86_FORM_SUB_RM64_IMM8:
        result = 3 + step * BUSTER_X86_COST_CHAIN_STEPS <= INT64_MAX;
        break;
    case X86_FORM_INC_RM64:
    case X86_FORM_DEC_RM64:
        result = 3 + BUSTER_X86_COST_CHAIN_STEPS <= INT64_MAX;
        break;
    default:
        break;
    }
    return id != 0 && result;
}

// The generated call preserves all SysV callee-saved registers. RDI is the positive loop count, RSI points to an
// aligned word containing its own address. No generated instruction writes memory except the prologue's pushes. The
// branch experiment is refused for a case outside x86_zen5_branch_case.
static bool x86_zen5_program(uint8_t id, X86CostExperiment experiment, X86CostProgram* program)
{
    const uint8_t saved[] = { X86_REGISTER_RBX, X86_REGISTER_RBP, X86_REGISTER_R12, X86_REGISTER_R13, X86_REGISTER_R14, X86_REGISTER_R15 };
    const uint8_t registers[] = { X86_REGISTER_RAX, X86_REGISTER_RDX, X86_REGISTER_R8, X86_REGISTER_R9, X86_REGISTER_R10, X86_REGISTER_R11,
        X86_REGISTER_RBX, X86_REGISTER_RBP, X86_REGISTER_RCX, X86_REGISTER_RDI, X86_REGISTER_R13, X86_REGISTER_R15 };
    X86Instruction records[BUSTER_X86_COST_RECORDS];
    uint32_t offsets[BUSTER_X86_COST_RECORDS];
    uint32_t count = 0;
    memset(program, 0, sizeof(*program));
    for (uint32_t i = 0; i < sizeof(saved); i += 1)
    {
        records[count++] = (X86Instruction){ .form = X86_FORM_PUSH_R64, .rm = saved[i] };
    }
    records[count++] = (X86Instruction){ .form = X86_FORM_MOV_R64_RM64, .reg = X86_REGISTER_R12, .rm = X86_REGISTER_RDI };
    records[count++] = (X86Instruction){ .form = X86_FORM_MOV_R64_IMM64, .rm = X86_REGISTER_R14, .immediate = 3 };
    const bool load = x86_zen5_cases[id].operands == X86_COST_L1_LOAD;
    bool valid = experiment != X86_COST_BRANCH || x86_zen5_branch_case(id);
    for (uint32_t i = 0; i < sizeof(registers); i += 1)
    {
        records[count++] = load ? (X86Instruction){ .form = X86_FORM_MOV_R64_RM64, .reg = registers[i], .rm = X86_REGISTER_RSI } :
            (X86Instruction){ .form = X86_FORM_MOV_R64_IMM64, .rm = registers[i], .immediate = 3 };
    }
    uint8_t prefix[512];
    const int64_t prefix_length = x86_encode_scalar(records, count, prefix, sizeof(prefix), 0, 0);
    valid &= prefix_length > 0;
    if (valid)
    {
        for (uint32_t i = 0; i < (64 - prefix_length % 64) % 64; i += 1)
        {
            records[count++] = (X86Instruction){ .form = X86_FORM_NOP };
        }
        const uint32_t loop = count;
        const bool mix = experiment == X86_COST_MIX_ADD || experiment == X86_COST_MIX_IMUL;
        const bool fused = experiment == X86_COST_BRANCH;
        const bool chain = experiment == X86_COST_CHAIN_DESTINATION || experiment == X86_COST_CHAIN_SOURCE;
        const bool source_path = experiment == X86_COST_CHAIN_SOURCE && x86_zen5_source_path(id);
        for (uint32_t i = 0; id != 0 && i < BUSTER_X86_COST_BLOCK; i += 1)
        {
            const uint32_t chains = mix || experiment == X86_COST_INDEPENDENT_SIX ? 6 : 12;
            const uint8_t destination = registers[chain ? (source_path ? (i + 1) % 12 : 0) : i % chains];
            uint8_t source = source_path ? registers[i % 12] : X86_REGISTER_R14;
            if (load)
            {
                source = chain ? (source_path ? registers[i % 12] : destination) : X86_REGISTER_RSI;
            }
            // Keep simple LEA's base at R14 in the throughput experiments: RBP/R13 as a base force a disp8=0
            // and are a different cost case. The index can use all twelve destinations without that change.
            const bool simple_index = !chain && x86_zen5_cases[id].operands == X86_COST_LEA_SIMPLE;
            records[count] = x86_zen5_fixture(id, destination, simple_index ? destination : source, source_path || simple_index);
            valid &= x86_zen5_id(&records[count]) == id;
            count += 1;
            // A jo to the next instruction, which isn't taken: the case keeps OF clear (x86_zen5_branch_case).
            if (fused)
            {
                records[count++] = (X86Instruction){ .form = X86_FORM_JO_REL8 };
            }
            if (mix)
            {
                const uint8_t blocker = registers[6 + i % 6];
                records[count++] = experiment == X86_COST_MIX_ADD ?
                    (X86Instruction){ .form = X86_FORM_ADD_RM64_R64, .rm = blocker, .reg = X86_REGISTER_R14 } :
                    (X86Instruction){ .form = X86_FORM_IMUL_R64_RM64, .reg = blocker, .rm = X86_REGISTER_R14 };
            }
        }
        program->body_count = count - loop;
        records[count++] = (X86Instruction){ .form = X86_FORM_DEC_RM64, .rm = X86_REGISTER_R12 };
        const uint32_t branch = count;
        records[count++] = (X86Instruction){ .form = X86_FORM_JNE_REL32 };
        for (uint32_t i = sizeof(saved); i > 0; i -= 1)
        {
            records[count++] = (X86Instruction){ .form = X86_FORM_POP_R64, .rm = saved[i - 1] };
        }
        records[count++] = (X86Instruction){ .form = X86_FORM_RET };
        valid &= count <= BUSTER_X86_COST_RECORDS && x86_validate(records, count) == count;
        if (valid)
        {
            program->code = mmap(0, BUSTER_X86_COST_CODE_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            valid = program->code != MAP_FAILED;
            if (!valid)
            {
                program->code = 0;
            }
        }
        if (valid)
        {
            int64_t length = x86_encode_scalar(records, count, program->code, BUSTER_X86_COST_CODE_SIZE, offsets, 0);
            valid = length > 0 && offsets[loop] % 64 == 0;
            if (valid)
            {
                records[branch].displacement = (int32_t)offsets[loop] - (int32_t)offsets[branch + 1];
                length = x86_encode_scalar(records, count, program->code, BUSTER_X86_COST_CODE_SIZE, offsets, 0);
                uint8_t vector[BUSTER_X86_COST_CODE_SIZE];
                const int64_t vector_length = x86_encode(records, count, vector, sizeof(vector), 0, 0);
                valid = vector_length == length && length > 0 && memcmp(vector, program->code, length) == 0;
            }
            if (valid)
            {
                program->length = length;
                program->hash = UINT64_C(14695981039346656037);
                for (uint32_t i = 0; i < program->length; i += 1)
                {
                    program->hash = (program->hash ^ program->code[i]) * UINT64_C(1099511628211);
                }
                valid = mprotect(program->code, BUSTER_X86_COST_CODE_SIZE, PROT_READ | PROT_EXEC) == 0;
            }
        }
    }
    if (!valid && program->code != 0)
    {
        munmap(program->code, BUSTER_X86_COST_CODE_SIZE);
        program->code = 0;
    }
    return valid;
}

// JIT execution is an external ABI boundary, not compiler-path dispatch. Positive iterations are checked at the CLI.
// Generated code has no Clang function-type metadata before its entry point. Disable only that metadata probe;
// ASan and the remaining UBSan checks still cover this wrapper and all construction, model and lookup code.
static __attribute__((no_sanitize("function"))) void x86_zen5_execute(const X86CostProgram* program, uint64_t iterations)
{
    uint64_t cell __attribute__((aligned(64)));
    cell = (uintptr_t)&cell;
    ((void (*)(uint64_t, uint64_t*))program->code)(iterations, &cell);
}

typedef struct X86CostCounters X86CostCounters;
struct X86CostCounters
{
    int fd[3];
};

typedef struct X86CostSample X86CostSample;
struct X86CostSample
{
    uint64_t count;
    uint64_t enabled;
    uint64_t running;
    uint64_t values[3];
};

static bool x86_zen5_counters(X86CostCounters* counters)
{
    bool valid = true;
    for (uint32_t i = 0; i < 3; i += 1)
    {
        counters->fd[i] = -1;
        if (valid)
        {
            struct perf_event_attr attributes = {
                .size = sizeof(attributes),
                .type = i == 2 ? PERF_TYPE_RAW : PERF_TYPE_HARDWARE,
                .config = i == 0 ? PERF_COUNT_HW_CPU_CYCLES : i == 1 ? PERF_COUNT_HW_INSTRUCTIONS : 0xc1,
                .disabled = i == 0,
                .pinned = i == 0,
                .exclude_kernel = 1,
                .exclude_hv = 1,
                .read_format = PERF_FORMAT_GROUP | PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING,
            };
            counters->fd[i] = syscall(SYS_perf_event_open, &attributes, 0, -1, i == 0 ? -1 : counters->fd[0], PERF_FLAG_FD_CLOEXEC);
            valid = counters->fd[i] >= 0;
        }
    }
    return valid;
}

static bool x86_zen5_sample(const X86CostProgram* program, uint64_t iterations, const X86CostCounters* counters, X86CostSample* sample)
{
    bool valid = ioctl(counters->fd[0], PERF_EVENT_IOC_RESET, PERF_IOC_FLAG_GROUP) == 0;
    valid &= ioctl(counters->fd[0], PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP) == 0;
    if (valid)
    {
        x86_zen5_execute(program, iterations);
    }
    valid &= ioctl(counters->fd[0], PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP) == 0;
    valid &= read(counters->fd[0], sample, sizeof(*sample)) == sizeof(*sample);
    valid &= sample->count == 3 && sample->running != 0 && sample->enabled == sample->running;
    const uint64_t expected = iterations * (program->body_count + 2);
    // Besides the loop: initialization, saves, restores, call and counter-control wrappers. A broad fixed bound
    // detects a wrong branch/count or a missing event without converting arbitrary counts into apparent evidence.
    valid &= sample->values[1] >= expected && sample->values[1] - expected < 4096;
    valid &= sample->values[0] != 0 && sample->values[2] != 0;
    if (!valid)
    {
        fprintf(stderr, "invalid counters: count=%lu enabled=%lu running=%lu cycles=%lu instructions=%lu expected=%lu macro-ops=%lu\n",
            sample->count, sample->enabled, sample->running, sample->values[0], sample->values[1], expected, sample->values[2]);
    }
    return valid;
}

static uint64_t x86_zen5_median(uint64_t* values, uint32_t count)
{
    for (uint32_t i = 1; i < count; i += 1)
    {
        const uint64_t value = values[i];
        uint32_t j = i;
        while (j > 0 && values[j - 1] > value)
        {
            values[j] = values[j - 1];
            j -= 1;
        }
        values[j] = value;
    }
    return values[count / 2];
}

static bool x86_zen5_number(const char* text, uint64_t minimum, uint64_t maximum, uint64_t* output)
{
    uint64_t value = 0;
    bool valid = text[0] != 0;
    for (uint64_t i = 0; valid && text[i] != 0; i += 1)
    {
        const uint32_t digit = (uint8_t)text[i] - '0';
        valid = digit <= 9 && value <= (maximum - digit) / 10;
        if (valid)
        {
            value = value * 10 + digit;
        }
    }
    valid &= value >= minimum && value <= maximum;
    if (valid)
    {
        *output = value;
    }
    return valid;
}

static int run_x86_zen5_measure(int argc, char** argv)
{
    uint64_t iterations = 10000;
    uint64_t passes = 7;
    const char* only = 0;
    bool valid = true;
    for (int i = 0; i < argc && valid; i += 1)
    {
        if (strcmp(argv[i], "--iterations") == 0 && i + 1 < argc)
        {
            valid = x86_zen5_number(argv[++i], 1, BUSTER_X86_COST_MAX_ITERATIONS, &iterations);
        }
        else if (strcmp(argv[i], "--passes") == 0 && i + 1 < argc)
        {
            valid = x86_zen5_number(argv[++i], 3, BUSTER_X86_COST_MAX_PASSES, &passes) && (passes & 1) != 0;
        }
        else if (strcmp(argv[i], "--only") == 0 && i + 1 < argc)
        {
            only = argv[++i];
        }
        else
        {
            valid = false;
        }
    }
    uint32_t eax, ebx, ecx, edx;
    __cpuid(1, eax, ebx, ecx, edx);
    const uint32_t signature = eax;
    char brand[49] = {0};
    for (uint32_t i = 0; i < 3; i += 1)
    {
        uint32_t words[4];
        __cpuid(0x80000002 + i, words[0], words[1], words[2], words[3]);
        memcpy(brand + 16 * i, words, 16);
    }
    const bool approved = signature == 0xb40f40 && strstr(brand, "AMD Ryzen 7 9700X") != 0;
    if (!approved)
    {
        fprintf(stderr, "measure-x86-zen5 requires the approved 9700X, got %s (CPUID %08x)\n", brand, signature);
        valid = false;
    }
    X86CostCounters counters = { .fd = { -1, -1, -1 } };
    X86CostProgram baseline = {0};
    if (valid)
    {
        valid = x86_encoder_initialize() && x86_zen5_initialize() && x86_zen5_counters(&counters) && x86_zen5_program(0, X86_COST_INDEPENDENT, &baseline);
    }
    if (valid)
    {
        printf("# CPU=%s CPUID=%08x compiler=%s iterations=%lu passes=%lu block=%u\n", brand, signature, __clang_version__, iterations, passes, BUSTER_X86_COST_BLOCK);
        printf("# cycles are core cycles; ops are retired macro-ops with the empty loop subtracted; mixes are cycles per target+blocker pair, branch per\n");
        printf("# instruction+jo pair, whose ops are 1 where the pair fuses, for the cases whose OF the chains keep clear\n");
        printf("case\texperiment\tcycles\tmin\tmax\tretired_ops\tinstructions\tcode_hash\n");
    }
    uint32_t matched = 0;
    for (uint32_t id = 1; valid && id < X86_COST_COUNT; id += 1)
    {
        if (only == 0 || strcmp(only, x86_zen5_cases[id].name) == 0)
        {
            matched += 1;
            X86CostProgram programs[X86_COST_EXPERIMENT_COUNT] = {0};
            uint64_t cycles[X86_COST_EXPERIMENT_COUNT][BUSTER_X86_COST_MAX_PASSES] = {0};
            uint64_t operations[X86_COST_EXPERIMENT_COUNT][BUSTER_X86_COST_MAX_PASSES] = {0};
            uint64_t instructions[X86_COST_EXPERIMENT_COUNT][BUSTER_X86_COST_MAX_PASSES] = {0};
            // The experiments the case runs: all but the branch one where its jo could be taken.
            const uint32_t experiments = x86_zen5_branch_case((uint8_t)id) ? X86_COST_EXPERIMENT_COUNT : X86_COST_BRANCH;
            for (uint32_t experiment = 0; valid && experiment < experiments; experiment += 1)
            {
                valid = x86_zen5_program(id, experiment, &programs[experiment]);
                if (valid)
                {
                    x86_zen5_execute(&programs[experiment], iterations);
                }
            }
            for (uint32_t pass = 0; valid && pass < passes; pass += 1)
            {
                X86CostSample empty = {0};
                valid = x86_zen5_sample(&baseline, iterations, &counters, &empty);
                for (uint32_t j = 0; valid && j < experiments; j += 1)
                {
                    const uint32_t experiment = (pass + j) % experiments;
                    X86CostSample sample = {0};
                    valid = x86_zen5_sample(&programs[experiment], iterations, &counters, &sample) && sample.values[2] >= empty.values[2];
                    cycles[experiment][pass] = sample.values[0];
                    operations[experiment][pass] = sample.values[2] - empty.values[2];
                    instructions[experiment][pass] = sample.values[1] - empty.values[1];
                }
            }
            if (valid)
            {
                static const char* const names[] = { "independent", "independent-six", "chain-destination", "chain-source", "mix-add", "mix-imul", "branch" };
                const double denominator = (double)iterations * BUSTER_X86_COST_BLOCK;
                for (uint32_t experiment = 0; experiment < experiments; experiment += 1)
                {
                    const uint64_t median = x86_zen5_median(cycles[experiment], passes);
                    const uint64_t ops = x86_zen5_median(operations[experiment], passes);
                    const uint64_t retired = x86_zen5_median(instructions[experiment], passes);
                    printf("%s\t%s\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%016lx\n", x86_zen5_cases[id].name, names[experiment], median / denominator,
                        cycles[experiment][0] / denominator, cycles[experiment][passes - 1] / denominator, ops / denominator, retired / denominator, programs[experiment].hash);
                }
            }
            else
            {
                fprintf(stderr, "measurement failed for %s\n", x86_zen5_cases[id].name);
            }
            for (uint32_t experiment = 0; experiment < X86_COST_EXPERIMENT_COUNT; experiment += 1)
            {
                if (programs[experiment].code != 0)
                {
                    munmap(programs[experiment].code, BUSTER_X86_COST_CODE_SIZE);
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
    if (baseline.code != 0)
    {
        munmap(baseline.code, BUSTER_X86_COST_CODE_SIZE);
    }
    valid &= matched != 0;
    if (!valid)
    {
        fprintf(stderr, "measure-x86-zen5 failed: check options/case name, CPU, perf permissions, pinned counters and generated loop counts; no missing counter is treated as zero\n");
    }
    return valid ? 0 : 1;
}
