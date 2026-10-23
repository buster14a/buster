// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #148's original naturally aligned 1/2/4/8-byte scalar atomic RMW SysV leaves. Include after the builtin catalog,
// lane.c, calling_convention.c and tier0.c. Only instruction records, branch metadata and call ABI metadata are
// published. This module does not touch a source object, publish a Link atom, dispatch source syntax or grant a
// catalog capability. Rules, not implementation, from https://llvm.org/docs/Atomics.html#atomics-and-codegen .
//
// GNU exchange/fetch calls take (object, value_bits, order); GNU compare_exchange_n takes (object, expected_address,
// desired_bits, weak, success_order, failure_order). Legacy fetch/exchange takes (object, value_bits), legacy CAS takes
// (object, expected_VALUE_bits, desired_bits), and legacy release takes only object. Pointers and values use U64, order
// and weak use I32. A bits result is zero-extended U64, a CAS boolean is U64 0/1 and release returns VOID. SysV's six GP
// argument registers suffice. Every fixed source argument, including semantically unused valid orders/weak,
// evaluates exactly once in the caller. Legacy ignored trailing arguments are not passed or evaluated by the call.
// Source type/order/history/alias checks are the typed action's job. Strong CAS implements weak requests.
//
// The source consumer MUST prove natural object alignment at least required_alignment and operation-appropriate
// accessibility for exactly bytes bytes: release requires writable storage only; RMW requires readable/writable
// storage. A cast alone is no proof. GNU CAS additionally requires expected_address to name a
// readable value and writable on failure, using exactly bytes bytes; it is not an atomic-object alignment contract.
// No assumption permits an eight-byte store to a narrow object. GNU CAS loads expected once and writes observed bits
// only on failure, preserving success aliases and read-only expected values. No volatile access is elided by the body.
//
// All RMW uses LOCK CMPXCHG, strengthening every valid order to seq_cst. Legacy release remains a store, using an
// exact-width MOV of zero followed by MFENCE; it never acquires a source-object read or joins a release sequence as
// an RMW merely to strengthen ordering. Retried arithmetic uses the latest observed
// value returned in RAX, and op_fetch returns its successful desired register without reloading shared memory.
// Arithmetic is raw modular integer/pointer arithmetic; the source producer supplies GNU's unscaled pointer delta.
// F32/F64 support is raw GNU exchange only: no numeric conversion, floating arithmetic, exceptions or errno handling.
// BOOL_BITS requires canonical 0/1 stored values and arguments; AND/OR/XOR, exchange/CAS and release preserve that
// representation. Bool ADD/SUB/NAND and all floating arithmetic/CAS are explicitly unsupported by this leaf slice.
//
// Every call must remain an unknown-effect compiler barrier, including release. These leaves clobber only caller-
// saved RAX/RDX/R8 and status flags, preserve callee-saved registers/RSP/FP controls, and need no stack or vector state.
// CAS branches consume ZF before it can be overwritten. Branch records have displacement zero and explicit record
// index targets, for normal branch sizing before either encoder. No HLE, CX16, XCHG or XADD form is required.
//
// Unsupported id/width/format and allocation exhaustion publish no records, metadata or arena-length change. One
// allocation holds records and branch columns; inputs and output arena storage must not overlap. Capacity is at most
// BUSTER_CBUILTIN_ATOMIC_RMW_RECORDS records and one branch. Cold bounded emission is scalar; target-host compilation
// and end-to-end producer/consumer performance qualification remain separate from record correctness.
#ifndef BUSTER_C_BUILTIN_ATOMIC_RMW_HELPERS_INCLUDED
#define BUSTER_C_BUILTIN_ATOMIC_RMW_HELPERS_INCLUDED

#define BUSTER_CBUILTIN_ATOMIC_RMW_RECORDS 12u

typedef enum CBuiltinAtomicRmwFormat
{
    CBUILTIN_ATOMIC_RMW_INTEGER_BITS,
    CBUILTIN_ATOMIC_RMW_BOOL_BITS,
    CBUILTIN_ATOMIC_RMW_F32_BITS,
    CBUILTIN_ATOMIC_RMW_F64_BITS,
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
    case CBUILTIN_SYNC_LOCK_TEST_AND_SET: operation = CBUILTIN_ATOMIC_RMW_EXCHANGE; break;
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
    default: break;
    }
    return operation;
}

