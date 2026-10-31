// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #148's original naturally aligned 1/2/4/8-byte scalar atomic RMW SysV leaves. Include after the builtin catalog,
// lane.c, calling_convention.c and tier0.c. Only instruction records, branch metadata and call ABI metadata are
// published. This module does not touch a source object, publish a Link atom, dispatch source syntax or grant a
// catalog capability. Rules, not implementation, from https://llvm.org/docs/Atomics.html#atomics-and-codegen .
//
// GNU exchange/fetch calls take (object, value_bits, order), clang's min/max and IEEE maximum/minimum ones too; GNU compare_exchange_n takes (object, expected_address,
// desired_bits, weak, success_order, failure_order). Legacy fetch/exchange takes (object, value_bits), __sync_swap's and
// clang's legacy minimum and maximum (__sync_fetch_and_min, _umin, _max and _umax) too, legacy CAS takes
// (object, expected_VALUE_bits, desired_bits), and legacy release takes only object. GNU's byte flag, test_and_set and
// clear, takes (object, order). Pointers and values use U64, order and weak use I32. A bits result is zero-extended U64,
// a CAS boolean and test_and_set's U64 0/1, and release and clear return VOID. SysV's six GP
// argument registers suffice. Every fixed source argument, including semantically unused valid orders/weak,
// evaluates exactly once in the caller. Legacy ignored trailing arguments are not passed or evaluated by the call.
// Source type/order/history/alias checks are the typed action's job. Strong CAS implements weak requests.
//
// The source consumer MUST give natural object alignment at least required_alignment, as clang's code generation gives the
// object's address (c_checker.c's check_graph_atomic_alignment), and operation-appropriate accessibility for exactly
// bytes bytes: release requires writable storage only; RMW requires readable/writable storage. GNU CAS additionally
// requires expected_address to name a
// readable value and writable on failure, using exactly bytes bytes; it is not an atomic-object alignment contract.
// No assumption permits an eight-byte store to a narrow object. GNU CAS loads expected once and writes observed bits
// only on failure, preserving success aliases and read-only expected values. No volatile access is elided by the body.
//
// All RMW uses LOCK CMPXCHG, strengthening every valid order to seq_cst. Legacy release remains a store, using an
// exact-width MOV of zero followed by MFENCE; it never acquires a source-object read or joins a release sequence as
// an RMW merely to strengthen ordering. Retried arithmetic uses the latest observed
// value returned in RAX, and op_fetch returns its successful desired register without reloading shared memory.
// Arithmetic is raw modular integer/pointer arithmetic; the source producer supplies GNU's unscaled pointer delta.
// clang's min and max (fetch_min, max_fetch, ...) keep the smaller or the larger of the object and the operand, in the
// object's width: unsigned for INTEGER_BITS and BOOL_BITS, signed for SIGNED_BITS, which no other operation takes.
// clang's fetch_uinc and fetch_udec (atomicrmw uinc_wrap and udec_wrap) compare the object with the operand unsigned in
// its width: 0 where it is at or above the operand and it plus one otherwise; the operand where it is 0 or above it,
// and it less one otherwise. They keep BOOL_BITS' 0/1. test_and_set exchanges 1 into its byte and returns whether the
// old byte wasn't 0; clear stores 0 as a legacy release does.
// F32/F64 support is raw GNU exchange and clang's IEEE maximum and minimum (fetch_fmaximum, fetch_fmaximum_num,
// fetch_fminimum and fetch_fminimum_num) only, the latter computed as LLVM's x86 expansion computes them
// (cbuiltin_atomic_rmw_ieee_body): no numeric conversion, floating arithmetic or errno handling.
// BOOL_BITS requires canonical 0/1 stored values and arguments; AND/OR/XOR, min/max, exchange/CAS and release preserve
// that representation. Bool ADD/SUB/NAND and floating arithmetic/CAS/min/max are explicitly unsupported here.
//
// Every call must remain an unknown-effect compiler barrier, including release. These leaves clobber only caller-
// saved RAX/RDX/R8 and status flags, the IEEE maximum and minimum XMM0 to XMM2 too and MXCSR's exception flags as
// clang's code does (its DAZ read), preserve callee-saved registers/RSP/FP controls, and need no stack.
// CAS branches consume ZF before it can be overwritten. Branch records have displacement zero and explicit record
// index targets, for normal branch sizing before either encoder. No HLE, CX16, XCHG or XADD form is required.
//
// Unsupported id/width/format and allocation exhaustion publish no records, metadata or arena-length change. One
// allocation holds records and branch columns; inputs and output arena storage must not overlap. Capacity is at most
// BUSTER_CBUILTIN_ATOMIC_RMW_RECORDS records and one branch. Cold bounded emission is scalar; target-host compilation
// and end-to-end producer/consumer performance qualification remain separate from record correctness.
#ifndef BUSTER_C_BUILTIN_ATOMIC_RMW_HELPERS_INCLUDED
#define BUSTER_C_BUILTIN_ATOMIC_RMW_HELPERS_INCLUDED

