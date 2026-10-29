// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Tier 1's per-function IR. Include after lane.c and calling_convention.c. ir_create allocates exact-sized columns in a function arena;
// producers write those columns directly, in batches, and publish only a complete function. Value IDs are instruction
// indices, blocks are contiguous nonempty ranges, block zero is entry and has no phis, and BUSTER_IR_NONE is never a
// value. The four byte columns and three u32 operand columns cost 16 bytes/instruction. Constants, calls, addresses
// and variable operands have sparse tables, and so do labels, a variadic definition's incoming frame and by-value
// aggregates' ABI records, which the IR's types can't give a backend.
// No column has readable padding: ir_verify uses masked tails and scalar indexed byte loads, never a gather of four
// bytes from a one-byte type. Rewrites build new columns; CFG and use lists are explicitly rebuilt when needed.
//
// Entry points: ir_create; ir_verify and ir_verify_scalar; ir_build_cfg; ir_build_uses; ir_print. All allocations are
// transactional and all scratch is returned. Input/output/scratch arenas must be disjoint. Raw column pointers must
// cover their declared logical counts; the verifier can check IDs and ranges, not a caller's mapping ownership.
// docs/ir.md records operand meanings, the producer/consumer contract and acceptance that needs #133/#134/#138.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include "host_isa.h"

#define BUSTER_IR_NONE UINT32_MAX
#define BUSTER_IR_VECTOR_COUNT 16
#define BUSTER_IR_VOLATILE 1
#define BUSTER_IR_SLOT_ADDRESS_TAKEN 1
#define BUSTER_IR_SLOT_VOLATILE 2
#define BUSTER_IR_SLOT_TEMPORARY 4
// A variadic definition's incoming frame (#148, IrFunction.variadic): the register save area, which holds at the function's
// entry its argument registers as the psABI lays them out (rdi, rsi, rdx, rcx, r8 and r9 from byte 0, then xmm0 to
// xmm7 from byte 48: BUSTER_ABI_SAVE_AREA_BYTES), and the overflow area, which has no bytes of its own: its address is
// where the incoming stack arguments past the named ones start. Each is address-taken and untyped, at most one of each.
#define BUSTER_IR_SLOT_REGISTER_SAVE_AREA 8
#define BUSTER_IR_SLOT_OVERFLOW_AREA 16
#define BUSTER_IR_SLOT_FLAGS (BUSTER_IR_SLOT_ADDRESS_TAKEN | BUSTER_IR_SLOT_VOLATILE | BUSTER_IR_SLOT_TEMPORARY | BUSTER_IR_SLOT_REGISTER_SAVE_AREA | BUSTER_IR_SLOT_OVERFLOW_AREA)

typedef enum IrType
{
    IR_VOID,
    IR_I1,
    IR_I8,
    IR_I16,
    IR_I32,
    IR_I64,
    IR_I128,
    IR_F32,
    IR_F64,
    IR_F80,
    IR_PTR,
    IR_TYPE_COUNT,
} IrType;

typedef enum IrOp
{
    IR_INVALID,
    IR_CONST,
    IR_UNDEF,
    IR_PARAM,
    IR_COPY,
    IR_ADD,
    IR_SUB,
    IR_MUL,
    IR_SDIV,
    IR_UDIV,
    IR_SREM,
    IR_UREM,
    IR_AND,
    IR_OR,
    IR_XOR,
    IR_SHL,
    IR_LSHR,
    IR_ASHR,
    IR_NEG,
    IR_NOT,
    IR_FADD,
    IR_FSUB,
    IR_FMUL,
    IR_FDIV,
    IR_FNEG,
    IR_ICMP,
    IR_FCMP,
    IR_SELECT,
    IR_TRUNC,
    IR_ZEXT,
    IR_SEXT,
    IR_FTRUNC,
    IR_FEXT,
    IR_SITOFP,
    IR_UITOFP,
    IR_FPTOSI,
    IR_FPTOUI,
    IR_INTTOPTR,
    IR_PTRTOINT,
    IR_BITCAST,
    IR_STACK_ADDR,
    IR_SYMBOL_ADDR,
    IR_ADDRESS,
    IR_LOAD,
    IR_STORE,
    IR_CALL,
    IR_CALL_INDIRECT,
    IR_PHI,
    IR_BR,
    IR_CBR,
    IR_SWITCH,
    IR_RET,
    IR_UNREACHABLE,
    IR_ALLOCA,
    IR_STACK_SAVE,
    IR_STACK_RESTORE,
    IR_BR_INDIRECT,
    IR_OP_COUNT,
} IrOp;

// ICMP EQ/NE accept pointers too; the other integer predicates require integer operands. FCMP uses EQ through GE:
// ordered comparisons, except NE, which is true for unordered inputs, as in C.
typedef enum IrPredicate
{
    IR_EQ,
    IR_NE,
    IR_LT,
    IR_LE,
    IR_GT,
    IR_GE,
    IR_ULT,
    IR_ULE,
    IR_UGT,
    IR_UGE,
    IR_PREDICATE_COUNT,
} IrPredicate;

typedef struct IrConstant IrConstant;
struct IrConstant
{
    uint64_t low;
    uint64_t high;
};

typedef struct IrSlot IrSlot;
struct IrSlot
{
    uint64_t size;
    uint32_t alignment;
    uint8_t type;
    uint8_t flags;
};

typedef struct IrAddress IrAddress;
struct IrAddress
{
    int64_t offset;
    uint8_t scale;
};

// An argument's or a parameter's ABI record (#131), which a call (IrAbiCall.passed) or the function
// (IrFunction.parameter_passed) has for a by-value aggregate: its location as the producer's calling convention
// assigned it, and its size, nonzero for an aggregate (a struct or a union, an __int128, a long double parameter or
// another object of the typed stream's), whose IR value is a ptr to its bytes, which the callee receives a copy of.
// The locations are the producer's, trusted as IrDominance is: the verifier checks the IR's rules, not the convention's.
typedef struct IrPassed IrPassed;
struct IrPassed
{
    AbiLocation location;
    uint32_t size;
};

// A call's ABI records: its totals and result as the convention assigned them, its arguments' IrPassed
// (passed[this.passed..+argument_count)), and, nonzero, its aggregate result's bytes, which the call writes where its
// instruction's c operand (a ptr) points; the call's own result type is then void.
typedef struct IrAbiCall IrAbiCall;
struct IrAbiCall
{
    AbiCall call;
    uint32_t passed;
    uint32_t result_size;
};

// Arguments in extra[argument_first..+argument_count), fixed parameter types in call_types[type_first..+parameter_count).
// The result and argument types are semantic IR types. ABI locations/registers are absent but for a call that passes or
// returns an aggregate by value: `abi` is then 1 + its records' index in IrFunction.abi_calls, and 0 for any other call.
typedef struct IrCall IrCall;
struct IrCall
{
    uint32_t argument_first;
    uint32_t argument_count;
    uint32_t type_first;
    uint32_t parameter_count;
    uint8_t result_type;
    bool variadic;
    uint32_t abi;
};

typedef struct IrSizes IrSizes;
struct IrSizes
{
    uint64_t instruction_count;
    uint64_t block_count;
    uint64_t extra_count;
    uint64_t constant_count;
    uint64_t slot_count;
    uint64_t address_count;
    uint64_t call_count;
    uint64_t call_type_count;
    uint64_t parameter_count;
    uint64_t label_count;
    uint64_t abi_call_count;
    uint64_t passed_count;
};

typedef struct IrFunction IrFunction;
struct IrFunction
{
    uint8_t* op;
    uint8_t* type;
    uint8_t* flags;
    uint8_t* alignment_log2;
    uint32_t* a;
    uint32_t* b;
    uint32_t* c;
    uint32_t instruction_count;
    uint32_t* block_offsets;
    uint32_t block_count;
    uint32_t* extra;
    uint32_t extra_count;
    IrConstant* constants;
    uint32_t constant_count;
    IrSlot* slots;
    uint32_t slot_count;
    IrAddress* addresses;
    uint32_t address_count;
    IrCall* calls;
    uint32_t call_count;
    uint8_t* call_types;
    uint32_t call_type_count;
    uint8_t* parameter_types;
    uint32_t parameter_count;
    uint8_t result_type;
    // Exclusive symbol bound supplied by the translation unit. UINT32_MAX permits any non-sentinel SymbolId.
    uint32_t symbol_count;
    // The labels (GNU C's && and goto *, and the producer's jump tables' cases): label k's block, at whose code the
    // producer defines its k-th label symbol, and which every br_indirect may go to; or BUSTER_IR_NONE for a label no
    // block holds, in a function without br_indirect (whose labels no jump can reach: a backend defines their symbols
    // anywhere in the function).
    uint32_t* labels;
    uint32_t label_count;
    // By-value aggregates' ABI records: the calls' (IrCall.abi), and the function's own when parameter_passed isn't 0:
    // its parameters' IrPassed, passed[parameter_passed - 1..+parameter_count), its result as the convention returns it
    // (result_abi) and, nonzero, its aggregate result's bytes, which it returns from the address its ret operand (a
    // ptr, its result type) holds.
    IrAbiCall* abi_calls;
    uint32_t abi_call_count;
    IrPassed* passed;
    uint32_t passed_count;
    uint32_t parameter_passed;
    uint32_t result_size;
    AbiReturn result_abi;
    // A variadic definition's incoming frame, which its BUSTER_IR_SLOT_REGISTER_SAVE_AREA and _OVERFLOW_AREA slots reach:
    // with `variadic`, `incoming` is its named arguments' assignment (abi_assign's named-only signature, as
    // Tier0Function.incoming_variadic): the overflow area starts stack_bytes past the incoming stack arguments.
    bool variadic;
    AbiCall incoming;
    // Exact bytes charged to the arena, alignment gaps included; excludes this view and optional derived arrays.
    uint64_t arena_bytes;
};

typedef enum IrError
{
    IR_OK,
    IR_ERROR_LIMIT,
    IR_ERROR_MEMORY,
    IR_ERROR_STRUCTURE,
    IR_ERROR_OPCODE,
    IR_ERROR_TYPE,
    IR_ERROR_OPERAND,
    IR_ERROR_METADATA,
    IR_ERROR_TERMINATOR,
    IR_ERROR_PHI,
    IR_ERROR_DOMINANCE,
    IR_ERROR_IO,
} IrError;

typedef struct IrCheck IrCheck;
struct IrCheck
{
    IrError error;
    uint32_t instruction;
    uint32_t detail;
    bool dominance_checked;
};

typedef struct IrCfg IrCfg;
struct IrCfg
{
    uint32_t block_count;
    uint32_t edge_count;
    uint32_t* successor_offsets;
    uint32_t* successors;
    uint32_t* predecessor_offsets;
    uint32_t* predecessors;
    uint32_t* instruction_blocks;
    uint64_t arena_bytes;
};

