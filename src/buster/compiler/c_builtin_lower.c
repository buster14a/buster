// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #148's integer builtin expansion into the existing Tier0Function stream. Include after c_builtin.c and tier0.c.
// cbuiltin_lower and its scalar copy twin append a bounded recipe transactionally; neither evaluates a source node.
// The caller evaluates each argument once into an existing local, then supplies those slots. Overflow destinations
// are U64 pointer locals: the recipe stores the converted integer through that pointer, then leaves its I32 boolean.
// Unary recipes leave the builtin's declared result. All recipes preserve the preexisting value stack.
//
// Generated code uses only baseline integer arithmetic, comparisons, shifts and memory operations. Counts use SWAR;
// full multiplication uses four 32x32 products. The compiler's vector copy uses its host ISA, independently of that
// generated-code policy. Recipes use U64 temporaries so arithmetic wraps and right shifts are logical. Narrow source
// values are explicitly converted first. Widths 8/16/32/64 have fixed machine classes; unsigned width1 is _Bool in a
// U8 slot and explicitly truncates to one bit. A 128-bit operand (BUSTER_CBUILTIN_LOWER_WIDE) is an address-taken frame
// object holding its 16 bytes, which integer LOADs read as limbs, and a 128-bit destination takes two U64 stores; the
// exact result's limbs come from U128 multiplications of 64-bit limbs (cbuiltin_lower_overflow_wide). Other
// bit-precise memory representations require a frontend contract and are refused here. clz/ctz of zero remain
// undefined by their source contracts (the emitted recipe happens to produce the width). ffs of zero is defined to
// produce zero.
//
// Output columns and local columns must be mutually disjoint, writable to their capacities, and disjoint from the
// buffer/call objects. Existing locals must be initialized. A successful append claims its new local range and entry
// range; a failure changes no counts or bytes. No allocation, source checking, Tier 1 acceptance or source dispatch
// is implied by this module. cbuiltin_lower_parameter_class and cbuiltin_lower_type_class expose the slot contract.
#ifndef BUSTER_C_BUILTIN_LOWER_INCLUDED
#define BUSTER_C_BUILTIN_LOWER_INCLUDED

#define BUSTER_CBUILTIN_LOWER_ENTRIES 512u
#define BUSTER_CBUILTIN_LOWER_TEMPORARIES 20u
// An overflow builtin's 128-bit operand or destination type (__int128's), with BUSTER_CBUILTIN_SIGNED or not: the other
// types' descriptors are their width (1 to 64) with it, whose seven bits stop short of 128.
#define BUSTER_CBUILTIN_LOWER_WIDE 0x100u

typedef enum CBuiltinLowerError
{
    CBUILTIN_LOWER_OK,
    CBUILTIN_LOWER_UNSUPPORTED,
    CBUILTIN_LOWER_ARGUMENT,
    CBUILTIN_LOWER_CAPACITY,
} CBuiltinLowerError;

typedef struct CBuiltinLowerBuffer CBuiltinLowerBuffer;
struct CBuiltinLowerBuffer
{
    uint8_t* kinds;
    uint8_t* classes;
    uint64_t* values;
    uint32_t count;
    uint32_t capacity;
    uint8_t* local_classes;
    uint8_t* local_flags;
    uint32_t local_count;
    uint32_t local_capacity;
};

typedef struct CBuiltinLowerCall CBuiltinLowerCall;
struct CBuiltinLowerCall
{
    CBuiltinId id;
    uint32_t a;
    uint32_t b;
    uint32_t destination;
    uint32_t a_type;
    uint32_t b_type;
    uint32_t result_type;
};

typedef struct CBuiltinLowerResult CBuiltinLowerResult;
struct CBuiltinLowerResult
{
    CBuiltinLowerError error;
    uint32_t first;
    uint32_t count;
    uint32_t result_class;
    uint32_t temporary_first;
    uint32_t temporary_count;
};

typedef struct CBuiltinLowerRecipe CBuiltinLowerRecipe;
struct CBuiltinLowerRecipe
{
    uint8_t kinds[BUSTER_CBUILTIN_LOWER_ENTRIES];
    uint8_t classes[BUSTER_CBUILTIN_LOWER_ENTRIES];
    uint64_t values[BUSTER_CBUILTIN_LOWER_ENTRIES];
    uint32_t count;
    uint32_t temporary_first;
    uint32_t temporary_count;
    bool full;
};

static uint32_t cbuiltin_lower_type_class(uint32_t type)
{
    const uint32_t width = type & ~BUSTER_CBUILTIN_SIGNED;
    const bool sign = (type & BUSTER_CBUILTIN_SIGNED) != 0;
    uint32_t result = BUSTER_TYPED_CLASS_VOID;
    if (width == 1 && !sign)
    {
        result = BUSTER_TYPED_CLASS_U8;
    }
    else if (width == 8 || width == 16 || width == 32 || width == 64)
    {
        result = width == 8 ? BUSTER_TYPED_CLASS_I8 : width == 16 ? BUSTER_TYPED_CLASS_I16 : width == 32 ? BUSTER_TYPED_CLASS_I32 : BUSTER_TYPED_CLASS_I64;
        result += !sign;
    }
    return result;
}

