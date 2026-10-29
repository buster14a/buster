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
    TIER1_RULE_COUNT,
} Tier1SelectRule;

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
};

// A record whose operation, on the destination register, has an immediate, as a request. The semantics module owns sign
// extension, zero idioms, immediate widths and the flags proof for each alternate form. An operation without a row
// leaves the record empty, which validation refuses. The target's allow-list is checked on the final records,
// independently of the host's ISA.
static void tier1_select_request(Tier1SelectRecords* result, X86Operation operation, uint32_t width, uint8_t destination, int64_t value, uint8_t live_flags)
{
    const uint32_t row = x86_choice_row(operation, width);
    if (row != BUSTER_X86_CHOICE_NO_ROW)
    {
        result->requested = true;
        result->request = result->count;
        result->destination = destination;
        result->row = (uint16_t)row;
        result->value = value;
        result->context = (uint16_t)(BUSTER_X86_CONTEXT_REGISTER | (destination == X86_REGISTER_RAX ? BUSTER_X86_CONTEXT_ACCUMULATOR : 0) | live_flags);
    }
    result->records[result->count++] = (X86Instruction){0};
}

// A function's requests, in its records' order, held until x86_choose_forms' 32 lanes are full or the function's records
// end (tier1_select_requests_flush): each one's record (`at`), destination register, row, value and context, and the
// path that chooses their forms.
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

