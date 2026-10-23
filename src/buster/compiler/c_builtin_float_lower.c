// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #148's native F32/F64 floating builtin recipes. Include after c_builtin_catalog.c and c_builtin_lower.c.
// The source producer evaluates a classification argument once into the supplied existing local, with its actual
// floating class. A fresh address-taken local of that same class preserves its representation; integer LOAD reads
// its bits. No floating comparison, arithmetic or numeric floating-to-integer conversion occurs. The recipe leaves
// one I32 value and preserves the preceding stack. Both publication paths emit baseline x86-64 operations.
//
// FPCLASSIFY returns 0 NaN, 1 infinity, 2 normal, 3 subnormal or 4 zero. Its five source selector expressions are NOT
// inputs here: the source producer must implement their checked conditional evaluation schedule. In particular,
// selecting an expression with an eager five-argument call would lose Clang's source evaluation semantics.
// HUGE/INF/NAN append a raw floating CONST only when the checker has certified an exact folded value. The producer
// must first preserve any evaluated operand effects; folded=true never authorizes erasing them. Unknown NaN payloads
// need a runtime library call outside this module. Long double and other floating formats are explicitly refused.
//
// Publication inherits cbuiltin_lower's disjoint-column, initialized-local and transactional capacity contracts.
// No source dispatch, allocation, source checking, FP environment change or Tier 1 acceptance is supplied here.
#ifndef BUSTER_C_BUILTIN_FLOAT_LOWER_INCLUDED
#define BUSTER_C_BUILTIN_FLOAT_LOWER_INCLUDED

typedef struct CBuiltinFloatLowerCall CBuiltinFloatLowerCall;
struct CBuiltinFloatLowerCall
{
    CBuiltinId id;
    uint32_t argument_local;
    uint32_t argument_class;
    bool folded;
    uint32_t constant_class;
    uint64_t constant_bits;
};

static uint32_t cbuiltin_float_lower_constant_class(CBuiltinId id)
{
    uint32_t result = BUSTER_TYPED_CLASS_VOID;
    if (id == CBUILTIN_HUGE_VALF || id == CBUILTIN_INFF || id == CBUILTIN_NANF)
    {
        result = BUSTER_TYPED_CLASS_F32;
    }
    else if (id == CBUILTIN_HUGE_VAL || id == CBUILTIN_INF || id == CBUILTIN_NAN)
    {
        result = BUSTER_TYPED_CLASS_F64;
    }
    return result;
}

static bool cbuiltin_float_lower_constant_valid(const CBuiltinFloatLowerCall* call)
{
    const bool narrow = call->constant_class == BUSTER_TYPED_CLASS_F32;
    const uint64_t infinity = narrow ? UINT64_C(0x7f800000) : UINT64_C(0x7ff0000000000000);
    const uint64_t quiet = narrow ? UINT64_C(0x00400000) : UINT64_C(0x0008000000000000);
    const uint64_t sign = narrow ? UINT64_C(0x80000000) : UINT64_C(0x8000000000000000);
    const bool nan = call->id == CBUILTIN_NAN || call->id == CBUILTIN_NANF;
    const bool result = (!narrow || call->constant_bits <= UINT32_MAX) &&
                        (nan ? (call->constant_bits & (infinity | quiet | sign)) == (infinity | quiet) : call->constant_bits == infinity);
    return result;
}

static void cbuiltin_float_lower_equal(CBuiltinLowerRecipe* recipe, uint32_t output, uint32_t input, uint64_t value)
{
    cbuiltin_lower_local(recipe, input);
    cbuiltin_lower_constant(recipe, value);
    cbuiltin_lower_operation(recipe, BUSTER_TYPED_EQ);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
    cbuiltin_lower_set(recipe, output);
}