static uint32_t cbuiltin_lower_parameter_class(CBuiltinId id)
{
    uint32_t result = BUSTER_TYPED_CLASS_VOID;
    if (id > CBUILTIN_NONE && id <= CBUILTIN_FFSLL)
    {
        const uint32_t width = id == CBUILTIN_BSWAP16 ? 16 : (id == CBUILTIN_BSWAP32 || (id >= CBUILTIN_CLZ && (id - CBUILTIN_CLZ) % 3 == 0) ? 32 : 64);
        const uint32_t type = width | (id >= CBUILTIN_FFS ? BUSTER_CBUILTIN_SIGNED : 0);
        result = cbuiltin_lower_type_class(type);
    }
    return result;
}

static void cbuiltin_lower_emit(CBuiltinLowerRecipe* recipe, uint32_t kind, uint32_t class, uint64_t value)
{
    if (recipe->count < BUSTER_CBUILTIN_LOWER_ENTRIES)
    {
        recipe->kinds[recipe->count] = (uint8_t)kind;
        recipe->classes[recipe->count] = (uint8_t)class;
        recipe->values[recipe->count] = value;
        recipe->count += 1;
    }
    else
    {
        recipe->full = true;
    }
}

static uint32_t cbuiltin_lower_temporary(CBuiltinLowerRecipe* recipe)
{
    const uint32_t result = recipe->temporary_first + recipe->temporary_count;
    recipe->temporary_count += 1;
    recipe->full |= recipe->temporary_count > BUSTER_CBUILTIN_LOWER_TEMPORARIES;
    return result;
}

static void cbuiltin_lower_local(CBuiltinLowerRecipe* recipe, uint32_t slot)
{
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, slot);
}

static void cbuiltin_lower_constant(CBuiltinLowerRecipe* recipe, uint64_t value)
{
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, value);
}

static void cbuiltin_lower_operation(CBuiltinLowerRecipe* recipe, uint32_t kind)
{
    const uint32_t class = kind >= BUSTER_TYPED_EQ && kind <= BUSTER_TYPED_GE ? BUSTER_TYPED_CLASS_I32 : BUSTER_TYPED_CLASS_U64;
    cbuiltin_lower_emit(recipe, kind, class, 0);
}

static void cbuiltin_lower_set(CBuiltinLowerRecipe* recipe, uint32_t slot)
{
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, slot);
}

static void cbuiltin_lower_binary(CBuiltinLowerRecipe* recipe, uint32_t output, uint32_t kind, uint32_t a, uint32_t b)
{
    cbuiltin_lower_local(recipe, a);
    cbuiltin_lower_local(recipe, b);
    cbuiltin_lower_operation(recipe, kind);
    if (kind >= BUSTER_TYPED_EQ && kind <= BUSTER_TYPED_GE)
    {
        cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
    }
    cbuiltin_lower_set(recipe, output);
}

static void cbuiltin_lower_immediate(CBuiltinLowerRecipe* recipe, uint32_t output, uint32_t kind, uint32_t a, uint64_t b)
{
    cbuiltin_lower_local(recipe, a);
    cbuiltin_lower_constant(recipe, b);
    cbuiltin_lower_operation(recipe, kind);
    cbuiltin_lower_set(recipe, output);
}

// Six independent-width SWAR reductions; no target POPCNT assumption. The incoming value is already zero-extended.
static void cbuiltin_lower_popcount(CBuiltinLowerRecipe* recipe, uint32_t x, uint32_t temporary)
{
    cbuiltin_lower_immediate(recipe, temporary, BUSTER_TYPED_SHR, x, 1);
    cbuiltin_lower_immediate(recipe, temporary, BUSTER_TYPED_AND, temporary, UINT64_C(0x5555555555555555));
    cbuiltin_lower_binary(recipe, x, BUSTER_TYPED_SUB, x, temporary);
    cbuiltin_lower_immediate(recipe, temporary, BUSTER_TYPED_SHR, x, 2);
    cbuiltin_lower_immediate(recipe, temporary, BUSTER_TYPED_AND, temporary, UINT64_C(0x3333333333333333));
    cbuiltin_lower_immediate(recipe, x, BUSTER_TYPED_AND, x, UINT64_C(0x3333333333333333));
    cbuiltin_lower_binary(recipe, x, BUSTER_TYPED_ADD, x, temporary);
    cbuiltin_lower_immediate(recipe, temporary, BUSTER_TYPED_SHR, x, 4);
    cbuiltin_lower_binary(recipe, x, BUSTER_TYPED_ADD, x, temporary);
    cbuiltin_lower_immediate(recipe, x, BUSTER_TYPED_AND, x, UINT64_C(0x0f0f0f0f0f0f0f0f));
    for (uint32_t shift = 8; shift <= 32; shift *= 2)
    {
        cbuiltin_lower_immediate(recipe, temporary, BUSTER_TYPED_SHR, x, shift);
        cbuiltin_lower_binary(recipe, x, BUSTER_TYPED_ADD, x, temporary);
    }
    cbuiltin_lower_immediate(recipe, x, BUSTER_TYPED_AND, x, 127);
}