// A trusted dominator-tree view produced by #174. Intervals are half open, nonempty for reachable blocks, and both
// endpoints zero for unreachable blocks. Same-block ordering is always checked. Cross-block uses involving an
// unreachable block are rejected when this view is supplied; dead regions can first be removed or verified without it.
typedef struct IrDominance IrDominance;
struct IrDominance
{
    const uint32_t* enter;
    const uint32_t* leave;
    uint32_t block_count;
};

typedef struct IrUse IrUse;
struct IrUse
{
    uint32_t instruction;
    // 0,1,2 are a,b,c; 3+argument index for calls, 3+incoming index for phis. No metadata enters a use list.
    uint32_t operand;
};

typedef struct IrUses IrUses;
struct IrUses
{
    uint32_t* offsets;
    IrUse* uses;
    uint32_t value_count;
    uint32_t use_count;
    uint64_t arena_bytes;
};

static __attribute__((unused)) IrError ir_create(Arena* arena, const IrSizes* sizes, IrFunction* result);
static __attribute__((unused)) IrCheck ir_verify(const IrFunction* function, Arena* scratch, const IrDominance* dominance);
static __attribute__((unused)) IrCheck ir_verify_scalar(const IrFunction* function, Arena* scratch, const IrDominance* dominance);
static __attribute__((unused)) IrError ir_build_cfg(const IrFunction* function, Arena* arena, Arena* scratch, IrCfg* result);
static __attribute__((unused)) IrError ir_build_uses(const IrFunction* function, Arena* arena, Arena* scratch, IrUses* result);
static __attribute__((unused)) IrError ir_print(const IrFunction* function, Arena* scratch, FILE* output);

static const uint16_t ir_type_bits[IR_TYPE_COUNT] = {
    0,
    1,
    8,
    16,
    32,
    64,
    128,
    32,
    64,
    80,
    64,
};
static const char* const ir_type_names[IR_TYPE_COUNT] = {
    "void",
    "i1",
    "i8",
    "i16",
    "i32",
    "i64",
    "i128",
    "f32",
    "f64",
    "f80",
    "ptr",
};
static const char* const ir_op_names[IR_OP_COUNT] = {
    "invalid",
    "const",
    "undef",
    "param",
    "copy",
    "add",
    "sub",
    "mul",
    "sdiv",
    "udiv",
    "srem",
    "urem",
    "and",
    "or",
    "xor",
    "shl",
    "lshr",
    "ashr",
    "neg",
    "not",
    "fadd",
    "fsub",
    "fmul",
    "fdiv",
    "fneg",
    "icmp",
    "fcmp",
    "select",
    "trunc",
    "zext",
    "sext",
    "ftrunc",
    "fext",
    "sitofp",
    "uitofp",
    "fptosi",
    "fptoui",
    "inttoptr",
    "ptrtoint",
    "bitcast",
    "stack_addr",
    "symbol_addr",
    "address",
    "load",
    "store",
    "call",
    "call_indirect",
    "phi",
    "br",
    "cbr",
    "switch",
    "ret",
    "unreachable",
    "alloca",
    "stack_save",
    "stack_restore",
    "br_indirect",
};

static bool ir_integer(uint32_t type)
{
    return type >= IR_I1 && type <= IR_I128;
}

static bool ir_floating(uint32_t type)
{
    return type >= IR_F32 && type <= IR_F80;
}

static bool ir_slice(uint32_t first, uint64_t count, uint32_t total)
{
    return first <= total && count <= (uint64_t)total - first;
}

static void* ir_allocate(Arena* arena, uint64_t count, uint64_t bytes, bool* ok)
{
    void* result = 0;
    if (*ok && count != 0)
    {
        result = arena_push_array(arena, count, bytes, 64);
        *ok = result != 0;
        if (*ok)
        {
            memset(result, 0, count * bytes);
        }
    }
    return result;
}

static IrError ir_create(Arena* arena, const IrSizes* sizes, IrFunction* result)
{
    const uint64_t mark = arena->length;
    IrError error = IR_OK;
    if (sizes->instruction_count > UINT32_MAX || sizes->block_count > UINT32_MAX || sizes->extra_count > UINT32_MAX || sizes->constant_count > UINT32_MAX || sizes->slot_count > UINT32_MAX || sizes->address_count > UINT32_MAX || sizes->call_count > UINT32_MAX || sizes->call_type_count > UINT32_MAX || sizes->parameter_count > UINT32_MAX ||
        sizes->label_count > UINT32_MAX || sizes->abi_call_count > UINT32_MAX || sizes->passed_count > UINT32_MAX)
    {
        error = IR_ERROR_LIMIT;
    }
    if (error == IR_OK)
    {
        bool ok = true;
        IrFunction candidate = {
            .instruction_count = (uint32_t)sizes->instruction_count,
            .block_count = (uint32_t)sizes->block_count,
            .extra_count = (uint32_t)sizes->extra_count,
            .constant_count = (uint32_t)sizes->constant_count,
            .slot_count = (uint32_t)sizes->slot_count,
            .address_count = (uint32_t)sizes->address_count,
            .call_count = (uint32_t)sizes->call_count,
            .call_type_count = (uint32_t)sizes->call_type_count,
            .parameter_count = (uint32_t)sizes->parameter_count,
            .symbol_count = UINT32_MAX,
            .label_count = (uint32_t)sizes->label_count,
            .abi_call_count = (uint32_t)sizes->abi_call_count,
            .passed_count = (uint32_t)sizes->passed_count,
        };
        candidate.op = ir_allocate(arena, sizes->instruction_count, 1, &ok);
        candidate.type = ir_allocate(arena, sizes->instruction_count, 1, &ok);
        candidate.flags = ir_allocate(arena, sizes->instruction_count, 1, &ok);
        candidate.alignment_log2 = ir_allocate(arena, sizes->instruction_count, 1, &ok);
        candidate.a = ir_allocate(arena, sizes->instruction_count, 4, &ok);
        candidate.b = ir_allocate(arena, sizes->instruction_count, 4, &ok);
        candidate.c = ir_allocate(arena, sizes->instruction_count, 4, &ok);
        candidate.block_offsets = ir_allocate(arena, sizes->block_count + 1, 4, &ok);
        candidate.extra = ir_allocate(arena, sizes->extra_count, 4, &ok);
        candidate.constants = ir_allocate(arena, sizes->constant_count, sizeof(IrConstant), &ok);
        candidate.slots = ir_allocate(arena, sizes->slot_count, sizeof(IrSlot), &ok);
        candidate.addresses = ir_allocate(arena, sizes->address_count, sizeof(IrAddress), &ok);
        candidate.calls = ir_allocate(arena, sizes->call_count, sizeof(IrCall), &ok);
        candidate.call_types = ir_allocate(arena, sizes->call_type_count, 1, &ok);
        candidate.parameter_types = ir_allocate(arena, sizes->parameter_count, 1, &ok);
        candidate.labels = ir_allocate(arena, sizes->label_count, 4, &ok);
        candidate.abi_calls = ir_allocate(arena, sizes->abi_call_count, sizeof(IrAbiCall), &ok);
        candidate.passed = ir_allocate(arena, sizes->passed_count, sizeof(IrPassed), &ok);
        if (ok)
        {
            if (sizes->instruction_count != 0)
            {
                memset(candidate.a, 0xff, sizes->instruction_count * 4);
                memset(candidate.b, 0xff, sizes->instruction_count * 4);
                memset(candidate.c, 0xff, sizes->instruction_count * 4);
            }
            candidate.arena_bytes = arena->length - mark;
            *result = candidate;
        }
        else
        {
            arena_pop_to(arena, mark);
            error = IR_ERROR_MEMORY;
        }
    }
    return error;
}

typedef enum IrRule
{
    IR_RULE_NONE,
    IR_RULE_CONSTANT,
    IR_RULE_PARAMETER,
    IR_RULE_SAME,
    IR_RULE_SHIFT,
    IR_RULE_ICMP,
    IR_RULE_FCMP,
    IR_RULE_SELECT,
    IR_RULE_TRUNC,
    IR_RULE_EXTEND,
    IR_RULE_FTRUNC,
    IR_RULE_FEXTEND,
    IR_RULE_FROM_INT,
    IR_RULE_FROM_FLOAT,
    IR_RULE_FROM_POINTER,
    IR_RULE_BITCAST,
    IR_RULE_SLOT,
    IR_RULE_SYMBOL,
    IR_RULE_ADDRESS,
    IR_RULE_LOAD,
    IR_RULE_STORE,
    IR_RULE_CALL,
    IR_RULE_PHI,
    IR_RULE_BRANCH,
    IR_RULE_CONDITIONAL,
    IR_RULE_SWITCH,
    IR_RULE_RETURN,
    IR_RULE_STACK,
    IR_RULE_INDIRECT,
} IrRule;

