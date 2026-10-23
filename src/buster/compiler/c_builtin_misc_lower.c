// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #148's baseline hint identities and memory recipes. Include after c_builtin_lower.c. The source producer checks
// arity/types and evaluates every runtime argument once into the supplied existing local; this stage evaluates no
// source expression. EXPECT's two locals are I64 (target long), pointers and sizes are U64, and memset's value is I32.
// Optional hint integers retain their checked integer machine class. Alignment promises remain producer metadata.
// PREFETCH is a valid hint no-op; x86-64 extract/frob return-address representations are the pointer identity.
//
// Memory operations use bounded-size emitted byte loops, never host memory reads or compiler-time unrolling of a
// runtime length. Zero length performs no memory access; memmove selects its backward loop only for an overlapping
// higher destination. memcmp compares unsigned bytes and returns their signed difference. strlen requires a valid
// terminated source string. These contracts preserve C's ordinary invalid-pointer and overlap undefined behavior.
// All loops preserve the enclosing value stack, use absolute enclosing-stream branch destinations and initialize
// their U64 temporaries before use. Return values have their declared machine classes. No helper allocation occurs.
//
// Publication has the same disjoint-column and transactional capacity contract as cbuiltin_lower, with exact scalar
// and masked vector copy twins. ALLOCA, FRAME_ADDRESS, RETURN_ADDRESS, TRAP and UNREACHABLE are explicitly refused:
// this module cannot invent a dynamic frame, frame-chain access or terminating typed operation.
#ifndef BUSTER_C_BUILTIN_MISC_LOWER_INCLUDED
#define BUSTER_C_BUILTIN_MISC_LOWER_INCLUDED

typedef struct CBuiltinMiscLowerCall CBuiltinMiscLowerCall;
struct CBuiltinMiscLowerCall
{
    CBuiltinId id;
    uint32_t arguments[3];
    uint32_t argument_count;
};

static bool cbuiltin_misc_lower_local_class(const CBuiltinLowerBuffer* buffer, uint32_t local, uint32_t class)
{
    const bool result = local < buffer->local_count && buffer->local_classes[local] == class;
    return result;
}

static bool cbuiltin_misc_lower_integer(const CBuiltinLowerBuffer* buffer, uint32_t local)
{
    const bool result = local < buffer->local_count && buffer->local_classes[local] >= BUSTER_TYPED_CLASS_I8 && buffer->local_classes[local] <= BUSTER_TYPED_CLASS_U64;
    return result;
}

static void cbuiltin_misc_lower_patch(CBuiltinLowerRecipe* recipe, uint32_t branch, uint32_t first)
{
    if (branch < BUSTER_CBUILTIN_LOWER_ENTRIES)
    {
        recipe->values[branch] = (uint64_t)first + recipe->count;
    }
}

static uint32_t cbuiltin_misc_lower_branch(CBuiltinLowerRecipe* recipe, uint32_t kind)
{
    const uint32_t result = recipe->count;
    cbuiltin_lower_emit(recipe, kind, kind == BUSTER_TYPED_JUMP ? BUSTER_TYPED_CLASS_VOID : BUSTER_TYPED_CLASS_I32, 0);
    return result;
}

static void cbuiltin_misc_lower_address(CBuiltinLowerRecipe* recipe, uint32_t pointer, uint32_t index)
{
    cbuiltin_lower_local(recipe, pointer);
    cbuiltin_lower_local(recipe, index);
    cbuiltin_lower_operation(recipe, BUSTER_TYPED_ADD);
}

static void cbuiltin_misc_lower_byte(CBuiltinLowerRecipe* recipe, uint32_t pointer, uint32_t index, uint32_t output)
{
    cbuiltin_misc_lower_address(recipe, pointer, index);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_U8, 0);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
    cbuiltin_lower_set(recipe, output);
}