static uint32_t cbuiltin_lower_unary(CBuiltinLowerRecipe* recipe, const CBuiltinLowerCall* call)
{
    const uint32_t parameter_class = cbuiltin_lower_parameter_class(call->id);
    const uint32_t width = tier0_class_bytes(parameter_class) * 8;
    const uint32_t result_class = call->id <= CBUILTIN_BSWAP64 ? parameter_class : BUSTER_TYPED_CLASS_I32;
    const uint32_t x = cbuiltin_lower_temporary(recipe);
    const uint32_t temporary = cbuiltin_lower_temporary(recipe);
    const uint32_t original = cbuiltin_lower_temporary(recipe);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_LOCAL, parameter_class, call->a);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
    cbuiltin_lower_constant(recipe, cbuiltin_mask(width));
    cbuiltin_lower_operation(recipe, BUSTER_TYPED_AND);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_STORE_LOCAL, BUSTER_TYPED_CLASS_U64, original);
    cbuiltin_lower_set(recipe, x);
    if (call->id <= CBUILTIN_BSWAP64)
    {
        const uint64_t masks[3] =
        {
            UINT64_C(0x00ff00ff00ff00ff),
            UINT64_C(0x0000ffff0000ffff),
            UINT64_C(0x00000000ffffffff),
        };
        for (uint32_t stage = 0; stage < 3; stage += 1)
        {
            const uint32_t shift = 8u << stage;
            cbuiltin_lower_immediate(recipe, temporary, BUSTER_TYPED_SHR, x, shift);
            cbuiltin_lower_immediate(recipe, temporary, BUSTER_TYPED_AND, temporary, masks[stage]);
            cbuiltin_lower_immediate(recipe, x, BUSTER_TYPED_AND, x, masks[stage]);
            cbuiltin_lower_immediate(recipe, x, BUSTER_TYPED_SHL, x, shift);
            cbuiltin_lower_binary(recipe, x, BUSTER_TYPED_OR, x, temporary);
        }
        cbuiltin_lower_immediate(recipe, x, BUSTER_TYPED_SHR, x, 64 - width);
    }
    else
    {
        if (call->id >= CBUILTIN_CLZ && call->id <= CBUILTIN_CLZLL)
        {
            for (uint32_t shift = 1; shift < width; shift *= 2)
            {
                cbuiltin_lower_immediate(recipe, temporary, BUSTER_TYPED_SHR, x, shift);
                cbuiltin_lower_binary(recipe, x, BUSTER_TYPED_OR, x, temporary);
            }
        }
        else if ((call->id >= CBUILTIN_CTZ && call->id <= CBUILTIN_CTZLL) || call->id >= CBUILTIN_FFS)
        {
            cbuiltin_lower_immediate(recipe, temporary, BUSTER_TYPED_SUB, x, 1);
            cbuiltin_lower_local(recipe, x);
            cbuiltin_lower_operation(recipe, BUSTER_TYPED_NOT);
            cbuiltin_lower_local(recipe, temporary);
            cbuiltin_lower_operation(recipe, BUSTER_TYPED_AND);
            cbuiltin_lower_constant(recipe, cbuiltin_mask(width));
            cbuiltin_lower_operation(recipe, BUSTER_TYPED_AND);
            cbuiltin_lower_set(recipe, x);
        }
        cbuiltin_lower_popcount(recipe, x, temporary);
        if (call->id >= CBUILTIN_CLZ && call->id <= CBUILTIN_CLZLL)
        {
            cbuiltin_lower_constant(recipe, width);
            cbuiltin_lower_local(recipe, x);
            cbuiltin_lower_operation(recipe, BUSTER_TYPED_SUB);
            cbuiltin_lower_set(recipe, x);
        }
        else if (call->id >= CBUILTIN_PARITY && call->id <= CBUILTIN_PARITYLL)
        {
            cbuiltin_lower_immediate(recipe, x, BUSTER_TYPED_AND, x, 1);
        }
        else if (call->id >= CBUILTIN_FFS)
        {
            cbuiltin_lower_immediate(recipe, x, BUSTER_TYPED_ADD, x, 1);
            cbuiltin_lower_local(recipe, original);
            cbuiltin_lower_constant(recipe, 0);
            cbuiltin_lower_operation(recipe, BUSTER_TYPED_NE);
            cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
            cbuiltin_lower_local(recipe, x);
            cbuiltin_lower_operation(recipe, BUSTER_TYPED_MUL);
            cbuiltin_lower_set(recipe, x);
        }
    }
    cbuiltin_lower_local(recipe, x);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONVERT, result_class, 0);
    return result_class;
}

