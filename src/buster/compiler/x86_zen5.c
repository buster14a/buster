// Copyright (c) 2026 David Gonzalez Martin
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// The Zen 5 processor model (#181): this repository's own 9700X observations of each form the backend emits, keyed by
// the encoder's form AND its operand kind (registers, a zero idiom, a memory operand with or without an index, and
// LEA's four address shapes). x86_zen5_rows.c holds the rows, which measure-x86-zen5 (x86_zen5_measure.c) prints.
//
// - Columns (X86CostColumn): the longest latency from a source operand to the register result (an address register's
//   too, for a memory kind), the load's extra latency on that path, reciprocal throughput, retired macro-ops, the
//   latency to the status flags, macro-ops dispatched to the floating-point unit, and the pipes the form contends for
//   with blockers of known pipes (BUSTER_X86_PIPE_*). Cycle quantities and op counts are fixed point, 256 a unit.
// - Every column of every case is measured (this harness observed it directly), derived (computed from observations
//   with an assumption docs/x86-zen5.md states, such as a bridge instruction's share of a round trip) or absent (the
//   case has no such path: a constant move's latency, a store's register result). Neither of the last two is a cost:
//   an absent or unmeasured value is BUSTER_X86_COST_UNKNOWN, never zero.
// - Cases whose rows agree in every column and mask share one dense ID (x86_zen5_initialize). A record resolves to its
//   ID once (x86_zen5_id); consumers carry the byte ID as a column and look up 32 at a time through eight ZMM
//   registers (x86_zen5_lookup: four banks of 64 IDs, vpermi2w), with x86_zen5_lookup_scalar as the exact twin.
// - An immediate's value, an EVEX zeroing opmask on registers or rounding decoration, a segment override and a fixup mark
//   don't change a record's case (docs/x86-zen5.md has the measurements that support it); a merging opmask
//   (X86_COST_MASKED) and any opmask on memory (X86_COST_MASKED_MEMORY) do.
//   Tuning never grants permission to emit an ISA feature: that remains x86_target.c's job.

#ifndef BUSTER_X86_ZEN5_C
#define BUSTER_X86_ZEN5_C

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "host_isa.h"
#include "baked.h"

#define BUSTER_X86_COST_UNKNOWN UINT16_MAX
#define BUSTER_X86_COST_SCALE 256
// Dense IDs are bytes; ID 0 is the unknown row. Four banks of 64 16-bit entries cover every byte value.
#define BUSTER_X86_COST_CAPACITY 256
#define BUSTER_X86_COST_BANK 64

typedef enum X86CostOperands
{
    // Every operand a register, or no operand.
    X86_COST_REGISTER,
    // An idiom of two equal sources, which depends on no input: xor, sub, pxor and their kin of a register with itself
    // (zero), pcmpeqd's (all ones).
    X86_COST_ZERO,
    // A memory operand addressed by a base and a displacement, by RIP, or by a displacement alone.
    X86_COST_MEMORY,
    // A memory operand with an index (a VSIB one's vector index too).
    X86_COST_MEMORY_INDEX,
    // LEA of one component and a displacement: [base + disp], [rip + disp].
    X86_COST_LEA_BASE,
    // LEA of [base + index], no scale or displacement. RBP and R13 as the base force a displacement: LEA_THREE.
    X86_COST_LEA_INDEX,
    // LEA of [base + index * 2, 4 or 8], no displacement.
    X86_COST_LEA_SCALED,
    // LEA of [base + index * scale + disp].
    X86_COST_LEA_THREE,
    // An EVEX record of registers merging into its destination under an opmask: the destination is a source too (a
    // masked vpaddd's latency is a cycle more than an unmasked one's). Zeroing on registers is the unmasked kind's.
    X86_COST_MASKED,
    // An EVEX record of a memory operand (a base, no index) under an opmask: a masked store (merging) or a zeroing
    // masked load (a zeroing vmovdqu8 load takes two cycles more from its address than an unmasked one).
    X86_COST_MASKED_MEMORY,
    X86_COST_OPERANDS_COUNT,
} X86CostOperands;

