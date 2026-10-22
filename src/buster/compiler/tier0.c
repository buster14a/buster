// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Tier 0 (#136): a function's typed stream to x86-64 instruction records, by expanding a template per entry, with
// registers from the depth of a value stack. docs/tier0.md has the design and its 9700X measurements.
//
// The input is the typed stream (#133 produces it): one entry per node of a function's body in evaluation order, each a
// kind (BUSTER_TYPED_*), a machine class (BUSTER_TYPED_CLASS_*) and a 64-bit value, with control flow as branch and
// jump entries whose values are the entries they go to, and the function's locals and calls beside it (Tier0Function
// says exactly what each entry means). Expressions are in postorder, so the stream is a stack machine's program: a
// value's register is its depth on one of two value stacks, one for integers and pointers and one for floating values,
// and the depth before each entry is a prefix sum of the entries' effects on the stacks. That needs no liveness
// analysis: the registers of the depth tables (tier0_integer_depths, tier0_floating_depths) are caller-saved, and a
// call saves the depths below its arguments, which are exactly the values live across it.
//
// The output is the function's records (X86Instruction), with every branch inside it in its rel8 form and two columns
// beside them, the branches' record indices and their targets' (contract 1 of #135, which size_branches takes), and a
// LinkReference per symbolic field (link_add_code_fixups). The prologue saves the callee-saved registers the locals
// live in and reserves the frame; the epilogue, last, undoes it.
//
// Five phases, each a pass over the entries:
//   1. Fusion. A leaf (a constant or a local) that is its consumer's last operand is the entry before it, since
//      postorder puts a node's last child last; so whether a leaf becomes an operand of its consumer's instruction (an
//      immediate, a register, a memory operand) is a property of a window of entries, as the lexer's punctuators are
//      of a window of bytes. The windows: a leaf into its consumer (the entry after it); a local and a constant into a
//      compare or a multiplication (cmp m, imm and imul r, m, imm); a pointer addition into a load or a store (its
//      addressing mode); a compare into the branch after it (cmp and jcc, with no setcc); and x = x op v into op x, v.
//      An entry a jump goes to (a label) ends every window: what comes before it isn't what a jump brings.
//   2. Depths. Each entry's effect on each stack (what it pushes less what it pops, with its group's when it absorbs
//      fused entries, which then count nothing) is a table lookup; the depths are the effects' prefix sums.
//   3. Homes. The locals each get a home, a register or a frame slot: the most referenced ones the registers no entry
//      touches, which are caller-saved ones no call clobbers and the callee-saved ones (rbx, rbp, r12 to r15), the
//      rest slots. Parameters stay where they arrive when nothing touches their registers. This, not elaborate
//      allocation, is what beats clang -O0, which loads and stores every local at every use.
//   4. Templates. Each entry's template follows from its kind, class, fusion and homes, and its records' places from
//      the prefix sum of the templates' lengths.
//   5. Expansion. Each template is a fixed run of records with holes (registers from the depths, the constant, the
//      homes), filled from the entry's 32-byte hole block: copy and patch, at the level of instruction records.
// The entries the templates don't cover (calls; division and shifts by a register, at depths where their fixed
// registers hold other values) go to tier0_emit_deferred, which computes their lengths before the prefix sum and writes
// their records at the places it reserved.
//
// Two paths make the same records: tier0_compile, whose fusion, depths, templates and hole blocks are computed 64
// entries at a time in AVX-512 and whose expansion stores a template's four records at once with one byte permute, and
// tier0_compile_scalar, its twin and the tests' oracle, one entry at a time, which builds each record field by field
// from the templates' specifications rather than from their compiled bytes. Homes, the frame and the deferred entries
// are shared: they are per function or per local, not per entry. After expansion both run the immediates' form choice
// (#221's x86_choose_forms and its twin): every record with an immediate takes the shortest form that keeps its value
// and the flags its template reads.
//
// Entry points: tier0_initialize (builds the templates; requires x86_encoder_initialize and x86_semantics_initialize),
// tier0_compile, tier0_compile_scalar and tier0_validate (the stream's rules, for the tests and for debugging). Like
// x86_encode and size_branches, the compile paths are the code generator's internal functions: they take a function
// tier0_validate accepts and check nothing about it in their loops. Files that use this one include it after
// x86_64_encoder.c, lane.c, link.c, x86_semantics.c, c_types.c and calling_convention.c; every function in it is
// static.
//
// Layout, in this order:
//   - The typed stream: the classes (BUSTER_TYPED_CLASS_*), the kinds (BUSTER_TYPED_*), the locals' flags and
//     Tier0Function.
//   - The output: Tier0Error, Tier0Code, and the prototypes.
//   - Registers: the depth tables, the homes' registers and the condition codes.
//   - Templates: the sources (BUSTER_TIER0_SOURCE_*), Tier0RecordSpec, the variants (BUSTER_TIER0_VARIANT_*), Tier0Template,
//     the tables (tier0_templates, tier0_slots), the builder (tier0_template_*) and tier0_initialize.
//   - Fusion and depths: the kinds' shapes (tier0_shapes), Tier0Entries (the per-entry columns both paths fill),
//     tier0_labels, tier0_fuse_scalar.
//   - Homes and the frame: Tier0Frame, tier0_assign_homes, tier0_layout_frame.
//   - Holes and expansion: tier0_holes_scalar, tier0_expand_scalar.
//   - The deferred entries: tier0_emit_deferred (calls, division, shifts), with its parallel moves.
//   - The rest: the prologue and the epilogue, branch columns, references, the immediates' choice, the drivers.
//   - The vector path.
//   - Validation: tier0_validate.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <immintrin.h>

// ---------------------------------------------------------------------------------------------------------------------
// The typed stream

// An entry's machine class: what a value is to the code generator. Integers by width and signedness (a pointer is U64),
// and the two floating types. A value of a class narrower than its register holds its value in the register's low
// bits; the rest are unspecified, so a consumer reads only the class's width (a 32-bit operation, a movsx).
#define BUSTER_TYPED_CLASS_VOID 0
#define BUSTER_TYPED_CLASS_I8 1
#define BUSTER_TYPED_CLASS_U8 2
#define BUSTER_TYPED_CLASS_I16 3
#define BUSTER_TYPED_CLASS_U16 4
#define BUSTER_TYPED_CLASS_I32 5
#define BUSTER_TYPED_CLASS_U32 6
#define BUSTER_TYPED_CLASS_I64 7
#define BUSTER_TYPED_CLASS_U64 8
#define BUSTER_TYPED_CLASS_F32 9
#define BUSTER_TYPED_CLASS_F64 10
#define BUSTER_TYPED_CLASSES 11
// Every class below 16, so that a kind and a class make a byte table's index.
#define BUSTER_TYPED_CLASS_LIMIT 16

// The kinds. Below, "the operand" of an entry that takes one is the entry before it (the root of its last operand, in
// postorder), whose class is "the operand's class"; a two-operand entry's first operand comes before that one's
// subtree. `class` is the entry's own class column; `value` its value column.
//   - Leaves, which push one value of `class`:
//     CONST: the value's bits (an integer sign- or zero-extended by its class to 64 bits; a float's bits, or a
//     double's). LOCAL: local `value`'s value. LOCAL_ADDRESS: its address (class U64; the local must have
//     BUSTER_TYPED_LOCAL_ADDRESS_TAKEN). GLOBAL: the value of the object at SymbolId `value`. GLOBAL_ADDRESS: the
//     address of SymbolId `value` (an object, a string literal or a function; class U64).
//   - DUP: pushes a copy of the top value, which has `class`.
//   - NEG and NOT: the operand (of `class`) negated or complemented (NOT for integers alone).
//   - CONVERT: the operand converted to `class` (by C's rules for the two classes; a conversion to a narrower integer
//     keeps the low bits).
//   - LOAD: the value of `class` at the address the operand (U64) holds.
//   - ADD ... SHR: the two operands' sum ... (by `class`, which both operands have, but a shift's count, any integer
//     class); DIV, MOD and SHR divide and shift right as `class`'s signedness says. The generated code wraps.
//   - PTR_ADD: the first operand (U64) plus the second (I64 or U64) times `value`, the element's size (nonzero).
//   - EQ ... GE: 1 or 0 (`class` I32) as the comparison of the two operands, of the operand's class, holds: signed or
//     unsigned by it, and for floating values IEEE's, false when either is NaN but for NE.
//   - STORE (and SET): stores the second operand (of `class`) at the address the first (U64) holds, and pushes the
//     value stored (and pushes nothing). STORE_LOCAL and SET_LOCAL store the operand in local `value`, and
//     STORE_GLOBAL and SET_GLOBAL in the object at SymbolId `value`, likewise.
//   - CALL: call `value` of the calls table, which pops its arguments, the first pushed first, and pushes its result
//     (of `class`, or nothing for VOID).
//   - DISCARD: pops the operand (of `class`): an expression statement's value.
//   - BRANCH_FALSE and BRANCH_TRUE: pop the operand (an integer of `class`) and go to entry `value` if it is zero, or
//     isn't.
//   - JUMP: goes to entry `value`. JUMP_VALUE: the same, taking the operand (of `class`) there: an arm of ?: or of a
//     value's && and ||, whose value the entry `value` finds on the stack as the arm after it leaves its own.
//   - RETURN: returns the operand (of `class`), or nothing when `class` is VOID; at statement level (with nothing else
//     on the stacks).
// A target `value` is an entry's index, or the entry count for the function's end. Every target is the start of a
// statement or of an arm, and the stacks there must hold the same values whichever way control arrives (as in a
// WebAssembly validator): tier0_validate checks it.
#define BUSTER_TYPED_NONE 0
#define BUSTER_TYPED_CONST 1
#define BUSTER_TYPED_LOCAL 2
#define BUSTER_TYPED_LOCAL_ADDRESS 3
#define BUSTER_TYPED_GLOBAL 4
#define BUSTER_TYPED_GLOBAL_ADDRESS 5
#define BUSTER_TYPED_DUP 6
#define BUSTER_TYPED_NEG 7
#define BUSTER_TYPED_NOT 8
#define BUSTER_TYPED_CONVERT 9
#define BUSTER_TYPED_LOAD 10
#define BUSTER_TYPED_ADD 11
#define BUSTER_TYPED_SUB 12
#define BUSTER_TYPED_MUL 13
#define BUSTER_TYPED_DIV 14
#define BUSTER_TYPED_MOD 15
#define BUSTER_TYPED_AND 16
#define BUSTER_TYPED_OR 17
#define BUSTER_TYPED_XOR 18
#define BUSTER_TYPED_SHL 19
#define BUSTER_TYPED_SHR 20
#define BUSTER_TYPED_PTR_ADD 21
#define BUSTER_TYPED_EQ 22
#define BUSTER_TYPED_NE 23
#define BUSTER_TYPED_LT 24
#define BUSTER_TYPED_LE 25
#define BUSTER_TYPED_GT 26
#define BUSTER_TYPED_GE 27
#define BUSTER_TYPED_STORE 28
#define BUSTER_TYPED_SET 29
#define BUSTER_TYPED_STORE_LOCAL 30
#define BUSTER_TYPED_SET_LOCAL 31
#define BUSTER_TYPED_STORE_GLOBAL 32
#define BUSTER_TYPED_SET_GLOBAL 33
#define BUSTER_TYPED_CALL 34
#define BUSTER_TYPED_DISCARD 35
#define BUSTER_TYPED_BRANCH_FALSE 36
#define BUSTER_TYPED_BRANCH_TRUE 37
#define BUSTER_TYPED_JUMP 38
#define BUSTER_TYPED_JUMP_VALUE 39
#define BUSTER_TYPED_RETURN 40
#define BUSTER_TYPED_KINDS 41
// Every kind below 64, so that a kind table is one ZMM register for vpermb.
#define BUSTER_TYPED_KIND_LIMIT 64

// A local's flags: its address is taken (LOCAL_ADDRESS), so it lives in the frame.
#define BUSTER_TYPED_LOCAL_ADDRESS_TAKEN 0x01

// The most a function may have of each (the columns' indices are 32-bit).
#define BUSTER_TIER0_MAX_ENTRIES (1u << 24)
#define BUSTER_TIER0_MAX_LOCALS (1u << 16)
#define BUSTER_TIER0_MAX_CALL_ARGUMENTS 64

// A function, as the front end hands it to Tier 0: the body's columns, the locals (parameters first, in order) with
// their classes and flags, the parameters' locations at entry and the result's (abi_assign of the function's own type),
// and the calls: call c's callee (a SymbolId), its arguments' locations and classes (call_first[c] onward, in
// `argument_locations` and `argument_classes`, call_argument_counts[c] of them), whether it is variadic (al counts its
// vector registers), and its totals (AbiCall), from abi_assign of the call's signature. The ABI's work is the front
// end's, in one batch per unit (abi_assign is vectorized across calls), so Tier 0 needs no type table.
typedef struct Tier0Function Tier0Function;
struct Tier0Function
{
    const uint8_t* kinds;
    const uint8_t* classes;
    const uint64_t* values;
    uint32_t count;
    const uint8_t* local_classes;
    const uint8_t* local_flags;
    uint32_t local_count;
    uint32_t parameter_count;
    const AbiLocation* parameters;
    AbiReturn result;
    const SymbolId* call_targets;
    const uint32_t* call_first;
    const uint32_t* call_argument_counts;
    const uint8_t* call_variadic;
    const AbiCall* calls;
    const AbiLocation* argument_locations;
    const uint8_t* argument_classes;
    uint32_t call_count;
    uint32_t argument_count;
};

// ---------------------------------------------------------------------------------------------------------------------
// The output

typedef enum Tier0Error
{
    TIER0_OK,
    // An entry no template or deferred emitter covers yet (a class or a conversion the first slice leaves out):
    // `node` names it.
    TIER0_ERROR_UNSUPPORTED,
    // An expression deeper than the depth tables (more than 9 integer or 16 floating values live at once): spilling
    // the deep values is the scalar path's, and not written yet.
    TIER0_ERROR_DEPTH,
    // The output arena or the scratch arena has no room.
    TIER0_ERROR_MEMORY,
    // tier0_validate's alone: the stream breaks a rule (`node` and Tier0Check.rule say which).
    TIER0_ERROR_INVALID,
} Tier0Error;

// A function's code: `count` records (prologue, body, epilogue), the branches' record indices and targets (contract 1:
// every branch a rel8 with displacement 0, indices ascending, a target the record its label marks or the count for the
// end), and a LinkReference per symbolic field, its instruction a record index. `deferred` counts the entries the
// vector path left to tier0_emit_deferred (the scalar path counts the same ones).
typedef struct Tier0Code Tier0Code;
struct Tier0Code
{
    X86Instruction* records;
    uint32_t count;
    uint32_t* branches;
    uint32_t* targets;
    uint32_t branch_count;
    LinkReference* references;
    uint32_t reference_count;
    uint32_t deferred;
    Tier0Error error;
    uint32_t node;
};

// What tier0_validate found: TIER0_OK, or TIER0_ERROR_INVALID with the first entry that breaks a rule and the rule
// (a short description, for the tests' messages).
typedef struct Tier0Check Tier0Check;
struct Tier0Check
{
    Tier0Error error;
    uint32_t node;
    const char* rule;
};

// Builds the templates. Returns false if one doesn't fit its slot (a broken table). Requires x86_encoder_initialize and
// x86_semantics_initialize.
static __attribute__((unused)) bool tier0_initialize(void);
// Compiles a function that tier0_validate accepts: the vector path. The records and columns come from `output`; the
// passes' columns from the thread's scratch arenas. Returns the code, or an error with nothing kept in `output`.
static __attribute__((unused)) Tier0Code tier0_compile(const Tier0Function* function, Arena* output);
// The same, an entry at a time: the twin and oracle, with identical results.
static __attribute__((unused)) Tier0Code tier0_compile_scalar(const Tier0Function* function, Arena* output);
// Whether the function keeps the typed stream's rules (the kinds, classes and values, operands and depths, targets and
// the stacks at them, locals and calls), for the tests and for debugging.
static __attribute__((unused)) Tier0Check tier0_validate(const Tier0Function* function);

// ---------------------------------------------------------------------------------------------------------------------
// Registers

// The depth tables: the register of the value at each depth of each stack. The caller-saved registers first, so that
// leaf code saves nothing: rax first, so that a call's result and a function's return value are at depth 0 where they
// arrive and leave, and division's dividend and quotient are rax's at depth 0; rcx next, so that a shift's count at
// depth 1 is cl. Then, for the rare expressions deeper than that, the callee-saved ones: the prologue saves those a
// function's depths reach, and a call needn't, since its callee preserves them. Contract 6 of #135 (the order) is
// decided in docs/tier0.md. XMM registers are all caller-saved: the floating stack has the 16.
#define BUSTER_TIER0_INTEGER_DEPTHS 15
#define BUSTER_TIER0_CALLER_SAVED_DEPTHS 9
#define BUSTER_TIER0_FLOATING_DEPTHS 16
static const uint8_t tier0_integer_depths[BUSTER_TIER0_INTEGER_DEPTHS] = {
    X86_REGISTER_RAX,
    X86_REGISTER_RCX,
    X86_REGISTER_RDX,
    X86_REGISTER_RSI,
    X86_REGISTER_RDI,
    X86_REGISTER_R8,
    X86_REGISTER_R9,
    X86_REGISTER_R10,
    X86_REGISTER_R11,
    X86_REGISTER_RBX,
    X86_REGISTER_R12,
    X86_REGISTER_R13,
    X86_REGISTER_R14,
    X86_REGISTER_R15,
    X86_REGISTER_RBP,
};

// The callee-saved registers, in the order the prologue pushes them and homes take them.
#define BUSTER_TIER0_CALLEE_SAVED 6
static const uint8_t tier0_callee_saved[BUSTER_TIER0_CALLEE_SAVED] = {
    X86_REGISTER_RBX,
    X86_REGISTER_RBP,
    X86_REGISTER_R12,
    X86_REGISTER_R13,
    X86_REGISTER_R14,
    X86_REGISTER_R15,
};

// A register set: a bit per general register (bits 0 to 15) and per XMM register (bits 16 to 31).
#define BUSTER_TIER0_XMM(n) (16 + (n))
#define BUSTER_TIER0_GENERAL_CALLER_SAVED                                                                              \
    (1u << X86_REGISTER_RAX | 1u << X86_REGISTER_RCX | 1u << X86_REGISTER_RDX | 1u << X86_REGISTER_RSI |               \
     1u << X86_REGISTER_RDI | 1u << X86_REGISTER_R8 | 1u << X86_REGISTER_R9 | 1u << X86_REGISTER_R10 |                \
     1u << X86_REGISTER_R11)
#define BUSTER_TIER0_XMM_ALL 0xffff0000u

// The condition codes, as the encoder's Jcc and SETcc rows number them (their 4-bit tttn field).
#define BUSTER_TIER0_CC_B 2
#define BUSTER_TIER0_CC_AE 3
#define BUSTER_TIER0_CC_E 4
#define BUSTER_TIER0_CC_NE 5
#define BUSTER_TIER0_CC_BE 6
#define BUSTER_TIER0_CC_A 7
#define BUSTER_TIER0_CC_L 12
#define BUSTER_TIER0_CC_GE 13
#define BUSTER_TIER0_CC_LE 14
#define BUSTER_TIER0_CC_G 15
static const uint16_t tier0_jcc_rel8[16] = {
    X86_FORM_JO_REL8,
    X86_FORM_JNO_REL8,
    X86_FORM_JB_REL8,
    X86_FORM_JAE_REL8,
    X86_FORM_JE_REL8,
    X86_FORM_JNE_REL8,
    X86_FORM_JBE_REL8,
    X86_FORM_JA_REL8,
    X86_FORM_JS_REL8,
    X86_FORM_JNS_REL8,
    X86_FORM_JP_REL8,
    X86_FORM_JNP_REL8,
    X86_FORM_JL_REL8,
    X86_FORM_JGE_REL8,
    X86_FORM_JLE_REL8,
    X86_FORM_JG_REL8,
};
static const uint16_t tier0_setcc[16] = {
    X86_FORM_SETO_RM8,
    X86_FORM_SETNO_RM8,
    X86_FORM_SETB_RM8,
    X86_FORM_SETAE_RM8,
    X86_FORM_SETE_RM8,
    X86_FORM_SETNE_RM8,
    X86_FORM_SETBE_RM8,
    X86_FORM_SETA_RM8,
    X86_FORM_SETS_RM8,
    X86_FORM_SETNS_RM8,
    X86_FORM_SETP_RM8,
    X86_FORM_SETNP_RM8,
    X86_FORM_SETL_RM8,
    X86_FORM_SETGE_RM8,
    X86_FORM_SETLE_RM8,
    X86_FORM_SETG_RM8,
};
// The status flags a condition reads (BUSTER_X86_FLAG_*), which stay live from the compare to the jcc or setcc.
static const uint8_t tier0_condition_flags[16] = {
    BUSTER_X86_FLAG_OF,
    BUSTER_X86_FLAG_OF,
    BUSTER_X86_FLAG_CF,
    BUSTER_X86_FLAG_CF,
    BUSTER_X86_FLAG_ZF,
    BUSTER_X86_FLAG_ZF,
    BUSTER_X86_FLAG_CF | BUSTER_X86_FLAG_ZF,
    BUSTER_X86_FLAG_CF | BUSTER_X86_FLAG_ZF,
    BUSTER_X86_FLAG_SF,
    BUSTER_X86_FLAG_SF,
    BUSTER_X86_FLAG_PF,
    BUSTER_X86_FLAG_PF,
    BUSTER_X86_FLAG_SF | BUSTER_X86_FLAG_OF,
    BUSTER_X86_FLAG_SF | BUSTER_X86_FLAG_OF,
    BUSTER_X86_FLAG_ZF | BUSTER_X86_FLAG_SF | BUSTER_X86_FLAG_OF,
    BUSTER_X86_FLAG_ZF | BUSTER_X86_FLAG_SF | BUSTER_X86_FLAG_OF,
};

static bool tier0_class_floating(uint32_t class)
{
    return class == BUSTER_TYPED_CLASS_F32 || class == BUSTER_TYPED_CLASS_F64;
}

static bool tier0_class_integer(uint32_t class)
{
    return class >= BUSTER_TYPED_CLASS_I8 && class <= BUSTER_TYPED_CLASS_U64;
}

static bool tier0_class_signed(uint32_t class)
{
    return class == BUSTER_TYPED_CLASS_I8 || class == BUSTER_TYPED_CLASS_I16 || class == BUSTER_TYPED_CLASS_I32 || class == BUSTER_TYPED_CLASS_I64;
}

// A class's width in bytes (0 for VOID).
static uint32_t tier0_class_bytes(uint32_t class)
{
    static const uint8_t bytes[BUSTER_TYPED_CLASS_LIMIT] = { 0, 1, 1, 2, 2, 4, 4, 8, 8, 4, 8 };
    return bytes[class & (BUSTER_TYPED_CLASS_LIMIT - 1)];
}

// ---------------------------------------------------------------------------------------------------------------------
// Templates

// The hole block: the 32 bytes of an entry that its template's holes take, in this order.
//   - K, the constant (8 bytes): a fused constant's value, an element's size, or their product.
//   - A, the home of the fused local (8 bytes): its displacement (4), register (1) and addressing mode (1).
//   - B, the home of the entry's own local, likewise.
//   - The registers of the depths around the entry: the integer stack's at its depth less 2, less 1 and at it (R2, R1,
//     R0), and the floating stack's (X2, X1, X0). An operand pushed last is at depth less 1; depth itself is free.
#define BUSTER_TIER0_HOLE_BYTES 32
#define BUSTER_TIER0_HOLE_K 0
#define BUSTER_TIER0_HOLE_A 8
#define BUSTER_TIER0_HOLE_B 16
#define BUSTER_TIER0_HOLE_DISPLACEMENT 0
#define BUSTER_TIER0_HOLE_REGISTER 4
#define BUSTER_TIER0_HOLE_MODE 5
#define BUSTER_TIER0_HOLE_REGISTERS 24