// Low/high limbs of the unsigned product. The intermediate sums each fit a U64 exactly, including their carry bits.
static void cbuiltin_lower_product(CBuiltinLowerRecipe* recipe, uint32_t low, uint32_t high, uint32_t a, uint32_t b)
{
    const uint32_t al = cbuiltin_lower_temporary(recipe);
    const uint32_t ah = cbuiltin_lower_temporary(recipe);
    const uint32_t bl = cbuiltin_lower_temporary(recipe);
    const uint32_t bh = cbuiltin_lower_temporary(recipe);
    const uint32_t middle = cbuiltin_lower_temporary(recipe);
    const uint32_t other = cbuiltin_lower_temporary(recipe);
    const uint32_t temporary = cbuiltin_lower_temporary(recipe);
    cbuiltin_lower_immediate(recipe, al, BUSTER_TYPED_AND, a, UINT32_MAX);
    cbuiltin_lower_immediate(recipe, ah, BUSTER_TYPED_SHR, a, 32);
    cbuiltin_lower_immediate(recipe, bl, BUSTER_TYPED_AND, b, UINT32_MAX);
    cbuiltin_lower_immediate(recipe, bh, BUSTER_TYPED_SHR, b, 32);
    cbuiltin_lower_binary(recipe, low, BUSTER_TYPED_MUL, a, b);
    cbuiltin_lower_binary(recipe, temporary, BUSTER_TYPED_MUL, al, bl);
    cbuiltin_lower_immediate(recipe, temporary, BUSTER_TYPED_SHR, temporary, 32);
    cbuiltin_lower_binary(recipe, middle, BUSTER_TYPED_MUL, ah, bl);
    cbuiltin_lower_binary(recipe, middle, BUSTER_TYPED_ADD, middle, temporary);
    cbuiltin_lower_immediate(recipe, temporary, BUSTER_TYPED_AND, middle, UINT32_MAX);
    cbuiltin_lower_binary(recipe, other, BUSTER_TYPED_MUL, al, bh);
    cbuiltin_lower_binary(recipe, other, BUSTER_TYPED_ADD, other, temporary);
    cbuiltin_lower_binary(recipe, high, BUSTER_TYPED_MUL, ah, bh);
    cbuiltin_lower_immediate(recipe, temporary, BUSTER_TYPED_SHR, middle, 32);
    cbuiltin_lower_binary(recipe, high, BUSTER_TYPED_ADD, high, temporary);
    cbuiltin_lower_immediate(recipe, temporary, BUSTER_TYPED_SHR, other, 32);
    cbuiltin_lower_binary(recipe, high, BUSTER_TYPED_ADD, high, temporary);
}

// The exact result is a two-limb two's-complement integer. Comparing both limbs with the destination's extended
// low value handles all mixed signedness cases, including unsigned64 + unsigned64 and signed64 * unsigned64.
static void cbuiltin_lower_overflow(CBuiltinLowerRecipe* recipe, const CBuiltinLowerCall* call)
{
    const uint32_t a = cbuiltin_lower_temporary(recipe);
    const uint32_t b = cbuiltin_lower_temporary(recipe);
    const uint32_t an = cbuiltin_lower_temporary(recipe);
    const uint32_t bn = cbuiltin_lower_temporary(recipe);
    const uint32_t low = cbuiltin_lower_temporary(recipe);
    const uint32_t high = cbuiltin_lower_temporary(recipe);
    const uint32_t temporary = cbuiltin_lower_temporary(recipe);
    const uint32_t normalized = cbuiltin_lower_temporary(recipe);
    const uint32_t mismatch = cbuiltin_lower_temporary(recipe);
    const uint32_t slots[2] =
    {
        call->a,
        call->b,
    };
    const uint32_t types[2] =
    {
        call->a_type,
        call->b_type,
    };
    const uint32_t values[2] =
    {
        a,
        b,
    };
    const uint32_t negatives[2] =
    {
        an,
        bn,
    };
    for (uint32_t i = 0; i < 2; i += 1)
    {
        cbuiltin_lower_emit(recipe, BUSTER_TYPED_LOCAL, cbuiltin_lower_type_class(types[i]), slots[i]);
        cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
        if (types[i] == 1)
        {
            cbuiltin_lower_constant(recipe, 1);
            cbuiltin_lower_operation(recipe, BUSTER_TYPED_AND);
        }
        cbuiltin_lower_set(recipe, values[i]);
        if ((types[i] & BUSTER_CBUILTIN_SIGNED) != 0)
        {
            cbuiltin_lower_immediate(recipe, negatives[i], BUSTER_TYPED_SHR, values[i], 63);
        }
        else
        {
            cbuiltin_lower_constant(recipe, 0);
            cbuiltin_lower_set(recipe, negatives[i]);
        }
    }
    if (call->id == CBUILTIN_MUL_OVERFLOW)
    {
        cbuiltin_lower_product(recipe, low, high, a, b);
        cbuiltin_lower_binary(recipe, temporary, BUSTER_TYPED_MUL, b, an);
        cbuiltin_lower_binary(recipe, high, BUSTER_TYPED_SUB, high, temporary);
        cbuiltin_lower_binary(recipe, temporary, BUSTER_TYPED_MUL, a, bn);
        cbuiltin_lower_binary(recipe, high, BUSTER_TYPED_SUB, high, temporary);
    }
    else
    {
        const bool subtract = call->id == CBUILTIN_SUB_OVERFLOW;
        cbuiltin_lower_binary(recipe, low, subtract ? BUSTER_TYPED_SUB : BUSTER_TYPED_ADD, a, b);
        cbuiltin_lower_binary(recipe, temporary, BUSTER_TYPED_LT, subtract ? a : low, subtract ? b : a);
        cbuiltin_lower_constant(recipe, 0);
        cbuiltin_lower_local(recipe, an);
        cbuiltin_lower_operation(recipe, BUSTER_TYPED_SUB);
        cbuiltin_lower_set(recipe, high);
        cbuiltin_lower_local(recipe, high);
        cbuiltin_lower_local(recipe, bn);
        cbuiltin_lower_operation(recipe, subtract ? BUSTER_TYPED_ADD : BUSTER_TYPED_SUB);
        cbuiltin_lower_local(recipe, temporary);
        cbuiltin_lower_operation(recipe, subtract ? BUSTER_TYPED_SUB : BUSTER_TYPED_ADD);
        cbuiltin_lower_set(recipe, high);
    }
    const uint32_t destination_class = cbuiltin_lower_type_class(call->result_type);
    cbuiltin_lower_local(recipe, low);
    if (call->result_type == 1)
    {
        cbuiltin_lower_constant(recipe, 1);
        cbuiltin_lower_operation(recipe, BUSTER_TYPED_AND);
    }
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONVERT, destination_class, 0);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
    cbuiltin_lower_set(recipe, normalized);
    cbuiltin_lower_binary(recipe, mismatch, BUSTER_TYPED_NE, low, normalized);
    cbuiltin_lower_constant(recipe, 0);
    if ((call->result_type & BUSTER_CBUILTIN_SIGNED) != 0)
    {
        cbuiltin_lower_local(recipe, normalized);
        cbuiltin_lower_constant(recipe, 63);
        cbuiltin_lower_operation(recipe, BUSTER_TYPED_SHR);
        cbuiltin_lower_operation(recipe, BUSTER_TYPED_SUB);
    }
    cbuiltin_lower_local(recipe, high);
    cbuiltin_lower_operation(recipe, BUSTER_TYPED_NE);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
    cbuiltin_lower_local(recipe, mismatch);
    cbuiltin_lower_operation(recipe, BUSTER_TYPED_OR);
    cbuiltin_lower_set(recipe, mismatch);
    cbuiltin_lower_local(recipe, call->destination);
    cbuiltin_lower_local(recipe, normalized);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONVERT, destination_class, 0);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_SET, destination_class, 0);
    cbuiltin_lower_local(recipe, mismatch);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_I32, 0);
}

