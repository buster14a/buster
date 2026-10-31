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
// ISNORMAL, ISSUBNORMAL, ISZERO, ISSIGNALING and ISFPCLASS test their FPClassTest mask (`class_mask`) as clang's
// llvm.is.fpclass does, by a table of 64 bits that the recipe indexes by six bits of the argument's representation
// (cbuiltin_float_lower_class_table); SIGNBITF and SIGNBITL are SIGNBIT of their already converted F32 and F80 arguments,
// and CANONICALIZE, CANONICALIZEF and CANONICALIZEL leave x * 1.0 of theirs, as clang's mulss, mulsd and fmulp do. The
// source producer classifies a vector's lanes one at a time, each loaded into the argument local (`lanes`).
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
    // The FPClassTest bits a classification tests (the checker's class_mask).
    uint32_t class_mask;
    // isfpclass of a vector, the source producer's (the recipe reads none of these): the frame objects of the vector and of
    // the result, its lanes and their bytes, each lane classified loaded into argument_local; 0 lanes for a scalar.
    uint32_t vector_local;
    uint32_t result_local;
    uint32_t lanes;
    uint32_t lane_bytes;
};

// The x87's encodings, by category: each line of cbuiltin_float_lower_x87_class gives one, its exponent and significand.
typedef enum CBuiltinFloatX87Category
{
    CBUILTIN_FLOAT_X87_ZERO,
    CBUILTIN_FLOAT_X87_DENORMAL,
    CBUILTIN_FLOAT_X87_PSEUDO_DENORMAL,
    CBUILTIN_FLOAT_X87_NORMAL,
    CBUILTIN_FLOAT_X87_UNNORMAL,
    CBUILTIN_FLOAT_X87_INFINITY,
    CBUILTIN_FLOAT_X87_PSEUDO_INFINITY,
    CBUILTIN_FLOAT_X87_SIGNALING,
    CBUILTIN_FLOAT_X87_QUIET,
    CBUILTIN_FLOAT_X87_PSEUDO_NAN,
    CBUILTIN_FLOAT_X87_CATEGORIES,
} CBuiltinFloatX87Category;

// The FPClassTest masks that LLVM tests by the inverse when the inverse is one of these (TargetLowering's
// invertFPClassTestIfSimpler), and with a compare also those that hold infinities and both NaNs.
static uint32_t cbuiltin_float_lower_invert(uint32_t test, bool compare)
{
    const uint16_t simple[] = { 0x003, 0x001, 0x002, 0x204, 0x200, 0x004, 0x108, 0x100, 0x008, 0x090, 0x080, 0x010, 0x060, 0x040, 0x020, 0x1f8,
                                0x1c0, 0x038, 0x063, 0x0f0, 0x0f3 };
    const uint16_t with_nan[] = { 0x207, 0x203, 0x007 };
    const uint32_t inverted = ~test & 0x3ffu;
    uint32_t result = 0;
    for (uint32_t k = 0; k < BUSTER_ARRAY_LENGTH(simple); k += 1)
    {
        result = inverted == simple[k] ? inverted : result;
    }
    for (uint32_t k = 0; compare && k < BUSTER_ARRAY_LENGTH(with_nan); k += 1)
    {
        result = inverted == with_nan[k] ? inverted : result;
    }
    return result;
}

