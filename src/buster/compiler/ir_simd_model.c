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
    // Shifts of each lane by the other operand's same lane (#141's element types), unsigned counts: a left or logical
    // right shift by the lane's bits or more is 0, an arithmetic one every bit the sign, as vpsllv, vpsrlv and vpsrav
    // compute them.
    IR_SIMD_SHL,
    IR_SIMD_LSHR,
    IR_SIMD_ASHR,
    // IEEE arithmetic of floating lanes (#141's element types), at the default rounding, as the x86's packed forms: a
    // NaN operand's quieted NaN, the first's where both are NaNs (x86's rule), and the default NaN of an invalid one.
    IR_SIMD_FADD,
    IR_SIMD_FSUB,
    IR_SIMD_FMUL,
    IR_SIMD_FDIV,
    // The width changes (#1840), bits moved whole, no lanes read: the `index`th part of a, of the result's width, a wider
    // vector (extract); a in the result's low bits and zeros past them (widen); and a with its `index`th part of b's width
    // replaced by b, a narrower vector (insert).
    IR_SIMD_EXTRACT,
    IR_SIMD_WIDEN,
    IR_SIMD_INSERT,
    // The permutes (#1840), lanes of a and b of the result's shape moved, no lanes read as numbers, their immediate the
    // action's `index` (ir.c's BUSTER_IR_INTRINSIC_PERMUTE to _SHUFFLE_LANES): a's lane that b's lane's low bits name, of
    // 1 << index bytes (permute); a's byte in the same 16 bytes that b's byte's low 4 bits name, or 0 where its top bit is
    // set (shuffle); b's lanes followed by a's from the count in index's low 4 bits on, of 1 << (index >> 4) bytes (align);
    // and a's 16-byte lanes for the low half and b's for the high half, as index's bits name them (shuffle_lanes).
    IR_SIMD_PERMUTE,
    IR_SIMD_SHUFFLE,
    IR_SIMD_ALIGN,
    IR_SIMD_SHUFFLE_LANES,
    // The conversions (#1840), of integer lanes whose sizes and mode the action's `index` holds (ir.c's
    // BUSTER_IR_INTRINSIC_EXTEND and _TRUNCATE): each of the result's lanes a's same lane widened, by zeros or its sign;
    // each of a's lanes narrowed, wrapping, or saturating a signed lane to the signed range or an unsigned one to the
    // unsigned range, into the result's low bytes, zeros past them.
    IR_SIMD_EXTEND,
    IR_SIMD_TRUNCATE,
    // A shift of each integer lane by one scalar count (#1840, ir.c's BUSTER_IR_INTRINSIC_SHIFT), b's low 32 bits read
    // unsigned (b a scalar, its shape none), the lanes' bytes' log2 and the shift in the action's `index`: a left or
    // logical right shift by the lane's bits or more is 0, an arithmetic one every bit the sign, as vpsllw to vpsraq
    // compute them by a count in an xmm register.
    IR_SIMD_SHIFT,
    // A binary operation of integer lanes (#1840, ir.c's BUSTER_IR_INTRINSIC_LANE_OP), the lanes' bytes' log2 and the
    // operation in the action's `index`: the unsigned or signed minimum or maximum, and the unsigned or signed saturating
    // sum or difference a - b.
    IR_SIMD_LANE_OP,
    // A permute of two tables (#1840, ir.c's BUSTER_IR_INTRINSIC_PERMUTE2), its lanes' bytes' log2 the action's `index`:
    // lane i is, of a's lanes followed by c's, the one that b's lane i's low bits name, as many as name twice the lanes.
    IR_SIMD_PERMUTE2,
    // A bitwise function of three vectors (#1840, ir.c's BUSTER_IR_INTRINSIC_TERNLOG), its truth table the action's
    // `index`: each bit is the index's bit (a << 2 | b << 1 | c) of a's, b's and c's same bit.
    IR_SIMD_TERNLOG,
    // The compress and the expand of a vector's lanes (#1840, ir.c's BUSTER_IR_INTRINSIC_COMPRESS and _EXPAND), the lanes'
    // bytes' log2 the action's `index`, b the mask of those lanes: a's lanes whose bits are set, packed from lane 0, and c's
    // past them; and a's lanes from lane 0 at the set bits' lanes, c's at the others.
    IR_SIMD_COMPRESS,
    IR_SIMD_EXPAND,
    // An AES round of each 16-byte lane (#1840, ir.c's BUSTER_IR_INTRINSIC_AES): ShiftRows, SubBytes and MixColumns of a
    // (the last round, the action's `index` 1, without MixColumns), then b xor'ed: vaesenc and vaesenclast.
    IR_SIMD_AES,
    // One lane of a vector (#1840, ir.c's BUSTER_IR_INTRINSIC_LANE_EXTRACT and _INSERT), its bytes' log2 in the action's
    // `index` bits 0 and 1 and its index past them: a's lane zero-extended into a scalar result (its shape none, the value
    // in words[0]); and a with the lane replaced by b's low bits, b a scalar (its shape none). And the sign bit of each of
    // a's lanes from bit 0 of a scalar result (#1840, ir.c's BUSTER_IR_INTRINSIC_MOVEMASK), the lanes' bytes' log2 the
    // action's `index`.
    IR_SIMD_LANE_EXTRACT,
    IR_SIMD_LANE_INSERT,
    IR_SIMD_MOVEMASK,
    // A unary operation of each of a's integer lanes (#1840, ir.c's BUSTER_IR_INTRINSIC_LANE_UNARY), the action's `index`
    // the IR's flags: the lanes' bytes' log2 and the operation (population count, leading zero count, conflicts).
    IR_SIMD_LANE_UNARY,
    IR_SIMD_OPERATION_COUNT,
} IrSimdOperation;