// A 128-bit operand's or destination's descriptor: BUSTER_CBUILTIN_LOWER_WIDE, signed or not, with no other bit beside it
// (a malformed one is refused as any other unsupported descriptor).
static bool cbuiltin_lower_wide(uint32_t type)
{
    return (type & ~BUSTER_CBUILTIN_SIGNED) == BUSTER_CBUILTIN_LOWER_WIDE;
}

// `slot` = `a` + `b` (or `a` - `b`, `kind` SUB), of U64 slots apart from `slot`, and `carry` = 1 when the sum wrapped (the
// difference borrowed), else 0.
static void cbuiltin_lower_carry(CBuiltinLowerRecipe* recipe, uint32_t slot, uint32_t carry, uint32_t kind, uint32_t a, uint32_t b)
{
    if (kind == BUSTER_TYPED_SUB)
    {
        cbuiltin_lower_binary(recipe, carry, BUSTER_TYPED_LT, a, b);
        cbuiltin_lower_binary(recipe, slot, BUSTER_TYPED_SUB, a, b);
    }
    else
    {
        cbuiltin_lower_binary(recipe, slot, BUSTER_TYPED_ADD, a, b);
        cbuiltin_lower_binary(recipe, carry, BUSTER_TYPED_LT, slot, a);
    }
}

// `low` and `high` = the 128-bit product of U64 slots `a` and `b`, by U128's multiplication of the two zero-extended.
static void cbuiltin_lower_wide_product(CBuiltinLowerRecipe* recipe, uint32_t low, uint32_t high, uint32_t a, uint32_t b)
{
    cbuiltin_lower_local(recipe, a);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U128, 0);
    cbuiltin_lower_local(recipe, b);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U128, 0);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_MUL, BUSTER_TYPED_CLASS_U128, 0);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_DUP, BUSTER_TYPED_CLASS_U128, 0);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
    cbuiltin_lower_set(recipe, low);
    cbuiltin_lower_constant(recipe, 64);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_SHR, BUSTER_TYPED_CLASS_U128, 0);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
    cbuiltin_lower_set(recipe, high);
}

