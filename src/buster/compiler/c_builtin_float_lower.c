// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #148's native F32/F64/F80 floating builtin recipes. Include after c_builtin_catalog.c and c_builtin_lower.c.
// The source producer evaluates a classification argument once into the supplied existing local, with its actual
// floating class. A fresh address-taken local of that same class preserves its representation; integer LOAD reads
// its bits. No floating comparison, arithmetic or numeric floating-to-integer conversion occurs. The recipe leaves
// one I32 value and preserves the preceding stack. Both publication paths emit baseline x86-64 operations.
// ISINF_SIGN leaves -1/0/+1 in I32. FABS/FABSF require an already prototype-converted F64/F32 argument local,
// clear only its sign representation bit and leave that FP class. A same-format signaling NaN is never quieted.
//
// A long double's (F80) argument local is a frame object (class VOID, address-taken, at least 16 bytes) holding the
// value in its first 10, whose fields integer LOADs read: no F80 temporary is made. Clang 23 classifies an x87 value
// partly by the x87's compares (isnan, fpclassify, isinf_sign's test), which take the encodings the x87 doesn't support
// (a nonzero exponent without the integer bit: unnormals, pseudo-infinities, pseudo-NaNs) as unordered and a
// pseudo-denormal (a zero exponent with it) as the number it is, and partly by its bits (isinf, isfinite, signbit);
// each recipe gives its builtin's result for every encoding. Under FENV_ACCESS (`fenv_access`), clang's strictfp
// isnan is LLVM's integer test, which also counts pseudo-denormals as NaNs. FABSL clears its argument object's sign bit
// in place and leaves its value, F80: the producer gives it an object of its own.
//
// FPCLASSIFY returns 0 NaN, 1 infinity, 2 normal, 3 subnormal or 4 zero. Its five source selector expressions are NOT
// inputs here: the source producer must implement their checked conditional evaluation schedule. In particular,
// selecting an expression with an eager five-argument call would lose Clang's source evaluation semantics.
// HUGE/INF/NAN append a raw floating CONST only when the checker has certified an exact folded value. The producer
// must first preserve any evaluated operand effects; folded=true never authorizes erasing them. Unknown NaN payloads
// need a runtime library call outside this module. Long double constants and other floating formats are refused.
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
    // The call is under #pragma STDC FENV_ACCESS ON: it classifies as clang's strictfp code does.
    bool fenv_access;
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

static uint32_t cbuiltin_float_lower_bits(CBuiltinLowerRecipe* recipe, const CBuiltinFloatLowerCall* call, uint32_t* floating)
{
    const bool narrow = call->argument_class == BUSTER_TYPED_CLASS_F32;
    *floating = cbuiltin_lower_temporary(recipe);
    const uint32_t bits = cbuiltin_lower_temporary(recipe);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_LOCAL, call->argument_class, call->argument_local);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_SET_LOCAL, call->argument_class, *floating);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, *floating);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_LOAD, narrow ? BUSTER_TYPED_CLASS_U32 : BUSTER_TYPED_CLASS_U64, 0);
    if (narrow)
    {
        cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
    }
    cbuiltin_lower_set(recipe, bits);
    return bits;
}

static void cbuiltin_float_lower_absolute(CBuiltinLowerRecipe* recipe, const CBuiltinFloatLowerCall* call)
{
    const bool narrow = call->argument_class == BUSTER_TYPED_CLASS_F32;
    uint32_t floating = 0;
    const uint32_t bits = cbuiltin_float_lower_bits(recipe, call, &floating);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, floating);
    cbuiltin_lower_local(recipe, bits);
    cbuiltin_lower_constant(recipe, narrow ? UINT64_C(0x7fffffff) : UINT64_C(0x7fffffffffffffff));
    cbuiltin_lower_operation(recipe, BUSTER_TYPED_AND);
    if (narrow)
    {
        cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U32, 0);
    }
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_SET, narrow ? BUSTER_TYPED_CLASS_U32 : BUSTER_TYPED_CLASS_U64, 0);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_LOCAL, call->argument_class, floating);
}