// A vector's lanes (#141's element types): what an operation reads its operands' bits as. A vector shape is a container
// of 128, 256 or 512 bits whatever its elements, as the typed stream's VECTOR class is bytes alone, and each operation on
// it says its lanes, as WebAssembly's v128 operations do: 32-bit integers (the default, the shapes' names'), 8-, 16- and
// 64-bit ones, or floats and doubles. A copy, the bitwise operations, a constant and memory read none (I32).
typedef enum IrSimdLane
{
    IR_SIMD_LANE_I32,
    IR_SIMD_LANE_I8,
    IR_SIMD_LANE_I16,
    IR_SIMD_LANE_I64,
    IR_SIMD_LANE_F32,
    IR_SIMD_LANE_F64,
    IR_SIMD_LANE_COUNT,
} IrSimdLane;

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
    IrSimdLane lane;
    // A width change's part (#1840: IR_SIMD_EXTRACT's and IR_SIMD_INSERT's), in units of the narrower vector; 0 for the
    // other operations.
    uint8_t index;
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

// A lane's bits.
static uint32_t ir_simd_lane_bits(IrSimdLane lane)
{
    static const uint8_t bits[IR_SIMD_LANE_COUNT] = { 32, 8, 16, 64, 32, 64 };
    const uint32_t result = (uint32_t)lane < IR_SIMD_LANE_COUNT ? bits[lane] : 0;
    return result;
}

static bool ir_simd_lane_floating(IrSimdLane lane)
{
    const bool result = lane == IR_SIMD_LANE_F32 || lane == IR_SIMD_LANE_F64;
    return result;
}

// The mask of a comparison of a vector's lanes, a bit per lane: m8 for 8 lanes or fewer, else m16, m32 or m64.
static IrSimdShape ir_simd_lane_mask(IrSimdShape shape, IrSimdLane lane)
{
    const uint32_t bits = ir_simd_lane_bits(lane);
    const uint32_t lanes = ir_simd_vector(shape) && bits != 0 ? ir_simd_bits(shape) / bits : 0;
    const IrSimdShape result = lanes == 0 ? IR_SIMD_NONE : lanes <= 8 ? IR_SIMD_M8 : lanes == 16 ? IR_SIMD_M16 : lanes == 32 ? IR_SIMD_M32 : IR_SIMD_M64;
    return result;
}