#define BUSTER_IR_INTS (((1u << (IR_I128 + 1)) - 1) & ~1u)
#define BUSTER_IR_FLOATS ((1u << IR_F32) | (1u << IR_F64) | (1u << IR_F80))
#define BUSTER_IR_VALUES (((1u << IR_TYPE_COUNT) - 1) & ~1u)
#define BUSTER_IR_TYPES ((1u << IR_TYPE_COUNT) - 1)
// op, SSA operands, occupied fields, result type set, semantic rule, terminator. These tables also drive use enumeration.
#define BUSTER_IR_ROWS(X) \
    X(IR_CONST,         0, 1, BUSTER_IR_VALUES, IR_RULE_CONSTANT, 0) \
    X(IR_UNDEF,         0, 0, BUSTER_IR_VALUES, IR_RULE_NONE, 0) \
    X(IR_PARAM,         0, 1, BUSTER_IR_VALUES, IR_RULE_PARAMETER, 0) \
    X(IR_COPY,          1, 1, BUSTER_IR_VALUES, IR_RULE_SAME, 0) \
    X(IR_ADD,           3, 3, BUSTER_IR_INTS, IR_RULE_SAME, 0) \
    X(IR_SUB,           3, 3, BUSTER_IR_INTS, IR_RULE_SAME, 0) \
    X(IR_MUL,           3, 3, BUSTER_IR_INTS, IR_RULE_SAME, 0) \
    X(IR_SDIV,          3, 3, BUSTER_IR_INTS, IR_RULE_SAME, 0) \
    X(IR_UDIV,          3, 3, BUSTER_IR_INTS, IR_RULE_SAME, 0) \
    X(IR_SREM,          3, 3, BUSTER_IR_INTS, IR_RULE_SAME, 0) \
    X(IR_UREM,          3, 3, BUSTER_IR_INTS, IR_RULE_SAME, 0) \
    X(IR_AND,           3, 3, BUSTER_IR_INTS, IR_RULE_SAME, 0) \
    X(IR_OR,            3, 3, BUSTER_IR_INTS, IR_RULE_SAME, 0) \
    X(IR_XOR,           3, 3, BUSTER_IR_INTS, IR_RULE_SAME, 0) \
    X(IR_SHL,           3, 3, BUSTER_IR_INTS, IR_RULE_SHIFT, 0) \
    X(IR_LSHR,          3, 3, BUSTER_IR_INTS, IR_RULE_SHIFT, 0) \
    X(IR_ASHR,          3, 3, BUSTER_IR_INTS, IR_RULE_SHIFT, 0) \
    X(IR_NEG,           1, 1, BUSTER_IR_INTS, IR_RULE_SAME, 0) \
    X(IR_NOT,           1, 1, BUSTER_IR_INTS, IR_RULE_SAME, 0) \
    X(IR_FADD,          3, 3, BUSTER_IR_FLOATS, IR_RULE_SAME, 0) \
    X(IR_FSUB,          3, 3, BUSTER_IR_FLOATS, IR_RULE_SAME, 0) \
    X(IR_FMUL,          3, 3, BUSTER_IR_FLOATS, IR_RULE_SAME, 0) \
    X(IR_FDIV,          3, 3, BUSTER_IR_FLOATS, IR_RULE_SAME, 0) \
    X(IR_FNEG,          1, 1, BUSTER_IR_FLOATS, IR_RULE_SAME, 0) \
    X(IR_ICMP,          3, 7, 1u << IR_I1, IR_RULE_ICMP, 0) \
    X(IR_FCMP,          3, 7, 1u << IR_I1, IR_RULE_FCMP, 0) \
    X(IR_SELECT,        7, 7, BUSTER_IR_VALUES, IR_RULE_SELECT, 0) \
    X(IR_TRUNC,         1, 1, BUSTER_IR_INTS, IR_RULE_TRUNC, 0) \
    X(IR_ZEXT,          1, 1, BUSTER_IR_INTS, IR_RULE_EXTEND, 0) \
    X(IR_SEXT,          1, 1, BUSTER_IR_INTS, IR_RULE_EXTEND, 0) \
    X(IR_FTRUNC,        1, 1, BUSTER_IR_FLOATS, IR_RULE_FTRUNC, 0) \
    X(IR_FEXT,          1, 1, BUSTER_IR_FLOATS, IR_RULE_FEXTEND, 0) \
    X(IR_SITOFP,        1, 1, BUSTER_IR_FLOATS, IR_RULE_FROM_INT, 0) \
    X(IR_UITOFP,        1, 1, BUSTER_IR_FLOATS, IR_RULE_FROM_INT, 0) \
    X(IR_FPTOSI,        1, 1, BUSTER_IR_INTS, IR_RULE_FROM_FLOAT, 0) \
    X(IR_FPTOUI,        1, 1, BUSTER_IR_INTS, IR_RULE_FROM_FLOAT, 0) \
    X(IR_INTTOPTR,      1, 1, 1u << IR_PTR, IR_RULE_FROM_INT, 0) \
    X(IR_PTRTOINT,      1, 1, BUSTER_IR_INTS, IR_RULE_FROM_POINTER, 0) \
    X(IR_BITCAST,       1, 1, BUSTER_IR_VALUES, IR_RULE_BITCAST, 0) \
    X(IR_STACK_ADDR,    0, 1, 1u << IR_PTR, IR_RULE_SLOT, 0) \
    X(IR_SYMBOL_ADDR,   0, 1, 1u << IR_PTR, IR_RULE_SYMBOL, 0) \
    X(IR_ADDRESS,       1, 7, 1u << IR_PTR, IR_RULE_ADDRESS, 0) \
    X(IR_LOAD,          1, 1, BUSTER_IR_VALUES, IR_RULE_LOAD, 0) \
    X(IR_STORE,         3, 3, 1u << IR_VOID, IR_RULE_STORE, 0) \
    X(IR_CALL,          0, 7, BUSTER_IR_TYPES, IR_RULE_CALL, 0) \
    X(IR_CALL_INDIRECT, 1, 7, BUSTER_IR_TYPES, IR_RULE_CALL, 0) \
    X(IR_PHI,           0, 3, BUSTER_IR_VALUES, IR_RULE_PHI, 0) \
    X(IR_BR,            0, 1, 1u << IR_VOID, IR_RULE_BRANCH, 1) \
    X(IR_CBR,           1, 7, 1u << IR_VOID, IR_RULE_CONDITIONAL, 1) \
    X(IR_SWITCH,        1, 7, 1u << IR_VOID, IR_RULE_SWITCH, 1) \
    X(IR_RET,           0, 1, 1u << IR_VOID, IR_RULE_RETURN, 1) \
    X(IR_UNREACHABLE,   0, 0, 1u << IR_VOID, IR_RULE_NONE, 1) \
    X(IR_ALLOCA,        1, 1, 1u << IR_PTR, IR_RULE_STACK, 0) \
    X(IR_STACK_SAVE,    0, 0, 1u << IR_I64, IR_RULE_NONE, 0) \
    X(IR_STACK_RESTORE, 1, 1, 1u << IR_VOID, IR_RULE_STACK, 0) \
    X(IR_BR_INDIRECT,   1, 7, 1u << IR_VOID, IR_RULE_INDIRECT, 1)

#define BUSTER_IR_OPERAND_ROW(op, uses, fields, types, rule, term) [op] = uses,
static const uint8_t ir_operand_table[64] = { BUSTER_IR_ROWS(BUSTER_IR_OPERAND_ROW) };
#undef BUSTER_IR_OPERAND_ROW
// The operands that are values where they aren't BUSTER_IR_NONE, as ir_operand_table's bits: an address's index (b),
// a return's value (a) and a call's destination for its aggregate result (c).
static const uint8_t ir_optional_table[64] = {
    [IR_ADDRESS] = 2,
    [IR_RET] = 1,
    [IR_CALL] = 4,
    [IR_CALL_INDIRECT] = 4,
};
#define BUSTER_IR_FIELD_ROW(op, uses, fields, types, rule, term) [op] = fields,
static const uint8_t ir_field_table[64] = { BUSTER_IR_ROWS(BUSTER_IR_FIELD_ROW) };
#undef BUSTER_IR_FIELD_ROW
#define BUSTER_IR_TYPE_ROW(op, uses, fields, types, rule, term) [op] = types,
static const uint16_t ir_result_table[64] = { BUSTER_IR_ROWS(BUSTER_IR_TYPE_ROW) };
#undef BUSTER_IR_TYPE_ROW
#define BUSTER_IR_RULE_ROW(op, uses, fields, types, rule, term) [op] = rule,
static const uint8_t ir_rule_table[64] = { BUSTER_IR_ROWS(BUSTER_IR_RULE_ROW) };
#undef BUSTER_IR_RULE_ROW
#define BUSTER_IR_TERM_ROW(op, uses, fields, types, rule, term) [op] = term,
static const uint8_t ir_terminator_table[64] = { BUSTER_IR_ROWS(BUSTER_IR_TERM_ROW) };
#undef BUSTER_IR_TERM_ROW
#undef BUSTER_IR_ROWS
_Static_assert(IR_OP_COUNT <= 64, "IR opcode tables must fit one byte-permute register");

static IrCheck ir_check(IrError error, uint32_t instruction, uint32_t detail)
{
    IrCheck result = {
        .error = error,
        .instruction = instruction,
        .detail = detail,
    };
    return result;
}

static bool ir_constant_fits(IrConstant value, uint8_t type)
{
    const uint32_t bits = ir_type_bits[type];
    bool result = true;
    if (bits < 64)
    {
        result = value.high == 0 && (value.low >> bits) == 0;
    }
    else if (bits == 64)
    {
        result = value.high == 0;
    }
    else if (bits < 128)
    {
        result = (value.high >> (bits - 64)) == 0;
    }
    return result;
}