// Where a register field comes from: a depth's register (R2 ... X0, in the hole block's order), a home's register (A or
// B, for a template that knows the home is a register), or a fixed register (FIXED plus its number).
#define BUSTER_TIER0_SOURCE_NONE 0
#define BUSTER_TIER0_SOURCE_R2 1
#define BUSTER_TIER0_SOURCE_R1 2
#define BUSTER_TIER0_SOURCE_R0 3
#define BUSTER_TIER0_SOURCE_X2 4
#define BUSTER_TIER0_SOURCE_X1 5
#define BUSTER_TIER0_SOURCE_X0 6
#define BUSTER_TIER0_SOURCE_A 7
#define BUSTER_TIER0_SOURCE_B 8
#define BUSTER_TIER0_SOURCE_FIXED 0x10
#define BUSTER_TIER0_FIXED(r) (BUSTER_TIER0_SOURCE_FIXED | (r))

// What the rm operand is: none; a register (from `rm`, a source); a home (A or B: a register, or [rsp + disp]); memory at
// a register (`rm`), at a register plus K, or at a register plus another (`index`) times 2^`scale`; or RIP-relative, a
// symbolic displacement (the entry's SymbolId, through a fixup).
#define BUSTER_TIER0_RM_NONE 0
#define BUSTER_TIER0_RM_REGISTER 1
#define BUSTER_TIER0_RM_HOME_A 2
#define BUSTER_TIER0_RM_HOME_B 3
#define BUSTER_TIER0_RM_BASE 4
#define BUSTER_TIER0_RM_BASE_K 5
#define BUSTER_TIER0_RM_BASE_INDEX 6
#define BUSTER_TIER0_RM_RIP 7

// What the immediate is: none, the spec's own (with its displacement for a 64-bit one), or K's low 32 bits.
#define BUSTER_TIER0_IMMEDIATE_NONE 0
#define BUSTER_TIER0_IMMEDIATE_STATIC 1
#define BUSTER_TIER0_IMMEDIATE_K 2

// A record's role besides its fields: a branch to the entry's target (its value) or to the function's end (a return's
// jump to the epilogue); a symbolic displacement (PC32 to the entry's SymbolId); an immediate whose form the choice pass
// makes the shortest that keeps it (with `live`, the status flags the template reads after the record).
#define BUSTER_TIER0_RECORD_BRANCH 0x01
#define BUSTER_TIER0_RECORD_END 0x02
#define BUSTER_TIER0_RECORD_FIXUP 0x04
#define BUSTER_TIER0_RECORD_CHOICE 0x08

typedef struct Tier0RecordSpec Tier0RecordSpec;
struct Tier0RecordSpec
{
    uint16_t form;
    uint8_t reg;
    uint8_t rm_kind;
    uint8_t rm;
    uint8_t index;
    uint8_t scale;
    uint8_t immediate_kind;
    int32_t immediate;
    int32_t displacement;
    uint8_t flags;
    uint8_t live;
    // A CHOICE record's row (x86_choice_row of its form's operation and width), which tier0_end fills in.
    uint16_t row;
};

// The variants: which operands an entry absorbs, and how. The key of a template is (kind, class, variant), where for a
// branch that absorbs a compare the kind and class are the compare's (and its operands' class), for x = x op v the
// operation's, and for CONVERT the variant is the source class (plus BUSTER_TIER0_VARIANT_CONVERT_HOME from a home).
//   - PLAIN: every operand on the stack. CONST: the last operand is a constant (K). HOME: the last is a local (A).
//     HOME_CONST: the first operand is a local (A) and the last a constant (K). For a floating constant, CONST is its
//     zero (xorps).
//   - INDEX(s): a load's address (or a pointer addition's lea) is [R2 + R1 * 2^s]; OFFSET: [R1 + K].
//   - BRANCH_FALSE and BRANCH_TRUE, plus the compare's PLAIN to HOME_CONST: a branch with its compare.
//   - RMW_CONST and RMW_HOME: x = x op v as op x, v (SET_LOCAL's).
//   - INDEX_CONST(s), INDEX_HOME(s), OFFSET_HOME: a store's address [R2 + R1 * 2^s] (or [R1 + K]) and its value a
//     constant (K) or a local (A).
//   - LAST: a return that is the function's last entry, without the jump to the epilogue.
#define BUSTER_TIER0_VARIANT_PLAIN 0
#define BUSTER_TIER0_VARIANT_CONST 1
#define BUSTER_TIER0_VARIANT_HOME 2
#define BUSTER_TIER0_VARIANT_HOME_CONST 3
#define BUSTER_TIER0_VARIANT_INDEX(s) (4 + (s))
#define BUSTER_TIER0_VARIANT_OFFSET 8
#define BUSTER_TIER0_VARIANT_BRANCH_FALSE 9
#define BUSTER_TIER0_VARIANT_BRANCH_TRUE 13
#define BUSTER_TIER0_VARIANT_RMW_CONST 17
#define BUSTER_TIER0_VARIANT_RMW_HOME 18
#define BUSTER_TIER0_VARIANT_INDEX_CONST(s) (19 + (s))
#define BUSTER_TIER0_VARIANT_INDEX_HOME(s) (23 + (s))
#define BUSTER_TIER0_VARIANT_OFFSET_HOME 27
#define BUSTER_TIER0_VARIANT_LAST 28
#define BUSTER_TIER0_VARIANT_CONVERT_HOME 16
#define BUSTER_TIER0_VARIANTS 32

// A template: up to four records, as specifications (for the scalar expansion and the passes after expansion) and
// compiled (for the vector expansion: the records' static bytes, each byte's hole in the hole block, and which bytes take
// their hole). Its holes: K is the value of entry `k_offset` back (BUSTER_TIER0_NO_OFFSET: none), times the value of
// entry `k_scale` back when there is one (a pointer addition's element size); A is the home of the local entry
// `a_offset` back reads. `required_depth` is the integer depth its fixed registers need (BUSTER_TIER0_ANY_DEPTH: any);
// at another, the entry goes to tier0_emit_deferred. `reach` is how far above the entry's depth its registers go on each
// stack (3 for R0 or X0, 2 for R1, 1 for R2, 0 for none), and `clobbers` the fixed registers it writes.
#define BUSTER_TIER0_TEMPLATE_RECORDS 4
#define BUSTER_TIER0_NO_OFFSET 0xff
#define BUSTER_TIER0_ANY_DEPTH 0xff
typedef struct Tier0Template Tier0Template;
struct Tier0Template
{
    _Alignas(64) uint8_t bytes[64];
    uint8_t holes[64];
    uint64_t mask;
    Tier0RecordSpec specs[BUSTER_TIER0_TEMPLATE_RECORDS];
    uint8_t length;
    uint8_t k_offset;
    uint8_t k_scale;
    uint8_t a_offset;
    uint8_t required_depth;
    uint8_t integer_reach;
    uint8_t floating_reach;
    uint32_t clobbers;
};

// The template table and the slots that find a key's template: slot_bases[kind * 16 + class] is the first of the key's
// 32 slots (one per variant), or the unsupported block's. A slot holds a template's index, with HOME_DEPENDENT when four
// templates follow for its homes (A and B each a register or memory: the index plus A's bit plus twice B's), or
// DEFERRED (tier0_emit_deferred's), or UNSUPPORTED.
#define BUSTER_TIER0_TEMPLATE_CAPACITY 2048
#define BUSTER_TIER0_SLOT_CAPACITY (400 * BUSTER_TIER0_VARIANTS)
#define BUSTER_TIER0_SLOT_UNSUPPORTED 0xffff
#define BUSTER_TIER0_SLOT_DEFERRED 0xfffe
#define BUSTER_TIER0_SLOT_HOME_DEPENDENT 0x8000
#define BUSTER_TIER0_SLOT_TEMPLATE 0x7fff
// An entry's template column: a template's index, or an absorbed entry's (no records) or a deferred one's.
#define BUSTER_TIER0_ENTRY_ABSORBED 0xffff
#define BUSTER_TIER0_ENTRY_DEFERRED 0xfffe

static Tier0Template tier0_templates[BUSTER_TIER0_TEMPLATE_CAPACITY];
static uint32_t tier0_template_count;
static uint16_t tier0_slot_bases[BUSTER_TYPED_KIND_LIMIT * BUSTER_TYPED_CLASS_LIMIT];
static uint16_t tier0_slots[BUSTER_TIER0_SLOT_CAPACITY];
static uint32_t tier0_slot_count;
static bool tier0_initialized;

// The template the builder is making, its key, and whether everything fit.
typedef struct Tier0Builder Tier0Builder;
struct Tier0Builder
{
    Tier0Template current;
    bool valid;
};

static Tier0Builder tier0_builder;

static void tier0_build_windows(void);

static Tier0RecordSpec tier0_spec(uint32_t form)
{
    Tier0RecordSpec spec = {
        .form = (uint16_t)form,
        .reg = BUSTER_TIER0_SOURCE_NONE,
        .rm_kind = BUSTER_TIER0_RM_NONE,
        .rm = BUSTER_TIER0_SOURCE_NONE,
        .index = BUSTER_TIER0_SOURCE_NONE,
        .scale = 0,
        .immediate_kind = BUSTER_TIER0_IMMEDIATE_NONE,
        .immediate = 0,
        .displacement = 0,
        .flags = 0,
        .live = 0,
        .row = BUSTER_X86_CHOICE_NO_ROW,
    };
    return spec;
}

// form reg, rm (both registers).
static Tier0RecordSpec tier0_rr(uint32_t form, uint8_t reg, uint8_t rm)
{
    Tier0RecordSpec spec = tier0_spec(form);
    spec.reg = reg;
    spec.rm_kind = BUSTER_TIER0_RM_REGISTER;
    spec.rm = rm;
    return spec;
}

// form reg, home (or form home, reg: the syntax decides, not the fields).
static Tier0RecordSpec tier0_rh(uint32_t form, uint8_t reg, uint8_t home)
{
    Tier0RecordSpec spec = tier0_spec(form);
    spec.reg = reg;
    spec.rm_kind = home;
    return spec;
}

// form reg, [base] (or [base + K], or [base + index * 2^scale]).
static Tier0RecordSpec tier0_rmem(uint32_t form, uint8_t reg, uint8_t rm_kind, uint8_t base, uint8_t index, uint32_t scale)
{
    Tier0RecordSpec spec = tier0_spec(form);
    spec.reg = reg;
    spec.rm_kind = rm_kind;
    spec.rm = base;
    spec.index = index;
    spec.scale = (uint8_t)scale;
    return spec;
}

// form reg, [rip + symbol].
static Tier0RecordSpec tier0_rrip(uint32_t form, uint8_t reg)
{
    Tier0RecordSpec spec = tier0_spec(form);
    spec.reg = reg;
    spec.rm_kind = BUSTER_TIER0_RM_RIP;
    spec.flags = BUSTER_TIER0_RECORD_FIXUP;
    return spec;
}

// An immediate: K's low bits, which the choice pass makes the shortest form for (with the flags live after it).
static Tier0RecordSpec tier0_with_k(Tier0RecordSpec spec, uint8_t live)
{
    spec.immediate_kind = BUSTER_TIER0_IMMEDIATE_K;
    spec.flags |= BUSTER_TIER0_RECORD_CHOICE;
    spec.live = live;
    return spec;
}

// A fixed immediate (and displacement, the high half of a 64-bit one).
static Tier0RecordSpec tier0_with_immediate(Tier0RecordSpec spec, int32_t immediate, int32_t high)
{
    spec.immediate_kind = BUSTER_TIER0_IMMEDIATE_STATIC;
    spec.immediate = immediate;
    spec.displacement = high;
    return spec;
}

// rm as a register (for the forms that name it alone: neg rm, setcc rm, push r).
static Tier0RecordSpec tier0_r(uint32_t form, uint8_t rm)
{
    return tier0_rr(form, BUSTER_TIER0_SOURCE_NONE, rm);
}

static Tier0RecordSpec tier0_branch(uint32_t form, uint8_t flags)
{
    Tier0RecordSpec spec = tier0_spec(form);
    spec.flags = flags;
    return spec;
}

static void tier0_begin(void)
{
    memset(&tier0_builder.current, 0, sizeof(tier0_builder.current));
    tier0_builder.current.k_offset = BUSTER_TIER0_NO_OFFSET;
    tier0_builder.current.k_scale = BUSTER_TIER0_NO_OFFSET;
    tier0_builder.current.a_offset = BUSTER_TIER0_NO_OFFSET;
    tier0_builder.current.required_depth = BUSTER_TIER0_ANY_DEPTH;
}

static void tier0_add(Tier0RecordSpec spec)
{
    Tier0Template* template = &tier0_builder.current;
    if (template->length < BUSTER_TIER0_TEMPLATE_RECORDS)
    {
        template->specs[template->length] = spec;
        template->length += 1;
    }
    else
    {
        tier0_builder.valid = false;
    }
}

// The hole-block byte of a register source, or 0xff for a fixed register or none.
static uint32_t tier0_source_hole(uint8_t source)
{
    uint32_t hole = 0xff;
    if (source >= BUSTER_TIER0_SOURCE_R2 && source <= BUSTER_TIER0_SOURCE_X0)
    {
        hole = BUSTER_TIER0_HOLE_REGISTERS + (uint32_t)(source - BUSTER_TIER0_SOURCE_R2);
    }
    else if (source == BUSTER_TIER0_SOURCE_A)
    {
        hole = BUSTER_TIER0_HOLE_A + BUSTER_TIER0_HOLE_REGISTER;
    }
    else if (source == BUSTER_TIER0_SOURCE_B)
    {
        hole = BUSTER_TIER0_HOLE_B + BUSTER_TIER0_HOLE_REGISTER;
    }
    return hole;
}

// How far above the depth a source reaches on its stack (BUSTER_TIER0_SOURCE_R2 ... X0), as Tier0Template.reach counts.
static void tier0_note_reach(Tier0Template* template, uint8_t source)
{
    if (source >= BUSTER_TIER0_SOURCE_R2 && source <= BUSTER_TIER0_SOURCE_R0)
    {
        const uint8_t reach = (uint8_t)(source - BUSTER_TIER0_SOURCE_R2 + 1);
        template->integer_reach = reach > template->integer_reach ? reach : template->integer_reach;
    }
    else if (source >= BUSTER_TIER0_SOURCE_X2 && source <= BUSTER_TIER0_SOURCE_X0)
    {
        const uint8_t reach = (uint8_t)(source - BUSTER_TIER0_SOURCE_X2 + 1);
        template->floating_reach = reach > template->floating_reach ? reach : template->floating_reach;
    }
}

// Sets one byte of a compiled record: a static byte, or a hole's.
static void tier0_compile_byte(Tier0Template* template, uint32_t record, uint32_t offset, uint32_t hole, uint8_t value)
{
    const uint32_t at = record * sizeof(X86Instruction) + offset;
    if (hole != 0xff)
    {
        template->holes[at] = (uint8_t)hole;
        template->mask |= 1ull << at;
    }
    else
    {
        template->bytes[at] = value;
    }
}

// A register field's byte: its hole, or the fixed register.
static void tier0_compile_register(Tier0Template* template, uint32_t record, uint32_t offset, uint8_t source)
{
    const uint8_t fixed = (source & BUSTER_TIER0_SOURCE_FIXED) != 0 ? (uint8_t)(source & ~BUSTER_TIER0_SOURCE_FIXED) : 0;
    tier0_compile_byte(template, record, offset, tier0_source_hole(source), fixed);
    tier0_note_reach(template, source);
}

// A home's operand bytes: register, mode and displacement from the home's part of the hole block.
static void tier0_compile_home(Tier0Template* template, uint32_t record, uint32_t home)
{
    tier0_compile_byte(template, record, offsetof(X86Instruction, rm), home + BUSTER_TIER0_HOLE_REGISTER, 0);
    tier0_compile_byte(template, record, offsetof(X86Instruction, mode), home + BUSTER_TIER0_HOLE_MODE, 0);
    for (uint32_t b = 0; b < 4; b += 1)
    {
        tier0_compile_byte(template, record, offsetof(X86Instruction, displacement) + b, home + BUSTER_TIER0_HOLE_DISPLACEMENT + b, 0);
    }
}

// Compiles the template's specifications into its bytes, holes and mask (the vector expansion's form of them).
static void tier0_compile_template(Tier0Template* template)
{
    for (uint32_t r = 0; r < template->length; r += 1)
    {
        const Tier0RecordSpec* spec = &template->specs[r];
        X86Instruction fixed = { 0 };
        fixed.form = (uint16_t)(spec->form | ((spec->flags & BUSTER_TIER0_RECORD_FIXUP) != 0 ? BUSTER_X86_FORM_FIXUP : 0));
        if (spec->immediate_kind == BUSTER_TIER0_IMMEDIATE_STATIC)
        {
            fixed.immediate = spec->immediate;
            fixed.displacement = spec->displacement;
        }
        if (spec->rm_kind == BUSTER_TIER0_RM_BASE || spec->rm_kind == BUSTER_TIER0_RM_BASE_K)
        {
            fixed.mode = BUSTER_X86_MEMORY_BASE;
        }
        else if (spec->rm_kind == BUSTER_TIER0_RM_BASE_INDEX)
        {
            fixed.mode = (uint8_t)BUSTER_X86_MEMORY_BASE_INDEX(spec->scale);
        }
        else if (spec->rm_kind == BUSTER_TIER0_RM_RIP)
        {
            fixed.mode = BUSTER_X86_MEMORY_RIP;
        }
        memcpy(template->bytes + r * sizeof(X86Instruction), &fixed, sizeof(fixed));
        if (spec->reg != BUSTER_TIER0_SOURCE_NONE)
        {
            tier0_compile_register(template, r, offsetof(X86Instruction, reg), spec->reg);
        }
        if (spec->rm_kind == BUSTER_TIER0_RM_REGISTER || spec->rm_kind == BUSTER_TIER0_RM_BASE || spec->rm_kind == BUSTER_TIER0_RM_BASE_K ||
            spec->rm_kind == BUSTER_TIER0_RM_BASE_INDEX)
        {
            tier0_compile_register(template, r, offsetof(X86Instruction, rm), spec->rm);
        }
        if (spec->rm_kind == BUSTER_TIER0_RM_BASE_INDEX)
        {
            tier0_compile_register(template, r, offsetof(X86Instruction, index), spec->index);
        }
        if (spec->rm_kind == BUSTER_TIER0_RM_HOME_A || spec->rm_kind == BUSTER_TIER0_RM_HOME_B)
        {
            tier0_compile_home(template, r, spec->rm_kind == BUSTER_TIER0_RM_HOME_A ? BUSTER_TIER0_HOLE_A : BUSTER_TIER0_HOLE_B);
        }
        if (spec->rm_kind == BUSTER_TIER0_RM_BASE_K)
        {
            for (uint32_t b = 0; b < 4; b += 1)
            {
                tier0_compile_byte(template, r, offsetof(X86Instruction, displacement) + b, BUSTER_TIER0_HOLE_K + b, 0);
            }
        }
        if (spec->immediate_kind == BUSTER_TIER0_IMMEDIATE_K)
        {
            for (uint32_t b = 0; b < 4; b += 1)
            {
                tier0_compile_byte(template, r, offsetof(X86Instruction, immediate) + b, BUSTER_TIER0_HOLE_K + b, 0);
            }
        }
    }
}

// Ends the template being built and puts it in the slot of (kind, class, variant): `home_part` is 0 for a plain slot,
// or 1 to 4 for the four templates of a home-dependent one, in order (A memory and B memory, A register, B register,
// both registers); the first of those makes the slot.
static void tier0_end(uint32_t kind, uint32_t class, uint32_t variant, uint32_t home_part)
{
    Tier0Template* template = &tier0_builder.current;
    const uint32_t key = kind * BUSTER_TYPED_CLASS_LIMIT + class;
    if (tier0_template_count >= BUSTER_TIER0_TEMPLATE_CAPACITY || variant >= BUSTER_TIER0_VARIANTS || kind >= BUSTER_TYPED_KIND_LIMIT ||
        class >= BUSTER_TYPED_CLASS_LIMIT)
    {
        tier0_builder.valid = false;
    }
    else
    {
        if (tier0_slot_bases[key] == 0)
        {
            if (tier0_slot_count + BUSTER_TIER0_VARIANTS <= BUSTER_TIER0_SLOT_CAPACITY)
            {
                tier0_slot_bases[key] = (uint16_t)tier0_slot_count;
                for (uint32_t v = 0; v < BUSTER_TIER0_VARIANTS; v += 1)
                {
                    tier0_slots[tier0_slot_count + v] = BUSTER_TIER0_SLOT_UNSUPPORTED;
                }
                tier0_slot_count += BUSTER_TIER0_VARIANTS;
            }
            else
            {
                tier0_builder.valid = false;
            }
        }
        const uint32_t slot = tier0_slot_bases[key] + variant;
        for (uint32_t r = 0; r < template->length; r += 1)
        {
            Tier0RecordSpec* spec = &template->specs[r];
            if ((spec->flags & BUSTER_TIER0_RECORD_CHOICE) != 0)
            {
                const X86Effects* effects = x86_effects(spec->form);
                spec->row = (uint16_t)x86_choice_row((X86Operation)effects->operation, effects->width);
                tier0_builder.valid &= spec->row != BUSTER_X86_CHOICE_NO_ROW;
            }
        }
        tier0_compile_template(template);
        const uint32_t index = tier0_template_count;
        tier0_templates[index] = *template;
        tier0_template_count += 1;
        if (home_part <= 1 && tier0_builder.valid)
        {
            // A slot is made once: a second template for it is a broken table.
            tier0_builder.valid = tier0_slots[slot] == BUSTER_TIER0_SLOT_UNSUPPORTED;
            tier0_slots[slot] = (uint16_t)(index | (home_part == 1 ? BUSTER_TIER0_SLOT_HOME_DEPENDENT : 0));
        }
    }
}

// The slot of (kind, class, variant) as deferred: tier0_emit_deferred makes its records.
static void tier0_defer(uint32_t kind, uint32_t class, uint32_t variant)
{
    tier0_begin();
    tier0_end(kind, class, variant, 0);
    const uint32_t slot = tier0_slot_bases[kind * BUSTER_TYPED_CLASS_LIMIT + class] + variant;
    tier0_template_count -= 1;
    tier0_slots[slot] = BUSTER_TIER0_SLOT_DEFERRED;
}

// Forms by an integer class's width: the 32-bit form for 1 to 4 bytes (C computes in int and wider), the 64-bit one
// for 8, and for the forms that move a class's own bytes, the 8-, 16-, 32- or 64-bit one.
static uint32_t tier0_wide(uint32_t class, uint32_t form32, uint32_t form64)
{
    return tier0_class_bytes(class) == 8 ? form64 : form32;
}

static uint32_t tier0_sized(uint32_t class, uint32_t form8, uint32_t form16, uint32_t form32, uint32_t form64)
{
    const uint32_t bytes = tier0_class_bytes(class);
    return bytes == 1 ? form8 : bytes == 2 ? form16 : bytes == 4 ? form32 : form64;
}

// A register load of an integer class from r/m: sign- or zero-extending a narrow one to 32 bits.
static uint32_t tier0_load_form(uint32_t class)
{
    uint32_t form = X86_FORM_MOV_R64_RM64;
    switch (class)
    {
    case BUSTER_TYPED_CLASS_I8:
        form = X86_FORM_MOVSX_R32_RM8;
        break;
    case BUSTER_TYPED_CLASS_U8:
        form = X86_FORM_MOVZX_R32_RM8;
        break;
    case BUSTER_TYPED_CLASS_I16:
        form = X86_FORM_MOVSX_R32_RM16;
        break;
    case BUSTER_TYPED_CLASS_U16:
        form = X86_FORM_MOVZX_R32_RM16;
        break;
    case BUSTER_TYPED_CLASS_I32:
    case BUSTER_TYPED_CLASS_U32:
        form = X86_FORM_MOV_R32_RM32;
        break;
    default:
        break;
    }
    return form;
}

// The integer operations that take the arithmetic group's forms (and their choice rows' operations).
typedef struct Tier0Arithmetic Tier0Arithmetic;
struct Tier0Arithmetic
{
    uint8_t kind;
    uint8_t operation;
    uint16_t r_rm[2];
    uint16_t rm_r[2];
    uint16_t rm_imm[2];
};