// Whether `operation` reads its operands as `lane`: the integer arithmetic and shifts integer lanes, the floating
// arithmetic floating ones, a comparison and a selection any, and every other operation none but the default.
static bool ir_simd_lane_valid(IrSimdOperation operation, IrSimdLane lane)
{
    bool valid = (uint32_t)lane < IR_SIMD_LANE_COUNT;
    switch (operation)
    {
        case IR_SIMD_ADD:
        case IR_SIMD_SUB:
        case IR_SIMD_MUL:
        case IR_SIMD_SHL:
        case IR_SIMD_LSHR:
        case IR_SIMD_ASHR: valid = valid && !ir_simd_lane_floating(lane); break;
        case IR_SIMD_FADD:
        case IR_SIMD_FSUB:
        case IR_SIMD_FMUL:
        case IR_SIMD_FDIV: valid = valid && ir_simd_lane_floating(lane); break;
        case IR_SIMD_COMPARE:
        case IR_SIMD_SELECT: break;
        default: valid = valid && lane == IR_SIMD_LANE_I32; break;
    }
    return valid;
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
        const IrSimdLane lane = action->lane;
        valid = action->operation == IR_SIMD_COMPARE || action->predicate == IR_SIMD_EQ;
        valid = valid && (action->index == 0 || action->operation == IR_SIMD_EXTRACT || action->operation == IR_SIMD_INSERT || action->operation == IR_SIMD_PERMUTE ||
                          action->operation == IR_SIMD_ALIGN || action->operation == IR_SIMD_SHUFFLE_LANES || action->operation == IR_SIMD_EXTEND ||
                          action->operation == IR_SIMD_TRUNCATE || action->operation == IR_SIMD_SHIFT ||
                          action->operation == IR_SIMD_LANE_OP || action->operation == IR_SIMD_PERMUTE2 || action->operation == IR_SIMD_TERNLOG ||
                          action->operation == IR_SIMD_COMPRESS || action->operation == IR_SIMD_EXPAND || action->operation == IR_SIMD_AES ||
                          action->operation == IR_SIMD_LANE_EXTRACT || action->operation == IR_SIMD_LANE_INSERT || action->operation == IR_SIMD_MOVEMASK ||
                          action->operation == IR_SIMD_LANE_UNARY);
        valid = valid && (!action->volatile_memory || action->operation == IR_SIMD_LOAD || action->operation == IR_SIMD_STORE);
        valid = valid && ir_simd_lane_valid(action->operation, lane);
        // A floating comparison's predicates are C's: ==, !=, <, <=, > and >= (no unsigned ones).
        valid = valid && (!ir_simd_lane_floating(lane) || action->operation != IR_SIMD_COMPARE || action->predicate <= IR_SIMD_GE);
        switch (action->operation)
        {
            case IR_SIMD_CONSTANT:
            case IR_SIMD_LOAD: valid = valid && type != IR_SIMD_NONE && a == IR_SIMD_NONE && b == IR_SIMD_NONE && c == IR_SIMD_NONE; break;
            case IR_SIMD_COPY:
            case IR_SIMD_NOT: valid = valid && type != IR_SIMD_NONE && a == type && b == IR_SIMD_NONE && c == IR_SIMD_NONE; break;
            case IR_SIMD_ADD:
            case IR_SIMD_SUB:
            case IR_SIMD_MUL:
            case IR_SIMD_SHL:
            case IR_SIMD_LSHR:
            case IR_SIMD_ASHR:
            case IR_SIMD_FADD:
            case IR_SIMD_FSUB:
            case IR_SIMD_FMUL:
            case IR_SIMD_FDIV: valid = valid && ir_simd_vector(type) && a == type && b == type && c == IR_SIMD_NONE; break;
            case IR_SIMD_AND:
            case IR_SIMD_OR:
            case IR_SIMD_XOR: valid = valid && type != IR_SIMD_NONE && a == type && b == type && c == IR_SIMD_NONE; break;
            case IR_SIMD_COMPARE: valid = valid && ir_simd_vector(a) && a == b && c == IR_SIMD_NONE && type == ir_simd_lane_mask(a, lane); break;
            case IR_SIMD_SELECT: valid = valid && ir_simd_vector(type) && a == ir_simd_lane_mask(type, lane) && b == type && c == type; break;
            case IR_SIMD_STORE: valid = valid && type == IR_SIMD_NONE && a != IR_SIMD_NONE && b == IR_SIMD_NONE && c == IR_SIMD_NONE; break;
            case IR_SIMD_EXTRACT:
                valid = valid && ir_simd_vector(type) && ir_simd_vector(a) && ir_simd_bits(type) < ir_simd_bits(a) && action->index < ir_simd_bits(a) / ir_simd_bits(type) &&
                        b == IR_SIMD_NONE && c == IR_SIMD_NONE;
                break;
            case IR_SIMD_WIDEN: valid = valid && ir_simd_vector(type) && ir_simd_vector(a) && ir_simd_bits(a) < ir_simd_bits(type) && b == IR_SIMD_NONE && c == IR_SIMD_NONE; break;
            case IR_SIMD_INSERT:
                valid = valid && ir_simd_vector(type) && a == type && ir_simd_vector(b) && ir_simd_bits(b) < ir_simd_bits(type) &&
                        action->index < ir_simd_bits(type) / ir_simd_bits(b) && c == IR_SIMD_NONE;
                break;
            case IR_SIMD_PERMUTE: valid = valid && ir_simd_vector(type) && a == type && b == type && c == IR_SIMD_NONE && action->index <= 3; break;
            case IR_SIMD_PERMUTE2: valid = valid && ir_simd_vector(type) && a == type && b == type && c == type && action->index <= 3; break;
            case IR_SIMD_TERNLOG: valid = valid && ir_simd_vector(type) && a == type && b == type && c == type; break;
            case IR_SIMD_AES: valid = valid && ir_simd_vector(type) && a == type && b == type && c == IR_SIMD_NONE && action->index <= 1; break;
            case IR_SIMD_LANE_EXTRACT:
                valid = valid && type == IR_SIMD_NONE && ir_simd_vector(a) && b == IR_SIMD_NONE && c == IR_SIMD_NONE &&
                        (uint32_t)(action->index >> 2) < ir_simd_bits(a) / (8u << (action->index & 3));
                break;
            case IR_SIMD_LANE_INSERT:
                valid = valid && ir_simd_vector(type) && a == type && b == IR_SIMD_NONE && c == IR_SIMD_NONE &&
                        (uint32_t)(action->index >> 2) < ir_simd_bits(type) / (8u << (action->index & 3));
                break;
            case IR_SIMD_LANE_UNARY:
                valid = valid && ir_simd_vector(type) && a == type && b == IR_SIMD_NONE && c == IR_SIMD_NONE && (action->index >> 2) <= 2 &&
                        ((action->index >> 2) == 0 || (action->index & 3) >= 2);
                break;
            case IR_SIMD_MOVEMASK:
                valid = valid && type == IR_SIMD_NONE && ir_simd_vector(a) && ir_simd_bits(a) <= 256 && b == IR_SIMD_NONE && c == IR_SIMD_NONE && action->index <= 3 &&
                        action->index != 1;
                break;
            case IR_SIMD_COMPRESS:
            case IR_SIMD_EXPAND:
            {
                static const IrSimdLane lanes[4] = { IR_SIMD_LANE_I8, IR_SIMD_LANE_I16, IR_SIMD_LANE_I32, IR_SIMD_LANE_I64 };
                valid = valid && ir_simd_vector(type) && a == type && c == type && action->index <= 3 && b == ir_simd_lane_mask(type, lanes[action->index & 3]);
            } break;
            case IR_SIMD_SHUFFLE: valid = valid && ir_simd_vector(type) && a == type && b == type && c == IR_SIMD_NONE; break;
            case IR_SIMD_ALIGN:
                valid = valid && ir_simd_vector(type) && a == type && b == type && c == IR_SIMD_NONE && (action->index >> 4 == 2 || action->index >> 4 == 3) &&
                        (action->index & 15u) < ir_simd_bits(type) / (8u << (action->index >> 4));
                break;
            case IR_SIMD_EXTEND:
            case IR_SIMD_TRUNCATE:
            {
                const uint32_t source = action->index & 3;
                const uint32_t result = (action->index >> 2) & 3;
                const uint32_t mode = action->index >> 4;
                const bool extend = action->operation == IR_SIMD_EXTEND;
                const uint32_t lanes = extend ? ir_simd_bits(type) / (8u << result) : ir_simd_bits(a) / (8u << source);
                const uint32_t bits = lanes * (8u << result);
                valid = valid && ir_simd_vector(type) && ir_simd_vector(a) && b == IR_SIMD_NONE && c == IR_SIMD_NONE &&
                        (extend ? result > source && mode <= 1 && lanes * (8u << source) <= ir_simd_bits(a) :
                                  result < source && mode <= 2 && ir_simd_bits(type) == (bits > 128 ? bits : 128));
            } break;
            case IR_SIMD_LANE_OP:
                valid = valid && ir_simd_vector(type) && a == type && b == type && c == IR_SIMD_NONE && (action->index >> 5) == 0 &&
                        (((action->index >> 2) & 7) < 4 || (action->index & 3) <= 1);
                break;
            case IR_SIMD_SHIFT:
                valid = valid && ir_simd_vector(type) && a == type && b == IR_SIMD_NONE && c == IR_SIMD_NONE && (action->index & 3) != 0 &&
                        ((action->index >> 2) & 3) <= 2 && (action->index >> 4) == 0;
                break;
            case IR_SIMD_SHUFFLE_LANES:
                valid = valid && a == type && b == type && c == IR_SIMD_NONE && ((type == IR_SIMD_V8I32 && action->index < 4) || type == IR_SIMD_V16I32);
                break;
            default: valid = false; break;
        }
    }
    return valid;
}