#define BUSTER_CBUILTIN_ATOMIC_RMW_RECORDS 17u

typedef enum CBuiltinAtomicRmwFormat
{
    CBUILTIN_ATOMIC_RMW_INTEGER_BITS,
    CBUILTIN_ATOMIC_RMW_BOOL_BITS,
    CBUILTIN_ATOMIC_RMW_F32_BITS,
    CBUILTIN_ATOMIC_RMW_F64_BITS,
    // Integer bits that min and max order as signed ones.
    CBUILTIN_ATOMIC_RMW_SIGNED_BITS,
} CBuiltinAtomicRmwFormat;

typedef struct CBuiltinAtomicRmwHelper CBuiltinAtomicRmwHelper;
struct CBuiltinAtomicRmwHelper
{
    Tier0Code code;
    uint32_t parameter_count;
    uint32_t result_class;
    uint8_t parameter_classes[6];
    bool compiler_barrier;
    uint32_t required_alignment;
};

typedef enum CBuiltinAtomicRmwOperation
{
    CBUILTIN_ATOMIC_RMW_NONE,
    CBUILTIN_ATOMIC_RMW_EXCHANGE,
    CBUILTIN_ATOMIC_RMW_COMPARE_EXCHANGE,
    CBUILTIN_ATOMIC_RMW_ADD,
    CBUILTIN_ATOMIC_RMW_SUB,
    CBUILTIN_ATOMIC_RMW_AND,
    CBUILTIN_ATOMIC_RMW_OR,
    CBUILTIN_ATOMIC_RMW_XOR,
    CBUILTIN_ATOMIC_RMW_NAND,
    CBUILTIN_ATOMIC_RMW_RELEASE,
    CBUILTIN_ATOMIC_RMW_MIN,
    CBUILTIN_ATOMIC_RMW_MAX,
    CBUILTIN_ATOMIC_RMW_UINC,
    CBUILTIN_ATOMIC_RMW_UDEC,
    CBUILTIN_ATOMIC_RMW_TEST_AND_SET,
    CBUILTIN_ATOMIC_RMW_CLEAR,
    CBUILTIN_ATOMIC_RMW_FMAXIMUM,
    CBUILTIN_ATOMIC_RMW_FMAXIMUM_NUM,
    CBUILTIN_ATOMIC_RMW_FMINIMUM,
    CBUILTIN_ATOMIC_RMW_FMINIMUM_NUM,
} CBuiltinAtomicRmwOperation;

typedef struct CBuiltinAtomicRmwRecipe CBuiltinAtomicRmwRecipe;
struct CBuiltinAtomicRmwRecipe
{
    X86Instruction records[BUSTER_CBUILTIN_ATOMIC_RMW_RECORDS];
    uint32_t count;
    uint32_t branch;
    uint32_t target;
    bool full;
};

