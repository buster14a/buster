// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #732's bounded semantic model, independent of an instruction encoding or compiler host ISA. The shared IR
// adapter maps its compact type/op IDs to these shapes. Only this explicit oracle has a 64-byte value: ordinary
// InterpreterValue and scalar IR constants retain their existing two-word layout. There is no allocation here.
// Layout: shape/operation admission, canonical value checks, scalar lane oracle and bounded memory effects.
#ifndef BUSTER_IR_SIMD_MODEL_INCLUDED
#define BUSTER_IR_SIMD_MODEL_INCLUDED

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

typedef enum IrSimdShape
{
    IR_SIMD_NONE,
    IR_SIMD_V4I32,
    IR_SIMD_V8I32,
    IR_SIMD_V16I32,
    IR_SIMD_M8,
    IR_SIMD_M16,
    IR_SIMD_M32,
    IR_SIMD_M64,
    IR_SIMD_SHAPE_COUNT,
} IrSimdShape;

typedef enum IrSimdOperation
{
    IR_SIMD_CONSTANT,
    IR_SIMD_COPY,
    IR_SIMD_ADD,
    IR_SIMD_SUB,
    IR_SIMD_MUL,
    IR_SIMD_AND,
    IR_SIMD_OR,
    IR_SIMD_XOR,
    IR_SIMD_NOT,
    IR_SIMD_COMPARE,
    IR_SIMD_SELECT,
    IR_SIMD_LOAD,
    IR_SIMD_STORE,
    IR_SIMD_OPERATION_COUNT,
} IrSimdOperation;

typedef enum IrSimdPredicate
{
    IR_SIMD_EQ,
    IR_SIMD_NE,
    IR_SIMD_LT,
    IR_SIMD_LE,
    IR_SIMD_GT,
    IR_SIMD_GE,
    IR_SIMD_ULT,
    IR_SIMD_ULE,
    IR_SIMD_UGT,
    IR_SIMD_UGE,
    IR_SIMD_PREDICATE_COUNT,
} IrSimdPredicate;

typedef struct IrSimdValue IrSimdValue;
struct IrSimdValue
{
    uint64_t words[8];
};

typedef struct IrSimdAction IrSimdAction;
struct IrSimdAction
{
    IrSimdOperation operation;
    IrSimdShape result;
    IrSimdShape operands[3];
    IrSimdPredicate predicate;
    bool volatile_memory;
};

typedef struct IrSimdMemory IrSimdMemory;
struct IrSimdMemory
{
    uint8_t* bytes;
    uint64_t size;
    uint64_t reads;
    uint64_t writes;
    uint64_t volatile_accesses;
};

static uint32_t ir_simd_bits(IrSimdShape shape)
{
    static const uint16_t bits[IR_SIMD_SHAPE_COUNT] = { 0, 128, 256, 512, 8, 16, 32, 64 };
    const uint32_t result = (uint32_t)shape < IR_SIMD_SHAPE_COUNT ? bits[shape] : 0;
    return result;
}

static bool ir_simd_vector(IrSimdShape shape)
{
    const bool result = shape >= IR_SIMD_V4I32 && shape <= IR_SIMD_V16I32;
    return result;
}

static IrSimdShape ir_simd_mask_shape(IrSimdShape shape)
{
    const IrSimdShape result = shape == IR_SIMD_V4I32 || shape == IR_SIMD_V8I32 ? IR_SIMD_M8 :
                              shape == IR_SIMD_V16I32 ? IR_SIMD_M16 : IR_SIMD_NONE;
    return result;
}

