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
#include <stddef.h>
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
//     (of `class`, or nothing for VOID). An indirect call (a calls table's target 0) pops, under its arguments, the
//     address it calls (U64), which was pushed before them.
//   - DISCARD: pops the operand (of `class`): an expression statement's value.
//   - BRANCH_FALSE and BRANCH_TRUE: pop the operand (an integer of `class`) and go to entry `value` if it is zero, or
//     isn't.
//   - JUMP: goes to entry `value`. JUMP_VALUE: the same, taking the operand (of `class`) there: an arm of ?: or of a
//     value's && and ||, whose value the entry `value` finds on the stack as the arm after it leaves its own.
//   - RETURN: returns the operand (of `class`), or nothing when `class` is VOID; at statement level (with nothing else
//     on the stacks).
//   - COPY: copies `value` bytes (1 to BUSTER_TIER0_MAX_COPY_BYTES) to the address the first operand (U64) holds from
//     the one the last (U64) holds, and pushes nothing (`class` VOID): a struct's assignment. The two ranges are the
//     same or apart. A front end copies more through memcpy, as clang -O0 does past this many bytes.
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
#define BUSTER_TYPED_COPY 41
#define BUSTER_TYPED_KINDS 42
// Every kind below 64, so that a kind table is one ZMM register for vpermb.
#define BUSTER_TYPED_KIND_LIMIT 64

// A local's flags: its address is taken (LOCAL_ADDRESS), so it lives in the frame. The three object roles below
// are mutually exclusive, nonparameter U64 descriptors with ADDRESS_TAKEN, usable only through LOCAL_ADDRESS.
// VA_LIST owns exactly sizeof(AbiVaList) bytes. The other roles alias the incoming variadic frame's two anchors.
#define BUSTER_TYPED_LOCAL_ADDRESS_TAKEN 0x01
#define BUSTER_TYPED_LOCAL_VA_LIST 0x02
#define BUSTER_TYPED_LOCAL_REGISTER_SAVE_AREA 0x04
#define BUSTER_TYPED_LOCAL_OVERFLOW_ARG_AREA 0x08
#define BUSTER_TYPED_LOCAL_ADDRESS_ONLY (BUSTER_TYPED_LOCAL_VA_LIST | BUSTER_TYPED_LOCAL_REGISTER_SAVE_AREA | BUSTER_TYPED_LOCAL_OVERFLOW_ARG_AREA)

// The most a function may have of each (the columns' indices are 32-bit), and of its objects' bytes, each object's
// size rounded up to 16, so that every frame displacement fits a disp32.
#define BUSTER_TIER0_MAX_ENTRIES (1u << 24)
#define BUSTER_TIER0_MAX_LOCALS (1u << 16)
#define BUSTER_TIER0_MAX_CALL_ARGUMENTS 64
#define BUSTER_TIER0_MAX_OBJECT_BYTES (1u << 30)
#define BUSTER_TIER0_MAX_OBJECT_ALIGNMENT_LOG2 4
// A COPY's bytes, at most: 16 stores of 16 bytes, the most LLVM expands a memcpy into at -O0.
#define BUSTER_TIER0_MAX_COPY_BYTES 256

// A function, as the front end hands it to Tier 0: the body's columns, the locals (parameters first, in order) with
// their classes and flags, the parameters' locations at entry and the result's (abi_assign of the function's own type),
// and the calls: call c's callee (a SymbolId, or 0 for an indirect call), its arguments' locations and classes
// (call_first[c] onward, in `argument_locations` and `argument_classes`, call_argument_counts[c] of them), whether it
// is variadic (al counts its vector registers), and its totals (AbiCall), from abi_assign of the call's signature. The
// ABI's work is the front end's, in one batch per unit (abi_assign is vectorized across calls), so Tier 0 needs no type
// table.
//
// The locals that are objects in the frame (arrays, structs and unions: anything that isn't one value of a class) are
// optional columns, 0 for a function without objects: local_sizes[l] is local l's size in bytes, nonzero for an
// object and 0 for a value, and local_alignments[l] its alignment's log2, at most 4 (16 bytes). An object's class is
// VOID, its flags have BUSTER_TYPED_LOCAL_ADDRESS_TAKEN, it isn't a parameter, and only LOCAL_ADDRESS refers to it: its
// bytes are loaded and stored through that address (PTR_ADD, LOAD, SET). Its first contents are the front end's to
// write. The objects take at most BUSTER_TIER0_MAX_OBJECT_BYTES in all.
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
    const uint32_t* local_sizes;
    const uint8_t* local_alignments;
    // NULL preserves the ordinary frame. Otherwise abi_assign's named-only own signature, with variadic=true:
    // count == named_count, GP count includes a hidden result pointer, stack_bytes is the end of the named stack
    // slots (not a frame/outgoing-area rounding). The producer retains this borrowed metadata through compilation.
    const AbiCall* incoming_variadic;
};

// Whether local `local` is an object in the frame (Tier0Function.local_sizes).
static bool tier0_is_object(const Tier0Function* function, uint64_t local)
{
    return function->local_sizes != 0 && function->local_sizes[local] != 0;
}

// The optional frame descriptors are checked even when unused. The typed stream itself retains tier0_validate's
// valid-input precondition; these checks also let both compile paths refuse malformed optional frame metadata.
static bool tier0_frame_input_valid(const Tier0Function* function)
{
    bool valid = function->local_count <= BUSTER_TIER0_MAX_LOCALS && function->parameter_count <= function->local_count;
    const AbiCall* incoming = function->incoming_variadic;
    if (incoming != 0)
    {
        valid = valid && incoming->integer_registers <= BUSTER_ABI_INTEGER_ARGUMENTS && incoming->vector_registers <= BUSTER_ABI_VECTOR_ARGUMENTS &&
            incoming->stack_alignment_log2 == BUSTER_ABI_STACK_ALIGNMENT_LOG2 && incoming->stack_bytes % BUSTER_ABI_SLOT_BYTES == 0 && incoming->stack_bytes <= INT32_MAX;
        for (uint32_t p = 0; valid && p < function->parameter_count; p += 1)
        {
            const AbiLocation location = function->parameters[p];
            if (location.kind == BUSTER_ABI_LOCATION_STACK)
            {
                valid = location.stack_offset % BUSTER_ABI_SLOT_BYTES == 0 && (uint64_t)location.stack_offset + BUSTER_ABI_SLOT_BYTES <= incoming->stack_bytes;
            }
        }
    }
    for (uint32_t local = 0; valid && local < function->local_count; local += 1)
    {
        const uint32_t flags = function->local_flags[local];
        const uint32_t role = flags & BUSTER_TYPED_LOCAL_ADDRESS_ONLY;
        valid = (flags & ~(BUSTER_TYPED_LOCAL_ADDRESS_TAKEN | BUSTER_TYPED_LOCAL_ADDRESS_ONLY)) == 0;
        if (role != 0)
        {
            valid = valid && (role & (role - 1)) == 0 && local >= function->parameter_count && function->local_classes[local] == BUSTER_TYPED_CLASS_U64 &&
                (flags & BUSTER_TYPED_LOCAL_ADDRESS_TAKEN) != 0 && (role == BUSTER_TYPED_LOCAL_VA_LIST || incoming != 0);
        }
    }
    return valid;
}

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
//     HOME_CONST: the first operand is a local (A) and the last a constant (K). HOME_HOME (a compare's): both operands
//     are locals (the first B, the last A). For a floating constant, CONST is its zero (xorps).
//   - INDEX(s): a load's address (or a pointer addition's lea) is [R2 + R1 * 2^s]; OFFSET: [R1 + K].
//   - BRANCH_FALSE and BRANCH_TRUE, plus the compare's PLAIN to HOME_HOME: a branch with its compare.
//   - RMW_CONST and RMW_HOME: x = x op v as op x, v (SET_LOCAL's).
//   - INDEX_CONST(s), INDEX_HOME(s), OFFSET_HOME: a store's address [R2 + R1 * 2^s] (or [R1 + K]) and its value a
//     constant (K) or a local (A).
//   - LAST: a return that is the function's last entry, without the jump to the epilogue.
//   - WIDE_SCALE: a pointer addition whose element size is past imul's sign-extended imm32, materialized in R0.
#define BUSTER_TIER0_VARIANT_PLAIN 0
#define BUSTER_TIER0_VARIANT_CONST 1
#define BUSTER_TIER0_VARIANT_HOME 2
#define BUSTER_TIER0_VARIANT_HOME_CONST 3
// A compare's alone, so it shares its number with a load's INDEX(0).
#define BUSTER_TIER0_VARIANT_HOME_HOME 4
#define BUSTER_TIER0_VARIANT_INDEX(s) (4 + (s))
#define BUSTER_TIER0_VARIANT_OFFSET 8
#define BUSTER_TIER0_VARIANT_BRANCH_FALSE 9
#define BUSTER_TIER0_VARIANT_BRANCH_TRUE 14
#define BUSTER_TIER0_VARIANT_RMW_CONST 19
#define BUSTER_TIER0_VARIANT_RMW_HOME 20
#define BUSTER_TIER0_VARIANT_INDEX_CONST(s) (21 + (s))
#define BUSTER_TIER0_VARIANT_INDEX_HOME(s) (25 + (s))
#define BUSTER_TIER0_VARIANT_OFFSET_HOME 29
#define BUSTER_TIER0_VARIANT_LAST 30
#define BUSTER_TIER0_VARIANT_WIDE_SCALE 31
#define BUSTER_TIER0_VARIANT_CONVERT_HOME 16
#define BUSTER_TIER0_VARIANTS 32

// A template: up to four records, as specifications (for the scalar expansion and the passes after expansion) and
// compiled (for the vector expansion: the records' static bytes, each byte's hole in the hole block, and which bytes take
// their hole). Its holes: K is the value of entry `k_offset` back (BUSTER_TIER0_NO_OFFSET: none), times the value of
// entry `k_scale` back when there is one (a pointer addition's element size); A is the home of the local entry
// `a_offset` back reads, and B the home of entry `b_offset` back's (without one, the entry's own local's). `required_depth` is the integer depth its fixed registers need (BUSTER_TIER0_ANY_DEPTH: any);
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
    uint8_t b_offset;
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

// What the passes after selection read of a template, in 32 bytes (the templates themselves are 270): its length, its
// branch record (0xff: none) and whether it goes to the function's end, its symbolic record (0xff: none), its choice
// record (0xff: none) with its row, live flags and immediate, its holes' offsets, its required depth, its slot's reach
// (the most of a home-dependent slot's cases', for the frame's touches, read before the homes are known) and its fixed
// clobbers. Every template has at most one record of each role (tier0_end checks).
typedef struct Tier0TemplateInfo Tier0TemplateInfo;
struct Tier0TemplateInfo
{
    uint8_t length;
    uint8_t branch;
    uint8_t end;
    uint8_t fixup;
    uint8_t choice;
    uint8_t choice_live;
    uint16_t choice_row;
    uint8_t k_offset;
    uint8_t k_scale;
    uint8_t a_offset;
    uint8_t b_offset;
    uint8_t required_depth;
    uint8_t integer_reach;
    uint8_t floating_reach;
    uint8_t choice_static;
    // B is the entry's own local's home (LOCAL, LOCAL_ADDRESS, STORE_LOCAL and SET_LOCAL's templates, x = x op v's).
    uint8_t b_own;
    uint8_t padding[3];
    int32_t choice_immediate;
    uint32_t clobbers;
    // Which bytes of the compiled records take their hole (tier0_compiled).
    uint64_t mask;
};
_Static_assert(sizeof(Tier0TemplateInfo) == 40, "a template's summary");

static Tier0TemplateInfo tier0_template_infos[BUSTER_TIER0_TEMPLATE_CAPACITY];

// Each template's four records compiled into one cache line, for the vector expansion: a byte the template fixes is
// that byte, and a hole's is its index in the hole block (below 32), which vpermb reads from its low bits; the
// summary's mask says which are holes, so that one load gives vpermb its indices and the blend its static bytes.
typedef struct Tier0Compiled Tier0Compiled;
struct Tier0Compiled
{
    _Alignas(64) uint8_t bytes[64];
};

static Tier0Compiled tier0_compiled[BUSTER_TIER0_TEMPLATE_CAPACITY];

// The vector path's per-entry word: everything the passes after fusion read of an entry's template, in 64 bits, so that
// selection gathers it, 8 entries at a time, and measuring and emission read it from the entries' column rather than a
// template's summary. A template's word is tier0_template_words'; a slot's (tier0_slot_words, by kind << 9 | class << 5
// | variant) is its first template's, and a home-dependent slot's says so, and whether its four cases' words differ in
// the index alone (HOME_UNIFORM: measuring adds the case to the index rather than gathering the case's word). A B from
// the entry's own local is the B of offset 0.
#define BUSTER_TIER0_WORD_TEMPLATE 0xfffull
#define BUSTER_TIER0_WORD_HOME_DEPENDENT (1ull << 12)
#define BUSTER_TIER0_WORD_DEFERRED (1ull << 13)
#define BUSTER_TIER0_WORD_UNSUPPORTED (1ull << 14)
#define BUSTER_TIER0_WORD_DEPTH_SHIFT 15
#define BUSTER_TIER0_WORD_ANY_DEPTH 15ull
#define BUSTER_TIER0_WORD_INTEGER_REACH_SHIFT 19
#define BUSTER_TIER0_WORD_FLOATING_REACH_SHIFT 21
#define BUSTER_TIER0_WORD_CLOBBERS_RDX (1ull << 23)
#define BUSTER_TIER0_WORD_LENGTH_SHIFT 24
#define BUSTER_TIER0_WORD_BRANCH (1ull << 27)
#define BUSTER_TIER0_WORD_FIXUP (1ull << 28)
#define BUSTER_TIER0_WORD_CHOICE (1ull << 29)
#define BUSTER_TIER0_WORD_BRANCH_SHIFT 30
#define BUSTER_TIER0_WORD_FIXUP_SHIFT 32
#define BUSTER_TIER0_WORD_CHOICE_SHIFT 34
#define BUSTER_TIER0_WORD_END (1ull << 36)
#define BUSTER_TIER0_WORD_CHOICE_STATIC (1ull << 37)
#define BUSTER_TIER0_WORD_LIVE_SHIFT 38
#define BUSTER_TIER0_WORD_K_SHIFT 44
#define BUSTER_TIER0_WORD_K_SCALE_SHIFT 47
#define BUSTER_TIER0_WORD_A_SHIFT 50
#define BUSTER_TIER0_WORD_B_SHIFT 53
#define BUSTER_TIER0_WORD_NO_OFFSET 7ull
// A word without holes (an absorbed entry's, a deferred slot's), whose lane emission expands and later records
// overwrite: no K or its scale, and no A or B, so that the lane looks no home up by a value that isn't a local's (with
// more than 32 locals, a gather from the homes at that value).
#define BUSTER_TIER0_WORD_NO_HOLES \
    (BUSTER_TIER0_WORD_NO_OFFSET << BUSTER_TIER0_WORD_K_SHIFT | BUSTER_TIER0_WORD_NO_OFFSET << BUSTER_TIER0_WORD_K_SCALE_SHIFT | \
     BUSTER_TIER0_WORD_NO_OFFSET << BUSTER_TIER0_WORD_A_SHIFT | BUSTER_TIER0_WORD_NO_OFFSET << BUSTER_TIER0_WORD_B_SHIFT)
#define BUSTER_TIER0_WORD_HOME_UNIFORM (1ull << 56)
#define BUSTER_TIER0_WORD_ABSORBED (1ull << 57)
#define BUSTER_TIER0_WORD_ROW_SHIFT 58
// The fields the passes after selection read: two words equal in these give the same records but for the template.
#define BUSTER_TIER0_WORD_EMISSION \
    (~(BUSTER_TIER0_WORD_TEMPLATE | BUSTER_TIER0_WORD_HOME_DEPENDENT | BUSTER_TIER0_WORD_HOME_UNIFORM | 0xffull << BUSTER_TIER0_WORD_DEPTH_SHIFT | BUSTER_TIER0_WORD_CLOBBERS_RDX))
#define BUSTER_TIER0_SLOT_WORDS (BUSTER_TYPED_KIND_LIMIT * BUSTER_TYPED_CLASS_LIMIT * BUSTER_TIER0_VARIANTS)