static CBuiltinAtomicRmwOperation cbuiltin_atomic_rmw_operation(CBuiltinId id)
{
    CBuiltinAtomicRmwOperation operation = CBUILTIN_ATOMIC_RMW_NONE;
    switch (id)
    {
    case CBUILTIN_ATOMIC_EXCHANGE_N:
    case CBUILTIN_SYNC_LOCK_TEST_AND_SET:
    case CBUILTIN_SYNC_SWAP: operation = CBUILTIN_ATOMIC_RMW_EXCHANGE; break;
    case CBUILTIN_ATOMIC_COMPARE_EXCHANGE_N:
    case CBUILTIN_SYNC_BOOL_COMPARE_AND_SWAP:
    case CBUILTIN_SYNC_VAL_COMPARE_AND_SWAP: operation = CBUILTIN_ATOMIC_RMW_COMPARE_EXCHANGE; break;
    case CBUILTIN_ATOMIC_FETCH_ADD:
    case CBUILTIN_ATOMIC_ADD_FETCH:
    case CBUILTIN_SYNC_FETCH_AND_ADD:
    case CBUILTIN_SYNC_ADD_AND_FETCH: operation = CBUILTIN_ATOMIC_RMW_ADD; break;
    case CBUILTIN_ATOMIC_FETCH_SUB:
    case CBUILTIN_ATOMIC_SUB_FETCH:
    case CBUILTIN_SYNC_FETCH_AND_SUB:
    case CBUILTIN_SYNC_SUB_AND_FETCH: operation = CBUILTIN_ATOMIC_RMW_SUB; break;
    case CBUILTIN_ATOMIC_FETCH_AND:
    case CBUILTIN_ATOMIC_AND_FETCH:
    case CBUILTIN_SYNC_FETCH_AND_AND:
    case CBUILTIN_SYNC_AND_AND_FETCH: operation = CBUILTIN_ATOMIC_RMW_AND; break;
    case CBUILTIN_ATOMIC_FETCH_OR:
    case CBUILTIN_ATOMIC_OR_FETCH:
    case CBUILTIN_SYNC_FETCH_AND_OR:
    case CBUILTIN_SYNC_OR_AND_FETCH: operation = CBUILTIN_ATOMIC_RMW_OR; break;
    case CBUILTIN_ATOMIC_FETCH_XOR:
    case CBUILTIN_ATOMIC_XOR_FETCH:
    case CBUILTIN_SYNC_FETCH_AND_XOR:
    case CBUILTIN_SYNC_XOR_AND_FETCH: operation = CBUILTIN_ATOMIC_RMW_XOR; break;
    case CBUILTIN_ATOMIC_FETCH_NAND:
    case CBUILTIN_ATOMIC_NAND_FETCH:
    case CBUILTIN_SYNC_FETCH_AND_NAND:
    case CBUILTIN_SYNC_NAND_AND_FETCH: operation = CBUILTIN_ATOMIC_RMW_NAND; break;
    case CBUILTIN_SYNC_LOCK_RELEASE: operation = CBUILTIN_ATOMIC_RMW_RELEASE; break;
    case CBUILTIN_ATOMIC_FETCH_MIN:
    case CBUILTIN_ATOMIC_MIN_FETCH:
    case CBUILTIN_SYNC_FETCH_AND_MIN:
    case CBUILTIN_SYNC_FETCH_AND_UMIN: operation = CBUILTIN_ATOMIC_RMW_MIN; break;
    case CBUILTIN_ATOMIC_FETCH_MAX:
    case CBUILTIN_ATOMIC_MAX_FETCH:
    case CBUILTIN_SYNC_FETCH_AND_MAX:
    case CBUILTIN_SYNC_FETCH_AND_UMAX: operation = CBUILTIN_ATOMIC_RMW_MAX; break;
    case CBUILTIN_ATOMIC_FETCH_UINC: operation = CBUILTIN_ATOMIC_RMW_UINC; break;
    case CBUILTIN_ATOMIC_FETCH_UDEC: operation = CBUILTIN_ATOMIC_RMW_UDEC; break;
    case CBUILTIN_ATOMIC_TEST_AND_SET: operation = CBUILTIN_ATOMIC_RMW_TEST_AND_SET; break;
    case CBUILTIN_ATOMIC_CLEAR: operation = CBUILTIN_ATOMIC_RMW_CLEAR; break;
    case CBUILTIN_ATOMIC_FETCH_FMAXIMUM: operation = CBUILTIN_ATOMIC_RMW_FMAXIMUM; break;
    case CBUILTIN_ATOMIC_FETCH_FMAXIMUM_NUM: operation = CBUILTIN_ATOMIC_RMW_FMAXIMUM_NUM; break;
    case CBUILTIN_ATOMIC_FETCH_FMINIMUM: operation = CBUILTIN_ATOMIC_RMW_FMINIMUM; break;
    case CBUILTIN_ATOMIC_FETCH_FMINIMUM_NUM: operation = CBUILTIN_ATOMIC_RMW_FMINIMUM_NUM; break;
    default: break;
    }
    return operation;
}

static bool cbuiltin_atomic_rmw_new_result(CBuiltinId id)
{
    const bool result = id == CBUILTIN_ATOMIC_ADD_FETCH || id == CBUILTIN_ATOMIC_SUB_FETCH || id == CBUILTIN_ATOMIC_AND_FETCH ||
                        id == CBUILTIN_ATOMIC_OR_FETCH || id == CBUILTIN_ATOMIC_XOR_FETCH || id == CBUILTIN_ATOMIC_NAND_FETCH ||
                        id == CBUILTIN_SYNC_ADD_AND_FETCH || id == CBUILTIN_SYNC_SUB_AND_FETCH || id == CBUILTIN_SYNC_AND_AND_FETCH ||
                        id == CBUILTIN_SYNC_OR_AND_FETCH || id == CBUILTIN_SYNC_XOR_AND_FETCH || id == CBUILTIN_SYNC_NAND_AND_FETCH ||
                        id == CBUILTIN_ATOMIC_MIN_FETCH || id == CBUILTIN_ATOMIC_MAX_FETCH;
    return result;
}