static void cbuiltin_float_lower_classify(CBuiltinLowerRecipe* recipe, const CBuiltinFloatLowerCall* call)
{
    const bool narrow = call->argument_class == BUSTER_TYPED_CLASS_F32;
    const uint64_t exponent_mask = narrow ? UINT64_C(0x7f800000) : UINT64_C(0x7ff0000000000000);
    const uint64_t fraction_mask = narrow ? UINT64_C(0x007fffff) : UINT64_C(0x000fffffffffffff);
    uint32_t floating = 0;
    const uint32_t bits = cbuiltin_float_lower_bits(recipe, call, &floating);
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
                if (call->id == CBUILTIN_ISINF_SIGN)
                {
                    // infinity * (1 - 2 * sign), calculated modulo 2^64 before conversion to the declared I32.
                    cbuiltin_lower_constant(recipe, 1);
                    cbuiltin_lower_local(recipe, bits);
                    cbuiltin_lower_constant(recipe, narrow ? 31 : 63);
                    cbuiltin_lower_operation(recipe, BUSTER_TYPED_SHR);
                    cbuiltin_lower_constant(recipe, 2);
                    cbuiltin_lower_operation(recipe, BUSTER_TYPED_MUL);
                    cbuiltin_lower_operation(recipe, BUSTER_TYPED_SUB);
                    cbuiltin_lower_operation(recipe, BUSTER_TYPED_MUL);
                }
            }
        }
    }
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_I32, 0);
}

// The address of a long double object's sign and exponent, its bytes 8 and 9.
static void cbuiltin_float_lower_x87_top(CBuiltinLowerRecipe* recipe, uint32_t object)
{
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, object);
    cbuiltin_lower_constant(recipe, 8);
    cbuiltin_lower_operation(recipe, BUSTER_TYPED_ADD);
}

// Pushes 1 or 0 (U64) as `slot` compares to `value` by `kind`, unsigned.
static void cbuiltin_float_lower_test(CBuiltinLowerRecipe* recipe, uint32_t kind, uint32_t slot, uint64_t value)
{
    cbuiltin_lower_local(recipe, slot);
    cbuiltin_lower_constant(recipe, value);
    cbuiltin_lower_operation(recipe, kind);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
}

// Pushes 1 - `slot`, of a slot that holds 1 or 0.
static void cbuiltin_float_lower_not(CBuiltinLowerRecipe* recipe, uint32_t slot)
{
    cbuiltin_lower_constant(recipe, 1);
    cbuiltin_lower_local(recipe, slot);
    cbuiltin_lower_operation(recipe, BUSTER_TYPED_SUB);
}