// The shape has no signedness: signed and unsigned comparisons are distinct predicates. Mask bit n governs
// lane n; unused high bits of m8 for v4i32 selection are ignored. Comparison always clears those unused bits.
static bool ir_simd_action_valid(const IrSimdAction* action)
{
    bool valid = action != NULL && (uint32_t)action->operation < IR_SIMD_OPERATION_COUNT &&
                 (uint32_t)action->result < IR_SIMD_SHAPE_COUNT && (uint32_t)action->predicate < IR_SIMD_PREDICATE_COUNT;
    for (uint32_t i = 0; valid && i < 3; i += 1)
    {
        valid = (uint32_t)action->operands[i] < IR_SIMD_SHAPE_COUNT;
    }
    if (valid)
    {
        const IrSimdShape a = action->operands[0];
        const IrSimdShape b = action->operands[1];
        const IrSimdShape c = action->operands[2];
        const IrSimdShape type = action->result;
        valid = action->operation == IR_SIMD_COMPARE || action->predicate == IR_SIMD_EQ;
        valid = valid && (!action->volatile_memory || action->operation == IR_SIMD_LOAD || action->operation == IR_SIMD_STORE);
        switch (action->operation)
        {
            case IR_SIMD_CONSTANT:
            case IR_SIMD_LOAD: valid = valid && type != IR_SIMD_NONE && a == IR_SIMD_NONE && b == IR_SIMD_NONE && c == IR_SIMD_NONE; break;
            case IR_SIMD_COPY:
            case IR_SIMD_NOT: valid = valid && type != IR_SIMD_NONE && a == type && b == IR_SIMD_NONE && c == IR_SIMD_NONE; break;
            case IR_SIMD_ADD:
            case IR_SIMD_SUB:
            case IR_SIMD_MUL: valid = valid && ir_simd_vector(type) && a == type && b == type && c == IR_SIMD_NONE; break;
            case IR_SIMD_AND:
            case IR_SIMD_OR:
            case IR_SIMD_XOR: valid = valid && type != IR_SIMD_NONE && a == type && b == type && c == IR_SIMD_NONE; break;
            case IR_SIMD_COMPARE: valid = valid && ir_simd_vector(a) && a == b && c == IR_SIMD_NONE && type == ir_simd_mask_shape(a); break;
            case IR_SIMD_SELECT: valid = valid && ir_simd_vector(type) && a == ir_simd_mask_shape(type) && b == type && c == type; break;
            case IR_SIMD_STORE: valid = valid && type == IR_SIMD_NONE && a != IR_SIMD_NONE && b == IR_SIMD_NONE && c == IR_SIMD_NONE; break;
            default: valid = false; break;
        }
    }
    return valid;
}

static bool ir_simd_value_valid(const IrSimdValue* value, IrSimdShape shape)
{
    const uint32_t bits = ir_simd_bits(shape);
    bool valid = value != NULL && bits != 0;
    for (uint32_t word = 0; valid && word < 8; word += 1)
    {
        const uint32_t first = word * 64;
        const uint32_t remaining = bits > first ? bits - first : 0;
        const uint64_t mask = remaining >= 64 ? UINT64_MAX : remaining != 0 ? (UINT64_C(1) << remaining) - 1 : 0;
        valid = (value->words[word] & ~mask) == 0;
    }
    return valid;
}

static uint32_t ir_simd_lane(const IrSimdValue* value, uint32_t lane)
{
    const uint32_t result = (uint32_t)(value->words[lane / 2] >> ((lane % 2) * 32));
    return result;
}

static bool ir_simd_compare(uint32_t a, uint32_t b, IrSimdPredicate predicate)
{
    const uint32_t signed_a = a ^ UINT32_C(0x80000000);
    const uint32_t signed_b = b ^ UINT32_C(0x80000000);
    bool result = false;
    switch (predicate)
    {
        case IR_SIMD_EQ: result = a == b; break;
        case IR_SIMD_NE: result = a != b; break;
        case IR_SIMD_LT: result = signed_a < signed_b; break;
        case IR_SIMD_LE: result = signed_a <= signed_b; break;
        case IR_SIMD_GT: result = signed_a > signed_b; break;
        case IR_SIMD_GE: result = signed_a >= signed_b; break;
        case IR_SIMD_ULT: result = a < b; break;
        case IR_SIMD_ULE: result = a <= b; break;
        case IR_SIMD_UGT: result = a > b; break;
        case IR_SIMD_UGE: result = a >= b; break;
        default: break;
    }
    return result;
}