static void cbuiltin_atomic_rmw_append(CBuiltinAtomicRmwRecipe* recipe, X86Instruction record)
{
    if (recipe->count < BUSTER_CBUILTIN_ATOMIC_RMW_RECORDS)
    {
        recipe->records[recipe->count] = record;
        recipe->count += 1;
    }
    else
    {
        recipe->full = true;
    }
}

static void cbuiltin_atomic_rmw_register(CBuiltinAtomicRmwRecipe* recipe, uint16_t form, uint8_t reg, uint8_t rm)
{
    cbuiltin_atomic_rmw_append(recipe, (X86Instruction){ .form = form, .reg = reg, .rm = rm });
}

static uint16_t cbuiltin_atomic_rmw_load_form(uint32_t bytes)
{
    const uint16_t result = bytes == 1 ? X86_FORM_MOVZX_R32_RM8 : bytes == 2 ? X86_FORM_MOVZX_R32_RM16 :
                            bytes == 4 ? X86_FORM_MOV_R32_RM32 : X86_FORM_MOV_R64_RM64;
    return result;
}

static void cbuiltin_atomic_rmw_memory(CBuiltinAtomicRmwRecipe* recipe, uint16_t form, uint8_t reg, uint8_t address)
{
    cbuiltin_atomic_rmw_append(recipe, (X86Instruction){ .form = form, .reg = reg, .rm = address, .mode = BUSTER_X86_MEMORY_BASE });
}

static void cbuiltin_atomic_rmw_compare(CBuiltinAtomicRmwRecipe* recipe, uint32_t bytes)
{
    const uint16_t form = bytes == 1 ? X86_FORM_LOCK_CMPXCHG_M8_R8 : bytes == 2 ? X86_FORM_LOCK_CMPXCHG_M16_R16 :
                          bytes == 4 ? X86_FORM_LOCK_CMPXCHG_M32_R32 : X86_FORM_LOCK_CMPXCHG_M64_R64;
    cbuiltin_atomic_rmw_memory(recipe, form, X86_REGISTER_RDX, X86_REGISTER_RDI);
}

static void cbuiltin_atomic_rmw_cas(CBuiltinAtomicRmwRecipe* recipe, CBuiltinId id, uint32_t bytes)
{
    const bool pointer_expected = id == CBUILTIN_ATOMIC_COMPARE_EXCHANGE_N;
    if (pointer_expected)
    {
        cbuiltin_atomic_rmw_memory(recipe, cbuiltin_atomic_rmw_load_form(bytes), X86_REGISTER_RAX, X86_REGISTER_RSI);
    }
    else
    {
        cbuiltin_atomic_rmw_register(recipe, cbuiltin_atomic_rmw_load_form(bytes), X86_REGISTER_RAX, X86_REGISTER_RSI);
    }
    cbuiltin_atomic_rmw_compare(recipe, bytes);
    if (id != CBUILTIN_SYNC_VAL_COMPARE_AND_SWAP)
    {
        cbuiltin_atomic_rmw_append(recipe, (X86Instruction){ .form = X86_FORM_SETE_RM8, .rm = X86_REGISTER_R8 });
        if (pointer_expected)
        {
            recipe->branch = recipe->count;
            cbuiltin_atomic_rmw_append(recipe, (X86Instruction){ .form = X86_FORM_JE_REL8 });
            const uint16_t store = bytes == 1 ? X86_FORM_MOV_RM8_R8 : bytes == 2 ? X86_FORM_MOV_RM16_R16 :
                                   bytes == 4 ? X86_FORM_MOV_RM32_R32 : X86_FORM_MOV_RM64_R64;
            cbuiltin_atomic_rmw_memory(recipe, store, X86_REGISTER_RAX, X86_REGISTER_RSI);
            recipe->target = recipe->count;
        }
        cbuiltin_atomic_rmw_register(recipe, X86_FORM_MOVZX_R32_RM8, X86_REGISTER_RAX, X86_REGISTER_R8);
    }
}

// Whether an operation is clang's IEEE maximum or minimum, of a float or a double.
static bool cbuiltin_atomic_rmw_ieee(CBuiltinAtomicRmwOperation operation)
{
    return operation >= CBUILTIN_ATOMIC_RMW_FMAXIMUM && operation <= CBUILTIN_ATOMIC_RMW_FMINIMUM_NUM;
}