static void cbuiltin_float_lower_classify(CBuiltinLowerRecipe* recipe, const CBuiltinFloatLowerCall* call)
{
    const bool narrow = call->argument_class == BUSTER_TYPED_CLASS_F32;
    const uint64_t exponent_mask = narrow ? UINT64_C(0x7f800000) : UINT64_C(0x7ff0000000000000);
    const uint64_t fraction_mask = narrow ? UINT64_C(0x007fffff) : UINT64_C(0x000fffffffffffff);
    const uint32_t floating = cbuiltin_lower_temporary(recipe);
    const uint32_t bits = cbuiltin_lower_temporary(recipe);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_LOCAL, call->argument_class, call->argument_local);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_SET_LOCAL, call->argument_class, floating);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, floating);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_LOAD, narrow ? BUSTER_TYPED_CLASS_U32 : BUSTER_TYPED_CLASS_U64, 0);
    if (narrow)
    {
        cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
    }
    cbuiltin_lower_set(recipe, bits);
    if (call->id == CBUILTIN_SIGNBIT)
    {
        cbuiltin_lower_local(recipe, bits);
        cbuiltin_lower_constant(recipe, narrow ? 31 : 63);
        cbuiltin_lower_operation(recipe, BUSTER_TYPED_SHR);
    }
    else
    {
        const uint32_t exponent = cbuiltin_lower_temporary(recipe);
        const uint32_t maximum = cbuiltin_lower_temporary(recipe);
        cbuiltin_lower_immediate(recipe, exponent, BUSTER_TYPED_AND, bits, exponent_mask);
        cbuiltin_float_lower_equal(recipe, maximum, exponent, exponent_mask);
        if (call->id == CBUILTIN_ISFINITE)
        {
            cbuiltin_lower_constant(recipe, 1);
            cbuiltin_lower_local(recipe, maximum);
            cbuiltin_lower_operation(recipe, BUSTER_TYPED_SUB);
        }
        else
        {
            const uint32_t fraction_zero = cbuiltin_lower_temporary(recipe);
            cbuiltin_lower_immediate(recipe, fraction_zero, BUSTER_TYPED_AND, bits, fraction_mask);
            cbuiltin_float_lower_equal(recipe, fraction_zero, fraction_zero, 0);
            if (call->id == CBUILTIN_FPCLASSIFY)
            {
                const uint32_t exponent_zero = cbuiltin_lower_temporary(recipe);
                cbuiltin_float_lower_equal(recipe, exponent_zero, exponent, 0);
                // 2 + exponent_zero * (1 + fraction_zero) - maximum * (2 - fraction_zero).
                // Exponent zero and exponent maximum are disjoint, giving exactly 0..4 without FP comparisons.
                cbuiltin_lower_constant(recipe, 2);
                cbuiltin_lower_local(recipe, exponent_zero);
                cbuiltin_lower_constant(recipe, 1);
                cbuiltin_lower_local(recipe, fraction_zero);
                cbuiltin_lower_operation(recipe, BUSTER_TYPED_ADD);
                cbuiltin_lower_operation(recipe, BUSTER_TYPED_MUL);
                cbuiltin_lower_operation(recipe, BUSTER_TYPED_ADD);
                cbuiltin_lower_local(recipe, maximum);
                cbuiltin_lower_constant(recipe, 2);
                cbuiltin_lower_local(recipe, fraction_zero);
                cbuiltin_lower_operation(recipe, BUSTER_TYPED_SUB);
                cbuiltin_lower_operation(recipe, BUSTER_TYPED_MUL);
                cbuiltin_lower_operation(recipe, BUSTER_TYPED_SUB);
            }
            else
            {
                cbuiltin_lower_local(recipe, maximum);
                if (call->id == CBUILTIN_ISNAN)
                {
                    cbuiltin_lower_constant(recipe, 1);
                    cbuiltin_lower_local(recipe, fraction_zero);
                    cbuiltin_lower_operation(recipe, BUSTER_TYPED_SUB);
                }
                else
                {
                    cbuiltin_lower_local(recipe, fraction_zero);
                }
                cbuiltin_lower_operation(recipe, BUSTER_TYPED_AND);
            }
        }
    }
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_I32, 0);
}

