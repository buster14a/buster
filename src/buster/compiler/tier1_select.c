// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Tier 1 instruction selection (#141). Planning precedes allocation: folding an address, shift or load extends its
// operands' live ranges to the consuming instruction. The plan publishes those effective uses directly, so allocation
// never guesses them from the unselected IR. Emission consumes that plan and the resulting physical-register column.
// Records, node boundaries and branch columns then go directly to branch sizing and the x86 encoder.
//
// This is a bounded integer slice. Unsupported operations, allocation fragments and target forms are explicit errors;
// semantic failures never select Tier 0 silently. docs/tier1-selection.md records the supported rules and boundaries.
// Include after ir.c, lane.c, x86_64_encoder.c, x86_semantics.c, x86_target.c, x86_target_forms.c and x86_zen5.c.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "host_isa.h"

#define BUSTER_TIER1_SELECT_NONE UINT32_MAX
#define BUSTER_TIER1_SELECT_NO_REGISTER UINT8_MAX
#define BUSTER_TIER1_SELECT_LANES 16
#define BUSTER_TIER1_SELECT_SCRATCH X86_REGISTER_R11
#define BUSTER_TIER1_SELECT_RECORDS_PER_NODE 6
// The most records a rule that takes x86's fixed registers writes (tier1_select_fixed), before its consumer's saves: a
// remainder by a constant's reciprocal whose multiplier takes the add sequence, whose divisor is past an imm32 and whose
// dividend is in rax or rdx has 13.
#define BUSTER_TIER1_SELECT_FIXED_RECORDS 13
#define BUSTER_TIER1_SELECT_CALL_ARGUMENTS 6

typedef enum Tier1SelectError
{
    TIER1_SELECT_OK,
    TIER1_SELECT_ERROR_INVALID,
    TIER1_SELECT_ERROR_UNSUPPORTED,
    TIER1_SELECT_ERROR_ALLOCATION,
    TIER1_SELECT_ERROR_TARGET,
    TIER1_SELECT_ERROR_MEMORY,
    TIER1_SELECT_ERROR_LIMIT,
} Tier1SelectError;

typedef enum Tier1SelectRule
{
    TIER1_RULE_NONE,
    TIER1_RULE_LEAF,
    TIER1_RULE_CONSTANT,
    TIER1_RULE_COPY,
    TIER1_RULE_UNARY,
    TIER1_RULE_REGISTER,
    TIER1_RULE_IMMEDIATE,
    TIER1_RULE_LEA_ADD,
    TIER1_RULE_LEA_SHIFT,
    TIER1_RULE_MULTIPLY_LEA,
    TIER1_RULE_MULTIPLY_SHIFT,
    TIER1_RULE_ADDRESS,
    TIER1_RULE_LOAD,
    TIER1_RULE_LOAD_ADDRESS,
    TIER1_RULE_LOAD_OPERAND,
    TIER1_RULE_STORE,
    TIER1_RULE_STORE_ADDRESS,
    TIER1_RULE_COMPARE,
    TIER1_RULE_COMPARE_IMMEDIATE,
    TIER1_RULE_BRANCH,
    TIER1_RULE_BRANCH_COMPARE,
    TIER1_RULE_SELECT,
    TIER1_RULE_JUMP,
    TIER1_RULE_RETURN,
    TIER1_RULE_CALL,
    TIER1_RULE_TRUNCATE,
    TIER1_RULE_ZERO_EXTEND,
    TIER1_RULE_SIGN_EXTEND,
    TIER1_RULE_UNREACHABLE,
    // A phi: no records, and no effective uses, since its operands are uses on its block's incoming edges, which the
    // CFG consumer's edge moves read (tier1_emit_cfg.c).
    TIER1_RULE_PHI,
    // A frame object's address (IR_STACK_ADDR of slot `immediate`): a lea from the stack pointer, whose base and
    // displacement the CFG consumer sets from the frame's layout of the object (tier1_emit_cfg.c).
    TIER1_RULE_STACK_ADDRESS,
    // A symbol's address (IR_SYMBOL_ADDR of symbol `immediate`): a lea from rip, marked for its reference, which the CFG
    // consumer gives it, or a load of the symbol's GOT slot where its access says so.
    TIER1_RULE_SYMBOL_ADDRESS,
    // A division or remainder of values[0] by values[1] (`immediate`'s bit 0 signed, bit 1 the remainder), and a shift of
    // values[0] by the count in values[1]: x86's fixed registers (rax and rdx, or cl), which the CFG consumer's sequence
    // takes, saving another value live past it there (tier1_emit_cfg_fixed).
    TIER1_RULE_DIVIDE,
    TIER1_RULE_SHIFT,
    // A signed division or remainder of values[0] by 2^`scale` (`immediate`'s bits as TIER1_RULE_DIVIDE's), rounded
    // toward zero as C's: the dividend plus 2^scale - 1 where it is negative, a bias the scratch register computes from
    // its sign, shifted right, or for the remainder masked and the bias taken back. An unsigned one is a shift or a mask
    // (TIER1_RULE_IMMEDIATE).
    TIER1_RULE_DIVIDE_POWER,
    // A division or remainder of values[0] by the constant `immediate` (the width's bits; tier1_select_reciprocal_divisor
    // says which), `scale`'s bits as TIER1_RULE_DIVIDE's `immediate`'s: a multiplication by the divisor's reciprocal and
    // shifts (Tier1SelectReciprocal), mul's or imul's rdx:rax, which the consumers save around it as TIER1_RULE_DIVIDE's.
    TIER1_RULE_DIVIDE_RECIPROCAL,
    // AVX-512's masks (#179), m8 to m64, in the opmask registers that the CFG consumer's allocation gives them (k1 to
    // k7: a node's register is its tier1_cfg.c location, BUSTER_TIER1_CFG_SPILL's class 2, whose low three bits are
    // the k register's number). `width` is the mask's bytes, 1 to 8: kmovb, kmovw, kmovd, kmovq and the opmask
    // instructions of that width, which leave the bits past it zero, as the IR's masks have them. A constant (zero by
    // kxor, every bit by kxnor, else its bits through the scratch register); a copy (kmov); and, or and xor of values[0]
    // and values[1]; not of values[0]; a load and a store, as the integer ones, the store's mask in values[2], each one
    // kmov, which must stay its node's last record: the CFG consumer writes a frame object's base and offset into that
    // record (`stack_object`, tier1_emit_cfg_records). Only the CFG consumer takes them.
    TIER1_RULE_MASK_CONSTANT,
    TIER1_RULE_MASK_COPY,
    TIER1_RULE_MASK_LOGIC,
    TIER1_RULE_MASK_NOT,
    TIER1_RULE_MASK_LOAD,
    TIER1_RULE_MASK_LOAD_ADDRESS,
    TIER1_RULE_MASK_STORE,
    TIER1_RULE_MASK_STORE_ADDRESS,
    // An indirect jump (IR_BR_INDIRECT, #789) to the address in values[0]'s register: a computed goto's from its
    // function's dispatch block, or a jump table's, its entry's offset added to the table's address. The CFG consumer
    // places its edges' moves after it, at its labels' blocks or in stubs where it defines their labels
    // (tier1_emit_cfg.c).
    TIER1_RULE_JUMP_INDIRECT,
    TIER1_RULE_COUNT,
} Tier1SelectRule;

// Whether a rule is a mask's (#179), which the uniform-register consumer and the straight-line one don't take.
static bool tier1_select_mask_rule(uint32_t rule)
{
    return rule >= TIER1_RULE_MASK_CONSTANT && rule <= TIER1_RULE_MASK_STORE_ADDRESS;
}

// Register-only leaf bodies use caller-saved registers. R11 is reserved for a destructive instruction's second
// operand when allocation assigned it the destination; it cannot hold any IR value, including a dead definition.
static bool tier1_select_register_allowed(uint8_t reg)
{
    const uint32_t allowed = (1u << X86_REGISTER_RAX) | (1u << X86_REGISTER_RCX) | (1u << X86_REGISTER_RDX) | (1u << X86_REGISTER_RSI) | (1u << X86_REGISTER_RDI) | (1u << X86_REGISTER_R8) | (1u << X86_REGISTER_R9) | (1u << X86_REGISTER_R10);
    return reg < 16 && (allowed & (1u << reg)) != 0;
}

static X86Instruction tier1_select_register_record(uint16_t form, uint8_t destination, uint8_t source)
{
    const X86Instruction result =
    {
        .reg = destination,
        .rm = source,
        .form = form,
    };
    return result;
}

static X86Instruction tier1_select_move(uint32_t width, uint8_t destination, uint8_t source)
{
    return tier1_select_register_record(width == 8 ? X86_FORM_MOV_R64_RM64 : X86_FORM_MOV_R32_RM32, destination, source);
}

// A node's records (tier1_select_expand's). At most one of them has an immediate whose form the function's batch chooses
// (tier1_select_requests_flush, #141): the record at `request` when `requested`, empty until the batch, with the
// request's destination, row, value and context.
typedef struct Tier1SelectRecords Tier1SelectRecords;
struct Tier1SelectRecords
{
    X86Instruction records[BUSTER_TIER1_SELECT_RECORDS_PER_NODE];
    int64_t value;
    uint16_t row;
    uint16_t context;
    uint8_t count;
    uint8_t request;
    uint8_t destination;
    bool requested;
    // A rule's sequence longer than a node's room: its records past the room weren't written, and every consumer refuses
    // the node (tier1_select_append).
    bool overflowed;
};

// A node's next record, where its room (BUSTER_TIER1_SELECT_RECORDS_PER_NODE) has one. A rule whose sequence is longer
// writes nothing past the room and marks the node's records overflowed, which every consumer refuses, so that the
// function fails rather than its expansion overwriting its own fields (#977's review: the signed power-of-two
// remainder's six records are the room's whole).
static void tier1_select_append(Tier1SelectRecords* result, X86Instruction record)
{
    const bool room = result->count < BUSTER_TIER1_SELECT_RECORDS_PER_NODE;
    if (room)
    {
        result->records[result->count] = record;
        result->count += 1;
    }
    result->overflowed |= !room;
}

// A record whose operation, on the destination register, has an immediate, as a request. The semantics module owns sign
// extension, zero idioms, immediate widths and the flags proof for each alternate form. An operation without a row
// leaves the record empty, which validation refuses. The target's allow-list is checked on the final records,
// independently of the host's ISA.
static void tier1_select_request(Tier1SelectRecords* result, X86Operation operation, uint32_t width, uint8_t destination, int64_t value, uint8_t live_flags)
{
    const uint32_t row = x86_choice_row(operation, width);
    if (row != BUSTER_X86_CHOICE_NO_ROW && result->count < BUSTER_TIER1_SELECT_RECORDS_PER_NODE)
    {
        result->requested = true;
        result->request = result->count;
        result->destination = destination;
        result->row = (uint16_t)row;
        result->value = value;
        result->context = (uint16_t)(BUSTER_X86_CONTEXT_REGISTER | (destination == X86_REGISTER_RAX ? BUSTER_X86_CONTEXT_ACCUMULATOR : 0) | live_flags);
    }
    tier1_select_append(result, (X86Instruction){0});
}

// A function's requests, in its records' order, held until x86_choose_forms' 32 lanes are full or the function's records
// end (tier1_select_requests_flush): each one's record (`at`), destination register, row, value and context. On the
// scalar path (`vector` false) each is chosen as it comes, by the twin, and none is held.
#define BUSTER_TIER1_SELECT_BATCH 32
typedef struct Tier1SelectRequests Tier1SelectRequests;
struct Tier1SelectRequests
{
    int64_t values[BUSTER_TIER1_SELECT_BATCH];
    uint32_t at[BUSTER_TIER1_SELECT_BATCH];
    uint16_t rows[BUSTER_TIER1_SELECT_BATCH];
    uint16_t contexts[BUSTER_TIER1_SELECT_BATCH];
    uint16_t forms[BUSTER_TIER1_SELECT_BATCH];
    uint8_t destinations[BUSTER_TIER1_SELECT_BATCH];
    uint32_t count;
    bool vector;
};

// The held requests' forms, chosen in one batch by x86_choose_forms: each record the chosen form's, or left empty where
// no form takes the value, which validation then refuses, as a choice of one record at a time did.
static void tier1_select_requests_flush(Tier1SelectRequests* requests, X86Instruction* records)
{
    x86_choose_forms(requests->rows, requests->values, requests->contexts, requests->count, requests->forms);
    for (uint32_t q = 0; q < requests->count; q += 1)
    {
        const uint16_t form = requests->forms[q];
        if (form != X86_FORM_NONE)
        {
            records[requests->at[q]] = x86_choice_instruction(form, (X86Instruction){ .rm = requests->destinations[q] }, requests->values[q]);
        }
    }
    requests->count = 0;
}

// The node's request, if it has one, its record at `first` among the function's `records`: held, a full batch's forms
// chosen into the records first, or on the scalar path chosen at once.
static void tier1_select_requests_add(Tier1SelectRequests* requests, const Tier1SelectRecords* selected, uint32_t first, X86Instruction* records)
{
    if (selected->requested && !requests->vector)
    {
        uint16_t form = X86_FORM_NONE;
        x86_choose_forms_scalar(&selected->row, &selected->value, &selected->context, 1, &form);
        if (form != X86_FORM_NONE)
        {
            records[first + selected->request] = x86_choice_instruction(form, (X86Instruction){ .rm = selected->destination }, selected->value);
        }
    }
    else if (selected->requested)
    {
        if (requests->count == BUSTER_TIER1_SELECT_BATCH)
        {
            tier1_select_requests_flush(requests, records);
        }
        const uint32_t q = requests->count++;
        requests->at[q] = first + selected->request;
        requests->values[q] = selected->value;
        requests->rows[q] = selected->row;
        requests->contexts[q] = selected->context;
        requests->destinations[q] = selected->destination;
    }
}

static bool tier1_select_immediate_fits(uint32_t width, uint64_t value)
{
    return width == 4 || (uint64_t)(int64_t)(int32_t)value == value;
}

static uint32_t tier1_select_log2(uint64_t value)
{
    return value != 0 && (value & (value - 1)) == 0 ? (uint32_t)__builtin_ctzll(value) : UINT32_MAX;
}

// A division by a constant as a multiplication by its reciprocal (Granlund and Montgomery, "Division by Invariant
// Integers using Multiplication", PLDI 1994), for N-bit words, N the width's bits:
// - unsigned: the quotient is the high N bits of the dividend, shifted right by `pre_shift`, times `multiplier`, shifted
//   right by `post_shift`. Where the multiplier needs N + 1 bits (`add`), `multiplier` is its low N bits and t that high
//   product: the quotient is (((n - t) >> 1) + t) >> (post_shift - 1), in which n - t can't borrow nor the sum carry.
// - signed, by the divisor's magnitude: the signed high product of the dividend and `multiplier`, plus the dividend where
//   the multiplier is 2^(N - 1) or more (`add`: `multiplier` is then negative as an N-bit number), shifted right
//   arithmetically by `post_shift`, plus one where that is negative, which makes the floor C's rounding toward zero; and
//   negated for a negative divisor.
typedef struct Tier1SelectReciprocal Tier1SelectReciprocal;
struct Tier1SelectReciprocal
{
    uint64_t multiplier;
    uint8_t pre_shift;
    uint8_t post_shift;
    bool add;
};

// floor((2^power + addend) / divisor), for `power` at most 128 (2^128 as the largest 128-bit number plus one), `addend`
// below 2^127 and a nonzero divisor.
static unsigned __int128 tier1_select_quotient_power(uint32_t power, unsigned __int128 addend, uint64_t divisor)
{
    const unsigned __int128 all = ~(unsigned __int128)0;
    return power < 128 ? (((unsigned __int128)1 << power) + addend) / divisor : all / divisor + (all % divisor + 1 + addend) / divisor;
}

// Granlund and Montgomery's CHOOSE_MULTIPLIER (their Figure 6.2) for a divisor of at least 3 that isn't a power of two, an
// N-bit word and dividends of `precision` bits: the multiplier, below 2^(N + 1) (and below 2^N for a precision below N),
// and its shift.
static unsigned __int128 tier1_select_multiplier(uint64_t divisor, uint32_t bits, uint32_t precision, uint8_t* post_shift)
{
    const uint32_t log = 64 - (uint32_t)__builtin_clzll(divisor - 1);
    unsigned __int128 low = tier1_select_quotient_power(bits + log, 0, divisor);
    unsigned __int128 high = tier1_select_quotient_power(bits + log, (unsigned __int128)1 << (bits + log - precision), divisor);
    uint32_t shift = log;
    while ((low >> 1) < (high >> 1) && shift > 0)
    {
        low >>= 1;
        high >>= 1;
        shift -= 1;
    }
    *post_shift = (uint8_t)shift;
    return high;
}

// The magnitude of a signed divisor whose two's complement is the width's bits of `divisor`.
static uint64_t tier1_select_magnitude(uint64_t divisor, uint32_t width)
{
    const uint64_t sign = UINT64_C(1) << (width * 8 - 1);
    const uint64_t extended = ((divisor & (sign | (sign - 1))) ^ sign) - sign;
    return (int64_t)extended < 0 ? 0 - extended : extended;
}

// Whether a constant divisor (the width's bits) takes TIER1_RULE_DIVIDE_RECIPROCAL: an unsigned one but zero and the powers
// of two (a shift or a mask), and a signed one of magnitude 3 or more but ±2^k and the sign bit. Zero and -1 stay with div
// and idiv, which raise C's undefined division by zero and INT_MIN / -1 as the hardware does, and 1 is a copy.
static bool tier1_select_reciprocal_divisor(uint64_t divisor, uint32_t width, bool is_signed)
{
    const uint64_t magnitude = is_signed ? tier1_select_magnitude(divisor, width) : divisor;
    return magnitude >= 3 && tier1_select_log2(magnitude) == UINT32_MAX;
}