// The requests' forms, chosen in one batch by x86_choose_forms (or its scalar twin): each record the chosen form's, or
// left empty where no form takes the value, which validation then refuses, as a choice of one record at a time did.
static void tier1_select_requests_flush(Tier1SelectRequests* requests, X86Instruction* records)
{
    if (requests->vector)
    {
        x86_choose_forms(requests->rows, requests->values, requests->contexts, requests->count, requests->forms);
    }
    else
    {
        x86_choose_forms_scalar(requests->rows, requests->values, requests->contexts, requests->count, requests->forms);
    }
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

// The node's request, if it has one, its record at `first` among the function's `records`, which a full batch's forms
// are chosen into first.
static void tier1_select_requests_add(Tier1SelectRequests* requests, const Tier1SelectRecords* selected, uint32_t first, X86Instruction* records)
{
    if (selected->requested)
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

static void tier1_select_compare(Tier1SelectRecords* result, const Tier1SelectHoles* holes)
{
    if (holes->compare_immediate)
    {
        tier1_select_request(result, X86_OPERATION_CMP, holes->width, holes->left, (int64_t)holes->immediate, tier1_select_condition_flags[holes->condition]);
    }
    else
    {
        result->records[result->count++] = tier1_select_register_record(holes->width == 8 ? X86_FORM_CMP_R64_RM64 : X86_FORM_CMP_R32_RM32, holes->left, holes->right);
    }
}

static Tier1SelectRecords tier1_select_expand(const Tier1SelectHoles* holes)
{
    Tier1SelectRecords result = {0};
    const uint8_t destination = holes->destination;
    const uint32_t width = holes->width;
    switch (holes->rule)
    {
    case TIER1_RULE_LEAF:
    case TIER1_RULE_NONE:
        break;
    case TIER1_RULE_CONSTANT:
        tier1_select_request(&result, X86_OPERATION_MOV, width, destination, (int64_t)holes->immediate, 0);
        break;
    case TIER1_RULE_COPY:
        if (destination != holes->left || width == 4)
        {
            result.records[result.count++] = tier1_select_move(width, destination, holes->left);
        }
        break;
    case TIER1_RULE_TRUNCATE:
    case TIER1_RULE_ZERO_EXTEND:
    {
        const uint32_t source_width = holes->rule == TIER1_RULE_TRUNCATE ? width : holes->source_width;
        const uint16_t form = source_width == 1 ? X86_FORM_MOVZX_R32_RM8 : source_width == 2 ? X86_FORM_MOVZX_R32_RM16 : X86_FORM_MOV_R32_RM32;
        result.records[result.count++] = tier1_select_register_record(form, destination, holes->left);
        break;
    }
    case TIER1_RULE_SIGN_EXTEND:
    {
        const uint16_t form = holes->source_width == 4 ? X86_FORM_MOVSXD_R64_RM32 : holes->source_width == 2 ? (width == 8 ? X86_FORM_MOVSX_R64_RM16 : X86_FORM_MOVSX_R32_RM16) : (width == 8 ? X86_FORM_MOVSX_R64_RM8 : X86_FORM_MOVSX_R32_RM8);
        result.records[result.count++] = tier1_select_register_record(form, destination, holes->left);
        if (width == 2)
        {
            // I8 -> I16 has sixteen significant bits. Clear the rest of its canonical 32-bit storage value.
            result.records[result.count++] = tier1_select_register_record(X86_FORM_MOVZX_R32_RM16, destination, destination);
        }
        break;
    }
    case TIER1_RULE_UNARY:
        if (destination != holes->left)
        {
            result.records[result.count++] = tier1_select_move(width, destination, holes->left);
        }
        result.records[result.count++] = (X86Instruction)
        {
            .rm = destination,
            .form = holes->operation == X86_OPERATION_NEG ? (width == 8 ? X86_FORM_NEG_RM64 : X86_FORM_NEG_RM32) : (width == 8 ? X86_FORM_NOT_RM64 : X86_FORM_NOT_RM32),
        };
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
                result.records[result.count++] = tier1_select_move(width, BUSTER_TIER1_SELECT_SCRATCH, right);
                right = BUSTER_TIER1_SELECT_SCRATCH;
            }
        }
        if (destination != left)
        {
            result.records[result.count++] = tier1_select_move(width, destination, left);
        }
        result.records[result.count++] = tier1_select_register_record(tier1_select_binary_form(holes->operation, width), destination, right);
        break;
    }
    case TIER1_RULE_IMMEDIATE:
    case TIER1_RULE_MULTIPLY_SHIFT:
    {
        if (holes->operation == X86_OPERATION_IMUL && holes->rule == TIER1_RULE_IMMEDIATE)
        {
            const bool small = (uint64_t)(int64_t)(int8_t)holes->immediate == holes->immediate || (width == 4 && (uint32_t)(int32_t)(int8_t)holes->immediate == (uint32_t)holes->immediate);
            const uint16_t form = width == 8 ? (small ? X86_FORM_IMUL_R64_RM64_IMM8 : X86_FORM_IMUL_R64_RM64_IMM32) : (small ? X86_FORM_IMUL_R32_RM32_IMM8 : X86_FORM_IMUL_R32_RM32_IMM32);
            X86Instruction record = tier1_select_register_record(form, destination, holes->left);
            record.immediate = small ? (int8_t)holes->immediate : (int32_t)holes->immediate;
            result.records[result.count++] = record;
        }
        else
        {
            if (destination != holes->left)
            {
                result.records[result.count++] = tier1_select_move(width, destination, holes->left);
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
        result.records[result.count++] = record;
        break;
    }
    case TIER1_RULE_LOAD:
    case TIER1_RULE_LOAD_ADDRESS:
    {
        X86Instruction record = holes->memory;
        record.reg = destination;
        record.form = width == 1 ? X86_FORM_MOVZX_R32_RM8 : width == 2 ? X86_FORM_MOVZX_R32_RM16 : width == 8 ? X86_FORM_MOV_R64_RM64 : X86_FORM_MOV_R32_RM32;
        result.records[result.count++] = record;
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
            result.records[result.count++] = tier1_select_move(8, BUSTER_TIER1_SELECT_SCRATCH, destination);
            memory.rm = aliases_base ? BUSTER_TIER1_SELECT_SCRATCH : memory.rm;
            memory.index = aliases_index ? BUSTER_TIER1_SELECT_SCRATCH : memory.index;
        }
        if (destination != holes->left)
        {
            result.records[result.count++] = tier1_select_move(width, destination, holes->left);
        }
        memory.reg = destination;
        memory.form = tier1_select_binary_form(holes->operation, width);
        result.records[result.count++] = memory;
        break;
    }
    case TIER1_RULE_STORE:
    case TIER1_RULE_STORE_ADDRESS:
    {
        X86Instruction record = holes->memory;
        record.reg = holes->right;
        record.form = width == 1 ? X86_FORM_MOV_RM8_R8 : width == 2 ? X86_FORM_MOV_RM16_R16 : width == 8 ? X86_FORM_MOV_RM64_R64 : X86_FORM_MOV_RM32_R32;
        result.records[result.count++] = record;
        break;
    }
    case TIER1_RULE_COMPARE:
    case TIER1_RULE_COMPARE_IMMEDIATE:
        tier1_select_compare(&result, holes);
        result.records[result.count++] = (X86Instruction){ .rm = destination, .form = tier1_select_condition_sets[holes->condition] };
        result.records[result.count++] = tier1_select_register_record(X86_FORM_MOVZX_R32_RM8, destination, destination);
        break;
    case TIER1_RULE_BRANCH_COMPARE:
        tier1_select_compare(&result, holes);
        result.records[result.count++] = (X86Instruction){ .form = tier1_select_condition_branches[holes->condition] };
        result.records[result.count++] = (X86Instruction){ .form = X86_FORM_JMP_REL8 };
        break;
    case TIER1_RULE_BRANCH:
        result.records[result.count++] = tier1_select_register_record(X86_FORM_TEST_RM32_R32, holes->left, holes->left);
        result.records[result.count++] = (X86Instruction){ .form = X86_FORM_JNE_REL8 };
        result.records[result.count++] = (X86Instruction){ .form = X86_FORM_JMP_REL8 };
        break;
    case TIER1_RULE_SELECT:
    {
        result.records[result.count++] = tier1_select_register_record(X86_FORM_TEST_RM32_R32, holes->left, holes->left);
        const bool inverse = destination == holes->right;
        const uint8_t initial = inverse ? holes->right : holes->third;
        const uint8_t alternate = inverse ? holes->third : holes->right;
        if (destination != initial)
        {
            result.records[result.count++] = tier1_select_move(width, destination, initial);
        }
        const uint16_t form = width == 8 ? (inverse ? X86_FORM_CMOVE_R64_RM64 : X86_FORM_CMOVNE_R64_RM64) : (inverse ? X86_FORM_CMOVE_R32_RM32 : X86_FORM_CMOVNE_R32_RM32);
        result.records[result.count++] = tier1_select_register_record(form, destination, alternate);
        break;
    }
    case TIER1_RULE_JUMP:
        result.records[result.count++] = (X86Instruction){ .form = X86_FORM_JMP_REL8 };
        break;
    case TIER1_RULE_RETURN:
        if (width != 0 && holes->left != X86_REGISTER_RAX)
        {
            result.records[result.count++] = tier1_select_move(width, X86_REGISTER_RAX, holes->left);
        }
        result.records[result.count++] = (X86Instruction){ .form = X86_FORM_RET };
        break;
    case TIER1_RULE_UNREACHABLE:
        result.records[result.count++] = (X86Instruction){ .form = X86_FORM_UD2 };
        break;
    default:
        result.records[result.count++] = (X86Instruction){0};
        break;
    }
    return result;
}

