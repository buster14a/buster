// Copyright (c) 2026 David Gonzalez Martin
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Original 9700X observations, keyed by encoder form AND operand case. This is a partial model, not an ISA policy.
// An absent observation stays unknown; in particular, retired macro-ops do not establish executed micro-ops or pipes.
// x86_zen5_id resolves a record once at selection time. Consumers carry its byte ID as a column and use
// x86_zen5_lookup for 32 lookups at a time through two ZMM registers, with x86_zen5_lookup_scalar as its exact twin.
// Latencies and reciprocal throughputs are cycles * 256. All unknown fields are UINT16_MAX, including row zero.
// The measured cases, their dependency paths and the limits of extrapolation are documented in docs/x86-zen5.md.

#ifndef BUSTER_X86_ZEN5_C
#define BUSTER_X86_ZEN5_C

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <immintrin.h>

#define BUSTER_X86_COST_UNKNOWN UINT16_MAX
#define BUSTER_X86_COST_SCALE 256
#define BUSTER_X86_COST_CAPACITY 64

typedef enum X86CostOperands
{
    X86_COST_REGISTER,
    X86_COST_ZERO,
    X86_COST_LEA_SIMPLE,
    X86_COST_LEA_SCALED,
    X86_COST_L1_LOAD,
    X86_COST_OPERANDS_COUNT,
} X86CostOperands;

typedef enum X86CostColumn
{
    X86_COST_GPR_LATENCY,
    X86_COST_THROUGHPUT,
    X86_COST_RETIRED_OPS,
    X86_COST_FLAGS_LATENCY,
    X86_COST_LOAD_EXTRA,
    X86_COST_EXECUTED_UOPS,
    X86_COST_PIPES,
    X86_COST_COLUMN_COUNT,
} X86CostColumn;