// A divisor's reciprocal (tier1_select_reciprocal_divisor's), its width's bits for an unsigned one.
static Tier1SelectReciprocal tier1_select_reciprocal(uint64_t divisor, uint32_t width, bool is_signed)
{
    const uint32_t bits = width * 8;
    const unsigned __int128 word = (unsigned __int128)1 << bits;
    Tier1SelectReciprocal result = {0};
    unsigned __int128 multiplier = 0;
    if (is_signed)
    {
        multiplier = tier1_select_multiplier(tier1_select_magnitude(divisor, width), bits, bits - 1, &result.post_shift);
        result.add = multiplier >= word >> 1;
    }
    else
    {
        multiplier = tier1_select_multiplier(divisor, bits, bits, &result.post_shift);
        // An even divisor whose multiplier needs N + 1 bits: the dividend shifted right by the divisor's zero bits, whose
        // odd part's multiplier then fits N bits (their Figure 6.2's retry).
        const uint32_t zeros = (uint32_t)__builtin_ctzll(divisor);
        if (multiplier >= word && zeros != 0)
        {
            multiplier = tier1_select_multiplier(divisor >> zeros, bits, bits - zeros, &result.post_shift);
            result.pre_shift = (uint8_t)zeros;
        }
        result.add = multiplier >= word;
    }
    result.multiplier = (uint64_t)(multiplier & (word - 1));
    return result;
}

static const uint16_t tier1_select_condition_branches[10] =
{
    X86_FORM_JE_REL8,
    X86_FORM_JNE_REL8,
    X86_FORM_JL_REL8,
    X86_FORM_JLE_REL8,
    X86_FORM_JG_REL8,
    X86_FORM_JGE_REL8,
    X86_FORM_JB_REL8,
    X86_FORM_JBE_REL8,
    X86_FORM_JA_REL8,
    X86_FORM_JAE_REL8,
};

static const uint16_t tier1_select_condition_sets[10] =
{
    X86_FORM_SETE_RM8,
    X86_FORM_SETNE_RM8,
    X86_FORM_SETL_RM8,
    X86_FORM_SETLE_RM8,
    X86_FORM_SETG_RM8,
    X86_FORM_SETGE_RM8,
    X86_FORM_SETB_RM8,
    X86_FORM_SETBE_RM8,
    X86_FORM_SETA_RM8,
    X86_FORM_SETAE_RM8,
};

static const uint8_t tier1_select_condition_flags[10] =
{
    BUSTER_X86_FLAG_ZF,
    BUSTER_X86_FLAG_ZF,
    BUSTER_X86_FLAG_SF | BUSTER_X86_FLAG_OF,
    BUSTER_X86_FLAG_ZF | BUSTER_X86_FLAG_SF | BUSTER_X86_FLAG_OF,
    BUSTER_X86_FLAG_ZF | BUSTER_X86_FLAG_SF | BUSTER_X86_FLAG_OF,
    BUSTER_X86_FLAG_SF | BUSTER_X86_FLAG_OF,
    BUSTER_X86_FLAG_CF,
    BUSTER_X86_FLAG_CF | BUSTER_X86_FLAG_ZF,
    BUSTER_X86_FLAG_CF | BUSTER_X86_FLAG_ZF,
    BUSTER_X86_FLAG_CF,
};

// A selected rule's holes, after allocation. This is transient emitter state, not another IR: operands are physical
// registers, `memory` is already an encoder addressing operand, and the rule fixes the exact record sequence.
typedef struct Tier1SelectHoles Tier1SelectHoles;
struct Tier1SelectHoles
{
    Tier1SelectRule rule;
    X86Operation operation;
    uint64_t immediate;
    X86Instruction memory;
    uint8_t width;
    uint8_t source_width;
    uint8_t destination;
    uint8_t left;
    uint8_t right;
    uint8_t third;
    uint8_t condition;
    uint8_t scale;
    bool compare_immediate;
    // The pattern's no-ops (Tier1SelectPattern.nops).
    uint8_t nops;
};

static uint16_t tier1_select_binary_form(X86Operation operation, uint32_t width)
{
    uint16_t form = X86_FORM_NONE;
    switch (operation)
    {
    case X86_OPERATION_ADD:
        form = width == 8 ? X86_FORM_ADD_R64_RM64 : X86_FORM_ADD_R32_RM32;
        break;
    case X86_OPERATION_SUB:
        form = width == 8 ? X86_FORM_SUB_R64_RM64 : X86_FORM_SUB_R32_RM32;
        break;
    case X86_OPERATION_AND:
        form = width == 8 ? X86_FORM_AND_R64_RM64 : X86_FORM_AND_R32_RM32;
        break;
    case X86_OPERATION_OR:
        form = width == 8 ? X86_FORM_OR_R64_RM64 : X86_FORM_OR_R32_RM32;
        break;
    case X86_OPERATION_XOR:
        form = width == 8 ? X86_FORM_XOR_R64_RM64 : X86_FORM_XOR_R32_RM32;
        break;
    case X86_OPERATION_IMUL:
        form = width == 8 ? X86_FORM_IMUL_R64_RM64 : X86_FORM_IMUL_R32_RM32;
        break;
    default:
        break;
    }
    return form;
}

static bool tier1_select_commutative(X86Operation operation)
{
    return operation == X86_OPERATION_ADD || operation == X86_OPERATION_AND || operation == X86_OPERATION_OR || operation == X86_OPERATION_XOR || operation == X86_OPERATION_IMUL;
}

// imul destination, source, value: its sign-extended imm8 form where the value fits one, else imm32's.
static X86Instruction tier1_select_multiply_immediate(uint32_t width, uint8_t destination, uint8_t source, uint64_t value)
{
    const bool small = (uint64_t)(int64_t)(int8_t)value == value || (width == 4 && (uint32_t)(int32_t)(int8_t)value == (uint32_t)value);
    const uint16_t form = width == 8 ? (small ? X86_FORM_IMUL_R64_RM64_IMM8 : X86_FORM_IMUL_R64_RM64_IMM32) : (small ? X86_FORM_IMUL_R32_RM32_IMM8 : X86_FORM_IMUL_R32_RM32_IMM32);
    X86Instruction record = tier1_select_register_record(form, destination, source);
    record.immediate = small ? (int8_t)value : (int32_t)value;
    return record;
}

// mov reg, value at `width`: mov r32, imm32 (which zero-extends), mov r64, simm32 or movabs, the shortest that holds it, as
// rematerialization chooses (without the zero idiom, which these sequences' constants never need).
static X86Instruction tier1_select_constant_move(uint32_t width, uint8_t reg, uint64_t value)
{
    const bool zero_extends = width == 4 || value <= UINT32_MAX;
    const bool sign_extends = (uint64_t)(int64_t)(int32_t)value == value;
    const X86Instruction result =
    {
        .displacement = zero_extends || sign_extends ? 0 : (int32_t)(value >> 32),
        .immediate = (int32_t)value,
        .rm = reg,
        .form = zero_extends ? X86_FORM_MOV_R32_IMM32 : sign_extends ? X86_FORM_MOV_RM64_IMM32 : X86_FORM_MOV_R64_IMM64,
    };
    return result;
}

// shr reg, count (sar for X86_OPERATION_SAR) at `width`.
static X86Instruction tier1_select_shift(X86Operation operation, uint32_t width, uint8_t reg, uint32_t count)
{
    const uint16_t form = operation == X86_OPERATION_SAR ? (width == 8 ? X86_FORM_SAR_RM64_IMM8 : X86_FORM_SAR_RM32_IMM8) : (width == 8 ? X86_FORM_SHR_RM64_IMM8 : X86_FORM_SHR_RM32_IMM8);
    const X86Instruction result = { .immediate = (int32_t)count, .rm = reg, .form = form };
    return result;
}

static void tier1_select_compare(Tier1SelectRecords* result, const Tier1SelectHoles* holes)
{
    if (holes->compare_immediate)
    {
        tier1_select_request(result, X86_OPERATION_CMP, holes->width, holes->left, (int64_t)holes->immediate, tier1_select_condition_flags[holes->condition]);
    }
    else
    {
        tier1_select_append(result, tier1_select_register_record(holes->width == 8 ? X86_FORM_CMP_R64_RM64 : X86_FORM_CMP_R32_RM32, holes->left, holes->right));
    }
}

// The opmask forms of a mask of each width (#179), by the width's log2, then a row of none for any other width: kmov
// between opmask registers, from memory, to memory and from a general register (a 32-bit one, but kmovq's 64), and the
// opmask instructions the selector takes.
// kmovb, kandb and the other byte forms need AVX512DQ, the doubleword and quadword ones AVX512BW, the word ones AVX512F:
// the target's allow-list decides, as for every record.
typedef struct Tier1SelectMaskForms Tier1SelectMaskForms;
struct Tier1SelectMaskForms
{
    uint16_t move;
    uint16_t load;
    uint16_t store;
    uint16_t from_general;
    uint16_t and_forms;
    uint16_t or_forms;
    uint16_t xor_forms;
    uint16_t xnor_forms;
    uint16_t not_forms;
};

static const Tier1SelectMaskForms tier1_select_mask_forms_table[5] =
{
    { X86_FORM_KMOVB_K_K, X86_FORM_KMOVB_K_M8, X86_FORM_KMOVB_M8_K, X86_FORM_KMOVB_K_R32, X86_FORM_KANDB_K_K_K, X86_FORM_KORB_K_K_K, X86_FORM_KXORB_K_K_K, X86_FORM_KXNORB_K_K_K, X86_FORM_KNOTB_K_K },
    { X86_FORM_KMOVW_K_K, X86_FORM_KMOVW_K_M16, X86_FORM_KMOVW_M16_K, X86_FORM_KMOVW_K_R32, X86_FORM_KANDW_K_K_K, X86_FORM_KORW_K_K_K, X86_FORM_KXORW_K_K_K, X86_FORM_KXNORW_K_K_K, X86_FORM_KNOTW_K_K },
    { X86_FORM_KMOVD_K_K, X86_FORM_KMOVD_K_M32, X86_FORM_KMOVD_M32_K, X86_FORM_KMOVD_K_R32, X86_FORM_KANDD_K_K_K, X86_FORM_KORD_K_K_K, X86_FORM_KXORD_K_K_K, X86_FORM_KXNORD_K_K_K, X86_FORM_KNOTD_K_K },
    { X86_FORM_KMOVQ_K_K, X86_FORM_KMOVQ_K_M64, X86_FORM_KMOVQ_M64_K, X86_FORM_KMOVQ_K_R64, X86_FORM_KANDQ_K_K_K, X86_FORM_KORQ_K_K_K, X86_FORM_KXORQ_K_K_K, X86_FORM_KXNORQ_K_K_K, X86_FORM_KNOTQ_K_K },
    { X86_FORM_NONE, X86_FORM_NONE, X86_FORM_NONE, X86_FORM_NONE, X86_FORM_NONE, X86_FORM_NONE, X86_FORM_NONE, X86_FORM_NONE, X86_FORM_NONE },
};

// A mask width's forms: 1, 2, 4 or 8 bytes. Any other width, which no mask has, gets X86_FORM_NONE throughout rather than
// another width's forms: no target's allow-list takes that form (x86_target_form_allowed), so the CFG consumer refuses a
// record of it at emission (TIER1_EMIT_TARGET).
static const Tier1SelectMaskForms* tier1_select_mask_forms(uint32_t width)
{
    const uint32_t index = width == 8 ? 3 : width == 4 ? 2 : width == 2 ? 1 : width == 1 ? 0 : 4;
    return &tier1_select_mask_forms_table[index];
}