// llvm.is.fpclass's integer tests of an x87 encoding (LLVM's expandIS_FPCLASS): its fields' tests of each class the
// mask holds, but zero and subnormal together are a zero exponent (a pseudo-denormal among them), and both NaNs take the
// unsupported encodings and pseudo-denormals ((exponent == 0) == integer bit) too.
static bool cbuiltin_float_lower_x87_integer(uint32_t test, CBuiltinFloatX87Category category, bool negative)
{
    const uint16_t exponents[CBUILTIN_FLOAT_X87_CATEGORIES] = { 0, 0, 0, 0x3fff, 0x3fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff };
    const uint64_t significands[CBUILTIN_FLOAT_X87_CATEGORIES] =
    {
        0,
        1,
        UINT64_C(0x8000000000000000),
        UINT64_C(0x8000000000000000),
        UINT64_C(0x4000000000000000),
        UINT64_C(0x8000000000000000),
        0,
        UINT64_C(0x8000000000000001),
        UINT64_C(0xc000000000000000),
        UINT64_C(0x4000000000000000),
    };
    const uint64_t integer_bit = UINT64_C(0x8000000000000000);
    const uint64_t quiet = UINT64_C(0xc000000000000000);
    const uint32_t inverted_test = cbuiltin_float_lower_invert(test, false);
    const uint32_t mask = inverted_test != 0 ? inverted_test : test;
    const uint64_t m = significands[category];
    const uint32_t e = exponents[category];
    const bool integer = (m & integer_bit) != 0;
    const bool both = (mask & 0xf0u) == 0xf0u;
    const uint32_t rest = both ? mask & ~0xf0u : mask;
    const uint32_t zero = rest & 0x60u;
    const uint32_t subnormal = rest & 0x90u;
    const uint32_t infinity = rest & 0x204u;
    const uint32_t nan = rest & 0x3u;
    const uint32_t normal = rest & 0x108u;
    bool result = both && e == 0;
    result |= zero != 0 && e == 0 && m == 0 && (zero == 0x60u || (zero == 0x40u ? !negative : negative));
    result |= subnormal != 0 && e == 0 && m != 0 && m < integer_bit && (subnormal == 0x90u || (subnormal == 0x80u ? !negative : negative));
    result |= infinity != 0 && e == 0x7fff && m == integer_bit && (infinity == 0x204u || (infinity == 0x200u ? !negative : negative));
    result |= nan == 0x3u && ((e == 0x7fff && m > integer_bit) || integer == (e == 0));
    result |= nan == 0x2u && e == 0x7fff && m >= quiet;
    result |= nan == 0x1u && e == 0x7fff && m > integer_bit && m < quiet;
    result |= normal != 0 && e != 0 && e != 0x7fff && integer && (normal == 0x108u || (normal == 0x100u ? !negative : negative));
    return result != (inverted_test != 0);
}

// Clang 23's -O0 code of llvm.is.fpclass for an x87 encoding of `category` and sign (LLVM's expandIS_FPCLASS under x86's
// rules: SETOEQ and SETUNE aren't legal, fabs isn't free). Outside FENV_ACCESS, a mask that one quiet compare tests takes
// the x87's compare, which takes an unsupported encoding as unordered and a pseudo-denormal as the smallest normal's
// magnitude: NaNs alone (unordered), zeros or NaNs (x ueq 0), one infinity or NaNs (x ueq inf), zeros, subnormals or NaNs
// (fabs(x) ult smallest normal), or one of those masks' inverses; any other mask takes the integer tests. test-checker
// checks every mask and encoding against clang's code.
static bool cbuiltin_float_lower_x87_class(uint32_t test, CBuiltinFloatX87Category category, bool negative, bool strict)
{
    const bool unordered = category == CBUILTIN_FLOAT_X87_UNNORMAL || category >= CBUILTIN_FLOAT_X87_PSEUDO_INFINITY;
    // The magnitude's rank: zero, below the smallest normal, the smallest normal, above it, infinity.
    const uint32_t rank = category == CBUILTIN_FLOAT_X87_ZERO ? 0 : category == CBUILTIN_FLOAT_X87_DENORMAL ? 1 : category == CBUILTIN_FLOAT_X87_PSEUDO_DENORMAL ? 2 :
                          category == CBUILTIN_FLOAT_X87_NORMAL ? 3 : 4;
    const uint32_t inverted_test = cbuiltin_float_lower_invert(test, true);
    const bool inverted = inverted_test != 0;
    const uint32_t mask = inverted ? inverted_test : test;
    // The ordered compare's answer for the mask (the inverse's, negated), or the integer tests' (2).
    uint32_t result = 2;
    if (test == 0 || test == 0x3ffu)
    {
        result = test != 0;
    }
    else if (strict)
    {
        result = 2;
    }
    else if (mask == 0x63u)
    {
        result = inverted ? !unordered && rank != 0 : unordered || rank == 0;
    }
    else if (mask == 0x3u)
    {
        result = inverted ? !unordered : unordered;
    }
    else if (mask == 0x203u || mask == 0x7u)
    {
        const bool equal = !unordered && rank == 4 && negative == (mask == 0x7u);
        result = inverted ? !unordered && !equal : unordered || equal;
    }
    else if (mask == 0xf3u)
    {
        result = inverted ? !unordered && rank >= 2 : unordered || rank < 2;
    }
    const bool answer = result == 2 ? cbuiltin_float_lower_x87_integer(test, category, negative) : result != 0;
    return answer;
}