// A long double's classification from its fields: e its exponent, i its integer bit (the significand's top), m the
// significand. The x87's compares take an unsupported encoding (e != 0, i = 0) as unordered and a pseudo-denormal
// (e = 0, i = 1) as a number of at least the smallest normal's magnitude; each line below is clang's result:
//   isnan: unordered, (e != 0 && !i) || (e = 0x7fff && m > 2^63); strictfp: (e = 0) == i || (e = 0x7fff && m > 2^63).
//   isinf: the bits of an infinity, e = 0x7fff && m = 2^63; isinf_sign: that, by the sign.
//   isfinite: e = 0 || (e != 0x7fff && i).
//   fpclassify, by the compares: zero e = 0 && m = 0, subnormal e = 0 && !i && m != 0, normal i && e != 0x7fff,
//   infinity as isinf, and NaN the rest; the category is infinity + 2 normal + 3 (e = 0 && !i) + zero.
static void cbuiltin_float_lower_x87_classify(CBuiltinLowerRecipe* recipe, const CBuiltinFloatLowerCall* call)
{
    const uint64_t integer_bit = UINT64_C(0x8000000000000000);
    const uint32_t significand = cbuiltin_lower_temporary(recipe);
    const uint32_t top = cbuiltin_lower_temporary(recipe);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, call->argument_local);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_U64, 0);
    cbuiltin_lower_set(recipe, significand);
    cbuiltin_float_lower_x87_top(recipe, call->argument_local);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_U16, 0);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
    cbuiltin_lower_set(recipe, top);
    if (call->id == CBUILTIN_SIGNBIT)
    {
        cbuiltin_lower_local(recipe, top);
        cbuiltin_lower_constant(recipe, 15);
        cbuiltin_lower_operation(recipe, BUSTER_TYPED_SHR);
    }
    else
    {
        const uint32_t exponent = cbuiltin_lower_temporary(recipe);
        const uint32_t integer = cbuiltin_lower_temporary(recipe);
        const uint32_t maximum = cbuiltin_lower_temporary(recipe);
        const uint32_t minimum = cbuiltin_lower_temporary(recipe);
        cbuiltin_lower_immediate(recipe, exponent, BUSTER_TYPED_AND, top, 0x7fff);
        cbuiltin_lower_immediate(recipe, integer, BUSTER_TYPED_SHR, significand, 63);
        cbuiltin_float_lower_equal(recipe, maximum, exponent, 0x7fff);
        cbuiltin_float_lower_equal(recipe, minimum, exponent, 0);
        if (call->id == CBUILTIN_ISFINITE)
        {
            cbuiltin_lower_local(recipe, minimum);
            cbuiltin_float_lower_not(recipe, maximum);
            cbuiltin_lower_local(recipe, integer);
            cbuiltin_lower_operation(recipe, BUSTER_TYPED_AND);
            cbuiltin_lower_operation(recipe, BUSTER_TYPED_OR);
        }
        else if (call->id == CBUILTIN_ISNAN)
        {
            cbuiltin_float_lower_test(recipe, BUSTER_TYPED_GT, significand, integer_bit);
            cbuiltin_lower_local(recipe, maximum);
            cbuiltin_lower_operation(recipe, BUSTER_TYPED_AND);
            if (call->fenv_access)
            {
                cbuiltin_lower_local(recipe, minimum);
                cbuiltin_lower_local(recipe, integer);
                cbuiltin_lower_operation(recipe, BUSTER_TYPED_EQ);
                cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
            }
            else
            {
                cbuiltin_float_lower_not(recipe, minimum);
                cbuiltin_float_lower_not(recipe, integer);
                cbuiltin_lower_operation(recipe, BUSTER_TYPED_AND);
            }
            cbuiltin_lower_operation(recipe, BUSTER_TYPED_OR);
        }
        else
        {
            cbuiltin_float_lower_test(recipe, BUSTER_TYPED_EQ, significand, integer_bit);
            cbuiltin_lower_local(recipe, maximum);
            cbuiltin_lower_operation(recipe, BUSTER_TYPED_AND);
            if (call->id == CBUILTIN_ISINF_SIGN)
            {
                // infinity * (1 - 2 * sign), calculated modulo 2^64 before conversion to the declared I32.
                cbuiltin_lower_constant(recipe, 1);
                cbuiltin_lower_local(recipe, top);
                cbuiltin_lower_constant(recipe, 15);
                cbuiltin_lower_operation(recipe, BUSTER_TYPED_SHR);
                cbuiltin_lower_constant(recipe, 2);
                cbuiltin_lower_operation(recipe, BUSTER_TYPED_MUL);
                cbuiltin_lower_operation(recipe, BUSTER_TYPED_SUB);
                cbuiltin_lower_operation(recipe, BUSTER_TYPED_MUL);
            }
            else if (call->id == CBUILTIN_FPCLASSIFY)
            {
                cbuiltin_lower_constant(recipe, 2);
                cbuiltin_lower_local(recipe, integer);
                cbuiltin_float_lower_not(recipe, maximum);
                cbuiltin_lower_operation(recipe, BUSTER_TYPED_AND);
                cbuiltin_lower_operation(recipe, BUSTER_TYPED_MUL);
                cbuiltin_lower_operation(recipe, BUSTER_TYPED_ADD);
                cbuiltin_lower_constant(recipe, 3);
                cbuiltin_lower_local(recipe, minimum);
                cbuiltin_float_lower_not(recipe, integer);
                cbuiltin_lower_operation(recipe, BUSTER_TYPED_AND);
                cbuiltin_lower_operation(recipe, BUSTER_TYPED_MUL);
                cbuiltin_lower_operation(recipe, BUSTER_TYPED_ADD);
                cbuiltin_float_lower_test(recipe, BUSTER_TYPED_EQ, significand, 0);
                cbuiltin_lower_local(recipe, minimum);
                cbuiltin_lower_operation(recipe, BUSTER_TYPED_AND);
                cbuiltin_lower_operation(recipe, BUSTER_TYPED_ADD);
            }
        }
    }
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_I32, 0);
}