static Tier1SelectRecords tier1_select_expand(const Tier1SelectHoles* holes)
{
    Tier1SelectRecords result = {0};
    const uint8_t destination = holes->destination;
    const uint32_t width = holes->width;
    for (uint32_t r = 0; r < holes->nops; r += 1)
    {
        tier1_select_append(&result, (X86Instruction){ .form = X86_FORM_NOP });
    }
    switch (holes->rule)
    {
    case TIER1_RULE_LEAF:
    case TIER1_RULE_NONE:
        break;
    case TIER1_RULE_CONSTANT:
        tier1_select_request(&result, X86_OPERATION_MOV, width, destination, (int64_t)holes->immediate, 0);
        break;
    case TIER1_RULE_COPY:
        // Only a copy whose narrow source the planner's canonical column certified vanishes in its own register: its
        // 32-bit storage has zeros past its width (tier1_select_canonical lists the producers, including the entry
        // MOVZX of a narrow parameter and the result MOVZX of a narrow call). A 32-bit copy of an I32 value keeps its
        // move: a parameter or call result may arrive with a dirty upper half, and the move is what clears it (the call
        // tests' hostile callees check this).
        if (destination != holes->left || (width == 4 && holes->source_width == 0))
        {
            tier1_select_append(&result, tier1_select_move(width, destination, holes->left));
        }
        break;
    case TIER1_RULE_TRUNCATE:
    case TIER1_RULE_ZERO_EXTEND:
    {
        // Truncations and zero extensions always emit: even in place they must clear the bits past the width.
        // Narrow zero extensions never reach this case from the planner; they select COPY above.
        const uint32_t source_width = holes->rule == TIER1_RULE_TRUNCATE ? width : holes->source_width;
        const uint16_t form = source_width == 1 ? X86_FORM_MOVZX_R32_RM8 : source_width == 2 ? X86_FORM_MOVZX_R32_RM16 : X86_FORM_MOV_R32_RM32;
        tier1_select_append(&result, tier1_select_register_record(form, destination, holes->left));
        break;
    }
    case TIER1_RULE_SIGN_EXTEND:
    {
        const uint16_t form = holes->source_width == 4 ? X86_FORM_MOVSXD_R64_RM32 : holes->source_width == 2 ? (width == 8 ? X86_FORM_MOVSX_R64_RM16 : X86_FORM_MOVSX_R32_RM16) : (width == 8 ? X86_FORM_MOVSX_R64_RM8 : X86_FORM_MOVSX_R32_RM8);
        tier1_select_append(&result, tier1_select_register_record(form, destination, holes->left));
        if (width == 2)
        {
            // I8 -> I16 has sixteen significant bits. Clear the rest of its canonical 32-bit storage value.
            tier1_select_append(&result, tier1_select_register_record(X86_FORM_MOVZX_R32_RM16, destination, destination));
        }
        break;
    }
    case TIER1_RULE_UNARY:
        if (destination != holes->left)
        {
            tier1_select_append(&result, tier1_select_move(width, destination, holes->left));
        }
        tier1_select_append(&result, (X86Instruction)
        {
            .rm = destination,
            .form = holes->operation == X86_OPERATION_NEG ? (width == 8 ? X86_FORM_NEG_RM64 : X86_FORM_NEG_RM32) : (width == 8 ? X86_FORM_NOT_RM64 : X86_FORM_NOT_RM32),
        });
        break;
    case TIER1_RULE_REGISTER:
    {
        uint8_t left = holes->left;
        uint8_t right = holes->right;
        if (destination == right && left != right)
        {
            if (tier1_select_commutative(holes->operation))
            {
                right = left;
                left = destination;
            }
            else
            {
                tier1_select_append(&result, tier1_select_move(width, BUSTER_TIER1_SELECT_SCRATCH, right));
                right = BUSTER_TIER1_SELECT_SCRATCH;
            }
        }
        if (destination != left)
        {
            tier1_select_append(&result, tier1_select_move(width, destination, left));
        }
        tier1_select_append(&result, tier1_select_register_record(tier1_select_binary_form(holes->operation, width), destination, right));
        break;
    }
    case TIER1_RULE_IMMEDIATE:
    case TIER1_RULE_MULTIPLY_SHIFT:
    {
        if (holes->operation == X86_OPERATION_IMUL && holes->rule == TIER1_RULE_IMMEDIATE)
        {
            tier1_select_append(&result, tier1_select_multiply_immediate(width, destination, holes->left, holes->immediate));
        }
        else
        {
            if (destination != holes->left)
            {
                tier1_select_append(&result, tier1_select_move(width, destination, holes->left));
            }
            const bool multiply_shift = holes->rule == TIER1_RULE_MULTIPLY_SHIFT;
            const int64_t immediate = multiply_shift ? (int64_t)holes->scale : (int64_t)holes->immediate;
            const X86Operation operation = multiply_shift ? X86_OPERATION_SHL : holes->operation;
            tier1_select_request(&result, operation, width, destination, immediate, 0);
        }
        break;
    }
    case TIER1_RULE_LEA_ADD:
    case TIER1_RULE_LEA_SHIFT:
    case TIER1_RULE_MULTIPLY_LEA:
    case TIER1_RULE_ADDRESS:
    {
        X86Instruction record = holes->memory;
        record.reg = destination;
        record.form = width == 8 ? X86_FORM_LEA_R64_M : X86_FORM_LEA_R32_M;
        tier1_select_append(&result, record);
        break;
    }
    case TIER1_RULE_LOAD:
    case TIER1_RULE_LOAD_ADDRESS:
    {
        X86Instruction record = holes->memory;
        record.reg = destination;
        record.form = width == 1 ? X86_FORM_MOVZX_R32_RM8 : width == 2 ? X86_FORM_MOVZX_R32_RM16 : width == 8 ? X86_FORM_MOV_R64_RM64 : X86_FORM_MOV_R32_RM32;
        tier1_select_append(&result, record);
        break;
    }
    case TIER1_RULE_LOAD_OPERAND:
    {
        X86Instruction memory = holes->memory;
        // Moving the left input must not destroy the address before its one memory access.
        const bool aliases_base = (memory.mode & BUSTER_X86_MODE_BASE) != 0 && memory.rm == destination;
        const bool aliases_index = (memory.mode & BUSTER_X86_MODE_INDEX) != 0 && memory.index == destination;
        if (destination != holes->left && (aliases_base || aliases_index))
        {
            tier1_select_append(&result, tier1_select_move(8, BUSTER_TIER1_SELECT_SCRATCH, destination));
            memory.rm = aliases_base ? BUSTER_TIER1_SELECT_SCRATCH : memory.rm;
            memory.index = aliases_index ? BUSTER_TIER1_SELECT_SCRATCH : memory.index;
        }
        if (destination != holes->left)
        {
            tier1_select_append(&result, tier1_select_move(width, destination, holes->left));
        }
        memory.reg = destination;
        memory.form = tier1_select_binary_form(holes->operation, width);
        tier1_select_append(&result, memory);
        break;
    }
    case TIER1_RULE_STORE:
    case TIER1_RULE_STORE_ADDRESS:
    {
        X86Instruction record = holes->memory;
        record.reg = holes->right;
        record.form = width == 1 ? X86_FORM_MOV_RM8_R8 : width == 2 ? X86_FORM_MOV_RM16_R16 : width == 8 ? X86_FORM_MOV_RM64_R64 : X86_FORM_MOV_RM32_R32;
        tier1_select_append(&result, record);
        break;
    }
    case TIER1_RULE_COMPARE:
    case TIER1_RULE_COMPARE_IMMEDIATE:
        tier1_select_compare(&result, holes);
        tier1_select_append(&result, (X86Instruction){ .rm = destination, .form = tier1_select_condition_sets[holes->condition] });
        tier1_select_append(&result, tier1_select_register_record(X86_FORM_MOVZX_R32_RM8, destination, destination));
        break;
    case TIER1_RULE_BRANCH_COMPARE:
        tier1_select_compare(&result, holes);
        tier1_select_append(&result, (X86Instruction){ .form = tier1_select_condition_branches[holes->condition] });
        tier1_select_append(&result, (X86Instruction){ .form = X86_FORM_JMP_REL8 });
        break;
    case TIER1_RULE_BRANCH:
        tier1_select_append(&result, tier1_select_register_record(X86_FORM_TEST_RM32_R32, holes->left, holes->left));
        tier1_select_append(&result, (X86Instruction){ .form = X86_FORM_JNE_REL8 });
        tier1_select_append(&result, (X86Instruction){ .form = X86_FORM_JMP_REL8 });
        break;
    case TIER1_RULE_SELECT:
    {
        tier1_select_append(&result, tier1_select_register_record(X86_FORM_TEST_RM32_R32, holes->left, holes->left));
        const bool inverse = destination == holes->right;
        const uint8_t initial = inverse ? holes->right : holes->third;
        const uint8_t alternate = inverse ? holes->third : holes->right;
        if (destination != initial)
        {
            tier1_select_append(&result, tier1_select_move(width, destination, initial));
        }
        const uint16_t form = width == 8 ? (inverse ? X86_FORM_CMOVE_R64_RM64 : X86_FORM_CMOVNE_R64_RM64) : (inverse ? X86_FORM_CMOVE_R32_RM32 : X86_FORM_CMOVNE_R32_RM32);
        tier1_select_append(&result, tier1_select_register_record(form, destination, alternate));
        break;
    }
    case TIER1_RULE_JUMP:
        tier1_select_append(&result, (X86Instruction){ .form = X86_FORM_JMP_REL8 });
        break;
    case TIER1_RULE_JUMP_INDIRECT:
        tier1_select_append(&result, (X86Instruction){ .form = X86_FORM_JMP_RM64, .rm = holes->left });
        break;
    case TIER1_RULE_RETURN:
        if (width != 0 && holes->left != X86_REGISTER_RAX)
        {
            tier1_select_append(&result, tier1_select_move(width, X86_REGISTER_RAX, holes->left));
        }
        tier1_select_append(&result, (X86Instruction){ .form = X86_FORM_RET });
        break;
    case TIER1_RULE_UNREACHABLE:
        tier1_select_append(&result, (X86Instruction){ .form = X86_FORM_UD2 });
        break;
    case TIER1_RULE_PHI:
        break;
    case TIER1_RULE_STACK_ADDRESS:
        tier1_select_append(&result, (X86Instruction){ .form = X86_FORM_LEA_R64_M, .reg = destination, .rm = X86_REGISTER_RSP, .mode = BUSTER_X86_MEMORY_BASE });
        break;
    case TIER1_RULE_SYMBOL_ADDRESS:
        tier1_select_append(&result, (X86Instruction){ .form = X86_FORM_LEA_R64_M | BUSTER_X86_FORM_FIXUP, .reg = destination, .mode = BUSTER_X86_MEMORY_RIP });
        break;
    case TIER1_RULE_DIVIDE_POWER:
    {
        // The bias in the scratch register: the dividend's sign through every bit, then its low `shift` bits (for a shift
        // of 1 the sign bit alone), added to the dividend by a lea, which leaves the dividend for the remainder's mask.
        const uint8_t bias = BUSTER_TIER1_SELECT_SCRATCH;
        const uint32_t bits = width * 8;
        const uint32_t shift = holes->scale;
        tier1_select_append(&result, tier1_select_move(width, bias, holes->left));
        if (shift > 1)
        {
            tier1_select_append(&result, (X86Instruction){ .form = width == 8 ? X86_FORM_SAR_RM64_IMM8 : X86_FORM_SAR_RM32_IMM8, .rm = bias, .immediate = (int32_t)(bits - 1) });
        }
        tier1_select_append(&result, (X86Instruction){ .form = width == 8 ? X86_FORM_SHR_RM64_IMM8 : X86_FORM_SHR_RM32_IMM8, .rm = bias, .immediate = (int32_t)(bits - shift) });
        tier1_select_append(&result, (X86Instruction){ .form = width == 8 ? X86_FORM_LEA_R64_M : X86_FORM_LEA_R32_M, .reg = destination, .rm = holes->left, .index = bias, .mode = (uint8_t)BUSTER_X86_MEMORY_BASE_INDEX(0) });
        if ((holes->immediate & 2) != 0)
        {
            const int32_t mask = (int32_t)((UINT64_C(1) << shift) - 1);
            const uint16_t form = mask <= 127 ? (width == 8 ? X86_FORM_AND_RM64_IMM8 : X86_FORM_AND_RM32_IMM8) : (width == 8 ? X86_FORM_AND_RM64_IMM32 : X86_FORM_AND_RM32_IMM32);
            tier1_select_append(&result, (X86Instruction){ .form = form, .rm = destination, .immediate = mask });
            tier1_select_append(&result, tier1_select_register_record(width == 8 ? X86_FORM_SUB_R64_RM64 : X86_FORM_SUB_R32_RM32, destination, bias));
        }
        else
        {
            tier1_select_append(&result, (X86Instruction){ .form = width == 8 ? X86_FORM_SAR_RM64_IMM8 : X86_FORM_SAR_RM32_IMM8, .rm = destination, .immediate = (int32_t)shift });
        }
        break;
    }
    // A mask's registers are opmask registers: a node's register is its location (class 2's), whose low three bits are
    // the k register; a memory operand's base and index are general registers.
    case TIER1_RULE_MASK_CONSTANT:
    {
        const Tier1SelectMaskForms* forms = tier1_select_mask_forms(width);
        const uint8_t k = destination & 7;
        const uint64_t every = width >= 8 ? UINT64_MAX : (UINT64_C(1) << (width * 8)) - 1;
        if (holes->immediate == 0 || holes->immediate == every)
        {
            tier1_select_append(&result, (X86Instruction){ .form = holes->immediate == 0 ? forms->xor_forms : forms->xnor_forms, .reg = k, .vvvv = k, .rm = k });
        }
        else
        {
            tier1_select_request(&result, X86_OPERATION_MOV, width == 8 ? 8 : 4, BUSTER_TIER1_SELECT_SCRATCH, (int64_t)holes->immediate, 0);
            tier1_select_append(&result, (X86Instruction){ .form = forms->from_general, .reg = k, .rm = BUSTER_TIER1_SELECT_SCRATCH });
        }
        break;
    }
    case TIER1_RULE_MASK_COPY:
        if ((destination & 7) != (holes->left & 7))
        {
            tier1_select_append(&result, (X86Instruction){ .form = tier1_select_mask_forms(width)->move, .reg = destination & 7, .rm = holes->left & 7 });
        }
        break;
    case TIER1_RULE_MASK_LOGIC:
    {
        const Tier1SelectMaskForms* forms = tier1_select_mask_forms(width);
        const uint16_t form = holes->operation == X86_OPERATION_AND ? forms->and_forms : holes->operation == X86_OPERATION_OR ? forms->or_forms : forms->xor_forms;
        tier1_select_append(&result, (X86Instruction){ .form = form, .reg = destination & 7, .vvvv = holes->left & 7, .rm = holes->right & 7 });
        break;
    }
    case TIER1_RULE_MASK_NOT:
        tier1_select_append(&result, (X86Instruction){ .form = tier1_select_mask_forms(width)->not_forms, .reg = destination & 7, .rm = holes->left & 7 });
        break;
    // One kmov each, the node's last record, which the CFG consumer's frame-object patch rewrites (`stack_object`).
    case TIER1_RULE_MASK_LOAD:
    case TIER1_RULE_MASK_LOAD_ADDRESS:
    {
        X86Instruction record = holes->memory;
        record.reg = destination & 7;
        record.form = tier1_select_mask_forms(width)->load;
        tier1_select_append(&result, record);
        break;
    }
    case TIER1_RULE_MASK_STORE:
    case TIER1_RULE_MASK_STORE_ADDRESS:
    {
        X86Instruction record = holes->memory;
        record.reg = holes->right & 7;
        record.form = tier1_select_mask_forms(width)->store;
        tier1_select_append(&result, record);
        break;
    }
    default:
        tier1_select_append(&result, (X86Instruction){0});
        break;
    }
    return result;
}

// The fixed registers a rule's records write (tier1_select_fixed), which its consumer saves around them where other values
// live past the node in them: rax and rdx for a division, rcx for a shift by a register, and none for any other rule.
static uint32_t tier1_select_fixed_registers(uint32_t rule)
{
    const uint32_t divide = (1u << X86_REGISTER_RAX) | (1u << X86_REGISTER_RDX);
    return rule == TIER1_RULE_DIVIDE || rule == TIER1_RULE_DIVIDE_RECIPROCAL ? divide : rule == TIER1_RULE_SHIFT ? 1u << X86_REGISTER_RCX : 0;
}

// TIER1_RULE_DIVIDE_RECIPROCAL's records (Tier1SelectReciprocal), the high product in rdx: the multiplier in rax, or in rdx
// where rax holds the dividend, times the dividend's register, or for a pre-shift the multiplier in rdx times the shifted
// dividend in rax. A dividend in rax or rdx that is read after the multiplication (`add`'s sequences and a remainder's)
// is copied into the scratch register first. A remainder is the dividend less the quotient times the divisor (its
// magnitude for a signed one, whose quotient is then left unnegated). The result moves into the destination last.
static uint32_t tier1_select_reciprocal_records(const Tier1SelectHoles* holes, X86Instruction* records)
{
    const uint32_t width = holes->width;
    const bool is_signed = (holes->scale & 1) != 0;
    const bool remainder = (holes->scale & 2) != 0;
    const Tier1SelectReciprocal reciprocal = tier1_select_reciprocal(holes->immediate, width, is_signed);
    const uint8_t rax = X86_REGISTER_RAX;
    const uint8_t rdx = X86_REGISTER_RDX;
    const bool kept = remainder || reciprocal.add;
    const uint8_t dividend = kept && (holes->left == rax || holes->left == rdx) ? BUSTER_TIER1_SELECT_SCRATCH : holes->left;
    uint32_t count = 0;
    if (dividend != holes->left)
    {
        records[count++] = tier1_select_move(width, dividend, holes->left);
    }
    const uint16_t multiply = is_signed ? (width == 8 ? X86_FORM_IMUL_RM64 : X86_FORM_IMUL_RM32) : (width == 8 ? X86_FORM_MUL_RM64 : X86_FORM_MUL_RM32);
    if (reciprocal.pre_shift != 0)
    {
        if (dividend != rax)
        {
            records[count++] = tier1_select_move(width, rax, dividend);
        }
        records[count++] = tier1_select_shift(X86_OPERATION_SHR, width, rax, reciprocal.pre_shift);
        records[count++] = tier1_select_constant_move(width, rdx, reciprocal.multiplier);
        records[count++] = (X86Instruction){ .rm = rdx, .form = multiply };
    }
    else
    {
        records[count++] = tier1_select_constant_move(width, dividend == rax ? rdx : rax, reciprocal.multiplier);
        records[count++] = (X86Instruction){ .rm = dividend == rax ? rdx : dividend, .form = multiply };
    }
    uint8_t quotient = rdx;
    if (!is_signed && reciprocal.add)
    {
        records[count++] = tier1_select_move(width, rax, dividend);
        records[count++] = tier1_select_register_record(tier1_select_binary_form(X86_OPERATION_SUB, width), rax, rdx);
        records[count++] = tier1_select_shift(X86_OPERATION_SHR, width, rax, 1);
        records[count++] = tier1_select_register_record(tier1_select_binary_form(X86_OPERATION_ADD, width), rax, rdx);
        if (reciprocal.post_shift > 1)
        {
            records[count++] = tier1_select_shift(X86_OPERATION_SHR, width, rax, reciprocal.post_shift - 1u);
        }
        quotient = rax;
    }
    else if (!is_signed && reciprocal.post_shift != 0)
    {
        records[count++] = tier1_select_shift(X86_OPERATION_SHR, width, rdx, reciprocal.post_shift);
    }
    else if (is_signed)
    {
        if (reciprocal.add)
        {
            records[count++] = tier1_select_register_record(tier1_select_binary_form(X86_OPERATION_ADD, width), rdx, dividend);
        }
        if (reciprocal.post_shift != 0)
        {
            records[count++] = tier1_select_shift(X86_OPERATION_SAR, width, rdx, reciprocal.post_shift);
        }
        records[count++] = tier1_select_move(width, rax, rdx);
        records[count++] = tier1_select_shift(X86_OPERATION_SHR, width, rax, width * 8 - 1);
        records[count++] = tier1_select_register_record(tier1_select_binary_form(X86_OPERATION_ADD, width), rdx, rax);
        const bool negative = ((holes->immediate >> (width * 8 - 1)) & 1) != 0;
        if (negative && !remainder)
        {
            records[count++] = (X86Instruction){ .rm = rdx, .form = width == 8 ? X86_FORM_NEG_RM64 : X86_FORM_NEG_RM32 };
        }
    }
    if (remainder)
    {
        const uint64_t divisor = is_signed ? tier1_select_magnitude(holes->immediate, width) : holes->immediate;
        const uint8_t other = quotient == rax ? rdx : rax;
        if (tier1_select_immediate_fits(width, divisor))
        {
            records[count++] = tier1_select_multiply_immediate(width, quotient, quotient, divisor);
        }
        else
        {
            records[count++] = tier1_select_constant_move(width, other, divisor);
            records[count++] = tier1_select_register_record(tier1_select_binary_form(X86_OPERATION_IMUL, width), quotient, other);
        }
        records[count++] = (X86Instruction){ .rm = quotient, .form = width == 8 ? X86_FORM_NEG_RM64 : X86_FORM_NEG_RM32 };
        records[count++] = tier1_select_register_record(tier1_select_binary_form(X86_OPERATION_ADD, width), quotient, dividend);
    }
    if (holes->destination != quotient)
    {
        records[count++] = tier1_select_move(width, holes->destination, quotient);
    }
    return count;
}

// The records of a rule that takes x86's fixed registers (tier1_select_fixed_registers), into `records`, which has room
// for BUSTER_TIER1_SELECT_FIXED_RECORDS; none for any other rule. They read every operand before writing a fixed
// register, through the scratch register where they must, and write the destination last; their consumers save the other
// values that live past the node in those registers around them, in their call-save rows (tier1_emit_fixed). A division
// or remainder by a register (TIER1_RULE_DIVIDE): the divisor in the scratch register, the dividend in rax, its sign or
// zero through rdx, then div or idiv. A shift by a register (TIER1_RULE_SHIFT): the value in the scratch register, the
// count in cl. A division by a constant's reciprocal: tier1_select_reciprocal_records.
static uint32_t tier1_select_fixed(const Tier1SelectHoles* holes, X86Instruction* records)
{
    const uint32_t width = holes->width;
    const uint8_t scratch = BUSTER_TIER1_SELECT_SCRATCH;
    uint32_t count = 0;
    if (holes->rule == TIER1_RULE_DIVIDE)
    {
        const bool signed_divide = (holes->immediate & 1) != 0;
        const uint8_t result = (holes->immediate & 2) != 0 ? X86_REGISTER_RDX : X86_REGISTER_RAX;
        records[count++] = tier1_select_move(8, scratch, holes->right);
        if (holes->left != X86_REGISTER_RAX)
        {
            records[count++] = tier1_select_move(8, X86_REGISTER_RAX, holes->left);
        }
        records[count++] = signed_divide ? (X86Instruction){ .form = width == 8 ? X86_FORM_CQO : X86_FORM_CDQ } : tier1_select_register_record(X86_FORM_XOR_R32_RM32, X86_REGISTER_RDX, X86_REGISTER_RDX);
        records[count++] = (X86Instruction){ .rm = scratch, .form = width == 8 ? (signed_divide ? X86_FORM_IDIV_RM64 : X86_FORM_DIV_RM64) : (signed_divide ? X86_FORM_IDIV_RM32 : X86_FORM_DIV_RM32) };
        if (holes->destination != result)
        {
            records[count++] = tier1_select_move(width, holes->destination, result);
        }
    }
    else if (holes->rule == TIER1_RULE_SHIFT)
    {
        const uint16_t form = holes->operation == X86_OPERATION_SHL ? (width == 8 ? X86_FORM_SHL_RM64_CL : X86_FORM_SHL_RM32_CL) :
                              holes->operation == X86_OPERATION_SHR ? (width == 8 ? X86_FORM_SHR_RM64_CL : X86_FORM_SHR_RM32_CL) :
                                                                      (width == 8 ? X86_FORM_SAR_RM64_CL : X86_FORM_SAR_RM32_CL);
        records[count++] = tier1_select_move(8, scratch, holes->left);
        if (holes->right != X86_REGISTER_RCX)
        {
            records[count++] = tier1_select_move(4, X86_REGISTER_RCX, holes->right);
        }
        records[count++] = (X86Instruction){ .rm = scratch, .form = form };
        records[count++] = tier1_select_move(width, holes->destination, scratch);
    }
    else if (holes->rule == TIER1_RULE_DIVIDE_RECIPROCAL)
    {
        count = tier1_select_reciprocal_records(holes, records);
    }
    return count;
}