static const Tier0Arithmetic tier0_arithmetic[5] = {
    {
        .kind = BUSTER_TYPED_ADD,
        .operation = X86_OPERATION_ADD,
        .r_rm = { X86_FORM_ADD_R32_RM32, X86_FORM_ADD_R64_RM64 },
        .rm_r = { X86_FORM_ADD_RM32_R32, X86_FORM_ADD_RM64_R64 },
        .rm_imm = { X86_FORM_ADD_RM32_IMM32, X86_FORM_ADD_RM64_IMM32 },
    },
    {
        .kind = BUSTER_TYPED_SUB,
        .operation = X86_OPERATION_SUB,
        .r_rm = { X86_FORM_SUB_R32_RM32, X86_FORM_SUB_R64_RM64 },
        .rm_r = { X86_FORM_SUB_RM32_R32, X86_FORM_SUB_RM64_R64 },
        .rm_imm = { X86_FORM_SUB_RM32_IMM32, X86_FORM_SUB_RM64_IMM32 },
    },
    {
        .kind = BUSTER_TYPED_AND,
        .operation = X86_OPERATION_AND,
        .r_rm = { X86_FORM_AND_R32_RM32, X86_FORM_AND_R64_RM64 },
        .rm_r = { X86_FORM_AND_RM32_R32, X86_FORM_AND_RM64_R64 },
        .rm_imm = { X86_FORM_AND_RM32_IMM32, X86_FORM_AND_RM64_IMM32 },
    },
    {
        .kind = BUSTER_TYPED_OR,
        .operation = X86_OPERATION_OR,
        .r_rm = { X86_FORM_OR_R32_RM32, X86_FORM_OR_R64_RM64 },
        .rm_r = { X86_FORM_OR_RM32_R32, X86_FORM_OR_RM64_R64 },
        .rm_imm = { X86_FORM_OR_RM32_IMM32, X86_FORM_OR_RM64_IMM32 },
    },
    {
        .kind = BUSTER_TYPED_XOR,
        .operation = X86_OPERATION_XOR,
        .r_rm = { X86_FORM_XOR_R32_RM32, X86_FORM_XOR_R64_RM64 },
        .rm_r = { X86_FORM_XOR_RM32_R32, X86_FORM_XOR_RM64_R64 },
        .rm_imm = { X86_FORM_XOR_RM32_IMM32, X86_FORM_XOR_RM64_IMM32 },
    },
};

// The classes arithmetic is done in (C promotes narrower ones), and every integer class.
static const uint8_t tier0_arithmetic_classes[4] = {
    BUSTER_TYPED_CLASS_I32,
    BUSTER_TYPED_CLASS_U32,
    BUSTER_TYPED_CLASS_I64,
    BUSTER_TYPED_CLASS_U64,
};

// A compare's condition code by kind (EQ to GE) and signedness.
static uint32_t tier0_condition(uint32_t kind, bool is_signed)
{
    static const uint8_t signed_codes[6] = { BUSTER_TIER0_CC_E, BUSTER_TIER0_CC_NE, BUSTER_TIER0_CC_L, BUSTER_TIER0_CC_LE, BUSTER_TIER0_CC_G, BUSTER_TIER0_CC_GE };
    static const uint8_t unsigned_codes[6] = { BUSTER_TIER0_CC_E, BUSTER_TIER0_CC_NE, BUSTER_TIER0_CC_B, BUSTER_TIER0_CC_BE, BUSTER_TIER0_CC_A, BUSTER_TIER0_CC_AE };
    const uint32_t index = kind - BUSTER_TYPED_EQ;
    return is_signed ? signed_codes[index] : unsigned_codes[index];
}

// The template's K and A: K is the value of entry k_offset back (times entry k_scale back's), A the home of entry
// a_offset back's local.
static void tier0_holes_from(uint32_t k_offset, uint32_t k_scale, uint32_t a_offset)
{
    tier0_builder.current.k_offset = (uint8_t)k_offset;
    tier0_builder.current.k_scale = (uint8_t)k_scale;
    tier0_builder.current.a_offset = (uint8_t)a_offset;
}

// A compare's record (cmp) for one of its variants, with the flags its condition reads live after it; `shift` is how
// far after the compare the template's entry is (1 for a branch that absorbs it).
static Tier0RecordSpec tier0_compare_record(uint32_t class, uint32_t variant, uint8_t live)
{
    const uint32_t cmp_r_rm = tier0_wide(class, X86_FORM_CMP_R32_RM32, X86_FORM_CMP_R64_RM64);
    const uint32_t cmp_rm_imm = tier0_wide(class, X86_FORM_CMP_RM32_IMM32, X86_FORM_CMP_RM64_IMM32);
    Tier0RecordSpec spec = tier0_rr(cmp_r_rm, BUSTER_TIER0_SOURCE_R2, BUSTER_TIER0_SOURCE_R1);
    if (variant == BUSTER_TIER0_VARIANT_CONST)
    {
        spec = tier0_with_k(tier0_r(cmp_rm_imm, BUSTER_TIER0_SOURCE_R1), live);
    }
    else if (variant == BUSTER_TIER0_VARIANT_HOME)
    {
        spec = tier0_rh(cmp_r_rm, BUSTER_TIER0_SOURCE_R1, BUSTER_TIER0_RM_HOME_A);
    }
    else if (variant == BUSTER_TIER0_VARIANT_HOME_CONST)
    {
        spec = tier0_with_k(tier0_rh(cmp_rm_imm, BUSTER_TIER0_SOURCE_NONE, BUSTER_TIER0_RM_HOME_A), live);
    }
    spec.live = live;
    return spec;
}

// K and A of a compare's variant, `shift` entries further back.
static void tier0_compare_holes(uint32_t variant, uint32_t shift)
{
    if (variant == BUSTER_TIER0_VARIANT_CONST)
    {
        tier0_holes_from(1 + shift, BUSTER_TIER0_NO_OFFSET, BUSTER_TIER0_NO_OFFSET);
    }
    else if (variant == BUSTER_TIER0_VARIANT_HOME)
    {
        tier0_holes_from(BUSTER_TIER0_NO_OFFSET, BUSTER_TIER0_NO_OFFSET, 1 + shift);
    }
    else if (variant == BUSTER_TIER0_VARIANT_HOME_CONST)
    {
        tier0_holes_from(1 + shift, BUSTER_TIER0_NO_OFFSET, 2 + shift);
    }
}

// The four templates of a home-dependent slot, in tier0_end's order. `make` adds the records for one case.
#define BUSTER_TIER0_HOME_CASES(kind, class, variant, k_offset, a_offset, make)                                        \
    for (uint32_t home_case = 0; home_case < 4; home_case += 1)                                                       \
    {                                                                                                                  \
        const bool a_register = (home_case & 1) != 0;                                                                  \
        const bool b_register = (home_case & 2) != 0;                                                                  \
        (void)a_register;                                                                                              \
        (void)b_register;                                                                                              \
        tier0_begin();                                                                                                 \
        tier0_holes_from(k_offset, BUSTER_TIER0_NO_OFFSET, a_offset);                                                  \
        make;                                                                                                          \
        tier0_end(kind, class, variant, home_case + 1);                                                                \
    }

// A value's move between a home and a register of its stack, for the copies: integer loads and stores by the class's
// width, floating ones as movss or movsd from or to memory and movaps between registers.
static uint32_t tier0_store_form(uint32_t class)
{
    uint32_t form = tier0_sized(class, X86_FORM_MOV_RM8_R8, X86_FORM_MOV_RM16_R16, X86_FORM_MOV_RM32_R32, X86_FORM_MOV_RM64_R64);
    if (class == BUSTER_TYPED_CLASS_F32)
    {
        form = X86_FORM_MOVSS_XM32_X;
    }
    else if (class == BUSTER_TYPED_CLASS_F64)
    {
        form = X86_FORM_MOVSD_XM64_X;
    }
    return form;
}

static uint32_t tier0_value_load_form(uint32_t class)
{
    uint32_t form = tier0_load_form(class);
    if (class == BUSTER_TYPED_CLASS_F32)
    {
        form = X86_FORM_MOVSS_X_XM32;
    }
    else if (class == BUSTER_TYPED_CLASS_F64)
    {
        form = X86_FORM_MOVSD_X_XM64;
    }
    return form;
}

// The SSE forms of a floating operation by class.
static uint32_t tier0_sse(uint32_t class, uint32_t single, uint32_t double_)
{
    return class == BUSTER_TYPED_CLASS_F32 ? single : double_;
}

// home <- value register (`value` a source of the class's stack): a store into memory, or a register move.
static Tier0RecordSpec tier0_home_store(uint32_t class, uint8_t home, bool home_register, uint8_t value)
{
    Tier0RecordSpec spec = tier0_rh(tier0_store_form(class), value, home);
    if (tier0_class_floating(class) && home_register)
    {
        spec = tier0_rh(X86_FORM_MOVAPS_X_XM, (uint8_t)(home == BUSTER_TIER0_RM_HOME_A ? BUSTER_TIER0_SOURCE_A : BUSTER_TIER0_SOURCE_B),
                        BUSTER_TIER0_RM_NONE);
        spec.rm_kind = BUSTER_TIER0_RM_REGISTER;
        spec.rm = value;
    }
    return spec;
}

// value register <- home: a load from memory or a register move (movaps between XMM registers).
static Tier0RecordSpec tier0_home_load(uint32_t class, uint8_t value, uint8_t home, bool home_register)
{
    Tier0RecordSpec spec = tier0_rh(tier0_value_load_form(class), value, home);
    if (tier0_class_floating(class) && home_register)
    {
        spec = tier0_rh(X86_FORM_MOVAPS_X_XM, value, home);
    }
    return spec;
}

// The conversion of a value from one class to another in place (`from` its source register, `to` its destination's),
// as records; a conversion that keeps the low bits needs none. Returns false for one the first slice leaves out.
static bool tier0_convert_records(uint32_t to, uint32_t from, uint8_t to_register, uint8_t from_register, uint8_t from_kind)
{
    bool supported = true;
    const bool to_floating = tier0_class_floating(to);
    const bool from_floating = tier0_class_floating(from);
    const uint32_t to_bytes = tier0_class_bytes(to);
    const uint32_t from_bytes = tier0_class_bytes(from);
    const bool from_home = from_kind == BUSTER_TIER0_RM_HOME_A;
    // The source as an rm operand: the register, or the home.
    Tier0RecordSpec source = tier0_rr(0, to_register, from_register);
    if (from_home)
    {
        source = tier0_rh(0, to_register, BUSTER_TIER0_RM_HOME_A);
    }
    if (!to_floating && !from_floating)
    {
        uint32_t form = X86_FORM_NONE;
        if (to_bytes > from_bytes)
        {
            if (from_bytes == 4)
            {
                form = tier0_class_signed(from) ? X86_FORM_MOVSXD_R64_RM32 : X86_FORM_MOV_R32_RM32;
            }
            else if (tier0_class_signed(from))
            {
                form = from_bytes == 1 ? (to_bytes == 8 ? X86_FORM_MOVSX_R64_RM8 : X86_FORM_MOVSX_R32_RM8)
                                       : (to_bytes == 8 ? X86_FORM_MOVSX_R64_RM16 : X86_FORM_MOVSX_R32_RM16);
            }
            else
            {
                form = from_bytes == 1 ? X86_FORM_MOVZX_R32_RM8 : X86_FORM_MOVZX_R32_RM16;
            }
        }
        else if (from_home)
        {
            // A narrowing (or same-width) conversion of a home: a load of the narrower width.
            form = to_bytes == 8 ? X86_FORM_MOV_R64_RM64 : to_bytes == 4 ? X86_FORM_MOV_R32_RM32 : to_bytes == 2 ? X86_FORM_MOVZX_R32_RM16 : X86_FORM_MOVZX_R32_RM8;
        }
        if (form != X86_FORM_NONE)
        {
            source.form = (uint16_t)form;
            tier0_add(source);
        }
    }
    else if (to_floating && !from_floating)
    {
        // Narrow integers and U32 widen first, into the source register (or the scratch, from a home), then convert as
        // I32 or I64.
        uint8_t widened = from_register;
        uint32_t as = from;
        if (from_bytes < 4 || from == BUSTER_TYPED_CLASS_U32)
        {
            Tier0RecordSpec widen = source;
            widen.reg = from_home ? BUSTER_TIER0_SOURCE_R0 : from_register;
            widen.form = (uint16_t)(from == BUSTER_TYPED_CLASS_U32 ? X86_FORM_MOV_R32_RM32 : tier0_load_form(from));
            tier0_add(widen);
            widened = widen.reg;
            as = from == BUSTER_TYPED_CLASS_U32 ? BUSTER_TYPED_CLASS_I64 : BUSTER_TYPED_CLASS_I32;
            source = tier0_rr(0, to_register, widened);
        }
        supported = from != BUSTER_TYPED_CLASS_U64;
        const bool wide = tier0_class_bytes(as) == 8;
        const uint32_t form = to == BUSTER_TYPED_CLASS_F32 ? (wide ? X86_FORM_CVTSI2SS_X_RM64 : X86_FORM_CVTSI2SS_X_RM32)
                                                           : (wide ? X86_FORM_CVTSI2SD_X_RM64 : X86_FORM_CVTSI2SD_X_RM32);
        source.form = (uint16_t)form;
        tier0_add(source);
    }
    else if (!to_floating && from_floating)
    {
        supported = to != BUSTER_TYPED_CLASS_U64;
        const bool wide = to_bytes == 8 || to == BUSTER_TYPED_CLASS_U32;
        const uint32_t form = from == BUSTER_TYPED_CLASS_F32 ? (wide ? X86_FORM_CVTTSS2SI_R64_XM32 : X86_FORM_CVTTSS2SI_R32_XM32)
                                                             : (wide ? X86_FORM_CVTTSD2SI_R64_XM64 : X86_FORM_CVTTSD2SI_R32_XM64);
        source.form = (uint16_t)form;
        tier0_add(source);
    }
    else if (to != from)
    {
        source.form = (uint16_t)(from == BUSTER_TYPED_CLASS_F32 ? X86_FORM_CVTSS2SD_X_XM32 : X86_FORM_CVTSD2SS_X_XM64);
        tier0_add(source);
    }
    else if (from_home)
    {
        source.form = (uint16_t)tier0_value_load_form(from);
        tier0_add(source);
    }
    return supported;
}