// `low` and `high` = the sum of `count` U64 slots, as U128 (a column of a product: no more than four of them).
static void cbuiltin_lower_wide_column(CBuiltinLowerRecipe* recipe, uint32_t low, uint32_t high, const uint32_t* slots, uint32_t count)
{
    for (uint32_t i = 0; i < count; i += 1)
    {
        cbuiltin_lower_local(recipe, slots[i]);
        cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U128, 0);
        if (i != 0)
        {
            cbuiltin_lower_emit(recipe, BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_U128, 0);
        }
    }
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_DUP, BUSTER_TYPED_CLASS_U128, 0);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
    cbuiltin_lower_set(recipe, low);
    cbuiltin_lower_constant(recipe, 64);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_SHR, BUSTER_TYPED_CLASS_U128, 0);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
    cbuiltin_lower_set(recipe, high);
}

// (high:low) -= (b1:b0) & mask, the 128-bit subtraction a negative operand's product needs.
static void cbuiltin_lower_wide_correct(CBuiltinLowerRecipe* recipe, uint32_t low, uint32_t high, uint32_t b0, uint32_t b1, uint32_t mask, uint32_t temporary)
{
    cbuiltin_lower_binary(recipe, temporary, BUSTER_TYPED_AND, b0, mask);
    cbuiltin_lower_local(recipe, high);
    cbuiltin_lower_local(recipe, b1);
    cbuiltin_lower_local(recipe, mask);
    cbuiltin_lower_operation(recipe, BUSTER_TYPED_AND);
    cbuiltin_lower_operation(recipe, BUSTER_TYPED_SUB);
    cbuiltin_lower_local(recipe, low);
    cbuiltin_lower_local(recipe, temporary);
    cbuiltin_lower_operation(recipe, BUSTER_TYPED_LT);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
    cbuiltin_lower_operation(recipe, BUSTER_TYPED_SUB);
    cbuiltin_lower_set(recipe, high);
    cbuiltin_lower_binary(recipe, low, BUSTER_TYPED_SUB, low, temporary);
}

