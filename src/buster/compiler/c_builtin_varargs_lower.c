// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #148's System V scalar va_list consumers. Include after c_builtin_lower.c and calling_convention.c. The caller
// evaluates each list/pointer operand once into a U64 local. Both start spellings take actual register-save/overflow
// addresses supplied by the function producer, plus its named-argument GP/FP offsets. This module cannot discover
// those frame addresses and does not imply that the source/function producer currently exposes them.
//
// cbuiltin_varargs_lower and its scalar publication twin stage a bounded existing Tier0Function recipe, with the same
// disjoint-column and all-or-nothing contract as cbuiltin_lower. An argument plan supports one integer/pointer or
// F32/F64 value in an eight-byte stack slot, or a struct or a union (#137: `size` nonzero) of up to two eightbytes in
// registers, the high one alone too, on the stack whole, or ignored; wider values are refused. The semantic caller
// retains C's default-promotion and va_arg type-compatibility rules. A successful va_arg leaves one result of
// result_class while preserving values already on both stacks: a scalar's value, or an aggregate's address (U64).
// Other operations leave no value. Branch destinations are absolute indices in the enclosing output stream, including
// when its prefix is nonempty.
//
// A va_arg register path advances only its used offsets; its overflow path rounds up to the plan's alignment and
// advances overflow_arg_area by stack_bytes. An aggregate's register path copies each of its eightbytes, 8 bytes at a
// time, from the save area into `object`, a frame object of the caller's with room for size rounded up to 8, whose
// address it leaves; its overflow path leaves the address of its bytes in the overflow area, and an ignored one
// object's address, the list untouched. Copy snapshots all three U64 words before storing exactly 24 bytes; end emits
// no entries because its operand effects occurred before this call. No new Tier 0 opcode or nonbaseline generated
// instruction is used. Test source/runtime frame integration independently of this typed consumer.
#ifndef BUSTER_C_BUILTIN_VARARGS_LOWER_INCLUDED
#define BUSTER_C_BUILTIN_VARARGS_LOWER_INCLUDED

#include <stddef.h>

typedef struct CBuiltinVaLowerCall CBuiltinVaLowerCall;
struct CBuiltinVaLowerCall
{
    uint32_t id;
    uint32_t list;
    uint32_t source;
    uint32_t register_save_area;
    uint32_t overflow_arg_area;
    uint32_t gp_offset;
    uint32_t fp_offset;
    uint32_t result_class;
    AbiVaArg plan;
    // va_arg of a struct or a union: its bytes (0 for a scalar), and the frame object its register path copies them to.
    uint32_t size;
    uint32_t object;
};

static bool cbuiltin_varargs_pointer(const CBuiltinLowerBuffer* buffer, uint32_t local)
{
    const bool result = local < buffer->local_count && buffer->local_classes[local] == BUSTER_TYPED_CLASS_U64;
    return result;
}

// An aggregate's plan: its address (U64) for a result, a frame object, and up to two eightbytes in registers whose
// sources count the plan's registers, the high one alone with HIGH, or its bytes on the stack, or nothing.
static bool cbuiltin_varargs_aggregate_plan(const CBuiltinLowerBuffer* buffer, const CBuiltinVaLowerCall* call)
{
    const AbiVaArg plan = call->plan;
    const uint32_t slots = (call->size + BUSTER_ABI_SLOT_BYTES - 1) / BUSTER_ABI_SLOT_BYTES * BUSTER_ABI_SLOT_BYTES;
    uint32_t general = 0;
    uint32_t vector = 0;
    for (uint32_t k = 0; k < 2; k += 1)
    {
        general += plan.sources[k] == BUSTER_ABI_VA_GENERAL ? 1 : 0;
        vector += plan.sources[k] == BUSTER_ABI_VA_VECTOR ? 1 : 0;
    }
    const bool high = (plan.flags & BUSTER_ABI_FLAG_HIGH) != 0;
    const bool object = call->object < buffer->local_count && buffer->local_classes[call->object] == BUSTER_TYPED_CLASS_VOID &&
                        (buffer->local_flags[call->object] & BUSTER_TYPED_LOCAL_ADDRESS_TAKEN) != 0;
    const bool registers = plan.kind == BUSTER_ABI_LOCATION_REGISTERS && call->size <= 2 * BUSTER_ABI_SLOT_BYTES && general == plan.integer_registers &&
                           vector == plan.vector_registers && general + vector != 0 && (plan.flags & ~BUSTER_ABI_FLAG_HIGH) == 0 &&
                           (high ? plan.sources[0] == BUSTER_ABI_VA_NONE && plan.sources[1] != BUSTER_ABI_VA_NONE && call->size > BUSTER_ABI_SLOT_BYTES
                                 : plan.sources[0] != BUSTER_ABI_VA_NONE && (plan.sources[1] == BUSTER_ABI_VA_NONE || call->size > BUSTER_ABI_SLOT_BYTES));
    const bool stack = plan.kind == BUSTER_ABI_LOCATION_STACK && plan.integer_registers == 0 && plan.vector_registers == 0 && plan.flags == 0 &&
                       plan.sources[0] == BUSTER_ABI_VA_NONE && plan.sources[1] == BUSTER_ABI_VA_NONE;
    const bool area = plan.stack_bytes == slots && plan.overflow_alignment_log2 >= BUSTER_ABI_SLOT_LOG2 && plan.overflow_alignment_log2 <= 31;
    const bool result = call->result_class == BUSTER_TYPED_CLASS_U64 && object && (plan.kind == BUSTER_ABI_LOCATION_IGNORE || ((registers || stack) && area));
    return result;
}