// Check shape before any instruction-indexed access. Metadata tables are checked even when no instruction uses them.
static IrCheck ir_check_shape(const IrFunction* f)
{
    IrCheck check = ir_check(IR_OK, BUSTER_IR_NONE, 0);
    const uint32_t n = f->instruction_count;
    if (f->result_type >= IR_TYPE_COUNT || f->block_offsets == 0 || ((n == 0) != (f->block_count == 0)) || (n == 0 && f->result_type != IR_VOID) || (n != 0 && (f->op == 0 || f->type == 0 || f->flags == 0 || f->alignment_log2 == 0 || f->a == 0 || f->b == 0 || f->c == 0)) || (f->extra_count != 0 && f->extra == 0) || (f->constant_count != 0 && f->constants == 0) || (f->slot_count != 0 && f->slots == 0) || (f->address_count != 0 && f->addresses == 0) || (f->call_count != 0 && f->calls == 0) || (f->call_type_count != 0 && f->call_types == 0) || (f->parameter_count != 0 && f->parameter_types == 0) || (f->label_count != 0 && f->labels == 0) || (f->abi_call_count != 0 && f->abi_calls == 0) || (f->passed_count != 0 && f->passed == 0))
    {
        check.error = IR_ERROR_STRUCTURE;
    }
    if (check.error == IR_OK && (f->block_offsets[0] != 0 || f->block_offsets[f->block_count] != n))
    {
        check.error = IR_ERROR_STRUCTURE;
    }
    for (uint32_t b = 0; check.error == IR_OK && b < f->block_count; b += 1)
    {
        if (f->block_offsets[b] >= f->block_offsets[b + 1] || f->block_offsets[b + 1] > n)
        {
            check = ir_check(IR_ERROR_STRUCTURE, BUSTER_IR_NONE, b);
        }
    }
    for (uint32_t p = 0; check.error == IR_OK && p < f->parameter_count; p += 1)
    {
        if (f->parameter_types[p] == IR_VOID || f->parameter_types[p] >= IR_TYPE_COUNT)
        {
            check = ir_check(IR_ERROR_METADATA, BUSTER_IR_NONE, p);
        }
    }
    for (uint32_t p = 0; check.error == IR_OK && p < f->call_type_count; p += 1)
    {
        if (f->call_types[p] == IR_VOID || f->call_types[p] >= IR_TYPE_COUNT)
        {
            check = ir_check(IR_ERROR_METADATA, BUSTER_IR_NONE, p);
        }
    }
    // The incoming frame's anchors: at most one of each, untyped, address-taken, of their sizes, in a variadic definition.
    uint32_t anchors = 0;
    for (uint32_t s = 0; check.error == IR_OK && s < f->slot_count; s += 1)
    {
        const IrSlot slot = f->slots[s];
        const uint32_t anchor = slot.flags & (BUSTER_IR_SLOT_REGISTER_SAVE_AREA | BUSTER_IR_SLOT_OVERFLOW_AREA);
        const uint64_t anchor_size = anchor == BUSTER_IR_SLOT_REGISTER_SAVE_AREA ? BUSTER_ABI_SAVE_AREA_BYTES : 0;
        const bool anchor_valid = anchor == 0 || ((anchor & (anchor - 1)) == 0 && (anchors & anchor) == 0 && f->variadic && slot.type == IR_VOID &&
                                                  (slot.flags & BUSTER_IR_SLOT_ADDRESS_TAKEN) != 0 && slot.size == anchor_size && slot.alignment >= 16);
        anchors |= anchor;
        if (slot.type >= IR_TYPE_COUNT || slot.alignment == 0 || (slot.alignment & (slot.alignment - 1)) != 0 || (slot.flags & ~BUSTER_IR_SLOT_FLAGS) != 0 || (slot.type < IR_TYPE_COUNT && slot.size < (ir_type_bits[slot.type] + 7u) / 8u) || !anchor_valid)
        {
            check = ir_check(IR_ERROR_METADATA, BUSTER_IR_NONE, s);
        }
    }
    for (uint32_t l = 0; check.error == IR_OK && l < f->label_count; l += 1)
    {
        if (f->labels[l] >= f->block_count && f->labels[l] != BUSTER_IR_NONE)
        {
            check = ir_check(IR_ERROR_METADATA, BUSTER_IR_NONE, l);
        }
    }
    // The function's own ABI records: its parameters', an aggregate's a ptr, and an aggregate result's a ptr too.
    const uint32_t own = f->parameter_passed;
    if (check.error == IR_OK && ((own != 0 && !ir_slice(own - 1, f->parameter_count, f->passed_count)) || (f->result_size != 0 && (own == 0 || f->result_type != IR_PTR))))
    {
        check = ir_check(IR_ERROR_METADATA, BUSTER_IR_NONE, 0);
    }
    for (uint32_t p = 0; check.error == IR_OK && own != 0 && p < f->parameter_count; p += 1)
    {
        if (f->passed[own - 1 + p].size != 0 && f->parameter_types[p] != IR_PTR)
        {
            check = ir_check(IR_ERROR_METADATA, BUSTER_IR_NONE, p);
        }
    }
    for (uint32_t a = 0; check.error == IR_OK && a < f->address_count; a += 1)
    {
        const uint32_t scale = f->addresses[a].scale;
        if (scale != 1 && scale != 2 && scale != 4 && scale != 8)
        {
            check = ir_check(IR_ERROR_METADATA, BUSTER_IR_NONE, a);
        }
    }
    for (uint32_t c = 0; check.error == IR_OK && c < f->call_count; c += 1)
    {
        const IrCall call = f->calls[c];
        const IrAbiCall* abi = call.abi != 0 && call.abi <= f->abi_call_count ? &f->abi_calls[call.abi - 1] : 0;
        if (call.result_type >= IR_TYPE_COUNT || call.argument_count > UINT32_MAX - 3 || !ir_slice(call.argument_first, call.argument_count, f->extra_count) || !ir_slice(call.type_first, call.parameter_count, f->call_type_count) || call.argument_count < call.parameter_count || (!call.variadic && call.argument_count != call.parameter_count) ||
            (call.abi != 0 && (abi == 0 || !ir_slice(abi->passed, call.argument_count, f->passed_count) || (abi->result_size != 0 && call.result_type != IR_VOID))))
        {
            check = ir_check(IR_ERROR_METADATA, BUSTER_IR_NONE, c);
        }
    }
    return check;
}

// Exact one-instruction oracle. The vector path calls this only for exceptional rules or a failing batch to obtain
// the same first error. It never reads a value's type until its ID and the enclosing columns have been checked.
static IrCheck ir_check_instruction(const IrFunction* f, uint32_t i)
{
    const uint32_t op = f->op[i];
    const uint32_t type = f->type[i];
    const uint32_t a = f->a[i];
    const uint32_t b = f->b[i];
    const uint32_t c = f->c[i];
    IrCheck check = ir_check(IR_OK, i, 0);
    if (op == IR_INVALID || op >= IR_OP_COUNT)
    {
        check.error = IR_ERROR_OPCODE;
    }
    else if (type >= IR_TYPE_COUNT || (ir_result_table[op] & (1u << type)) == 0)
    {
        check.error = IR_ERROR_TYPE;
    }
    else if (((ir_field_table[op] & 1) == 0 && a != BUSTER_IR_NONE) || ((ir_field_table[op] & 2) == 0 && b != BUSTER_IR_NONE) || ((ir_field_table[op] & 4) == 0 && c != BUSTER_IR_NONE))
    {
        check.error = IR_ERROR_OPERAND;
    }
    else if (((op == IR_LOAD || op == IR_STORE) && ((f->flags[i] & ~BUSTER_IR_VOLATILE) != 0 || f->alignment_log2[i] > 31)) || ((op != IR_LOAD && op != IR_STORE) && (f->flags[i] != 0 || f->alignment_log2[i] != 0)))
    {
        check.error = IR_ERROR_METADATA;
    }
    uint32_t operand_types[3] = { 0 };
    const uint32_t operands[3] = {
        a,
        b,
        c,
    };
    for (uint32_t p = 0; check.error == IR_OK && p < 3; p += 1)
    {
        if ((ir_operand_table[op] & (1u << p)) != 0)
        {
            if (operands[p] >= f->instruction_count || f->type[operands[p]] == IR_VOID || f->type[operands[p]] >= IR_TYPE_COUNT)
            {
                check = ir_check(IR_ERROR_OPERAND, i, p);
            }
            else
            {
                operand_types[p] = f->type[operands[p]];
            }
        }
    }
    if (check.error == IR_OK)
    {
        const uint32_t ta = operand_types[0];
        const uint32_t tb = operand_types[1];
        bool valid = true;
        switch (ir_rule_table[op])
        {
            case IR_RULE_NONE: break;
            case IR_RULE_CONSTANT: valid = a < f->constant_count && ir_constant_fits(f->constants[a], (uint8_t)type); break;
            case IR_RULE_PARAMETER: valid = a < f->parameter_count && f->parameter_types[a] == type; break;
            case IR_RULE_SAME: valid = ta == type && ((ir_operand_table[op] & 2) == 0 || tb == type); break;
            case IR_RULE_SHIFT: valid = ta == type && ir_integer(tb); break;
            case IR_RULE_ICMP: valid = ta == tb && c < IR_PREDICATE_COUNT && (ir_integer(ta) || (ta == IR_PTR && c <= IR_NE)); break;
            case IR_RULE_FCMP: valid = ta == tb && ir_floating(ta) && c <= IR_GE; break;
            case IR_RULE_SELECT: valid = ta == IR_I1 && tb == type && operand_types[2] == type; break;
            case IR_RULE_TRUNC: valid = ir_integer(ta) && ir_type_bits[ta] > ir_type_bits[type]; break;
            case IR_RULE_EXTEND: valid = ir_integer(ta) && ir_type_bits[ta] < ir_type_bits[type]; break;
            case IR_RULE_FTRUNC: valid = ir_floating(ta) && ir_type_bits[ta] > ir_type_bits[type]; break;
            case IR_RULE_FEXTEND: valid = ir_floating(ta) && ir_type_bits[ta] < ir_type_bits[type]; break;
            case IR_RULE_FROM_INT: valid = ir_integer(ta); break;
            case IR_RULE_FROM_FLOAT: valid = ir_floating(ta); break;
            case IR_RULE_FROM_POINTER: valid = ta == IR_PTR; break;
            case IR_RULE_BITCAST: valid = ir_type_bits[ta] == ir_type_bits[type]; break;
            case IR_RULE_SLOT: valid = a < f->slot_count; break;
            case IR_RULE_SYMBOL: valid = a != 0 && a < f->symbol_count; break;
            case IR_RULE_ADDRESS:
            {
                valid = ta == IR_PTR && c < f->address_count && (b == BUSTER_IR_NONE || (b < f->instruction_count && f->type[b] == IR_I64));
            } break;
            case IR_RULE_LOAD:
            case IR_RULE_STORE: valid = ta == IR_PTR; break;
            case IR_RULE_CALL:
            {
                valid = b < f->call_count && (op == IR_CALL ? a != 0 && a < f->symbol_count : ta == IR_PTR);
                if (valid)
                {
                    const IrCall call = f->calls[b];
                    // A call that returns an aggregate writes it where c points (IrAbiCall.result_size); any other has no c.
                    const IrAbiCall* abi = call.abi != 0 ? &f->abi_calls[call.abi - 1] : 0;
                    const bool into = abi != 0 && abi->result_size != 0;
                    valid = call.result_type == type && (into ? c < f->instruction_count && f->type[c] == IR_PTR : c == BUSTER_IR_NONE);
                    for (uint32_t p = 0; valid && p < call.argument_count; p += 1)
                    {
                        const uint32_t value = f->extra[call.argument_first + p];
                        valid = value < f->instruction_count && f->type[value] != IR_VOID && f->type[value] < IR_TYPE_COUNT && (p >= call.parameter_count || f->type[value] == f->call_types[call.type_first + p]) &&
                                (abi == 0 || f->passed[abi->passed + p].size == 0 || f->type[value] == IR_PTR);
                    }
                }
            } break;
            case IR_RULE_PHI: valid = b != 0 && b <= UINT32_MAX - 3 && ir_slice(a, (uint64_t)b * 2, f->extra_count); break;
            case IR_RULE_BRANCH: valid = a < f->block_count; break;
            case IR_RULE_CONDITIONAL: valid = ta == IR_I1 && b < f->block_count && c < f->block_count; break;
            case IR_RULE_SWITCH:
            {
                valid = ir_integer(ta) && ir_slice(b, (uint64_t)c * 2 + 1, f->extra_count);
                if (valid)
                {
                    valid = f->extra[b] < f->block_count;
                    for (uint32_t p = 0; valid && p < c; p += 1)
                    {
                        const uint32_t constant = f->extra[b + 1 + p * 2];
                        valid = constant < f->constant_count && f->extra[b + 2 + p * 2] < f->block_count && ir_constant_fits(f->constants[constant], (uint8_t)ta);
                    }
                }
            } break;
            case IR_RULE_RETURN: valid = f->result_type == IR_VOID ? a == BUSTER_IR_NONE : a < f->instruction_count && f->type[a] == f->result_type; break;
            case IR_RULE_INDIRECT:
            {
                // An indirect branch goes to the label whose address it holds, one of its list's (label indices in
                // extra[b..+c), a jump table's, #785) or, without a list, any of the function's: each must have its block.
                const bool list = b != BUSTER_IR_NONE;
                valid = ta == IR_PTR && (list ? c != 0 && ir_slice(b, c, f->extra_count) : c == BUSTER_IR_NONE);
                for (uint32_t l = 0; valid && l < (list ? c : f->label_count); l += 1)
                {
                    const uint32_t label = list ? f->extra[b + l] : l;
                    valid = label < f->label_count && f->labels[label] != BUSTER_IR_NONE;
                }
            } break;
            // Validate the fixed-width stack rule outside the established explicit semantic cases.
            default: valid = ir_rule_table[op] == IR_RULE_STACK && ta == IR_I64; break;
        }
        if (!valid)
        {
            check.error = ir_rule_table[op] == IR_RULE_PHI ? IR_ERROR_PHI : IR_ERROR_TYPE;
        }
    }
    return check;
}