static uint64_t tier0_slot_words[BUSTER_TIER0_SLOT_WORDS];
static uint64_t tier0_template_words[BUSTER_TIER0_TEMPLATE_CAPACITY];
// Per form, what x86_choice_instruction does with it as a chosen form: its immediate's bytes, and whether it takes no
// operands (the accumulator's and push's forms) or its destination twice (a zero idiom, test r, r).
#define BUSTER_TIER0_SHAPE_BYTES 0x0f
#define BUSTER_TIER0_SHAPE_CLEAR 0x10
#define BUSTER_TIER0_SHAPE_TWICE 0x20
static uint8_t tier0_choice_shapes[X86_FORM_COUNT];
// Per template: its compiled records' hole mask, for emission.
static uint64_t tier0_masks[BUSTER_TIER0_TEMPLATE_CAPACITY];
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
static bool tier0_build_vector_tables(void);
static bool tier0_build_words(void);
static bool tier0_build_choice_shapes(void);

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
    tier0_builder.current.b_offset = BUSTER_TIER0_NO_OFFSET;
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
        Tier0TemplateInfo info;
        memset(&info, 0, sizeof(info));
        info.length = template->length;
        info.branch = 0xff;
        info.fixup = 0xff;
        info.choice = 0xff;
        info.choice_row = BUSTER_X86_CHOICE_NO_ROW;
        info.k_offset = template->k_offset;
        info.k_scale = template->k_scale;
        info.a_offset = template->a_offset;
        info.b_offset = template->b_offset;
        info.required_depth = template->required_depth;
        info.integer_reach = template->integer_reach;
        info.floating_reach = template->floating_reach;
        info.clobbers = template->clobbers;
        info.mask = template->mask;
        info.b_own = template->b_offset == BUSTER_TIER0_NO_OFFSET &&
                             (kind == BUSTER_TYPED_LOCAL || kind == BUSTER_TYPED_LOCAL_ADDRESS || kind == BUSTER_TYPED_STORE_LOCAL || kind == BUSTER_TYPED_SET_LOCAL ||
                              variant == BUSTER_TIER0_VARIANT_RMW_CONST || variant == BUSTER_TIER0_VARIANT_RMW_HOME)
                         ? 1
                         : 0;
        for (uint32_t b = 0; b < 64; b += 1)
        {
            tier0_compiled[index].bytes[b] = ((template->mask >> b) & 1) != 0 ? template->holes[b] : template->bytes[b];
        }
        for (uint32_t r = 0; r < template->length; r += 1)
        {
            const Tier0RecordSpec* spec = &template->specs[r];
            if ((spec->flags & (BUSTER_TIER0_RECORD_BRANCH | BUSTER_TIER0_RECORD_END)) != 0)
            {
                tier0_builder.valid &= info.branch == 0xff;
                info.branch = (uint8_t)r;
                info.end = (spec->flags & BUSTER_TIER0_RECORD_END) != 0 ? 1 : 0;
            }
            if ((spec->flags & BUSTER_TIER0_RECORD_FIXUP) != 0)
            {
                tier0_builder.valid &= info.fixup == 0xff;
                info.fixup = (uint8_t)r;
            }
            if ((spec->flags & BUSTER_TIER0_RECORD_CHOICE) != 0)
            {
                tier0_builder.valid &= info.choice == 0xff;
                info.choice = (uint8_t)r;
                info.choice_row = spec->row;
                info.choice_live = spec->live;
                info.choice_static = spec->immediate_kind == BUSTER_TIER0_IMMEDIATE_STATIC ? 1 : 0;
                info.choice_immediate = spec->immediate;
            }
        }
        tier0_template_infos[index] = info;
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
    // COPY: tier0_emit_deferred's, its moves by its size.
    tier0_defer(BUSTER_TYPED_COPY, BUSTER_TYPED_CLASS_VOID, BUSTER_TIER0_VARIANT_PLAIN);
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
            // Two locals: cmp x, y with x (B) or y (A) a register, or through the scratch, as a value and as a branch.
            const uint32_t cmp_r_rm = tier0_wide(class, X86_FORM_CMP_R32_RM32, X86_FORM_CMP_R64_RM64);
            const uint32_t cmp_rm_r = tier0_wide(class, X86_FORM_CMP_RM32_R32, X86_FORM_CMP_RM64_R64);
            const uint32_t load = tier0_wide(class, X86_FORM_MOV_R32_RM32, X86_FORM_MOV_R64_RM64);
            for (uint32_t context = 0; context < 3; context += 1)
            {
                const uint32_t shift = context == 0 ? 0 : 1;
                const uint32_t variant = context == 0 ? BUSTER_TIER0_VARIANT_HOME_HOME
                                         : context == 1 ? BUSTER_TIER0_VARIANT_BRANCH_FALSE + BUSTER_TIER0_VARIANT_HOME_HOME
                                                        : BUSTER_TIER0_VARIANT_BRANCH_TRUE + BUSTER_TIER0_VARIANT_HOME_HOME;
                BUSTER_TIER0_HOME_CASES(kind, class, variant, no, 1 + shift, {
                    tier0_builder.current.b_offset = (uint8_t)(2 + shift);
                    Tier0RecordSpec compare = tier0_rh(cmp_r_rm, BUSTER_TIER0_SOURCE_B, BUSTER_TIER0_RM_HOME_A);
                    if (!b_register && a_register)
                    {
                        compare = tier0_rh(cmp_rm_r, BUSTER_TIER0_SOURCE_A, BUSTER_TIER0_RM_HOME_B);
                    }
                    else if (!b_register)
                    {
                        tier0_add(tier0_rh(load, BUSTER_TIER0_SOURCE_R0, BUSTER_TIER0_RM_HOME_B));
                        compare = tier0_rh(cmp_r_rm, BUSTER_TIER0_SOURCE_R0, BUSTER_TIER0_RM_HOME_A);
                    }
                    compare.live = live;
                    tier0_add(compare);
                    if (context == 0)
                    {
                        tier0_add(tier0_r(tier0_setcc[cc], BUSTER_TIER0_SOURCE_R0));
                        tier0_add(tier0_rr(X86_FORM_MOVZX_R32_RM8, BUSTER_TIER0_SOURCE_R0, BUSTER_TIER0_SOURCE_R0));
                    }
                    else
                    {
                        tier0_add(tier0_branch(tier0_jcc_rel8[context == 2 ? cc : cc ^ 1], BUSTER_TIER0_RECORD_BRANCH));
                    }
                });
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
    // A size past the imm32 (which imul sign-extends): in R0, as a constant is (the choice pass shortens the mov), then
    // the 64-bit imul.
    tier0_begin();
    tier0_holes_from(0, no, no);
    tier0_add(tier0_with_k(tier0_r(X86_FORM_MOV_RM64_IMM32, BUSTER_TIER0_SOURCE_R0), 0));
    tier0_add(tier0_rr(X86_FORM_IMUL_R64_RM64, BUSTER_TIER0_SOURCE_R1, BUSTER_TIER0_SOURCE_R0));
    tier0_add(tier0_rr(X86_FORM_ADD_R64_RM64, BUSTER_TIER0_SOURCE_R2, BUSTER_TIER0_SOURCE_R1));
    tier0_end(BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, BUSTER_TIER0_VARIANT_WIDE_SCALE, 0);
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
        for (uint32_t slot = 0; slot < tier0_slot_count; slot += 1)
        {
            const uint16_t value = tier0_slots[slot];
            if (value != BUSTER_TIER0_SLOT_UNSUPPORTED && value != BUSTER_TIER0_SLOT_DEFERRED && (value & BUSTER_TIER0_SLOT_HOME_DEPENDENT) != 0)
            {
                Tier0TemplateInfo* first = &tier0_template_infos[value & BUSTER_TIER0_SLOT_TEMPLATE];
                for (uint32_t c = 1; c < 4; c += 1)
                {
                    const Tier0TemplateInfo* other = &tier0_template_infos[(value & BUSTER_TIER0_SLOT_TEMPLATE) + c];
                    first->integer_reach = other->integer_reach > first->integer_reach ? other->integer_reach : first->integer_reach;
                    first->floating_reach = other->floating_reach > first->floating_reach ? other->floating_reach : first->floating_reach;
                    first->clobbers |= other->clobbers;
                }
            }
        }
        tier0_initialized = tier0_builder.valid && tier0_build_vector_tables() && tier0_build_words() && tier0_build_choice_shapes();
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
    [BUSTER_TYPED_COPY] = { BUSTER_TIER0_STACK_INTEGER, BUSTER_TIER0_STACK_OPERAND, BUSTER_TIER0_STACK_NONE },
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
    // The vector path's per-entry words (BUSTER_TIER0_WORD_*).
    uint64_t* words;
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
        counts[1] -= function->call_targets[call] == 0 ? 1 : 0;
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
        // The index fits 32 bits and the size is below 2^31, so that their product can't overflow: it must fit too.
        const int64_t product = (int64_t)value * (int64_t)scale;
        fits = tier0_fits_int32((int64_t)value) && scale <= INT32_MAX && tier0_fits_int32(product);
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
        else if (kind == BUSTER_TYPED_PTR_ADD && values[i] > INT32_MAX)
        {
            variant = BUSTER_TIER0_VARIANT_WIDE_SCALE;
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
        else if (i >= 2 && !here && !before && kind >= BUSTER_TYPED_EQ && kind <= BUSTER_TYPED_GE && kinds[i - 1] == BUSTER_TYPED_LOCAL &&
                 kinds[i - 2] == BUSTER_TYPED_LOCAL && tier0_slot(kind, key_class, BUSTER_TIER0_VARIANT_HOME_HOME) != BUSTER_TIER0_SLOT_UNSUPPORTED)
        {
            variant = BUSTER_TIER0_VARIANT_HOME_HOME;
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
            if (entries->variants[i - 1] <= BUSTER_TIER0_VARIANT_HOME_HOME &&
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
// a call's arguments, saved around it), two slots for the deferred division and shifts, the optional incoming
// variadic register-save area, the locals' slots, then the objects. Incoming saves never share outgoing
// call-preservation storage. `size` is what the prologue subtracts from rsp (with the pushes and the return address, a
// multiple of 16 when the function calls or has an object aligned to 16); `saved` has a bit per tier0_callee_saved
// register the prologue pushes. `clobbers` is the register set the body touches (BUSTER_TIER0_XMM's layout), and
// `integer_top` and `floating_top` the highest depth registers in it.
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
    uint32_t variadic_area;
    uint32_t overflow_area;
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

// Each local's references: its LOCAL, LOCAL_ADDRESS, STORE_LOCAL and SET_LOCAL entries (`references` has local_count).
static void tier0_count_references(const Tier0Function* function, uint32_t* references)
{
    const uint8_t* kinds = function->kinds;
    const uint64_t* values = function->values;
    memset(references, 0, function->local_count * sizeof(uint32_t));
    for (uint32_t i = 0; i < function->count; i += 1)
    {
        const uint8_t kind = kinds[i];
        if (kind == BUSTER_TYPED_LOCAL || kind == BUSTER_TYPED_LOCAL_ADDRESS || kind == BUSTER_TYPED_STORE_LOCAL || kind == BUSTER_TYPED_SET_LOCAL)
        {
            references[values[i]] += 1;
        }
    }
}

static __mmask64 tier0_lanes(int64_t first, uint32_t count);

// tier0_count_references for the vector path: the referring entries of 64 at a time as a mask (vpshufbitqmb of their
// kinds in the four kinds' bitset), then a count per set bit, without a branch per entry.
static void tier0_count_references_vector(const Tier0Function* function, uint32_t* references)
{
    const uint8_t* kinds = function->kinds;
    const uint64_t* values = function->values;
    const uint32_t count = function->count;
    memset(references, 0, function->local_count * sizeof(uint32_t));
    const uint64_t kinds_referring = 1ull << BUSTER_TYPED_LOCAL | 1ull << BUSTER_TYPED_LOCAL_ADDRESS | 1ull << BUSTER_TYPED_STORE_LOCAL | 1ull << BUSTER_TYPED_SET_LOCAL;
    const __m512i referring = _mm512_set1_epi64((int64_t)kinds_referring);
    _Static_assert(BUSTER_TYPED_KIND_LIMIT == 64, "a kind is a bit of a quadword");
    for (uint32_t first = 0; first < count; first += 64)
    {
        const __mmask64 lanes = tier0_lanes(first, count);
        uint64_t bits = _mm512_mask_bitshuffle_epi64_mask(lanes, referring, _mm512_maskz_loadu_epi8(lanes, kinds + first));
        while (bits != 0)
        {
            const uint32_t i = first + (uint32_t)__builtin_ctzll(bits);
            bits &= bits - 1;
            references[values[i]] += 1;
        }
    }
}

// The locals by references, most first and by index among equals: an insertion sort, since a function's locals are few.
// The vector path's twin, tier0_assign_registers_vector, ranks the locals instead.
static void tier0_order_locals(uint32_t locals, const uint32_t* references, uint32_t* order)
{
    for (uint32_t l = 0; l < locals; l += 1)
    {
        order[l] = l;
    }
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
}

// The greedy loop of tier0_assign_homes for the vector path, for up to 64 locals, without the order: the loop gives the
// k-th local that takes a register (not in one yet, its address not taken, referenced), counted among its kind's by
// references and then index, the k-th free register of its kind: an integer one the caller-saved registers in depth
// order, then the callee-saved ones if it is referenced twice or more (those come first in the order), and a floating
// one the XMM registers. So each eligible local's rank among its kind's (by vector compares, 16 locals at a time
// against each eligible local) gives its register (vpermd of the free registers' list), and its home is a masked
// store. Returns the callee-saved registers taken (Tier0Frame.saved's bits).
static uint32_t tier0_assign_registers_vector(const Tier0Function* function, Tier0Home* homes, const uint32_t* references, uint32_t taken)
{
    const uint32_t locals = function->local_count;
    // The free registers in the order the loop takes them: caller-saved then callee-saved, and XMM.
    uint32_t general[16] = { 0 };
    uint32_t callee[BUSTER_TIER0_CALLEE_SAVED + 1] = { 0 };
    uint32_t xmm[16] = { 0 };
    uint32_t callers = 0;
    uint32_t callees = 0;
    uint32_t xmms = 0;
    for (uint32_t d = 0; d < BUSTER_TIER0_CALLER_SAVED_DEPTHS; d += 1)
    {
        const uint32_t reg = tier0_integer_depths[d];
        general[callers] = reg;
        callers += (taken >> reg & 1) == 0 ? 1 : 0;
    }
    for (uint32_t c = 0; c < BUSTER_TIER0_CALLEE_SAVED; c += 1)
    {
        const uint32_t reg = tier0_callee_saved[c];
        general[callers + callees] = reg;
        // The callee-saved registers the first n locals past the caller-saved ones take, as Tier0Frame.saved's bits.
        callee[callees + 1] = callee[callees] | 1u << c;
        callees += (taken >> reg & 1) == 0 ? 1 : 0;
    }
    for (uint32_t x = 0; x < 16; x += 1)
    {
        xmm[xmms] = x;
        xmms += (taken >> BUSTER_TIER0_XMM(x) & 1) == 0 ? 1 : 0;
    }
    // Which locals take a register, and of which kind.
    const __m512i is_register = _mm512_set1_epi64((int64_t)(0xffull << (8 * offsetof(Tier0Home, is_register))));
    uint64_t integers = 0;
    uint64_t floatings = 0;
    for (uint32_t first = 0; first < locals; first += 16)
    {
        const __mmask16 lanes = (__mmask16)tier0_lanes(first, locals);
        const __m512i homes_low = _mm512_maskz_loadu_epi64((__mmask8)lanes, homes + first);
        const __m512i homes_high = _mm512_maskz_loadu_epi64((__mmask8)(lanes >> 8), homes + first + 8);
        const __mmask16 in_register = _mm512_kunpackb(_mm512_test_epi64_mask(homes_high, is_register), _mm512_test_epi64_mask(homes_low, is_register));
        const __m512i counts = _mm512_maskz_loadu_epi32(lanes, references + first);
        const __m128i flags = _mm_maskz_loadu_epi8(lanes, function->local_flags + first);
        const __m128i classes = _mm_maskz_loadu_epi8(lanes, function->local_classes + first);
        const __mmask16 taken_address = _mm_test_epi8_mask(flags, _mm_set1_epi8((char)BUSTER_TYPED_LOCAL_ADDRESS_TAKEN));
        const __mmask16 floating = _mm_cmpeq_epi8_mask(classes, _mm_set1_epi8(BUSTER_TYPED_CLASS_F32)) | _mm_cmpeq_epi8_mask(classes, _mm_set1_epi8(BUSTER_TYPED_CLASS_F64));
        const uint64_t eligible = lanes & _mm512_test_epi32_mask(counts, counts) & ~in_register & ~taken_address;
        integers |= (eligible & ~(uint64_t)floating) << first;
        floatings |= (eligible & floating) << first;
    }
    const __m512i list = _mm512_loadu_si512(general);
    const __m512i xmm_list = _mm512_loadu_si512(xmm);
    const __m512i one = _mm512_set1_epi32(1);
    uint32_t callee_taken = 0;
    for (uint32_t first = 0; first < locals; first += 16)
    {
        const __mmask16 integer_lanes = (__mmask16)(integers >> first);
        const __mmask16 floating_lanes = (__mmask16)(floatings >> first);
        const __m512i counts = _mm512_maskz_loadu_epi32((__mmask16)tier0_lanes(first, locals), references + first);
        const __m512i index = _mm512_add_epi32(_mm512_set1_epi32((int32_t)first), _mm512_set_epi32(15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0));
        __m512i rank = _mm512_setzero_si512();
        uint64_t others = integers | floatings;
        while (others != 0)
        {
            const uint32_t other = (uint32_t)__builtin_ctzll(others);
            others &= others - 1;
            const __m512i key = _mm512_set1_epi32((int32_t)references[other]);
            const __mmask16 same = (floatings >> other & 1) != 0 ? floating_lanes : integer_lanes;
            const __mmask16 ahead = _mm512_cmpgt_epu32_mask(key, counts) | (_mm512_cmpeq_epi32_mask(key, counts) & _mm512_cmpgt_epu32_mask(index, _mm512_set1_epi32((int32_t)other)));
            rank = _mm512_mask_add_epi32(rank, same & ahead, rank, one);
        }
        // Integer locals: a caller-saved register below `callers`, a callee-saved one below `callers + callees` when
        // referenced twice; floating ones an XMM register below `xmms`.
        const __mmask16 caller_lanes = integer_lanes & _mm512_cmplt_epu32_mask(rank, _mm512_set1_epi32((int32_t)callers));
        const __mmask16 callee_lanes = integer_lanes & ~caller_lanes & _mm512_cmplt_epu32_mask(rank, _mm512_set1_epi32((int32_t)(callers + callees))) &
                                       _mm512_cmpge_epu32_mask(counts, _mm512_set1_epi32(2));
        const __mmask16 xmm_lanes = floating_lanes & _mm512_cmplt_epu32_mask(rank, _mm512_set1_epi32((int32_t)xmms));
        const __mmask16 assigned = caller_lanes | callee_lanes | xmm_lanes;
        callee_taken += (uint32_t)__builtin_popcount(callee_lanes);
        const __m512i reg = _mm512_mask_permutexvar_epi32(_mm512_permutexvar_epi32(rank, list), xmm_lanes, rank, xmm_list);
        // A home in a register: reg in byte 4, is_register in byte 6.
        const __m512i low = _mm512_or_si512(_mm512_slli_epi64(_mm512_cvtepu32_epi64(_mm512_castsi512_si256(reg)), 32), _mm512_set1_epi64(1ll << 48));
        const __m512i high = _mm512_or_si512(_mm512_slli_epi64(_mm512_cvtepu32_epi64(_mm512_extracti64x4_epi64(reg, 1)), 32), _mm512_set1_epi64(1ll << 48));
        _mm512_mask_storeu_epi64(homes + first, (__mmask8)assigned, low);
        _mm512_mask_storeu_epi64(homes + first + 8, (__mmask8)(assigned >> 8), high);
    }
    return callee[callee_taken];
}

// Phase 3: each local's home, and the frame. A parameter whose register the body doesn't touch stays in it. The other
// locals, most referenced first (by index among equals), take the free caller-saved registers, then (referenced at
// least twice, for a push and a pop) the callee-saved ones; a floating local takes a free XMM register. The rest, and
// every local whose address is taken, get frame slots; a parameter on the stack keeps its incoming slot, unless it takes
// a register, which the prologue loads from that slot. `references` (local_count) holds each local's references, and
// `order` (local_count) is scratch for the locals by them (tier0_order_locals).
static void tier0_assign_homes(const Tier0Function* function, Tier0Frame* frame, Tier0Home* homes, const uint32_t* references, uint32_t* order, bool vector)
{
    const uint32_t locals = function->local_count;
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
    // The rest by references, most first (`order`, tier0_order_locals'); for the vector path and up to 64 locals, by their
    // ranks (tier0_assign_registers_vector).
    const bool ranked = vector && locals <= 64;
    if (ranked)
    {
        frame->saved |= tier0_assign_registers_vector(function, homes, references, taken);
    }
    else
    {
        tier0_order_locals(locals, references, order);
    }
    for (uint32_t k = 0; k < locals && !ranked; k += 1)
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
// save area, the fixed slots, the optional incoming variadic saves, each memory local (8 bytes, or exactly 24 for an
// address-only VA_LIST), and the objects, each at a multiple of its alignment and taking its size rounded up to 8, in
// the locals' order. Anchor descriptors alias their frame/incoming addresses without owning another slot. A parameter
// that arrives on the stack keeps its incoming slot, above the return address. Check every displacement in 64 bits
// before narrowing; an unrepresentable frame is refused before any records or output are published.
// tier0_layout_frame's slots for the vector path, 16 locals at a time: the mask of those that need one (not in a
// register, referenced or address-taken, not a parameter on the stack, not an anchor, not an object) from the homes,
// the references, the flags and the sizes, then a slot for each set bit, in order. Returns the offset past them, and
// in *objects_seen whether there are objects; offsets past INT32_MAX narrow to garbage, which the caller's check of
// the final, larger offset refuses.
static uint64_t tier0_frame_slots_vector(const Tier0Function* function, Tier0Home* homes, const uint32_t* references, uint64_t offset, bool* objects_seen)
{
    const uint32_t locals = function->local_count;
    const __m512i is_register = _mm512_set1_epi64((int64_t)(0xffull << (8 * offsetof(Tier0Home, is_register))));
    const __m128i roles = _mm_set1_epi8((char)BUSTER_TYPED_LOCAL_ADDRESS_ONLY);
    for (uint32_t first = 0; first < locals; first += 16)
    {
        const __mmask16 lanes = (__mmask16)tier0_lanes(first, locals);
        const __m512i homes_low = _mm512_maskz_loadu_epi64((__mmask8)lanes, homes + first);
        const __m512i homes_high = _mm512_maskz_loadu_epi64((__mmask8)(lanes >> 8), homes + first + 8);
        const __mmask16 in_register = _mm512_kunpackb(_mm512_test_epi64_mask(homes_high, is_register), _mm512_test_epi64_mask(homes_low, is_register));
        const __m512i counts = _mm512_maskz_loadu_epi32(lanes, references + first);
        const __m128i flags = _mm_maskz_loadu_epi8(lanes, function->local_flags + first);
        const __m128i role = _mm_and_si128(flags, roles);
        const __mmask16 needed = _mm512_test_epi32_mask(counts, counts) | _mm_test_epi8_mask(flags, _mm_set1_epi8((char)BUSTER_TYPED_LOCAL_ADDRESS_TAKEN));
        const __m512i sizes = function->local_sizes != 0 ? _mm512_maskz_loadu_epi32(lanes, function->local_sizes + first) : _mm512_setzero_si512();
        const __mmask16 objects = _mm512_test_epi32_mask(sizes, sizes);
        *objects_seen |= objects != 0;
        const __mmask16 anchor = _mm_cmpeq_epi8_mask(role, _mm_set1_epi8((char)BUSTER_TYPED_LOCAL_REGISTER_SAVE_AREA)) |
                                 _mm_cmpeq_epi8_mask(role, _mm_set1_epi8((char)BUSTER_TYPED_LOCAL_OVERFLOW_ARG_AREA));
        const uint32_t va_list = _mm_cmpeq_epi8_mask(role, _mm_set1_epi8((char)BUSTER_TYPED_LOCAL_VA_LIST));
        uint32_t on_stack = 0;
        for (uint32_t p = first; p < function->parameter_count && p < first + 16; p += 1)
        {
            on_stack |= function->parameters[p].kind == BUSTER_ABI_LOCATION_STACK ? 1u << (p - first) : 0;
        }
        uint32_t slots = lanes & needed & ~in_register & ~on_stack & ~anchor & ~objects;
        while (slots != 0)
        {
            const uint32_t lane = (uint32_t)__builtin_ctz(slots);
            const uint32_t local = first + lane;
            slots &= slots - 1;
            homes[local].displacement = (int32_t)(uint32_t)offset;
            homes[local].reg = X86_REGISTER_RSP;
            homes[local].mode = BUSTER_X86_MEMORY_BASE;
            offset += (va_list >> lane & 1) != 0 ? sizeof(AbiVaList) : BUSTER_ABI_SLOT_BYTES;
        }
    }
    return offset;
}

// The objects' places for the vector path, past the slots: the mask of the objects 16 locals at a time, then each set
// bit's object at its alignment. Returns the offset past them, and in *aligned whether one is aligned to 16; offsets
// past INT32_MAX narrow to garbage, as the slots' do, which the caller's check of the final offset refuses.
static uint64_t tier0_frame_objects_vector(const Tier0Function* function, Tier0Home* homes, uint64_t offset, bool* aligned)
{
    const uint32_t locals = function->local_count;
    for (uint32_t first = 0; first < locals && function->local_sizes != 0; first += 16)
    {
        const __mmask16 lanes = (__mmask16)tier0_lanes(first, locals);
        const __m512i sizes = _mm512_maskz_loadu_epi32(lanes, function->local_sizes + first);
        uint32_t objects = _mm512_test_epi32_mask(sizes, sizes);
        while (objects != 0)
        {
            const uint32_t local = first + (uint32_t)__builtin_ctz(objects);
            objects &= objects - 1;
            const uint64_t alignment = 1ull << function->local_alignments[local];
            offset = (offset + alignment - 1) & ~(alignment - 1);
            homes[local].displacement = (int32_t)(uint32_t)offset;
            homes[local].reg = X86_REGISTER_RSP;
            homes[local].mode = BUSTER_X86_MEMORY_BASE;
            offset += ((uint64_t)function->local_sizes[local] + 7) & ~UINT64_C(7);
            *aligned |= alignment == 16;
        }
    }
    return offset;
}

static bool tier0_layout_frame(const Tier0Function* function, Tier0Frame* frame, Tier0Home* homes, const uint32_t* references, bool vector)
{
    uint64_t outgoing = 0;
    for (uint32_t c = 0; c < function->call_count; c += 1)
    {
        outgoing = function->calls[c].stack_bytes > outgoing ? function->calls[c].stack_bytes : outgoing;
    }
    uint64_t offset = (outgoing + 7) & ~UINT64_C(7);
    bool valid = offset <= INT32_MAX;
    frame->save_area = valid ? (uint32_t)offset : 0;
    offset += 8 * frame->save_slots;
    valid = valid && offset <= INT32_MAX;
    frame->fixed_area = valid ? (uint32_t)offset : 0;
    offset += 8 * frame->fixed_slots;
    if (function->incoming_variadic != 0)
    {
        valid = valid && offset <= INT32_MAX;
        frame->variadic_area = valid ? (uint32_t)offset : 0;
        offset += BUSTER_ABI_SAVE_AREA_BYTES;
    }
    for (uint32_t l = 0; l < function->local_count && !vector; l += 1)
    {
        const bool address_taken = (function->local_flags[l] & BUSTER_TYPED_LOCAL_ADDRESS_TAKEN) != 0;
        const uint32_t role = function->local_flags[l] & BUSTER_TYPED_LOCAL_ADDRESS_ONLY;
        const bool anchor = role == BUSTER_TYPED_LOCAL_REGISTER_SAVE_AREA || role == BUSTER_TYPED_LOCAL_OVERFLOW_ARG_AREA;
        const bool on_stack = l < function->parameter_count && function->parameters[l].kind == BUSTER_ABI_LOCATION_STACK;
        if (!homes[l].is_register && (references[l] != 0 || address_taken) && !on_stack && !anchor && !tier0_is_object(function, l))
        {
            valid = valid && offset <= INT32_MAX;
            homes[l].displacement = valid ? (int32_t)offset : 0;
            homes[l].reg = X86_REGISTER_RSP;
            homes[l].mode = BUSTER_X86_MEMORY_BASE;
            offset += role == BUSTER_TYPED_LOCAL_VA_LIST ? sizeof(AbiVaList) : BUSTER_ABI_SLOT_BYTES;
        }
    }
    bool aligned = false;
    for (uint32_t l = 0; l < function->local_count && !vector; l += 1)
    {
        if (tier0_is_object(function, l))
        {
            const uint64_t alignment = 1ull << function->local_alignments[l];
            offset = (offset + alignment - 1) & ~(alignment - 1);
            valid = valid && offset <= INT32_MAX;
            homes[l].displacement = valid ? (int32_t)offset : 0;
            homes[l].reg = X86_REGISTER_RSP;
            homes[l].mode = BUSTER_X86_MEMORY_BASE;
            offset += ((uint64_t)function->local_sizes[l] + 7) & ~UINT64_C(7);
            aligned |= alignment == 16;
        }
    }
    bool objects = false;
    offset = vector ? tier0_frame_slots_vector(function, homes, references, offset, &objects) : offset;
    offset = vector && objects ? tier0_frame_objects_vector(function, homes, offset, &aligned) : offset;
    // With the pushes and the return address, a multiple of 16 at every call, and for an object aligned to 16.
    const uint64_t above = 8 * frame->pushes + 8;
    if (frame->calls || aligned)
    {
        offset = (offset + above + 15) / 16 * 16 - above;
    }
    valid = valid && offset <= INT32_MAX;
    frame->size = valid ? (uint32_t)offset : 0;
    if (function->incoming_variadic != 0)
    {
        const uint64_t overflow = offset + above + function->incoming_variadic->stack_bytes;
        valid = valid && overflow <= INT32_MAX;
        frame->overflow_area = valid ? (uint32_t)overflow : 0;
    }
    for (uint32_t l = 0; l < function->local_count; l += 1)
    {
        const uint32_t role = function->local_flags[l] & BUSTER_TYPED_LOCAL_ADDRESS_ONLY;
        if (role == BUSTER_TYPED_LOCAL_REGISTER_SAVE_AREA || role == BUSTER_TYPED_LOCAL_OVERFLOW_ARG_AREA)
        {
            homes[l].displacement = (int32_t)(role == BUSTER_TYPED_LOCAL_REGISTER_SAVE_AREA ? frame->variadic_area : frame->overflow_area);
            homes[l].reg = X86_REGISTER_RSP;
            homes[l].mode = BUSTER_X86_MEMORY_BASE;
        }
    }
    for (uint32_t p = 0; p < function->parameter_count; p += 1)
    {
        if (function->parameters[p].kind == BUSTER_ABI_LOCATION_STACK)
        {
            const uint64_t incoming = offset + above + function->parameters[p].stack_offset;
            valid = valid && incoming <= INT32_MAX;
            if (!homes[p].is_register)
            {
                homes[p].displacement = valid ? (int32_t)incoming : 0;
                homes[p].reg = X86_REGISTER_RSP;
                homes[p].mode = BUSTER_X86_MEMORY_BASE;
            }
        }
    }
    return valid;
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
static uint32_t tier0_integer_window[BUSTER_TIER0_WINDOWS] __attribute__((aligned(64)));
static uint32_t tier0_floating_window[BUSTER_TIER0_WINDOWS] __attribute__((aligned(64)));

static void tier0_build_windows(void)
{
    for (int32_t d = 0; d < BUSTER_TIER0_WINDOWS; d += 1)
    {
        uint32_t integer = 0;
        uint32_t floating = 0;
        for (int32_t k = 0; k < 3; k += 1)
        {
            const int32_t depth = d - 2 + k;
            integer |= (uint32_t)(depth >= 0 && depth < BUSTER_TIER0_INTEGER_DEPTHS ? tier0_integer_depths[depth] : 0) << (8 * k);
            floating |= (uint32_t)(depth >= 0 && depth < BUSTER_TIER0_FLOATING_DEPTHS ? depth : 0) << (8 * k);
        }
        tier0_integer_window[d] = integer;
        tier0_floating_window[d] = floating;
    }
}

// Entry i's hole block, for its template: K, A, B and the registers around its depths (R2, R1 and R0 in bytes 0 to 2,
// X2, X1 and X0 in bytes 3 to 5). The bytes no template reads (a home's is_register and padding) are left as they are.
static Tier0Holes tier0_holes_scalar(const Tier0Function* function, const Tier0Entries* entries, const Tier0Home* homes, uint32_t i, const Tier0TemplateInfo* template)
{
    Tier0Holes holes;
    holes.k = 0;
    holes.a = homes[0];
    holes.b = homes[0];
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
    if (template->b_offset != BUSTER_TIER0_NO_OFFSET)
    {
        holes.b = homes[function->values[i - template->b_offset]];
    }
    else if (template->b_own != 0)
    {
        holes.b = homes[function->values[i]];
    }
    const uint32_t depth = entries->depths[i] & (BUSTER_TIER0_WINDOWS - 1);
    const uint32_t floating = entries->floating_depths[i] & (BUSTER_TIER0_WINDOWS - 1);
    const uint64_t registers = (uint64_t)tier0_integer_window[depth] | (uint64_t)tier0_floating_window[floating] << 24;
    memcpy(holes.registers, &registers, sizeof(registers));
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

// The argument moves at once: each destination register gets its source's value, every register below 32. The sources
// are distinct, as are the destinations, so the moves form paths and cycles (of general registers alone: a call's
// floating arguments only move down, and xchg has no XMM form), on bitmasks: a move whose destination no
// other move still reads goes first (the lowest such register); when none is left, the rest are cycles, and an xchg of
// the lowest destination with its source settles that move, the move that read the destination reading the source
// instead. A path's move takes a record, and a cycle of n moves n - 1 (its last move becomes a register's own).
static void tier0_parallel_moves(Tier0Writer* writer, const uint8_t* to, const uint8_t* from, uint32_t count)
{
    uint8_t source_of[32];
    uint8_t reader_of[32];
    uint32_t pending = 0;
    uint32_t sources = 0;
    for (uint32_t k = 0; k < count; k += 1)
    {
        if (to[k] != from[k])
        {
            source_of[to[k]] = from[k];
            reader_of[from[k]] = to[k];
            pending |= 1u << to[k];
            sources |= 1u << from[k];
        }
    }
    while (pending != 0)
    {
        const uint32_t ready = pending & ~sources;
        const uint32_t destination = (uint32_t)__builtin_ctz(ready != 0 ? ready : pending);
        const uint32_t source = source_of[destination];
        pending &= ~(1u << destination);
        if (ready != 0)
        {
            tier0_move(writer, destination, source);
            sources &= ~(1u << source);
        }
        else
        {
            tier0_put(writer, tier0_instruction(X86_FORM_XCHG_RM64_R64, (uint8_t)source, (uint8_t)destination, 0, 0, 0));
            const uint32_t reader = reader_of[destination];
            source_of[reader] = (uint8_t)source;
            reader_of[source] = (uint8_t)reader;
            sources &= ~(1u << destination);
            if (reader == source)
            {
                pending &= ~(1u << source);
                sources &= ~(1u << source);
            }
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

// A call: the depths below its arguments (and an indirect call's callee) saved, the stack arguments stored, the register
// ones moved into place, with an indirect call's callee into r11 (no argument's register, nor al's), al for a variadic
// call, the call, the result moved to its depth, and the saved depths restored.
static void tier0_emit_call(const Tier0Function* function, const Tier0Entries* entries, const Tier0Frame* frame, uint32_t i, Tier0Writer* writer)
{
    const uint32_t call = (uint32_t)function->values[i];
    const uint32_t first = function->call_first[call];
    const uint32_t count = function->call_argument_counts[call];
    const bool indirect = function->call_targets[call] == 0;
    uint32_t integer_arguments = 0;
    uint32_t floating_arguments = 0;
    for (uint32_t a = 0; a < count; a += 1)
    {
        const bool floating = tier0_class_floating(function->argument_classes[first + a]);
        integer_arguments += floating ? 0 : 1;
        floating_arguments += floating ? 1 : 0;
    }
    // An indirect call's callee is at depth `below`, under its arguments, where the result goes.
    const uint32_t below = entries->depths[i] - integer_arguments - (indirect ? 1 : 0);
    const uint32_t arguments_base = below + (indirect ? 1 : 0);
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
    uint8_t to[BUSTER_TIER0_MAX_CALL_ARGUMENTS + 1];
    uint8_t from[BUSTER_TIER0_MAX_CALL_ARGUMENTS + 1];
    uint32_t moves = 0;
    uint32_t integer = 0;
    uint32_t floating = 0;
    for (uint32_t a = 0; a < count; a += 1)
    {
        const uint32_t class = function->argument_classes[first + a];
        const AbiLocation location = function->argument_locations[first + a];
        const bool is_floating = tier0_class_floating(class);
        const uint32_t source = is_floating ? BUSTER_TIER0_XMM(floating_below + floating) : tier0_integer_depths[arguments_base + integer];
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
    if (indirect)
    {
        to[moves] = X86_REGISTER_R11;
        from[moves] = tier0_integer_depths[below];
        moves += 1;
    }
    tier0_parallel_moves(writer, to, from, moves);
    if (function->call_variadic[call])
    {
        tier0_put(writer, tier0_instruction(X86_FORM_MOV_R32_IMM32, 0, X86_REGISTER_RAX, 0, 0, function->calls[call].vector_registers));
    }
    if (indirect)
    {
        tier0_put(writer, tier0_instruction(X86_FORM_CALL_RM64, 0, X86_REGISTER_R11, BUSTER_X86_REGISTER_DIRECT, 0, 0));
    }
    else
    {
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
    }
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

// A copy of `value` bytes, from the address at depth − 1 to the one at depth − 2, through its stacks' first free
// register: from 16 bytes, 16 at a time in an XMM register (movups), the last 16 ending at the end, overlapping the ones
// before; below 16, two pieces of a general register's 8, 4, 2 or 1 bytes, the second ending at the end (one when
// that is the whole). The ranges are the same or apart, so overlapping pieces copy what they would apart.
static void tier0_emit_copy(const Tier0Function* function, const Tier0Entries* entries, uint32_t i, Tier0Writer* writer)
{
    const uint32_t size = (uint32_t)function->values[i];
    const uint32_t depth = entries->depths[i];
    const uint8_t destination = tier0_integer_depths[depth - 2];
    const uint8_t source = tier0_integer_depths[depth - 1];
    if (size >= 16)
    {
        const uint8_t scratch = entries->floating_depths[i];
        for (uint32_t offset = 0; offset < size; offset += 16)
        {
            const int32_t at = (int32_t)(offset + 16 <= size ? offset : size - 16);
            tier0_put(writer, tier0_instruction(X86_FORM_MOVUPS_X_XM, scratch, source, BUSTER_X86_MEMORY_BASE, at, 0));
            tier0_put(writer, tier0_instruction(X86_FORM_MOVUPS_XM_X, scratch, destination, BUSTER_X86_MEMORY_BASE, at, 0));
        }
    }
    else
    {
        const uint8_t scratch = tier0_integer_depths[depth];
        const uint32_t piece = size >= 8 ? 8 : size >= 4 ? 4 : size >= 2 ? 2 : 1;
        const uint32_t load = piece == 8 ? X86_FORM_MOV_R64_RM64 : piece == 4 ? X86_FORM_MOV_R32_RM32 : piece == 2 ? X86_FORM_MOVZX_R32_RM16 : X86_FORM_MOVZX_R32_RM8;
        const uint32_t store = piece == 8 ? X86_FORM_MOV_RM64_R64 : piece == 4 ? X86_FORM_MOV_RM32_R32 : piece == 2 ? X86_FORM_MOV_RM16_R16 : X86_FORM_MOV_RM8_R8;
        tier0_put(writer, tier0_instruction(load, scratch, source, BUSTER_X86_MEMORY_BASE, 0, 0));
        tier0_put(writer, tier0_instruction(store, scratch, destination, BUSTER_X86_MEMORY_BASE, 0, 0));
        if (size > piece)
        {
            tier0_put(writer, tier0_instruction(load, scratch, source, BUSTER_X86_MEMORY_BASE, (int32_t)(size - piece), 0));
            tier0_put(writer, tier0_instruction(store, scratch, destination, BUSTER_X86_MEMORY_BASE, (int32_t)(size - piece), 0));
        }
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
    else if (kind == BUSTER_TYPED_COPY)
    {
        tier0_emit_copy(function, entries, i, writer);
    }
    else
    {
        tier0_emit_shift(entries, frame, i, writer);
    }
}

// How many records a deferred entry takes (and references: a call's one), counted from what its emitter decides rather
// than by running it: the vector path's measuring. Each term mirrors its emitter's step: a call's saves and restores,
// argument extensions, stack stores, its register moves (one per move but the last of each cycle, which the xchg
// before it resolves), al, the call and the result's move; division's saves, moves, extension and divide; a shift's
// swaps or its saved rcx. The scalar path counts by running the emitter, so that the twins check the count.
static uint32_t tier0_count_deferred(const Tier0Function* function, const Tier0Entries* entries, uint32_t i, uint32_t* references)
{
    const uint32_t kind = entries->key_kinds[i];
    uint32_t count = 0;
    if (kind == BUSTER_TYPED_CALL)
    {
        const uint32_t call = (uint32_t)function->values[i];
        const uint32_t first = function->call_first[call];
        const uint32_t arguments = function->call_argument_counts[call];
        // One pass from the last argument back: an argument's register is its stack's depth less the arguments of its
        // stack after it, so the counts before it aren't needed first. An indirect call's callee is past the first.
        const bool indirect = function->call_targets[call] == 0;
        uint8_t to[BUSTER_TIER0_MAX_CALL_ARGUMENTS + 1];
        uint8_t from[BUSTER_TIER0_MAX_CALL_ARGUMENTS + 1];
        uint32_t moves = 0;
        uint32_t integer_after = 0;
        uint32_t floating_after = 0;
        const uint32_t depth = entries->depths[i];
        const uint32_t floating_depth = entries->floating_depths[i];
        for (uint32_t a = arguments; a > 0; a -= 1)
        {
            const uint32_t class = function->argument_classes[first + a - 1];
            const AbiLocation location = function->argument_locations[first + a - 1];
            const bool is_floating = tier0_class_floating(class);
            const uint32_t source = is_floating ? BUSTER_TIER0_XMM(floating_depth - 1 - floating_after) : tier0_integer_depths[depth - 1 - integer_after];
            integer_after += is_floating ? 0 : 1;
            floating_after += is_floating ? 1 : 0;
            count += tier0_class_integer(class) && tier0_class_bytes(class) < 4 ? 1 : 0;
            const uint8_t reg = location.registers[0];
            const uint8_t destination = (uint8_t)((reg & BUSTER_ABI_REGISTER_VECTOR) != 0 ? BUSTER_TIER0_XMM(reg & 0x1f) : reg);
            const bool stack = location.kind == BUSTER_ABI_LOCATION_STACK;
            const bool move = !stack && destination != source;
            to[moves] = destination;
            from[moves] = (uint8_t)source;
            moves += move ? 1 : 0;
            count += stack ? 1 : 0;
        }
        const uint32_t below = depth - integer_after - (indirect ? 1 : 0);
        // (A direct call's `below` may be past the table: no move reads it then.)
        const uint8_t callee = indirect ? tier0_integer_depths[below] : X86_REGISTER_R11;
        to[moves] = X86_REGISTER_R11;
        from[moves] = callee;
        moves += callee != X86_REGISTER_R11 ? 1 : 0;
        const uint32_t floating_below = floating_depth - floating_after;
        const uint32_t saved = below < BUSTER_TIER0_CALLER_SAVED_DEPTHS ? below : BUSTER_TIER0_CALLER_SAVED_DEPTHS;
        count += 2 * (saved + floating_below);
        // The cycles among the moves, as tier0_parallel_moves finds them: the paths' moves taken away, what is left.
        uint8_t source_of[32];
        uint32_t pending = 0;
        uint32_t sources = 0;
        for (uint32_t k = 0; k < moves; k += 1)
        {
            source_of[to[k]] = from[k];
            pending |= 1u << to[k];
            sources |= 1u << from[k];
        }
        uint32_t ready = pending & ~sources;
        while (ready != 0)
        {
            const uint32_t destination = (uint32_t)__builtin_ctz(ready);
            pending &= ~(1u << destination);
            sources &= ~(1u << source_of[destination]);
            ready = pending & ~sources;
        }
        uint32_t cycles = 0;
        while (pending != 0)
        {
            uint32_t at = (uint32_t)__builtin_ctz(pending);
            cycles += 1;
            while ((pending >> at & 1) != 0)
            {
                pending &= ~(1u << at);
                at = source_of[at];
            }
        }
        count += moves - cycles;
        count += function->call_variadic[call] ? 1 : 0;
        count += 1;
        const uint32_t result = tier0_stack(function->classes[i]);
        count += (result == 1 && below != 0) || (result == 2 && floating_below != 0) ? 1 : 0;
        *references += indirect ? 0 : 1;
    }
    else if (kind == BUSTER_TYPED_DIV || kind == BUSTER_TYPED_MOD)
    {
        const bool home = entries->variants[i] == BUSTER_TIER0_VARIANT_HOME;
        const uint32_t depth = entries->depths[i];
        const uint32_t dividend = tier0_integer_depths[depth - (home ? 1 : 2)];
        const uint32_t result = kind == BUSTER_TYPED_MOD ? X86_REGISTER_RDX : X86_REGISTER_RAX;
        // rax is depth 0's register and rdx depth 2's: live when the depth passes them.
        const bool save_rax = depth > 0 && dividend != X86_REGISTER_RAX;
        const bool save_rdx = depth > 2 && dividend != X86_REGISTER_RDX;
        count = 2 * ((save_rax ? 1u : 0u) + (save_rdx ? 1u : 0u)) + (dividend != X86_REGISTER_RAX ? 1 : 0) + 2 + (dividend != result ? 1 : 0);
    }
    else if (kind == BUSTER_TYPED_COPY)
    {
        // A load and a store per 16 bytes or part of them; below 16, per piece, one or two.
        const uint32_t size = (uint32_t)function->values[i];
        const uint32_t piece = size >= 8 ? 8 : size >= 4 ? 4 : size >= 2 ? 2 : 1;
        count = size >= 16 ? 2 * ((size + 15) / 16) : size > piece ? 4 : 2;
    }
    else
    {
        const uint32_t value = tier0_integer_depths[entries->depths[i] - 2];
        count = value == X86_REGISTER_RCX ? 3 : 4;
    }
    return count;
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
    if (function->incoming_variadic != 0)
    {
        // Snapshot before any parameter home move can overwrite an unnamed argument. Full unaligned baseline
        // XMM stores preserve all 128 bits without reading AL, adding branches or touching FP status/control.
        for (uint32_t general = 0; general < BUSTER_ABI_INTEGER_ARGUMENTS; general += 1)
        {
            tier0_put(writer, tier0_instruction(X86_FORM_MOV_RM64_R64, abi_integer_arguments[general], X86_REGISTER_RSP, BUSTER_X86_MEMORY_BASE,
                (int32_t)(frame->variadic_area + general * BUSTER_ABI_SLOT_BYTES), 0));
        }
        for (uint32_t vector = 0; vector < BUSTER_ABI_VECTOR_ARGUMENTS; vector += 1)
        {
            tier0_put(writer, tier0_instruction(X86_FORM_MOVUPS_XM_X, vector, X86_REGISTER_RSP, BUSTER_X86_MEMORY_BASE,
                (int32_t)(frame->variadic_area + BUSTER_ABI_SAVE_AREA_VECTOR_OFFSET + vector * BUSTER_ABI_SAVE_AREA_VECTOR_SLOT_BYTES), 0));
        }
    }
    // Each parameter into its home: a register's moved or stored, and a register home of one on the stack loaded from its
    // incoming slot. No home is an arrival register (tier0_assign_homes), so the order doesn't matter.
    const uint32_t above = 8 * frame->pushes + 8;
    for (uint32_t p = 0; p < function->parameter_count; p += 1)
    {
        const AbiLocation location = function->parameters[p];
        const uint32_t class = function->local_classes[p];
        const bool address_taken = (function->local_flags[p] & BUSTER_TYPED_LOCAL_ADDRESS_TAKEN) != 0;
        if (location.kind == BUSTER_ABI_LOCATION_STACK && homes[p].is_register)
        {
            const int32_t incoming = (int32_t)(frame->size + above + location.stack_offset);
            tier0_put(writer, tier0_instruction(tier0_value_load_form(class), homes[p].reg, X86_REGISTER_RSP, BUSTER_X86_MEMORY_BASE, incoming, 0));
        }
        else if (location.kind == BUSTER_ABI_LOCATION_REGISTERS && (references[p] != 0 || address_taken))
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
// How many records tier0_emit_prologue writes, without writing them: the pushes, the frame's sub, and each parameter's
// move, store or load into its home, after the incoming variadic saves (a store per argument register) when it has them.
// The epilogue's are the sub's add, the pops and the ret.
static uint32_t tier0_count_prologue(const Tier0Function* function, const Tier0Frame* frame, const Tier0Home* homes, const uint32_t* references)
{
    uint32_t count = frame->pushes + (frame->size != 0 ? 1 : 0);
    count += function->incoming_variadic != 0 ? BUSTER_ABI_INTEGER_ARGUMENTS + BUSTER_ABI_VECTOR_ARGUMENTS : 0;
    for (uint32_t p = 0; p < function->parameter_count; p += 1)
    {
        const AbiLocation location = function->parameters[p];
        const bool address_taken = (function->local_flags[p] & BUSTER_TYPED_LOCAL_ADDRESS_TAKEN) != 0;
        const bool arrives = location.kind == BUSTER_ABI_LOCATION_REGISTERS && (references[p] != 0 || address_taken);
        const bool stays = homes[p].is_register && homes[p].reg == (location.registers[0] & 0x1f);
        count += location.kind == BUSTER_ABI_LOCATION_STACK && homes[p].is_register ? 1 : 0;
        count += arrives && !stays ? 1 : 0;
    }
    return count;
}

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
// The passes after fusion

// The deferred entries are those whose template column says so (a deferred slot, or a template at the wrong depth).
static bool tier0_is_deferred(uint16_t entry)
{
    return entry == BUSTER_TIER0_ENTRY_DEFERRED || entry == BUSTER_TIER0_SLOT_DEFERRED;
}

// Selection: each entry's template from its key's slot (ABSORBED for an absorbed entry; DEFERRED for a deferred slot
// or a template whose fixed registers need another depth; with HOME_DEPENDENT until the homes are known), and what the
// body touches, into the frame: the depth registers it reaches, the templates' fixed registers, and for a call
// everything caller-saved, with the call save area's and the fixed slots' sizes. Returns the first entry without a
// template (the count if none), and sets *error to TIER0_ERROR_DEPTH for depths past the tables.
static uint32_t tier0_select(const Tier0Function* function, Tier0Entries* entries, Tier0Frame* frame, Tier0Error* error)
{
    uint32_t unsupported = function->count;
    int32_t integer_top = -1;
    int32_t floating_top = -1;
    uint32_t clobbers = 0;
    uint32_t save_slots = 0;
    bool fixed = false;
    bool calls = false;
    for (uint32_t i = 0; i < function->count; i += 1)
    {
        uint16_t entry = BUSTER_TIER0_ENTRY_ABSORBED;
        if (!entries->absorbed[i])
        {
            entry = tier0_slot(entries->key_kinds[i], entries->key_classes[i], entries->variants[i]);
            const int32_t depth = entries->depths[i];
            const int32_t floating = entries->floating_depths[i];
            if (entry == BUSTER_TIER0_SLOT_UNSUPPORTED)
            {
                unsupported = unsupported == function->count ? i : unsupported;
            }
            else if (entry != BUSTER_TIER0_SLOT_DEFERRED)
            {
                const Tier0TemplateInfo* info = &tier0_template_infos[entry & BUSTER_TIER0_SLOT_TEMPLATE];
                if (info->required_depth != BUSTER_TIER0_ANY_DEPTH && info->required_depth != depth)
                {
                    entry = BUSTER_TIER0_ENTRY_DEFERRED;
                }
                else
                {
                    const int32_t top = info->integer_reach != 0 ? depth + info->integer_reach - 3 : -1;
                    const int32_t floating_reach = info->floating_reach != 0 ? floating + info->floating_reach - 3 : -1;
                    integer_top = top > integer_top ? top : integer_top;
                    floating_top = floating_reach > floating_top ? floating_reach : floating_top;
                    clobbers |= info->clobbers;
                }
            }
            if (tier0_is_deferred(entry))
            {
                if (function->kinds[i] == BUSTER_TYPED_CALL)
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
                    const int32_t below = depth - (int32_t)integer_arguments - (function->call_targets[call] == 0 ? 1 : 0);
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
                else if (function->kinds[i] == BUSTER_TYPED_COPY)
                {
                    // A copy's scratch, its stacks' first free register: an XMM one from 16 bytes, a general one below.
                    const bool wide = function->values[i] >= 16;
                    integer_top = !wide && depth > integer_top ? depth : integer_top;
                    floating_top = wide && floating > floating_top ? floating : floating_top;
                }
                else
                {
                    // Division and shifts by a register: rax, rdx and rcx, saved around them in the fixed slots.
                    clobbers |= 1u << X86_REGISTER_RAX | 1u << X86_REGISTER_RDX | 1u << X86_REGISTER_RCX;
                    fixed = true;
                }
            }
        }
        entries->templates[i] = entry;
    }
    *error = integer_top >= BUSTER_TIER0_INTEGER_DEPTHS || floating_top >= BUSTER_TIER0_FLOATING_DEPTHS ? TIER0_ERROR_DEPTH : TIER0_OK;
    frame->integer_top = integer_top;
    frame->floating_top = floating_top;
    frame->clobbers = clobbers | tier0_depth_registers(integer_top, floating_top);
    frame->save_slots = save_slots;
    frame->fixed_slots = fixed ? 2 : 0;
    frame->calls = calls;
    return unsupported;
}

// The counts the output's columns need, after the homes: each entry's final template (a home-dependent slot's case
// for its homes) and length (a deferred entry's as its emitter counts it), and the branch records, references and
// choice requests.
typedef struct Tier0Counts Tier0Counts;
struct Tier0Counts
{
    uint32_t branches;
    uint32_t references;
    uint32_t choices;
};

static Tier0Counts tier0_measure(const Tier0Function* function, Tier0Entries* entries, const Tier0Frame* frame, const Tier0Home* homes)
{
    Tier0Counts counts = { 0, 0, 0 };
    for (uint32_t i = 0; i < function->count; i += 1)
    {
        const uint16_t entry = entries->templates[i];
        uint32_t length = 0;
        if (tier0_is_deferred(entry))
        {
            Tier0Writer counter = { 0, 0, 0, 0 };
            tier0_emit_deferred(function, entries, frame, homes, i, &counter);
            length = counter.count;
            counts.references += counter.reference_count;
        }
        else if (entry != BUSTER_TIER0_ENTRY_ABSORBED)
        {
            uint32_t index = entry & BUSTER_TIER0_SLOT_TEMPLATE;
            if ((entry & BUSTER_TIER0_SLOT_HOME_DEPENDENT) != 0)
            {
                const Tier0TemplateInfo* first = &tier0_template_infos[index];
                const uint8_t kind = function->kinds[i];
                const bool own = kind == BUSTER_TYPED_LOCAL || kind == BUSTER_TYPED_LOCAL_ADDRESS || kind == BUSTER_TYPED_STORE_LOCAL || kind == BUSTER_TYPED_SET_LOCAL;
                const bool a_register = first->a_offset != BUSTER_TIER0_NO_OFFSET && homes[function->values[i - first->a_offset]].is_register != 0;
                const bool b_register = first->b_offset != BUSTER_TIER0_NO_OFFSET ? homes[function->values[i - first->b_offset]].is_register != 0
                                                                                  : own && homes[function->values[i]].is_register != 0;
                index += (a_register ? 1u : 0u) + (b_register ? 2u : 0u);
                entries->templates[i] = (uint16_t)index;
            }
            const Tier0TemplateInfo* info = &tier0_template_infos[index];
            length = info->length;
            counts.branches += info->branch != 0xff ? 1 : 0;
            counts.references += info->fixup != 0xff ? 1 : 0;
            counts.choices += info->choice != 0xff ? 1 : 0;
        }
        entries->lengths[i] = (uint16_t)length;
    }
    return counts;
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
static bool tier0_push_output(Tier0Code* code, Arena* output, uint32_t records, Tier0Counts counts)
{
    code->records = BUSTER_ARENA_PUSH_ARRAY(output, X86Instruction, (uint64_t)records + BUSTER_TIER0_TEMPLATE_RECORDS);
    code->branches = BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, (uint64_t)counts.branches + 1);
    code->targets = BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, (uint64_t)counts.branches + 1);
    code->references = BUSTER_ARENA_PUSH_ARRAY(output, LinkReference, (uint64_t)counts.references + 1);
    code->count = records;
    return code->records != 0 && code->branches != 0 && code->targets != 0 && code->references != 0;
}

// A CHOICE record's context: whether its rm is a register (and rax), inc and dec allowed, and the flags live after it.
static uint16_t tier0_choice_context(const X86Instruction* record, uint8_t live)
{
    const bool reg = record->mode == BUSTER_X86_REGISTER_DIRECT;
    uint32_t context = BUSTER_X86_CONTEXT_INC_DEC | live;
    context |= reg ? BUSTER_X86_CONTEXT_REGISTER : 0;
    context |= reg && record->rm == X86_REGISTER_RAX ? BUSTER_X86_CONTEXT_ACCUMULATOR : 0;
    return (uint16_t)context;
}

// The record for a chosen form (x86_choice_instruction), its fixup flag kept; none chosen keeps it as it is.
static void tier0_apply_choice(X86Instruction* record, uint32_t form, int64_t value)
{
    if (form != X86_FORM_NONE)
    {
        const uint16_t fixup = (uint16_t)(record->form & BUSTER_X86_FORM_FIXUP);
        *record = x86_choice_instruction(form, *record, value);
        record->form |= fixup;
    }
}

// Every form's shape (BUSTER_TIER0_SHAPE_*). Returns false for an immediate past 8 bytes.
static bool tier0_build_choice_shapes(void)
{
    bool valid = true;
    for (uint32_t form = 0; form < X86_FORM_COUNT; form += 1)
    {
        const X86FormDescription* description = x86_form_description(form);
        const uint32_t syntax = description->syntax;
        valid &= description->immediate_bytes <= 8;
        uint32_t shape = description->immediate_bytes & BUSTER_TIER0_SHAPE_BYTES;
        shape |= syntax == X86_SYNTAX_ACC_IMM || syntax == X86_SYNTAX_IMM ? BUSTER_TIER0_SHAPE_CLEAR : 0;
        shape |= syntax == X86_SYNTAX_RM_REG ? BUSTER_TIER0_SHAPE_TWICE : 0;
        tier0_choice_shapes[form] = (uint8_t)shape;
    }
    return valid;
}

// The immediates' choice requests, gathered by emission: each CHOICE record's index, row, value and context.
typedef struct Tier0Choices Tier0Choices;
struct Tier0Choices
{
    uint32_t* records;
    uint16_t* rows;
    int64_t* values;
    uint16_t* contexts;
    uint16_t* forms;
    uint32_t count;
};

// Emission for the scalar path, in entry order: the prologue; each entry's template expanded at its place, field by
// field from its specifications, its choice request, branch and reference, or a deferred entry's records; then the
// epilogue. tier0_emit_vector is its twin.
static void tier0_emit_body(const Tier0Function* function, const Tier0Entries* entries, const Tier0Frame* frame, const Tier0Home* homes, const uint32_t* references,
                            Tier0Code* code, Tier0Choices* choices)
{
    Tier0Writer writer = { code->records, 0, code->references, 0 };
    tier0_emit_prologue(function, frame, homes, references, &writer);
    uint32_t branches = 0;
    for (uint32_t i = 0; i < function->count; i += 1)
    {
        const uint16_t entry = entries->templates[i];
        const uint32_t position = entries->positions[i];
        if (tier0_is_deferred(entry))
        {
            writer.count = position;
            tier0_emit_deferred(function, entries, frame, homes, i, &writer);
            code->deferred += 1;
        }
        else if (entry != BUSTER_TIER0_ENTRY_ABSORBED && tier0_template_infos[entry].length != 0)
        {
            const Tier0TemplateInfo* info = &tier0_template_infos[entry];
            const Tier0Holes holes = tier0_holes_scalar(function, entries, homes, i, info);
            const Tier0Template* template = &tier0_templates[entry];
            for (uint32_t r = 0; r < info->length; r += 1)
            {
                code->records[position + r] = tier0_record_scalar(&template->specs[r], &holes);
            }
            if (info->choice != 0xff)
            {
                const uint32_t at = position + info->choice;
                choices->records[choices->count] = at;
                choices->rows[choices->count] = info->choice_row;
                choices->values[choices->count] = info->choice_static != 0 ? (int64_t)info->choice_immediate : (int64_t)holes.k;
                choices->contexts[choices->count] = tier0_choice_context(&code->records[at], info->choice_live);
                choices->count += 1;
            }
            if (info->branch != 0xff)
            {
                const uint32_t target = info->end != 0 ? function->count : (uint32_t)function->values[i];
                code->branches[branches] = position + info->branch;
                code->targets[branches] = entries->positions[target];
                branches += 1;
            }
            if (info->fixup != 0xff)
            {
                code->references[writer.reference_count] = (LinkReference){
                    .instruction = position + info->fixup,
                    .field = X86_FIXUP_DISPLACEMENT,
                    .kind = BUSTER_LINK_FIXUP_PC32,
                    .target = (SymbolId)function->values[i],
                    .addend = 0,
                };
                writer.reference_count += 1;
            }
        }
    }
    writer.count = entries->positions[function->count];
    tier0_emit_epilogue(frame, &writer);
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
    entries->words = BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, padded);
    return entries->words != 0 && entries->key_kinds != 0 && entries->key_classes != 0 && entries->variants != 0 && entries->absorbed != 0 && entries->integer_effects != 0 &&
           entries->floating_effects != 0 && entries->depths != 0 && entries->floating_depths != 0 && entries->templates != 0 && entries->lengths != 0 &&
           entries->positions != 0;
}

static uint32_t tier0_select_vector(const Tier0Function* function, Tier0Entries* entries, Tier0Frame* frame, Tier0Error* error);
static Tier0Counts tier0_measure_vector(const Tier0Function* function, Tier0Entries* entries, const Tier0Home* homes, uint32_t prologue);
static void tier0_emit_vector(const Tier0Function* function, const Tier0Entries* entries, const Tier0Frame* frame, const Tier0Home* homes, const uint32_t* references,
                              Tier0Code* code, Tier0Choices* choices);

// tier0_apply_choice for every request, for the vector path: each chosen form's record from its shape (one byte) rather
// than its description, on the record's two 8-byte halves (the immediate and the displacement; the operands, the form,
// vvvv and the decorations) rather than its fields, without a branch: the form with the fixup flag kept, the operands
// cleared or the destination taken twice, and the immediate's field (and a 64-bit immediate's high half in the
// displacement); a request whose form is X86_FORM_NONE keeps its record.
static void tier0_apply_choices_vector(X86Instruction* records, const Tier0Choices* choices)
{
    _Static_assert(offsetof(X86Instruction, displacement) == 4 && offsetof(X86Instruction, reg) == 8 && offsetof(X86Instruction, form) == 12,
                   "a record's halves: immediate and displacement; reg, rm, index, mode, form, vvvv and decorations");
    const uint32_t* indices = choices->records;
    const uint16_t* forms = choices->forms;
    const int64_t* values = choices->values;
    const uint32_t count = choices->count;
    uint8_t* bytes_of = (uint8_t*)records;
    for (uint32_t q = 0; q < count; q += 1)
    {
        uint8_t* record = bytes_of + (uint64_t)indices[q] * sizeof(X86Instruction);
        uint64_t low = 0;
        uint64_t high = 0;
        memcpy(&low, record, sizeof(low));
        memcpy(&high, record + 8, sizeof(high));
        const uint32_t form = forms[q];
        const uint32_t shape = tier0_choice_shapes[form];
        const uint64_t value = (uint64_t)values[q];
        const uint32_t bytes = shape & BUSTER_TIER0_SHAPE_BYTES;
        const bool clear = (shape & BUSTER_TIER0_SHAPE_CLEAR) != 0;
        const bool twice = (shape & BUSTER_TIER0_SHAPE_TWICE) != 0;
        const uint32_t shift = (bytes >= 4 ? 32 : 64 - 8 * bytes) & 63;
        const uint64_t immediate = bytes == 0 ? 0 : (uint32_t)((int64_t)(value << shift) >> shift);
        const uint64_t displacement = bytes == 8 ? value >> 32 : clear ? 0 : low >> 32;
        const uint64_t operands = high & 0xffffffffull;
        const uint64_t chosen_operands = clear ? 0 : twice ? (operands & ~0xffull) | (operands >> 8 & 0xff) : operands;
        const uint64_t chosen_form = form | (high >> 32 & BUSTER_X86_FORM_FIXUP);
        const uint64_t chosen_low = immediate | displacement << 32;
        const uint64_t chosen_high = chosen_operands | chosen_form << 32 | (high & 0xffff000000000000ull);
        low = form == X86_FORM_NONE ? low : chosen_low;
        high = form == X86_FORM_NONE ? high : chosen_high;
        memcpy(record, &low, sizeof(low));
        memcpy(record + 8, &high, sizeof(high));
    }
}

// What both paths share once the fusion and the depths are in the entries' columns, each pass in the path's version:
// selection, the references' count, the homes and the frame (shared), measuring and positions (the vector path's
// measuring sums them), the output, emission and the immediates' choice (x86_choose_forms or its twin).
static Tier0Error tier0_generate(const Tier0Function* function, Tier0Entries* entries, Arena* scratch, Arena* output, Tier0Code* code, bool vector)
{
    Tier0Frame frame;
    memset(&frame, 0, sizeof(frame));
    Tier0Error error = tier0_frame_input_valid(function) ? TIER0_OK : TIER0_ERROR_UNSUPPORTED;
    const uint32_t unsupported = error == TIER0_OK ? (vector ? tier0_select_vector(function, entries, &frame, &error) : tier0_select(function, entries, &frame, &error)) : function->count;
    if (unsupported != function->count)
    {
        error = TIER0_ERROR_UNSUPPORTED;
        code->node = unsupported;
    }
    const uint32_t locals = function->local_count;
    Tier0Home* homes = BUSTER_ARENA_PUSH_ARRAY(scratch, Tier0Home, locals + 1);
    uint32_t* references = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, locals + 1);
    uint32_t* order = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, locals + 1);
    if (error == TIER0_OK && (homes == 0 || references == 0 || order == 0))
    {
        error = TIER0_ERROR_MEMORY;
    }
    Tier0Choices choices;
    memset(&choices, 0, sizeof(choices));
    if (error == TIER0_OK)
    {
        if (vector)
        {
            tier0_count_references_vector(function, references);
        }
        else
        {
            tier0_count_references(function, references);
        }
        tier0_assign_homes(function, &frame, homes, references, order, vector);
        error = tier0_layout_frame(function, &frame, homes, references, vector) ? TIER0_OK : TIER0_ERROR_UNSUPPORTED;
    }
    if (error == TIER0_OK)
    {
        Tier0Counts counts;
        uint32_t epilogue = 0;
        if (vector)
        {
            counts = tier0_measure_vector(function, entries, homes, tier0_count_prologue(function, &frame, homes, references));
            epilogue = (frame.size != 0 ? 1 : 0) + frame.pushes + 1;
        }
        else
        {
            Tier0Writer counter = { 0, 0, 0, 0 };
            tier0_emit_prologue(function, &frame, homes, references, &counter);
            counts = tier0_measure(function, entries, &frame, homes);
            tier0_positions_scalar(function, entries, counter.count);
            counter.count = 0;
            tier0_emit_epilogue(&frame, &counter);
            epilogue = counter.count;
        }
        // The requests' columns have room for the vector path's last compressed store of 8.
        choices.records = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, counts.choices + 8);
        choices.rows = BUSTER_ARENA_PUSH_ARRAY(scratch, uint16_t, counts.choices + 8);
        choices.values = BUSTER_ARENA_PUSH_ARRAY(scratch, int64_t, counts.choices + 8);
        choices.contexts = BUSTER_ARENA_PUSH_ARRAY(scratch, uint16_t, counts.choices + 8);
        choices.forms = BUSTER_ARENA_PUSH_ARRAY(scratch, uint16_t, counts.choices + 8);
        const bool room = choices.records != 0 && choices.rows != 0 && choices.values != 0 && choices.contexts != 0 && choices.forms != 0;
        if (!room || !tier0_push_output(code, output, entries->positions[function->count] + epilogue, counts))
        {
            error = TIER0_ERROR_MEMORY;
        }
    }
    if (error == TIER0_OK)
    {
        if (vector)
        {
            tier0_emit_vector(function, entries, &frame, homes, references, code, &choices);
            x86_choose_forms(choices.rows, choices.values, choices.contexts, choices.count, choices.forms);
        }
        else
        {
            tier0_emit_body(function, entries, &frame, homes, references, code, &choices);
            x86_choose_forms_scalar(choices.rows, choices.values, choices.contexts, choices.count, choices.forms);
        }
        if (vector)
        {
            tier0_apply_choices_vector(code->records, &choices);
        }
        else
        {
            for (uint32_t q = 0; q < choices.count; q += 1)
            {
                tier0_apply_choice(&code->records[choices.records[q]], choices.forms[q], choices.values[q]);
            }
        }
    }
    return error;
}

// Ends a compilation: on an error, what was pushed to `output` is popped and only the error and its entry are kept.
static Tier0Code tier0_conclude(Tier0Code code, Tier0Error error, Arena* output, uint64_t output_length)
{
    if (error != TIER0_OK)
    {
        arena_pop_to(output, output_length);
        const uint32_t node = code.node;
        memset(&code, 0, sizeof(code));
        code.node = node;
    }
    code.error = error;
    return code;
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
    if (error == TIER0_OK)
    {
        memset(labels, 0, ((uint64_t)count / 64 + 2) * sizeof(uint64_t));
        tier0_labels(function, labels);
        tier0_fuse_scalar(function, labels, &entries);
        tier0_depths_scalar(function, &entries);
        error = tier0_generate(function, &entries, scratch.arena, output, &code, false);
    }
    code = tier0_conclude(code, error, output, output_length);
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
    else if (!tier0_frame_input_valid(function))
    {
        tier0_reject(&validator, 0, "invalid incoming variadic metadata or address-only local role");
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
        if (function->local_sizes != 0 && function->local_alignments == 0)
        {
            tier0_reject(&validator, 0, "the objects' sizes without their alignments");
        }
        uint64_t object_bytes = 0;
        for (uint32_t l = 0; l < function->local_count && validator.check.error == TIER0_OK; l += 1)
        {
            if (tier0_is_object(function, l))
            {
                if (function->local_classes[l] != BUSTER_TYPED_CLASS_VOID || (function->local_flags[l] & BUSTER_TYPED_LOCAL_ADDRESS_TAKEN) == 0 ||
                    l < function->parameter_count || function->local_alignments[l] > BUSTER_TIER0_MAX_OBJECT_ALIGNMENT_LOG2)
                {
                    tier0_reject(&validator, 0, "an object that isn't an address-taken local of class VOID, or a parameter, or aligned past 16");
                }
                object_bytes += ((uint64_t)function->local_sizes[l] + 15) & ~15ull;
            }
            else if (function->local_classes[l] == BUSTER_TYPED_CLASS_VOID || function->local_classes[l] >= BUSTER_TYPED_CLASSES)
            {
                tier0_reject(&validator, 0, "a local without a class");
            }
        }
        if (object_bytes > BUSTER_TIER0_MAX_OBJECT_BYTES)
        {
            tier0_reject(&validator, 0, "objects past BUSTER_TIER0_MAX_OBJECT_BYTES");
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
                if (value >= function->local_count ||
                    (kind == BUSTER_TYPED_LOCAL && (function->local_classes[value] != class || tier0_is_object(function, value) ||
                                                    (function->local_flags[value] & BUSTER_TYPED_LOCAL_ADDRESS_ONLY) != 0)) ||
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
                const bool bad_target = local ? value >= function->local_count || function->local_classes[value] != class || tier0_is_object(function, value) ||
                                                    (function->local_flags[value] & BUSTER_TYPED_LOCAL_ADDRESS_ONLY) != 0
                                              : value == 0 || value > UINT32_MAX;
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
                if (function->call_targets[value] == 0 && tier0_pop(&validator, i) != BUSTER_TYPED_CLASS_U64)
                {
                    tier0_reject(&validator, i, "an indirect call's callee that isn't U64");
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
            case BUSTER_TYPED_COPY:
            {
                const uint32_t source = tier0_pop(&validator, i);
                const uint32_t destination = tier0_pop(&validator, i);
                if (source != BUSTER_TYPED_CLASS_U64 || destination != BUSTER_TYPED_CLASS_U64 || class != BUSTER_TYPED_CLASS_VOID || value == 0 ||
                    value > BUSTER_TIER0_MAX_COPY_BYTES)
                {
                    tier0_reject(&validator, i, "a copy's addresses, class or size");
                }
                break;
            }
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

// The vector fusion's tables, built from the slots once (tier0_build_vector_tables), so that both paths follow one
// table: a kind's shape (first operand, last operand, push: BUSTER_TIER0_STACK_* in bits 0-1, 2 and 3-4) and flags, a
// class's stack, width and kind, and the properties of a (kind, key class): which fused variants exist. The properties
// are a byte table indexed by (row << 4 | class), where a kind's row groups the kinds with the same properties over the
// classes (at most 16 rows, so that the table is 256 bytes, two vpermi2b and a blend), and a CONVERT's (to << 4 | from)
// table says which conversions take a home.
#define BUSTER_TIER0_PROPERTY_CONST 0x01
#define BUSTER_TIER0_PROPERTY_HOME 0x02
#define BUSTER_TIER0_PROPERTY_HOME_CONST 0x04
#define BUSTER_TIER0_PROPERTY_RMW_CONST 0x08
#define BUSTER_TIER0_PROPERTY_RMW_HOME 0x10
#define BUSTER_TIER0_PROPERTY_BRANCH 0x20
#define BUSTER_TIER0_PROPERTY_ADDRESS 0x40
#define BUSTER_TIER0_PROPERTY_ADDRESS_CONST 0x80
// Kind flags.
#define BUSTER_TIER0_KIND_COMPARE 0x01
#define BUSTER_TIER0_KIND_BRANCH 0x02
#define BUSTER_TIER0_KIND_SHIFT 0x04
#define BUSTER_TIER0_KIND_EXEMPT 0x08
#define BUSTER_TIER0_KIND_HOME_CONST 0x10
#define BUSTER_TIER0_KIND_RMW 0x20
#define BUSTER_TIER0_KIND_CONTROL 0x40
#define BUSTER_TIER0_ROWS 16

static uint8_t tier0_vector_shapes[BUSTER_TYPED_KIND_LIMIT] __attribute__((aligned(64)));
static uint8_t tier0_vector_kind_flags[BUSTER_TYPED_KIND_LIMIT] __attribute__((aligned(64)));
static uint8_t tier0_vector_rows[BUSTER_TYPED_KIND_LIMIT] __attribute__((aligned(64)));
static uint8_t tier0_vector_stacks[BUSTER_TYPED_KIND_LIMIT] __attribute__((aligned(64)));
static uint8_t tier0_vector_wide[BUSTER_TYPED_KIND_LIMIT] __attribute__((aligned(64)));
static uint8_t tier0_vector_integer[BUSTER_TYPED_KIND_LIMIT] __attribute__((aligned(64)));
static uint8_t tier0_vector_properties[BUSTER_TIER0_ROWS * BUSTER_TYPED_CLASS_LIMIT] __attribute__((aligned(64)));
static uint8_t tier0_vector_convert_home[BUSTER_TYPED_CLASS_LIMIT * BUSTER_TYPED_CLASS_LIMIT] __attribute__((aligned(64)));

// A (kind, class)'s properties, from its slots.
static uint8_t tier0_properties_of(uint32_t kind, uint32_t class)
{
    uint8_t properties = 0;
    properties |= tier0_slot(kind, class, BUSTER_TIER0_VARIANT_CONST) != BUSTER_TIER0_SLOT_UNSUPPORTED ? BUSTER_TIER0_PROPERTY_CONST : 0;
    properties |= tier0_slot(kind, class, BUSTER_TIER0_VARIANT_HOME) != BUSTER_TIER0_SLOT_UNSUPPORTED ? BUSTER_TIER0_PROPERTY_HOME : 0;
    properties |= tier0_slot(kind, class, BUSTER_TIER0_VARIANT_HOME_CONST) != BUSTER_TIER0_SLOT_UNSUPPORTED ? BUSTER_TIER0_PROPERTY_HOME_CONST : 0;
    properties |= tier0_slot(kind, class, BUSTER_TIER0_VARIANT_RMW_CONST) != BUSTER_TIER0_SLOT_UNSUPPORTED ? BUSTER_TIER0_PROPERTY_RMW_CONST : 0;
    properties |= tier0_slot(kind, class, BUSTER_TIER0_VARIANT_RMW_HOME) != BUSTER_TIER0_SLOT_UNSUPPORTED ? BUSTER_TIER0_PROPERTY_RMW_HOME : 0;
    bool branch = true;
    for (uint32_t v = 0; v <= BUSTER_TIER0_VARIANT_HOME_HOME; v += 1)
    {
        branch &= tier0_slot(kind, class, BUSTER_TIER0_VARIANT_BRANCH_FALSE + v) != BUSTER_TIER0_SLOT_UNSUPPORTED;
        branch &= tier0_slot(kind, class, BUSTER_TIER0_VARIANT_BRANCH_TRUE + v) != BUSTER_TIER0_SLOT_UNSUPPORTED;
    }
    properties |= branch ? BUSTER_TIER0_PROPERTY_BRANCH : 0;
    bool address = kind == BUSTER_TYPED_LOAD || kind == BUSTER_TYPED_SET;
    bool address_const = kind == BUSTER_TYPED_SET;
    for (uint32_t s = 0; s < 4; s += 1)
    {
        if (kind == BUSTER_TYPED_LOAD)
        {
            address &= tier0_slot(kind, class, BUSTER_TIER0_VARIANT_INDEX(s)) != BUSTER_TIER0_SLOT_UNSUPPORTED;
        }
        else
        {
            address &= tier0_slot(kind, class, BUSTER_TIER0_VARIANT_INDEX_HOME(s)) != BUSTER_TIER0_SLOT_UNSUPPORTED;
            address_const &= tier0_slot(kind, class, BUSTER_TIER0_VARIANT_INDEX_CONST(s)) != BUSTER_TIER0_SLOT_UNSUPPORTED;
        }
    }
    address &= tier0_slot(kind, class, kind == BUSTER_TYPED_LOAD ? BUSTER_TIER0_VARIANT_OFFSET : BUSTER_TIER0_VARIANT_OFFSET_HOME) != BUSTER_TIER0_SLOT_UNSUPPORTED;
    properties |= address ? BUSTER_TIER0_PROPERTY_ADDRESS : 0;
    properties |= address_const ? BUSTER_TIER0_PROPERTY_ADDRESS_CONST : 0;
    return properties;
}

// Builds the vector tables. Returns false if the kinds need more than BUSTER_TIER0_ROWS rows of properties, or a
// property the vector path reads as all-or-none is some (a table the twins would disagree on).
static bool tier0_build_vector_tables(void)
{
    bool valid = true;
    uint8_t rows[BUSTER_TIER0_ROWS][BUSTER_TYPED_CLASS_LIMIT];
    uint32_t row_count = 1;
    memset(rows, 0, sizeof(rows));
    for (uint32_t kind = 0; kind < BUSTER_TYPED_KIND_LIMIT; kind += 1)
    {
        const Tier0Shape shape = tier0_shapes[kind];
        tier0_vector_shapes[kind] = (uint8_t)(shape.first | (shape.last == BUSTER_TIER0_STACK_OPERAND ? 4 : 0) | shape.push << 3);
        uint8_t flags = 0;
        flags |= kind >= BUSTER_TYPED_EQ && kind <= BUSTER_TYPED_GE ? BUSTER_TIER0_KIND_COMPARE : 0;
        flags |= kind == BUSTER_TYPED_BRANCH_FALSE || kind == BUSTER_TYPED_BRANCH_TRUE ? BUSTER_TIER0_KIND_BRANCH : 0;
        flags |= kind == BUSTER_TYPED_SHL || kind == BUSTER_TYPED_SHR ? BUSTER_TIER0_KIND_SHIFT : 0;
        flags |= kind == BUSTER_TYPED_DISCARD || kind == BUSTER_TYPED_STORE_LOCAL || kind == BUSTER_TYPED_STORE_GLOBAL ? BUSTER_TIER0_KIND_EXEMPT : 0;
        flags |= kind == BUSTER_TYPED_MUL || (kind >= BUSTER_TYPED_EQ && kind <= BUSTER_TYPED_GE) ? BUSTER_TIER0_KIND_HOME_CONST : 0;
        flags |= kind == BUSTER_TYPED_ADD || kind == BUSTER_TYPED_SUB || (kind >= BUSTER_TYPED_AND && kind <= BUSTER_TYPED_SHR) ? BUSTER_TIER0_KIND_RMW : 0;
        flags |= kind >= BUSTER_TYPED_BRANCH_FALSE && kind <= BUSTER_TYPED_JUMP_VALUE ? BUSTER_TIER0_KIND_CONTROL : 0;
        tier0_vector_kind_flags[kind] = flags;
        uint8_t row[BUSTER_TYPED_CLASS_LIMIT];
        for (uint32_t class = 0; class < BUSTER_TYPED_CLASS_LIMIT; class += 1)
        {
            row[class] = kind < BUSTER_TYPED_KINDS && kind != BUSTER_TYPED_CONVERT ? tier0_properties_of(kind, class) : 0;
            // A compare takes two locals exactly where it takes a local and a constant: the vector path reads one bit.
            if ((flags & BUSTER_TIER0_KIND_COMPARE) != 0)
            {
                const bool home_home = tier0_slot(kind, class, BUSTER_TIER0_VARIANT_HOME_HOME) != BUSTER_TIER0_SLOT_UNSUPPORTED;
                valid &= home_home == ((row[class] & BUSTER_TIER0_PROPERTY_HOME_CONST) != 0);
            }
        }
        uint32_t found = row_count;
        for (uint32_t r = 0; r < row_count && found == row_count; r += 1)
        {
            found = memcmp(rows[r], row, sizeof(row)) == 0 ? r : found;
        }
        if (found == row_count)
        {
            valid &= row_count < BUSTER_TIER0_ROWS;
            if (row_count < BUSTER_TIER0_ROWS)
            {
                memcpy(rows[row_count], row, sizeof(row));
                row_count += 1;
            }
        }
        tier0_vector_rows[kind] = (uint8_t)(found < BUSTER_TIER0_ROWS ? found : 0);
    }
    for (uint32_t r = 0; r < BUSTER_TIER0_ROWS; r += 1)
    {
        memcpy(tier0_vector_properties + r * BUSTER_TYPED_CLASS_LIMIT, rows[r], BUSTER_TYPED_CLASS_LIMIT);
    }
    for (uint32_t class = 0; class < BUSTER_TYPED_KIND_LIMIT; class += 1)
    {
        const uint32_t c = class & (BUSTER_TYPED_CLASS_LIMIT - 1);
        tier0_vector_stacks[class] = (uint8_t)(c < BUSTER_TYPED_CLASSES ? tier0_stack(c) : 0);
        tier0_vector_wide[class] = c < BUSTER_TYPED_CLASSES && tier0_class_bytes(c) == 8 ? 0xff : 0;
        tier0_vector_integer[class] = c < BUSTER_TYPED_CLASSES && tier0_class_integer(c) ? 0xff : 0;
    }
    for (uint32_t to = 0; to < BUSTER_TYPED_CLASS_LIMIT; to += 1)
    {
        for (uint32_t from = 0; from < BUSTER_TYPED_CLASS_LIMIT; from += 1)
        {
            const bool home = to < BUSTER_TYPED_CLASSES && from < BUSTER_TYPED_CLASSES &&
                              tier0_slot(BUSTER_TYPED_CONVERT, to, from | BUSTER_TIER0_VARIANT_CONVERT_HOME) != BUSTER_TIER0_SLOT_UNSUPPORTED;
            tier0_vector_convert_home[to << 4 | from] = home ? BUSTER_TIER0_PROPERTY_HOME : 0;
        }
    }
    return valid;
}

// The lanes of a 64-entry block from `first` that hold entries (0 <= first + lane < count).
static __mmask64 tier0_lanes(int64_t first, uint32_t count)
{
    const int64_t low = first < 0 ? -first : 0;
    const int64_t high = (int64_t)count - first;
    uint64_t mask = 0;
    if (high > low)
    {
        const uint64_t below_high = high >= 64 ? ~0ull : (1ull << high) - 1;
        const uint64_t below_low = low >= 64 ? ~0ull : (1ull << low) - 1;
        mask = below_high & ~below_low;
    }
    return (__mmask64)mask;
}

// 64 bytes of a column from entry `first`, zero outside the entries (no lane outside them is read).
static __m512i tier0_bytes_at(const uint8_t* column, int64_t first, uint32_t count)
{
    return _mm512_maskz_loadu_epi8(tier0_lanes(first, count), (const void*)((uintptr_t)column + (uintptr_t)first));
}

// A table lookup per byte: vpermb from a 64-byte table.
static __m512i tier0_lookup(const uint8_t* table, __m512i index)
{
    return _mm512_permutexvar_epi8(index, _mm512_load_si512(table));
}

// A 256-byte table's lookup per byte: two vpermi2b and a blend by the index's top bit.
static __m512i tier0_lookup256(const uint8_t* table, __m512i index)
{
    const __m512i low = _mm512_permutex2var_epi8(_mm512_load_si512(table), index, _mm512_load_si512(table + 64));
    const __m512i high = _mm512_permutex2var_epi8(_mm512_load_si512(table + 128), index, _mm512_load_si512(table + 192));
    return _mm512_mask_blend_epi8(_mm512_movepi8_mask(index), low, high);
}

// The bits of a 64-bit-per-block bitset for the 64 entries from `first` shifted back by `back` entries (bit j is entry
// first + j - back's), across the block before.
static uint64_t tier0_bits_back(const uint64_t* bits, uint32_t block, uint32_t back)
{
    const uint64_t current = bits[block];
    const uint64_t previous = block != 0 ? bits[block - 1] : 0;
    return back == 0 ? current : current << back | previous >> (64 - back);
}

// And forward by `ahead` (bit j is entry first + j + ahead's), across the block after (`blocks` of them).
static uint64_t tier0_bits_ahead(const uint64_t* bits, uint32_t block, uint32_t blocks, uint32_t ahead)
{
    const uint64_t current = bits[block];
    const uint64_t next = block + 1 < blocks ? bits[block + 1] : 0;
    return ahead == 0 ? current : current >> ahead | next << (64 - ahead);
}

// A byte vector's lanes shifted back by `back` (lane j holds lane j - back), the block before's last lanes first.
static __m512i tier0_shift_back(__m512i current, __m512i previous, uint32_t back)
{
    const __m512i index = _mm512_add_epi8(_mm512_set_epi8(63, 62, 61, 60, 59, 58, 57, 56, 55, 54, 53, 52, 51, 50, 49, 48, 47, 46, 45, 44, 43, 42, 41, 40, 39,
                                                          38, 37, 36, 35, 34, 33, 32, 31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17, 16, 15,
                                                          14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0),
                                          _mm512_set1_epi8((char)(64 - back)));
    return _mm512_permutex2var_epi8(previous, index, current);
}

// The vector fusion's bitsets and columns: P0's predicates of each entry's value (as an immediate of 32 bits, a count up
// to 255, zero, a size below 2^31, the product with the entry before's fitting 32 bits, the value of three entries back),
// P1's leaf fusions per consumer and P2's compound ones.
typedef struct Tier0Bits Tier0Bits;
struct Tier0Bits
{
    uint64_t* fits;
    uint64_t* count;
    uint64_t* zero;
    uint64_t* size;
    uint64_t* product;
    uint64_t* same;
    uint64_t* sib;
    uint8_t* sib_log2;
    uint64_t* labels;
    // A consumer takes the leaf before it; takes a local and a constant (HOME_CONST); takes a pointer addition or a
    // compare before it; takes a pointer addition two back; takes x = x op v's op and x.
    uint64_t* leaf;
    uint64_t* home_const;
    uint64_t* one;
    uint64_t* two;
    uint64_t* rmw;
    uint32_t blocks;
};

// P0: each entry's value predicates, 8 entries at a time (a byte of each bitset), and its SIB scale's log2.
static void tier0_vector_values(const Tier0Function* function, Tier0Bits* bits)
{
    const uint32_t count = function->count;
    const __m512i int32_max = _mm512_set1_epi64(INT32_MAX);
    for (uint32_t first = 0; first < bits->blocks * 64; first += 8)
    {
        const __mmask8 lanes = (__mmask8)tier0_lanes((int64_t)first, count);
        const __mmask8 lanes_1 = (__mmask8)(tier0_lanes((int64_t)first - 1, count));
        const __mmask8 lanes_3 = (__mmask8)(tier0_lanes((int64_t)first - 3, count));
        const __m512i value = _mm512_maskz_loadu_epi64(lanes, function->values + first);
        const __m512i previous = _mm512_maskz_loadu_epi64(lanes_1, (const void*)((uintptr_t)(function->values + first) - 8));
        const __m512i back3 = _mm512_maskz_loadu_epi64(lanes_3, (const void*)((uintptr_t)(function->values + first) - 24));
        const __m512i narrowed = _mm512_cvtepi32_epi64(_mm512_cvtepi64_epi32(value));
        const __m512i product = _mm512_mullo_epi64(previous, value);
        const __m512i narrowed_product = _mm512_cvtepi32_epi64(_mm512_cvtepi64_epi32(product));
        const __m512i previous_narrowed = _mm512_cvtepi32_epi64(_mm512_cvtepi64_epi32(previous));
        const uint32_t byte = first / 8;
        ((uint8_t*)bits->fits)[byte] = (uint8_t)_mm512_mask_cmpeq_epi64_mask(lanes, value, narrowed);
        ((uint8_t*)bits->count)[byte] = (uint8_t)_mm512_mask_cmple_epu64_mask(lanes, value, _mm512_set1_epi64(255));
        ((uint8_t*)bits->zero)[byte] = (uint8_t)_mm512_mask_testn_epi64_mask(lanes, value, value);
        ((uint8_t*)bits->size)[byte] = (uint8_t)_mm512_mask_cmple_epu64_mask(lanes, value, int32_max);
        ((uint8_t*)bits->product)[byte] =
            (uint8_t)(_mm512_mask_cmpeq_epi64_mask(lanes, product, narrowed_product) & _mm512_cmpeq_epi64_mask(previous, previous_narrowed));
        ((uint8_t*)bits->same)[byte] = (uint8_t)_mm512_mask_cmpeq_epi64_mask(lanes & lanes_3, back3, value);
        // A SIB scale: one bit set, at most 8; its log2 is 63 less its leading zeros.
        const __mmask8 one_bit = _mm512_cmpeq_epi64_mask(_mm512_popcnt_epi64(value), _mm512_set1_epi64(1));
        const __mmask8 small = _mm512_cmple_epu64_mask(value, _mm512_set1_epi64(8));
        ((uint8_t*)bits->sib)[byte] = (uint8_t)(lanes & one_bit & small);
        const __m512i log2 = _mm512_sub_epi64(_mm512_set1_epi64(63), _mm512_lzcnt_epi64(value));
        _mm_storel_epi64((__m128i*)(bits->sib_log2 + first), _mm512_cvtepi64_epi8(_mm512_maskz_mov_epi64(one_bit & small, log2)));
    }
}

// The labels, a bit per entry: the control entries' targets (found 64 at a time, then set one at a time: they are few).
static void tier0_vector_labels(const Tier0Function* function, uint64_t* labels)
{
    const uint32_t count = function->count;
    for (uint32_t first = 0; first < count; first += 64)
    {
        const __m512i kinds = tier0_bytes_at(function->kinds, first, count);
        const __m512i flags = tier0_lookup(tier0_vector_kind_flags, kinds);
        uint64_t control = _mm512_test_epi8_mask(flags, _mm512_set1_epi8(BUSTER_TIER0_KIND_CONTROL)) & tier0_lanes(first, count);
        while (control != 0)
        {
            const uint32_t entry = first + (uint32_t)__builtin_ctzll(control);
            const uint64_t target = function->values[entry];
            labels[target >> 6] |= 1ull << (target & 63);
            control &= control - 1;
        }
    }
}

// The fixed byte vectors of a variant code.
static __m512i tier0_variant(uint32_t variant)
{
    return _mm512_set1_epi8((char)variant);
}

// P1 and P2: each consumer's fusions and key, and every entry's raw effects, 64 entries at a time.
static void tier0_vector_fuse(const Tier0Function* function, Tier0Bits* bits, Tier0Entries* entries)
{
    const uint32_t count = function->count;
    const __m512i one = _mm512_set1_epi8(1);
    __m512i previous_kinds = _mm512_setzero_si512();
    __m512i previous_classes = _mm512_setzero_si512();
    __m512i previous_kinds_2 = _mm512_setzero_si512();
    // P1: raw effects, leaf fusions, the entries' own variants and keys.
    for (uint32_t block = 0; block < bits->blocks; block += 1)
    {
        const uint32_t first = block * 64;
        const __mmask64 lanes = tier0_lanes(first, count);
        const __m512i kinds = tier0_bytes_at(function->kinds, first, count);
        const __m512i classes = tier0_bytes_at(function->classes, first, count);
        const __m512i kinds_1 = tier0_shift_back(kinds, previous_kinds, 1);
        const __m512i classes_1 = tier0_shift_back(classes, previous_classes, 1);
        const __m512i kinds_2 = tier0_shift_back(kinds, previous_kinds, 2);
        (void)previous_kinds_2;
        const __m512i shape = tier0_lookup(tier0_vector_shapes, kinds);
        const __m512i flags = tier0_lookup(tier0_vector_kind_flags, kinds);
        const __m512i stack = tier0_lookup(tier0_vector_stacks, classes);
        const __m512i stack_1 = tier0_lookup(tier0_vector_stacks, classes_1);
        // The shape's codes.
        const __m512i first_code = _mm512_and_si512(shape, _mm512_set1_epi8(3));
        const __mmask64 has_last = _mm512_test_epi8_mask(shape, _mm512_set1_epi8(4)) &
                                   ~(_mm512_cmpeq_epi8_mask(kinds, _mm512_set1_epi8(BUSTER_TYPED_RETURN)) & _mm512_testn_epi8_mask(classes, classes));
        const __m512i push_code = _mm512_and_si512(_mm512_srli_epi16(shape, 3), _mm512_set1_epi8(3));
        // Raw effects per stack: pushes less pops.
        __m512i integer = _mm512_setzero_si512();
        __m512i floating = _mm512_setzero_si512();
        const __mmask64 push_class = _mm512_cmpeq_epi8_mask(push_code, _mm512_set1_epi8(BUSTER_TIER0_STACK_CLASS));
        const __mmask64 push_integer = _mm512_cmpeq_epi8_mask(push_code, _mm512_set1_epi8(BUSTER_TIER0_STACK_INTEGER)) |
                                       (push_class & _mm512_cmpeq_epi8_mask(stack, one));
        const __mmask64 push_floating = push_class & _mm512_cmpeq_epi8_mask(stack, _mm512_set1_epi8(2));
        const __mmask64 first_class = _mm512_cmpeq_epi8_mask(first_code, _mm512_set1_epi8(BUSTER_TIER0_STACK_CLASS));
        const __mmask64 first_operand = _mm512_cmpeq_epi8_mask(first_code, _mm512_set1_epi8(BUSTER_TIER0_STACK_OPERAND));
        const __mmask64 first_integer = _mm512_cmpeq_epi8_mask(first_code, _mm512_set1_epi8(BUSTER_TIER0_STACK_INTEGER)) |
                                        (first_class & _mm512_cmpeq_epi8_mask(stack, one)) | (first_operand & _mm512_cmpeq_epi8_mask(stack_1, one));
        const __mmask64 first_floating = (first_class & _mm512_cmpeq_epi8_mask(stack, _mm512_set1_epi8(2))) |
                                         (first_operand & _mm512_cmpeq_epi8_mask(stack_1, _mm512_set1_epi8(2)));
        const __mmask64 last_integer = has_last & _mm512_cmpeq_epi8_mask(stack_1, one);
        const __mmask64 last_floating = has_last & _mm512_cmpeq_epi8_mask(stack_1, _mm512_set1_epi8(2));
        integer = _mm512_mask_add_epi8(integer, push_integer, integer, one);
        integer = _mm512_mask_sub_epi8(integer, first_integer, integer, one);
        integer = _mm512_mask_sub_epi8(integer, last_integer, integer, one);
        floating = _mm512_mask_add_epi8(floating, push_floating, floating, one);
        floating = _mm512_mask_sub_epi8(floating, first_floating, floating, one);
        floating = _mm512_mask_sub_epi8(floating, last_floating, floating, one);
        _mm512_mask_storeu_epi8(entries->integer_effects + first, lanes, integer);
        _mm512_mask_storeu_epi8(entries->floating_effects + first, lanes, floating);
        // The key class: a compare's operands'.
        const __mmask64 compare = _mm512_test_epi8_mask(flags, _mm512_set1_epi8(BUSTER_TIER0_KIND_COMPARE));
        const __m512i key_class = _mm512_mask_blend_epi8(compare, classes, classes_1);
        const __m512i row = tier0_lookup(tier0_vector_rows, kinds);
        __m512i properties = tier0_lookup256(tier0_vector_properties, _mm512_or_si512(_mm512_slli_epi16(row, 4), key_class));
        const __mmask64 convert = _mm512_cmpeq_epi8_mask(kinds, _mm512_set1_epi8(BUSTER_TYPED_CONVERT));
        const __m512i convert_home = tier0_lookup256(tier0_vector_convert_home, _mm512_or_si512(_mm512_slli_epi16(classes, 4), classes_1));
        properties = _mm512_mask_blend_epi8(convert, properties, convert_home);
        // The value predicates for this block, and one back and two back.
        const uint64_t labels = bits->labels[block];
        const uint64_t labels_1 = tier0_bits_back(bits->labels, block, 1);
        const uint64_t fits_1 = tier0_bits_back(bits->fits, block, 1);
        const uint64_t count_1 = tier0_bits_back(bits->count, block, 1);
        const uint64_t size = bits->size[block];
        const uint64_t product = bits->product[block];
        const uint64_t wide = _mm512_movepi8_mask(tier0_lookup(tier0_vector_wide, key_class));
        const uint64_t shift = _mm512_test_epi8_mask(flags, _mm512_set1_epi8(BUSTER_TIER0_KIND_SHIFT));
        const uint64_t exempt = _mm512_test_epi8_mask(flags, _mm512_set1_epi8(BUSTER_TIER0_KIND_EXEMPT));
        const uint64_t pointer_add = _mm512_cmpeq_epi8_mask(kinds, _mm512_set1_epi8(BUSTER_TYPED_PTR_ADD));
        const uint64_t discard = _mm512_cmpeq_epi8_mask(kinds, _mm512_set1_epi8(BUSTER_TYPED_DISCARD));
        // Whether the constant before fits the consumer's immediate.
        const uint64_t general_fits = exempt | ~wide | fits_1;
        const uint64_t fits = (shift & count_1) | (pointer_add & fits_1 & size & product) | (~shift & ~pointer_add & general_fits);
        const uint64_t constant_1 = _mm512_cmpeq_epi8_mask(kinds_1, _mm512_set1_epi8(BUSTER_TYPED_CONST));
        const uint64_t integer_1 = _mm512_movepi8_mask(tier0_lookup(tier0_vector_integer, classes_1));
        const uint64_t local_1 = _mm512_cmpeq_epi8_mask(kinds_1, _mm512_set1_epi8(BUSTER_TYPED_LOCAL));
        const uint64_t local_2 = _mm512_cmpeq_epi8_mask(kinds_2, _mm512_set1_epi8(BUSTER_TYPED_LOCAL));
        const uint64_t property_const = _mm512_test_epi8_mask(properties, _mm512_set1_epi8(BUSTER_TIER0_PROPERTY_CONST));
        const uint64_t property_home = _mm512_test_epi8_mask(properties, _mm512_set1_epi8(BUSTER_TIER0_PROPERTY_HOME));
        const uint64_t property_home_const = _mm512_test_epi8_mask(properties, _mm512_set1_epi8(BUSTER_TIER0_PROPERTY_HOME_CONST));
        const uint64_t home_const_kind = _mm512_test_epi8_mask(flags, _mm512_set1_epi8(BUSTER_TIER0_KIND_HOME_CONST));
        const uint64_t home_const = lanes & home_const_kind & ~labels & ~labels_1 & constant_1 & local_2 & integer_1 & (~wide | fits_1) & property_home_const &
                                    (block != 0 ? ~0ull : ~3ull);
        const uint64_t home_home = lanes & compare & ~labels & ~labels_1 & local_1 & local_2 & property_home_const & (block != 0 ? ~0ull : ~3ull);
        const uint64_t leaf_constant = constant_1 & (integer_1 | discard) & fits & property_const;
        const uint64_t leaf_home = local_1 & property_home;
        const uint64_t leaf = lanes & has_last & ~labels & (leaf_constant | leaf_home) & ~home_const & ~home_home & (block != 0 ? ~0ull : ~1ull);
        bits->leaf[block] = leaf;
        bits->home_const[block] = home_const | home_home;
        // The variants: the entry's own, then the leaf fusion's.
        __m512i variant = _mm512_setzero_si512();
        const uint64_t floating_zero = _mm512_cmpeq_epi8_mask(kinds, _mm512_set1_epi8(BUSTER_TYPED_CONST)) & _mm512_cmpeq_epi8_mask(stack, _mm512_set1_epi8(2)) &
                                       bits->zero[block];
        variant = _mm512_mask_mov_epi8(variant, floating_zero, tier0_variant(BUSTER_TIER0_VARIANT_CONST));
        const __m512i sib_log2 = _mm512_maskz_loadu_epi8(lanes, bits->sib_log2 + first);
        variant = _mm512_mask_add_epi8(variant, pointer_add & bits->sib[block], sib_log2, tier0_variant(BUSTER_TIER0_VARIANT_INDEX(0)));
        variant = _mm512_mask_mov_epi8(variant, lanes & pointer_add & ~size, tier0_variant(BUSTER_TIER0_VARIANT_WIDE_SCALE));
        const uint64_t last_entry = count != 0 && count - 1 >= first && count - 1 < first + 64 ? 1ull << (count - 1 - first) : 0;
        variant = _mm512_mask_mov_epi8(variant, _mm512_cmpeq_epi8_mask(kinds, _mm512_set1_epi8(BUSTER_TYPED_RETURN)) & last_entry,
                                       tier0_variant(BUSTER_TIER0_VARIANT_LAST));
        variant = _mm512_mask_mov_epi8(variant, convert, classes_1);
        variant = _mm512_mask_mov_epi8(variant, leaf & leaf_constant, tier0_variant(BUSTER_TIER0_VARIANT_CONST));
        variant = _mm512_mask_mov_epi8(variant, leaf & leaf_home & ~convert, tier0_variant(BUSTER_TIER0_VARIANT_HOME));
        variant = _mm512_mask_mov_epi8(variant, leaf & leaf_home & convert, _mm512_or_si512(classes_1, tier0_variant(BUSTER_TIER0_VARIANT_CONVERT_HOME)));
        variant = _mm512_mask_mov_epi8(variant, home_const, tier0_variant(BUSTER_TIER0_VARIANT_HOME_CONST));
        variant = _mm512_mask_mov_epi8(variant, home_home, tier0_variant(BUSTER_TIER0_VARIANT_HOME_HOME));
        _mm512_mask_storeu_epi8(entries->variants + first, lanes, variant);
        _mm512_mask_storeu_epi8(entries->key_kinds + first, lanes, kinds);
        _mm512_mask_storeu_epi8(entries->key_classes + first, lanes, key_class);
        previous_kinds_2 = previous_kinds;
        previous_kinds = kinds;
        previous_classes = classes;
    }
    // The calls' effects, from the calls table (they are few).
    for (uint32_t first = 0; first < count; first += 64)
    {
        uint64_t calls = _mm512_cmpeq_epi8_mask(tier0_bytes_at(function->kinds, first, count), _mm512_set1_epi8(BUSTER_TYPED_CALL)) & tier0_lanes(first, count);
        while (calls != 0)
        {
            const uint32_t entry = first + (uint32_t)__builtin_ctzll(calls);
            int32_t integer = 0;
            int32_t floating = 0;
            tier0_raw_effects(function, entry, &integer, &floating);
            entries->integer_effects[entry] = (int8_t)integer;
            entries->floating_effects[entry] = (int8_t)floating;
            calls &= calls - 1;
        }
    }
    // P2: the compound fusions, from P1's variants of the entries before.
    __m512i previous_variants = _mm512_setzero_si512();
    __m512i previous_key_classes = _mm512_setzero_si512();
    previous_kinds = _mm512_setzero_si512();
    previous_classes = _mm512_setzero_si512();
    for (uint32_t block = 0; block < bits->blocks; block += 1)
    {
        const uint32_t first = block * 64;
        const __mmask64 lanes = tier0_lanes(first, count);
        const __m512i kinds = tier0_bytes_at(function->kinds, first, count);
        const __m512i classes = tier0_bytes_at(function->classes, first, count);
        const __m512i variants = _mm512_maskz_loadu_epi8(lanes, entries->variants + first);
        const __m512i key_classes = _mm512_maskz_loadu_epi8(lanes, entries->key_classes + first);
        const __m512i kinds_1 = tier0_shift_back(kinds, previous_kinds, 1);
        const __m512i kinds_2 = tier0_shift_back(kinds, previous_kinds, 2);
        const __m512i kinds_3 = tier0_shift_back(kinds, previous_kinds, 3);
        const __m512i classes_1 = tier0_shift_back(classes, previous_classes, 1);
        const __m512i classes_3 = tier0_shift_back(classes, previous_classes, 3);
        const __m512i variants_1 = tier0_shift_back(variants, previous_variants, 1);
        const __m512i variants_2 = tier0_shift_back(variants, previous_variants, 2);
        const __m512i key_classes_1 = tier0_shift_back(key_classes, previous_key_classes, 1);
        const uint64_t labels = bits->labels[block];
        const uint64_t labels_1 = tier0_bits_back(bits->labels, block, 1);
        const uint64_t labels_2 = tier0_bits_back(bits->labels, block, 2);
        const uint64_t leaf = bits->leaf[block];
        const uint64_t leaf_2 = tier0_bits_back(bits->leaf, block, 2);
        const uint64_t home_const_2 = tier0_bits_back(bits->home_const, block, 2);
        const uint64_t valid_1 = block != 0 ? ~0ull : ~1ull;
        const uint64_t valid_2 = block != 0 ? ~0ull : ~3ull;
        const uint64_t valid_3 = block != 0 ? ~0ull : ~7ull;
        const __m512i row = tier0_lookup(tier0_vector_rows, kinds);
        const __m512i properties = tier0_lookup256(tier0_vector_properties, _mm512_or_si512(_mm512_slli_epi16(row, 4), classes));
        const __m512i flags_1 = tier0_lookup(tier0_vector_kind_flags, kinds_1);
        const uint64_t index_1 = _mm512_cmpge_epu8_mask(variants_1, tier0_variant(BUSTER_TIER0_VARIANT_INDEX(0))) &
                                 _mm512_cmple_epu8_mask(variants_1, tier0_variant(BUSTER_TIER0_VARIANT_INDEX(3)));
        const uint64_t constant_variant_1 = _mm512_cmpeq_epi8_mask(variants_1, tier0_variant(BUSTER_TIER0_VARIANT_CONST));
        const uint64_t pointer_add_1 = _mm512_cmpeq_epi8_mask(kinds_1, _mm512_set1_epi8(BUSTER_TYPED_PTR_ADD));
        const uint64_t pointer_add_2 = _mm512_cmpeq_epi8_mask(kinds_2, _mm512_set1_epi8(BUSTER_TYPED_PTR_ADD));
        const uint64_t index_2 = _mm512_cmpge_epu8_mask(variants_2, tier0_variant(BUSTER_TIER0_VARIANT_INDEX(0))) &
                                 _mm512_cmple_epu8_mask(variants_2, tier0_variant(BUSTER_TIER0_VARIANT_INDEX(3)));
        const uint64_t constant_variant_2 = _mm512_cmpeq_epi8_mask(variants_2, tier0_variant(BUSTER_TIER0_VARIANT_CONST));
        const uint64_t property_address = _mm512_test_epi8_mask(properties, _mm512_set1_epi8(BUSTER_TIER0_PROPERTY_ADDRESS));
        const uint64_t property_address_const = _mm512_test_epi8_mask(properties, _mm512_set1_epi8(BUSTER_TIER0_PROPERTY_ADDRESS_CONST));
        // A load's address.
        const uint64_t load = lanes & valid_1 & _mm512_cmpeq_epi8_mask(kinds, _mm512_set1_epi8(BUSTER_TYPED_LOAD)) & ~labels & pointer_add_1 &
                              (index_1 | constant_variant_1) & property_address;
        // A store's address, before its fused value.
        const uint64_t set = _mm512_cmpeq_epi8_mask(kinds, _mm512_set1_epi8(BUSTER_TYPED_SET));
        const uint64_t value_constant = _mm512_cmpeq_epi8_mask(variants, tier0_variant(BUSTER_TIER0_VARIANT_CONST));
        const uint64_t value_home = _mm512_cmpeq_epi8_mask(variants, tier0_variant(BUSTER_TIER0_VARIANT_HOME));
        const uint64_t store = lanes & valid_2 & set & leaf & ~labels & ~labels_1 & pointer_add_2 &
                               ((index_2 & value_constant & property_address_const) | (index_2 & value_home & property_address) |
                                (constant_variant_2 & value_home & property_address));
        // A branch's compare.
        const uint64_t branch = _mm512_cmpeq_epi8_mask(kinds, _mm512_set1_epi8(BUSTER_TYPED_BRANCH_FALSE)) |
                                _mm512_cmpeq_epi8_mask(kinds, _mm512_set1_epi8(BUSTER_TYPED_BRANCH_TRUE));
        const __m512i compare_row = tier0_lookup(tier0_vector_rows, kinds_1);
        const __m512i compare_properties = tier0_lookup256(tier0_vector_properties, _mm512_or_si512(_mm512_slli_epi16(compare_row, 4), key_classes_1));
        const uint64_t compare_1 = _mm512_test_epi8_mask(flags_1, _mm512_set1_epi8(BUSTER_TIER0_KIND_COMPARE));
        const uint64_t compare_fusable = _mm512_cmple_epu8_mask(variants_1, tier0_variant(BUSTER_TIER0_VARIANT_HOME_HOME)) &
                                         _mm512_test_epi8_mask(compare_properties, _mm512_set1_epi8(BUSTER_TIER0_PROPERTY_BRANCH));
        const uint64_t fused_branch = lanes & valid_1 & branch & ~labels & compare_1 & compare_fusable;
        // x = x op v.
        const uint64_t set_local = _mm512_cmpeq_epi8_mask(kinds, _mm512_set1_epi8(BUSTER_TYPED_SET_LOCAL));
        const uint64_t rmw_kind_1 = _mm512_test_epi8_mask(flags_1, _mm512_set1_epi8(BUSTER_TIER0_KIND_RMW));
        const uint64_t inner_home_1 = _mm512_cmpeq_epi8_mask(variants_1, tier0_variant(BUSTER_TIER0_VARIANT_HOME));
        const __m512i op_row = tier0_lookup(tier0_vector_rows, kinds_1);
        const __m512i op_properties = tier0_lookup256(tier0_vector_properties, _mm512_or_si512(_mm512_slli_epi16(op_row, 4), classes));
        const uint64_t rmw_property = (constant_variant_1 & _mm512_test_epi8_mask(op_properties, _mm512_set1_epi8(BUSTER_TIER0_PROPERTY_RMW_CONST))) |
                                      (inner_home_1 & _mm512_test_epi8_mask(op_properties, _mm512_set1_epi8(BUSTER_TIER0_PROPERTY_RMW_HOME)));
        const uint64_t x_local = _mm512_cmpeq_epi8_mask(kinds_3, _mm512_set1_epi8(BUSTER_TYPED_LOCAL));
        // x is never absorbed already: the entry after it is a leaf, which absorbs nothing, and the op two after it
        // isn't a kind that takes a local and a constant.
        (void)home_const_2;
        (void)leaf_2;
        const uint64_t rmw = lanes & valid_3 & set_local & ~labels & ~labels_1 & ~labels_2 & rmw_kind_1 & rmw_property & x_local &
                             bits->same[block] & _mm512_cmpeq_epi8_mask(classes_3, classes) & _mm512_cmpeq_epi8_mask(classes_1, classes);
        bits->one[block] = load | fused_branch | rmw;
        bits->two[block] = store;
        bits->rmw[block] = rmw;
        // The keys and variants of the compound fusions.
        __m512i variant = variants;
        variant = _mm512_mask_mov_epi8(variant, load & constant_variant_1, tier0_variant(BUSTER_TIER0_VARIANT_OFFSET));
        variant = _mm512_mask_mov_epi8(variant, load & index_1, variants_1);
        const __m512i store_index = _mm512_sub_epi8(variants_2, tier0_variant(BUSTER_TIER0_VARIANT_INDEX(0)));
        variant = _mm512_mask_add_epi8(variant, store & index_2 & value_constant, store_index, tier0_variant(BUSTER_TIER0_VARIANT_INDEX_CONST(0)));
        variant = _mm512_mask_add_epi8(variant, store & index_2 & value_home, store_index, tier0_variant(BUSTER_TIER0_VARIANT_INDEX_HOME(0)));
        variant = _mm512_mask_mov_epi8(variant, store & constant_variant_2, tier0_variant(BUSTER_TIER0_VARIANT_OFFSET_HOME));
        const __m512i branch_base = _mm512_mask_blend_epi8(_mm512_cmpeq_epi8_mask(kinds, _mm512_set1_epi8(BUSTER_TYPED_BRANCH_TRUE)),
                                                           tier0_variant(BUSTER_TIER0_VARIANT_BRANCH_FALSE), tier0_variant(BUSTER_TIER0_VARIANT_BRANCH_TRUE));
        variant = _mm512_mask_add_epi8(variant, fused_branch, branch_base, variants_1);
        variant = _mm512_mask_mov_epi8(variant, rmw & constant_variant_1, tier0_variant(BUSTER_TIER0_VARIANT_RMW_CONST));
        variant = _mm512_mask_mov_epi8(variant, rmw & inner_home_1, tier0_variant(BUSTER_TIER0_VARIANT_RMW_HOME));
        _mm512_mask_storeu_epi8(entries->variants + first, lanes, variant);
        _mm512_mask_storeu_epi8(entries->key_kinds + first, lanes & (fused_branch | rmw), kinds_1);
        _mm512_mask_storeu_epi8(entries->key_classes + first, lanes & fused_branch, key_classes_1);
        previous_variants = variants;
        previous_key_classes = key_classes;
        previous_kinds = kinds;
        previous_classes = classes;
    }
}

// P3 and P4: each entry's absorber (the consumer that takes it, 1 to 3 entries on, after one pointer-jump step for an
// absorber that is absorbed itself), the effects folded into the absorbers, and the depths, their prefix sums.
static void tier0_vector_depths(const Tier0Function* function, Tier0Bits* bits, Tier0Entries* entries)
{
    const uint32_t count = function->count;
    const uint32_t blocks = bits->blocks;
    // P3a: the direct absorber's distance (0 for none), into the `absorbed` column (overwritten in P3b by the absorbed
    // flag).
    for (uint32_t block = 0; block < blocks; block += 1)
    {
        const uint32_t first = block * 64;
        const __mmask64 lanes = tier0_lanes(first, count);
        const uint64_t by_one = tier0_bits_ahead(bits->leaf, block, blocks, 1) | tier0_bits_ahead(bits->home_const, block, blocks, 1) |
                                tier0_bits_ahead(bits->one, block, blocks, 1);
        const uint64_t by_two = tier0_bits_ahead(bits->home_const, block, blocks, 2) | tier0_bits_ahead(bits->two, block, blocks, 2);
        const uint64_t by_three = tier0_bits_ahead(bits->rmw, block, blocks, 3);
        __m512i distance = _mm512_setzero_si512();
        distance = _mm512_mask_mov_epi8(distance, by_three, _mm512_set1_epi8(3));
        distance = _mm512_mask_mov_epi8(distance, by_two, _mm512_set1_epi8(2));
        distance = _mm512_mask_mov_epi8(distance, by_one, _mm512_set1_epi8(1));
        _mm512_mask_storeu_epi8(entries->absorbed + first, lanes, distance);
    }
    // P3b: the ultimate absorber's distance (the direct one's plus its own), then the folded effects: an absorbed
    // entry's effects count at its ultimate absorber. Both read the distances around the block: loads from the column
    // (each block's lanes ahead are P3a's, which is done).
    int32_t integer_depth = 0;
    int32_t floating_depth = 0;
    __m512i previous_ultimate = _mm512_setzero_si512();
    __m512i previous_integer = _mm512_setzero_si512();
    __m512i previous_floating = _mm512_setzero_si512();
    const __m512i lane_index = _mm512_set_epi8(63, 62, 61, 60, 59, 58, 57, 56, 55, 54, 53, 52, 51, 50, 49, 48, 47, 46, 45, 44, 43, 42, 41, 40, 39, 38, 37, 36,
                                               35, 34, 33, 32, 31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7,
                                               6, 5, 4, 3, 2, 1, 0);
    __m512i next_direct = blocks > 0 ? _mm512_maskz_loadu_epi8(tier0_lanes(0, count), entries->absorbed) : _mm512_setzero_si512();
    for (uint32_t block = 0; block < blocks; block += 1)
    {
        const uint32_t first = block * 64;
        const __mmask64 lanes = tier0_lanes(first, count);
        const __m512i direct = next_direct;
        next_direct = block + 1 < blocks ? _mm512_maskz_loadu_epi8(tier0_lanes(first + 64, count), entries->absorbed + first + 64) : _mm512_setzero_si512();
        // The absorber's own distance: lane j + direct[j], across into the next block.
        const __m512i at = _mm512_add_epi8(lane_index, direct);
        const __m512i absorber_direct = _mm512_permutex2var_epi8(direct, at, next_direct);
        const __mmask64 absorbed = _mm512_test_epi8_mask(direct, direct);
        const __m512i ultimate = _mm512_maskz_add_epi8(absorbed, direct, absorber_direct);
        const __m512i integer_raw = _mm512_maskz_loadu_epi8(lanes, entries->integer_effects + first);
        const __m512i floating_raw = _mm512_maskz_loadu_epi8(lanes, entries->floating_effects + first);
        // An entry's folded effects: its own unless absorbed, plus each entry 1 to 3 back whose ultimate absorber it is.
        __m512i integer = _mm512_maskz_mov_epi8(~absorbed, integer_raw);
        __m512i floating = _mm512_maskz_mov_epi8(~absorbed, floating_raw);
        for (uint32_t back = 1; back <= 3; back += 1)
        {
            const __m512i ultimate_back = tier0_shift_back(ultimate, previous_ultimate, back);
            const __mmask64 mine = _mm512_cmpeq_epi8_mask(ultimate_back, _mm512_set1_epi8((char)back));
            integer = _mm512_mask_add_epi8(integer, mine, integer, tier0_shift_back(integer_raw, previous_integer, back));
            floating = _mm512_mask_add_epi8(floating, mine, floating, tier0_shift_back(floating_raw, previous_floating, back));
        }
        _mm512_mask_storeu_epi8(entries->absorbed + first, lanes, _mm512_maskz_mov_epi8(absorbed, _mm512_set1_epi8(1)));
        _mm512_mask_storeu_epi8(entries->integer_effects + first, lanes, integer);
        _mm512_mask_storeu_epi8(entries->floating_effects + first, lanes, floating);
        // P4: the depths, the exclusive prefix sums of the folded effects: in-register scans of bytes (log steps), plus
        // the depth the block starts at.
        __m512i integer_sum = _mm512_maskz_mov_epi8(lanes, integer);
        __m512i floating_sum = _mm512_maskz_mov_epi8(lanes, floating);
        for (uint32_t step = 1; step < 64; step *= 2)
        {
            const __m512i zero = _mm512_setzero_si512();
            integer_sum = _mm512_add_epi8(integer_sum, tier0_shift_back(integer_sum, zero, step));
            floating_sum = _mm512_add_epi8(floating_sum, tier0_shift_back(floating_sum, zero, step));
        }
        const __m512i zero = _mm512_setzero_si512();
        const __m512i integer_depths = _mm512_add_epi8(tier0_shift_back(integer_sum, zero, 1), _mm512_set1_epi8((char)integer_depth));
        const __m512i floating_depths = _mm512_add_epi8(tier0_shift_back(floating_sum, zero, 1), _mm512_set1_epi8((char)floating_depth));
        _mm512_mask_storeu_epi8(entries->depths + first, lanes, integer_depths);
        _mm512_mask_storeu_epi8(entries->floating_depths + first, lanes, floating_depths);
        integer_depth += (int8_t)_mm_extract_epi8(_mm512_extracti32x4_epi32(integer_sum, 3), 15);
        floating_depth += (int8_t)_mm_extract_epi8(_mm512_extracti32x4_epi32(floating_sum, 3), 15);
        previous_ultimate = ultimate;
        previous_integer = integer_raw;
        previous_floating = floating_raw;
    }
}

// A template's word (a slot's adds HOME_DEPENDENT and HOME_UNIFORM, in tier0_build_words).
static uint64_t tier0_template_word(uint32_t index)
{
    const Tier0TemplateInfo* info = &tier0_template_infos[index];
    const uint64_t depth = info->required_depth == BUSTER_TIER0_ANY_DEPTH ? BUSTER_TIER0_WORD_ANY_DEPTH : info->required_depth;
    const uint64_t k = info->k_offset == BUSTER_TIER0_NO_OFFSET ? BUSTER_TIER0_WORD_NO_OFFSET : info->k_offset;
    const uint64_t k_scale = info->k_scale == BUSTER_TIER0_NO_OFFSET ? BUSTER_TIER0_WORD_NO_OFFSET : info->k_scale;
    const uint64_t a = info->a_offset == BUSTER_TIER0_NO_OFFSET ? BUSTER_TIER0_WORD_NO_OFFSET : info->a_offset;
    const uint64_t b = info->b_offset != BUSTER_TIER0_NO_OFFSET ? info->b_offset : info->b_own != 0 ? 0 : BUSTER_TIER0_WORD_NO_OFFSET;
    uint64_t word = index;
    word |= depth << BUSTER_TIER0_WORD_DEPTH_SHIFT;
    word |= (uint64_t)info->integer_reach << BUSTER_TIER0_WORD_INTEGER_REACH_SHIFT;
    word |= (uint64_t)info->floating_reach << BUSTER_TIER0_WORD_FLOATING_REACH_SHIFT;
    word |= info->clobbers != 0 ? BUSTER_TIER0_WORD_CLOBBERS_RDX : 0;
    word |= (uint64_t)info->length << BUSTER_TIER0_WORD_LENGTH_SHIFT;
    word |= info->branch != 0xff ? BUSTER_TIER0_WORD_BRANCH | (uint64_t)info->branch << BUSTER_TIER0_WORD_BRANCH_SHIFT : 0;
    word |= info->fixup != 0xff ? BUSTER_TIER0_WORD_FIXUP | (uint64_t)info->fixup << BUSTER_TIER0_WORD_FIXUP_SHIFT : 0;
    word |= info->choice != 0xff
                ? BUSTER_TIER0_WORD_CHOICE | (uint64_t)info->choice << BUSTER_TIER0_WORD_CHOICE_SHIFT | (uint64_t)info->choice_row << BUSTER_TIER0_WORD_ROW_SHIFT
                : 0;
    word |= info->end != 0 ? BUSTER_TIER0_WORD_END : 0;
    word |= info->choice_static != 0 ? BUSTER_TIER0_WORD_CHOICE_STATIC : 0;
    word |= (uint64_t)info->choice_live << BUSTER_TIER0_WORD_LIVE_SHIFT;
    word |= k << BUSTER_TIER0_WORD_K_SHIFT | k_scale << BUSTER_TIER0_WORD_K_SCALE_SHIFT | a << BUSTER_TIER0_WORD_A_SHIFT | b << BUSTER_TIER0_WORD_B_SHIFT;
    return word;
}

// Every template's and slot's word, and the templates' masks. Returns false for a template the words can't hold: a fixed
// register besides rdx, a static choice immediate other than 0, a choice row past 63, or past 4,096 templates.
static bool tier0_build_words(void)
{
    bool valid = tier0_template_count <= BUSTER_TIER0_WORD_TEMPLATE + 1;
    for (uint32_t t = 0; t < tier0_template_count; t += 1)
    {
        const Tier0TemplateInfo* info = &tier0_template_infos[t];
        valid &= info->clobbers == 0 || info->clobbers == 1u << X86_REGISTER_RDX;
        valid &= info->choice_static == 0 || info->choice_immediate == 0;
        valid &= info->choice == 0xff || info->choice_row < 64;
        tier0_masks[t] = info->mask;
        tier0_template_words[t] = tier0_template_word(t);
    }
    for (uint32_t kind = 0; kind < BUSTER_TYPED_KIND_LIMIT; kind += 1)
    {
        for (uint32_t class = 0; class < BUSTER_TYPED_CLASS_LIMIT; class += 1)
        {
            for (uint32_t variant = 0; variant < BUSTER_TIER0_VARIANTS; variant += 1)
            {
                const uint16_t slot = tier0_slot(kind, class, variant);
                uint64_t word = BUSTER_TIER0_WORD_UNSUPPORTED;
                if (slot == BUSTER_TIER0_SLOT_DEFERRED)
                {
                    word = BUSTER_TIER0_WORD_DEFERRED | BUSTER_TIER0_WORD_NO_HOLES | BUSTER_TIER0_WORD_ANY_DEPTH << BUSTER_TIER0_WORD_DEPTH_SHIFT;
                }
                else if (slot != BUSTER_TIER0_SLOT_UNSUPPORTED)
                {
                    const uint32_t index = slot & BUSTER_TIER0_SLOT_TEMPLATE;
                    word = tier0_template_words[index];
                    if ((slot & BUSTER_TIER0_SLOT_HOME_DEPENDENT) != 0)
                    {
                        bool uniform = true;
                        for (uint32_t c = 1; c < 4; c += 1)
                        {
                            uniform &= ((tier0_template_words[index + c] ^ word) & BUSTER_TIER0_WORD_EMISSION) == 0;
                        }
                        word |= BUSTER_TIER0_WORD_HOME_DEPENDENT | (uniform ? BUSTER_TIER0_WORD_HOME_UNIFORM : 0);
                    }
                }
                tier0_slot_words[kind << 9 | class << 5 | variant] = word;
            }
        }
    }
    return valid;
}

// The homes of a function's locals for the vector passes: up to 32 in four registers, where a vpermt2q looks up 8 (two
// and a blend past 16), and gathered past 32.
typedef struct Tier0HomeTable Tier0HomeTable;
struct Tier0HomeTable
{
    __m512i homes[4];
    const Tier0Home* all;
    uint32_t count;
};

static Tier0HomeTable tier0_home_table(const Tier0Home* homes, uint32_t count)
{
    Tier0HomeTable table;
    for (uint32_t r = 0; r < 4; r += 1)
    {
        table.homes[r] = _mm512_maskz_loadu_epi64((__mmask8)tier0_lanes(8 * r, count), homes + 8 * r);
    }
    table.all = homes;
    table.count = count;
    return table;
}

// The homes of each lane's local, in the lanes of `valid` (0 in the others).
static __m512i tier0_home_lookup(const Tier0HomeTable* table, __m512i local, __mmask8 valid)
{
    __m512i home;
    if (table->count <= 16)
    {
        home = _mm512_maskz_permutex2var_epi64(valid, table->homes[0], local, table->homes[1]);
    }
    else if (table->count <= 32)
    {
        const __m512i low = _mm512_permutex2var_epi64(table->homes[0], local, table->homes[1]);
        const __m512i high = _mm512_permutex2var_epi64(table->homes[2], local, table->homes[3]);
        home = _mm512_maskz_mov_epi64(valid, _mm512_mask_blend_epi64(_mm512_test_epi64_mask(local, _mm512_set1_epi64(16)), low, high));
    }
    else
    {
        home = _mm512_mask_i64gather_epi64(_mm512_setzero_si512(), valid, local, table->all, 8);
    }
    return home;
}

// The value of the entry `offset` back (0 to 3) for each lane of a block, from the block's values and the block's before
// it: lane j reads lane 8 + j - offset of the pair (an offset of 7, none, reads a value no lane uses).
static __m512i tier0_value_back(__m512i previous, __m512i current, __m512i offset)
{
    return _mm512_permutex2var_epi64(previous, _mm512_sub_epi64(_mm512_set_epi64(15, 14, 13, 12, 11, 10, 9, 8), offset), current);
}

// Selection for the vector path, 8 entries at a time: each entry's word gathered from its slot's (an absorbed entry's
// ABSORBED), the depth checks (a template whose fixed registers need another depth is deferred), the depths each stack
// reaches and the fixed clobbers, as masks and lane maxima; the deferred entries' touches an entry at a time, as
// tier0_select's.
static uint32_t tier0_select_vector(const Tier0Function* function, Tier0Entries* entries, Tier0Frame* frame, Tier0Error* error)
{
    const uint32_t count = function->count;
    const uint8_t* key_kinds = entries->key_kinds;
    const uint8_t* key_classes = entries->key_classes;
    const uint8_t* variants = entries->variants;
    const uint8_t* absorbed = entries->absorbed;
    const uint8_t* depths = entries->depths;
    const uint8_t* floating_depths = entries->floating_depths;
    uint64_t* words = entries->words;
    uint32_t unsupported = count;
    __m512i integer_top = _mm512_set1_epi64(-1);
    __m512i floating_top = _mm512_set1_epi64(-1);
    // The kept templates' words ORed together (for CLOBBERS_RDX), and the lanes with an unsupported entry in any block.
    __m512i kept_words = _mm512_setzero_si512();
    __mmask8 any_unsupported = 0;
    int32_t deferred_integer_top = -1;
    int32_t deferred_floating_top = -1;
    uint32_t clobbers = 0;
    uint32_t save_slots = 0;
    bool fixed = false;
    bool calls = false;
    const __m512i three = _mm512_set1_epi64(3);
    const __m512i fifteen = _mm512_set1_epi64(15);
    const __m512i not_template = _mm512_set1_epi64((int64_t)(BUSTER_TIER0_WORD_ABSORBED | BUSTER_TIER0_WORD_DEFERRED | BUSTER_TIER0_WORD_UNSUPPORTED));
    for (uint32_t first = 0; first < count; first += 8)
    {
        const __mmask8 lanes = (__mmask8)tier0_lanes(first, count);
        const __m256i kind = _mm256_and_si256(_mm256_cvtepu8_epi32(_mm_maskz_loadu_epi8(lanes, key_kinds + first)), _mm256_set1_epi32(BUSTER_TYPED_KIND_LIMIT - 1));
        const __m256i class = _mm256_and_si256(_mm256_cvtepu8_epi32(_mm_maskz_loadu_epi8(lanes, key_classes + first)), _mm256_set1_epi32(BUSTER_TYPED_CLASS_LIMIT - 1));
        const __m256i variant = _mm256_and_si256(_mm256_cvtepu8_epi32(_mm_maskz_loadu_epi8(lanes, variants + first)), _mm256_set1_epi32(BUSTER_TIER0_VARIANTS - 1));
        const __m256i slot = _mm256_or_si256(_mm256_or_si256(_mm256_slli_epi32(kind, 9), _mm256_slli_epi32(class, 5)), variant);
        const __m128i taken = _mm_maskz_loadu_epi8(lanes, absorbed + first);
        const __mmask8 own = lanes & (__mmask8)_mm_testn_epi8_mask(taken, taken);
        __m512i word = _mm512_mask_i32gather_epi64(_mm512_set1_epi64((int64_t)(BUSTER_TIER0_WORD_ABSORBED | BUSTER_TIER0_WORD_NO_HOLES)), own, slot, tier0_slot_words, 8);
        const __m512i depth = _mm512_cvtepu8_epi64(_mm_maskz_loadu_epi8(lanes, depths + first));
        const __m512i floating = _mm512_cvtepu8_epi64(_mm_maskz_loadu_epi8(lanes, floating_depths + first));
        const __m512i required = _mm512_and_si512(_mm512_srli_epi64(word, BUSTER_TIER0_WORD_DEPTH_SHIFT), fifteen);
        const __mmask8 template = lanes & _mm512_testn_epi64_mask(word, not_template);
        const __mmask8 elsewhere = template & _mm512_cmpneq_epi64_mask(required, fifteen) & _mm512_cmpneq_epi64_mask(required, depth);
        word = _mm512_mask_or_epi64(word, elsewhere, word, _mm512_set1_epi64((int64_t)BUSTER_TIER0_WORD_DEFERRED));
        _mm512_mask_storeu_epi64(words + first, lanes, word);
        const __mmask8 kept = template & ~elsewhere;
        const __m512i integer_reach = _mm512_and_si512(_mm512_srli_epi64(word, BUSTER_TIER0_WORD_INTEGER_REACH_SHIFT), three);
        const __m512i floating_reach = _mm512_and_si512(_mm512_srli_epi64(word, BUSTER_TIER0_WORD_FLOATING_REACH_SHIFT), three);
        const __mmask8 reaches = kept & _mm512_test_epi64_mask(integer_reach, integer_reach);
        const __mmask8 floating_reaches = kept & _mm512_test_epi64_mask(floating_reach, floating_reach);
        integer_top = _mm512_mask_max_epi64(integer_top, reaches, integer_top, _mm512_sub_epi64(_mm512_add_epi64(depth, integer_reach), three));
        floating_top = _mm512_mask_max_epi64(floating_top, floating_reaches, floating_top, _mm512_sub_epi64(_mm512_add_epi64(floating, floating_reach), three));
        kept_words = _mm512_mask_or_epi64(kept_words, kept, kept_words, word);
        any_unsupported |= lanes & _mm512_test_epi64_mask(word, _mm512_set1_epi64((int64_t)BUSTER_TIER0_WORD_UNSUPPORTED));
        // The deferred entries' touches, as the scalar selection has them.
        uint32_t deferred = lanes & _mm512_test_epi64_mask(word, _mm512_set1_epi64((int64_t)BUSTER_TIER0_WORD_DEFERRED));
        while (deferred != 0)
        {
            const uint32_t i = first + (uint32_t)__builtin_ctz(deferred);
            deferred &= deferred - 1;
            const int32_t entry_depth = depths[i];
            const int32_t entry_floating = floating_depths[i];
            if (function->kinds[i] == BUSTER_TYPED_CALL)
            {
                const uint32_t call = (uint32_t)function->values[i];
                const uint32_t call_first = function->call_first[call];
                uint32_t integer_arguments = 0;
                uint32_t floating_arguments = 0;
                for (uint32_t a = 0; a < function->call_argument_counts[call]; a += 1)
                {
                    const bool is_floating = tier0_class_floating(function->argument_classes[call_first + a]);
                    integer_arguments += is_floating ? 0 : 1;
                    floating_arguments += is_floating ? 1 : 0;
                }
                const int32_t below = entry_depth - (int32_t)integer_arguments - (function->call_targets[call] == 0 ? 1 : 0);
                const int32_t floating_below = entry_floating - (int32_t)floating_arguments;
                const uint32_t saved = below < BUSTER_TIER0_CALLER_SAVED_DEPTHS ? (uint32_t)below : BUSTER_TIER0_CALLER_SAVED_DEPTHS;
                const uint32_t slots = saved + (uint32_t)floating_below;
                save_slots = slots > save_slots ? slots : save_slots;
                const uint32_t result = tier0_stack(function->classes[i]);
                deferred_integer_top = result == 1 && below > deferred_integer_top ? below : deferred_integer_top;
                deferred_floating_top = result == 2 && floating_below > deferred_floating_top ? floating_below : deferred_floating_top;
                clobbers |= BUSTER_TIER0_GENERAL_CALLER_SAVED | BUSTER_TIER0_XMM_ALL;
                calls = true;
            }
            else if (function->kinds[i] == BUSTER_TYPED_COPY)
            {
                const bool wide = function->values[i] >= 16;
                deferred_integer_top = !wide && entry_depth > deferred_integer_top ? entry_depth : deferred_integer_top;
                deferred_floating_top = wide && entry_floating > deferred_floating_top ? entry_floating : deferred_floating_top;
            }
            else
            {
                clobbers |= 1u << X86_REGISTER_RAX | 1u << X86_REGISTER_RDX | 1u << X86_REGISTER_RCX;
                fixed = true;
            }
        }
    }
    // The first unsupported entry, rare: a compile that fails.
    for (uint32_t i = 0; i < count && any_unsupported != 0 && unsupported == count; i += 1)
    {
        unsupported = (words[i] & BUSTER_TIER0_WORD_UNSUPPORTED) != 0 ? i : count;
    }
    const bool rdx = (_mm512_reduce_or_epi64(kept_words) & BUSTER_TIER0_WORD_CLOBBERS_RDX) != 0;
    const int64_t vector_integer_top = _mm512_reduce_max_epi64(integer_top);
    const int64_t vector_floating_top = _mm512_reduce_max_epi64(floating_top);
    const int32_t integer_reached = vector_integer_top > deferred_integer_top ? (int32_t)vector_integer_top : deferred_integer_top;
    const int32_t floating_reached = vector_floating_top > deferred_floating_top ? (int32_t)vector_floating_top : deferred_floating_top;
    clobbers |= rdx ? 1u << X86_REGISTER_RDX : 0;
    *error = integer_reached >= BUSTER_TIER0_INTEGER_DEPTHS || floating_reached >= BUSTER_TIER0_FLOATING_DEPTHS ? TIER0_ERROR_DEPTH : TIER0_OK;
    frame->integer_top = integer_reached;
    frame->floating_top = floating_reached;
    frame->clobbers = clobbers | tier0_depth_registers(integer_reached, floating_reached);
    frame->save_slots = save_slots;
    frame->fixed_slots = fixed ? 2 : 0;
    frame->calls = calls;
    return unsupported;
}

// Measuring for the vector path, 8 words at a time, from the prologue's length on:
// - a home-dependent entry's word is resolved to its homes' case: for a uniform slot, the case added to the template;
//   otherwise, the case's word gathered;
// - each entry's length comes from its word, or for a deferred one from tier0_count_deferred (an entry at a time: they
//   are few);
// - the positions are the lengths' prefix sums, in registers;
// - the branch records', references' and choice requests' counts come from popcounts.
// The last block's words and positions are stored whole, past the entries: emission expands every lane of a block, and a
// lane past the entries has template 0 and the end's position, which the epilogue overwrites.
static Tier0Counts tier0_measure_vector(const Tier0Function* function, Tier0Entries* entries, const Tier0Home* homes, uint32_t prologue)
{
    const uint32_t count = function->count;
    const uint64_t* values = function->values;
    uint64_t* words = entries->words;
    uint32_t* positions = entries->positions;
    const Tier0HomeTable table = tier0_home_table(homes, function->local_count);
    Tier0Counts counts = { 0, 0, 0 };
    const __m512i seven = _mm512_set1_epi64(7);
    const __m512i zero = _mm512_setzero_si512();
    const __m512i is_register = _mm512_set1_epi64((int64_t)(0xffull << (8 * offsetof(Tier0Home, is_register))));
    // Up to 64 locals, which of them are in registers as one mask: a lane's case is then a shift of it.
    const bool few = function->local_count <= 64;
    uint64_t registers = 0;
    for (uint32_t first = 0; first < function->local_count && few; first += 8)
    {
        const __m512i eight = _mm512_maskz_loadu_epi64((__mmask8)tier0_lanes(first, function->local_count), homes + first);
        registers |= (uint64_t)_mm512_test_epi64_mask(eight, is_register) << first;
    }
    const __m512i register_bits = _mm512_set1_epi64((int64_t)registers);
    const __m512i one = _mm512_set1_epi64(1);
    __m512i carry = _mm512_set1_epi64(prologue);
    __m512i previous = zero;
    for (uint32_t first = 0; first < count; first += 8)
    {
        const __mmask8 lanes = (__mmask8)tier0_lanes(first, count);
        const __m512i current = _mm512_maskz_loadu_epi64(lanes, values + first);
        __m512i word = _mm512_maskz_loadu_epi64(lanes, words + first);
        const __mmask8 dependent = _mm512_test_epi64_mask(word, _mm512_set1_epi64((int64_t)BUSTER_TIER0_WORD_HOME_DEPENDENT)) &
                                   _mm512_testn_epi64_mask(word, _mm512_set1_epi64((int64_t)BUSTER_TIER0_WORD_DEFERRED));
        if (dependent != 0)
        {
            const __m512i a_offset = _mm512_and_si512(_mm512_srli_epi64(word, BUSTER_TIER0_WORD_A_SHIFT), seven);
            const __m512i b_offset = _mm512_and_si512(_mm512_srli_epi64(word, BUSTER_TIER0_WORD_B_SHIFT), seven);
            const __mmask8 a_valid = dependent & _mm512_cmpneq_epi64_mask(a_offset, seven);
            const __mmask8 b_valid = dependent & _mm512_cmpneq_epi64_mask(b_offset, seven);
            const __m512i a_local = tier0_value_back(previous, current, a_offset);
            const __m512i b_local = tier0_value_back(previous, current, b_offset);
            __mmask8 a_register = 0;
            __mmask8 b_register = 0;
            if (few)
            {
                a_register = a_valid & _mm512_test_epi64_mask(_mm512_srlv_epi64(register_bits, a_local), one);
                b_register = b_valid & _mm512_test_epi64_mask(_mm512_srlv_epi64(register_bits, b_local), one);
            }
            else
            {
                a_register = _mm512_test_epi64_mask(tier0_home_lookup(&table, a_local, a_valid), is_register);
                b_register = _mm512_test_epi64_mask(tier0_home_lookup(&table, b_local, b_valid), is_register);
            }
            __m512i cases = _mm512_maskz_mov_epi64(a_register, one);
            cases = _mm512_mask_add_epi64(cases, b_register, cases, _mm512_set1_epi64(2));
            const __mmask8 uniform = dependent & _mm512_test_epi64_mask(word, _mm512_set1_epi64((int64_t)BUSTER_TIER0_WORD_HOME_UNIFORM));
            const __mmask8 gathered = dependent & ~uniform;
            word = _mm512_mask_add_epi64(word, uniform, word, cases);
            if (gathered != 0)
            {
                const __m512i index = _mm512_add_epi64(_mm512_and_si512(word, _mm512_set1_epi64((int64_t)BUSTER_TIER0_WORD_TEMPLATE)), cases);
                word = _mm512_mask_i64gather_epi64(word, gathered, index, tier0_template_words, 8);
            }
            word = _mm512_mask_andnot_epi64(word, dependent, _mm512_set1_epi64((int64_t)(BUSTER_TIER0_WORD_HOME_DEPENDENT | BUSTER_TIER0_WORD_HOME_UNIFORM)), word);
        }
        _mm512_storeu_si512(words + first, word);
        const __mmask8 deferred = lanes & _mm512_test_epi64_mask(word, _mm512_set1_epi64((int64_t)BUSTER_TIER0_WORD_DEFERRED));
        const __mmask8 template = lanes & _mm512_testn_epi64_mask(word, _mm512_set1_epi64((int64_t)(BUSTER_TIER0_WORD_DEFERRED | BUSTER_TIER0_WORD_ABSORBED)));
        __m512i length = _mm512_maskz_and_epi64(template, _mm512_srli_epi64(word, BUSTER_TIER0_WORD_LENGTH_SHIFT), seven);
        uint32_t rest = deferred;
        while (rest != 0)
        {
            const uint32_t lane = (uint32_t)__builtin_ctz(rest);
            rest &= rest - 1;
            const uint32_t records = tier0_count_deferred(function, entries, first + lane, &counts.references);
            length = _mm512_mask_set1_epi64(length, (__mmask8)(1u << lane), (int64_t)records);
        }
        counts.branches += (uint32_t)__builtin_popcount(template & _mm512_test_epi64_mask(word, _mm512_set1_epi64((int64_t)BUSTER_TIER0_WORD_BRANCH)));
        counts.references += (uint32_t)__builtin_popcount(template & _mm512_test_epi64_mask(word, _mm512_set1_epi64((int64_t)BUSTER_TIER0_WORD_FIXUP)));
        counts.choices += (uint32_t)__builtin_popcount(template & _mm512_test_epi64_mask(word, _mm512_set1_epi64((int64_t)BUSTER_TIER0_WORD_CHOICE)));
        __m512i sum = _mm512_add_epi64(length, _mm512_alignr_epi64(length, zero, 7));
        sum = _mm512_add_epi64(sum, _mm512_alignr_epi64(sum, zero, 6));
        sum = _mm512_add_epi64(sum, _mm512_alignr_epi64(sum, zero, 4));
        _mm256_storeu_si256((__m256i*)(positions + first), _mm512_cvtepi64_epi32(_mm512_add_epi64(carry, _mm512_sub_epi64(sum, length))));
        carry = _mm512_add_epi64(carry, _mm512_permutexvar_epi64(seven, sum));
        previous = current;
    }
    positions[count] = (uint32_t)_mm_cvtsi128_si64(_mm512_castsi512_si128(carry));
    return counts;
}

// One lane of emission: entry i's template expanded from `pair` (its hole block in the low 256 bits, or in the high ones
// for an odd lane, whose indices take 32 more), stored at its position, and its choice record's reg, rm, index and mode
// bytes put in dword `lane` of `fields`. A lane of an absorbed or deferred entry, or past the entries, expands its word's
// template too: the later lanes, the deferred entries and the epilogue write over whatever it stores.
static inline __attribute__((always_inline)) __m512i tier0_expand(const uint64_t* words, const uint32_t* positions, X86Instruction* records, uint32_t i, uint32_t lane,
                                                                 __m512i pair, __m512i choice_fields, __m512i fields)
{
    const uint32_t index = (uint32_t)(words[i] & BUSTER_TIER0_WORD_TEMPLATE);
    const __m512i compiled = _mm512_load_si512(tier0_compiled[index].bytes);
    const __m512i indices = (lane & 1) != 0 ? _mm512_or_si512(compiled, _mm512_set1_epi8(32)) : compiled;
    const __m512i record = _mm512_mask_blend_epi8(tier0_masks[index], compiled, _mm512_permutexvar_epi8(indices, pair));
    _mm512_storeu_si512(records + positions[i], record);
    return _mm512_mask_permutexvar_epi32(fields, (__mmask16)(1u << lane), choice_fields, record);
}

// Emission for the vector path, 8 entries at a time:
// - the hole blocks are computed in vectors (K from the values 0 to 3 entries back, A and B from the homes, the
//   registers from the depths' windows) and moved by vpermt2q into four registers of two blocks each;
// - every lane is expanded (tier0_expand), in order, so that each store writes over the ones before it that ran into its
//   records;
// - the choice requests are compressed into their columns, their contexts read from the expanded records;
// - the branches, then the references and the deferred entries, are written an entry at a time in order, the deferred
//   entries over what the lanes stored at their places.
static void tier0_emit_vector(const Tier0Function* function, const Tier0Entries* entries, const Tier0Frame* frame, const Tier0Home* homes, const uint32_t* references,
                              Tier0Code* code, Tier0Choices* choices)
{
    const uint32_t count = function->count;
    const uint64_t* values = function->values;
    const uint64_t* words = entries->words;
    const uint32_t* positions = entries->positions;
    const uint8_t* depths = entries->depths;
    const uint8_t* floating_depths = entries->floating_depths;
    X86Instruction* records = code->records;
    uint32_t* branch_records = code->branches;
    uint32_t* targets = code->targets;
    LinkReference* links = code->references;
    uint32_t* choice_records = choices->records;
    uint16_t* rows = choices->rows;
    int64_t* choice_values = choices->values;
    uint16_t* contexts = choices->contexts;
    const Tier0HomeTable table = tier0_home_table(homes, function->local_count);
    Tier0Writer writer = { records, 0, links, 0 };
    tier0_emit_prologue(function, frame, homes, references, &writer);
    uint32_t branches = 0;
    uint32_t requests = 0;
    uint32_t deferred_count = 0;
    const __m512i seven = _mm512_set1_epi64(7);
    const __m512i zero = _mm512_setzero_si512();
    const __m512i integer_low = _mm512_load_si512(tier0_integer_window);
    const __m512i integer_high = _mm512_load_si512(tier0_integer_window + 16);
    const __m512i floating_low = _mm512_load_si512(tier0_floating_window);
    const __m512i floating_high = _mm512_load_si512(tier0_floating_window + 16);
    const __m512i interleave_low = _mm512_set_epi64(11, 3, 10, 2, 9, 1, 8, 0);
    const __m512i interleave_high = _mm512_set_epi64(15, 7, 14, 6, 13, 5, 12, 4);
    const __m512i pair_low = _mm512_set_epi64(11, 10, 3, 2, 9, 8, 1, 0);
    const __m512i pair_high = _mm512_set_epi64(15, 14, 7, 6, 13, 12, 5, 4);
    __m512i previous = zero;
    for (uint32_t first = 0; first < count; first += 8)
    {
        const __mmask8 lanes = (__mmask8)tier0_lanes(first, count);
        const __m512i current = _mm512_maskz_loadu_epi64(lanes, values + first);
        const __m512i word = _mm512_loadu_si512(words + first);
        const __m512i k_offset = _mm512_and_si512(_mm512_srli_epi64(word, BUSTER_TIER0_WORD_K_SHIFT), seven);
        const __m512i k_scale = _mm512_and_si512(_mm512_srli_epi64(word, BUSTER_TIER0_WORD_K_SCALE_SHIFT), seven);
        const __m512i a_offset = _mm512_and_si512(_mm512_srli_epi64(word, BUSTER_TIER0_WORD_A_SHIFT), seven);
        const __m512i b_offset = _mm512_and_si512(_mm512_srli_epi64(word, BUSTER_TIER0_WORD_B_SHIFT), seven);
        const __m512i k_value = _mm512_maskz_mov_epi64(_mm512_cmpneq_epi64_mask(k_offset, seven), tier0_value_back(previous, current, k_offset));
        const __m512i scale = _mm512_mask_mov_epi64(_mm512_set1_epi64(1), _mm512_cmpneq_epi64_mask(k_scale, seven), tier0_value_back(previous, current, k_scale));
        const __m512i k = _mm512_mullo_epi64(k_value, scale);
        const __m512i a = tier0_home_lookup(&table, tier0_value_back(previous, current, a_offset), lanes & _mm512_cmpneq_epi64_mask(a_offset, seven));
        const __m512i b = tier0_home_lookup(&table, tier0_value_back(previous, current, b_offset), lanes & _mm512_cmpneq_epi64_mask(b_offset, seven));
        const __m512i depth = _mm512_cvtepu8_epi32(_mm_maskz_loadu_epi8(lanes, depths + first));
        const __m512i floating = _mm512_cvtepu8_epi32(_mm_maskz_loadu_epi8(lanes, floating_depths + first));
        const __m512i integer_window = _mm512_permutex2var_epi32(integer_low, depth, integer_high);
        const __m512i floating_window = _mm512_permutex2var_epi32(floating_low, floating, floating_high);
        const __m512i registers = _mm512_or_si512(_mm512_cvtepu32_epi64(_mm512_castsi512_si256(integer_window)),
                                                  _mm512_slli_epi64(_mm512_cvtepu32_epi64(_mm512_castsi512_si256(floating_window)), 24));
        // The blocks: K and A interleaved, B and the registers interleaved, then each pair of entries' quadwords.
        const __m512i ka_low = _mm512_permutex2var_epi64(k, interleave_low, a);
        const __m512i ka_high = _mm512_permutex2var_epi64(k, interleave_high, a);
        const __m512i br_low = _mm512_permutex2var_epi64(b, interleave_low, registers);
        const __m512i br_high = _mm512_permutex2var_epi64(b, interleave_high, registers);
        const __m512i pair0 = _mm512_permutex2var_epi64(ka_low, pair_low, br_low);
        const __m512i pair1 = _mm512_permutex2var_epi64(ka_low, pair_high, br_low);
        const __m512i pair2 = _mm512_permutex2var_epi64(ka_high, pair_low, br_high);
        const __m512i pair3 = _mm512_permutex2var_epi64(ka_high, pair_high, br_high);
        const __m512i at = _mm512_and_si512(_mm512_srli_epi64(word, BUSTER_TIER0_WORD_CHOICE_SHIFT), _mm512_set1_epi64(3));
        const __m512i choice_fields = _mm512_castsi256_si512(_mm512_cvtepi64_epi32(_mm512_add_epi64(_mm512_slli_epi64(at, 2), _mm512_set1_epi64(2))));
        __m512i fields = zero;
        fields = tier0_expand(words, positions, records, first + 0, 0, pair0, choice_fields, fields);
        fields = tier0_expand(words, positions, records, first + 1, 1, pair0, choice_fields, fields);
        fields = tier0_expand(words, positions, records, first + 2, 2, pair1, choice_fields, fields);
        fields = tier0_expand(words, positions, records, first + 3, 3, pair1, choice_fields, fields);
        fields = tier0_expand(words, positions, records, first + 4, 4, pair2, choice_fields, fields);
        fields = tier0_expand(words, positions, records, first + 5, 5, pair2, choice_fields, fields);
        fields = tier0_expand(words, positions, records, first + 6, 6, pair3, choice_fields, fields);
        fields = tier0_expand(words, positions, records, first + 7, 7, pair3, choice_fields, fields);
        const __mmask8 template = lanes & _mm512_testn_epi64_mask(word, _mm512_set1_epi64((int64_t)(BUSTER_TIER0_WORD_DEFERRED | BUSTER_TIER0_WORD_ABSORBED)));
        const __mmask8 choice = template & _mm512_test_epi64_mask(word, _mm512_set1_epi64((int64_t)BUSTER_TIER0_WORD_CHOICE));
        if (choice != 0)
        {
            // The context of each request (tier0_choice_context's) from its record's rm and mode, then the four columns
            // compressed, each stored whole (the columns have room for 8 past the last request).
            const __m512i position = _mm512_cvtepu32_epi64(_mm256_loadu_si256((const __m256i*)(positions + first)));
            const __m512i chosen = _mm512_cvtepu32_epi64(_mm512_castsi512_si256(fields));
            const __m512i rm = _mm512_and_si512(_mm512_srli_epi64(chosen, 8), _mm512_set1_epi64(0xff));
            const __m512i mode = _mm512_srli_epi64(chosen, 24);
            const __mmask8 direct = _mm512_cmpeq_epi64_mask(mode, _mm512_set1_epi64(BUSTER_X86_REGISTER_DIRECT));
            const __mmask8 accumulator = direct & _mm512_cmpeq_epi64_mask(rm, _mm512_set1_epi64(X86_REGISTER_RAX));
            __m512i context = _mm512_or_si512(_mm512_set1_epi64(BUSTER_X86_CONTEXT_INC_DEC), _mm512_and_si512(_mm512_srli_epi64(word, BUSTER_TIER0_WORD_LIVE_SHIFT), _mm512_set1_epi64(0x3f)));
            context = _mm512_mask_or_epi64(context, direct, context, _mm512_set1_epi64(BUSTER_X86_CONTEXT_REGISTER));
            context = _mm512_mask_or_epi64(context, accumulator, context, _mm512_set1_epi64(BUSTER_X86_CONTEXT_ACCUMULATOR));
            const __mmask8 dynamic = choice & _mm512_testn_epi64_mask(word, _mm512_set1_epi64((int64_t)BUSTER_TIER0_WORD_CHOICE_STATIC));
            _mm256_storeu_si256((__m256i*)(choice_records + requests), _mm256_maskz_compress_epi32(choice, _mm512_cvtepi64_epi32(_mm512_add_epi64(position, at))));
            _mm_storeu_si128((__m128i*)(rows + requests), _mm_maskz_compress_epi16(choice, _mm512_cvtepi64_epi16(_mm512_srli_epi64(word, BUSTER_TIER0_WORD_ROW_SHIFT))));
            _mm512_storeu_si512(choice_values + requests, _mm512_maskz_compress_epi64(choice, _mm512_maskz_mov_epi64(dynamic, k)));
            _mm_storeu_si128((__m128i*)(contexts + requests), _mm_maskz_compress_epi16(choice, _mm512_cvtepi64_epi16(context)));
            requests += (uint32_t)__builtin_popcount(choice);
        }
        uint32_t branch = template & _mm512_test_epi64_mask(word, _mm512_set1_epi64((int64_t)BUSTER_TIER0_WORD_BRANCH));
        while (branch != 0)
        {
            const uint32_t i = first + (uint32_t)__builtin_ctz(branch);
            branch &= branch - 1;
            const uint64_t entry_word = words[i];
            const uint32_t target = (entry_word & BUSTER_TIER0_WORD_END) != 0 ? count : (uint32_t)values[i];
            branch_records[branches] = positions[i] + (uint32_t)(entry_word >> BUSTER_TIER0_WORD_BRANCH_SHIFT & 3);
            targets[branches] = positions[target];
            branches += 1;
        }
        // The references and the deferred entries, in entry order: a call's references come where its records do.
        const uint32_t deferred = lanes & _mm512_test_epi64_mask(word, _mm512_set1_epi64((int64_t)BUSTER_TIER0_WORD_DEFERRED));
        uint32_t rest = deferred | (template & _mm512_test_epi64_mask(word, _mm512_set1_epi64((int64_t)BUSTER_TIER0_WORD_FIXUP)));
        while (rest != 0)
        {
            const uint32_t lane = (uint32_t)__builtin_ctz(rest);
            rest &= rest - 1;
            const uint32_t i = first + lane;
            if ((deferred >> lane & 1) != 0)
            {
                writer.count = positions[i];
                tier0_emit_deferred(function, entries, frame, homes, i, &writer);
                deferred_count += 1;
            }
            else
            {
                links[writer.reference_count] = (LinkReference){
                    .instruction = positions[i] + (uint32_t)(words[i] >> BUSTER_TIER0_WORD_FIXUP_SHIFT & 3),
                    .field = X86_FIXUP_DISPLACEMENT,
                    .kind = BUSTER_LINK_FIXUP_PC32,
                    .target = (SymbolId)values[i],
                    .addend = 0,
                };
                writer.reference_count += 1;
            }
        }
        previous = current;
    }
    writer.count = positions[count];
    tier0_emit_epilogue(frame, &writer);
    code->branch_count = branches;
    code->reference_count = writer.reference_count;
    code->deferred += deferred_count;
    choices->count = requests;
}

static Tier0Code tier0_compile(const Tier0Function* function, Arena* output)
{
    Tier0Code code;
    memset(&code, 0, sizeof(code));
    const uint64_t output_length = output->length;
    Scratch scratch = scratch_begin(output);
    Tier0Entries entries;
    const uint32_t count = function->count;
    const uint32_t blocks = (count + 63) / 64;
    Tier0Bits bits;
    memset(&bits, 0, sizeof(bits));
    bits.blocks = blocks;
    uint64_t** columns[] = { &bits.fits, &bits.count, &bits.zero, &bits.size, &bits.product, &bits.same, &bits.sib, &bits.labels, &bits.leaf,
                             &bits.home_const, &bits.one, &bits.two, &bits.rmw };
    bool pushed = tier0_push_entries(&entries, scratch.arena, count);
    for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(columns); c += 1)
    {
        // A word past the blocks for the labels' target past the last entry (the function's end).
        *columns[c] = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint64_t, (uint64_t)blocks + 2);
        pushed = pushed && *columns[c] != 0;
        if (*columns[c] != 0)
        {
            memset(*columns[c], 0, ((uint64_t)blocks + 2) * sizeof(uint64_t));
        }
    }
    bits.sib_log2 = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint8_t, (uint64_t)blocks * 64 + 64);
    Tier0Error error = pushed && bits.sib_log2 != 0 ? TIER0_OK : TIER0_ERROR_MEMORY;
    if (error == TIER0_OK)
    {
        tier0_vector_labels(function, bits.labels);
        tier0_vector_values(function, &bits);
        tier0_vector_fuse(function, &bits, &entries);
        tier0_vector_depths(function, &bits, &entries);
        error = tier0_generate(function, &entries, scratch.arena, output, &code, true);
    }
    code = tier0_conclude(code, error, output, output_length);
    scratch_end(scratch);
    return code;
}