static bool cbuiltin_atomic_rmw_new_result(CBuiltinId id)
{
    const bool result = id == CBUILTIN_ATOMIC_ADD_FETCH || id == CBUILTIN_ATOMIC_SUB_FETCH || id == CBUILTIN_ATOMIC_AND_FETCH ||
                        id == CBUILTIN_ATOMIC_OR_FETCH || id == CBUILTIN_ATOMIC_XOR_FETCH || id == CBUILTIN_ATOMIC_NAND_FETCH ||
                        id == CBUILTIN_SYNC_ADD_AND_FETCH || id == CBUILTIN_SYNC_SUB_AND_FETCH || id == CBUILTIN_SYNC_AND_AND_FETCH ||
                        id == CBUILTIN_SYNC_OR_AND_FETCH || id == CBUILTIN_SYNC_XOR_AND_FETCH || id == CBUILTIN_SYNC_NAND_AND_FETCH;
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

static void cbuiltin_atomic_rmw_loop(CBuiltinAtomicRmwRecipe* recipe, CBuiltinId id, CBuiltinAtomicRmwOperation operation, uint32_t bytes)
{
    cbuiltin_atomic_rmw_memory(recipe, cbuiltin_atomic_rmw_load_form(bytes), X86_REGISTER_RAX, X86_REGISTER_RDI);
    recipe->target = recipe->count;
    if (operation == CBUILTIN_ATOMIC_RMW_EXCHANGE)
    {
        cbuiltin_atomic_rmw_register(recipe, X86_FORM_MOV_R64_RM64, X86_REGISTER_RDX, X86_REGISTER_RSI);
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
}

static __attribute__((unused)) CBuiltinAtomicRmwHelper cbuiltin_atomic_rmw_helper_emit(CBuiltinId id, uint32_t bytes, CBuiltinAtomicRmwFormat format, Arena* output)
{
    CBuiltinAtomicRmwHelper result = { 0 };
    result.code.error = TIER0_ERROR_UNSUPPORTED;
    const CBuiltinAtomicRmwOperation operation = cbuiltin_atomic_rmw_operation(id);
    const bool width = bytes == 1 || bytes == 2 || bytes == 4 || bytes == 8;
    const bool integer = format == CBUILTIN_ATOMIC_RMW_INTEGER_BITS;
    const bool boolean = format == CBUILTIN_ATOMIC_RMW_BOOL_BITS && bytes == 1 && operation != CBUILTIN_ATOMIC_RMW_ADD &&
                         operation != CBUILTIN_ATOMIC_RMW_SUB && operation != CBUILTIN_ATOMIC_RMW_NAND;
    const bool floating = id == CBUILTIN_ATOMIC_EXCHANGE_N && ((format == CBUILTIN_ATOMIC_RMW_F32_BITS && bytes == 4) ||
                          (format == CBUILTIN_ATOMIC_RMW_F64_BITS && bytes == 8));
    if (operation != CBUILTIN_ATOMIC_RMW_NONE && width && (integer || boolean || floating))
    {
        CBuiltinAtomicRmwRecipe recipe = { 0 };
        if (operation == CBUILTIN_ATOMIC_RMW_RELEASE)
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
            cbuiltin_atomic_rmw_loop(&recipe, id, operation, bytes);
        }
        cbuiltin_atomic_rmw_append(&recipe, (X86Instruction){ .form = X86_FORM_RET });
        if (recipe.full)
        {
            result.code.error = TIER0_ERROR_MEMORY;
        }
        else
        {
            const bool branched = operation != CBUILTIN_ATOMIC_RMW_RELEASE &&
                                  (operation != CBUILTIN_ATOMIC_RMW_COMPARE_EXCHANGE || id == CBUILTIN_ATOMIC_COMPARE_EXCHANGE_N);
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
                const bool gnu = id >= CBUILTIN_ATOMIC_EXCHANGE_N && id <= CBUILTIN_ATOMIC_NAND_FETCH;
                result.parameter_count = operation == CBUILTIN_ATOMIC_RMW_RELEASE ? 1 : id == CBUILTIN_ATOMIC_COMPARE_EXCHANGE_N ? 6 :
                                         operation == CBUILTIN_ATOMIC_RMW_COMPARE_EXCHANGE || gnu ? 3 : 2;
                result.result_class = operation == CBUILTIN_ATOMIC_RMW_RELEASE ? BUSTER_TYPED_CLASS_VOID : BUSTER_TYPED_CLASS_U64;
                for (uint32_t i = 0; i < result.parameter_count; i += 1)
                {
                    result.parameter_classes[i] = BUSTER_TYPED_CLASS_U64;
                }
                if (gnu)
                {
                    const uint32_t first_order = id == CBUILTIN_ATOMIC_COMPARE_EXCHANGE_N ? 3 : 2;
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