static __m512i ir_lookup_bytes(__m512i table, __m128i op)
{
    return _mm512_cvtepu8_epi32(_mm512_castsi512_si128(_mm512_permutexvar_epi8(_mm512_zextsi128_si512(op), table)));
}

// A bit for every row needing the scalar semantic check. The common arithmetic, comparison, selection and memory
// rules are checked here. Indexed byte loads are scalar, bounded and independent; comparisons remain 16-wide.
static __mmask16 ir_check_batch(const IrFunction* f, uint32_t first, uint32_t count, __mmask16* terminators)
{
    const __mmask16 active = (__mmask16)((1u << count) - 1u);
    const __m128i op_bytes = _mm_maskz_loadu_epi8(active, f->op + first);
    const __m512i op = _mm512_cvtepu8_epi32(op_bytes);
    const __m512i type = _mm512_cvtepu8_epi32(_mm_maskz_loadu_epi8(active, f->type + first));
    const __m512i flags = _mm512_cvtepu8_epi32(_mm_maskz_loadu_epi8(active, f->flags + first));
    const __m512i align = _mm512_cvtepu8_epi32(_mm_maskz_loadu_epi8(active, f->alignment_log2 + first));
    const __m512i a = _mm512_maskz_loadu_epi32(active, f->a + first);
    const __m512i b = _mm512_maskz_loadu_epi32(active, f->b + first);
    const __m512i c = _mm512_maskz_loadu_epi32(active, f->c + first);
    const __m512i operands = ir_lookup_bytes(_mm512_loadu_si512(ir_operand_table), op_bytes);
    const __m512i fields = ir_lookup_bytes(_mm512_loadu_si512(ir_field_table), op_bytes);
    const __m512i rules = ir_lookup_bytes(_mm512_loadu_si512(ir_rule_table), op_bytes);
    const __m512i terms = ir_lookup_bytes(_mm512_loadu_si512(ir_terminator_table), op_bytes);
    const __m512i op_words = _mm512_cvtepu8_epi16(_mm256_zextsi128_si256(op_bytes));
    const __m512i allowed_words = _mm512_permutex2var_epi16(_mm512_loadu_si512(ir_result_table), op_words, _mm512_loadu_si512(ir_result_table + 32));
    const __m512i allowed = _mm512_cvtepu16_epi32(_mm512_castsi512_si256(allowed_words));
    const __m512i one = _mm512_set1_epi32(1);
    const __m512i none = _mm512_set1_epi32(-1);
    const __m512i limit = _mm512_set1_epi32((int)f->instruction_count);
    *terminators = active & _mm512_test_epi32_mask(terms, one);
    __mmask16 bad = _mm512_cmpeq_epi32_mask(op, _mm512_setzero_si512()) | _mm512_cmpge_epu32_mask(op, _mm512_set1_epi32(IR_OP_COUNT));
    bad |= _mm512_cmpge_epu32_mask(type, _mm512_set1_epi32(IR_TYPE_COUNT));
    bad |= ~_mm512_test_epi32_mask(allowed, _mm512_sllv_epi32(one, type));
    const __mmask16 use_a = _mm512_test_epi32_mask(operands, one) & active;
    const __mmask16 use_b = _mm512_test_epi32_mask(operands, _mm512_set1_epi32(2)) & active;
    const __mmask16 use_c = _mm512_test_epi32_mask(operands, _mm512_set1_epi32(4)) & active;
    bad |= (~_mm512_test_epi32_mask(fields, one)) & _mm512_cmpneq_epi32_mask(a, none);
    bad |= (~_mm512_test_epi32_mask(fields, _mm512_set1_epi32(2))) & _mm512_cmpneq_epi32_mask(b, none);
    bad |= (~_mm512_test_epi32_mask(fields, _mm512_set1_epi32(4))) & _mm512_cmpneq_epi32_mask(c, none);
    bad |= use_a & _mm512_cmpge_epu32_mask(a, limit);
    bad |= use_b & _mm512_cmpge_epu32_mask(b, limit);
    bad |= use_c & _mm512_cmpge_epu32_mask(c, limit);
    const __mmask16 memory = _mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(IR_LOAD)) | _mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(IR_STORE));
    bad |= memory & (_mm512_test_epi32_mask(flags, _mm512_set1_epi32(~BUSTER_IR_VOLATILE)) | _mm512_cmpgt_epu32_mask(align, _mm512_set1_epi32(31)));
    bad |= ~memory & (_mm512_test_epi32_mask(flags, none) | _mm512_test_epi32_mask(align, none));
    uint32_t types_a[BUSTER_IR_VECTOR_COUNT] = { 0 };
    uint32_t types_b[BUSTER_IR_VECTOR_COUNT] = { 0 };
    uint32_t types_c[BUSTER_IR_VECTOR_COUNT] = { 0 };
    for (uint32_t lane = 0; lane < count; lane += 1)
    {
        const uint32_t bit = 1u << lane;
        const uint32_t index = first + lane;
        if ((use_a & bit) != 0 && f->a[index] < f->instruction_count)
        {
            types_a[lane] = f->type[f->a[index]];
        }
        if ((use_b & bit) != 0 && f->b[index] < f->instruction_count)
        {
            types_b[lane] = f->type[f->b[index]];
        }
        if ((use_c & bit) != 0 && f->c[index] < f->instruction_count)
        {
            types_c[lane] = f->type[f->c[index]];
        }
    }
    const __m512i ta = _mm512_loadu_si512(types_a);
    const __m512i tb = _mm512_loadu_si512(types_b);
    const __m512i tc = _mm512_loadu_si512(types_c);
    bad |= use_a & (_mm512_cmpeq_epi32_mask(ta, _mm512_setzero_si512()) | _mm512_cmpge_epu32_mask(ta, _mm512_set1_epi32(IR_TYPE_COUNT)));
    bad |= use_b & (_mm512_cmpeq_epi32_mask(tb, _mm512_setzero_si512()) | _mm512_cmpge_epu32_mask(tb, _mm512_set1_epi32(IR_TYPE_COUNT)));
    bad |= use_c & (_mm512_cmpeq_epi32_mask(tc, _mm512_setzero_si512()) | _mm512_cmpge_epu32_mask(tc, _mm512_set1_epi32(IR_TYPE_COUNT)));
    const __mmask16 same = _mm512_cmpeq_epi32_mask(rules, _mm512_set1_epi32(IR_RULE_SAME));
    const __mmask16 shift = _mm512_cmpeq_epi32_mask(rules, _mm512_set1_epi32(IR_RULE_SHIFT));
    const __mmask16 icmp = _mm512_cmpeq_epi32_mask(rules, _mm512_set1_epi32(IR_RULE_ICMP));
    const __mmask16 fcmp = _mm512_cmpeq_epi32_mask(rules, _mm512_set1_epi32(IR_RULE_FCMP));
    const __mmask16 select = _mm512_cmpeq_epi32_mask(rules, _mm512_set1_epi32(IR_RULE_SELECT));
    const __mmask16 no_rule = _mm512_cmpeq_epi32_mask(rules, _mm512_set1_epi32(IR_RULE_NONE));
    const __mmask16 a_integer = _mm512_cmpge_epu32_mask(ta, _mm512_set1_epi32(IR_I1)) & _mm512_cmple_epu32_mask(ta, _mm512_set1_epi32(IR_I128));
    const __mmask16 b_integer = _mm512_cmpge_epu32_mask(tb, _mm512_set1_epi32(IR_I1)) & _mm512_cmple_epu32_mask(tb, _mm512_set1_epi32(IR_I128));
    const __mmask16 a_float = _mm512_cmpge_epu32_mask(ta, _mm512_set1_epi32(IR_F32)) & _mm512_cmple_epu32_mask(ta, _mm512_set1_epi32(IR_F80));
    bad |= same & (_mm512_cmpneq_epi32_mask(ta, type) | (use_b & _mm512_cmpneq_epi32_mask(tb, type)));
    bad |= shift & (_mm512_cmpneq_epi32_mask(ta, type) | ~b_integer);
    const __mmask16 pointer_compare = _mm512_cmpeq_epi32_mask(ta, _mm512_set1_epi32(IR_PTR)) & _mm512_cmple_epu32_mask(c, _mm512_set1_epi32(IR_NE));
    bad |= icmp & (_mm512_cmpneq_epi32_mask(ta, tb) | _mm512_cmpge_epu32_mask(c, _mm512_set1_epi32(IR_PREDICATE_COUNT)) | ~(a_integer | pointer_compare));
    bad |= fcmp & (_mm512_cmpneq_epi32_mask(ta, tb) | _mm512_cmpgt_epu32_mask(c, _mm512_set1_epi32(IR_GE)) | ~a_float);
    bad |= select & (_mm512_cmpneq_epi32_mask(ta, _mm512_set1_epi32(IR_I1)) | _mm512_cmpneq_epi32_mask(tb, type) | _mm512_cmpneq_epi32_mask(tc, type));
    bad |= memory & _mm512_cmpneq_epi32_mask(ta, _mm512_set1_epi32(IR_PTR));
    const __mmask16 defer = ~(same | shift | icmp | fcmp | select | memory | no_rule);
    return active & (bad | defer);
}

static IrCheck ir_check_rows(const IrFunction* f, bool vector)
{
    (void)vector;

    IrCheck check = ir_check(IR_OK, BUSTER_IR_NONE, 0);
    uint32_t block = 0;
    uint32_t first = 0;
    while (check.error == IR_OK && first < f->instruction_count)
    {
        const uint32_t remaining = f->instruction_count - first;
        const uint32_t count = BUSTER_VECTOR_PATH(vector) && remaining > BUSTER_IR_VECTOR_COUNT ? BUSTER_IR_VECTOR_COUNT : BUSTER_VECTOR_PATH(vector) ? remaining : 1;
        __mmask16 expected = 0;
        while (block < f->block_count && f->block_offsets[block + 1] - first <= count)
        {
            expected |= (__mmask16)(1u << (f->block_offsets[block + 1] - first - 1));
            block += 1;
        }
        __mmask16 actual = 0;
        uint32_t pending = 1;
        if (BUSTER_VECTOR_PATH(vector))
        {
            pending = ir_check_batch(f, first, count, &actual);
        }
        else if (f->op[first] < IR_OP_COUNT)
        {
            actual = ir_terminator_table[f->op[first]];
        }
        const uint32_t bad_terms = actual ^ expected;
        pending |= bad_terms;
        while (check.error == IR_OK && pending != 0)
        {
            const uint32_t lane = (uint32_t)__builtin_ctz(pending);
            pending &= pending - 1;
            check = ir_check_instruction(f, first + lane);
            if (check.error == IR_OK && (bad_terms & (1u << lane)) != 0)
            {
                check = ir_check(IR_ERROR_TERMINATOR, first + lane, 0);
            }
        }
        first += count;
    }
    if (check.error == IR_OK)
    {
        check.instruction = BUSTER_IR_NONE;
    }
    return check;
}