// Every template, by kind.
static void tier0_define_templates(void)
{
    const uint8_t no = BUSTER_TIER0_NO_OFFSET;
    // Leaves and their copies, by class.
    for (uint32_t class = BUSTER_TYPED_CLASS_I8; class < BUSTER_TYPED_CLASSES; class += 1)
    {
        const bool floating = tier0_class_floating(class);
        const uint8_t top = floating ? BUSTER_TIER0_SOURCE_X0 : BUSTER_TIER0_SOURCE_R0;
        const uint8_t last = floating ? BUSTER_TIER0_SOURCE_X1 : BUSTER_TIER0_SOURCE_R1;
        // CONST: mov r, imm (the choice pass shortens it); a floating one through a general register, or xorps for +0.
        tier0_begin();
        tier0_holes_from(0, no, no);
        if (!floating)
        {
            const uint32_t form = tier0_wide(class, X86_FORM_MOV_RM32_IMM32, X86_FORM_MOV_RM64_IMM32);
            tier0_add(tier0_with_k(tier0_r(form, BUSTER_TIER0_SOURCE_R0), 0));
        }
        else
        {
            const uint32_t mov = class == BUSTER_TYPED_CLASS_F32 ? X86_FORM_MOV_RM32_IMM32 : X86_FORM_MOV_RM64_IMM32;
            tier0_add(tier0_with_k(tier0_r(mov, BUSTER_TIER0_SOURCE_R0), 0));
            tier0_add(tier0_rr(class == BUSTER_TYPED_CLASS_F32 ? X86_FORM_MOVD_X_RM32 : X86_FORM_MOVQ_X_RM64, BUSTER_TIER0_SOURCE_X0, BUSTER_TIER0_SOURCE_R0));
        }
        tier0_end(BUSTER_TYPED_CONST, class, BUSTER_TIER0_VARIANT_PLAIN, 0);
        if (floating)
        {
            tier0_begin();
            tier0_add(tier0_rr(X86_FORM_XORPS_X_XM128, BUSTER_TIER0_SOURCE_X0, BUSTER_TIER0_SOURCE_X0));
            tier0_end(BUSTER_TYPED_CONST, class, BUSTER_TIER0_VARIANT_CONST, 0);
        }
        // LOCAL: a load of its home (movaps from an XMM home).
        BUSTER_TIER0_HOME_CASES(BUSTER_TYPED_LOCAL, class, BUSTER_TIER0_VARIANT_PLAIN, no, no,
                                tier0_add(tier0_home_load(class, top, BUSTER_TIER0_RM_HOME_B, b_register)));
        // GLOBAL: a RIP-relative load.
        tier0_begin();
        tier0_add(tier0_rrip(tier0_value_load_form(class), top));
        tier0_end(BUSTER_TYPED_GLOBAL, class, BUSTER_TIER0_VARIANT_PLAIN, 0);
        // DUP: a copy of the top.
        tier0_begin();
        tier0_add(floating ? tier0_rr(X86_FORM_MOVAPS_X_XM, top, last) : tier0_rr(X86_FORM_MOV_R64_RM64, top, last));
        tier0_end(BUSTER_TYPED_DUP, class, BUSTER_TIER0_VARIANT_PLAIN, 0);
        // LOAD: [R1], [R2 + R1 * 2^s] or [R1 + K]; an integer replaces its pointer, a floating value goes to its stack.
        const uint8_t load_to = floating ? BUSTER_TIER0_SOURCE_X0 : BUSTER_TIER0_SOURCE_R1;
        const uint32_t load = tier0_value_load_form(class);
        tier0_begin();
        tier0_add(tier0_rmem(load, load_to, BUSTER_TIER0_RM_BASE, BUSTER_TIER0_SOURCE_R1, 0, 0));
        tier0_end(BUSTER_TYPED_LOAD, class, BUSTER_TIER0_VARIANT_PLAIN, 0);
        for (uint32_t s = 0; s < 4; s += 1)
        {
            tier0_begin();
            tier0_add(tier0_rmem(load, floating ? BUSTER_TIER0_SOURCE_X0 : BUSTER_TIER0_SOURCE_R2, BUSTER_TIER0_RM_BASE_INDEX, BUSTER_TIER0_SOURCE_R2,
                                 BUSTER_TIER0_SOURCE_R1, s));
            tier0_end(BUSTER_TYPED_LOAD, class, BUSTER_TIER0_VARIANT_INDEX(s), 0);
        }
        tier0_begin();
        tier0_holes_from(2, 1, no);
        tier0_add(tier0_rmem(load, load_to, BUSTER_TIER0_RM_BASE_K, BUSTER_TIER0_SOURCE_R1, 0, 0));
        tier0_end(BUSTER_TYPED_LOAD, class, BUSTER_TIER0_VARIANT_OFFSET, 0);
        // STORE: [R2] = R1, the value moved down to the result's place; a floating value stays where it is.
        tier0_begin();
        if (floating)
        {
            tier0_add(tier0_rmem(tier0_store_form(class), BUSTER_TIER0_SOURCE_X1, BUSTER_TIER0_RM_BASE, BUSTER_TIER0_SOURCE_R1, 0, 0));
        }
        else
        {
            tier0_add(tier0_rmem(tier0_store_form(class), BUSTER_TIER0_SOURCE_R1, BUSTER_TIER0_RM_BASE, BUSTER_TIER0_SOURCE_R2, 0, 0));
            tier0_add(tier0_rr(X86_FORM_MOV_R64_RM64, BUSTER_TIER0_SOURCE_R2, BUSTER_TIER0_SOURCE_R1));
        }
        tier0_end(BUSTER_TYPED_STORE, class, BUSTER_TIER0_VARIANT_PLAIN, 0);
        // SET: [address] = value, from a register, a constant or a home; the address [R2] (or [R1] for a floating value),
        // [R2 + R1 * 2^s] or [R1 + K].
        tier0_begin();
        tier0_add(floating ? tier0_rmem(tier0_store_form(class), BUSTER_TIER0_SOURCE_X1, BUSTER_TIER0_RM_BASE, BUSTER_TIER0_SOURCE_R1, 0, 0)
                           : tier0_rmem(tier0_store_form(class), BUSTER_TIER0_SOURCE_R1, BUSTER_TIER0_RM_BASE, BUSTER_TIER0_SOURCE_R2, 0, 0));
        tier0_end(BUSTER_TYPED_SET, class, BUSTER_TIER0_VARIANT_PLAIN, 0);
        const uint32_t store_imm = tier0_sized(class, X86_FORM_MOV_RM8_IMM8, X86_FORM_MOV_RM16_IMM16, X86_FORM_MOV_RM32_IMM32, X86_FORM_MOV_RM64_IMM32);
        if (!floating)
        {
            tier0_begin();
            tier0_holes_from(1, no, no);
            tier0_add(tier0_with_k(tier0_rmem(store_imm, BUSTER_TIER0_SOURCE_NONE, BUSTER_TIER0_RM_BASE, BUSTER_TIER0_SOURCE_R1, 0, 0), 0));
            tier0_end(BUSTER_TYPED_SET, class, BUSTER_TIER0_VARIANT_CONST, 0);
            for (uint32_t s = 0; s < 4; s += 1)
            {
                tier0_begin();
                tier0_holes_from(1, no, no);
                tier0_add(tier0_with_k(tier0_rmem(store_imm, BUSTER_TIER0_SOURCE_NONE, BUSTER_TIER0_RM_BASE_INDEX, BUSTER_TIER0_SOURCE_R2, BUSTER_TIER0_SOURCE_R1, s), 0));
                tier0_end(BUSTER_TYPED_SET, class, BUSTER_TIER0_VARIANT_INDEX_CONST(s), 0);
            }
        }
        // A home's value into memory: straight from an XMM or general register home, or through the scratch.
        const uint8_t scratch = floating ? BUSTER_TIER0_SOURCE_X0 : BUSTER_TIER0_SOURCE_R0;
        for (uint32_t address = 0; address < 6; address += 1)
        {
            // 0: [R1]; 1 to 4: [R2 + R1 * 2^(address - 1)]; 5: [R1 + K].
            const uint32_t variant = address == 0 ? BUSTER_TIER0_VARIANT_HOME : address == 5 ? BUSTER_TIER0_VARIANT_OFFSET_HOME : BUSTER_TIER0_VARIANT_INDEX_HOME(address - 1);
            const uint8_t rm_kind = address == 0 ? BUSTER_TIER0_RM_BASE : address == 5 ? BUSTER_TIER0_RM_BASE_K : BUSTER_TIER0_RM_BASE_INDEX;
            const uint8_t base = address == 0 || address == 5 ? BUSTER_TIER0_SOURCE_R1 : BUSTER_TIER0_SOURCE_R2;
            const uint32_t k_offset = address == 5 ? 3 : no;
            BUSTER_TIER0_HOME_CASES(BUSTER_TYPED_SET, class, variant, k_offset, 1, {
                (void)b_register;
                if (address == 5)
                {
                    tier0_builder.current.k_scale = 2;
                }
                if (!a_register)
                {
                    tier0_add(tier0_home_load(class, scratch, BUSTER_TIER0_RM_HOME_A, false));
                }
                tier0_add(tier0_rmem(tier0_store_form(class), a_register ? BUSTER_TIER0_SOURCE_A : scratch, rm_kind, base, BUSTER_TIER0_SOURCE_R1,
                                     address >= 1 && address <= 4 ? address - 1 : 0));
            });
        }
        // STORE_LOCAL and SET_LOCAL: the value into the local's home, from a register, a constant or another home.
        BUSTER_TIER0_HOME_CASES(BUSTER_TYPED_STORE_LOCAL, class, BUSTER_TIER0_VARIANT_PLAIN, no, no, {
            (void)a_register;
            tier0_add(tier0_home_store(class, BUSTER_TIER0_RM_HOME_B, b_register, last));
        });
        BUSTER_TIER0_HOME_CASES(BUSTER_TYPED_SET_LOCAL, class, BUSTER_TIER0_VARIANT_PLAIN, no, no, {
            (void)a_register;
            tier0_add(tier0_home_store(class, BUSTER_TIER0_RM_HOME_B, b_register, last));
        });
        if (!floating)
        {
            tier0_begin();
            tier0_holes_from(1, no, no);
            tier0_add(tier0_with_k(tier0_r(tier0_wide(class, X86_FORM_MOV_RM32_IMM32, X86_FORM_MOV_RM64_IMM32), BUSTER_TIER0_SOURCE_R0), 0));
            tier0_add(tier0_rh(tier0_store_form(class), BUSTER_TIER0_SOURCE_R0, BUSTER_TIER0_RM_HOME_B));
            tier0_end(BUSTER_TYPED_STORE_LOCAL, class, BUSTER_TIER0_VARIANT_CONST, 0);
            tier0_begin();
            tier0_holes_from(1, no, no);
            tier0_add(tier0_with_k(tier0_rh(store_imm, BUSTER_TIER0_SOURCE_NONE, BUSTER_TIER0_RM_HOME_B), 0));
            tier0_end(BUSTER_TYPED_SET_LOCAL, class, BUSTER_TIER0_VARIANT_CONST, 0);
        }
        BUSTER_TIER0_HOME_CASES(BUSTER_TYPED_STORE_LOCAL, class, BUSTER_TIER0_VARIANT_HOME, no, 1, {
            tier0_add(tier0_home_load(class, top, BUSTER_TIER0_RM_HOME_A, a_register));
            tier0_add(tier0_home_store(class, BUSTER_TIER0_RM_HOME_B, b_register, top));
        });
        BUSTER_TIER0_HOME_CASES(BUSTER_TYPED_SET_LOCAL, class, BUSTER_TIER0_VARIANT_HOME, no, 1, {
            if (b_register)
            {
                Tier0RecordSpec copy = tier0_home_load(class, BUSTER_TIER0_SOURCE_B, BUSTER_TIER0_RM_HOME_A, a_register);
                tier0_add(copy);
            }
            else if (a_register)
            {
                tier0_add(tier0_home_store(class, BUSTER_TIER0_RM_HOME_B, false, BUSTER_TIER0_SOURCE_A));
            }
            else
            {
                tier0_add(tier0_home_load(class, top, BUSTER_TIER0_RM_HOME_A, false));
                tier0_add(tier0_home_store(class, BUSTER_TIER0_RM_HOME_B, false, top));
            }
        });
        // STORE_GLOBAL and SET_GLOBAL: the same into [rip + symbol].
        tier0_begin();
        tier0_add(tier0_rrip(tier0_store_form(class), last));
        tier0_end(BUSTER_TYPED_STORE_GLOBAL, class, BUSTER_TIER0_VARIANT_PLAIN, 0);
        tier0_begin();
        tier0_add(tier0_rrip(tier0_store_form(class), last));
        tier0_end(BUSTER_TYPED_SET_GLOBAL, class, BUSTER_TIER0_VARIANT_PLAIN, 0);
        if (!floating)
        {
            tier0_begin();
            tier0_holes_from(1, no, no);
            tier0_add(tier0_with_k(tier0_r(tier0_wide(class, X86_FORM_MOV_RM32_IMM32, X86_FORM_MOV_RM64_IMM32), BUSTER_TIER0_SOURCE_R0), 0));
            tier0_add(tier0_rrip(tier0_store_form(class), BUSTER_TIER0_SOURCE_R0));
            tier0_end(BUSTER_TYPED_STORE_GLOBAL, class, BUSTER_TIER0_VARIANT_CONST, 0);
            tier0_begin();
            tier0_holes_from(1, no, no);
            tier0_add(tier0_with_k(tier0_rrip(store_imm, BUSTER_TIER0_SOURCE_NONE), 0));
            tier0_end(BUSTER_TYPED_SET_GLOBAL, class, BUSTER_TIER0_VARIANT_CONST, 0);
        }
        BUSTER_TIER0_HOME_CASES(BUSTER_TYPED_SET_GLOBAL, class, BUSTER_TIER0_VARIANT_HOME, no, 1, {
            (void)b_register;
            if (!a_register)
            {
                tier0_add(tier0_home_load(class, top, BUSTER_TIER0_RM_HOME_A, false));
            }
            tier0_add(tier0_rrip(tier0_store_form(class), a_register ? BUSTER_TIER0_SOURCE_A : top));
        });
        BUSTER_TIER0_HOME_CASES(BUSTER_TYPED_STORE_GLOBAL, class, BUSTER_TIER0_VARIANT_HOME, no, 1, {
            (void)b_register;
            tier0_add(tier0_home_load(class, top, BUSTER_TIER0_RM_HOME_A, a_register));
            tier0_add(tier0_rrip(tier0_store_form(class), top));
        });
        // CALL: tier0_emit_deferred's.
        tier0_defer(BUSTER_TYPED_CALL, class, BUSTER_TIER0_VARIANT_PLAIN);
        // DISCARD: nothing, whatever it discards.
        for (uint32_t variant = BUSTER_TIER0_VARIANT_PLAIN; variant <= BUSTER_TIER0_VARIANT_HOME; variant += 1)
        {
            tier0_begin();
            tier0_end(BUSTER_TYPED_DISCARD, class, variant, 0);
        }
        // JUMP_VALUE: the value stays where the target's arm leaves its own.
        tier0_begin();
        tier0_add(tier0_branch(X86_FORM_JMP_REL8, BUSTER_TIER0_RECORD_BRANCH));
        tier0_end(BUSTER_TYPED_JUMP_VALUE, class, BUSTER_TIER0_VARIANT_PLAIN, 0);
        // RETURN: the value is at depth 0 (rax or xmm0); a narrow integer is extended to 32 bits, as the convention
        // has the callee do. Then the jump to the epilogue, but from the last entry.
        for (uint32_t last_entry = 0; last_entry < 2; last_entry += 1)
        {
            tier0_begin();
            if (!floating && tier0_class_bytes(class) < 4)
            {
                tier0_add(tier0_rr(tier0_load_form(class), BUSTER_TIER0_SOURCE_R1, BUSTER_TIER0_SOURCE_R1));
            }
            if (last_entry == 0)
            {
                tier0_add(tier0_branch(X86_FORM_JMP_REL8, BUSTER_TIER0_RECORD_END));
            }
            tier0_end(BUSTER_TYPED_RETURN, class, last_entry != 0 ? BUSTER_TIER0_VARIANT_LAST : BUSTER_TIER0_VARIANT_PLAIN, 0);
        }
        // Conversions to this class from every class.
        for (uint32_t from = BUSTER_TYPED_CLASS_I8; from < BUSTER_TYPED_CLASSES; from += 1)
        {
            for (uint32_t home = 0; home < 2; home += 1)
            {
                const bool to_floating = floating;
                const bool from_floating = tier0_class_floating(from);
                // In place on one stack; across stacks, from the operand's register (or a home) to the free one.
                uint8_t to_register = top;
                uint8_t from_register = from_floating ? BUSTER_TIER0_SOURCE_X1 : BUSTER_TIER0_SOURCE_R1;
                if (to_floating == from_floating && home == 0)
                {
                    to_register = from_register;
                }
                tier0_begin();
                tier0_holes_from(no, no, home != 0 ? 1 : no);
                const bool supported = tier0_convert_records(class, from, to_register, from_register, home != 0 ? BUSTER_TIER0_RM_HOME_A : BUSTER_TIER0_RM_REGISTER);
                if (supported)
                {
                    tier0_end(BUSTER_TYPED_CONVERT, class, from | (home != 0 ? BUSTER_TIER0_VARIANT_CONVERT_HOME : 0), 0);
                }
            }
        }
    }
    // The addresses: lea from the frame or RIP-relative.
    tier0_begin();
    tier0_add(tier0_rh(X86_FORM_LEA_R64_M, BUSTER_TIER0_SOURCE_R0, BUSTER_TIER0_RM_HOME_B));
    tier0_end(BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, BUSTER_TIER0_VARIANT_PLAIN, 0);
    tier0_begin();
    tier0_add(tier0_rrip(X86_FORM_LEA_R64_M, BUSTER_TIER0_SOURCE_R0));
    tier0_end(BUSTER_TYPED_GLOBAL_ADDRESS, BUSTER_TYPED_CLASS_U64, BUSTER_TIER0_VARIANT_PLAIN, 0);
    // JUMP.
    tier0_begin();
    tier0_add(tier0_branch(X86_FORM_JMP_REL8, BUSTER_TIER0_RECORD_BRANCH));
    tier0_end(BUSTER_TYPED_JUMP, BUSTER_TYPED_CLASS_VOID, BUSTER_TIER0_VARIANT_PLAIN, 0);
    // RETURN of nothing.
    tier0_begin();
    tier0_add(tier0_branch(X86_FORM_JMP_REL8, BUSTER_TIER0_RECORD_END));
    tier0_end(BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, BUSTER_TIER0_VARIANT_PLAIN, 0);
    tier0_begin();
    tier0_end(BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, BUSTER_TIER0_VARIANT_LAST, 0);
    tier0_defer(BUSTER_TYPED_CALL, BUSTER_TYPED_CLASS_VOID, BUSTER_TIER0_VARIANT_PLAIN);
    // Integer arithmetic, by the class it is done in.
    for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(tier0_arithmetic_classes); c += 1)
    {
        const uint32_t class = tier0_arithmetic_classes[c];
        const uint32_t w = tier0_class_bytes(class) == 8 ? 1 : 0;
        const bool is_signed = tier0_class_signed(class);
        for (uint32_t a = 0; a < BUSTER_ARRAY_LENGTH(tier0_arithmetic); a += 1)
        {
            const Tier0Arithmetic* op = &tier0_arithmetic[a];
            tier0_begin();
            tier0_add(tier0_rr(op->r_rm[w], BUSTER_TIER0_SOURCE_R2, BUSTER_TIER0_SOURCE_R1));
            tier0_end(op->kind, class, BUSTER_TIER0_VARIANT_PLAIN, 0);
            tier0_begin();
            tier0_holes_from(1, no, no);
            tier0_add(tier0_with_k(tier0_r(op->rm_imm[w], BUSTER_TIER0_SOURCE_R1), 0));
            tier0_end(op->kind, class, BUSTER_TIER0_VARIANT_CONST, 0);
            tier0_begin();
            tier0_holes_from(no, no, 1);
            tier0_add(tier0_rh(op->r_rm[w], BUSTER_TIER0_SOURCE_R1, BUSTER_TIER0_RM_HOME_A));
            tier0_end(op->kind, class, BUSTER_TIER0_VARIANT_HOME, 0);
            // x = x op v, into the home: op x, K; or op x, v from a register home (straight), or through the scratch.
            tier0_begin();
            tier0_holes_from(2, no, no);
            tier0_add(tier0_with_k(tier0_rh(op->rm_imm[w], BUSTER_TIER0_SOURCE_NONE, BUSTER_TIER0_RM_HOME_B), 0));
            tier0_end(op->kind, class, BUSTER_TIER0_VARIANT_RMW_CONST, 0);
            BUSTER_TIER0_HOME_CASES(op->kind, class, BUSTER_TIER0_VARIANT_RMW_HOME, no, 2, {
                if (b_register)
                {
                    tier0_add(tier0_rh(op->r_rm[w], BUSTER_TIER0_SOURCE_B, BUSTER_TIER0_RM_HOME_A));
                }
                else if (a_register)
                {
                    tier0_add(tier0_rh(op->rm_r[w], BUSTER_TIER0_SOURCE_A, BUSTER_TIER0_RM_HOME_B));
                }
                else
                {
                    tier0_add(tier0_rh(tier0_wide(class, X86_FORM_MOV_R32_RM32, X86_FORM_MOV_R64_RM64), BUSTER_TIER0_SOURCE_R0, BUSTER_TIER0_RM_HOME_A));
                    tier0_add(tier0_rh(op->rm_r[w], BUSTER_TIER0_SOURCE_R0, BUSTER_TIER0_RM_HOME_B));
                }
            });
        }
        // MUL: imul r, rm; imul r, rm, imm (from a register or a home).
        const uint32_t imul = tier0_wide(class, X86_FORM_IMUL_R32_RM32, X86_FORM_IMUL_R64_RM64);
        const uint32_t imul_imm = tier0_wide(class, X86_FORM_IMUL_R32_RM32_IMM32, X86_FORM_IMUL_R64_RM64_IMM32);
        tier0_begin();
        tier0_add(tier0_rr(imul, BUSTER_TIER0_SOURCE_R2, BUSTER_TIER0_SOURCE_R1));
        tier0_end(BUSTER_TYPED_MUL, class, BUSTER_TIER0_VARIANT_PLAIN, 0);
        tier0_begin();
        tier0_holes_from(1, no, no);
        tier0_add(tier0_with_k(tier0_rr(imul_imm, BUSTER_TIER0_SOURCE_R1, BUSTER_TIER0_SOURCE_R1), 0));
        tier0_end(BUSTER_TYPED_MUL, class, BUSTER_TIER0_VARIANT_CONST, 0);
        tier0_begin();
        tier0_holes_from(no, no, 1);
        tier0_add(tier0_rh(imul, BUSTER_TIER0_SOURCE_R1, BUSTER_TIER0_RM_HOME_A));
        tier0_end(BUSTER_TYPED_MUL, class, BUSTER_TIER0_VARIANT_HOME, 0);
        tier0_begin();
        tier0_holes_from(1, no, 2);
        tier0_add(tier0_with_k(tier0_rh(imul_imm, BUSTER_TIER0_SOURCE_R0, BUSTER_TIER0_RM_HOME_A), 0));
        tier0_end(BUSTER_TYPED_MUL, class, BUSTER_TIER0_VARIANT_HOME_CONST, 0);
        // DIV and MOD: the dividend in rax (depth 0), the divisor in rcx (depth 1) or a home; rdx extended from rax (or
        // zeroed). Elsewhere, tier0_emit_deferred's.
        for (uint32_t mod = 0; mod < 2; mod += 1)
        {
            for (uint32_t home = 0; home < 2; home += 1)
            {
                tier0_begin();
                tier0_holes_from(no, no, home != 0 ? 1 : no);
                tier0_builder.current.required_depth = home != 0 ? 1 : 2;
                tier0_builder.current.clobbers = 1u << X86_REGISTER_RDX;
                if (is_signed)
                {
                    tier0_add(tier0_spec(w != 0 ? X86_FORM_CQO : X86_FORM_CDQ));
                }
                else
                {
                    tier0_add(tier0_rr(X86_FORM_XOR_R32_RM32, BUSTER_TIER0_FIXED(X86_REGISTER_RDX), BUSTER_TIER0_FIXED(X86_REGISTER_RDX)));
                }
                const uint32_t divide = is_signed ? tier0_wide(class, X86_FORM_IDIV_RM32, X86_FORM_IDIV_RM64) : tier0_wide(class, X86_FORM_DIV_RM32, X86_FORM_DIV_RM64);
                tier0_add(home != 0 ? tier0_rh(divide, BUSTER_TIER0_SOURCE_NONE, BUSTER_TIER0_RM_HOME_A) : tier0_r(divide, BUSTER_TIER0_SOURCE_R1));
                if (mod != 0)
                {
                    tier0_add(tier0_rr(tier0_wide(class, X86_FORM_MOV_R32_RM32, X86_FORM_MOV_R64_RM64), home != 0 ? BUSTER_TIER0_SOURCE_R1 : BUSTER_TIER0_SOURCE_R2,
                                       BUSTER_TIER0_FIXED(X86_REGISTER_RDX)));
                }
                tier0_end(mod != 0 ? BUSTER_TYPED_MOD : BUSTER_TYPED_DIV, class, home != 0 ? BUSTER_TIER0_VARIANT_HOME : BUSTER_TIER0_VARIANT_PLAIN, 0);
            }
        }
        // Shifts: by a constant anywhere; by cl from depth 2 (the count at depth 1, rcx), elsewhere deferred.
        for (uint32_t right = 0; right < 2; right += 1)
        {
            const uint32_t kind = right != 0 ? BUSTER_TYPED_SHR : BUSTER_TYPED_SHL;
            const uint32_t by_imm = right == 0 ? tier0_wide(class, X86_FORM_SHL_RM32_IMM8, X86_FORM_SHL_RM64_IMM8)
                                    : is_signed ? tier0_wide(class, X86_FORM_SAR_RM32_IMM8, X86_FORM_SAR_RM64_IMM8)
                                                : tier0_wide(class, X86_FORM_SHR_RM32_IMM8, X86_FORM_SHR_RM64_IMM8);
            const uint32_t by_cl = right == 0 ? tier0_wide(class, X86_FORM_SHL_RM32_CL, X86_FORM_SHL_RM64_CL)
                                   : is_signed ? tier0_wide(class, X86_FORM_SAR_RM32_CL, X86_FORM_SAR_RM64_CL)
                                               : tier0_wide(class, X86_FORM_SHR_RM32_CL, X86_FORM_SHR_RM64_CL);
            tier0_begin();
            tier0_holes_from(1, no, no);
            tier0_add(tier0_with_k(tier0_r(by_imm, BUSTER_TIER0_SOURCE_R1), 0));
            tier0_end(kind, class, BUSTER_TIER0_VARIANT_CONST, 0);
            tier0_begin();
            tier0_builder.current.required_depth = 2;
            tier0_add(tier0_r(by_cl, BUSTER_TIER0_SOURCE_R2));
            tier0_end(kind, class, BUSTER_TIER0_VARIANT_PLAIN, 0);
            tier0_begin();
            tier0_holes_from(2, no, no);
            tier0_add(tier0_with_k(tier0_rh(by_imm, BUSTER_TIER0_SOURCE_NONE, BUSTER_TIER0_RM_HOME_B), 0));
            tier0_end(kind, class, BUSTER_TIER0_VARIANT_RMW_CONST, 0);
        }
        // NEG and NOT, in place.
        tier0_begin();
        tier0_add(tier0_r(tier0_wide(class, X86_FORM_NEG_RM32, X86_FORM_NEG_RM64), BUSTER_TIER0_SOURCE_R1));
        tier0_end(BUSTER_TYPED_NEG, class, BUSTER_TIER0_VARIANT_PLAIN, 0);
        tier0_begin();
        tier0_add(tier0_r(tier0_wide(class, X86_FORM_NOT_RM32, X86_FORM_NOT_RM64), BUSTER_TIER0_SOURCE_R1));
        tier0_end(BUSTER_TYPED_NOT, class, BUSTER_TIER0_VARIANT_PLAIN, 0);
        // Compares, as a value (cmp, setcc, movzx) and as a branch's condition (cmp, jcc).
        for (uint32_t kind = BUSTER_TYPED_EQ; kind <= BUSTER_TYPED_GE; kind += 1)
        {
            const uint32_t cc = tier0_condition(kind, is_signed);
            const uint8_t live = tier0_condition_flags[cc];
            for (uint32_t variant = BUSTER_TIER0_VARIANT_PLAIN; variant <= BUSTER_TIER0_VARIANT_HOME_CONST; variant += 1)
            {
                const uint8_t result = variant == BUSTER_TIER0_VARIANT_PLAIN ? BUSTER_TIER0_SOURCE_R2
                                       : variant == BUSTER_TIER0_VARIANT_HOME_CONST ? BUSTER_TIER0_SOURCE_R0
                                                                                     : BUSTER_TIER0_SOURCE_R1;
                tier0_begin();
                tier0_compare_holes(variant, 0);
                tier0_add(tier0_compare_record(class, variant, live));
                tier0_add(tier0_r(tier0_setcc[cc], result));
                tier0_add(tier0_rr(X86_FORM_MOVZX_R32_RM8, result, result));
                tier0_end(kind, class, variant, 0);
                for (uint32_t when = 0; when < 2; when += 1)
                {
                    tier0_begin();
                    tier0_compare_holes(variant, 1);
                    tier0_add(tier0_compare_record(class, variant, live));
                    tier0_add(tier0_branch(tier0_jcc_rel8[when != 0 ? cc : cc ^ 1], BUSTER_TIER0_RECORD_BRANCH));
                    tier0_end(kind, class, (when != 0 ? BUSTER_TIER0_VARIANT_BRANCH_TRUE : BUSTER_TIER0_VARIANT_BRANCH_FALSE) + variant, 0);
                }
            }
        }
    }
    // Branches on a value: test it (or compare a home with 0), then je or jne.
    for (uint32_t class = BUSTER_TYPED_CLASS_I8; class <= BUSTER_TYPED_CLASS_U64; class += 1)
    {
        const uint32_t test = tier0_sized(class, X86_FORM_TEST_RM8_R8, X86_FORM_TEST_RM16_R16, X86_FORM_TEST_RM32_R32, X86_FORM_TEST_RM64_R64);
        const uint32_t compare = tier0_sized(class, X86_FORM_CMP_RM8_IMM8, X86_FORM_CMP_RM16_IMM16, X86_FORM_CMP_RM32_IMM32, X86_FORM_CMP_RM64_IMM32);
        for (uint32_t when = 0; when < 2; when += 1)
        {
            const uint32_t kind = when != 0 ? BUSTER_TYPED_BRANCH_TRUE : BUSTER_TYPED_BRANCH_FALSE;
            const uint32_t jump = when != 0 ? X86_FORM_JNE_REL8 : X86_FORM_JE_REL8;
            tier0_begin();
            tier0_add(tier0_rr(test, BUSTER_TIER0_SOURCE_R1, BUSTER_TIER0_SOURCE_R1));
            tier0_add(tier0_branch(jump, BUSTER_TIER0_RECORD_BRANCH));
            tier0_end(kind, class, BUSTER_TIER0_VARIANT_PLAIN, 0);
            tier0_begin();
            tier0_holes_from(no, no, 1);
            Tier0RecordSpec zero = tier0_with_immediate(tier0_rh(compare, BUSTER_TIER0_SOURCE_NONE, BUSTER_TIER0_RM_HOME_A), 0, 0);
            zero.flags |= BUSTER_TIER0_RECORD_CHOICE;
            zero.live = BUSTER_X86_FLAG_ZF;
            tier0_add(zero);
            tier0_add(tier0_branch(jump, BUSTER_TIER0_RECORD_BRANCH));
            tier0_end(kind, class, BUSTER_TIER0_VARIANT_HOME, 0);
        }
    }
    // Pointer addition: lea for the four SIB scales, imul and add for another, add for a constant index.
    for (uint32_t s = 0; s < 4; s += 1)
    {
        tier0_begin();
        tier0_add(tier0_rmem(X86_FORM_LEA_R64_M, BUSTER_TIER0_SOURCE_R2, BUSTER_TIER0_RM_BASE_INDEX, BUSTER_TIER0_SOURCE_R2, BUSTER_TIER0_SOURCE_R1, s));
        tier0_end(BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, BUSTER_TIER0_VARIANT_INDEX(s), 0);
    }
    tier0_begin();
    tier0_holes_from(0, no, no);
    tier0_add(tier0_with_k(tier0_rr(X86_FORM_IMUL_R64_RM64_IMM32, BUSTER_TIER0_SOURCE_R1, BUSTER_TIER0_SOURCE_R1), 0));
    tier0_add(tier0_rr(X86_FORM_ADD_R64_RM64, BUSTER_TIER0_SOURCE_R2, BUSTER_TIER0_SOURCE_R1));
    tier0_end(BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, BUSTER_TIER0_VARIANT_PLAIN, 0);
    tier0_begin();
    tier0_holes_from(1, 0, no);
    tier0_add(tier0_with_k(tier0_r(X86_FORM_ADD_RM64_IMM32, BUSTER_TIER0_SOURCE_R1), 0));
    tier0_end(BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, BUSTER_TIER0_VARIANT_CONST, 0);
    // Floating arithmetic, compares as values (cmpsd's predicate, its mask's low bit), NEG.
    for (uint32_t class = BUSTER_TYPED_CLASS_F32; class <= BUSTER_TYPED_CLASS_F64; class += 1)
    {
        static const uint8_t kinds[4] = { BUSTER_TYPED_ADD, BUSTER_TYPED_SUB, BUSTER_TYPED_MUL, BUSTER_TYPED_DIV };
        const uint32_t singles[4] = { X86_FORM_ADDSS_X_XM32, X86_FORM_SUBSS_X_XM32, X86_FORM_MULSS_X_XM32, X86_FORM_DIVSS_X_XM32 };
        const uint32_t doubles[4] = { X86_FORM_ADDSD_X_XM64, X86_FORM_SUBSD_X_XM64, X86_FORM_MULSD_X_XM64, X86_FORM_DIVSD_X_XM64 };
        for (uint32_t k = 0; k < 4; k += 1)
        {
            const uint32_t form = tier0_sse(class, singles[k], doubles[k]);
            tier0_begin();
            tier0_add(tier0_rr(form, BUSTER_TIER0_SOURCE_X2, BUSTER_TIER0_SOURCE_X1));
            tier0_end(kinds[k], class, BUSTER_TIER0_VARIANT_PLAIN, 0);
            tier0_begin();
            tier0_holes_from(no, no, 1);
            tier0_add(tier0_rh(form, BUSTER_TIER0_SOURCE_X1, BUSTER_TIER0_RM_HOME_A));
            tier0_end(kinds[k], class, BUSTER_TIER0_VARIANT_HOME, 0);
        }
        const uint32_t cmp = tier0_sse(class, X86_FORM_CMPSS_X_XM32_IMM8, X86_FORM_CMPSD_X_XM64_IMM8);
        for (uint32_t kind = BUSTER_TYPED_EQ; kind <= BUSTER_TYPED_GE; kind += 1)
        {
            // EQ_OQ, NEQ_UQ, LT_OS and LE_OS; GT and GE as LT and LE with the operands swapped.
            static const uint8_t predicates[6] = { 0, 4, 1, 2, 1, 2 };
            const bool swapped = kind == BUSTER_TYPED_GT || kind == BUSTER_TYPED_GE;
            const uint8_t into = swapped ? BUSTER_TIER0_SOURCE_X1 : BUSTER_TIER0_SOURCE_X2;
            tier0_begin();
            tier0_add(tier0_with_immediate(tier0_rr(cmp, into, swapped ? BUSTER_TIER0_SOURCE_X2 : BUSTER_TIER0_SOURCE_X1), predicates[kind - BUSTER_TYPED_EQ], 0));
            tier0_add(tier0_rr(X86_FORM_MOVD_RM32_X, into, BUSTER_TIER0_SOURCE_R0));
            tier0_add(tier0_with_immediate(tier0_r(X86_FORM_AND_RM32_IMM8, BUSTER_TIER0_SOURCE_R0), 1, 0));
            tier0_end(kind, class, BUSTER_TIER0_VARIANT_PLAIN, 0);
            if (!swapped)
            {
                tier0_begin();
                tier0_holes_from(no, no, 1);
                tier0_add(tier0_with_immediate(tier0_rh(cmp, BUSTER_TIER0_SOURCE_X1, BUSTER_TIER0_RM_HOME_A), predicates[kind - BUSTER_TYPED_EQ], 0));
                tier0_add(tier0_rr(X86_FORM_MOVD_RM32_X, BUSTER_TIER0_SOURCE_X1, BUSTER_TIER0_SOURCE_R0));
                tier0_add(tier0_with_immediate(tier0_r(X86_FORM_AND_RM32_IMM8, BUSTER_TIER0_SOURCE_R0), 1, 0));
                tier0_end(kind, class, BUSTER_TIER0_VARIANT_HOME, 0);
            }
        }
        // NEG flips the sign bit: the mask through a general register into the free XMM register, then xorps.
        tier0_begin();
        if (class == BUSTER_TYPED_CLASS_F32)
        {
            tier0_add(tier0_with_immediate(tier0_r(X86_FORM_MOV_R32_IMM32, BUSTER_TIER0_SOURCE_R0), INT32_MIN, 0));
            tier0_add(tier0_rr(X86_FORM_MOVD_X_RM32, BUSTER_TIER0_SOURCE_X0, BUSTER_TIER0_SOURCE_R0));
        }
        else
        {
            tier0_add(tier0_with_immediate(tier0_r(X86_FORM_MOV_R64_IMM64, BUSTER_TIER0_SOURCE_R0), 0, INT32_MIN));
            tier0_add(tier0_rr(X86_FORM_MOVQ_X_RM64, BUSTER_TIER0_SOURCE_X0, BUSTER_TIER0_SOURCE_R0));
        }
        tier0_add(tier0_rr(X86_FORM_XORPS_X_XM128, BUSTER_TIER0_SOURCE_X1, BUSTER_TIER0_SOURCE_X0));
        tier0_end(BUSTER_TYPED_NEG, class, BUSTER_TIER0_VARIANT_PLAIN, 0);
    }
}