typedef enum X86CostColumn
{
    // The longest latency from a source operand (a register, flags, an address register) to the register result.
    X86_COST_LATENCY,
    // Reciprocal throughput: cycles per instruction in a block of independent instances.
    X86_COST_THROUGHPUT,
    // AMD PMCx0C1, retired macro-ops.
    X86_COST_RETIRED_OPS,
    // The latency from a register source to the status flags.
    X86_COST_FLAGS_LATENCY,
    // A memory kind's extra latency from the address to the result, over the register path.
    X86_COST_LOAD_EXTRA,
    // AMD PMCx0AB (umask 4): macro-ops dispatched to the floating-point unit.
    X86_COST_FP_OPS,
    // BUSTER_X86_PIPE_* bits: the blockers this case's instructions measurably contend with at one blocker each.
    X86_COST_PIPES,
    X86_COST_COLUMN_COUNT,
} X86CostColumn;

#define BUSTER_X86_COST_ALL_COLUMNS ((1u << X86_COST_COLUMN_COUNT) - 1)

// The blockers of known pipes the harness mixes each case with (x86_zen5_blockers), one bit each.
#define BUSTER_X86_PIPE_ALU 0x0001
#define BUSTER_X86_PIPE_MULTIPLY 0x0002
#define BUSTER_X86_PIPE_SHIFT 0x0004
#define BUSTER_X86_PIPE_LOAD 0x0008
#define BUSTER_X86_PIPE_STORE 0x0010
#define BUSTER_X86_PIPE_FADD 0x0020
#define BUSTER_X86_PIPE_FMUL 0x0040
#define BUSTER_X86_PIPE_VECTOR_ALU 0x0080
#define BUSTER_X86_PIPE_SHUFFLE 0x0100
#define BUSTER_X86_PIPE_TRANSFER 0x0200
#define BUSTER_X86_PIPE_BLOCKERS 10

// The rows: X(form, operand kind, latency, throughput, retired ops, flags latency, load extra, FP ops, pipes, measured,
// derived, absent), the last three masks of X86CostColumn bits. U is an unknown value.
#define U BUSTER_X86_COST_UNKNOWN
#define BUSTER_X86_ZEN5_ROWS(X) BUSTER_X86_ZEN5_TABLE(X)
#include "buster/compiler/x86_zen5_rows.c"

typedef enum X86CostCaseIndex
{
    X86_COST_CASE_NONE,
#define BUSTER_X86_COST_CASE_INDEX(form, operands, ...) X86_COST_CASE_##form##_##operands,
    BUSTER_X86_ZEN5_ROWS(BUSTER_X86_COST_CASE_INDEX)
#undef BUSTER_X86_COST_CASE_INDEX
    X86_COST_CASE_COUNT,
} X86CostCaseIndex;

typedef struct X86CostCase X86CostCase;
struct X86CostCase
{
    const char* name;
    uint16_t values[X86_COST_COLUMN_COUNT];
    uint16_t form;
    uint8_t operands;
    uint8_t measured;
    uint8_t derived;
    uint8_t absent;
};