// A classification's table: bit z | x << 1 | i << 2 | f << 3 | q << 4 | s << 5 of it is its answer for a representation
// whose exponent is zero (z) or all ones (x), whose integer bit is set (i: a float's or double's implicit one), whose
// fraction below it is zero (f), whose top fraction bit is set (q) and whose sign is set (s). A float's or double's
// answer is its class's FPClassTest bit in the mask; an x87 one's is clang's (cbuiltin_float_lower_x87_class).
static uint64_t cbuiltin_float_lower_class_table(const CBuiltinFloatLowerCall* call)
{
    const bool x87 = call->argument_class == BUSTER_TYPED_CLASS_F80;
    const bool strict = call->fenv_access;
    uint64_t table = 0;
    for (uint32_t index = 0; index < 64; index += 1)
    {
        const bool z = (index & 1) != 0;
        const bool x = (index & 2) != 0;
        const bool i = (index & 4) != 0;
        const bool f = (index & 8) != 0;
        const bool q = (index & 16) != 0;
        const bool s = (index & 32) != 0;
        // Combinations no representation has: both exponent extremes, a zero fraction with its top bit set.
        const bool possible = !(z && x) && !(f && q);
        const CBuiltinFloatX87Category category = z ? (i ? CBUILTIN_FLOAT_X87_PSEUDO_DENORMAL : f ? CBUILTIN_FLOAT_X87_ZERO : CBUILTIN_FLOAT_X87_DENORMAL) :
                                                  x ? (i ? (f ? CBUILTIN_FLOAT_X87_INFINITY : q ? CBUILTIN_FLOAT_X87_QUIET : CBUILTIN_FLOAT_X87_SIGNALING) :
                                                           (f ? CBUILTIN_FLOAT_X87_PSEUDO_INFINITY : CBUILTIN_FLOAT_X87_PSEUDO_NAN)) :
                                                  i ? CBUILTIN_FLOAT_X87_NORMAL : CBUILTIN_FLOAT_X87_UNNORMAL;
        // A float's or double's classes' FPClassTest bits, positive and negative, by category; none for an encoding they lack.
        const uint16_t positive[CBUILTIN_FLOAT_X87_CATEGORIES] = { 0x40, 0x80, 0, 0x100, 0, 0x200, 0, 0x1, 0x2, 0 };
        const uint16_t negative[CBUILTIN_FLOAT_X87_CATEGORIES] = { 0x20, 0x10, 0, 0x8, 0, 0x4, 0, 0x1, 0x2, 0 };
        const bool answer = x87 ? cbuiltin_float_lower_x87_class(call->class_mask, category, s, strict) :
                                  ((s ? negative[category] : positive[category]) & call->class_mask) != 0;
        table |= (uint64_t)(possible && answer) << index;
    }
    return table;
}

// Pushes 1 - `slot`, of a slot that holds 1 or 0.
static void cbuiltin_float_lower_not(CBuiltinLowerRecipe* recipe, uint32_t slot)
{
    cbuiltin_lower_constant(recipe, 1);
    cbuiltin_lower_local(recipe, slot);
    cbuiltin_lower_operation(recipe, BUSTER_TYPED_SUB);
}

// Pushes a slot's 1 or 0 shifted left by `shift`, for an index's sum.
static void cbuiltin_float_lower_index_bit(CBuiltinLowerRecipe* recipe, uint32_t slot, uint32_t shift)
{
    cbuiltin_lower_local(recipe, slot);
    if (shift != 0)
    {
        cbuiltin_lower_constant(recipe, shift);
        cbuiltin_lower_operation(recipe, BUSTER_TYPED_SHL);
    }
}

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