#define BUSTER_TIER1_SELECT_MAX_INSTRUCTIONS (1u << 20)
// The largest frame object, and the most aligned, the selector takes: the frame's offsets are 32-bit.
#define BUSTER_TIER1_SELECT_MAX_OBJECT_BYTES (UINT64_C(1) << 28)
#define BUSTER_TIER1_SELECT_MAX_OBJECT_ALIGNMENT 4096u
#define BUSTER_TIER1_SELECT_MAX_CANDIDATES 8
// The most links a pattern's walk follows through a truth value's chain or an address's (tier1_select_truth,
// tier1_select_address): a bound on each node's walk, which every node of a chain takes again as its own pattern's.
#define BUSTER_TIER1_SELECT_WALK 8

// Cold per-node emission metadata. The hot rule-choice pass uses contiguous 16-lane cost arrays; the emitter consumes
// all fields of one selected pattern. `values` preserve operand roles, including absent slots, while the effective-use
// CSR is the compact, directly consumable liveness handoff. CALL uses none of the three ordinary value slots: its
// ordered argument slice, including duplicates, is in the effective-use CSR, immediate is its symbol ID, and width
// is the result width (zero for void). Memory/conversion patterns keep logical byte widths, while the allocator and
// frame consumer use tier1_select_width's 4-byte canonical storage for I8/I16. Conversion source_width is the source's
// logical byte width, except canonical I1 zero-extension reads its 32-bit storage. A zero extension of a canonical
// I1, I8 or I16 value selects COPY instead, with canonical width and the narrow source width retained; COPY of a narrow
// source carries that width only when tier1_select_canonical certified it, and any other COPY leaves source_width zero.
// `fold_count` counts the nodes the pattern computes itself rather than reads (maximal munch's rank): which nodes the
// code leaves out follows from what the chosen patterns read (tier1_select_plan). For TIER1_RULE_ADDRESS,
// TIER1_RULE_LOAD, TIER1_RULE_LOAD_ADDRESS, TIER1_RULE_LOAD_OPERAND, TIER1_RULE_STORE, TIER1_RULE_STORE_ADDRESS and a
// mask's TIER1_RULE_MASK_LOAD_ADDRESS and TIER1_RULE_MASK_STORE_ADDRESS (#179), `stack_object` is BUSTER_IR_NONE, or the
// frame object the address walk reached, whose offset the consumer adds to the
// displacement at emission (tier1_emit_cfg.c): the base value is then BUSTER_IR_NONE, and no register is read for it.
// The original function stays immutable.
typedef struct Tier1SelectPattern Tier1SelectPattern;
struct Tier1SelectPattern
{
    uint64_t immediate;
    uint32_t values[3];
    int32_t displacement;
    uint32_t stack_object;
    uint8_t rule;
    uint8_t operation;
    uint8_t width;
    uint8_t source_width;
    uint8_t condition;
    uint8_t scale;
    uint8_t fold_count;
    uint8_t intrinsic_cost;
    bool compare_immediate;
    // A test's: no-ops the node's expansion writes before its rule's records, which take its sequence to its room and
    // past it (#989's review). Every pattern the selector makes has none.
    uint8_t nops;
};

typedef struct Tier1SelectPlan Tier1SelectPlan;
struct Tier1SelectPlan
{
    const IrFunction* function;
    Tier1SelectPattern* patterns;
    uint8_t* covered;
    uint32_t* effective_use_offsets;
    uint32_t* effective_use_values;
    uint32_t effective_use_count;
    uint32_t* costs;
    uint64_t estimated_cost;
    uint64_t arena_bytes;
    // The path that planned it, which the legacy emitter's choice of immediates' forms takes too.
    bool vector;
};

typedef struct Tier1SelectCode Tier1SelectCode;
struct Tier1SelectCode
{
    X86Instruction* records;
    uint8_t* cost_ids;
    uint32_t count;
    uint32_t* node_first;
    uint32_t* branches;
    uint32_t* targets;
    uint32_t branch_count;
    uint32_t unknown_costs;
    uint32_t error_node;
    uint64_t arena_bytes;
};

typedef struct Tier1SelectCandidates Tier1SelectCandidates;
struct Tier1SelectCandidates
{
    Tier1SelectPattern patterns[BUSTER_TIER1_SELECT_MAX_CANDIDATES];
    uint32_t costs[BUSTER_TIER1_SELECT_MAX_CANDIDATES];
    uint32_t count;
};

typedef struct Tier1SelectForest Tier1SelectForest;
struct Tier1SelectForest
{
    const IrFunction* function;
    const uint32_t* blocks;
    const uint32_t* uses;
    const uint32_t* costs;
    // Each node's last memory barrier before it, plus one (0 for none): a store, a call or a volatile access, which a
    // load isn't folded past (tier1_select_load_folds; tier1_select_barriers). The IR has no atomic access of its own:
    // C's atomic operations reach it as calls of the unit's leaves.
    const uint32_t* barriers;
    // Each narrow node's canonical-storage answer (tier1_select_canonical): its register holds zeros past its width.
    const uint8_t* canonical;
};

static bool tier1_select_barrier(const IrFunction* function, uint32_t node)
{
    const uint32_t op = function->op[node];
    return op == IR_STORE || op == IR_CALL || op == IR_CALL_INDIRECT || (op == IR_LOAD && (function->flags[node] & BUSTER_IR_VOLATILE) != 0);
}

// Each node's barriers entry (Tier1SelectForest.barriers), a running maximum of the barriers' positions plus one: the
// scalar twin of tier1_select_barriers. A pass of its own, apart from the plan's pass over the nodes, where the store
// would make the operands' walk reload the function's columns.
static void tier1_select_barriers_scalar(const IrFunction* function, uint32_t count, uint32_t* barriers)
{
    uint32_t barrier = 0;
    for (uint32_t node = 0; node < count; node += 1)
    {
        barriers[node] = barrier;
        barrier = tier1_select_barrier(function, node) ? node + 1 : barrier;
    }
}

// The same, 16 nodes at a time: their kinds and flags compared at once, each barrier's lane its position plus one, the
// running maximum by four shifts of the lanes and the vectors' before, and each node's entry its lane before's. The
// columns are read, and the entries written, with masked loads and stores.
static void tier1_select_barriers(const IrFunction* function, uint32_t count, uint32_t* barriers)
{
    const __m512i zero = _mm512_setzero_si512();
    const __m512i lanes = _mm512_setr_epi32(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16);
    __m512i carry = zero;
    for (uint32_t first = 0; first < count; first += 16)
    {
        const uint32_t left = count - first;
        const __mmask16 active = left >= 16 ? (__mmask16)0xffff : (__mmask16)((1u << left) - 1);
        const __m512i op = _mm512_cvtepu8_epi32(_mm_maskz_loadu_epi8(active, function->op + first));
        const __m512i flags = _mm512_cvtepu8_epi32(_mm_maskz_loadu_epi8(active, function->flags + first));
        const __mmask16 volatile_load = _mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(IR_LOAD)) & _mm512_test_epi32_mask(flags, _mm512_set1_epi32(BUSTER_IR_VOLATILE));
        const __mmask16 barrier = (_mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(IR_STORE)) | _mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(IR_CALL)) |
                                   _mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(IR_CALL_INDIRECT)) | volatile_load) & active;
        __m512i at = _mm512_maskz_add_epi32(barrier, lanes, _mm512_set1_epi32((int)first));
        at = _mm512_max_epu32(at, _mm512_alignr_epi32(at, zero, 15));
        at = _mm512_max_epu32(at, _mm512_alignr_epi32(at, zero, 14));
        at = _mm512_max_epu32(at, _mm512_alignr_epi32(at, zero, 12));
        at = _mm512_max_epu32(at, _mm512_alignr_epi32(at, zero, 8));
        at = _mm512_max_epu32(at, carry);
        _mm512_mask_storeu_epi32(barriers + first, active, _mm512_alignr_epi32(at, carry, 15));
        carry = _mm512_permutexvar_epi32(_mm512_set1_epi32(15), at);
    }
}

static uint32_t tier1_select_width(uint32_t type)
{
    // Narrow SSA values occupy a zero-extended 32-bit register/spill slot; signedness is explicit in IR_SEXT.
    return type == IR_I1 || type == IR_I8 || type == IR_I16 || type == IR_I32 ? 4 : type == IR_I64 || type == IR_PTR ? 8 : 0;
}

static uint32_t tier1_select_memory_width(uint32_t type)
{
    return type == IR_I8 ? 1 : type == IR_I16 ? 2 : type == IR_I32 ? 4 : type == IR_I64 || type == IR_PTR ? 8 : 0;
}

// A mask's bytes (#179): m8 to m64's 1 to 8, the width of the opmask instructions that take it; 0 for any other type.
static uint32_t tier1_select_mask_width(uint32_t type)
{
    return type == IR_M8 ? 1 : type == IR_M16 ? 2 : type == IR_M32 ? 4 : type == IR_M64 ? 8 : 0;
}

// Whether a function has vectors, which the selector doesn't take (#141): a vector type anywhere, its constants or its
// operations, in its instructions, its parameters, calls and slots. Masks (#179) it takes, as ir_function_has_simd
// doesn't. The instructions 64 at a time on the vector path, one at a time on its scalar twin.
static bool tier1_select_has_vectors(const IrFunction* function, bool vector)
{
    (void)vector;

    bool found = function->vector_constant_count != 0 || ir_vector_type(function->result_type);
    if (BUSTER_VECTOR_PATH(vector))
    {
        for (uint32_t first = 0; !found && first < function->instruction_count; first += 64)
        {
            const uint32_t count = function->instruction_count - first < 64 ? function->instruction_count - first : 64;
            const __mmask64 active = count == 64 ? UINT64_MAX : (UINT64_C(1) << count) - 1;
            const __m512i types = _mm512_maskz_loadu_epi8(active, function->type + first);
            const __m512i ops = _mm512_maskz_loadu_epi8(active, function->op + first);
            const __mmask64 vectors = _mm512_mask_cmpge_epu8_mask(active, types, _mm512_set1_epi8(IR_V4I32)) & ~_mm512_mask_cmpge_epu8_mask(active, types, _mm512_set1_epi8(IR_V16I32 + 1));
            const __mmask64 operations = _mm512_mask_cmpeq_epi8_mask(active, ops, _mm512_set1_epi8(IR_VCONST)) | _mm512_mask_cmpeq_epi8_mask(active, ops, _mm512_set1_epi8(IR_VCMP)) |
                                         _mm512_mask_cmpeq_epi8_mask(active, ops, _mm512_set1_epi8(IR_VSELECT));
            found = (vectors | operations) != 0;
        }
    }
    else
    {
        for (uint32_t i = 0; !found && i < function->instruction_count; i += 1)
        {
            found = ir_vector_type(function->type[i]) || function->op[i] == IR_VCONST || function->op[i] == IR_VCMP || function->op[i] == IR_VSELECT;
        }
    }
    for (uint32_t i = 0; !found && i < function->parameter_count; i += 1)
    {
        found = ir_vector_type(function->parameter_types[i]);
    }
    for (uint32_t i = 0; !found && i < function->call_type_count; i += 1)
    {
        found = ir_vector_type(function->call_types[i]);
    }
    for (uint32_t i = 0; !found && i < function->call_count; i += 1)
    {
        found = ir_vector_type(function->calls[i].result_type);
    }
    for (uint32_t i = 0; !found && i < function->slot_count; i += 1)
    {
        found = ir_vector_type(function->slots[i].type);
    }
    return found;
}

// The width a narrow source's copy reads: I1's canonical storage is its 32 bits, I8 one byte and I16 two. Zero says the
// type isn't narrow.
static uint32_t tier1_select_narrow_width(uint32_t type)
{
    return type == IR_I1 ? 4 : type == IR_I8 ? 1 : type == IR_I16 ? 2 : 0;
}

#define BUSTER_TIER1_SELECT_CANONICAL_PASSES 8

// A narrow SSA value is canonical in its 32-bit storage when every write to the register that holds it leaves the bits
// past its width zero. These producers write all 32 bits themselves:
//   IR_CONST, IR_UNDEF   the type's masked bits, in a 32-bit move (or zero)
//   IR_PARAM             a narrow parameter's entry MOVZX, before any read (tier1_emit_cfg.c; narrow parameters keep the
//                        function out of the straight-line consumer)
//   IR_LOAD              MOVZX_R32_RM8/RM16 (byte/word loads zero-extend)
//   IR_TRUNC             MOVZX_R32_RM8/RM16 (a truncation to I8/I16)
//   IR_SEXT              MOVSX, then the I16 MOVZX
//   IR_ICMP              setcc, then MOVZX_R32_RM8
//   IR_CALL/INDIRECT     a narrow result's MOVZX from RAX (tier1_emit.c)
// COPY, BITCAST, ZEXT, SELECT and PHI only move narrow values (MOV, CMOV, edge moves, spill/reload), so they are
// canonical when their narrow operands are. Any other producer is left noncanonical: a copy of it keeps its clearing
// move and a zero extension of it keeps its MOVZX, which is the safe answer for a rule this selector doesn't know yet.
// The passes are bounded; an unresolved phi chain stays noncanonical, also safely. The narrow fixtures' whole-register
// probes (tier1_narrow_test.c) check the supported producers natively.
static void tier1_select_canonical(const IrFunction* function, const uint32_t* blocks, uint8_t* canonical)
{
    const uint32_t count = function->instruction_count;
    for (uint32_t node = 0; node < count; node += 1)
    {
        canonical[node] = 0;
        if (tier1_select_narrow_width(function->type[node]) != 0)
        {
            switch (function->op[node])
            {
            case IR_CONST:
            case IR_UNDEF:
            case IR_PARAM:
            case IR_LOAD:
            case IR_TRUNC:
            case IR_SEXT:
            case IR_ICMP:
            case IR_CALL:
            case IR_CALL_INDIRECT:
                canonical[node] = 1;
                break;
            default:
                break;
            }
        }
    }
    for (uint32_t pass = 0; pass < BUSTER_TIER1_SELECT_CANONICAL_PASSES; pass += 1)
    {
        bool changed = false;
        for (uint32_t node = 0; node < count; node += 1)
        {
            if (canonical[node] == 0 && tier1_select_narrow_width(function->type[node]) != 0)
            {
                const uint32_t op = function->op[node];
                const uint32_t a = function->a[node];
                bool value = false;
                if (op == IR_COPY || op == IR_BITCAST || op == IR_ZEXT)
                {
                    value = a < count && canonical[a] != 0;
                }
                else if (op == IR_SELECT)
                {
                    value = function->b[node] < count && function->c[node] < count && canonical[function->b[node]] != 0 && canonical[function->c[node]] != 0;
                }
                else if (op == IR_PHI)
                {
                    value = true;
                    const uint32_t operand_count = (uint32_t)ir_operand_count(function, node);
                    for (uint32_t p = 0; p < operand_count; p += 1)
                    {
                        const uint32_t operand = ir_operand(function, node, p, blocks[node]).value;
                        value = value && operand < count && canonical[operand] != 0;
                    }
                }
                if (value)
                {
                    canonical[node] = 1;
                    changed = true;
                }
            }
        }
        if (!changed)
        {
            break;
        }
    }
}

// This selection boundary intentionally excludes extension/truncation ABI rules for narrow integers, floating-point
// and aggregates. The shared verifier checks descriptor bounds and actual argument types before this helper runs.
static bool tier1_select_call_supported(const IrFunction* function, uint32_t node)
{
    // A direct call, or one through a pointer, whose callee value the CFG consumer moves with the arguments.
    bool supported = node < function->instruction_count && (function->op[node] == IR_CALL || function->op[node] == IR_CALL_INDIRECT);
    if (supported)
    {
        const IrCall call = function->calls[function->b[node]];
        const uint32_t type = function->type[node];
        // A variadic call's unnamed arguments are promoted ones: its al says no vector register holds one (tier1_emit_call).
        // A narrow result is zero-extended after the call.
        const bool result = type == IR_VOID || type == IR_I1 || type == IR_I8 || type == IR_I16 || type == IR_I32 || type == IR_I64 || type == IR_PTR;
        supported = call.abi == 0 && call.argument_count <= BUSTER_TIER1_SELECT_CALL_ARGUMENTS &&
                    (call.variadic ? call.parameter_count <= call.argument_count : call.parameter_count == call.argument_count) && result;
        for (uint32_t argument = 0; supported && argument < call.argument_count; argument += 1)
        {
            const uint32_t argument_type = function->call_types[call.type_first + argument];
            supported = argument_type == IR_I32 || argument_type == IR_I64 || argument_type == IR_PTR;
        }
    }
    return supported;
}