static const X86CostCase x86_zen5_cases[X86_COST_CASE_COUNT] = {
    {
        .name = "unknown",
        .values = { U, U, U, U, U, U, U },
    },
#define BUSTER_X86_COST_CASE(form, operands, latency, throughput, ops, flags, load, fp, pipes, measured, derived, absent) \
    { #form "/" #operands, { latency, throughput, ops, flags, load, fp, pipes }, X86_FORM_##form, X86_COST_##operands, measured, derived, absent },
    BUSTER_X86_ZEN5_ROWS(BUSTER_X86_COST_CASE)
#undef BUSTER_X86_COST_CASE
};
#undef U

static BUSTER_BAKED_UNIT uint8_t x86_zen5_form_ids[X86_FORM_COUNT][X86_COST_OPERANDS_COUNT];
static BUSTER_BAKED_UNIT uint8_t x86_zen5_case_ids[X86_COST_CASE_COUNT];
// Per dense ID: the columns the harness measured, derived, and found absent. Zero means none, not a zero cost.
static BUSTER_BAKED_UNIT uint8_t x86_zen5_measured_fields[BUSTER_X86_COST_CAPACITY];
static BUSTER_BAKED_UNIT uint8_t x86_zen5_derived_fields[BUSTER_X86_COST_CAPACITY];
static BUSTER_BAKED_UNIT uint8_t x86_zen5_absent_fields[BUSTER_X86_COST_CAPACITY];
static BUSTER_BAKED_UNIT uint16_t x86_zen5_columns[X86_COST_COLUMN_COUNT][BUSTER_X86_COST_CAPACITY] __attribute__((aligned(64)));
static BUSTER_BAKED_UNIT uint32_t x86_zen5_id_count;
static BUSTER_BAKED_UNIT bool x86_zen5_initialized;

// Whether dense ID `id` already holds exactly the row of `entry`.
static bool x86_zen5_same_row(uint32_t id, const X86CostCase* entry)
{
    bool same = x86_zen5_measured_fields[id] == entry->measured && x86_zen5_derived_fields[id] == entry->derived && x86_zen5_absent_fields[id] == entry->absent;
    for (uint32_t column = 0; column < X86_COST_COLUMN_COUNT; column += 1)
    {
        same &= x86_zen5_columns[column][id] == entry->values[column];
    }
    return same;
}

// Startup only, before worker threads. Reinitialization is deterministic. No allocation or encoder mutation. The bake
// (baked.c) runs this too, so a baked executable returns at the first call. Fails where two rows name one case, a row
// marks a column twice, or the distinct rows exceed the byte IDs.
static bool x86_zen5_initialize(void)
{
    if (!x86_zen5_initialized)
    {
        memset(x86_zen5_form_ids, 0, sizeof(x86_zen5_form_ids));
        memset(x86_zen5_case_ids, 0, sizeof(x86_zen5_case_ids));
        memset(x86_zen5_measured_fields, 0, sizeof(x86_zen5_measured_fields));
        memset(x86_zen5_derived_fields, 0, sizeof(x86_zen5_derived_fields));
        memset(x86_zen5_absent_fields, 0, sizeof(x86_zen5_absent_fields));
        memset(x86_zen5_columns, 0xff, sizeof(x86_zen5_columns));
        bool valid = true;
        uint32_t count = 1;
        for (uint32_t c = 1; c < X86_COST_CASE_COUNT; c += 1)
        {
            const X86CostCase* entry = &x86_zen5_cases[c];
            valid &= entry->form < X86_FORM_COUNT && entry->operands < X86_COST_OPERANDS_COUNT;
            valid &= (entry->measured & entry->derived) == 0 && ((entry->measured | entry->derived) & entry->absent) == 0;
            uint32_t id = 1;
            while (id < count && !x86_zen5_same_row(id, entry))
            {
                id += 1;
            }
            if (valid && id == count && count < BUSTER_X86_COST_CAPACITY)
            {
                for (uint32_t column = 0; column < X86_COST_COLUMN_COUNT; column += 1)
                {
                    x86_zen5_columns[column][id] = entry->values[column];
                }
                x86_zen5_measured_fields[id] = entry->measured;
                x86_zen5_derived_fields[id] = entry->derived;
                x86_zen5_absent_fields[id] = entry->absent;
                count += 1;
            }
            valid &= id < count;
            if (valid)
            {
                valid &= x86_zen5_form_ids[entry->form][entry->operands] == 0;
                x86_zen5_form_ids[entry->form][entry->operands] = (uint8_t)id;
                x86_zen5_case_ids[c] = (uint8_t)id;
            }
        }
        x86_zen5_id_count = count;
        x86_zen5_initialized = valid;
    }
    return x86_zen5_initialized;
}

// The case index of a form's operand kind, or X86_COST_CASE_NONE: for the harness and initialization-time decisions
// (tier0_inc_dec), not a per-record path.
static uint32_t x86_zen5_case_find(uint32_t form, uint32_t operands)
{
    uint32_t found = X86_COST_CASE_NONE;
    for (uint32_t c = 1; c < X86_COST_CASE_COUNT && found == X86_COST_CASE_NONE; c += 1)
    {
        found = x86_zen5_cases[c].form == form && x86_zen5_cases[c].operands == operands ? c : X86_COST_CASE_NONE;
    }
    return found;
}

// A record's operand kind, or X86_COST_OPERANDS_COUNT for an address shape no row describes (an index without a base, a
// 32-bit address). `form` is the form number without a segment override or fixup mark.
static uint32_t x86_zen5_operands(const X86Instruction* instruction, uint32_t form)
{
    const uint32_t mode = instruction->mode;
    const X86FormDescription* description = &x86_form_descriptions[form];
    uint32_t operands = X86_COST_OPERANDS_COUNT;
    // An opmask on a memory record, or merging into a register destination: its own kinds, which only the forms
    // measured with one have. A masked load's row is a zeroing one: merging into a vector destination reads it too,
    // which no row describes. Zeroing on registers is the unmasked kind's.
    const bool masked = (instruction->decorations & BUSTER_X86_DECORATION_MASK) != 0 && description->family == BUSTER_X86_FAMILY_EVEX &&
                        (description->rm & BUSTER_X86_OPERAND_VSIB) == 0;
    const bool zeroing = (instruction->decorations & BUSTER_X86_DECORATION_ZEROING) != 0;
    if (masked && mode != BUSTER_X86_REGISTER_DIRECT)
    {
        const uint32_t klass = description->reg & BUSTER_X86_OPERAND_CLASS;
        const uint32_t syntax = description->syntax;
        const bool into = klass >= BUSTER_X86_CLASS_XMM && klass <= BUSTER_X86_CLASS_ZMM &&
                          (syntax == X86_SYNTAX_REG_RM || syntax == X86_SYNTAX_REG_RM_IMM || syntax == X86_SYNTAX_REG_VVVV_RM ||
                           syntax == X86_SYNTAX_REG_VVVV_RM_IMM || syntax == X86_SYNTAX_REG_RM_VVVV);
        const bool simple = (mode & BUSTER_X86_MODE_INDEX) == 0 && (mode & BUSTER_X86_MODE_ADDRESS_SIZE) == 0;
        operands = simple && (zeroing || !into) ? X86_COST_MASKED_MEMORY : X86_COST_OPERANDS_COUNT;
    }
    else if (masked && !zeroing)
    {
        operands = X86_COST_MASKED;
    }
    else if (mode == BUSTER_X86_REGISTER_DIRECT)
    {
        // An idiom's two sources are one register: reg and rm where the destination is a source, vvvv and rm in a
        // three-operand form. Only forms with a ZERO row have one.
        const bool three = description->vvvv != BUSTER_X86_OPERAND_NONE && description->syntax == X86_SYNTAX_REG_VVVV_RM;
        const bool same = three ? instruction->vvvv == instruction->rm : instruction->reg == instruction->rm;
        operands = same && x86_zen5_form_ids[form][X86_COST_ZERO] != 0 ? X86_COST_ZERO : X86_COST_REGISTER;
    }
    else if ((mode & BUSTER_X86_MODE_ADDRESS_SIZE) == 0)
    {
        const bool base = (mode & BUSTER_X86_MODE_BASE) != 0;
        const bool index = (mode & BUSTER_X86_MODE_INDEX) != 0;
        if (x86_zen5_form_ids[form][X86_COST_LEA_BASE] | x86_zen5_form_ids[form][X86_COST_LEA_INDEX] | x86_zen5_form_ids[form][X86_COST_LEA_SCALED] |
            x86_zen5_form_ids[form][X86_COST_LEA_THREE])
        {
            // RBP and R13 as a base force an encoded displacement, a third component even when it is zero.
            const bool displacement = instruction->displacement != 0 || (base && ((instruction->rm & 7) == X86_REGISTER_RBP));
            if (!index)
            {
                operands = X86_COST_LEA_BASE;
            }
            else if (base)
            {
                operands = displacement ? X86_COST_LEA_THREE : (mode & BUSTER_X86_MODE_SCALE) != 0 ? X86_COST_LEA_SCALED : X86_COST_LEA_INDEX;
            }
        }
        else if (!index || base)
        {
            operands = index ? X86_COST_MEMORY_INDEX : X86_COST_MEMORY;
        }
    }
    return operands;
}

// Accepts validated 64-bit-mode records and fails closed (ID 0) on a form or operand kind no row describes. A memory
// kind assumes an ordinary cacheable L1 hit, which a record can't prove; division assumes the operand ranges the rows
// were measured with (docs/x86-zen5.md).
static uint8_t x86_zen5_id(const X86Instruction* instruction)
{
    uint8_t id = 0;
    const uint32_t number = instruction->form & BUSTER_X86_FORM_NUMBER;
    if (number < BUSTER_X86_FORM_TEMPLATES)
    {
        const uint32_t form = number % X86_FORM_COUNT;
        const uint32_t operands = x86_zen5_operands(instruction, form);
        id = operands < X86_COST_OPERANDS_COUNT ? x86_zen5_form_ids[form][operands] : 0;
    }
    return id;
}

// Every byte indexes a slot; the slots past the rows are unknown. Inputs and outputs are disjoint bounded slices; zero
// count touches neither. Columns must be one of X86CostColumn; keeping that invariant out of the loop is deliberate.
static __attribute__((noinline)) void x86_zen5_lookup_scalar(const uint8_t* restrict ids, uint64_t count, X86CostColumn column, uint16_t* restrict output)
{
    const uint16_t* table = x86_zen5_columns[column];
    #pragma clang loop vectorize(disable) interleave(disable)
    for (uint64_t i = 0; i < count; i += 1)
    {
        output[i] = table[ids[i]];
    }
}

#if BUSTER_HOST_VECTOR
// 32 IDs: a 64-entry permute per bank (index bits 0 to 5), the banks chosen by bits 6 and 7.
static inline __m512i x86_zen5_lookup_keys(__m512i keys, const __m512i* banks)
{
    const __m512i first = _mm512_permutex2var_epi16(banks[0], keys, banks[1]);
    const __m512i second = _mm512_permutex2var_epi16(banks[2], keys, banks[3]);
    const __m512i third = _mm512_permutex2var_epi16(banks[4], keys, banks[5]);
    const __m512i fourth = _mm512_permutex2var_epi16(banks[6], keys, banks[7]);
    const __mmask32 bit6 = _mm512_test_epi16_mask(keys, _mm512_set1_epi16(64));
    const __mmask32 bit7 = _mm512_test_epi16_mask(keys, _mm512_set1_epi16(128));
    const __m512i low = _mm512_mask_blend_epi16(bit6, first, second);
    const __m512i high = _mm512_mask_blend_epi16(bit6, third, fourth);
    return _mm512_mask_blend_epi16(bit7, low, high);
}
#endif

static __attribute__((noinline)) void x86_zen5_lookup(const uint8_t* restrict ids, uint64_t count, X86CostColumn column, uint16_t* restrict output)
{
#if !BUSTER_HOST_VECTOR
    x86_zen5_lookup_scalar(ids, count, column, output);
#else
    __m512i banks[8];
    for (uint32_t b = 0; b < 8; b += 1)
    {
        banks[b] = _mm512_load_si512(x86_zen5_columns[column] + 32 * b);
    }
    const uint64_t full = count & ~UINT64_C(31);
    for (uint64_t i = 0; i < full; i += 32)
    {
        const __m512i keys = _mm512_cvtepu8_epi16(_mm256_loadu_si256((const __m256i*)(ids + i)));
        _mm512_storeu_si512(output + i, x86_zen5_lookup_keys(keys, banks));
    }
    if (full < count)
    {
        const uint32_t remaining = (uint32_t)(count - full);
        const __mmask32 active = UINT32_MAX >> (32 - remaining);
        const __m512i keys = _mm512_cvtepu8_epi16(_mm256_maskz_loadu_epi8(active, ids + full));
        _mm512_mask_storeu_epi16(output + full, active, x86_zen5_lookup_keys(keys, banks));
    }
#endif
}

// Whether dense ID `id` accounts for every column of `required` (BUSTER_X86_COST_ALL_COLUMNS bits): measured, derived
// or absent. ID 0 accounts for none.
static bool x86_zen5_accounts(uint8_t id, uint32_t required)
{
    const uint32_t accounted = (uint32_t)x86_zen5_measured_fields[id] | x86_zen5_derived_fields[id] | x86_zen5_absent_fields[id];
    return id != 0 && (required & ~accounted) == 0;
}

// A consumer's records are tested explicitly, never inferred from this model's own cases. A record counts as missing
// where its case is unknown or a requested column is neither measured, derived nor absent. Zero requests count only
// unknown cases.
static uint64_t x86_zen5_missing(const X86Instruction* instructions, uint64_t count, uint32_t required_columns)
{
    uint64_t missing = 0;
    for (uint64_t i = 0; i < count; i += 1)
    {
        missing += !x86_zen5_accounts(x86_zen5_id(&instructions[i]), required_columns);
    }
    return missing;
}

#endif