static void cbuiltin_misc_lower_copy_byte(CBuiltinLowerRecipe* recipe, const CBuiltinMiscLowerCall* call, uint32_t index)
{
    cbuiltin_misc_lower_address(recipe, call->arguments[0], index);
    if (call->id == CBUILTIN_MEMSET)
    {
        cbuiltin_lower_emit(recipe, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_I32, call->arguments[1]);
        cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U8, 0);
    }
    else
    {
        cbuiltin_misc_lower_address(recipe, call->arguments[1], index);
        cbuiltin_lower_emit(recipe, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_U8, 0);
    }
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_U8, 0);
}

static void cbuiltin_misc_lower_copy(CBuiltinLowerRecipe* recipe, const CBuiltinMiscLowerCall* call, uint32_t first)
{
    const uint32_t index = cbuiltin_lower_temporary(recipe);
    uint32_t backward_end = UINT32_MAX;
    if (call->id == CBUILTIN_MEMMOVE)
    {
        cbuiltin_lower_local(recipe, call->arguments[0]);
        cbuiltin_lower_local(recipe, call->arguments[1]);
        cbuiltin_lower_operation(recipe, BUSTER_TYPED_GT);
        const uint32_t lower_destination = cbuiltin_misc_lower_branch(recipe, BUSTER_TYPED_BRANCH_FALSE);
        cbuiltin_lower_local(recipe, call->arguments[0]);
        cbuiltin_lower_local(recipe, call->arguments[1]);
        cbuiltin_lower_operation(recipe, BUSTER_TYPED_SUB);
        cbuiltin_lower_local(recipe, call->arguments[2]);
        cbuiltin_lower_operation(recipe, BUSTER_TYPED_LT);
        const uint32_t disjoint = cbuiltin_misc_lower_branch(recipe, BUSTER_TYPED_BRANCH_FALSE);
        cbuiltin_lower_local(recipe, call->arguments[2]);
        cbuiltin_lower_set(recipe, index);
        const uint32_t head = first + recipe->count;
        cbuiltin_lower_local(recipe, index);
        cbuiltin_lower_constant(recipe, 0);
        cbuiltin_lower_operation(recipe, BUSTER_TYPED_NE);
        const uint32_t zero = cbuiltin_misc_lower_branch(recipe, BUSTER_TYPED_BRANCH_FALSE);
        cbuiltin_lower_immediate(recipe, index, BUSTER_TYPED_SUB, index, 1);
        cbuiltin_misc_lower_copy_byte(recipe, call, index);
        cbuiltin_lower_emit(recipe, BUSTER_TYPED_JUMP, BUSTER_TYPED_CLASS_VOID, head);
        cbuiltin_misc_lower_patch(recipe, zero, first);
        backward_end = cbuiltin_misc_lower_branch(recipe, BUSTER_TYPED_JUMP);
        cbuiltin_misc_lower_patch(recipe, lower_destination, first);
        cbuiltin_misc_lower_patch(recipe, disjoint, first);
    }
    cbuiltin_lower_constant(recipe, 0);
    cbuiltin_lower_set(recipe, index);
    const uint32_t head = first + recipe->count;
    cbuiltin_lower_local(recipe, index);
    cbuiltin_lower_local(recipe, call->arguments[2]);
    cbuiltin_lower_operation(recipe, BUSTER_TYPED_LT);
    const uint32_t zero = cbuiltin_misc_lower_branch(recipe, BUSTER_TYPED_BRANCH_FALSE);
    cbuiltin_misc_lower_copy_byte(recipe, call, index);
    cbuiltin_lower_immediate(recipe, index, BUSTER_TYPED_ADD, index, 1);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_JUMP, BUSTER_TYPED_CLASS_VOID, head);
    cbuiltin_misc_lower_patch(recipe, zero, first);
    cbuiltin_misc_lower_patch(recipe, backward_end, first);
    cbuiltin_lower_local(recipe, call->arguments[0]);
}

