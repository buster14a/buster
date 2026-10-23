// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Tier 1 rematerialization candidates and baseline-x86 copies. Include after x86_64_encoder.c. The IR/selection
// adapter publishes these facts once, as columns. Only integer bit constants and explicitly stable symbol/stack
// addresses qualify. Loads, TLS recipes, dynamic stack addresses, floating operations, and mutable operands do not.
// tier1_rematerialization_marks[_scalar] produce the allocator's byte column; tier1_rematerialize emits one copy.
// The caller supplies actual flags liveness at each copy and the final stable frame-base displacement. Every symbol
// copy carries a distinct encoder FIXUP flag and its own returned symbol/addend for link_add_code_fixups.
// Input columns are immutable, unpadded and borrowed; marks has exactly count writable bytes and must be disjoint
// from every input column. The individual result object is likewise disjoint from the borrowed recipe columns.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <immintrin.h>

#define BUSTER_TIER1_REMAT_NONE 0
#define BUSTER_TIER1_REMAT_INTEGER 1
#define BUSTER_TIER1_REMAT_SYMBOL 2
#define BUSTER_TIER1_REMAT_STACK 3
#define BUSTER_TIER1_REMAT_STABLE 1
#define BUSTER_TIER1_REMAT_NO_SYMBOL UINT32_MAX
#define BUSTER_TIER1_REMAT_MAX_VALUES (1u << 20)

typedef struct Tier1RematerializationInput Tier1RematerializationInput;
struct Tier1RematerializationInput
{
    const uint8_t* operations;
    const uint8_t* widths;
    const uint8_t* flags;
    // INTEGER: raw value bits. SYMBOL: symbol id, which must fit u32 and differ from UINT32_MAX.
    // STACK: final signed displacement from the frame planner's stable base register, sign-extended to 64 bits.
    const uint64_t* values;
    const int64_t* addends;
    // Optional measured comparable costs. Unknown (0) means prefer rematerialization. A known cost larger than
    // reload_cost suppresses the candidate; equality keeps it because it also avoids a spill store and slot.
    const uint16_t* costs;
    const uint16_t* reload_costs;
    uint32_t count;
};

typedef struct Tier1Rematerialized Tier1Rematerialized;
struct Tier1Rematerialized
{
    X86Instruction instruction;
    uint32_t symbol;
    int64_t addend;
};

static bool tier1_rematerialization_input_valid(const Tier1RematerializationInput* input, const uint8_t* marks)
{
    bool valid = input != 0;
    if (valid)
    {
        valid = input->count <= BUSTER_TIER1_REMAT_MAX_VALUES && (input->count == 0 || (input->operations != 0 && input->widths != 0 && input->flags != 0 && input->values != 0 && marks != 0));
        valid = valid && ((input->costs == 0 && input->reload_costs == 0) || (input->costs != 0 && input->reload_costs != 0));
    }
    return valid;
}

static bool tier1_rematerialization_eligible(const Tier1RematerializationInput* input, uint32_t i)
{
    const uint32_t op = input->operations[i];
    const uint32_t width = input->widths[i];
    const uint64_t value = input->values[i];
    bool eligible = false;
    if (op == BUSTER_TIER1_REMAT_INTEGER)
    {
        eligible = width == 8 || (width == 4 && value <= UINT32_MAX);
    }
    else if (op == BUSTER_TIER1_REMAT_SYMBOL)
    {
        eligible = width == 8 && input->flags[i] == BUSTER_TIER1_REMAT_STABLE && value < UINT32_MAX;
    }
    else if (op == BUSTER_TIER1_REMAT_STACK)
    {
        eligible = width == 8 && input->flags[i] == BUSTER_TIER1_REMAT_STABLE && (int64_t)value == (int64_t)(int32_t)value;
    }
    if (eligible && input->costs != 0 && input->costs[i] != 0 && input->reload_costs[i] != 0)
    {
        eligible = input->costs[i] <= input->reload_costs[i];
    }
    return eligible;
}

static __attribute__((unused)) bool tier1_rematerialization_marks_scalar(const Tier1RematerializationInput* input, uint8_t* marks)
{
    const bool valid = tier1_rematerialization_input_valid(input, marks);
    if (valid)
    {
        for (uint32_t i = 0; i < input->count; i += 1)
        {
            marks[i] = tier1_rematerialization_eligible(input, i) ? 1 : 0;
        }
    }
    return valid;
}