static bool cbuiltin_varargs_plan(const CBuiltinVaLowerCall* call)
{
    const AbiVaArg plan = call->plan;
    const bool integer = call->result_class >= BUSTER_TYPED_CLASS_I8 && call->result_class <= BUSTER_TYPED_CLASS_U64;
    const bool floating = call->result_class == BUSTER_TYPED_CLASS_F32 || call->result_class == BUSTER_TYPED_CLASS_F64;
    const bool slot = plan.stack_bytes == BUSTER_ABI_SLOT_BYTES && plan.overflow_alignment_log2 >= BUSTER_ABI_SLOT_LOG2 &&
                      plan.overflow_alignment_log2 <= 31 && plan.flags == 0 && plan.sources[1] == BUSTER_ABI_VA_NONE;
    const bool registers = plan.kind == BUSTER_ABI_LOCATION_REGISTERS &&
        ((integer && plan.integer_registers == 1 && plan.vector_registers == 0 && plan.sources[0] == BUSTER_ABI_VA_GENERAL) ||
         (floating && plan.integer_registers == 0 && plan.vector_registers == 1 && plan.sources[0] == BUSTER_ABI_VA_VECTOR));
    const bool stack = plan.kind == BUSTER_ABI_LOCATION_STACK && plan.integer_registers == 0 && plan.vector_registers == 0 && plan.sources[0] == BUSTER_ABI_VA_NONE;
    const bool result = (integer || floating) && slot && (registers || stack);
    return result;
}

static void cbuiltin_varargs_address(CBuiltinLowerRecipe* recipe, uint32_t pointer, uint32_t offset)
{
    cbuiltin_lower_local(recipe, pointer);
    if (offset != 0)
    {
        cbuiltin_lower_constant(recipe, offset);
        cbuiltin_lower_operation(recipe, BUSTER_TYPED_ADD);
    }
}

static void cbuiltin_varargs_load(CBuiltinLowerRecipe* recipe, uint32_t pointer, uint32_t offset, uint32_t class)
{
    cbuiltin_varargs_address(recipe, pointer, offset);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_LOAD, class, 0);
}

static void cbuiltin_varargs_store_local(CBuiltinLowerRecipe* recipe, uint32_t pointer, uint32_t offset, uint32_t local, uint32_t class)
{
    cbuiltin_varargs_address(recipe, pointer, offset);
    cbuiltin_lower_local(recipe, local);
    if (class != BUSTER_TYPED_CLASS_U64)
    {
        cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONVERT, class, 0);
    }
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_SET, class, 0);
}

static void cbuiltin_varargs_store_constant(CBuiltinLowerRecipe* recipe, uint32_t pointer, uint32_t offset, uint32_t value)
{
    cbuiltin_varargs_address(recipe, pointer, offset);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U32, value);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_U32, 0);
}