static bool tier0_initialize(void)
{
    if (!tier0_initialized)
    {
        memset(tier0_slot_bases, 0, sizeof(tier0_slot_bases));
        tier0_template_count = 0;
        tier0_builder.valid = true;
        // Block 0 of the slots: every (kind, class) without templates points at it, all UNSUPPORTED.
        for (uint32_t v = 0; v < BUSTER_TIER0_VARIANTS; v += 1)
        {
            tier0_slots[v] = BUSTER_TIER0_SLOT_UNSUPPORTED;
        }
        tier0_slot_count = BUSTER_TIER0_VARIANTS;
        tier0_build_windows();
        tier0_define_templates();
        tier0_initialized = tier0_builder.valid;
    }
    return tier0_initialized;
}

// ---------------------------------------------------------------------------------------------------------------------
// Fusion and depths

// What an entry pops and pushes, by kind: its first operand's stack (none, its own class's, its last operand's class's,
// or the integer stack: an address), whether it has a last operand (the entry before it, of that entry's class), and
// what it pushes (none, a value of its class, or an integer). CALL's are its arguments' and result's.
#define BUSTER_TIER0_STACK_NONE 0
#define BUSTER_TIER0_STACK_CLASS 1
#define BUSTER_TIER0_STACK_OPERAND 2
#define BUSTER_TIER0_STACK_INTEGER 3

typedef struct Tier0Shape Tier0Shape;
struct Tier0Shape
{
    uint8_t first;
    uint8_t last;
    uint8_t push;
};

#define BUSTER_TIER0_LEAF { BUSTER_TIER0_STACK_NONE, BUSTER_TIER0_STACK_NONE, BUSTER_TIER0_STACK_CLASS }
#define BUSTER_TIER0_UNARY { BUSTER_TIER0_STACK_NONE, BUSTER_TIER0_STACK_OPERAND, BUSTER_TIER0_STACK_CLASS }
#define BUSTER_TIER0_BINARY { BUSTER_TIER0_STACK_CLASS, BUSTER_TIER0_STACK_OPERAND, BUSTER_TIER0_STACK_CLASS }
#define BUSTER_TIER0_COMPARE { BUSTER_TIER0_STACK_OPERAND, BUSTER_TIER0_STACK_OPERAND, BUSTER_TIER0_STACK_INTEGER }
#define BUSTER_TIER0_ADDRESSED { BUSTER_TIER0_STACK_INTEGER, BUSTER_TIER0_STACK_OPERAND, BUSTER_TIER0_STACK_CLASS }
#define BUSTER_TIER0_SINK { BUSTER_TIER0_STACK_NONE, BUSTER_TIER0_STACK_OPERAND, BUSTER_TIER0_STACK_NONE }
static const Tier0Shape tier0_shapes[BUSTER_TYPED_KIND_LIMIT] = {
    [BUSTER_TYPED_CONST] = BUSTER_TIER0_LEAF,
    [BUSTER_TYPED_LOCAL] = BUSTER_TIER0_LEAF,
    [BUSTER_TYPED_LOCAL_ADDRESS] = BUSTER_TIER0_LEAF,
    [BUSTER_TYPED_GLOBAL] = BUSTER_TIER0_LEAF,
    [BUSTER_TYPED_GLOBAL_ADDRESS] = BUSTER_TIER0_LEAF,
    [BUSTER_TYPED_DUP] = BUSTER_TIER0_LEAF,
    [BUSTER_TYPED_NEG] = BUSTER_TIER0_UNARY,
    [BUSTER_TYPED_NOT] = BUSTER_TIER0_UNARY,
    [BUSTER_TYPED_CONVERT] = BUSTER_TIER0_UNARY,
    [BUSTER_TYPED_LOAD] = BUSTER_TIER0_UNARY,
    [BUSTER_TYPED_ADD] = BUSTER_TIER0_BINARY,
    [BUSTER_TYPED_SUB] = BUSTER_TIER0_BINARY,
    [BUSTER_TYPED_MUL] = BUSTER_TIER0_BINARY,
    [BUSTER_TYPED_DIV] = BUSTER_TIER0_BINARY,
    [BUSTER_TYPED_MOD] = BUSTER_TIER0_BINARY,
    [BUSTER_TYPED_AND] = BUSTER_TIER0_BINARY,
    [BUSTER_TYPED_OR] = BUSTER_TIER0_BINARY,
    [BUSTER_TYPED_XOR] = BUSTER_TIER0_BINARY,
    [BUSTER_TYPED_SHL] = BUSTER_TIER0_BINARY,
    [BUSTER_TYPED_SHR] = BUSTER_TIER0_BINARY,
    [BUSTER_TYPED_PTR_ADD] = BUSTER_TIER0_ADDRESSED,
    [BUSTER_TYPED_EQ] = BUSTER_TIER0_COMPARE,
    [BUSTER_TYPED_NE] = BUSTER_TIER0_COMPARE,
    [BUSTER_TYPED_LT] = BUSTER_TIER0_COMPARE,
    [BUSTER_TYPED_LE] = BUSTER_TIER0_COMPARE,
    [BUSTER_TYPED_GT] = BUSTER_TIER0_COMPARE,
    [BUSTER_TYPED_GE] = BUSTER_TIER0_COMPARE,
    [BUSTER_TYPED_STORE] = BUSTER_TIER0_ADDRESSED,
    [BUSTER_TYPED_SET] = { BUSTER_TIER0_STACK_INTEGER, BUSTER_TIER0_STACK_OPERAND, BUSTER_TIER0_STACK_NONE },
    [BUSTER_TYPED_STORE_LOCAL] = BUSTER_TIER0_UNARY,
    [BUSTER_TYPED_SET_LOCAL] = BUSTER_TIER0_SINK,
    [BUSTER_TYPED_STORE_GLOBAL] = BUSTER_TIER0_UNARY,
    [BUSTER_TYPED_SET_GLOBAL] = BUSTER_TIER0_SINK,
    [BUSTER_TYPED_CALL] = { BUSTER_TIER0_STACK_NONE, BUSTER_TIER0_STACK_NONE, BUSTER_TIER0_STACK_CLASS },
    [BUSTER_TYPED_DISCARD] = BUSTER_TIER0_SINK,
    [BUSTER_TYPED_BRANCH_FALSE] = BUSTER_TIER0_SINK,
    [BUSTER_TYPED_BRANCH_TRUE] = BUSTER_TIER0_SINK,
    [BUSTER_TYPED_JUMP] = { BUSTER_TIER0_STACK_NONE, BUSTER_TIER0_STACK_NONE, BUSTER_TIER0_STACK_NONE },
    [BUSTER_TYPED_JUMP_VALUE] = BUSTER_TIER0_SINK,
    [BUSTER_TYPED_RETURN] = BUSTER_TIER0_SINK,
};

// The per-entry columns both paths fill, which the tests compare: each entry's key (its template's kind, class and
// variant), whether a later entry absorbs it, its effects on the two stacks (with its absorbed entries'), the depths
// before it, its template (or ABSORBED or DEFERRED; with HOME_DEPENDENT until the homes are known), its records' count
// and its first record's index.
typedef struct Tier0Entries Tier0Entries;
struct Tier0Entries
{
    uint8_t* key_kinds;
    uint8_t* key_classes;
    uint8_t* variants;
    uint8_t* absorbed;
    int8_t* integer_effects;
    int8_t* floating_effects;
    uint8_t* depths;
    uint8_t* floating_depths;
    uint16_t* templates;
    uint16_t* lengths;
    uint32_t* positions;
};

static bool tier0_label(const uint64_t* labels, uint32_t entry)
{
    return ((labels[entry >> 6] >> (entry & 63)) & 1) != 0;
}

// The entries a branch or a jump goes to, a bit each (the bit past the last entry is the function's end), into
// `labels`, which has room for count + 1 bits and is zero.
static void tier0_labels(const Tier0Function* function, uint64_t* labels)
{
    for (uint32_t i = 0; i < function->count; i += 1)
    {
        const uint8_t kind = function->kinds[i];
        if (kind >= BUSTER_TYPED_BRANCH_FALSE && kind <= BUSTER_TYPED_JUMP_VALUE)
        {
            const uint64_t target = function->values[i];
            labels[target >> 6] |= 1ull << (target & 63);
        }
    }
}

// Which stack a class's values live on: 0 for none (VOID), 1 for the integer stack, 2 for the floating one.
static uint32_t tier0_stack(uint32_t class)
{
    return tier0_class_floating(class) ? 2 : class == BUSTER_TYPED_CLASS_VOID ? 0 : 1;
}

// Entry i's own effects on the integer and floating stacks (what it pushes less what it pops), without fusion.
static void tier0_raw_effects(const Tier0Function* function, uint32_t i, int32_t* integer, int32_t* floating)
{
    const uint8_t kind = function->kinds[i];
    const uint8_t class = function->classes[i];
    const uint8_t operand = i > 0 ? function->classes[i - 1] : BUSTER_TYPED_CLASS_VOID;
    const Tier0Shape shape = tier0_shapes[kind];
    int32_t counts[3] = { 0, 0, 0 };
    if (kind == BUSTER_TYPED_CALL)
    {
        const uint32_t call = (uint32_t)function->values[i];
        const uint32_t first = function->call_first[call];
        for (uint32_t a = 0; a < function->call_argument_counts[call]; a += 1)
        {
            counts[tier0_stack(function->argument_classes[first + a])] -= 1;
        }
        counts[tier0_stack(class)] += 1;
    }
    else
    {
        const uint32_t first = shape.first == BUSTER_TIER0_STACK_CLASS     ? tier0_stack(class)
                               : shape.first == BUSTER_TIER0_STACK_OPERAND ? tier0_stack(operand)
                               : shape.first == BUSTER_TIER0_STACK_INTEGER ? 1
                                                                           : 0;
        const bool returns_nothing = kind == BUSTER_TYPED_RETURN && class == BUSTER_TYPED_CLASS_VOID;
        const uint32_t last = shape.last == BUSTER_TIER0_STACK_OPERAND && !returns_nothing ? tier0_stack(operand) : 0;
        const uint32_t push = shape.push == BUSTER_TIER0_STACK_CLASS ? tier0_stack(class) : shape.push == BUSTER_TIER0_STACK_INTEGER ? 1 : 0;
        counts[first] -= 1;
        counts[last] -= 1;
        counts[push] += 1;
    }
    *integer = counts[1];
    *floating = counts[2];
}

// The class a kind's templates are keyed by: a compare's operands' (the entry before it), the entry's own otherwise.
static uint32_t tier0_key_class(uint32_t kind, uint32_t class, uint32_t operand)
{
    return kind >= BUSTER_TYPED_EQ && kind <= BUSTER_TYPED_GE ? operand : class;
}

static uint16_t tier0_slot(uint32_t kind, uint32_t class, uint32_t variant)
{
    return tier0_slots[tier0_slot_bases[(kind & (BUSTER_TYPED_KIND_LIMIT - 1)) * BUSTER_TYPED_CLASS_LIMIT + (class & (BUSTER_TYPED_CLASS_LIMIT - 1))] +
                       (variant & (BUSTER_TIER0_VARIANTS - 1))];
}

// The variant code of a fusion for a kind: CONVERT's carry its source class.
static uint32_t tier0_kind_variant(uint32_t kind, uint32_t operand, uint32_t variant)
{
    uint32_t result = variant;
    if (kind == BUSTER_TYPED_CONVERT)
    {
        result = variant == BUSTER_TIER0_VARIANT_HOME ? operand | BUSTER_TIER0_VARIANT_CONVERT_HOME
                 : variant == BUSTER_TIER0_VARIANT_PLAIN ? operand
                                                          : BUSTER_TIER0_VARIANTS - 1;
    }
    return result;
}

static bool tier0_fits_int32(int64_t value)
{
    return value >= INT32_MIN && value <= INT32_MAX;
}

// Whether `kind`, keyed by `class`, takes the constant `value` as an immediate: any 32-bit pattern for a 1- to 4-byte
// operation, a sign-extended 32-bit one for an 8-byte one (but `mov r, imm` takes any), a shift's count up to 255, an
// index whose product with the element's size `scale` fits 32 bits.
static bool tier0_constant_fits(uint32_t kind, uint32_t class, uint64_t value, uint64_t scale)
{
    bool fits = tier0_class_bytes(class) < 8 || tier0_fits_int32((int64_t)value);
    if (kind == BUSTER_TYPED_SHL || kind == BUSTER_TYPED_SHR)
    {
        fits = value <= 255;
    }
    else if (kind == BUSTER_TYPED_PTR_ADD)
    {
        int64_t product = 0;
        fits = !__builtin_mul_overflow((int64_t)value, (int64_t)scale, &product) && tier0_fits_int32(product);
    }
    else if (kind == BUSTER_TYPED_DISCARD || kind == BUSTER_TYPED_STORE_LOCAL || kind == BUSTER_TYPED_STORE_GLOBAL)
    {
        fits = true;
    }
    return fits;
}

// Whether a pointer addition's element size is a SIB scale, and which.
static bool tier0_sib_scale(uint64_t size, uint32_t* log2)
{
    const bool sib = size == 1 || size == 2 || size == 4 || size == 8;
    *log2 = size == 8 ? 3 : size == 4 ? 2 : size == 2 ? 1 : 0;
    return sib;
}

static void tier0_absorb(Tier0Entries* entries, uint32_t consumer, uint32_t absorbed)
{
    entries->integer_effects[consumer] = (int8_t)(entries->integer_effects[consumer] + entries->integer_effects[absorbed]);
    entries->floating_effects[consumer] = (int8_t)(entries->floating_effects[consumer] + entries->floating_effects[absorbed]);
    entries->integer_effects[absorbed] = 0;
    entries->floating_effects[absorbed] = 0;
    entries->absorbed[absorbed] = 1;
}

// Phase 1, an entry at a time: each consumer takes the entries its windows allow (they are all behind it, and no two
// consumers want the same one), and gets its key. The vector path computes the same columns with masks.
static void tier0_fuse_scalar(const Tier0Function* function, const uint64_t* labels, Tier0Entries* entries)
{
    const uint8_t* kinds = function->kinds;
    const uint8_t* classes = function->classes;
    const uint64_t* values = function->values;
    for (uint32_t i = 0; i < function->count; i += 1)
    {
        const uint8_t kind = kinds[i];
        const uint8_t class = classes[i];
        const uint8_t operand = i > 0 ? classes[i - 1] : BUSTER_TYPED_CLASS_VOID;
        uint32_t key_kind = kind;
        uint32_t key_class = tier0_key_class(kind, class, operand);
        uint32_t variant = tier0_kind_variant(kind, operand, BUSTER_TIER0_VARIANT_PLAIN);
        int32_t integer = 0;
        int32_t floating = 0;
        tier0_raw_effects(function, i, &integer, &floating);
        entries->integer_effects[i] = (int8_t)integer;
        entries->floating_effects[i] = (int8_t)floating;
        entries->absorbed[i] = 0;
        const bool here = tier0_label(labels, i);
        const bool before = i > 0 && tier0_label(labels, i - 1);
        // The entry's own variants.
        uint32_t log2 = 0;
        if (kind == BUSTER_TYPED_CONST && tier0_class_floating(class) && values[i] == 0)
        {
            variant = BUSTER_TIER0_VARIANT_CONST;
        }
        else if (kind == BUSTER_TYPED_PTR_ADD && tier0_sib_scale(values[i], &log2))
        {
            variant = BUSTER_TIER0_VARIANT_INDEX(log2);
        }
        else if (kind == BUSTER_TYPED_RETURN && i + 1 == function->count)
        {
            variant = BUSTER_TIER0_VARIANT_LAST;
        }
        // A local and a constant as a compare's or a multiplication's operands; or a leaf as the last operand.
        const bool has_last = tier0_shapes[kind].last == BUSTER_TIER0_STACK_OPERAND && !(kind == BUSTER_TYPED_RETURN && class == BUSTER_TYPED_CLASS_VOID);
        const bool binary = tier0_shapes[kind].first != BUSTER_TIER0_STACK_NONE;
        const uint64_t scale = kind == BUSTER_TYPED_PTR_ADD ? values[i] : 1;
        bool leaf_taken = false;
        if (i >= 2 && !here && !before && (kind == BUSTER_TYPED_MUL || (kind >= BUSTER_TYPED_EQ && kind <= BUSTER_TYPED_GE)) && kinds[i - 1] == BUSTER_TYPED_CONST &&
            kinds[i - 2] == BUSTER_TYPED_LOCAL && tier0_class_integer(operand) && tier0_constant_fits(kind, key_class, values[i - 1], 1) &&
            tier0_slot(kind, key_class, BUSTER_TIER0_VARIANT_HOME_CONST) != BUSTER_TIER0_SLOT_UNSUPPORTED)
        {
            variant = BUSTER_TIER0_VARIANT_HOME_CONST;
            tier0_absorb(entries, i, i - 1);
            tier0_absorb(entries, i, i - 2);
            leaf_taken = true;
        }
        if (!leaf_taken && i >= 1 && !here && has_last)
        {
            const uint8_t previous = kinds[i - 1];
            const bool constant = previous == BUSTER_TYPED_CONST && (tier0_class_integer(operand) || kind == BUSTER_TYPED_DISCARD) &&
                                  tier0_constant_fits(kind, key_class, values[i - 1], scale);
            const uint32_t fused = constant ? BUSTER_TIER0_VARIANT_CONST : previous == BUSTER_TYPED_LOCAL ? BUSTER_TIER0_VARIANT_HOME : BUSTER_TIER0_VARIANT_PLAIN;
            const uint32_t fused_variant = tier0_kind_variant(kind, operand, fused);
            if (fused != BUSTER_TIER0_VARIANT_PLAIN && tier0_slot(kind, key_class, fused_variant) != BUSTER_TIER0_SLOT_UNSUPPORTED)
            {
                variant = fused_variant;
                tier0_absorb(entries, i, i - 1);
                leaf_taken = true;
            }
        }
        (void)binary;
        // A store's address: the pointer addition before its fused value.
        if (kind == BUSTER_TYPED_SET && leaf_taken && i >= 2 && kinds[i - 2] == BUSTER_TYPED_PTR_ADD && !entries->absorbed[i - 2] && !before)
        {
            const uint32_t address = entries->variants[i - 2];
            uint32_t fused = BUSTER_TIER0_VARIANTS;
            if (address >= BUSTER_TIER0_VARIANT_INDEX(0) && address <= BUSTER_TIER0_VARIANT_INDEX(3))
            {
                const uint32_t s = address - BUSTER_TIER0_VARIANT_INDEX(0);
                fused = variant == BUSTER_TIER0_VARIANT_CONST ? BUSTER_TIER0_VARIANT_INDEX_CONST(s) : BUSTER_TIER0_VARIANT_INDEX_HOME(s);
            }
            else if (address == BUSTER_TIER0_VARIANT_CONST && variant == BUSTER_TIER0_VARIANT_HOME)
            {
                fused = BUSTER_TIER0_VARIANT_OFFSET_HOME;
            }
            if (fused < BUSTER_TIER0_VARIANTS && tier0_slot(kind, key_class, fused) != BUSTER_TIER0_SLOT_UNSUPPORTED)
            {
                variant = fused;
                tier0_absorb(entries, i, i - 2);
            }
        }
        // A load's address.
        if (kind == BUSTER_TYPED_LOAD && i >= 1 && !here && kinds[i - 1] == BUSTER_TYPED_PTR_ADD)
        {
            const uint32_t address = entries->variants[i - 1];
            uint32_t fused = BUSTER_TIER0_VARIANTS;
            if (address >= BUSTER_TIER0_VARIANT_INDEX(0) && address <= BUSTER_TIER0_VARIANT_INDEX(3))
            {
                fused = address;
            }
            else if (address == BUSTER_TIER0_VARIANT_CONST)
            {
                fused = BUSTER_TIER0_VARIANT_OFFSET;
            }
            if (fused < BUSTER_TIER0_VARIANTS && tier0_slot(kind, key_class, fused) != BUSTER_TIER0_SLOT_UNSUPPORTED)
            {
                variant = fused;
                tier0_absorb(entries, i, i - 1);
            }
        }
        // A branch's compare.
        if ((kind == BUSTER_TYPED_BRANCH_FALSE || kind == BUSTER_TYPED_BRANCH_TRUE) && i >= 1 && !here && kinds[i - 1] >= BUSTER_TYPED_EQ &&
            kinds[i - 1] <= BUSTER_TYPED_GE)
        {
            const uint32_t base = kind == BUSTER_TYPED_BRANCH_TRUE ? BUSTER_TIER0_VARIANT_BRANCH_TRUE : BUSTER_TIER0_VARIANT_BRANCH_FALSE;
            const uint32_t fused = base + entries->variants[i - 1];
            if (entries->variants[i - 1] <= BUSTER_TIER0_VARIANT_HOME_CONST &&
                tier0_slot(kinds[i - 1], entries->key_classes[i - 1], fused) != BUSTER_TIER0_SLOT_UNSUPPORTED)
            {
                key_kind = kinds[i - 1];
                key_class = entries->key_classes[i - 1];
                variant = fused;
                tier0_absorb(entries, i, i - 1);
            }
        }
        // x = x op v.
        if (kind == BUSTER_TYPED_SET_LOCAL && i >= 3 && !here && !before && !tier0_label(labels, i - 2))
        {
            const uint8_t op = kinds[i - 1];
            const uint32_t inner = entries->variants[i - 1];
            const bool rmw_kind = (op >= BUSTER_TYPED_AND && op <= BUSTER_TYPED_SHR) || op == BUSTER_TYPED_ADD || op == BUSTER_TYPED_SUB;
            const uint32_t fused = inner == BUSTER_TIER0_VARIANT_CONST ? BUSTER_TIER0_VARIANT_RMW_CONST : BUSTER_TIER0_VARIANT_RMW_HOME;
            if (rmw_kind && (inner == BUSTER_TIER0_VARIANT_CONST || inner == BUSTER_TIER0_VARIANT_HOME) && kinds[i - 3] == BUSTER_TYPED_LOCAL &&
                !entries->absorbed[i - 3] && values[i - 3] == values[i] && classes[i - 3] == class && classes[i - 1] == class &&
                tier0_slot(op, class, fused) != BUSTER_TIER0_SLOT_UNSUPPORTED)
            {
                key_kind = op;
                key_class = class;
                variant = fused;
                tier0_absorb(entries, i, i - 1);
                tier0_absorb(entries, i, i - 3);
            }
        }
        entries->key_kinds[i] = (uint8_t)key_kind;
        entries->key_classes[i] = (uint8_t)key_class;
        entries->variants[i] = (uint8_t)variant;
    }
}