static void cbuiltin_misc_lower_compare(CBuiltinLowerRecipe* recipe, const CBuiltinMiscLowerCall* call, uint32_t first)
{
    const uint32_t index = cbuiltin_lower_temporary(recipe);
    const uint32_t left = cbuiltin_lower_temporary(recipe);
    const uint32_t right = cbuiltin_lower_temporary(recipe);
    const uint32_t result = cbuiltin_lower_temporary(recipe);
    cbuiltin_lower_constant(recipe, 0);
    cbuiltin_lower_set(recipe, index);
    cbuiltin_lower_constant(recipe, 0);
    cbuiltin_lower_set(recipe, result);
    const uint32_t head = first + recipe->count;
    cbuiltin_lower_local(recipe, index);
    cbuiltin_lower_local(recipe, call->arguments[2]);
    cbuiltin_lower_operation(recipe, BUSTER_TYPED_LT);
    const uint32_t zero = cbuiltin_misc_lower_branch(recipe, BUSTER_TYPED_BRANCH_FALSE);
    cbuiltin_misc_lower_byte(recipe, call->arguments[0], index, left);
    cbuiltin_misc_lower_byte(recipe, call->arguments[1], index, right);
    cbuiltin_lower_local(recipe, left);
    cbuiltin_lower_local(recipe, right);
    cbuiltin_lower_operation(recipe, BUSTER_TYPED_EQ);
    const uint32_t same = cbuiltin_misc_lower_branch(recipe, BUSTER_TYPED_BRANCH_TRUE);
    cbuiltin_lower_binary(recipe, result, BUSTER_TYPED_SUB, left, right);
    const uint32_t different = cbuiltin_misc_lower_branch(recipe, BUSTER_TYPED_JUMP);
    cbuiltin_misc_lower_patch(recipe, same, first);
    cbuiltin_lower_immediate(recipe, index, BUSTER_TYPED_ADD, index, 1);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_JUMP, BUSTER_TYPED_CLASS_VOID, head);
    cbuiltin_misc_lower_patch(recipe, zero, first);
    cbuiltin_misc_lower_patch(recipe, different, first);
    cbuiltin_lower_local(recipe, result);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_I32, 0);
}

static void cbuiltin_misc_lower_length(CBuiltinLowerRecipe* recipe, const CBuiltinMiscLowerCall* call, uint32_t first)
{
    const uint32_t index = cbuiltin_lower_temporary(recipe);
    cbuiltin_lower_constant(recipe, 0);
    cbuiltin_lower_set(recipe, index);
    const uint32_t head = first + recipe->count;
    cbuiltin_misc_lower_address(recipe, call->arguments[0], index);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_U8, 0);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U8, 0);
    cbuiltin_lower_operation(recipe, BUSTER_TYPED_EQ);
    const uint32_t end = cbuiltin_misc_lower_branch(recipe, BUSTER_TYPED_BRANCH_TRUE);
    cbuiltin_lower_immediate(recipe, index, BUSTER_TYPED_ADD, index, 1);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_JUMP, BUSTER_TYPED_CLASS_VOID, head);
    cbuiltin_misc_lower_patch(recipe, end, first);
    cbuiltin_lower_local(recipe, index);
}