static CBuiltinLowerResult cbuiltin_float_lower_append(CBuiltinLowerBuffer* buffer, const CBuiltinFloatLowerCall* call, bool vector)
{
    CBuiltinLowerResult result = { 0 };
    CBuiltinLowerRecipe recipe = { 0 };
    const bool classification = call != 0 && call->id >= CBUILTIN_ISNAN && call->id <= CBUILTIN_FPCLASSIFY;
    const uint32_t constant_class = call != 0 ? cbuiltin_float_lower_constant_class(call->id) : BUSTER_TYPED_CLASS_VOID;
    if (buffer == 0 || call == 0 || buffer->count > buffer->capacity || buffer->local_count > buffer->local_capacity ||
        buffer->capacity > BUSTER_TIER0_MAX_ENTRIES || buffer->local_capacity > BUSTER_TIER0_MAX_LOCALS || buffer->kinds == 0 ||
        buffer->classes == 0 || buffer->values == 0 || buffer->local_classes == 0 || buffer->local_flags == 0)
    {
        result.error = CBUILTIN_LOWER_ARGUMENT;
    }
    else if (!classification && constant_class == BUSTER_TYPED_CLASS_VOID)
    {
        result.error = CBUILTIN_LOWER_UNSUPPORTED;
    }
    else if (classification && call->argument_class != BUSTER_TYPED_CLASS_F32 && call->argument_class != BUSTER_TYPED_CLASS_F64)
    {
        result.error = CBUILTIN_LOWER_UNSUPPORTED;
    }
    else if (classification && (call->argument_local >= buffer->local_count || buffer->local_classes[call->argument_local] != call->argument_class))
    {
        result.error = CBUILTIN_LOWER_ARGUMENT;
    }
    else if (!classification && !call->folded)
    {
        result.error = CBUILTIN_LOWER_UNSUPPORTED;
    }
    else if (!classification && (call->constant_class != constant_class || !cbuiltin_float_lower_constant_valid(call)))
    {
        result.error = CBUILTIN_LOWER_ARGUMENT;
    }
    else
    {
        recipe.temporary_first = buffer->local_count;
        if (classification)
        {
            cbuiltin_float_lower_classify(&recipe, call);
        }
        else
        {
            cbuiltin_lower_emit(&recipe, BUSTER_TYPED_CONST, constant_class, call->constant_bits);
        }
        if (recipe.full || recipe.count > buffer->capacity - buffer->count || recipe.temporary_count > buffer->local_capacity - buffer->local_count)
        {
            result.error = CBUILTIN_LOWER_CAPACITY;
        }
        else
        {
            result.first = buffer->count;
            result.count = recipe.count;
            result.result_class = classification ? BUSTER_TYPED_CLASS_I32 : constant_class;
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
            if (classification)
            {
                buffer->local_classes[buffer->local_count] = (uint8_t)call->argument_class;
                buffer->local_flags[buffer->local_count] = BUSTER_TYPED_LOCAL_ADDRESS_TAKEN;
            }
            buffer->count += recipe.count;
            buffer->local_count += recipe.temporary_count;
        }
    }
    return result;
}

static CBuiltinLowerResult cbuiltin_float_lower(CBuiltinLowerBuffer* buffer, const CBuiltinFloatLowerCall* call)
{
    const CBuiltinLowerResult result = cbuiltin_float_lower_append(buffer, call, true);
    return result;
}

static CBuiltinLowerResult cbuiltin_float_lower_scalar(CBuiltinLowerBuffer* buffer, const CBuiltinFloatLowerCall* call)
{
    const CBuiltinLowerResult result = cbuiltin_float_lower_append(buffer, call, false);
    return result;
}

#endif