// clang's IEEE maximum and minimum as LLVM's x86 expansion of llvm.maximum, llvm.minimum, llvm.maximumnum and
// llvm.minimumnum computes them on every target without AVX10.2 (X86ISelLowering's LowerFMINIMUM_FMAXIMUM; the lowering
// refuses the others, check_lower_helper): maxss or minss of the old value and the operand, which is the operand where
// either is a NaN or both are zeros; the sign of the result cleared where the old value's is clear (maximum: the result
// AND the old value OR'd with all but the sign) or set where it is set (minimum: OR'd with the old value's sign); and
// where the old value is a NaN, it. The _num forms then take the operand where the old value is a NaN, and the old value
// where the operand is one. A NaN result is so neither IEEE's quieted one nor of IEEE's sign. XMM1 holds the operand,
// XMM0 the old value and XMM2 the computation; a NaN shows as ucomiss's unordered (PF), the instructions raising the
// exceptions clang's code raises (maxss and minss raise invalid for any NaN, as its cmpunordss do for a signaling one).
static void cbuiltin_atomic_rmw_ieee_body(CBuiltinAtomicRmwRecipe* recipe, CBuiltinAtomicRmwOperation operation, uint32_t bytes)
{
    const bool wide = bytes == 8;
    const bool maximum = operation == CBUILTIN_ATOMIC_RMW_FMAXIMUM || operation == CBUILTIN_ATOMIC_RMW_FMAXIMUM_NUM;
    const bool number = operation == CBUILTIN_ATOMIC_RMW_FMAXIMUM_NUM || operation == CBUILTIN_ATOMIC_RMW_FMINIMUM_NUM;
    const uint16_t compare = wide ? X86_FORM_UCOMISD_X_XM64 : X86_FORM_UCOMISS_X_XM32;
    const uint16_t select = maximum ? (wide ? X86_FORM_MAXSD_X_XM64 : X86_FORM_MAXSS_X_XM32) : (wide ? X86_FORM_MINSD_X_XM64 : X86_FORM_MINSS_X_XM32);
    const uint16_t sign = maximum ? (wide ? X86_FORM_BTR_RM64_IMM8 : X86_FORM_BTR_RM32_IMM8) : (wide ? X86_FORM_BTS_RM64_IMM8 : X86_FORM_BTS_RM32_IMM8);
    cbuiltin_atomic_rmw_register(recipe, wide ? X86_FORM_MOVQ_X_RM64 : X86_FORM_MOVD_X_RM32, 0, X86_REGISTER_RAX);
    cbuiltin_atomic_rmw_register(recipe, X86_FORM_MOVAPS_X_XM, 2, 0);
    cbuiltin_atomic_rmw_register(recipe, select, 2, 1);
    cbuiltin_atomic_rmw_register(recipe, wide ? X86_FORM_MOVQ_RM64_X : X86_FORM_MOVD_RM32_X, 2, X86_REGISTER_RDX);
    // The result's sign: R8 the result with it cleared (maximum) or set (minimum), taken where the old value's sign is clear
    // (maximum) or set (minimum).
    cbuiltin_atomic_rmw_register(recipe, X86_FORM_MOV_R64_RM64, X86_REGISTER_R8, X86_REGISTER_RDX);
    cbuiltin_atomic_rmw_append(recipe, (X86Instruction){ .form = sign, .rm = X86_REGISTER_R8, .immediate = wide ? 63 : 31 });
    cbuiltin_atomic_rmw_register(recipe, wide ? X86_FORM_TEST_RM64_R64 : X86_FORM_TEST_RM32_R32, X86_REGISTER_RAX, X86_REGISTER_RAX);
    cbuiltin_atomic_rmw_register(recipe, maximum ? X86_FORM_CMOVNS_R64_RM64 : X86_FORM_CMOVS_R64_RM64, X86_REGISTER_RDX, X86_REGISTER_R8);
    // A NaN old value: itself, or in the _num forms the operand, which a NaN operand then gives way to the old value.
    cbuiltin_atomic_rmw_register(recipe, compare, 0, 0);
    cbuiltin_atomic_rmw_register(recipe, X86_FORM_CMOVP_R64_RM64, X86_REGISTER_RDX, number ? X86_REGISTER_RSI : X86_REGISTER_RAX);
    if (number)
    {
        cbuiltin_atomic_rmw_register(recipe, compare, 1, 1);
        cbuiltin_atomic_rmw_register(recipe, X86_FORM_CMOVP_R64_RM64, X86_REGISTER_RDX, X86_REGISTER_RAX);
    }
}