static uint32_t ir_successor_count(const IrFunction* f, uint32_t i)
{
    const uint32_t op = f->op[i];
    return op == IR_BR ? 1 : op == IR_CBR ? 2 : op == IR_SWITCH ? f->c[i] + 1 : op == IR_BR_INDIRECT ? (f->b[i] == BUSTER_IR_NONE ? f->label_count : f->c[i]) : 0;
}

static uint32_t ir_successor(const IrFunction* f, uint32_t i, uint32_t edge)
{
    uint32_t result = f->a[i];
    if (f->op[i] == IR_CBR)
    {
        result = edge == 0 ? f->b[i] : f->c[i];
    }
    else if (f->op[i] == IR_SWITCH)
    {
        result = f->extra[f->b[i] + edge * 2];
    }
    else if (f->op[i] == IR_BR_INDIRECT)
    {
        result = f->labels[f->b[i] == BUSTER_IR_NONE ? edge : f->extra[f->b[i] + edge]];
    }
    return result;
}

// Internal version also permits arena==scratch for the verifier, which discards the entire graph at its end.
// Public callers use disjoint arenas; their graph excludes the marks/cursors and survives scratch restoration.
static IrError ir_cfg_validated(const IrFunction* f, Arena* arena, Arena* scratch, IrCfg* result)
{
    const uint64_t mark = arena->length;
    const uint64_t temporary = scratch->length;
    bool ok = true;
    IrError error = IR_OK;
    IrCfg cfg = {
        .block_count = f->block_count,
    };
    cfg.successor_offsets = ir_allocate(arena, (uint64_t)f->block_count + 1, 4, &ok);
    cfg.predecessor_offsets = ir_allocate(arena, (uint64_t)f->block_count + 1, 4, &ok);
    cfg.instruction_blocks = ir_allocate(arena, f->instruction_count, 4, &ok);
    uint32_t* seen = ir_allocate(scratch, f->block_count, 4, &ok);
    uint32_t* cursor = ir_allocate(scratch, f->block_count, 4, &ok);
    if (!ok)
    {
        error = IR_ERROR_MEMORY;
    }
    if (error == IR_OK && f->block_count != 0)
    {
        memset(seen, 0xff, (uint64_t)f->block_count * 4);
    }
    uint64_t edges = 0;
    for (uint32_t block = 0; error == IR_OK && block < f->block_count; block += 1)
    {
        const uint32_t first = f->block_offsets[block];
        const uint32_t end = f->block_offsets[block + 1];
        const __m512i ids = _mm512_set1_epi32((int)block);
        uint32_t i = first;
        while (end - i >= BUSTER_IR_VECTOR_COUNT)
        {
            _mm512_storeu_si512(cfg.instruction_blocks + i, ids);
            i += BUSTER_IR_VECTOR_COUNT;
        }
        if (i != end)
        {
            _mm512_mask_storeu_epi32(cfg.instruction_blocks + i, (__mmask16)((1u << (end - i)) - 1), ids);
        }
        const uint32_t count = ir_successor_count(f, end - 1);
        for (uint32_t e = 0; e < count; e += 1)
        {
            const uint32_t target = ir_successor(f, end - 1, e);
            if (seen[target] != block)
            {
                seen[target] = block;
                cfg.successor_offsets[block + 1] += 1;
                cfg.predecessor_offsets[target + 1] += 1;
                edges += 1;
            }
        }
        if (edges > UINT32_MAX)
        {
            error = IR_ERROR_LIMIT;
        }
    }
    if (error == IR_OK)
    {
        cfg.edge_count = (uint32_t)edges;
        cfg.successors = ir_allocate(arena, edges, 4, &ok);
        cfg.predecessors = ir_allocate(arena, edges, 4, &ok);
        if (!ok)
        {
            error = IR_ERROR_MEMORY;
        }
    }
    if (error == IR_OK)
    {
        for (uint32_t b = 0; b < f->block_count; b += 1)
        {
            cfg.successor_offsets[b + 1] += cfg.successor_offsets[b];
            cfg.predecessor_offsets[b + 1] += cfg.predecessor_offsets[b];
            cursor[b] = cfg.predecessor_offsets[b];
            seen[b] = BUSTER_IR_NONE;
        }
        for (uint32_t b = 0; b < f->block_count; b += 1)
        {
            const uint32_t last = f->block_offsets[b + 1] - 1;
            const uint32_t count = ir_successor_count(f, last);
            uint32_t place = cfg.successor_offsets[b];
            for (uint32_t e = 0; e < count; e += 1)
            {
                const uint32_t target = ir_successor(f, last, e);
                if (seen[target] != b)
                {
                    seen[target] = b;
                    cfg.successors[place] = target;
                    place += 1;
                    cfg.predecessors[cursor[target]] = b;
                    cursor[target] += 1;
                }
            }
        }
        cfg.arena_bytes = arena->length - mark;
        *result = cfg;
    }
    else
    {
        arena_pop_to(arena, mark);
    }
    if (arena != scratch)
    {
        arena_pop_to(scratch, temporary);
    }
    return error;
}

static IrError ir_build_cfg(const IrFunction* f, Arena* arena, Arena* scratch, IrCfg* result)
{
    IrCheck check = ir_check_shape(f);
    if (check.error == IR_OK && arena == scratch)
    {
        check.error = IR_ERROR_STRUCTURE;
    }
    if (check.error == IR_OK)
    {
        check = ir_check_rows(f, true);
    }
    if (check.error == IR_OK)
    {
        check.error = ir_cfg_validated(f, arena, scratch, result);
    }
    return check.error;
}

// Enumerate real value operands without materializing another stream. count/place/fill use exactly this contract,
// while tests enumerate independently from the opcode meanings. Phi edges are uses in their predecessor block.
typedef struct IrOperand IrOperand;
struct IrOperand
{
    uint32_t value;
    uint32_t position;
    uint32_t block;
};

static uint32_t ir_value_mask(const IrFunction* f, uint32_t i)
{
    const uint32_t op = f->op[i];
    uint32_t result = ir_operand_table[op];
    const uint32_t optional = ir_optional_table[op];
    if (optional != 0)
    {
        const uint32_t present = (uint32_t)(f->a[i] != BUSTER_IR_NONE) | (uint32_t)(f->b[i] != BUSTER_IR_NONE) << 1 | (uint32_t)(f->c[i] != BUSTER_IR_NONE) << 2;
        result |= optional & present;
    }
    return result;
}

static uint64_t ir_operand_count(const IrFunction* f, uint32_t i)
{
    uint64_t result = (uint32_t)__builtin_popcount(ir_value_mask(f, i));
    if (f->op[i] == IR_CALL || f->op[i] == IR_CALL_INDIRECT)
    {
        result += f->calls[f->b[i]].argument_count;
    }
    else if (f->op[i] == IR_PHI)
    {
        result += f->b[i];
    }
    return result;
}

static IrOperand ir_operand(const IrFunction* f, uint32_t i, uint32_t number, uint32_t block)
{
    uint32_t mask = ir_value_mask(f, i);
    const uint32_t direct = (uint32_t)__builtin_popcount(mask);
    IrOperand result = {
        .block = block,
    };
    if (number < direct)
    {
        for (uint32_t p = 0; p < number; p += 1)
        {
            mask &= mask - 1;
        }
        result.position = (uint32_t)__builtin_ctz(mask);
        result.value = result.position == 0 ? f->a[i] : result.position == 1 ? f->b[i] : f->c[i];
    }
    else if (f->op[i] == IR_PHI)
    {
        const uint32_t p = number - direct;
        result.position = 3 + p;
        result.block = f->extra[f->a[i] + p * 2];
        result.value = f->extra[f->a[i] + p * 2 + 1];
    }
    else
    {
        const uint32_t p = number - direct;
        result.position = 3 + p;
        result.value = f->extra[f->calls[f->b[i]].argument_first + p];
    }
    return result;
}

static bool ir_predecessor(const IrCfg* cfg, uint32_t block, uint32_t candidate)
{
    uint32_t first = cfg->predecessor_offsets[block];
    uint32_t end = cfg->predecessor_offsets[block + 1];
    const uint32_t limit = end;
    while (first < end)
    {
        const uint32_t middle = first + (end - first) / 2;
        if (cfg->predecessors[middle] < candidate)
        {
            first = middle + 1;
        }
        else
        {
            end = middle;
        }
    }
    return first < limit && cfg->predecessors[first] == candidate;
}

static IrError ir_switch_unique(const IrFunction* f, uint32_t instruction, Arena* scratch)
{
    const uint64_t mark = scratch->length;
    const uint32_t count = f->c[instruction];
    bool ok = true;
    // Sorting bounds the work even for adversarial i128 values. A hash of two 64-bit halves admits large families
    // of distinct keys with identical hashes; an unbounded linear probe would turn verification into quadratic work.
    uint32_t* keys = count > 1 ? ir_allocate(scratch, count, 4, &ok) : 0;
    uint32_t* next = count > 1 ? ir_allocate(scratch, count, 4, &ok) : 0;
    IrError error = ok ? IR_OK : IR_ERROR_MEMORY;
    if (error == IR_OK && count > 1)
    {
        for (uint32_t p = 0; p < count; p += 1)
        {
            keys[p] = f->extra[f->b[instruction] + p * 2 + 1];
        }
        const uint32_t bytes = (ir_type_bits[f->type[f->a[instruction]]] + 7u) / 8u;
        for (uint32_t byte = 0; byte < bytes; byte += 1)
        {
            uint32_t offsets[256] = { 0 };
            const uint32_t shift = (byte & 7u) * 8u;
            for (uint32_t p = 0; p < count; p += 1)
            {
                const IrConstant value = f->constants[keys[p]];
                const uint32_t digit = (uint32_t)((byte < 8 ? value.low : value.high) >> shift) & 255u;
                offsets[digit] += 1;
            }
            uint32_t total = 0;
            for (uint32_t digit = 0; digit < 256; digit += 1)
            {
                const uint32_t frequency = offsets[digit];
                offsets[digit] = total;
                total += frequency;
            }
            for (uint32_t p = 0; p < count; p += 1)
            {
                const IrConstant value = f->constants[keys[p]];
                const uint32_t digit = (uint32_t)((byte < 8 ? value.low : value.high) >> shift) & 255u;
                next[offsets[digit]] = keys[p];
                offsets[digit] += 1;
            }
            uint32_t* swap = keys;
            keys = next;
            next = swap;
        }
        for (uint32_t p = 1; error == IR_OK && p < count; p += 1)
        {
            const IrConstant a = f->constants[keys[p - 1]];
            const IrConstant b = f->constants[keys[p]];
            if (a.low == b.low && a.high == b.high)
            {
                error = IR_ERROR_METADATA;
            }
        }
    }
    arena_pop_to(scratch, mark);
    return error;
}