#define BUSTER_TIER1_SELECT_MAX_INSTRUCTIONS (1u << 20)
#define BUSTER_TIER1_SELECT_MAX_CANDIDATES 8
#define BUSTER_TIER1_SELECT_MAX_FOLDS 3

// Cold per-node emission metadata. The hot rule-choice pass uses contiguous 16-lane cost arrays; the emitter consumes
// all fields of one selected pattern. `values` preserve operand roles, including absent slots, while the effective-use
// CSR is the compact, directly consumable liveness handoff. CALL uses none of the three ordinary value slots: its
// ordered argument slice, including duplicates, is in the effective-use CSR, immediate is its symbol ID, and width
// is the result width (zero for void). Memory/conversion patterns keep logical byte widths, while the allocator and
// frame consumer use tier1_select_width's 4-byte canonical storage for I8/I16. Conversion source_width is the source's
// logical byte width, except canonical I1 zero-extension reads its 32-bit storage. The original function stays immutable.
typedef struct Tier1SelectPattern Tier1SelectPattern;
struct Tier1SelectPattern
{
    uint64_t immediate;
    uint32_t values[3];
    uint32_t folds[BUSTER_TIER1_SELECT_MAX_FOLDS];
    int32_t displacement;
    uint8_t rule;
    uint8_t operation;
    uint8_t width;
    uint8_t source_width;
    uint8_t condition;
    uint8_t scale;
    uint8_t fold_count;
    uint8_t intrinsic_cost;
    bool compare_immediate;
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
    // load isn't folded past (tier1_select_load_folds). The IR has no atomic access of its own: C's atomic operations
    // reach it as calls of the unit's leaves.
    const uint32_t* barriers;
};