static void cbuiltin_atomic_rmw_loop(CBuiltinAtomicRmwRecipe* recipe, CBuiltinId id, CBuiltinAtomicRmwOperation operation, uint32_t bytes, bool is_signed)
{
    cbuiltin_atomic_rmw_memory(recipe, cbuiltin_atomic_rmw_load_form(bytes), X86_REGISTER_RAX, X86_REGISTER_RDI);
    if (cbuiltin_atomic_rmw_ieee(operation))
    {
        // The operand's bits in XMM1, once.
        cbuiltin_atomic_rmw_register(recipe, bytes == 8 ? X86_FORM_MOVQ_X_RM64 : X86_FORM_MOVD_X_RM32, 1, X86_REGISTER_RSI);
    }
    recipe->target = recipe->count;
    if (cbuiltin_atomic_rmw_ieee(operation))
    {
        cbuiltin_atomic_rmw_ieee_body(recipe, operation, bytes);
    }
    else if (operation == CBUILTIN_ATOMIC_RMW_EXCHANGE)
    {
        cbuiltin_atomic_rmw_register(recipe, X86_FORM_MOV_R64_RM64, X86_REGISTER_RDX, X86_REGISTER_RSI);
    }
    else if (operation == CBUILTIN_ATOMIC_RMW_MIN || operation == CBUILTIN_ATOMIC_RMW_MAX)
    {
        // The operand replaces the object's value where it is smaller (min) or larger (max), compared in the object's width.
        const uint16_t compare = bytes == 1 ? X86_FORM_CMP_RM8_R8 : bytes == 2 ? X86_FORM_CMP_RM16_R16 : bytes == 4 ? X86_FORM_CMP_RM32_R32 : X86_FORM_CMP_RM64_R64;
        const bool min = operation == CBUILTIN_ATOMIC_RMW_MIN;
        const uint16_t move = min ? (is_signed ? X86_FORM_CMOVG_R64_RM64 : X86_FORM_CMOVA_R64_RM64) :
                                    (is_signed ? X86_FORM_CMOVL_R64_RM64 : X86_FORM_CMOVB_R64_RM64);
        cbuiltin_atomic_rmw_register(recipe, X86_FORM_MOV_R64_RM64, X86_REGISTER_RDX, X86_REGISTER_RAX);
        cbuiltin_atomic_rmw_register(recipe, compare, X86_REGISTER_RSI, X86_REGISTER_RDX);
        cbuiltin_atomic_rmw_register(recipe, move, X86_REGISTER_RDX, X86_REGISTER_RSI);
    }
    else if (operation == CBUILTIN_ATOMIC_RMW_TEST_AND_SET)
    {
        // 1 exchanged into the byte.
        cbuiltin_atomic_rmw_append(recipe, (X86Instruction){ .form = X86_FORM_MOV_R32_IMM32, .rm = X86_REGISTER_RDX, .immediate = 1 });
    }
    else if (operation == CBUILTIN_ATOMIC_RMW_UINC || operation == CBUILTIN_ATOMIC_RMW_UDEC)
    {
        // The value plus one (uinc) or less one (udec), then compared with the operand unsigned in the object's width: 0
        // where the value is at or above it (uinc), and the operand where the value is above it or 0 (udec). R8 holds the
        // increment's 0, cleared before the compare whose flags the move reads.
        const bool increment = operation == CBUILTIN_ATOMIC_RMW_UINC;
        const uint16_t compare = bytes == 1 ? X86_FORM_CMP_RM8_R8 : bytes == 2 ? X86_FORM_CMP_RM16_R16 : bytes == 4 ? X86_FORM_CMP_RM32_R32 : X86_FORM_CMP_RM64_R64;
        const uint16_t test = bytes == 1 ? X86_FORM_TEST_RM8_R8 : bytes == 2 ? X86_FORM_TEST_RM16_R16 : bytes == 4 ? X86_FORM_TEST_RM32_R32 : X86_FORM_TEST_RM64_R64;
        cbuiltin_atomic_rmw_register(recipe, X86_FORM_MOV_R64_RM64, X86_REGISTER_RDX, X86_REGISTER_RAX);
        cbuiltin_atomic_rmw_append(recipe, (X86Instruction){ .form = increment ? X86_FORM_ADD_RM64_IMM8 : X86_FORM_SUB_RM64_IMM8, .rm = X86_REGISTER_RDX, .immediate = 1 });
        if (increment)
        {
            cbuiltin_atomic_rmw_register(recipe, X86_FORM_XOR_RM32_R32, X86_REGISTER_R8, X86_REGISTER_R8);
            cbuiltin_atomic_rmw_register(recipe, compare, X86_REGISTER_RSI, X86_REGISTER_RAX);
            cbuiltin_atomic_rmw_register(recipe, X86_FORM_CMOVAE_R64_RM64, X86_REGISTER_RDX, X86_REGISTER_R8);
        }
        else
        {
            cbuiltin_atomic_rmw_register(recipe, compare, X86_REGISTER_RSI, X86_REGISTER_RAX);
            cbuiltin_atomic_rmw_register(recipe, X86_FORM_CMOVA_R64_RM64, X86_REGISTER_RDX, X86_REGISTER_RSI);
            cbuiltin_atomic_rmw_register(recipe, test, X86_REGISTER_RAX, X86_REGISTER_RAX);
            cbuiltin_atomic_rmw_register(recipe, X86_FORM_CMOVE_R64_RM64, X86_REGISTER_RDX, X86_REGISTER_RSI);
        }
    }
    else
    {
        cbuiltin_atomic_rmw_register(recipe, X86_FORM_MOV_R64_RM64, X86_REGISTER_RDX, X86_REGISTER_RAX);
        uint16_t form = X86_FORM_AND_RM64_R64;
        if (operation == CBUILTIN_ATOMIC_RMW_ADD)
        {
            form = X86_FORM_ADD_RM64_R64;
        }
        else if (operation == CBUILTIN_ATOMIC_RMW_SUB)
        {
            form = X86_FORM_SUB_RM64_R64;
        }
        else if (operation == CBUILTIN_ATOMIC_RMW_OR)
        {
            form = X86_FORM_OR_RM64_R64;
        }
        else if (operation == CBUILTIN_ATOMIC_RMW_XOR)
        {
            form = X86_FORM_XOR_RM64_R64;
        }
        cbuiltin_atomic_rmw_register(recipe, form, X86_REGISTER_RSI, X86_REGISTER_RDX);
        if (operation == CBUILTIN_ATOMIC_RMW_NAND)
        {
            cbuiltin_atomic_rmw_append(recipe, (X86Instruction){ .form = X86_FORM_NOT_RM64, .rm = X86_REGISTER_RDX });
        }
    }
    cbuiltin_atomic_rmw_compare(recipe, bytes);
    recipe->branch = recipe->count;
    cbuiltin_atomic_rmw_append(recipe, (X86Instruction){ .form = X86_FORM_JNE_REL8 });
    if (cbuiltin_atomic_rmw_new_result(id))
    {
        cbuiltin_atomic_rmw_register(recipe, cbuiltin_atomic_rmw_load_form(bytes), X86_REGISTER_RAX, X86_REGISTER_RDX);
    }
    else if (operation == CBUILTIN_ATOMIC_RMW_TEST_AND_SET)
    {
        // Whether the byte was set: its old value isn't 0.
        cbuiltin_atomic_rmw_register(recipe, X86_FORM_TEST_RM8_R8, X86_REGISTER_RAX, X86_REGISTER_RAX);
        cbuiltin_atomic_rmw_append(recipe, (X86Instruction){ .form = X86_FORM_SETNE_RM8, .rm = X86_REGISTER_RAX });
        cbuiltin_atomic_rmw_register(recipe, X86_FORM_MOVZX_R32_RM8, X86_REGISTER_RAX, X86_REGISTER_RAX);
    }
}