static X86Operation tier1_select_operation(uint32_t op)
{
    X86Operation result = X86_OPERATION_NONE;
    switch (op)
    {
    case IR_ADD:
        result = X86_OPERATION_ADD;
        break;
    case IR_SUB:
        result = X86_OPERATION_SUB;
        break;
    case IR_MUL:
        result = X86_OPERATION_IMUL;
        break;
    case IR_AND:
        result = X86_OPERATION_AND;
        break;
    case IR_OR:
        result = X86_OPERATION_OR;
        break;
    case IR_XOR:
        result = X86_OPERATION_XOR;
        break;
    case IR_SHL:
        result = X86_OPERATION_SHL;
        break;
    case IR_LSHR:
        result = X86_OPERATION_SHR;
        break;
    case IR_ASHR:
        result = X86_OPERATION_SAR;
        break;
    case IR_NEG:
        result = X86_OPERATION_NEG;
        break;
    case IR_NOT:
        result = X86_OPERATION_NOT;
        break;
    }
    return result;
}

static bool tier1_select_tree_child(const Tier1SelectForest* forest, uint32_t parent, uint32_t value)
{
    return value != BUSTER_IR_NONE && value < parent && forest->uses[value] == 1 && forest->blocks[value] == forest->blocks[parent];
}

// A load moves into its consumer's memory operand: its only use, in its block, past nodes that neither write memory,
// call nor access a volatile object (no barrier between them), and itself not volatile.
static bool tier1_select_load_folds(const Tier1SelectForest* forest, uint32_t consumer, uint32_t load)
{
    const IrFunction* function = forest->function;
    return load != BUSTER_IR_NONE && function->op[load] == IR_LOAD && function->flags[load] == 0 && tier1_select_tree_child(forest, consumer, load) && forest->barriers[consumer] <= load;
}

static void tier1_select_fold(const Tier1SelectForest* forest, uint32_t parent, uint32_t value, Tier1SelectPattern* pattern)
{
    pattern->fold_count += tier1_select_tree_child(forest, parent, value) ? 1 : 0;
}

static void tier1_select_add_candidate(const Tier1SelectForest* forest, uint32_t node, Tier1SelectCandidates* candidates, Tier1SelectPattern pattern)
{
    assert(candidates->count < BUSTER_TIER1_SELECT_MAX_CANDIDATES);
    uint32_t cost = pattern.intrinsic_cost;
    for (uint32_t operand = 0; operand < 3; operand += 1)
    {
        const uint32_t value = pattern.values[operand];
        bool repeated = false;
        for (uint32_t previous = 0; previous < operand; previous += 1)
        {
            repeated |= pattern.values[previous] == value;
        }
        if (!repeated && tier1_select_tree_child(forest, node, value))
        {
            cost += forest->costs[value];
        }
    }
    candidates->patterns[candidates->count] = pattern;
    candidates->costs[candidates->count] = cost;
    candidates->count += 1;
}

static bool tier1_select_constant(const IrFunction* function, uint32_t value, uint64_t* bits)
{
    const bool result = value < function->instruction_count && function->op[value] == IR_CONST;
    if (result)
    {
        *bits = function->constants[function->a[value]].low;
    }
    return result;
}

// Each integer predicate's negation: !(a < b) is a >= b, and so on.
static const uint8_t tier1_select_negations[IR_PREDICATE_COUNT] = { IR_NE, IR_EQ, IR_GE, IR_GT, IR_LE, IR_LT, IR_UGE, IR_UGT, IR_ULE, IR_ULT };

// C's truth values reach the IR as integers: `a < b` is `zext (icmp lt a, b)`, which `if`, `!` and `&&` test against
// zero again, `icmp ne (zext x), 0` (`eq` for `!`), once for each operator the source nests. The walk follows such a
// chain back from `value`, an I1, to the value it tests, through links that only `root`'s pattern uses, at most
// BUSTER_TIER1_SELECT_WALK of them: each a zero extension, a zero and their comparison in consecutive nodes, right after
// the value it extends, so that nothing the pattern doesn't compute runs between the test and the root. It returns the
// node it stops at, counts the nodes it passed among the pattern's folds, and says whether an odd number of the links
// were `eq`, where the root's truth is the stopped node's negation.
static uint32_t tier1_select_truth(const Tier1SelectForest* forest, uint32_t root, uint32_t value, Tier1SelectPattern* pattern, bool* negated)
{
    const IrFunction* function = forest->function;
    bool odd = false;
    bool walking = true;
    for (uint32_t link = 0; walking && link < BUSTER_TIER1_SELECT_WALK; link += 1)
    {
        uint64_t zero = 1;
        const uint32_t extended = function->op[value] == IR_ICMP ? function->a[value] : BUSTER_IR_NONE;
        walking = extended != BUSTER_IR_NONE && function->c[value] <= IR_NE && extended + 2 == value && function->b[value] + 1 == value &&
                  tier1_select_constant(function, function->b[value], &zero) && zero == 0 && function->op[extended] == IR_ZEXT &&
                  function->type[function->a[extended]] == IR_I1 && function->a[extended] + 1 == extended && tier1_select_tree_child(forest, root, value) &&
                  tier1_select_tree_child(forest, root, extended) && tier1_select_tree_child(forest, root, function->b[value]);
        if (walking)
        {
            odd ^= function->c[value] == IR_EQ;
            pattern->fold_count += 3;
            value = function->a[extended];
        }
    }
    *negated = odd;
    return value;
}

// The comparison `tested`, an ICMP of 32- or 64-bit integers or pointers that only `root`'s pattern uses, as the pattern's
// own: its operands, their width, its predicate (with `negated`, its negation) and a constant right operand that fits as
// the immediate.
static bool tier1_select_comparison(const Tier1SelectForest* forest, uint32_t root, uint32_t tested, bool negated, Tier1SelectPattern* pattern)
{
    const IrFunction* function = forest->function;
    const uint32_t left = function->a[tested];
    const bool result = function->op[tested] == IR_ICMP && tier1_select_tree_child(forest, root, tested) &&
                        (function->type[left] == IR_I32 || function->type[left] == IR_I64 || function->type[left] == IR_PTR);
    if (result)
    {
        pattern->values[0] = left;
        pattern->values[1] = function->b[tested];
        pattern->width = (uint8_t)tier1_select_width(function->type[left]);
        pattern->condition = negated ? tier1_select_negations[function->c[tested]] : (uint8_t)function->c[tested];
        tier1_select_fold(forest, root, tested, pattern);
        uint64_t immediate = 0;
        if (tier1_select_constant(function, function->b[tested], &immediate) && tier1_select_immediate_fits(pattern->width, immediate))
        {
            pattern->values[1] = BUSTER_IR_NONE;
            pattern->immediate = immediate;
            pattern->compare_immediate = true;
            tier1_select_fold(forest, root, function->b[tested], pattern);
        }
    }
    return result;
}

// A frame object's address the selector takes: a lea from the frame's base (TIER1_RULE_STACK_ADDRESS), or folded into
// a user's memory operand (tier1_select_address), both filled at emission from the frame's layout. The frame's offsets
// are 32-bit, and an over-aligned object realigns it.
static bool tier1_select_stack_object(const IrFunction* function, uint32_t slot)
{
    return slot < function->slot_count && (function->slots[slot].flags & (BUSTER_IR_SLOT_REGISTER_SAVE_AREA | BUSTER_IR_SLOT_OVERFLOW_AREA)) == 0 &&
           function->slots[slot].size <= BUSTER_TIER1_SELECT_MAX_OBJECT_BYTES && function->slots[slot].alignment <= BUSTER_TIER1_SELECT_MAX_OBJECT_ALIGNMENT;
}

// A memory operand's address: base + index * 2^scale + displacement, the frame object the walk reached instead of a
// base value, and the nodes its walk passed.
typedef struct Tier1SelectAddress Tier1SelectAddress;
struct Tier1SelectAddress
{
    int64_t displacement;
    uint32_t base;
    uint32_t index;
    uint32_t object;
    uint8_t scale;
    uint8_t folds;
};

// An address's index without the constants added to it: through 64-bit additions of a constant (on either side) and
// subtractions of one that only `root`'s pattern uses, at most BUSTER_TIER1_SELECT_WALK of them, each constant times
// `scale` joining `*displacement` while it fits 32 bits, each node passed counted in `*folds` (#977's review: `&p[100 + i]`
// had its addition's lea). A 32-bit index's addition stays where a sign extension follows it: (i + c) * s isn't
// i * s + c * s when i + c overflows, which the IR doesn't rule out (and -fwrapv defines).
static uint32_t tier1_select_index(const Tier1SelectForest* forest, uint32_t root, uint32_t index, int64_t scale, int64_t* displacement, uint8_t* folds)
{
    const IrFunction* function = forest->function;
    bool walking = true;
    for (uint32_t step = 0; walking && step < BUSTER_TIER1_SELECT_WALK; step += 1)
    {
        const uint32_t op = function->op[index];
        uint64_t constant = 0;
        const bool right = (op == IR_ADD || op == IR_SUB) && tier1_select_constant(function, function->b[index], &constant);
        const bool left = op == IR_ADD && !right && tier1_select_constant(function, function->a[index], &constant);
        int64_t term = 0;
        int64_t sum = 0;
        walking = (right || left) && function->type[index] == IR_I64 && tier1_select_tree_child(forest, root, index) && !__builtin_mul_overflow((int64_t)constant, scale, &term) &&
                  (op == IR_SUB ? !__builtin_sub_overflow(*displacement, term, &sum) : !__builtin_add_overflow(*displacement, term, &sum)) && sum == (int64_t)(int32_t)sum;
        if (walking)
        {
            const uint32_t constant_node = right ? function->b[index] : function->a[index];
            *folds = (uint8_t)(*folds + 1 + (tier1_select_tree_child(forest, root, constant_node) ? 1 : 0));
            *displacement = sum;
            index = right ? function->a[index] : function->b[index];
        }
    }
    return index;
}

// The address continued from `address.base` through the nodes that only `root`'s pattern uses, at most
// BUSTER_TIER1_SELECT_WALK of them: the conversions between a pointer and its 64 bits that the typed stream's integers
// put around every address (`inttoptr`, `ptrtoint`), a 64-bit addition of a constant, and addresses, each taken whole
// while the operand has room for its index, a constant index joining the displacement, and while the displacement fits
// 32 bits. A frame object's address ends the walk: its offset joins the displacement at emission, from the frame's
// layout, with the frame's base register, so no value is read for it. The object may live in the entry block past the
// walk's block: the producer materializes every slot address there, which dominates every use. No narrowed address or
// speculative load is introduced: the walk stops at the base, the first node it can't take.
static Tier1SelectAddress tier1_select_address(const Tier1SelectForest* forest, uint32_t root, Tier1SelectAddress address)
{
    const IrFunction* function = forest->function;
    bool walking = address.object == BUSTER_IR_NONE;
    for (uint32_t step = 0; walking && step < BUSTER_TIER1_SELECT_WALK; step += 1)
    {
        const uint32_t node = address.base;
        const uint32_t op = function->op[node];
        const uint32_t a = function->a[node];
        const uint32_t b = function->b[node];
        Tier1SelectAddress next = address;
        uint64_t constant = 0;
        uint32_t constant_node = BUSTER_IR_NONE;
        uint8_t peeled = 0;
        bool taken = tier1_select_tree_child(forest, root, node);
        if (taken && op == IR_ADDRESS)
        {
            const IrAddress operand = function->addresses[function->c[node]];
            const bool constant_index = b != BUSTER_IR_NONE && tier1_select_constant(function, b, &constant);
            const bool indexed = b != BUSTER_IR_NONE && !constant_index;
            int64_t offset = 0;
            taken = (!indexed || address.index == BUSTER_IR_NONE) && !__builtin_mul_overflow((int64_t)constant, (int64_t)operand.scale, &offset) &&
                    !__builtin_add_overflow(offset, operand.offset, &offset) && !__builtin_add_overflow(address.displacement, offset, &next.displacement);
            next.index = indexed && taken ? tier1_select_index(forest, root, b, operand.scale, &next.displacement, &peeled) : address.index;
            next.scale = indexed ? (uint8_t)tier1_select_log2(operand.scale) : address.scale;
            next.base = a;
            constant_node = constant_index ? b : BUSTER_IR_NONE;
        }
        else if (taken && op == IR_ADD && function->type[node] == IR_I64)
        {
            const bool right = tier1_select_constant(function, b, &constant);
            const bool left = !right && tier1_select_constant(function, a, &constant);
            taken = (right || left) && !__builtin_add_overflow(address.displacement, (int64_t)constant, &next.displacement);
            next.base = right ? a : b;
            constant_node = right ? b : a;
        }
        else if (taken && op == IR_STACK_ADDR && tier1_select_stack_object(function, a))
        {
            next.object = a;
            next.base = BUSTER_IR_NONE;
        }
        else if (op == IR_STACK_ADDR && tier1_select_stack_object(function, a) && forest->blocks[node] == 0 && forest->uses[node] == 1 && node < root)
        {
            // The same past its block: the producer materializes every slot address in the entry block, which dominates
            // every use, and the address is pure with a function-global offset, so no value is read for it anywhere.
            // Longer cross-block chains stay unfolded.
            next.object = a;
            next.base = BUSTER_IR_NONE;
            taken = true;
        }
        else
        {
            taken = taken && ((op == IR_INTTOPTR && function->type[a] == IR_I64) || (op == IR_PTRTOINT && function->type[node] == IR_I64));
            next.base = a;
        }
        walking = taken && next.displacement == (int64_t)(int32_t)next.displacement;
        if (walking)
        {
            next.folds = (uint8_t)(address.folds + 1 + peeled + (constant_node != BUSTER_IR_NONE && tier1_select_tree_child(forest, root, constant_node) ? 1 : 0));
            address = next;
            walking = address.object == BUSTER_IR_NONE;
        }
    }
    return address;
}

// The memory operand of `node`'s access through `pointer`, in values[first] and values[first + 1] (its base and index),
// with its scale and displacement. Returns whether the walk took anything.
static bool tier1_select_memory(const Tier1SelectForest* forest, uint32_t node, uint32_t pointer, Tier1SelectPattern* pattern, uint32_t first)
{
    const Tier1SelectAddress address = tier1_select_address(forest, node, (Tier1SelectAddress){ .base = pointer, .index = BUSTER_IR_NONE, .object = BUSTER_IR_NONE });
    pattern->values[first] = address.base;
    pattern->values[first + 1] = address.index;
    pattern->stack_object = address.object;
    pattern->scale = address.scale;
    pattern->displacement = (int32_t)address.displacement;
    pattern->fold_count += address.folds;
    return address.folds != 0;
}