// Pushes the table's bit (cbuiltin_float_lower_class_table) that the index in `index` selects, which the classify
// recipes' common tail converts to I32.
static void cbuiltin_float_lower_class_select(CBuiltinLowerRecipe* recipe, const CBuiltinFloatLowerCall* call, uint32_t index)
{
    cbuiltin_lower_constant(recipe, cbuiltin_float_lower_class_table(call));
    cbuiltin_lower_local(recipe, index);
    cbuiltin_lower_operation(recipe, BUSTER_TYPED_SHR);
    cbuiltin_lower_constant(recipe, 1);
    cbuiltin_lower_operation(recipe, BUSTER_TYPED_AND);
}

// ISNORMAL ... ISFPCLASS of a float or a double: its index from its bits, the integer bit its exponent's being nonzero.
static void cbuiltin_float_lower_class_ieee(CBuiltinLowerRecipe* recipe, const CBuiltinFloatLowerCall* call, uint32_t bits)
{
    const bool narrow = call->argument_class == BUSTER_TYPED_CLASS_F32;
    const uint64_t exponent_mask = narrow ? UINT64_C(0x7f800000) : UINT64_C(0x7ff0000000000000);
    const uint64_t fraction_mask = narrow ? UINT64_C(0x007fffff) : UINT64_C(0x000fffffffffffff);
    const uint32_t exponent = cbuiltin_lower_temporary(recipe);
    const uint32_t zero = cbuiltin_lower_temporary(recipe);
    const uint32_t maximum = cbuiltin_lower_temporary(recipe);
    const uint32_t fraction = cbuiltin_lower_temporary(recipe);
    const uint32_t index = cbuiltin_lower_temporary(recipe);
    cbuiltin_lower_immediate(recipe, exponent, BUSTER_TYPED_AND, bits, exponent_mask);
    cbuiltin_float_lower_equal(recipe, zero, exponent, 0);
    cbuiltin_float_lower_equal(recipe, maximum, exponent, exponent_mask);
    cbuiltin_lower_immediate(recipe, fraction, BUSTER_TYPED_AND, bits, fraction_mask);
    cbuiltin_float_lower_equal(recipe, fraction, fraction, 0);
    cbuiltin_float_lower_index_bit(recipe, zero, 0);
    cbuiltin_float_lower_index_bit(recipe, maximum, 1);
    cbuiltin_lower_operation(recipe, BUSTER_TYPED_ADD);
    cbuiltin_float_lower_not(recipe, zero);
    cbuiltin_lower_constant(recipe, 2);
    cbuiltin_lower_operation(recipe, BUSTER_TYPED_SHL);
    cbuiltin_lower_operation(recipe, BUSTER_TYPED_ADD);
    cbuiltin_float_lower_index_bit(recipe, fraction, 3);
    cbuiltin_lower_operation(recipe, BUSTER_TYPED_ADD);
    // The top fraction bit (the quiet bit), then the sign, each moved to its place in the index.
    cbuiltin_lower_local(recipe, bits);
    cbuiltin_lower_constant(recipe, narrow ? 18 : 47);
    cbuiltin_lower_operation(recipe, BUSTER_TYPED_SHR);
    cbuiltin_lower_constant(recipe, 16);
    cbuiltin_lower_operation(recipe, BUSTER_TYPED_AND);
    cbuiltin_lower_operation(recipe, BUSTER_TYPED_ADD);
    cbuiltin_lower_local(recipe, bits);
    cbuiltin_lower_constant(recipe, narrow ? 31 : 63);
    cbuiltin_lower_operation(recipe, BUSTER_TYPED_SHR);
    cbuiltin_lower_constant(recipe, 5);
    cbuiltin_lower_operation(recipe, BUSTER_TYPED_SHL);
    cbuiltin_lower_operation(recipe, BUSTER_TYPED_ADD);
    cbuiltin_lower_set(recipe, index);
    cbuiltin_float_lower_class_select(recipe, call, index);
}