// A 128-bit operand or destination (__int128's): the operands as 128-bit two's complement limbs (x1:x0), a narrower
// one's x1 its extension, and the exact result as limbs r0 to r3 of 64 bits, enough of them for it in two's complement:
// three for a sum or a difference, four for a product (an unsigned one never sets the top two all ones, so it can't pass
// as a negative value). A product is the operands' patterns' unsigned one, its columns' 64-bit products by U128, less
// 2^128 times the other operand's pattern for each negative operand. The result fits a destination of width w when the
// limbs past w are the extension of its value's top bit (or zero, unsigned); the destination takes the low w bits, a
// 128-bit one its two limbs in two stores.
static void cbuiltin_lower_overflow_wide(CBuiltinLowerRecipe* recipe, const CBuiltinLowerCall* call)
{
    uint32_t limbs[2][2];
    uint32_t extensions[2];
    const uint32_t slots[2] = { call->a, call->b };
    const uint32_t types[2] = { call->a_type, call->b_type };
    for (uint32_t i = 0; i < 2; i += 1)
    {
        limbs[i][0] = cbuiltin_lower_temporary(recipe);
        limbs[i][1] = cbuiltin_lower_temporary(recipe);
        extensions[i] = cbuiltin_lower_temporary(recipe);
        if (cbuiltin_lower_wide(types[i]))
        {
            cbuiltin_lower_emit(recipe, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, slots[i]);
            cbuiltin_lower_emit(recipe, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_U64, 0);
            cbuiltin_lower_set(recipe, limbs[i][0]);
            cbuiltin_lower_emit(recipe, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, slots[i]);
            cbuiltin_lower_constant(recipe, 8);
            cbuiltin_lower_operation(recipe, BUSTER_TYPED_ADD);
            cbuiltin_lower_emit(recipe, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_U64, 0);
            cbuiltin_lower_set(recipe, limbs[i][1]);
        }
        else
        {
            cbuiltin_lower_emit(recipe, BUSTER_TYPED_LOCAL, cbuiltin_lower_type_class(types[i]), slots[i]);
            cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
            if (types[i] == 1)
            {
                cbuiltin_lower_constant(recipe, 1);
                cbuiltin_lower_operation(recipe, BUSTER_TYPED_AND);
            }
            cbuiltin_lower_set(recipe, limbs[i][0]);
            cbuiltin_lower_constant(recipe, 0);
            if ((types[i] & BUSTER_CBUILTIN_SIGNED) != 0)
            {
                cbuiltin_lower_local(recipe, limbs[i][0]);
                cbuiltin_lower_constant(recipe, 63);
                cbuiltin_lower_operation(recipe, BUSTER_TYPED_SHR);
                cbuiltin_lower_operation(recipe, BUSTER_TYPED_SUB);
            }
            cbuiltin_lower_set(recipe, limbs[i][1]);
        }
        cbuiltin_lower_constant(recipe, 0);
        if ((types[i] & BUSTER_CBUILTIN_SIGNED) != 0)
        {
            cbuiltin_lower_local(recipe, limbs[i][1]);
            cbuiltin_lower_constant(recipe, 63);
            cbuiltin_lower_operation(recipe, BUSTER_TYPED_SHR);
            cbuiltin_lower_operation(recipe, BUSTER_TYPED_SUB);
        }
        cbuiltin_lower_set(recipe, extensions[i]);
    }
    const uint32_t a0 = limbs[0][0];
    const uint32_t a1 = limbs[0][1];
    const uint32_t b0 = limbs[1][0];
    const uint32_t b1 = limbs[1][1];
    uint32_t r[4];
    for (uint32_t i = 0; i < 4; i += 1)
    {
        r[i] = cbuiltin_lower_temporary(recipe);
    }
    const uint32_t t0 = cbuiltin_lower_temporary(recipe);
    const uint32_t t1 = cbuiltin_lower_temporary(recipe);
    const uint32_t t2 = cbuiltin_lower_temporary(recipe);
    const uint32_t t3 = cbuiltin_lower_temporary(recipe);
    const bool product = call->id == CBUILTIN_MUL_OVERFLOW;
    if (product)
    {
        cbuiltin_lower_wide_product(recipe, r[0], r[1], a0, b0);
        cbuiltin_lower_wide_product(recipe, t0, t1, a0, b1);
        cbuiltin_lower_wide_product(recipe, t2, t3, a1, b0);
        const uint32_t first[3] = { r[1], t0, t2 };
        cbuiltin_lower_wide_column(recipe, r[1], t0, first, 3);
        cbuiltin_lower_wide_product(recipe, t2, r[3], a1, b1);
        const uint32_t second[4] = { t0, t1, t3, t2 };
        cbuiltin_lower_wide_column(recipe, r[2], t0, second, 4);
        cbuiltin_lower_binary(recipe, r[3], BUSTER_TYPED_ADD, r[3], t0);
        cbuiltin_lower_wide_correct(recipe, r[2], r[3], b0, b1, extensions[0], t0);
        cbuiltin_lower_wide_correct(recipe, r[2], r[3], a0, a1, extensions[1], t0);
    }
    else
    {
        // r = a + b (or a - b) limb by limb, each limb's carry (or borrow) into the next, and the extensions' sum (or
        // difference) with the last carry as r2, the extension of the 129-bit result.
        const uint32_t kind = call->id == CBUILTIN_SUB_OVERFLOW ? BUSTER_TYPED_SUB : BUSTER_TYPED_ADD;
        cbuiltin_lower_carry(recipe, r[0], t0, kind, a0, b0);
        cbuiltin_lower_carry(recipe, t1, t2, kind, a1, b1);
        cbuiltin_lower_carry(recipe, r[1], t3, kind, t1, t0);
        cbuiltin_lower_local(recipe, extensions[0]);
        cbuiltin_lower_local(recipe, extensions[1]);
        cbuiltin_lower_operation(recipe, kind);
        cbuiltin_lower_local(recipe, t2);
        cbuiltin_lower_operation(recipe, kind);
        cbuiltin_lower_local(recipe, t3);
        cbuiltin_lower_operation(recipe, kind);
        cbuiltin_lower_set(recipe, r[2]);
    }
    const uint32_t limb_count = product ? 4 : 3;
    const bool sign = (call->result_type & BUSTER_CBUILTIN_SIGNED) != 0;
    const bool wide = cbuiltin_lower_wide(call->result_type);
    const uint32_t destination_class = wide ? BUSTER_TYPED_CLASS_U64 : cbuiltin_lower_type_class(call->result_type);
    const uint32_t normalized = t1;
    const uint32_t extension = t2;
    const uint32_t mismatch = t3;
    // The destination's value as limbs, its low w bits extended: a narrower one's low limb, and its extension above it.
    cbuiltin_lower_local(recipe, r[0]);
    if (!wide)
    {
        if (call->result_type == 1)
        {
            cbuiltin_lower_constant(recipe, 1);
            cbuiltin_lower_operation(recipe, BUSTER_TYPED_AND);
        }
        cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONVERT, destination_class, 0);
        cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
    }
    cbuiltin_lower_set(recipe, normalized);
    cbuiltin_lower_constant(recipe, 0);
    if (sign)
    {
        cbuiltin_lower_local(recipe, wide ? r[1] : normalized);
        cbuiltin_lower_constant(recipe, 63);
        cbuiltin_lower_operation(recipe, BUSTER_TYPED_SHR);
        cbuiltin_lower_operation(recipe, BUSTER_TYPED_SUB);
    }
    cbuiltin_lower_set(recipe, extension);
    cbuiltin_lower_binary(recipe, mismatch, BUSTER_TYPED_NE, r[0], normalized);
    for (uint32_t limb = wide ? 2 : 1; limb < limb_count; limb += 1)
    {
        cbuiltin_lower_local(recipe, r[limb]);
        cbuiltin_lower_local(recipe, extension);
        cbuiltin_lower_operation(recipe, BUSTER_TYPED_NE);
        cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
        cbuiltin_lower_local(recipe, mismatch);
        cbuiltin_lower_operation(recipe, BUSTER_TYPED_OR);
        cbuiltin_lower_set(recipe, mismatch);
    }
    cbuiltin_lower_local(recipe, call->destination);
    cbuiltin_lower_local(recipe, normalized);
    if (!wide)
    {
        cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONVERT, destination_class, 0);
    }
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_SET, destination_class, 0);
    if (wide)
    {
        cbuiltin_lower_local(recipe, call->destination);
        cbuiltin_lower_constant(recipe, 8);
        cbuiltin_lower_operation(recipe, BUSTER_TYPED_ADD);
        cbuiltin_lower_local(recipe, r[1]);
        cbuiltin_lower_emit(recipe, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_U64, 0);
    }
    cbuiltin_lower_local(recipe, mismatch);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_I32, 0);
}