typedef struct IrUseBatch IrUseBatch;
struct IrUseBatch
{
    uint32_t definitions[BUSTER_IR_VECTOR_COUNT];
    uint32_t positions[BUSTER_IR_VECTOR_COUNT];
    uint32_t definition_blocks[BUSTER_IR_VECTOR_COUNT];
    uint32_t use_blocks[BUSTER_IR_VECTOR_COUNT];
    uint32_t definition_enter[BUSTER_IR_VECTOR_COUNT];
    uint32_t definition_leave[BUSTER_IR_VECTOR_COUNT];
    uint32_t use_enter[BUSTER_IR_VECTOR_COUNT];
    uint32_t use_leave[BUSTER_IR_VECTOR_COUNT];
    uint32_t instructions[BUSTER_IR_VECTOR_COUNT];
    uint32_t operands[BUSTER_IR_VECTOR_COUNT];
    uint32_t count;
};

static IrCheck ir_check_use_batch(const IrUseBatch* batch, bool dominance, bool vector)
{
    (void)vector;

    IrCheck check = ir_check(IR_OK, BUSTER_IR_NONE, 0);
    uint32_t bad = 0;
    if (BUSTER_VECTOR_PATH(vector))
    {
        const __m512i definition_blocks = _mm512_loadu_si512(batch->definition_blocks);
        const __m512i use_blocks = _mm512_loadu_si512(batch->use_blocks);
        const __mmask16 same = _mm512_cmpeq_epi32_mask(definition_blocks, use_blocks);
        bad = same & _mm512_cmpge_epu32_mask(_mm512_loadu_si512(batch->definitions), _mm512_loadu_si512(batch->positions));
        if (dominance)
        {
            const __m512i definition_leave = _mm512_loadu_si512(batch->definition_leave);
            const __m512i use_leave = _mm512_loadu_si512(batch->use_leave);
            bad |= ~same & (_mm512_cmpeq_epi32_mask(definition_leave, _mm512_setzero_si512()) | _mm512_cmpeq_epi32_mask(use_leave, _mm512_setzero_si512()) | _mm512_cmpgt_epu32_mask(_mm512_loadu_si512(batch->definition_enter), _mm512_loadu_si512(batch->use_enter)) | _mm512_cmplt_epu32_mask(definition_leave, use_leave));
        }
        bad &= (1u << batch->count) - 1u;
    }
    else
    {
        for (uint32_t lane = 0; lane < batch->count; lane += 1)
        {
            const bool same = batch->definition_blocks[lane] == batch->use_blocks[lane];
            const bool invalid = same ? batch->definitions[lane] >= batch->positions[lane] : dominance && (batch->definition_leave[lane] == 0 || batch->use_leave[lane] == 0 || batch->definition_enter[lane] > batch->use_enter[lane] || batch->definition_leave[lane] < batch->use_leave[lane]);
            bad |= (uint32_t)invalid << lane;
        }
    }
    if (bad != 0)
    {
        const uint32_t first = (uint32_t)__builtin_ctz(bad);
        check = ir_check(IR_ERROR_DOMINANCE, batch->instructions[first], batch->operands[first]);
    }
    return check;
}

static IrCheck ir_check_dominance(const IrFunction* f, const IrCfg* cfg, const IrDominance* dominance, bool vector)
{
    (void)vector;

    IrCheck check = ir_check(IR_OK, BUSTER_IR_NONE, 0);
    IrUseBatch batch = { 0 };
    for (uint32_t i = 0; check.error == IR_OK && i < f->instruction_count; i += 1)
    {
        const uint64_t count = ir_operand_count(f, i);
        for (uint32_t p = 0; check.error == IR_OK && p < count; p += 1)
        {
            const IrOperand operand = ir_operand(f, i, p, cfg->instruction_blocks[i]);
            const uint32_t definition_block = cfg->instruction_blocks[operand.value];
            const uint32_t lane = batch.count;
            batch.definitions[lane] = operand.value;
            batch.positions[lane] = f->op[i] == IR_PHI ? f->block_offsets[operand.block + 1] - 1 : i;
            batch.definition_blocks[lane] = definition_block;
            batch.use_blocks[lane] = operand.block;
            batch.instructions[lane] = i;
            batch.operands[lane] = operand.position;
            if (dominance != 0)
            {
                batch.definition_enter[lane] = dominance->enter[definition_block];
                batch.definition_leave[lane] = dominance->leave[definition_block];
                batch.use_enter[lane] = dominance->enter[operand.block];
                batch.use_leave[lane] = dominance->leave[operand.block];
            }
            batch.count += 1;
            if (batch.count == BUSTER_IR_VECTOR_COUNT)
            {
                check = ir_check_use_batch(&batch, dominance != 0, BUSTER_VECTOR_PATH(vector));
                batch.count = 0;
            }
        }
    }
    if (check.error == IR_OK && batch.count != 0)
    {
        check = ir_check_use_batch(&batch, dominance != 0, BUSTER_VECTOR_PATH(vector));
    }
    return check;
}

static IrCheck ir_check_graph(const IrFunction* f, const IrCfg* cfg, Arena* scratch, const IrDominance* dominance, bool vector)
{
    (void)vector;

    IrCheck check = ir_check(IR_OK, BUSTER_IR_NONE, 0);
    bool ok = true;
    uint32_t* seen = ir_allocate(scratch, f->block_count, 4, &ok);
    if (!ok)
    {
        check.error = IR_ERROR_MEMORY;
    }
    if (ok && f->block_count != 0)
    {
        memset(seen, 0xff, (uint64_t)f->block_count * 4);
    }
    if (dominance != 0 && (dominance->block_count != f->block_count || (f->block_count != 0 && (dominance->enter == 0 || dominance->leave == 0))))
    {
        check.error = IR_ERROR_DOMINANCE;
    }
    for (uint32_t b = 0; check.error == IR_OK && dominance != 0 && b < f->block_count; b += 1)
    {
        if (dominance->enter[b] >= dominance->leave[b] && (dominance->enter[b] != 0 || dominance->leave[b] != 0))
        {
            check = ir_check(IR_ERROR_DOMINANCE, BUSTER_IR_NONE, b);
        }
    }
    for (uint32_t b = 0; check.error == IR_OK && b < f->block_count; b += 1)
    {
        bool past_phis = false;
        for (uint32_t i = f->block_offsets[b]; check.error == IR_OK && i < f->block_offsets[b + 1]; i += 1)
        {
            if (f->op[i] == IR_PHI)
            {
                // Initial function entry has no predecessor from which a phi can select a value. A producer needing
                // a loop at entry must add a real preheader with an explicit initial value before introducing phis.
                if (b == 0 || past_phis || f->b[i] != cfg->predecessor_offsets[b + 1] - cfg->predecessor_offsets[b])
                {
                    check = ir_check(IR_ERROR_PHI, i, 0);
                }
                for (uint32_t p = 0; check.error == IR_OK && p < f->b[i]; p += 1)
                {
                    const uint32_t predecessor = f->extra[f->a[i] + p * 2];
                    const uint32_t value = f->extra[f->a[i] + p * 2 + 1];
                    if (predecessor >= f->block_count || !ir_predecessor(cfg, b, predecessor) || seen[predecessor] == i || value >= f->instruction_count || f->type[value] != f->type[i])
                    {
                        check = ir_check(IR_ERROR_PHI, i, p);
                    }
                    else
                    {
                        seen[predecessor] = i;
                    }
                }
            }
            else
            {
                past_phis = true;
            }
            if (check.error == IR_OK && f->op[i] == IR_SWITCH)
            {
                check = ir_check(ir_switch_unique(f, i, scratch), i, 0);
            }
        }
    }
    if (check.error == IR_OK)
    {
        check = ir_check_dominance(f, cfg, dominance, BUSTER_VECTOR_PATH(vector));
    }
    if (check.error == IR_OK)
    {
        check.instruction = BUSTER_IR_NONE;
        check.dominance_checked = dominance != 0;
    }
    return check;
}

static IrCheck ir_verify_path(const IrFunction* f, Arena* scratch, const IrDominance* dominance, bool vector)
{
    (void)vector;

    const uint64_t mark = scratch->length;
    IrCheck check = ir_check_shape(f);
    if (check.error == IR_OK)
    {
        check = ir_check_rows(f, BUSTER_VECTOR_PATH(vector));
    }
    IrCfg cfg = { 0 };
    if (check.error == IR_OK)
    {
        check.error = ir_cfg_validated(f, scratch, scratch, &cfg);
    }
    if (check.error == IR_OK)
    {
        check = ir_check_graph(f, &cfg, scratch, dominance, BUSTER_VECTOR_PATH(vector));
    }
    arena_pop_to(scratch, mark);
    return check;
}

static IrCheck ir_verify_scalar(const IrFunction* f, Arena* scratch, const IrDominance* dominance);

static IrCheck ir_verify(const IrFunction* f, Arena* scratch, const IrDominance* dominance)
{
#if !BUSTER_HOST_VECTOR
    return ir_verify_scalar(f, scratch, dominance);
#else
    return ir_verify_path(f, scratch, dominance, true);
#endif
}

static IrCheck ir_verify_scalar(const IrFunction* f, Arena* scratch, const IrDominance* dominance)
{
    return ir_verify_path(f, scratch, dominance, false);
}