static __attribute__((unused)) CBuiltinAtomicRmwHelper cbuiltin_atomic_rmw_helper_emit(CBuiltinId id, uint32_t bytes, CBuiltinAtomicRmwFormat format, Arena* output)
{
    CBuiltinAtomicRmwHelper result = { 0 };
    result.code.error = TIER0_ERROR_UNSUPPORTED;
    const CBuiltinAtomicRmwOperation operation = cbuiltin_atomic_rmw_operation(id);
    const bool width = bytes == 1 || bytes == 2 || bytes == 4 || bytes == 8;
    const bool ordered = operation == CBUILTIN_ATOMIC_RMW_MIN || operation == CBUILTIN_ATOMIC_RMW_MAX;
    // The IEEE maximum and minimum take a float's or a double's bits alone, and GNU's exchange those too.
    const bool ieee = cbuiltin_atomic_rmw_ieee(operation);
    const bool integer = !ieee && (format == CBUILTIN_ATOMIC_RMW_INTEGER_BITS || (ordered && format == CBUILTIN_ATOMIC_RMW_SIGNED_BITS));
    const bool boolean = !ieee && format == CBUILTIN_ATOMIC_RMW_BOOL_BITS && bytes == 1 && operation != CBUILTIN_ATOMIC_RMW_ADD &&
                         operation != CBUILTIN_ATOMIC_RMW_SUB && operation != CBUILTIN_ATOMIC_RMW_NAND;
    const bool floating = (id == CBUILTIN_ATOMIC_EXCHANGE_N || ieee) && ((format == CBUILTIN_ATOMIC_RMW_F32_BITS && bytes == 4) ||
                          (format == CBUILTIN_ATOMIC_RMW_F64_BITS && bytes == 8));
    // GNU's byte flag (test_and_set and clear) is of one byte, and a clear stores 0 as a legacy release does.
    const bool byte = operation == CBUILTIN_ATOMIC_RMW_TEST_AND_SET || operation == CBUILTIN_ATOMIC_RMW_CLEAR;
    const bool store = operation == CBUILTIN_ATOMIC_RMW_RELEASE || operation == CBUILTIN_ATOMIC_RMW_CLEAR;
    if (operation != CBUILTIN_ATOMIC_RMW_NONE && width && (integer || boolean || floating) && (!byte || bytes == 1))
    {
        CBuiltinAtomicRmwRecipe recipe = { 0 };
        if (store)
        {
            cbuiltin_atomic_rmw_register(&recipe, X86_FORM_XOR_RM32_R32, X86_REGISTER_RDX, X86_REGISTER_RDX);
            const uint16_t store = bytes == 1 ? X86_FORM_MOV_RM8_R8 : bytes == 2 ? X86_FORM_MOV_RM16_R16 :
                                   bytes == 4 ? X86_FORM_MOV_RM32_R32 : X86_FORM_MOV_RM64_R64;
            cbuiltin_atomic_rmw_memory(&recipe, store, X86_REGISTER_RDX, X86_REGISTER_RDI);
            cbuiltin_atomic_rmw_append(&recipe, (X86Instruction){ .form = X86_FORM_MFENCE });
        }
        else if (operation == CBUILTIN_ATOMIC_RMW_COMPARE_EXCHANGE)
        {
            cbuiltin_atomic_rmw_cas(&recipe, id, bytes);
        }
        else
        {
            cbuiltin_atomic_rmw_loop(&recipe, id, operation, bytes, format == CBUILTIN_ATOMIC_RMW_SIGNED_BITS);
        }
        cbuiltin_atomic_rmw_append(&recipe, (X86Instruction){ .form = X86_FORM_RET });
        if (recipe.full)
        {
            result.code.error = TIER0_ERROR_MEMORY;
        }
        else
        {
            const bool branched = !store && (operation != CBUILTIN_ATOMIC_RMW_COMPARE_EXCHANGE || id == CBUILTIN_ATOMIC_COMPARE_EXCHANGE_N);
            const uint64_t record_bytes = (uint64_t)recipe.count * sizeof(X86Instruction);
            const uint64_t branch_bytes = branched ? sizeof(uint32_t) : 0;
            uint8_t* storage = output != NULL ? arena_push_array(output, record_bytes + 2 * branch_bytes, 1, _Alignof(X86Instruction)) : NULL;
            if (storage == NULL)
            {
                result.code.error = TIER0_ERROR_MEMORY;
            }
            else
            {
                result.code.records = (X86Instruction*)storage;
                result.code.count = recipe.count;
                memcpy(result.code.records, recipe.records, (size_t)record_bytes);
                if (branched)
                {
                    result.code.branches = (uint32_t*)(storage + record_bytes);
                    result.code.targets = (uint32_t*)(storage + record_bytes + branch_bytes);
                    result.code.branches[0] = recipe.branch;
                    result.code.targets[0] = recipe.target;
                    result.code.branch_count = 1;
                }
                result.code.error = TIER0_OK;
                // The GNU builtins take an order (the byte flag's its second argument), the legacy ones (__sync_fetch_and_min's
                // minimum too) none.
                const bool gnu = (id >= CBUILTIN_ATOMIC_EXCHANGE_N && id <= CBUILTIN_ATOMIC_NAND_FETCH) ||
                                 (id >= CBUILTIN_ATOMIC_FETCH_MIN && id <= CBUILTIN_ATOMIC_FETCH_UDEC) || ieee;
                result.parameter_count = operation == CBUILTIN_ATOMIC_RMW_RELEASE ? 1 : id == CBUILTIN_ATOMIC_COMPARE_EXCHANGE_N ? 6 :
                                         byte ? 2 : operation == CBUILTIN_ATOMIC_RMW_COMPARE_EXCHANGE || gnu ? 3 : 2;
                result.result_class = store ? BUSTER_TYPED_CLASS_VOID : BUSTER_TYPED_CLASS_U64;
                for (uint32_t i = 0; i < result.parameter_count; i += 1)
                {
                    result.parameter_classes[i] = BUSTER_TYPED_CLASS_U64;
                }
                if (gnu || byte)
                {
                    const uint32_t first_order = id == CBUILTIN_ATOMIC_COMPARE_EXCHANGE_N ? 3 : byte ? 1 : 2;
                    for (uint32_t i = first_order; i < result.parameter_count; i += 1)
                    {
                        result.parameter_classes[i] = BUSTER_TYPED_CLASS_I32;
                    }
                }
                result.required_alignment = bytes;
                result.compiler_barrier = true;
            }
        }
    }
    return result;
}

#endif