// Form, operand case, immediate, latency, reciprocal throughput, retired macro-ops. The last three fields come
// ONLY from this repository's measure-x86-zen5 command. Unknowns are retained until a controlled observation exists.
#define BUSTER_X86_ZEN5_ROWS(X) \
    X(ADD_RM32_R32, REGISTER, 0, 65535, 65535, 65535) \
    X(ADD_RM64_R64, REGISTER, 0, 65535, 65535, 65535) \
    X(ADD_R32_RM32, REGISTER, 0, 65535, 65535, 65535) \
    X(ADD_R64_RM64, REGISTER, 0, 65535, 65535, 65535) \
    X(SUB_RM32_R32, REGISTER, 0, 65535, 65535, 65535) \
    X(SUB_RM64_R64, REGISTER, 0, 65535, 65535, 65535) \
    X(AND_RM32_R32, REGISTER, 0, 65535, 65535, 65535) \
    X(AND_RM64_R64, REGISTER, 0, 65535, 65535, 65535) \
    X(OR_RM32_R32, REGISTER, 0, 65535, 65535, 65535) \
    X(OR_RM64_R64, REGISTER, 0, 65535, 65535, 65535) \
    X(XOR_RM32_R32, REGISTER, 0, 65535, 65535, 65535) \
    X(XOR_RM64_R64, REGISTER, 0, 65535, 65535, 65535) \
    X(XOR_RM32_R32, ZERO, 0, 65535, 65535, 65535) \
    X(XOR_RM64_R64, ZERO, 0, 65535, 65535, 65535) \
    X(ADD_RM32_IMM8, REGISTER, 3, 65535, 65535, 65535) \
    X(ADD_RM64_IMM8, REGISTER, 3, 65535, 65535, 65535) \
    X(ADD_RM32_IMM32, REGISTER, 4660, 65535, 65535, 65535) \
    X(ADD_RM64_IMM32, REGISTER, 4660, 65535, 65535, 65535) \
    X(SUB_RM32_IMM8, REGISTER, 3, 65535, 65535, 65535) \
    X(SUB_RM64_IMM8, REGISTER, 3, 65535, 65535, 65535) \
    X(AND_RM32_IMM8, REGISTER, 3, 65535, 65535, 65535) \
    X(AND_RM64_IMM8, REGISTER, 3, 65535, 65535, 65535) \
    X(OR_RM32_IMM8, REGISTER, 3, 65535, 65535, 65535) \
    X(OR_RM64_IMM8, REGISTER, 3, 65535, 65535, 65535) \
    X(XOR_RM32_IMM8, REGISTER, 3, 65535, 65535, 65535) \
    X(XOR_RM64_IMM8, REGISTER, 3, 65535, 65535, 65535) \
    X(INC_RM32, REGISTER, 0, 65535, 65535, 65535) \
    X(INC_RM64, REGISTER, 0, 65535, 65535, 65535) \
    X(DEC_RM32, REGISTER, 0, 65535, 65535, 65535) \
    X(DEC_RM64, REGISTER, 0, 65535, 65535, 65535) \
    X(NEG_RM32, REGISTER, 0, 65535, 65535, 65535) \
    X(NEG_RM64, REGISTER, 0, 65535, 65535, 65535) \
    X(NOT_RM32, REGISTER, 0, 65535, 65535, 65535) \
    X(NOT_RM64, REGISTER, 0, 65535, 65535, 65535) \
    X(SHL_RM32_1, REGISTER, 0, 65535, 65535, 65535) \
    X(SHL_RM64_1, REGISTER, 0, 65535, 65535, 65535) \
    X(SHR_RM32_1, REGISTER, 0, 65535, 65535, 65535) \
    X(SHR_RM64_1, REGISTER, 0, 65535, 65535, 65535) \
    X(SAR_RM32_1, REGISTER, 0, 65535, 65535, 65535) \
    X(SAR_RM64_1, REGISTER, 0, 65535, 65535, 65535) \
    X(SHL_RM32_IMM8, REGISTER, 3, 65535, 65535, 65535) \
    X(SHL_RM64_IMM8, REGISTER, 3, 65535, 65535, 65535) \
    X(SHR_RM32_IMM8, REGISTER, 3, 65535, 65535, 65535) \
    X(SHR_RM64_IMM8, REGISTER, 3, 65535, 65535, 65535) \
    X(SAR_RM32_IMM8, REGISTER, 3, 65535, 65535, 65535) \
    X(SAR_RM64_IMM8, REGISTER, 3, 65535, 65535, 65535) \
    X(IMUL_R32_RM32, REGISTER, 0, 65535, 65535, 65535) \
    X(IMUL_R64_RM64, REGISTER, 0, 65535, 65535, 65535) \
    X(IMUL_R32_RM32_IMM8, REGISTER, 3, 65535, 65535, 65535) \
    X(IMUL_R64_RM64_IMM8, REGISTER, 3, 65535, 65535, 65535) \
    X(LEA_R32_M, LEA_SIMPLE, 0, 65535, 65535, 65535) \
    X(LEA_R64_M, LEA_SIMPLE, 0, 65535, 65535, 65535) \
    X(LEA_R32_M, LEA_SCALED, 0, 65535, 65535, 65535) \
    X(LEA_R64_M, LEA_SCALED, 0, 65535, 65535, 65535) \
    X(MOV_R32_IMM32, REGISTER, 4660, 65535, 65535, 65535) \
    X(MOV_R64_IMM64, REGISTER, 4660, 65535, 65535, 65535) \
    X(MOV_R64_RM64, L1_LOAD, 0, 65535, 65535, 65535)

typedef enum X86CostId
{
    X86_COST_NONE,
#define BUSTER_X86_COST_ID(form, operands, immediate, latency, throughput, ops) X86_COST_##form##_##operands,
    BUSTER_X86_ZEN5_ROWS(BUSTER_X86_COST_ID)
#undef BUSTER_X86_COST_ID
    X86_COST_COUNT,
} X86CostId;
_Static_assert(X86_COST_COUNT <= BUSTER_X86_COST_CAPACITY, "a cost column fits two ZMM registers");

typedef struct X86CostCase X86CostCase;
struct X86CostCase
{
    uint16_t form;
    uint8_t operands;
    int32_t immediate;
    uint16_t latency;
    uint16_t throughput;
    uint16_t retired_ops;
    const char* name;
};