static CBuiltinLowerResult cbuiltin_misc_append(CBuiltinLowerBuffer* buffer, const CBuiltinMiscLowerCall* call, bool vector)
{
    CBuiltinLowerResult result = { 0 };
    CBuiltinLowerRecipe recipe = { 0 };
    uint32_t minimum = 0;
    uint32_t maximum = 0;
    uint32_t result_class = BUSTER_TYPED_CLASS_VOID;
    if (buffer == 0 || call == 0 || buffer->count > buffer->capacity || buffer->local_count > buffer->local_capacity ||
        buffer->capacity > BUSTER_TIER0_MAX_ENTRIES || buffer->local_capacity > BUSTER_TIER0_MAX_LOCALS || buffer->kinds == 0 ||
        buffer->classes == 0 || buffer->values == 0 || buffer->local_classes == 0 || buffer->local_flags == 0)
    {
        result.error = CBUILTIN_LOWER_ARGUMENT;
    }
    else
    {
        switch (call->id)
        {
        case CBUILTIN_EXPECT: minimum = maximum = 2; result_class = BUSTER_TYPED_CLASS_I64; break;
        case CBUILTIN_ASSUME_ALIGNED: minimum = 2; maximum = 3; result_class = BUSTER_TYPED_CLASS_U64; break;
        case CBUILTIN_PREFETCH: minimum = 1; maximum = 3; break;
        case CBUILTIN_EXTRACT_RETURN_ADDR:
        case CBUILTIN_FROB_RETURN_ADDR:
        case CBUILTIN_STRLEN: minimum = maximum = 1; result_class = BUSTER_TYPED_CLASS_U64; break;
        case CBUILTIN_MEMCPY:
        case CBUILTIN_MEMMOVE:
        case CBUILTIN_MEMSET: minimum = maximum = 3; result_class = BUSTER_TYPED_CLASS_U64; break;
        case CBUILTIN_MEMCMP: minimum = maximum = 3; result_class = BUSTER_TYPED_CLASS_I32; break;
        default: result.error = CBUILTIN_LOWER_UNSUPPORTED; break;
        }
        if (result.error == CBUILTIN_LOWER_OK && (call->argument_count < minimum || call->argument_count > maximum))
        {
            result.error = CBUILTIN_LOWER_ARGUMENT;
        }
        for (uint32_t i = 0; result.error == CBUILTIN_LOWER_OK && i < call->argument_count; i += 1)
        {
            const uint32_t class = call->id == CBUILTIN_EXPECT ? BUSTER_TYPED_CLASS_I64 : call->id == CBUILTIN_MEMSET && i == 1 ? BUSTER_TYPED_CLASS_I32 : BUSTER_TYPED_CLASS_U64;
            const bool hint_integer = i > 0 && (call->id == CBUILTIN_PREFETCH || (call->id == CBUILTIN_ASSUME_ALIGNED && i == 1));
            const bool valid = hint_integer ? cbuiltin_misc_lower_integer(buffer, call->arguments[i]) : cbuiltin_misc_lower_local_class(buffer, call->arguments[i], class);
            if (!valid)
            {
                result.error = CBUILTIN_LOWER_ARGUMENT;
            }
        }
    }
    if (result.error == CBUILTIN_LOWER_OK)
    {
        recipe.temporary_first = buffer->local_count;
        if (call->id == CBUILTIN_MEMCPY || call->id == CBUILTIN_MEMMOVE || call->id == CBUILTIN_MEMSET)
        {
            cbuiltin_misc_lower_copy(&recipe, call, buffer->count);
        }
        else if (call->id == CBUILTIN_MEMCMP)
        {
            cbuiltin_misc_lower_compare(&recipe, call, buffer->count);
        }
        else if (call->id == CBUILTIN_STRLEN)
        {
            cbuiltin_misc_lower_length(&recipe, call, buffer->count);
        }
        else if (call->id != CBUILTIN_PREFETCH)
        {
            cbuiltin_lower_emit(&recipe, BUSTER_TYPED_LOCAL, result_class, call->arguments[0]);
        }
        if (recipe.full || recipe.count > buffer->capacity - buffer->count || recipe.temporary_count > buffer->local_capacity - buffer->local_count)
        {
            result.error = CBUILTIN_LOWER_CAPACITY;
        }
        else
        {
            result.first = buffer->count;
            result.count = recipe.count;
            result.result_class = result_class;
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

static CBuiltinLowerResult cbuiltin_misc_lower(CBuiltinLowerBuffer* buffer, const CBuiltinMiscLowerCall* call)
{
    const CBuiltinLowerResult result = cbuiltin_misc_append(buffer, call, true);
    return result;
}

static CBuiltinLowerResult cbuiltin_misc_lower_scalar(CBuiltinLowerBuffer* buffer, const CBuiltinMiscLowerCall* call)
{
    const CBuiltinLowerResult result = cbuiltin_misc_append(buffer, call, false);
    return result;
}

#endif