// Phase 2: the depths before each entry, the effects' exclusive prefix sums.
static void tier0_depths_scalar(const Tier0Function* function, Tier0Entries* entries)
{
    int32_t integer = 0;
    int32_t floating = 0;
    for (uint32_t i = 0; i < function->count; i += 1)
    {
        entries->depths[i] = (uint8_t)integer;
        entries->floating_depths[i] = (uint8_t)floating;
        integer += entries->integer_effects[i];
        floating += entries->floating_effects[i];
    }
}

// Each entry's template, from its key's slot (ABSORBED for an absorbed entry; DEFERRED for a deferred slot or a
// template whose fixed registers need another depth). Returns the first entry without one, or the count.
static uint32_t tier0_templates_scalar(const Tier0Function* function, Tier0Entries* entries)
{
    uint32_t unsupported = function->count;
    for (uint32_t i = 0; i < function->count; i += 1)
    {
        uint16_t entry = BUSTER_TIER0_ENTRY_ABSORBED;
        if (!entries->absorbed[i])
        {
            const uint16_t slot = tier0_slot(entries->key_kinds[i], entries->key_classes[i], entries->variants[i]);
            entry = slot;
            if (slot == BUSTER_TIER0_SLOT_UNSUPPORTED)
            {
                unsupported = unsupported == function->count ? i : unsupported;
            }
            else if (slot != BUSTER_TIER0_SLOT_DEFERRED)
            {
                const Tier0Template* template = &tier0_templates[slot & BUSTER_TIER0_SLOT_TEMPLATE];
                if (template->required_depth != BUSTER_TIER0_ANY_DEPTH && template->required_depth != entries->depths[i])
                {
                    entry = BUSTER_TIER0_ENTRY_DEFERRED;
                }
            }
        }
        entries->templates[i] = entry;
    }
    return unsupported;
}

// ---------------------------------------------------------------------------------------------------------------------
// Homes and the frame

// A local's home, in the hole block's layout: a register (mode 0) or a frame slot ([rsp + displacement]).
typedef struct Tier0Home Tier0Home;
struct Tier0Home
{
    int32_t displacement;
    uint8_t reg;
    uint8_t mode;
    uint8_t is_register;
    uint8_t padding;
};
_Static_assert(sizeof(Tier0Home) == 8, "a home is a hole block's eight bytes");

// The frame below the saved registers, from rsp up: the outgoing stack arguments, the call save area (the depths below
// a call's arguments, saved around it), two slots for the deferred division and shifts, then the locals' slots.
// `size` is what the prologue subtracts from rsp (with the pushes and the return address, a multiple of 16 when the
// function calls); `saved` has a bit per tier0_callee_saved register the prologue pushes. `clobbers` is the register set
// the body touches (BUSTER_TIER0_XMM's layout), and `integer_top` and `floating_top` the highest depth registers in it.
typedef struct Tier0Frame Tier0Frame;
struct Tier0Frame
{
    uint32_t size;
    uint32_t saved;
    uint32_t pushes;
    uint32_t save_area;
    uint32_t save_slots;
    uint32_t fixed_area;
    uint32_t fixed_slots;
    uint32_t clobbers;
    int32_t integer_top;
    int32_t floating_top;
    bool calls;
};

// The registers each depth table's prefix covers, as a register set.
static uint32_t tier0_depth_registers(int32_t integer_top, int32_t floating_top)
{
    uint32_t set = 0;
    for (int32_t d = 0; d <= integer_top && d < BUSTER_TIER0_INTEGER_DEPTHS; d += 1)
    {
        set |= 1u << tier0_integer_depths[d];
    }
    for (int32_t d = 0; d <= floating_top && d < BUSTER_TIER0_FLOATING_DEPTHS; d += 1)
    {
        set |= 1u << BUSTER_TIER0_XMM(d);
    }
    return set;
}

// What the body touches: the depth registers it reaches, the templates' fixed registers, and for a call everything
// caller-saved; with the call save area's and the fixed slots' sizes. Fails with TIER0_ERROR_DEPTH past the tables.
static Tier0Error tier0_touches(const Tier0Function* function, const Tier0Entries* entries, Tier0Frame* frame)
{
    int32_t integer_top = -1;
    int32_t floating_top = -1;
    uint32_t clobbers = 0;
    uint32_t save_slots = 0;
    bool fixed = false;
    bool calls = false;
    Tier0Error error = TIER0_OK;
    for (uint32_t i = 0; i < function->count; i += 1)
    {
        const uint16_t entry = entries->templates[i];
        const int32_t depth = entries->depths[i];
        const int32_t floating = entries->floating_depths[i];
        if (entry == BUSTER_TIER0_ENTRY_DEFERRED || entry == BUSTER_TIER0_SLOT_DEFERRED)
        {
            const uint8_t kind = function->kinds[i];
            if (kind == BUSTER_TYPED_CALL)
            {
                const uint32_t call = (uint32_t)function->values[i];
                const uint32_t first = function->call_first[call];
                uint32_t integer_arguments = 0;
                uint32_t floating_arguments = 0;
                for (uint32_t a = 0; a < function->call_argument_counts[call]; a += 1)
                {
                    const bool is_floating = tier0_class_floating(function->argument_classes[first + a]);
                    integer_arguments += is_floating ? 0 : 1;
                    floating_arguments += is_floating ? 1 : 0;
                }
                const int32_t below = depth - (int32_t)integer_arguments;
                const int32_t floating_below = floating - (int32_t)floating_arguments;
                const uint32_t saved = below < BUSTER_TIER0_CALLER_SAVED_DEPTHS ? (uint32_t)below : BUSTER_TIER0_CALLER_SAVED_DEPTHS;
                const uint32_t slots = saved + (uint32_t)floating_below;
                save_slots = slots > save_slots ? slots : save_slots;
                const uint32_t result = tier0_stack(function->classes[i]);
                integer_top = result == 1 && below > integer_top ? below : integer_top;
                floating_top = result == 2 && floating_below > floating_top ? floating_below : floating_top;
                clobbers |= BUSTER_TIER0_GENERAL_CALLER_SAVED | BUSTER_TIER0_XMM_ALL;
                calls = true;
            }
            else
            {
                // Division and shifts by a register: rax, rdx and rcx, saved around them in the fixed slots.
                clobbers |= 1u << X86_REGISTER_RAX | 1u << X86_REGISTER_RDX | 1u << X86_REGISTER_RCX;
                fixed = true;
            }
        }
        else if (entry != BUSTER_TIER0_ENTRY_ABSORBED)
        {
            const uint32_t base = entry & BUSTER_TIER0_SLOT_TEMPLATE;
            const uint32_t cases = (entry & BUSTER_TIER0_SLOT_HOME_DEPENDENT) != 0 ? 4 : 1;
            for (uint32_t c = 0; c < cases; c += 1)
            {
                const Tier0Template* template = &tier0_templates[base + c];
                if (template->integer_reach != 0)
                {
                    const int32_t top = depth + template->integer_reach - 3;
                    integer_top = top > integer_top ? top : integer_top;
                }
                if (template->floating_reach != 0)
                {
                    const int32_t top = floating + template->floating_reach - 3;
                    floating_top = top > floating_top ? top : floating_top;
                }
                clobbers |= template->clobbers;
            }
        }
    }
    if (integer_top >= BUSTER_TIER0_INTEGER_DEPTHS || floating_top >= BUSTER_TIER0_FLOATING_DEPTHS)
    {
        error = TIER0_ERROR_DEPTH;
    }
    frame->integer_top = integer_top;
    frame->floating_top = floating_top;
    frame->clobbers = clobbers | tier0_depth_registers(integer_top, floating_top);
    frame->save_slots = save_slots;
    frame->fixed_slots = fixed ? 2 : 0;
    frame->calls = calls;
    return error;
}

// Phase 3: each local's home, and the frame. A parameter whose register the body doesn't touch stays in it. The other
// locals, most referenced first (by index among equals), take the free caller-saved registers, then (referenced at
// least twice, for a push and a pop) the callee-saved ones; a floating local takes a free XMM register. The rest, and
// every local whose address is taken, get frame slots; a parameter on the stack keeps its incoming slot. `references`
// (local_count) and `order` (local_count) are scratch.
static void tier0_assign_homes(const Tier0Function* function, Tier0Frame* frame, Tier0Home* homes, uint32_t* references, uint32_t* order)
{
    const uint32_t locals = function->local_count;
    memset(references, 0, locals * sizeof(uint32_t));
    for (uint32_t i = 0; i < function->count; i += 1)
    {
        const uint8_t kind = function->kinds[i];
        if (kind == BUSTER_TYPED_LOCAL || kind == BUSTER_TYPED_LOCAL_ADDRESS || kind == BUSTER_TYPED_STORE_LOCAL || kind == BUSTER_TYPED_SET_LOCAL)
        {
            references[function->values[i]] += 1;
        }
    }
    // The registers parameters arrive in: no other local takes one, so the prologue's moves never overwrite a source.
    uint32_t arrivals = 0;
    for (uint32_t p = 0; p < function->parameter_count; p += 1)
    {
        const AbiLocation location = function->parameters[p];
        if (location.kind == BUSTER_ABI_LOCATION_REGISTERS)
        {
            const uint8_t reg = location.registers[0];
            arrivals |= (reg & BUSTER_ABI_REGISTER_VECTOR) != 0 ? 1u << BUSTER_TIER0_XMM(reg & 0x1f) : 1u << reg;
        }
    }
    uint32_t taken = frame->clobbers | arrivals | 1u << X86_REGISTER_RSP;
    for (uint32_t l = 0; l < locals; l += 1)
    {
        homes[l] = (Tier0Home){ 0, 0, 0, 0, 0 };
        order[l] = l;
    }
    // Parameters that stay where they arrive.
    for (uint32_t p = 0; p < function->parameter_count; p += 1)
    {
        const AbiLocation location = function->parameters[p];
        const bool address_taken = (function->local_flags[p] & BUSTER_TYPED_LOCAL_ADDRESS_TAKEN) != 0;
        if (location.kind == BUSTER_ABI_LOCATION_REGISTERS && !address_taken && references[p] != 0)
        {
            const uint8_t reg = location.registers[0];
            const uint32_t bit = (reg & BUSTER_ABI_REGISTER_VECTOR) != 0 ? 1u << BUSTER_TIER0_XMM(reg & 0x1f) : 1u << reg;
            if ((frame->clobbers & bit) == 0)
            {
                homes[p].reg = (uint8_t)(reg & 0x1f);
                homes[p].is_register = 1;
            }
        }
    }
    // The rest by references, most first: an insertion sort, since a function's locals are few.
    for (uint32_t a = 1; a < locals; a += 1)
    {
        const uint32_t local = order[a];
        uint32_t b = a;
        while (b > 0 && references[order[b - 1]] < references[local])
        {
            order[b] = order[b - 1];
            b -= 1;
        }
        order[b] = local;
    }
    for (uint32_t k = 0; k < locals; k += 1)
    {
        const uint32_t local = order[k];
        const bool address_taken = (function->local_flags[local] & BUSTER_TYPED_LOCAL_ADDRESS_TAKEN) != 0;
        const bool floating = tier0_class_floating(function->local_classes[local]);
        if (!homes[local].is_register && !address_taken && references[local] != 0)
        {
            uint32_t chosen = UINT32_MAX;
            if (floating)
            {
                for (uint32_t x = 0; x < 16 && chosen == UINT32_MAX; x += 1)
                {
                    chosen = (taken & 1u << BUSTER_TIER0_XMM(x)) == 0 ? x : UINT32_MAX;
                }
                if (chosen != UINT32_MAX)
                {
                    taken |= 1u << BUSTER_TIER0_XMM(chosen);
                }
            }
            else
            {
                for (uint32_t d = 0; d < BUSTER_TIER0_CALLER_SAVED_DEPTHS && chosen == UINT32_MAX; d += 1)
                {
                    const uint32_t reg = tier0_integer_depths[d];
                    chosen = (taken & 1u << reg) == 0 ? reg : UINT32_MAX;
                }
                for (uint32_t c = 0; c < BUSTER_TIER0_CALLEE_SAVED && chosen == UINT32_MAX && references[local] >= 2; c += 1)
                {
                    const uint32_t reg = tier0_callee_saved[c];
                    if ((taken & 1u << reg) == 0)
                    {
                        chosen = reg;
                        frame->saved |= 1u << c;
                    }
                }
                if (chosen != UINT32_MAX)
                {
                    taken |= 1u << chosen;
                }
            }
            if (chosen != UINT32_MAX)
            {
                homes[local].reg = (uint8_t)chosen;
                homes[local].is_register = 1;
            }
        }
    }
    // The callee-saved registers the depths reach are saved too.
    for (uint32_t c = 0; c < BUSTER_TIER0_CALLEE_SAVED; c += 1)
    {
        frame->saved |= (frame->clobbers & 1u << tier0_callee_saved[c]) != 0 ? 1u << c : 0;
    }
    frame->pushes = (uint32_t)__builtin_popcount(frame->saved);
}