// fabsl: the object's sign bit cleared in place, then its value. No x87 operation touches the bits.
static void cbuiltin_float_lower_x87_absolute(CBuiltinLowerRecipe* recipe, const CBuiltinFloatLowerCall* call)
{
    cbuiltin_float_lower_x87_top(recipe, call->argument_local);
    cbuiltin_float_lower_x87_top(recipe, call->argument_local);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_U16, 0);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
    cbuiltin_lower_constant(recipe, 0x7fff);
    cbuiltin_lower_operation(recipe, BUSTER_TYPED_AND);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U16, 0);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_U16, 0);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_F80, call->argument_local);
}

static CBuiltinLowerResult cbuiltin_float_lower_append(CBuiltinLowerBuffer* buffer, const CBuiltinFloatLowerCall* call, bool vector)
{
    CBuiltinLowerResult result = { 0 };
    CBuiltinLowerRecipe recipe = { 0 };
    const bool classification = call != 0 && call->id >= CBUILTIN_ISNAN && call->id <= CBUILTIN_ISINF_SIGN;
    const bool absolute = call != 0 && (call->id == CBUILTIN_FABS || call->id == CBUILTIN_FABSF || call->id == CBUILTIN_FABSL);
    const bool argument = classification || absolute;
    const bool x87 = argument && call->argument_class == BUSTER_TYPED_CLASS_F80;
    const uint32_t constant_class = call != 0 ? cbuiltin_float_lower_constant_class(call->id) : BUSTER_TYPED_CLASS_VOID;
    // A long double's argument is a frame object (class VOID) that its address reaches.
    const uint32_t argument_local_class = x87 ? BUSTER_TYPED_CLASS_VOID : argument ? call->argument_class : BUSTER_TYPED_CLASS_VOID;
    if (buffer == 0 || call == 0 || buffer->count > buffer->capacity || buffer->local_count > buffer->local_capacity ||
        buffer->capacity > BUSTER_TIER0_MAX_ENTRIES || buffer->local_capacity > BUSTER_TIER0_MAX_LOCALS || buffer->kinds == 0 ||
        buffer->classes == 0 || buffer->values == 0 || buffer->local_classes == 0 || buffer->local_flags == 0)
    {
        result.error = CBUILTIN_LOWER_ARGUMENT;
    }
    else if (!argument && constant_class == BUSTER_TYPED_CLASS_VOID)
    {
        result.error = CBUILTIN_LOWER_UNSUPPORTED;
    }
    else if (argument && call->argument_class != BUSTER_TYPED_CLASS_F32 && call->argument_class != BUSTER_TYPED_CLASS_F64 && !x87)
    {
        result.error = CBUILTIN_LOWER_UNSUPPORTED;
    }
    else if (absolute && call->argument_class != (call->id == CBUILTIN_FABSF ? BUSTER_TYPED_CLASS_F32 :
                                                  call->id == CBUILTIN_FABSL ? BUSTER_TYPED_CLASS_F80 : BUSTER_TYPED_CLASS_F64))
    {
        result.error = CBUILTIN_LOWER_ARGUMENT;
    }
    else if (argument && (call->argument_local >= buffer->local_count || buffer->local_classes[call->argument_local] != argument_local_class ||
                          (x87 && (buffer->local_flags[call->argument_local] & BUSTER_TYPED_LOCAL_ADDRESS_TAKEN) == 0)))
    {
        result.error = CBUILTIN_LOWER_ARGUMENT;
    }
    else if (!argument && !call->folded)
    {
        result.error = CBUILTIN_LOWER_UNSUPPORTED;
    }
    else if (!argument && (call->constant_class != constant_class || !cbuiltin_float_lower_constant_valid(call)))
    {
        result.error = CBUILTIN_LOWER_ARGUMENT;
    }
    else
    {
        recipe.temporary_first = buffer->local_count;
        if (classification && x87)
        {
            cbuiltin_float_lower_x87_classify(&recipe, call);
        }
        else if (absolute && x87)
        {
            cbuiltin_float_lower_x87_absolute(&recipe, call);
        }
        else if (classification)
        {
            cbuiltin_float_lower_classify(&recipe, call);
        }
        else if (absolute)
        {
            cbuiltin_float_lower_absolute(&recipe, call);
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
            result.result_class = classification ? BUSTER_TYPED_CLASS_I32 : absolute ? call->argument_class : constant_class;
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
            // An F32 or F64 recipe's first temporary is its argument's address-taken copy; an F80 one's are all U64.
            if (argument && !x87)
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