static const X86CostCase x86_zen5_cases[X86_COST_COUNT] = {
    { .name = "unknown", .latency = 65535, .throughput = 65535, .retired_ops = 65535 },
#define BUSTER_X86_COST_CASE(form, operands, immediate, latency, throughput, ops) \
    { X86_FORM_##form, X86_COST_##operands, immediate, latency, throughput, ops, #form "/" #operands },
    BUSTER_X86_ZEN5_ROWS(BUSTER_X86_COST_CASE)
#undef BUSTER_X86_COST_CASE
};

static uint8_t x86_zen5_form_ids[X86_FORM_COUNT][X86_COST_OPERANDS_COUNT];
// Bits identify fields observed by this repository's 9700X harness. Zero means no observation, not a zero cost.
static uint8_t x86_zen5_measured_fields[BUSTER_X86_COST_CAPACITY];
static uint16_t x86_zen5_columns[X86_COST_COLUMN_COUNT][BUSTER_X86_COST_CAPACITY] __attribute__((aligned(64)));

// Startup only, before worker threads. Reinitialization is deterministic. No allocation or encoder mutation.
static bool x86_zen5_initialize(void)
{
    memset(x86_zen5_form_ids, 0, sizeof(x86_zen5_form_ids));
    memset(x86_zen5_measured_fields, 0, sizeof(x86_zen5_measured_fields));
    memset(x86_zen5_columns, 0xff, sizeof(x86_zen5_columns));
    bool valid = true;
    for (uint32_t id = 1; id < X86_COST_COUNT; id += 1)
    {
        const X86CostCase* entry = &x86_zen5_cases[id];
        valid &= x86_zen5_form_ids[entry->form][entry->operands] == 0;
        x86_zen5_form_ids[entry->form][entry->operands] = id;
        x86_zen5_columns[X86_COST_GPR_LATENCY][id] = entry->latency;
        x86_zen5_columns[X86_COST_THROUGHPUT][id] = entry->throughput;
        x86_zen5_columns[X86_COST_RETIRED_OPS][id] = entry->retired_ops;
        for (uint32_t column = X86_COST_GPR_LATENCY; column <= X86_COST_RETIRED_OPS; column += 1)
        {
            x86_zen5_measured_fields[id] |= (x86_zen5_columns[column][id] != BUSTER_X86_COST_UNKNOWN) << column;
        }
    }
    return valid;
}

// Accepts validated 64-bit-mode records, and fails closed on unsupported cases. L1_LOAD assumes an aligned,
// ordinary cacheable L1 hit; this API cannot prove that runtime condition. No prefix/decoration costs are inferred.
// Immediate cases currently admit only the benchmarked value; no rule extrapolates to zero shifts or other values.
static uint8_t x86_zen5_id(const X86Instruction* instruction)
{
    uint8_t id = 0;
    if (instruction->form < X86_FORM_COUNT && instruction->decorations == 0)
    {
        const uint32_t form = instruction->form;
        uint32_t operands = X86_COST_OPERANDS_COUNT;
        if (instruction->mode == BUSTER_X86_REGISTER_DIRECT)
        {
            operands = X86_COST_REGISTER;
            if ((form == X86_FORM_XOR_RM32_R32 || form == X86_FORM_XOR_RM64_R64) && instruction->reg == instruction->rm)
            {
                operands = X86_COST_ZERO;
            }
            if ((form == X86_FORM_SUB_RM32_R32 || form == X86_FORM_SUB_RM64_R64) && instruction->reg == instruction->rm)
            {
                operands = X86_COST_OPERANDS_COUNT;
            }
        }
        else if (form == X86_FORM_LEA_R32_M || form == X86_FORM_LEA_R64_M)
        {
            // RBP/R13 force an encoded displacement even when it is zero. On the measured LEA stream that changes
            // the macro-op count; do not average those address shapes into the no-displacement case.
            if (instruction->mode == BUSTER_X86_MEMORY_BASE_INDEX(0) && instruction->displacement == 0 &&
                instruction->rm != X86_REGISTER_RBP && instruction->rm != X86_REGISTER_R13)
            {
                operands = X86_COST_LEA_SIMPLE;
            }
            else if (instruction->mode == BUSTER_X86_MEMORY_BASE_INDEX(3) && instruction->displacement == 4660)
            {
                operands = X86_COST_LEA_SCALED;
            }
        }
        else if (instruction->mode == BUSTER_X86_MEMORY_BASE && instruction->displacement == 0)
        {
            operands = X86_COST_L1_LOAD;
        }
        if (operands < X86_COST_OPERANDS_COUNT)
        {
            id = x86_zen5_form_ids[form][operands];
            if (instruction->immediate != x86_zen5_cases[id].immediate ||
                (form == X86_FORM_MOV_R64_IMM64 && instruction->displacement != 0))
            {
                id = 0;
            }
        }
    }
    return id;
}

// Unknown IDs do not wrap through the permutation tables. Inputs and outputs are disjoint bounded slices; zero
// count touches neither. Columns must be one of X86CostColumn; keeping that invariant out of the loop is deliberate.
static __attribute__((noinline)) void x86_zen5_lookup_scalar(const uint8_t* restrict ids, uint64_t count, X86CostColumn column, uint16_t* restrict output)
{
    #pragma clang loop vectorize(disable) interleave(disable)
    for (uint64_t i = 0; i < count; i += 1)
    {
        output[i] = x86_zen5_columns[column][ids[i] < X86_COST_COUNT ? ids[i] : 0];
    }
}

static __attribute__((noinline)) void x86_zen5_lookup(const uint8_t* restrict ids, uint64_t count, X86CostColumn column, uint16_t* restrict output)
{
    const __m512i low = _mm512_load_si512(x86_zen5_columns[column]);
    const __m512i high = _mm512_load_si512(x86_zen5_columns[column] + 32);
    const uint64_t full = count & ~UINT64_C(31);
    for (uint64_t i = 0; i < full; i += 32)
    {
        const __m256i bytes = _mm256_loadu_si256((const __m256i*)(ids + i));
        const __m512i keys = _mm512_cvtepu8_epi16(bytes);
        const __mmask32 known = _mm256_cmplt_epu8_mask(bytes, _mm256_set1_epi8(X86_COST_COUNT));
        const __m512i values = _mm512_permutex2var_epi16(low, keys, high);
        _mm512_storeu_si512(output + i, _mm512_mask_mov_epi16(_mm512_set1_epi16(-1), known, values));
    }
    if (full < count)
    {
        const uint32_t remaining = count - full;
        const __mmask32 active = UINT32_MAX >> (32 - remaining);
        const __m256i bytes = _mm256_maskz_loadu_epi8(active, ids + full);
        const __m512i keys = _mm512_cvtepu8_epi16(bytes);
        const __mmask32 known = _mm256_cmplt_epu8_mask(bytes, _mm256_set1_epi8(X86_COST_COUNT));
        const __m512i values = _mm512_permutex2var_epi16(low, keys, high);
        _mm512_mask_storeu_epi16(output + full, active, _mm512_mask_mov_epi16(_mm512_set1_epi16(-1), known, values));
    }
}

// A consumer's manifest is tested explicitly, never inferred from this model's own cases. An unknown requested
// field is a coverage failure even when the form has some observations. Zero requests means no evidence of coverage.
static uint64_t x86_zen5_missing(const X86Instruction* instructions, uint64_t count, uint32_t required_columns)
{
    uint64_t missing = 0;
    for (uint64_t i = 0; i < count; i += 1)
    {
        const uint8_t id = x86_zen5_id(&instructions[i]);
        bool absent = id == 0;
        for (uint32_t column = 0; column < X86_COST_COLUMN_COUNT; column += 1)
        {
            absent |= (required_columns & (1u << column)) != 0 && x86_zen5_columns[column][id] == BUSTER_X86_COST_UNKNOWN;
        }
        missing += absent;
    }
    return missing;
}

#endif