// The frame's offsets and size, and the memory homes' displacements: the outgoing arguments at rsp, then the call
// save area, the fixed slots and a slot per local in memory (8 bytes each: every class fits). A parameter that arrives
// on the stack keeps its incoming slot, above the return address.
static void tier0_layout_frame(const Tier0Function* function, Tier0Frame* frame, Tier0Home* homes, const uint32_t* references)
{
    uint32_t outgoing = 0;
    for (uint32_t c = 0; c < function->call_count; c += 1)
    {
        outgoing = function->calls[c].stack_bytes > outgoing ? function->calls[c].stack_bytes : outgoing;
    }
    uint32_t offset = (outgoing + 7) & ~7u;
    frame->save_area = offset;
    offset += 8 * frame->save_slots;
    frame->fixed_area = offset;
    offset += 8 * frame->fixed_slots;
    for (uint32_t l = 0; l < function->local_count; l += 1)
    {
        const bool address_taken = (function->local_flags[l] & BUSTER_TYPED_LOCAL_ADDRESS_TAKEN) != 0;
        const bool on_stack = l < function->parameter_count && function->parameters[l].kind == BUSTER_ABI_LOCATION_STACK;
        if (!homes[l].is_register && (references[l] != 0 || address_taken) && !on_stack)
        {
            homes[l].displacement = (int32_t)offset;
            homes[l].reg = X86_REGISTER_RSP;
            homes[l].mode = BUSTER_X86_MEMORY_BASE;
            offset += 8;
        }
    }
    // With the pushes and the return address, a multiple of 16 at every call.
    const uint32_t above = 8 * frame->pushes + 8;
    if (frame->calls)
    {
        offset = (offset + above + 15) / 16 * 16 - above;
    }
    frame->size = offset;
    for (uint32_t p = 0; p < function->parameter_count; p += 1)
    {
        if (function->parameters[p].kind == BUSTER_ABI_LOCATION_STACK && !homes[p].is_register)
        {
            homes[p].displacement = (int32_t)(frame->size + above + function->parameters[p].stack_offset);
            homes[p].reg = X86_REGISTER_RSP;
            homes[p].mode = BUSTER_X86_MEMORY_BASE;
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Holes and expansion

typedef struct Tier0Holes Tier0Holes;
struct Tier0Holes
{
    uint64_t k;
    Tier0Home a;
    Tier0Home b;
    uint8_t registers[8];
};
_Static_assert(sizeof(Tier0Holes) == BUSTER_TIER0_HOLE_BYTES, "the hole block is 32 bytes");
_Static_assert(offsetof(Tier0Holes, a) == BUSTER_TIER0_HOLE_A && offsetof(Tier0Holes, b) == BUSTER_TIER0_HOLE_B, "the hole block's layout");
_Static_assert(offsetof(Tier0Holes, registers) == BUSTER_TIER0_HOLE_REGISTERS, "the hole block's layout");

// The registers around each depth, for the hole blocks: entry d holds the integer stack's registers at d - 2, d - 1 and
// d, and the floating stack's, as R2, R1, R0, X2, X1 and X0 (0 where the table has none, which no template uses).
#define BUSTER_TIER0_WINDOWS 32
static uint8_t tier0_integer_window[BUSTER_TIER0_WINDOWS][4] __attribute__((aligned(64)));
static uint8_t tier0_floating_window[BUSTER_TIER0_WINDOWS][4] __attribute__((aligned(64)));

static void tier0_build_windows(void)
{
    for (int32_t d = 0; d < BUSTER_TIER0_WINDOWS; d += 1)
    {
        for (int32_t k = 0; k < 3; k += 1)
        {
            const int32_t depth = d - 2 + k;
            tier0_integer_window[d][k] = depth >= 0 && depth < BUSTER_TIER0_INTEGER_DEPTHS ? tier0_integer_depths[depth] : 0;
            tier0_floating_window[d][k] = depth >= 0 && depth < BUSTER_TIER0_FLOATING_DEPTHS ? (uint8_t)depth : 0;
        }
        tier0_integer_window[d][3] = 0;
        tier0_floating_window[d][3] = 0;
    }
}

// Entry i's hole block, for its template.
static Tier0Holes tier0_holes_scalar(const Tier0Function* function, const Tier0Entries* entries, const Tier0Home* homes, uint32_t i, const Tier0Template* template)
{
    Tier0Holes holes;
    memset(&holes, 0, sizeof(holes));
    if (template->k_offset != BUSTER_TIER0_NO_OFFSET)
    {
        holes.k = function->values[i - template->k_offset];
        if (template->k_scale != BUSTER_TIER0_NO_OFFSET)
        {
            holes.k *= function->values[i - template->k_scale];
        }
    }
    if (template->a_offset != BUSTER_TIER0_NO_OFFSET)
    {
        holes.a = homes[function->values[i - template->a_offset]];
    }
    const uint8_t kind = function->kinds[i];
    if (kind == BUSTER_TYPED_LOCAL || kind == BUSTER_TYPED_LOCAL_ADDRESS || kind == BUSTER_TYPED_STORE_LOCAL || kind == BUSTER_TYPED_SET_LOCAL)
    {
        holes.b = homes[function->values[i]];
    }
    const uint32_t depth = entries->depths[i] < BUSTER_TIER0_WINDOWS ? entries->depths[i] : BUSTER_TIER0_WINDOWS - 1;
    const uint32_t floating = entries->floating_depths[i] < BUSTER_TIER0_WINDOWS ? entries->floating_depths[i] : BUSTER_TIER0_WINDOWS - 1;
    for (uint32_t k = 0; k < 3; k += 1)
    {
        holes.registers[k] = tier0_integer_window[depth][k];
        holes.registers[3 + k] = tier0_floating_window[floating][k];
    }
    holes.a.is_register = 0;
    holes.b.is_register = 0;
    return holes;
}

static uint8_t tier0_register_of(uint8_t source, const Tier0Holes* holes)
{
    uint8_t reg = 0;
    if (source >= BUSTER_TIER0_SOURCE_R2 && source <= BUSTER_TIER0_SOURCE_X0)
    {
        reg = holes->registers[source - BUSTER_TIER0_SOURCE_R2];
    }
    else if (source == BUSTER_TIER0_SOURCE_A)
    {
        reg = holes->a.reg;
    }
    else if (source == BUSTER_TIER0_SOURCE_B)
    {
        reg = holes->b.reg;
    }
    else if ((source & BUSTER_TIER0_SOURCE_FIXED) != 0)
    {
        reg = (uint8_t)(source & ~BUSTER_TIER0_SOURCE_FIXED);
    }
    return reg;
}

// A record from its specification, field by field: the scalar expansion (the vector one copies the compiled bytes).
static X86Instruction tier0_record_scalar(const Tier0RecordSpec* spec, const Tier0Holes* holes)
{
    X86Instruction record;
    memset(&record, 0, sizeof(record));
    record.form = (uint16_t)(spec->form | ((spec->flags & BUSTER_TIER0_RECORD_FIXUP) != 0 ? BUSTER_X86_FORM_FIXUP : 0));
    if (spec->immediate_kind == BUSTER_TIER0_IMMEDIATE_STATIC)
    {
        record.immediate = spec->immediate;
        record.displacement = spec->displacement;
    }
    else if (spec->immediate_kind == BUSTER_TIER0_IMMEDIATE_K)
    {
        record.immediate = (int32_t)(uint32_t)holes->k;
    }
    record.reg = tier0_register_of(spec->reg, holes);
    switch (spec->rm_kind)
    {
    case BUSTER_TIER0_RM_REGISTER:
        record.rm = tier0_register_of(spec->rm, holes);
        break;
    case BUSTER_TIER0_RM_HOME_A:
    case BUSTER_TIER0_RM_HOME_B:
    {
        const Tier0Home* home = spec->rm_kind == BUSTER_TIER0_RM_HOME_A ? &holes->a : &holes->b;
        record.rm = home->reg;
        record.mode = home->mode;
        record.displacement = home->displacement;
        break;
    }
    case BUSTER_TIER0_RM_BASE:
        record.rm = tier0_register_of(spec->rm, holes);
        record.mode = BUSTER_X86_MEMORY_BASE;
        break;
    case BUSTER_TIER0_RM_BASE_K:
        record.rm = tier0_register_of(spec->rm, holes);
        record.mode = BUSTER_X86_MEMORY_BASE;
        record.displacement = (int32_t)(uint32_t)holes->k;
        break;
    case BUSTER_TIER0_RM_BASE_INDEX:
        record.rm = tier0_register_of(spec->rm, holes);
        record.index = tier0_register_of(spec->index, holes);
        record.mode = (uint8_t)BUSTER_X86_MEMORY_BASE_INDEX(spec->scale);
        break;
    case BUSTER_TIER0_RM_RIP:
        record.mode = BUSTER_X86_MEMORY_RIP;
        break;
    default:
        break;
    }
    return record;
}

// ---------------------------------------------------------------------------------------------------------------------
// The deferred entries

// A record sink: writes records at `records[count]` onward (when `records` isn't 0: the function's records, `count` an
// absolute index) and counts them, and adds the calls' references.
typedef struct Tier0Writer Tier0Writer;
struct Tier0Writer
{
    X86Instruction* records;
    uint32_t count;
    LinkReference* references;
    uint32_t reference_count;
};

static void tier0_put(Tier0Writer* writer, X86Instruction record)
{
    if (writer->records != 0)
    {
        writer->records[writer->count] = record;
    }
    writer->count += 1;
}

static X86Instruction tier0_instruction(uint32_t form, uint8_t reg, uint8_t rm, uint8_t mode, int32_t displacement, int32_t immediate)
{
    X86Instruction record;
    memset(&record, 0, sizeof(record));
    record.form = (uint16_t)form;
    record.reg = reg;
    record.rm = rm;
    record.mode = mode;
    record.displacement = displacement;
    record.immediate = immediate;
    return record;
}

// mov [rsp + offset], reg and back, 64-bit (a general register) or as a double (an XMM one, BUSTER_TIER0_XMM's).
static void tier0_spill(Tier0Writer* writer, uint32_t reg, uint32_t offset, bool load)
{
    const bool xmm = reg >= 16;
    const uint32_t form = xmm ? (load ? X86_FORM_MOVSD_X_XM64 : X86_FORM_MOVSD_XM64_X) : (load ? X86_FORM_MOV_R64_RM64 : X86_FORM_MOV_RM64_R64);
    tier0_put(writer, tier0_instruction(form, (uint8_t)(reg & 15), X86_REGISTER_RSP, BUSTER_X86_MEMORY_BASE, (int32_t)offset, 0));
}

// A register move, general (64-bit) or XMM (movaps).
static void tier0_move(Tier0Writer* writer, uint32_t to, uint32_t from)
{
    const bool xmm = to >= 16;
    tier0_put(writer, tier0_instruction(xmm ? X86_FORM_MOVAPS_X_XM : X86_FORM_MOV_R64_RM64, (uint8_t)(to & 15), (uint8_t)(from & 15), 0, 0, 0));
}

// Moves `count` registers at once (to[k] <- from[k], each in the register set's numbering: XMM registers from 16): the
// moves whose destination no other move still reads go first; a cycle of general registers breaks with an xchg. The
// destinations are distinct, and so are the sources. `to` and `from` are changed.
static void tier0_parallel_moves(Tier0Writer* writer, uint8_t* to, uint8_t* from, uint32_t count)
{
    uint32_t pending = count;
    while (pending != 0)
    {
        bool progress = false;
        for (uint32_t k = 0; k < pending; k += 1)
        {
            bool read = false;
            for (uint32_t j = 0; j < pending; j += 1)
            {
                read |= j != k && from[j] == to[k];
            }
            if (!read)
            {
                if (to[k] != from[k])
                {
                    tier0_move(writer, to[k], from[k]);
                }
                to[k] = to[pending - 1];
                from[k] = from[pending - 1];
                pending -= 1;
                progress = true;
                k = pending;
            }
        }
        if (!progress)
        {
            // Every destination is another move's source: a cycle (or several). xchg the first move's two registers:
            // its destination is then done, and the move that read its destination reads its source instead.
            const uint32_t a = to[0];
            const uint32_t b = from[0];
            tier0_put(writer, tier0_instruction(X86_FORM_XCHG_RM64_R64, (uint8_t)b, (uint8_t)a, 0, 0, 0));
            for (uint32_t j = 1; j < pending; j += 1)
            {
                from[j] = from[j] == a ? (uint8_t)b : from[j];
            }
            to[0] = to[pending - 1];
            from[0] = from[pending - 1];
            pending -= 1;
        }
    }
}

// A narrow integer extended to 32 bits in its register, as the convention has the caller do for an argument.
static void tier0_extend(Tier0Writer* writer, uint32_t class, uint32_t reg)
{
    if (tier0_class_integer(class) && tier0_class_bytes(class) < 4)
    {
        tier0_put(writer, tier0_instruction(tier0_load_form(class), (uint8_t)reg, (uint8_t)reg, 0, 0, 0));
    }
}

// A call: the depths below its arguments saved, the stack arguments stored, the register ones moved into place (al for
// a variadic call), the call, the result moved to its depth, and the saved depths restored.
static void tier0_emit_call(const Tier0Function* function, const Tier0Entries* entries, const Tier0Frame* frame, uint32_t i, Tier0Writer* writer)
{
    const uint32_t call = (uint32_t)function->values[i];
    const uint32_t first = function->call_first[call];
    const uint32_t count = function->call_argument_counts[call];
    uint32_t integer_arguments = 0;
    uint32_t floating_arguments = 0;
    for (uint32_t a = 0; a < count; a += 1)
    {
        const bool floating = tier0_class_floating(function->argument_classes[first + a]);
        integer_arguments += floating ? 0 : 1;
        floating_arguments += floating ? 1 : 0;
    }
    const uint32_t below = entries->depths[i] - integer_arguments;
    const uint32_t floating_below = entries->floating_depths[i] - floating_arguments;
    // The depths below the arguments in callee-saved registers survive the call by themselves.
    const uint32_t saved = below < BUSTER_TIER0_CALLER_SAVED_DEPTHS ? below : BUSTER_TIER0_CALLER_SAVED_DEPTHS;
    for (uint32_t d = 0; d < saved; d += 1)
    {
        tier0_spill(writer, tier0_integer_depths[d], frame->save_area + 8 * d, false);
    }
    for (uint32_t d = 0; d < floating_below; d += 1)
    {
        tier0_spill(writer, BUSTER_TIER0_XMM(d), frame->save_area + 8 * (saved + d), false);
    }
    uint8_t to[BUSTER_TIER0_MAX_CALL_ARGUMENTS];
    uint8_t from[BUSTER_TIER0_MAX_CALL_ARGUMENTS];
    uint32_t moves = 0;
    uint32_t integer = 0;
    uint32_t floating = 0;
    for (uint32_t a = 0; a < count; a += 1)
    {
        const uint32_t class = function->argument_classes[first + a];
        const AbiLocation location = function->argument_locations[first + a];
        const bool is_floating = tier0_class_floating(class);
        const uint32_t source = is_floating ? BUSTER_TIER0_XMM(floating_below + floating) : tier0_integer_depths[below + integer];
        integer += is_floating ? 0 : 1;
        floating += is_floating ? 1 : 0;
        tier0_extend(writer, class, source);
        if (location.kind == BUSTER_ABI_LOCATION_STACK)
        {
            const uint32_t form = is_floating ? (class == BUSTER_TYPED_CLASS_F32 ? X86_FORM_MOVSS_XM32_X : X86_FORM_MOVSD_XM64_X) : X86_FORM_MOV_RM64_R64;
            tier0_put(writer, tier0_instruction(form, (uint8_t)(source & 15), X86_REGISTER_RSP, BUSTER_X86_MEMORY_BASE, (int32_t)location.stack_offset, 0));
        }
        else
        {
            const uint8_t reg = location.registers[0];
            to[moves] = (uint8_t)((reg & BUSTER_ABI_REGISTER_VECTOR) != 0 ? BUSTER_TIER0_XMM(reg & 0x1f) : reg);
            from[moves] = (uint8_t)source;
            moves += 1;
        }
    }
    tier0_parallel_moves(writer, to, from, moves);
    if (function->call_variadic[call])
    {
        tier0_put(writer, tier0_instruction(X86_FORM_MOV_R32_IMM32, 0, X86_REGISTER_RAX, 0, 0, function->calls[call].vector_registers));
    }
    if (writer->records != 0)
    {
        writer->references[writer->reference_count] = (LinkReference){
            .instruction = writer->count,
            .field = X86_FIXUP_DISPLACEMENT,
            .kind = BUSTER_LINK_FIXUP_CALL,
            .target = function->call_targets[call],
            .addend = 0,
        };
    }
    writer->reference_count += 1;
    tier0_put(writer, tier0_instruction(X86_FORM_CALL_REL32 | BUSTER_X86_FORM_FIXUP, 0, 0, 0, 0, 0));
    const uint32_t result = tier0_stack(function->classes[i]);
    if (result == 1 && below != 0)
    {
        tier0_move(writer, tier0_integer_depths[below], X86_REGISTER_RAX);
    }
    else if (result == 2 && floating_below != 0)
    {
        tier0_move(writer, BUSTER_TIER0_XMM(floating_below), BUSTER_TIER0_XMM(0));
    }
    for (uint32_t d = 0; d < saved; d += 1)
    {
        tier0_spill(writer, tier0_integer_depths[d], frame->save_area + 8 * d, true);
    }
    for (uint32_t d = 0; d < floating_below; d += 1)
    {
        tier0_spill(writer, BUSTER_TIER0_XMM(d), frame->save_area + 8 * (saved + d), true);
    }
}

// Division with its operands where rax and rdx hold other values: those saved in the fixed slots, the dividend moved
// into rax, the divisor read from its register, its saved slot or its home; the quotient or remainder moved back.
static void tier0_emit_division(const Tier0Function* function, const Tier0Entries* entries, const Tier0Frame* frame, const Tier0Home* homes, uint32_t i,
                                Tier0Writer* writer)
{
    const uint32_t kind = entries->key_kinds[i];
    const uint32_t class = entries->key_classes[i];
    const bool home = entries->variants[i] == BUSTER_TIER0_VARIANT_HOME;
    const uint32_t depth = entries->depths[i];
    const bool wide = tier0_class_bytes(class) == 8;
    const bool is_signed = tier0_class_signed(class);
    const uint32_t dividend = tier0_integer_depths[depth - (home ? 1 : 2)];
    const uint32_t divisor = home ? 0 : tier0_integer_depths[depth - 1];
    // The stack's registers below the entry's depth hold live values (the operands among them).
    bool live[16] = { false };
    for (uint32_t d = 0; d < depth; d += 1)
    {
        live[tier0_integer_depths[d]] = true;
    }
    const bool save_rax = live[X86_REGISTER_RAX] && dividend != X86_REGISTER_RAX;
    const bool save_rdx = live[X86_REGISTER_RDX] && dividend != X86_REGISTER_RDX;
    const uint32_t rax_slot = frame->fixed_area;
    const uint32_t rdx_slot = frame->fixed_area + 8;
    if (save_rax)
    {
        tier0_spill(writer, X86_REGISTER_RAX, rax_slot, false);
    }
    if (save_rdx)
    {
        tier0_spill(writer, X86_REGISTER_RDX, rdx_slot, false);
    }
    if (dividend != X86_REGISTER_RAX)
    {
        tier0_move(writer, X86_REGISTER_RAX, dividend);
    }
    if (is_signed)
    {
        tier0_put(writer, tier0_instruction(wide ? X86_FORM_CQO : X86_FORM_CDQ, 0, 0, 0, 0, 0));
    }
    else
    {
        tier0_put(writer, tier0_instruction(X86_FORM_XOR_R32_RM32, X86_REGISTER_RDX, X86_REGISTER_RDX, 0, 0, 0));
    }
    const uint32_t form = is_signed ? (wide ? X86_FORM_IDIV_RM64 : X86_FORM_IDIV_RM32) : (wide ? X86_FORM_DIV_RM64 : X86_FORM_DIV_RM32);
    X86Instruction divide = tier0_instruction(form, 0, (uint8_t)divisor, 0, 0, 0);
    if (home)
    {
        const Tier0Home a = homes[function->values[i - 1]];
        divide.rm = a.reg;
        divide.mode = a.mode;
        divide.displacement = a.displacement;
    }
    else if (divisor == X86_REGISTER_RAX || divisor == X86_REGISTER_RDX)
    {
        divide.rm = X86_REGISTER_RSP;
        divide.mode = BUSTER_X86_MEMORY_BASE;
        divide.displacement = (int32_t)(divisor == X86_REGISTER_RAX ? rax_slot : rdx_slot);
    }
    tier0_put(writer, divide);
    const uint32_t result = kind == BUSTER_TYPED_MOD ? X86_REGISTER_RDX : X86_REGISTER_RAX;
    if (dividend != result)
    {
        tier0_move(writer, dividend, result);
    }
    if (save_rax)
    {
        tier0_spill(writer, X86_REGISTER_RAX, rax_slot, true);
    }
    if (save_rdx)
    {
        tier0_spill(writer, X86_REGISTER_RDX, rdx_slot, true);
    }
}

// A shift by a register count that isn't in rcx: with the value in rcx, the two swapped around the shift; otherwise
// rcx saved in a fixed slot, the count moved into it, and restored after.
static void tier0_emit_shift(const Tier0Entries* entries, const Tier0Frame* frame, uint32_t i, Tier0Writer* writer)
{
    const uint32_t class = entries->key_classes[i];
    const bool wide = tier0_class_bytes(class) == 8;
    const bool right = entries->key_kinds[i] == BUSTER_TYPED_SHR;
    const bool is_signed = tier0_class_signed(class);
    const uint32_t depth = entries->depths[i];
    const uint32_t value = tier0_integer_depths[depth - 2];
    const uint32_t count = tier0_integer_depths[depth - 1];
    const uint32_t form = !right ? (wide ? X86_FORM_SHL_RM64_CL : X86_FORM_SHL_RM32_CL)
                          : is_signed ? (wide ? X86_FORM_SAR_RM64_CL : X86_FORM_SAR_RM32_CL)
                                      : (wide ? X86_FORM_SHR_RM64_CL : X86_FORM_SHR_RM32_CL);
    if (value == X86_REGISTER_RCX)
    {
        tier0_put(writer, tier0_instruction(X86_FORM_XCHG_RM64_R64, X86_REGISTER_RCX, (uint8_t)count, 0, 0, 0));
        tier0_put(writer, tier0_instruction(form, 0, (uint8_t)count, 0, 0, 0));
        tier0_put(writer, tier0_instruction(X86_FORM_XCHG_RM64_R64, X86_REGISTER_RCX, (uint8_t)count, 0, 0, 0));
    }
    else
    {
        tier0_spill(writer, X86_REGISTER_RCX, frame->fixed_area, false);
        tier0_put(writer, tier0_instruction(X86_FORM_MOV_R32_RM32, X86_REGISTER_RCX, (uint8_t)count, 0, 0, 0));
        tier0_put(writer, tier0_instruction(form, 0, (uint8_t)value, 0, 0, 0));
        tier0_spill(writer, X86_REGISTER_RCX, frame->fixed_area, true);
    }
}

// The records of a deferred entry (into `writer`, which counts them when it has no records).
static void tier0_emit_deferred(const Tier0Function* function, const Tier0Entries* entries, const Tier0Frame* frame, const Tier0Home* homes, uint32_t i,
                                Tier0Writer* writer)
{
    const uint32_t kind = entries->key_kinds[i];
    if (kind == BUSTER_TYPED_CALL)
    {
        tier0_emit_call(function, entries, frame, i, writer);
    }
    else if (kind == BUSTER_TYPED_DIV || kind == BUSTER_TYPED_MOD)
    {
        tier0_emit_division(function, entries, frame, homes, i, writer);
    }
    else
    {
        tier0_emit_shift(entries, frame, i, writer);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// The prologue and the epilogue

// The prologue: the callee-saved registers the homes use pushed, the frame reserved, and the parameters moved from where
// they arrive to their homes (none for one that stays, or that arrives on the stack).
static void tier0_emit_prologue(const Tier0Function* function, const Tier0Frame* frame, const Tier0Home* homes, const uint32_t* references, Tier0Writer* writer)
{
    for (uint32_t c = 0; c < BUSTER_TIER0_CALLEE_SAVED; c += 1)
    {
        if ((frame->saved & 1u << c) != 0)
        {
            tier0_put(writer, tier0_instruction(X86_FORM_PUSH_R64, 0, tier0_callee_saved[c], 0, 0, 0));
        }
    }
    if (frame->size != 0)
    {
        const uint32_t form = frame->size <= 127 ? X86_FORM_SUB_RM64_IMM8 : X86_FORM_SUB_RM64_IMM32;
        tier0_put(writer, tier0_instruction(form, 0, X86_REGISTER_RSP, 0, 0, (int32_t)frame->size));
    }
    for (uint32_t p = 0; p < function->parameter_count; p += 1)
    {
        const AbiLocation location = function->parameters[p];
        const uint32_t class = function->local_classes[p];
        const bool address_taken = (function->local_flags[p] & BUSTER_TYPED_LOCAL_ADDRESS_TAKEN) != 0;
        if (location.kind == BUSTER_ABI_LOCATION_REGISTERS && (references[p] != 0 || address_taken))
        {
            const uint8_t arrival = location.registers[0];
            const bool floating = (arrival & BUSTER_ABI_REGISTER_VECTOR) != 0;
            const uint8_t from = (uint8_t)(arrival & 0x1f);
            const Tier0Home home = homes[p];
            if (home.is_register && home.reg != from)
            {
                tier0_move(writer, floating ? BUSTER_TIER0_XMM(home.reg) : home.reg, floating ? BUSTER_TIER0_XMM(from) : from);
            }
            else if (!home.is_register)
            {
                tier0_put(writer, tier0_instruction(tier0_store_form(class), from, home.reg, home.mode, home.displacement, 0));
            }
        }
    }
}

// The epilogue: the frame released, the saved registers popped, ret.
static void tier0_emit_epilogue(const Tier0Frame* frame, Tier0Writer* writer)
{
    if (frame->size != 0)
    {
        const uint32_t form = frame->size <= 127 ? X86_FORM_ADD_RM64_IMM8 : X86_FORM_ADD_RM64_IMM32;
        tier0_put(writer, tier0_instruction(form, 0, X86_REGISTER_RSP, 0, 0, (int32_t)frame->size));
    }
    for (uint32_t c = BUSTER_TIER0_CALLEE_SAVED; c > 0; c -= 1)
    {
        if ((frame->saved & 1u << (c - 1)) != 0)
        {
            tier0_put(writer, tier0_instruction(X86_FORM_POP_R64, 0, tier0_callee_saved[c - 1], 0, 0, 0));
        }
    }
    tier0_put(writer, tier0_instruction(X86_FORM_RET, 0, 0, 0, 0, 0));
}

// ---------------------------------------------------------------------------------------------------------------------
// The passes after expansion

// A CHOICE record's shortest form that keeps its immediate (`value`) and the flags its template reads after it: the
// record rebuilt for the chosen form (x86_choice_instruction), its fixup flag kept. Its context: whether its rm is a
// register (and rax), inc and dec allowed, and the live flags.
static uint16_t tier0_choice_context(const X86Instruction* record, const Tier0RecordSpec* spec)
{
    const bool reg = record->mode == BUSTER_X86_REGISTER_DIRECT;
    uint32_t context = BUSTER_X86_CONTEXT_INC_DEC | spec->live;
    context |= reg ? BUSTER_X86_CONTEXT_REGISTER : 0;
    context |= reg && record->rm == X86_REGISTER_RAX ? BUSTER_X86_CONTEXT_ACCUMULATOR : 0;
    return (uint16_t)context;
}

static int64_t tier0_choice_value(const Tier0RecordSpec* spec, uint64_t k)
{
    return spec->immediate_kind == BUSTER_TIER0_IMMEDIATE_K ? (int64_t)k : (int64_t)spec->immediate;
}

static void tier0_apply_choice(X86Instruction* record, uint32_t form, int64_t value)
{
    if (form != X86_FORM_NONE)
    {
        const uint16_t fixup = (uint16_t)(record->form & BUSTER_X86_FORM_FIXUP);
        *record = x86_choice_instruction(form, *record, value);
        record->form |= fixup;
    }
}

// The template of an entry once the homes are known: a home-dependent slot's case for its homes.
static uint32_t tier0_resolve_template(const Tier0Function* function, const Tier0Home* homes, uint32_t i, uint16_t slot)
{
    uint32_t index = slot & BUSTER_TIER0_SLOT_TEMPLATE;
    if ((slot & BUSTER_TIER0_SLOT_HOME_DEPENDENT) != 0)
    {
        const Tier0Template* first = &tier0_templates[index];
        const uint8_t kind = function->kinds[i];
        const bool own = kind == BUSTER_TYPED_LOCAL || kind == BUSTER_TYPED_LOCAL_ADDRESS || kind == BUSTER_TYPED_STORE_LOCAL || kind == BUSTER_TYPED_SET_LOCAL;
        const bool a_register = first->a_offset != BUSTER_TIER0_NO_OFFSET && homes[function->values[i - first->a_offset]].is_register;
        const bool b_register = own && homes[function->values[i]].is_register;
        index += (a_register ? 1u : 0u) + (b_register ? 2u : 0u);
    }
    return index;
}

// The count of branch and fixup records of the body's templates, for the output's columns.
static void tier0_count_columns(const Tier0Function* function, const Tier0Entries* entries, uint32_t* branches, uint32_t* references)
{
    uint32_t branch_count = 0;
    uint32_t reference_count = 0;
    for (uint32_t i = 0; i < function->count; i += 1)
    {
        const uint16_t entry = entries->templates[i];
        if (entry == BUSTER_TIER0_ENTRY_DEFERRED || entry == BUSTER_TIER0_SLOT_DEFERRED)
        {
            reference_count += function->kinds[i] == BUSTER_TYPED_CALL ? 1 : 0;
        }
        else if (entry != BUSTER_TIER0_ENTRY_ABSORBED)
        {
            const Tier0Template* template = &tier0_templates[entry];
            for (uint32_t r = 0; r < template->length; r += 1)
            {
                branch_count += (template->specs[r].flags & (BUSTER_TIER0_RECORD_BRANCH | BUSTER_TIER0_RECORD_END)) != 0 ? 1 : 0;
                reference_count += (template->specs[r].flags & BUSTER_TIER0_RECORD_FIXUP) != 0 ? 1 : 0;
            }
        }
    }
    *branches = branch_count;
    *references = reference_count;
}

// The deferred entries are those whose template column says so (a deferred slot, or a template at the wrong depth).
static bool tier0_is_deferred(uint16_t entry)
{
    return entry == BUSTER_TIER0_ENTRY_DEFERRED || entry == BUSTER_TIER0_SLOT_DEFERRED;
}

// What both paths share once the per-entry columns are filled: the frame's touches, the homes, the frame, each entry's
// resolved template and length (a deferred entry's counted by its emitter), the prologue's length, and the output's
// columns pushed from `output`. Returns the error, with code->count and the columns set on success.
typedef struct Tier0Shared Tier0Shared;
struct Tier0Shared
{
    Tier0Frame frame;
    Tier0Home* homes;
    uint32_t* references;
    uint32_t prologue;
    uint32_t body;
};

static Tier0Error tier0_prepare(const Tier0Function* function, Tier0Entries* entries, Tier0Shared* shared, Arena* scratch)
{
    Tier0Error error = tier0_touches(function, entries, &shared->frame);
    const uint32_t locals = function->local_count;
    shared->homes = BUSTER_ARENA_PUSH_ARRAY(scratch, Tier0Home, locals + 1);
    shared->references = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, locals + 1);
    uint32_t* order = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, locals + 1);
    if (error == TIER0_OK && (shared->homes == 0 || shared->references == 0 || order == 0))
    {
        error = TIER0_ERROR_MEMORY;
    }
    if (error == TIER0_OK)
    {
        tier0_assign_homes(function, &shared->frame, shared->homes, shared->references, order);
        tier0_layout_frame(function, &shared->frame, shared->homes, shared->references);
        for (uint32_t i = 0; i < function->count; i += 1)
        {
            const uint16_t entry = entries->templates[i];
            if (entry != BUSTER_TIER0_ENTRY_ABSORBED && !tier0_is_deferred(entry))
            {
                entries->templates[i] = (uint16_t)tier0_resolve_template(function, shared->homes, i, entry);
            }
        }
        Tier0Writer counter = { 0, 0, 0, 0 };
        tier0_emit_prologue(function, &shared->frame, shared->homes, shared->references, &counter);
        shared->prologue = counter.count;
    }
    return error;
}

// Each entry's length: its template's, a deferred one's as its emitter counts it, none for an absorbed one.
static void tier0_lengths_scalar(const Tier0Function* function, Tier0Entries* entries, const Tier0Shared* shared)
{
    for (uint32_t i = 0; i < function->count; i += 1)
    {
        const uint16_t entry = entries->templates[i];
        uint32_t length = 0;
        if (tier0_is_deferred(entry))
        {
            Tier0Writer counter = { 0, 0, 0, 0 };
            tier0_emit_deferred(function, entries, &shared->frame, shared->homes, i, &counter);
            length = counter.count;
        }
        else if (entry != BUSTER_TIER0_ENTRY_ABSORBED)
        {
            length = tier0_templates[entry].length;
        }
        entries->lengths[i] = (uint16_t)length;
    }
}

// Each entry's first record, after the prologue (the entry past the last is the epilogue's first record).
static void tier0_positions_scalar(const Tier0Function* function, Tier0Entries* entries, uint32_t prologue)
{
    uint32_t position = prologue;
    for (uint32_t i = 0; i < function->count; i += 1)
    {
        entries->positions[i] = position;
        position += entries->lengths[i];
    }
    entries->positions[function->count] = position;
}

// The output's columns, pushed from `output`: room for the records and, past them, the vector expansion's last store.
static bool tier0_push_output(Tier0Code* code, Arena* output, uint32_t records, uint32_t branches, uint32_t references)
{
    code->records = BUSTER_ARENA_PUSH_ARRAY(output, X86Instruction, (uint64_t)records + BUSTER_TIER0_TEMPLATE_RECORDS);
    code->branches = BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, (uint64_t)branches + 1);
    code->targets = BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, (uint64_t)branches + 1);
    code->references = BUSTER_ARENA_PUSH_ARRAY(output, LinkReference, (uint64_t)references + 1);
    code->count = records;
    return code->records != 0 && code->branches != 0 && code->targets != 0 && code->references != 0;
}

// The epilogue, the deferred entries' records and the branch columns and references, in entry order, for either path
// (the templates' records are written already, and so are their choices).
static void tier0_finish(const Tier0Function* function, const Tier0Entries* entries, const Tier0Shared* shared, Tier0Code* code)
{
    Tier0Writer writer = { code->records, 0, code->references, 0 };
    tier0_emit_prologue(function, &shared->frame, shared->homes, shared->references, &writer);
    uint32_t branches = 0;
    for (uint32_t i = 0; i < function->count; i += 1)
    {
        const uint16_t entry = entries->templates[i];
        if (tier0_is_deferred(entry))
        {
            writer.count = entries->positions[i];
            tier0_emit_deferred(function, entries, &shared->frame, shared->homes, i, &writer);
            code->deferred += 1;
        }
        else if (entry != BUSTER_TIER0_ENTRY_ABSORBED)
        {
            const Tier0Template* template = &tier0_templates[entry];
            for (uint32_t r = 0; r < template->length; r += 1)
            {
                const uint8_t flags = template->specs[r].flags;
                if ((flags & (BUSTER_TIER0_RECORD_BRANCH | BUSTER_TIER0_RECORD_END)) != 0)
                {
                    const uint32_t target = (flags & BUSTER_TIER0_RECORD_END) != 0 ? function->count : (uint32_t)function->values[i];
                    code->branches[branches] = entries->positions[i] + r;
                    code->targets[branches] = entries->positions[target];
                    branches += 1;
                }
                if ((flags & BUSTER_TIER0_RECORD_FIXUP) != 0)
                {
                    code->references[writer.reference_count] = (LinkReference){
                        .instruction = entries->positions[i] + r,
                        .field = X86_FIXUP_DISPLACEMENT,
                        .kind = BUSTER_LINK_FIXUP_PC32,
                        .target = (SymbolId)function->values[i],
                        .addend = 0,
                    };
                    writer.reference_count += 1;
                }
            }
        }
    }
    writer.count = entries->positions[function->count];
    tier0_emit_epilogue(&shared->frame, &writer);
    code->branch_count = branches;
    code->reference_count = writer.reference_count;
}

// The per-entry columns, from the scratch arena.
static bool tier0_push_entries(Tier0Entries* entries, Arena* scratch, uint32_t count)
{
    const uint64_t padded = (uint64_t)count + 64;
    entries->key_kinds = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, padded);
    entries->key_classes = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, padded);
    entries->variants = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, padded);
    entries->absorbed = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, padded);
    entries->integer_effects = BUSTER_ARENA_PUSH_ARRAY(scratch, int8_t, padded);
    entries->floating_effects = BUSTER_ARENA_PUSH_ARRAY(scratch, int8_t, padded);
    entries->depths = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, padded);
    entries->floating_depths = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, padded);
    entries->templates = BUSTER_ARENA_PUSH_ARRAY(scratch, uint16_t, padded);
    entries->lengths = BUSTER_ARENA_PUSH_ARRAY(scratch, uint16_t, padded);
    entries->positions = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, padded);
    return entries->key_kinds != 0 && entries->key_classes != 0 && entries->variants != 0 && entries->absorbed != 0 && entries->integer_effects != 0 &&
           entries->floating_effects != 0 && entries->depths != 0 && entries->floating_depths != 0 && entries->templates != 0 && entries->lengths != 0 &&
           entries->positions != 0;
}