static Tier1SelectCandidates tier1_select_candidates(const Tier1SelectForest* forest, uint32_t node)
{
    const IrFunction* function = forest->function;
    const uint32_t op = function->op[node];
    const uint32_t a = function->a[node];
    const uint32_t b = function->b[node];
    const uint32_t c = function->c[node];
    const uint32_t type = function->type[node];
    const uint32_t width = tier1_select_width(type);
    Tier1SelectCandidates candidates = {0};
    Tier1SelectPattern base =
    {
        .values = {BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE},
        .stack_object = BUSTER_IR_NONE,
        .operation = (uint8_t)tier1_select_operation(op),
        .width = (uint8_t)width,
    };
    const bool integer = type == IR_I32 || type == IR_I64;
    // A mask's operations (#179): the stored mask's width for a store, the result's for the others.
    const uint32_t mask_width = tier1_select_mask_width(op == IR_STORE && b < function->instruction_count ? function->type[b] : type);
    if (mask_width != 0 && (op == IR_CONST || op == IR_UNDEF))
    {
        // An undefined mask is any mask: zero, kxor's.
        base.rule = TIER1_RULE_MASK_CONSTANT;
        base.width = (uint8_t)mask_width;
        base.immediate = op == IR_CONST ? function->constants[a].low & (mask_width < 8 ? (UINT64_C(1) << (mask_width * 8)) - 1 : UINT64_MAX) : 0;
        const uint64_t every = mask_width < 8 ? (UINT64_C(1) << (mask_width * 8)) - 1 : UINT64_MAX;
        base.intrinsic_cost = base.immediate == 0 || base.immediate == every ? 1 : 2;
        tier1_select_add_candidate(forest, node, &candidates, base);
    }
    else if (mask_width != 0 && op == IR_COPY)
    {
        base.rule = TIER1_RULE_MASK_COPY;
        base.width = (uint8_t)mask_width;
        base.values[0] = a;
        base.intrinsic_cost = 1;
        tier1_select_add_candidate(forest, node, &candidates, base);
    }
    else if (mask_width != 0 && (op == IR_AND || op == IR_OR || op == IR_XOR))
    {
        base.rule = TIER1_RULE_MASK_LOGIC;
        base.width = (uint8_t)mask_width;
        base.values[0] = a;
        base.values[1] = b;
        base.intrinsic_cost = 1;
        tier1_select_add_candidate(forest, node, &candidates, base);
    }
    else if (mask_width != 0 && op == IR_NOT)
    {
        base.rule = TIER1_RULE_MASK_NOT;
        base.width = (uint8_t)mask_width;
        base.values[0] = a;
        base.intrinsic_cost = 1;
        tier1_select_add_candidate(forest, node, &candidates, base);
    }
    else if (mask_width != 0 && (op == IR_LOAD || op == IR_STORE))
    {
        // kmov of the mask's width, from or to the address, folded into its memory operand as an integer's is.
        const bool store = op == IR_STORE;
        base.rule = store ? TIER1_RULE_MASK_STORE : TIER1_RULE_MASK_LOAD;
        base.width = (uint8_t)mask_width;
        base.values[0] = a;
        base.values[2] = store ? b : BUSTER_IR_NONE;
        base.intrinsic_cost = 1;
        Tier1SelectPattern folded = base;
        const bool walked = tier1_select_memory(forest, node, a, &folded, 0);
        // Through a frame object the folded address stands alone, as an integer's does.
        if (!walked || folded.stack_object == BUSTER_IR_NONE)
        {
            tier1_select_add_candidate(forest, node, &candidates, base);
        }
        if (walked)
        {
            folded.rule = store ? TIER1_RULE_MASK_STORE_ADDRESS : TIER1_RULE_MASK_LOAD_ADDRESS;
            tier1_select_add_candidate(forest, node, &candidates, folded);
        }
    }
    else if (mask_width != 0 && op == IR_PHI)
    {
        base.rule = TIER1_RULE_PHI;
        base.width = (uint8_t)mask_width;
        tier1_select_add_candidate(forest, node, &candidates, base);
    }
    else if (op == IR_CONST && width != 0)
    {
        base.rule = TIER1_RULE_CONSTANT;
        const uint32_t bits = ir_type_bits[type];
        base.immediate = function->constants[a].low & (bits < 64 ? (UINT64_C(1) << bits) - 1 : UINT64_MAX);
        base.intrinsic_cost = 1;
        tier1_select_add_candidate(forest, node, &candidates, base);
    }
    else if (op == IR_UNDEF && width != 0)
    {
        // Any value is an undefined one's: zero, a constant like any other.
        base.rule = TIER1_RULE_CONSTANT;
        base.immediate = 0;
        base.intrinsic_cost = 1;
        tier1_select_add_candidate(forest, node, &candidates, base);
    }
    else if (op == IR_PARAM && (integer || type == IR_PTR || type == IR_I1 || type == IR_I8 || type == IR_I16) && a < 6)
    {
        base.rule = TIER1_RULE_LEAF;
        tier1_select_add_candidate(forest, node, &candidates, base);
    }
    else if ((op == IR_COPY && width != 0) || (op == IR_INTTOPTR && function->type[a] == IR_I64) || (op == IR_PTRTOINT && type == IR_I64) || (op == IR_BITCAST && width != 0 && tier1_select_width(function->type[a]) == width && ir_type_bits[function->type[a]] == ir_type_bits[type]))
    {
        base.rule = TIER1_RULE_COPY;
        base.values[0] = a;
        // A copy of a narrow value can vanish in its own register only when the planner's canonical column certifies
        // that value's 32-bit storage (tier1_select_canonical: an entry MOVZX for narrow parameters, the result MOVZX
        // for narrow calls, and the producers' own full-width writes). An I32 source keeps its clearing move because a
        // parameter or call result may arrive with a dirty upper half.
        const uint8_t narrow_width = (uint8_t)tier1_select_narrow_width(function->type[a]);
        base.source_width = narrow_width != 0 && forest->canonical != 0 && forest->canonical[a] != 0 ? narrow_width : 0;
        base.intrinsic_cost = 1;
        // A pointer's 64 bits, or the pointer of 64 bits, from an address: the address's lea, or the copy of what the
        // conversions around it convert.
        const Tier1SelectAddress address = op == IR_INTTOPTR || op == IR_PTRTOINT ? tier1_select_address(forest, node, (Tier1SelectAddress){ .base = a, .index = BUSTER_IR_NONE, .object = BUSTER_IR_NONE })
                                                                                  : (Tier1SelectAddress){ .base = a, .index = BUSTER_IR_NONE, .object = BUSTER_IR_NONE };
        // Through a frame object the walked address stands alone: its base would tie it where the object is past the
        // walk's block (a cross-block read costs nothing), and the paths would cover the object apart.
        if (address.folds == 0 || address.object == BUSTER_IR_NONE)
        {
            tier1_select_add_candidate(forest, node, &candidates, base);
        }
        if (address.folds != 0)
        {
            Tier1SelectPattern walked = base;
            // Through a frame object the address is a lea from the frame's base, never a copy of no value.
            walked.rule = address.object != BUSTER_IR_NONE || address.index != BUSTER_IR_NONE || address.displacement != 0 ? TIER1_RULE_ADDRESS : TIER1_RULE_COPY;
            walked.values[0] = address.base;
            walked.values[1] = address.index;
            walked.stack_object = address.object;
            walked.scale = address.scale;
            walked.displacement = (int32_t)address.displacement;
            walked.fold_count = address.folds;
            walked.source_width = 0;
            tier1_select_add_candidate(forest, node, &candidates, walked);
        }
    }
    else if ((op == IR_TRUNC && (type == IR_I8 || type == IR_I16 || type == IR_I32) && tier1_select_memory_width(function->type[a]) != 0 && function->type[a] != IR_PTR) ||
             (op == IR_ZEXT && (type == IR_I8 || type == IR_I16 || integer) && (function->type[a] == IR_I1 || function->type[a] == IR_I8 || function->type[a] == IR_I16 || function->type[a] == IR_I32)) ||
             (op == IR_SEXT && (type == IR_I16 || integer) && (function->type[a] == IR_I8 || function->type[a] == IR_I16 || function->type[a] == IR_I32)))
    {
        // A zero extension of a canonical I1, I8 or I16 value is a copy: the source register already holds the
        // destination value with zeros past its width (the planner's canonical column, tier1_select_canonical), and a
        // 32-bit move zeroes the whole 64-bit register. This keeps the second movzx of issue #981's speedtest1 loop out
        // of the plan on both paths. The copy carries the narrow source width so the emitter can drop it in its own
        // register; an I32 source keeps its clearing move. For `zext(icmp)` the truth-value candidate below usually wins,
        // as #140 folds the comparison's own setcc/movzx; this copy covers a comparison the fold leaves alone.
        const uint8_t narrow_width = (uint8_t)tier1_select_narrow_width(function->type[a]);
        const bool narrow_copy = op == IR_ZEXT && narrow_width != 0 && forest->canonical != 0 && forest->canonical[a] != 0;
        base.rule = narrow_copy ? TIER1_RULE_COPY : op == IR_TRUNC ? TIER1_RULE_TRUNCATE : op == IR_ZEXT ? TIER1_RULE_ZERO_EXTEND : TIER1_RULE_SIGN_EXTEND;
        base.values[0] = a;
        if (narrow_copy)
        {
            base.source_width = narrow_width;
            base.intrinsic_cost = 1;
        }
        else
        {
            base.width = (uint8_t)tier1_select_memory_width(type);
            base.source_width = function->type[a] == IR_I1 ? 4 : (uint8_t)tier1_select_memory_width(function->type[a]);
            base.intrinsic_cost = op == IR_SEXT && type == IR_I16 ? 2 : 1;
        }
        tier1_select_add_candidate(forest, node, &candidates, base);
        // A truth value as an integer: the comparison it is, through C's truth values, right before the extension, its
        // setcc and movzx already the extension's 32 (or 64) bits.
        Tier1SelectPattern folded = base;
        bool negated = false;
        const uint32_t tested = op == IR_ZEXT && function->type[a] == IR_I1 && a + 1 == node ? tier1_select_truth(forest, node, a, &folded, &negated) : BUSTER_IR_NONE;
        if (tested != BUSTER_IR_NONE && tier1_select_comparison(forest, node, tested, negated, &folded))
        {
            folded.rule = folded.compare_immediate ? TIER1_RULE_COMPARE_IMMEDIATE : TIER1_RULE_COMPARE;
            folded.source_width = 0;
            folded.intrinsic_cost = 3;
            tier1_select_add_candidate(forest, node, &candidates, folded);
        }
    }
    else if ((op == IR_NEG || op == IR_NOT) && integer)
    {
        base.rule = TIER1_RULE_UNARY;
        base.values[0] = a;
        base.intrinsic_cost = 2;
        tier1_select_add_candidate(forest, node, &candidates, base);
    }
    else if ((op == IR_ADD || op == IR_SUB || op == IR_MUL || op == IR_AND || op == IR_OR || op == IR_XOR) && integer)
    {
        base.rule = TIER1_RULE_REGISTER;
        base.values[0] = a;
        base.values[1] = b;
        base.intrinsic_cost = 3;
        tier1_select_add_candidate(forest, node, &candidates, base);
        if (op == IR_ADD)
        {
            Tier1SelectPattern lea = base;
            lea.rule = TIER1_RULE_LEA_ADD;
            lea.intrinsic_cost = 1;
            tier1_select_add_candidate(forest, node, &candidates, lea);
        }
        uint32_t constant = b;
        uint32_t other = a;
        uint64_t immediate = 0;
        bool have_constant = tier1_select_constant(function, constant, &immediate);
        if (!have_constant && tier1_select_commutative((X86Operation)base.operation))
        {
            constant = a;
            other = b;
            have_constant = tier1_select_constant(function, constant, &immediate);
        }
        if (have_constant && tier1_select_immediate_fits(width, immediate))
        {
            Tier1SelectPattern folded = base;
            folded.rule = TIER1_RULE_IMMEDIATE;
            folded.values[0] = other;
            folded.values[1] = BUSTER_IR_NONE;
            folded.immediate = immediate;
            folded.intrinsic_cost = op == IR_MUL ? 1 : 2;
            tier1_select_fold(forest, node, constant, &folded);
            tier1_select_add_candidate(forest, node, &candidates, folded);
            if (op == IR_ADD)
            {
                folded.rule = TIER1_RULE_LEA_ADD;
                folded.displacement = (int32_t)immediate;
                folded.intrinsic_cost = 1;
                tier1_select_add_candidate(forest, node, &candidates, folded);
            }
        }
        if (op == IR_MUL && have_constant)
        {
            Tier1SelectPattern folded = base;
            folded.values[0] = other;
            folded.values[1] = BUSTER_IR_NONE;
            tier1_select_fold(forest, node, constant, &folded);
            const uint32_t shift = tier1_select_log2(immediate);
            if (shift < width * 8)
            {
                folded.rule = TIER1_RULE_MULTIPLY_SHIFT;
                folded.scale = (uint8_t)shift;
                folded.intrinsic_cost = 2;
                tier1_select_add_candidate(forest, node, &candidates, folded);
            }
            if (immediate == 3 || immediate == 5 || immediate == 9)
            {
                folded.rule = TIER1_RULE_MULTIPLY_LEA;
                folded.values[1] = other;
                folded.scale = (uint8_t)tier1_select_log2(immediate - 1);
                folded.intrinsic_cost = 1;
                tier1_select_add_candidate(forest, node, &candidates, folded);
            }
        }
        if (op == IR_ADD)
        {
            const uint32_t shift_node = function->op[b] == IR_SHL ? b : a;
            const uint32_t other_node = shift_node == b ? a : b;
            uint64_t shift = 0;
            if (function->op[shift_node] == IR_SHL && tier1_select_tree_child(forest, node, shift_node) && tier1_select_constant(function, function->b[shift_node], &shift) && shift <= 3)
            {
                Tier1SelectPattern folded = base;
                folded.rule = TIER1_RULE_LEA_SHIFT;
                folded.values[0] = other_node;
                folded.values[1] = function->a[shift_node];
                folded.scale = (uint8_t)shift;
                folded.intrinsic_cost = 1;
                tier1_select_fold(forest, node, shift_node, &folded);
                tier1_select_fold(forest, node, function->b[shift_node], &folded);
                tier1_select_add_candidate(forest, node, &candidates, folded);
            }
        }
        // The load the memory operand takes: the right operand's, or for a commutative operation the left's where the
        // right isn't a load. Trying the left after a right load that can't fold cost the scalar twin's plan 2.5% on its
        // shapes, its inlined code's registers and frame laid out otherwise, for a case code generators rarely make.
        const uint32_t load = function->op[b] == IR_LOAD ? b : tier1_select_commutative((X86Operation)base.operation) ? a : BUSTER_IR_NONE;
        if (tier1_select_load_folds(forest, node, load))
        {
            Tier1SelectPattern folded = base;
            folded.rule = TIER1_RULE_LOAD_OPERAND;
            folded.values[0] = load == b ? a : b;
            folded.values[1] = BUSTER_IR_NONE;
            folded.values[2] = BUSTER_IR_NONE;
            tier1_select_fold(forest, node, load, &folded);
            tier1_select_memory(forest, node, function->a[load], &folded, 1);
            folded.intrinsic_cost = 3;
            tier1_select_add_candidate(forest, node, &candidates, folded);
        }
    }
    else if ((op == IR_SHL || op == IR_LSHR || op == IR_ASHR) && integer)
    {
        uint64_t immediate = 0;
        if (tier1_select_constant(function, b, &immediate) && immediate < width * 8)
        {
            base.rule = TIER1_RULE_IMMEDIATE;
            base.values[0] = a;
            base.immediate = immediate;
            base.intrinsic_cost = 2;
            tier1_select_fold(forest, node, b, &base);
            tier1_select_add_candidate(forest, node, &candidates, base);
        }
        else
        {
            base.rule = TIER1_RULE_SHIFT;
            base.values[0] = a;
            base.values[1] = b;
            base.intrinsic_cost = 4;
            tier1_select_add_candidate(forest, node, &candidates, base);
        }
    }
    else if ((op == IR_SDIV || op == IR_UDIV || op == IR_SREM || op == IR_UREM) && integer)
    {
        base.rule = TIER1_RULE_DIVIDE;
        base.values[0] = a;
        base.values[1] = b;
        base.immediate = (op == IR_SDIV || op == IR_SREM ? 1u : 0u) | (op == IR_SREM || op == IR_UREM ? 2u : 0u);
        base.intrinsic_cost = 8;
        tier1_select_add_candidate(forest, node, &candidates, base);
        // By a power of two 2^k: unsigned, a shift right by k or a mask of k bits (a copy and zero for 2^0), and signed,
        // TIER1_RULE_DIVIDE_POWER's sequence, for a positive divisor (k at most the width's bits less 2).
        const uint32_t bits = width * 8;
        uint64_t divisor = 0;
        const bool constant = tier1_select_constant(function, b, &divisor);
        divisor &= bits < 64 ? (UINT64_C(1) << bits) - 1 : UINT64_MAX;
        const uint32_t shift = constant ? tier1_select_log2(divisor) : UINT32_MAX;
        const bool is_signed = op == IR_SDIV || op == IR_SREM;
        const bool remainder = op == IR_SREM || op == IR_UREM;
        Tier1SelectPattern folded = base;
        folded.values[1] = BUSTER_IR_NONE;
        tier1_select_fold(forest, node, b, &folded);
        const bool unsigned_power = shift < bits && (!is_signed || shift == 0);
        const bool signed_power = is_signed && shift >= 1 && shift <= bits - 2;
        const bool mask = (unsigned_power || signed_power) && tier1_select_immediate_fits(width, divisor - 1);
        if (unsigned_power && !remainder && shift == 0)
        {
            folded.rule = TIER1_RULE_COPY;
            folded.intrinsic_cost = 1;
            tier1_select_add_candidate(forest, node, &candidates, folded);
        }
        else if (unsigned_power && !remainder)
        {
            folded.rule = TIER1_RULE_IMMEDIATE;
            folded.operation = X86_OPERATION_SHR;
            folded.immediate = shift;
            folded.intrinsic_cost = 2;
            tier1_select_add_candidate(forest, node, &candidates, folded);
        }
        else if (unsigned_power && mask)
        {
            folded.rule = TIER1_RULE_IMMEDIATE;
            folded.operation = X86_OPERATION_AND;
            folded.immediate = divisor - 1;
            folded.intrinsic_cost = 2;
            tier1_select_add_candidate(forest, node, &candidates, folded);
        }
        else if (signed_power && (!remainder || mask))
        {
            folded.rule = TIER1_RULE_DIVIDE_POWER;
            folded.scale = (uint8_t)shift;
            folded.intrinsic_cost = 5;
            tier1_select_add_candidate(forest, node, &candidates, folded);
        }
        // By any other constant but those div and idiv keep (tier1_select_reciprocal_divisor): its reciprocal. Its cost is
        // below TIER1_RULE_DIVIDE's though its records may outnumber div's: div's latency, which a count of records
        // doesn't see, is the larger (docs/tier1-selection.md has the 9700X's numbers).
        else if (constant && tier1_select_reciprocal_divisor(divisor, width, is_signed))
        {
            folded.rule = TIER1_RULE_DIVIDE_RECIPROCAL;
            folded.immediate = divisor;
            folded.scale = (uint8_t)base.immediate;
            folded.intrinsic_cost = 7;
            tier1_select_add_candidate(forest, node, &candidates, folded);
        }
    }
    else if (op == IR_ADDRESS)
    {
        // The address's lea, its constant index in the displacement, through what its base's walk takes.
        const IrAddress operand = function->addresses[c];
        uint64_t constant = 0;
        const bool constant_index = b != BUSTER_IR_NONE && tier1_select_constant(function, b, &constant);
        int64_t displacement = 0;
        if (!__builtin_mul_overflow((int64_t)constant, (int64_t)operand.scale, &displacement) && !__builtin_add_overflow(displacement, operand.offset, &displacement) &&
            displacement == (int64_t)(int32_t)displacement)
        {
            Tier1SelectAddress start = {
                .displacement = displacement,
                .base = a,
                .index = constant_index ? BUSTER_IR_NONE : b,
                .object = BUSTER_IR_NONE,
                .scale = constant_index ? 0 : (uint8_t)tier1_select_log2(operand.scale),
            };
            start.index = start.index != BUSTER_IR_NONE ? tier1_select_index(forest, node, b, operand.scale, &start.displacement, &start.folds) : start.index;
            const Tier1SelectAddress address = tier1_select_address(forest, node, start);
            base.rule = TIER1_RULE_ADDRESS;
            base.values[0] = address.base;
            base.values[1] = address.index;
            base.stack_object = address.object;
            base.displacement = (int32_t)address.displacement;
            base.scale = address.scale;
            base.fold_count = address.folds;
            base.intrinsic_cost = 1;
            if (constant_index)
            {
                tier1_select_fold(forest, node, b, &base);
            }
            tier1_select_add_candidate(forest, node, &candidates, base);
        }
    }
    else if ((op == IR_LOAD && tier1_select_memory_width(type) != 0) || (op == IR_STORE && tier1_select_memory_width(function->type[b]) != 0))
    {
        const bool store = op == IR_STORE;
        base.rule = store ? TIER1_RULE_STORE : TIER1_RULE_LOAD;
        base.values[0] = a;
        base.values[2] = store ? b : BUSTER_IR_NONE;
        base.width = (uint8_t)tier1_select_memory_width(store ? function->type[b] : type);
        base.intrinsic_cost = 1;
        Tier1SelectPattern folded = base;
        const bool walked = tier1_select_memory(forest, node, a, &folded, 0);
        // Through a frame object the folded address stands alone, as above.
        if (!walked || folded.stack_object == BUSTER_IR_NONE)
        {
            tier1_select_add_candidate(forest, node, &candidates, base);
        }
        if (walked)
        {
            folded.rule = store ? TIER1_RULE_STORE_ADDRESS : TIER1_RULE_LOAD_ADDRESS;
            tier1_select_add_candidate(forest, node, &candidates, folded);
        }
    }
    else if (op == IR_ICMP && (function->type[a] == IR_I32 || function->type[a] == IR_I64 || function->type[a] == IR_PTR))
    {
        base.rule = TIER1_RULE_COMPARE;
        base.values[0] = a;
        base.values[1] = b;
        base.width = (uint8_t)tier1_select_width(function->type[a]);
        base.condition = (uint8_t)c;
        base.intrinsic_cost = 3;
        tier1_select_add_candidate(forest, node, &candidates, base);
        uint64_t immediate = 0;
        if (tier1_select_constant(function, b, &immediate) && tier1_select_immediate_fits(base.width, immediate))
        {
            base.rule = TIER1_RULE_COMPARE_IMMEDIATE;
            base.values[1] = BUSTER_IR_NONE;
            base.immediate = immediate;
            base.compare_immediate = true;
            tier1_select_fold(forest, node, b, &base);
            tier1_select_add_candidate(forest, node, &candidates, base);
        }
    }
    else if (op == IR_SELECT && width != 0)
    {
        base.rule = TIER1_RULE_SELECT;
        base.values[0] = a;
        base.values[1] = b;
        base.values[2] = c;
        base.intrinsic_cost = 3;
        tier1_select_add_candidate(forest, node, &candidates, base);
    }
    else if (op == IR_BR)
    {
        base.rule = TIER1_RULE_JUMP;
        base.intrinsic_cost = 1;
        tier1_select_add_candidate(forest, node, &candidates, base);
    }
    else if (op == IR_BR_INDIRECT)
    {
        base.rule = TIER1_RULE_JUMP_INDIRECT;
        base.values[0] = a;
        base.width = 8;
        base.intrinsic_cost = 1;
        tier1_select_add_candidate(forest, node, &candidates, base);
    }
    else if (op == IR_CBR)
    {
        base.rule = TIER1_RULE_BRANCH;
        base.values[0] = a;
        base.intrinsic_cost = 3;
        tier1_select_add_candidate(forest, node, &candidates, base);
        // The comparison the condition tests, through C's truth values, right before the branch: its jcc.
        Tier1SelectPattern folded = base;
        bool negated = false;
        const uint32_t tested = a + 1 == node ? tier1_select_truth(forest, node, a, &folded, &negated) : BUSTER_IR_NONE;
        if (tested != BUSTER_IR_NONE && tier1_select_comparison(forest, node, tested, negated, &folded))
        {
            folded.rule = TIER1_RULE_BRANCH_COMPARE;
            tier1_select_add_candidate(forest, node, &candidates, folded);
        }
    }
    else if ((op == IR_CALL || op == IR_CALL_INDIRECT) && tier1_select_call_supported(function, node))
    {
        base.rule = TIER1_RULE_CALL;
        base.immediate = a;
        // Structural lowering estimate: call plus possible argument/result copies; preservation is emitter-owned.
        base.intrinsic_cost = (uint8_t)(1 + function->calls[b].argument_count + (type != IR_VOID));
        tier1_select_add_candidate(forest, node, &candidates, base);
    }
    // A narrow result leaves in its canonical zero-extended 32 bits: a caller extends it by its own type, as clang's and
    // Tier 0's do, the psABI leaving the bits past its width unspecified.
    else if (op == IR_RET && (a == BUSTER_IR_NONE || function->type[a] == IR_I1 || function->type[a] == IR_I8 || function->type[a] == IR_I16 || function->type[a] == IR_I32 ||
                              function->type[a] == IR_I64 || function->type[a] == IR_PTR))
    {
        base.rule = TIER1_RULE_RETURN;
        base.values[0] = a;
        base.width = a == BUSTER_IR_NONE ? 0 : (uint8_t)tier1_select_width(function->type[a]);
        base.intrinsic_cost = a == BUSTER_IR_NONE ? 1 : 2;
        tier1_select_add_candidate(forest, node, &candidates, base);
    }
    else if (op == IR_UNREACHABLE)
    {
        base.rule = TIER1_RULE_UNREACHABLE;
        base.intrinsic_cost = 1;
        tier1_select_add_candidate(forest, node, &candidates, base);
    }
    else if (op == IR_PHI && width != 0)
    {
        base.rule = TIER1_RULE_PHI;
        tier1_select_add_candidate(forest, node, &candidates, base);
    }
    else if (op == IR_STACK_ADDR && tier1_select_stack_object(function, a))
    {
        base.rule = TIER1_RULE_STACK_ADDRESS;
        base.immediate = a;
        base.intrinsic_cost = 1;
        tier1_select_add_candidate(forest, node, &candidates, base);
    }
    else if (op == IR_SYMBOL_ADDR)
    {
        base.rule = TIER1_RULE_SYMBOL_ADDRESS;
        base.immediate = a;
        base.intrinsic_cost = 1;
        tier1_select_add_candidate(forest, node, &candidates, base);
    }
    return candidates;
}

