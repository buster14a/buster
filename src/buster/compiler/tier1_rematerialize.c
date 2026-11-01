// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Tier 1 rematerialization candidates and baseline-x86 copies. Include after x86_64_encoder.c and x86_zen5.c. The
// IR/selection adapter publishes these facts once, as columns. Only integer bit constants and explicitly stable
// symbol/stack addresses qualify: a symbol's address computed from rip, or read from its GOT slot where the consumer
// certifies that access (BUSTER_TIER1_REMAT_GOT: the loader writes the slot before the program runs, and nothing
// changes it after). Other loads, TLS recipes, dynamic stack addresses, floating operations, and mutable operands do
// not. tier1_rematerialization_marks[_scalar] produce the allocator's byte column; tier1_rematerialize emits one copy;
// tier1_rematerialization_model_costs fills the optional cost columns from the Zen 5 model where it has measured them.
// The caller supplies actual flags liveness at each copy and the final stable frame-base displacement. Every symbol
// copy carries a distinct encoder FIXUP flag and its own returned symbol/addend for link_add_code_fixups, a PC32
// reference for a lea or a LOAD_GOT one for a GOT slot's load (Tier1Rematerialized.got).
// Input columns are immutable, unpadded and borrowed; marks has exactly count writable bytes and must be disjoint
// from every input column. The individual result object is likewise disjoint from the borrowed recipe columns.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "host_isa.h"

#define BUSTER_TIER1_REMAT_NONE 0
#define BUSTER_TIER1_REMAT_INTEGER 1
#define BUSTER_TIER1_REMAT_SYMBOL 2
#define BUSTER_TIER1_REMAT_STACK 3
#define BUSTER_TIER1_REMAT_STABLE 1
// With STABLE, a SYMBOL recipe's address is its GOT slot's content, which the copy loads rather than computes.
#define BUSTER_TIER1_REMAT_GOT 2
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
    // A symbol copy that loads its GOT slot (BUSTER_TIER1_REMAT_GOT), whose reference is a LOAD_GOT rather than a PC32.
    bool got;
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
        eligible = width == 8 && (input->flags[i] == BUSTER_TIER1_REMAT_STABLE || input->flags[i] == (BUSTER_TIER1_REMAT_STABLE | BUSTER_TIER1_REMAT_GOT)) && value < UINT32_MAX;
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
#if !BUSTER_HOST_VECTOR
    return tier1_rematerialization_marks_scalar(input, marks);
#else
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
#endif
}

static bool tier1_rematerialize(const Tier1RematerializationInput* input, uint32_t index, uint8_t destination, uint8_t frame_base, uint8_t flags_live, Tier1Rematerialized* result);

// Candidate i's model costs (tier1_rematerialization_model_costs): its copy's measured GPR latency, and its reload's,
// each 0 where the model has none.
static void tier1_rematerialization_model_cost(const Tier1RematerializationInput* input, uint32_t i, uint8_t frame_base, uint16_t reload, uint16_t* costs, uint16_t* reload_costs)
{
    Tier1Rematerialized copy = { 0 };
    const bool built = tier1_rematerialize(input, i, X86_REGISTER_RAX, frame_base, 0, &copy);
    const uint16_t latency = built ? x86_zen5_columns[X86_COST_GPR_LATENCY][x86_zen5_id(&copy.instruction)] : BUSTER_X86_COST_UNKNOWN;
    costs[i] = latency != BUSTER_X86_COST_UNKNOWN ? latency : 0;
    reload_costs[i] = built ? reload : 0;
}

// The Zen 5 model's costs (x86_zen5.c, #181) of each candidate's copy and of the reload it saves, as the GPR latency it
// measured (cycles * 256), into `costs` and `reload_costs` (count entries each, which the caller's input then borrows;
// the input given here has neither): a copy as tier1_rematerialize emits it, into rax with the flags dead and
// `frame_base` a stack address's base, and the reload as the model's aligned L1-hit load (`mov r64, [base]`), the
// cheapest a load of a slot can be, so that a copy no costlier than it is no costlier than any reload it saves. A cost
// the model hasn't measured is 0, unknown, which keeps the candidate (tier1_rematerialization_eligible): with every
// copy's latency unmeasured, as now, every candidate is recomputed. The vector path classifies 64 operations at a time
// by table, as the marks do, and visits the candidates alone; the scalar path is its twin. Requires x86_zen5_initialize.
static __attribute__((unused)) bool tier1_rematerialization_model_costs(const Tier1RematerializationInput* input, uint8_t frame_base, uint16_t* costs, uint16_t* reload_costs, bool vector)
{
    uint8_t dummy = 0;
    const bool valid = tier1_rematerialization_input_valid(input, &dummy) && input->costs == 0 && (input->count == 0 || (costs != 0 && reload_costs != 0));
    const X86Instruction load = { .form = X86_FORM_MOV_R64_RM64, .reg = X86_REGISTER_RAX, .rm = frame_base, .mode = BUSTER_X86_MEMORY_BASE };
    const uint16_t measured = x86_zen5_columns[X86_COST_GPR_LATENCY][x86_zen5_id(&load)];
    const uint16_t reload = measured != BUSTER_X86_COST_UNKNOWN ? measured : 0;
    if (valid && input->count != 0)
    {
        memset(costs, 0, (uint64_t)input->count * sizeof(uint16_t));
        memset(reload_costs, 0, (uint64_t)input->count * sizeof(uint16_t));
    }
#if BUSTER_HOST_VECTOR
    if (valid && BUSTER_VECTOR_PATH(vector))
    {
        static const uint8_t table[64] __attribute__((aligned(64))) = {
            [BUSTER_TIER1_REMAT_INTEGER] = 1,
            [BUSTER_TIER1_REMAT_SYMBOL] = 1,
            [BUSTER_TIER1_REMAT_STACK] = 1,
        };
        const __m512i lookup = _mm512_load_si512(table);
        for (uint32_t first = 0; first < input->count; first += 64)
        {
            const uint32_t remaining = input->count - first;
            const __mmask64 lanes = remaining >= 64 ? UINT64_MAX : (1ull << remaining) - 1;
            const __m512i ops = _mm512_maskz_loadu_epi8(lanes, input->operations + first);
            const __m512i candidates = _mm512_permutexvar_epi8(ops, lookup);
            __mmask64 selected = lanes & _mm512_cmplt_epu8_mask(ops, _mm512_set1_epi8(64)) & _mm512_cmpneq_epi8_mask(candidates, _mm512_setzero_si512());
            while (selected != 0)
            {
                const uint32_t lane = (uint32_t)__builtin_ctzll(selected);
                selected &= selected - 1;
                tier1_rematerialization_model_cost(input, first + lane, frame_base, reload, costs, reload_costs);
            }
        }
    }
    else
#endif
    {
        for (uint32_t i = 0; valid && i < input->count; i += 1)
        {
            const uint32_t op = input->operations[i];
            if (op == BUSTER_TIER1_REMAT_INTEGER || op == BUSTER_TIER1_REMAT_SYMBOL || op == BUSTER_TIER1_REMAT_STACK)
            {
                tier1_rematerialization_model_cost(input, i, frame_base, reload, costs, reload_costs);
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
            // A lea of the symbol from rip, or a load of its GOT slot.
            candidate.got = (input->flags[index] & BUSTER_TIER1_REMAT_GOT) != 0;
            candidate.instruction.form = (candidate.got ? X86_FORM_MOV_R64_RM64 : X86_FORM_LEA_R64_M) | BUSTER_X86_FORM_FIXUP;
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