// The scalar expansion and choice: each template's records from its specifications, then each CHOICE record's form.
static void tier0_expand_scalar(const Tier0Function* function, const Tier0Entries* entries, const Tier0Shared* shared, Tier0Code* code)
{
    for (uint32_t i = 0; i < function->count; i += 1)
    {
        const uint16_t entry = entries->templates[i];
        if (entry != BUSTER_TIER0_ENTRY_ABSORBED && !tier0_is_deferred(entry))
        {
            const Tier0Template* template = &tier0_templates[entry];
            const Tier0Holes holes = tier0_holes_scalar(function, entries, shared->homes, i, template);
            for (uint32_t r = 0; r < template->length; r += 1)
            {
                const Tier0RecordSpec* spec = &template->specs[r];
                X86Instruction record = tier0_record_scalar(spec, &holes);
                if ((spec->flags & BUSTER_TIER0_RECORD_CHOICE) != 0)
                {
                    const int64_t value = tier0_choice_value(spec, holes.k);
                    const uint16_t context = tier0_choice_context(&record, spec);
                    uint16_t form = X86_FORM_NONE;
                    x86_choose_forms_scalar(&spec->row, &value, &context, 1, &form);
                    tier0_apply_choice(&record, form, value);
                }
                code->records[entries->positions[i] + r] = record;
            }
        }
    }
}

static Tier0Code tier0_compile_scalar(const Tier0Function* function, Arena* output)
{
    Tier0Code code;
    memset(&code, 0, sizeof(code));
    const uint64_t output_length = output->length;
    Scratch scratch = scratch_begin(output);
    Tier0Entries entries;
    const uint32_t count = function->count;
    uint64_t* labels = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint64_t, (uint64_t)count / 64 + 2);
    Tier0Error error = labels != 0 && tier0_push_entries(&entries, scratch.arena, count) ? TIER0_OK : TIER0_ERROR_MEMORY;
    Tier0Shared shared;
    memset(&shared, 0, sizeof(shared));
    if (error == TIER0_OK)
    {
        memset(labels, 0, ((uint64_t)count / 64 + 2) * sizeof(uint64_t));
        tier0_labels(function, labels);
        tier0_fuse_scalar(function, labels, &entries);
        tier0_depths_scalar(function, &entries);
        const uint32_t unsupported = tier0_templates_scalar(function, &entries);
        if (unsupported != count)
        {
            error = TIER0_ERROR_UNSUPPORTED;
            code.node = unsupported;
        }
    }
    error = error == TIER0_OK ? tier0_prepare(function, &entries, &shared, scratch.arena) : error;
    if (error == TIER0_OK)
    {
        tier0_lengths_scalar(function, &entries, &shared);
        tier0_positions_scalar(function, &entries, shared.prologue);
        Tier0Writer counter = { 0, 0, 0, 0 };
        tier0_emit_epilogue(&shared.frame, &counter);
        uint32_t branches = 0;
        uint32_t references = 0;
        tier0_count_columns(function, &entries, &branches, &references);
        if (!tier0_push_output(&code, output, entries.positions[count] + counter.count, branches, references))
        {
            error = TIER0_ERROR_MEMORY;
        }
    }
    if (error == TIER0_OK)
    {
        tier0_expand_scalar(function, &entries, &shared, &code);
        tier0_finish(function, &entries, &shared, &code);
    }
    if (error != TIER0_OK)
    {
        arena_pop_to(output, output_length);
        const uint32_t node = code.node;
        memset(&code, 0, sizeof(code));
        code.node = node;
    }
    code.error = error;
    scratch_end(scratch);
    return code;
}

// ---------------------------------------------------------------------------------------------------------------------
// Validation

// The stream's rules, checked by running it as a stack machine over one stack of classes (in push order, which the two
// register stacks project), with a hash of the stack recorded at each target when control first reaches it and
// compared on every other arrival.
typedef struct Tier0Validator Tier0Validator;
struct Tier0Validator
{
    uint8_t* stack;
    uint32_t depth;
    bool reachable;
    Tier0Check check;
    // Each target's stack when control first reached it (its depth, and where its copy is in `saved`), for the code
    // only jumps reach.
    uint32_t* saved_depths;
    uint64_t* saved_offsets;
    Arena* saved;
};

static void tier0_reject(Tier0Validator* validator, uint32_t node, const char* rule)
{
    if (validator->check.error == TIER0_OK)
    {
        validator->check.error = TIER0_ERROR_INVALID;
        validator->check.node = node;
        validator->check.rule = rule;
    }
}

static uint32_t tier0_pop(Tier0Validator* validator, uint32_t node)
{
    uint32_t class = BUSTER_TYPED_CLASS_VOID;
    if (validator->depth == 0)
    {
        tier0_reject(validator, node, "an operand the stack doesn't have");
    }
    else
    {
        validator->depth -= 1;
        class = validator->stack[validator->depth];
    }
    return class;
}

static void tier0_push(Tier0Validator* validator, uint32_t class)
{
    validator->stack[validator->depth] = (uint8_t)class;
    validator->depth += 1;
}

static uint64_t tier0_stack_hash(const Tier0Validator* validator)
{
    uint64_t hash = 0xcbf29ce484222325ull ^ validator->depth;
    for (uint32_t d = 0; d < validator->depth; d += 1)
    {
        hash = (hash ^ validator->stack[d]) * 0x100000001b3ull;
    }
    return hash;
}

// Records the stack at a target (its hash and a copy), or compares it with the one recorded (`hashes` and `seen` per
// entry, and the end).
static void tier0_arrive(Tier0Validator* validator, uint64_t* hashes, uint8_t* seen, uint32_t target, uint32_t node)
{
    const uint64_t hash = tier0_stack_hash(validator);
    if (seen[target] == 0)
    {
        hashes[target] = hash;
        seen[target] = 1;
        uint8_t* copy = BUSTER_ARENA_PUSH_ARRAY(validator->saved, uint8_t, (uint64_t)validator->depth + 1);
        if (copy == 0)
        {
            tier0_reject(validator, node, "no scratch memory");
        }
        else
        {
            memcpy(copy, validator->stack, validator->depth);
            validator->saved_depths[target] = validator->depth;
            validator->saved_offsets[target] = (uint64_t)(copy - validator->saved->base);
        }
    }
    else if (hashes[target] != hash)
    {
        tier0_reject(validator, node, "a target reached with different stacks");
    }
}

static Tier0Check tier0_validate(const Tier0Function* function)
{
    Tier0Validator validator;
    memset(&validator, 0, sizeof(validator));
    validator.reachable = true;
    const uint32_t count = function->count;
    Scratch scratch = scratch_begin(0);
    validator.stack = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint8_t, (uint64_t)count + 1);
    uint64_t* hashes = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint64_t, (uint64_t)count + 1);
    uint8_t* seen = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint8_t, (uint64_t)count + 1);
    uint64_t* labels = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint64_t, (uint64_t)count / 64 + 2);
    validator.saved_depths = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint32_t, (uint64_t)count + 1);
    validator.saved_offsets = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint64_t, (uint64_t)count + 1);
    validator.saved = scratch.arena;
    if (validator.stack == 0 || hashes == 0 || seen == 0 || labels == 0 || validator.saved_depths == 0 || validator.saved_offsets == 0)
    {
        tier0_reject(&validator, 0, "no scratch memory");
    }
    else if (count > BUSTER_TIER0_MAX_ENTRIES || function->local_count > BUSTER_TIER0_MAX_LOCALS || function->parameter_count > function->local_count)
    {
        tier0_reject(&validator, 0, "more entries or locals than Tier 0 takes, or more parameters than locals");
    }
    else
    {
        memset(seen, 0, (uint64_t)count + 1);
        memset(labels, 0, ((uint64_t)count / 64 + 2) * sizeof(uint64_t));
        for (uint32_t i = 0; i < count; i += 1)
        {
            const uint8_t kind = function->kinds[i];
            if (kind >= BUSTER_TYPED_BRANCH_FALSE && kind <= BUSTER_TYPED_JUMP_VALUE)
            {
                if (function->values[i] > count)
                {
                    tier0_reject(&validator, i, "a target past the function's end");
                }
                else
                {
                    labels[function->values[i] >> 6] |= 1ull << (function->values[i] & 63);
                }
            }
        }
        for (uint32_t l = 0; l < function->local_count; l += 1)
        {
            if (function->local_classes[l] == BUSTER_TYPED_CLASS_VOID || function->local_classes[l] >= BUSTER_TYPED_CLASSES)
            {
                tier0_reject(&validator, 0, "a local without a class");
            }
        }
        for (uint32_t p = 0; p < function->parameter_count; p += 1)
        {
            const AbiLocation location = function->parameters[p];
            const bool floating = tier0_class_floating(function->local_classes[p]);
            const bool vector = (location.registers[0] & BUSTER_ABI_REGISTER_VECTOR) != 0;
            if (location.kind != BUSTER_ABI_LOCATION_STACK && (location.kind != BUSTER_ABI_LOCATION_REGISTERS || floating != vector))
            {
                tier0_reject(&validator, 0, "a parameter's location doesn't fit its class");
            }
        }
        for (uint32_t c = 0; c < function->call_count; c += 1)
        {
            if ((uint64_t)function->call_first[c] + function->call_argument_counts[c] > function->argument_count ||
                function->call_argument_counts[c] > BUSTER_TIER0_MAX_CALL_ARGUMENTS)
            {
                tier0_reject(&validator, 0, "a call's arguments past the argument columns");
            }
        }
        uint32_t return_class = BUSTER_TIER0_ANY_DEPTH;
        for (uint32_t i = 0; i < count && validator.check.error == TIER0_OK; i += 1)
        {
            const uint8_t kind = function->kinds[i];
            const uint8_t class = function->classes[i];
            const uint64_t value = function->values[i];
            if (tier0_label(labels, i))
            {
                if (validator.reachable)
                {
                    tier0_arrive(&validator, hashes, seen, i, i);
                }
                else if (seen[i] != 0)
                {
                    // Only jumps reach it: the stack they recorded.
                    validator.depth = validator.saved_depths[i];
                    memcpy(validator.stack, validator.saved->base + validator.saved_offsets[i], validator.depth);
                    validator.reachable = true;
                }
            }
            if (!validator.reachable)
            {
                // Code no path reaches (after a jump or a return, before a target): at statement level.
                validator.depth = 0;
                validator.reachable = true;
            }
            if (kind == BUSTER_TYPED_NONE || kind >= BUSTER_TYPED_KINDS || class >= BUSTER_TYPED_CLASSES)
            {
                tier0_reject(&validator, i, "an unknown kind or class");
                break;
            }
            const uint32_t operand = i > 0 ? function->classes[i - 1] : BUSTER_TYPED_CLASS_VOID;
            switch (kind)
            {
            case BUSTER_TYPED_CONST:
            case BUSTER_TYPED_GLOBAL:
                if (class == BUSTER_TYPED_CLASS_VOID || (kind == BUSTER_TYPED_GLOBAL && (value == 0 || value > UINT32_MAX)))
                {
                    tier0_reject(&validator, i, "a leaf without a class, or a global without a symbol");
                }
                tier0_push(&validator, class);
                break;
            case BUSTER_TYPED_LOCAL:
            case BUSTER_TYPED_LOCAL_ADDRESS:
                if (value >= function->local_count || (kind == BUSTER_TYPED_LOCAL && function->local_classes[value] != class) ||
                    (kind == BUSTER_TYPED_LOCAL_ADDRESS && (class != BUSTER_TYPED_CLASS_U64 || (function->local_flags[value] & BUSTER_TYPED_LOCAL_ADDRESS_TAKEN) == 0)))
                {
                    tier0_reject(&validator, i, "a local reference that doesn't match its local");
                }
                tier0_push(&validator, class);
                break;
            case BUSTER_TYPED_GLOBAL_ADDRESS:
                if (class != BUSTER_TYPED_CLASS_U64 || value == 0 || value > UINT32_MAX)
                {
                    tier0_reject(&validator, i, "an address that isn't U64, or without a symbol");
                }
                tier0_push(&validator, class);
                break;
            case BUSTER_TYPED_DUP:
                if (validator.depth == 0 || validator.stack[validator.depth - 1] != class)
                {
                    tier0_reject(&validator, i, "DUP of a value it doesn't have");
                }
                tier0_push(&validator, class);
                break;
            case BUSTER_TYPED_NEG:
            case BUSTER_TYPED_NOT:
                if (tier0_pop(&validator, i) != class || class == BUSTER_TYPED_CLASS_VOID || (kind == BUSTER_TYPED_NOT && !tier0_class_integer(class)))
                {
                    tier0_reject(&validator, i, "NEG or NOT of another class");
                }
                tier0_push(&validator, class);
                break;
            case BUSTER_TYPED_CONVERT:
                if (tier0_pop(&validator, i) == BUSTER_TYPED_CLASS_VOID || class == BUSTER_TYPED_CLASS_VOID)
                {
                    tier0_reject(&validator, i, "CONVERT from or to VOID");
                }
                tier0_push(&validator, class);
                break;
            case BUSTER_TYPED_LOAD:
                if (tier0_pop(&validator, i) != BUSTER_TYPED_CLASS_U64 || class == BUSTER_TYPED_CLASS_VOID)
                {
                    tier0_reject(&validator, i, "LOAD from something that isn't U64");
                }
                tier0_push(&validator, class);
                break;
            case BUSTER_TYPED_PTR_ADD:
            {
                const uint32_t index = tier0_pop(&validator, i);
                const uint32_t pointer = tier0_pop(&validator, i);
                if (class != BUSTER_TYPED_CLASS_U64 || pointer != BUSTER_TYPED_CLASS_U64 || (index != BUSTER_TYPED_CLASS_I64 && index != BUSTER_TYPED_CLASS_U64) || value == 0)
                {
                    tier0_reject(&validator, i, "PTR_ADD's operands or size");
                }
                tier0_push(&validator, class);
                break;
            }
            case BUSTER_TYPED_STORE:
            case BUSTER_TYPED_SET:
            {
                const uint32_t stored = tier0_pop(&validator, i);
                const uint32_t address = tier0_pop(&validator, i);
                if (address != BUSTER_TYPED_CLASS_U64 || stored != class || class == BUSTER_TYPED_CLASS_VOID)
                {
                    tier0_reject(&validator, i, "a store's address or value");
                }
                if (kind == BUSTER_TYPED_STORE)
                {
                    tier0_push(&validator, class);
                }
                break;
            }
            case BUSTER_TYPED_STORE_LOCAL:
            case BUSTER_TYPED_SET_LOCAL:
            case BUSTER_TYPED_STORE_GLOBAL:
            case BUSTER_TYPED_SET_GLOBAL:
            {
                const bool local = kind == BUSTER_TYPED_STORE_LOCAL || kind == BUSTER_TYPED_SET_LOCAL;
                const bool bad_target = local ? value >= function->local_count || function->local_classes[value] != class : value == 0 || value > UINT32_MAX;
                if (tier0_pop(&validator, i) != class || bad_target)
                {
                    tier0_reject(&validator, i, "a store to a local or a global that doesn't match");
                }
                if (kind == BUSTER_TYPED_STORE_LOCAL || kind == BUSTER_TYPED_STORE_GLOBAL)
                {
                    tier0_push(&validator, class);
                }
                break;
            }
            case BUSTER_TYPED_CALL:
            {
                if (value >= function->call_count)
                {
                    tier0_reject(&validator, i, "a call past the calls table");
                    break;
                }
                const uint32_t first = function->call_first[value];
                const uint32_t arguments = function->call_argument_counts[value];
                for (uint32_t a = arguments; a > 0; a -= 1)
                {
                    if (tier0_pop(&validator, i) != function->argument_classes[first + a - 1])
                    {
                        tier0_reject(&validator, i, "a call's argument of another class");
                    }
                }
                if (class != BUSTER_TYPED_CLASS_VOID)
                {
                    tier0_push(&validator, class);
                }
                break;
            }
            case BUSTER_TYPED_DISCARD:
            case BUSTER_TYPED_JUMP_VALUE:
                if (tier0_pop(&validator, i) != class || class == BUSTER_TYPED_CLASS_VOID)
                {
                    tier0_reject(&validator, i, "DISCARD or JUMP_VALUE of another class");
                }
                if (kind == BUSTER_TYPED_JUMP_VALUE)
                {
                    tier0_push(&validator, class);
                    tier0_arrive(&validator, hashes, seen, (uint32_t)value, i);
                    validator.depth -= 1;
                    validator.reachable = false;
                }
                break;
            case BUSTER_TYPED_BRANCH_FALSE:
            case BUSTER_TYPED_BRANCH_TRUE:
                if (tier0_pop(&validator, i) != class || !tier0_class_integer(class))
                {
                    tier0_reject(&validator, i, "a branch on something that isn't an integer of its class");
                }
                tier0_arrive(&validator, hashes, seen, (uint32_t)value, i);
                break;
            case BUSTER_TYPED_JUMP:
                tier0_arrive(&validator, hashes, seen, (uint32_t)value, i);
                validator.reachable = false;
                break;
            case BUSTER_TYPED_RETURN:
                if (class != BUSTER_TYPED_CLASS_VOID && tier0_pop(&validator, i) != class)
                {
                    tier0_reject(&validator, i, "a return of another class");
                }
                if (validator.depth != 0 || (return_class != BUSTER_TIER0_ANY_DEPTH && return_class != class))
                {
                    tier0_reject(&validator, i, "a return with values left, or of two classes");
                }
                return_class = class;
                validator.reachable = false;
                break;
            default:
            {
                // The binary operations and the compares.
                const uint32_t last = tier0_pop(&validator, i);
                const uint32_t first = tier0_pop(&validator, i);
                const bool compare = kind >= BUSTER_TYPED_EQ && kind <= BUSTER_TYPED_GE;
                const bool shift = kind == BUSTER_TYPED_SHL || kind == BUSTER_TYPED_SHR;
                const bool integer_only = shift || kind == BUSTER_TYPED_MOD || kind == BUSTER_TYPED_AND || kind == BUSTER_TYPED_OR || kind == BUSTER_TYPED_XOR;
                bool valid = compare ? first == last && class == BUSTER_TYPED_CLASS_I32 && first != BUSTER_TYPED_CLASS_VOID
                                     : first == class && (shift ? tier0_class_integer(last) : last == class) && class != BUSTER_TYPED_CLASS_VOID;
                valid &= !integer_only || tier0_class_integer(class);
                valid &= compare || tier0_class_bytes(class) >= 4;
                if (!valid)
                {
                    tier0_reject(&validator, i, "a binary operation's operands");
                }
                (void)operand;
                tier0_push(&validator, class);
                break;
            }
            }
        }
        if (validator.check.error == TIER0_OK && validator.reachable && validator.depth != 0)
        {
            tier0_reject(&validator, count, "values left at the function's end");
        }
        if (validator.check.error == TIER0_OK && validator.reachable)
        {
            tier0_arrive(&validator, hashes, seen, count, count);
        }
    }
    scratch_end(scratch);
    return validator.check;
}

// ---------------------------------------------------------------------------------------------------------------------
// The vector path

static Tier0Code tier0_compile(const Tier0Function* function, Arena* output)
{
    return tier0_compile_scalar(function, output);
}