// Inlined, as ir.c's ir_constant_fits is, into the verifier's per-instruction oracle: there as a call, it cost --tier1 -c of
// sqlite3.c 0.24% more instructions in ir_check_rows' loop, which has no vector constant (#141's intrinsics,
// buster-performance's tier1-intrinsics-141-2026-11-03).
static inline __attribute__((always_inline)) bool ir_simd_value_valid(const IrSimdValue* value, IrSimdShape shape)
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

// Lane `lane` of `bits` bits (8, 16, 32 or 64), zero-extended.
static uint64_t ir_simd_lane_read(const IrSimdValue* value, uint32_t bits, uint32_t lane)
{
    const uint32_t first = lane * bits;
    const uint64_t word = value->words[first / 64] >> (first % 64);
    const uint64_t result = bits == 64 ? word : word & ((UINT64_C(1) << bits) - 1);
    return result;
}

static void ir_simd_lane_write(IrSimdValue* value, uint32_t bits, uint32_t lane, uint64_t lane_value)
{
    const uint32_t first = lane * bits;
    const uint64_t mask = bits == 64 ? UINT64_MAX : (UINT64_C(1) << bits) - 1;
    value->words[first / 64] = (value->words[first / 64] & ~(mask << (first % 64))) | ((lane_value & mask) << (first % 64));
}