static void cbuiltin_varargs_start(CBuiltinLowerRecipe* recipe, const CBuiltinVaLowerCall* call, uint32_t list)
{
    cbuiltin_varargs_store_constant(recipe, list, offsetof(AbiVaList, gp_offset), call->gp_offset);
    cbuiltin_varargs_store_constant(recipe, list, offsetof(AbiVaList, fp_offset), call->fp_offset);
    cbuiltin_varargs_store_local(recipe, list, offsetof(AbiVaList, overflow_arg_area), call->overflow_arg_area, BUSTER_TYPED_CLASS_U64);
    cbuiltin_varargs_store_local(recipe, list, offsetof(AbiVaList, reg_save_area), call->register_save_area, BUSTER_TYPED_CLASS_U64);
}

static void cbuiltin_varargs_copy(CBuiltinLowerRecipe* recipe, const CBuiltinVaLowerCall* call, uint32_t list)
{
    const uint32_t source = cbuiltin_lower_temporary(recipe);
    uint32_t words[3];
    cbuiltin_lower_local(recipe, call->source);
    cbuiltin_lower_set(recipe, source);
    for (uint32_t word = 0; word < 3; word += 1)
    {
        words[word] = cbuiltin_lower_temporary(recipe);
        cbuiltin_varargs_load(recipe, source, word * sizeof(uint64_t), BUSTER_TYPED_CLASS_U64);
        cbuiltin_lower_set(recipe, words[word]);
    }
    for (uint32_t word = 0; word < 3; word += 1)
    {
        cbuiltin_varargs_store_local(recipe, list, word * sizeof(uint64_t), words[word], BUSTER_TYPED_CLASS_U64);
    }
}

static void cbuiltin_varargs_argument(CBuiltinLowerRecipe* recipe, const CBuiltinVaLowerCall* call, uint32_t list, uint32_t first)
{
    const AbiVaArg plan = call->plan;
    const uint32_t address = cbuiltin_lower_temporary(recipe);
    uint32_t branch = UINT32_MAX;
    uint32_t jump = UINT32_MAX;
    if (plan.kind == BUSTER_ABI_LOCATION_REGISTERS)
    {
        const bool general = plan.integer_registers != 0;
        const uint32_t field = general ? offsetof(AbiVaList, gp_offset) : offsetof(AbiVaList, fp_offset);
        const uint32_t stride = general ? BUSTER_ABI_SLOT_BYTES : BUSTER_ABI_SAVE_AREA_VECTOR_SLOT_BYTES;
        const uint32_t limit = (general ? BUSTER_ABI_SAVE_AREA_VECTOR_OFFSET : BUSTER_ABI_SAVE_AREA_BYTES) - stride;
        const uint32_t offset = cbuiltin_lower_temporary(recipe);
        cbuiltin_varargs_load(recipe, list, field, BUSTER_TYPED_CLASS_U32);
        cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
        cbuiltin_lower_set(recipe, offset);
        cbuiltin_lower_local(recipe, offset);
        cbuiltin_lower_constant(recipe, limit);
        cbuiltin_lower_operation(recipe, BUSTER_TYPED_LE);
        branch = recipe->count;
        cbuiltin_lower_emit(recipe, BUSTER_TYPED_BRANCH_FALSE, BUSTER_TYPED_CLASS_I32, 0);
        cbuiltin_varargs_load(recipe, list, offsetof(AbiVaList, reg_save_area), BUSTER_TYPED_CLASS_U64);
        cbuiltin_lower_local(recipe, offset);
        cbuiltin_lower_operation(recipe, BUSTER_TYPED_ADD);
        cbuiltin_lower_set(recipe, address);
        cbuiltin_lower_immediate(recipe, offset, BUSTER_TYPED_ADD, offset, stride);
        cbuiltin_varargs_store_local(recipe, list, field, offset, BUSTER_TYPED_CLASS_U32);
        jump = recipe->count;
        cbuiltin_lower_emit(recipe, BUSTER_TYPED_JUMP, BUSTER_TYPED_CLASS_VOID, 0);
    }
    if (branch != UINT32_MAX && branch < BUSTER_CBUILTIN_LOWER_ENTRIES)
    {
        recipe->values[branch] = first + recipe->count;
    }
    const uint64_t alignment = UINT64_C(1) << plan.overflow_alignment_log2;
    cbuiltin_varargs_load(recipe, list, offsetof(AbiVaList, overflow_arg_area), BUSTER_TYPED_CLASS_U64);
    cbuiltin_lower_constant(recipe, alignment - 1);
    cbuiltin_lower_operation(recipe, BUSTER_TYPED_ADD);
    cbuiltin_lower_constant(recipe, 0 - alignment);
    cbuiltin_lower_operation(recipe, BUSTER_TYPED_AND);
    cbuiltin_lower_set(recipe, address);
    cbuiltin_varargs_address(recipe, list, offsetof(AbiVaList, overflow_arg_area));
    cbuiltin_lower_local(recipe, address);
    cbuiltin_lower_constant(recipe, plan.stack_bytes);
    cbuiltin_lower_operation(recipe, BUSTER_TYPED_ADD);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_U64, 0);
    if (jump != UINT32_MAX && jump < BUSTER_CBUILTIN_LOWER_ENTRIES)
    {
        recipe->values[jump] = first + recipe->count;
    }
    cbuiltin_lower_local(recipe, address);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_LOAD, call->result_class, 0);
}