// Success commits one complete result/effect. Failure changes neither output nor memory/counters. Inputs may
// alias the result; a local staging value prevents cross-lane feedback. Memory addresses are bounded offsets,
// never arbitrary host pointers. Memory storage is disjoint from the value views and their pointer slice. This
// model neither authorizes native SIMD nor interprets k0 as an implicit mask.
static bool ir_simd_evaluate(const IrSimdAction* action, const IrSimdValue* const values[3], const IrSimdValue* constant,
                             IrSimdMemory* memory, uint64_t offset, IrSimdValue* output)
{
    bool valid = ir_simd_action_valid(action) && values != NULL && output != NULL;
    for (uint32_t i = 0; valid && i < 3; i += 1)
    {
        valid = action->operands[i] == IR_SIMD_NONE || ir_simd_value_valid(values[i], action->operands[i]);
    }
    const bool access = valid && (action->operation == IR_SIMD_LOAD || action->operation == IR_SIMD_STORE);
    const bool store = valid && action->operation == IR_SIMD_STORE;
    const uint32_t bits = valid ? ir_simd_bits(store ? action->operands[0] : action->result) : 0;
    const uint32_t bytes = bits / 8;
    if (access)
    {
        valid = memory != NULL && memory->bytes != NULL && offset <= memory->size && bytes <= memory->size - offset &&
                (store ? memory->writes : memory->reads) <= UINT64_MAX - bytes &&
                (!action->volatile_memory || memory->volatile_accesses <= UINT64_MAX - bytes);
    }
    IrSimdValue staged = { 0 };
    if (valid && action->operation == IR_SIMD_CONSTANT)
    {
        valid = ir_simd_value_valid(constant, action->result);
        if (valid)
        {
            staged = *constant;
        }
    }
    else if (valid && access)
    {
        for (uint32_t byte = 0; byte < bytes; byte += 1)
        {
            if (store)
            {
                const uint8_t value = (uint8_t)(values[0]->words[byte / 8] >> ((byte % 8) * 8));
                if (action->volatile_memory)
                {
                    ((volatile uint8_t*)memory->bytes)[offset + byte] = value;
                }
                else
                {
                    memory->bytes[offset + byte] = value;
                }
            }
            else
            {
                const uint8_t value = action->volatile_memory ? ((volatile const uint8_t*)memory->bytes)[offset + byte] : memory->bytes[offset + byte];
                staged.words[byte / 8] |= (uint64_t)value << ((byte % 8) * 8);
            }
        }
        memory->reads += store ? 0 : bytes;
        memory->writes += store ? bytes : 0;
        memory->volatile_accesses += action->volatile_memory ? bytes : 0;
    }
    else if (valid)
    {
        const bool mask = !ir_simd_vector(action->result) && action->operation != IR_SIMD_COMPARE;
        const uint32_t lanes = mask ? 1 : ir_simd_bits(action->operation == IR_SIMD_COMPARE ? action->operands[0] : action->result) / 32;
        for (uint32_t lane = 0; lane < lanes; lane += 1)
        {
            const uint64_t a = mask ? values[0]->words[0] : ir_simd_lane(values[0], lane);
            const uint64_t b = action->operands[1] != IR_SIMD_NONE ? (mask ? values[1]->words[0] : ir_simd_lane(values[1], lane)) : 0;
            uint64_t value = 0;
            switch (action->operation)
            {
                case IR_SIMD_COPY: value = a; break;
                case IR_SIMD_ADD: value = (uint32_t)(a + b); break;
                case IR_SIMD_SUB: value = (uint32_t)(a - b); break;
                case IR_SIMD_MUL: value = (uint32_t)(a * b); break;
                case IR_SIMD_AND: value = a & b; break;
                case IR_SIMD_OR: value = a | b; break;
                case IR_SIMD_XOR: value = a ^ b; break;
                case IR_SIMD_NOT: value = ~a; break;
                case IR_SIMD_COMPARE: staged.words[0] |= (uint64_t)ir_simd_compare((uint32_t)a, (uint32_t)b, action->predicate) << lane; break;
                case IR_SIMD_SELECT: value = ir_simd_lane(values[(values[0]->words[0] & (UINT64_C(1) << lane)) != 0 ? 1 : 2], lane); break;
                default: valid = false; break;
            }
            if (mask)
            {
                staged.words[0] = bits == 64 ? value : value & ((UINT64_C(1) << bits) - 1);
            }
            else if (action->operation != IR_SIMD_COMPARE)
            {
                staged.words[lane / 2] |= (uint64_t)(uint32_t)value << ((lane % 2) * 32);
            }
        }
    }
    if (valid)
    {
        *output = staged;
    }
    return valid;
}

#endif
