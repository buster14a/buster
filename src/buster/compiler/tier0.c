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
#include "host_isa.h"

// ---------------------------------------------------------------------------------------------------------------------
// The typed stream

// An entry's machine class: what a value is to the code generator. Integers by width and signedness (a pointer is U64),
// the two SSE floating types, long double's x87 extended type (F80), the 128-bit integers (I128 and U128) and vectors
// (VECTOR: 16, 32 or 64 bytes, GNU C's vector types and the intrinsics' __m128 to __m512i). A value of a class narrower
// than its register holds its value in the register's low bits; the rest are unspecified, so a consumer reads only the
// class's width (a 32-bit operation, a movsx). An F80, I128 or U128 value (a memory class's, tier0_class_memory) takes an
// integer depth, as an integer would, and lives in that depth's 16-byte frame slot (Tier0Frame.memory_area), not in its
// register: calls and the arms of ?: then need nothing of their own for it. A VECTOR value does too, in that depth's
// 64-byte frame slot (Tier0Frame.vector_area), its bytes from the slot's first; its width is the entry's (LOAD's value,
// an object's size, a constant's), which tier0_validate follows from depth to depth, as a class.
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
#define BUSTER_TYPED_CLASS_F80 11
#define BUSTER_TYPED_CLASS_I128 12
#define BUSTER_TYPED_CLASS_U128 13
#define BUSTER_TYPED_CLASS_VECTOR 14
#define BUSTER_TYPED_CLASSES 15
// Every class below 16, so that a kind and a class make a byte table's index.
#define BUSTER_TYPED_CLASS_LIMIT 16

// The kinds. Below, "the operand" of an entry that takes one is the entry before it (the root of its last operand, in
// postorder), whose class is "the operand's class"; a two-operand entry's first operand comes before that one's
// subtree. Both paths read the last operand's class from the entry before, so tier0_validate refuses a last operand of
// another class; a value left under a store of its class (an increment's DUP leaves the old one under its SET) has it
// and is compiled right. `class` is the entry's own class column; `value` its value column.
//   - Leaves, which push one value of `class`:
//     CONST: the value's bits (an integer sign- or zero-extended by its class to 64 bits; a float's bits, or a
//     double's). LOCAL: local `value`'s value. LOCAL_ADDRESS: its address (class U64; the local must have
//     BUSTER_TYPED_LOCAL_ADDRESS_TAKEN). GLOBAL: the value of the object at SymbolId `value`. GLOBAL_ADDRESS: the
//     address of SymbolId `value` (an object, a string literal or a function; class U64). Each symbol is reached as
//     Tier0Function.symbol_access says (directly without the column), and so is STORE_GLOBAL's and SET_GLOBAL's.
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
//     same or apart. A front end copies more through a runtime call (the C producer uses memmove for self-assignment).
//   - ALLOCA: reserves as many bytes as the operand (U64) says, rounded up to a multiple of 16 (of the frame's alignment
//     in a realigned frame, which so stays aligned), on the stack, and pushes their address (`class` U64, a multiple of
//     16; `value` 0): a variable-length array's storage. The bytes stay until a STACK_RESTORE releases them or the
//     function returns.
//   - STACK_SAVE: pushes the stack's position (`class` U64; `value` 0), which only STACK_RESTORE reads.
//   - STACK_RESTORE: pops the operand (U64), a position STACK_SAVE pushed in this function, and releases what ALLOCA
//     has reserved since (`class` U64; `value` 0): the end of a variable-length array's scope.
//   - JUMP_INDIRECT: pops the operand (U64), the address of one of the function's labels (Tier0Function.label_entries),
//     and goes there (`class` U64; `value` 0): GNU C's goto *. At statement level, as the labels are: an address of a
//     label is a GLOBAL_ADDRESS of the symbol a front end defines at its record (Tier0Code.label_records).
//   - X86: the machine operation `value` of Tier0Function.operations (Tier0Operation, a short run of records), which
//     pops its operands, the first pushed first, of the classes its row says, and pushes its result, of the row's result
//     class (`class`, or nothing for VOID): a vector operation, an intrinsic's instruction.
//   - FRAME_ADDRESS: pushes the function's frame address (`class` U64; `value` 0), its frame pointer: the address of the
//     caller's rbp that its prologue pushed, under the return address and 16 bytes below its incoming arguments, as
//     clang -O0's frame has it (__builtin_frame_address(0)).
// A function with ALLOCA, STACK_SAVE, STACK_RESTORE or FRAME_ADDRESS has a frame pointer, rbp, which then neither a depth
// nor a home takes: its integer stack has one depth fewer. So has a function whose frame is realigned (an object, or a call's
// outgoing area, aligned past 16 bytes: Tier0Frame.alignment_log2); with those entries too, r15 holds the realigned
// frame's address, which rsp leaves, and its integer stack has two depths fewer.
// Long double (F80) takes CONST (`value` an index into Tier0Function.x87_constants), DUP, NEG, CONVERT (to and from every
// class; to an integer by truncation, as C converts), LOAD, STORE and SET (its 10 bytes at the address), ADD, SUB, MUL and
// DIV, the compares (IEEE's, as the floating types'), CALL (arguments on the stack, a result in st0), DISCARD, JUMP_VALUE
// and RETURN (in st0). Its locals and parameters are objects and its globals are reached through their addresses: no
// GLOBAL, STORE_GLOBAL or SET_GLOBAL of F80, and LOCAL, STORE_LOCAL and SET_LOCAL of F80 only on an object of at least 16
// bytes, whose first 10 they read or write (a front end's temporaries of the class, and its locals'). Its arithmetic is
// the x87's at the control word the convention keeps (64-bit significands, rounding to nearest).
// The 128-bit integers (I128 and U128) take CONST (`value` the low 64 bits, sign-extended for I128 and zero-extended for
// U128: a front end builds a wider constant of them), DUP, NEG, NOT, CONVERT (to and from the classes I8 to U64, which
// keeps the low bits, and between the two), LOAD, STORE and SET (16 bytes at the address), ADD, SUB, MUL, AND, OR, XOR, SHL
// and SHR (by a count of a class I8 to U64, below 128; SHR by the class's signedness), the compares, DISCARD and
// JUMP_VALUE, with the same rules for their locals and globals as long double's: LOCAL, STORE_LOCAL and SET_LOCAL of them
// read or write an object's first 16 bytes. A front end divides them, converts them to and from the floating classes,
// and passes and returns them through calls of its runtime's functions and objects: an argument, a parameter or a result
// of one is a 16-byte object, two INTEGER eightbytes as the convention passes it.
// Vectors (VECTOR) take CONST (`value` an index into Tier0Function.vector_constants), DUP, LOAD, STORE and SET (`value`
// the vector's bytes at the address: 16, 32 or 64), LOCAL, STORE_LOCAL and SET_LOCAL (of an object of the vector's size,
// all of whose bytes they read or write), DISCARD and JUMP_VALUE, in a function that says it has them
// (Tier0Function.vectors). Their operations are X86 entries, whose rows may also take and push integers and floating
// values. Their globals are reached through their addresses, as long double's are, and a front end passes and returns
// them through calls as aggregates: an argument, a parameter or a result of one is an object whose bytes one vector
// register holds whole (an XMM, YMM or ZMM register), as the convention passes it, or the stack.
// A target `value` is an entry's index, or the entry count for the function's end. Every target is the start of a
// statement or of an arm, and the stacks there must hold the same values whichever way control arrives (as in a
// WebAssembly validator): tier0_validate checks it. A label of label_entries is a target JUMP_INDIRECT may reach, with
// nothing on the stacks.
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
#define BUSTER_TYPED_ALLOCA 42
#define BUSTER_TYPED_STACK_SAVE 43
#define BUSTER_TYPED_STACK_RESTORE 44
// After the stack's other kinds, so that the four are one range (tier0_moves_stack).
#define BUSTER_TYPED_FRAME_ADDRESS 45
#define BUSTER_TYPED_JUMP_INDIRECT 46
// The vectors' kind, the last, which the validators' paths test as one with the kinds past it.
#define BUSTER_TYPED_X86 47
#define BUSTER_TYPED_KINDS 48
// Every kind below 64, so that a kind table is one ZMM register for vpermb.
#define BUSTER_TYPED_KIND_LIMIT 64
// Tier 0's own kinds, past the stream's: GLOBAL, GLOBAL_ADDRESS, STORE_GLOBAL and SET_GLOBAL, in that order from each
// one's first, of a symbol that Tier0Function.symbol_access reaches through its GOT slot, as a thread-local of the image's
// or as a thread-local through its GOT slot. Fusion keys an entry by them (tier0_access_kind), so that they select
// templates of their own; no stream has them, and a kind the stream gains moves them along.
#define BUSTER_TIER0_KIND_GOT BUSTER_TYPED_KINDS
#define BUSTER_TIER0_KIND_LOCAL_EXEC (BUSTER_TIER0_KIND_GOT + 4)
#define BUSTER_TIER0_KIND_INITIAL_EXEC (BUSTER_TIER0_KIND_LOCAL_EXEC + 4)
#define BUSTER_TIER0_KINDS (BUSTER_TIER0_KIND_INITIAL_EXEC + 4)
_Static_assert(BUSTER_TIER0_KINDS <= BUSTER_TYPED_KIND_LIMIT, "Tier 0's own kinds below the kind tables' limit");

// How the code reaches a symbol that GLOBAL, GLOBAL_ADDRESS, STORE_GLOBAL and SET_GLOBAL name (Tier0Function's
// symbol_access):
//   - DIRECT: the symbol is in the image, RIP-relative: [rip + symbol], a PC32.
//   - GOT: the symbol may be outside the image (in a shared library): its address from its GOT slot, mov r64, [rip +
//     slot] (a LOAD_GOT, which a linker relaxes to lea where the symbol turns out to be in the image), then the access
//     through that register. The direct path has no copy relocations, so a shared library's data is reached this way,
//     and so is a function's address that may be one.
//   - LOCAL_EXEC: a thread-local of the image's, fs:[its offset from the thread pointer] (a TPOFF32): an executable's.
//   - INITIAL_EXEC: a thread-local that may be outside the image: its offset from its GOT slot, mov r64, [rip + slot] (a
//     GOTTPOFF), then fs:[that register].
#define BUSTER_TIER0_ACCESS_DIRECT 0
#define BUSTER_TIER0_ACCESS_GOT 1
#define BUSTER_TIER0_ACCESS_LOCAL_EXEC 2
#define BUSTER_TIER0_ACCESS_INITIAL_EXEC 3
#define BUSTER_TIER0_ACCESSES 4

// A local's flags: its address is taken (LOCAL_ADDRESS), so it lives in the frame. The three object roles below
// are mutually exclusive, nonparameter U64 descriptors with ADDRESS_TAKEN, usable only through LOCAL_ADDRESS.
// VA_LIST owns exactly sizeof(AbiVaList) bytes. The other roles alias the incoming variadic frame's two anchors.
#define BUSTER_TYPED_LOCAL_ADDRESS_TAKEN 0x01
#define BUSTER_TYPED_LOCAL_VA_LIST 0x02
#define BUSTER_TYPED_LOCAL_REGISTER_SAVE_AREA 0x04
#define BUSTER_TYPED_LOCAL_OVERFLOW_ARG_AREA 0x08
#define BUSTER_TYPED_LOCAL_ADDRESS_ONLY (BUSTER_TYPED_LOCAL_VA_LIST | BUSTER_TYPED_LOCAL_REGISTER_SAVE_AREA | BUSTER_TYPED_LOCAL_OVERFLOW_ARG_AREA)

// A machine operation, a row of Tier0Function.operations, which X86 entries name: `record_count` records (up to
// BUSTER_TIER0_OPERATION_RECORDS), emitted in order, with `operand_count` operands (operand_classes, the first pushed
// first: integers, floating values and VECTORs, each VECTOR one's bytes in operand_bytes) and a result (result_class,
// VOID for none; a VECTOR one's bytes in result_bytes). Its records name registers by role, never by number
// (BUSTER_TIER0_ROLE_*), and Tier 0 places each role where the entry's depths leave room:
//   - in a vector register's field (XMM, YMM or ZMM: reg, vvvv, rm, a VSIB operand's index, and an /is4 operand, whose
//     immediate's bits 7 to 4 hold j for VECTOR(j)): VECTOR(j), the operation's vector register j (0 to 15), which
//     holds VECTOR operand j when the records start, and whose register 0 holds the VECTOR result when they end, the
//     others free; FLOATING(j), floating operand j's XMM register; FLOATING_RESULT, the floating result's;
//   - in a general register's field (reg, vvvv, rm, a memory operand's base and index): INTEGER(j), integer operand j's
//     register (an address among them); INTEGER_RESULT, the integer result's; SCRATCH(k), a free one (0 to 3);
//   - an opmask register (a K field, and EVEX's mask decoration): itself, k0 to k7, which no value keeps between entries.
// Each form is a VEX or an EVEX one with its operands in its fields alone, without a branch, a symbol or another class of
// register (tier0_operation_valid), so that a row reads and writes its operands, its result, its scratch registers, the
// opmask registers and memory through its addresses, and nothing else. The VECTOR registers are the floating stack's free
// ones from its depth on (tier0_operation_placement): a VEX form names them below 16, so a row whose VEX forms reach past
// the stack's room is refused at its depth (TIER0_ERROR_DEPTH), as an integer depth past the table is; an EVEX form
// names them to 31.
#define BUSTER_TIER0_OPERATION_RECORDS 8
#define BUSTER_TIER0_OPERATION_OPERANDS 8
#define BUSTER_TIER0_OPERATION_VECTORS 16
#define BUSTER_TIER0_SCRATCH 4
#define BUSTER_TIER0_ROLE 0x80
#define BUSTER_TIER0_ROLE_VECTOR(j) (BUSTER_TIER0_ROLE | (j))
#define BUSTER_TIER0_ROLE_FLOATING(j) (BUSTER_TIER0_ROLE | 0x20 | (j))
#define BUSTER_TIER0_ROLE_FLOATING_RESULT (BUSTER_TIER0_ROLE | 0x30)
#define BUSTER_TIER0_ROLE_INTEGER(j) (BUSTER_TIER0_ROLE | (j))
#define BUSTER_TIER0_ROLE_INTEGER_RESULT (BUSTER_TIER0_ROLE | 0x10)
#define BUSTER_TIER0_ROLE_SCRATCH(k) (BUSTER_TIER0_ROLE | 0x20 | (k))

typedef struct Tier0Operation Tier0Operation;
struct Tier0Operation
{
    X86Instruction records[BUSTER_TIER0_OPERATION_RECORDS];
    uint8_t record_count;
    uint8_t operand_count;
    uint8_t operand_classes[BUSTER_TIER0_OPERATION_OPERANDS];
    uint8_t operand_bytes[BUSTER_TIER0_OPERATION_OPERANDS];
    uint8_t result_class;
    uint8_t result_bytes;
};

// The most a function may have of each (the columns' indices are 32-bit), and of its objects' bytes, each object's
// size rounded up to 16 and, aligned past 16, its alignment less 16 (the most its place may move by), so that every frame
// displacement fits a disp32. An object's alignment, and a call's outgoing area's (AbiCall.stack_alignment_log2), is at
// most 2^30: past 16 bytes, the function's frame is realigned to the most any of them needs (Tier0Frame.alignment_log2).
#define BUSTER_TIER0_MAX_ENTRIES (1u << 24)
// Local identities and homes use 32-bit indices too; the former 16-bit cap refused large generated functions (#622).
#define BUSTER_TIER0_MAX_LOCALS BUSTER_TIER0_MAX_ENTRIES
// The deepest a stack may go: the deep slots' count, with room in the depth columns' bytes.
#define BUSTER_TIER0_MAX_DEPTH 240
#define BUSTER_TIER0_MAX_CALL_ARGUMENTS (BUSTER_TIER0_MAX_DEPTH - 1)
#define BUSTER_TIER0_MAX_OBJECT_BYTES (1u << 30)
#define BUSTER_TIER0_MAX_OBJECT_ALIGNMENT_LOG2 30
// The alignment an ordinary frame keeps (rsp's at every call, as the convention has it), past which it is realigned.
#define BUSTER_TIER0_FRAME_ALIGNMENT_LOG2 4
// A COPY's bytes, at most: 16 stores of 16 bytes, the most LLVM expands a memcpy into at -O0.
#define BUSTER_TIER0_MAX_COPY_BYTES 256
// An aggregate argument's bytes, at most (Tier0Function.argument_sizes): one on the stack is copied into place by a load
// and a store per 16 bytes, so that a call's records stay within 8,192 per argument.
#define BUSTER_TIER0_MAX_ARGUMENT_BYTES (1u << 16)

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
// object and 0 for a value, and local_alignments[l] its alignment's log2, at most BUSTER_TIER0_MAX_OBJECT_ALIGNMENT_LOG2
// (an object aligned past 16 bytes realigns its function's frame). An object's class is VOID, its flags have
// BUSTER_TYPED_LOCAL_ADDRESS_TAKEN, and LOCAL_ADDRESS refers to it: its bytes are loaded and stored through that address
// (PTR_ADD, LOAD, SET). LOCAL, STORE_LOCAL and SET_LOCAL of a class may name an object too, of at least the class's
// bytes (a memory class's slot's 16; tier0_value_object), whose first bytes they read or write: a front end's value
// that must live in memory, as a long double's local, or one aligned past 8 bytes. Its first contents are the front
// end's to write, but for a parameter's. The objects take at most BUSTER_TIER0_MAX_OBJECT_BYTES in all.
//
// Structs and unions passed and returned by value (#137) are three optional parts, 0 for a function without them. An
// aggregate's value on the stacks is its address (U64), as a front end evaluates one:
//   - argument_sizes[a] (argument_count of them): argument a's bytes, nonzero for an aggregate (class U64, its address)
//     and 0 for a value. The call reads its bytes where its location says: copied to the stack area (STACK), its
//     eightbytes loaded into registers (REGISTERS; SPLIT, a part each in a register and in a stack slot), or nothing
//     (IGNORE). A load reads only the aggregate's own bytes, whatever its size. An empty aggregate has size 0 here: it
//     passes as the value its address is, which a register or a slot then holds and no callee reads.
//   - A parameter that is an object: an aggregate passed by value. Its location says where its bytes arrive, and the
//     prologue stores a register's in its home; one on the stack whole (STACK) keeps its incoming bytes as its home at a
//     multiple of its alignment, and the prologue copies the others' slots into their homes (tier0_parameter_in_place).
//   - call_result_objects[c] (call_count of them): 1 + the local index of the object that receives call c's aggregate
//     result, or 0. Its result (AbiCall.result) is REGISTERS, whose parts the call stores in the object, or MEMORY, the
//     object's address the hidden pointer it passes in rdi. The call's class is VOID: its result is the object's.
//   - result_size: the bytes of the function's own aggregate result when `result` returns it in registers (REGISTERS),
//     else 0. Every RETURN has class U64, the address of the result's bytes, which the epilogue loads into the result
//     registers; control never reaches the function's end but through a RETURN. A result through a hidden pointer
//     (MEMORY) is the front end's: the pointer is parameter 0 (U64, arriving in rdi), its bytes copied there, and each
//     RETURN returns it (U64).
// The parts of an aggregate in registers come from abi_assign's locations alone: eightbyte k (0 and 8, or the high one
// alone with BUSTER_ABI_FLAG_HIGH) in registers[k], a general or an XMM register, an XMM one whole (16 bytes) when it is
// the only part of a 16-byte aggregate.
//
// How the code reaches the symbols of GLOBAL, GLOBAL_ADDRESS, STORE_GLOBAL and SET_GLOBAL is an optional column, 0 for
// every symbol in the image: symbol_access[s] is SymbolId s's BUSTER_TIER0_ACCESS_*, for each of the `symbol_count`
// SymbolIds from 0, which must cover every symbol those entries name. It is per unit, so that one column serves every
// function of a unit, and a front end that knows which symbols its unit defines fills it: a thread-local by one of the
// two TLS accesses, any other symbol directly or through the GOT. Calls are unaffected: a call of a function outside the
// image goes through the linker's stub.
//
// Long double's constants are an optional column too, 0 for a function without them: x87_constants holds
// x87_constant_count 80-bit values, two words each (the significand, then the sign and exponent in the low 16 bits), and
// a CONST of class F80 is the one its value indexes. `x87` says whether the function has long double values at all: an
// entry of class F80, or a long double result; and `int128` whether it has 128-bit integer values: an entry of class I128
// or U128. A function with either has the memory classes' slots in its frame, and only its compile looks for their
// entries: one without them compiles as if Tier 0 had none.
//
// The vectors' constants and the machine operations are optional columns too, 0 for a function without them:
// vector_constants holds vector_constant_count constants of 64 bytes each (eight words, the vector's first bytes first,
// those past its bytes unread), vector_constant_bytes each one's bytes (16, 32 or 64), and a CONST of class VECTOR is the
// one its value indexes; operations holds operation_count rows (Tier0Operation), the one an X86 entry's value indexes.
// `vectors` says whether the function has VECTOR values or X86 entries. A function with them has the vectors' slots in
// its frame, and only its compile looks for their entries, as long double's are looked for; and it runs on a processor
// with AVX-512 (F, VL, BW and DQ), whose EVEX forms Tier 0's own records for its vectors are: a front end gives one to a
// unit whose target has them, as the intrinsics that make vectors of 64 bytes need.
//
// The labels whose addresses the code takes (GNU C's labels as values, which JUMP_INDIRECT goes to) are an optional
// column too, 0 for a function without them: label_entries holds label_count entries' indices, each a statement's start
// or the count for the function's end. Tier0Code.label_records gives each one's first record, where the front end
// defines the symbol whose GLOBAL_ADDRESS is the label's address.
//
// entry_records asks for each entry's first record in the code (Tier0Code.entry_records), for source rows (#395); a
// compile without it allocates and copies nothing for them.
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
    const uint32_t* argument_sizes;
    const uint32_t* call_result_objects;
    uint32_t result_size;
    const uint8_t* symbol_access;
    uint32_t symbol_count;
    const uint64_t* x87_constants;
    uint32_t x87_constant_count;
    bool x87;
    bool int128;
    const uint64_t* vector_constants;
    const uint8_t* vector_constant_bytes;
    uint32_t vector_constant_count;
    const Tier0Operation* operations;
    uint32_t operation_count;
    bool vectors;
    const uint32_t* label_entries;
    uint32_t label_count;
    bool entry_records;
};

// Whether local `local` is an object in the frame (Tier0Function.local_sizes).
static bool tier0_is_object(const Tier0Function* function, uint64_t local)
{
    return function->local_sizes != 0 && function->local_sizes[local] != 0;
}

static bool tier0_parameters_valid(const Tier0Function* function);
static bool tier0_aggregates_valid(const Tier0Function* function);

// The optional frame descriptors are checked even when unused. The typed stream itself retains tier0_validate's
// valid-input precondition; these checks also let both compile paths refuse malformed optional frame metadata: the
// parameters' locations, where an incoming variadic area bounds them or objects may be among them
// (tier0_parameters_valid), and the aggregates' (tier0_aggregates_valid). Out of line: the compile paths call it once
// per function, and inline it changed clang's code for their shared passes (tier0_generate) whenever it changed.
static __attribute__((noinline)) bool tier0_frame_input_valid(const Tier0Function* function)
{
    bool valid = function->local_count <= BUSTER_TIER0_MAX_LOCALS && function->parameter_count <= function->local_count;
    const AbiCall* incoming = function->incoming_variadic;
    if (incoming != 0)
    {
        // The incoming area at 16 bytes' alignment, or a named argument's past it, which raises it.
        valid = valid && incoming->integer_registers <= BUSTER_ABI_INTEGER_ARGUMENTS && incoming->vector_registers <= BUSTER_ABI_VECTOR_ARGUMENTS &&
            incoming->stack_alignment_log2 >= BUSTER_ABI_STACK_ALIGNMENT_LOG2 && incoming->stack_alignment_log2 <= BUSTER_TIER0_MAX_OBJECT_ALIGNMENT_LOG2 &&
            incoming->stack_bytes % BUSTER_ABI_SLOT_BYTES == 0 && incoming->stack_bytes <= INT32_MAX;
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
    valid = valid && ((incoming == 0 && function->local_sizes == 0) || tier0_parameters_valid(function));
    return valid && tier0_aggregates_valid(function);
}

// ---------------------------------------------------------------------------------------------------------------------
// The output

typedef enum Tier0Error
{
    TIER0_OK,
    // An entry no template or deferred emitter covers yet: `node` names it.
    TIER0_ERROR_UNSUPPORTED,
    // A stack deeper than the deep slots take (BUSTER_TIER0_MAX_DEPTH), or past the depth tables where deep values don't
    // reach (tier0_select_deep): a function with values of the memory classes (long double's and the 128-bit integers'),
    // or a call with aggregate arguments.
    TIER0_ERROR_DEPTH,
    // The output arena or the scratch arena has no room.
    TIER0_ERROR_MEMORY,
    // tier0_validate's alone: the stream breaks a rule (`node` and Tier0Check.rule say which).
    TIER0_ERROR_INVALID,
} Tier0Error;

// A function's code: `count` records (prologue, body, epilogue), the branches' record indices and targets (contract 1:
// every branch a rel8 with displacement 0, indices ascending, a target the record its label marks or the count for the
// end), and a LinkReference per symbolic field, its instruction a record index. With Tier0Function.entry_records,
// `entry_records` is each of the function's `entry_count` typed entries' first record, and one more, the epilogue's:
// ascending, an entry that emits no record (a leaf fused into its consumer) at the next entry's; with branch sizing's
// offsets, where each entry's code starts in bytes, which source rows give a debugger (debug.c, #395). Without it,
// entry_records is null and entry_count 0. `deferred` counts the entries the vector path left to tier0_emit_deferred
// (the scalar path counts the same ones), and `deep` those among them whose values went to the deep slots
// (tier0_select_deep). `label_records` has a record index for each of Tier0Function.label_entries, label_count of them
// in their order (0 for a function without them): the label's first record. The frame is the one tier0_emit_prologue
// made, for its unwind tables (unwind_tier0_frame, #395): the callee-saved registers it pushed (Tier0Frame.saved's
// bits, in tier0_callee_saved's order), whether it pushed rbp first and pointed it at the push (a frame pointer, which
// a function that moves rsp as it runs has, and one whose frame is realigned), the bytes it reserved below the pushes,
// the log2 of the alignment it realigned rsp to after them (0 for none: `and rsp, -alignment`, a frame pointer's alone)
// and whether it then copied rsp into r15, the realigned frame's base where the body moves rsp (`frame_base`, r15 among
// the pushes). The prologue's pushes, the frame pointer's mov, the reservation, the realignment and the base's mov are
// the code's first records, and the epilogue's release (an add, or with a frame pointer the lea or mov), pops and ret
// its last.
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
    uint32_t* entry_records;
    uint32_t entry_count;
    uint32_t* label_records;
    uint32_t label_count;
    uint32_t deferred;
    uint32_t deep;
    uint32_t frame_saved;
    uint32_t frame_size;
    bool frame_pointer;
    uint8_t frame_alignment_log2;
    bool frame_base;
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
// the stacks at them, locals and calls): the lowering's check of every function it lowers, and the tests'. The vector
// path, which leaves to the scalar validator whatever it doesn't take, so that a refusal is always the scalar's.
static __attribute__((unused)) Tier0Check tier0_validate(const Tier0Function* function);
// The same an entry at a time: the twin, with identical results.
static __attribute__((unused)) Tier0Check tier0_validate_scalar(const Tier0Function* function);

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

// Whether an entry of this kind is one of the stack's, which give its function a frame pointer: those that move rsp in
// the body or read it (ALLOCA, STACK_SAVE and STACK_RESTORE), and FRAME_ADDRESS, which reads the frame pointer itself.
// A function with any of them has the frame of one whose rsp moves.
static bool tier0_moves_stack(uint32_t kind)
{
    _Static_assert(BUSTER_TYPED_FRAME_ADDRESS == BUSTER_TYPED_STACK_RESTORE + 1 && BUSTER_TYPED_STACK_RESTORE == BUSTER_TYPED_ALLOCA + 2,
                   "the stack's kinds are one range");
    return kind >= BUSTER_TYPED_ALLOCA && kind <= BUSTER_TYPED_FRAME_ADDRESS;
}

// The integer depths a function may reach: the table's, less rbp, its last, in a function with a frame pointer (one whose
// entries move rsp, `moves`, or whose frame is `realigned`), and less r15 too, the one before it, in a realigned frame
// whose entries move rsp, which r15 then addresses (Tier0Frame.based).
static int32_t tier0_integer_depth_count(bool realigned, bool moves)
{
    _Static_assert(BUSTER_TIER0_INTEGER_DEPTHS == 15, "rbp is the depth table's last register, and r15 the one before it");
    return BUSTER_TIER0_INTEGER_DEPTHS - (realigned ? 1 : 0) - (moves ? 1 : 0);
}

// The callee-saved registers, in the order the prologue pushes them and homes take them. r15, a realigned frame's base
// (Tier0Frame.based), is the last (BUSTER_TIER0_SAVED_R15, Tier0Frame.saved's bit for it; tier0_initialize checks it).
#define BUSTER_TIER0_CALLEE_SAVED 6
#define BUSTER_TIER0_SAVED_R15 5
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
// The convention's registers: arguments and parameters arrive in rdi, rsi, rdx, rcx, r8, r9 and xmm0 to xmm7, and
// results in rax, rdx, xmm0 and xmm1. abi_assign's locations name no others, and the emitters take no others there: the
// prologue sets a frame pointer's rbp and copies through rax and xmm15 before the parameters move, and doesn't save a
// register a parameter stays in; a call sets al and calls through r11; the epilogue loads the result before it pops the
// callee-saved registers.
#define BUSTER_TIER0_ARGUMENT_REGISTERS                                                                                \
    (1u << X86_REGISTER_RDI | 1u << X86_REGISTER_RSI | 1u << X86_REGISTER_RDX | 1u << X86_REGISTER_RCX |               \
     1u << X86_REGISTER_R8 | 1u << X86_REGISTER_R9 | 0xffu << BUSTER_TIER0_XMM(0))
#define BUSTER_TIER0_RESULT_REGISTERS (1u << X86_REGISTER_RAX | 1u << X86_REGISTER_RDX | 3u << BUSTER_TIER0_XMM(0))

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

// Whether a class is long double's: its values take integer depths and live in their frame slots (BUSTER_TYPED_CLASS_F80).
static bool tier0_class_x87(uint32_t class)
{
    return class == BUSTER_TYPED_CLASS_F80;
}

// Whether a class is a 128-bit integer's (I128, U128), whose values take integer depths and live in their frame slots too.
static bool tier0_class_int128(uint32_t class)
{
    return class == BUSTER_TYPED_CLASS_I128 || class == BUSTER_TYPED_CLASS_U128;
}

// Whether a class is a memory class, whose values take integer depths and live in their 16-byte frame slots
// (Tier0Frame.memory_area): long double's and the 128-bit integers'.
static bool tier0_class_memory(uint32_t class)
{
    return tier0_class_x87(class) || tier0_class_int128(class);
}

// Whether a class is a vector's, whose values take integer depths and live in their 64-byte frame slots
// (Tier0Frame.vector_area).
static bool tier0_class_vector(uint32_t class)
{
    return class == BUSTER_TYPED_CLASS_VECTOR;
}

// A class's width in bytes (0 for VOID), a long double's and a vector's their slots'.
static uint32_t tier0_class_bytes(uint32_t class)
{
    static const uint8_t bytes[BUSTER_TYPED_CLASS_LIMIT] = { 0, 1, 1, 2, 2, 4, 4, 8, 8, 4, 8, 16, 16, 16, 64 };
    return bytes[class & (BUSTER_TYPED_CLASS_LIMIT - 1)];
}

// Whether a vector's bytes are a width Tier 0 has: 16, 32 or 64, an XMM, YMM or ZMM register's.
static bool tier0_vector_bytes_valid(uint64_t bytes)
{
    return bytes == 16 || bytes == 32 || bytes == 64;
}

// ---------------------------------------------------------------------------------------------------------------------
// Aggregates in registers

// A part of an aggregate passed or returned in a register (#137): eightbyte `offset` (0 or 8) and its `bytes` (1 to 8,
// or 16 for a 16-byte aggregate's only part in an XMM register), in `reg` (Tier 0's numbering: a general register, or
// BUSTER_TIER0_XMM's), or in a stack slot (BUSTER_TIER0_PART_STACK: a SPLIT location's other part, or the high eightbyte
// alone on the stack), and how it loads from the aggregate's bytes without reading past them (`load`):
//   - EXACT: one load of its width, a general register's 1, 2, 4 or 8 bytes (zero-extended) or an XMM one's 4, 8 or 16;
//   - OVERLAP: a general register's high part of 3, 5, 6 or 7 bytes, as the 8 bytes that end where it ends, shifted
//     right past the ones before it;
//   - TEMPORARY: the rest (a general register's only part of 3, 5, 6 or 7 bytes, an XMM register's of 1, 2, 3, 5, 6 or
//     7, which only halves make), through an 8-byte temporary in the red zone that its bytes are copied into first.
// A store from a register (a parameter's arrival, a call's result) writes the register's width, 8 bytes or 16, which an
// object's frame space, its size rounded up to 8, has room for.
#define BUSTER_TIER0_PART_EXACT 0
#define BUSTER_TIER0_PART_OVERLAP 1
#define BUSTER_TIER0_PART_TEMPORARY 2
#define BUSTER_TIER0_PART_STACK 0xff
// The red zone's temporaries, 8 bytes each below rsp: a call's argument setup writes and reads them before the call
// instruction's push, and the epilogue's result loads before it returns. A call has at most 14 (a part per argument
// register), so the red zone's 128 bytes hold them.
#define BUSTER_TIER0_TEMPORARIES 16

typedef struct Tier0Part Tier0Part;
struct Tier0Part
{
    uint8_t reg;
    uint8_t offset;
    uint8_t bytes;
    uint8_t load;
};

// An ABI register (BUSTER_ABI_*, a vector register's with BUSTER_ABI_REGISTER_VECTOR) as Tier 0 numbers it.
static uint8_t tier0_abi_register(uint8_t reg)
{
    return (uint8_t)((reg & BUSTER_ABI_REGISTER_VECTOR) != 0 ? BUSTER_TIER0_XMM(reg & 0x1f) : reg);
}

// The parts of an aggregate of `size` bytes in two registers (a location's, or a result's first two), registers[k]
// holding eightbyte k, or with `high` the only part, eightbyte 1, and with `low` the only part, eightbyte 0;
// BUSTER_ABI_REGISTER_NONE is no part, or with `split` (a SPLIT location, or a STACK one of the high or the low eightbyte
// alone) the part in the stack slot. Returns how many, into `parts`.
static uint32_t tier0_parts_of(const uint8_t* registers, bool high, bool low, bool split, uint32_t size, Tier0Part* parts)
{
    uint32_t count = 0;
    for (uint32_t k = 0; k < (high || low ? 1u : 2u); k += 1)
    {
        const uint8_t reg = registers[k];
        if (reg != BUSTER_ABI_REGISTER_NONE || split)
        {
            const uint32_t offset = high ? 8 : 8 * k;
            const bool xmm = reg != BUSTER_ABI_REGISTER_NONE && (reg & BUSTER_ABI_REGISTER_VECTOR) != 0;
            const bool only = !split && registers[1 - k] == BUSTER_ABI_REGISTER_NONE;
            const uint32_t left = size > offset ? size - offset : 0;
            const uint32_t whole = size >= 64 ? 64 : size >= 32 ? 32 : 16;
            const uint32_t bytes = xmm && only && !high && size >= 16 ? whole : left < 8 ? left : 8;
            const bool exact = xmm ? bytes == 4 || bytes == 8 || bytes >= 16 : bytes == 1 || bytes == 2 || bytes == 4 || bytes == 8;
            parts[count] = (Tier0Part){
                .reg = reg != BUSTER_ABI_REGISTER_NONE ? tier0_abi_register(reg) : BUSTER_TIER0_PART_STACK,
                .offset = (uint8_t)offset,
                .bytes = (uint8_t)bytes,
                .load = reg == BUSTER_ABI_REGISTER_NONE || exact ? BUSTER_TIER0_PART_EXACT : !xmm && offset >= 8 ? BUSTER_TIER0_PART_OVERLAP : BUSTER_TIER0_PART_TEMPORARY,
            };
            count += 1;
        }
    }
    return count;
}

// Whether an aggregate at `location` is on the stack whole, its bytes in its slots from its first: STACK, but for the high
// eightbyte alone (BUSTER_ABI_FLAG_HIGH, which abi_assign puts in one slot when LLVM has no register left for it) and the
// low eightbyte alone of a larger value (BUSTER_ABI_FLAG_LOW, in one slot too: a copy of the value would run into the
// next argument's).
static bool tier0_aggregate_whole(AbiLocation location)
{
    return location.kind == BUSTER_ABI_LOCATION_STACK && (location.flags & (BUSTER_ABI_FLAG_HIGH | BUSTER_ABI_FLAG_LOW)) == 0;
}

// The parts of an aggregate argument or parameter of `size` bytes at `location` (none on the stack whole or ignored).
static uint32_t tier0_location_parts(AbiLocation location, uint32_t size, Tier0Part* parts)
{
    const bool high = (location.flags & BUSTER_ABI_FLAG_HIGH) != 0;
    const bool low = location.kind == BUSTER_ABI_LOCATION_STACK && (location.flags & BUSTER_ABI_FLAG_LOW) != 0;
    const bool registers = location.kind == BUSTER_ABI_LOCATION_REGISTERS || location.kind == BUSTER_ABI_LOCATION_SPLIT;
    const bool slot = location.kind == BUSTER_ABI_LOCATION_STACK && (high || low);
    return registers || slot ? tier0_parts_of(location.registers, high, low, location.kind != BUSTER_ABI_LOCATION_REGISTERS, size, parts) : 0;
}

// Whether parameter `p` keeps its incoming stack bytes as its home: a value on the stack, or an aggregate on the stack
// whole at a multiple of its alignment, to 16 (the incoming area starts at a multiple of 16). The prologue copies the
// others' slots into their objects: the high eightbyte alone, two eightbytes that LLVM put in slots of 8 bytes each when
// it had no register left for them, whatever the aggregate's alignment, and an object aligned past 16, whose caller
// aligns its slot only as far as its type's own alignment goes (an aligned typedef passes as its base: AbiCall's
// stack_alignment_log2), into the realigned frame.
static bool tier0_parameter_in_place(const Tier0Function* function, uint32_t p)
{
    const AbiLocation location = function->parameters[p];
    const uint32_t alignment_log2 = tier0_is_object(function, p) ? function->local_alignments[p] : 0;
    const bool aligned = alignment_log2 <= BUSTER_TIER0_FRAME_ALIGNMENT_LOG2 && location.stack_offset % (1u << alignment_log2) == 0;
    return tier0_is_object(function, p) ? tier0_aggregate_whole(location) && aligned : location.kind == BUSTER_ABI_LOCATION_STACK;
}

// The parts of an aggregate result of `size` bytes returned in registers (none otherwise).
static uint32_t tier0_result_parts(AbiReturn result, uint32_t size, Tier0Part* parts)
{
    return result.kind == BUSTER_ABI_RESULT_REGISTERS ? tier0_parts_of(result.registers, (result.flags & BUSTER_ABI_FLAG_HIGH) != 0, false, false, size, parts) : 0;
}

// ABI register `reg`'s bit in a register set when it is one of `allowed` (BUSTER_TIER0_ARGUMENT_REGISTERS or
// BUSTER_TIER0_RESULT_REGISTERS), else 0: none for BUSTER_ABI_REGISTER_NONE, an x87 register or any other.
static uint32_t tier0_register_bit(uint8_t reg, uint32_t allowed)
{
    return (reg & ~BUSTER_ABI_REGISTER_VECTOR) < 16 ? (1u << tier0_abi_register(reg)) & allowed : 0;
}

// Whether two registers hold an aggregate of `size` bytes as eightbytes: each one of `allowed` (the argument registers,
// or the results') or none, and two apart; the first one present (eightbyte 0, or with `high` eightbyte 1 alone), the
// second only past 8 bytes; with `split`, exactly one present (the other part in a stack slot) and 9 to 16 bytes. A
// 16-byte aggregate's only XMM part takes the whole register, and so does a 32- or 64-byte one's YMM or ZMM part (a
// vector's, or a struct of one's), which only an only vector register holds.
static bool tier0_parts_valid(const uint8_t* registers, bool high, bool split, uint32_t size, uint32_t allowed)
{
    const bool first = registers[0] != BUSTER_ABI_REGISTER_NONE;
    const bool second = registers[1] != BUSTER_ABI_REGISTER_NONE;
    const uint32_t first_bit = tier0_register_bit(registers[0], allowed);
    const uint32_t second_bit = tier0_register_bit(registers[1], allowed);
    const bool wide = (size == 32 || size == 64) && first && (registers[0] & BUSTER_ABI_REGISTER_VECTOR) != 0 && !second && !high && !split;
    bool valid = size != 0 && (size <= 16 || wide) && (!first || first_bit != 0) && (!second || second_bit != 0) && (first_bit & second_bit) == 0;
    if (split)
    {
        valid = valid && first != second && !high && size > 8;
    }
    else
    {
        valid = valid && first && (high ? !second && size > 8 : !second || size > 8);
    }
    return valid;
}

// Whether `location` places an aggregate of `size` bytes: ignored, on the stack inside `stack_bytes` (the area its call's
// arguments take, or an incoming one's) whole, or in argument registers (tier0_parts_valid), a SPLIT one's stack part in
// an 8-byte slot, as is the high or the low eightbyte alone on the stack, with no register.
static bool tier0_aggregate_location_valid(AbiLocation location, uint32_t size, uint64_t stack_bytes)
{
    const bool high = (location.flags & BUSTER_ABI_FLAG_HIGH) != 0;
    const bool low = (location.flags & BUSTER_ABI_FLAG_LOW) != 0;
    const bool whole = tier0_aggregate_whole(location);
    const uint64_t slot = whole ? ((uint64_t)size + 7) & ~UINT64_C(7) : BUSTER_ABI_SLOT_BYTES;
    const bool stack = location.kind == BUSTER_ABI_LOCATION_STACK || location.kind == BUSTER_ABI_LOCATION_SPLIT;
    bool valid = location.kind == BUSTER_ABI_LOCATION_IGNORE || stack || location.kind == BUSTER_ABI_LOCATION_REGISTERS;
    valid = valid && (!stack || (location.stack_offset % BUSTER_ABI_SLOT_BYTES == 0 && (uint64_t)location.stack_offset + slot <= stack_bytes));
    if (location.kind == BUSTER_ABI_LOCATION_STACK && (high || low))
    {
        valid = valid && !(high && low) && location.registers[0] == BUSTER_ABI_REGISTER_NONE && location.registers[1] == BUSTER_ABI_REGISTER_NONE &&
                size > 8 && size <= 16;
    }
    else if (location.kind == BUSTER_ABI_LOCATION_REGISTERS || location.kind == BUSTER_ABI_LOCATION_SPLIT)
    {
        valid = valid && tier0_parts_valid(location.registers, high, location.kind == BUSTER_ABI_LOCATION_SPLIT, size, BUSTER_TIER0_ARGUMENT_REGISTERS);
    }
    // LOW is a STACK location's alone.
    valid = valid && (!low || location.kind == BUSTER_ABI_LOCATION_STACK);
    return valid;
}

// How many x87 registers a result comes back in: st0, or st0 and st1 (a COMPLEX_X87 one), each part 16 bytes after the
// one before; 0 for any other result.
static uint32_t tier0_x87_parts(AbiReturn result)
{
    const bool x87 = result.kind == BUSTER_ABI_RESULT_REGISTERS && result.registers[0] == BUSTER_ABI_REGISTER_X87;
    return x87 ? (result.registers[1] == (BUSTER_ABI_REGISTER_X87 | 1) ? 2u : 1u) : 0u;
}

// Whether a result in x87 registers is one the emitters follow: st0, or st0 and st1, 16 bytes apart, for an aggregate
// of up to 16 bytes a part (`size`), or a long double's value (0).
static bool tier0_x87_result_valid(AbiReturn result, uint32_t size)
{
    const uint32_t parts = tier0_x87_parts(result);
    const bool second = result.registers[1] == BUSTER_ABI_REGISTER_NONE || parts == 2;
    return parts != 0 && second && result.part_bytes == 16 && (result.flags & BUSTER_ABI_FLAG_HIGH) == 0 && result.registers[2] == BUSTER_ABI_REGISTER_NONE &&
           result.registers[3] == BUSTER_ABI_REGISTER_NONE && size <= 16 * parts;
}

// Whether `result` returns an aggregate of `size` bytes in registers: as eightbytes (16 bytes at most), in two result
// registers at most, 8-byte parts or a 16-, 32- or 64-byte one in a vector register whole; or in x87 registers
// (tier0_x87_result_valid), a struct of a long double's in st0.
static bool tier0_aggregate_result_valid(AbiReturn result, uint32_t size)
{
    const bool high = (result.flags & BUSTER_ABI_FLAG_HIGH) != 0;
    const bool vector = tier0_vector_bytes_valid(result.part_bytes) && result.registers[1] == BUSTER_ABI_REGISTER_NONE &&
                        (result.registers[0] & BUSTER_ABI_REGISTER_VECTOR) != 0 && !high && size == result.part_bytes;
    const bool eightbytes = result.kind == BUSTER_ABI_RESULT_REGISTERS && (result.part_bytes == 8 || vector) && result.registers[2] == BUSTER_ABI_REGISTER_NONE &&
                            result.registers[3] == BUSTER_ABI_REGISTER_NONE && tier0_parts_valid(result.registers, high, false, size, BUSTER_TIER0_RESULT_REGISTERS);
    return eightbytes || (size != 0 && tier0_x87_result_valid(result, size));
}

// The bytes a call's aggregate result in registers stores into its object (tier0_emit_call): each part's register whole,
// 8 bytes from its eightbyte's offset, or 16, 32 or 64 for a vector's, or 16 for an x87 register's (whose fstp writes 10
// of them).
static uint32_t tier0_result_extent(AbiReturn result)
{
    const bool high = (result.flags & BUSTER_ABI_FLAG_HIGH) != 0;
    const uint32_t x87 = tier0_x87_parts(result);
    return x87 != 0                     ? 16 * x87
           : result.part_bytes >= 16    ? result.part_bytes
           : high || result.registers[1] != BUSTER_ABI_REGISTER_NONE ? 16
                                                                     : 8;
}

// The bytes each register of an aggregate result in registers stores whole (tier0_store_part's): a vector register's
// part's 16, 32 or 64, or an eightbyte's 8.
static uint32_t tier0_result_part_bytes(AbiReturn result)
{
    return result.part_bytes >= 16 ? result.part_bytes : 8;
}

// Whether a call's argument or a parameter at `location`, an aggregate of `size` bytes (0 for a value), is one the
// emitters follow inside `stack_bytes` (its call's stack area, or the incoming one): a value ignored, in an argument
// register or in an 8-byte slot, and an aggregate as tier0_aggregate_location_valid has it. `registers` gets the
// argument registers it takes, as a register set.
static bool tier0_place_valid(AbiLocation location, uint32_t size, uint64_t stack_bytes, uint32_t* registers)
{
    const uint32_t first = tier0_register_bit(location.registers[0], BUSTER_TIER0_ARGUMENT_REGISTERS);
    bool valid = false;
    if (size == 0)
    {
        const bool in_register = location.kind == BUSTER_ABI_LOCATION_REGISTERS;
        const bool slot = location.kind == BUSTER_ABI_LOCATION_STACK && location.stack_offset % BUSTER_ABI_SLOT_BYTES == 0 &&
                          (uint64_t)location.stack_offset + BUSTER_ABI_SLOT_BYTES <= stack_bytes;
        valid = location.kind == BUSTER_ABI_LOCATION_IGNORE || (in_register && first != 0) || slot;
        *registers = in_register ? first : 0;
    }
    else
    {
        const bool parts = location.kind == BUSTER_ABI_LOCATION_REGISTERS || location.kind == BUSTER_ABI_LOCATION_SPLIT;
        valid = tier0_aggregate_location_valid(location, size, stack_bytes);
        *registers = parts ? first | tier0_register_bit(location.registers[1], BUSTER_TIER0_ARGUMENT_REGISTERS) : 0;
    }
    return valid;
}

// Call `c`'s locations and its aggregates' metadata (argument_sizes and call_result_objects), so that no emitter reads or
// writes out of place: every argument where tier0_place_valid has it in the call's stack area, no register two arguments
// take, and an aggregate of 64 KB at most; a result object an object with room for what the call stores, its hidden
// pointer's rdi no argument's; and no hidden pointer without an object, which would point nowhere.
static bool tier0_call_valid(const Tier0Function* function, uint32_t c)
{
    const uint32_t first = function->call_first[c];
    const uint32_t count = function->call_argument_counts[c];
    const AbiReturn result = function->calls[c].result;
    const uint32_t object = function->call_result_objects != 0 ? function->call_result_objects[c] : 0;
    const uint64_t stack_bytes = function->calls[c].stack_bytes;
    // The registers the call's arguments take, as a register set (BUSTER_TIER0_XMM's). The outgoing area starts at rsp, a
    // multiple of the frame's alignment at the call: 16, or the most a call's area needs past it, to which the frame is
    // realigned (tier0_frame_alignment).
    uint32_t taken = 0;
    bool valid = (uint64_t)first + count <= function->argument_count && function->calls[c].stack_alignment_log2 <= BUSTER_TIER0_MAX_OBJECT_ALIGNMENT_LOG2;
    for (uint32_t a = 0; valid && a < count; a += 1)
    {
        const uint32_t size = function->argument_sizes != 0 ? function->argument_sizes[first + a] : 0;
        uint32_t registers = 0;
        valid = size <= BUSTER_TIER0_MAX_ARGUMENT_BYTES && tier0_place_valid(function->argument_locations[first + a], size, stack_bytes, &registers) &&
                (taken & registers) == 0;
        taken |= registers;
    }
    if (object != 0)
    {
        const uint32_t local = object - 1;
        valid = valid && local < function->local_count && tier0_is_object(function, local);
        if (valid && result.kind == BUSTER_ABI_RESULT_REGISTERS)
        {
            const uint32_t extent = tier0_result_extent(result);
            valid = tier0_aggregate_result_valid(result, extent) && (((uint64_t)function->local_sizes[local] + 7) & ~UINT64_C(7)) >= extent;
        }
        else
        {
            valid = valid && (result.kind == BUSTER_ABI_RESULT_MEMORY || result.kind == BUSTER_ABI_RESULT_IGNORE);
            valid = valid && (result.kind != BUSTER_ABI_RESULT_MEMORY || (taken >> X86_REGISTER_RDI & 1) == 0);
        }
    }
    else
    {
        valid = valid && result.kind != BUSTER_ABI_RESULT_MEMORY;
    }
    return valid;
}

// Whether every call is as tier0_call_valid has it: tier0_validate's check of the calls, a function's without aggregates
// among them, whose calls tier0_aggregates_valid leaves to it.
static bool tier0_calls_valid(const Tier0Function* function)
{
    bool valid = true;
    for (uint32_t c = 0; valid && c < function->call_count; c += 1)
    {
        valid = tier0_call_valid(function, c);
    }
    return valid;
}

// The parameters' locations, as tier0_place_valid has them inside the incoming area (a variadic function's, else all of
// int32), each object's aligned to 2^30 at most, and no register two parameters take: each arrives in its own, which the
// prologue reads it from or leaves it in as its home. tier0_validate's check of the parameters, and the compile paths'
// where an optional part bounds them (incoming_variadic) or may make objects of them (local_sizes).
static bool tier0_parameters_valid(const Tier0Function* function)
{
    const uint64_t incoming = function->incoming_variadic != 0 ? function->incoming_variadic->stack_bytes : INT32_MAX;
    uint32_t taken = 0;
    bool valid = true;
    for (uint32_t p = 0; valid && p < function->parameter_count; p += 1)
    {
        const bool object = tier0_is_object(function, p);
        uint32_t registers = 0;
        valid = (!object || (function->local_alignments != 0 && function->local_alignments[p] <= BUSTER_TIER0_MAX_OBJECT_ALIGNMENT_LOG2)) &&
                tier0_place_valid(function->parameters[p], object ? function->local_sizes[p] : 0, incoming, &registers) && (taken & registers) == 0;
        taken |= registers;
    }
    return valid;
}

// The aggregates' metadata (argument_sizes, call_result_objects and result_size; the parameters that are objects
// tier0_parameters_valid checks), checked by both compile paths too, so that no emitter reads or writes out of place:
// each call of a function that passes or receives aggregates (tier0_call_valid; a function without argument_sizes and
// call_result_objects has its typed stream's calls, which tier0_validate checks as the compile paths' precondition, at no
// cost to them), and a function whose result goes through a hidden pointer taking it as parameter 0 (U64, in rdi).
static bool tier0_aggregates_valid(const Tier0Function* function)
{
    bool valid = (function->argument_sizes == 0 && function->call_result_objects == 0) || tier0_calls_valid(function);
    if (function->result_size != 0)
    {
        valid = valid && tier0_aggregate_result_valid(function->result, function->result_size);
    }
    else if (function->result.kind == BUSTER_ABI_RESULT_MEMORY)
    {
        const AbiLocation hidden = function->parameter_count != 0 ? function->parameters[0] : (AbiLocation){ 0 };
        valid = valid && function->parameter_count != 0 && !tier0_is_object(function, 0) && function->local_classes[0] == BUSTER_TYPED_CLASS_U64 &&
                hidden.kind == BUSTER_ABI_LOCATION_REGISTERS && hidden.registers[0] == BUSTER_ABI_RDI;
    }
    return valid;
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
// a register (`rm`), at a register plus K, or at a register plus another (`index`) times 2^`scale`; RIP-relative, a
// symbolic displacement (the entry's SymbolId, through a fixup); or an absolute 32-bit address, the spec's displacement
// (a thread-local's offset through a fixup, or fs:[0]).
#define BUSTER_TIER0_RM_NONE 0
#define BUSTER_TIER0_RM_REGISTER 1
#define BUSTER_TIER0_RM_HOME_A 2
#define BUSTER_TIER0_RM_HOME_B 3
#define BUSTER_TIER0_RM_BASE 4
#define BUSTER_TIER0_RM_BASE_K 5
#define BUSTER_TIER0_RM_BASE_INDEX 6
#define BUSTER_TIER0_RM_RIP 7
#define BUSTER_TIER0_RM_ABSOLUTE 8

// What the immediate is: none, the spec's own (with its displacement for a 64-bit one), or K's low 32 bits.
#define BUSTER_TIER0_IMMEDIATE_NONE 0
#define BUSTER_TIER0_IMMEDIATE_STATIC 1
#define BUSTER_TIER0_IMMEDIATE_K 2

// A record's role besides its fields: a branch to the entry's target (its value) or to the function's end (a return's
// jump to the epilogue); a symbolic field (the entry's SymbolId through a fixup of `fixup_kind`, PC32 when 0, in the
// field `fixup_field`); an immediate whose form the choice pass makes the shortest that keeps it (with `live`, the status
// flags the template reads after the record).
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
    // A FIXUP record's BUSTER_LINK_FIXUP_* (0 for PC32) and X86FixupField.
    uint8_t fixup_kind;
    uint8_t fixup_field;
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
#define BUSTER_TIER0_SLOT_CAPACITY (512 * BUSTER_TIER0_VARIANTS)
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
// branch record (0xff: none) and whether it goes to the function's end, its symbolic record (0xff: none) with its fixup's
// kind and field, its choice record (0xff: none) with its row, live flags and immediate, its holes' offsets, its required
// depth, its slot's reach (the most of a home-dependent slot's cases', for the frame's touches, read before the homes are
// known) and its fixed clobbers. Every template has at most one record of each role (tier0_end checks).
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
    // The symbolic record's BUSTER_LINK_FIXUP_* and X86FixupField.
    uint8_t fixup_kind;
    uint8_t fixup_field;
    // How far below the entry's depth its registers read on each stack (2 for R2, 1 for R1, 0 for neither; the floating
    // stack's from bit 2), the most of a home-dependent slot's cases', as its reach: a deep entry's window
    // (tier0_select_deep).
    uint8_t window;
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

// mov reg, [rip + the symbol's GOT slot]: its address (`kind` LOAD_GOT) or a thread-local's offset (GOTTPOFF), whose
// instruction is the mov r64 the linkers rewrite.
static Tier0RecordSpec tier0_rslot(uint8_t reg, uint8_t kind)
{
    Tier0RecordSpec spec = tier0_rrip(X86_FORM_MOV_R64_RM64, reg);
    spec.fixup_kind = kind;
    return spec;
}

// form reg, fs:[the symbol's offset from the thread pointer] (local-exec: a TPOFF32 in the absolute address).
static Tier0RecordSpec tier0_rtls(uint32_t form, uint8_t reg)
{
    Tier0RecordSpec spec = tier0_spec(form + BUSTER_X86_FORM_FS);
    spec.reg = reg;
    spec.rm_kind = BUSTER_TIER0_RM_ABSOLUTE;
    spec.flags = BUSTER_TIER0_RECORD_FIXUP;
    spec.fixup_kind = BUSTER_LINK_FIXUP_TPOFF32;
    return spec;
}

// form reg, fs:[0] (the thread pointer, at its own address) or fs:[base]: a thread-local's address or its bytes.
static Tier0RecordSpec tier0_rfs(uint32_t form, uint8_t reg, uint8_t rm_kind, uint8_t base)
{
    Tier0RecordSpec spec = tier0_rmem(form + BUSTER_X86_FORM_FS, reg, rm_kind, base, 0, 0);
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

// How far below the depth a source reads on its stack (BUSTER_TIER0_SOURCE_R2 and R1, X2 and X1), as
// Tier0TemplateInfo.window keeps it: the integer stack's in bits 0 and 1, the floating stack's in bits 2 and 3.
static uint32_t tier0_source_window(uint8_t source)
{
    uint32_t window = 0;
    if (source == BUSTER_TIER0_SOURCE_R2 || source == BUSTER_TIER0_SOURCE_R1)
    {
        window = source == BUSTER_TIER0_SOURCE_R2 ? 2 : 1;
    }
    else if (source == BUSTER_TIER0_SOURCE_X2 || source == BUSTER_TIER0_SOURCE_X1)
    {
        window = (source == BUSTER_TIER0_SOURCE_X2 ? 2u : 1u) << 2;
    }
    return window;
}

// The deeper of two windows, stack by stack.
static uint8_t tier0_window_max(uint32_t a, uint32_t b)
{
    const uint32_t integer = (a & 3) > (b & 3) ? a & 3 : b & 3;
    const uint32_t floating = (a >> 2 & 3) > (b >> 2 & 3) ? a >> 2 & 3 : b >> 2 & 3;
    return (uint8_t)(integer | floating << 2);
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
        else if (spec->rm_kind == BUSTER_TIER0_RM_ABSOLUTE)
        {
            fixed.mode = BUSTER_X86_MEMORY_ABSOLUTE;
            fixed.displacement = spec->displacement;
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
        for (uint32_t r = 0; r < template->length; r += 1)
        {
            const Tier0RecordSpec* spec = &template->specs[r];
            const bool rm = spec->rm_kind == BUSTER_TIER0_RM_REGISTER || spec->rm_kind == BUSTER_TIER0_RM_BASE || spec->rm_kind == BUSTER_TIER0_RM_BASE_K ||
                            spec->rm_kind == BUSTER_TIER0_RM_BASE_INDEX;
            info.window = tier0_window_max(info.window, tier0_source_window(spec->reg));
            info.window = tier0_window_max(info.window, rm ? tier0_source_window(spec->rm) : 0);
            info.window = tier0_window_max(info.window, spec->rm_kind == BUSTER_TIER0_RM_BASE_INDEX ? tier0_source_window(spec->index) : 0);
        }
        // A template for one depth reads the values below it through its fixed registers too (division's dividend in rax).
        info.window = tier0_window_max(info.window, template->required_depth != BUSTER_TIER0_ANY_DEPTH ? template->required_depth : 0);
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
                info.fixup_kind = spec->fixup_kind != 0 ? spec->fixup_kind : BUSTER_LINK_FIXUP_PC32;
                info.fixup_field = spec->fixup_field;
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
// as records; a conversion that keeps the low bits needs none. Returns false for one between U64 and a floating class,
// which has a deferred emitter of its own (tier0_emit_u64_convert).
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
    // Leaves and their copies, by class (long double's are at the end).
    for (uint32_t class = BUSTER_TYPED_CLASS_I8; class <= BUSTER_TYPED_CLASS_F64; class += 1)
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
        // GLOBAL, STORE_GLOBAL and SET_GLOBAL of a symbol the access column reaches otherwise: through its GOT slot, its
        // address into R0 and the access through it; a thread-local of the image's at fs:[its offset]; and one through
        // its GOT slot, its offset into R0 and the access at fs:[R0]. R0 is free above the operand, as it is beside a
        // floating one. They take no fused local or constant, whose value would need a register besides R0. A constant
        // stored through the GOT as an immediate would, but its properties would be a row past the vector fusion's
        // sixteen (tier0_build_vector_tables), which every function's lookups would pay for.
        tier0_begin();
        tier0_add(tier0_rslot(BUSTER_TIER0_SOURCE_R0, BUSTER_LINK_FIXUP_LOAD_GOT));
        tier0_add(tier0_rmem(tier0_value_load_form(class), top, BUSTER_TIER0_RM_BASE, BUSTER_TIER0_SOURCE_R0, 0, 0));
        tier0_end(BUSTER_TIER0_KIND_GOT, class, BUSTER_TIER0_VARIANT_PLAIN, 0);
        tier0_begin();
        tier0_add(tier0_rtls(tier0_value_load_form(class), top));
        tier0_end(BUSTER_TIER0_KIND_LOCAL_EXEC, class, BUSTER_TIER0_VARIANT_PLAIN, 0);
        tier0_begin();
        tier0_add(tier0_rslot(BUSTER_TIER0_SOURCE_R0, BUSTER_LINK_FIXUP_GOTTPOFF));
        tier0_add(tier0_rfs(tier0_value_load_form(class), top, BUSTER_TIER0_RM_BASE, BUSTER_TIER0_SOURCE_R0));
        tier0_end(BUSTER_TIER0_KIND_INITIAL_EXEC, class, BUSTER_TIER0_VARIANT_PLAIN, 0);
        for (uint32_t store = 2; store < 4; store += 1)
        {
            tier0_begin();
            tier0_add(tier0_rslot(BUSTER_TIER0_SOURCE_R0, BUSTER_LINK_FIXUP_LOAD_GOT));
            tier0_add(tier0_rmem(tier0_store_form(class), last, BUSTER_TIER0_RM_BASE, BUSTER_TIER0_SOURCE_R0, 0, 0));
            tier0_end(BUSTER_TIER0_KIND_GOT + store, class, BUSTER_TIER0_VARIANT_PLAIN, 0);
            tier0_begin();
            tier0_add(tier0_rtls(tier0_store_form(class), last));
            tier0_end(BUSTER_TIER0_KIND_LOCAL_EXEC + store, class, BUSTER_TIER0_VARIANT_PLAIN, 0);
            tier0_begin();
            tier0_add(tier0_rslot(BUSTER_TIER0_SOURCE_R0, BUSTER_LINK_FIXUP_GOTTPOFF));
            tier0_add(tier0_rfs(tier0_store_form(class), last, BUSTER_TIER0_RM_BASE, BUSTER_TIER0_SOURCE_R0));
            tier0_end(BUSTER_TIER0_KIND_INITIAL_EXEC + store, class, BUSTER_TIER0_VARIANT_PLAIN, 0);
        }
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
        // Conversions to this class from every class but long double.
        for (uint32_t from = BUSTER_TYPED_CLASS_I8; from <= BUSTER_TYPED_CLASS_F64; from += 1)
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
    // A symbol's address from its GOT slot; a thread-local's, the thread pointer (which fs:[0] holds) plus its offset, as
    // a TPOFF32 immediate or from its GOT slot.
    tier0_begin();
    tier0_add(tier0_rslot(BUSTER_TIER0_SOURCE_R0, BUSTER_LINK_FIXUP_LOAD_GOT));
    tier0_end(BUSTER_TIER0_KIND_GOT + 1, BUSTER_TYPED_CLASS_U64, BUSTER_TIER0_VARIANT_PLAIN, 0);
    tier0_begin();
    tier0_add(tier0_rfs(X86_FORM_MOV_R64_RM64, BUSTER_TIER0_SOURCE_R0, BUSTER_TIER0_RM_ABSOLUTE, BUSTER_TIER0_SOURCE_NONE));
    Tier0RecordSpec offset = tier0_with_immediate(tier0_r(X86_FORM_ADD_RM64_IMM32, BUSTER_TIER0_SOURCE_R0), 0, 0);
    offset.flags |= BUSTER_TIER0_RECORD_FIXUP;
    offset.fixup_kind = BUSTER_LINK_FIXUP_TPOFF32;
    offset.fixup_field = X86_FIXUP_IMMEDIATE;
    tier0_add(offset);
    tier0_end(BUSTER_TIER0_KIND_LOCAL_EXEC + 1, BUSTER_TYPED_CLASS_U64, BUSTER_TIER0_VARIANT_PLAIN, 0);
    tier0_begin();
    tier0_add(tier0_rslot(BUSTER_TIER0_SOURCE_R0, BUSTER_LINK_FIXUP_GOTTPOFF));
    tier0_add(tier0_rfs(X86_FORM_ADD_R64_RM64, BUSTER_TIER0_SOURCE_R0, BUSTER_TIER0_RM_ABSOLUTE, BUSTER_TIER0_SOURCE_NONE));
    tier0_end(BUSTER_TIER0_KIND_INITIAL_EXEC + 1, BUSTER_TYPED_CLASS_U64, BUSTER_TIER0_VARIANT_PLAIN, 0);
    // JUMP.
    tier0_begin();
    tier0_add(tier0_branch(X86_FORM_JMP_REL8, BUSTER_TIER0_RECORD_BRANCH));
    tier0_end(BUSTER_TYPED_JUMP, BUSTER_TYPED_CLASS_VOID, BUSTER_TIER0_VARIANT_PLAIN, 0);
    // JUMP_INDIRECT: to the address in the operand's register, without a fused operand, which would make it a jump through
    // memory.
    tier0_begin();
    tier0_add(tier0_r(X86_FORM_JMP_RM64, BUSTER_TIER0_SOURCE_R1));
    tier0_end(BUSTER_TYPED_JUMP_INDIRECT, BUSTER_TYPED_CLASS_U64, BUSTER_TIER0_VARIANT_PLAIN, 0);
    // RETURN of nothing.
    tier0_begin();
    tier0_add(tier0_branch(X86_FORM_JMP_REL8, BUSTER_TIER0_RECORD_END));
    tier0_end(BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, BUSTER_TIER0_VARIANT_PLAIN, 0);
    tier0_begin();
    tier0_end(BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, BUSTER_TIER0_VARIANT_LAST, 0);
    tier0_defer(BUSTER_TYPED_CALL, BUSTER_TYPED_CLASS_VOID, BUSTER_TIER0_VARIANT_PLAIN);
    // COPY: tier0_emit_deferred's, its moves by its size.
    tier0_defer(BUSTER_TYPED_COPY, BUSTER_TYPED_CLASS_VOID, BUSTER_TIER0_VARIANT_PLAIN);
    // ALLOCA, STACK_SAVE, STACK_RESTORE and FRAME_ADDRESS: tier0_emit_deferred's, which read the frame's outgoing area.
    tier0_defer(BUSTER_TYPED_ALLOCA, BUSTER_TYPED_CLASS_U64, BUSTER_TIER0_VARIANT_PLAIN);
    tier0_defer(BUSTER_TYPED_STACK_SAVE, BUSTER_TYPED_CLASS_U64, BUSTER_TIER0_VARIANT_PLAIN);
    tier0_defer(BUSTER_TYPED_STACK_RESTORE, BUSTER_TYPED_CLASS_U64, BUSTER_TIER0_VARIANT_PLAIN);
    tier0_defer(BUSTER_TYPED_FRAME_ADDRESS, BUSTER_TYPED_CLASS_U64, BUSTER_TIER0_VARIANT_PLAIN);
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
    // Conversions between U64 and the floating classes: tier0_emit_u64_convert's, from the operand's register alone (no
    // home's variant, so that no local fuses into one).
    tier0_defer(BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_F32, BUSTER_TYPED_CLASS_U64);
    tier0_defer(BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_F64, BUSTER_TYPED_CLASS_U64);
    tier0_defer(BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_F32);
    tier0_defer(BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_F64);
    // Long double: every entry tier0_emit_x87's, its values in their depths' slots, but those that make no records of
    // their own: DISCARD (nothing), JUMP_VALUE (the value stays in its slot, which the target's depth names too) and
    // RETURN (the epilogue loads st0 from slot 0). Each is its key's plain variant alone, so that no entry fuses into
    // one and the vector fusion's property rows stay as they are.
    static const uint8_t x87_kinds[] = {
        BUSTER_TYPED_CONST, BUSTER_TYPED_DUP, BUSTER_TYPED_NEG, BUSTER_TYPED_LOAD, BUSTER_TYPED_STORE, BUSTER_TYPED_SET, BUSTER_TYPED_ADD,
        BUSTER_TYPED_SUB,   BUSTER_TYPED_MUL, BUSTER_TYPED_DIV, BUSTER_TYPED_EQ,   BUSTER_TYPED_NE,    BUSTER_TYPED_LT,  BUSTER_TYPED_LE,
        BUSTER_TYPED_GT,    BUSTER_TYPED_GE,  BUSTER_TYPED_CALL,
        BUSTER_TYPED_LOCAL, BUSTER_TYPED_STORE_LOCAL, BUSTER_TYPED_SET_LOCAL,
    };
    for (uint32_t k = 0; k < BUSTER_ARRAY_LENGTH(x87_kinds); k += 1)
    {
        tier0_defer(x87_kinds[k], BUSTER_TYPED_CLASS_F80, BUSTER_TIER0_VARIANT_PLAIN);
    }
    for (uint32_t other = BUSTER_TYPED_CLASS_I8; other <= BUSTER_TYPED_CLASS_F80; other += 1)
    {
        tier0_defer(BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_F80, other);
        if (other != BUSTER_TYPED_CLASS_F80)
        {
            tier0_defer(BUSTER_TYPED_CONVERT, other, BUSTER_TYPED_CLASS_F80);
        }
    }
    tier0_begin();
    tier0_end(BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_F80, BUSTER_TIER0_VARIANT_PLAIN, 0);
    tier0_begin();
    tier0_add(tier0_branch(X86_FORM_JMP_REL8, BUSTER_TIER0_RECORD_BRANCH));
    tier0_end(BUSTER_TYPED_JUMP_VALUE, BUSTER_TYPED_CLASS_F80, BUSTER_TIER0_VARIANT_PLAIN, 0);
    tier0_begin();
    tier0_add(tier0_branch(X86_FORM_JMP_REL8, BUSTER_TIER0_RECORD_END));
    tier0_end(BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_F80, BUSTER_TIER0_VARIANT_PLAIN, 0);
    tier0_begin();
    tier0_end(BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_F80, BUSTER_TIER0_VARIANT_LAST, 0);
    // The 128-bit integers: every entry tier0_emit_int128's, as long double's are, but DISCARD and JUMP_VALUE, whose
    // values stay in their slots. Each is its key's plain variant alone: no constant or local fuses into one.
    static const uint8_t int128_kinds[] = {
        BUSTER_TYPED_CONST, BUSTER_TYPED_DUP, BUSTER_TYPED_NEG, BUSTER_TYPED_NOT, BUSTER_TYPED_LOAD, BUSTER_TYPED_STORE, BUSTER_TYPED_SET,
        BUSTER_TYPED_ADD,   BUSTER_TYPED_SUB, BUSTER_TYPED_MUL, BUSTER_TYPED_AND, BUSTER_TYPED_OR,   BUSTER_TYPED_XOR,   BUSTER_TYPED_SHL,
        BUSTER_TYPED_SHR,   BUSTER_TYPED_EQ,  BUSTER_TYPED_NE,  BUSTER_TYPED_LT,  BUSTER_TYPED_LE,   BUSTER_TYPED_GT,    BUSTER_TYPED_GE,
        BUSTER_TYPED_LOCAL, BUSTER_TYPED_STORE_LOCAL, BUSTER_TYPED_SET_LOCAL,
    };
    for (uint32_t class = BUSTER_TYPED_CLASS_I128; class <= BUSTER_TYPED_CLASS_U128; class += 1)
    {
        for (uint32_t k = 0; k < BUSTER_ARRAY_LENGTH(int128_kinds); k += 1)
        {
            tier0_defer(int128_kinds[k], class, BUSTER_TIER0_VARIANT_PLAIN);
        }
        for (uint32_t other = BUSTER_TYPED_CLASS_I8; other <= BUSTER_TYPED_CLASS_U64; other += 1)
        {
            tier0_defer(BUSTER_TYPED_CONVERT, class, other);
            tier0_defer(BUSTER_TYPED_CONVERT, other, class);
        }
        tier0_defer(BUSTER_TYPED_CONVERT, class, BUSTER_TYPED_CLASS_I128);
        tier0_defer(BUSTER_TYPED_CONVERT, class, BUSTER_TYPED_CLASS_U128);
        tier0_begin();
        tier0_end(BUSTER_TYPED_DISCARD, class, BUSTER_TIER0_VARIANT_PLAIN, 0);
        tier0_begin();
        tier0_add(tier0_branch(X86_FORM_JMP_REL8, BUSTER_TIER0_RECORD_BRANCH));
        tier0_end(BUSTER_TYPED_JUMP_VALUE, class, BUSTER_TIER0_VARIANT_PLAIN, 0);
    }
    // Vectors: every entry tier0_emit_vector's, as the memory classes' are, but DISCARD and JUMP_VALUE, whose values stay
    // in their slots; and the machine operations of every result class. Each is its key's plain variant alone: no
    // constant, local or address fuses into one.
    static const uint8_t vector_kinds[] = {
        BUSTER_TYPED_CONST, BUSTER_TYPED_DUP, BUSTER_TYPED_LOAD, BUSTER_TYPED_STORE, BUSTER_TYPED_SET, BUSTER_TYPED_LOCAL, BUSTER_TYPED_STORE_LOCAL,
        BUSTER_TYPED_SET_LOCAL,
    };
    for (uint32_t k = 0; k < BUSTER_ARRAY_LENGTH(vector_kinds); k += 1)
    {
        tier0_defer(vector_kinds[k], BUSTER_TYPED_CLASS_VECTOR, BUSTER_TIER0_VARIANT_PLAIN);
    }
    tier0_begin();
    tier0_end(BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_VECTOR, BUSTER_TIER0_VARIANT_PLAIN, 0);
    tier0_begin();
    tier0_add(tier0_branch(X86_FORM_JMP_REL8, BUSTER_TIER0_RECORD_BRANCH));
    tier0_end(BUSTER_TYPED_JUMP_VALUE, BUSTER_TYPED_CLASS_VECTOR, BUSTER_TIER0_VARIANT_PLAIN, 0);
    for (uint32_t class = BUSTER_TYPED_CLASS_VOID; class <= BUSTER_TYPED_CLASS_VECTOR; class += 1)
    {
        if (!tier0_class_memory(class))
        {
            tier0_defer(BUSTER_TYPED_X86, class, BUSTER_TIER0_VARIANT_PLAIN);
        }
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
                    first->window = tier0_window_max(first->window, other->window);
                    first->clobbers |= other->clobbers;
                }
            }
        }
        // A realigned frame's base is the callee-saved registers' last (BUSTER_TIER0_SAVED_R15) and the integer depth
        // table's before rbp, so that the depths it takes are the table's last two (tier0_integer_depth_count).
        const bool base = tier0_callee_saved[BUSTER_TIER0_SAVED_R15] == X86_REGISTER_R15 && tier0_integer_depths[BUSTER_TIER0_INTEGER_DEPTHS - 2] == X86_REGISTER_R15;
        tier0_initialized = tier0_builder.valid && base && tier0_build_vector_tables() && tier0_build_words() && tier0_build_choice_shapes();
    }
    return tier0_initialized;
}

// ---------------------------------------------------------------------------------------------------------------------
// Fusion and depths

// What an entry pops and pushes, by kind: its first operand's stack (none, its own class's, its last operand's class's,
// or the integer stack: an address), whether it has a last operand (the entry before it, of that entry's class), and
// what it pushes (none, a value of its class, or an integer). CALL's are its arguments' and result's, and X86's its
// row's operands' and result's (tier0_operation_effects).
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
    [BUSTER_TYPED_ALLOCA] = BUSTER_TIER0_UNARY,
    [BUSTER_TYPED_STACK_SAVE] = BUSTER_TIER0_LEAF,
    [BUSTER_TYPED_STACK_RESTORE] = BUSTER_TIER0_SINK,
    [BUSTER_TYPED_JUMP_INDIRECT] = BUSTER_TIER0_SINK,
    [BUSTER_TYPED_X86] = { BUSTER_TIER0_STACK_NONE, BUSTER_TIER0_STACK_NONE, BUSTER_TIER0_STACK_CLASS },
    [BUSTER_TYPED_FRAME_ADDRESS] = BUSTER_TIER0_LEAF,
    [BUSTER_TIER0_KIND_GOT] = BUSTER_TIER0_LEAF,
    [BUSTER_TIER0_KIND_GOT + 1] = BUSTER_TIER0_LEAF,
    [BUSTER_TIER0_KIND_GOT + 2] = BUSTER_TIER0_UNARY,
    [BUSTER_TIER0_KIND_GOT + 3] = BUSTER_TIER0_SINK,
    [BUSTER_TIER0_KIND_LOCAL_EXEC] = BUSTER_TIER0_LEAF,
    [BUSTER_TIER0_KIND_LOCAL_EXEC + 1] = BUSTER_TIER0_LEAF,
    [BUSTER_TIER0_KIND_LOCAL_EXEC + 2] = BUSTER_TIER0_UNARY,
    [BUSTER_TIER0_KIND_LOCAL_EXEC + 3] = BUSTER_TIER0_SINK,
    [BUSTER_TIER0_KIND_INITIAL_EXEC] = BUSTER_TIER0_LEAF,
    [BUSTER_TIER0_KIND_INITIAL_EXEC + 1] = BUSTER_TIER0_LEAF,
    [BUSTER_TIER0_KIND_INITIAL_EXEC + 2] = BUSTER_TIER0_UNARY,
    [BUSTER_TIER0_KIND_INITIAL_EXEC + 3] = BUSTER_TIER0_SINK,
};

// Whether a kind names a symbol (GLOBAL, GLOBAL_ADDRESS, STORE_GLOBAL and SET_GLOBAL), which the access column reaches.
static bool tier0_global_kind(uint32_t kind)
{
    return kind == BUSTER_TYPED_GLOBAL || kind == BUSTER_TYPED_GLOBAL_ADDRESS || kind == BUSTER_TYPED_STORE_GLOBAL || kind == BUSTER_TYPED_SET_GLOBAL;
}

// The kind a global kind is keyed by for a symbol of access `access` (BUSTER_TIER0_ACCESS_*): for every access but
// DIRECT, its own, from BUSTER_TIER0_KIND_GOT and the others in GLOBAL, GLOBAL_ADDRESS, STORE_GLOBAL and SET_GLOBAL's
// order. GLOBAL and GLOBAL_ADDRESS are 4 and 5, STORE_GLOBAL and SET_GLOBAL 32 and 33: the order is the low bit and the
// fifth.
static uint32_t tier0_kind_by_access(uint32_t kind, uint32_t access)
{
    static const uint8_t bases[BUSTER_TIER0_ACCESSES] = { 0, BUSTER_TIER0_KIND_GOT, BUSTER_TIER0_KIND_LOCAL_EXEC, BUSTER_TIER0_KIND_INITIAL_EXEC };
    return tier0_global_kind(kind) && access != BUSTER_TIER0_ACCESS_DIRECT ? bases[access] + (kind & 1) + (kind >> 4 & 2) : kind;
}
_Static_assert(BUSTER_TYPED_GLOBAL == 4 && BUSTER_TYPED_GLOBAL_ADDRESS == 5 && BUSTER_TYPED_STORE_GLOBAL == 32 && BUSTER_TYPED_SET_GLOBAL == 33,
               "tier0_kind_by_access's order of the global kinds");

// The kind fusion keys an entry of kind `kind` and value `symbol` by before its fusions: a global kind's for its
// symbol's access (tier0_kind_by_access), the entry's kind otherwise. `access` and `count` are the function's column and
// its symbols, read once per function (the fusion's byte stores may alias the function's fields under
// -fno-strict-aliasing, so reads through the function would load them again for every entry), or 0 and 0 in the
// fusion's copy for functions without a column, where the lookup folds away. A symbol past the column is direct, as
// tier0_validate requires none to be.
static uint32_t tier0_access_kind(uint32_t kind, uint64_t symbol, const uint8_t* access, uint32_t count)
{
    const bool reached = count != 0 && tier0_global_kind(kind) && symbol < count;
    return reached ? tier0_kind_by_access(kind, access[symbol] & (BUSTER_TIER0_ACCESSES - 1)) : kind;
}

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
    // Modulo-byte deltas: a wide CALL can pop more than a signed byte represents.
    int8_t* integer_effects;
    int8_t* floating_effects;
    uint8_t* depths;
    uint8_t* floating_depths;
    uint16_t* templates;
    uint32_t* lengths;
    uint32_t* positions;
    // Full measured count before narrowing positions; output allocation refuses an unrepresentable total.
    uint64_t record_count;
    // The vector path's per-entry words (BUSTER_TIER0_WORD_*).
    uint64_t* words;
    // The deep entries (tier0_select_deep), 0 in a function without them: each entry's BUSTER_TIER0_DEEP_* flags, and a
    // deep entry's depths before it, where `depths` and `floating_depths` hold the ones its records are made at.
    uint8_t* deep;
    uint8_t* deep_depths;
    uint8_t* deep_floating_depths;
};

// A deep entry's flags (Tier0Entries.deep, tier0_select_deep's): the stacks whose values it reads from their deep slots
// into the stack's first registers (INTEGER, FLOATING), or a call that reads its arguments from their slots (CALL); the
// stacks whose registers it stores into their slots first (SPILL_*), their values being in registers before it, and those
// it loads back from them last (FILL_*), their values being in registers after it.
#define BUSTER_TIER0_DEEP_INTEGER 0x01
#define BUSTER_TIER0_DEEP_FLOATING 0x02
#define BUSTER_TIER0_DEEP_CALL 0x04
#define BUSTER_TIER0_DEEP_SPILL_INTEGER 0x08
#define BUSTER_TIER0_DEEP_SPILL_FLOATING 0x10
#define BUSTER_TIER0_DEEP_FILL_INTEGER 0x20
#define BUSTER_TIER0_DEEP_FILL_FLOATING 0x40

static bool tier0_label(const uint64_t* labels, uint32_t entry)
{
    return ((labels[entry >> 6] >> (entry & 63)) & 1) != 0;
}

// The labels JUMP_INDIRECT may go to (Tier0Function.label_entries), a bit each, into `labels`.
static void tier0_label_entries(const Tier0Function* function, uint64_t* labels)
{
    for (uint32_t k = 0; k < function->label_count; k += 1)
    {
        const uint32_t target = function->label_entries[k];
        labels[target >> 6] |= 1ull << (target & 63);
    }
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
    tier0_label_entries(function, labels);
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

// Whether entry i takes a last operand whose class both paths read from the entry before it (its stack in
// tier0_raw_effects, and the entry's template key and variant): a kind whose shape has one, but RETURN of nothing. A
// call reads its own arguments' classes.
static bool tier0_takes_last(const Tier0Function* function, uint32_t i)
{
    const uint8_t kind = function->kinds[i];
    return tier0_shapes[kind].last == BUSTER_TIER0_STACK_OPERAND && !(kind == BUSTER_TYPED_RETURN && function->classes[i] == BUSTER_TYPED_CLASS_VOID);
}

// Phase 1, an entry at a time: each consumer takes the entries its windows allow (they are all behind it, and no two
// consumers want the same one), and gets its key. The vector path computes the same columns with masks. `access` and
// `access_count` are the function's access column and its symbols (tier0_access_kind), or 0 and 0 without one.
static inline __attribute__((always_inline)) void tier0_fuse_entries(const Tier0Function* function, const uint64_t* labels, Tier0Entries* entries,
                                                                    const uint8_t* access, uint32_t access_count)
{
    const uint8_t* kinds = function->kinds;
    const uint8_t* classes = function->classes;
    const uint64_t* values = function->values;
    for (uint32_t i = 0; i < function->count; i += 1)
    {
        const uint8_t kind = kinds[i];
        const uint8_t class = classes[i];
        const uint8_t operand = i > 0 ? classes[i - 1] : BUSTER_TYPED_CLASS_VOID;
        uint32_t key_kind = tier0_access_kind(kind, values[i], access, access_count);
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
            if (fused != BUSTER_TIER0_VARIANT_PLAIN && tier0_slot(key_kind, key_class, fused_variant) != BUSTER_TIER0_SLOT_UNSUPPORTED)
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

// Phase 1 for a function with an access column: tier0_fuse_entries's copy with the lookups. It is a function of its
// own because inlined beside the other copy it cost that one two more register moves per entry.
static __attribute__((noinline)) void tier0_fuse_columned(const Tier0Function* function, const uint64_t* labels, Tier0Entries* entries)
{
    tier0_fuse_entries(function, labels, entries, function->symbol_access, function->symbol_count);
}

// Phase 1 by tier0_fuse_entries's copies, as the encoder's loops are made (encode_batches): a function without an access
// column gets the one whose lookups fold away, inlined into tier0_compile_scalar as the loop was before the column.
static void tier0_fuse_scalar(const Tier0Function* function, const uint64_t* labels, Tier0Entries* entries)
{
    if (function->symbol_access != 0)
    {
        tier0_fuse_columned(function, labels, entries);
    }
    else
    {
        tier0_fuse_entries(function, labels, entries, 0, 0);
    }
}

// The X86 entries' effects, from their rows' operands and results, in place of the shape's that phase 1 gives them (one
// push of their class): a pass of its own, which both paths make after phase 1 over a function with vectors alone, so
// that the others' fusion has no look for the kind (in tier0_raw_effects, a compare that cost every entry two
// instructions on the scalar path and one on the vector one). No X86 entry absorbs or is absorbed: their keys have no
// fused variants.
static __attribute__((noinline)) void tier0_operation_effects(const Tier0Function* function, Tier0Entries* entries)
{
    for (uint32_t i = 0; i < function->count; i += 1)
    {
        if (function->kinds[i] == BUSTER_TYPED_X86)
        {
            const Tier0Operation* operation = &function->operations[function->values[i]];
            int32_t counts[3] = { 0, 0, 0 };
            for (uint32_t j = 0; j < operation->operand_count; j += 1)
            {
                counts[tier0_stack(operation->operand_classes[j])] -= 1;
            }
            counts[tier0_stack(function->classes[i])] += 1;
            entries->integer_effects[i] = (int8_t)counts[1];
            entries->floating_effects[i] = (int8_t)counts[2];
        }
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
// a call's arguments, saved around it), two slots for the deferred division and shifts, the deep slots (8 bytes per
// depth from `deep_area`, value d of the integer stack in slot d, `deep_slots` of them, then `deep_floating_slots` of
// the floating stack's: tier0_select_deep's, none without deep entries), the memory classes' slots (`memory_slots`: 16
// bytes per integer depth from `memory_area`, an F80, I128 or U128 value at depth d in slot d), the vectors' (`vectors`:
// 64 bytes per integer depth from `vector_area`, a VECTOR value at depth d in slot d), the optional incoming
// variadic register-save area, the locals' slots, then the objects. Incoming saves never share outgoing
// call-preservation storage. `size` is what the prologue subtracts from rsp (with the pushes and the return address, a
// multiple of 16 when the function calls or has an object aligned to 16, but in a realigned frame, whose rsp the prologue
// rounds down after it); `saved` has a bit per tier0_callee_saved
// register the prologue pushes. `clobbers` is the register set the body touches (BUSTER_TIER0_XMM's layout), and
// `integer_top` and `floating_top` the highest depth registers in it.
//
// A function with ALLOCA, STACK_SAVE or STACK_RESTORE entries (`moves`) moves rsp in its body, so its frame is
// addressed from a frame pointer (`pointer`): the prologue pushes rbp first and points it at the push, `pushes` counts
// it, and the body reaches every place of the frame as [rbp + its offset from rsp + `bias`] (`base` is RBP, where it is
// RSP otherwise, with no bias), but the outgoing arguments, which stay at rsp. The outgoing area is then a multiple of 16
// (`outgoing`), so that an allocation (at rsp + `outgoing`) is aligned and ends where the area began, and rsp stays a
// multiple of 16 at every entry.
//
// A function whose objects or calls' outgoing areas need more than 16 bytes' alignment (`alignment_log2`, 0 for none:
// tier0_frame_alignment) has a realigned frame: rbp pushed and pointed at the push, the callee-saved registers pushed, the
// frame reserved, then rsp rounded down to the alignment (`and rsp, -alignment`), which leaves rbp at a distance no one
// knows from it. Its places are then addressed from rsp with no bias, or with `moves` from r15 (`based`), pushed with
// the callee-saved registers and set to rsp after the realignment, since the body moves rsp: an allocation then rounds
// its bytes up to the frame's alignment, so that rsp keeps it. Its incoming places (stack parameters' homes, an
// aggregate's incoming bytes, the variadic overflow area) are addressed from rbp, past the return address. `depth_count`
// is the integer depths the frame leaves the stack (tier0_integer_depth_count), and the incoming area starts at
// [`incoming_base` + `incoming`]: rsp's, past the frame, the pushes and the return address, or rbp's in a realigned frame.
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
    uint32_t deep_area;
    uint32_t deep_slots;
    uint32_t deep_floating_slots;
    uint32_t memory_area;
    uint32_t vector_area;
    uint32_t variadic_area;
    uint32_t overflow_area;
    uint32_t outgoing;
    uint32_t clobbers;
    int32_t integer_top;
    int32_t floating_top;
    int32_t bias;
    int32_t depth_count;
    int32_t incoming;
    uint8_t base;
    uint8_t incoming_base;
    uint8_t alignment_log2;
    bool calls;
    bool moves;
    bool pointer;
    bool based;
    // Whether a parameter is an object (an aggregate's, tier0_object_parameters), without which the passes over the
    // parameters leave out what only objects need.
    bool object_parameters;
    // Whether the function has values of the memory classes or vectors (tier0_memory_function), whose entries its compile
    // looks for; whether it has the memory classes' slots (Tier0Function.x87 or int128) and the vectors' (vectors); and
    // whether it returns a long double, in st0 from slot 0.
    bool memory;
    bool memory_slots;
    bool vectors;
    bool x87_result;
};

// A memory class's slot: 16 bytes for each integer depth, of which an x87 value takes 10 and a 128-bit integer 16.
#define BUSTER_TIER0_MEMORY_SLOT_BYTES 16
// A vector's slot: 64 bytes for each integer depth, of which a VECTOR value takes its bytes from the first.
#define BUSTER_TIER0_VECTOR_SLOT_BYTES 64

// Whether a function has values of the memory classes or vectors (Tier0Frame.memory): long doubles, 128-bit integers,
// VECTOR values or X86 entries, each of whose entries its compile looks for, out of line.
static bool tier0_memory_function(const Tier0Function* function)
{
    return function->x87 || function->int128 || function->vectors;
}

// Whether LOCAL, STORE_LOCAL and SET_LOCAL of a VECTOR may name local `local` (below the count): an object of the
// vector's bytes (16, 32 or 64), all of which the entries read or write, but an address-only one.
static bool tier0_vector_object(const Tier0Function* function, uint64_t local)
{
    return tier0_is_object(function, local) && tier0_vector_bytes_valid(function->local_sizes[local]) &&
           (function->local_flags[local] & BUSTER_TYPED_LOCAL_ADDRESS_ONLY) == 0;
}

// Whether LOCAL, STORE_LOCAL and SET_LOCAL of class `class` may name local `local` (below the count) as an object: one of
// at least the class's bytes (a memory class's slot's 16), but an address-only one (a va_list's, a variadic frame's
// anchor), whose first bytes the entries read and write, as its home's, the place in the frame it has as a slot would:
// a front end's temporaries of a memory class and its locals, which live in memory, and its values that must (one aligned
// past 8 bytes, its object's alignment). A value's local of the class is named otherwise.
static bool tier0_value_object(const Tier0Function* function, uint64_t local, uint32_t class)
{
    return class != BUSTER_TYPED_CLASS_VOID && class < BUSTER_TYPED_CLASSES && tier0_is_object(function, local) &&
           function->local_sizes[local] >= tier0_class_bytes(class) && (function->local_flags[local] & BUSTER_TYPED_LOCAL_ADDRESS_ONLY) == 0;
}

// The frame's registers, once selection knows whether the entries move rsp (`moves`): a frame pointer where they do or
// the frame is realigned, a realigned frame's base (r15) where both, and the integer depths they leave the stack.
static void tier0_frame_registers(Tier0Frame* frame, bool moves, int32_t depth_count)
{
    const bool realigned = frame->alignment_log2 != 0;
    frame->moves = moves;
    frame->pointer = moves || realigned;
    frame->based = moves && realigned;
    frame->depth_count = depth_count;
}

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

// At most 16 general or 16 floating registers can be homes. Only each kind's first 16 eligible locals in the stable
// reference order can take one, whatever the clobbers. Keep those prefixes instead of sorting every local (#664).
#define BUSTER_TIER0_HOME_CANDIDATES 16u
_Static_assert(BUSTER_TIER0_CALLER_SAVED_DEPTHS + BUSTER_TIER0_CALLEE_SAVED <= BUSTER_TIER0_HOME_CANDIDATES, "the bounded prefix covers every general home");

static uint32_t tier0_register_locals(const Tier0Function* function, const Tier0Home* homes, const uint32_t* references, uint32_t* order)
{
    uint32_t count = 0;
    for (uint32_t kind = 0; kind < 2; kind += 1)
    {
        uint32_t counts[BUSTER_TIER0_HOME_CANDIDATES] = { 0 };
        uint32_t indices[BUSTER_TIER0_HOME_CANDIDATES] = { 0 };
        #pragma clang loop vectorize(disable) interleave(disable) unroll(disable)
        for (uint32_t local = 0; local < function->local_count; local += 1)
        {
            const uint32_t reference = references[local];
            const bool eligible = !homes[local].is_register && (function->local_flags[local] & BUSTER_TYPED_LOCAL_ADDRESS_TAKEN) == 0 &&
                                  tier0_class_floating(function->local_classes[local]) == (kind != 0);
            if (eligible && reference > counts[BUSTER_TIER0_HOME_CANDIDATES - 1])
            {
                uint32_t at = BUSTER_TIER0_HOME_CANDIDATES - 1;
                #pragma clang loop vectorize(disable) interleave(disable) unroll(disable)
                while (at > 0 && reference > counts[at - 1])
                {
                    counts[at] = counts[at - 1];
                    indices[at] = indices[at - 1];
                    at -= 1;
                }
                counts[at] = reference;
                indices[at] = local;
            }
        }
        for (uint32_t at = 0; at < BUSTER_TIER0_HOME_CANDIDATES && counts[at] != 0; at += 1)
        {
            order[count] = indices[at];
            count += 1;
        }
    }
    return count;
}

// The same bounded prefixes, with 16-local eligibility and threshold masks and register-resident insertion. Locals
// arrive by index, so a new key precedes only strictly smaller counts: equal keys retain their original index order.
// Every insertion moves at most 16 lanes, independent of the function's local count. Loads honor the logical tails.
static uint32_t tier0_register_locals_vector(const Tier0Function* function, const Tier0Home* homes, const uint32_t* references, uint32_t* order)
{
    const __m512i previous = _mm512_set_epi32(14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 0);
    const __m512i is_register = _mm512_set1_epi64((int64_t)(0xffull << (8 * offsetof(Tier0Home, is_register))));
    uint32_t count = 0;
    for (uint32_t kind = 0; kind < 2; kind += 1)
    {
        __m512i best = _mm512_setzero_si512();
        __m512i indices = _mm512_setzero_si512();
        for (uint32_t first = 0; first < function->local_count; first += 16)
        {
            const __mmask16 lanes = (__mmask16)tier0_lanes(first, function->local_count);
            const __m512i counts = _mm512_maskz_loadu_epi32(lanes, references + first);
            const __m512i low = _mm512_maskz_loadu_epi64((__mmask8)lanes, homes + first);
            const __m512i high = (lanes >> 8) != 0 ? _mm512_maskz_loadu_epi64((__mmask8)(lanes >> 8), homes + first + 8) : _mm512_setzero_si512();
            const __mmask16 in_register = _mm512_kunpackb(_mm512_test_epi64_mask(high, is_register), _mm512_test_epi64_mask(low, is_register));
            const __m128i flags = _mm_maskz_loadu_epi8(lanes, function->local_flags + first);
            const __m128i classes = _mm_maskz_loadu_epi8(lanes, function->local_classes + first);
            const __mmask16 address = _mm_test_epi8_mask(flags, _mm_set1_epi8((char)BUSTER_TYPED_LOCAL_ADDRESS_TAKEN));
            const __mmask16 floating = _mm_cmpeq_epi8_mask(classes, _mm_set1_epi8(BUSTER_TYPED_CLASS_F32)) | _mm_cmpeq_epi8_mask(classes, _mm_set1_epi8(BUSTER_TYPED_CLASS_F64));
            const __m512i least = _mm512_permutexvar_epi32(_mm512_set1_epi32(BUSTER_TIER0_HOME_CANDIDATES - 1), best);
            uint32_t candidates = lanes & ~in_register & ~address & (kind != 0 ? floating : ~floating) & _mm512_cmpgt_epu32_mask(counts, least);
            while (candidates != 0)
            {
                const uint32_t local = first + (uint32_t)__builtin_ctz(candidates);
                candidates &= candidates - 1;
                const __m512i key = _mm512_set1_epi32((int32_t)references[local]);
                const __mmask16 after = _mm512_cmpgt_epu32_mask(key, best);
                if (after != 0)
                {
                    const __mmask16 at = (__mmask16)(1u << (uint32_t)__builtin_ctz(after));
                    best = _mm512_mask_permutexvar_epi32(best, after, previous, best);
                    best = _mm512_mask_mov_epi32(best, at, key);
                    indices = _mm512_mask_permutexvar_epi32(indices, after, previous, indices);
                    indices = _mm512_mask_set1_epi32(indices, at, (int32_t)local);
                }
            }
        }
        const __mmask16 kept = _mm512_test_epi32_mask(best, best);
        _mm512_mask_compressstoreu_epi32(order + count, kept, indices);
        count += (uint32_t)__builtin_popcount(kept);
    }
    return count;
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

// Whether a parameter is an object (tier0_is_object): an aggregate passed by value (Tier0Frame.object_parameters).
static bool tier0_object_parameters(const Tier0Function* function)
{
    bool objects = false;
    for (uint32_t p = 0; p < function->parameter_count && function->local_sizes != 0; p += 1)
    {
        objects |= function->local_sizes[p] != 0;
    }
    return objects;
}

// The log2 of the alignment a function's frame needs past 16 bytes, to which the prologue realigns it
// (Tier0Frame.alignment_log2), or 0 for none: the most its objects need and the most its calls' outgoing areas need
// (AbiCall.stack_alignment_log2, which an argument aligned past 16 raises). An aggregate parameter that keeps its
// incoming bytes rather than living in the frame (tier0_parameter_in_place) is aligned to 16 at most, so that every
// object may count. Out of line, once per function, before selection, whose integer depths it decides; the vector
// path's twin is tier0_frame_alignment_vector.
static __attribute__((noinline)) uint32_t tier0_frame_alignment(const Tier0Function* function)
{
    uint32_t log2 = BUSTER_TIER0_FRAME_ALIGNMENT_LOG2;
    #pragma clang loop vectorize(disable) interleave(disable) unroll(disable)
    for (uint32_t l = 0; l < function->local_count && function->local_sizes != 0; l += 1)
    {
        const uint32_t alignment = function->local_sizes[l] != 0 ? function->local_alignments[l] : 0;
        log2 = alignment > log2 ? alignment : log2;
    }
    #pragma clang loop vectorize(disable) interleave(disable) unroll(disable)
    for (uint32_t c = 0; c < function->call_count; c += 1)
    {
        const uint32_t alignment = function->calls[c].stack_alignment_log2;
        log2 = alignment > log2 ? alignment : log2;
    }
    return log2 > BUSTER_TIER0_FRAME_ALIGNMENT_LOG2 ? log2 : 0;
}

// tier0_frame_alignment for the vector path: the objects' alignments 16 locals at a time (their bytes loaded under the
// mask of the locals with sizes) and the calls' 4 at a time (each one's stack_alignment_log2, the low byte of its second
// dword), as dwords whose most is the answer.
static __attribute__((noinline)) uint32_t tier0_frame_alignment_vector(const Tier0Function* function)
{
    _Static_assert(sizeof(AbiCall) == 16 && offsetof(AbiCall, stack_alignment_log2) == 4, "4 calls in 64 bytes, each one's alignment the low byte of its second dword");
    __m512i most = _mm512_setzero_si512();
    const uint32_t locals = function->local_sizes != 0 ? function->local_count : 0;
    #pragma clang loop unroll(disable)
    for (uint32_t first = 0; first < locals; first += 16)
    {
        const __m512i sizes = _mm512_maskz_loadu_epi32((__mmask16)tier0_lanes(first, locals), function->local_sizes + first);
        const __mmask16 objects = _mm512_test_epi32_mask(sizes, sizes);
        most = _mm512_max_epu32(most, _mm512_cvtepu8_epi32(_mm_maskz_loadu_epi8(objects, function->local_alignments + first)));
    }
    #pragma clang loop unroll(disable)
    for (uint32_t first = 0; first < function->call_count; first += 4)
    {
        const uint32_t left = function->call_count - first;
        const __mmask16 alignments = (__mmask16)((left >= 4 ? 0xffffu : (1u << 4 * left) - 1) & 0x2222u);
        const __m512i calls = _mm512_maskz_loadu_epi32(alignments, function->calls + first);
        most = _mm512_max_epu32(most, _mm512_and_si512(calls, _mm512_set1_epi32(0xff)));
    }
    const uint32_t log2 = _mm512_reduce_max_epu32(most);
    return log2 > BUSTER_TIER0_FRAME_ALIGNMENT_LOG2 ? log2 : 0;
}

// Phase 3: each local's home, and the frame. A parameter whose register the body doesn't touch stays in it. The other
// locals, most referenced first (by index among equals), take the free caller-saved registers, then (referenced at
// least twice, for a push and a pop) the callee-saved ones; a floating local takes a free XMM register. The rest, and
// every local whose address is taken, get frame slots; a parameter on the stack keeps its incoming slot, unless it takes
// a register, which the prologue loads from that slot. `references` (local_count) holds each local's references, and
// `order` (2 * BUSTER_TIER0_HOME_CANDIDATES) is scratch for the bounded eligible prefixes by kind.
static void tier0_assign_homes(const Tier0Function* function, Tier0Frame* frame, Tier0Home* homes, const uint32_t* references, uint32_t* order, bool vector)
{
    (void)vector;

    const uint32_t locals = function->local_count;
    // The registers parameters arrive in: no other local takes one, so the prologue's moves never overwrite a source. An
    // aggregate's parts arrive in both of a location's registers (a SPLIT one's in one of them).
    uint32_t arrivals = 0;
    for (uint32_t p = 0; p < function->parameter_count; p += 1)
    {
        const AbiLocation location = function->parameters[p];
        const uint8_t reg = location.registers[0];
        arrivals |= location.kind == BUSTER_ABI_LOCATION_REGISTERS && reg != BUSTER_ABI_REGISTER_NONE ? 1u << tier0_abi_register(reg) : 0;
    }
    const bool objects = frame->object_parameters;
    for (uint32_t p = 0; p < function->parameter_count && objects; p += 1)
    {
        const AbiLocation location = function->parameters[p];
        const bool parts = tier0_is_object(function, p) && (location.kind == BUSTER_ABI_LOCATION_REGISTERS || location.kind == BUSTER_ABI_LOCATION_SPLIT);
        for (uint32_t k = 0; k < 2 && parts; k += 1)
        {
            const uint8_t reg = location.registers[k];
            arrivals |= reg != BUSTER_ABI_REGISTER_NONE ? 1u << tier0_abi_register(reg) : 0;
        }
    }
    // A frame pointer's rbp is no home either, nor a realigned frame's base, r15.
    uint32_t taken = frame->clobbers | arrivals | 1u << X86_REGISTER_RSP | (frame->pointer ? 1u << X86_REGISTER_RBP : 0) | (frame->based ? 1u << X86_REGISTER_R15 : 0);
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
    // The eligible prefixes by references, most first (`order`); for the vector path and up to 64 locals, by their
    // ranks (tier0_assign_registers_vector).
    const bool ranked = BUSTER_VECTOR_PATH(vector) && locals <= 64;
    uint32_t candidates = 0;
    if (ranked)
    {
        frame->saved |= tier0_assign_registers_vector(function, homes, references, taken);
    }
    else
    {
        candidates = vector ? tier0_register_locals_vector(function, homes, references, order) : tier0_register_locals(function, homes, references, order);
    }
    for (uint32_t k = 0; k < candidates; k += 1)
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
    // The callee-saved registers the depths reach are saved too (never rbp with a frame pointer, whose depth is refused),
    // and a realigned frame's base, r15 (the depths never reach it).
    for (uint32_t c = 0; c < BUSTER_TIER0_CALLEE_SAVED; c += 1)
    {
        frame->saved |= (frame->clobbers & 1u << tier0_callee_saved[c]) != 0 ? 1u << c : 0;
    }
    frame->saved |= frame->based ? 1u << BUSTER_TIER0_SAVED_R15 : 0;
    // The frame pointer's push, first, counts with the others.
    frame->pushes = (uint32_t)__builtin_popcount(frame->saved) + (frame->pointer ? 1u : 0u);
}

// The frame's offsets and size, and the memory homes' displacements: the outgoing arguments at rsp, then the call
// save area, the fixed slots, the deep slots, long double's slots, the optional incoming variadic saves, each memory
// local (8 bytes, or exactly 24 for an address-only VA_LIST), and the objects, each at a multiple of its alignment and
// taking its size rounded up to 8, in the locals' order. Anchor descriptors alias their frame/incoming addresses without owning
// another slot. A parameter that arrives on the stack keeps its incoming slot, above the return address, and an
// aggregate's its incoming slots, which the callee owns (its stack bytes are a copy). Check every displacement in 64
// bits before narrowing; an unrepresentable frame is refused before any records or output are published.
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
static uint64_t tier0_frame_objects_vector(const Tier0Function* function, const Tier0Frame* frame, Tier0Home* homes, uint64_t offset, bool* aligned)
{
    const uint32_t locals = function->local_count;
    const bool object_parameters = frame->object_parameters;
    for (uint32_t first = 0; first < locals && function->local_sizes != 0; first += 16)
    {
        const __mmask16 lanes = (__mmask16)tier0_lanes(first, locals);
        const __m512i sizes = _mm512_maskz_loadu_epi32(lanes, function->local_sizes + first);
        // An aggregate parameter on the stack whole keeps its incoming bytes (tier0_parameter_in_place).
        uint32_t in_place = 0;
        for (uint32_t p = first; p < function->parameter_count && p < first + 16 && object_parameters; p += 1)
        {
            in_place |= tier0_parameter_in_place(function, p) ? 1u << (p - first) : 0;
        }
        uint32_t objects = _mm512_test_epi32_mask(sizes, sizes) & ~in_place;
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
    (void)vector;

    const bool object_parameters = frame->object_parameters;
    uint64_t outgoing = 0;
    for (uint32_t c = 0; c < function->call_count; c += 1)
    {
        outgoing = function->calls[c].stack_bytes > outgoing ? function->calls[c].stack_bytes : outgoing;
    }
    // With entries that move rsp, a multiple of 16: where an allocation starts, aligned, above the outgoing arguments.
    uint64_t offset = frame->moves ? (outgoing + 15) & ~UINT64_C(15) : (outgoing + 7) & ~UINT64_C(7);
    bool valid = offset <= INT32_MAX;
    frame->outgoing = valid ? (uint32_t)offset : 0;
    frame->save_area = valid ? (uint32_t)offset : 0;
    offset += 8 * frame->save_slots;
    valid = valid && offset <= INT32_MAX;
    frame->fixed_area = valid ? (uint32_t)offset : 0;
    offset += 8 * frame->fixed_slots;
    valid = valid && offset <= INT32_MAX;
    frame->deep_area = valid ? (uint32_t)offset : 0;
    offset += 8 * ((uint64_t)frame->deep_slots + frame->deep_floating_slots);
    if (frame->memory_slots)
    {
        valid = valid && offset <= INT32_MAX;
        frame->memory_area = valid ? (uint32_t)offset : 0;
        offset += BUSTER_TIER0_MEMORY_SLOT_BYTES * BUSTER_TIER0_INTEGER_DEPTHS;
    }
    if (frame->vectors)
    {
        valid = valid && offset <= INT32_MAX;
        frame->vector_area = valid ? (uint32_t)offset : 0;
        offset += BUSTER_TIER0_VECTOR_SLOT_BYTES * BUSTER_TIER0_INTEGER_DEPTHS;
    }
    if (function->incoming_variadic != 0)
    {
        valid = valid && offset <= INT32_MAX;
        frame->variadic_area = valid ? (uint32_t)offset : 0;
        offset += BUSTER_ABI_SAVE_AREA_BYTES;
    }
    for (uint32_t l = 0; l < function->local_count && !BUSTER_VECTOR_PATH(vector); l += 1)
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
    for (uint32_t l = 0; l < function->local_count && !BUSTER_VECTOR_PATH(vector); l += 1)
    {
        const bool in_place = object_parameters && l < function->parameter_count && tier0_parameter_in_place(function, l);
        if (tier0_is_object(function, l) && !in_place)
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
    offset = BUSTER_VECTOR_PATH(vector) ? tier0_frame_slots_vector(function, homes, references, offset, &objects) : offset;
    offset = BUSTER_VECTOR_PATH(vector) && objects ? tier0_frame_objects_vector(function, frame, homes, offset, &aligned) : offset;
    // With the pushes and the return address, a multiple of 16 at every call, for an object aligned to 16, and with a
    // frame pointer, under which every allocation keeps it so. A realigned frame's rsp is rounded down to its alignment
    // after the reservation instead, which keeps it so too.
    const uint64_t above = 8 * frame->pushes + 8;
    const bool realigned = frame->alignment_log2 != 0;
    if ((frame->calls || aligned || frame->pointer) && !realigned)
    {
        offset = (offset + above + 15) / 16 * 16 - above;
    }
    valid = valid && offset <= INT32_MAX;
    frame->size = valid ? (uint32_t)offset : 0;
    // The incoming area: past the frame, the pushes and the return address from rsp, or in a realigned frame, whose rsp is
    // at a distance from them no one knows, past rbp's push and the return address from rbp.
    const uint64_t incoming = realigned ? 16 : offset + above;
    frame->incoming_base = realigned ? X86_REGISTER_RBP : X86_REGISTER_RSP;
    frame->incoming = incoming <= INT32_MAX ? (int32_t)incoming : 0;
    if (function->incoming_variadic != 0)
    {
        const uint64_t overflow = incoming + function->incoming_variadic->stack_bytes;
        valid = valid && overflow <= INT32_MAX;
        frame->overflow_area = valid ? (uint32_t)overflow : 0;
    }
    for (uint32_t l = 0; l < function->local_count; l += 1)
    {
        const uint32_t role = function->local_flags[l] & BUSTER_TYPED_LOCAL_ADDRESS_ONLY;
        if (role == BUSTER_TYPED_LOCAL_REGISTER_SAVE_AREA || role == BUSTER_TYPED_LOCAL_OVERFLOW_ARG_AREA)
        {
            const bool save_area = role == BUSTER_TYPED_LOCAL_REGISTER_SAVE_AREA;
            homes[l].displacement = (int32_t)(save_area ? frame->variadic_area : frame->overflow_area);
            homes[l].reg = save_area ? X86_REGISTER_RSP : frame->incoming_base;
            homes[l].mode = BUSTER_X86_MEMORY_BASE;
        }
    }
    // Every incoming slot the prologue reads or a home keeps in reach of a 32-bit displacement.
    for (uint32_t p = 0; p < function->parameter_count; p += 1)
    {
        const uint32_t kind = function->parameters[p].kind;
        if (kind == BUSTER_ABI_LOCATION_STACK || kind == BUSTER_ABI_LOCATION_SPLIT)
        {
            const uint64_t place = incoming + function->parameters[p].stack_offset;
            valid = valid && place <= INT32_MAX;
            if (!homes[p].is_register && (object_parameters ? tier0_parameter_in_place(function, p) : kind == BUSTER_ABI_LOCATION_STACK))
            {
                homes[p].displacement = valid ? (int32_t)place : 0;
                homes[p].reg = frame->incoming_base;
                homes[p].mode = BUSTER_X86_MEMORY_BASE;
            }
        }
    }
    // With a frame pointer, every memory home from rbp, which is the frame's size and the pushes after rbp's above rsp:
    // the homes' displacements less that, each between -2^31 and the displacement it was. A realigned frame's places stay
    // rsp's, or with entries that move rsp become r15's (`based`), which holds rsp as the prologue leaves it: with no bias.
    const bool biased = frame->pointer && !realigned;
    frame->base = frame->based ? X86_REGISTER_R15 : biased ? X86_REGISTER_RBP : X86_REGISTER_RSP;
    valid = valid && (!biased || offset + 8 * (frame->pushes - 1) <= INT32_MAX);
    frame->bias = valid && biased ? (int32_t)-(int64_t)(offset + 8 * (frame->pushes - 1)) : 0;
    for (uint32_t l = 0; l < function->local_count && frame->base != X86_REGISTER_RSP; l += 1)
    {
        if (!homes[l].is_register && homes[l].reg == X86_REGISTER_RSP && homes[l].mode == BUSTER_X86_MEMORY_BASE)
        {
            homes[l].displacement = (int32_t)((int64_t)homes[l].displacement + frame->bias);
            homes[l].reg = frame->base;
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
// Always inlined, as it was with one caller: the scalar path's emission, whose loop it is most of; the deep entries'
// (tier0_emit_deep) is the second.
static inline __attribute__((always_inline)) X86Instruction tier0_record_scalar(const Tier0RecordSpec* spec, const Tier0Holes* holes)
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
    case BUSTER_TIER0_RM_ABSOLUTE:
        record.mode = BUSTER_X86_MEMORY_ABSOLUTE;
        record.displacement = spec->displacement;
        break;
    default:
        break;
    }
    return record;
}

// ---------------------------------------------------------------------------------------------------------------------
// The deferred entries

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

// A record sink: writes records at `records[count]` onward (when `records` isn't 0: the function's records, `count` an
// absolute index) and counts them, and adds the calls' references and a deep entry's branch and choice request
// (tier0_emit_deep's: its branch's record and target in the code's branch columns, `branch_count` of them, and its
// request in `choices`, which emission's templates add to too; counted in `choice_count` when counting).
typedef struct Tier0Writer Tier0Writer;
struct Tier0Writer
{
    X86Instruction* records;
    uint32_t count;
    LinkReference* references;
    uint32_t reference_count;
    uint32_t* branches;
    uint32_t* targets;
    uint32_t branch_count;
    Tier0Choices* choices;
    uint32_t choice_count;
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

// mov [rsp + offset], reg and back (from rbp with a frame pointer: Tier0Frame's base and bias), 64-bit (a general
// register) or as a double (an XMM one, BUSTER_TIER0_XMM's).
static void tier0_spill(Tier0Writer* writer, const Tier0Frame* frame, uint32_t reg, uint32_t offset, bool load)
{
    const bool xmm = reg >= 16;
    const uint32_t form = xmm ? (load ? X86_FORM_MOVSD_X_XM64 : X86_FORM_MOVSD_XM64_X) : (load ? X86_FORM_MOV_R64_RM64 : X86_FORM_MOV_RM64_R64);
    tier0_put(writer, tier0_instruction(form, (uint8_t)(reg & 15), frame->base, BUSTER_X86_MEMORY_BASE, (int32_t)((int64_t)offset + frame->bias), 0));
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
// instead. A path's move takes a record, and a cycle of n moves n - 1 (its last move becomes a register's own). Always
// inlined, as it was with one caller: every call's moves go through it, and the second caller, an aggregate's XMM moves
// (tier0_aggregate_moves), would otherwise put a call of it in every call's emission.
static inline __attribute__((always_inline)) void tier0_parallel_moves(Tier0Writer* writer, const uint8_t* to, const uint8_t* from, uint32_t count)
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

// `size` bytes copied from [from + from_displacement] to [to + to_displacement], the ranges the same or apart: from 16
// bytes, a load and a store with movups per 16 through XMM register `xmm`, the last 16 ending at the end; below, one or
// two of a general register's 8, 4, 2 or 1 bytes through `general`, the second ending at the end. Overlapping pieces copy
// what they would apart. A COPY's records, and a call's aggregates' (tier0_emit_call).
static void tier0_copy(Tier0Writer* writer, uint8_t to, int32_t to_displacement, uint8_t from, int32_t from_displacement, uint32_t size, uint8_t general, uint8_t xmm)
{
    if (size >= 16)
    {
        for (uint32_t offset = 0; offset < size; offset += 16)
        {
            const int32_t at = (int32_t)(offset + 16 <= size ? offset : size - 16);
            tier0_put(writer, tier0_instruction(X86_FORM_MOVUPS_X_XM, xmm, from, BUSTER_X86_MEMORY_BASE, from_displacement + at, 0));
            tier0_put(writer, tier0_instruction(X86_FORM_MOVUPS_XM_X, xmm, to, BUSTER_X86_MEMORY_BASE, to_displacement + at, 0));
        }
    }
    else
    {
        const uint32_t piece = size >= 8 ? 8 : size >= 4 ? 4 : size >= 2 ? 2 : 1;
        const uint32_t load = piece == 8 ? X86_FORM_MOV_R64_RM64 : piece == 4 ? X86_FORM_MOV_R32_RM32 : piece == 2 ? X86_FORM_MOVZX_R32_RM16 : X86_FORM_MOVZX_R32_RM8;
        const uint32_t store = piece == 8 ? X86_FORM_MOV_RM64_R64 : piece == 4 ? X86_FORM_MOV_RM32_R32 : piece == 2 ? X86_FORM_MOV_RM16_R16 : X86_FORM_MOV_RM8_R8;
        tier0_put(writer, tier0_instruction(load, general, from, BUSTER_X86_MEMORY_BASE, from_displacement, 0));
        tier0_put(writer, tier0_instruction(store, general, to, BUSTER_X86_MEMORY_BASE, to_displacement, 0));
        if (size > piece)
        {
            tier0_put(writer, tier0_instruction(load, general, from, BUSTER_X86_MEMORY_BASE, from_displacement + (int32_t)(size - piece), 0));
            tier0_put(writer, tier0_instruction(store, general, to, BUSTER_X86_MEMORY_BASE, to_displacement + (int32_t)(size - piece), 0));
        }
    }
}

// How many records tier0_copy writes for `size` bytes (at least 1): a load and a store per 16 bytes or part of them;
// below 16, per piece, one or two.
static uint32_t tier0_copy_records(uint32_t size)
{
    const uint32_t piece = size >= 8 ? 8 : size >= 4 ? 4 : size >= 2 ? 2 : 1;
    return size >= 16 ? 2 * ((size + 15) / 16) : size > piece ? 4 : 2;
}

// The red zone temporary `t`'s place below rsp (BUSTER_TIER0_TEMPORARIES).
static int32_t tier0_temporary(uint32_t t)
{
    return -8 * (int32_t)(t + 1);
}

// Part `part` of an aggregate whose address is in `base` loaded into its register as tier0_parts_of says: one load of its
// width, the 8 bytes that end where it ends shifted down, or 8 bytes from red zone temporary `temporary`, which holds its
// bytes.
static void tier0_load_part(Tier0Writer* writer, Tier0Part part, uint8_t base, uint32_t temporary)
{
    const bool xmm = part.reg >= 16;
    const uint8_t reg = (uint8_t)(part.reg & 15);
    if (part.load == BUSTER_TIER0_PART_TEMPORARY)
    {
        const uint32_t form = xmm ? X86_FORM_MOVSD_X_XM64 : X86_FORM_MOV_R64_RM64;
        tier0_put(writer, tier0_instruction(form, reg, X86_REGISTER_RSP, BUSTER_X86_MEMORY_BASE, tier0_temporary(temporary), 0));
    }
    else if (part.load == BUSTER_TIER0_PART_OVERLAP)
    {
        tier0_put(writer, tier0_instruction(X86_FORM_MOV_R64_RM64, reg, base, BUSTER_X86_MEMORY_BASE, (int32_t)part.offset + part.bytes - 8, 0));
        tier0_put(writer, tier0_instruction(X86_FORM_SHR_RM64_IMM8, 0, reg, BUSTER_X86_REGISTER_DIRECT, 0, 8 * (8 - part.bytes)));
    }
    else
    {
        const uint32_t form = xmm ? (part.bytes == 64   ? X86_FORM_VMOVDQU64_Z_ZM512
                                     : part.bytes == 32 ? X86_FORM_VMOVDQU_Y_YM256
                                     : part.bytes == 16 ? X86_FORM_MOVUPS_X_XM
                                     : part.bytes == 8  ? X86_FORM_MOVSD_X_XM64
                                                        : X86_FORM_MOVSS_X_XM32)
                                  : part.bytes == 8 ? X86_FORM_MOV_R64_RM64 : part.bytes == 4 ? X86_FORM_MOV_R32_RM32 : part.bytes == 2 ? X86_FORM_MOVZX_R32_RM16 : X86_FORM_MOVZX_R32_RM8;
        tier0_put(writer, tier0_instruction(form, reg, base, BUSTER_X86_MEMORY_BASE, part.offset, 0));
    }
}

// How many records tier0_load_part writes.
static uint32_t tier0_part_records(Tier0Part part)
{
    return part.load == BUSTER_TIER0_PART_OVERLAP ? 2 : 1;
}

// A register's part stored whole at `offset` in the object at `home`: 8 bytes, or 16 for a vector's XMM register, and
// 32 or 64 for its YMM or ZMM register (vmovdqu, which AVX has, for 32; vmovdqu64, AVX-512F's, for 64, a ZMM register
// being AVX-512F's).
static void tier0_store_part(Tier0Writer* writer, uint8_t reg, uint32_t bytes, Tier0Home home, uint32_t offset)
{
    const uint32_t vector = bytes == 64   ? X86_FORM_VMOVDQU64_ZM512_Z
                            : bytes == 32 ? X86_FORM_VMOVDQU_YM256_Y
                            : bytes == 16 ? X86_FORM_MOVUPS_XM_X
                                          : X86_FORM_MOVSD_XM64_X;
    const uint32_t form = reg >= 16 ? vector : X86_FORM_MOV_RM64_R64;
    tier0_put(writer, tier0_instruction(form, (uint8_t)(reg & 15), home.reg, home.mode, home.displacement + (int32_t)offset, 0));
}

// The depths each stack reaches, as selection takes them.
typedef struct Tier0Tops Tier0Tops;
struct Tier0Tops
{
    int32_t integer;
    int32_t floating;
};

// What a function's aggregate arguments (argument_sizes) need at their calls besides the moves, both selections' touches,
// the stacks' tops past `tops`: at a call, its integer stack's first free register, the general scratch of a copy
// below 16 bytes (a SPLIT argument's stack part, an aggregate on the stack, a TEMPORARY part's bytes), and its floating
// stack's, the XMM scratch of a copy from 16 bytes. A pass of its own, which the selections make over a function with
// argument_sizes alone, after their loops over the entries, which then carry nothing for it (every call is deferred, and
// an unsupported entry fails the compile whatever the depths).
static Tier0Tops tier0_select_scratch(const Tier0Function* function, const Tier0Entries* entries, Tier0Tops tops)
{
    for (uint32_t i = 0; i < function->count; i += 1)
    {
        bool general = false;
        bool xmm = false;
        if (function->kinds[i] == BUSTER_TYPED_CALL && entries->absorbed[i] == 0)
        {
            const uint32_t call = (uint32_t)function->values[i];
            const uint32_t first = function->call_first[call];
            for (uint32_t a = 0; a < function->call_argument_counts[call]; a += 1)
            {
                const uint32_t size = function->argument_sizes[first + a];
                const AbiLocation location = function->argument_locations[first + a];
                Tier0Part parts[2];
                const uint32_t count = size != 0 ? tier0_location_parts(location, size, parts) : 0;
                for (uint32_t k = 0; k < count; k += 1)
                {
                    general |= parts[k].reg == BUSTER_TIER0_PART_STACK || parts[k].load == BUSTER_TIER0_PART_TEMPORARY;
                }
                general |= size != 0 && size < 16 && tier0_aggregate_whole(location);
                xmm |= size >= 16 && tier0_aggregate_whole(location);
            }
        }
        const int32_t depth = entries->depths[i];
        const int32_t floating = entries->floating_depths[i];
        tops.integer = general && depth > tops.integer ? depth : tops.integer;
        tops.floating = xmm && floating > tops.floating ? floating : tops.floating;
    }
    return tops;
}

// A call's aggregate argument in registers as tier0_emit_call places it: the register its address is in, its parts and
// each TEMPORARY one's red zone temporary, and the register that holds its address while its general parts load.
typedef struct Tier0CallAggregate Tier0CallAggregate;
struct Tier0CallAggregate
{
    Tier0Part parts[2];
    uint8_t temporaries[2];
    uint8_t count;
    uint8_t source;
    uint8_t holder;
};

// A call's aggregate arguments (argument_sizes) after its values' stores, before the moves (tier0_emit_call's), each in
// `aggregates` in the arguments' order: on the stack whole, its bytes copied into its slots; else its parts (none for one
// ignored), a SPLIT one's stack part copied into its slot and a TEMPORARY one's bytes into its red zone temporary, the
// copies through each stack's first free register (tier0_select_scratch's). Returns how many there are. Out of line, as
// tier0_aggregate_moves is: a pass of its own, so that a call's loop over its values carries nothing for them.
static __attribute__((noinline)) uint32_t tier0_place_aggregates(const Tier0Function* function, const Tier0Entries* entries, uint32_t i, uint32_t arguments_base,
                                                                 uint32_t floating_below, Tier0CallAggregate* aggregates, Tier0Writer* writer)
{
    const uint32_t call = (uint32_t)function->values[i];
    const uint32_t first = function->call_first[call];
    const uint8_t general = entries->depths[i] < BUSTER_TIER0_INTEGER_DEPTHS ? tier0_integer_depths[entries->depths[i]] : 0;
    const uint8_t xmm = entries->floating_depths[i];
    uint32_t count = 0;
    uint32_t temporaries = 0;
    uint32_t integer = 0;
    uint32_t floating = 0;
    for (uint32_t a = 0; a < function->call_argument_counts[call]; a += 1)
    {
        const uint32_t size = function->argument_sizes[first + a];
        const AbiLocation location = function->argument_locations[first + a];
        const bool is_floating = tier0_class_floating(function->argument_classes[first + a]);
        const uint8_t source = (uint8_t)(is_floating ? BUSTER_TIER0_XMM(floating_below + floating) : tier0_integer_depths[arguments_base + integer]);
        integer += is_floating ? 0 : 1;
        floating += is_floating ? 1 : 0;
        if (size != 0)
        {
            Tier0CallAggregate* aggregate = &aggregates[count];
            count += 1;
            aggregate->source = source;
            aggregate->holder = source;
            aggregate->count = tier0_aggregate_whole(location) ? 0 : (uint8_t)tier0_location_parts(location, size, aggregate->parts);
            if (tier0_aggregate_whole(location))
            {
                tier0_copy(writer, X86_REGISTER_RSP, (int32_t)location.stack_offset, source, 0, size, general, xmm);
            }
            for (uint32_t k = 0; k < aggregate->count; k += 1)
            {
                const Tier0Part part = aggregate->parts[k];
                aggregate->temporaries[k] = (uint8_t)temporaries;
                if (part.reg == BUSTER_TIER0_PART_STACK)
                {
                    tier0_copy(writer, X86_REGISTER_RSP, (int32_t)location.stack_offset, source, part.offset, part.bytes, general, xmm);
                }
                else if (part.load == BUSTER_TIER0_PART_TEMPORARY)
                {
                    tier0_copy(writer, X86_REGISTER_RSP, tier0_temporary(temporaries), source, part.offset, part.bytes, general, xmm);
                    temporaries += 1;
                }
            }
        }
    }
    return count;
}

// How many records tier0_place_aggregates and the loads after the moves write for an aggregate argument of `size` bytes
// at `location` (tier0_count_deferred's): a copy on the stack whole, and per part a stack part's copy, a TEMPORARY one's
// copy and load, or its loads; with the last of its general parts' registers in *own (BUSTER_TIER0_PART_STACK for none)
// and the set of them in *own_set.
static uint32_t tier0_aggregate_records(AbiLocation location, uint32_t size, uint8_t* own, uint32_t* own_set)
{
    Tier0Part parts[2];
    const bool whole = tier0_aggregate_whole(location);
    const uint32_t part_count = whole ? 0 : tier0_location_parts(location, size, parts);
    uint32_t count = whole ? tier0_copy_records(size) : 0;
    *own = BUSTER_TIER0_PART_STACK;
    *own_set = 0;
    for (uint32_t k = 0; k < part_count; k += 1)
    {
        const Tier0Part part = parts[k];
        const bool general = part.reg < 16;
        count += part.reg == BUSTER_TIER0_PART_STACK ? tier0_copy_records(part.bytes)
               : part.load == BUSTER_TIER0_PART_TEMPORARY ? tier0_copy_records(part.bytes) + 1 : tier0_part_records(part);
        *own = general ? part.reg : *own;
        *own_set |= general ? 1u << part.reg : 0;
    }
    return count;
}

// The records a call's result object takes after its moves (tier0_emit_call's): the lea of its address into rdi for a
// hidden pointer, or a store per register of a result in registers.
static uint32_t tier0_result_object_records(AbiReturn returned)
{
    const uint32_t stores = (returned.registers[0] != BUSTER_ABI_REGISTER_NONE ? 1u : 0u) + (returned.registers[1] != BUSTER_ABI_REGISTER_NONE ? 1u : 0u);
    return returned.kind == BUSTER_ABI_RESULT_MEMORY ? 1 : returned.kind == BUSTER_ABI_RESULT_REGISTERS ? stores : 0;
}

// The general registers a call's argument setup writes (tier0_emit_call's): each value's in a register, each aggregate's
// general parts' and an indirect call's r11. Only an aggregate's address in registers asks, so a call with one alone.
static uint32_t tier0_call_written(const Tier0Function* function, uint32_t call)
{
    const uint32_t first = function->call_first[call];
    uint32_t written = function->call_targets[call] == 0 ? 1u << X86_REGISTER_R11 : 0;
    for (uint32_t a = 0; a < function->call_argument_counts[call]; a += 1)
    {
        const uint32_t size = function->argument_sizes[first + a];
        const AbiLocation location = function->argument_locations[first + a];
        const uint8_t destination = tier0_abi_register(location.registers[0]);
        written |= size == 0 && location.kind == BUSTER_ABI_LOCATION_REGISTERS && destination < 16 ? 1u << destination : 0;
        Tier0Part parts[2];
        const uint32_t count = size != 0 ? tier0_location_parts(location, size, parts) : 0;
        for (uint32_t k = 0; k < count; k += 1)
        {
            written |= parts[k].reg < 16 ? 1u << parts[k].reg : 0;
        }
    }
    return written;
}

// A call's moves around its aggregates' parts in registers, tier0_emit_call's steps 1 and 2: with XMM parts, the XMM
// moves, then the XMM parts, leaving the general moves in `to` and `from`; then each address that a move or a general
// part would overwrite moved first into the register that holds it while its general parts load (its `holder`), with
// the general moves. Returns how many moves are left.
static __attribute__((noinline)) uint32_t tier0_aggregate_moves(const Tier0Function* function, uint32_t call, Tier0CallAggregate* aggregates, uint32_t aggregate_count,
                                                                uint8_t* to, uint8_t* from, uint32_t moves, Tier0Writer* writer)
{
    bool xmm_parts = false;
    for (uint32_t g = 0; g < aggregate_count; g += 1)
    {
        for (uint32_t k = 0; k < aggregates[g].count; k += 1)
        {
            xmm_parts |= aggregates[g].parts[k].reg != BUSTER_TIER0_PART_STACK && aggregates[g].parts[k].reg >= 16;
        }
    }
    if (xmm_parts)
    {
        uint8_t xmm_to[BUSTER_TIER0_MAX_CALL_ARGUMENTS + 1];
        uint8_t xmm_from[BUSTER_TIER0_MAX_CALL_ARGUMENTS + 1];
        uint32_t xmm_moves = 0;
        uint32_t general_moves = 0;
        for (uint32_t k = 0; k < moves; k += 1)
        {
            xmm_to[xmm_moves] = to[k];
            xmm_from[xmm_moves] = from[k];
            xmm_moves += to[k] >= 16 ? 1 : 0;
            to[general_moves] = to[k];
            from[general_moves] = from[k];
            general_moves += to[k] < 16 ? 1 : 0;
        }
        moves = general_moves;
        tier0_parallel_moves(writer, xmm_to, xmm_from, xmm_moves);
        for (uint32_t g = 0; g < aggregate_count; g += 1)
        {
            for (uint32_t k = 0; k < aggregates[g].count; k += 1)
            {
                const Tier0Part part = aggregates[g].parts[k];
                if (part.reg != BUSTER_TIER0_PART_STACK && part.reg >= 16)
                {
                    tier0_load_part(writer, part, aggregates[g].source, aggregates[g].temporaries[k]);
                }
            }
        }
    }
    // An address waits in its aggregate's last general part's register (its own, unless it is in another of its parts'
    // already), which loads last.
    const uint32_t written = tier0_call_written(function, call);
    for (uint32_t g = 0; g < aggregate_count; g += 1)
    {
        Tier0CallAggregate* aggregate = &aggregates[g];
        uint8_t own = BUSTER_TIER0_PART_STACK;
        bool in_own = false;
        for (uint32_t k = 0; k < aggregate->count; k += 1)
        {
            const uint8_t reg = aggregate->parts[k].reg;
            own = reg < 16 ? reg : own;
            in_own |= reg == aggregate->source;
        }
        if (own != BUSTER_TIER0_PART_STACK && (written >> aggregate->source & 1) != 0)
        {
            aggregate->holder = in_own ? aggregate->source : own;
            to[moves] = aggregate->holder;
            from[moves] = aggregate->source;
            moves += aggregate->holder != aggregate->source ? 1 : 0;
        }
    }
    return moves;
}

// Whether entry i is a memory class's, tier0_emit_x87's or tier0_emit_int128's (a call of class F80 is
// tier0_emit_call's): of a memory class by its key (its own class, or a compare's operands'), or a conversion from one
// (whose variant is its operand's class).
static bool tier0_memory_entry(const Tier0Entries* entries, uint32_t i)
{
    return tier0_class_memory(entries->key_classes[i]) || (entries->key_kinds[i] == BUSTER_TYPED_CONVERT && tier0_class_memory(entries->variants[i]));
}

// Whether a memory class's entry is a 128-bit integer's (tier0_emit_int128's) rather than long double's: no entry
// converts between the two.
static bool tier0_int128_entry(const Tier0Entries* entries, uint32_t i)
{
    return tier0_class_int128(entries->key_classes[i]) || (entries->key_kinds[i] == BUSTER_TYPED_CONVERT && tier0_class_int128(entries->variants[i]));
}

// What a 128-bit integer entry (tier0_emit_int128's) uses besides its slots: up to three scratch registers, from the
// depth registers its operands leave free (a 128-bit operand is in its slot, so the registers from the depth of its
// lowest such operand on, as far as 4), but those its fixed registers take (rax and rdx for MUL, rcx for a shift); the
// highest integer depth among them (-1 for none); and whether it saves rax, rdx or rcx in the fixed slots, where one
// holds a value below its operands. Both selections' tops and its emitter take it from tier0_int128_plan, so that they
// agree.
typedef struct Tier0Int128Plan Tier0Int128Plan;
struct Tier0Int128Plan
{
    uint8_t scratch[3];
    int32_t top;
    bool save_rax;
    bool save_rdx;
    bool save_rcx;
};

// The first `count` depth registers from depth `from` (as far as from + 3), but those in the register set `fixed`, into
// plan->scratch, with the highest depth taken in plan->top. A depth past the table takes rax in its place, with its depth
// as the top, past the table too: selection then refuses the function (TIER0_ERROR_DEPTH), whose code is never made.
static void tier0_int128_take(Tier0Int128Plan* plan, int32_t from, uint32_t fixed, uint32_t count)
{
    uint32_t taken = 0;
    for (int32_t d = from; d < from + 4 && taken < count; d += 1)
    {
        const bool in_table = d >= 0 && d < BUSTER_TIER0_INTEGER_DEPTHS;
        const uint8_t reg = in_table ? tier0_integer_depths[d] : X86_REGISTER_RAX;
        if (d >= 0 && (!in_table || (fixed >> reg & 1) == 0))
        {
            plan->scratch[taken] = reg;
            plan->top = d > plan->top ? d : plan->top;
            taken += 1;
        }
    }
}

// Entry i's plan (Tier0Int128Plan), from its kind and its depth d before it:
//   - CONST: the register at d where the low half is past a sign-extended imm32; DUP, LOAD and LOCAL: the one at d
//     (LOAD's address is at d − 1); STORE and SET: the one at d − 1 (the address is at d − 2); STORE_LOCAL and
//     SET_LOCAL: the one at d − 1, whose value is in its slot;
//   - MUL: two from d − 2, none of them rax or rdx, which hold values below the operands from depths 3 and 5 on (rax is
//     depth 0's register, rdx depth 2's);
//   - SHL and SHR: three from d − 2, none of them rcx (depth 1's register, its count's at depth 2, holding a value below
//     the operands from depth 4 on);
//   - EQ and NE: two from d − 2, the first the result's; ADD, SUB, AND, OR, XOR and the ordered compares: one from d − 2;
//   - CONVERT to or from an integer class: the integer's register, at d − 1; NEG, NOT and CONVERT between I128 and U128:
//     none.
static Tier0Int128Plan tier0_int128_plan(const Tier0Function* function, const Tier0Entries* entries, uint32_t i)
{
    const uint32_t kind = entries->key_kinds[i];
    const uint32_t class = entries->key_classes[i];
    const int32_t depth = entries->depths[i];
    Tier0Int128Plan plan = { { X86_REGISTER_RAX, X86_REGISTER_RAX, X86_REGISTER_RAX }, -1, false, false, false };
    if (kind == BUSTER_TYPED_CONST)
    {
        tier0_int128_take(&plan, depth, 0, tier0_fits_int32((int64_t)function->values[i]) ? 0 : 1);
    }
    else if (kind == BUSTER_TYPED_DUP || kind == BUSTER_TYPED_LOAD || kind == BUSTER_TYPED_LOCAL)
    {
        tier0_int128_take(&plan, depth, 0, 1);
    }
    else if (kind == BUSTER_TYPED_STORE || kind == BUSTER_TYPED_SET || kind == BUSTER_TYPED_STORE_LOCAL || kind == BUSTER_TYPED_SET_LOCAL)
    {
        tier0_int128_take(&plan, depth - 1, 0, 1);
    }
    else if (kind == BUSTER_TYPED_MUL)
    {
        tier0_int128_take(&plan, depth - 2, 1u << X86_REGISTER_RAX | 1u << X86_REGISTER_RDX, 2);
        plan.save_rax = depth > 2;
        plan.save_rdx = depth > 4;
    }
    else if (kind == BUSTER_TYPED_SHL || kind == BUSTER_TYPED_SHR)
    {
        tier0_int128_take(&plan, depth - 2, 1u << X86_REGISTER_RCX, 3);
        plan.save_rcx = depth > 3;
    }
    else if (kind == BUSTER_TYPED_EQ || kind == BUSTER_TYPED_NE)
    {
        tier0_int128_take(&plan, depth - 2, 0, 2);
    }
    else if (kind >= BUSTER_TYPED_ADD && kind <= BUSTER_TYPED_GE)
    {
        tier0_int128_take(&plan, depth - 2, 0, 1);
    }
    else if (kind == BUSTER_TYPED_CONVERT && !(tier0_class_int128(class) && tier0_class_int128(entries->variants[i])))
    {
        tier0_int128_take(&plan, depth - 1, 0, 1);
    }
    return plan;
}

// What a memory class's entry (tier0_memory_entry's, not a call) writes besides its slots, both selections', as the
// stacks' tops. Long double's: a compare's result and its scratch (the registers at depth − 2 and − 1), a conversion's
// integer result (− 1, and for a U64 the register at its depth too) and a conversion's floating result (the floating
// stack's depth). A 128-bit integer's: its plan's registers (tier0_int128_plan), and rax and rdx for MUL and rcx for a
// shift into `clobbers`, with `fixed` set where it saves one of them in the fixed slots. And into `slot_top`, the slot
// it writes at its own depth: a leaf's (CONST, DUP, LOCAL), and a long double's from a floating value, which moves to the
// integer stack. Every other slot an entry writes is one at a depth an operand's register had first, which a register's
// top counts: so the slots past the integer table's count (as many as there are slots) show in `slot_top` and the tops.
static void tier0_memory_tops(const Tier0Function* function, const Tier0Entries* entries, uint32_t i, int32_t* integer_top, int32_t* floating_top,
                              int32_t* slot_top, uint32_t* clobbers, bool* fixed)
{
    const int32_t depth = entries->depths[i];
    const uint32_t kind = entries->key_kinds[i];
    const uint32_t class = entries->key_classes[i];
    const bool leaf = kind == BUSTER_TYPED_CONST || kind == BUSTER_TYPED_DUP || kind == BUSTER_TYPED_LOCAL;
    const bool moved = kind == BUSTER_TYPED_CONVERT && tier0_class_x87(class) && tier0_class_floating(entries->variants[i]);
    int32_t integer = -1;
    int32_t floating = -1;
    *slot_top = (leaf || moved) && depth > *slot_top ? depth : *slot_top;
    if (tier0_int128_entry(entries, i))
    {
        const Tier0Int128Plan plan = tier0_int128_plan(function, entries, i);
        integer = plan.top;
        *clobbers |= kind == BUSTER_TYPED_MUL ? 1u << X86_REGISTER_RAX | 1u << X86_REGISTER_RDX : 0;
        *clobbers |= kind == BUSTER_TYPED_SHL || kind == BUSTER_TYPED_SHR ? 1u << X86_REGISTER_RCX : 0;
        *fixed |= plan.save_rax || plan.save_rdx || plan.save_rcx;
    }
    else
    {
        const bool from_x87 = kind == BUSTER_TYPED_CONVERT && !tier0_class_x87(class);
        integer = kind >= BUSTER_TYPED_EQ && kind <= BUSTER_TYPED_GE ? depth - 1 : -1;
        integer = from_x87 && tier0_class_integer(class) ? (class == BUSTER_TYPED_CLASS_U64 ? depth : depth - 1) : integer;
        floating = from_x87 && tier0_class_floating(class) ? (int32_t)entries->floating_depths[i] : -1;
    }
    *integer_top = integer > *integer_top ? integer : *integer_top;
    *floating_top = floating > *floating_top ? floating : *floating_top;
}

// A memory class's slot of integer depth `depth`, from rsp (or rbp with a frame pointer), as a memory operand of `form`.
static X86Instruction tier0_memory_slot(uint32_t form, const Tier0Frame* frame, uint32_t depth, int32_t offset)
{
    const int64_t displacement = (int64_t)frame->memory_area + BUSTER_TIER0_MEMORY_SLOT_BYTES * (int64_t)depth + offset + frame->bias;
    return tier0_instruction(form, 0, frame->base, BUSTER_X86_MEMORY_BASE, (int32_t)displacement, 0);
}

// A 128-bit integer's half in its slot (`half` 0 for the low one, 1 for the high one, 8 bytes on) as the memory operand of
// `form`, with `reg`.
static X86Instruction tier0_int128_half(uint32_t form, const Tier0Frame* frame, uint32_t depth, uint32_t half, uint8_t reg)
{
    X86Instruction record = tier0_memory_slot(form, frame, depth, 8 * (int32_t)half);
    record.reg = reg;
    return record;
}

// A red zone temporary below rsp as a memory operand of `form` (tier0_temporary's place), with `reg`.
static X86Instruction tier0_red_zone(uint32_t form, uint8_t reg, uint32_t t)
{
    return tier0_instruction(form, reg, X86_REGISTER_RSP, BUSTER_X86_MEMORY_BASE, tier0_temporary(t), 0);
}

// An x87 instruction on st(i).
static X86Instruction tier0_x87_register(uint32_t form, uint32_t i)
{
    return tier0_instruction(form, 0, (uint8_t)i, BUSTER_X86_REGISTER_DIRECT, 0, 0);
}

// A call: the depths below its arguments (and an indirect call's callee) saved, the stack arguments stored, the register
// ones moved into place, with an indirect call's callee into r11 (no argument's register, nor al's), al for a variadic
// call, the call, the result moved to its depth, and the saved depths restored.
//
// An aggregate argument (Tier0Function.argument_sizes) is its address: on the stack, its bytes are copied into its slots
// after the values' stores; in registers, its parts load from its bytes, after every value they would overwrite has
// moved:
//   1. the XMM moves, then the XMM parts from each address where its argument left it (the floating arguments' moves
//      have no cycles: their sources and destinations both rise with the arguments' order);
//   2. the general moves at once, with an address that some move or part would overwrite moved first into the register
//      of its aggregate's last general part, where it waits;
//   3. each aggregate's general parts, the one in the register that holds its address last.
// Without XMM parts, the moves of both kinds go at once, as a call without aggregates has them. A TEMPORARY part's bytes
// go to its red zone temporary with the copies (tier0_place_aggregates). The copies' scratch registers are each stack's
// first free one (tier0_select_scratch). A result object (call_result_objects) has its address in rdi for a hidden
// pointer, or gets the result's registers stored in it after the call. `sized` says whether the function has
// argument_sizes and `memory` whether it has values of the memory classes (Tier0Frame.memory), whose long doubles calls
// take and give: constants in the code for functions without them (tier0_emit_deferred_entry's), so that a function
// without aggregate arguments has its calls' loops over the values without a look at their sizes, and one without memory
// classes without a look for long doubles; the memory classes' code, out of line, takes `sized` from its function. A
// struct result in x87 registers, which stores into its object, is any function's.
static inline __attribute__((always_inline)) void tier0_emit_call(const Tier0Function* function, const Tier0Entries* entries, const Tier0Frame* frame,
                                                                  const Tier0Home* homes, uint32_t i, bool sized, bool memory, Tier0Writer* writer)
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
        tier0_spill(writer, frame, tier0_integer_depths[d], frame->save_area + 8 * d, false);
    }
    for (uint32_t d = 0; d < floating_below; d += 1)
    {
        tier0_spill(writer, frame, BUSTER_TIER0_XMM(d), frame->save_area + 8 * (saved + d), false);
    }
    uint8_t to[2 * BUSTER_TIER0_MAX_CALL_ARGUMENTS + 1];
    uint8_t from[2 * BUSTER_TIER0_MAX_CALL_ARGUMENTS + 1];
    uint32_t moves = 0;
    uint32_t integer = 0;
    uint32_t floating = 0;
    for (uint32_t a = 0; a < count; a += 1)
    {
        const uint32_t class = function->argument_classes[first + a];
        const AbiLocation location = function->argument_locations[first + a];
        const bool value = !sized || function->argument_sizes[first + a] == 0;
        const bool is_floating = tier0_class_floating(class);
        const uint32_t depth = arguments_base + integer;
        const uint32_t source = is_floating ? BUSTER_TIER0_XMM(floating_below + floating) : tier0_integer_depths[depth];
        integer += is_floating ? 0 : 1;
        floating += is_floating ? 1 : 0;
        if (value)
        {
            tier0_extend(writer, class, source);
            if (memory && location.kind == BUSTER_ABI_LOCATION_STACK && tier0_class_x87(class))
            {
                // A long double from its slot into its 16 bytes of the stack, of which it takes 10.
                tier0_put(writer, tier0_memory_slot(X86_FORM_FLD_M80, frame, depth, 0));
                tier0_put(writer, tier0_instruction(X86_FORM_FSTP_M80, 0, X86_REGISTER_RSP, BUSTER_X86_MEMORY_BASE, (int32_t)location.stack_offset, 0));
            }
            else if (location.kind == BUSTER_ABI_LOCATION_STACK)
            {
                const uint32_t form = is_floating ? (class == BUSTER_TYPED_CLASS_F32 ? X86_FORM_MOVSS_XM32_X : X86_FORM_MOVSD_XM64_X) : X86_FORM_MOV_RM64_R64;
                tier0_put(writer, tier0_instruction(form, (uint8_t)(source & 15), X86_REGISTER_RSP, BUSTER_X86_MEMORY_BASE, (int32_t)location.stack_offset, 0));
            }
            else if (location.kind == BUSTER_ABI_LOCATION_REGISTERS)
            {
                to[moves] = tier0_abi_register(location.registers[0]);
                from[moves] = (uint8_t)source;
                moves += 1;
            }
        }
    }
    if (indirect)
    {
        to[moves] = X86_REGISTER_R11;
        from[moves] = tier0_integer_depths[below];
        moves += 1;
    }
    Tier0CallAggregate placed[BUSTER_TIER0_MAX_CALL_ARGUMENTS];
    const uint32_t aggregate_count = sized ? tier0_place_aggregates(function, entries, i, arguments_base, floating_below, placed, writer) : 0;
    moves = aggregate_count != 0 ? tier0_aggregate_moves(function, call, placed, aggregate_count, to, from, moves, writer) : moves;
    tier0_parallel_moves(writer, to, from, moves);
    for (uint32_t g = 0; g < aggregate_count; g += 1)
    {
        const Tier0CallAggregate* aggregate = &placed[g];
        for (uint32_t pass = 0; pass < 2; pass += 1)
        {
            for (uint32_t k = 0; k < aggregate->count; k += 1)
            {
                const Tier0Part part = aggregate->parts[k];
                if (part.reg < 16 && (part.reg == aggregate->holder) == (pass == 1))
                {
                    tier0_load_part(writer, part, aggregate->holder, aggregate->temporaries[k]);
                }
            }
        }
    }
    const uint32_t object = function->call_result_objects != 0 ? function->call_result_objects[call] : 0;
    const AbiReturn returned = function->calls[call].result;
    if (object != 0 && returned.kind == BUSTER_ABI_RESULT_MEMORY)
    {
        const Tier0Home home = homes[object - 1];
        tier0_put(writer, tier0_instruction(X86_FORM_LEA_R64_M, X86_REGISTER_RDI, home.reg, home.mode, home.displacement, 0));
    }
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
    if (object != 0 && returned.kind == BUSTER_ABI_RESULT_REGISTERS)
    {
        const Tier0Home home = homes[object - 1];
        const bool high = (returned.flags & BUSTER_ABI_FLAG_HIGH) != 0;
        const uint32_t parts = tier0_x87_parts(returned);
        // An x87 result's parts popped in order, st0's at 0 and st1's 16 bytes on.
        for (uint32_t k = 0; k < parts; k += 1)
        {
            tier0_put(writer, tier0_instruction(X86_FORM_FSTP_M80, 0, home.reg, home.mode, home.displacement + 16 * (int32_t)k, 0));
        }
        for (uint32_t k = 0; k < 2 && parts == 0; k += 1)
        {
            if (returned.registers[k] != BUSTER_ABI_REGISTER_NONE)
            {
                const uint32_t offset = high ? 8 : 8 * k;
                tier0_store_part(writer, tier0_abi_register(returned.registers[k]), tier0_result_part_bytes(returned), home, offset);
            }
        }
    }
    else if (memory && tier0_class_x87(function->classes[i]))
    {
        // A long double result from st0 into its depth's slot.
        tier0_put(writer, tier0_memory_slot(X86_FORM_FSTP_M80, frame, below, 0));
    }
    else if (result == 1 && below != 0)
    {
        tier0_move(writer, tier0_integer_depths[below], X86_REGISTER_RAX);
    }
    else if (result == 2 && floating_below != 0)
    {
        tier0_move(writer, BUSTER_TIER0_XMM(floating_below), BUSTER_TIER0_XMM(0));
    }
    for (uint32_t d = 0; d < saved; d += 1)
    {
        tier0_spill(writer, frame, tier0_integer_depths[d], frame->save_area + 8 * d, true);
    }
    for (uint32_t d = 0; d < floating_below; d += 1)
    {
        tier0_spill(writer, frame, BUSTER_TIER0_XMM(d), frame->save_area + 8 * (saved + d), true);
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
        tier0_spill(writer, frame, X86_REGISTER_RAX, rax_slot, false);
    }
    if (save_rdx)
    {
        tier0_spill(writer, frame, X86_REGISTER_RDX, rdx_slot, false);
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
        divide.rm = frame->base;
        divide.mode = BUSTER_X86_MEMORY_BASE;
        divide.displacement = (int32_t)((int64_t)(divisor == X86_REGISTER_RAX ? rax_slot : rdx_slot) + frame->bias);
    }
    tier0_put(writer, divide);
    const uint32_t result = kind == BUSTER_TYPED_MOD ? X86_REGISTER_RDX : X86_REGISTER_RAX;
    if (dividend != result)
    {
        tier0_move(writer, dividend, result);
    }
    if (save_rax)
    {
        tier0_spill(writer, frame, X86_REGISTER_RAX, rax_slot, true);
    }
    if (save_rdx)
    {
        tier0_spill(writer, frame, X86_REGISTER_RDX, rdx_slot, true);
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
        tier0_spill(writer, frame, X86_REGISTER_RCX, frame->fixed_area, false);
        tier0_put(writer, tier0_instruction(X86_FORM_MOV_R32_RM32, X86_REGISTER_RCX, (uint8_t)count, 0, 0, 0));
        tier0_put(writer, tier0_instruction(form, 0, (uint8_t)value, 0, 0, 0));
        tier0_spill(writer, frame, X86_REGISTER_RCX, frame->fixed_area, true);
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
    tier0_copy(writer, tier0_integer_depths[depth - 2], 0, tier0_integer_depths[depth - 1], 0, size, size < 16 ? tier0_integer_depths[depth] : 0,
               entries->floating_depths[i]);
}

// ALLOCA's records, and STACK_SAVE's, STACK_RESTORE's and FRAME_ADDRESS's (tier0_emit_stack's).
#define BUSTER_TIER0_ALLOCA_RECORDS 4
#define BUSTER_TIER0_STACK_RECORDS 1

// The stack's entries, in a function with a frame pointer: ALLOCA's count (at depth − 1) rounded up to 16 (to a
// realigned frame's alignment, so that rsp keeps it) and taken from rsp, and the allocation's address in its register,
// above the outgoing area (add, and, sub and lea, or mov with no area); STACK_SAVE's rsp into the register at its depth,
// STACK_RESTORE's back from the one at depth − 1, and FRAME_ADDRESS's rbp into the register at its depth.
static void tier0_emit_stack(const Tier0Entries* entries, const Tier0Frame* frame, uint32_t i, Tier0Writer* writer)
{
    const uint32_t kind = entries->key_kinds[i];
    const uint32_t depth = entries->depths[i];
    if (kind == BUSTER_TYPED_ALLOCA)
    {
        const uint8_t count = tier0_integer_depths[depth - 1];
        const uint32_t log2 = frame->alignment_log2 != 0 ? frame->alignment_log2 : BUSTER_TIER0_FRAME_ALIGNMENT_LOG2;
        const int32_t alignment = (int32_t)(UINT32_C(1) << log2);
        tier0_put(writer, tier0_instruction(log2 <= 7 ? X86_FORM_ADD_RM64_IMM8 : X86_FORM_ADD_RM64_IMM32, 0, count, BUSTER_X86_REGISTER_DIRECT, 0, alignment - 1));
        tier0_put(writer, tier0_instruction(log2 <= 7 ? X86_FORM_AND_RM64_IMM8 : X86_FORM_AND_RM64_IMM32, 0, count, BUSTER_X86_REGISTER_DIRECT, 0, -alignment));
        tier0_put(writer, tier0_instruction(X86_FORM_SUB_RM64_R64, count, X86_REGISTER_RSP, BUSTER_X86_REGISTER_DIRECT, 0, 0));
        if (frame->outgoing != 0)
        {
            tier0_put(writer, tier0_instruction(X86_FORM_LEA_R64_M, count, X86_REGISTER_RSP, BUSTER_X86_MEMORY_BASE, (int32_t)frame->outgoing, 0));
        }
        else
        {
            tier0_put(writer, tier0_instruction(X86_FORM_MOV_R64_RM64, count, X86_REGISTER_RSP, BUSTER_X86_REGISTER_DIRECT, 0, 0));
        }
    }
    else if (kind == BUSTER_TYPED_STACK_SAVE || kind == BUSTER_TYPED_FRAME_ADDRESS)
    {
        const uint8_t source = kind == BUSTER_TYPED_STACK_SAVE ? X86_REGISTER_RSP : X86_REGISTER_RBP;
        tier0_put(writer, tier0_instruction(X86_FORM_MOV_R64_RM64, tier0_integer_depths[depth], source, BUSTER_X86_REGISTER_DIRECT, 0, 0));
    }
    else
    {
        tier0_put(writer, tier0_instruction(X86_FORM_MOV_R64_RM64, X86_REGISTER_RSP, tier0_integer_depths[depth - 1], BUSTER_X86_REGISTER_DIRECT, 0, 0));
    }
}

// The x87 control word's rounding toward zero around a store to an integer, which C's conversions need and the x87's
// default rounding to nearest doesn't give (fisttp, which would, is SSE3's, past the baseline): before, the control word
// saved in red zone temporary 0 and loaded with RC 11 through `scratch` and temporary 1; after, temporary 0 loaded back.
// A function must keep the control word as it found it, and keeps it but for these records.
#define BUSTER_TIER0_X87_TRUNCATE_RECORDS 5
static void tier0_x87_truncate(Tier0Writer* writer, uint8_t scratch)
{
    tier0_put(writer, tier0_red_zone(X86_FORM_FNSTCW_M16, 0, 0));
    tier0_put(writer, tier0_red_zone(X86_FORM_MOVZX_R32_RM16, scratch, 0));
    tier0_put(writer, tier0_instruction(X86_FORM_OR_RM32_IMM32, 0, scratch, BUSTER_X86_REGISTER_DIRECT, 0, 0x0c00));
    tier0_put(writer, tier0_red_zone(X86_FORM_MOV_RM16_R16, scratch, 1));
    tier0_put(writer, tier0_red_zone(X86_FORM_FLDCW_M16, 0, 1));
}

// A long double entry's records (tier0_memory_entry's): its operands loaded onto the x87 stack from their slots (an F80
// operand at integer depth d in slot d), the x87 operation, and its result stored into its own slot (or its register, for
// an integer or a floating class), leaving the x87 stack empty. With d the entry's integer depth:
//   - CONST: its constant's 10 bytes stored into slot d as immediates;
//   - LOAD, STORE, SET: the value through the address in the register at d − 1 (or d − 2, a store's), STORE's kept in
//     slot d − 2 too; DUP: slot d − 1 copied into slot d; LOCAL: its object's 10 bytes into slot d; STORE_LOCAL and
//     SET_LOCAL: slot d − 1 into its object; NEG: fchs;
//   - ADD, SUB, MUL, DIV: slots d − 2 and d − 1 into st1 and st0, then faddp (fsubp, fmulp, fdivp) st(1), st(0), whose
//     st(1) − st(0) is the first operand less the last, into slot d − 2;
//   - the compares: fucomip of the operands in the order that makes each condition one that unordered operands fail
//     (a, b as st0, st1 for >, >=; b, a for <, <=, which are b > a and b >= a), as SSE's ucomisd has them, into the
//     register at d − 2 (EQ and NE with the parity flag, through the register at d − 1);
//   - CONVERT to F80: a floating value through red zone temporary 0 (fld), an integer one too (fild), widened first: a
//     narrow one to 32 bits, a U32 to 64, and a U64 read as an I64 and 2^64 added when its top bit is set (from a float
//     constant the top bit makes, which is exact in a 64-bit significand); into slot d for a floating operand (the value
//     moves to the integer stack, at its depth), d − 1 for an integer one;
//   - CONVERT from F80: to float or double, through temporary 0 into the floating stack's free register; to an integer,
//     fistp into temporary 2 under the control word's truncation (tier0_x87_truncate), and for a U64 a value of 2^63 or
//     more less 2^63 first, its top bit set after (fcmovbe chooses what to take away, setbe and shl the bit, both from one
//     fucomi with 2^63, through temporary 3 and the register at d).
static void tier0_emit_x87(const Tier0Function* function, const Tier0Entries* entries, const Tier0Frame* frame, const Tier0Home* homes, uint32_t i,
                           Tier0Writer* writer)
{
    const uint32_t kind = entries->key_kinds[i];
    const uint32_t class = entries->key_classes[i];
    const uint32_t depth = entries->depths[i];
    const uint32_t floating = entries->floating_depths[i];
    if (kind == BUSTER_TYPED_CONST)
    {
        const uint64_t constant = function->values[i];
        const uint64_t significand = function->x87_constants[2 * constant];
        const uint64_t top = function->x87_constants[2 * constant + 1];
        X86Instruction low = tier0_memory_slot(X86_FORM_MOV_RM32_IMM32, frame, depth, 0);
        X86Instruction high = tier0_memory_slot(X86_FORM_MOV_RM32_IMM32, frame, depth, 4);
        X86Instruction exponent = tier0_memory_slot(X86_FORM_MOV_RM16_IMM16, frame, depth, 8);
        low.immediate = (int32_t)(uint32_t)significand;
        high.immediate = (int32_t)(uint32_t)(significand >> 32);
        exponent.immediate = (int32_t)(uint16_t)top;
        tier0_put(writer, low);
        tier0_put(writer, high);
        tier0_put(writer, exponent);
    }
    else if (kind == BUSTER_TYPED_LOAD)
    {
        tier0_put(writer, tier0_instruction(X86_FORM_FLD_M80, 0, tier0_integer_depths[depth - 1], BUSTER_X86_MEMORY_BASE, 0, 0));
        tier0_put(writer, tier0_memory_slot(X86_FORM_FSTP_M80, frame, depth - 1, 0));
    }
    else if (kind == BUSTER_TYPED_STORE || kind == BUSTER_TYPED_SET)
    {
        tier0_put(writer, tier0_memory_slot(X86_FORM_FLD_M80, frame, depth - 1, 0));
        if (kind == BUSTER_TYPED_STORE)
        {
            tier0_put(writer, tier0_x87_register(X86_FORM_FLD_ST, 0));
        }
        tier0_put(writer, tier0_instruction(X86_FORM_FSTP_M80, 0, tier0_integer_depths[depth - 2], BUSTER_X86_MEMORY_BASE, 0, 0));
        if (kind == BUSTER_TYPED_STORE)
        {
            tier0_put(writer, tier0_memory_slot(X86_FORM_FSTP_M80, frame, depth - 2, 0));
        }
    }
    else if (kind == BUSTER_TYPED_DUP)
    {
        tier0_put(writer, tier0_memory_slot(X86_FORM_FLD_M80, frame, depth - 1, 0));
        tier0_put(writer, tier0_memory_slot(X86_FORM_FSTP_M80, frame, depth, 0));
    }
    else if (kind == BUSTER_TYPED_LOCAL || kind == BUSTER_TYPED_STORE_LOCAL || kind == BUSTER_TYPED_SET_LOCAL)
    {
        // The object's first 10 bytes into slot d, or slot d − 1's into them (tier0_value_object's).
        const Tier0Home home = homes[function->values[i]];
        const bool local = kind == BUSTER_TYPED_LOCAL;
        tier0_put(writer, local ? tier0_instruction(X86_FORM_FLD_M80, 0, home.reg, home.mode, home.displacement, 0)
                                : tier0_memory_slot(X86_FORM_FLD_M80, frame, depth - 1, 0));
        tier0_put(writer, local ? tier0_memory_slot(X86_FORM_FSTP_M80, frame, depth, 0)
                                : tier0_instruction(X86_FORM_FSTP_M80, 0, home.reg, home.mode, home.displacement, 0));
    }
    else if (kind == BUSTER_TYPED_NEG)
    {
        tier0_put(writer, tier0_memory_slot(X86_FORM_FLD_M80, frame, depth - 1, 0));
        tier0_put(writer, tier0_instruction(X86_FORM_FCHS, 0, 0, 0, 0, 0));
        tier0_put(writer, tier0_memory_slot(X86_FORM_FSTP_M80, frame, depth - 1, 0));
    }
    else if (kind >= BUSTER_TYPED_ADD && kind <= BUSTER_TYPED_DIV)
    {
        static const uint16_t operations[4] = { X86_FORM_FADDP_ST_ST0, X86_FORM_FSUBP_ST_ST0, X86_FORM_FMULP_ST_ST0, X86_FORM_FDIVP_ST_ST0 };
        tier0_put(writer, tier0_memory_slot(X86_FORM_FLD_M80, frame, depth - 2, 0));
        tier0_put(writer, tier0_memory_slot(X86_FORM_FLD_M80, frame, depth - 1, 0));
        tier0_put(writer, tier0_x87_register(operations[kind - BUSTER_TYPED_ADD], 1));
        tier0_put(writer, tier0_memory_slot(X86_FORM_FSTP_M80, frame, depth - 2, 0));
    }
    else if (kind >= BUSTER_TYPED_EQ && kind <= BUSTER_TYPED_GE)
    {
        // st0 the first operand (a, at d − 2) for EQ, NE, GT and GE, the last (b) for LT and LE.
        const bool swapped = kind == BUSTER_TYPED_LT || kind == BUSTER_TYPED_LE;
        const uint8_t result = tier0_integer_depths[depth - 2];
        const uint8_t scratch = tier0_integer_depths[depth - 1];
        tier0_put(writer, tier0_memory_slot(X86_FORM_FLD_M80, frame, swapped ? depth - 2 : depth - 1, 0));
        tier0_put(writer, tier0_memory_slot(X86_FORM_FLD_M80, frame, swapped ? depth - 1 : depth - 2, 0));
        tier0_put(writer, tier0_x87_register(X86_FORM_FUCOMIP_ST0_ST, 1));
        tier0_put(writer, tier0_x87_register(X86_FORM_FSTP_ST, 0));
        if (kind == BUSTER_TYPED_EQ || kind == BUSTER_TYPED_NE)
        {
            const bool equal = kind == BUSTER_TYPED_EQ;
            tier0_put(writer, tier0_instruction(equal ? X86_FORM_SETE_RM8 : X86_FORM_SETNE_RM8, 0, result, BUSTER_X86_REGISTER_DIRECT, 0, 0));
            tier0_put(writer, tier0_instruction(equal ? X86_FORM_SETNP_RM8 : X86_FORM_SETP_RM8, 0, scratch, BUSTER_X86_REGISTER_DIRECT, 0, 0));
            tier0_put(writer, tier0_instruction(equal ? X86_FORM_AND_RM8_R8 : X86_FORM_OR_RM8_R8, scratch, result, BUSTER_X86_REGISTER_DIRECT, 0, 0));
        }
        else
        {
            const bool strict = kind == BUSTER_TYPED_GT || kind == BUSTER_TYPED_LT;
            tier0_put(writer, tier0_instruction(strict ? X86_FORM_SETA_RM8 : X86_FORM_SETAE_RM8, 0, result, BUSTER_X86_REGISTER_DIRECT, 0, 0));
        }
        tier0_put(writer, tier0_instruction(X86_FORM_MOVZX_R32_RM8, result, result, BUSTER_X86_REGISTER_DIRECT, 0, 0));
    }
    else if (tier0_class_x87(class))
    {
        // CONVERT to F80 from the operand's class (the variant).
        const uint32_t from = entries->variants[i];
        const uint8_t operand = tier0_integer_depths[depth != 0 ? depth - 1 : 0];
        if (from == BUSTER_TYPED_CLASS_F32 || from == BUSTER_TYPED_CLASS_F64)
        {
            const bool single = from == BUSTER_TYPED_CLASS_F32;
            tier0_put(writer, tier0_red_zone(single ? X86_FORM_MOVSS_XM32_X : X86_FORM_MOVSD_XM64_X, (uint8_t)(floating - 1), 0));
            tier0_put(writer, tier0_red_zone(single ? X86_FORM_FLD_M32 : X86_FORM_FLD_M64, 0, 0));
            tier0_put(writer, tier0_memory_slot(X86_FORM_FSTP_M80, frame, depth, 0));
        }
        else if (!tier0_class_x87(from))
        {
            const bool wide = from == BUSTER_TYPED_CLASS_U32 || tier0_class_bytes(from) == 8;
            if (tier0_class_bytes(from) < 4)
            {
                tier0_put(writer, tier0_instruction(tier0_load_form(from), operand, operand, BUSTER_X86_REGISTER_DIRECT, 0, 0));
            }
            else if (from == BUSTER_TYPED_CLASS_U32)
            {
                tier0_put(writer, tier0_instruction(X86_FORM_MOV_R32_RM32, operand, operand, BUSTER_X86_REGISTER_DIRECT, 0, 0));
            }
            tier0_put(writer, tier0_red_zone(wide ? X86_FORM_MOV_RM64_R64 : X86_FORM_MOV_RM32_R32, operand, 0));
            tier0_put(writer, tier0_red_zone(wide ? X86_FORM_FILD_M64 : X86_FORM_FILD_M32, 0, 0));
            if (from == BUSTER_TYPED_CLASS_U64)
            {
                // 2^64 as a float (0x5f800000) where the top bit is set, else 0: sar makes the bit a mask.
                tier0_put(writer, tier0_instruction(X86_FORM_SAR_RM64_IMM8, 0, operand, BUSTER_X86_REGISTER_DIRECT, 0, 63));
                tier0_put(writer, tier0_instruction(X86_FORM_AND_RM32_IMM32, 0, operand, BUSTER_X86_REGISTER_DIRECT, 0, 0x5f800000));
                tier0_put(writer, tier0_red_zone(X86_FORM_MOV_RM32_R32, operand, 1));
                tier0_put(writer, tier0_red_zone(X86_FORM_FADD_M32, 0, 1));
            }
            tier0_put(writer, tier0_memory_slot(X86_FORM_FSTP_M80, frame, depth - 1, 0));
        }
    }
    else if (tier0_class_floating(class))
    {
        // CONVERT from F80 to float or double.
        const bool single = class == BUSTER_TYPED_CLASS_F32;
        tier0_put(writer, tier0_memory_slot(X86_FORM_FLD_M80, frame, depth - 1, 0));
        tier0_put(writer, tier0_red_zone(single ? X86_FORM_FSTP_M32 : X86_FORM_FSTP_M64, 0, 0));
        tier0_put(writer, tier0_red_zone(single ? X86_FORM_MOVSS_X_XM32 : X86_FORM_MOVSD_X_XM64, (uint8_t)floating, 0));
    }
    else
    {
        // CONVERT from F80 to an integer class, truncating.
        const uint8_t result = tier0_integer_depths[depth - 1];
        tier0_x87_truncate(writer, result);
        tier0_put(writer, tier0_memory_slot(X86_FORM_FLD_M80, frame, depth - 1, 0));
        if (class == BUSTER_TYPED_CLASS_U64)
        {
            const uint8_t high = tier0_integer_depths[depth];
            X86Instruction limit = tier0_red_zone(X86_FORM_MOV_RM32_IMM32, 0, 3);
            limit.immediate = 0x5f000000;
            tier0_put(writer, limit);
            tier0_put(writer, tier0_red_zone(X86_FORM_FLD_M32, 0, 3));
            tier0_put(writer, tier0_x87_register(X86_FORM_FUCOMI_ST0_ST, 1));
            tier0_put(writer, tier0_instruction(X86_FORM_SETBE_RM8, 0, high, BUSTER_X86_REGISTER_DIRECT, 0, 0));
            tier0_put(writer, tier0_instruction(X86_FORM_FLDZ, 0, 0, 0, 0, 0));
            tier0_put(writer, tier0_x87_register(X86_FORM_FCMOVBE_ST0_ST, 1));
            tier0_put(writer, tier0_x87_register(X86_FORM_FSTP_ST, 1));
            tier0_put(writer, tier0_x87_register(X86_FORM_FSUBP_ST_ST0, 1));
            tier0_put(writer, tier0_red_zone(X86_FORM_FISTP_M64, 0, 2));
            tier0_put(writer, tier0_red_zone(X86_FORM_FLDCW_M16, 0, 0));
            tier0_put(writer, tier0_instruction(X86_FORM_MOVZX_R32_RM8, high, high, BUSTER_X86_REGISTER_DIRECT, 0, 0));
            tier0_put(writer, tier0_instruction(X86_FORM_SHL_RM64_IMM8, 0, high, BUSTER_X86_REGISTER_DIRECT, 0, 63));
            tier0_put(writer, tier0_red_zone(X86_FORM_XOR_R64_RM64, high, 2));
            tier0_put(writer, tier0_instruction(X86_FORM_MOV_R64_RM64, result, high, BUSTER_X86_REGISTER_DIRECT, 0, 0));
        }
        else
        {
            tier0_put(writer, tier0_red_zone(X86_FORM_FISTP_M64, 0, 2));
            tier0_put(writer, tier0_red_zone(X86_FORM_FLDCW_M16, 0, 0));
            tier0_put(writer, tier0_red_zone(X86_FORM_MOV_R64_RM64, result, 2));
        }
    }
}

// How many records tier0_emit_x87 writes for entry i, without writing them (tier0_count_deferred's).
static uint32_t tier0_count_x87(const Tier0Entries* entries, uint32_t i)
{
    const uint32_t kind = entries->key_kinds[i];
    const uint32_t class = entries->key_classes[i];
    uint32_t count = 0;
    if (kind == BUSTER_TYPED_CONST || kind == BUSTER_TYPED_NEG)
    {
        count = 3;
    }
    else if (kind == BUSTER_TYPED_LOAD || kind == BUSTER_TYPED_SET || kind == BUSTER_TYPED_DUP || kind == BUSTER_TYPED_LOCAL || kind == BUSTER_TYPED_STORE_LOCAL ||
             kind == BUSTER_TYPED_SET_LOCAL)
    {
        count = 2;
    }
    else if (kind == BUSTER_TYPED_STORE || (kind >= BUSTER_TYPED_ADD && kind <= BUSTER_TYPED_DIV))
    {
        count = 4;
    }
    else if (kind >= BUSTER_TYPED_EQ && kind <= BUSTER_TYPED_GE)
    {
        count = kind == BUSTER_TYPED_EQ || kind == BUSTER_TYPED_NE ? 8 : 6;
    }
    else if (tier0_class_x87(class))
    {
        const uint32_t from = entries->variants[i];
        count = tier0_class_x87(from) ? 0 : tier0_class_floating(from) ? 3 : from == BUSTER_TYPED_CLASS_U64 ? 7 : tier0_class_bytes(from) < 4 || from == BUSTER_TYPED_CLASS_U32 ? 4 : 3;
    }
    else if (tier0_class_floating(class))
    {
        count = 3;
    }
    else
    {
        count = BUSTER_TIER0_X87_TRUNCATE_RECORDS + 1 + (class == BUSTER_TYPED_CLASS_U64 ? 14 : 3);
    }
    return count;
}

// A 128-bit integer entry's records, in its plan's registers (tier0_int128_plan; s0, s1 and s2 below): its operands from
// their slots (an I128 or U128 operand at integer depth d in slot d, its low half at 0 and its high half at 8), its result
// into its own slot, or for an integer result, its register. With d the entry's integer depth:
//   - CONST: the value's halves stored into slot d as sign-extended immediates, the low one through s0 (movabs) where it
//     is past one; DUP: slot d − 1 copied into slot d through s0; LOAD: the 16 bytes at the address in the register at
//     d − 1 into slot d − 1, through s0; STORE and SET: slot d − 1 to the address in the register at d − 2, through s0,
//     STORE's into slot d − 2 too; LOCAL: its object's 16 bytes into slot d, through s0; STORE_LOCAL and SET_LOCAL: slot
//     d − 1 into its object, through s0;
//   - NEG: the low half's neg, the carry added into the high half (adc 0), then its neg, in place; NOT: each half's not;
//   - ADD, SUB, AND, OR, XOR: each half of the last operand through s0 into the first's, in place, the high halves with
//     the carry (adc, sbb), which mov leaves alone;
//   - MUL: the cross products' low halves (imul, into s0 and s1) summed, the low halves' whole product (mul, into rdx and
//     rax), its high half plus the sum, into slot d − 2; rax and rdx saved in the fixed slots where they hold values;
//   - SHL and SHR: the value's halves into s0 (low) and s1 (high), shifted by cl as halves (shld or shrd, then shl, shr or
//     sar), then for a count of 64 or more (test cl, 64) the halves moved up or down and the vacated one filled from s2
//     (zero, or for a signed SHR the sign, sar 63, which neither shift by cl changes), by cmovne; the count moved into
//     rcx first, and rcx saved in a fixed slot where it holds a value; the shifts by cl take the count's low 6 bits;
//   - EQ and NE: the halves' differences (xor) ORed into s0, the result's register at d − 2, then sete or setne;
//   - LT and GE: the first operand less the last as a cmp of the low halves and an sbb of the high ones, into s0, then
//     setl or setge (setb or setae for U128); GT and LE: LT and GE with the operands swapped;
//   - CONVERT from an integer class at d − 1: the value sign- or zero-extended to 64 bits in its register, stored as the
//     low half, then its sign (sar 63) or zero as the high half; to an integer class, the low half loaded into the
//     register at d − 1, whose low bits the class keeps; between I128 and U128, nothing.
static void tier0_emit_int128(const Tier0Function* function, const Tier0Entries* entries, const Tier0Frame* frame, const Tier0Home* homes, uint32_t i,
                              Tier0Writer* writer)
{
    const uint32_t kind = entries->key_kinds[i];
    const uint32_t class = entries->key_classes[i];
    const uint32_t depth = entries->depths[i];
    const Tier0Int128Plan plan = tier0_int128_plan(function, entries, i);
    const uint8_t s0 = plan.scratch[0];
    const uint8_t s1 = plan.scratch[1];
    const uint8_t s2 = plan.scratch[2];
    const uint8_t direct = BUSTER_X86_REGISTER_DIRECT;
    if (kind == BUSTER_TYPED_CONST)
    {
        const uint64_t value = function->values[i];
        X86Instruction low = tier0_memory_slot(X86_FORM_MOV_RM64_IMM32, frame, depth, 0);
        X86Instruction high = tier0_memory_slot(X86_FORM_MOV_RM64_IMM32, frame, depth, 8);
        high.immediate = class == BUSTER_TYPED_CLASS_I128 && (int64_t)value < 0 ? -1 : 0;
        if (tier0_fits_int32((int64_t)value))
        {
            low.immediate = (int32_t)value;
            tier0_put(writer, low);
        }
        else
        {
            tier0_put(writer, tier0_instruction(X86_FORM_MOV_R64_IMM64, 0, s0, direct, (int32_t)(value >> 32), (int32_t)(uint32_t)value));
            tier0_put(writer, tier0_int128_half(X86_FORM_MOV_RM64_R64, frame, depth, 0, s0));
        }
        tier0_put(writer, high);
    }
    else if (kind == BUSTER_TYPED_DUP || kind == BUSTER_TYPED_LOAD)
    {
        const uint8_t address = tier0_integer_depths[depth - 1];
        for (uint32_t half = 0; half < 2; half += 1)
        {
            if (kind == BUSTER_TYPED_DUP)
            {
                tier0_put(writer, tier0_int128_half(X86_FORM_MOV_R64_RM64, frame, depth - 1, half, s0));
            }
            else
            {
                tier0_put(writer, tier0_instruction(X86_FORM_MOV_R64_RM64, s0, address, BUSTER_X86_MEMORY_BASE, 8 * (int32_t)half, 0));
            }
            tier0_put(writer, tier0_int128_half(X86_FORM_MOV_RM64_R64, frame, kind == BUSTER_TYPED_DUP ? depth : depth - 1, half, s0));
        }
    }
    else if (kind == BUSTER_TYPED_STORE || kind == BUSTER_TYPED_SET)
    {
        const uint8_t address = tier0_integer_depths[depth - 2];
        for (uint32_t half = 0; half < 2; half += 1)
        {
            tier0_put(writer, tier0_int128_half(X86_FORM_MOV_R64_RM64, frame, depth - 1, half, s0));
            tier0_put(writer, tier0_instruction(X86_FORM_MOV_RM64_R64, s0, address, BUSTER_X86_MEMORY_BASE, 8 * (int32_t)half, 0));
            if (kind == BUSTER_TYPED_STORE)
            {
                tier0_put(writer, tier0_int128_half(X86_FORM_MOV_RM64_R64, frame, depth - 2, half, s0));
            }
        }
    }
    else if (kind == BUSTER_TYPED_LOCAL || kind == BUSTER_TYPED_STORE_LOCAL || kind == BUSTER_TYPED_SET_LOCAL)
    {
        // The object's first 16 bytes into slot d, or slot d − 1's into them (tier0_value_object's).
        const Tier0Home home = homes[function->values[i]];
        const bool local = kind == BUSTER_TYPED_LOCAL;
        for (uint32_t half = 0; half < 2; half += 1)
        {
            const int32_t displacement = home.displacement + 8 * (int32_t)half;
            tier0_put(writer, local ? tier0_instruction(X86_FORM_MOV_R64_RM64, s0, home.reg, home.mode, displacement, 0)
                                    : tier0_int128_half(X86_FORM_MOV_R64_RM64, frame, depth - 1, half, s0));
            tier0_put(writer, local ? tier0_int128_half(X86_FORM_MOV_RM64_R64, frame, depth, half, s0)
                                    : tier0_instruction(X86_FORM_MOV_RM64_R64, s0, home.reg, home.mode, displacement, 0));
        }
    }
    else if (kind == BUSTER_TYPED_NEG)
    {
        X86Instruction carry = tier0_memory_slot(X86_FORM_ADC_RM64_IMM8, frame, depth - 1, 8);
        carry.immediate = 0;
        tier0_put(writer, tier0_memory_slot(X86_FORM_NEG_RM64, frame, depth - 1, 0));
        tier0_put(writer, carry);
        tier0_put(writer, tier0_memory_slot(X86_FORM_NEG_RM64, frame, depth - 1, 8));
    }
    else if (kind == BUSTER_TYPED_NOT)
    {
        tier0_put(writer, tier0_memory_slot(X86_FORM_NOT_RM64, frame, depth - 1, 0));
        tier0_put(writer, tier0_memory_slot(X86_FORM_NOT_RM64, frame, depth - 1, 8));
    }
    else if (kind == BUSTER_TYPED_MUL)
    {
        if (plan.save_rax)
        {
            tier0_spill(writer, frame, X86_REGISTER_RAX, frame->fixed_area, false);
        }
        if (plan.save_rdx)
        {
            tier0_spill(writer, frame, X86_REGISTER_RDX, frame->fixed_area + 8, false);
        }
        tier0_put(writer, tier0_int128_half(X86_FORM_MOV_R64_RM64, frame, depth - 2, 1, s0));
        tier0_put(writer, tier0_int128_half(X86_FORM_IMUL_R64_RM64, frame, depth - 1, 0, s0));
        tier0_put(writer, tier0_int128_half(X86_FORM_MOV_R64_RM64, frame, depth - 1, 1, s1));
        tier0_put(writer, tier0_int128_half(X86_FORM_IMUL_R64_RM64, frame, depth - 2, 0, s1));
        tier0_put(writer, tier0_instruction(X86_FORM_ADD_R64_RM64, s0, s1, direct, 0, 0));
        tier0_put(writer, tier0_int128_half(X86_FORM_MOV_R64_RM64, frame, depth - 2, 0, X86_REGISTER_RAX));
        tier0_put(writer, tier0_memory_slot(X86_FORM_MUL_RM64, frame, depth - 1, 0));
        tier0_put(writer, tier0_instruction(X86_FORM_ADD_R64_RM64, X86_REGISTER_RDX, s0, direct, 0, 0));
        tier0_put(writer, tier0_int128_half(X86_FORM_MOV_RM64_R64, frame, depth - 2, 0, X86_REGISTER_RAX));
        tier0_put(writer, tier0_int128_half(X86_FORM_MOV_RM64_R64, frame, depth - 2, 1, X86_REGISTER_RDX));
        if (plan.save_rax)
        {
            tier0_spill(writer, frame, X86_REGISTER_RAX, frame->fixed_area, true);
        }
        if (plan.save_rdx)
        {
            tier0_spill(writer, frame, X86_REGISTER_RDX, frame->fixed_area + 8, true);
        }
    }
    else if (kind == BUSTER_TYPED_SHL || kind == BUSTER_TYPED_SHR)
    {
        const uint8_t count = tier0_integer_depths[depth - 1];
        const bool right = kind == BUSTER_TYPED_SHR;
        if (plan.save_rcx)
        {
            tier0_spill(writer, frame, X86_REGISTER_RCX, frame->fixed_area, false);
        }
        if (count != X86_REGISTER_RCX)
        {
            tier0_put(writer, tier0_instruction(X86_FORM_MOV_R32_RM32, X86_REGISTER_RCX, count, direct, 0, 0));
        }
        tier0_put(writer, tier0_int128_half(X86_FORM_MOV_R64_RM64, frame, depth - 2, 0, s0));
        tier0_put(writer, tier0_int128_half(X86_FORM_MOV_R64_RM64, frame, depth - 2, 1, s1));
        if (right && class == BUSTER_TYPED_CLASS_I128)
        {
            tier0_put(writer, tier0_instruction(X86_FORM_SHRD_RM64_R64_CL, s1, s0, direct, 0, 0));
            tier0_put(writer, tier0_instruction(X86_FORM_MOV_R64_RM64, s2, s1, direct, 0, 0));
            tier0_put(writer, tier0_instruction(X86_FORM_SAR_RM64_IMM8, 0, s2, direct, 0, 63));
            tier0_put(writer, tier0_instruction(X86_FORM_SAR_RM64_CL, 0, s1, direct, 0, 0));
        }
        else if (right)
        {
            tier0_put(writer, tier0_instruction(X86_FORM_SHRD_RM64_R64_CL, s1, s0, direct, 0, 0));
            tier0_put(writer, tier0_instruction(X86_FORM_SHR_RM64_CL, 0, s1, direct, 0, 0));
            tier0_put(writer, tier0_instruction(X86_FORM_XOR_R32_RM32, s2, s2, direct, 0, 0));
        }
        else
        {
            tier0_put(writer, tier0_instruction(X86_FORM_SHLD_RM64_R64_CL, s0, s1, direct, 0, 0));
            tier0_put(writer, tier0_instruction(X86_FORM_SHL_RM64_CL, 0, s0, direct, 0, 0));
            tier0_put(writer, tier0_instruction(X86_FORM_XOR_R32_RM32, s2, s2, direct, 0, 0));
        }
        // From a count of 64 on, the half that moved holds the whole value's bits: a right shift's low half takes the high
        // one, and a left shift's high half the low one, the other filled.
        const uint8_t moved = right ? s0 : s1;
        const uint8_t vacated = right ? s1 : s0;
        tier0_put(writer, tier0_instruction(X86_FORM_TEST_RM8_IMM8, 0, X86_REGISTER_RCX, direct, 0, 64));
        tier0_put(writer, tier0_instruction(X86_FORM_CMOVNE_R64_RM64, moved, vacated, direct, 0, 0));
        tier0_put(writer, tier0_instruction(X86_FORM_CMOVNE_R64_RM64, vacated, s2, direct, 0, 0));
        tier0_put(writer, tier0_int128_half(X86_FORM_MOV_RM64_R64, frame, depth - 2, 0, s0));
        tier0_put(writer, tier0_int128_half(X86_FORM_MOV_RM64_R64, frame, depth - 2, 1, s1));
        if (plan.save_rcx)
        {
            tier0_spill(writer, frame, X86_REGISTER_RCX, frame->fixed_area, true);
        }
    }
    else if (kind == BUSTER_TYPED_EQ || kind == BUSTER_TYPED_NE)
    {
        tier0_put(writer, tier0_int128_half(X86_FORM_MOV_R64_RM64, frame, depth - 2, 0, s0));
        tier0_put(writer, tier0_int128_half(X86_FORM_XOR_R64_RM64, frame, depth - 1, 0, s0));
        tier0_put(writer, tier0_int128_half(X86_FORM_MOV_R64_RM64, frame, depth - 2, 1, s1));
        tier0_put(writer, tier0_int128_half(X86_FORM_XOR_R64_RM64, frame, depth - 1, 1, s1));
        tier0_put(writer, tier0_instruction(X86_FORM_OR_R64_RM64, s0, s1, direct, 0, 0));
        tier0_put(writer, tier0_instruction(kind == BUSTER_TYPED_EQ ? X86_FORM_SETE_RM8 : X86_FORM_SETNE_RM8, 0, s0, direct, 0, 0));
        tier0_put(writer, tier0_instruction(X86_FORM_MOVZX_R32_RM8, s0, s0, direct, 0, 0));
    }
    else if (kind >= BUSTER_TYPED_LT && kind <= BUSTER_TYPED_GE)
    {
        const bool swapped = kind == BUSTER_TYPED_GT || kind == BUSTER_TYPED_LE;
        const bool less = kind == BUSTER_TYPED_LT || kind == BUSTER_TYPED_GT;
        const bool is_signed = class == BUSTER_TYPED_CLASS_I128;
        const uint32_t first = swapped ? depth - 1 : depth - 2;
        const uint32_t last = swapped ? depth - 2 : depth - 1;
        const uint32_t set = less ? (is_signed ? X86_FORM_SETL_RM8 : X86_FORM_SETB_RM8) : (is_signed ? X86_FORM_SETGE_RM8 : X86_FORM_SETAE_RM8);
        tier0_put(writer, tier0_int128_half(X86_FORM_MOV_R64_RM64, frame, first, 0, s0));
        tier0_put(writer, tier0_int128_half(X86_FORM_CMP_R64_RM64, frame, last, 0, s0));
        tier0_put(writer, tier0_int128_half(X86_FORM_MOV_R64_RM64, frame, first, 1, s0));
        tier0_put(writer, tier0_int128_half(X86_FORM_SBB_R64_RM64, frame, last, 1, s0));
        tier0_put(writer, tier0_instruction(set, 0, s0, direct, 0, 0));
        tier0_put(writer, tier0_instruction(X86_FORM_MOVZX_R32_RM8, s0, s0, direct, 0, 0));
    }
    else if (kind >= BUSTER_TYPED_ADD && kind <= BUSTER_TYPED_XOR)
    {
        // ADD, SUB, AND, OR and XOR (no 128-bit entry divides).
        const uint32_t low = kind == BUSTER_TYPED_ADD   ? X86_FORM_ADD_RM64_R64
                             : kind == BUSTER_TYPED_SUB ? X86_FORM_SUB_RM64_R64
                             : kind == BUSTER_TYPED_AND ? X86_FORM_AND_RM64_R64
                             : kind == BUSTER_TYPED_OR  ? X86_FORM_OR_RM64_R64
                                                        : X86_FORM_XOR_RM64_R64;
        const uint32_t high = kind == BUSTER_TYPED_ADD ? X86_FORM_ADC_RM64_R64 : kind == BUSTER_TYPED_SUB ? X86_FORM_SBB_RM64_R64 : low;
        for (uint32_t half = 0; half < 2; half += 1)
        {
            tier0_put(writer, tier0_int128_half(X86_FORM_MOV_R64_RM64, frame, depth - 1, half, s0));
            tier0_put(writer, tier0_int128_half(half == 0 ? low : high, frame, depth - 2, half, s0));
        }
    }
    else if (tier0_class_int128(class) && !tier0_class_int128(entries->variants[i]))
    {
        // CONVERT from an integer class: its value in s0, the register at d − 1.
        const uint32_t from = entries->variants[i];
        if (tier0_class_bytes(from) < 8)
        {
            static const uint16_t extensions[BUSTER_TYPED_CLASS_U32 + 1] = {
                [BUSTER_TYPED_CLASS_I8] = X86_FORM_MOVSX_R64_RM8,   [BUSTER_TYPED_CLASS_U8] = X86_FORM_MOVZX_R32_RM8,
                [BUSTER_TYPED_CLASS_I16] = X86_FORM_MOVSX_R64_RM16, [BUSTER_TYPED_CLASS_U16] = X86_FORM_MOVZX_R32_RM16,
                [BUSTER_TYPED_CLASS_I32] = X86_FORM_MOVSXD_R64_RM32, [BUSTER_TYPED_CLASS_U32] = X86_FORM_MOV_R32_RM32,
            };
            tier0_put(writer, tier0_instruction(extensions[from], s0, s0, direct, 0, 0));
        }
        tier0_put(writer, tier0_int128_half(X86_FORM_MOV_RM64_R64, frame, depth - 1, 0, s0));
        if (tier0_class_signed(from))
        {
            tier0_put(writer, tier0_instruction(X86_FORM_SAR_RM64_IMM8, 0, s0, direct, 0, 63));
            tier0_put(writer, tier0_int128_half(X86_FORM_MOV_RM64_R64, frame, depth - 1, 1, s0));
        }
        else
        {
            tier0_put(writer, tier0_memory_slot(X86_FORM_MOV_RM64_IMM32, frame, depth - 1, 8));
        }
    }
    else if (!tier0_class_int128(class))
    {
        // CONVERT to an integer class.
        tier0_put(writer, tier0_int128_half(X86_FORM_MOV_R64_RM64, frame, depth - 1, 0, s0));
    }
}

// How many records tier0_emit_int128 writes for entry i, without writing them (tier0_count_deferred's).
static uint32_t tier0_count_int128(const Tier0Function* function, const Tier0Entries* entries, uint32_t i)
{
    const uint32_t kind = entries->key_kinds[i];
    const uint32_t class = entries->key_classes[i];
    const Tier0Int128Plan plan = tier0_int128_plan(function, entries, i);
    uint32_t count = 0;
    if (kind == BUSTER_TYPED_CONST)
    {
        count = tier0_fits_int32((int64_t)function->values[i]) ? 2 : 3;
    }
    else if (kind == BUSTER_TYPED_DUP || kind == BUSTER_TYPED_LOAD || kind == BUSTER_TYPED_SET || kind == BUSTER_TYPED_LOCAL || kind == BUSTER_TYPED_STORE_LOCAL ||
             kind == BUSTER_TYPED_SET_LOCAL)
    {
        count = 4;
    }
    else if (kind == BUSTER_TYPED_STORE)
    {
        count = 6;
    }
    else if (kind == BUSTER_TYPED_NEG || kind == BUSTER_TYPED_NOT)
    {
        count = kind == BUSTER_TYPED_NEG ? 3 : 2;
    }
    else if (kind == BUSTER_TYPED_MUL)
    {
        count = 10 + 2 * ((plan.save_rax ? 1u : 0u) + (plan.save_rdx ? 1u : 0u));
    }
    else if (kind == BUSTER_TYPED_SHL || kind == BUSTER_TYPED_SHR)
    {
        const bool arithmetic = kind == BUSTER_TYPED_SHR && class == BUSTER_TYPED_CLASS_I128;
        const bool moved = tier0_integer_depths[entries->depths[i] - 1] != X86_REGISTER_RCX;
        count = (plan.save_rcx ? 2u : 0u) + (moved ? 1u : 0u) + 2 + (arithmetic ? 4u : 3u) + 3 + 2;
    }
    else if (kind == BUSTER_TYPED_EQ || kind == BUSTER_TYPED_NE)
    {
        count = 7;
    }
    else if (kind >= BUSTER_TYPED_LT && kind <= BUSTER_TYPED_GE)
    {
        count = 6;
    }
    else if (kind >= BUSTER_TYPED_ADD && kind <= BUSTER_TYPED_XOR)
    {
        count = 4;
    }
    else if (tier0_class_int128(class) && !tier0_class_int128(entries->variants[i]))
    {
        const uint32_t from = entries->variants[i];
        count = (tier0_class_bytes(from) < 8 ? 1u : 0u) + 1 + (tier0_class_signed(from) ? 2u : 1u);
    }
    else if (!tier0_class_int128(class))
    {
        count = 1;
    }
    return count;
}

// Whether entry i is a vector's (tier0_emit_vector_entry's): an X86 entry, or one keyed by class VECTOR. A call is
// tier0_emit_call's, and none passes or returns a VECTOR value.
static bool tier0_vector_entry(const Tier0Entries* entries, uint32_t i)
{
    return entries->key_kinds[i] == BUSTER_TYPED_X86 || tier0_class_vector(entries->key_classes[i]);
}

// A vector's slot of integer depth `depth`, from rsp (or rbp with a frame pointer), as the memory operand of `form`, with
// vector register `reg`.
static X86Instruction tier0_vector_slot(uint32_t form, const Tier0Frame* frame, uint32_t depth, uint8_t reg)
{
    const int64_t displacement = (int64_t)frame->vector_area + BUSTER_TIER0_VECTOR_SLOT_BYTES * (int64_t)depth + frame->bias;
    return tier0_instruction(form, reg, frame->base, BUSTER_X86_MEMORY_BASE, (int32_t)displacement, 0);
}

// The register a VECTOR entry copies its value through (tier0_emit_vector_value's): zmm16, which only EVEX forms name, so
// that no value of the floating stack's is in it.
#define BUSTER_TIER0_VECTOR_SCRATCH 16

// vmovdqu64's load and store of `bytes` (16, 32 or 64), which take any address and any of the 32 vector registers.
static uint32_t tier0_vector_load(uint32_t bytes)
{
    return bytes == 16 ? X86_FORM_VMOVDQU64_X_XM128 : bytes == 32 ? X86_FORM_VMOVDQU64_Y_YM256 : X86_FORM_VMOVDQU64_Z_ZM512;
}

static uint32_t tier0_vector_store(uint32_t bytes)
{
    return bytes == 16 ? X86_FORM_VMOVDQU64_XM128_X : bytes == 32 ? X86_FORM_VMOVDQU64_YM256_Y : X86_FORM_VMOVDQU64_ZM512_Z;
}

// Whether constant k's word w is a sign-extended imm32, which one mov qword [slot], imm32 stores; any other takes two
// mov dword.
static bool tier0_vector_word_fits(const Tier0Function* function, uint64_t k, uint32_t w)
{
    return tier0_fits_int32((int64_t)function->vector_constants[8 * k + w]);
}

// Whether constant k is all zeros up to its bytes, which vpxord and one store make.
static bool tier0_vector_zero(const Tier0Function* function, uint64_t k)
{
    uint64_t bits = 0;
    for (uint32_t w = 0; w < function->vector_constant_bytes[k] / 8; w += 1)
    {
        bits |= function->vector_constants[8 * k + w];
    }
    return bits == 0;
}

// A VECTOR entry's records (tier0_vector_entry's but X86's), its value through zmm16 (BUSTER_TIER0_VECTOR_SCRATCH), with
// d the entry's integer depth:
//   - CONST: zeros by vpxord and a store into slot d; any other constant, its words up to its bytes stored into slot d as
//     immediates, a mov qword of a sign-extended imm32 or two mov dword each;
//   - DUP: slot d − 1 copied into slot d;
//   - LOAD: the value's bytes at the address in the register at d − 1 into slot d − 1; STORE and SET: slot d − 1's into
//     the bytes at the address in the register at d − 2, STORE's kept in slot d − 2 too;
//   - LOCAL: its object's bytes into slot d; STORE_LOCAL and SET_LOCAL: slot d − 1's into them.
// A slot's copies move all 64 bytes, whose bytes past the value's are unspecified, as a narrow integer's register's are.
static void tier0_emit_vector_value(const Tier0Function* function, const Tier0Entries* entries, const Tier0Frame* frame, const Tier0Home* homes, uint32_t i,
                                    Tier0Writer* writer)
{
    const uint32_t kind = entries->key_kinds[i];
    const uint32_t depth = entries->depths[i];
    const uint64_t value = function->values[i];
    const uint8_t scratch = BUSTER_TIER0_VECTOR_SCRATCH;
    if (kind == BUSTER_TYPED_CONST && tier0_vector_zero(function, value))
    {
        tier0_put(writer, (X86Instruction){ .form = X86_FORM_VPXORD_X_X_XM128, .reg = scratch, .vvvv = scratch, .rm = scratch });
        tier0_put(writer, tier0_vector_slot(X86_FORM_VMOVDQU64_ZM512_Z, frame, depth, scratch));
    }
    else if (kind == BUSTER_TYPED_CONST)
    {
        for (uint32_t w = 0; w < function->vector_constant_bytes[value] / 8; w += 1)
        {
            const uint64_t word = function->vector_constants[8 * value + w];
            const bool fits = tier0_vector_word_fits(function, value, w);
            for (uint32_t half = 0; half < (fits ? 1u : 2u); half += 1)
            {
                X86Instruction record = tier0_vector_slot(fits ? X86_FORM_MOV_RM64_IMM32 : X86_FORM_MOV_RM32_IMM32, frame, depth, 0);
                record.displacement += (int32_t)(8 * w + 4 * half);
                record.immediate = (int32_t)(uint32_t)(word >> (32 * half));
                tier0_put(writer, record);
            }
        }
    }
    else if (kind == BUSTER_TYPED_DUP)
    {
        tier0_put(writer, tier0_vector_slot(X86_FORM_VMOVDQU64_Z_ZM512, frame, depth - 1, scratch));
        tier0_put(writer, tier0_vector_slot(X86_FORM_VMOVDQU64_ZM512_Z, frame, depth, scratch));
    }
    else if (kind == BUSTER_TYPED_LOAD)
    {
        tier0_put(writer, tier0_instruction(tier0_vector_load((uint32_t)value), scratch, tier0_integer_depths[depth - 1], BUSTER_X86_MEMORY_BASE, 0, 0));
        tier0_put(writer, tier0_vector_slot(X86_FORM_VMOVDQU64_ZM512_Z, frame, depth - 1, scratch));
    }
    else if (kind == BUSTER_TYPED_STORE || kind == BUSTER_TYPED_SET)
    {
        tier0_put(writer, tier0_vector_slot(X86_FORM_VMOVDQU64_Z_ZM512, frame, depth - 1, scratch));
        tier0_put(writer, tier0_instruction(tier0_vector_store((uint32_t)value), scratch, tier0_integer_depths[depth - 2], BUSTER_X86_MEMORY_BASE, 0, 0));
        if (kind == BUSTER_TYPED_STORE)
        {
            tier0_put(writer, tier0_vector_slot(X86_FORM_VMOVDQU64_ZM512_Z, frame, depth - 2, scratch));
        }
    }
    else
    {
        // LOCAL, STORE_LOCAL and SET_LOCAL: all of the object's bytes (tier0_vector_object's).
        const Tier0Home home = homes[value];
        const uint32_t bytes = function->local_sizes[value];
        if (kind == BUSTER_TYPED_LOCAL)
        {
            tier0_put(writer, tier0_instruction(tier0_vector_load(bytes), scratch, home.reg, home.mode, home.displacement, 0));
            tier0_put(writer, tier0_vector_slot(X86_FORM_VMOVDQU64_ZM512_Z, frame, depth, scratch));
        }
        else
        {
            tier0_put(writer, tier0_vector_slot(X86_FORM_VMOVDQU64_Z_ZM512, frame, depth - 1, scratch));
            tier0_put(writer, tier0_instruction(tier0_vector_store(bytes), scratch, home.reg, home.mode, home.displacement, 0));
        }
    }
}

// How many records tier0_emit_vector_value writes for entry i, without writing them (tier0_count_deferred's).
static uint32_t tier0_count_vector_value(const Tier0Function* function, const Tier0Entries* entries, uint32_t i)
{
    const uint32_t kind = entries->key_kinds[i];
    const uint64_t value = function->values[i];
    uint32_t count = kind == BUSTER_TYPED_STORE ? 3 : 2;
    if (kind == BUSTER_TYPED_CONST && !tier0_vector_zero(function, value))
    {
        count = 0;
        for (uint32_t w = 0; w < function->vector_constant_bytes[value] / 8; w += 1)
        {
            count += tier0_vector_word_fits(function, value, w) ? 1 : 2;
        }
    }
    return count;
}

// Where an X86 entry's roles are (tier0_operation_placement), from its depths before it, d and f: each operand's place
// by operand, the first pushed first at the lowest depth of its stack (an integer's register, a floating value's XMM
// register, a VECTOR's integer depth, its slot's); the integer result's register and the floating result's, at the
// first depth of their stack's operands (at d or f with none); the scratch registers, the depth registers from d on (from
// d + 1 where an integer result takes d's); and the first VECTOR register, the floating stack's from f on (from f + 1
// where a floating result takes f's), which a VEX form names below 16 and an EVEX form below 32. `scratch_depth` is the
// first scratch register's depth, and `result_depth` the result's on its stack.
typedef struct Tier0Placement Tier0Placement;
struct Tier0Placement
{
    uint8_t operands[BUSTER_TIER0_OPERATION_OPERANDS];
    uint8_t scratch[BUSTER_TIER0_SCRATCH];
    uint8_t integer_result;
    uint8_t floating_result;
    uint8_t vector;
    int32_t scratch_depth;
    int32_t result_depth;
};

static Tier0Placement tier0_operation_placement(const Tier0Operation* operation, int32_t depth, int32_t floating)
{
    Tier0Placement placement;
    memset(&placement, 0, sizeof(placement));
    uint32_t integers = 0;
    uint32_t floatings = 0;
    for (uint32_t j = 0; j < operation->operand_count; j += 1)
    {
        const bool on_floating = tier0_stack(operation->operand_classes[j]) == 2;
        integers += on_floating ? 0 : 1;
        floatings += on_floating ? 1 : 0;
    }
    const int32_t integer_first = depth - (int32_t)integers;
    const int32_t floating_first = floating - (int32_t)floatings;
    uint32_t integer = 0;
    uint32_t floating_index = 0;
    for (uint32_t j = 0; j < operation->operand_count; j += 1)
    {
        const uint32_t class = operation->operand_classes[j];
        const int32_t at = tier0_stack(class) == 2 ? floating_first + (int32_t)floating_index : integer_first + (int32_t)integer;
        const bool in_table = at >= 0 && at < BUSTER_TIER0_INTEGER_DEPTHS;
        placement.operands[j] = (uint8_t)(tier0_stack(class) == 2 || tier0_class_vector(class) ? at : in_table ? tier0_integer_depths[at] : X86_REGISTER_RAX);
        integer += tier0_stack(class) == 2 ? 0 : 1;
        floating_index += tier0_stack(class) == 2 ? 1 : 0;
    }
    const uint32_t result = operation->result_class;
    const bool integer_result = tier0_class_integer(result);
    const bool floating_result = tier0_stack(result) == 2;
    placement.result_depth = floating_result ? floating_first : integer_first;
    placement.integer_result = integer_first >= 0 && integer_first < BUSTER_TIER0_INTEGER_DEPTHS ? tier0_integer_depths[integer_first] : X86_REGISTER_RAX;
    placement.floating_result = (uint8_t)floating_first;
    placement.scratch_depth = depth + (integer_result && integers == 0 ? 1 : 0);
    for (uint32_t k = 0; k < BUSTER_TIER0_SCRATCH; k += 1)
    {
        const int32_t at = placement.scratch_depth + (int32_t)k;
        placement.scratch[k] = at < BUSTER_TIER0_INTEGER_DEPTHS ? tier0_integer_depths[at] : X86_REGISTER_RAX;
    }
    placement.vector = (uint8_t)(floating + (floating_result && floatings == 0 ? 1 : 0));
    return placement;
}

// Whether an /is4 operand is the form's: a register in the immediate's bits 7 to 4, of the reg operand's class.
static bool tier0_form_is4(const X86FormDescription* description)
{
    const uint8_t syntax = description->syntax;
    return syntax == X86_SYNTAX_REG_VVVV_RM_IS4 || syntax == X86_SYNTAX_REG_VVVV_IS4_RM || syntax == X86_SYNTAX_REG_VVVV_RM_IS4_IMM ||
           syntax == X86_SYNTAX_REG_VVVV_IS4_RM_IMM;
}

// A row's record's register fields, as its form has them (tier0_operation_fields): up to four, each a pointer to its byte
// in the record and its register class (BUSTER_X86_CLASS_*, a memory operand's base and a general index GPR64's, a VSIB
// index its vector class's); and whether the form has an /is4 operand, whose role (j, for VECTOR(j)) is in the
// immediate's bits 7 to 4.
#define BUSTER_TIER0_FIELDS 4
typedef struct Tier0Fields Tier0Fields;
struct Tier0Fields
{
    uint8_t* bytes[BUSTER_TIER0_FIELDS];
    uint8_t classes[BUSTER_TIER0_FIELDS];
    uint32_t count;
    bool is4;
};

static Tier0Fields tier0_operation_fields(X86Instruction* record)
{
    const X86FormDescription* description = x86_form_description(record->form);
    Tier0Fields fields;
    memset(&fields, 0, sizeof(fields));
    const uint16_t operands[2] = { description->reg, description->vvvv };
    uint8_t* bytes[2] = { &record->reg, &record->vvvv };
    for (uint32_t k = 0; k < 2; k += 1)
    {
        if ((operands[k] & BUSTER_X86_OPERAND_CLASS) != BUSTER_X86_CLASS_NONE)
        {
            fields.bytes[fields.count] = bytes[k];
            fields.classes[fields.count] = (uint8_t)(operands[k] & BUSTER_X86_OPERAND_CLASS);
            fields.count += 1;
        }
    }
    if (record->mode == BUSTER_X86_REGISTER_DIRECT && (description->rm & BUSTER_X86_OPERAND_CLASS) != BUSTER_X86_CLASS_NONE)
    {
        fields.bytes[fields.count] = &record->rm;
        fields.classes[fields.count] = (uint8_t)(description->rm & BUSTER_X86_OPERAND_CLASS);
        fields.count += 1;
    }
    else if (record->mode != BUSTER_X86_REGISTER_DIRECT)
    {
        if ((record->mode & BUSTER_X86_MODE_BASE) != 0)
        {
            fields.bytes[fields.count] = &record->rm;
            fields.classes[fields.count] = BUSTER_X86_CLASS_GPR64;
            fields.count += 1;
        }
        if ((record->mode & BUSTER_X86_MODE_INDEX) != 0)
        {
            const bool vsib = (description->rm & BUSTER_X86_OPERAND_VSIB) != 0;
            fields.bytes[fields.count] = &record->index;
            fields.classes[fields.count] = (uint8_t)(vsib ? description->rm & BUSTER_X86_OPERAND_CLASS : BUSTER_X86_CLASS_GPR64);
            fields.count += 1;
        }
    }
    fields.is4 = tier0_form_is4(description);
    return fields;
}

// Whether a register class is a vector register's (XMM, YMM, ZMM) or a general register's (16, 32 or 64 bits).
static bool tier0_vector_register_class(uint32_t class)
{
    return class == BUSTER_X86_CLASS_XMM || class == BUSTER_X86_CLASS_YMM || class == BUSTER_X86_CLASS_ZMM;
}

static bool tier0_general_register_class(uint32_t class)
{
    return class == BUSTER_X86_CLASS_GPR16 || class == BUSTER_X86_CLASS_GPR32 || class == BUSTER_X86_CLASS_GPR64;
}

// A role's register by `placement` (tier0_operation_placement's), in a field of register class `class`: in a vector
// register's, VECTOR(j)'s, FLOATING(j)'s or FLOATING_RESULT's; in a general register's, INTEGER(j)'s, INTEGER_RESULT's or
// SCRATCH(k)'s; an opmask register, itself.
static uint8_t tier0_operation_register(uint8_t role, uint32_t class, const Tier0Placement* placement)
{
    uint8_t reg = role;
    if (tier0_vector_register_class(class))
    {
        reg = role < BUSTER_TIER0_ROLE_FLOATING(0)        ? (uint8_t)(placement->vector + (role & 15))
              : role == BUSTER_TIER0_ROLE_FLOATING_RESULT ? placement->floating_result
                                                          : placement->operands[role & 7];
    }
    else if (tier0_general_register_class(class))
    {
        reg = role == BUSTER_TIER0_ROLE_INTEGER_RESULT ? placement->integer_result
              : role >= BUSTER_TIER0_ROLE_SCRATCH(0)   ? placement->scratch[role & 3]
                                                       : placement->operands[role & 7];
    }
    return reg;
}

// A row's record with its roles placed (tier0_emit_operation's, and tier0_operation_valid's with a placement of its own).
static X86Instruction tier0_operation_place(X86Instruction record, const Tier0Placement* placement)
{
    Tier0Fields fields = tier0_operation_fields(&record);
    for (uint32_t k = 0; k < fields.count; k += 1)
    {
        *fields.bytes[k] = tier0_operation_register(*fields.bytes[k], fields.classes[k], placement);
    }
    if (fields.is4)
    {
        const uint32_t reg = (uint32_t)placement->vector + ((uint32_t)record.immediate >> 4 & 15);
        record.immediate = (int32_t)((reg << 4) | ((uint32_t)record.immediate & 15));
    }
    return record;
}

// What a row's records reach (tier0_operation_reach), with the loads and the store tier0_emit_operation writes around
// them: one past the highest VECTOR register any form names, a VECTOR operand j's load writes (VECTOR(j)) or a VECTOR
// result's store reads (VECTOR(0)), and one past the highest a VEX form names (an /is4 operand's among them), and one
// past the highest scratch register; 0 for none. A row of no records writes its operands' registers too (#556's review:
// a floating home in one of them was lost).
typedef struct Tier0Reach Tier0Reach;
struct Tier0Reach
{
    uint32_t vectors;
    uint32_t vex_vectors;
    uint32_t scratch;
};

static Tier0Reach tier0_operation_reach(const Tier0Operation* operation)
{
    Tier0Reach reach = { 0, 0, 0 };
    for (uint32_t r = 0; r < operation->record_count; r += 1)
    {
        X86Instruction record = operation->records[r];
        const Tier0Fields fields = tier0_operation_fields(&record);
        const bool vex = x86_form_description(record.form)->family == BUSTER_X86_FAMILY_VEX;
        for (uint32_t k = 0; k < fields.count; k += 1)
        {
            const uint8_t role = *fields.bytes[k];
            const bool vector = tier0_vector_register_class(fields.classes[k]) && role < BUSTER_TIER0_ROLE_FLOATING(0);
            const bool scratch = tier0_general_register_class(fields.classes[k]) && role >= BUSTER_TIER0_ROLE_SCRATCH(0);
            const uint32_t past = (uint32_t)(role & 15) + 1;
            reach.vectors = vector && past > reach.vectors ? past : reach.vectors;
            reach.vex_vectors = vector && vex && past > reach.vex_vectors ? past : reach.vex_vectors;
            reach.scratch = scratch && (uint32_t)(role & 3) + 1 > reach.scratch ? (uint32_t)(role & 3) + 1 : reach.scratch;
        }
        if (fields.is4)
        {
            const uint32_t past = ((uint32_t)record.immediate >> 4 & 15) + 1;
            reach.vectors = past > reach.vectors ? past : reach.vectors;
            reach.vex_vectors = vex && past > reach.vex_vectors ? past : reach.vex_vectors;
        }
    }
    for (uint32_t j = 0; j < operation->operand_count; j += 1)
    {
        reach.vectors = tier0_class_vector(operation->operand_classes[j]) && j + 1 > reach.vectors ? j + 1 : reach.vectors;
    }
    reach.vectors = tier0_class_vector(operation->result_class) && reach.vectors == 0 ? 1 : reach.vectors;
    return reach;
}

// An X86 entry's records (tier0_vector_entry's): each VECTOR operand j's slot loaded into VECTOR(j) at its width, which
// zeroes the register past the operand's bytes, so that a form that reads it wider reads zeros rather than the slot's
// unspecified bytes past the value (#556's review); the row's records with their roles placed
// (tier0_operation_placement); and a VECTOR result stored whole from VECTOR(0) into its slot.
static void tier0_emit_operation(const Tier0Function* function, const Tier0Entries* entries, const Tier0Frame* frame, uint32_t i, Tier0Writer* writer)
{
    const Tier0Operation* operation = &function->operations[function->values[i]];
    const Tier0Placement placement = tier0_operation_placement(operation, entries->depths[i], entries->floating_depths[i]);
    for (uint32_t j = 0; j < operation->operand_count; j += 1)
    {
        if (tier0_class_vector(operation->operand_classes[j]))
        {
            tier0_put(writer, tier0_vector_slot(tier0_vector_load(operation->operand_bytes[j]), frame, placement.operands[j], (uint8_t)(placement.vector + j)));
        }
    }
    for (uint32_t r = 0; r < operation->record_count; r += 1)
    {
        tier0_put(writer, tier0_operation_place(operation->records[r], &placement));
    }
    if (tier0_class_vector(operation->result_class))
    {
        tier0_put(writer, tier0_vector_slot(X86_FORM_VMOVDQU64_ZM512_Z, frame, (uint32_t)placement.result_depth, placement.vector));
    }
}

// How many records tier0_emit_operation writes for entry i (tier0_count_deferred's).
static uint32_t tier0_count_operation(const Tier0Function* function, uint32_t i)
{
    const Tier0Operation* operation = &function->operations[function->values[i]];
    uint32_t count = operation->record_count + (tier0_class_vector(operation->result_class) ? 1u : 0u);
    for (uint32_t j = 0; j < operation->operand_count; j += 1)
    {
        count += tier0_class_vector(operation->operand_classes[j]) ? 1 : 0;
    }
    return count;
}

// The records of a vector's entry (tier0_vector_entry's).
static void tier0_emit_vector_entry(const Tier0Function* function, const Tier0Entries* entries, const Tier0Frame* frame, const Tier0Home* homes, uint32_t i,
                                    Tier0Writer* writer)
{
    if (entries->key_kinds[i] == BUSTER_TYPED_X86)
    {
        tier0_emit_operation(function, entries, frame, i, writer);
    }
    else
    {
        tier0_emit_vector_value(function, entries, frame, homes, i, writer);
    }
}

static uint32_t tier0_count_vector_entry(const Tier0Function* function, const Tier0Entries* entries, uint32_t i)
{
    return entries->key_kinds[i] == BUSTER_TYPED_X86 ? tier0_count_operation(function, i) : tier0_count_vector_value(function, entries, i);
}

// What a vector's entry (tier0_vector_entry's) writes besides its slots, both selections', as the stacks' tops: an X86
// entry's integer result's register and its scratch registers, and its floating result's and the VECTOR registers that are
// the floating stack's (below 16); a VEX form's past 16, or any form's past 32, take the floating stack's top past its
// table, so that selection refuses the function (TIER0_ERROR_DEPTH). And into `slot_top` the slot it writes at its own
// depth: a leaf's (CONST, DUP, LOCAL) and an X86 entry's VECTOR result's without operands on the integer stack. Every
// other slot it writes is one at a depth an operand's register had first, as the memory classes' are (tier0_memory_tops).
static void tier0_vector_tops(const Tier0Function* function, const Tier0Entries* entries, uint32_t i, int32_t* integer_top, int32_t* floating_top, int32_t* slot_top)
{
    const uint32_t kind = entries->key_kinds[i];
    const int32_t depth = entries->depths[i];
    int32_t integer = -1;
    int32_t floating = -1;
    int32_t slot = kind == BUSTER_TYPED_CONST || kind == BUSTER_TYPED_DUP || kind == BUSTER_TYPED_LOCAL ? depth : -1;
    if (kind == BUSTER_TYPED_X86)
    {
        const Tier0Operation* operation = &function->operations[function->values[i]];
        const Tier0Placement placement = tier0_operation_placement(operation, depth, entries->floating_depths[i]);
        const Tier0Reach reach = tier0_operation_reach(operation);
        const uint32_t result = operation->result_class;
        integer = tier0_class_integer(result) ? placement.result_depth : -1;
        integer = reach.scratch != 0 && placement.scratch_depth + (int32_t)reach.scratch - 1 > integer ? placement.scratch_depth + (int32_t)reach.scratch - 1 : integer;
        floating = tier0_stack(result) == 2 ? placement.result_depth : -1;
        const int32_t vectors = reach.vectors != 0 ? placement.vector + (int32_t)reach.vectors - 1 : -1;
        const bool fits = placement.vector + reach.vex_vectors <= BUSTER_TIER0_FLOATING_DEPTHS && placement.vector + reach.vectors <= 32;
        floating = !fits ? BUSTER_TIER0_FLOATING_DEPTHS : vectors > floating ? (vectors < BUSTER_TIER0_FLOATING_DEPTHS ? vectors : BUSTER_TIER0_FLOATING_DEPTHS - 1) : floating;
        slot = tier0_class_vector(result) && placement.result_depth == depth ? depth : -1;
    }
    *integer_top = integer > *integer_top ? integer : *integer_top;
    *floating_top = floating > *floating_top ? floating : *floating_top;
    *slot_top = slot > *slot_top ? slot : *slot_top;
}

// Whether a conversion between U64 and F32 or F64, at the stacks' depths before it (`depth` and `floating`), takes the
// register form of tier0_emit_u64_convert: where its scratch registers exist, the integer stack's at `depth` to F32 or
// F64 (its operand's is at depth − 1), and to U64 the integer stack's at depth + 1 (its result's is at depth) and the
// floating stack's at `floating` (its operand's is at floating − 1). The integer table has `depth_count` depths
// (tier0_integer_depth_count's: 15, 14 with a frame pointer, 13 in a realigned frame that moves rsp). Elsewhere the red
// zone form takes its place, which needs no register but its operand's and its result's, so that every depth that has
// room for the conversion's value takes it (#429's review).
static bool tier0_u64_convert_registers(bool to_u64, uint32_t depth, uint32_t floating, int32_t depth_count)
{
    const uint32_t integer = to_u64 ? depth + 1 : depth;
    return integer < (uint32_t)depth_count && (!to_u64 || floating < BUSTER_TIER0_FLOATING_DEPTHS);
}

// A conversion's registers in a selection's tops (both selections'): its result's, the integer stack's at its depth to
// U64 and the floating stack's free one to F32 or F64, into `tops`; and the register form's scratch registers into
// `scratch`, by whether the function's entries move rsp ([1]) or not ([0]), which a selection knows only once it has
// seen every entry, in a frame `realigned` or not (known before).
static void tier0_select_u64_convert(bool to_u64, int32_t depth, int32_t floating, bool realigned, Tier0Tops* tops, Tier0Tops* scratch)
{
    tops->integer = to_u64 && depth > tops->integer ? depth : tops->integer;
    tops->floating = !to_u64 && floating > tops->floating ? floating : tops->floating;
    for (uint32_t moves = 0; moves < 2; moves += 1)
    {
        const bool registers = tier0_u64_convert_registers(to_u64, (uint32_t)depth, (uint32_t)floating, tier0_integer_depth_count(realigned, moves != 0));
        const int32_t integer = to_u64 ? depth + 1 : depth;
        scratch[moves].integer = registers && integer > scratch[moves].integer ? integer : scratch[moves].integer;
        scratch[moves].floating = registers && to_u64 && floating > scratch[moves].floating ? floating : scratch[moves].floating;
    }
}

// A conversion between U64 and F32 or F64, which SSE has no instruction for before AVX-512: deferred, with no branch, in
// one of two forms of as many records, so that measuring needn't know which (tier0_u64_convert_registers chooses).
// The register form uses the operand's and the result's registers and the stacks' first free ones, and no memory:
//   - To F32 or F64 from U64, x in the register at d − 1, the result in the floating stack's free register: a signed
//     conversion of x where its top bit is clear, and otherwise of x halved with its lowest bit kept, (x >> 1) | (x & 1),
//     which rounds as x does (made in the register at d, then cmovns chooses), then the result's exponent raised by x's
//     top bit (added at the exponent's lowest bit, 52 or 23, through the register at d): the result doubled, exactly,
//     where it was halved, as clang's code does with a branch. The result's register is zeroed first, which breaks
//     cvtsi2sd's dependence on the value it held.
//   - To U64 from F32 or F64, the value in the floating stack's top register, the result in the register at d: one signed
//     truncation, of the value less a bias, 2^63 where the value is at least that and 0 below it (cmovae from the compare
//     with 2^63, which the register at d + 1 and the floating stack's free register hold), then the top bit set back from
//     the compare's result, which setae keeps in the register at d + 1 (a shift by 63 keeps its lowest bit alone, so
//     the rest of the register needn't be cleared). The subtraction is exact (Sterbenz's lemma from 2^63, and of 0 below
//     it) and the truncation's operand is below 2^63, so a value in U64's range raises no invalid exception, and an
//     inexact one only where it has a fraction, as C's conversion does (#429's review): truncating both ways and choosing
//     raised them for nothing. A value out of U64's range (C leaves it undefined) gives the signed truncation's result,
//     its top bit flipped.
// The red zone form keeps to the operand's and the result's registers, its scratch values in red zone temporaries 0 to 2
// instead: from U64, x's copy in temporary 0 and the result's bits in temporary 1; to U64, 2^63 in temporary 0, setae's
// byte in temporary 1 (shifted into the top bit in place) and the bias in temporary 2. It is as exact, and raises the
// same exceptions, but its loads wait for the stores before them: as the only form, it took a conversion-heavy loop to
// twice clang -O0's time on the 9700X, so it takes only the depths the register form has no room at.
// The operand's register is the conversion's to overwrite: it consumes the value.
#define BUSTER_TIER0_FROM_U64_RECORDS 14
#define BUSTER_TIER0_TO_U64_RECORDS 11
// `registers` is the form, which tier0_emit_u64_convert gives as tier0_u64_convert_registers chooses it at the entry's
// depths, and a deep entry's emission (tier0_emit_deep_deferred) at its depths before its window's.
static void tier0_emit_u64_convert_form(const Tier0Entries* entries, bool registers, uint32_t i, Tier0Writer* writer)
{
    const uint32_t class = entries->key_classes[i];
    const uint32_t depth = entries->depths[i];
    const uint32_t floating = entries->floating_depths[i];
    const uint8_t direct = BUSTER_X86_REGISTER_DIRECT;
    const bool to_u64 = !tier0_class_floating(class);
    if (!to_u64 && registers)
    {
        const bool single = class == BUSTER_TYPED_CLASS_F32;
        const uint8_t x = tier0_integer_depths[depth - 1];
        const uint8_t halved = tier0_integer_depths[depth];
        const uint8_t result = (uint8_t)floating;
        tier0_put(writer, tier0_instruction(X86_FORM_MOV_R64_RM64, halved, x, direct, 0, 0));
        tier0_put(writer, tier0_instruction(X86_FORM_AND_RM32_IMM8, 0, halved, direct, 0, 1));
        tier0_put(writer, tier0_instruction(X86_FORM_ADD_R32_RM32, halved, halved, direct, 0, 0));
        tier0_put(writer, tier0_instruction(X86_FORM_OR_R64_RM64, halved, x, direct, 0, 0));
        tier0_put(writer, tier0_instruction(X86_FORM_SHR_RM64_IMM8, 0, halved, direct, 0, 1));
        tier0_put(writer, tier0_instruction(X86_FORM_TEST_RM64_R64, x, x, direct, 0, 0));
        tier0_put(writer, tier0_instruction(X86_FORM_CMOVNS_R64_RM64, halved, x, direct, 0, 0));
        tier0_put(writer, tier0_instruction(X86_FORM_XORPS_X_XM128, result, result, direct, 0, 0));
        tier0_put(writer, tier0_instruction(single ? X86_FORM_CVTSI2SS_X_RM64 : X86_FORM_CVTSI2SD_X_RM64, result, halved, direct, 0, 0));
        tier0_put(writer, tier0_instruction(X86_FORM_SHR_RM64_IMM8, 0, x, direct, 0, 63));
        tier0_put(writer, tier0_instruction(X86_FORM_SHL_RM64_IMM8, 0, x, direct, 0, single ? 23 : 52));
        tier0_put(writer, tier0_instruction(single ? X86_FORM_MOVD_RM32_X : X86_FORM_MOVQ_RM64_X, result, halved, direct, 0, 0));
        tier0_put(writer, tier0_instruction(single ? X86_FORM_ADD_R32_RM32 : X86_FORM_ADD_R64_RM64, halved, x, direct, 0, 0));
        tier0_put(writer, tier0_instruction(single ? X86_FORM_MOVD_X_RM32 : X86_FORM_MOVQ_X_RM64, result, halved, direct, 0, 0));
    }
    else if (!to_u64)
    {
        // x halved in its own register (x's copy in temporary 0), x's sign tested in its copy, and the exponent raised in
        // the result's bits in temporary 1.
        const bool single = class == BUSTER_TYPED_CLASS_F32;
        const uint8_t x = tier0_integer_depths[depth - 1];
        const uint8_t result = (uint8_t)floating;
        tier0_put(writer, tier0_red_zone(X86_FORM_MOV_RM64_R64, x, 0));
        tier0_put(writer, tier0_instruction(X86_FORM_AND_RM32_IMM8, 0, x, direct, 0, 1));
        tier0_put(writer, tier0_instruction(X86_FORM_ADD_R32_RM32, x, x, direct, 0, 0));
        tier0_put(writer, tier0_red_zone(X86_FORM_OR_R64_RM64, x, 0));
        tier0_put(writer, tier0_instruction(X86_FORM_SHR_RM64_IMM8, 0, x, direct, 0, 1));
        tier0_put(writer, tier0_red_zone(X86_FORM_CMP_RM64_IMM8, 0, 0));
        tier0_put(writer, tier0_red_zone(X86_FORM_CMOVGE_R64_RM64, x, 0));
        tier0_put(writer, tier0_instruction(single ? X86_FORM_CVTSI2SS_X_RM64 : X86_FORM_CVTSI2SD_X_RM64, result, x, direct, 0, 0));
        tier0_put(writer, tier0_red_zone(X86_FORM_MOV_R64_RM64, x, 0));
        tier0_put(writer, tier0_instruction(X86_FORM_SHR_RM64_IMM8, 0, x, direct, 0, 63));
        tier0_put(writer, tier0_instruction(X86_FORM_SHL_RM64_IMM8, 0, x, direct, 0, single ? 23 : 52));
        tier0_put(writer, tier0_red_zone(single ? X86_FORM_MOVSS_XM32_X : X86_FORM_MOVSD_XM64_X, result, 1));
        tier0_put(writer, tier0_red_zone(single ? X86_FORM_ADD_RM32_R32 : X86_FORM_ADD_RM64_R64, x, 1));
        tier0_put(writer, tier0_red_zone(single ? X86_FORM_MOVSS_X_XM32 : X86_FORM_MOVSD_X_XM64, result, 1));
    }
    else if (registers)
    {
        const bool single = entries->variants[i] == BUSTER_TYPED_CLASS_F32;
        const uint8_t value = (uint8_t)(floating - 1);
        const uint8_t limit = (uint8_t)floating;
        const uint8_t result = tier0_integer_depths[depth];
        const uint8_t bit = tier0_integer_depths[depth + 1];
        // 2^63, as a float (0x5f000000) or a double (0x43e0000000000000: immediate | displacement << 32).
        tier0_put(writer, single ? tier0_instruction(X86_FORM_MOV_R32_IMM32, 0, bit, 0, 0, 0x5f000000) : tier0_instruction(X86_FORM_MOV_R64_IMM64, 0, bit, 0, 0x43e00000, 0));
        tier0_put(writer, tier0_instruction(single ? X86_FORM_MOVD_X_RM32 : X86_FORM_MOVQ_X_RM64, limit, bit, direct, 0, 0));
        tier0_put(writer, tier0_instruction(single ? X86_FORM_UCOMISS_X_XM32 : X86_FORM_UCOMISD_X_XM64, value, limit, direct, 0, 0));
        // The bias: 0 (a mov, which keeps the flags), or 2^63 where the value is at least that.
        tier0_put(writer, tier0_instruction(X86_FORM_MOV_R32_IMM32, 0, result, 0, 0, 0));
        tier0_put(writer, tier0_instruction(single ? X86_FORM_CMOVAE_R32_RM32 : X86_FORM_CMOVAE_R64_RM64, result, bit, direct, 0, 0));
        tier0_put(writer, tier0_instruction(X86_FORM_SETAE_RM8, 0, bit, direct, 0, 0));
        tier0_put(writer, tier0_instruction(single ? X86_FORM_MOVD_X_RM32 : X86_FORM_MOVQ_X_RM64, limit, result, direct, 0, 0));
        tier0_put(writer, tier0_instruction(single ? X86_FORM_SUBSS_X_XM32 : X86_FORM_SUBSD_X_XM64, value, limit, direct, 0, 0));
        tier0_put(writer, tier0_instruction(single ? X86_FORM_CVTTSS2SI_R64_XM32 : X86_FORM_CVTTSD2SI_R64_XM64, result, value, direct, 0, 0));
        // The top bit back: the compare's result shifted into place.
        tier0_put(writer, tier0_instruction(X86_FORM_SHL_RM64_IMM8, 0, bit, direct, 0, 63));
        tier0_put(writer, tier0_instruction(X86_FORM_XOR_R64_RM64, result, bit, direct, 0, 0));
    }
    else
    {
        const bool single = entries->variants[i] == BUSTER_TYPED_CLASS_F32;
        const uint8_t value = (uint8_t)(floating - 1);
        const uint8_t result = tier0_integer_depths[depth];
        // 2^63 in temporary 0, through the result's register, which holds nothing yet.
        tier0_put(writer, single ? tier0_instruction(X86_FORM_MOV_R32_IMM32, 0, result, 0, 0, 0x5f000000) : tier0_instruction(X86_FORM_MOV_R64_IMM64, 0, result, 0, 0x43e00000, 0));
        tier0_put(writer, tier0_red_zone(single ? X86_FORM_MOV_RM32_R32 : X86_FORM_MOV_RM64_R64, result, 0));
        tier0_put(writer, tier0_red_zone(single ? X86_FORM_UCOMISS_X_XM32 : X86_FORM_UCOMISD_X_XM64, value, 0));
        tier0_put(writer, tier0_red_zone(X86_FORM_SETAE_RM8, 0, 1));
        tier0_put(writer, tier0_instruction(X86_FORM_MOV_R32_IMM32, 0, result, 0, 0, 0));
        tier0_put(writer, tier0_red_zone(single ? X86_FORM_CMOVAE_R32_RM32 : X86_FORM_CMOVAE_R64_RM64, result, 0));
        tier0_put(writer, tier0_red_zone(single ? X86_FORM_MOV_RM32_R32 : X86_FORM_MOV_RM64_R64, result, 2));
        tier0_put(writer, tier0_red_zone(single ? X86_FORM_SUBSS_X_XM32 : X86_FORM_SUBSD_X_XM64, value, 2));
        tier0_put(writer, tier0_instruction(single ? X86_FORM_CVTTSS2SI_R64_XM32 : X86_FORM_CVTTSD2SI_R64_XM64, result, value, direct, 0, 0));
        // The top bit back: setae's byte shifted into it in place (its lowest bit alone survives a shift by 63).
        X86Instruction top = tier0_red_zone(X86_FORM_SHL_RM64_IMM8, 0, 1);
        top.immediate = 63;
        tier0_put(writer, top);
        tier0_put(writer, tier0_red_zone(X86_FORM_XOR_R64_RM64, result, 1));
    }
}

static void tier0_emit_u64_convert(const Tier0Entries* entries, int32_t depth_count, uint32_t i, Tier0Writer* writer)
{
    const bool to_u64 = !tier0_class_floating(entries->key_classes[i]);
    tier0_emit_u64_convert_form(entries, tier0_u64_convert_registers(to_u64, entries->depths[i], entries->floating_depths[i], depth_count), i, writer);
}

// How many records tier0_emit_u64_convert writes for entry i (tier0_count_deferred's).
static uint32_t tier0_count_u64_convert(const Tier0Entries* entries, uint32_t i)
{
    return tier0_class_floating(entries->key_classes[i]) ? BUSTER_TIER0_FROM_U64_RECORDS : BUSTER_TIER0_TO_U64_RECORDS;
}

// ---------------------------------------------------------------------------------------------------------------------
// Deep entries

// A deep entry as its records see it (tier0_select_deep marks them), per stack, the integer stack's at 0 and the floating
// stack's at 1: its depth before it, and at its exit (a JUMP_VALUE's at its target, which its value is on); the depth its
// own records are made at, which the depth columns hold for a stack it is deep on (its window: the values it reads, loaded
// into the stack's first registers); and whether it leaves a value at the exit's top.
typedef struct Tier0DeepEntry Tier0DeepEntry;
struct Tier0DeepEntry
{
    uint32_t before[2];
    uint32_t exit[2];
    uint32_t window[2];
    bool result[2];
};

static Tier0DeepEntry tier0_deep_entry(const Tier0Function* function, const Tier0Entries* entries, uint32_t i)
{
    const uint8_t kind = function->kinds[i];
    const uint32_t stack = tier0_stack(function->classes[i]);
    const Tier0Shape shape = tier0_shapes[kind];
    const bool jump_value = kind == BUSTER_TYPED_JUMP_VALUE;
    // The stack the entry leaves a value on (1 or 2), or 0.
    const uint32_t leaves = kind == BUSTER_TYPED_CALL || jump_value || shape.push == BUSTER_TIER0_STACK_CLASS ? stack
                            : shape.push == BUSTER_TIER0_STACK_INTEGER                                     ? 1
                                                                                                           : 0;
    Tier0DeepEntry deep;
    deep.before[0] = entries->deep_depths[i];
    deep.before[1] = entries->deep_floating_depths[i];
    deep.exit[0] = (uint8_t)((int32_t)deep.before[0] + entries->integer_effects[i] + (jump_value && stack == 1 ? 1 : 0));
    deep.exit[1] = (uint8_t)((int32_t)deep.before[1] + entries->floating_effects[i] + (jump_value && stack == 2 ? 1 : 0));
    deep.window[0] = entries->depths[i];
    deep.window[1] = entries->floating_depths[i];
    deep.result[0] = leaves == 1;
    deep.result[1] = leaves == 2;
    return deep;
}

// The register of depth d of a stack (0, the integer one, or 1).
static uint32_t tier0_stack_register(uint32_t stack, uint32_t d)
{
    return stack == 0 ? tier0_integer_depths[d] : BUSTER_TIER0_XMM(d);
}

// Value `index` of a stack's deep slot, as tier0_spill takes its offset.
static uint32_t tier0_deep_slot(const Tier0Frame* frame, uint32_t stack, uint32_t index)
{
    return frame->deep_area + 8 * ((stack == 0 ? 0 : frame->deep_slots) + index);
}

// A deep entry's records before its own: the registers of each stack its flags spill stored into their slots, then the
// window of each stack it is deep on loaded from its slots into the stack's first registers.
static void tier0_deep_enter(Tier0Writer* writer, const Tier0Frame* frame, const Tier0DeepEntry* deep, uint32_t flags)
{
    for (uint32_t s = 0; s < 2; s += 1)
    {
        const bool spill = (flags & (s == 0 ? BUSTER_TIER0_DEEP_SPILL_INTEGER : BUSTER_TIER0_DEEP_SPILL_FLOATING)) != 0;
        for (uint32_t d = 0; spill && d < deep->before[s]; d += 1)
        {
            tier0_spill(writer, frame, tier0_stack_register(s, d), tier0_deep_slot(frame, s, d), false);
        }
    }
    for (uint32_t s = 0; s < 2; s += 1)
    {
        const bool window = (flags & (s == 0 ? BUSTER_TIER0_DEEP_INTEGER : BUSTER_TIER0_DEEP_FLOATING)) != 0;
        const uint32_t base = deep->before[s] - deep->window[s];
        for (uint32_t d = 0; window && d < deep->window[s]; d += 1)
        {
            tier0_spill(writer, frame, tier0_stack_register(s, d), tier0_deep_slot(frame, s, base + d), true);
        }
    }
}

// A deep entry's records after its own: the value it leaves on each stack it is deep on stored from its register at the
// window's depth into its slot, then the registers of each stack its flags fill loaded back from their slots.
static void tier0_deep_leave(Tier0Writer* writer, const Tier0Frame* frame, const Tier0DeepEntry* deep, uint32_t flags)
{
    for (uint32_t s = 0; s < 2; s += 1)
    {
        const bool window = (flags & (s == 0 ? BUSTER_TIER0_DEEP_INTEGER : BUSTER_TIER0_DEEP_FLOATING)) != 0;
        if (window && deep->result[s])
        {
            const uint32_t top = deep->exit[s] - 1;
            const uint32_t base = deep->before[s] - deep->window[s];
            tier0_spill(writer, frame, tier0_stack_register(s, top - base), tier0_deep_slot(frame, s, top), false);
        }
    }
    for (uint32_t s = 0; s < 2; s += 1)
    {
        const bool fill = (flags & (s == 0 ? BUSTER_TIER0_DEEP_FILL_INTEGER : BUSTER_TIER0_DEEP_FILL_FLOATING)) != 0;
        for (uint32_t d = 0; fill && d < deep->exit[s]; d += 1)
        {
            tier0_spill(writer, frame, tier0_stack_register(s, d), tier0_deep_slot(frame, s, d), true);
        }
    }
}

// The template of a slot for entry i's homes: a home-dependent slot's case (A and B each a register or memory: the
// first template's index plus A's bit plus twice B's).
static inline __attribute__((always_inline)) uint32_t tier0_home_case(const Tier0Function* function, const Tier0Home* homes, uint32_t i, uint16_t entry)
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
    }
    return index;
}

// Whether a deep entry's slot has a template at the depth of its window (`depth`), or its records are its deferred
// emitter's at that depth.
static bool tier0_deep_template(uint16_t slot, uint32_t depth)
{
    const bool template = slot != BUSTER_TIER0_SLOT_DEFERRED && slot != BUSTER_TIER0_SLOT_UNSUPPORTED;
    const uint32_t required = template ? tier0_template_infos[slot & BUSTER_TIER0_SLOT_TEMPLATE].required_depth : BUSTER_TIER0_ANY_DEPTH;
    return template && (required == BUSTER_TIER0_ANY_DEPTH || required == depth);
}

// A deep branch's record, with its column's entry: its record's index and its target's first record.
static void tier0_put_branch(Tier0Writer* writer, const Tier0Entries* entries, X86Instruction record, uint32_t target)
{
    if (writer->records != 0)
    {
        writer->branches[writer->branch_count] = writer->count;
        writer->targets[writer->branch_count] = entries->positions[target];
    }
    writer->branch_count += 1;
    tier0_put(writer, record);
}

static uint16_t tier0_choice_context(const X86Instruction* record, uint8_t live);

// A deep call (BUSTER_TIER0_DEEP_CALL): every value in its slot, the stacks its flags spill stored there first; each
// argument read from its slot, a stack one through rax into its place in the outgoing area, then each register one
// straight into its register; an indirect call's callee into r11; then the call as tier0_emit_call makes it, its result
// stored into its slot, and the stacks its flags fill loaded back. Nothing below the arguments is saved around it: it is
// in the slots. Aggregate copies run before any ABI register is populated, so their scratch registers are free;
// register parts then load from the saved addresses, without the ordinary path's parallel-move conflicts.
static void tier0_emit_deep_call(const Tier0Function* function, const Tier0Entries* entries, const Tier0Frame* frame, const Tier0Home* homes, uint32_t i,
                                 Tier0Writer* writer)
{
    const uint32_t flags = entries->deep[i];
    const uint32_t call = (uint32_t)function->values[i];
    const uint32_t first = function->call_first[call];
    const uint32_t count = function->call_argument_counts[call];
    const bool indirect = function->call_targets[call] == 0;
    const Tier0DeepEntry deep = tier0_deep_entry(function, entries, i);
    uint32_t integer_arguments = 0;
    uint32_t floating_arguments = 0;
    for (uint32_t a = 0; a < count; a += 1)
    {
        const bool floating = tier0_class_floating(function->argument_classes[first + a]);
        integer_arguments += floating ? 0 : 1;
        floating_arguments += floating ? 1 : 0;
    }
    const uint32_t below = deep.before[0] - integer_arguments - (indirect ? 1 : 0);
    const uint32_t floating_below = deep.before[1] - floating_arguments;
    tier0_deep_enter(writer, frame, &deep, flags & (BUSTER_TIER0_DEEP_SPILL_INTEGER | BUSTER_TIER0_DEEP_SPILL_FLOATING));
    for (uint32_t pass = 0; pass < 2; pass += 1)
    {
        uint32_t integer = 0;
        uint32_t floating = 0;
        uint32_t temporary = 0;
        for (uint32_t a = 0; a < count; a += 1)
        {
            const uint32_t class = function->argument_classes[first + a];
            const uint32_t size = function->argument_sizes != 0 ? function->argument_sizes[first + a] : 0;
            const AbiLocation location = function->argument_locations[first + a];
            const bool is_floating = tier0_class_floating(class);
            const uint32_t slot = is_floating ? tier0_deep_slot(frame, 1, floating_below + floating) : tier0_deep_slot(frame, 0, below + (indirect ? 1 : 0) + integer);
            integer += is_floating ? 0 : 1;
            floating += is_floating ? 1 : 0;
            if (size != 0)
            {
                const bool whole = tier0_aggregate_whole(location);
                Tier0Part parts[2];
                const uint32_t part_count = whole ? 0 : tier0_location_parts(location, size, parts);
                if (pass == 0 && whole)
                {
                    tier0_spill(writer, frame, X86_REGISTER_RAX, slot, true);
                    tier0_copy(writer, X86_REGISTER_RSP, (int32_t)location.stack_offset, X86_REGISTER_RAX, 0, size,
                               X86_REGISTER_R10, 0);
                }
                else if (!whole && part_count != 0)
                {
                    tier0_spill(writer, frame, X86_REGISTER_RAX, slot, true);
                    for (uint32_t k = 0; k < part_count; k += 1)
                    {
                        const Tier0Part part = parts[k];
                        if (pass == 0 && part.reg == BUSTER_TIER0_PART_STACK)
                        {
                            tier0_copy(writer, X86_REGISTER_RSP, (int32_t)location.stack_offset, X86_REGISTER_RAX, part.offset, part.bytes,
                                       X86_REGISTER_R10, 0);
                        }
                        else if (pass == 0 && part.load == BUSTER_TIER0_PART_TEMPORARY)
                        {
                            tier0_copy(writer, X86_REGISTER_RSP, tier0_temporary(temporary), X86_REGISTER_RAX, part.offset, part.bytes,
                                       X86_REGISTER_R10, 0);
                        }
                        else if (pass != 0 && part.reg != BUSTER_TIER0_PART_STACK)
                        {
                            tier0_load_part(writer, part, X86_REGISTER_RAX, temporary);
                        }
                        temporary += part.reg != BUSTER_TIER0_PART_STACK && part.load == BUSTER_TIER0_PART_TEMPORARY ? 1 : 0;
                    }
                }
            }
            else if (pass == 0 && location.kind == BUSTER_ABI_LOCATION_STACK)
            {
                tier0_spill(writer, frame, X86_REGISTER_RAX, slot, true);
                tier0_extend(writer, class, X86_REGISTER_RAX);
                tier0_put(writer, tier0_instruction(X86_FORM_MOV_RM64_R64, X86_REGISTER_RAX, X86_REGISTER_RSP, BUSTER_X86_MEMORY_BASE, (int32_t)location.stack_offset, 0));
            }
            else if (pass == 1 && location.kind == BUSTER_ABI_LOCATION_REGISTERS)
            {
                const uint32_t destination = tier0_abi_register(location.registers[0]);
                tier0_spill(writer, frame, destination, slot, true);
                tier0_extend(writer, class, destination);
            }
        }
    }
    if (indirect)
    {
        tier0_spill(writer, frame, X86_REGISTER_R11, tier0_deep_slot(frame, 0, below), true);
    }
    const uint32_t object = function->call_result_objects != 0 ? function->call_result_objects[call] : 0;
    const AbiReturn returned = function->calls[call].result;
    if (object != 0 && returned.kind == BUSTER_ABI_RESULT_MEMORY)
    {
        const Tier0Home home = homes[object - 1];
        tier0_put(writer, tier0_instruction(X86_FORM_LEA_R64_M, X86_REGISTER_RDI, home.reg, home.mode, home.displacement, 0));
    }
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
    if (object != 0 && returned.kind == BUSTER_ABI_RESULT_REGISTERS)
    {
        const Tier0Home home = homes[object - 1];
        const bool high = (returned.flags & BUSTER_ABI_FLAG_HIGH) != 0;
        const uint32_t parts = tier0_x87_parts(returned);
        for (uint32_t k = 0; k < parts; k += 1)
        {
            tier0_put(writer, tier0_instruction(X86_FORM_FSTP_M80, 0, home.reg, home.mode, home.displacement + 16 * (int32_t)k, 0));
        }
        for (uint32_t k = 0; k < 2 && parts == 0; k += 1)
        {
            if (returned.registers[k] != BUSTER_ABI_REGISTER_NONE)
            {
                const uint32_t offset = high ? 8 : 8 * k;
                tier0_store_part(writer, tier0_abi_register(returned.registers[k]), tier0_result_part_bytes(returned), home, offset);
            }
        }
    }
    else if (result != 0)
    {
        const uint32_t stack = result - 1;
        tier0_spill(writer, frame, tier0_stack_register(stack, 0), tier0_deep_slot(frame, stack, stack == 0 ? below : floating_below), false);
    }
    tier0_deep_leave(writer, frame, &deep, flags & (BUSTER_TIER0_DEEP_FILL_INTEGER | BUSTER_TIER0_DEEP_FILL_FLOATING));
}

// A deep entry's own records where its slot has no template at its window's depth: its deferred emitter's at that depth.
static void tier0_emit_deep_deferred(const Tier0Function* function, const Tier0Entries* entries, const Tier0Frame* frame, const Tier0Home* homes, uint32_t i,
                                     Tier0Writer* writer)
{
    const uint32_t kind = entries->key_kinds[i];
    if (kind == BUSTER_TYPED_DIV || kind == BUSTER_TYPED_MOD)
    {
        tier0_emit_division(function, entries, frame, homes, i, writer);
    }
    else if (kind == BUSTER_TYPED_COPY)
    {
        tier0_emit_copy(function, entries, i, writer);
    }
    else if (tier0_moves_stack(kind))
    {
        tier0_emit_stack(entries, frame, i, writer);
    }
    else if (kind == BUSTER_TYPED_CONVERT)
    {
        // Its form at its depths before its window's, where selection counted its registers and tier0_deferred_uses
        // found them (so a deep conversion to U64 is always the red zone's, and one from U64 deep on the floating stack
        // alone the registers' where its integer depth has room).
        const bool to_u64 = !tier0_class_floating(entries->key_classes[i]);
        const bool registers = tier0_u64_convert_registers(to_u64, entries->deep_depths[i], entries->deep_floating_depths[i], frame->depth_count);
        tier0_emit_u64_convert_form(entries, registers, i, writer);
    }
    else
    {
        tier0_emit_shift(entries, frame, i, writer);
    }
}

// A deep entry's records (tier0_select_deep's), in place of its template's or its deferred emitter's: those, at the
// depths of its windows, between tier0_deep_enter's and tier0_deep_leave's; a template's branch, its last record, after
// them, whose loads keep the flags its compare set; its choice request with the templates' (the writer's). Out of line:
// the deep entries are rare, and the deferred entries' dispatch inlines what it calls.
static __attribute__((noinline)) void tier0_emit_deep(const Tier0Function* function, const Tier0Entries* entries, const Tier0Frame* frame, const Tier0Home* homes, uint32_t i, Tier0Writer* writer)
{
    const uint32_t flags = entries->deep[i];
    if ((flags & BUSTER_TIER0_DEEP_CALL) != 0)
    {
        tier0_emit_deep_call(function, entries, frame, homes, i, writer);
    }
    else
    {
        const Tier0DeepEntry deep = tier0_deep_entry(function, entries, i);
        tier0_deep_enter(writer, frame, &deep, flags);
        const uint16_t slot = tier0_slot(entries->key_kinds[i], entries->key_classes[i], entries->variants[i]);
        X86Instruction branch;
        memset(&branch, 0, sizeof(branch));
        uint32_t target = UINT32_MAX;
        if (tier0_deep_template(slot, entries->depths[i]))
        {
            const uint32_t index = tier0_home_case(function, homes, i, slot);
            const Tier0TemplateInfo* info = &tier0_template_infos[index];
            const Tier0Holes holes = tier0_holes_scalar(function, entries, homes, i, info);
            for (uint32_t r = 0; r < info->length; r += 1)
            {
                const X86Instruction record = tier0_record_scalar(&tier0_templates[index].specs[r], &holes);
                if (r == info->choice && writer->choices != 0)
                {
                    Tier0Choices* choices = writer->choices;
                    choices->records[choices->count] = writer->count;
                    choices->rows[choices->count] = info->choice_row;
                    choices->values[choices->count] = info->choice_static != 0 ? (int64_t)info->choice_immediate : (int64_t)holes.k;
                    choices->contexts[choices->count] = tier0_choice_context(&record, info->choice_live);
                    choices->count += 1;
                }
                writer->choice_count += r == info->choice ? 1 : 0;
                if (r == info->fixup)
                {
                    if (writer->records != 0)
                    {
                        writer->references[writer->reference_count] = (LinkReference){
                            .instruction = writer->count,
                            .field = info->fixup_field,
                            .kind = info->fixup_kind,
                            .target = (SymbolId)function->values[i],
                            .addend = 0,
                        };
                    }
                    writer->reference_count += 1;
                }
                if (r == info->branch)
                {
                    branch = record;
                    target = info->end != 0 ? function->count : (uint32_t)function->values[i];
                }
                else
                {
                    tier0_put(writer, record);
                }
            }
        }
        else
        {
            tier0_emit_deep_deferred(function, entries, frame, homes, i, writer);
        }
        tier0_deep_leave(writer, frame, &deep, flags);
        if (target != UINT32_MAX)
        {
            tier0_put_branch(writer, entries, branch, target);
        }
    }
}

// The records of a deferred entry (into `writer`, which counts them when it has no records). `memory` is
// Tier0Frame.memory: a constant at each of tier0_emit_deferred's two uses, so that a function without values of the
// memory classes has its deferred entries' code without a look for theirs, and one with them has its own, out of line
// (#423's measurements).
static inline __attribute__((always_inline)) void tier0_emit_deferred_entry(const Tier0Function* function, const Tier0Entries* entries, const Tier0Frame* frame,
                                                                            const Tier0Home* homes, uint32_t i, bool memory, Tier0Writer* writer)
{
    const uint32_t kind = entries->key_kinds[i];
    if (kind == BUSTER_TYPED_CALL && memory)
    {
        tier0_emit_call(function, entries, frame, homes, i, function->argument_sizes != 0, true, writer);
    }
    else if (kind == BUSTER_TYPED_CALL && function->argument_sizes != 0)
    {
        tier0_emit_call(function, entries, frame, homes, i, true, false, writer);
    }
    else if (kind == BUSTER_TYPED_CALL)
    {
        tier0_emit_call(function, entries, frame, homes, i, false, false, writer);
    }
    else if (memory && tier0_vector_entry(entries, i))
    {
        tier0_emit_vector_entry(function, entries, frame, homes, i, writer);
    }
    else if (memory && tier0_int128_entry(entries, i))
    {
        tier0_emit_int128(function, entries, frame, homes, i, writer);
    }
    else if (memory && tier0_memory_entry(entries, i))
    {
        tier0_emit_x87(function, entries, frame, homes, i, writer);
    }
    else if (kind == BUSTER_TYPED_DIV || kind == BUSTER_TYPED_MOD)
    {
        tier0_emit_division(function, entries, frame, homes, i, writer);
    }
    else if (kind == BUSTER_TYPED_COPY)
    {
        tier0_emit_copy(function, entries, i, writer);
    }
    else if (tier0_moves_stack(kind))
    {
        tier0_emit_stack(entries, frame, i, writer);
    }
    else if (kind == BUSTER_TYPED_CONVERT)
    {
        tier0_emit_u64_convert(entries, frame->depth_count, i, writer);
    }
    else
    {
        tier0_emit_shift(entries, frame, i, writer);
    }
}

static __attribute__((noinline)) void tier0_emit_deferred_memory(const Tier0Function* function, const Tier0Entries* entries, const Tier0Frame* frame,
                                                              const Tier0Home* homes, uint32_t i, Tier0Writer* writer)
{
    tier0_emit_deferred_entry(function, entries, frame, homes, i, true, writer);
}

static void tier0_emit_deferred(const Tier0Function* function, const Tier0Entries* entries, const Tier0Frame* frame, const Tier0Home* homes, uint32_t i,
                                Tier0Writer* writer)
{
    if (frame->memory)
    {
        tier0_emit_deferred_memory(function, entries, frame, homes, i, writer);
    }
    else
    {
        tier0_emit_deferred_entry(function, entries, frame, homes, i, false, writer);
    }
}

// A deferred entry of a function with deep entries (which has no values of the memory classes: tier0_select_deep refuses them): a
// deep entry's records (tier0_emit_deep's), or another's as in any function. Only measuring's and emission's versions for
// such functions call it, so that the others don't test for deep entries.
static void tier0_emit_deferred_deep(const Tier0Function* function, const Tier0Entries* entries, const Tier0Frame* frame, const Tier0Home* homes, uint32_t i,
                                     Tier0Writer* writer)
{
    if (entries->deep[i] != 0)
    {
        tier0_emit_deep(function, entries, frame, homes, i, writer);
    }
    else
    {
        tier0_emit_deferred_entry(function, entries, frame, homes, i, false, writer);
    }
}

// How many records a call takes, and its reference (tier0_count_deferred's). `sized` and `memory` are tier0_emit_call's.
static inline __attribute__((always_inline)) uint32_t tier0_count_call(const Tier0Function* function, const Tier0Entries* entries, uint32_t i, bool sized,
                                                                       bool memory, uint32_t* references)
{
    uint32_t count = 0;
    const uint32_t call = (uint32_t)function->values[i];
    const uint32_t first = function->call_first[call];
    const uint32_t arguments = function->call_argument_counts[call];
    // One pass from the last argument back: an argument's register is its stack's depth less the arguments of its
    // stack after it, so the counts before it aren't needed first. An indirect call's callee is past the first. An
    // aggregate's copies and loads are counted by its parts (tier0_aggregate_records); the addresses that wait for
    // their general parts in a register of their own (tier0_emit_call's) are known once every register the call
    // writes is (tier0_call_written).
    const bool indirect = function->call_targets[call] == 0;
    uint8_t to[2 * BUSTER_TIER0_MAX_CALL_ARGUMENTS + 1];
    uint8_t from[2 * BUSTER_TIER0_MAX_CALL_ARGUMENTS + 1];
    uint8_t addresses[BUSTER_TIER0_MAX_CALL_ARGUMENTS];
    uint8_t owns[BUSTER_TIER0_MAX_CALL_ARGUMENTS];
    uint32_t own_sets[BUSTER_TIER0_MAX_CALL_ARGUMENTS];
    uint32_t moves = 0;
    uint32_t aggregate_count = 0;
    uint32_t integer_after = 0;
    uint32_t floating_after = 0;
    const uint32_t depth = entries->depths[i];
    const uint32_t floating_depth = entries->floating_depths[i];
    for (uint32_t a = arguments; a > 0; a -= 1)
    {
        const uint32_t class = function->argument_classes[first + a - 1];
        const AbiLocation location = function->argument_locations[first + a - 1];
        const uint32_t size = sized ? function->argument_sizes[first + a - 1] : 0;
        const bool is_floating = tier0_class_floating(class);
        const uint32_t source = is_floating ? BUSTER_TIER0_XMM(floating_depth - 1 - floating_after) : tier0_integer_depths[depth - 1 - integer_after];
        integer_after += is_floating ? 0 : 1;
        floating_after += is_floating ? 1 : 0;
        if (size == 0)
        {
            count += tier0_class_integer(class) && tier0_class_bytes(class) < 4 ? 1 : 0;
            const uint8_t destination = tier0_abi_register(location.registers[0]);
            to[moves] = destination;
            from[moves] = (uint8_t)source;
            moves += location.kind == BUSTER_ABI_LOCATION_REGISTERS && destination != source ? 1 : 0;
            count += location.kind == BUSTER_ABI_LOCATION_STACK ? (memory && tier0_class_x87(class) ? 2u : 1u) : 0u;
        }
        else
        {
            count += tier0_aggregate_records(location, size, &owns[aggregate_count], &own_sets[aggregate_count]);
            addresses[aggregate_count] = (uint8_t)source;
            aggregate_count += 1;
        }
    }
    const uint32_t written = aggregate_count != 0 ? tier0_call_written(function, call) : 0;
    for (uint32_t g = 0; g < aggregate_count; g += 1)
    {
        const bool waits = owns[g] != BUSTER_TIER0_PART_STACK && (written >> addresses[g] & 1) != 0 && (own_sets[g] >> addresses[g] & 1) == 0;
        to[moves] = owns[g];
        from[moves] = addresses[g];
        moves += waits ? 1 : 0;
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
    // A result object: its address in rdi for a hidden pointer, or a store per part of the result's registers, which
    // take the result's move's place.
    const uint32_t object = function->call_result_objects != 0 ? function->call_result_objects[call] : 0;
    const bool stored = object != 0 && function->calls[call].result.kind == BUSTER_ABI_RESULT_REGISTERS;
    count += object != 0 ? tier0_result_object_records(function->calls[call].result) : 0;
    // A long double's result is stored into its slot at every depth.
    const uint32_t result = tier0_stack(function->classes[i]);
    count += !stored && ((result == 1 && (below != 0 || (memory && tier0_class_x87(function->classes[i])))) || (result == 2 && floating_below != 0)) ? 1 : 0;
    *references += indirect ? 0 : 1;
    return count;
}

// How many records a deferred entry takes (and references: a call's one), counted from what its emitter decides rather
// than by running it: the vector path's measuring. Each term mirrors its emitter's step: a call's saves and restores,
// argument extensions, stack stores, its register moves (one per move but the last of each cycle, which the xchg
// before it resolves), al, the call and the result's move; division's saves, moves, extension and divide; a shift's
// swaps or its saved rcx; the entries that move rsp, their fixed counts. The scalar path counts by running the
// emitter, so that the twins check the count. `memory` is tier0_emit_deferred_entry's: a constant at each of
// tier0_measure_vector's two instances.
static inline __attribute__((always_inline)) uint32_t tier0_count_deferred(const Tier0Function* function, const Tier0Entries* entries, uint32_t i, bool memory,
                                                                           uint32_t* references)
{
    const uint32_t kind = entries->key_kinds[i];
    uint32_t count = 0;
    if (kind == BUSTER_TYPED_CALL && memory)
    {
        count = tier0_count_call(function, entries, i, function->argument_sizes != 0, true, references);
    }
    else if (kind == BUSTER_TYPED_CALL && function->argument_sizes != 0)
    {
        count = tier0_count_call(function, entries, i, true, false, references);
    }
    else if (kind == BUSTER_TYPED_CALL)
    {
        count = tier0_count_call(function, entries, i, false, false, references);
    }
    else if (memory && tier0_vector_entry(entries, i))
    {
        count = tier0_count_vector_entry(function, entries, i);
    }
    else if (memory && tier0_int128_entry(entries, i))
    {
        count = tier0_count_int128(function, entries, i);
    }
    else if (memory && tier0_memory_entry(entries, i))
    {
        count = tier0_count_x87(entries, i);
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
        count = tier0_copy_records((uint32_t)function->values[i]);
    }
    else if (tier0_moves_stack(kind))
    {
        count = kind == BUSTER_TYPED_ALLOCA ? BUSTER_TIER0_ALLOCA_RECORDS : BUSTER_TIER0_STACK_RECORDS;
    }
    else if (kind == BUSTER_TYPED_CONVERT)
    {
        count = tier0_count_u64_convert(entries, i);
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

// A realigned frame's rounding of rsp down to its alignment (Tier0Frame.alignment_log2): and rsp, -alignment, imm8 to
// 128 bytes.
static X86Instruction tier0_realignment(uint32_t alignment_log2)
{
    const uint32_t form = alignment_log2 <= 7 ? X86_FORM_AND_RM64_IMM8 : X86_FORM_AND_RM64_IMM32;
    return tier0_instruction(form, 0, X86_REGISTER_RSP, BUSTER_X86_REGISTER_DIRECT, 0, (int32_t)-(int64_t)(UINT64_C(1) << alignment_log2));
}

// A realigned frame's records after its reservation: rsp rounded down to its alignment, and with entries that move rsp
// r15 set to it, its base. Out of line and cold, so that the prologue of a frame that isn't realigned keeps its code.
static __attribute__((cold, noinline)) void tier0_emit_realignment(const Tier0Frame* frame, Tier0Writer* writer)
{
    tier0_put(writer, tier0_realignment(frame->alignment_log2));
    if (frame->based)
    {
        tier0_put(writer, tier0_instruction(X86_FORM_MOV_RM64_R64, X86_REGISTER_RSP, X86_REGISTER_R15, BUSTER_X86_REGISTER_DIRECT, 0, 0));
    }
}

// The prologue: with a frame pointer, rbp pushed and pointed at the push; the callee-saved registers the homes use
// pushed, the frame reserved, in a realigned frame rsp rounded down to its alignment (and with entries that move rsp,
// r15 set to it, its base), and the parameters moved from where they arrive to their homes (none for one that stays,
// or that arrives on the stack).
static void tier0_emit_prologue(const Tier0Function* function, const Tier0Frame* frame, const Tier0Home* homes, const uint32_t* references, Tier0Writer* writer)
{
    if (frame->pointer)
    {
        tier0_put(writer, tier0_instruction(X86_FORM_PUSH_R64, 0, X86_REGISTER_RBP, 0, 0, 0));
        tier0_put(writer, tier0_instruction(X86_FORM_MOV_RM64_R64, X86_REGISTER_RSP, X86_REGISTER_RBP, BUSTER_X86_REGISTER_DIRECT, 0, 0));
    }
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
    if (frame->alignment_log2 != 0)
    {
        tier0_emit_realignment(frame, writer);
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
    // incoming slot. No home is an arrival register (tier0_assign_homes), so the order doesn't matter. An aggregate's
    // parts in registers are stored in its object, each register whole; first, its bytes in stack slots that it doesn't
    // keep as its home (tier0_parameter_in_place) are copied there, through rax and xmm15, which no parameter arrives in
    // and no move to a home has written yet: a part's slot whole, or the slots of one on the stack whole. The incoming
    // slots are rsp's, or rbp's in a realigned frame (Tier0Frame.incoming_base).
    const uint8_t base = frame->incoming_base;
    const bool objects = frame->object_parameters;
    for (uint32_t p = 0; p < function->parameter_count && objects; p += 1)
    {
        if (tier0_is_object(function, p) && references[p] != 0)
        {
            const AbiLocation location = function->parameters[p];
            const int32_t incoming = (int32_t)((uint32_t)frame->incoming + location.stack_offset);
            Tier0Part parts[2];
            const uint32_t count = tier0_location_parts(location, function->local_sizes[p], parts);
            for (uint32_t k = 0; k < count; k += 1)
            {
                if (parts[k].reg == BUSTER_TIER0_PART_STACK)
                {
                    tier0_put(writer, tier0_instruction(X86_FORM_MOV_R64_RM64, X86_REGISTER_RAX, base, BUSTER_X86_MEMORY_BASE, incoming, 0));
                    tier0_store_part(writer, X86_REGISTER_RAX, 8, homes[p], parts[k].offset);
                }
            }
            if (tier0_aggregate_whole(location) && !tier0_parameter_in_place(function, p))
            {
                tier0_copy(writer, homes[p].reg, homes[p].displacement, base, incoming, function->local_sizes[p], X86_REGISTER_RAX, 15);
            }
        }
    }
    for (uint32_t p = 0; p < function->parameter_count; p += 1)
    {
        const AbiLocation location = function->parameters[p];
        const uint32_t class = function->local_classes[p];
        const bool address_taken = (function->local_flags[p] & BUSTER_TYPED_LOCAL_ADDRESS_TAKEN) != 0;
        if (objects && tier0_is_object(function, p))
        {
            Tier0Part parts[2];
            const uint32_t count = references[p] != 0 ? tier0_location_parts(location, function->local_sizes[p], parts) : 0;
            for (uint32_t k = 0; k < count; k += 1)
            {
                if (parts[k].reg != BUSTER_TIER0_PART_STACK)
                {
                    tier0_store_part(writer, parts[k].reg, parts[k].bytes, homes[p], parts[k].offset);
                }
            }
        }
        else if (location.kind == BUSTER_ABI_LOCATION_STACK && homes[p].is_register)
        {
            const int32_t incoming = (int32_t)((uint32_t)frame->incoming + location.stack_offset);
            tier0_put(writer, tier0_instruction(tier0_value_load_form(class), homes[p].reg, base, BUSTER_X86_MEMORY_BASE, incoming, 0));
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

// How many records tier0_emit_prologue writes, without writing them: the pushes, a frame pointer's mov, the frame's sub,
// a realigned frame's and and its base's mov, and each parameter's move, store or load into its home (an aggregate's part
// stores and slot copies), after the incoming variadic saves (a store per argument register) when it has them. The
// epilogue's are tier0_count_epilogue's.
static uint32_t tier0_count_prologue(const Tier0Function* function, const Tier0Frame* frame, const Tier0Home* homes, const uint32_t* references)
{
    uint32_t count = frame->pushes + (frame->pointer ? 1 : 0) + (frame->size != 0 ? 1 : 0) + (frame->alignment_log2 != 0 ? 1 : 0) + (frame->based ? 1 : 0);
    count += function->incoming_variadic != 0 ? BUSTER_ABI_INTEGER_ARGUMENTS + BUSTER_ABI_VECTOR_ARGUMENTS : 0;
    const bool objects = frame->object_parameters;
    for (uint32_t p = 0; p < function->parameter_count; p += 1)
    {
        const AbiLocation location = function->parameters[p];
        const bool address_taken = (function->local_flags[p] & BUSTER_TYPED_LOCAL_ADDRESS_TAKEN) != 0;
        const bool value = !objects || !tier0_is_object(function, p);
        const bool arrives = value && location.kind == BUSTER_ABI_LOCATION_REGISTERS && (references[p] != 0 || address_taken);
        const bool stays = homes[p].is_register && homes[p].reg == (location.registers[0] & 0x1f);
        count += value && location.kind == BUSTER_ABI_LOCATION_STACK && homes[p].is_register ? 1 : 0;
        count += arrives && !stays ? 1 : 0;
    }
    // An aggregate's: a store per part in a register, a load and a store for its part in a stack slot, and the copy of its
    // slots when it doesn't keep them.
    for (uint32_t p = 0; p < function->parameter_count && objects; p += 1)
    {
        if (tier0_is_object(function, p) && references[p] != 0)
        {
            const AbiLocation location = function->parameters[p];
            Tier0Part parts[2];
            const uint32_t part_count = tier0_location_parts(location, function->local_sizes[p], parts);
            for (uint32_t k = 0; k < part_count; k += 1)
            {
                count += parts[k].reg == BUSTER_TIER0_PART_STACK ? 2 : 1;
            }
            count += tier0_aggregate_whole(location) && !tier0_parameter_in_place(function, p) ? tier0_copy_records(function->local_sizes[p]) : 0;
        }
    }
    return count;
}

// An aggregate result's registers (result_size) loaded from the address its RETURN leaves in rax (tier0_emit_epilogue's):
// the TEMPORARY parts' bytes copied to their red zone temporaries through rcx (no result register), the parts in other
// registers than rax, then rax's. Out of line: a function returning an aggregate in registers alone has them.
static __attribute__((noinline)) void tier0_load_result(const Tier0Function* function, Tier0Writer* writer)
{
    Tier0Part parts[2];
    // An x87 result's parts (a struct of a long double's) loaded from the last, so that st0 holds the first.
    const uint32_t x87 = tier0_x87_parts(function->result);
    for (uint32_t k = x87; k > 0; k -= 1)
    {
        tier0_put(writer, tier0_instruction(X86_FORM_FLD_M80, 0, X86_REGISTER_RAX, BUSTER_X86_MEMORY_BASE, 16 * (int32_t)(k - 1), 0));
    }
    const uint32_t count = x87 == 0 ? tier0_result_parts(function->result, function->result_size, parts) : 0;
    for (uint32_t k = 0; k < count; k += 1)
    {
        if (parts[k].load == BUSTER_TIER0_PART_TEMPORARY)
        {
            tier0_copy(writer, X86_REGISTER_RSP, tier0_temporary(k), X86_REGISTER_RAX, parts[k].offset, parts[k].bytes, X86_REGISTER_RCX, 0);
        }
    }
    for (uint32_t pass = 0; pass < 2; pass += 1)
    {
        for (uint32_t k = 0; k < count; k += 1)
        {
            if ((parts[k].reg == X86_REGISTER_RAX) == (pass == 1))
            {
                tier0_load_part(writer, parts[k], X86_REGISTER_RAX, k);
            }
        }
    }
}

// How many records tier0_load_result writes: each part's loads, and a TEMPORARY one's copy. Out of line, as it is.
static __attribute__((noinline)) uint32_t tier0_result_records(const Tier0Function* function)
{
    Tier0Part parts[2];
    const uint32_t x87 = tier0_x87_parts(function->result);
    const uint32_t count = x87 == 0 ? tier0_result_parts(function->result, function->result_size, parts) : 0;
    uint32_t records = x87;
    for (uint32_t k = 0; k < count; k += 1)
    {
        records += tier0_part_records(parts[k]) + (parts[k].load == BUSTER_TIER0_PART_TEMPORARY ? tier0_copy_records(parts[k].bytes) : 0);
    }
    return records;
}

// Whether a function returns a long double's value, in st0 from slot 0, where every RETURN leaves it.
static bool tier0_x87_returns(const Tier0Function* function)
{
    return function->result_size == 0 && tier0_x87_parts(function->result) != 0;
}

// The epilogue: an aggregate result's registers loaded (tier0_load_result), or a long double's st0 from slot 0, while the
// frame they may be in (and what the body allocated) is still there; then the frame released (with a frame pointer, rsp
// back from rbp, past what the body allocated), the saved registers popped (and rbp last), ret.
static void tier0_emit_epilogue(const Tier0Function* function, const Tier0Frame* frame, Tier0Writer* writer)
{
    if (function->result_size != 0)
    {
        tier0_load_result(function, writer);
    }
    else if (frame->x87_result)
    {
        tier0_put(writer, tier0_memory_slot(X86_FORM_FLD_M80, frame, 0, 0));
    }
    if (frame->pointer)
    {
        const int32_t pushed = (int32_t)(8 * (frame->pushes - 1));
        tier0_put(writer, pushed != 0 ? tier0_instruction(X86_FORM_LEA_R64_M, X86_REGISTER_RSP, X86_REGISTER_RBP, BUSTER_X86_MEMORY_BASE, -pushed, 0)
                                      : tier0_instruction(X86_FORM_MOV_RM64_R64, X86_REGISTER_RBP, X86_REGISTER_RSP, BUSTER_X86_REGISTER_DIRECT, 0, 0));
    }
    else if (frame->size != 0)
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
    if (frame->pointer)
    {
        tier0_put(writer, tier0_instruction(X86_FORM_POP_R64, 0, X86_REGISTER_RBP, 0, 0, 0));
    }
    tier0_put(writer, tier0_instruction(X86_FORM_RET, 0, 0, 0, 0, 0));
}

// How many records tier0_emit_epilogue writes, without writing them: the result's loads (tier0_result_records), the
// frame's release (an add, or with a frame pointer the lea or mov), the pops and the ret.
static uint32_t tier0_count_epilogue(const Tier0Function* function, const Tier0Frame* frame)
{
    const uint32_t records = (frame->size != 0 || frame->pointer ? 1 : 0) + frame->pushes + 1;
    return records + (function->result_size != 0 ? tier0_result_records(function) : frame->x87_result ? 1 : 0);
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
// template (the count if none), and sets *error to TIER0_ERROR_DEPTH for depths past the tables. `memory` is
// tier0_memory_function's and `realigned` whether the frame is (Tier0Frame.alignment_log2): constants at tier0_select's
// inline use, both false, so that a function without values of the memory classes or a realigned frame has the loop
// without a look for their entries (#423's measurements) and without the frame's alignment, which its other entries
// don't read; the out-of-line use takes either from the function.
static inline __attribute__((always_inline)) uint32_t tier0_select_entries(const Tier0Function* function, Tier0Entries* entries, Tier0Frame* frame,
                                                                           Tier0Error* error, bool memory, bool realigned)
{
    uint32_t unsupported = function->count;
    int32_t integer_top = -1;
    int32_t floating_top = -1;
    int32_t slot_top = -1;
    Tier0Tops convert_scratch[2] = { { -1, -1 }, { -1, -1 } };
    uint32_t clobbers = 0;
    uint32_t save_slots = 0;
    bool fixed = false;
    bool calls = false;
    bool moves = false;
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
                else if (tier0_moves_stack(function->kinds[i]))
                {
                    // The frame pointer's; STACK_SAVE's and FRAME_ADDRESS's value at its depth (ALLOCA's and
                    // STACK_RESTORE's operand is the entry before's).
                    const bool leaf = function->kinds[i] == BUSTER_TYPED_STACK_SAVE || function->kinds[i] == BUSTER_TYPED_FRAME_ADDRESS;
                    integer_top = leaf && depth > integer_top ? depth : integer_top;
                    moves = true;
                }
                else if (memory && tier0_vector_entry(entries, i))
                {
                    tier0_vector_tops(function, entries, i, &integer_top, &floating_top, &slot_top);
                }
                else if (memory && tier0_memory_entry(entries, i))
                {
                    tier0_memory_tops(function, entries, i, &integer_top, &floating_top, &slot_top, &clobbers, &fixed);
                }
                else if (function->kinds[i] == BUSTER_TYPED_CONVERT)
                {
                    // A conversion between U64 and a floating class (tier0_emit_u64_convert): its result's register, and its
                    // register form's scratch ones, which the tops take once the frame pointer is known.
                    Tier0Tops tops = { integer_top, floating_top };
                    tier0_select_u64_convert(function->classes[i] == BUSTER_TYPED_CLASS_U64, depth, floating, realigned, &tops, convert_scratch);
                    integer_top = tops.integer;
                    floating_top = tops.floating;
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
    // The conversions' register forms' scratch registers, as the entries that move rsp decide them.
    const Tier0Tops convert = convert_scratch[moves ? 1 : 0];
    integer_top = convert.integer > integer_top ? convert.integer : integer_top;
    floating_top = convert.floating > floating_top ? convert.floating : floating_top;
    // An aggregate's copies take each stack's first free register.
    if (function->argument_sizes != 0)
    {
        const Tier0Tops tops = tier0_select_scratch(function, entries, (Tier0Tops){ integer_top, floating_top });
        integer_top = tops.integer;
        floating_top = tops.floating;
    }
    const int32_t depth_count = tier0_integer_depth_count(realigned, moves);
    *error = integer_top >= depth_count || floating_top >= BUSTER_TIER0_FLOATING_DEPTHS || slot_top >= BUSTER_TIER0_INTEGER_DEPTHS ? TIER0_ERROR_DEPTH : TIER0_OK;
    frame->integer_top = integer_top;
    frame->floating_top = floating_top;
    frame->clobbers = clobbers | tier0_depth_registers(integer_top, floating_top);
    frame->save_slots = save_slots;
    frame->fixed_slots = fixed ? 2 : 0;
    frame->calls = calls;
    tier0_frame_registers(frame, moves, depth_count);
    return unsupported;
}

// The selection of a function with values of the memory classes or a realigned frame, out of line.
static __attribute__((noinline)) uint32_t tier0_select_memory(const Tier0Function* function, Tier0Entries* entries, Tier0Frame* frame, Tier0Error* error)
{
    return tier0_select_entries(function, entries, frame, error, tier0_memory_function(function), frame->alignment_log2 != 0);
}

static uint32_t tier0_select(const Tier0Function* function, Tier0Entries* entries, Tier0Frame* frame, Tier0Error* error)
{
    const bool rare = tier0_memory_function(function) || frame->alignment_log2 != 0;
    return rare ? tier0_select_memory(function, entries, frame, error) : tier0_select_entries(function, entries, frame, error, false, false);
}

// What a deferred slot's emitter (but a call's) uses of each stack (0, the integer one, or 1): whether it uses its
// registers, how far below the depth it reads (its window) and the highest depth it writes, as tier0_select_deep reads a
// template's from its reach and window. `depth_count` is the function's integer depths (tier0_integer_depth_count). A
// long double's are never asked for.
static void tier0_deferred_uses(const Tier0Function* function, const Tier0Entries* entries, uint32_t i, const uint32_t* before, int32_t depth_count, bool* uses,
                                uint32_t* window, int32_t* touch)
{
    const uint32_t kind = entries->key_kinds[i];
    if (kind == BUSTER_TYPED_COPY)
    {
        // Its two addresses, and its scratch at the depth (an XMM register's from 16 bytes).
        const bool wide = function->values[i] >= 16;
        uses[0] = true;
        window[0] = 2;
        touch[0] = wide ? (int32_t)before[0] - 1 : (int32_t)before[0];
        uses[1] = wide;
        touch[1] = (int32_t)before[1];
    }
    else if (kind == BUSTER_TYPED_CONVERT)
    {
        // A conversion between U64 and a floating class: its operand on one stack and its result on the other, and in its
        // register form the stacks' first free registers (to U64, the integer stack's next one too), where
        // tier0_u64_convert_registers chooses that form at these depths, as selection did and its emission does.
        const uint32_t to = tier0_class_floating(entries->key_classes[i]) ? 1 : 0;
        const bool registers = tier0_u64_convert_registers(to == 0, before[0], before[1], depth_count);
        uses[0] = true;
        uses[1] = true;
        window[1 - to] = 1;
        touch[0] = to == 0 ? (int32_t)before[0] + (registers ? 1 : 0) : (int32_t)before[0] - (registers ? 0 : 1);
        touch[1] = to == 0 ? (int32_t)before[1] - (registers ? 0 : 1) : (int32_t)before[1];
    }
    else
    {
        // ALLOCA, STACK_SAVE, STACK_RESTORE and FRAME_ADDRESS: an operand, a result, or both, at their depths.
        const bool operand = kind != BUSTER_TYPED_STACK_SAVE && kind != BUSTER_TYPED_FRAME_ADDRESS;
        uses[0] = true;
        window[0] = operand ? 1 : 0;
        touch[0] = operand ? (int32_t)before[0] - 1 : (int32_t)before[0];
    }
}

// The deep entries of a function whose stacks go past the depth tables (selection's TIER0_ERROR_DEPTH): their values in
// the frame's deep slots. At each entry's start a stack's values are where its depth says, all in their registers up to
// the table's depth count (14 integer ones with a frame pointer, 15 without; 16 floating ones) and all in their slots past
// it, so that every way into a label agrees. An entry is deep on a stack where its exit (a JUMP_VALUE's at its target)
// and its start are on the two sides of that line, or where it uses the stack's registers past it or reaches past the
// table. It loads the values it reads (its window, 2 at most) from their slots into the stack's first registers, after
// storing the registers into their slots if they were there, makes its own records at that depth (the depth columns hold
// it from here on: tier0_emit_deep), stores its result into its slot, and loads the registers back if its exit is within
// the table. A call deep on either stack takes its arguments from their slots (tier0_emit_deep_call). Every deep entry is
// deferred, in both paths, and counted in *deep_count. Returns TIER0_OK, or TIER0_ERROR_DEPTH where deep values don't
// reach: a function with memory-class values (whose slots its depths index), or a stack deeper than BUSTER_TIER0_MAX_DEPTH, which the depth
// columns' bytes would wrap past.
static Tier0Error tier0_select_deep(const Tier0Function* function, Tier0Entries* entries, Tier0Frame* frame, Arena* scratch, bool vector, uint32_t* deep_count)
{
    (void)vector;

    const uint32_t count = function->count;
    const uint32_t limits[2] = { (uint32_t)frame->depth_count, BUSTER_TIER0_FLOATING_DEPTHS };
    const uint8_t window_flags[2] = { BUSTER_TIER0_DEEP_INTEGER, BUSTER_TIER0_DEEP_FLOATING };
    const uint8_t spill_flags[2] = { BUSTER_TIER0_DEEP_SPILL_INTEGER, BUSTER_TIER0_DEEP_SPILL_FLOATING };
    const uint8_t fill_flags[2] = { BUSTER_TIER0_DEEP_FILL_INTEGER, BUSTER_TIER0_DEEP_FILL_FLOATING };
    entries->deep = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)count + 1);
    entries->deep_depths = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)count + 1);
    entries->deep_floating_depths = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)count + 1);
    Tier0Error error = entries->deep != 0 && entries->deep_depths != 0 && entries->deep_floating_depths != 0 ? TIER0_OK : TIER0_ERROR_MEMORY;
    error = error == TIER0_OK && tier0_memory_function(function) ? TIER0_ERROR_DEPTH : error;
    uint32_t slots[2] = { 0, 0 };
    for (uint32_t i = 0; i < count && error == TIER0_OK; i += 1)
    {
        // A stack's depth rises by one entry at a time at most, so one past the limit would have shown here first.
        const uint32_t before[2] = { entries->depths[i], entries->floating_depths[i] };
        error = before[0] >= BUSTER_TIER0_MAX_DEPTH || before[1] >= BUSTER_TIER0_MAX_DEPTH ? TIER0_ERROR_DEPTH : TIER0_OK;
        uint32_t flags = 0;
        uint32_t window[2] = { 0, 0 };
        if (entries->absorbed[i] == 0 && error == TIER0_OK)
        {
            const uint8_t kind = function->kinds[i];
            const uint32_t stack = tier0_stack(function->classes[i]);
            const bool jump_value = kind == BUSTER_TYPED_JUMP_VALUE;
            const uint32_t exit[2] = { (uint8_t)((int32_t)before[0] + entries->integer_effects[i] + (jump_value && stack == 1 ? 1 : 0)),
                                       (uint8_t)((int32_t)before[1] + entries->floating_effects[i] + (jump_value && stack == 2 ? 1 : 0)) };
            if (kind == BUSTER_TYPED_CALL)
            {
                const uint32_t call = (uint32_t)function->values[i];
                bool aggregates = false;
                for (uint32_t a = 0; a < function->call_argument_counts[call] && function->argument_sizes != 0; a += 1)
                {
                    aggregates |= function->argument_sizes[function->call_first[call] + a] != 0;
                }
                const bool deep = before[0] > limits[0] || exit[0] > limits[0] || before[1] > limits[1] || exit[1] > limits[1] ||
                                  (aggregates && (before[0] >= limits[0] || before[1] >= limits[1]));
                for (uint32_t s = 0; s < 2 && deep; s += 1)
                {
                    flags |= BUSTER_TIER0_DEEP_CALL | (before[s] <= limits[s] ? spill_flags[s] : 0) | (exit[s] <= limits[s] ? fill_flags[s] : 0);
                    slots[s] = before[s] > slots[s] ? before[s] : slots[s];
                    slots[s] = exit[s] > slots[s] ? exit[s] : slots[s];
                }
            }
            else
            {
                bool uses[2] = { false, false };
                int32_t touch[2] = { -1, -1 };
                const uint16_t slot = tier0_slot(entries->key_kinds[i], entries->key_classes[i], entries->variants[i]);
                if (slot != BUSTER_TIER0_SLOT_DEFERRED && slot != BUSTER_TIER0_SLOT_UNSUPPORTED)
                {
                    // Its registers, or the values its window reads through its fixed registers alone (division's
                    // dividend in rax, with its divisor a local's home).
                    const Tier0TemplateInfo* info = &tier0_template_infos[slot & BUSTER_TIER0_SLOT_TEMPLATE];
                    window[0] = info->window & 3;
                    window[1] = info->window >> 2 & 3;
                    uses[0] = info->integer_reach != 0 || window[0] != 0;
                    uses[1] = info->floating_reach != 0 || window[1] != 0;
                    touch[0] = (int32_t)before[0] + info->integer_reach - 3;
                    touch[1] = (int32_t)before[1] + info->floating_reach - 3;
                }
                else
                {
                    tier0_deferred_uses(function, entries, i, before, frame->depth_count, uses, window, touch);
                }
                for (uint32_t s = 0; s < 2; s += 1)
                {
                    const bool crosses = (before[s] > limits[s]) != (exit[s] > limits[s]);
                    const bool deep = crosses || (uses[s] && (before[s] > limits[s] || touch[s] >= (int32_t)limits[s]));
                    flags |= deep ? window_flags[s] | (before[s] <= limits[s] ? spill_flags[s] : 0) | (exit[s] <= limits[s] ? fill_flags[s] : 0) : 0;
                    slots[s] = deep && before[s] > slots[s] ? before[s] : slots[s];
                    slots[s] = deep && exit[s] > slots[s] ? exit[s] : slots[s];
                }
            }
        }
        entries->deep[i] = (uint8_t)flags;
        *deep_count += flags != 0 ? 1 : 0;
        if (flags != 0)
        {
            // Its depths, and the depths its records are made at on the stacks it is deep on: its windows'.
            entries->deep_depths[i] = (uint8_t)before[0];
            entries->deep_floating_depths[i] = (uint8_t)before[1];
            entries->depths[i] = (flags & BUSTER_TIER0_DEEP_INTEGER) != 0 ? (uint8_t)window[0] : entries->depths[i];
            entries->floating_depths[i] = (flags & BUSTER_TIER0_DEEP_FLOATING) != 0 ? (uint8_t)window[1] : entries->floating_depths[i];
            if (BUSTER_VECTOR_PATH(vector))
            {
                entries->words[i] |= BUSTER_TIER0_WORD_DEFERRED;
            }
            else
            {
                entries->templates[i] = BUSTER_TIER0_ENTRY_DEFERRED;
            }
        }
    }
    if (error == TIER0_OK)
    {
        // The depth registers within the tables, which the deep entries' records keep to (never a frame pointer's rbp, or a
        // realigned frame's base, r15).
        frame->deep_slots = slots[0];
        frame->deep_floating_slots = slots[1];
        frame->integer_top = frame->integer_top < (int32_t)limits[0] ? frame->integer_top : (int32_t)limits[0] - 1;
        frame->floating_top = frame->floating_top < (int32_t)limits[1] ? frame->floating_top : (int32_t)limits[1] - 1;
        frame->clobbers &= (frame->pointer ? ~(1u << X86_REGISTER_RBP) : ~0u) & (frame->based ? ~(1u << X86_REGISTER_R15) : ~0u);
    }
    return error;
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

// The scalar path's measuring. `deep` is whether the function has deep entries (tier0_select_deep's), a constant at each
// of tier0_measure's two uses, so that a function without them is measured as before they existed; a deep entry's
// template may add a branch and a choice request (tier0_emit_deep's).
static inline __attribute__((always_inline)) Tier0Counts tier0_measure_entries(const Tier0Function* function, Tier0Entries* entries, const Tier0Frame* frame,
                                                                              const Tier0Home* homes, bool deep)
{
    Tier0Counts counts = { 0, 0, 0 };
    for (uint32_t i = 0; i < function->count; i += 1)
    {
        const uint16_t entry = entries->templates[i];
        uint32_t length = 0;
        if (tier0_is_deferred(entry))
        {
            Tier0Writer counter = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };
            if (deep)
            {
                tier0_emit_deferred_deep(function, entries, frame, homes, i, &counter);
            }
            else
            {
                tier0_emit_deferred(function, entries, frame, homes, i, &counter);
            }
            length = counter.count;
            counts.references += counter.reference_count;
            counts.branches += deep ? counter.branch_count : 0;
            counts.choices += deep ? counter.choice_count : 0;
        }
        else if (entry != BUSTER_TIER0_ENTRY_ABSORBED)
        {
            const uint32_t index = tier0_home_case(function, homes, i, entry);
            if ((entry & BUSTER_TIER0_SLOT_HOME_DEPENDENT) != 0)
            {
                entries->templates[i] = (uint16_t)index;
            }
            const Tier0TemplateInfo* info = &tier0_template_infos[index];
            length = info->length;
            counts.branches += info->branch != 0xff ? 1 : 0;
            counts.references += info->fixup != 0xff ? 1 : 0;
            counts.choices += info->choice != 0xff ? 1 : 0;
        }
        entries->lengths[i] = length;
    }
    return counts;
}

// The scalar measuring of a function with deep entries, out of line.
static __attribute__((noinline)) Tier0Counts tier0_measure_deep(const Tier0Function* function, Tier0Entries* entries, const Tier0Frame* frame, const Tier0Home* homes)
{
    return tier0_measure_entries(function, entries, frame, homes, true);
}

static Tier0Counts tier0_measure(const Tier0Function* function, Tier0Entries* entries, const Tier0Frame* frame, const Tier0Home* homes)
{
    return entries->deep != 0 ? tier0_measure_deep(function, entries, frame, homes) : tier0_measure_entries(function, entries, frame, homes, false);
}

// How many records tier0_emit_deep writes for entry i, with its references and branch counted into `counts`, from what
// the emitter decides rather than by running it (tier0_count_deferred's way): the vector path's measuring.
static uint32_t tier0_count_deep(const Tier0Function* function, const Tier0Entries* entries, const Tier0Home* homes, uint32_t i, Tier0Counts* counts)
{
    const uint32_t flags = entries->deep[i];
    const Tier0DeepEntry deep = tier0_deep_entry(function, entries, i);
    uint32_t count = 0;
    count += (flags & BUSTER_TIER0_DEEP_SPILL_INTEGER) != 0 ? deep.before[0] : 0;
    count += (flags & BUSTER_TIER0_DEEP_SPILL_FLOATING) != 0 ? deep.before[1] : 0;
    count += (flags & BUSTER_TIER0_DEEP_FILL_INTEGER) != 0 ? deep.exit[0] : 0;
    count += (flags & BUSTER_TIER0_DEEP_FILL_FLOATING) != 0 ? deep.exit[1] : 0;
    if ((flags & BUSTER_TIER0_DEEP_CALL) != 0)
    {
        // Each argument's load (and its store, on the stack) with a narrow integer's extension, r11's, the hidden
        // pointer's lea, al, the call, and the result's store (or a result object's).
        const uint32_t call = (uint32_t)function->values[i];
        const uint32_t first = function->call_first[call];
        for (uint32_t a = 0; a < function->call_argument_counts[call]; a += 1)
        {
            const uint32_t class = function->argument_classes[first + a];
            const AbiLocation location = function->argument_locations[first + a];
            const uint32_t kind = location.kind;
            const uint32_t size = function->argument_sizes != 0 ? function->argument_sizes[first + a] : 0;
            const uint32_t extension = tier0_class_integer(class) && tier0_class_bytes(class) < 4 ? 1 : 0;
            if (size != 0)
            {
                uint8_t own = 0;
                uint32_t own_set = 0;
                count += tier0_aggregate_records(location, size, &own, &own_set);
                // Whole stack arguments need the address once; register/split arguments in both passes.
                Tier0Part parts[2];
                count += tier0_aggregate_whole(location) ? 1 : tier0_location_parts(location, size, parts) != 0 ? 2 : 0;
            }
            else
            {
                count += kind == BUSTER_ABI_LOCATION_STACK ? 2 + extension : kind == BUSTER_ABI_LOCATION_REGISTERS ? 1 + extension : 0;
            }
        }
        const bool indirect = function->call_targets[call] == 0;
        const uint32_t object = function->call_result_objects != 0 ? function->call_result_objects[call] : 0;
        const bool stored = object != 0 && function->calls[call].result.kind == BUSTER_ABI_RESULT_REGISTERS;
        count += indirect ? 1 : 0;
        count += object != 0 ? tier0_result_object_records(function->calls[call].result) : 0;
        count += function->call_variadic[call] ? 1 : 0;
        count += 1;
        count += !stored && tier0_stack(function->classes[i]) != 0 ? 1 : 0;
        counts->references += indirect ? 0 : 1;
    }
    else
    {
        // Each window's loads and its result's store, and its own records.
        count += (flags & BUSTER_TIER0_DEEP_INTEGER) != 0 ? deep.window[0] + (deep.result[0] ? 1u : 0u) : 0;
        count += (flags & BUSTER_TIER0_DEEP_FLOATING) != 0 ? deep.window[1] + (deep.result[1] ? 1u : 0u) : 0;
        const uint16_t slot = tier0_slot(entries->key_kinds[i], entries->key_classes[i], entries->variants[i]);
        if (tier0_deep_template(slot, entries->depths[i]))
        {
            const Tier0TemplateInfo* info = &tier0_template_infos[tier0_home_case(function, homes, i, slot)];
            count += info->length;
            counts->references += info->fixup != 0xff ? 1 : 0;
            counts->branches += info->branch != 0xff ? 1 : 0;
            counts->choices += info->choice != 0xff ? 1 : 0;
        }
        else
        {
            count += tier0_count_deferred(function, entries, i, false, &counts->references);
        }
    }
    return count;
}

// The branch columns in record order, for the vector path, whose emission writes a block's deep entries' branches
// (tier0_emit_deep's) after its templates': an insertion sort, which moves only those.
static void tier0_sort_branches(uint32_t* branches, uint32_t* targets, uint32_t count)
{
    for (uint32_t k = 1; k < count; k += 1)
    {
        const uint32_t branch = branches[k];
        const uint32_t target = targets[k];
        uint32_t j = k;
        while (j > 0 && branches[j - 1] > branch)
        {
            branches[j] = branches[j - 1];
            targets[j] = targets[j - 1];
            j -= 1;
        }
        branches[j] = branch;
        targets[j] = target;
    }
}

// Each entry's first record, after the prologue (the entry past the last is the epilogue's first record).
static void tier0_positions_scalar(const Tier0Function* function, Tier0Entries* entries, uint32_t prologue)
{
    uint64_t position = prologue;
    for (uint32_t i = 0; i < function->count; i += 1)
    {
        entries->positions[i] = (uint32_t)position;
        position += entries->lengths[i];
    }
    entries->record_count = position;
    entries->positions[function->count] = (uint32_t)position;
}

// The output's columns, pushed from `output`: room for the records and, past them, the vector expansion's last store;
// and the entries' first records (`entries` of them and the epilogue's) when asked for (`positions` not null).
static bool tier0_push_output(Tier0Code* code, Arena* output, uint64_t records, Tier0Counts counts, const uint32_t* positions, uint32_t entries)
{
    // Both position passes accumulate in 64 bits. Refuse before allocation or publication when the record IDs,
    // including the vector emitter's padding, would wrap; narrowed scratch positions are never consumed then.
    bool room = records <= UINT32_MAX - BUSTER_TIER0_TEMPLATE_RECORDS;
    if (room)
    {
        code->records = BUSTER_ARENA_PUSH_ARRAY(output, X86Instruction, records + BUSTER_TIER0_TEMPLATE_RECORDS);
        code->branches = BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, (uint64_t)counts.branches + 1);
        code->targets = BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, (uint64_t)counts.branches + 1);
        code->references = BUSTER_ARENA_PUSH_ARRAY(output, LinkReference, (uint64_t)counts.references + 1);
        // The entries' positions are final once measured: emission writes each entry's records from its position.
        code->entry_records = positions != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, (uint64_t)entries + 1) : 0;
        code->count = (uint32_t)records;
        code->entry_count = positions != 0 ? entries : 0;
        room = code->records != 0 && code->branches != 0 && code->targets != 0 && code->references != 0 && (positions == 0 || code->entry_records != 0);
        if (room && positions != 0)
        {
            memcpy(code->entry_records, positions, ((uint64_t)entries + 1) * sizeof(uint32_t));
        }
    }
    return room;
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


// Emission for the scalar path, in entry order: the prologue; each entry's template expanded at its place, field by
// field from its specifications, its choice request, branch and reference, or a deferred entry's records; then the
// epilogue. tier0_emit_vector is its twin. `deep` is tier0_measure_entries': with deep entries, the branch count goes
// to their emitter in the writer and back (a deep entry's branch comes in entry order, as a template's does).
static inline __attribute__((always_inline)) void tier0_emit_body_entries(const Tier0Function* function, const Tier0Entries* entries, const Tier0Frame* frame,
                                                                         const Tier0Home* homes, const uint32_t* references, Tier0Code* code, Tier0Choices* choices,
                                                                         bool deep)
{
    Tier0Writer writer = { code->records, 0, code->references, 0, code->branches, code->targets, 0, choices, 0 };
    tier0_emit_prologue(function, frame, homes, references, &writer);
    uint32_t branches = 0;
    for (uint32_t i = 0; i < function->count; i += 1)
    {
        const uint16_t entry = entries->templates[i];
        const uint32_t position = entries->positions[i];
        if (tier0_is_deferred(entry))
        {
            writer.count = position;
            if (deep)
            {
                writer.branch_count = branches;
                tier0_emit_deferred_deep(function, entries, frame, homes, i, &writer);
                branches = writer.branch_count;
            }
            else
            {
                tier0_emit_deferred(function, entries, frame, homes, i, &writer);
            }
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
                    .field = info->fixup_field,
                    .kind = info->fixup_kind,
                    .target = (SymbolId)function->values[i],
                    .addend = 0,
                };
                writer.reference_count += 1;
            }
        }
    }
    writer.count = entries->positions[function->count];
    tier0_emit_epilogue(function, frame, &writer);
    code->branch_count = branches;
    code->reference_count = writer.reference_count;
}

// The scalar emission of a function with deep entries, out of line.
static __attribute__((noinline)) void tier0_emit_body_deep(const Tier0Function* function, const Tier0Entries* entries, const Tier0Frame* frame, const Tier0Home* homes,
                                                           const uint32_t* references, Tier0Code* code, Tier0Choices* choices)
{
    tier0_emit_body_entries(function, entries, frame, homes, references, code, choices, true);
}

static void tier0_emit_body(const Tier0Function* function, const Tier0Entries* entries, const Tier0Frame* frame, const Tier0Home* homes, const uint32_t* references,
                            Tier0Code* code, Tier0Choices* choices)
{
    if (entries->deep != 0)
    {
        tier0_emit_body_deep(function, entries, frame, homes, references, code, choices);
    }
    else
    {
        tier0_emit_body_entries(function, entries, frame, homes, references, code, choices, false);
    }
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
    entries->lengths = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, padded);
    entries->positions = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, padded);
    entries->words = BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, padded);
    entries->deep = 0;
    entries->deep_depths = 0;
    entries->deep_floating_depths = 0;
    return entries->words != 0 && entries->key_kinds != 0 && entries->key_classes != 0 && entries->variants != 0 && entries->absorbed != 0 && entries->integer_effects != 0 &&
           entries->floating_effects != 0 && entries->depths != 0 && entries->floating_depths != 0 && entries->templates != 0 && entries->lengths != 0 &&
           entries->positions != 0;
}

static uint32_t tier0_select_vector(const Tier0Function* function, Tier0Entries* entries, Tier0Frame* frame, Tier0Error* error);
static Tier0Counts tier0_measure_vector(const Tier0Function* function, Tier0Entries* entries, const Tier0Home* homes, uint32_t prologue, bool memory);
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
    (void)vector;

    Tier0Frame frame;
    memset(&frame, 0, sizeof(frame));
    Tier0Error error = tier0_frame_input_valid(function) ? TIER0_OK : TIER0_ERROR_UNSUPPORTED;
    // The frame's alignment past 16 bytes, if it needs one, which the integer depths the selections leave depend on.
    frame.alignment_log2 = error == TIER0_OK ? (uint8_t)(BUSTER_VECTOR_PATH(vector) ? tier0_frame_alignment_vector(function) : tier0_frame_alignment(function)) : 0;
    const uint32_t unsupported = error == TIER0_OK ? (BUSTER_VECTOR_PATH(vector) ? tier0_select_vector(function, entries, &frame, &error) : tier0_select(function, entries, &frame, &error)) : function->count;
    if (error == TIER0_ERROR_DEPTH && unsupported == function->count)
    {
        error = tier0_select_deep(function, entries, &frame, scratch, BUSTER_VECTOR_PATH(vector), &code->deep);
    }
    // The memory classes' slots, a function with their values' (whose long double result, if one, leaves st0 from slot 0),
    // and the vectors'.
    frame.memory = tier0_memory_function(function);
    frame.memory_slots = function->x87 || function->int128;
    frame.vectors = function->vectors;
    frame.x87_result = function->x87 && tier0_x87_returns(function);
    if (unsupported != function->count)
    {
        error = TIER0_ERROR_UNSUPPORTED;
        code->node = unsupported;
    }
    const uint32_t locals = function->local_count;
    Tier0Home* homes = BUSTER_ARENA_PUSH_ARRAY(scratch, Tier0Home, locals + 1);
    uint32_t* references = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, locals + 1);
    uint32_t order[2 * BUSTER_TIER0_HOME_CANDIDATES];
    if (error == TIER0_OK && (homes == 0 || references == 0))
    {
        error = TIER0_ERROR_MEMORY;
    }
    Tier0Choices choices;
    memset(&choices, 0, sizeof(choices));
    if (error == TIER0_OK)
    {
        if (BUSTER_VECTOR_PATH(vector))
        {
            tier0_count_references_vector(function, references);
        }
        else
        {
            tier0_count_references(function, references);
        }
        frame.object_parameters = tier0_object_parameters(function);
        tier0_assign_homes(function, &frame, homes, references, order, BUSTER_VECTOR_PATH(vector));
        error = tier0_layout_frame(function, &frame, homes, references, BUSTER_VECTOR_PATH(vector)) ? TIER0_OK : TIER0_ERROR_UNSUPPORTED;
    }
    if (error == TIER0_OK)
    {
        Tier0Counts counts;
        uint32_t epilogue = 0;
        if (BUSTER_VECTOR_PATH(vector))
        {
            counts = tier0_measure_vector(function, entries, homes, tier0_count_prologue(function, &frame, homes, references), frame.memory);
            epilogue = tier0_count_epilogue(function, &frame);
        }
        else
        {
            Tier0Writer counter = { 0, 0, 0, 0 };
            tier0_emit_prologue(function, &frame, homes, references, &counter);
            counts = tier0_measure(function, entries, &frame, homes);
            tier0_positions_scalar(function, entries, counter.count);
            counter.count = 0;
            tier0_emit_epilogue(function, &frame, &counter);
            epilogue = counter.count;
        }
        // The requests' columns have room for the vector path's last compressed store of 8.
        choices.records = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, counts.choices + 8);
        choices.rows = BUSTER_ARENA_PUSH_ARRAY(scratch, uint16_t, counts.choices + 8);
        choices.values = BUSTER_ARENA_PUSH_ARRAY(scratch, int64_t, counts.choices + 8);
        choices.contexts = BUSTER_ARENA_PUSH_ARRAY(scratch, uint16_t, counts.choices + 8);
        choices.forms = BUSTER_ARENA_PUSH_ARRAY(scratch, uint16_t, counts.choices + 8);
        const bool room = choices.records != 0 && choices.rows != 0 && choices.values != 0 && choices.contexts != 0 && choices.forms != 0;
        if (!room || !tier0_push_output(code, output, entries->record_count + epilogue, counts, function->entry_records ? entries->positions : 0,
                                        function->count))
        {
            error = TIER0_ERROR_MEMORY;
        }
    }
    if (error == TIER0_OK)
    {
        if (BUSTER_VECTOR_PATH(vector))
        {
            tier0_emit_vector(function, entries, &frame, homes, references, code, &choices);
            x86_choose_forms(choices.rows, choices.values, choices.contexts, choices.count, choices.forms);
        }
        else
        {
            tier0_emit_body(function, entries, &frame, homes, references, code, &choices);
            x86_choose_forms_scalar(choices.rows, choices.values, choices.contexts, choices.count, choices.forms);
        }
        if (BUSTER_VECTOR_PATH(vector))
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
        code->frame_saved = frame.saved;
        code->frame_size = frame.size;
        code->frame_pointer = frame.pointer;
        code->frame_alignment_log2 = frame.alignment_log2;
        code->frame_base = frame.based;
    }
    return error;
}

// Each label's first record (Tier0Code.label_records), from the positions both paths give, after tier0_generate.
static Tier0Error tier0_label_records(const Tier0Function* function, const Tier0Entries* entries, Arena* output, Tier0Code* code)
{
    Tier0Error error = TIER0_OK;
    if (function->label_count != 0)
    {
        code->label_records = BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, function->label_count);
        error = code->label_records != 0 ? TIER0_OK : TIER0_ERROR_MEMORY;
        for (uint32_t k = 0; error == TIER0_OK && k < function->label_count; k += 1)
        {
            code->label_records[k] = entries->positions[function->label_entries[k]];
        }
        code->label_count = error == TIER0_OK ? function->label_count : 0;
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
        if (function->vectors)
        {
            tier0_operation_effects(function, &entries);
        }
        tier0_depths_scalar(function, &entries);
        error = tier0_generate(function, &entries, scratch.arena, output, &code, false);
        error = error == TIER0_OK ? tier0_label_records(function, &entries, output, &code) : error;
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

// A VECTOR value in tier0_validate's stack: its bytes in a byte of its own past the classes (17, 18 and 20 for 16, 32
// and 64), so that a vector's width follows it from depth to depth, and a join compares it, as a class.
#define BUSTER_TIER0_STACK_VECTOR(bytes) (BUSTER_TYPED_CLASS_LIMIT | ((bytes) >> 4))

// The class of a byte of tier0_validate's stack: VECTOR for a vector's, the class itself otherwise.
static uint32_t tier0_stack_class(uint32_t entry)
{
    return entry >= BUSTER_TYPED_CLASS_LIMIT ? BUSTER_TYPED_CLASS_VECTOR : entry;
}

// Whether entry i takes its last operand (tier0_takes_last) from a stack whose top has another class than the entry
// before it, which both paths read for it: in postorder that entry is the operand itself, and a value left under a store
// of its class (an increment's DUP leaves one) has it too. At a target, every way control arrives brings the same stack
// (tier0_arrive), the top's class among it.
static inline __attribute__((always_inline)) bool tier0_last_mismatch(const Tier0Function* function, const Tier0Validator* validator, uint32_t i)
{
    return tier0_takes_last(function, i) && validator->depth != 0 && tier0_stack_class(validator->stack[validator->depth - 1]) != function->classes[i - 1];
}

// Whether a form may be a row's (tier0_operation_valid): a VEX or EVEX one, without a segment override, a symbol or a
// branch, whose syntax names no implicit register, that has a vector or an opmask operand when it is EVEX (APX's forms
// of general registers alone are a target's that Tier 0 doesn't take), and none of those whose implicit operands their
// syntax doesn't name: vzeroupper and vzeroall (every vector register), mulx (rdx), vmaskmovdqu (rdi's memory) and the
// string compares (rax, rcx, rdx and xmm0).
static bool tier0_operation_form_valid(uint32_t form)
{
    static const uint16_t implicit[] = {
        X86_FORM_VZEROUPPER, X86_FORM_VZEROALL, X86_FORM_VMASKMOVDQU_X_X, X86_FORM_VPCMPESTRM_X_XM128_IMM8, X86_FORM_VPCMPESTRI_X_XM128_IMM8,
        X86_FORM_VPCMPISTRM_X_XM128_IMM8, X86_FORM_VPCMPISTRI_X_XM128_IMM8, X86_FORM_VPCMPESTRMQ_X_XM128_IMM8, X86_FORM_VPCMPESTRIQ_X_XM128_IMM8,
    };
    bool valid = form < X86_FORM_COUNT;
    for (uint32_t k = 0; valid && k < BUSTER_ARRAY_LENGTH(implicit); k += 1)
    {
        valid = form != implicit[k];
    }
    if (valid)
    {
        const X86FormDescription* description = x86_form_description(form);
        const uint8_t syntax = description->syntax;
        const bool explicit = syntax == X86_SYNTAX_RM || syntax == X86_SYNTAX_RM_REG || syntax == X86_SYNTAX_REG_RM || syntax == X86_SYNTAX_RM_IMM ||
                              syntax == X86_SYNTAX_REG_RM_IMM || syntax == X86_SYNTAX_RM_REG_IMM || syntax == X86_SYNTAX_REG_VVVV_RM ||
                              syntax == X86_SYNTAX_REG_VVVV_RM_IMM || syntax == X86_SYNTAX_REG_RM_VVVV || syntax == X86_SYNTAX_VVVV_RM ||
                              syntax == X86_SYNTAX_VVVV_RM_IMM || syntax == X86_SYNTAX_RM_REG_VVVV || syntax == X86_SYNTAX_RM_VVVV_REG ||
                              syntax == X86_SYNTAX_REG || tier0_form_is4(description);
        const bool vex = description->family == BUSTER_X86_FAMILY_VEX;
        const bool evex = description->family == BUSTER_X86_FAMILY_EVEX && x86_form_has_vector_operand(description) &&
                          (description->evex & (BUSTER_X86_EVEX_ND | BUSTER_X86_EVEX_NF)) == 0;
        const bool mulx = vex && description->opcode == 0x0f38f6 && description->prefixes == 0xf2;
        valid = explicit && (vex || evex) && !mulx && description->relative_bytes == 0;
    }
    return valid;
}

// Whether a role may be in a field of register class `class` of `operation`'s records: in a vector register's, VECTOR(j)
// for any j below 16, FLOATING(j) for a floating operand j and FLOATING_RESULT for a floating result, those two in an XMM
// field; in a general register's (16, 32 or 64 bits: a memory operand's base or index too), INTEGER(j) for an integer
// operand j no narrower than the field, INTEGER_RESULT for an integer result and SCRATCH(k); in an opmask register's, k0
// to k7. An integer's register holds its class's bytes, the rest unspecified, so a field reads it only where it is no
// wider: a base or an index (GPR64) a 64-bit integer's, a 32-bit field a 32- or 64-bit one's, and a front end extends a
// narrower operand first (CONVERT). A result's field is written whole: a 32-bit one zero-extends into the register, and
// none of the forms tier0_operation_form_valid takes has a 16-bit general register (those are APX's).
static bool tier0_operation_role_valid(const Tier0Operation* operation, uint8_t role, uint32_t class)
{
    const uint32_t j = role & 7;
    const bool operand = j < operation->operand_count;
    bool valid = false;
    if (tier0_vector_register_class(class))
    {
        const bool xmm = class == BUSTER_X86_CLASS_XMM;
        valid = (role >= BUSTER_TIER0_ROLE_VECTOR(0) && role < BUSTER_TIER0_ROLE_VECTOR(BUSTER_TIER0_OPERATION_VECTORS)) ||
                (xmm && role >= BUSTER_TIER0_ROLE_FLOATING(0) && role < BUSTER_TIER0_ROLE_FLOATING(BUSTER_TIER0_OPERATION_OPERANDS) && operand &&
                 tier0_class_floating(operation->operand_classes[j])) ||
                (xmm && role == BUSTER_TIER0_ROLE_FLOATING_RESULT && tier0_class_floating(operation->result_class));
    }
    else if (tier0_general_register_class(class))
    {
        const uint32_t bytes = class == BUSTER_X86_CLASS_GPR64 ? 8 : class == BUSTER_X86_CLASS_GPR32 ? 4 : 2;
        valid = (role >= BUSTER_TIER0_ROLE_INTEGER(0) && role < BUSTER_TIER0_ROLE_INTEGER(BUSTER_TIER0_OPERATION_OPERANDS) && operand &&
                 tier0_class_integer(operation->operand_classes[j]) && tier0_class_bytes(operation->operand_classes[j]) >= bytes) ||
                (role == BUSTER_TIER0_ROLE_INTEGER_RESULT && tier0_class_integer(operation->result_class)) ||
                (role >= BUSTER_TIER0_ROLE_SCRATCH(0) && role < BUSTER_TIER0_ROLE_SCRATCH(BUSTER_TIER0_SCRATCH));
    }
    else if (class == BUSTER_X86_CLASS_OPMASK)
    {
        valid = role < 8;
    }
    return valid;
}

// Whether a machine operation's row is one Tier 0 places and emits as Tier0Operation has it: its counts within their
// bounds; its operands integers, floating values and VECTORs of 16, 32 or 64 bytes; its result VOID or one of those; and
// each record of a form tier0_operation_form_valid takes, with its fields' roles as tier0_operation_role_valid has them,
// no pair or block of registers, a memory operand's address from a base and an optional index (no RIP-relative or
// absolute one), and then, with its roles placed somewhere (VECTOR(j) as register j), a record x86_validate takes.
static bool tier0_operation_valid(const Tier0Operation* operation)
{
    bool valid = operation->record_count <= BUSTER_TIER0_OPERATION_RECORDS && operation->operand_count <= BUSTER_TIER0_OPERATION_OPERANDS;
    for (uint32_t j = 0; valid && j < operation->operand_count; j += 1)
    {
        const uint32_t class = operation->operand_classes[j];
        valid = tier0_class_integer(class) || tier0_class_floating(class) || (tier0_class_vector(class) && tier0_vector_bytes_valid(operation->operand_bytes[j]));
    }
    const uint32_t result = operation->result_class;
    valid = valid && (result == BUSTER_TYPED_CLASS_VOID || tier0_class_integer(result) || tier0_class_floating(result) ||
                      (tier0_class_vector(result) && tier0_vector_bytes_valid(operation->result_bytes)));
    Tier0Placement sample;
    memset(&sample, 0, sizeof(sample));
    for (uint32_t j = 0; j < BUSTER_TIER0_OPERATION_OPERANDS; j += 1)
    {
        sample.operands[j] = tier0_class_floating(operation->operand_classes[j]) ? (uint8_t)j : X86_REGISTER_RSI;
    }
    for (uint32_t k = 0; k < BUSTER_TIER0_SCRATCH; k += 1)
    {
        sample.scratch[k] = X86_REGISTER_RDX;
    }
    sample.integer_result = X86_REGISTER_RAX;
    for (uint32_t r = 0; valid && r < operation->record_count; r += 1)
    {
        X86Instruction record = operation->records[r];
        valid = (record.form & BUSTER_X86_FORM_FIXUP) == 0 && tier0_operation_form_valid(record.form);
        if (valid)
        {
            const X86FormDescription* description = x86_form_description(record.form);
            const uint16_t pairs = BUSTER_X86_OPERAND_PAIR | BUSTER_X86_OPERAND_BLOCK;
            const bool memory = record.mode != BUSTER_X86_REGISTER_DIRECT;
            valid = ((description->reg | description->vvvv | description->rm) & pairs) == 0 && (!memory || (record.mode & BUSTER_X86_MODE_BASE) != 0) &&
                    (record.mode & BUSTER_X86_MODE_RIP) == 0;
            const Tier0Fields fields = tier0_operation_fields(&record);
            for (uint32_t k = 0; valid && k < fields.count; k += 1)
            {
                valid = tier0_operation_role_valid(operation, *fields.bytes[k], fields.classes[k]);
            }
            const X86Instruction placed = tier0_operation_place(record, &sample);
            valid = valid && x86_validate(&placed, 1) == 1;
        }
    }
    return valid;
}

// tier0_validate's rules for a vector's entry (an X86 entry, or one of class VECTOR), whose operands and result take
// their places in the validator's stack, a VECTOR one as its width's (BUSTER_TIER0_STACK_VECTOR): kinds, constants,
// objects and rows (which tier0_validate checks first, tier0_operation_valid) as Tier0Function has them.
static void tier0_validate_vector(const Tier0Function* function, Tier0Validator* validator, uint64_t* hashes, uint8_t* seen, uint32_t i)
{
    const uint8_t kind = function->kinds[i];
    const uint8_t class = function->classes[i];
    const uint64_t value = function->values[i];
    if (!function->vectors)
    {
        tier0_reject(validator, i, "a vector entry in a function without vectors");
    }
    else if (kind == BUSTER_TYPED_X86)
    {
        const Tier0Operation* operation = value < function->operation_count ? &function->operations[value] : 0;
        if (operation == 0 || operation->result_class != class)
        {
            tier0_reject(validator, i, "an X86 entry past the operations, or of another class than its row's result");
        }
        for (uint32_t j = operation != 0 ? operation->operand_count : 0; j > 0 && validator->check.error == TIER0_OK; j -= 1)
        {
            const uint32_t operand = operation->operand_classes[j - 1];
            const uint32_t expected = tier0_class_vector(operand) ? BUSTER_TIER0_STACK_VECTOR(operation->operand_bytes[j - 1]) : operand;
            if (tier0_pop(validator, i) != expected)
            {
                tier0_reject(validator, i, "an X86 entry's operand of another class or width than its row's");
            }
        }
        if (operation != 0 && class != BUSTER_TYPED_CLASS_VOID)
        {
            tier0_push(validator, tier0_class_vector(class) ? BUSTER_TIER0_STACK_VECTOR(operation->result_bytes) : class);
        }
    }
    else if (kind == BUSTER_TYPED_CONST)
    {
        if (value >= function->vector_constant_count)
        {
            tier0_reject(validator, i, "a vector constant past vector_constants");
        }
        else
        {
            tier0_push(validator, BUSTER_TIER0_STACK_VECTOR(function->vector_constant_bytes[value]));
        }
    }
    else if (kind == BUSTER_TYPED_DUP)
    {
        if (validator->depth == 0 || validator->stack[validator->depth - 1] < BUSTER_TYPED_CLASS_LIMIT)
        {
            tier0_reject(validator, i, "DUP of a vector it doesn't have");
        }
        else
        {
            tier0_push(validator, validator->stack[validator->depth - 1]);
        }
    }
    else if (kind == BUSTER_TYPED_LOAD)
    {
        if (tier0_pop(validator, i) != BUSTER_TYPED_CLASS_U64 || !tier0_vector_bytes_valid(value))
        {
            tier0_reject(validator, i, "a vector's LOAD from something that isn't U64, or of 16, 32 or 64 bytes");
        }
        tier0_push(validator, BUSTER_TIER0_STACK_VECTOR(tier0_vector_bytes_valid(value) ? value : 16));
    }
    else if (kind == BUSTER_TYPED_STORE || kind == BUSTER_TYPED_SET)
    {
        const uint32_t stored = tier0_pop(validator, i);
        const uint32_t address = tier0_pop(validator, i);
        if (address != BUSTER_TYPED_CLASS_U64 || !tier0_vector_bytes_valid(value) || stored != BUSTER_TIER0_STACK_VECTOR(value))
        {
            tier0_reject(validator, i, "a vector's store's address, or a value of other bytes than its own");
        }
        if (kind == BUSTER_TYPED_STORE)
        {
            tier0_push(validator, stored);
        }
    }
    else if (kind == BUSTER_TYPED_LOCAL || kind == BUSTER_TYPED_STORE_LOCAL || kind == BUSTER_TYPED_SET_LOCAL)
    {
        const bool object = value < function->local_count && tier0_vector_object(function, value);
        const uint32_t entry = object ? BUSTER_TIER0_STACK_VECTOR(function->local_sizes[value]) : 0;
        if (!object || (kind != BUSTER_TYPED_LOCAL && tier0_pop(validator, i) != entry))
        {
            tier0_reject(validator, i, "a vector's local that isn't an object of its bytes (16, 32 or 64)");
        }
        if (kind != BUSTER_TYPED_SET_LOCAL)
        {
            tier0_push(validator, entry);
        }
    }
    else if (kind == BUSTER_TYPED_DISCARD || kind == BUSTER_TYPED_JUMP_VALUE)
    {
        const uint32_t taken = tier0_pop(validator, i);
        if (taken < BUSTER_TYPED_CLASS_LIMIT)
        {
            tier0_reject(validator, i, "DISCARD or JUMP_VALUE of a vector it doesn't have");
        }
        if (kind == BUSTER_TYPED_JUMP_VALUE)
        {
            tier0_push(validator, taken);
            tier0_arrive(validator, hashes, seen, (uint32_t)value, i);
            validator->depth -= 1;
            validator->reachable = false;
        }
    }
    else
    {
        tier0_reject(validator, i, "a vector entry of a kind vectors don't take (their operations are X86 entries, their calls an aggregate's)");
    }
}

// tier0_validate_scalar's checks of the tables beside the stream, which the vector path (tier0_validate_accepts) shares:
// the locals and the objects, the vectors' constants and the operations' rows, long double's constants and result, the
// objects' bytes, the parameters' locations and the calls' arguments. Each refusal is at node 0.
static void tier0_validate_tables(const Tier0Function* function, Tier0Validator* validator)
{
    if (function->local_sizes != 0 && function->local_alignments == 0)
    {
        tier0_reject(validator, 0, "the objects' sizes without their alignments");
    }
    uint64_t object_bytes = 0;
    for (uint32_t l = 0; l < function->local_count && validator->check.error == TIER0_OK; l += 1)
    {
        if (tier0_is_object(function, l))
        {
            if (function->local_classes[l] != BUSTER_TYPED_CLASS_VOID || (function->local_flags[l] & BUSTER_TYPED_LOCAL_ADDRESS_TAKEN) == 0 ||
                function->local_alignments[l] > BUSTER_TIER0_MAX_OBJECT_ALIGNMENT_LOG2)
            {
                tier0_reject(validator, 0, "an object that isn't an address-taken local of class VOID, or aligned past 2^30");
            }
            // Its size rounded up to 16 and, aligned past 16, the most its place may move by for its alignment.
            const uint64_t alignment = UINT64_C(1) << (function->local_alignments[l] & 63);
            object_bytes += (((uint64_t)function->local_sizes[l] + 15) & ~15ull) + (alignment > 16 ? alignment - 16 : 0);
        }
        else if (function->local_classes[l] == BUSTER_TYPED_CLASS_VOID || function->local_classes[l] >= BUSTER_TYPED_CLASSES)
        {
            tier0_reject(validator, 0, "a local without a class");
        }
        else if (tier0_class_memory(function->local_classes[l]))
        {
            tier0_reject(validator, 0, "a long double or 128-bit integer local that isn't an object");
        }
        else if (tier0_class_vector(function->local_classes[l]))
        {
            tier0_reject(validator, 0, "a vector local that isn't an object");
        }
    }
    // The vectors' constants, 16, 32 or 64 bytes each, and the machine operations' rows (tier0_operation_valid).
    if ((function->vector_constant_count != 0 && (function->vector_constants == 0 || function->vector_constant_bytes == 0)) ||
        (function->operation_count != 0 && function->operations == 0))
    {
        tier0_reject(validator, 0, "the vectors' constants' or the operations' count without their columns");
    }
    for (uint32_t k = 0; k < function->vector_constant_count && validator->check.error == TIER0_OK; k += 1)
    {
        if (!tier0_vector_bytes_valid(function->vector_constant_bytes[k]))
        {
            tier0_reject(validator, 0, "a vector constant of other than 16, 32 or 64 bytes");
        }
    }
    for (uint32_t k = 0; k < function->operation_count && validator->check.error == TIER0_OK; k += 1)
    {
        if (!tier0_operation_valid(&function->operations[k]))
        {
            tier0_reject(validator, 0, "an operation's row Tier 0 doesn't place: its counts, classes, forms, roles or memory operands");
        }
    }
    if (function->x87_constant_count != 0 && function->x87_constants == 0)
    {
        tier0_reject(validator, 0, "long double's constants' count without the constants");
    }
    // A long double result in st0, which the epilogue loads from slot 0; a complex one (st0 and st1) isn't Tier 0's.
    if (function->result_size == 0 && tier0_x87_parts(function->result) != 0 && (tier0_x87_parts(function->result) != 1 || !tier0_x87_result_valid(function->result, 0)))
    {
        tier0_reject(validator, 0, "a function's x87 result that isn't one long double in st0");
    }
    // Long double's values have their slots only in a function that says it has them (Tier0Function.x87): its result,
    // from slot 0, and below each entry of class F80.
    if (tier0_x87_returns(function) && !function->x87)
    {
        tier0_reject(validator, 0, "a long double result in a function without x87");
    }
    if (object_bytes > BUSTER_TIER0_MAX_OBJECT_BYTES)
    {
        tier0_reject(validator, 0, "objects past BUSTER_TIER0_MAX_OBJECT_BYTES");
    }
    for (uint32_t p = 0; p < function->parameter_count; p += 1)
    {
        // A value in a register of its kind (an object's location, and the registers, tier0_parameters_valid checks).
        const AbiLocation location = function->parameters[p];
        const bool floating = tier0_class_floating(function->local_classes[p]);
        const bool vector = (location.registers[0] & BUSTER_ABI_REGISTER_VECTOR) != 0;
        if (!tier0_is_object(function, p) && location.kind != BUSTER_ABI_LOCATION_STACK && (location.kind != BUSTER_ABI_LOCATION_REGISTERS || floating != vector))
        {
            tier0_reject(validator, 0, "a parameter's location doesn't fit its class");
        }
    }
    for (uint32_t c = 0; c < function->call_count; c += 1)
    {
        if ((uint64_t)function->call_first[c] + function->call_argument_counts[c] > function->argument_count ||
            function->call_argument_counts[c] > BUSTER_TIER0_MAX_CALL_ARGUMENTS)
        {
            tier0_reject(validator, 0, "a call's arguments past the argument columns");
        }
        for (uint32_t a = 0; a < function->call_argument_counts[c] && validator->check.error == TIER0_OK; a += 1)
        {
            // A value in a register of its kind; an aggregate's address U64 (its location tier0_aggregates_valid checks).
            const uint32_t argument = function->call_first[c] + a;
            const uint32_t class = function->argument_classes[argument];
            const AbiLocation location = function->argument_locations[argument];
            const bool aggregate = function->argument_sizes != 0 && function->argument_sizes[argument] != 0;
            const bool vector = (location.registers[0] & BUSTER_ABI_REGISTER_VECTOR) != 0;
            // A long double on the stack alone, in 16 bytes at a multiple of 16 inside its call's area.
            const bool x87 = tier0_class_x87(class) &&
                             (aggregate || location.kind != BUSTER_ABI_LOCATION_STACK || location.stack_offset % 16 != 0 ||
                              (uint64_t)location.stack_offset + 16 > function->calls[c].stack_bytes);
            if (x87 || (aggregate ? class != BUSTER_TYPED_CLASS_U64 : location.kind == BUSTER_ABI_LOCATION_REGISTERS && tier0_class_floating(class) != vector))
            {
                tier0_reject(validator, 0, "a call's argument whose class doesn't fit its location, or an aggregate's that isn't U64");
            }
            // A 128-bit integer argument is an object's address, as a struct of two INTEGER eightbytes is, and so is a
            // vector's, whose bytes one vector register holds whole. A class past the classes is none: a vector's
            // stack byte (BUSTER_TIER0_STACK_VECTOR) would match one at the CALL, which compares its arguments' bytes
            // with these (#556's review).
            if (tier0_class_int128(class) || tier0_class_vector(class))
            {
                tier0_reject(validator, 0, "a call's 128-bit integer or vector argument as a value, which an object passes");
            }
            if (class == BUSTER_TYPED_CLASS_VOID || class >= BUSTER_TYPED_CLASSES)
            {
                tier0_reject(validator, 0, "a call's argument without a class");
            }
        }
    }
}

// The stream's rules an entry at a time: the twin and oracle of the vector path (tier0_validate_accepts), which gives
// tier0_validate's verdict and rule wherever the vector path doesn't take a stream. Out of line: it runs for --scalar and
// for the streams the vector path leaves to it, not for every function the lowering validates.
static __attribute__((noinline)) Tier0Check tier0_validate_scalar(const Tier0Function* function)
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
    else if (!tier0_frame_input_valid(function) || !tier0_calls_valid(function) || !tier0_parameters_valid(function))
    {
        tier0_reject(&validator, 0, "invalid incoming variadic metadata, address-only local role, call or parameter location, or aggregate metadata");
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
        if (function->label_count != 0 && function->label_entries == 0)
        {
            tier0_reject(&validator, 0, "the labels' count without their entries");
        }
        for (uint32_t k = 0; k < function->label_count && function->label_entries != 0 && validator.check.error == TIER0_OK; k += 1)
        {
            const uint32_t target = function->label_entries[k];
            if (target > count)
            {
                tier0_reject(&validator, 0, "a label past the function's end");
            }
            else
            {
                // A JUMP_INDIRECT may arrive at any label, with nothing on the stacks: as if before the first entry.
                labels[target >> 6] |= 1ull << (target & 63);
                tier0_arrive(&validator, hashes, seen, target, 0);
            }
        }
        tier0_validate_tables(function, &validator);
        uint32_t return_class = BUSTER_TIER0_ANY_DEPTH;
        // A function returning an aggregate leaves rax an address at every RETURN: in registers, its bytes', which the
        // epilogue loads the result from; through a hidden pointer, the pointer, which the caller may read the result
        // through. Only a RETURN of U64 leaves it there.
        const bool aggregate_result = function->result_size != 0 || function->result.kind == BUSTER_ABI_RESULT_MEMORY;
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
            // An unknown kind or class and the vectors' (X86 and VECTOR, the last of each) take one test, so that the
            // other entries run no test of their own for vectors: the lowering validates every function it lowers, and
            // a second test per entry cost sqlite3.c's -c 1.5 million instructions (#556's review). A vector's entry
            // checks its last operand as the others do below (tier0_last_mismatch), then the vectors' rules; none of the
            // memory classes' checks applies to it.
            if (kind == BUSTER_TYPED_NONE || kind >= BUSTER_TYPED_X86 || class >= BUSTER_TYPED_CLASS_VECTOR)
            {
                if (kind == BUSTER_TYPED_NONE || kind >= BUSTER_TYPED_KINDS || class >= BUSTER_TYPED_CLASSES)
                {
                    tier0_reject(&validator, i, "an unknown kind or class");
                    break;
                }
                if (tier0_last_mismatch(function, &validator, i))
                {
                    tier0_reject(&validator, i, "a last operand of another class than the entry before it, which both paths read for it");
                    break;
                }
                tier0_validate_vector(function, &validator, hashes, seen, i);
                continue;
            }
            if (tier0_class_x87(class) && !function->x87)
            {
                tier0_reject(&validator, i, "a long double entry in a function without x87");
                break;
            }
            if (tier0_class_int128(class) && !function->int128)
            {
                tier0_reject(&validator, i, "a 128-bit integer entry in a function without int128");
                break;
            }
            if (tier0_global_kind(kind) && function->symbol_access != 0 && (value >= function->symbol_count || function->symbol_access[value] >= BUSTER_TIER0_ACCESSES))
            {
                tier0_reject(&validator, i, "a symbol past the access column, or an access that doesn't exist");
                break;
            }
            if (tier0_last_mismatch(function, &validator, i))
            {
                tier0_reject(&validator, i, "a last operand of another class than the entry before it, which both paths read for it");
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
                if (tier0_class_x87(class) && (kind == BUSTER_TYPED_GLOBAL || value >= function->x87_constant_count))
                {
                    tier0_reject(&validator, i, "a long double global read as a value (its address's LOAD reads it), or a constant past x87_constants");
                }
                if (tier0_class_int128(class) && kind == BUSTER_TYPED_GLOBAL)
                {
                    tier0_reject(&validator, i, "a 128-bit integer global read as a value (its address's LOAD reads it)");
                }
                tier0_push(&validator, class);
                break;
            case BUSTER_TYPED_LOCAL:
            case BUSTER_TYPED_LOCAL_ADDRESS:
                if (value >= function->local_count ||
                    (kind == BUSTER_TYPED_LOCAL && !tier0_value_object(function, value, class) &&
                     (function->local_classes[value] != class || tier0_is_object(function, value) ||
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
                if (tier0_pop(&validator, i) != class || class == BUSTER_TYPED_CLASS_VOID ||
                    (kind == BUSTER_TYPED_NOT && !tier0_class_integer(class) && !tier0_class_int128(class)))
                {
                    tier0_reject(&validator, i, "NEG or NOT of another class");
                }
                tier0_push(&validator, class);
                break;
            case BUSTER_TYPED_CONVERT:
            {
                const uint32_t from = tier0_pop(&validator, i);
                if (from == BUSTER_TYPED_CLASS_VOID || class == BUSTER_TYPED_CLASS_VOID)
                {
                    tier0_reject(&validator, i, "CONVERT from or to VOID");
                }
                // A vector's stack byte, which is no class, would pass the check above: a vector's conversions are X86
                // entries (#556's review). The kinds that compare an operand with a class refuse one already.
                if (tier0_stack_class(from) == BUSTER_TYPED_CLASS_VECTOR)
                {
                    tier0_reject(&validator, i, "a CONVERT of a vector, whose conversions are X86 entries");
                }
                // A 128-bit integer converts to and from the other integers alone: a front end calls its runtime's
                // functions for the floating classes, as clang's code does.
                if ((tier0_class_int128(from) && !tier0_class_integer(class) && !tier0_class_int128(class)) ||
                    (tier0_class_int128(class) && !tier0_class_integer(from) && !tier0_class_int128(from)))
                {
                    tier0_reject(&validator, i, "a conversion between a 128-bit integer and a floating class");
                }
                tier0_push(&validator, class);
                break;
            }
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
                const bool bad_target = local ? value >= function->local_count ||
                                                    (!tier0_value_object(function, value, class) &&
                                                     (function->local_classes[value] != class || tier0_is_object(function, value) ||
                                                      (function->local_flags[value] & BUSTER_TYPED_LOCAL_ADDRESS_ONLY) != 0))
                                              : value == 0 || value > UINT32_MAX || tier0_class_memory(class);
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
                const bool object = function->call_result_objects != 0 && function->call_result_objects[value] != 0;
                if (object && class != BUSTER_TYPED_CLASS_VOID)
                {
                    tier0_reject(&validator, i, "a call whose result goes to its object, with a class");
                }
                // A 128-bit integer result comes back into an object, as a struct of two INTEGER eightbytes does.
                if (tier0_class_int128(class))
                {
                    tier0_reject(&validator, i, "a call's 128-bit integer result as a value, which an object takes");
                }
                // A result in x87 registers leaves them only as a long double's value (st0, the call's class F80) or
                // into its object (tier0_call_valid checks its registers), which the call pops.
                const AbiReturn returned = function->calls[value].result;
                const bool x87_value = tier0_x87_parts(returned) == 1 && tier0_x87_result_valid(returned, 0);
                if ((tier0_x87_parts(returned) != 0 && !object && !tier0_class_x87(class)) || (tier0_class_x87(class) && (!x87_value || object)))
                {
                    tier0_reject(&validator, i, "a call whose x87 result neither a long double's class nor an object takes");
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
            case BUSTER_TYPED_JUMP_INDIRECT:
                // To one of the labels, which tier0_arrive found reached with nothing on the stacks.
                if (tier0_pop(&validator, i) != BUSTER_TYPED_CLASS_U64 || class != BUSTER_TYPED_CLASS_U64 || value != 0)
                {
                    tier0_reject(&validator, i, "an indirect jump to something that isn't an address (U64), or with a value");
                }
                if (validator.depth != 0 || function->label_count == 0)
                {
                    tier0_reject(&validator, i, "an indirect jump with values left, or in a function without labels");
                }
                validator.reachable = false;
                break;
            case BUSTER_TYPED_RETURN:
                if (class != BUSTER_TYPED_CLASS_VOID && tier0_pop(&validator, i) != class)
                {
                    tier0_reject(&validator, i, "a return of another class");
                }
                if (aggregate_result && class != BUSTER_TYPED_CLASS_U64)
                {
                    tier0_reject(&validator, i, "a return of an aggregate that isn't an address (U64): its bytes', or the hidden pointer");
                }
                if (tier0_class_int128(class))
                {
                    tier0_reject(&validator, i, "a return of a 128-bit integer as a value, which a result object's address returns");
                }
                if (tier0_x87_returns(function) != tier0_class_x87(class))
                {
                    tier0_reject(&validator, i, "a return of a long double from a function whose result isn't one in st0, or of another class from one whose is");
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
            case BUSTER_TYPED_ALLOCA:
            case BUSTER_TYPED_STACK_SAVE:
            case BUSTER_TYPED_STACK_RESTORE:
            case BUSTER_TYPED_FRAME_ADDRESS:
            {
                // A byte count in, an address out; a position out; a position in; the frame's address out.
                const bool leaf = kind == BUSTER_TYPED_STACK_SAVE || kind == BUSTER_TYPED_FRAME_ADDRESS;
                if ((!leaf && tier0_pop(&validator, i) != BUSTER_TYPED_CLASS_U64) || class != BUSTER_TYPED_CLASS_U64 || value != 0)
                {
                    tier0_reject(&validator, i, "an allocation, a stack position saved or restored, or a frame's address, that isn't U64 or has a value");
                }
                if (kind != BUSTER_TYPED_STACK_RESTORE)
                {
                    tier0_push(&validator, class);
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
                // A compare's operands are alike, and two vectors' stack bytes are too, which a vector's compares (X86
                // entries) don't leave here (#556's review).
                bool valid = compare ? first == last && class == BUSTER_TYPED_CLASS_I32 && first != BUSTER_TYPED_CLASS_VOID &&
                                           tier0_stack_class(first) != BUSTER_TYPED_CLASS_VECTOR
                                     : first == class && (shift ? tier0_class_integer(last) : last == class) && class != BUSTER_TYPED_CLASS_VOID;
                valid &= !integer_only || tier0_class_integer(class) || (tier0_class_int128(class) && kind != BUSTER_TYPED_MOD);
                valid &= compare || tier0_class_bytes(class) >= 4;
                // A front end divides 128-bit integers through its runtime's functions, as clang's code does.
                valid &= !tier0_class_int128(class) || (kind != BUSTER_TYPED_DIV && kind != BUSTER_TYPED_MOD);
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
        // The end, reached by falling off the last entry or by a jump to it (seen[count]), is a statement's start: no
        // values may be left there either way.
        if (validator.check.error == TIER0_OK && ((validator.reachable && validator.depth != 0) || (seen[count] != 0 && validator.saved_depths[count] != 0)))
        {
            tier0_reject(&validator, count, "values left at the function's end");
        }
        if (validator.check.error == TIER0_OK && (validator.reachable || seen[count] != 0) && aggregate_result)
        {
            // Neither the last entry's fallthrough nor a jump to the end leaves rax the address a RETURN does.
            tier0_reject(&validator, count, "a function returning an aggregate that reaches its end");
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
// Per access, the kind each kind is keyed by for a symbol of that access (tier0_kind_by_access), for vpermb.
static uint8_t tier0_vector_accesses[BUSTER_TIER0_ACCESSES][BUSTER_TYPED_KIND_LIMIT] __attribute__((aligned(64)));

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
        for (uint32_t access = 0; access < BUSTER_TIER0_ACCESSES; access += 1)
        {
            tier0_vector_accesses[access][kind] = (uint8_t)tier0_kind_by_access(kind, access);
        }
        uint8_t row[BUSTER_TYPED_CLASS_LIMIT];
        for (uint32_t class = 0; class < BUSTER_TYPED_CLASS_LIMIT; class += 1)
        {
            row[class] = kind < BUSTER_TIER0_KINDS && kind != BUSTER_TYPED_CONVERT ? tier0_properties_of(kind, class) : 0;
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

// The labels, a bit per entry: the control entries' targets (found 64 at a time, then set one at a time: they are few),
// and the labels JUMP_INDIRECT may go to.
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
    tier0_label_entries(function, labels);
}

// The fixed byte vectors of a variant code.
static __m512i tier0_variant(uint32_t variant)
{
    return _mm512_set1_epi8((char)variant);
}

// tier0_access_kind for the 64 entries from `first`, whose kinds are `kinds`: the global kinds' lanes by compares, each
// one's symbol's access read from the column (they are few, as the calls are), then each access's lanes keyed by a vpermb
// of its table.
static __m512i tier0_vector_access_kinds(const Tier0Function* function, __m512i kinds, uint32_t first, __mmask64 lanes)
{
    const __m512i two = _mm512_set1_epi8(2);
    const uint64_t global = lanes & (_mm512_cmplt_epu8_mask(_mm512_sub_epi8(kinds, _mm512_set1_epi8(BUSTER_TYPED_GLOBAL)), two) |
                                     _mm512_cmplt_epu8_mask(_mm512_sub_epi8(kinds, _mm512_set1_epi8(BUSTER_TYPED_STORE_GLOBAL)), two));
    uint64_t accesses[BUSTER_TIER0_ACCESSES] = { 0, 0, 0, 0 };
    for (uint64_t rest = global; rest != 0; rest &= rest - 1)
    {
        const uint32_t lane = (uint32_t)__builtin_ctzll(rest);
        const uint64_t symbol = function->values[first + lane];
        const uint32_t access = symbol < function->symbol_count ? function->symbol_access[symbol] & (BUSTER_TIER0_ACCESSES - 1) : BUSTER_TIER0_ACCESS_DIRECT;
        accesses[access] |= 1ull << lane;
    }
    __m512i keys = kinds;
    for (uint32_t access = BUSTER_TIER0_ACCESS_GOT; access < BUSTER_TIER0_ACCESSES; access += 1)
    {
        keys = _mm512_mask_permutexvar_epi8(keys, accesses[access], kinds, _mm512_load_si512(tier0_vector_accesses[access]));
    }
    return keys;
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
        // The key class: a compare's operands'. The key kind: a global kind's for its symbol's access.
        const __mmask64 compare = _mm512_test_epi8_mask(flags, _mm512_set1_epi8(BUSTER_TIER0_KIND_COMPARE));
        const __m512i key_class = _mm512_mask_blend_epi8(compare, classes, classes_1);
        const __m512i key_kind = function->symbol_access != 0 ? tier0_vector_access_kinds(function, kinds, first, lanes) : kinds;
        const __m512i row = tier0_lookup(tier0_vector_rows, key_kind);
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
        _mm512_mask_storeu_epi8(entries->key_kinds + first, lanes, key_kind);
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
// tier0_select's, and `memory` and `realigned` as tier0_select_entries's.
static inline __attribute__((always_inline)) uint32_t tier0_select_vector_entries(const Tier0Function* function, Tier0Entries* entries, Tier0Frame* frame,
                                                                                  Tier0Error* error, bool memory, bool realigned)
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
    int32_t slot_top = -1;
    Tier0Tops convert_scratch[2] = { { -1, -1 }, { -1, -1 } };
    uint32_t clobbers = 0;
    uint32_t save_slots = 0;
    bool fixed = false;
    bool calls = false;
    bool moves = false;
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
            else if (tier0_moves_stack(function->kinds[i]))
            {
                const bool leaf = function->kinds[i] == BUSTER_TYPED_STACK_SAVE || function->kinds[i] == BUSTER_TYPED_FRAME_ADDRESS;
                deferred_integer_top = leaf && entry_depth > deferred_integer_top ? entry_depth : deferred_integer_top;
                moves = true;
            }
            else if (memory && tier0_vector_entry(entries, i))
            {
                tier0_vector_tops(function, entries, i, &deferred_integer_top, &deferred_floating_top, &slot_top);
            }
            else if (memory && tier0_memory_entry(entries, i))
            {
                tier0_memory_tops(function, entries, i, &deferred_integer_top, &deferred_floating_top, &slot_top, &clobbers, &fixed);
            }
            else if (function->kinds[i] == BUSTER_TYPED_CONVERT)
            {
                Tier0Tops tops = { deferred_integer_top, deferred_floating_top };
                tier0_select_u64_convert(function->classes[i] == BUSTER_TYPED_CLASS_U64, entry_depth, entry_floating, realigned, &tops, convert_scratch);
                deferred_integer_top = tops.integer;
                deferred_floating_top = tops.floating;
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
    const Tier0Tops convert = convert_scratch[moves ? 1 : 0];
    deferred_integer_top = convert.integer > deferred_integer_top ? convert.integer : deferred_integer_top;
    deferred_floating_top = convert.floating > deferred_floating_top ? convert.floating : deferred_floating_top;
    if (function->argument_sizes != 0)
    {
        const Tier0Tops tops = tier0_select_scratch(function, entries, (Tier0Tops){ deferred_integer_top, deferred_floating_top });
        deferred_integer_top = tops.integer;
        deferred_floating_top = tops.floating;
    }
    const bool rdx = (_mm512_reduce_or_epi64(kept_words) & BUSTER_TIER0_WORD_CLOBBERS_RDX) != 0;
    const int64_t vector_integer_top = _mm512_reduce_max_epi64(integer_top);
    const int64_t vector_floating_top = _mm512_reduce_max_epi64(floating_top);
    const int32_t integer_reached = vector_integer_top > deferred_integer_top ? (int32_t)vector_integer_top : deferred_integer_top;
    const int32_t floating_reached = vector_floating_top > deferred_floating_top ? (int32_t)vector_floating_top : deferred_floating_top;
    clobbers |= rdx ? 1u << X86_REGISTER_RDX : 0;
    const int32_t depth_count = tier0_integer_depth_count(realigned, moves);
    *error = integer_reached >= depth_count || floating_reached >= BUSTER_TIER0_FLOATING_DEPTHS || slot_top >= BUSTER_TIER0_INTEGER_DEPTHS ? TIER0_ERROR_DEPTH : TIER0_OK;
    frame->integer_top = integer_reached;
    frame->floating_top = floating_reached;
    frame->clobbers = clobbers | tier0_depth_registers(integer_reached, floating_reached);
    frame->save_slots = save_slots;
    frame->fixed_slots = fixed ? 2 : 0;
    frame->calls = calls;
    tier0_frame_registers(frame, moves, depth_count);
    return unsupported;
}

// The vector selection of a function with values of the memory classes or a realigned frame, out of line.
static __attribute__((noinline)) uint32_t tier0_select_vector_memory(const Tier0Function* function, Tier0Entries* entries, Tier0Frame* frame, Tier0Error* error)
{
    return tier0_select_vector_entries(function, entries, frame, error, tier0_memory_function(function), frame->alignment_log2 != 0);
}

static uint32_t tier0_select_vector(const Tier0Function* function, Tier0Entries* entries, Tier0Frame* frame, Tier0Error* error)
{
    const bool rare = tier0_memory_function(function) || frame->alignment_log2 != 0;
    return rare ? tier0_select_vector_memory(function, entries, frame, error) : tier0_select_vector_entries(function, entries, frame, error, false, false);
}

// Measuring for the vector path, 8 words at a time, from the prologue's length on:
// - a home-dependent entry's word is resolved to its homes' case: for a uniform slot, the case added to the template;
//   otherwise, the case's word gathered;
// - each entry's length comes from its word, or for a deferred one from tier0_count_deferred (an entry at a time: they
//   are few);
// - the positions are the lengths' prefix sums, in registers;
// - the branch records', references' and choice requests' counts come from popcounts.
// The last block's words and positions are stored whole, past the entries: emission expands every lane of a block, and a
// lane past the entries has template 0 and the end's position, which the epilogue overwrites. `memory` is
// Tier0Frame.memory and `deep` whether the function has deep entries (tier0_select_deep's, which has no values of the
// memory classes): constants at each of tier0_measure_vector's three uses, as tier0_select_entries's are.
static inline __attribute__((always_inline)) Tier0Counts tier0_measure_vector_entries(const Tier0Function* function, Tier0Entries* entries, const Tier0Home* homes,
                                                                                      uint32_t prologue, bool memory, bool deep)
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
            const uint32_t records = deep && entries->deep[first + lane] != 0 ? tier0_count_deep(function, entries, homes, first + lane, &counts)
                                                                              : tier0_count_deferred(function, entries, first + lane, memory, &counts.references);
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
    entries->record_count = (uint64_t)_mm_cvtsi128_si64(_mm512_castsi512_si128(carry));
    positions[count] = (uint32_t)entries->record_count;
    return counts;
}

// The vector measuring of a function with values of the memory classes, out of line.
static __attribute__((noinline)) Tier0Counts tier0_measure_vector_memory(const Tier0Function* function, Tier0Entries* entries, const Tier0Home* homes, uint32_t prologue)
{
    return tier0_measure_vector_entries(function, entries, homes, prologue, true, false);
}

// And of a function with deep entries.
static __attribute__((noinline)) Tier0Counts tier0_measure_vector_deep(const Tier0Function* function, Tier0Entries* entries, const Tier0Home* homes, uint32_t prologue)
{
    return tier0_measure_vector_entries(function, entries, homes, prologue, false, true);
}

static Tier0Counts tier0_measure_vector(const Tier0Function* function, Tier0Entries* entries, const Tier0Home* homes, uint32_t prologue, bool memory)
{
    return memory             ? tier0_measure_vector_memory(function, entries, homes, prologue)
           : entries->deep != 0 ? tier0_measure_vector_deep(function, entries, homes, prologue)
                                : tier0_measure_vector_entries(function, entries, homes, prologue, false, false);
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
// `deep` is tier0_emit_body_entries': with deep entries, the branch and request counts go to their emitter in the writer
// and back, and since a block's deep entries' branches come after its templates', the branch columns are sorted last
// (tier0_sort_branches).
static inline __attribute__((always_inline)) void tier0_emit_vector_entries(const Tier0Function* function, const Tier0Entries* entries, const Tier0Frame* frame,
                                                                           const Tier0Home* homes, const uint32_t* references, Tier0Code* code, Tier0Choices* choices,
                                                                           bool deep)
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
    Tier0Writer writer = { records, 0, links, 0, branch_records, targets, 0, choices, 0 };
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
                if (deep)
                {
                    writer.branch_count = branches;
                    choices->count = requests;
                    tier0_emit_deferred_deep(function, entries, frame, homes, i, &writer);
                    branches = writer.branch_count;
                    requests = choices->count;
                }
                else
                {
                    tier0_emit_deferred(function, entries, frame, homes, i, &writer);
                }
                deferred_count += 1;
            }
            else
            {
                const Tier0TemplateInfo* info = &tier0_template_infos[words[i] & BUSTER_TIER0_WORD_TEMPLATE];
                links[writer.reference_count] = (LinkReference){
                    .instruction = positions[i] + (uint32_t)(words[i] >> BUSTER_TIER0_WORD_FIXUP_SHIFT & 3),
                    .field = info->fixup_field,
                    .kind = info->fixup_kind,
                    .target = (SymbolId)values[i],
                    .addend = 0,
                };
                writer.reference_count += 1;
            }
        }
        previous = current;
    }
    writer.count = positions[count];
    tier0_emit_epilogue(function, frame, &writer);
    if (deep)
    {
        tier0_sort_branches(branch_records, targets, branches);
    }
    code->branch_count = branches;
    code->reference_count = writer.reference_count;
    code->deferred += deferred_count;
    choices->count = requests;
}

// The vector emission of a function with deep entries, out of line.
static __attribute__((noinline)) void tier0_emit_vector_deep(const Tier0Function* function, const Tier0Entries* entries, const Tier0Frame* frame, const Tier0Home* homes,
                                                             const uint32_t* references, Tier0Code* code, Tier0Choices* choices)
{
    tier0_emit_vector_entries(function, entries, frame, homes, references, code, choices, true);
}

static void tier0_emit_vector(const Tier0Function* function, const Tier0Entries* entries, const Tier0Frame* frame, const Tier0Home* homes, const uint32_t* references,
                              Tier0Code* code, Tier0Choices* choices)
{
    if (entries->deep != 0)
    {
        tier0_emit_vector_deep(function, entries, frame, homes, references, code, choices);
    }
    else
    {
        tier0_emit_vector_entries(function, entries, frame, homes, references, code, choices, false);
    }
}

static Tier0Code tier0_compile(const Tier0Function* function, Arena* output)
{
#if !BUSTER_HOST_VECTOR
    return tier0_compile_scalar(function, output);
#else
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
        if (function->vectors)
        {
            tier0_operation_effects(function, &entries);
        }
        tier0_vector_depths(function, &bits, &entries);
        error = tier0_generate(function, &entries, scratch.arena, output, &code, true);
        error = error == TIER0_OK ? tier0_label_records(function, &entries, output, &code) : error;
    }
    code = tier0_conclude(code, error, output, output_length);
    scratch_end(scratch);
    return code;
#endif
}

// ---------------------------------------------------------------------------------------------------------------------
// Validation's vector path (#569)
//
// tier0_validate_scalar's rules 64 entries at a time in byte lanes, as Tier 0's passes read the stream: the depth before
// each entry is the prefix sum of the entries' effects (what each pushes less what it pops, by table, a call's from its
// operands), and the stack holds the values those entries pushed. An operand's class is its producer's, the entry that
// pushed it: the nearest one before the operand's reader whose floor (its depth after its own pops) is at or below the
// operand's place, found by a nearest-smaller-value search over the block's floors (tier0_scan_previous), or for a value
// from an earlier block the carried stack's, which holds each place's class and producer. The rules are then compares
// of those classes and the entries' own against masks of classes by kind, the values against bounds by kind, and the
// locals' and calls' rules against tables of the function's.
//
// That stack is tier0_validate_scalar's wherever control arrives at a target with the stack the prefix sums give the
// target, which the vector path checks: a forward arrival's depth, the producer of the value just below what it may
// differ in (so that every value below it is the same one) and the class of the value a JUMP_VALUE brings, against every
// other arrival's and the target's own; a backward arrival only with nothing on the stack; and code that no path reaches
// before a target (after a jump or a return) at depth 0, where the scalar validator starts again. Under those checks the
// two stacks are the same at every entry, so that the vector path takes a stream only where the scalar validator takes
// it (tier0_validate_test.c checks that on every stream the tests make, and on every short one), and leaves every other
// one to it, which gives the verdict and the rule: a stream that breaks a rule, and one the vector path doesn't decide: a
// function with vectors, a block whose pops come to more than BUSTER_TIER0_SCAN_POPS, a depth past BUSTER_TIER0_SCAN_DEPTH
// at a block's start, or an arrival the rules above don't take (a backward one with values on the stack, or a target
// whose arrivals bring values from different producers, which the scalar validator compares by their classes).

// The vector path's limits, past which it leaves a function to the scalar validator: the values a block's entries pop,
// so that its depths relative to its start stay in a byte (from -BUSTER_TIER0_SCAN_POPS to 64), and the depth at a
// block's start, so that the carried stack's classes are a register's 64 bytes, a place each (a block reads them there,
// and stores into memory would stall the next block's reads of them), and every depth is below 129.
#define BUSTER_TIER0_SCAN_POPS 100
#define BUSTER_TIER0_SCAN_DEPTH 64
// The carried stack's producers, which the arrivals' checks read a place at a time: 64 below place 0 (no value:
// BUSTER_TIER0_SCAN_NONE), the places, and room past them for a block's values stored whole.
#define BUSTER_TIER0_SCAN_STACK (64 + 2 * BUSTER_TIER0_SCAN_DEPTH)
#define BUSTER_TIER0_SCAN_NONE 0xffffffffu
// A target that no forward arrival has reached yet (tier0_validate_accepts' `expected`).
#define BUSTER_TIER0_SCAN_UNREACHED 0xff

// Per kind, what an entry pops (bits 0 and 1: a call's are its operands', and a RETURN of nothing pops nothing), whether
// it pushes a value of its class (a call only with a class), and whether its value must be nonzero (PTR_ADD's size).
#define BUSTER_TIER0_SCAN_PUSH 0x04
#define BUSTER_TIER0_SCAN_NONZERO 0x08
static const uint8_t tier0_scan_shapes[BUSTER_TYPED_KIND_LIMIT] __attribute__((aligned(64))) = {
    [BUSTER_TYPED_CONST] = BUSTER_TIER0_SCAN_PUSH,
    [BUSTER_TYPED_LOCAL] = BUSTER_TIER0_SCAN_PUSH,
    [BUSTER_TYPED_LOCAL_ADDRESS] = BUSTER_TIER0_SCAN_PUSH,
    [BUSTER_TYPED_GLOBAL] = BUSTER_TIER0_SCAN_PUSH,
    [BUSTER_TYPED_GLOBAL_ADDRESS] = BUSTER_TIER0_SCAN_PUSH,
    [BUSTER_TYPED_DUP] = BUSTER_TIER0_SCAN_PUSH,
    [BUSTER_TYPED_NEG] = 1 | BUSTER_TIER0_SCAN_PUSH,
    [BUSTER_TYPED_NOT] = 1 | BUSTER_TIER0_SCAN_PUSH,
    [BUSTER_TYPED_CONVERT] = 1 | BUSTER_TIER0_SCAN_PUSH,
    [BUSTER_TYPED_LOAD] = 1 | BUSTER_TIER0_SCAN_PUSH,
    [BUSTER_TYPED_ADD] = 2 | BUSTER_TIER0_SCAN_PUSH,
    [BUSTER_TYPED_SUB] = 2 | BUSTER_TIER0_SCAN_PUSH,
    [BUSTER_TYPED_MUL] = 2 | BUSTER_TIER0_SCAN_PUSH,
    [BUSTER_TYPED_DIV] = 2 | BUSTER_TIER0_SCAN_PUSH,
    [BUSTER_TYPED_MOD] = 2 | BUSTER_TIER0_SCAN_PUSH,
    [BUSTER_TYPED_AND] = 2 | BUSTER_TIER0_SCAN_PUSH,
    [BUSTER_TYPED_OR] = 2 | BUSTER_TIER0_SCAN_PUSH,
    [BUSTER_TYPED_XOR] = 2 | BUSTER_TIER0_SCAN_PUSH,
    [BUSTER_TYPED_SHL] = 2 | BUSTER_TIER0_SCAN_PUSH,
    [BUSTER_TYPED_SHR] = 2 | BUSTER_TIER0_SCAN_PUSH,
    [BUSTER_TYPED_PTR_ADD] = 2 | BUSTER_TIER0_SCAN_PUSH | BUSTER_TIER0_SCAN_NONZERO,
    [BUSTER_TYPED_EQ] = 2 | BUSTER_TIER0_SCAN_PUSH,
    [BUSTER_TYPED_NE] = 2 | BUSTER_TIER0_SCAN_PUSH,
    [BUSTER_TYPED_LT] = 2 | BUSTER_TIER0_SCAN_PUSH,
    [BUSTER_TYPED_LE] = 2 | BUSTER_TIER0_SCAN_PUSH,
    [BUSTER_TYPED_GT] = 2 | BUSTER_TIER0_SCAN_PUSH,
    [BUSTER_TYPED_GE] = 2 | BUSTER_TIER0_SCAN_PUSH,
    [BUSTER_TYPED_STORE] = 2 | BUSTER_TIER0_SCAN_PUSH,
    [BUSTER_TYPED_SET] = 2,
    [BUSTER_TYPED_STORE_LOCAL] = 1 | BUSTER_TIER0_SCAN_PUSH,
    [BUSTER_TYPED_SET_LOCAL] = 1,
    [BUSTER_TYPED_STORE_GLOBAL] = 1 | BUSTER_TIER0_SCAN_PUSH,
    [BUSTER_TYPED_SET_GLOBAL] = 1,
    [BUSTER_TYPED_CALL] = BUSTER_TIER0_SCAN_PUSH,
    [BUSTER_TYPED_DISCARD] = 1,
    [BUSTER_TYPED_BRANCH_FALSE] = 1,
    [BUSTER_TYPED_BRANCH_TRUE] = 1,
    [BUSTER_TYPED_JUMP] = 0,
    [BUSTER_TYPED_JUMP_VALUE] = 1,
    [BUSTER_TYPED_RETURN] = 1,
    [BUSTER_TYPED_COPY] = 2,
    [BUSTER_TYPED_ALLOCA] = 1 | BUSTER_TIER0_SCAN_PUSH,
    [BUSTER_TYPED_STACK_SAVE] = BUSTER_TIER0_SCAN_PUSH,
    [BUSTER_TYPED_STACK_RESTORE] = 1,
    [BUSTER_TYPED_JUMP_INDIRECT] = 1,
    [BUSTER_TYPED_FRAME_ADDRESS] = BUSTER_TIER0_SCAN_PUSH,
};

// Per kind, the bounds of its value (tier0_validate_accepts' table of them, the function's): none; a local, a call, a
// target (an entry or the end), a symbol (nonzero, of 32 bits, inside the access column when there is one); a copy's
// bytes; zero; and, for a long double's constant (a CONST of F80, by its class), an index into x87_constants.
#define BUSTER_TIER0_BOUND_NONE 0
#define BUSTER_TIER0_BOUND_LOCAL 1
#define BUSTER_TIER0_BOUND_CALL 2
#define BUSTER_TIER0_BOUND_TARGET 3
#define BUSTER_TIER0_BOUND_SYMBOL 4
#define BUSTER_TIER0_BOUND_COPY 5
#define BUSTER_TIER0_BOUND_ZERO 6
#define BUSTER_TIER0_BOUND_X87 7
static const uint8_t tier0_scan_bounds[BUSTER_TYPED_KIND_LIMIT] __attribute__((aligned(64))) = {
    [BUSTER_TYPED_LOCAL] = BUSTER_TIER0_BOUND_LOCAL,
    [BUSTER_TYPED_LOCAL_ADDRESS] = BUSTER_TIER0_BOUND_LOCAL,
    [BUSTER_TYPED_GLOBAL] = BUSTER_TIER0_BOUND_SYMBOL,
    [BUSTER_TYPED_GLOBAL_ADDRESS] = BUSTER_TIER0_BOUND_SYMBOL,
    [BUSTER_TYPED_STORE_LOCAL] = BUSTER_TIER0_BOUND_LOCAL,
    [BUSTER_TYPED_SET_LOCAL] = BUSTER_TIER0_BOUND_LOCAL,
    [BUSTER_TYPED_STORE_GLOBAL] = BUSTER_TIER0_BOUND_SYMBOL,
    [BUSTER_TYPED_SET_GLOBAL] = BUSTER_TIER0_BOUND_SYMBOL,
    [BUSTER_TYPED_CALL] = BUSTER_TIER0_BOUND_CALL,
    [BUSTER_TYPED_BRANCH_FALSE] = BUSTER_TIER0_BOUND_TARGET,
    [BUSTER_TYPED_BRANCH_TRUE] = BUSTER_TIER0_BOUND_TARGET,
    [BUSTER_TYPED_JUMP] = BUSTER_TIER0_BOUND_TARGET,
    [BUSTER_TYPED_JUMP_VALUE] = BUSTER_TIER0_BOUND_TARGET,
    [BUSTER_TYPED_COPY] = BUSTER_TIER0_BOUND_COPY,
    [BUSTER_TYPED_ALLOCA] = BUSTER_TIER0_BOUND_ZERO,
    [BUSTER_TYPED_STACK_SAVE] = BUSTER_TIER0_BOUND_ZERO,
    [BUSTER_TYPED_STACK_RESTORE] = BUSTER_TIER0_BOUND_ZERO,
    [BUSTER_TYPED_JUMP_INDIRECT] = BUSTER_TIER0_BOUND_ZERO,
    [BUSTER_TYPED_FRAME_ADDRESS] = BUSTER_TIER0_BOUND_ZERO,
};

// Per kind, its operands' rules: it reads the class of its top operand (TOP: one it pops, or DUP's) and of the one below
// (SECOND), each in its kind's mask of classes (tier0_scan_top, tier0_scan_second), or the entry's own (TOP_SELF,
// SECOND_SELF), or the top's (COMPARE, the second's), or in CONVERT's mask by the entry's class (CONVERT, the top's); the
// top's class is the entry before's (LAST, tier0_last_mismatch's rule; not for a RETURN of nothing); and nothing is left
// under its operands (FLOOR).
#define BUSTER_TIER0_RULE_TOP 0x01
#define BUSTER_TIER0_RULE_SECOND 0x02
#define BUSTER_TIER0_RULE_LAST 0x04
#define BUSTER_TIER0_RULE_TOP_SELF 0x08
#define BUSTER_TIER0_RULE_SECOND_SELF 0x10
#define BUSTER_TIER0_RULE_COMPARE 0x20
#define BUSTER_TIER0_RULE_CONVERT 0x40
#define BUSTER_TIER0_RULE_FLOOR 0x80
#define BUSTER_TIER0_RULE_UNARY (BUSTER_TIER0_RULE_TOP | BUSTER_TIER0_RULE_LAST | BUSTER_TIER0_RULE_TOP_SELF)
#define BUSTER_TIER0_RULE_BINARY (BUSTER_TIER0_RULE_TOP | BUSTER_TIER0_RULE_SECOND | BUSTER_TIER0_RULE_LAST | BUSTER_TIER0_RULE_SECOND_SELF)
static const uint8_t tier0_scan_rules[BUSTER_TYPED_KIND_LIMIT] __attribute__((aligned(64))) = {
    [BUSTER_TYPED_DUP] = BUSTER_TIER0_RULE_TOP | BUSTER_TIER0_RULE_TOP_SELF,
    [BUSTER_TYPED_NEG] = BUSTER_TIER0_RULE_UNARY,
    [BUSTER_TYPED_NOT] = BUSTER_TIER0_RULE_UNARY,
    [BUSTER_TYPED_CONVERT] = BUSTER_TIER0_RULE_TOP | BUSTER_TIER0_RULE_LAST | BUSTER_TIER0_RULE_CONVERT,
    [BUSTER_TYPED_LOAD] = BUSTER_TIER0_RULE_TOP | BUSTER_TIER0_RULE_LAST,
    [BUSTER_TYPED_ADD] = BUSTER_TIER0_RULE_BINARY | BUSTER_TIER0_RULE_TOP_SELF,
    [BUSTER_TYPED_SUB] = BUSTER_TIER0_RULE_BINARY | BUSTER_TIER0_RULE_TOP_SELF,
    [BUSTER_TYPED_MUL] = BUSTER_TIER0_RULE_BINARY | BUSTER_TIER0_RULE_TOP_SELF,
    [BUSTER_TYPED_DIV] = BUSTER_TIER0_RULE_BINARY | BUSTER_TIER0_RULE_TOP_SELF,
    [BUSTER_TYPED_MOD] = BUSTER_TIER0_RULE_BINARY | BUSTER_TIER0_RULE_TOP_SELF,
    [BUSTER_TYPED_AND] = BUSTER_TIER0_RULE_BINARY | BUSTER_TIER0_RULE_TOP_SELF,
    [BUSTER_TYPED_OR] = BUSTER_TIER0_RULE_BINARY | BUSTER_TIER0_RULE_TOP_SELF,
    [BUSTER_TYPED_XOR] = BUSTER_TIER0_RULE_BINARY | BUSTER_TIER0_RULE_TOP_SELF,
    [BUSTER_TYPED_SHL] = BUSTER_TIER0_RULE_BINARY,
    [BUSTER_TYPED_SHR] = BUSTER_TIER0_RULE_BINARY,
    [BUSTER_TYPED_PTR_ADD] = BUSTER_TIER0_RULE_TOP | BUSTER_TIER0_RULE_SECOND | BUSTER_TIER0_RULE_LAST,
    [BUSTER_TYPED_EQ] = BUSTER_TIER0_RULE_TOP | BUSTER_TIER0_RULE_SECOND | BUSTER_TIER0_RULE_LAST | BUSTER_TIER0_RULE_COMPARE,
    [BUSTER_TYPED_NE] = BUSTER_TIER0_RULE_TOP | BUSTER_TIER0_RULE_SECOND | BUSTER_TIER0_RULE_LAST | BUSTER_TIER0_RULE_COMPARE,
    [BUSTER_TYPED_LT] = BUSTER_TIER0_RULE_TOP | BUSTER_TIER0_RULE_SECOND | BUSTER_TIER0_RULE_LAST | BUSTER_TIER0_RULE_COMPARE,
    [BUSTER_TYPED_LE] = BUSTER_TIER0_RULE_TOP | BUSTER_TIER0_RULE_SECOND | BUSTER_TIER0_RULE_LAST | BUSTER_TIER0_RULE_COMPARE,
    [BUSTER_TYPED_GT] = BUSTER_TIER0_RULE_TOP | BUSTER_TIER0_RULE_SECOND | BUSTER_TIER0_RULE_LAST | BUSTER_TIER0_RULE_COMPARE,
    [BUSTER_TYPED_GE] = BUSTER_TIER0_RULE_TOP | BUSTER_TIER0_RULE_SECOND | BUSTER_TIER0_RULE_LAST | BUSTER_TIER0_RULE_COMPARE,
    [BUSTER_TYPED_STORE] = BUSTER_TIER0_RULE_UNARY | BUSTER_TIER0_RULE_SECOND,
    [BUSTER_TYPED_SET] = BUSTER_TIER0_RULE_UNARY | BUSTER_TIER0_RULE_SECOND,
    [BUSTER_TYPED_STORE_LOCAL] = BUSTER_TIER0_RULE_UNARY,
    [BUSTER_TYPED_SET_LOCAL] = BUSTER_TIER0_RULE_UNARY,
    [BUSTER_TYPED_STORE_GLOBAL] = BUSTER_TIER0_RULE_UNARY,
    [BUSTER_TYPED_SET_GLOBAL] = BUSTER_TIER0_RULE_UNARY,
    [BUSTER_TYPED_DISCARD] = BUSTER_TIER0_RULE_UNARY,
    [BUSTER_TYPED_BRANCH_FALSE] = BUSTER_TIER0_RULE_UNARY,
    [BUSTER_TYPED_BRANCH_TRUE] = BUSTER_TIER0_RULE_UNARY,
    [BUSTER_TYPED_JUMP_VALUE] = BUSTER_TIER0_RULE_UNARY,
    [BUSTER_TYPED_RETURN] = BUSTER_TIER0_RULE_UNARY | BUSTER_TIER0_RULE_FLOOR,
    [BUSTER_TYPED_COPY] = BUSTER_TIER0_RULE_TOP | BUSTER_TIER0_RULE_SECOND | BUSTER_TIER0_RULE_LAST,
    [BUSTER_TYPED_ALLOCA] = BUSTER_TIER0_RULE_TOP | BUSTER_TIER0_RULE_LAST,
    [BUSTER_TYPED_STACK_RESTORE] = BUSTER_TIER0_RULE_TOP | BUSTER_TIER0_RULE_LAST,
    [BUSTER_TYPED_JUMP_INDIRECT] = BUSTER_TIER0_RULE_TOP | BUSTER_TIER0_RULE_LAST | BUSTER_TIER0_RULE_FLOOR,
};

// Per kind, what else it takes part in: arrivals at its value (CONTROL), an end to the code falling through after it
// (TRANSFER), a local's classes by the function's table of them (LOCAL; ADDRESS for LOCAL_ADDRESS, whose local's address
// is taken), the access column (GLOBAL), a call's own rules (CALL), and one class for every RETURN (RETURN).
#define BUSTER_TIER0_SIDE_CONTROL 0x01
#define BUSTER_TIER0_SIDE_TRANSFER 0x02
#define BUSTER_TIER0_SIDE_LOCAL 0x04
#define BUSTER_TIER0_SIDE_ADDRESS 0x08
#define BUSTER_TIER0_SIDE_GLOBAL 0x10
#define BUSTER_TIER0_SIDE_CALL 0x20
#define BUSTER_TIER0_SIDE_RETURN 0x40
static const uint8_t tier0_scan_sides[BUSTER_TYPED_KIND_LIMIT] __attribute__((aligned(64))) = {
    [BUSTER_TYPED_LOCAL] = BUSTER_TIER0_SIDE_LOCAL,
    [BUSTER_TYPED_LOCAL_ADDRESS] = BUSTER_TIER0_SIDE_ADDRESS,
    [BUSTER_TYPED_GLOBAL] = BUSTER_TIER0_SIDE_GLOBAL,
    [BUSTER_TYPED_GLOBAL_ADDRESS] = BUSTER_TIER0_SIDE_GLOBAL,
    [BUSTER_TYPED_STORE_LOCAL] = BUSTER_TIER0_SIDE_LOCAL,
    [BUSTER_TYPED_SET_LOCAL] = BUSTER_TIER0_SIDE_LOCAL,
    [BUSTER_TYPED_STORE_GLOBAL] = BUSTER_TIER0_SIDE_GLOBAL,
    [BUSTER_TYPED_SET_GLOBAL] = BUSTER_TIER0_SIDE_GLOBAL,
    [BUSTER_TYPED_CALL] = BUSTER_TIER0_SIDE_CALL,
    [BUSTER_TYPED_BRANCH_FALSE] = BUSTER_TIER0_SIDE_CONTROL,
    [BUSTER_TYPED_BRANCH_TRUE] = BUSTER_TIER0_SIDE_CONTROL,
    [BUSTER_TYPED_JUMP] = BUSTER_TIER0_SIDE_CONTROL | BUSTER_TIER0_SIDE_TRANSFER,
    [BUSTER_TYPED_JUMP_VALUE] = BUSTER_TIER0_SIDE_CONTROL | BUSTER_TIER0_SIDE_TRANSFER,
    [BUSTER_TYPED_RETURN] = BUSTER_TIER0_SIDE_TRANSFER | BUSTER_TIER0_SIDE_RETURN,
    [BUSTER_TYPED_JUMP_INDIRECT] = BUSTER_TIER0_SIDE_TRANSFER,
};

// Masks of classes, a bit per class: every class but the vectors' (which the vector path leaves to the scalar validator),
// every one with a value, those without the memory classes, the integers, the widths arithmetic takes (4 bytes and more),
// those division takes (no 128-bit integers), remainder's and the bitwise operations' (integers, and the 128-bit ones
// but for remainder), NOT's, PTR_ADD's index's, and single classes.
#define BUSTER_TIER0_CLASSES_ALL 0x3fff
#define BUSTER_TIER0_CLASSES_VALUES 0x3ffe
#define BUSTER_TIER0_CLASSES_SCALARS 0x07fe
#define BUSTER_TIER0_CLASSES_INTEGERS 0x01fe
#define BUSTER_TIER0_CLASSES_ARITHMETIC 0x3fe0
#define BUSTER_TIER0_CLASSES_DIVISION 0x0fe0
#define BUSTER_TIER0_CLASSES_REMAINDER 0x01e0
#define BUSTER_TIER0_CLASSES_BITWISE 0x31e0
#define BUSTER_TIER0_CLASSES_NOT 0x31fe
#define BUSTER_TIER0_CLASSES_WIDE_INTEGERS 0x0180
#define BUSTER_TIER0_CLASSES_NO_INT128 0x0ffe
#define BUSTER_TIER0_CLASS(class) (1u << (class))
_Static_assert(BUSTER_TYPED_CLASS_VECTOR == 14 && BUSTER_TYPED_CLASS_F80 == 11 && BUSTER_TYPED_CLASS_I128 == 12 && BUSTER_TYPED_CLASS_U128 == 13,
               "the vector path's masks of classes");

// The tables of 16-bit masks of classes below, each as two tables of bytes for vpermb (its masks' low bytes, and their
// high bytes), from one list of entries.
#define BUSTER_TIER0_LOW_BYTE(index, mask) [index] = (uint8_t)((mask) & 0xff),
#define BUSTER_TIER0_HIGH_BYTE(index, mask) [index] = (uint8_t)((mask) >> 8),

// Per kind, the classes an entry may have (the function's tables restrict CALL's and RETURN's further, and a function
// without labels takes no JUMP_INDIRECT); an unknown kind's, NONE's and X86's none.
#define BUSTER_TIER0_SCAN_OWN(entry)                                                                                   \
    entry(BUSTER_TYPED_CONST, BUSTER_TIER0_CLASSES_VALUES)                                                             \
    entry(BUSTER_TYPED_LOCAL, BUSTER_TIER0_CLASSES_VALUES)                                                             \
    entry(BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TIER0_CLASS(BUSTER_TYPED_CLASS_U64))                                      \
    entry(BUSTER_TYPED_GLOBAL, BUSTER_TIER0_CLASSES_SCALARS)                                                           \
    entry(BUSTER_TYPED_GLOBAL_ADDRESS, BUSTER_TIER0_CLASS(BUSTER_TYPED_CLASS_U64))                                     \
    entry(BUSTER_TYPED_DUP, BUSTER_TIER0_CLASSES_VALUES)                                                               \
    entry(BUSTER_TYPED_NEG, BUSTER_TIER0_CLASSES_VALUES)                                                               \
    entry(BUSTER_TYPED_NOT, BUSTER_TIER0_CLASSES_NOT)                                                                  \
    entry(BUSTER_TYPED_CONVERT, BUSTER_TIER0_CLASSES_VALUES)                                                           \
    entry(BUSTER_TYPED_LOAD, BUSTER_TIER0_CLASSES_VALUES)                                                              \
    entry(BUSTER_TYPED_ADD, BUSTER_TIER0_CLASSES_ARITHMETIC)                                                           \
    entry(BUSTER_TYPED_SUB, BUSTER_TIER0_CLASSES_ARITHMETIC)                                                           \
    entry(BUSTER_TYPED_MUL, BUSTER_TIER0_CLASSES_ARITHMETIC)                                                           \
    entry(BUSTER_TYPED_DIV, BUSTER_TIER0_CLASSES_DIVISION)                                                             \
    entry(BUSTER_TYPED_MOD, BUSTER_TIER0_CLASSES_REMAINDER)                                                            \
    entry(BUSTER_TYPED_AND, BUSTER_TIER0_CLASSES_BITWISE)                                                              \
    entry(BUSTER_TYPED_OR, BUSTER_TIER0_CLASSES_BITWISE)                                                               \
    entry(BUSTER_TYPED_XOR, BUSTER_TIER0_CLASSES_BITWISE)                                                              \
    entry(BUSTER_TYPED_SHL, BUSTER_TIER0_CLASSES_BITWISE)                                                              \
    entry(BUSTER_TYPED_SHR, BUSTER_TIER0_CLASSES_BITWISE)                                                              \
    entry(BUSTER_TYPED_PTR_ADD, BUSTER_TIER0_CLASS(BUSTER_TYPED_CLASS_U64))                                            \
    entry(BUSTER_TYPED_EQ, BUSTER_TIER0_CLASS(BUSTER_TYPED_CLASS_I32))                                                 \
    entry(BUSTER_TYPED_NE, BUSTER_TIER0_CLASS(BUSTER_TYPED_CLASS_I32))                                                 \
    entry(BUSTER_TYPED_LT, BUSTER_TIER0_CLASS(BUSTER_TYPED_CLASS_I32))                                                 \
    entry(BUSTER_TYPED_LE, BUSTER_TIER0_CLASS(BUSTER_TYPED_CLASS_I32))                                                 \
    entry(BUSTER_TYPED_GT, BUSTER_TIER0_CLASS(BUSTER_TYPED_CLASS_I32))                                                 \
    entry(BUSTER_TYPED_GE, BUSTER_TIER0_CLASS(BUSTER_TYPED_CLASS_I32))                                                 \
    entry(BUSTER_TYPED_STORE, BUSTER_TIER0_CLASSES_VALUES)                                                             \
    entry(BUSTER_TYPED_SET, BUSTER_TIER0_CLASSES_VALUES)                                                               \
    entry(BUSTER_TYPED_STORE_LOCAL, BUSTER_TIER0_CLASSES_VALUES)                                                       \
    entry(BUSTER_TYPED_SET_LOCAL, BUSTER_TIER0_CLASSES_VALUES)                                                         \
    entry(BUSTER_TYPED_STORE_GLOBAL, BUSTER_TIER0_CLASSES_SCALARS)                                                     \
    entry(BUSTER_TYPED_SET_GLOBAL, BUSTER_TIER0_CLASSES_SCALARS)                                                       \
    entry(BUSTER_TYPED_CALL, BUSTER_TIER0_CLASSES_ALL)                                                                 \
    entry(BUSTER_TYPED_DISCARD, BUSTER_TIER0_CLASSES_VALUES)                                                           \
    entry(BUSTER_TYPED_BRANCH_FALSE, BUSTER_TIER0_CLASSES_INTEGERS)                                                    \
    entry(BUSTER_TYPED_BRANCH_TRUE, BUSTER_TIER0_CLASSES_INTEGERS)                                                     \
    entry(BUSTER_TYPED_JUMP, BUSTER_TIER0_CLASSES_ALL)                                                                 \
    entry(BUSTER_TYPED_JUMP_VALUE, BUSTER_TIER0_CLASSES_VALUES)                                                        \
    entry(BUSTER_TYPED_RETURN, BUSTER_TIER0_CLASSES_ALL)                                                               \
    entry(BUSTER_TYPED_COPY, BUSTER_TIER0_CLASS(BUSTER_TYPED_CLASS_VOID))                                              \
    entry(BUSTER_TYPED_ALLOCA, BUSTER_TIER0_CLASS(BUSTER_TYPED_CLASS_U64))                                             \
    entry(BUSTER_TYPED_STACK_SAVE, BUSTER_TIER0_CLASS(BUSTER_TYPED_CLASS_U64))                                         \
    entry(BUSTER_TYPED_STACK_RESTORE, BUSTER_TIER0_CLASS(BUSTER_TYPED_CLASS_U64))                                      \
    entry(BUSTER_TYPED_JUMP_INDIRECT, BUSTER_TIER0_CLASS(BUSTER_TYPED_CLASS_U64))                                      \
    entry(BUSTER_TYPED_FRAME_ADDRESS, BUSTER_TIER0_CLASS(BUSTER_TYPED_CLASS_U64))
static const uint8_t tier0_scan_own_low[BUSTER_TYPED_KIND_LIMIT] __attribute__((aligned(64))) = { BUSTER_TIER0_SCAN_OWN(BUSTER_TIER0_LOW_BYTE) };
static const uint8_t tier0_scan_own_high[BUSTER_TYPED_KIND_LIMIT] __attribute__((aligned(64))) = { BUSTER_TIER0_SCAN_OWN(BUSTER_TIER0_HIGH_BYTE) };

// Per kind, the classes its top operand may have, besides TOP_SELF's, COMPARE's and CONVERT's.
#define BUSTER_TIER0_SCAN_TOP(entry)                                                                                   \
    entry(BUSTER_TYPED_LOAD, BUSTER_TIER0_CLASS(BUSTER_TYPED_CLASS_U64))                                               \
    entry(BUSTER_TYPED_SHL, BUSTER_TIER0_CLASSES_INTEGERS)                                                             \
    entry(BUSTER_TYPED_SHR, BUSTER_TIER0_CLASSES_INTEGERS)                                                             \
    entry(BUSTER_TYPED_PTR_ADD, BUSTER_TIER0_CLASSES_WIDE_INTEGERS)                                                    \
    entry(BUSTER_TYPED_EQ, BUSTER_TIER0_CLASSES_VALUES)                                                                \
    entry(BUSTER_TYPED_NE, BUSTER_TIER0_CLASSES_VALUES)                                                                \
    entry(BUSTER_TYPED_LT, BUSTER_TIER0_CLASSES_VALUES)                                                                \
    entry(BUSTER_TYPED_LE, BUSTER_TIER0_CLASSES_VALUES)                                                                \
    entry(BUSTER_TYPED_GT, BUSTER_TIER0_CLASSES_VALUES)                                                                \
    entry(BUSTER_TYPED_GE, BUSTER_TIER0_CLASSES_VALUES)                                                                \
    entry(BUSTER_TYPED_COPY, BUSTER_TIER0_CLASS(BUSTER_TYPED_CLASS_U64))                                               \
    entry(BUSTER_TYPED_ALLOCA, BUSTER_TIER0_CLASS(BUSTER_TYPED_CLASS_U64))                                             \
    entry(BUSTER_TYPED_STACK_RESTORE, BUSTER_TIER0_CLASS(BUSTER_TYPED_CLASS_U64))                                      \
    entry(BUSTER_TYPED_JUMP_INDIRECT, BUSTER_TIER0_CLASS(BUSTER_TYPED_CLASS_U64))
static const uint8_t tier0_scan_top_low[BUSTER_TYPED_KIND_LIMIT] __attribute__((aligned(64))) = { BUSTER_TIER0_SCAN_TOP(BUSTER_TIER0_LOW_BYTE) };
static const uint8_t tier0_scan_top_high[BUSTER_TYPED_KIND_LIMIT] __attribute__((aligned(64))) = { BUSTER_TIER0_SCAN_TOP(BUSTER_TIER0_HIGH_BYTE) };

// Per kind, the classes the operand below its top may have, besides SECOND_SELF's and COMPARE's: an address.
#define BUSTER_TIER0_SCAN_SECOND(entry)                                                                                \
    entry(BUSTER_TYPED_PTR_ADD, BUSTER_TIER0_CLASS(BUSTER_TYPED_CLASS_U64))                                            \
    entry(BUSTER_TYPED_STORE, BUSTER_TIER0_CLASS(BUSTER_TYPED_CLASS_U64))                                              \
    entry(BUSTER_TYPED_SET, BUSTER_TIER0_CLASS(BUSTER_TYPED_CLASS_U64))                                                \
    entry(BUSTER_TYPED_COPY, BUSTER_TIER0_CLASS(BUSTER_TYPED_CLASS_U64))
static const uint8_t tier0_scan_second_low[BUSTER_TYPED_KIND_LIMIT] __attribute__((aligned(64))) = { BUSTER_TIER0_SCAN_SECOND(BUSTER_TIER0_LOW_BYTE) };
static const uint8_t tier0_scan_second_high[BUSTER_TYPED_KIND_LIMIT] __attribute__((aligned(64))) = { BUSTER_TIER0_SCAN_SECOND(BUSTER_TIER0_HIGH_BYTE) };

// Per class (the first 16 bytes of 64, for vpermb), the classes CONVERT to it may take: a 128-bit integer only the
// integers, a floating class no 128-bit integer, an integer every class with a value.
#define BUSTER_TIER0_SCAN_CONVERT(entry)                                                                               \
    entry(BUSTER_TYPED_CLASS_I8, BUSTER_TIER0_CLASSES_VALUES)                                                          \
    entry(BUSTER_TYPED_CLASS_U8, BUSTER_TIER0_CLASSES_VALUES)                                                          \
    entry(BUSTER_TYPED_CLASS_I16, BUSTER_TIER0_CLASSES_VALUES)                                                         \
    entry(BUSTER_TYPED_CLASS_U16, BUSTER_TIER0_CLASSES_VALUES)                                                         \
    entry(BUSTER_TYPED_CLASS_I32, BUSTER_TIER0_CLASSES_VALUES)                                                         \
    entry(BUSTER_TYPED_CLASS_U32, BUSTER_TIER0_CLASSES_VALUES)                                                         \
    entry(BUSTER_TYPED_CLASS_I64, BUSTER_TIER0_CLASSES_VALUES)                                                         \
    entry(BUSTER_TYPED_CLASS_U64, BUSTER_TIER0_CLASSES_VALUES)                                                         \
    entry(BUSTER_TYPED_CLASS_F32, BUSTER_TIER0_CLASSES_NO_INT128)                                                      \
    entry(BUSTER_TYPED_CLASS_F64, BUSTER_TIER0_CLASSES_NO_INT128)                                                      \
    entry(BUSTER_TYPED_CLASS_F80, BUSTER_TIER0_CLASSES_NO_INT128)                                                      \
    entry(BUSTER_TYPED_CLASS_I128, BUSTER_TIER0_CLASSES_NOT)                                                           \
    entry(BUSTER_TYPED_CLASS_U128, BUSTER_TIER0_CLASSES_NOT)
static const uint8_t tier0_scan_convert_low[64] __attribute__((aligned(64))) = { BUSTER_TIER0_SCAN_CONVERT(BUSTER_TIER0_LOW_BYTE) };
static const uint8_t tier0_scan_convert_high[64] __attribute__((aligned(64))) = { BUSTER_TIER0_SCAN_CONVERT(BUSTER_TIER0_HIGH_BYTE) };

// The lanes' own indices, 0 to 63.
static const uint8_t tier0_scan_lane_indices[64] __attribute__((aligned(64))) = {
    0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
    32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63,
};

// Whether each lane's class (below 16) is in its 16-bit mask of classes, whose low and high bytes are `low` and `high`.
static __mmask64 tier0_scan_member(__m512i low, __m512i high, __m512i classes)
{
    const __m512i bits = _mm512_set4_epi32((int)0x80402010, 0x08040201, (int)0x80402010, 0x08040201);
    const __m512i row = _mm512_mask_blend_epi8(_mm512_cmpge_epu8_mask(classes, _mm512_set1_epi8(8)), low, high);
    return _mm512_test_epi8_mask(row, _mm512_shuffle_epi8(bits, classes));
}

// For each lane, the nearest lane before it in the block whose floor is at or below `place` (relative to the block's
// start: where the lane's operand is): that lane, or a byte with bit 7 set where there is none (the operand comes from an
// earlier block). `windows[s]` holds each lane's lowest floor over its last 2^s lanes as a key (the floor with its sign
// bit flipped, so that keys order as unsigned bytes, and 0xff before the block): the parser's doubling search
// (parse_previous_smaller8), with masks.
static __m512i tier0_scan_previous(const __m512i* windows, __m512i place)
{
    const __m512i threshold = _mm512_xor_si512(_mm512_add_epi8(place, _mm512_set1_epi8(1)), _mm512_set1_epi8((char)0x80));
    __m512i lane = _mm512_sub_epi8(_mm512_load_si512(tier0_scan_lane_indices), _mm512_set1_epi8(1));
    for (int32_t s = 5; s >= 0; s -= 1)
    {
        const __mmask64 none = _mm512_cmpge_epu8_mask(_mm512_permutexvar_epi8(lane, windows[s]), threshold);
        lane = _mm512_mask_sub_epi8(lane, none, lane, _mm512_set1_epi8((char)(1 << s)));
    }
    return lane;
}

// A byte vector's lanes shifted ahead by `ahead` (lane j holds lane j + ahead), `fill`'s past the last.
static __m512i tier0_shift_ahead(__m512i current, __m512i fill, uint32_t ahead)
{
    const __m512i index = _mm512_add_epi8(_mm512_load_si512(tier0_scan_lane_indices), _mm512_set1_epi8((char)ahead));
    return _mm512_permutex2var_epi8(current, index, fill);
}

// Each lane's lowest floor among the lanes after it in `kept` (127 where there are none): a lane that pushes keeps its
// value on the stack past them where its own floor is lower.
static __m512i tier0_scan_later_floors(__m512i floors, __mmask64 kept)
{
    const __m512i high = _mm512_set1_epi8(127);
    __m512i lowest = _mm512_mask_mov_epi8(high, kept, floors);
    for (uint32_t step = 1; step < 64; step *= 2)
    {
        lowest = _mm512_min_epi8(lowest, tier0_shift_ahead(lowest, high, step));
    }
    return tier0_shift_ahead(lowest, high, 1);
}

// Quarter q of a byte vector, lanes 16q to 16q + 15.
static __m128i tier0_scan_quarter(__m512i bytes, uint32_t q)
{
    return q == 0 ? _mm512_extracti32x4_epi32(bytes, 0) : q == 1 ? _mm512_extracti32x4_epi32(bytes, 1) : q == 2 ? _mm512_extracti32x4_epi32(bytes, 2)
                                                                                                         : _mm512_extracti32x4_epi32(bytes, 3);
}

// The low bytes of four quarters of dwords, as one byte vector.
static __m512i tier0_scan_bytes(const __m512i* words)
{
    const __m256i low = _mm256_inserti128_si256(_mm256_castsi128_si256(_mm512_cvtepi32_epi8(words[0])), _mm512_cvtepi32_epi8(words[1]), 1);
    const __m256i high = _mm256_inserti128_si256(_mm256_castsi128_si256(_mm512_cvtepi32_epi8(words[2])), _mm512_cvtepi32_epi8(words[3]), 1);
    return _mm512_inserti64x4(_mm512_castsi256_si512(low), high, 1);
}

// The words of `table` at the lanes' indices (the values' low dwords, by quarter) in `lanes`, zero elsewhere, by
// quarter: the lookup of a table too large for vpermi2b, of more than 128 locals or calls.
static void tier0_scan_gather(const uint32_t* table, const __m512i* indices, __mmask64 lanes, __m512i* words)
{
    for (uint32_t q = 0; q < 4; q += 1)
    {
        words[q] = _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), (__mmask16)(lanes >> (16 * q)), indices[q], (const void*)table, 4);
    }
}

// The producer of the value `slot` places below the top before a lane at `depth` (1 for the top): the block's lane the
// search found (`found`, below 64, from the block's `first` entry), or the carried stack's, BUSTER_TIER0_SCAN_NONE below
// the stack's bottom.
static uint32_t tier0_scan_producer(uint32_t first, uint32_t found, uint32_t depth, uint32_t slot, const uint32_t* producers)
{
    return found < 64 ? first + found : producers[64 + (int64_t)depth - slot];
}

// The classes LOCAL, STORE_LOCAL and SET_LOCAL may have on local `local` (tier0_validate_scalar's rule: its own class, or
// for an object every class of its size or less; none for an address-only one), and bit 15 where LOCAL_ADDRESS may take
// its address.
static uint32_t tier0_scan_local(const Tier0Function* function, uint32_t local)
{
    const uint32_t flags = function->local_flags[local];
    uint32_t classes = 0;
    if (tier0_is_object(function, local))
    {
        // By the classes' bytes: 16 for all, then 8 (no memory class), 4 (I8 to U32 and F32), 2 and 1.
        const uint32_t size = function->local_sizes[local];
        classes = size >= 16 ? 0x3ffe : size >= 8 ? 0x07fe : size >= 4 ? 0x027e : size >= 2 ? 0x001e : 0x0006;
    }
    else
    {
        classes = function->local_classes[local] < BUSTER_TYPED_CLASS_VECTOR ? BUSTER_TIER0_CLASS(function->local_classes[local]) : 0;
    }
    classes = (flags & BUSTER_TYPED_LOCAL_ADDRESS_ONLY) == 0 ? classes & BUSTER_TIER0_CLASSES_VALUES : 0;
    return classes | ((flags & BUSTER_TYPED_LOCAL_ADDRESS_TAKEN) != 0 ? 0x8000u : 0);
}

// tier0_scan_local's classes of the 64 locals from `first` (below the count), as their low bytes and their high bytes by
// lane (zero past the locals): for vpermi2b, in registers, where a vector load of bytes just stored one at a time would
// stall. It reads no quarter of 16 past the locals: a masked load of memory past an arena's touched pages takes a slow
// path, though it reads none of it.
static void tier0_scan_locals(const Tier0Function* function, uint32_t first, __m512i* low, __m512i* high)
{
    const uint32_t count = function->local_count;
    const __m512i classes = tier0_bytes_at(function->local_classes, first, count);
    const __m512i flags = tier0_bytes_at(function->local_flags, first, count);
    // An object's size, by its classes' bytes (none, 1, 2, 4, 8 and 16 or more), from the optional sizes' column.
    __m512i sizes[4] = { _mm512_setzero_si512(), _mm512_setzero_si512(), _mm512_setzero_si512(), _mm512_setzero_si512() };
    for (uint32_t q = 0; q < 4 && first + 16 * q < count; q += 1)
    {
        const __mmask16 quarter = (__mmask16)(tier0_lanes(first, count) >> (16 * q));
        const uintptr_t at = (uintptr_t)function->local_sizes + ((uintptr_t)first + 16 * q) * sizeof(uint32_t);
        const __m512i size = function->local_sizes != 0 ? _mm512_maskz_loadu_epi32(quarter, (const void*)at) : _mm512_setzero_si512();
        __m512i step = _mm512_setzero_si512();
        static const uint32_t thresholds[5] = { 1, 2, 4, 8, 16 };
        for (uint32_t t = 0; t < 5; t += 1)
        {
            step = _mm512_mask_add_epi32(step, _mm512_cmpge_epu32_mask(size, _mm512_set1_epi32((int)thresholds[t])), step, _mm512_set1_epi32(1));
        }
        sizes[q] = step;
    }
    const __m512i size = tier0_scan_bytes(sizes);
    // By size: 16 bytes or more, every class; 8, all but the memory classes; 4, I8 to U32 and F32; 2 and 1.
    const __m512i object_low = _mm512_set_epi64(0, 0, 0, 0, 0, 0, 0, 0x0000fefe7e1e0600ll);
    const __m512i object_high = _mm512_set_epi64(0, 0, 0, 0, 0, 0, 0, 0x00003f0702000000ll);
    const __m512i bits = _mm512_set4_epi32((int)0x80402010, 0x08040201, (int)0x80402010, 0x08040201);
    const __m512i bit = _mm512_shuffle_epi8(bits, classes);
    const __mmask64 objects = _mm512_test_epi8_mask(size, size);
    const __mmask64 high_classes = _mm512_cmpge_epu8_mask(classes, _mm512_set1_epi8(8));
    __m512i classes_low = _mm512_mask_blend_epi8(objects, _mm512_maskz_mov_epi8(~high_classes, bit), _mm512_permutexvar_epi8(size, object_low));
    __m512i classes_high = _mm512_mask_blend_epi8(objects, _mm512_maskz_mov_epi8(high_classes, bit), _mm512_permutexvar_epi8(size, object_high));
    // None for an address-only local, every one with a value (no VOID, no vector), and LOCAL_ADDRESS's bit where its
    // address is taken.
    const __mmask64 usable = _mm512_testn_epi8_mask(flags, _mm512_set1_epi8(BUSTER_TYPED_LOCAL_ADDRESS_ONLY));
    classes_low = _mm512_and_si512(_mm512_maskz_mov_epi8(usable, classes_low), _mm512_set1_epi8((char)(BUSTER_TIER0_CLASSES_VALUES & 0xff)));
    classes_high = _mm512_and_si512(_mm512_maskz_mov_epi8(usable, classes_high), _mm512_set1_epi8((char)(BUSTER_TIER0_CLASSES_VALUES >> 8)));
    const __mmask64 taken = _mm512_test_epi8_mask(flags, _mm512_set1_epi8(BUSTER_TYPED_LOCAL_ADDRESS_TAKEN));
    *low = classes_low;
    *high = _mm512_or_si512(classes_high, _mm512_maskz_mov_epi8(taken, _mm512_set1_epi8((char)0x80)));
}

// The operands (each argument, and an indirect call's callee) of the 64 calls from `first` (below the count), by lane
// (zero past the calls), in a register; it reads no quarter of 16 past the calls (tier0_scan_locals).
static __m512i tier0_scan_operands(const Tier0Function* function, uint32_t first)
{
    const uint32_t count = function->call_count;
    __m512i operands[4] = { _mm512_setzero_si512(), _mm512_setzero_si512(), _mm512_setzero_si512(), _mm512_setzero_si512() };
    for (uint32_t q = 0; q < 4 && first + 16 * q < count; q += 1)
    {
        const __mmask16 quarter = (__mmask16)(tier0_lanes(first, count) >> (16 * q));
        const uintptr_t arguments = (uintptr_t)function->call_argument_counts + ((uintptr_t)first + 16 * q) * sizeof(uint32_t);
        const uintptr_t targets = (uintptr_t)function->call_targets + ((uintptr_t)first + 16 * q) * sizeof(SymbolId);
        const __m512i target = _mm512_maskz_loadu_epi32(quarter, (const void*)targets);
        const __m512i argument = _mm512_maskz_loadu_epi32(quarter, (const void*)arguments);
        operands[q] = _mm512_mask_add_epi32(argument, quarter & _mm512_testn_epi32_mask(target, target), argument, _mm512_set1_epi32(1));
    }
    _Static_assert(sizeof(SymbolId) == 4, "a call's target is a dword");
    return tier0_scan_bytes(operands);
}

// The classes a CALL of call `call` may have (tier0_validate_scalar's rules): VOID alone where an object takes its
// result, no 128-bit integer (an object takes one), and with a result in x87 registers F80 where it is a long double's
// value in st0, and VOID where an object takes it.
static uint32_t tier0_scan_call(const Tier0Function* function, uint32_t call)
{
    const bool object = function->call_result_objects != 0 && function->call_result_objects[call] != 0;
    const AbiReturn returned = function->calls[call].result;
    uint32_t classes = BUSTER_TIER0_CLASSES_ALL &
                       ~(BUSTER_TIER0_CLASS(BUSTER_TYPED_CLASS_I128) | BUSTER_TIER0_CLASS(BUSTER_TYPED_CLASS_U128) | BUSTER_TIER0_CLASS(BUSTER_TYPED_CLASS_F80));
    if (object)
    {
        classes = BUSTER_TIER0_CLASS(BUSTER_TYPED_CLASS_VOID);
    }
    else if (tier0_x87_parts(returned) != 0)
    {
        classes = tier0_x87_parts(returned) == 1 && tier0_x87_result_valid(returned, 0) ? BUSTER_TIER0_CLASS(BUSTER_TYPED_CLASS_F80) : 0;
    }
    return classes;
}

// The classes call `call`'s operands must have on the stack, from the first pushed (an indirect call's callee, U64, then
// its arguments), by lane, and their count, into `count`.
static __m512i tier0_scan_call_operands(const Tier0Function* function, uint32_t call, uint32_t* count)
{
    const uint32_t arguments = function->call_argument_counts[call];
    const bool indirect = function->call_targets[call] == 0;
    const uintptr_t classes = (uintptr_t)function->argument_classes + function->call_first[call];
    const __m512i given = _mm512_maskz_loadu_epi8(_bzhi_u64(~0ull, arguments), (const void*)classes);
    *count = arguments + (indirect ? 1 : 0);
    return _mm512_mask_blend_epi8(indirect ? ~(__mmask64)0 : 0, given, tier0_shift_back(given, _mm512_set1_epi8(BUSTER_TYPED_CLASS_U64), 1));
}

// The locals' rules of tier0_frame_input_valid and tier0_validate_tables for a function without an incoming variadic
// area, 64 at a time: flags of the roles alone, an address-only local a va_list U64 past the parameters with its address
// taken; an object an address-taken VOID one aligned to 2^30 at most, the objects' bytes (each one's rounded up to 16
// and, aligned past 16, its alignment less 16) within BUSTER_TIER0_MAX_OBJECT_BYTES; any other one of a value's class
// but a memory class's or a vector's.
static bool tier0_scan_locals_valid(const Tier0Function* function)
{
    const uint32_t count = function->local_count;
    bool valid = !(function->local_sizes != 0 && function->local_alignments == 0);
    __m512i object_bytes = _mm512_setzero_si512();
    for (uint32_t first = 0; valid && first < count; first += 64)
    {
        const __mmask64 lanes = tier0_lanes(first, count);
        const __m512i classes = tier0_bytes_at(function->local_classes, first, count);
        const __m512i flags = tier0_bytes_at(function->local_flags, first, count);
        const __mmask64 parameters = tier0_lanes(first, function->parameter_count);
        const __mmask64 roles = _mm512_test_epi8_mask(flags, _mm512_set1_epi8(BUSTER_TYPED_LOCAL_ADDRESS_ONLY));
        const __mmask64 taken = _mm512_test_epi8_mask(flags, _mm512_set1_epi8(BUSTER_TYPED_LOCAL_ADDRESS_TAKEN));
        __mmask64 refused = _mm512_test_epi8_mask(flags, _mm512_set1_epi8((char)~(BUSTER_TYPED_LOCAL_ADDRESS_TAKEN | BUSTER_TYPED_LOCAL_ADDRESS_ONLY)));
        refused |= roles & (_mm512_cmpneq_epi8_mask(_mm512_and_si512(flags, _mm512_set1_epi8(BUSTER_TYPED_LOCAL_ADDRESS_ONLY)), _mm512_set1_epi8(BUSTER_TYPED_LOCAL_VA_LIST)) |
                            _mm512_cmpneq_epi8_mask(classes, _mm512_set1_epi8(BUSTER_TYPED_CLASS_U64)) | ~taken | parameters);
        // The objects, and their bytes, in 64-bit lanes.
        __mmask64 objects = 0;
        const __m512i alignments = function->local_alignments != 0 ? tier0_bytes_at(function->local_alignments, first, count) : _mm512_setzero_si512();
        for (uint32_t e = 0; function->local_sizes != 0 && e < 8 && first + 8 * e < count; e += 1)
        {
            const __mmask8 eighth = (__mmask8)(lanes >> (8 * e));
            const uintptr_t at = (uintptr_t)function->local_sizes + ((uintptr_t)first + 8 * e) * sizeof(uint32_t);
            const __m512i size = _mm512_cvtepu32_epi64(_mm256_maskz_loadu_epi32(eighth, (const void*)at));
            const __m512i log2 = _mm512_cvtepu8_epi64(_mm512_castsi512_si128(_mm512_permutexvar_epi8(_mm512_add_epi8(_mm512_load_si512(tier0_scan_lane_indices), _mm512_set1_epi8((char)(8 * e))), alignments)));
            const __mmask8 present = _mm512_test_epi64_mask(size, size);
            const __m512i rounded = _mm512_and_si512(_mm512_add_epi64(size, _mm512_set1_epi64(15)), _mm512_set1_epi64(~15ll));
            const __m512i slack = _mm512_maskz_sub_epi64(_mm512_cmpgt_epu64_mask(log2, _mm512_set1_epi64(BUSTER_TIER0_FRAME_ALIGNMENT_LOG2)),
                                                         _mm512_sllv_epi64(_mm512_set1_epi64(1), log2), _mm512_set1_epi64(16));
            object_bytes = _mm512_add_epi64(object_bytes, _mm512_maskz_add_epi64(present, rounded, slack));
            objects |= (__mmask64)present << (8 * e);
        }
        refused |= objects & (_mm512_cmpneq_epi8_mask(classes, _mm512_setzero_si512()) | ~taken |
                              _mm512_cmpgt_epu8_mask(alignments, _mm512_set1_epi8(BUSTER_TIER0_MAX_OBJECT_ALIGNMENT_LOG2)));
        refused |= ~objects & (_mm512_cmpeq_epi8_mask(classes, _mm512_setzero_si512()) | _mm512_cmpgt_epu8_mask(classes, _mm512_set1_epi8(BUSTER_TYPED_CLASS_F64)));
        valid = (refused & lanes) == 0;
    }
    return valid && (uint64_t)_mm512_reduce_add_epi64(object_bytes) <= BUSTER_TIER0_MAX_OBJECT_BYTES;
}

// The calls' rules of tier0_calls_valid and tier0_validate_tables for a function without aggregates' metadata, each
// call's arguments eight at a time: the arguments in range, at most BUSTER_TIER0_MAX_CALL_ARGUMENTS, the area's alignment
// 2^30 at most, and no result through a hidden pointer (no object to take it); each argument of a value's class (not VOID,
// not a 128-bit integer's or a vector's) ignored, in an argument register of its kind that no other argument of the call
// takes, or in an 8-byte slot inside the call's area, a long double's a 16-byte one at a multiple of 16.
static bool tier0_scan_calls_valid(const Tier0Function* function)
{
    bool valid = true;
    for (uint32_t c = 0; valid && c < function->call_count; c += 1)
    {
        const uint32_t first = function->call_first[c];
        const uint32_t count = function->call_argument_counts[c];
        const AbiCall* call = &function->calls[c];
        const __m512i stack_bytes = _mm512_set1_epi64((int64_t)call->stack_bytes);
        valid = (uint64_t)first + count <= function->argument_count && count <= BUSTER_TIER0_MAX_CALL_ARGUMENTS &&
                call->stack_alignment_log2 <= BUSTER_TIER0_MAX_OBJECT_ALIGNMENT_LOG2 && call->result.kind != BUSTER_ABI_RESULT_MEMORY;
        uint64_t either = 0;
        uint64_t sum = 0;
        for (uint32_t a = 0; valid && a < count; a += 8)
        {
            const __mmask8 lanes = (__mmask8)_bzhi_u32(0xff, count - a);
            const uintptr_t at = (uintptr_t)function->argument_locations + ((uintptr_t)first + a) * sizeof(AbiLocation);
            const uintptr_t classes_at = (uintptr_t)function->argument_classes + first + a;
            const __m512i locations = _mm512_maskz_loadu_epi64(lanes, (const void*)at);
            const __m512i classes = _mm512_cvtepu8_epi64(_mm_maskz_loadu_epi8((__mmask16)lanes, (const void*)classes_at));
            _Static_assert(sizeof(AbiLocation) == 8 && offsetof(AbiLocation, registers) == 4 && offsetof(AbiLocation, kind) == 6, "a location's fields in its qword");
            const __m512i offset = _mm512_and_si512(locations, _mm512_set1_epi64(0xffffffffll));
            const __m512i reg = _mm512_and_si512(_mm512_srli_epi64(locations, 32), _mm512_set1_epi64(0xff));
            const __m512i kind = _mm512_and_si512(_mm512_srli_epi64(locations, 48), _mm512_set1_epi64(0xff));
            const __mmask8 registers = _mm512_cmpeq_epi64_mask(kind, _mm512_set1_epi64(BUSTER_ABI_LOCATION_REGISTERS));
            const __mmask8 stack = _mm512_cmpeq_epi64_mask(kind, _mm512_set1_epi64(BUSTER_ABI_LOCATION_STACK));
            // A register's bit (tier0_register_bit): a vector one's from BUSTER_TIER0_XMM(0), of the argument registers.
            const __mmask8 vector = _mm512_test_epi64_mask(reg, _mm512_set1_epi64(BUSTER_ABI_REGISTER_VECTOR));
            const __m512i number = _mm512_mask_add_epi64(reg, vector, _mm512_and_si512(reg, _mm512_set1_epi64(0x1f)), _mm512_set1_epi64(BUSTER_TIER0_XMM(0)));
            const __m512i bit = _mm512_and_si512(_mm512_sllv_epi64(_mm512_set1_epi64(1), number), _mm512_set1_epi64(BUSTER_TIER0_ARGUMENT_REGISTERS));
            const __mmask8 argument_register = _mm512_cmplt_epu64_mask(_mm512_andnot_si512(_mm512_set1_epi64(BUSTER_ABI_REGISTER_VECTOR), reg), _mm512_set1_epi64(16)) &
                                               _mm512_test_epi64_mask(bit, bit);
            const __mmask8 slot = _mm512_testn_epi64_mask(offset, _mm512_set1_epi64(BUSTER_ABI_SLOT_BYTES - 1)) &
                                  _mm512_cmple_epu64_mask(_mm512_add_epi64(offset, _mm512_set1_epi64(BUSTER_ABI_SLOT_BYTES)), stack_bytes);
            const __mmask8 placed = _mm512_cmpeq_epi64_mask(kind, _mm512_set1_epi64(BUSTER_ABI_LOCATION_IGNORE)) | (registers & argument_register) | (stack & slot);
            const __mmask8 floating = _mm512_cmpeq_epi64_mask(classes, _mm512_set1_epi64(BUSTER_TYPED_CLASS_F32)) |
                                      _mm512_cmpeq_epi64_mask(classes, _mm512_set1_epi64(BUSTER_TYPED_CLASS_F64));
            const __mmask8 x87 = _mm512_cmpeq_epi64_mask(classes, _mm512_set1_epi64(BUSTER_TYPED_CLASS_F80));
            const __mmask8 x87_placed = stack & _mm512_testn_epi64_mask(offset, _mm512_set1_epi64(15)) &
                                        _mm512_cmple_epu64_mask(_mm512_add_epi64(offset, _mm512_set1_epi64(16)), stack_bytes);
            __mmask8 refused = ~placed | (registers & (floating ^ vector)) | (x87 & ~x87_placed);
            refused |= _mm512_cmpeq_epi64_mask(classes, _mm512_setzero_si512()) | _mm512_cmpgt_epu64_mask(classes, _mm512_set1_epi64(BUSTER_TYPED_CLASS_F80));
            const __m512i taken = _mm512_maskz_mov_epi64(registers & lanes, bit);
            either |= (uint64_t)_mm512_reduce_or_epi64(taken);
            sum += (uint64_t)_mm512_reduce_add_epi64(taken);
            valid = (refused & lanes) == 0;
        }
        // No register two arguments take: their bits' sum is their union.
        valid = valid && either == sum;
    }
    return valid;
}

// tier0_validate_scalar's checks of the frame and the tables (tier0_frame_input_valid, tier0_calls_valid,
// tier0_parameters_valid and tier0_validate_tables): for a function without the optional parts that only those take
// (aggregates' metadata, an incoming variadic area, vectors' constants and operations), the locals 64 at a time and each
// call's arguments eight at a time, and the scalar helpers' for the parameters and the rest; the scalar helpers' alone
// for a function with them.
static bool tier0_scan_tables(const Tier0Function* function)
{
    const bool plain = function->argument_sizes == 0 && function->call_result_objects == 0 && function->incoming_variadic == 0 && function->result_size == 0 &&
                       function->result.kind != BUSTER_ABI_RESULT_MEMORY && function->vector_constant_count == 0 && function->operation_count == 0;
    bool valid = function->local_count <= BUSTER_TIER0_MAX_LOCALS && function->parameter_count <= function->local_count;
    if (valid && !plain)
    {
        valid = tier0_frame_input_valid(function) && tier0_calls_valid(function) && tier0_parameters_valid(function);
        Tier0Validator tables;
        memset(&tables, 0, sizeof(tables));
        if (valid)
        {
            tier0_validate_tables(function, &tables);
        }
        valid = valid && tables.check.error == TIER0_OK;
    }
    else if (valid)
    {
        valid = tier0_scan_locals_valid(function) && tier0_scan_calls_valid(function) && tier0_parameters_valid(function);
        // Each parameter that isn't an object in a register of its kind or on the stack (tier0_parameters_valid checks the
        // registers and the slots).
        for (uint32_t p = 0; valid && p < function->parameter_count; p += 1)
        {
            const AbiLocation location = function->parameters[p];
            const bool floating = tier0_class_floating(function->local_classes[p]);
            const bool vector = (location.registers[0] & BUSTER_ABI_REGISTER_VECTOR) != 0;
            valid = tier0_is_object(function, p) || location.kind == BUSTER_ABI_LOCATION_STACK || (location.kind == BUSTER_ABI_LOCATION_REGISTERS && floating == vector);
        }
        // Long double's constants, and its result: one long double in st0, in a function with x87.
        const uint32_t parts = tier0_x87_parts(function->result);
        valid = valid && (function->x87_constant_count == 0 || function->x87_constants != 0) && (parts == 0 || (parts == 1 && tier0_x87_result_valid(function->result, 0))) &&
                !(tier0_x87_returns(function) && !function->x87);
    }
    return valid;
}

// The vector path: whether it takes the function's stream, which is then one tier0_validate_scalar takes too. False
// leaves the function to the scalar validator, for the verdict and the rule.
static __attribute__((noinline)) bool tier0_validate_accepts(const Tier0Function* function)
{
    const uint32_t count = function->count;
    const uint32_t local_count = function->local_count;
    const uint32_t call_count = function->call_count;
    // What tier0_validate_scalar checks before the stream: the limits, the frame, the calls, the parameters and the tables.
    bool valid = !function->vectors && count <= BUSTER_TIER0_MAX_ENTRIES && (function->label_count == 0 || function->label_entries != 0) && tier0_scan_tables(function);
    Scratch scratch = scratch_begin(0);
    // Per entry, the depth before it (and room for a block's store past the last); per entry and the end, the depth the
    // forward arrivals there bring (UNREACHED before the first) and what else they bring: the producer just below what
    // they may differ in, whether they bring a value (a JUMP_VALUE's) and its class; and past 128 locals or calls, their
    // words for the gathers (tier0_scan_local's, and a call's operands).
    uint8_t* depths = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint8_t, (uint64_t)count + 64) : 0;
    uint8_t* expected = depths != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint8_t, (uint64_t)count + 65) : 0;
    uint64_t* arrivals = expected != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint64_t, (uint64_t)count + 1) : 0;
    // Per entry and the end, a bit where an arrival there brings nothing but depth 0, the most of them (statements').
    uint64_t* emptied_targets = arrivals != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint64_t, (uint64_t)count / 64 + 1) : 0;
    const bool few_locals = local_count <= 128;
    const bool few_calls = call_count <= 128;
    uint32_t* local_words = arrivals != 0 && !few_locals ? BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint32_t, local_count) : 0;
    uint32_t* call_words = arrivals != 0 && !few_calls ? BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint32_t, call_count) : 0;
    valid = valid && emptied_targets != 0 && (few_locals || local_words != 0) && (few_calls || call_words != 0);
    // The locals' classes and the calls' operands for vpermi2b where there are 128 or fewer (each table's two halves),
    // and their words for the gathers otherwise.
    __m512i local_low[2] = { _mm512_setzero_si512(), _mm512_setzero_si512() };
    __m512i local_high[2] = { _mm512_setzero_si512(), _mm512_setzero_si512() };
    __m512i call_operands[2] = { _mm512_setzero_si512(), _mm512_setzero_si512() };
    for (uint32_t h = 0; valid && h < 2 && few_locals && 64 * h < local_count; h += 1)
    {
        tier0_scan_locals(function, 64 * h, &local_low[h], &local_high[h]);
    }
    for (uint32_t h = 0; valid && h < 2 && few_calls && 64 * h < call_count; h += 1)
    {
        call_operands[h] = tier0_scan_operands(function, 64 * h);
    }
    for (uint32_t l = 0; valid && !few_locals && l < local_count; l += 1)
    {
        local_words[l] = tier0_scan_local(function, l);
    }
    for (uint32_t c = 0; valid && !few_calls && c < call_count; c += 1)
    {
        // Counts fit in a byte, including an indirect callee. More than 64 operands defer to the scalar validator.
        call_words[c] = function->call_argument_counts[c] + (function->call_targets[c] == 0 ? 1 : 0);
    }
    // The values' bounds, by BUSTER_TIER0_BOUND_* (an empty range where there is nothing to name: [1, 0]), built in
    // registers: a vector load of words just stored one at a time would stall.
    _Static_assert(BUSTER_TIER0_BOUND_NONE == 0 && BUSTER_TIER0_BOUND_LOCAL == 1 && BUSTER_TIER0_BOUND_CALL == 2 && BUSTER_TIER0_BOUND_TARGET == 3 &&
                       BUSTER_TIER0_BOUND_SYMBOL == 4 && BUSTER_TIER0_BOUND_COPY == 5 && BUSTER_TIER0_BOUND_ZERO == 6 && BUSTER_TIER0_BOUND_X87 == 7,
                   "the bounds' order in their vectors");
    const uint32_t x87_count = function->x87_constant_count;
    const uint32_t symbol_limit = function->symbol_access == 0 ? UINT32_MAX : function->symbol_count == 0 ? 0 : function->symbol_count - 1;
    const __m512i minimum = _mm512_set_epi32(0, 0, 0, 0, 0, 0, 0, 0, x87_count == 0 ? 1 : 0, 0, 1, 1, 0, call_count == 0 ? 1 : 0, local_count == 0 ? 1 : 0, 0);
    const __m512i maximum = _mm512_set_epi32(0, 0, 0, 0, 0, 0, 0, 0, (int)(x87_count == 0 ? 0 : x87_count - 1), 0, BUSTER_TIER0_MAX_COPY_BYTES, (int)symbol_limit,
                                             (int)count, (int)(call_count == 0 ? 0 : call_count - 1), (int)(local_count == 0 ? 0 : local_count - 1), (int)UINT32_MAX);
    // Every kind's classes with the function's: long double's and the 128-bit integers' only where it has them; RETURN's
    // the address of an aggregate's bytes (U64) where the function returns one, no 128-bit integer, and F80 exactly where
    // it returns a long double's value in st0; JUMP_INDIRECT's none without labels.
    const uint32_t allowed = BUSTER_TIER0_CLASSES_ALL & ~(function->x87 ? 0 : BUSTER_TIER0_CLASS(BUSTER_TYPED_CLASS_F80)) &
                             ~(function->int128 ? 0 : BUSTER_TIER0_CLASS(BUSTER_TYPED_CLASS_I128) | BUSTER_TIER0_CLASS(BUSTER_TYPED_CLASS_U128));
    const bool aggregate = function->result_size != 0 || function->result.kind == BUSTER_ABI_RESULT_MEMORY;
    uint32_t returned = (aggregate ? BUSTER_TIER0_CLASS(BUSTER_TYPED_CLASS_U64) : BUSTER_TIER0_CLASSES_ALL) &
                        ~(BUSTER_TIER0_CLASS(BUSTER_TYPED_CLASS_I128) | BUSTER_TIER0_CLASS(BUSTER_TYPED_CLASS_U128));
    returned &= tier0_x87_returns(function) ? BUSTER_TIER0_CLASS(BUSTER_TYPED_CLASS_F80) : ~BUSTER_TIER0_CLASS(BUSTER_TYPED_CLASS_F80);
    returned &= allowed;
    const uint32_t indirect = function->label_count != 0 ? BUSTER_TIER0_CLASS(BUSTER_TYPED_CLASS_U64) : 0;
    __m512i own_low = _mm512_and_si512(_mm512_load_si512(tier0_scan_own_low), _mm512_set1_epi8((char)(allowed & 0xff)));
    __m512i own_high = _mm512_and_si512(_mm512_load_si512(tier0_scan_own_high), _mm512_set1_epi8((char)(allowed >> 8)));
    own_low = _mm512_mask_mov_epi8(own_low, (__mmask64)1 << BUSTER_TYPED_RETURN, _mm512_set1_epi8((char)(returned & 0xff)));
    own_high = _mm512_mask_mov_epi8(own_high, (__mmask64)1 << BUSTER_TYPED_RETURN, _mm512_set1_epi8((char)(returned >> 8)));
    own_low = _mm512_mask_mov_epi8(own_low, (__mmask64)1 << BUSTER_TYPED_JUMP_INDIRECT, _mm512_set1_epi8((char)(indirect & 0xff)));
    own_high = _mm512_mask_mov_epi8(own_high, (__mmask64)1 << BUSTER_TYPED_JUMP_INDIRECT, _mm512_set1_epi8((char)(indirect >> 8)));
    if (valid)
    {
        memset(expected, BUSTER_TIER0_SCAN_UNREACHED, (uint64_t)count + 65);
        memset(arrivals, 0, ((uint64_t)count + 1) * sizeof(uint64_t));
        memset(emptied_targets, 0, ((uint64_t)count / 64 + 1) * sizeof(uint64_t));
    }
    // The labels JUMP_INDIRECT may go to: arrivals with nothing on the stack, before the first entry.
    for (uint32_t k = 0; valid && k < function->label_count; k += 1)
    {
        const uint32_t target = function->label_entries[k];
        valid = target <= count;
        if (valid)
        {
            expected[target] = 0;
            arrivals[target] = BUSTER_TIER0_SCAN_NONE;
        }
    }
    // The carried stack: each place's class, by lane from place 0, and its producer, from place 0 at index 64.
    __m512i carried = _mm512_setzero_si512();
    uint32_t stack_producers[BUSTER_TIER0_SCAN_STACK] __attribute__((aligned(64)));
    for (uint32_t p = 0; p < 64; p += 1)
    {
        stack_producers[p] = BUSTER_TIER0_SCAN_NONE;
    }
    const __m512i zero = _mm512_setzero_si512();
    const __m512i one = _mm512_set1_epi8(1);
    const __m512i lane_indices = _mm512_load_si512(tier0_scan_lane_indices);
    const __m512i even = _mm512_set_epi32(30, 28, 26, 24, 22, 20, 18, 16, 14, 12, 10, 8, 6, 4, 2, 0);
    const __m512i odd = _mm512_add_epi32(even, _mm512_set1_epi32(1));
    uint32_t base = 0;
    uint32_t previous_kind = BUSTER_TYPED_NONE;
    uint32_t previous_class = BUSTER_TYPED_CLASS_VOID;
    uint32_t return_class = BUSTER_TIER0_SCAN_UNREACHED;
    for (uint32_t first = 0; valid && first < count; first += 64)
    {
        const __mmask64 lanes = tier0_lanes(first, count);
        const uint32_t live = count - first < 64 ? count - first : 64;
        const __m512i kinds = tier0_bytes_at(function->kinds, first, count);
        const __m512i classes = tier0_bytes_at(function->classes, first, count);
        const __m512i previous_kinds = tier0_shift_back(kinds, _mm512_set1_epi8((char)previous_kind), 1);
        const __m512i previous_classes = tier0_shift_back(classes, _mm512_set1_epi8((char)previous_class), 1);
        // An unknown kind or class, an X86 entry and a vector's class: the scalar validator's.
        __mmask64 refused = lanes & (_mm512_cmpeq_epi8_mask(kinds, zero) | _mm512_cmpge_epu8_mask(kinds, _mm512_set1_epi8(BUSTER_TYPED_X86)) |
                                     _mm512_cmpge_epu8_mask(classes, _mm512_set1_epi8(BUSTER_TYPED_CLASS_VECTOR)));
        const __m512i shapes = tier0_lookup(tier0_scan_shapes, kinds);
        const __m512i rules = tier0_lookup(tier0_scan_rules, kinds);
        const __m512i sides = tier0_lookup(tier0_scan_sides, kinds);
        const __mmask64 x87_constants = _mm512_cmpeq_epi8_mask(kinds, _mm512_set1_epi8(BUSTER_TYPED_CONST)) &
                                        _mm512_cmpeq_epi8_mask(classes, _mm512_set1_epi8(BUSTER_TYPED_CLASS_F80));
        const __m512i bounds = _mm512_mask_mov_epi8(tier0_lookup(tier0_scan_bounds, kinds), x87_constants, _mm512_set1_epi8(BUSTER_TIER0_BOUND_X87));
        // The values, by their low and high dwords in quarters of 16 lanes, against their kinds' bounds; their low bytes
        // index the locals' and the calls' tables. No load of eight past the entries (tier0_scan_locals).
        __m512i low_words[4];
        __mmask64 bounded = 0;
        __mmask64 nonzero = 0;
        for (uint32_t q = 0; q < 4; q += 1)
        {
            const uintptr_t at = (uintptr_t)function->values + ((uintptr_t)first + 16 * q) * sizeof(uint64_t);
            const __m512i a = first + 16 * q < count ? _mm512_maskz_loadu_epi64((__mmask8)(lanes >> (16 * q)), (const void*)at) : zero;
            const __m512i b = first + 16 * q + 8 < count ? _mm512_maskz_loadu_epi64((__mmask8)(lanes >> (16 * q + 8)), (const void*)(at + 64)) : zero;
            low_words[q] = _mm512_permutex2var_epi32(a, even, b);
            const __m512i high_words = _mm512_permutex2var_epi32(a, odd, b);
            const __m512i bound = _mm512_cvtepu8_epi32(tier0_scan_quarter(bounds, q));
            const __mmask16 inside = _mm512_cmpge_epu32_mask(low_words[q], _mm512_permutexvar_epi32(bound, minimum)) &
                                     _mm512_cmple_epu32_mask(low_words[q], _mm512_permutexvar_epi32(bound, maximum)) &
                                     (_mm512_cmpeq_epi32_mask(high_words, zero) | _mm512_cmpeq_epi32_mask(bound, zero));
            const __m512i any = _mm512_or_si512(low_words[q], high_words);
            bounded |= (__mmask64)inside << (16 * q);
            nonzero |= (__mmask64)_mm512_test_epi32_mask(any, any) << (16 * q);
        }
        refused |= lanes & (~bounded | (_mm512_test_epi8_mask(shapes, _mm512_set1_epi8(BUSTER_TIER0_SCAN_NONZERO)) & ~nonzero));
        const __m512i indices = tier0_scan_bytes(low_words);
        // What each entry pops: its kind's, a call's operands, nothing for a RETURN of nothing; and what it pushes: a value
        // of its class, for a call only where it has one.
        const __mmask64 calls = lanes & _mm512_test_epi8_mask(sides, _mm512_set1_epi8(BUSTER_TIER0_SIDE_CALL));
        const __mmask64 returns = lanes & _mm512_test_epi8_mask(sides, _mm512_set1_epi8(BUSTER_TIER0_SIDE_RETURN));
        const __mmask64 empty_returns = returns & _mm512_cmpeq_epi8_mask(classes, zero);
        __m512i operands = zero;
        if (few_calls)
        {
            operands = _mm512_permutex2var_epi8(call_operands[0], indices, call_operands[1]);
        }
        else if (calls != 0)
        {
            __m512i words[4];
            tier0_scan_gather(call_words, low_words, calls & bounded, words);
            operands = tier0_scan_bytes(words);
        }
        // One comparison register holds at most 64 operands. An indirect call with 64 arguments has 65; its
        // complete operand check belongs to the scalar validator rather than dropping the last argument.
        refused |= calls & _mm512_cmpgt_epu8_mask(operands, _mm512_set1_epi8(64));
        __m512i pops = _mm512_and_si512(shapes, _mm512_set1_epi8(3));
        pops = _mm512_mask_mov_epi8(pops, calls, operands);
        pops = _mm512_mask_mov_epi8(pops, empty_returns, zero);
        const __mmask64 pushes = lanes & _mm512_test_epi8_mask(shapes, _mm512_set1_epi8(BUSTER_TIER0_SCAN_PUSH)) & ~(calls & _mm512_cmpeq_epi8_mask(classes, zero));
        valid = valid && (uint64_t)_mm512_reduce_add_epi64(_mm512_sad_epu8(pops, zero)) <= BUSTER_TIER0_SCAN_POPS;
        // The depths before and after each entry, relative to the block's start: prefix sums of the effects in six shifted
        // adds, and its floor, the depth after its pops.
        const __m512i effects = _mm512_sub_epi8(_mm512_maskz_mov_epi8(pushes, one), pops);
        __m512i after = effects;
        for (uint32_t step = 1; step < 64; step *= 2)
        {
            after = _mm512_add_epi8(after, tier0_shift_back(after, zero, step));
        }
        const __m512i before = _mm512_sub_epi8(after, effects);
        const __m512i floors = _mm512_sub_epi8(before, pops);
        // An operand the stack doesn't have: the lowest place an entry reads (its floor, or DUP's top) below the stack's
        // bottom.
        const __mmask64 tops = lanes & _mm512_test_epi8_mask(rules, _mm512_set1_epi8(BUSTER_TIER0_RULE_TOP)) & ~empty_returns;
        const __m512i lowest = _mm512_sub_epi8(before, _mm512_max_epu8(pops, _mm512_maskz_mov_epi8(tops, one)));
        refused |= lanes & _mm512_cmplt_epi8_mask(lowest, _mm512_set1_epi8((char)-(int32_t)base));
        // Each operand's producer: for the top (place before - 1) and the one below it (before - 2), the nearest lane whose
        // floor is at or below the place, or the carried stack's below the block's start; and its class.
        __m512i windows[6];
        windows[0] = _mm512_xor_si512(floors, _mm512_set1_epi8((char)0x80));
        for (uint32_t s = 1; s < 6; s += 1)
        {
            windows[s] = _mm512_min_epu8(windows[s - 1], tier0_shift_back(windows[s - 1], _mm512_set1_epi8((char)0xff), 1u << (s - 1)));
        }
        const __m512i first_producers = tier0_scan_previous(windows, _mm512_sub_epi8(before, one));
        const __m512i second_producers = tier0_scan_previous(windows, _mm512_sub_epi8(before, _mm512_set1_epi8(2)));
        const __m512i top = _mm512_mask_blend_epi8(~_mm512_movepi8_mask(first_producers), _mm512_permutexvar_epi8(_mm512_add_epi8(before, _mm512_set1_epi8((char)(base - 1))), carried),
                                                   _mm512_permutexvar_epi8(first_producers, classes));
        const __m512i second = _mm512_mask_blend_epi8(~_mm512_movepi8_mask(second_producers), _mm512_permutexvar_epi8(_mm512_add_epi8(before, _mm512_set1_epi8((char)(base - 2))), carried),
                                                      _mm512_permutexvar_epi8(second_producers, classes));
        // The rules: each entry's class; its top's, by its kind's mask, its own, or CONVERT's mask by its class; the one
        // below, by its kind's mask, its own or the top's; the top the entry before (tier0_last_mismatch's); and nothing
        // under a RETURN's or a JUMP_INDIRECT's operand.
        refused |= lanes & ~tier0_scan_member(_mm512_permutexvar_epi8(kinds, own_low), _mm512_permutexvar_epi8(kinds, own_high), classes);
        __mmask64 top_fits = tier0_scan_member(tier0_lookup(tier0_scan_top_low, kinds), tier0_lookup(tier0_scan_top_high, kinds), top);
        top_fits |= _mm512_test_epi8_mask(rules, _mm512_set1_epi8(BUSTER_TIER0_RULE_TOP_SELF)) & _mm512_cmpeq_epi8_mask(top, classes);
        top_fits |= _mm512_test_epi8_mask(rules, _mm512_set1_epi8(BUSTER_TIER0_RULE_CONVERT)) &
                    tier0_scan_member(tier0_lookup(tier0_scan_convert_low, classes), tier0_lookup(tier0_scan_convert_high, classes), top);
        refused |= tops & ~top_fits;
        __mmask64 second_fits = tier0_scan_member(tier0_lookup(tier0_scan_second_low, kinds), tier0_lookup(tier0_scan_second_high, kinds), second);
        second_fits |= _mm512_test_epi8_mask(rules, _mm512_set1_epi8(BUSTER_TIER0_RULE_SECOND_SELF)) & _mm512_cmpeq_epi8_mask(second, classes);
        second_fits |= _mm512_test_epi8_mask(rules, _mm512_set1_epi8(BUSTER_TIER0_RULE_COMPARE)) & _mm512_cmpeq_epi8_mask(second, top);
        refused |= lanes & _mm512_test_epi8_mask(rules, _mm512_set1_epi8(BUSTER_TIER0_RULE_SECOND)) & ~second_fits;
        refused |= lanes & _mm512_test_epi8_mask(rules, _mm512_set1_epi8(BUSTER_TIER0_RULE_LAST)) & ~empty_returns & _mm512_cmpneq_epi8_mask(top, previous_classes);
        const __mmask64 emptied = _mm512_cmpeq_epi8_mask(floors, _mm512_set1_epi8((char)-(int32_t)base));
        refused |= lanes & _mm512_test_epi8_mask(rules, _mm512_set1_epi8((char)BUSTER_TIER0_RULE_FLOOR)) & ~emptied;
        // A local's classes by the function's table (the bit LOCAL_ADDRESS tests, 15, where its address is taken).
        const __mmask64 locals = lanes & _mm512_test_epi8_mask(sides, _mm512_set1_epi8(BUSTER_TIER0_SIDE_LOCAL | BUSTER_TIER0_SIDE_ADDRESS));
        __m512i local_classes_low = zero;
        __m512i local_classes_high = zero;
        if (few_locals)
        {
            local_classes_low = _mm512_permutex2var_epi8(local_low[0], indices, local_low[1]);
            local_classes_high = _mm512_permutex2var_epi8(local_high[0], indices, local_high[1]);
        }
        else if (locals != 0)
        {
            __m512i words[4];
            tier0_scan_gather(local_words, low_words, locals & bounded, words);
            local_classes_low = tier0_scan_bytes(words);
            for (uint32_t q = 0; q < 4; q += 1)
            {
                words[q] = _mm512_srli_epi32(words[q], 8);
            }
            local_classes_high = tier0_scan_bytes(words);
        }
        const __m512i local_bit = _mm512_mask_mov_epi8(classes, _mm512_test_epi8_mask(sides, _mm512_set1_epi8(BUSTER_TIER0_SIDE_ADDRESS)), _mm512_set1_epi8(15));
        refused |= locals & ~tier0_scan_member(local_classes_low, local_classes_high, local_bit);
        // One class for every RETURN: the first one's.
        uint8_t class_lanes[64] __attribute__((aligned(64)));
        _mm512_store_si512(class_lanes, classes);
        const uint32_t first_return = returns != 0 ? class_lanes[__builtin_ctzll(returns)] : return_class;
        return_class = return_class == BUSTER_TIER0_SCAN_UNREACHED ? first_return : return_class;
        refused |= returns & _mm512_cmpneq_epi8_mask(classes, _mm512_set1_epi8((char)return_class));
        // The lane-at-a-time checks below index the producer stack and shift by classes. Only a block whose
        // kinds, classes, bounds and depths already passed may execute them; malformed byte depths can wrap.
        valid = valid && refused == 0;
        // The lanes' depths, producers and classes for the arrivals' and the calls' checks, which take them a lane at a
        // time; and each lane's lowest place the lanes before it in the block reached (the place 0 where none did).
        uint8_t depth_lanes[64] __attribute__((aligned(64)));
        uint8_t kind_lanes[64] __attribute__((aligned(64)));
        uint8_t first_lanes[64] __attribute__((aligned(64)));
        uint8_t second_lanes[64] __attribute__((aligned(64)));
        uint8_t top_lanes[64] __attribute__((aligned(64)));
        uint8_t second_class_lanes[64] __attribute__((aligned(64)));
        uint8_t reached_lanes[64] __attribute__((aligned(64)));
        const __m512i depth = _mm512_add_epi8(before, _mm512_set1_epi8((char)base));
        __m512i reached = _mm512_mask_mov_epi8(_mm512_set1_epi8(127), lanes, floors);
        for (uint32_t step = 1; step < 64; step *= 2)
        {
            reached = _mm512_min_epi8(reached, tier0_shift_back(reached, _mm512_set1_epi8(127), step));
        }
        _mm512_storeu_si512(depths + first, depth);
        _mm512_store_si512(depth_lanes, depth);
        _mm512_store_si512(kind_lanes, kinds);
        _mm512_store_si512(first_lanes, first_producers);
        _mm512_store_si512(second_lanes, second_producers);
        _mm512_store_si512(top_lanes, top);
        _mm512_store_si512(second_class_lanes, second);
        _mm512_store_si512(reached_lanes, tier0_shift_back(reached, zero, 1));
        // The targets the block holds, as forward arrivals from earlier blocks left them: read before this block's own
        // arrivals, which store no target of the block's (a vector load of bytes just stored a byte at a time stalls).
        const __m512i expectations = _mm512_loadu_si512(expected + first);
        // Each arrival's depth (a branch's and a JUMP_VALUE's after its pop, a JUMP_VALUE's with its value) and those that
        // bring nothing to compare but depth 0, a statement's: a bit for the target (checked where the target's block
        // is, `emptied_targets`), and backward, its depth 0 now.
        const __mmask64 controls = lanes & _mm512_test_epi8_mask(sides, _mm512_set1_epi8(BUSTER_TIER0_SIDE_CONTROL)) & bounded;
        const __mmask64 jumps = _mm512_cmpeq_epi8_mask(kinds, _mm512_set1_epi8(BUSTER_TYPED_JUMP));
        const __mmask64 brought_values = _mm512_cmpeq_epi8_mask(kinds, _mm512_set1_epi8(BUSTER_TYPED_JUMP_VALUE));
        const __m512i arrivals_depth = _mm512_add_epi8(_mm512_sub_epi8(depth, _mm512_maskz_mov_epi8(~jumps, one)), _mm512_maskz_mov_epi8(brought_values, one));
        const __mmask64 empty = controls & _mm512_cmpeq_epi8_mask(arrivals_depth, zero);
        bool failed = false;
        for (uint64_t pending = empty; valid && pending != 0; pending &= pending - 1)
        {
            const uint32_t entry = first + (uint32_t)__builtin_ctzll(pending);
            const uint32_t target = (uint32_t)function->values[entry];
            emptied_targets[target >> 6] |= 1ull << (target & 63);
            failed |= (target <= entry) & (depths[target] != 0);
        }
        // The others: forward, the depth, the producer just below what they may differ in (of the top for a JUMP, which
        // pops nothing, and of the one below for a branch's and a JUMP_VALUE's) and the class of the value a JUMP_VALUE
        // brings, against the target's own where the block holds it (`near`), and otherwise against the first forward
        // arrival's, which the target's block checks; backward, the scalar validator's.
        __mmask64 near = 0;
        for (uint64_t pending = controls & ~empty; valid && pending != 0; pending &= pending - 1)
        {
            const uint32_t lane = (uint32_t)__builtin_ctzll(pending);
            const uint32_t entry = first + lane;
            const uint32_t target = (uint32_t)function->values[entry];
            const uint32_t popped = kind_lanes[lane] != BUSTER_TYPED_JUMP ? 1 : 0;
            const uint32_t brings = kind_lanes[lane] == BUSTER_TYPED_JUMP_VALUE ? 1 : 0;
            const uint32_t arrival = depth_lanes[lane] - popped + brings;
            const uint32_t below = tier0_scan_producer(first, (popped != 0 ? second_lanes : first_lanes)[lane], depth_lanes[lane], 1 + popped, stack_producers);
            const uint32_t value_class = brings != 0 ? class_lanes[lane] : 0;
            const bool forward = target > entry;
            const bool inside = forward && target < first + live;
            const uint32_t at = (target - first) & 63;
            const uint32_t at_below = tier0_scan_producer(first, (brings != 0 ? second_lanes : first_lanes)[at], depth_lanes[at], 1 + brings, stack_producers);
            failed |= inside && (depth_lanes[at] != arrival || at_below != below || (brings != 0 && top_lanes[at] != value_class));
            near |= (__mmask64)(inside ? 1 : 0) << at;
            const uint64_t brought = below | (uint64_t)brings << 32 | (uint64_t)value_class << 40;
            const bool far = forward && !inside;
            const uint32_t old = expected[target];
            const uint64_t old_brought = arrivals[target];
            const bool fresh = old == BUSTER_TIER0_SCAN_UNREACHED;
            failed |= (far && !fresh && (old != arrival || old_brought != brought)) || !forward;
            expected[target] = (uint8_t)(far && fresh ? arrival : old);
            arrivals[target] = far && fresh ? brought : old_brought;
        }
        // The targets' depths: 0 where an arrival brings nothing else; from earlier blocks' other arrivals, theirs, and
        // the same producer below what they bring and the same value a JUMP_VALUE brings; and code no path reaches before
        // a target, after a jump or a return, at depth 0, where tier0_validate_scalar starts again.
        refused |= lanes & emptied_targets[first >> 6] & _mm512_test_epi8_mask(depth, depth);
        const __mmask64 reached_before = lanes & _mm512_cmpneq_epi8_mask(expectations, _mm512_set1_epi8((char)BUSTER_TIER0_SCAN_UNREACHED));
        refused |= reached_before & _mm512_cmpneq_epi8_mask(expectations, depth);
        refused |= lanes & _mm512_test_epi8_mask(tier0_lookup(tier0_scan_sides, previous_kinds), _mm512_set1_epi8(BUSTER_TIER0_SIDE_TRANSFER)) & ~(reached_before | near) &
                   _mm512_test_epi8_mask(depth, depth);
        valid = valid && refused == 0 && !failed;
        for (uint64_t pending = reached_before & _mm512_test_epi8_mask(expectations, expectations); valid && pending != 0; pending &= pending - 1)
        {
            const uint32_t lane = (uint32_t)__builtin_ctzll(pending);
            const uint64_t brought = arrivals[first + lane];
            const uint32_t brings = (uint32_t)(brought >> 32) & 1;
            const uint32_t below = tier0_scan_producer(first, (brings != 0 ? second_lanes : first_lanes)[lane], depth_lanes[lane], 1 + brings, stack_producers);
            failed |= below != (uint32_t)brought || (brings != 0 && top_lanes[lane] != (uint8_t)(brought >> 40));
        }
        // Each call: its class by its own rules, and its operands' (an indirect call's callee, U64, then its arguments, the
        // first pushed first) on the stack: for two or fewer, the top's and the one below's; for more, from the carried
        // stack below the lowest place the lanes before it reached and above it the values those lanes left, in order.
        const __mmask64 few_operands = calls & bounded & _mm512_cmple_epu8_mask(operands, _mm512_set1_epi8(2));
        for (uint64_t pending = few_operands; valid && pending != 0; pending &= pending - 1)
        {
            const uint32_t lane = (uint32_t)__builtin_ctzll(pending);
            const uint32_t call = (uint32_t)function->values[first + lane];
            uint32_t count_operands = 0;
            const __m512i wanted = tier0_scan_call_operands(function, call, &count_operands);
            const __m512i stack = _mm512_mask_mov_epi8(_mm512_set1_epi8((char)second_class_lanes[lane]), (__mmask64)1 << ((count_operands - 1) & 63),
                                                       _mm512_set1_epi8((char)top_lanes[lane]));
            failed |= ((tier0_scan_call(function, call) >> class_lanes[lane]) & 1) == 0 || _mm512_mask_cmpneq_epi8_mask(_bzhi_u64(~0ull, count_operands), stack, wanted) != 0;
        }
        for (uint64_t pending = calls & bounded & ~few_operands; valid && pending != 0; pending &= pending - 1)
        {
            const uint32_t lane = (uint32_t)__builtin_ctzll(pending);
            const uint32_t call = (uint32_t)function->values[first + lane];
            uint32_t count_operands = 0;
            const __m512i wanted = tier0_scan_call_operands(function, call, &count_operands);
            const int32_t at = (int32_t)depth_lanes[lane];
            // Below the stack's bottom (an operand it doesn't have), the call is refused already: its places from 0.
            const int32_t bottom = at >= (int32_t)count_operands ? at - (int32_t)count_operands : 0;
            const int32_t lowest_place = (int32_t)base + (int8_t)reached_lanes[lane];
            const int32_t below = (lowest_place < at ? lowest_place : at) - bottom;
            const uint32_t kept = below > 0 ? (uint32_t)below : 0;
            const __m512i held_below = _mm512_maskz_permutexvar_epi8(_bzhi_u64(~0ull, kept), _mm512_add_epi8(lane_indices, _mm512_set1_epi8((char)bottom)), carried);
            const __mmask64 earlier = _bzhi_u64(~0ull, lane);
            const __m512i held = _mm512_maskz_compress_epi8(pushes & earlier & _mm512_cmplt_epi8_mask(floors, tier0_scan_later_floors(floors, earlier)), classes);
            const __m512i inside = _mm512_permutexvar_epi8(_mm512_add_epi8(lane_indices, _mm512_set1_epi8((char)(bottom - lowest_place))), held);
            const __m512i stack = _mm512_mask_blend_epi8(_bzhi_u64(~0ull, kept), inside, held_below);
            failed |= ((tier0_scan_call(function, call) >> class_lanes[lane]) & 1) == 0 || _mm512_mask_cmpneq_epi8_mask(_bzhi_u64(~0ull, count_operands), stack, wanted) != 0;
        }
        // A global entry's symbol's access, where the function has the column.
        if (function->symbol_access != 0)
        {
            for (uint64_t pending = lanes & _mm512_test_epi8_mask(sides, _mm512_set1_epi8(BUSTER_TIER0_SIDE_GLOBAL)) & bounded; pending != 0; pending &= pending - 1)
            {
                failed |= function->symbol_access[(uint32_t)function->values[first + (uint32_t)__builtin_ctzll(pending)]] >= BUSTER_TIER0_ACCESSES;
            }
        }
        valid = valid && refused == 0 && !failed;
        if (valid)
        {
            // The values the block leaves on the stack, onto the carried stack: each lane's that pushes and whose floor is
            // below every later lane's, in order from the lowest place the block reached.
            uint8_t lowest_lanes[64] __attribute__((aligned(64)));
            _mm512_store_si512(lowest_lanes, reached);
            const uint32_t place = base + (uint32_t)(int32_t)(int8_t)lowest_lanes[live - 1];
            const __mmask64 left = pushes & _mm512_cmplt_epi8_mask(floors, tier0_scan_later_floors(floors, lanes));
            const __m512i left_classes = _mm512_maskz_compress_epi8(left, classes);
            carried = _mm512_mask_blend_epi8(~_bzhi_u64(~0ull, place), carried, _mm512_permutexvar_epi8(_mm512_sub_epi8(lane_indices, _mm512_set1_epi8((char)place)), left_classes));
            uint32_t offset = 64 + place;
            for (uint32_t q = 0; q < 4; q += 1)
            {
                const __mmask16 quarter = (__mmask16)(left >> (16 * q));
                const __m512i producers = _mm512_add_epi32(_mm512_set1_epi32((int)(first + 16 * q)), _mm512_cvtepu8_epi32(tier0_scan_quarter(lane_indices, 0)));
                _mm512_storeu_si512(stack_producers + offset, _mm512_maskz_compress_epi32(quarter, producers));
                offset += (uint32_t)__builtin_popcount(quarter);
            }
            base = (uint32_t)((int32_t)base + (int8_t)_mm_extract_epi8(_mm512_extracti32x4_epi32(after, 3), 15));
            valid = base <= BUSTER_TIER0_SCAN_DEPTH;
            previous_kind = kind_lanes[live - 1];
            previous_class = class_lanes[live - 1];
        }
    }
    // The end, where no value is left and a function returning an aggregate never arrives (its RETURNs leave the address
    // the epilogue reads).
    if (valid)
    {
        const bool falls = count == 0 || (tier0_scan_sides[function->kinds[count - 1]] & BUSTER_TIER0_SIDE_TRANSFER) == 0;
        const bool jumped = expected[count] != BUSTER_TIER0_SCAN_UNREACHED || ((emptied_targets[count >> 6] >> (count & 63)) & 1) != 0;
        valid = base == 0 && (expected[count] == BUSTER_TIER0_SCAN_UNREACHED || expected[count] == 0) && !(aggregate && (falls || jumped));
    }
    scratch_end(scratch);
    return valid;
}

#define BUSTER_TIER0_VALIDATE_SMALL_COUNT 16

static Tier0Check tier0_validate(const Tier0Function* function)
{
    Tier0Check check;
    memset(&check, 0, sizeof(check));
    // Locked 9700X size controls favor scalar validation through 16 entries. Keep empty streams on their cheap
    // vector setup path and do not infer a small total workload from entry count when metadata tables are large.
    const bool small = function->count != 0 && function->count <= BUSTER_TIER0_VALIDATE_SMALL_COUNT &&
                       function->local_count <= BUSTER_TIER0_VALIDATE_SMALL_COUNT && function->call_count <= BUSTER_TIER0_VALIDATE_SMALL_COUNT &&
                       function->argument_count <= BUSTER_TIER0_VALIDATE_SMALL_COUNT;
    if (small || !tier0_validate_accepts(function))
    {
        check = tier0_validate_scalar(function);
    }
    return check;
}