static void cbuiltin_float_lower_classify(CBuiltinLowerRecipe* recipe, const CBuiltinFloatLowerCall* call)
{
    const bool narrow = call->argument_class == BUSTER_TYPED_CLASS_F32;
    const uint64_t exponent_mask = narrow ? UINT64_C(0x7f800000) : UINT64_C(0x7ff0000000000000);
    const uint64_t fraction_mask = narrow ? UINT64_C(0x007fffff) : UINT64_C(0x000fffffffffffff);
    uint32_t floating = 0;
    const uint32_t bits = cbuiltin_float_lower_bits(recipe, call, &floating);
    if (cbuiltin_float_class(call->id))
    {
        cbuiltin_float_lower_class_ieee(recipe, call, bits);
    }
    else if (call->id == CBUILTIN_SIGNBIT || call->id == CBUILTIN_SIGNBITF)
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
    if (cbuiltin_float_class(call->id))
    {
        // The index from the fields: the integer bit (63), the top fraction bit (62) and the sign (79) moved to their places.
        const uint32_t exponent = cbuiltin_lower_temporary(recipe);
        const uint32_t zero = cbuiltin_lower_temporary(recipe);
        const uint32_t maximum = cbuiltin_lower_temporary(recipe);
        const uint32_t fraction = cbuiltin_lower_temporary(recipe);
        const uint32_t index = cbuiltin_lower_temporary(recipe);
        cbuiltin_lower_immediate(recipe, exponent, BUSTER_TYPED_AND, top, 0x7fff);
        cbuiltin_float_lower_equal(recipe, zero, exponent, 0);
        cbuiltin_float_lower_equal(recipe, maximum, exponent, 0x7fff);
        cbuiltin_lower_immediate(recipe, fraction, BUSTER_TYPED_AND, significand, UINT64_C(0x7fffffffffffffff));
        cbuiltin_float_lower_equal(recipe, fraction, fraction, 0);
        cbuiltin_float_lower_index_bit(recipe, zero, 0);
        cbuiltin_float_lower_index_bit(recipe, maximum, 1);
        cbuiltin_lower_operation(recipe, BUSTER_TYPED_ADD);
        cbuiltin_lower_local(recipe, significand);
        cbuiltin_lower_constant(recipe, 61);
        cbuiltin_lower_operation(recipe, BUSTER_TYPED_SHR);
        cbuiltin_lower_constant(recipe, 4);
        cbuiltin_lower_operation(recipe, BUSTER_TYPED_AND);
        cbuiltin_lower_operation(recipe, BUSTER_TYPED_ADD);
        cbuiltin_float_lower_index_bit(recipe, fraction, 3);
        cbuiltin_lower_operation(recipe, BUSTER_TYPED_ADD);
        cbuiltin_lower_local(recipe, significand);
        cbuiltin_lower_constant(recipe, 58);
        cbuiltin_lower_operation(recipe, BUSTER_TYPED_SHR);
        cbuiltin_lower_constant(recipe, 16);
        cbuiltin_lower_operation(recipe, BUSTER_TYPED_AND);
        cbuiltin_lower_operation(recipe, BUSTER_TYPED_ADD);
        cbuiltin_lower_local(recipe, top);
        cbuiltin_lower_constant(recipe, 10);
        cbuiltin_lower_operation(recipe, BUSTER_TYPED_SHR);
        cbuiltin_lower_constant(recipe, 32);
        cbuiltin_lower_operation(recipe, BUSTER_TYPED_AND);
        cbuiltin_lower_operation(recipe, BUSTER_TYPED_ADD);
        cbuiltin_lower_set(recipe, index);
        cbuiltin_float_lower_class_select(recipe, call, index);
    }
    else if (call->id == CBUILTIN_SIGNBIT || call->id == CBUILTIN_SIGNBITL)
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

// canonicalize: its argument times 1.0 of its class, as clang's mulss, mulsd and fld1 and fmulp, which quiet a signaling
// NaN and leave an unsupported x87 encoding the default NaN; a long double's 1.0 is 1 converted, exactly.
static void cbuiltin_float_lower_canonicalize(CBuiltinLowerRecipe* recipe, const CBuiltinFloatLowerCall* call)
{
    const bool narrow = call->argument_class == BUSTER_TYPED_CLASS_F32;
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_LOCAL, call->argument_class, call->argument_local);
    if (call->argument_class == BUSTER_TYPED_CLASS_F80)
    {
        cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, 1);
        cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_F80, 0);
    }
    else
    {
        cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONST, call->argument_class, narrow ? UINT64_C(0x3f800000) : UINT64_C(0x3ff0000000000000));
    }
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_MUL, call->argument_class, 0);
}