// An overflow builtin's operand or destination type the recipes take: one of a machine class, or a 128-bit one.
static bool cbuiltin_lower_type_valid(uint32_t type)
{
    return cbuiltin_lower_type_class(type) != BUSTER_TYPED_CLASS_VOID || cbuiltin_lower_wide(type);
}

// Whether `slot` holds an operand of `type`: a local of its class, or for a 128-bit one an address-taken frame object
// (class VOID) whose first 16 bytes hold it.
static bool cbuiltin_lower_slot_valid(const CBuiltinLowerBuffer* buffer, uint32_t slot, uint32_t type)
{
    const bool wide = cbuiltin_lower_wide(type);
    return buffer->local_classes[slot] == (wide ? BUSTER_TYPED_CLASS_VOID : cbuiltin_lower_type_class(type)) &&
           (!wide || (buffer->local_flags[slot] & BUSTER_TYPED_LOCAL_ADDRESS_TAKEN) != 0);
}

static CBuiltinLowerResult cbuiltin_lower_append(CBuiltinLowerBuffer* buffer, const CBuiltinLowerCall* call, bool vector)
{
    CBuiltinLowerResult result = { 0 };
    CBuiltinLowerRecipe recipe = { 0 };
    const bool unary = call != 0 && call->id > CBUILTIN_NONE && call->id <= CBUILTIN_FFSLL;
    const bool overflow = call != 0 && call->id >= CBUILTIN_ADD_OVERFLOW && call->id <= CBUILTIN_MUL_OVERFLOW;
    if (buffer == 0 || call == 0 || buffer->count > buffer->capacity || buffer->local_count > buffer->local_capacity ||
        buffer->capacity > BUSTER_TIER0_MAX_ENTRIES || buffer->local_capacity > BUSTER_TIER0_MAX_LOCALS || buffer->kinds == 0 ||
        buffer->classes == 0 || buffer->values == 0 || buffer->local_classes == 0 || buffer->local_flags == 0)
    {
        result.error = CBUILTIN_LOWER_ARGUMENT;
    }
    else if (!unary && !overflow)
    {
        result.error = CBUILTIN_LOWER_UNSUPPORTED;
    }
    else if (call->a >= buffer->local_count || (overflow && (call->b >= buffer->local_count || call->destination >= buffer->local_count)))
    {
        result.error = CBUILTIN_LOWER_ARGUMENT;
    }
    else if (unary && buffer->local_classes[call->a] != cbuiltin_lower_parameter_class(call->id))
    {
        result.error = CBUILTIN_LOWER_ARGUMENT;
    }
    else if (overflow && (!cbuiltin_lower_type_valid(call->a_type) || !cbuiltin_lower_type_valid(call->b_type) || !cbuiltin_lower_type_valid(call->result_type)))
    {
        result.error = CBUILTIN_LOWER_UNSUPPORTED;
    }
    else if (overflow && (!cbuiltin_lower_slot_valid(buffer, call->a, call->a_type) || !cbuiltin_lower_slot_valid(buffer, call->b, call->b_type) ||
                          buffer->local_classes[call->destination] != BUSTER_TYPED_CLASS_U64))
    {
        result.error = CBUILTIN_LOWER_ARGUMENT;
    }
    else
    {
        recipe.temporary_first = buffer->local_count;
        result.result_class = unary ? cbuiltin_lower_unary(&recipe, call) : BUSTER_TYPED_CLASS_I32;
        if (overflow && (cbuiltin_lower_wide(call->a_type) || cbuiltin_lower_wide(call->b_type) || cbuiltin_lower_wide(call->result_type)))
        {
            cbuiltin_lower_overflow_wide(&recipe, call);
        }
        else if (overflow)
        {
            cbuiltin_lower_overflow(&recipe, call);
        }
        if (recipe.full || recipe.count > buffer->capacity - buffer->count || recipe.temporary_count > buffer->local_capacity - buffer->local_count)
        {
            result.error = CBUILTIN_LOWER_CAPACITY;
            result.result_class = 0;
        }
        else
        {
            result.first = buffer->count;
            result.count = recipe.count;
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

static CBuiltinLowerResult cbuiltin_lower(CBuiltinLowerBuffer* buffer, const CBuiltinLowerCall* call)
{
    const CBuiltinLowerResult result = cbuiltin_lower_append(buffer, call, true);
    return result;
}

static CBuiltinLowerResult cbuiltin_lower_scalar(CBuiltinLowerBuffer* buffer, const CBuiltinLowerCall* call)
{
    const CBuiltinLowerResult result = cbuiltin_lower_append(buffer, call, false);
    return result;
}

#endif