// A lane's integer comparison at `bits` bits, its operands zero-extended: signed ones by their sign bits flipped.
static bool ir_simd_compare_bits(uint64_t a, uint64_t b, uint32_t bits, IrSimdPredicate predicate)
{
    const uint64_t sign = UINT64_C(1) << (bits - 1);
    const uint64_t signed_a = a ^ sign;
    const uint64_t signed_b = b ^ sign;
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

// A floating lane's bits as a double, and back: a float's or a double's (`bits` 32 or 64). A float widens exactly,
// its NaN payload kept in the top of the double's (ir_simd_float_bits narrows it back).
static double ir_simd_float_value(uint64_t lane, uint32_t bits)
{
    double result = 0;
    if (bits == 32)
    {
        float single = 0;
        const uint32_t word = (uint32_t)lane;
        memcpy(&single, &word, sizeof(single));
        result = single;
    }
    else
    {
        memcpy(&result, &lane, sizeof(result));
    }
    return result;
}

static bool ir_simd_float_nan(uint64_t lane, uint32_t bits)
{
    const uint64_t exponent = bits == 32 ? UINT64_C(0x7f800000) : UINT64_C(0x7ff0000000000000);
    const uint64_t fraction = bits == 32 ? UINT64_C(0x007fffff) : UINT64_C(0x000fffffffffffff);
    return (lane & exponent) == exponent && (lane & fraction) != 0;
}

// A floating lane's arithmetic as x86's packed forms compute it at the default MXCSR: a NaN operand's quieted bits, the
// first's where both are NaNs; else the operation in the lane's precision (a float's in float, rounded once), whose
// invalid results are the host's default NaN, x86's, since the compiler runs on x86-64 alone.
static uint64_t ir_simd_float_operation(IrSimdOperation operation, uint64_t a, uint64_t b, uint32_t bits)
{
    const uint64_t quiet = bits == 32 ? UINT64_C(0x00400000) : UINT64_C(0x0008000000000000);
    uint64_t result = 0;
    if (ir_simd_float_nan(a, bits))
    {
        result = a | quiet;
    }
    else if (ir_simd_float_nan(b, bits))
    {
        result = b | quiet;
    }
    else if (bits == 32)
    {
        const float x = (float)ir_simd_float_value(a, 32);
        const float y = (float)ir_simd_float_value(b, 32);
        const float z = operation == IR_SIMD_FADD ? x + y : operation == IR_SIMD_FSUB ? x - y : operation == IR_SIMD_FMUL ? x * y : x / y;
        uint32_t word = 0;
        memcpy(&word, &z, sizeof(word));
        result = word;
    }
    else
    {
        const double x = ir_simd_float_value(a, 64);
        const double y = ir_simd_float_value(b, 64);
        const double z = operation == IR_SIMD_FADD ? x + y : operation == IR_SIMD_FSUB ? x - y : operation == IR_SIMD_FMUL ? x * y : x / y;
        memcpy(&result, &z, sizeof(result));
    }
    return result;
}

// A floating lane's comparison as C's operators compare (clang's fcmp oeq, une, olt, ole, ogt and oge): false for a NaN
// operand but !='s, true.
static bool ir_simd_float_compare(uint64_t a, uint64_t b, uint32_t bits, IrSimdPredicate predicate)
{
    const double x = ir_simd_float_value(a, bits);
    const double y = ir_simd_float_value(b, bits);
    const bool unordered = ir_simd_float_nan(a, bits) || ir_simd_float_nan(b, bits);
    bool result = false;
    switch (predicate)
    {
        case IR_SIMD_EQ: result = !unordered && x == y; break;
        case IR_SIMD_NE: result = unordered || x != y; break;
        case IR_SIMD_LT: result = !unordered && x < y; break;
        case IR_SIMD_LE: result = !unordered && x <= y; break;
        case IR_SIMD_GT: result = !unordered && x > y; break;
        case IR_SIMD_GE: result = !unordered && x >= y; break;
        default: break;
    }
    return result;
}

// An integer lane's arithmetic at `bits` bits, its operands and result zero-extended: a shift's count is the other
// lane's whole value, unsigned.
static uint64_t ir_simd_integer_operation(IrSimdOperation operation, uint64_t a, uint64_t b, uint32_t bits)
{
    const uint64_t mask = bits == 64 ? UINT64_MAX : (UINT64_C(1) << bits) - 1;
    const uint64_t sign = UINT64_C(1) << (bits - 1);
    uint64_t result = 0;
    switch (operation)
    {
        case IR_SIMD_ADD: result = a + b; break;
        case IR_SIMD_SUB: result = a - b; break;
        case IR_SIMD_MUL: result = a * b; break;
        case IR_SIMD_SHL: result = b >= bits ? 0 : a << b; break;
        case IR_SIMD_LSHR: result = b >= bits ? 0 : a >> b; break;
        case IR_SIMD_ASHR:
        {
            const uint64_t fill = (a & sign) != 0 ? mask : 0;
            result = b >= bits ? fill : (a >> b) | (fill & ~(mask >> b));
        } break;
        default: break;
    }
    return result & mask;
}

// AES's arithmetic in GF(2^8) modulo x^8 + x^4 + x^3 + x + 1 (FIPS 197, #1840): a byte's product by 2 (xtime), and its
// S-box entry, the affine map of its multiplicative inverse (0's inverse 0), computed rather than tabulated.
static uint8_t ir_simd_aes_double(uint8_t value)
{
    return (uint8_t)((uint32_t)value << 1 ^ ((value & 0x80) != 0 ? 0x1b : 0));
}

static uint8_t ir_simd_aes_multiply(uint8_t a, uint8_t b)
{
    uint8_t product = 0;
    uint8_t factor = a;
    for (uint32_t bit = 0; bit < 8; bit += 1)
    {
        product ^= (b >> bit & 1) != 0 ? factor : 0;
        factor = ir_simd_aes_double(factor);
    }
    return product;
}

static uint8_t ir_simd_aes_sbox(uint8_t value)
{
    // The inverse as value^254, by squaring and multiplying.
    uint8_t inverse = 1;
    uint8_t power = value;
    for (uint32_t bit = 1; bit < 8; bit += 1)
    {
        power = ir_simd_aes_multiply(power, power);
        inverse = ir_simd_aes_multiply(inverse, power);
    }
    inverse = value == 0 ? 0 : inverse;
    uint8_t result = 0x63;
    for (uint32_t bit = 0; bit < 8; bit += 1)
    {
        const uint32_t sum = (uint32_t)(inverse >> bit) ^ (uint32_t)(inverse >> ((bit + 4) % 8)) ^ (uint32_t)(inverse >> ((bit + 5) % 8)) ^
                             (uint32_t)(inverse >> ((bit + 6) % 8)) ^ (uint32_t)(inverse >> ((bit + 7) % 8));
        result ^= (uint8_t)((sum & 1) << bit);
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
    else if (valid && (action->operation == IR_SIMD_EXTRACT || action->operation == IR_SIMD_WIDEN || action->operation == IR_SIMD_INSERT))
    {
        // Whole 64-bit words: every vector is 2, 4 or 8 of them.
        const uint32_t result_words = ir_simd_bits(action->result) / 64;
        const uint32_t part_words = ir_simd_bits(action->operation == IR_SIMD_INSERT ? action->operands[1] : action->operation == IR_SIMD_EXTRACT ? action->result : action->operands[0]) / 64;
        const uint32_t first = action->index * part_words;
        for (uint32_t word = 0; word < result_words; word += 1)
        {
            const bool inserted = action->operation == IR_SIMD_INSERT && word >= first && word < first + part_words;
            staged.words[word] = action->operation == IR_SIMD_EXTRACT ? values[0]->words[first + word] :
                                 action->operation == IR_SIMD_WIDEN   ? (word < part_words ? values[0]->words[word] : 0) :
                                 inserted                             ? values[1]->words[word - first] :
                                                                        values[0]->words[word];
        }
    }
    else if (valid && (action->operation == IR_SIMD_EXTEND || action->operation == IR_SIMD_TRUNCATE))
    {
        // Lane by lane: the source's lane read, extended to 64 bits by its sign where a sign extension or a signed
        // saturation reads it so, then widened or narrowed.
        const uint32_t source = 8u << (action->index & 3);
        const uint32_t result = 8u << ((action->index >> 2) & 3);
        const uint32_t mode = action->index >> 4;
        const bool extend = action->operation == IR_SIMD_EXTEND;
        const uint32_t lanes = extend ? ir_simd_bits(action->result) / result : ir_simd_bits(action->operands[0]) / source;
        for (uint32_t lane = 0; lane < lanes; lane += 1)
        {
            const uint64_t raw = ir_simd_lane_read(values[0], source, lane);
            const int64_t signed_value = source == 64 ? (int64_t)raw : (int64_t)(raw << (64 - source)) >> (64 - source);
            uint64_t value = raw;
            if (extend)
            {
                value = mode == 1 ? (uint64_t)signed_value : raw;
            }
            else if (mode == 1)
            {
                const int64_t high = (int64_t)((UINT64_C(1) << (result - 1)) - 1);
                const int64_t low = -high - 1;
                value = (uint64_t)(signed_value > high ? high : signed_value < low ? low : signed_value);
            }
            else if (mode == 2)
            {
                const uint64_t high = (UINT64_C(1) << (result - 1)) * 2 - 1;
                value = raw > high ? high : raw;
            }
            ir_simd_lane_write(&staged, result, lane, result == 64 ? value : value & ((UINT64_C(1) << result) - 1));
        }
    }
    else if (valid && action->operation == IR_SIMD_LANE_OP)
    {
        // Each lane as an unsigned or a signed integer of its bits: the minimum or maximum, or the exact sum or difference
        // clamped to the lane's range.
        const uint32_t lane_bits = 8u << (action->index & 3);
        const uint32_t operation = (action->index >> 2) & 7;
        const bool is_signed = (operation & 1) != 0;
        const uint32_t lanes = ir_simd_bits(action->result) / lane_bits;
        const uint64_t mask = lane_bits == 64 ? UINT64_MAX : (UINT64_C(1) << lane_bits) - 1;
        for (uint32_t lane = 0; lane < lanes; lane += 1)
        {
            const uint64_t a = ir_simd_lane_read(values[0], lane_bits, lane);
            const uint64_t b = ir_simd_lane_read(values[1], lane_bits, lane);
            const int64_t sa = lane_bits == 64 ? (int64_t)a : (int64_t)(a << (64 - lane_bits)) >> (64 - lane_bits);
            const int64_t sb = lane_bits == 64 ? (int64_t)b : (int64_t)(b << (64 - lane_bits)) >> (64 - lane_bits);
            const bool less = is_signed ? sa < sb : a < b;
            // Saturation's lanes are bytes or words, so the exact sum or difference fits an int64_t.
            const int64_t wide = operation < 6 ? (is_signed ? sa + sb : (int64_t)(a + b)) : (is_signed ? sa - sb : (int64_t)a - (int64_t)b);
            const int64_t high = is_signed ? (int64_t)(mask >> 1) : (int64_t)mask;
            const int64_t low = is_signed ? -high - 1 : 0;
            const uint64_t value = operation < 2 ? (less ? a : b) : operation < 4 ? (less ? b : a) : (uint64_t)(wide > high ? high : wide < low ? low : wide);
            ir_simd_lane_write(&staged, lane_bits, lane, value & mask);
        }
    }
    else if (valid && action->operation == IR_SIMD_SHIFT)
    {
        // Each lane by the scalar count, by the lane operations' own shifts (ir_simd_integer_operation), which give 0 or the
        // sign past the lane's bits.
        static const uint8_t shifts[3] = { IR_SIMD_SHL, IR_SIMD_LSHR, IR_SIMD_ASHR };
        valid = values[1] != NULL;
        const uint32_t lane_bits = 8u << (action->index & 3);
        const uint32_t lanes = ir_simd_bits(action->result) / lane_bits;
        const uint64_t count = valid ? values[1]->words[0] & UINT32_MAX : 0;
        for (uint32_t lane = 0; valid && lane < lanes; lane += 1)
        {
            const uint64_t a = ir_simd_lane_read(values[0], lane_bits, lane);
            ir_simd_lane_write(&staged, lane_bits, lane, ir_simd_integer_operation((IrSimdOperation)shifts[(action->index >> 2) & 3], a, count, lane_bits));
        }
    }
    else if (valid && action->operation == IR_SIMD_AES)
    {
        // FIPS 197 on x86's layout, byte i of a 16-byte lane row i % 4 of column i / 4: ShiftRows moves byte 5 to 1 and the
        // like, SubBytes takes each byte's S-box entry (ir_simd_aes_sbox), MixColumns each column's product by 2, 3, 1, 1.
        static const uint8_t rows[16] = { 0, 5, 10, 15, 4, 9, 14, 3, 8, 13, 2, 7, 12, 1, 6, 11 };
        const uint8_t* a = (const uint8_t*)values[0]->words;
        const uint8_t* b = (const uint8_t*)values[1]->words;
        uint8_t* out = (uint8_t*)staged.words;
        const uint32_t bytes = ir_simd_bits(action->result) / 8;
        for (uint32_t lane = 0; lane < bytes; lane += 16)
        {
            uint8_t state[16];
            for (uint32_t i = 0; i < 16; i += 1)
            {
                state[i] = ir_simd_aes_sbox(a[lane + rows[i]]);
            }
            for (uint32_t column = 0; action->index == 0 && column < 4; column += 1)
            {
                const uint8_t* c = state + column * 4;
                const uint8_t mixed[4] = {
                    (uint8_t)(ir_simd_aes_double(c[0]) ^ ir_simd_aes_double(c[1]) ^ c[1] ^ c[2] ^ c[3]),
                    (uint8_t)(c[0] ^ ir_simd_aes_double(c[1]) ^ ir_simd_aes_double(c[2]) ^ c[2] ^ c[3]),
                    (uint8_t)(c[0] ^ c[1] ^ ir_simd_aes_double(c[2]) ^ ir_simd_aes_double(c[3]) ^ c[3]),
                    (uint8_t)(ir_simd_aes_double(c[0]) ^ c[0] ^ c[1] ^ c[2] ^ ir_simd_aes_double(c[3])),
                };
                memcpy(state + column * 4, mixed, 4);
            }
            for (uint32_t i = 0; i < 16; i += 1)
            {
                out[lane + i] = (uint8_t)(state[i] ^ b[lane + i]);
            }
        }
    }
    else if (valid && (action->operation == IR_SIMD_LANE_EXTRACT || action->operation == IR_SIMD_LANE_INSERT))
    {
        // The lane's bits zero-extended into a scalar's word, or b's low bits written over it in a's copy.
        const uint32_t lane_bits = 8u << (action->index & 3);
        const uint32_t lane = (uint32_t)(action->index >> 2);
        valid = action->operation == IR_SIMD_LANE_EXTRACT || values[1] != NULL;
        if (valid && action->operation == IR_SIMD_LANE_EXTRACT)
        {
            staged.words[0] = ir_simd_lane_read(values[0], lane_bits, lane);
        }
        else if (valid)
        {
            staged = *values[0];
            ir_simd_lane_write(&staged, lane_bits, lane, lane_bits == 64 ? values[1]->words[0] : values[1]->words[0] & ((UINT64_C(1) << lane_bits) - 1));
        }
    }
    else if (valid && action->operation == IR_SIMD_LANE_UNARY)
    {
        // Each lane's set bits, its leading zeros (its bits for zero), or the bits of the lanes below it equal to it.
        const uint32_t lane_bits = 8u << (action->index & 3);
        const uint32_t operation = (uint32_t)(action->index >> 2);
        const uint32_t lanes = ir_simd_bits(action->result) / lane_bits;
        for (uint32_t lane = 0; lane < lanes; lane += 1)
        {
            const uint64_t x = ir_simd_lane_read(values[0], lane_bits, lane);
            uint64_t value = 0;
            if (operation == 0)
            {
                value = (uint64_t)__builtin_popcountll(x);
            }
            else if (operation == 1)
            {
                value = x == 0 ? lane_bits : (uint64_t)__builtin_clzll(x) - (64 - lane_bits);
            }
            else
            {
                for (uint32_t below = 0; below < lane; below += 1)
                {
                    value |= (uint64_t)(ir_simd_lane_read(values[0], lane_bits, below) == x) << below;
                }
            }
            ir_simd_lane_write(&staged, lane_bits, lane, value);
        }
    }
    else if (valid && action->operation == IR_SIMD_MOVEMASK)
    {
        // Each lane's top bit, lane i's into bit i.
        const uint32_t lane_bits = 8u << action->index;
        const uint32_t lanes = ir_simd_bits(action->operands[0]) / lane_bits;
        for (uint32_t lane = 0; lane < lanes; lane += 1)
        {
            staged.words[0] |= (ir_simd_lane_read(values[0], lane_bits, lane) >> (lane_bits - 1)) << lane;
        }
    }
    else if (valid && (action->operation == IR_SIMD_COMPRESS || action->operation == IR_SIMD_EXPAND))
    {
        // Lane by lane, the mask's bits in order: a compress takes a's selected lanes into the next lane, an expand puts
        // a's next lane into each selected lane; the third operand's fill the rest.
        const uint32_t size = 1u << action->index;
        const uint32_t lanes = ir_simd_bits(action->result) / 8 / size;
        const uint64_t mask = values[1]->words[0];
        const uint8_t* a = (const uint8_t*)values[0]->words;
        const uint8_t* c = (const uint8_t*)values[2]->words;
        uint8_t* out = (uint8_t*)staged.words;
        memcpy(out, c, (uint64_t)lanes * size);
        uint32_t next = 0;
        for (uint32_t lane = 0; lane < lanes; lane += 1)
        {
            if ((mask >> lane & 1) != 0)
            {
                const bool compress = action->operation == IR_SIMD_COMPRESS;
                memcpy(out + (compress ? next : lane) * size, a + (compress ? lane : next) * size, size);
                next += 1;
            }
        }
    }
    else if (valid && action->operation == IR_SIMD_TERNLOG)
    {
        // Each bit of the truth table, a 64-bit word at a time: the minterms whose bits are set, or'ed.
        const uint32_t words = ir_simd_bits(action->result) / 64;
        for (uint32_t w = 0; w < words; w += 1)
        {
            const uint64_t a = values[0]->words[w];
            const uint64_t b = values[1]->words[w];
            const uint64_t c = values[2]->words[w];
            uint64_t value = 0;
            for (uint32_t minterm = 0; minterm < 8; minterm += 1)
            {
                const uint64_t term = ((minterm & 4) != 0 ? a : ~a) & ((minterm & 2) != 0 ? b : ~b) & ((minterm & 1) != 0 ? c : ~c);
                value |= (action->index >> minterm & 1) != 0 ? term : 0;
            }
            staged.words[w] = value;
        }
    }
    else if (valid && action->operation == IR_SIMD_PERMUTE2)
    {
        // Byte by byte: each result byte is a byte of the lane of a, or of c past a's lanes, that b's lane names.
        const uint8_t* a = (const uint8_t*)values[0]->words;
        const uint8_t* b = (const uint8_t*)values[1]->words;
        const uint8_t* c = (const uint8_t*)values[2]->words;
        uint8_t* out = (uint8_t*)staged.words;
        const uint32_t bytes = ir_simd_bits(action->result) / 8;
        const uint32_t size = 1u << action->index;
        const uint32_t lanes = bytes / size;
        for (uint32_t byte = 0; byte < bytes; byte += 1)
        {
            uint64_t index = 0;
            memcpy(&index, b + (byte / size) * size, size);
            const uint32_t selected = (uint32_t)(index & (2 * lanes - 1));
            out[byte] = (selected < lanes ? a : c)[(selected & (lanes - 1)) * size + byte % size];
        }
    }
    else if (valid && action->operation >= IR_SIMD_PERMUTE && action->operation <= IR_SIMD_SHUFFLE_LANES)
    {
        // Byte by byte: each result byte is one byte of a or b, or 0 (a shuffle's of a control byte's top bit).
        const uint8_t* a = (const uint8_t*)values[0]->words;
        const uint8_t* b = (const uint8_t*)values[1]->words;
        uint8_t* out = (uint8_t*)staged.words;
        const uint32_t bytes = ir_simd_bits(action->result) / 8;
        const uint32_t size = action->operation == IR_SIMD_PERMUTE ? 1u << action->index : action->operation == IR_SIMD_ALIGN ? 1u << (action->index >> 4) : 16u;
        const uint32_t lanes = bytes / size;
        for (uint32_t byte = 0; byte < bytes; byte += 1)
        {
            const uint32_t lane = byte / size;
            const uint32_t within = byte % size;
            uint8_t value = 0;
            if (action->operation == IR_SIMD_PERMUTE)
            {
                uint64_t index = 0;
                memcpy(&index, b + lane * size, size);
                value = a[(uint32_t)(index & (lanes - 1)) * size + within];
            }
            else if (action->operation == IR_SIMD_SHUFFLE)
            {
                value = (b[byte] & 0x80) != 0 ? 0 : a[(byte & ~15u) | (b[byte] & 15u)];
            }
            else if (action->operation == IR_SIMD_ALIGN)
            {
                const uint32_t from = lane + (action->index & 15u);
                value = from < lanes ? b[from * size + within] : a[(from - lanes) * size + within];
            }
            else
            {
                const uint32_t width = lanes == 2 ? 1 : 2;
                const uint32_t selected = (action->index >> (lane * width)) & (lanes - 1);
                value = (lane < lanes / 2 ? a : b)[selected * 16 + within];
            }
            out[byte] = value;
        }
    }
    else if (valid)
    {
        const bool mask = !ir_simd_vector(action->result) && action->operation != IR_SIMD_COMPARE;
        // A mask's operations take its bits whole; a vector's, a lane at a time at the action's lanes (the bitwise
        // operations, the copy and a selection's I32 lanes are the same bits whatever the elements).
        const uint32_t lane_bits = mask ? 64 : ir_simd_lane_bits(action->lane);
        const uint32_t lanes = mask ? 1 : ir_simd_bits(action->operation == IR_SIMD_COMPARE ? action->operands[0] : action->result) / lane_bits;
        const bool floating = ir_simd_lane_floating(action->lane);
        for (uint32_t lane = 0; valid && lane < lanes; lane += 1)
        {
            const uint64_t a = mask ? values[0]->words[0] : ir_simd_lane_read(values[0], lane_bits, lane);
            const uint64_t b = action->operands[1] != IR_SIMD_NONE ? (mask ? values[1]->words[0] : ir_simd_lane_read(values[1], lane_bits, lane)) : 0;
            uint64_t value = 0;
            switch (action->operation)
            {
                case IR_SIMD_COPY: value = a; break;
                case IR_SIMD_ADD:
                case IR_SIMD_SUB:
                case IR_SIMD_MUL:
                case IR_SIMD_SHL:
                case IR_SIMD_LSHR:
                case IR_SIMD_ASHR: value = ir_simd_integer_operation(action->operation, a, b, lane_bits); break;
                case IR_SIMD_FADD:
                case IR_SIMD_FSUB:
                case IR_SIMD_FMUL:
                case IR_SIMD_FDIV: value = ir_simd_float_operation(action->operation, a, b, lane_bits); break;
                case IR_SIMD_AND: value = a & b; break;
                case IR_SIMD_OR: value = a | b; break;
                case IR_SIMD_XOR: value = a ^ b; break;
                case IR_SIMD_NOT: value = ~a; break;
                case IR_SIMD_COMPARE:
                {
                    const bool set = floating ? ir_simd_float_compare(a, b, lane_bits, action->predicate) : ir_simd_compare_bits(a, b, lane_bits, action->predicate);
                    staged.words[0] |= (uint64_t)set << lane;
                } break;
                case IR_SIMD_SELECT: value = ir_simd_lane_read(values[(values[0]->words[0] & (UINT64_C(1) << lane)) != 0 ? 1 : 2], lane_bits, lane); break;
                default: valid = false; break;
            }
            if (mask)
            {
                staged.words[0] = bits == 64 ? value : value & ((UINT64_C(1) << bits) - 1);
            }
            else if (action->operation != IR_SIMD_COMPARE)
            {
                ir_simd_lane_write(&staged, lane_bits, lane, value);
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