static IrError ir_build_uses(const IrFunction* f, Arena* arena, Arena* scratch, IrUses* result)
{
    const uint64_t mark = arena->length;
    const uint64_t temporary = scratch->length;
    IrCheck check = arena == scratch ? ir_check(IR_ERROR_STRUCTURE, BUSTER_IR_NONE, 0) : ir_verify(f, scratch, 0);
    IrUses uses = {
        .value_count = f->instruction_count,
    };
    bool ok = true;
    uint32_t* cursor = 0;
    if (check.error == IR_OK)
    {
        uses.offsets = ir_allocate(arena, (uint64_t)f->instruction_count + 1, 4, &ok);
        cursor = ir_allocate(scratch, f->instruction_count, 4, &ok);
        if (!ok)
        {
            check.error = IR_ERROR_MEMORY;
        }
    }
    uint64_t total = 0;
    for (uint32_t i = 0; check.error == IR_OK && i < f->instruction_count; i += 1)
    {
        const uint64_t count = ir_operand_count(f, i);
        total += count;
        if (total > UINT32_MAX)
        {
            check.error = IR_ERROR_LIMIT;
        }
        else
        {
            for (uint32_t p = 0; p < count; p += 1)
            {
                const IrOperand operand = ir_operand(f, i, p, 0);
                uses.offsets[operand.value + 1] += 1;
            }
        }
    }
    if (check.error == IR_OK)
    {
        uses.use_count = (uint32_t)total;
        uses.uses = ir_allocate(arena, total, sizeof(IrUse), &ok);
        if (!ok)
        {
            check.error = IR_ERROR_MEMORY;
        }
    }
    if (check.error == IR_OK)
    {
        for (uint32_t value = 0; value < f->instruction_count; value += 1)
        {
            uses.offsets[value + 1] += uses.offsets[value];
            cursor[value] = uses.offsets[value];
        }
        for (uint32_t i = 0; i < f->instruction_count; i += 1)
        {
            const uint64_t count = ir_operand_count(f, i);
            for (uint32_t p = 0; p < count; p += 1)
            {
                const IrOperand operand = ir_operand(f, i, p, 0);
                uses.uses[cursor[operand.value]] = (IrUse){
                    .instruction = i,
                    .operand = operand.position,
                };
                cursor[operand.value] += 1;
            }
        }
        uses.arena_bytes = arena->length - mark;
        *result = uses;
    }
    else
    {
        arena_pop_to(arena, mark);
    }
    arena_pop_to(scratch, temporary);
    return check.error;
}

static void ir_print_constant(FILE* output, IrConstant value)
{
    fprintf(output, "0x%016" PRIx64 "%016" PRIx64, value.high, value.low);
}

// The ABI records' fields, in their structs' order but for the padding: loc(kind,register0,register1,stack_offset,flags),
// ret(kind,flags,part_bytes,register0,...,register3) and call(stack_bytes,stack_alignment_log2,integer_registers,
// vector_registers,ret(...)).
static void ir_print_location(FILE* output, AbiLocation location)
{
    fprintf(output, "loc(%u,%u,%u,%" PRIu32 ",%u)", location.kind, location.registers[0], location.registers[1], location.stack_offset, location.flags);
}

static void ir_print_return(FILE* output, AbiReturn result)
{
    _Static_assert(BUSTER_ABI_RESULT_PARTS == 4, "a result prints four registers");
    fprintf(output, "ret(%u,%u,%u,%u,%u,%u,%u)", result.kind, result.flags, result.part_bytes, result.registers[0], result.registers[1], result.registers[2], result.registers[3]);
}

static void ir_print_abi_call(FILE* output, AbiCall call)
{
    fprintf(output, "call(%" PRIu32 ",%u,%u,%u,", call.stack_bytes, call.stack_alignment_log2, call.integer_registers, call.vector_registers);
    ir_print_return(output, call.result);
    fputc(')', output);
}

static IrError ir_print(const IrFunction* f, Arena* scratch, FILE* output)
{
    IrCheck check = ir_verify(f, scratch, 0);
    if (check.error == IR_OK && output == 0)
    {
        check.error = IR_ERROR_IO;
    }
    if (check.error == IR_OK)
    {
        fprintf(output, "function (");
        for (uint32_t p = 0; p < f->parameter_count; p += 1)
        {
            fprintf(output, "%s%s", p == 0 ? "" : ", ", ir_type_names[f->parameter_types[p]]);
        }
        fprintf(output, ") -> %s {\n", ir_type_names[f->result_type]);
        if (f->variadic)
        {
            fprintf(output, "  variadic ");
            ir_print_abi_call(output, f->incoming);
            fputc('\n', output);
        }
        if (f->parameter_passed != 0)
        {
            fprintf(output, "  result size=%" PRIu32 " ", f->result_size);
            ir_print_return(output, f->result_abi);
            fprintf(output, " passed=#%" PRIu32 "\n", f->parameter_passed - 1);
        }
        for (uint32_t p = 0; p < f->passed_count; p += 1)
        {
            fprintf(output, "  passed #%" PRIu32 " size=%" PRIu32 " ", p, f->passed[p].size);
            ir_print_location(output, f->passed[p].location);
            fputc('\n', output);
        }
        for (uint32_t c = 0; c < f->abi_call_count; c += 1)
        {
            fprintf(output, "  abi #%" PRIu32 " ", c);
            ir_print_abi_call(output, f->abi_calls[c].call);
            fprintf(output, " passed=#%" PRIu32 " result=%" PRIu32 "\n", f->abi_calls[c].passed, f->abi_calls[c].result_size);
        }
        for (uint32_t s = 0; s < f->slot_count; s += 1)
        {
            const IrSlot slot = f->slots[s];
            fprintf(output, "  slot $%" PRIu32 " size=%" PRIu64 " align=%" PRIu32 " type=%s flags=0x%02x\n", s, slot.size, slot.alignment, ir_type_names[slot.type], slot.flags);
        }
        for (uint32_t l = 0; l < f->label_count; l += 1)
        {
            if (f->labels[l] == BUSTER_IR_NONE)
            {
                fprintf(output, "  label #%" PRIu32 " none\n", l);
            }
            else
            {
                fprintf(output, "  label #%" PRIu32 " b%" PRIu32 "\n", l, f->labels[l]);
            }
        }
        for (uint32_t block = 0; block < f->block_count; block += 1)
        {
            fprintf(output, "b%" PRIu32 ":\n", block);
            for (uint32_t i = f->block_offsets[block]; i < f->block_offsets[block + 1]; i += 1)
            {
                const uint32_t op = f->op[i];
                const uint32_t a = f->a[i];
                const uint32_t b = f->b[i];
                const uint32_t c = f->c[i];
                fprintf(output, "  %%%" PRIu32 " = %s.%s", i, ir_op_names[op], ir_type_names[f->type[i]]);
                switch (op)
                {
                    case IR_CONST:
                    {
                        fputc(' ', output);
                        ir_print_constant(output, f->constants[a]);
                    } break;
                    case IR_PARAM: fprintf(output, " %" PRIu32, a); break;
                    case IR_STACK_ADDR: fprintf(output, " $%" PRIu32, a); break;
                    case IR_SYMBOL_ADDR: fprintf(output, " @%" PRIu32, a); break;
                    case IR_ADDRESS:
                    {
                        fprintf(output, " %%%" PRIu32, a);
                        if (b != BUSTER_IR_NONE)
                        {
                            fprintf(output, " + %%%" PRIu32 " * %u", b, f->addresses[c].scale);
                        }
                        fprintf(output, " offset=%" PRId64, f->addresses[c].offset);
                    } break;
                    case IR_CALL:
                    case IR_CALL_INDIRECT:
                    {
                        const IrCall call = f->calls[b];
                        fprintf(output, " %c%" PRIu32 "(", op == IR_CALL ? '@' : '%', a);
                        for (uint32_t p = 0; p < call.argument_count; p += 1)
                        {
                            fprintf(output, "%s%%%" PRIu32, p == 0 ? "" : ", ", f->extra[call.argument_first + p]);
                        }
                        fprintf(output, ") fixed=(");
                        for (uint32_t p = 0; p < call.parameter_count; p += 1)
                        {
                            fprintf(output, "%s%s", p == 0 ? "" : ", ", ir_type_names[f->call_types[call.type_first + p]]);
                        }
                        fprintf(output, ") variadic=%u", (unsigned)call.variadic);
                        if (call.abi != 0)
                        {
                            fprintf(output, " abi=#%" PRIu32, call.abi - 1);
                        }
                        if (c != BUSTER_IR_NONE)
                        {
                            fprintf(output, " into=%%%" PRIu32, c);
                        }
                    } break;
                    case IR_PHI:
                    {
                        for (uint32_t p = 0; p < b; p += 1)
                        {
                            fprintf(output, "%s[%%%" PRIu32 ", b%" PRIu32 "]", p == 0 ? " " : ", ", f->extra[a + p * 2 + 1], f->extra[a + p * 2]);
                        }
                    } break;
                    case IR_BR: fprintf(output, " b%" PRIu32, a); break;
                    case IR_BR_INDIRECT:
                    {
                        // Its labels, when it has a list of them.
                        fprintf(output, " %%%" PRIu32, a);
                        for (uint32_t p = 0; b != BUSTER_IR_NONE && p < c; p += 1)
                        {
                            fprintf(output, "%s#%" PRIu32 "%s", p == 0 ? " [" : ", ", f->extra[b + p], p + 1 == c ? "]" : "");
                        }
                    } break;
                    case IR_CBR: fprintf(output, " %%%" PRIu32 ", b%" PRIu32 ", b%" PRIu32, a, b, c); break;
                    case IR_SWITCH:
                    {
                        fprintf(output, " %%%" PRIu32 ", default b%" PRIu32, a, f->extra[b]);
                        for (uint32_t p = 0; p < c; p += 1)
                        {
                            fprintf(output, ", [");
                            ir_print_constant(output, f->constants[f->extra[b + p * 2 + 1]]);
                            fprintf(output, ", b%" PRIu32 "]", f->extra[b + p * 2 + 2]);
                        }
                    } break;
                    default:
                    {
                        const uint32_t mask = ir_value_mask(f, i);
                        bool first = true;
                        for (uint32_t p = 0; p < 3; p += 1)
                        {
                            if ((mask & (1u << p)) != 0)
                            {
                                fprintf(output, "%s%%%" PRIu32, first ? " " : ", ", p == 0 ? a : p == 1 ? b : c);
                                first = false;
                            }
                        }
                        if (op == IR_ICMP || op == IR_FCMP)
                        {
                            static const char* const predicates[IR_PREDICATE_COUNT] = {
                                "eq",
                                "ne",
                                "lt",
                                "le",
                                "gt",
                                "ge",
                                "ult",
                                "ule",
                                "ugt",
                                "uge",
                            };
                            fprintf(output, " %s", predicates[c]);
                        }
                        if (op == IR_LOAD || op == IR_STORE)
                        {
                            fprintf(output, " align=%" PRIu32 "%s", UINT32_C(1) << f->alignment_log2[i], (f->flags[i] & BUSTER_IR_VOLATILE) != 0 ? " volatile" : "");
                        }
                    } break;
                }
                fputc('\n', output);
            }
        }
        fprintf(output, "}\n");
        if (ferror(output))
        {
            check.error = IR_ERROR_IO;
        }
    }
    return check.error;
}