// The class a builtin's prototype converts its argument to (fabs's, signbitf's, canonicalize's), or VOID for one that
// takes it unconverted.
static uint32_t cbuiltin_float_lower_prototype(CBuiltinId id)
{
    const uint32_t result = id == CBUILTIN_FABSF || id == CBUILTIN_SIGNBITF || id == CBUILTIN_CANONICALIZEF ? BUSTER_TYPED_CLASS_F32 :
                            id == CBUILTIN_FABS || id == CBUILTIN_CANONICALIZE                              ? BUSTER_TYPED_CLASS_F64 :
                            id == CBUILTIN_FABSL || id == CBUILTIN_SIGNBITL || id == CBUILTIN_CANONICALIZEL ? BUSTER_TYPED_CLASS_F80 :
                                                                                                              BUSTER_TYPED_CLASS_VOID;
    return result;
}

static CBuiltinLowerResult cbuiltin_float_lower_append(CBuiltinLowerBuffer* buffer, const CBuiltinFloatLowerCall* call, bool vector)
{
    (void)vector;

    CBuiltinLowerResult result = { 0 };
    CBuiltinLowerRecipe recipe = { 0 };
    const bool classification = call != 0 && ((call->id >= CBUILTIN_ISNAN && call->id <= CBUILTIN_ISINF_SIGN) || cbuiltin_float_class(call->id) ||
                                              cbuiltin_float_signbit(call->id));
    const bool absolute = call != 0 && (call->id == CBUILTIN_FABS || call->id == CBUILTIN_FABSF || call->id == CBUILTIN_FABSL);
    const bool canonicalize = call != 0 && cbuiltin_float_canonicalize(call->id);
    const bool argument = classification || absolute || canonicalize;
    const uint32_t prototype = call != 0 ? cbuiltin_float_lower_prototype(call->id) : BUSTER_TYPED_CLASS_VOID;
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
    else if ((prototype != BUSTER_TYPED_CLASS_VOID && call->argument_class != prototype) ||
             (cbuiltin_float_class(call->id) && call->class_mask > 0x3ffu))
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
        if (canonicalize)
        {
            cbuiltin_float_lower_canonicalize(&recipe, call);
        }
        else if (classification && x87)
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
            result.result_class = classification ? BUSTER_TYPED_CLASS_I32 : absolute || canonicalize ? call->argument_class : constant_class;
            result.temporary_first = buffer->local_count;
            result.temporary_count = recipe.temporary_count;
            if (BUSTER_VECTOR_PATH(vector))
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
            // An F32 or F64 recipe's first temporary is its argument's address-taken copy; an F80 one's are all U64, and a
            // canonicalization has none.
            if ((classification || absolute) && !x87)
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

static CBuiltinLowerResult cbuiltin_float_lower_scalar(CBuiltinLowerBuffer* buffer, const CBuiltinFloatLowerCall* call);

static CBuiltinLowerResult cbuiltin_float_lower(CBuiltinLowerBuffer* buffer, const CBuiltinFloatLowerCall* call)
{
#if !BUSTER_HOST_VECTOR
    return cbuiltin_float_lower_scalar(buffer, call);
#else
    const CBuiltinLowerResult result = cbuiltin_float_lower_append(buffer, call, true);
    return result;
#endif
}

static CBuiltinLowerResult cbuiltin_float_lower_scalar(CBuiltinLowerBuffer* buffer, const CBuiltinFloatLowerCall* call)
{
    const CBuiltinLowerResult result = cbuiltin_float_lower_append(buffer, call, false);
    return result;
}

#endif