// The scalar oracle is maximal munch: most covered nodes first. A switch fixes ties independently of the vector
// minimum-cost selection. Both share legality, not the choice of cheapest cover. In particular munch chooses a
// power-of-two multiply's shift before its three-operand immediate IMUL; the costed cover may choose the latter. A
// division by a register ranks below every rule for a constant divisor, which covers its constant even where the
// constant has other uses (and so doesn't count among the folds), as the costed cover always prefers them.
static uint32_t tier1_select_munch(const Tier1SelectCandidates* candidates)
{
    uint32_t chosen = 0;
    uint32_t priority = 0;
    for (uint32_t p = 0; p < candidates->count; p += 1)
    {
        const Tier1SelectPattern* pattern = &candidates->patterns[p];
        uint32_t rank = 0;
        switch (pattern->rule)
        {
        case TIER1_RULE_DIVIDE:
            rank = 0;
            break;
        case TIER1_RULE_MULTIPLY_SHIFT:
            rank = 9;
            break;
        case TIER1_RULE_LEA_SHIFT:
        case TIER1_RULE_BRANCH_COMPARE:
        case TIER1_RULE_LOAD_ADDRESS:
        case TIER1_RULE_STORE_ADDRESS:
        case TIER1_RULE_MASK_LOAD_ADDRESS:
        case TIER1_RULE_MASK_STORE_ADDRESS:
            rank = 8;
            break;
        case TIER1_RULE_LOAD_OPERAND:
            rank = 7;
            break;
        case TIER1_RULE_MULTIPLY_LEA:
            rank = 6;
            break;
        case TIER1_RULE_IMMEDIATE:
        case TIER1_RULE_COMPARE_IMMEDIATE:
            rank = 5;
            break;
        case TIER1_RULE_LEA_ADD:
            rank = 4;
            break;
        default:
            rank = 1;
            break;
        }
        rank += 16u * pattern->fold_count;
        if (rank > priority)
        {
            chosen = p;
            priority = rank;
        }
    }
    return chosen;
}

// Function/plan/scratch storage is disjoint. `dominance` is required for any cross-block value use: the structural IR
// verifier alone does not establish that such a definition reaches its use. No input-dependent recursion is used. With
// `verify` false, the caller has verified the IR with `dominance` (tier1_select_plan_verified).
static Tier1SelectError tier1_select_plan_checked(const IrFunction* function, const IrDominance* dominance, Arena* output, Arena* scratch, bool vector, bool verify, Tier1SelectPlan* result, uint32_t* error_node)
{
    (void)vector;

    Tier1SelectError error = TIER1_SELECT_OK;
    uint32_t failed_node = BUSTER_IR_NONE;
    const bool arguments = function != 0 && output != 0 && scratch != 0 && output != scratch && result != 0;
    const uint64_t output_mark = arguments ? output->length : 0;
    const uint64_t scratch_mark = arguments ? scratch->length : 0;
    const uint32_t count = function != 0 ? function->instruction_count : 0;
    Tier1SelectPlan plan = { .function = function, .vector = BUSTER_VECTOR_PATH(vector) };
    if (!arguments)
    {
        error = TIER1_SELECT_ERROR_INVALID;
    }
    else if (count > BUSTER_TIER1_SELECT_MAX_INSTRUCTIONS)
    {
        error = TIER1_SELECT_ERROR_LIMIT;
    }
    else
    {
        const IrCheck check = !verify ? ir_check(IR_OK, BUSTER_IR_NONE, 0) : BUSTER_VECTOR_PATH(vector) ? ir_verify(function, scratch, dominance) : ir_verify_scalar(function, scratch, dominance);
        if (check.error != IR_OK)
        {
            error = check.error == IR_ERROR_MEMORY ? TIER1_SELECT_ERROR_MEMORY : TIER1_SELECT_ERROR_INVALID;
            failed_node = check.instruction;
        }
        else if (tier1_select_has_vectors(function, vector))
        {
            error = TIER1_SELECT_ERROR_UNSUPPORTED;
        }
    }
    uint32_t* blocks = 0;
    uint32_t* uses = 0;
    uint32_t* barriers = 0;
    uint32_t* depths = 0;
    uint32_t* level_offsets = 0;
    uint32_t* level_nodes = 0;
    uint32_t* positions = 0;
    uint8_t* canonical = 0;
    if (error == TIER1_SELECT_OK)
    {
        bool ok = true;
        blocks = ir_allocate(scratch, count, 4, &ok);
        uses = ir_allocate(scratch, count, 4, &ok);
        barriers = ir_allocate(scratch, count, 4, &ok);
        depths = ir_allocate(scratch, count, 4, &ok);
        level_offsets = ir_allocate(scratch, (uint64_t)count + 2, 4, &ok);
        level_nodes = ir_allocate(scratch, count, 4, &ok);
        positions = ir_allocate(scratch, (uint64_t)count + 1, 4, &ok);
        canonical = ir_allocate(scratch, count, 1, &ok);
        plan.patterns = ir_allocate(output, count, sizeof(Tier1SelectPattern), &ok);
        plan.covered = ir_allocate(output, count, 1, &ok);
        plan.costs = ir_allocate(output, count, 4, &ok);
        plan.effective_use_offsets = ir_allocate(output, (uint64_t)count + 1, 4, &ok);
        if (!ok)
        {
            error = TIER1_SELECT_ERROR_MEMORY;
        }
    }
    uint32_t maximum_depth = 0;
    uint64_t effective_capacity = 0;
    // By-value aggregates' ABI records and a variadic definition's incoming frame (#131, #789) aren't selected yet. Labels
    // and br_indirect are: the CFG consumer defines the labels' symbols (tier1_emit_cfg.c).
    if (error == TIER1_SELECT_OK && (function->parameter_passed != 0 || function->variadic))
    {
        error = TIER1_SELECT_ERROR_UNSUPPORTED;
    }
    for (uint32_t block = 0; error == TIER1_SELECT_OK && block < function->block_count; block += 1)
    {
        for (uint32_t node = function->block_offsets[block]; node < function->block_offsets[block + 1]; node += 1)
        {
            blocks[node] = block;
        }
    }
    for (uint32_t node = 0; error == TIER1_SELECT_OK && node < count; node += 1)
    {
        const uint32_t op = function->op[node];
        if (op == IR_SWITCH || op == IR_ALLOCA || op == IR_STACK_SAVE || op == IR_STACK_RESTORE || ((op == IR_CALL || op == IR_CALL_INDIRECT) && !tier1_select_call_supported(function, node)))
        {
            error = TIER1_SELECT_ERROR_UNSUPPORTED;
        }
        // A parameter anywhere in the entry block: the straight-line consumer takes only a prefix of them, and gives the
        // others' functions to the CFG consumer, which defines every parameter at the function's start, before any
        // instruction (tier1_emit_cfg.c).
        if (op == IR_PARAM && blocks[node] != 0)
        {
            error = TIER1_SELECT_ERROR_UNSUPPORTED;
        }
        effective_capacity += (op == IR_CALL || op == IR_CALL_INDIRECT) && error == TIER1_SELECT_OK ? function->calls[function->b[node]].argument_count + 1 : 3;
        const uint32_t operand_count = error == TIER1_SELECT_OK ? (uint32_t)ir_operand_count(function, node) : 0;
        // A phi's operand reaches it along an edge: it may follow the phi (a loop's back edge), and its use counts, but
        // nothing folds into a phi, so its depth doesn't order the phi's choice. Its own loop leaves the others' as it was.
        for (uint32_t p = 0; error == TIER1_SELECT_OK && op == IR_PHI && p < operand_count; p += 1)
        {
            const IrOperand operand = ir_operand(function, node, p, blocks[node]);
            if (operand.value >= count || (blocks[operand.value] != blocks[node] && dominance == 0))
            {
                error = TIER1_SELECT_ERROR_INVALID;
            }
            else
            {
                uses[operand.value] += 1;
            }
        }
        for (uint32_t p = 0; error == TIER1_SELECT_OK && op != IR_PHI && p < operand_count; p += 1)
        {
            const IrOperand operand = ir_operand(function, node, p, blocks[node]);
            // A value of another block, whose definition the dominance shows reaches its use, may follow it in the
            // instructions' order: a computed goto's dispatch block's phis, which the lowering puts last, read in the
            // labels' blocks (#789). Only a tree child, before its consumer in its block, orders the node's choice.
            const bool across = operand.value < count && blocks[operand.value] != blocks[node];
            if (operand.value >= count || (across ? dominance == 0 : operand.value >= node))
            {
                error = TIER1_SELECT_ERROR_INVALID;
            }
            else
            {
                uses[operand.value] += 1;
                const uint32_t next = operand.value < node ? depths[operand.value] + 1 : 0;
                depths[node] = next > depths[node] ? next : depths[node];
            }
        }
        maximum_depth = depths[node] > maximum_depth ? depths[node] : maximum_depth;
        level_offsets[depths[node] + 1] += 1;
        if (error != TIER1_SELECT_OK)
        {
            failed_node = node;
        }
    }
    if (error == TIER1_SELECT_OK && BUSTER_VECTOR_PATH(vector))
    {
        tier1_select_barriers(function, count, barriers);
    }
    else if (error == TIER1_SELECT_OK)
    {
        tier1_select_barriers_scalar(function, count, barriers);
    }
    if (error == TIER1_SELECT_OK)
    {
        tier1_select_canonical(function, blocks, canonical);
    }
    if (error == TIER1_SELECT_OK)
    {
        bool ok = effective_capacity <= UINT32_MAX;
        if (!ok)
        {
            error = TIER1_SELECT_ERROR_LIMIT;
        }
        else
        {
            plan.effective_use_values = ir_allocate(output, effective_capacity, 4, &ok);
            error = ok ? TIER1_SELECT_OK : TIER1_SELECT_ERROR_MEMORY;
        }
    }
    if (error == TIER1_SELECT_OK)
    {
        for (uint32_t level = 0; level <= maximum_depth; level += 1)
        {
            level_offsets[level + 1] += level_offsets[level];
            positions[level] = level_offsets[level];
        }
        for (uint32_t node = 0; node < count; node += 1)
        {
            level_nodes[positions[depths[node]]++] = node;
        }
    }
    const Tier1SelectForest forest =
    {
        .function = function,
        .blocks = blocks,
        .uses = uses,
        .costs = plan.costs,
        .barriers = barriers,
        .canonical = canonical,
    };
    for (uint32_t level = 0; error == TIER1_SELECT_OK && level <= maximum_depth; level += 1)
    {
        for (uint32_t first = level_offsets[level]; error == TIER1_SELECT_OK && first < level_offsets[level + 1]; first += BUSTER_TIER1_SELECT_LANES)
        {
            const uint32_t remaining = level_offsets[level + 1] - first;
            const uint32_t lanes = remaining < BUSTER_TIER1_SELECT_LANES ? remaining : BUSTER_TIER1_SELECT_LANES;
            Tier1SelectCandidates batch[BUSTER_TIER1_SELECT_LANES];
            uint32_t chosen[BUSTER_TIER1_SELECT_LANES] = {0};
            uint32_t cost_columns[BUSTER_TIER1_SELECT_MAX_CANDIDATES][BUSTER_TIER1_SELECT_LANES];
            memset(cost_columns, 0xff, sizeof(cost_columns));
            for (uint32_t lane = 0; error == TIER1_SELECT_OK && lane < lanes; lane += 1)
            {
                const uint32_t node = level_nodes[first + lane];
                batch[lane] = tier1_select_candidates(&forest, node);
                if (batch[lane].count == 0)
                {
                    error = TIER1_SELECT_ERROR_UNSUPPORTED;
                    failed_node = node;
                }
                else
                {
                    for (uint32_t p = 0; p < batch[lane].count; p += 1)
                    {
                        cost_columns[p][lane] = batch[lane].costs[p];
                    }
                    if (!BUSTER_VECTOR_PATH(vector))
                    {
                        chosen[lane] = tier1_select_munch(&batch[lane]);
                    }
                }
            }
            if (error == TIER1_SELECT_OK && BUSTER_VECTOR_PATH(vector))
            {
                __m512i best_cost = _mm512_set1_epi32(-1);
                __m512i best_rule = _mm512_setzero_si512();
                for (uint32_t p = 0; p < BUSTER_TIER1_SELECT_MAX_CANDIDATES; p += 1)
                {
                    const __m512i candidate_cost = _mm512_loadu_si512(cost_columns[p]);
                    const __mmask16 better = _mm512_cmplt_epu32_mask(candidate_cost, best_cost);
                    best_cost = _mm512_mask_mov_epi32(best_cost, better, candidate_cost);
                    best_rule = _mm512_mask_set1_epi32(best_rule, better, (int)p);
                }
                const __mmask16 active = (__mmask16)((1u << lanes) - 1);
                _mm512_mask_storeu_epi32(chosen, active, best_rule);
            }
            for (uint32_t lane = 0; error == TIER1_SELECT_OK && lane < lanes; lane += 1)
            {
                const uint32_t node = level_nodes[first + lane];
                plan.patterns[node] = batch[lane].patterns[chosen[lane]];
                plan.costs[node] = batch[lane].costs[chosen[lane]];
            }
        }
    }
    if (error == TIER1_SELECT_OK)
    {
        // What the code computes, from the last node back: a node with an effect (a store, a call, a terminator, a
        // volatile load), a parameter, a phi and its operands, which its edges' moves read, a node nothing uses, which the
        // optimizer kept for a fault it may raise (a load or a division), and each value a computed node's pattern reads.
        // Every other node is covered: its users' patterns compute it themselves, a constant as an immediate, a load as a
        // memory operand, an address as a displacement, a truth value's chain as a branch's comparison, however deep their
        // walks went. A non-phi node's values precede it, so each is final when the walk back reaches it.
        uint8_t* needed = plan.covered;
        for (uint32_t node = 0; node < count; node += 1)
        {
            const uint32_t operand_count = function->op[node] == IR_PHI ? (uint32_t)ir_operand_count(function, node) : 0;
            needed[node] |= function->op[node] == IR_PHI ? 1 : 0;
            for (uint32_t p = 0; p < operand_count; p += 1)
            {
                needed[ir_operand(function, node, p, blocks[node]).value] = 1;
            }
        }
        for (uint32_t reverse = count; reverse != 0; reverse -= 1)
        {
            const uint32_t node = reverse - 1;
            const uint32_t op = function->op[node];
            needed[node] |= tier1_select_barrier(function, node) || op == IR_BR || op == IR_CBR || op == IR_BR_INDIRECT || op == IR_RET || op == IR_UNREACHABLE || op == IR_PARAM || uses[node] == 0 ? 1 : 0;
            const Tier1SelectPattern* pattern = &plan.patterns[node];
            if (needed[node] != 0 && pattern->rule == TIER1_RULE_CALL)
            {
                const IrCall call = function->calls[function->b[node]];
                for (uint32_t argument = 0; argument < call.argument_count; argument += 1)
                {
                    needed[function->extra[call.argument_first + argument]] = 1;
                }
                if (op == IR_CALL_INDIRECT)
                {
                    needed[function->a[node]] = 1;
                }
            }
            for (uint32_t operand = 0; needed[node] != 0 && pattern->rule != TIER1_RULE_CALL && operand < 3; operand += 1)
            {
                if (pattern->values[operand] != BUSTER_IR_NONE)
                {
                    needed[pattern->values[operand]] = 1;
                }
            }
        }
        for (uint32_t node = 0; node < count; node += 1)
        {
            plan.covered[node] = needed[node] ^ 1;
        }
        for (uint32_t node = 0; node < count; node += 1)
        {
            plan.effective_use_offsets[node] = plan.effective_use_count;
            if (plan.covered[node] == 0)
            {
                const Tier1SelectPattern* pattern = &plan.patterns[node];
                plan.estimated_cost += pattern->intrinsic_cost;
                if (pattern->rule == TIER1_RULE_CALL)
                {
                    const IrCall call = function->calls[function->b[node]];
                    for (uint32_t argument = 0; argument < call.argument_count; argument += 1)
                    {
                        plan.effective_use_values[plan.effective_use_count++] = function->extra[call.argument_first + argument];
                    }
                    // An indirect call's callee after its arguments.
                    if (function->op[node] == IR_CALL_INDIRECT)
                    {
                        plan.effective_use_values[plan.effective_use_count++] = function->a[node];
                    }
                }
                else
                {
                    for (uint32_t operand = 0; operand < 3; operand += 1)
                    {
                        if (pattern->values[operand] != BUSTER_IR_NONE)
                        {
                            plan.effective_use_values[plan.effective_use_count++] = pattern->values[operand];
                        }
                    }
                }
            }
        }
        plan.effective_use_offsets[count] = plan.effective_use_count;
        plan.arena_bytes = output->length - output_mark;
        *result = plan;
    }
    if (arguments)
    {
        arena_pop_to(scratch, scratch_mark);
        if (error != TIER1_SELECT_OK)
        {
            arena_pop_to(output, output_mark);
        }
    }
    if (error_node != 0)
    {
        *error_node = failed_node;
    }
    return error;
}