static bool tier1_select_barrier(const IrFunction* function, uint32_t node)
{
    const uint32_t op = function->op[node];
    return op == IR_STORE || op == IR_CALL || op == IR_CALL_INDIRECT || (op == IR_LOAD && (function->flags[node] & BUSTER_IR_VOLATILE) != 0);
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

// This selection boundary intentionally excludes extension/truncation ABI rules for narrow integers, floating-point
// and aggregates. The shared verifier checks descriptor bounds and actual argument types before this helper runs.
static bool tier1_select_call_supported(const IrFunction* function, uint32_t node)
{
    bool supported = node < function->instruction_count && function->op[node] == IR_CALL;
    if (supported)
    {
        const IrCall call = function->calls[function->b[node]];
        const uint32_t type = function->type[node];
        supported = !call.variadic && call.abi == 0 && call.argument_count <= BUSTER_TIER1_SELECT_CALL_ARGUMENTS && call.parameter_count == call.argument_count && (type == IR_VOID || type == IR_I32 || type == IR_I64 || type == IR_PTR);
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
    if (tier1_select_tree_child(forest, parent, value))
    {
        assert(pattern->fold_count < BUSTER_TIER1_SELECT_MAX_FOLDS);
        pattern->folds[pattern->fold_count++] = value;
    }
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

// Only an explicit IR address with a representable displacement is folded. No narrowed address or speculative load
// is introduced. A large offset remains an explicit unsupported operation until its materialization path exists.
static bool tier1_select_memory(const Tier1SelectForest* forest, uint32_t node, uint32_t pointer, Tier1SelectPattern* pattern, uint32_t first)
{
    const IrFunction* function = forest->function;
    bool folded = false;
    pattern->values[first] = pointer;
    pattern->values[first + 1] = BUSTER_IR_NONE;
    pattern->scale = 0;
    pattern->displacement = 0;
    if (function->op[pointer] == IR_ADDRESS && tier1_select_tree_child(forest, node, pointer))
    {
        const IrAddress address = function->addresses[function->c[pointer]];
        if (address.offset == (int64_t)(int32_t)address.offset)
        {
            pattern->values[first] = function->a[pointer];
            pattern->values[first + 1] = function->b[pointer];
            pattern->scale = (uint8_t)tier1_select_log2(address.scale);
            pattern->displacement = (int32_t)address.offset;
            tier1_select_fold(forest, node, pointer, pattern);
            folded = true;
        }
    }
    return folded;
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
        .folds = {BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE},
        .operation = (uint8_t)tier1_select_operation(op),
        .width = (uint8_t)width,
    };
    const bool integer = type == IR_I32 || type == IR_I64;
    if (op == IR_CONST && width != 0)
    {
        base.rule = TIER1_RULE_CONSTANT;
        const uint32_t bits = ir_type_bits[type];
        base.immediate = function->constants[a].low & (bits < 64 ? (UINT64_C(1) << bits) - 1 : UINT64_MAX);
        base.intrinsic_cost = 1;
        tier1_select_add_candidate(forest, node, &candidates, base);
    }
    else if (op == IR_PARAM && (integer || type == IR_PTR) && a < 6)
    {
        base.rule = TIER1_RULE_LEAF;
        tier1_select_add_candidate(forest, node, &candidates, base);
    }
    else if ((op == IR_COPY && width != 0) || (op == IR_INTTOPTR && function->type[a] == IR_I64) || (op == IR_PTRTOINT && type == IR_I64) || (op == IR_BITCAST && width != 0 && tier1_select_width(function->type[a]) == width && ir_type_bits[function->type[a]] == ir_type_bits[type]))
    {
        base.rule = TIER1_RULE_COPY;
        base.values[0] = a;
        base.intrinsic_cost = 1;
        tier1_select_add_candidate(forest, node, &candidates, base);
    }
    else if ((op == IR_TRUNC && (type == IR_I8 || type == IR_I16 || type == IR_I32) && tier1_select_memory_width(function->type[a]) != 0 && function->type[a] != IR_PTR) ||
             (op == IR_ZEXT && (type == IR_I8 || type == IR_I16 || integer) && (function->type[a] == IR_I1 || function->type[a] == IR_I8 || function->type[a] == IR_I16 || function->type[a] == IR_I32)) ||
             (op == IR_SEXT && (type == IR_I16 || integer) && (function->type[a] == IR_I8 || function->type[a] == IR_I16 || function->type[a] == IR_I32)))
    {
        base.rule = op == IR_TRUNC ? TIER1_RULE_TRUNCATE : op == IR_ZEXT ? TIER1_RULE_ZERO_EXTEND : TIER1_RULE_SIGN_EXTEND;
        base.values[0] = a;
        base.width = (uint8_t)tier1_select_memory_width(type);
        base.source_width = function->type[a] == IR_I1 ? 4 : (uint8_t)tier1_select_memory_width(function->type[a]);
        base.intrinsic_cost = op == IR_SEXT && type == IR_I16 ? 2 : 1;
        tier1_select_add_candidate(forest, node, &candidates, base);
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
        // The load the memory operand takes: the right operand's, or for a commutative operation either's.
        const bool commutative = tier1_select_commutative((X86Operation)base.operation);
        const uint32_t load = tier1_select_load_folds(forest, node, b) ? b : commutative && tier1_select_load_folds(forest, node, a) ? a : BUSTER_IR_NONE;
        if (load != BUSTER_IR_NONE)
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
    }
    else if (op == IR_ADDRESS)
    {
        const IrAddress address = function->addresses[c];
        if (address.offset == (int64_t)(int32_t)address.offset)
        {
            base.rule = TIER1_RULE_ADDRESS;
            base.values[0] = a;
            base.values[1] = b;
            base.displacement = (int32_t)address.offset;
            base.scale = (uint8_t)tier1_select_log2(address.scale);
            base.intrinsic_cost = 1;
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
        tier1_select_add_candidate(forest, node, &candidates, base);
        Tier1SelectPattern folded = base;
        if (tier1_select_memory(forest, node, a, &folded, 0))
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
    else if (op == IR_CBR)
    {
        base.rule = TIER1_RULE_BRANCH;
        base.values[0] = a;
        base.intrinsic_cost = 3;
        tier1_select_add_candidate(forest, node, &candidates, base);
        if (a + 1 == node && function->op[a] == IR_ICMP && tier1_select_tree_child(forest, node, a) && (function->type[function->a[a]] == IR_I32 || function->type[function->a[a]] == IR_I64 || function->type[function->a[a]] == IR_PTR))
        {
            Tier1SelectPattern folded = base;
            folded.rule = TIER1_RULE_BRANCH_COMPARE;
            folded.values[0] = function->a[a];
            folded.values[1] = function->b[a];
            folded.width = (uint8_t)tier1_select_width(function->type[function->a[a]]);
            folded.condition = (uint8_t)function->c[a];
            tier1_select_fold(forest, node, a, &folded);
            uint64_t immediate = 0;
            if (tier1_select_constant(function, function->b[a], &immediate) && tier1_select_immediate_fits(folded.width, immediate))
            {
                folded.values[1] = BUSTER_IR_NONE;
                folded.immediate = immediate;
                folded.compare_immediate = true;
                tier1_select_fold(forest, node, function->b[a], &folded);
            }
            tier1_select_add_candidate(forest, node, &candidates, folded);
        }
    }
    else if (op == IR_CALL && tier1_select_call_supported(function, node))
    {
        base.rule = TIER1_RULE_CALL;
        base.immediate = a;
        // Structural lowering estimate: call plus possible argument/result copies; preservation is emitter-owned.
        base.intrinsic_cost = (uint8_t)(1 + function->calls[b].argument_count + (type != IR_VOID));
        tier1_select_add_candidate(forest, node, &candidates, base);
    }
    else if (op == IR_RET && (a == BUSTER_IR_NONE || function->type[a] == IR_I1 || function->type[a] == IR_I32 || function->type[a] == IR_I64 || function->type[a] == IR_PTR))
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
    return candidates;
}

// The scalar oracle is maximal munch: most covered nodes first. A switch fixes ties independently of the vector
// minimum-cost selection. Both share legality, not the choice of cheapest cover. In particular munch chooses a
// power-of-two multiply's shift before its three-operand immediate IMUL; the costed cover may choose the latter.
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
        case TIER1_RULE_MULTIPLY_SHIFT:
            rank = 9;
            break;
        case TIER1_RULE_LEA_SHIFT:
        case TIER1_RULE_BRANCH_COMPARE:
        case TIER1_RULE_LOAD_ADDRESS:
        case TIER1_RULE_STORE_ADDRESS:
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
// verifier alone does not establish that such a definition reaches its use. No input-dependent recursion is used.
static Tier1SelectError tier1_select_plan(const IrFunction* function, const IrDominance* dominance, Arena* output, Arena* scratch, bool vector, Tier1SelectPlan* result, uint32_t* error_node)
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
        const IrCheck check = BUSTER_VECTOR_PATH(vector) ? ir_verify(function, scratch, dominance) : ir_verify_scalar(function, scratch, dominance);
        if (check.error != IR_OK)
        {
            error = check.error == IR_ERROR_MEMORY ? TIER1_SELECT_ERROR_MEMORY : TIER1_SELECT_ERROR_INVALID;
            failed_node = check.instruction;
        }
    }
    uint32_t* blocks = 0;
    uint32_t* uses = 0;
    uint32_t* barriers = 0;
    uint32_t* depths = 0;
    uint32_t* level_offsets = 0;
    uint32_t* level_nodes = 0;
    uint32_t* positions = 0;
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
    bool parameter_prefix = true;
    uint32_t barrier = 0;
    // Labels (whose symbols the frame and the emitter would have to define), by-value aggregates' ABI records and a
    // variadic definition's incoming frame (#131) aren't selected yet.
    if (error == TIER1_SELECT_OK && (function->label_count != 0 || function->parameter_passed != 0 || function->variadic))
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
        if (op == IR_PHI || op == IR_CALL_INDIRECT || op == IR_SWITCH || op == IR_ALLOCA || op == IR_STACK_SAVE || op == IR_STACK_RESTORE || op == IR_BR_INDIRECT || (op == IR_CALL && !tier1_select_call_supported(function, node)))
        {
            error = TIER1_SELECT_ERROR_UNSUPPORTED;
        }
        if (op == IR_PARAM && (!parameter_prefix || blocks[node] != 0))
        {
            error = TIER1_SELECT_ERROR_UNSUPPORTED;
        }
        parameter_prefix &= op == IR_PARAM;
        barriers[node] = barrier;
        barrier = tier1_select_barrier(function, node) ? node + 1 : barrier;
        effective_capacity += op == IR_CALL && error == TIER1_SELECT_OK ? function->calls[function->b[node]].argument_count : 3;
        const uint32_t operand_count = error == TIER1_SELECT_OK ? (uint32_t)ir_operand_count(function, node) : 0;
        for (uint32_t p = 0; error == TIER1_SELECT_OK && p < operand_count; p += 1)
        {
            const IrOperand operand = ir_operand(function, node, p, blocks[node]);
            if (operand.value >= node || (blocks[operand.value] != blocks[node] && dominance == 0))
            {
                error = TIER1_SELECT_ERROR_INVALID;
            }
            else
            {
                uses[operand.value] += 1;
                const uint32_t next = depths[operand.value] + 1;
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
        for (uint32_t reverse = count; reverse != 0; reverse -= 1)
        {
            const uint32_t node = reverse - 1;
            if (plan.covered[node] == 0)
            {
                const Tier1SelectPattern* pattern = &plan.patterns[node];
                for (uint32_t f = 0; f < pattern->fold_count; f += 1)
                {
                    plan.covered[pattern->folds[f]] = 1;
                }
            }
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

static X86Instruction tier1_select_memory_holes(const Tier1SelectPattern* pattern, const uint8_t* registers, uint32_t first)
{
    const uint32_t index = pattern->values[first + 1];
    const X86Instruction result =
    {
        .displacement = pattern->displacement,
        .rm = registers[pattern->values[first]],
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
        holes.memory = tier1_select_memory_holes(pattern, registers, 0);
        if (pattern->rule == TIER1_RULE_STORE || pattern->rule == TIER1_RULE_STORE_ADDRESS)
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
    // This legacy consumer has neither ABI argument moves nor call-clobber preservation or symbolic fixups.
    // Reject the complete call plan before allocating records, even if earlier value locations are spilled.
    for (uint32_t node = 0; error == TIER1_SELECT_OK && node < count; node += 1)
    {
        if (plan->function->op[node] == IR_CALL || plan->patterns[node].rule == TIER1_RULE_CALL)
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
                if (function->op[node] == IR_BR)
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
