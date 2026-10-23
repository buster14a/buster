// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #148's small original SysV leaf bodies for scalar atomic load/store and fences. Include after the builtin catalog,
// lane.c, calling_convention.c and tier0.c. cbuiltin_atomic_helper_emit publishes only instruction records and ABI
// metadata; it never reads or writes a source object. A source consumer may define a local TEXT atom from these
// records and call it using existing ordinary-call fixups. This file has no Link writer/atom dependency.
//
// load_n: RDI points to an object, ESI is the order, and RAX returns zero-extended raw bits. store_n: RDI is the object,
// RSI the low value bits, EDX the order, and the result is void. thread/signal fences take their order in EDI;
// sync_synchronize has no arguments. Every argument, including a dynamic order, must be evaluated exactly once by
// the caller. Semantic order validation is separate. Helpers implement seq_cst loads/stores/thread fences for every
// valid order, which deliberately strengthens weaker requests; a signal fence needs no machine instruction.
//
// Crucially, every helper call, including signal_fence, must remain an unknown-effect compiler barrier. Do not mark
// these symbols pure/readnone, remove repeated calls, or inline their records into a stream that may reorder memory.
// compiler_barrier is this consumer obligation, not a claim that a RET instruction itself constrains an optimizer.
//
// The caller must establish natural object alignment of at least required_alignment for load/store. These helpers
// do not implement packed/known-unaligned objects lacking that proof, cross-cacheline/page accesses, 16-byte atomics, generic aggregate
// atomics, or atomic read/modify/write operations. An unknown pointer cast is not alignment proof. A valid typed-object
// alignment contract must be explicitly established by the source consumer, or that operation needs another fallback.
// Size zero is required for fences. Unsupported IDs/sizes and arena exhaustion publish no bytes or arena length.
//
// Baseline x86-64 uses MOV for naturally aligned scalar atomic loads. A MOV followed by MFENCE implements these
// seq_cst stores; thread/sync fences use MFENCE. The algorithm is original and uses existing encoder forms only.
// Background: https://llvm.org/docs/Atomics.html#atomics-and-codegen
// This standalone body emitter does not establish source dispatch, target acceptance or source-level atomic support.
#ifndef BUSTER_C_BUILTIN_ATOMIC_HELPERS_INCLUDED
#define BUSTER_C_BUILTIN_ATOMIC_HELPERS_INCLUDED

typedef struct CBuiltinAtomicHelper CBuiltinAtomicHelper;
struct CBuiltinAtomicHelper
{
    Tier0Code code;
    uint32_t parameter_count;
    uint32_t result_class;
    uint8_t parameter_classes[3];
    bool compiler_barrier;
    uint32_t required_alignment;
};

static __attribute__((unused)) CBuiltinAtomicHelper cbuiltin_atomic_helper_emit(CBuiltinId id, uint32_t bytes, Arena* output)
{
    CBuiltinAtomicHelper result = { 0 };
    result.code.error = TIER0_ERROR_UNSUPPORTED;
    const bool load = id == CBUILTIN_ATOMIC_LOAD_N;
    const bool store = id == CBUILTIN_ATOMIC_STORE_N;
    const bool thread_fence = id == CBUILTIN_ATOMIC_THREAD_FENCE || id == CBUILTIN_SYNC_SYNCHRONIZE;
    const bool signal_fence = id == CBUILTIN_ATOMIC_SIGNAL_FENCE;
    const bool scalar_size = bytes == 1 || bytes == 2 || bytes == 4 || bytes == 8;
    const bool valid = ((load || store) && scalar_size) || ((thread_fence || signal_fence) && bytes == 0);
    if (valid)
    {
        X86Instruction records[3] = { 0 };
        uint32_t count = 0;
        if (load || store)
        {
            uint32_t form;
            if (load)
            {
                form = bytes == 1 ? X86_FORM_MOVZX_R32_RM8 : bytes == 2 ? X86_FORM_MOVZX_R32_RM16 : bytes == 4 ? X86_FORM_MOV_R32_RM32 : X86_FORM_MOV_R64_RM64;
            }
            else
            {
                form = bytes == 1 ? X86_FORM_MOV_RM8_R8 : bytes == 2 ? X86_FORM_MOV_RM16_R16 : bytes == 4 ? X86_FORM_MOV_RM32_R32 : X86_FORM_MOV_RM64_R64;
            }
            records[count] = (X86Instruction)
            {
                .reg = load ? X86_REGISTER_RAX : X86_REGISTER_RSI,
                .rm = X86_REGISTER_RDI,
                .mode = BUSTER_X86_MEMORY_BASE,
                .form = (uint16_t)form,
            };
            count += 1;
        }
        if (store || thread_fence)
        {
            records[count] = (X86Instruction){ .form = X86_FORM_MFENCE };
            count += 1;
        }
        records[count] = (X86Instruction){ .form = X86_FORM_RET };
        count += 1;
        X86Instruction* published = output != NULL ? BUSTER_ARENA_PUSH_ARRAY(output, X86Instruction, count) : NULL;
        if (published == NULL)
        {
            result.code.error = TIER0_ERROR_MEMORY;
        }
        else
        {
            memcpy(published, records, count * sizeof(X86Instruction));
            result.code.records = published;
            result.code.count = count;
            result.code.error = TIER0_OK;
            result.parameter_count = load ? 2 : store ? 3 : id == CBUILTIN_SYNC_SYNCHRONIZE ? 0 : 1;
            result.result_class = load ? BUSTER_TYPED_CLASS_U64 : BUSTER_TYPED_CLASS_VOID;
            result.parameter_classes[0] = load || store ? BUSTER_TYPED_CLASS_U64 : result.parameter_count != 0 ? BUSTER_TYPED_CLASS_I32 : BUSTER_TYPED_CLASS_VOID;
            result.parameter_classes[1] = load ? BUSTER_TYPED_CLASS_I32 : store ? BUSTER_TYPED_CLASS_U64 : BUSTER_TYPED_CLASS_VOID;
            result.parameter_classes[2] = store ? BUSTER_TYPED_CLASS_I32 : BUSTER_TYPED_CLASS_VOID;
            result.compiler_barrier = true;
            result.required_alignment = load || store ? bytes : 0;
        }
    }
    return result;
}

#endif