// The plan of a function that hasn't been verified: ir_verify (or ir_verify_scalar on the scalar path) with `dominance`
// first, which refuses what the plan can't read.
static Tier1SelectError tier1_select_plan(const IrFunction* function, const IrDominance* dominance, Arena* output, Arena* scratch, bool vector, Tier1SelectPlan* result, uint32_t* error_node)
{
    return tier1_select_plan_checked(function, dominance, output, scratch, vector, true, result, error_node);
}

// The plan of a function its caller has verified with `dominance`, as ir_optimize verifies its result: the same plan as
// tier1_select_plan's, without verifying it again. On the frozen sqlite3.c, the verification was a quarter of selection's
// cycles (#141). Unverified IR here may be read outside its columns.
static Tier1SelectError tier1_select_plan_verified(const IrFunction* function, const IrDominance* dominance, Arena* output, Arena* scratch, bool vector, Tier1SelectPlan* result, uint32_t* error_node)
{
    return tier1_select_plan_checked(function, dominance, output, scratch, vector, false, result, error_node);
}

static X86Instruction tier1_select_memory_holes(const Tier1SelectPattern* pattern, const uint8_t* registers, uint32_t first)
{
    const uint32_t index = pattern->values[first + 1];
    const X86Instruction result =
    {
        .displacement = pattern->displacement,
        // A frame object's base is the frame's base register, which the consumer sets at emission with the object's
        // offset (tier1_emit_cfg.c): until then the stack pointer's, as the object's lea's is.
        .rm = pattern->stack_object != BUSTER_IR_NONE ? X86_REGISTER_RSP : registers[pattern->values[first]],
        .index = index == BUSTER_IR_NONE ? 0 : registers[index],
        .mode = index == BUSTER_IR_NONE ? BUSTER_X86_MEMORY_BASE : (uint8_t)BUSTER_X86_MEMORY_BASE_INDEX(pattern->scale),
    };
    return result;
}

static Tier1SelectHoles tier1_select_holes(const Tier1SelectPlan* plan, const uint8_t* registers, uint32_t node)
{
    const Tier1SelectPattern* pattern = &plan->patterns[node];
    Tier1SelectHoles holes =
    {
        .rule = (Tier1SelectRule)pattern->rule,
        .operation = (X86Operation)pattern->operation,
        .immediate = pattern->immediate,
        .width = pattern->width,
        .source_width = pattern->source_width,
        .destination = plan->function->type[node] == IR_VOID ? 0 : registers[node],
        .left = pattern->values[0] == BUSTER_IR_NONE ? 0 : registers[pattern->values[0]],
        .right = pattern->values[1] == BUSTER_IR_NONE ? 0 : registers[pattern->values[1]],
        .third = pattern->values[2] == BUSTER_IR_NONE ? 0 : registers[pattern->values[2]],
        .condition = pattern->condition,
        .scale = pattern->scale,
        .compare_immediate = pattern->compare_immediate,
        .nops = pattern->nops,
    };
    switch (pattern->rule)
    {
    case TIER1_RULE_LEA_ADD:
    case TIER1_RULE_LEA_SHIFT:
    case TIER1_RULE_MULTIPLY_LEA:
    case TIER1_RULE_ADDRESS:
    case TIER1_RULE_LOAD:
    case TIER1_RULE_LOAD_ADDRESS:
    case TIER1_RULE_STORE:
    case TIER1_RULE_STORE_ADDRESS:
    case TIER1_RULE_MASK_LOAD:
    case TIER1_RULE_MASK_LOAD_ADDRESS:
    case TIER1_RULE_MASK_STORE:
    case TIER1_RULE_MASK_STORE_ADDRESS:
        holes.memory = tier1_select_memory_holes(pattern, registers, 0);
        if (pattern->rule == TIER1_RULE_STORE || pattern->rule == TIER1_RULE_STORE_ADDRESS || pattern->rule == TIER1_RULE_MASK_STORE || pattern->rule == TIER1_RULE_MASK_STORE_ADDRESS)
        {
            holes.right = holes.third;
        }
        break;
    case TIER1_RULE_LOAD_OPERAND:
        holes.memory = tier1_select_memory_holes(pattern, registers, 1);
        break;
    default:
        break;
    }
    return holes;
}

// A leaf body's incoming System V integer arguments. Register assignment must pin these values from function entry;
// an IR_PARAM does not emit a late copy from a register that earlier instructions may already have overwritten.
static const uint8_t tier1_select_parameter_registers[6] =
{
    X86_REGISTER_RDI,
    X86_REGISTER_RSI,
    X86_REGISTER_RDX,
    X86_REGISTER_RCX,
    X86_REGISTER_R8,
    X86_REGISTER_R9,
};

// The plan and allocation are validated internal compiler products. All records receive the same independent target,
// operand and immediate checks; a failure is an error, never another optimization mode. This body does not spill,
// make calls, save callee-saved registers, or attach symbolic relocations.
static Tier1SelectError tier1_select_emit(const Tier1SelectPlan* plan, const uint8_t* registers, const uint8_t* allowed_forms, Arena* output, Tier1SelectCode* result, uint32_t* error_node)
{
    const bool arguments = plan != 0 && plan->function != 0 && allowed_forms != 0 && output != 0 && result != 0 && (plan->function->instruction_count == 0 || registers != 0);
    Tier1SelectError error = arguments ? TIER1_SELECT_OK : TIER1_SELECT_ERROR_INVALID;
    const uint64_t mark = arguments ? output->length : 0;
    const uint32_t count = arguments ? plan->function->instruction_count : 0;
    uint32_t failed_node = BUSTER_IR_NONE;
    Tier1SelectCode code = {0};
    if (arguments && count > BUSTER_TIER1_SELECT_MAX_INSTRUCTIONS)
    {
        error = TIER1_SELECT_ERROR_LIMIT;
    }
    // This legacy consumer has neither ABI argument moves nor call-clobber preservation or symbolic fixups, nor the
    // edge moves a phi needs, nor the saves around a fixed-register sequence's (tier1_select_fixed). Reject the complete
    // plan before allocating records, even if earlier value locations are spilled. A memory operand through a frame
    // object needs the frame's layout too, and an indirect jump's labels their symbols: the CFG consumer's.
    if (error == TIER1_SELECT_OK && plan->function->label_count != 0)
    {
        error = TIER1_SELECT_ERROR_UNSUPPORTED;
    }
    for (uint32_t node = 0; error == TIER1_SELECT_OK && node < count; node += 1)
    {
        if (plan->function->op[node] == IR_CALL || plan->patterns[node].rule == TIER1_RULE_CALL || plan->patterns[node].rule == TIER1_RULE_PHI ||
            plan->patterns[node].rule == TIER1_RULE_STACK_ADDRESS || plan->patterns[node].rule == TIER1_RULE_SYMBOL_ADDRESS || tier1_select_fixed_registers(plan->patterns[node].rule) != 0 ||
            tier1_select_mask_rule(plan->patterns[node].rule) || plan->patterns[node].stack_object != BUSTER_IR_NONE || plan->patterns[node].rule == TIER1_RULE_JUMP_INDIRECT)
        {
            error = TIER1_SELECT_ERROR_UNSUPPORTED;
            failed_node = node;
        }
    }
    Tier1SelectRequests requests = { .vector = plan != 0 && plan->vector };
    if (error == TIER1_SELECT_OK)
    {
        bool ok = true;
        code.records = ir_allocate(output, (uint64_t)count * BUSTER_TIER1_SELECT_RECORDS_PER_NODE, sizeof(X86Instruction), &ok);
        code.cost_ids = ir_allocate(output, (uint64_t)count * BUSTER_TIER1_SELECT_RECORDS_PER_NODE, 1, &ok);
        code.node_first = ir_allocate(output, (uint64_t)count + 1, 4, &ok);
        code.branches = ir_allocate(output, (uint64_t)plan->function->block_count * 2, 4, &ok);
        code.targets = ir_allocate(output, (uint64_t)plan->function->block_count * 2, 4, &ok);
        if (!ok)
        {
            error = TIER1_SELECT_ERROR_MEMORY;
        }
    }
    // Each node's records go in as selected, their immediates' requests with them; a node whose registers allocation
    // didn't give stops the loop.
    uint32_t emitted = 0;
    for (uint32_t node = 0; error == TIER1_SELECT_OK && node < count; node += 1)
    {
        code.node_first[node] = code.count;
        if (plan->covered[node] == 0)
        {
            const IrFunction* function = plan->function;
            const Tier1SelectPattern* pattern = &plan->patterns[node];
            bool allocated = function->type[node] == IR_VOID || tier1_select_register_allowed(registers[node]);
            for (uint32_t p = 0; p < 3; p += 1)
            {
                const uint32_t value = pattern->values[p];
                allocated &= value == BUSTER_IR_NONE || tier1_select_register_allowed(registers[value]);
            }
            if (function->op[node] == IR_PARAM)
            {
                allocated &= function->a[node] < 6 && registers[node] == tier1_select_parameter_registers[function->a[node]];
            }
            if (!allocated)
            {
                error = TIER1_SELECT_ERROR_ALLOCATION;
                failed_node = node;
            }
            else
            {
                const Tier1SelectHoles holes = tier1_select_holes(plan, registers, node);
                const Tier1SelectRecords selected = tier1_select_expand(&holes);
                memcpy(code.records + code.count, selected.records, selected.count * sizeof(X86Instruction));
                tier1_select_requests_add(&requests, &selected, code.count, code.records);
                code.count += selected.count;
                if (selected.overflowed)
                {
                    error = TIER1_SELECT_ERROR_INVALID;
                    failed_node = node;
                }
                else if (function->op[node] == IR_BR)
                {
                    code.branches[code.branch_count] = code.count - 1;
                    code.targets[code.branch_count++] = function->block_offsets[function->a[node]];
                }
                else if (function->op[node] == IR_CBR)
                {
                    code.branches[code.branch_count] = code.count - 2;
                    code.targets[code.branch_count++] = function->block_offsets[function->b[node]];
                    code.branches[code.branch_count] = code.count - 1;
                    code.targets[code.branch_count++] = function->block_offsets[function->c[node]];
                }
            }
        }
        emitted = error == TIER1_SELECT_OK ? node + 1 : node;
    }
    // The records' immediates' forms, chosen for the function in batches, then every record checked in order: an
    // invalid record or a form the target doesn't take is its node's failure, before a later node's allocation's, as
    // when each node's records were checked before the next node was selected.
    if (error == TIER1_SELECT_OK || error == TIER1_SELECT_ERROR_ALLOCATION)
    {
        tier1_select_requests_flush(&requests, code.records);
        code.node_first[emitted] = code.count;
        const uint32_t valid = (uint32_t)x86_validate(code.records, code.count);
        uint32_t r = 0;
        while (r < valid && x86_target_form_allowed(allowed_forms, code.records[r].form))
        {
            code.cost_ids[r] = x86_zen5_id(&code.records[r]);
            code.unknown_costs += code.cost_ids[r] == 0;
            r += 1;
        }
        if (r < code.count)
        {
            uint32_t node = 0;
            while (code.node_first[node + 1] <= r)
            {
                node += 1;
            }
            error = r < valid ? TIER1_SELECT_ERROR_TARGET : TIER1_SELECT_ERROR_INVALID;
            failed_node = node;
        }
    }
    if (error == TIER1_SELECT_OK)
    {
        code.node_first[count] = code.count;
        for (uint32_t branch = 0; branch < code.branch_count; branch += 1)
        {
            code.targets[branch] = code.node_first[code.targets[branch]];
        }
        code.error_node = BUSTER_IR_NONE;
        code.arena_bytes = output->length - mark;
        *result = code;
    }
    else if (arguments)
    {
        arena_pop_to(output, mark);
    }
    if (error_node != 0)
    {
        *error_node = failed_node;
    }
    return error;
}