// An aggregate's va_arg (cbuiltin_varargs_aggregate_plan): its register path, when every register it needs is left,
// copies each eightbyte from the save area into the object (the general ones from gp_offset on, the vector ones from
// fp_offset on, a slot each) and advances both offsets; otherwise its overflow path takes its bytes' place in the
// overflow area. Its address is left either way, the object's for an ignored one.
static void cbuiltin_varargs_aggregate(CBuiltinLowerRecipe* recipe, const CBuiltinVaLowerCall* call, uint32_t list, uint32_t first)
{
    const AbiVaArg plan = call->plan;
    const uint32_t address = cbuiltin_lower_temporary(recipe);
    uint32_t branches[2] = { UINT32_MAX, UINT32_MAX };
    uint32_t jump = UINT32_MAX;
    if (plan.kind == BUSTER_ABI_LOCATION_REGISTERS)
    {
        static const uint32_t fields[2] = { offsetof(AbiVaList, gp_offset), offsetof(AbiVaList, fp_offset) };
        static const uint32_t strides[2] = { BUSTER_ABI_SLOT_BYTES, BUSTER_ABI_SAVE_AREA_VECTOR_SLOT_BYTES };
        static const uint32_t ends[2] = { BUSTER_ABI_SAVE_AREA_VECTOR_OFFSET, BUSTER_ABI_SAVE_AREA_BYTES };
        const uint32_t needed[2] = { plan.integer_registers, plan.vector_registers };
        uint32_t offsets[2] = { UINT32_MAX, UINT32_MAX };
        for (uint32_t kind = 0; kind < 2; kind += 1)
        {
            if (needed[kind] != 0)
            {
                offsets[kind] = cbuiltin_lower_temporary(recipe);
                cbuiltin_varargs_load(recipe, list, fields[kind], BUSTER_TYPED_CLASS_U32);
                cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
                cbuiltin_lower_set(recipe, offsets[kind]);
                cbuiltin_lower_local(recipe, offsets[kind]);
                cbuiltin_lower_constant(recipe, ends[kind] - needed[kind] * strides[kind]);
                cbuiltin_lower_operation(recipe, BUSTER_TYPED_LE);
                branches[kind] = recipe->count;
                cbuiltin_lower_emit(recipe, BUSTER_TYPED_BRANCH_FALSE, BUSTER_TYPED_CLASS_I32, 0);
            }
        }
        const uint32_t save = cbuiltin_lower_temporary(recipe);
        cbuiltin_varargs_load(recipe, list, offsetof(AbiVaList, reg_save_area), BUSTER_TYPED_CLASS_U64);
        cbuiltin_lower_set(recipe, save);
        uint32_t taken[2] = { 0, 0 };
        for (uint32_t k = 0; k < 2; k += 1)
        {
            if (plan.sources[k] != BUSTER_ABI_VA_NONE)
            {
                const uint32_t kind = plan.sources[k] == BUSTER_ABI_VA_GENERAL ? 0 : 1;
                cbuiltin_lower_emit(recipe, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, call->object);
                if (k != 0)
                {
                    cbuiltin_lower_constant(recipe, BUSTER_ABI_SLOT_BYTES);
                    cbuiltin_lower_operation(recipe, BUSTER_TYPED_ADD);
                }
                cbuiltin_lower_local(recipe, save);
                cbuiltin_lower_local(recipe, offsets[kind]);
                cbuiltin_lower_operation(recipe, BUSTER_TYPED_ADD);
                if (taken[kind] != 0)
                {
                    cbuiltin_lower_constant(recipe, taken[kind] * strides[kind]);
                    cbuiltin_lower_operation(recipe, BUSTER_TYPED_ADD);
                }
                cbuiltin_lower_emit(recipe, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_U64, 0);
                cbuiltin_lower_emit(recipe, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_U64, 0);
                taken[kind] += 1;
            }
        }
        for (uint32_t kind = 0; kind < 2; kind += 1)
        {
            if (needed[kind] != 0)
            {
                cbuiltin_lower_immediate(recipe, offsets[kind], BUSTER_TYPED_ADD, offsets[kind], needed[kind] * strides[kind]);
                cbuiltin_varargs_store_local(recipe, list, fields[kind], offsets[kind], BUSTER_TYPED_CLASS_U32);
            }
        }
        cbuiltin_lower_emit(recipe, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, call->object);
        cbuiltin_lower_set(recipe, address);
        jump = recipe->count;
        cbuiltin_lower_emit(recipe, BUSTER_TYPED_JUMP, BUSTER_TYPED_CLASS_VOID, 0);
    }
    for (uint32_t kind = 0; kind < 2; kind += 1)
    {
        if (branches[kind] != UINT32_MAX && branches[kind] < BUSTER_CBUILTIN_LOWER_ENTRIES)
        {
            recipe->values[branches[kind]] = first + recipe->count;
        }
    }
    if (plan.kind == BUSTER_ABI_LOCATION_IGNORE)
    {
        cbuiltin_lower_emit(recipe, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, call->object);
        cbuiltin_lower_set(recipe, address);
    }
    else
    {
        const uint64_t alignment = UINT64_C(1) << plan.overflow_alignment_log2;
        cbuiltin_varargs_load(recipe, list, offsetof(AbiVaList, overflow_arg_area), BUSTER_TYPED_CLASS_U64);
        cbuiltin_lower_constant(recipe, alignment - 1);
        cbuiltin_lower_operation(recipe, BUSTER_TYPED_ADD);
        cbuiltin_lower_constant(recipe, 0 - alignment);
        cbuiltin_lower_operation(recipe, BUSTER_TYPED_AND);
        cbuiltin_lower_set(recipe, address);
        cbuiltin_varargs_address(recipe, list, offsetof(AbiVaList, overflow_arg_area));
        cbuiltin_lower_local(recipe, address);
        cbuiltin_lower_constant(recipe, plan.stack_bytes);
        cbuiltin_lower_operation(recipe, BUSTER_TYPED_ADD);
        cbuiltin_lower_emit(recipe, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_U64, 0);
    }
    if (jump != UINT32_MAX && jump < BUSTER_CBUILTIN_LOWER_ENTRIES)
    {
        recipe->values[jump] = first + recipe->count;
    }
    cbuiltin_lower_local(recipe, address);
}