static __attribute__((unused)) bool tier1_rematerialization_marks(const Tier1RematerializationInput* input, uint8_t* marks)
{
    const bool valid = tier1_rematerialization_input_valid(input, marks);
    if (valid)
    {
        // Operation table lookup rejects every unrecognized value, including byte values beyond the table.
        static const uint8_t table[64] __attribute__((aligned(64))) = {
            [BUSTER_TIER1_REMAT_INTEGER] = 1,
            [BUSTER_TIER1_REMAT_SYMBOL] = 1,
            [BUSTER_TIER1_REMAT_STACK] = 1,
        };
        const __m512i lookup = _mm512_load_si512(table);
        for (uint32_t first = 0; first < input->count; first += 64)
        {
            const uint32_t remaining = input->count - first;
            const uint32_t count = remaining < 64 ? remaining : 64;
            const __mmask64 lanes = count == 64 ? UINT64_MAX : (1ull << count) - 1;
            const __m512i ops = _mm512_maskz_loadu_epi8(lanes, input->operations + first);
            const __m512i candidates = _mm512_permutexvar_epi8(ops, lookup);
            __mmask64 selected = lanes & _mm512_cmplt_epu8_mask(ops, _mm512_set1_epi8(64)) & _mm512_cmpneq_epi8_mask(candidates, _mm512_setzero_si512());
            _mm512_mask_storeu_epi8(marks + first, lanes, _mm512_setzero_si512());
            // The u64 payloads and cost facts are sparse, so inspect only classified candidates. This avoids seven
            // otherwise unconditional columns for the common noncandidate instruction and preserves the exact tail.
            while (selected != 0)
            {
                const uint32_t lane = (uint32_t)__builtin_ctzll(selected);
                selected &= selected - 1;
                marks[first + lane] = tier1_rematerialization_eligible(input, first + lane) ? 1 : 0;
            }
        }
    }
    return valid;
}

static __attribute__((unused)) bool tier1_rematerialize(const Tier1RematerializationInput* input, uint32_t index, uint8_t destination, uint8_t frame_base, uint8_t flags_live, Tier1Rematerialized* result)
{
    uint8_t dummy = 0;
    bool valid = result != 0 && tier1_rematerialization_input_valid(input, &dummy) && index < input->count && destination < 16;
    Tier1Rematerialized candidate = {
        .symbol = BUSTER_TIER1_REMAT_NO_SYMBOL,
    };
    if (valid)
    {
        valid = tier1_rematerialization_eligible(input, index);
    }
    if (valid)
    {
        const uint32_t operation = input->operations[index];
        const uint64_t value = input->values[index];
        candidate.instruction.rm = destination;
        if (operation == BUSTER_TIER1_REMAT_INTEGER)
        {
            if (value == 0 && flags_live == 0)
            {
                candidate.instruction.form = X86_FORM_XOR_RM32_R32;
                candidate.instruction.reg = destination;
            }
            else if (value <= UINT32_MAX)
            {
                candidate.instruction.form = X86_FORM_MOV_R32_IMM32;
                candidate.instruction.immediate = (int32_t)value;
            }
            else if ((int64_t)value == (int64_t)(int32_t)value)
            {
                candidate.instruction.form = X86_FORM_MOV_RM64_IMM32;
                candidate.instruction.immediate = (int32_t)value;
            }
            else
            {
                candidate.instruction.form = X86_FORM_MOV_R64_IMM64;
                candidate.instruction.immediate = (int32_t)value;
                candidate.instruction.displacement = (int32_t)(value >> 32);
            }
        }
        else if (operation == BUSTER_TIER1_REMAT_SYMBOL)
        {
            candidate.instruction.form = X86_FORM_LEA_R64_M | BUSTER_X86_FORM_FIXUP;
            candidate.instruction.mode = BUSTER_X86_MEMORY_RIP;
            candidate.instruction.reg = destination;
            candidate.instruction.rm = 0;
            candidate.symbol = (uint32_t)value;
            candidate.addend = input->addends != 0 ? input->addends[index] : 0;
        }
        else
        {
            valid = frame_base < 16;
            candidate.instruction.form = X86_FORM_LEA_R64_M;
            candidate.instruction.mode = BUSTER_X86_MEMORY_BASE;
            candidate.instruction.rm = frame_base;
            candidate.instruction.reg = destination;
            candidate.instruction.displacement = (int32_t)value;
        }
    }
    if (valid)
    {
        *result = candidate;
    }
    return valid;
}