static CBuiltinLowerResult cbuiltin_varargs_append(CBuiltinLowerBuffer* buffer, const CBuiltinVaLowerCall* call, bool vector)
{
    CBuiltinLowerResult result = { 0 };
    CBuiltinLowerRecipe recipe = { 0 };
    const bool start = call != 0 && (call->id == CBUILTIN_VA_START || call->id == CBUILTIN_C23_VA_START);
    const bool argument = call != 0 && call->id == CBUILTIN_VA_ARG;
    const bool copy = call != 0 && call->id == CBUILTIN_VA_COPY;
    const bool end = call != 0 && call->id == CBUILTIN_VA_END;
    if (buffer == 0 || call == 0 || buffer->count > buffer->capacity || buffer->local_count > buffer->local_capacity ||
        buffer->capacity > BUSTER_TIER0_MAX_ENTRIES || buffer->local_capacity > BUSTER_TIER0_MAX_LOCALS || buffer->kinds == 0 ||
        buffer->classes == 0 || buffer->values == 0 || buffer->local_classes == 0 || buffer->local_flags == 0)
    {
        result.error = CBUILTIN_LOWER_ARGUMENT;
    }
    else if (!start && !argument && !copy && !end)
    {
        result.error = CBUILTIN_LOWER_UNSUPPORTED;
    }
    else if (!cbuiltin_varargs_pointer(buffer, call->list) || (copy && !cbuiltin_varargs_pointer(buffer, call->source)) ||
             (start && (!cbuiltin_varargs_pointer(buffer, call->register_save_area) || !cbuiltin_varargs_pointer(buffer, call->overflow_arg_area))))
    {
        result.error = CBUILTIN_LOWER_ARGUMENT;
    }
    else if (start && (call->gp_offset > BUSTER_ABI_SAVE_AREA_VECTOR_OFFSET || call->gp_offset % BUSTER_ABI_SLOT_BYTES != 0 ||
                       call->fp_offset < BUSTER_ABI_SAVE_AREA_VECTOR_OFFSET || call->fp_offset > BUSTER_ABI_SAVE_AREA_BYTES ||
                       (call->fp_offset - BUSTER_ABI_SAVE_AREA_VECTOR_OFFSET) % BUSTER_ABI_SAVE_AREA_VECTOR_SLOT_BYTES != 0))
    {
        result.error = CBUILTIN_LOWER_ARGUMENT;
    }
    else if (argument && (call->size != 0 ? !cbuiltin_varargs_aggregate_plan(buffer, call) : !cbuiltin_varargs_plan(call)))
    {
        result.error = CBUILTIN_LOWER_UNSUPPORTED;
    }
    else
    {
        recipe.temporary_first = buffer->local_count;
        if (!end)
        {
            const uint32_t list = cbuiltin_lower_temporary(&recipe);
            cbuiltin_lower_local(&recipe, call->list);
            cbuiltin_lower_set(&recipe, list);
            if (start)
            {
                cbuiltin_varargs_start(&recipe, call, list);
            }
            else if (copy)
            {
                cbuiltin_varargs_copy(&recipe, call, list);
            }
            else if (call->size != 0)
            {
                cbuiltin_varargs_aggregate(&recipe, call, list, buffer->count);
            }
            else
            {
                cbuiltin_varargs_argument(&recipe, call, list, buffer->count);
            }
        }
        if (recipe.full || recipe.count > buffer->capacity - buffer->count || recipe.temporary_count > buffer->local_capacity - buffer->local_count)
        {
            result.error = CBUILTIN_LOWER_CAPACITY;
        }
        else
        {
            result.first = buffer->count;
            result.count = recipe.count;
            result.result_class = argument ? call->result_class : BUSTER_TYPED_CLASS_VOID;
            result.temporary_first = buffer->local_count;
            result.temporary_count = recipe.temporary_count;
            if (vector)
            {
                for (uint32_t i = 0; i < recipe.count; i += 64)
                {
                    const uint32_t remaining = recipe.count - i < 64 ? recipe.count - i : 64;
                    const __mmask64 mask = UINT64_MAX >> (64 - remaining);
                    _mm512_mask_storeu_epi8(buffer->kinds + buffer->count + i, mask, _mm512_maskz_loadu_epi8(mask, recipe.kinds + i));
                    _mm512_mask_storeu_epi8(buffer->classes + buffer->count + i, mask, _mm512_maskz_loadu_epi8(mask, recipe.classes + i));
                }
                for (uint32_t i = 0; i < recipe.count; i += 8)
                {
                    const uint32_t remaining = recipe.count - i < 8 ? recipe.count - i : 8;
                    const __mmask8 mask = (__mmask8)(UINT32_C(255) >> (8 - remaining));
                    _mm512_mask_storeu_epi64(buffer->values + buffer->count + i, mask, _mm512_maskz_loadu_epi64(mask, recipe.values + i));
                }
            }
            else
            {
                for (uint32_t i = 0; i < recipe.count; i += 1)
                {
                    buffer->kinds[buffer->count + i] = recipe.kinds[i];
                    buffer->classes[buffer->count + i] = recipe.classes[i];
                    buffer->values[buffer->count + i] = recipe.values[i];
                }
            }
            memset(buffer->local_classes + buffer->local_count, BUSTER_TYPED_CLASS_U64, recipe.temporary_count);
            memset(buffer->local_flags + buffer->local_count, 0, recipe.temporary_count);
            buffer->count += recipe.count;
            buffer->local_count += recipe.temporary_count;
        }
    }
    return result;
}

static CBuiltinLowerResult cbuiltin_varargs_lower(CBuiltinLowerBuffer* buffer, const CBuiltinVaLowerCall* call)
{
    const CBuiltinLowerResult result = cbuiltin_varargs_append(buffer, call, true);
    return result;
}

static CBuiltinLowerResult cbuiltin_varargs_lower_scalar(CBuiltinLowerBuffer* buffer, const CBuiltinVaLowerCall* call)
{
    const CBuiltinLowerResult result = cbuiltin_varargs_append(buffer, call, false);
    return result;
}

#endif
