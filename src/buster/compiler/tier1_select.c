// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Tier 1 instruction selection (#141). Planning precedes allocation: folding an address, shift or load extends its
// operands' live ranges to the consuming instruction. The plan publishes those effective uses directly, so allocation
// never guesses them from the unselected IR. Emission consumes that plan and the resulting physical-register column.
// Records, node boundaries and branch columns then go directly to branch sizing and the x86 encoder.
//
// This is a bounded integer slice. Unsupported operations, allocation fragments and target forms are explicit errors;
// semantic failures never select Tier 0 silently. docs/tier1-selection.md records the supported rules and boundaries.
// Include after ir.c, lane.c, x86_64_encoder.c, x86_semantics.c, x86_target.c, x86_target_forms.c and x86_zen5.c.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "host_isa.h"

#define BUSTER_TIER1_SELECT_NONE UINT32_MAX
#define BUSTER_TIER1_SELECT_NO_REGISTER UINT8_MAX
#define BUSTER_TIER1_SELECT_LANES 16
#define BUSTER_TIER1_SELECT_SCRATCH X86_REGISTER_R11
#define BUSTER_TIER1_SELECT_RECORDS_PER_NODE 6
// The most records a rule that takes x86's fixed registers writes (tier1_select_fixed), before its consumer's saves: a
// remainder by a constant's reciprocal whose multiplier takes the add sequence, whose divisor is past an imm32 and whose
// dividend is in rax or rdx has 13.
#define BUSTER_TIER1_SELECT_FIXED_RECORDS 13
#define BUSTER_TIER1_SELECT_CALL_ARGUMENTS 6
// The most integer or pointer arguments a call passes, and parameters a function takes (#141): those past the registers
// in the stack's 8-byte slots, the outgoing area's at a call and the incoming area's at the entry. Their values needn't
// be in registers there: the CFG consumer stores each argument from wherever it is and loads each parameter into its
// first fragment's place (tier1_emit_cfg.c). 64 since #1847, from 16: _mm512_set_epi8's parameters (and the scalar
// host's emulation's arguments) and printf's calls of the compiler's own tests, 16 of main_unit_backend.c's 25 refusals.
#define BUSTER_TIER1_SELECT_MAX_ARGUMENTS 64

typedef enum Tier1SelectError
{
    TIER1_SELECT_OK,
    TIER1_SELECT_ERROR_INVALID,
    TIER1_SELECT_ERROR_UNSUPPORTED,
    TIER1_SELECT_ERROR_ALLOCATION,
    TIER1_SELECT_ERROR_TARGET,
    TIER1_SELECT_ERROR_MEMORY,
    TIER1_SELECT_ERROR_LIMIT,
} Tier1SelectError;

typedef enum Tier1SelectRule
{
    TIER1_RULE_NONE,
    TIER1_RULE_LEAF,
    TIER1_RULE_CONSTANT,
    TIER1_RULE_COPY,
    TIER1_RULE_UNARY,
    TIER1_RULE_REGISTER,
    TIER1_RULE_IMMEDIATE,
    TIER1_RULE_LEA_ADD,
    TIER1_RULE_LEA_SHIFT,
    TIER1_RULE_MULTIPLY_LEA,
    TIER1_RULE_MULTIPLY_SHIFT,
    TIER1_RULE_ADDRESS,
    TIER1_RULE_LOAD,
    TIER1_RULE_LOAD_ADDRESS,
    TIER1_RULE_LOAD_OPERAND,
    TIER1_RULE_STORE,
    TIER1_RULE_STORE_ADDRESS,
    TIER1_RULE_COMPARE,
    TIER1_RULE_COMPARE_IMMEDIATE,
    TIER1_RULE_BRANCH,
    TIER1_RULE_BRANCH_COMPARE,
    TIER1_RULE_SELECT,
    TIER1_RULE_JUMP,
    TIER1_RULE_RETURN,
    TIER1_RULE_CALL,
    TIER1_RULE_TRUNCATE,
    TIER1_RULE_ZERO_EXTEND,
    TIER1_RULE_SIGN_EXTEND,
    TIER1_RULE_UNREACHABLE,
    // A phi: no records, and no effective uses, since its operands are uses on its block's incoming edges, which the
    // CFG consumer's edge moves read (tier1_emit_cfg.c).
    TIER1_RULE_PHI,
    // A frame object's address (IR_STACK_ADDR of slot `immediate`): a lea from the stack pointer, whose base and
    // displacement the CFG consumer sets from the frame's layout of the object (tier1_emit_cfg.c).
    TIER1_RULE_STACK_ADDRESS,
    // A symbol's address (IR_SYMBOL_ADDR of symbol `immediate`): a lea from rip, marked for its reference, which the CFG
    // consumer gives it, or a load of the symbol's GOT slot where its access says so.
    TIER1_RULE_SYMBOL_ADDRESS,
    // A division or remainder of values[0] by values[1] (`immediate`'s bit 0 signed, bit 1 the remainder), and a shift of
    // values[0] by the count in values[1]: x86's fixed registers (rax and rdx, or cl), which the CFG consumer's sequence
    // takes, saving another value live past it there (tier1_emit_cfg_fixed).
    TIER1_RULE_DIVIDE,
    TIER1_RULE_SHIFT,
    // A signed division or remainder of values[0] by 2^`scale` (`immediate`'s bits as TIER1_RULE_DIVIDE's), rounded
    // toward zero as C's: the dividend plus 2^scale - 1 where it is negative, a bias the scratch register computes from
    // its sign, shifted right, or for the remainder masked and the bias taken back. An unsigned one is a shift or a mask
    // (TIER1_RULE_IMMEDIATE).
    TIER1_RULE_DIVIDE_POWER,
    // A division or remainder of values[0] by the constant `immediate` (the width's bits; tier1_select_reciprocal_divisor
    // says which), `scale`'s bits as TIER1_RULE_DIVIDE's `immediate`'s: a multiplication by the divisor's reciprocal and
    // shifts (Tier1SelectReciprocal), mul's or imul's rdx:rax, which the consumers save around it as TIER1_RULE_DIVIDE's.
    TIER1_RULE_DIVIDE_RECIPROCAL,
    // AVX-512's masks (#179), m8 to m64, in the opmask registers that the CFG consumer's allocation gives them (k1 to
    // k7: a node's register is its tier1_cfg.c location, BUSTER_TIER1_CFG_SPILL's class 2, whose low three bits are
    // the k register's number). `width` is the mask's bytes, 1 to 8: kmovb, kmovw, kmovd, kmovq and the opmask
    // instructions of that width, which leave the bits past it zero, as the IR's masks have them. A constant (zero by
    // kxor, every bit by kxnor, else its bits through the scratch register); a copy (kmov); and, or and xor of values[0]
    // and values[1]; not of values[0]; a load and a store, as the integer ones, the store's mask in values[2], each one
    // kmov, which must stay its node's last record: the CFG consumer writes a frame object's base and offset into that
    // record (`stack_object`, tier1_emit_cfg_records). Only the CFG consumer takes them.
    TIER1_RULE_MASK_CONSTANT,
    TIER1_RULE_MASK_COPY,
    TIER1_RULE_MASK_LOGIC,
    TIER1_RULE_MASK_NOT,
    // A bitcast of an integer to a mask of its width, kmov k, r32 (r64 for a quadword), which kmovb, kmovw and kmovd read
    // the low bits of alone; and of a mask to an integer of its width, kmov r32 (r64), k, which zero-extends past the
    // mask's bits: a narrow integer's canonical 32-bit storage (tier1_select_canonical).
    TIER1_RULE_MASK_FROM_INTEGER,
    TIER1_RULE_MASK_TO_INTEGER,
    TIER1_RULE_MASK_LOAD,
    TIER1_RULE_MASK_LOAD_ADDRESS,
    TIER1_RULE_MASK_STORE,
    TIER1_RULE_MASK_STORE_ADDRESS,
    // An indirect jump (IR_BR_INDIRECT, #789) to the address in values[0]'s register: a computed goto's from its
    // function's dispatch block, or a jump table's, its entry's offset added to the table's address. The CFG consumer
    // places its edges' moves after it, at its labels' blocks or in stubs where it defines their labels
    // (tier1_emit_cfg.c).
    TIER1_RULE_JUMP_INDIRECT,
    // A frame slot's lifetime marker (IR_LIFETIME, #177): no records. The CFG consumer colors the frame's objects by the
    // markers (tier1_emit_cfg_lifetimes).
    TIER1_RULE_LIFETIME,
    // Floating values (#141), F32 and F64, in the vector class's registers that the CFG consumer's allocation gives them
    // (xmm0 to xmm14, xmm15 the class's scratch: a node's register is its tier1_cfg.c location, class 1's, whose low
    // four bits are the xmm register), by SSE2's scalar forms, the baseline target's. `width` is the value's bytes, 4 or
    // 8: the ss or the sd form (tier1_select_float_forms). A constant: zero by xorps, any other's bits through the scratch
    // general register (mov, then movd or movq). A copy (movaps). An add, subtract, multiply or divide of values[0] by
    // values[1], `operation` its index among them (IR_FADD's order: `scale` is a folded address's), the right operand
    // from memory where it folds a load (FLOAT_LOAD_OPERAND, values[1] and values[2] the address's base and index). A
    // negation, xorps of the sign bit, which the scratch general register moves into the vector class's scratch
    // register. A load and a store as the integer ones, the store's value in values[2], each one movss or movsd, which stays its node's last record (the CFG consumer patches a frame object's
    // base and offset into it). A comparison's truth value, `condition` its IR predicate (EQ to GE): clear the general
    // result, ucomis and setcc, and for == and != the parity flag's setcc and their and or or, since unordered sets ZF. A branch
    // on one, < and <= with its operands swapped, `condition` the unsigned condition (UGT, UGE, or their negations ULE and
    // ULT) whose jump reads ucomis's flags: ja and jae are false for unordered operands, and jbe and jb, !(a > b) and
    // !(a >= b), true. The conversions (FLOAT_CONVERT, `scale` its kind: BUSTER_TIER1_FLOAT_FROM_SIGNED and the others,
    // `width` and `source_width` the result's and the operand's bytes, an integer's by its memory width). A return of
    // values[0] in xmm0. Only the CFG consumer takes them.
    TIER1_RULE_FLOAT_CONSTANT,
    TIER1_RULE_FLOAT_COPY,
    TIER1_RULE_FLOAT_BINARY,
    TIER1_RULE_FLOAT_LOAD_OPERAND,
    TIER1_RULE_FLOAT_NEGATE,
    TIER1_RULE_FLOAT_LOAD,
    TIER1_RULE_FLOAT_LOAD_ADDRESS,
    TIER1_RULE_FLOAT_STORE,
    TIER1_RULE_FLOAT_STORE_ADDRESS,
    TIER1_RULE_FLOAT_COMPARE,
    TIER1_RULE_FLOAT_BRANCH_COMPARE,
    TIER1_RULE_FLOAT_CONVERT,
    TIER1_RULE_FLOAT_CONVERT_WIDE,
    TIER1_RULE_FLOAT_RETURN,
    // The dynamic stack (#141), whose functions are the CFG consumer's with a frame pointer: an allocation rounds its
    // size up to the frame's alignment, takes it from rsp and gives rsp as its address (lea, and, sub and mov from its
    // size's register, or with ALLOCA_IMMEDIATE a sub of the rounded constant), so that rsp keeps that alignment at
    // every call after it. Its pattern's and holes' `scale` is the alignment's log2: 4 from selection, and a realigned
    // frame's from the CFG consumer. A save copies rsp and a restore sets it.
    TIER1_RULE_ALLOCA,
    TIER1_RULE_ALLOCA_IMMEDIATE,
    TIER1_RULE_STACK_SAVE,
    TIER1_RULE_STACK_RESTORE,
    // The function's frame address (#1847, IR_STACK_SAVE of BUSTER_IR_STACK_SAVE_FRAME): a copy of its frame pointer, rbp,
    // which its frame keeps (tier1_emit_cfg_frame_flags), into the destination.
    TIER1_RULE_FRAME_ADDRESS,
    // A memory fence (IR_FENCE, #1059) of the order and scope `immediate`: mfence for a thread fence of seq_cst, the one
    // x86-64 needs an instruction for, and no records for the others. No value; no pass moved an access across it.
    TIER1_RULE_FENCE,
    // The high 64 bits of values[0] times values[1], unsigned (IR_INTRINSIC's mulhu, #141's 128-bit integers): one
    // operand in rax, mul of the other, the result in rdx, which the consumers save around it as
    // TIER1_RULE_DIVIDE_RECIPROCAL's.
    TIER1_RULE_MULTIPLY_HIGH,
    // An x86 builtin's bit manipulation of values[0] by values[1] (IR_INTRINSIC's pdep, pext, bzhi and bextr, #141), the
    // intrinsic `immediate` (BUSTER_IR_INTRINSIC_*): its one VEX instruction of `width` 4 or 8, whose three registers
    // are its own, the destination's any of them (tier1_select_intrinsic); the bit operations' one instruction (#1374);
    // and the signed overflows' operation and seto (#1385, tier1_select_overflow), the destination any register.
    TIER1_RULE_INTRINSIC,
    // Long double (F80, #141), whose values the CFG consumer keeps in frame objects of their own, one per value, 16 bytes
    // each, and computes on the x87 stack between them, leaving it empty after each node (tier1_emit_cfg_x87): its kind
    // in `scale` (BUSTER_TIER1_X87_*), and in `values` the node's operands that aren't long doubles alone (a load's or a
    // store's address, a conversion's integer or floating operand), its long double operands read from the IR's
    // operands, which no register holds. Only the CFG consumer takes it.
    TIER1_RULE_X87,
    // The IR's vectors (#141), v4i32, v8i32 and v16i32, in the vector class's registers that the CFG consumer's
    // allocation gives them as it gives the floating values (xmm0 to xmm14, a node's register its location's low four
    // bits, xmm15 the class's scratch), by AVX-512's EVEX forms of `width` 16, 32 or 64 bytes, the xmm, ymm or zmm one
    // (tier1_select_vector_forms), or at a target without AVX512VL by AVX's and AVX2's VEX forms of the xmm or ymm one
    // (#1281, Tier1SelectHoles.vex): its kind in `scale` (BUSTER_TIER1_VECTOR_*). A vector's loads and stores are
    // TIER1_RULE_FLOAT_LOAD's and _STORE's, and their _ADDRESS folds, at those widths (vmovdqu32, or vmovdqu), and its
    // phis TIER1_RULE_PHI's. Only the CFG consumer takes it.
    TIER1_RULE_VECTOR,
    // An atomic read/modify/write (IR_ATOMIC_RMW, #1059) of the object at the address in values[0], of values[1], by
    // the operation `immediate` (BUSTER_IR_RMW_OPERATION's: an exchange, an addition or a compare-exchange): the operand
    // copied to the scratch register, xchg or lock xadd of it with the object, a full barrier every order takes, and its
    // old value zero-extended into the destination, as a load's is; a compare-exchange's, with values[1] the expected
    // value and values[2] the desired one, rax saved in the scratch register (and an operand in rax read from there),
    // the expected value moved into rax, lock cmpxchg of the desired one with the object, the old value zero-extended
    // from rax into the destination, and rax restored where the destination isn't it. Never folded into a consumer, and
    // a barrier no load folds past.
    TIER1_RULE_ATOMIC_RMW,
    // A machine row (#141), an IR row call's (IrCall.row): the typed stream's X86 row, x86 records whose registers are Tier
    // 0's roles, which the CFG consumer places (tier1_emit_cfg_row) as Tier 0 does (Tier0Placement) but for its vector
    // roles, placed one at a time (#1283): a vector result in its destination and a vector operand in its own register
    // where nothing else there is read after they are written, else in zmm16 and on, which no value takes, or in a run of
    // free registers below 16 for a row of VEX or legacy forms and at a target without AVX512VL (#1281), moved there and
    // out of there; its integer, mask and floating operands in their own registers where its records don't write them,
    // else moved into free registers of their classes (below 16 for a floating role a VEX or legacy form names, and at a
    // target without AVX512VL); its scratch roles and its integer or mask result in free registers, the result in its
    // destination where no operand is. `immediate` is the row's index in
    // IrFunction.rows; its operands are the call's arguments, the node's effective uses, each in a register
    // (tier1_select_row_supported). Only the CFG consumer takes it.
    TIER1_RULE_ROW,
    // An asm body (#1839), an IR body call's (IrCall.body): an asm statement's records, which the CFG consumer writes in
    // the node's place (tier1_emit_cfg_asm) with no call, its operand block's base (BUSTER_TIER0_ASM_BLOCK) the frame's base
    // register and its displacement the block's frame object's past it, the block being the IR_STACK_ADDR the call's one
    // argument is, which no register holds: the node has no operand and no result. Its own branches are the function's,
    // to its records or its end. The general registers it touches are its fixed registers (tier1_select_node_fixed): the
    // values that live past it there are saved in their call-save rows around it, a callee-saved register's too, as a
    // fixed rule's are, and so are the floating values in the xmm registers it touches, in their rows, as a call's are;
    // the prologue saves the callee-saved ones it touches. `immediate` is the body's index in IrFunction.asm_bodies (tier1_select_asm_supported).
    // Only the CFG consumer takes it.
    TIER1_RULE_ASM,
    TIER1_RULE_COUNT,
} Tier1SelectRule;

// TIER1_RULE_VECTOR's kinds, its pattern's `scale` (#141):
// - ZERO (a constant of zeros, or undef): vpxord of the destination with itself;
// - ONES (every bit): vpternlogd of the destination with itself, 0xff;
// - SPLAT (every lane `immediate`): the lane into the scratch general register and vpbroadcastd from it;
// - CONSTANT (any other, its index in `immediate`): its words as immediates into the red zone below rsp and a load, which
//   the CFG consumer writes (tier1_emit_cfg_vector_constant), having the constant's payload;
// - COPY of values[0], vmovdqa64 at the width, none where the allocation gave both one register;
// - BINARY of values[0] and values[1], `operation` its index among add, sub, mul, and, or and xor (vpaddd, vpsubd,
//   vpmulld, vpandq, vporq, vpxord: unmasked, a quadword's and and or are the doublewords' bits): three operands, so the
//   destination may be either;
// - NOT of values[0]: vpternlogd with 0x55, the complement of its third operand;
// - COMPARE of values[0] and values[1] into a mask, `condition` its IR predicate: vpcmpd, or vpcmpud for an unsigned
//   one, into the mask's opmask register (the destination's low three bits, class 2's);
// - SELECT of values[1] where the mask values[0] is set and of values[2] where it isn't: vpblendmd under the mask;
// - SCALAR_SPLAT of values[0], whose scalar IR type is in `source_width`: repeat its raw bits without floating
//   arithmetic, by a broadcast or the baseline move/unpack/shuffle sequence.
// A vector's encoding level (#141): EVEX's at a target with AVX512VL, VEX's at one with AVX but without it (#1281), and
// legacy SSE's at one without AVX (the x86-64 baseline and x86-64-v2), whose vectors are 16 bytes alone. The helpers
// take it where they took `vex` (BUSTER_TIER1_ENCODING_VEX is `vex`'s true), from Tier1EmitPrepared's vex and sse.
#define BUSTER_TIER1_ENCODING_EVEX 0u
#define BUSTER_TIER1_ENCODING_VEX 1u
#define BUSTER_TIER1_ENCODING_SSE 2u

// BINARY's operations, its pattern's `operation`, of the lanes its `source_width` holds (#141's element types, the IR's
// flags: an IrSimdLane): the integer arithmetic and the bitwise operations, each lane's shift by the other operand's
// same lane, and the floating arithmetic (tier1_select_lane_form).
#define BUSTER_TIER1_VECTOR_ADD 0
#define BUSTER_TIER1_VECTOR_SUB 1
#define BUSTER_TIER1_VECTOR_MUL 2
#define BUSTER_TIER1_VECTOR_AND 3
#define BUSTER_TIER1_VECTOR_OR 4
#define BUSTER_TIER1_VECTOR_XOR 5
#define BUSTER_TIER1_VECTOR_SHL 6
#define BUSTER_TIER1_VECTOR_LSHR 7
#define BUSTER_TIER1_VECTOR_ASHR 8
#define BUSTER_TIER1_VECTOR_FADD 9
#define BUSTER_TIER1_VECTOR_FSUB 10
#define BUSTER_TIER1_VECTOR_FMUL 11
#define BUSTER_TIER1_VECTOR_FDIV 12
#define BUSTER_TIER1_VECTOR_OPERATIONS 13
#define BUSTER_TIER1_VECTOR_ZERO 0
#define BUSTER_TIER1_VECTOR_ONES 1
#define BUSTER_TIER1_VECTOR_SPLAT 2
#define BUSTER_TIER1_VECTOR_CONSTANT 3
#define BUSTER_TIER1_VECTOR_COPY 4
#define BUSTER_TIER1_VECTOR_BINARY 5
#define BUSTER_TIER1_VECTOR_NOT 6
#define BUSTER_TIER1_VECTOR_COMPARE 7
#define BUSTER_TIER1_VECTOR_SELECT 8
#define BUSTER_TIER1_VECTOR_SCALAR_SPLAT 9
// A width change (#1840): `operation` its kind past BUSTER_IR_INTRINSIC_EXTRACT (0 extract, 1 widen, 2 insert),
// `source_width` the other vector's bytes (an extract's or a widen's operand's, an insert's inserted values[1]'s),
// `immediate` its part (tier1_select_width_change).
#define BUSTER_TIER1_VECTOR_WIDTH_CHANGE 10
// A permute (#1840): `operation` its kind past BUSTER_IR_INTRINSIC_PERMUTE (0 permute, 1 shuffle, 2 align, 3
// shuffle_lanes), `immediate` the IR's flags (tier1_select_permute_form).
#define BUSTER_TIER1_VECTOR_PERMUTE 11
// A conversion (#1840): `operation` its kind past BUSTER_IR_INTRINSIC_EXTEND (0 extend, 1 truncate), `immediate` the
// IR's flags, `source_width` the source's bytes (tier1_select_convert_form).
#define BUSTER_TIER1_VECTOR_CONVERT 12
// A shift by a scalar count (#1840): `operation` the IR's flags (its lanes and shift), and either the count in
// values[1], a general register, which vmovd moves into the vector class's scratch register for the shift's form by an
// xmm count, or, where the count is a constant (`condition` 1), the form by an immediate, the count in `immediate`
// (tier1_select_vector_shift).
#define BUSTER_TIER1_VECTOR_SHIFT 13
// A mask to a vector (#1840): the select of all ones where the mask values[0] is set and zeros elsewhere, at the lanes
// in `source_width` (vpmovm2b to vpmovm2q), and a vector's sign bits as a mask: the comparison of values[0]'s signed
// lanes less than zero (vpmovb2m to vpmovq2m) (tier1_select_mask_vector).
#define BUSTER_TIER1_VECTOR_MASK_EXPAND 14
#define BUSTER_TIER1_VECTOR_SIGN_MASK 15
// A lane operation (#1840): `operation` the IR's flags (its lanes and operation), values[0] and values[1] its operands,
// the first in vvvv (ir_simd_lane_op_forms: vpminub to vpmaxsq, vpaddusb to vpsubsw).
#define BUSTER_TIER1_VECTOR_LANE_OP 16
// A permute of two tables (#1840): `immediate` its lanes' bytes' log2, values[0] the first table, values[1] the indices
// and values[2] the second table: vpermt2b to vpermt2q into the first table's register, which a copy fills first where the
// destination isn't it, the scratch register keeping an operand the copy would overwrite (tier1_select_permute2).
#define BUSTER_TIER1_VECTOR_PERMUTE2 17
// A bitwise function of three vectors (#1840): `immediate` its truth table, values[0] to values[2] its operands:
// vpternlogd with the operand whose register the destination holds as its first, the table's bits permuted to match, or
// the first moved into the destination first (tier1_select_ternlog).
#define BUSTER_TIER1_VECTOR_TERNLOG 18
// A compress or an expand (#1840): `operation` 0 or 1, `immediate` the lanes' bytes' log2, values[0] the source,
// values[1] the mask and values[2] the passthrough: vpcompressb to vpexpandq merged under the mask into the destination,
// which takes the passthrough first (tier1_select_compress).
#define BUSTER_TIER1_VECTOR_COMPRESS 19
// An AES round (#1840): `immediate` the round, values[0] the state and values[1] the round key: vaesenc or vaesenclast
// (ir_simd_aes_forms; the VEX form where the target has no VL), the state in vvvv and the key in rm.
#define BUSTER_TIER1_VECTOR_AES 20
// One lane of a vector and its lanes' sign bits (#1840, tier1_select_lane): `immediate` the IR's flags; a lane's
// extraction of values[0] into the general destination, by vmovd or vmovq for lane 0 of 4 or 8 bytes and vpextrb to
// vpextrq otherwise, after vextracti32x4 into the vector class's scratch register for a lane past the low 16 bytes; a
// lane's insertion of the general values[1] into values[0]'s copy, by vpinsrb to vpinsrq in place in a 16-byte vector's
// low lane and through the scratch register and vinserti32x4 otherwise; and vpmovmskb, vmovmskps or vmovmskpd of
// values[0] into the general destination.
#define BUSTER_TIER1_VECTOR_LANE_EXTRACT 21
#define BUSTER_TIER1_VECTOR_LANE_INSERT 22
#define BUSTER_TIER1_VECTOR_MOVEMASK 23
// A floating operation at its own rounding of values[0] and values[1] (#1840): `immediate` the IR's flags, one record of
// ir_simd_fround_forms' with the rounding in its decorations.
#define BUSTER_TIER1_VECTOR_FROUND 24

// TIER1_RULE_X87's kinds, its pattern's `scale` (#141):
// - CONSTANT, its constant's index in `immediate`, and UNDEF, which stores nothing;
// - LOAD and STORE of the 10 bytes through the address in values[0], the stored value the IR's b;
// - COPY; BINARY, `operation` its index among IR_FADD to IR_FDIV; NEGATE;
// - COMPARE, `condition` its IR predicate, the truth value into the destination as a floating compare's;
// - FROM_FLOAT and TO_FLOAT, a float's or a double's (`source_width` or `width` 4 or 8) in an xmm register;
// - FROM_INT and TO_INT, an integer's (`source_width` or `width` its memory width), signed where `operation` is 1.
#define BUSTER_TIER1_X87_CONSTANT 0
#define BUSTER_TIER1_X87_UNDEF 1
#define BUSTER_TIER1_X87_LOAD 2
#define BUSTER_TIER1_X87_STORE 3
#define BUSTER_TIER1_X87_COPY 4
#define BUSTER_TIER1_X87_BINARY 5
#define BUSTER_TIER1_X87_NEGATE 6
#define BUSTER_TIER1_X87_COMPARE 7
#define BUSTER_TIER1_X87_FROM_FLOAT 8
#define BUSTER_TIER1_X87_TO_FLOAT 9
#define BUSTER_TIER1_X87_FROM_INT 10
#define BUSTER_TIER1_X87_TO_INT 11

// TIER1_RULE_FLOAT_CONVERT's kinds, its pattern's `scale` (#141):
// - FROM_SIGNED, an integer's sitofp: cvtsi2ss or cvtsi2sd of its 32 or 64 bits, an I8's or an I16's sign-extended into
//   the scratch register first (its canonical storage is zero-extended);
// - FROM_UNSIGNED, uitofp of an integer narrower than 64 bits: its zero extension's, which the 32-bit form converts
//   exactly below 2^16 and the 64-bit form of the scratch register's copy for an I32;
// - TO_SIGNED and TO_UNSIGNED, fptosi to 8 to 64 bits and fptoui to 8 to 32: cvttss2si or cvttsd2si, a narrow result
//   zero-extended into its canonical storage, and an unsigned 32-bit one converted to 64 bits, whose low 32 are its;
// - RESIZE, fext and ftrunc: cvtss2sd and cvtsd2ss;
// - FROM_BITS and TO_BITS, a bitcast between an F32 and an I32 or an F64 and an I64: movd or movq.
// Each conversion to a floating value zeroes its destination first (xorps), but a resize: cvtsi2s* writes its low
// element alone, which would otherwise wait for the register's last writer.
// TIER1_RULE_FLOAT_CONVERT_WIDE is an unsigned I64's (#141), FROM_UNSIGNED or TO_UNSIGNED by its `scale`, whose values
// past INT64_MAX no single conversion takes: a sequence longer than a node's room, through the fixed-register path's
// (tier1_select_fixed), which names the scratch register alone, holding no value (tier1_select_fixed_registers).
#define BUSTER_TIER1_FLOAT_FROM_SIGNED 0
#define BUSTER_TIER1_FLOAT_FROM_UNSIGNED 1
#define BUSTER_TIER1_FLOAT_TO_SIGNED 2
#define BUSTER_TIER1_FLOAT_TO_UNSIGNED 3
#define BUSTER_TIER1_FLOAT_RESIZE 4
#define BUSTER_TIER1_FLOAT_FROM_BITS 5
#define BUSTER_TIER1_FLOAT_TO_BITS 6
// The vector class's scratch register, which holds no value: a non-commutative operation's right operand where the
// allocation gave it the destination.
#define BUSTER_TIER1_SELECT_FLOAT_SCRATCH 15

// Whether a rule is a mask's (#179), which the uniform-register consumer and the straight-line one don't take.
static bool tier1_select_mask_rule(uint32_t rule)
{
    return rule >= TIER1_RULE_MASK_CONSTANT && rule <= TIER1_RULE_MASK_STORE_ADDRESS;
}

// Whether a rule is a floating value's (#141), which only the CFG consumer takes, its vector class's.
static bool tier1_select_float_rule(uint32_t rule)
{
    return rule >= TIER1_RULE_FLOAT_CONSTANT && rule <= TIER1_RULE_FLOAT_RETURN;
}

// Register-only leaf bodies use caller-saved registers. R11 is reserved for a destructive instruction's second
// operand when allocation assigned it the destination; it cannot hold any IR value, including a dead definition.
static bool tier1_select_register_allowed(uint8_t reg)
{
    const uint32_t allowed = (1u << X86_REGISTER_RAX) | (1u << X86_REGISTER_RCX) | (1u << X86_REGISTER_RDX) | (1u << X86_REGISTER_RSI) | (1u << X86_REGISTER_RDI) | (1u << X86_REGISTER_R8) | (1u << X86_REGISTER_R9) | (1u << X86_REGISTER_R10);
    return reg < 16 && (allowed & (1u << reg)) != 0;
}

static X86Instruction tier1_select_register_record(uint16_t form, uint8_t destination, uint8_t source)
{
    const X86Instruction result =
    {
        .reg = destination,
        .rm = source,
        .form = form,
    };
    return result;
}

static X86Instruction tier1_select_move(uint32_t width, uint8_t destination, uint8_t source)
{
    return tier1_select_register_record(width == 8 ? X86_FORM_MOV_R64_RM64 : X86_FORM_MOV_R32_RM32, destination, source);
}

// A node's records (tier1_select_expand's). At most one of them has an immediate whose form the function's batch chooses
// (tier1_select_requests_flush, #141): the record at `request` when `requested`, empty until the batch, with the
// request's destination, row, value and context.
typedef struct Tier1SelectRecords Tier1SelectRecords;
struct Tier1SelectRecords
{
    X86Instruction records[BUSTER_TIER1_SELECT_RECORDS_PER_NODE];
    int64_t value;
    uint16_t row;
    uint16_t context;
    uint8_t count;
    uint8_t request;
    uint8_t destination;
    bool requested;
    // A rule's sequence longer than a node's room: its records past the room weren't written, and every consumer refuses
    // the node (tier1_select_append).
    bool overflowed;
};

// A node's next record, where its room (BUSTER_TIER1_SELECT_RECORDS_PER_NODE) has one. A rule whose sequence is longer
// writes nothing past the room and marks the node's records overflowed, which every consumer refuses, so that the
// function fails rather than its expansion overwriting its own fields (#977's review: the signed power-of-two
// remainder's six records are the room's whole).
static void tier1_select_append(Tier1SelectRecords* result, X86Instruction record)
{
    const bool room = result->count < BUSTER_TIER1_SELECT_RECORDS_PER_NODE;
    if (room)
    {
        result->records[result->count] = record;
        result->count += 1;
    }
    result->overflowed |= !room;
}

// A record whose operation, on the destination register, has an immediate, as a request. The semantics module owns sign
// extension, zero idioms, immediate widths and the flags proof for each alternate form. An operation without a row
// leaves the record empty, which validation refuses. The target's allow-list is checked on the final records,
// independently of the host's ISA.
static void tier1_select_request(Tier1SelectRecords* result, X86Operation operation, uint32_t width, uint8_t destination, int64_t value, uint8_t live_flags)
{
    const uint32_t row = x86_choice_row(operation, width);
    if (row != BUSTER_X86_CHOICE_NO_ROW && result->count < BUSTER_TIER1_SELECT_RECORDS_PER_NODE)
    {
        result->requested = true;
        result->request = result->count;
        result->destination = destination;
        result->row = (uint16_t)row;
        result->value = value;
        result->context = (uint16_t)(BUSTER_X86_CONTEXT_REGISTER | (destination == X86_REGISTER_RAX ? BUSTER_X86_CONTEXT_ACCUMULATOR : 0) | live_flags);
    }
    tier1_select_append(result, (X86Instruction){0});
}

// A function's requests, in its records' order, held until x86_choose_forms' 32 lanes are full or the function's records
// end (tier1_select_requests_flush): each one's record (`at`), destination register, row, value and context. On the
// scalar path (`vector` false) each is chosen as it comes, by the twin, and none is held.
#define BUSTER_TIER1_SELECT_BATCH 32
typedef struct Tier1SelectRequests Tier1SelectRequests;
struct Tier1SelectRequests
{
    int64_t values[BUSTER_TIER1_SELECT_BATCH];
    uint32_t at[BUSTER_TIER1_SELECT_BATCH];
    uint16_t rows[BUSTER_TIER1_SELECT_BATCH];
    uint16_t contexts[BUSTER_TIER1_SELECT_BATCH];
    uint16_t forms[BUSTER_TIER1_SELECT_BATCH];
    uint8_t destinations[BUSTER_TIER1_SELECT_BATCH];
    uint32_t count;
    bool vector;
};

// The held requests' forms, chosen in one batch by x86_choose_forms: each record the chosen form's, or left empty where
// no form takes the value, which validation then refuses, as a choice of one record at a time did.
static void tier1_select_requests_flush(Tier1SelectRequests* requests, X86Instruction* records)
{
    x86_choose_forms(requests->rows, requests->values, requests->contexts, requests->count, requests->forms);
    for (uint32_t q = 0; q < requests->count; q += 1)
    {
        const uint16_t form = requests->forms[q];
        if (form != X86_FORM_NONE)
        {
            records[requests->at[q]] = x86_choice_instruction(form, (X86Instruction){ .rm = requests->destinations[q] }, requests->values[q]);
        }
    }
    requests->count = 0;
}

// The node's request, if it has one, its record at `first` among the function's `records`: held, a full batch's forms
// chosen into the records first, or on the scalar path chosen at once.
static void tier1_select_requests_add(Tier1SelectRequests* requests, const Tier1SelectRecords* selected, uint32_t first, X86Instruction* records)
{
    if (selected->requested && !requests->vector)
    {
        uint16_t form = X86_FORM_NONE;
        x86_choose_forms_scalar(&selected->row, &selected->value, &selected->context, 1, &form);
        if (form != X86_FORM_NONE)
        {
            records[first + selected->request] = x86_choice_instruction(form, (X86Instruction){ .rm = selected->destination }, selected->value);
        }
    }
    else if (selected->requested)
    {
        if (requests->count == BUSTER_TIER1_SELECT_BATCH)
        {
            tier1_select_requests_flush(requests, records);
        }
        const uint32_t q = requests->count++;
        requests->at[q] = first + selected->request;
        requests->values[q] = selected->value;
        requests->rows[q] = selected->row;
        requests->contexts[q] = selected->context;
        requests->destinations[q] = selected->destination;
    }
}

static bool tier1_select_immediate_fits(uint32_t width, uint64_t value)
{
    return width == 4 || (uint64_t)(int64_t)(int32_t)value == value;
}

static uint32_t tier1_select_log2(uint64_t value)
{
    return value != 0 && (value & (value - 1)) == 0 ? (uint32_t)__builtin_ctzll(value) : UINT32_MAX;
}

// A division by a constant as a multiplication by its reciprocal (Granlund and Montgomery, "Division by Invariant
// Integers using Multiplication", PLDI 1994), for N-bit words, N the width's bits:
// - unsigned: the quotient is the high N bits of the dividend, shifted right by `pre_shift`, times `multiplier`, shifted
//   right by `post_shift`. Where the multiplier needs N + 1 bits (`add`), `multiplier` is its low N bits and t that high
//   product: the quotient is (((n - t) >> 1) + t) >> (post_shift - 1), in which n - t can't borrow nor the sum carry.
// - signed, by the divisor's magnitude: the signed high product of the dividend and `multiplier`, plus the dividend where
//   the multiplier is 2^(N - 1) or more (`add`: `multiplier` is then negative as an N-bit number), shifted right
//   arithmetically by `post_shift`, plus one where that is negative, which makes the floor C's rounding toward zero; and
//   negated for a negative divisor.
typedef struct Tier1SelectReciprocal Tier1SelectReciprocal;
struct Tier1SelectReciprocal
{
    uint64_t multiplier;
    uint8_t pre_shift;
    uint8_t post_shift;
    bool add;
};

// floor((2^power + addend) / divisor), for `power` at most 128 (2^128 as the largest 128-bit number plus one), `addend`
// below 2^127 and a nonzero divisor.
static unsigned __int128 tier1_select_quotient_power(uint32_t power, unsigned __int128 addend, uint64_t divisor)
{
    const unsigned __int128 all = ~(unsigned __int128)0;
    return power < 128 ? (((unsigned __int128)1 << power) + addend) / divisor : all / divisor + (all % divisor + 1 + addend) / divisor;
}

// Granlund and Montgomery's CHOOSE_MULTIPLIER (their Figure 6.2) for a divisor of at least 3 that isn't a power of two, an
// N-bit word and dividends of `precision` bits: the multiplier, below 2^(N + 1) (and below 2^N for a precision below N),
// and its shift.
static unsigned __int128 tier1_select_multiplier(uint64_t divisor, uint32_t bits, uint32_t precision, uint8_t* post_shift)
{
    const uint32_t log = 64 - (uint32_t)__builtin_clzll(divisor - 1);
    unsigned __int128 low = tier1_select_quotient_power(bits + log, 0, divisor);
    unsigned __int128 high = tier1_select_quotient_power(bits + log, (unsigned __int128)1 << (bits + log - precision), divisor);
    uint32_t shift = log;
    while ((low >> 1) < (high >> 1) && shift > 0)
    {
        low >>= 1;
        high >>= 1;
        shift -= 1;
    }
    *post_shift = (uint8_t)shift;
    return high;
}

// The magnitude of a signed divisor whose two's complement is the width's bits of `divisor`.
static uint64_t tier1_select_magnitude(uint64_t divisor, uint32_t width)
{
    const uint64_t sign = UINT64_C(1) << (width * 8 - 1);
    const uint64_t extended = ((divisor & (sign | (sign - 1))) ^ sign) - sign;
    return (int64_t)extended < 0 ? 0 - extended : extended;
}

// Whether a constant divisor (the width's bits) takes TIER1_RULE_DIVIDE_RECIPROCAL: an unsigned one but zero and the powers
// of two (a shift or a mask), and a signed one of magnitude 3 or more but ±2^k and the sign bit. Zero and -1 stay with div
// and idiv, which raise C's undefined division by zero and INT_MIN / -1 as the hardware does, and 1 is a copy.
static bool tier1_select_reciprocal_divisor(uint64_t divisor, uint32_t width, bool is_signed)
{
    const uint64_t magnitude = is_signed ? tier1_select_magnitude(divisor, width) : divisor;
    return magnitude >= 3 && tier1_select_log2(magnitude) == UINT32_MAX;
}

// A divisor's reciprocal (tier1_select_reciprocal_divisor's), its width's bits for an unsigned one.
static Tier1SelectReciprocal tier1_select_reciprocal(uint64_t divisor, uint32_t width, bool is_signed)
{
    const uint32_t bits = width * 8;
    const unsigned __int128 word = (unsigned __int128)1 << bits;
    Tier1SelectReciprocal result = {0};
    unsigned __int128 multiplier = 0;
    if (is_signed)
    {
        multiplier = tier1_select_multiplier(tier1_select_magnitude(divisor, width), bits, bits - 1, &result.post_shift);
        result.add = multiplier >= word >> 1;
    }
    else
    {
        multiplier = tier1_select_multiplier(divisor, bits, bits, &result.post_shift);
        // An even divisor whose multiplier needs N + 1 bits: the dividend shifted right by the divisor's zero bits, whose
        // odd part's multiplier then fits N bits (their Figure 6.2's retry).
        const uint32_t zeros = (uint32_t)__builtin_ctzll(divisor);
        if (multiplier >= word && zeros != 0)
        {
            multiplier = tier1_select_multiplier(divisor >> zeros, bits, bits - zeros, &result.post_shift);
            result.pre_shift = (uint8_t)zeros;
        }
        result.add = multiplier >= word;
    }
    result.multiplier = (uint64_t)(multiplier & (word - 1));
    return result;
}

static const uint16_t tier1_select_condition_branches[10] =
{
    X86_FORM_JE_REL8,
    X86_FORM_JNE_REL8,
    X86_FORM_JL_REL8,
    X86_FORM_JLE_REL8,
    X86_FORM_JG_REL8,
    X86_FORM_JGE_REL8,
    X86_FORM_JB_REL8,
    X86_FORM_JBE_REL8,
    X86_FORM_JA_REL8,
    X86_FORM_JAE_REL8,
};

static const uint16_t tier1_select_condition_sets[10] =
{
    X86_FORM_SETE_RM8,
    X86_FORM_SETNE_RM8,
    X86_FORM_SETL_RM8,
    X86_FORM_SETLE_RM8,
    X86_FORM_SETG_RM8,
    X86_FORM_SETGE_RM8,
    X86_FORM_SETB_RM8,
    X86_FORM_SETBE_RM8,
    X86_FORM_SETA_RM8,
    X86_FORM_SETAE_RM8,
};

static const uint8_t tier1_select_condition_flags[10] =
{
    BUSTER_X86_FLAG_ZF,
    BUSTER_X86_FLAG_ZF,
    BUSTER_X86_FLAG_SF | BUSTER_X86_FLAG_OF,
    BUSTER_X86_FLAG_ZF | BUSTER_X86_FLAG_SF | BUSTER_X86_FLAG_OF,
    BUSTER_X86_FLAG_ZF | BUSTER_X86_FLAG_SF | BUSTER_X86_FLAG_OF,
    BUSTER_X86_FLAG_SF | BUSTER_X86_FLAG_OF,
    BUSTER_X86_FLAG_CF,
    BUSTER_X86_FLAG_CF | BUSTER_X86_FLAG_ZF,
    BUSTER_X86_FLAG_CF | BUSTER_X86_FLAG_ZF,
    BUSTER_X86_FLAG_CF,
};

// A selected rule's holes, after allocation. This is transient emitter state, not another IR: operands are physical
// registers, `memory` is already an encoder addressing operand, and the rule fixes the exact record sequence.
typedef struct Tier1SelectHoles Tier1SelectHoles;
struct Tier1SelectHoles
{
    Tier1SelectRule rule;
    X86Operation operation;
    uint64_t immediate;
    X86Instruction memory;
    uint8_t width;
    uint8_t source_width;
    uint8_t destination;
    uint8_t left;
    uint8_t right;
    uint8_t third;
    uint8_t condition;
    uint8_t scale;
    bool compare_immediate;
    bool compare_memory;
    bool sign_result;
    // The pattern's no-ops (Tier1SelectPattern.nops).
    uint8_t nops;
    // A load's or a store's segment override (#1447, BUSTER_IR_SEGMENT's FS, GS or SS), which its access's record takes
    // (tier1_select_segment), or 0.
    uint8_t segment;
    // The target has no AVX512VL (#1281): the vector class's 16- and 32-byte values take VEX forms
    // (tier1_select_vector_forms, tier1_select_class_forms). The CFG consumer sets it (Tier1EmitPrepared.vex).
    bool vex;
    // The target has no AVX either (the x86-64 baseline, x86-64-v2: #141's SSE targets): its 16-byte vectors take legacy
    // SSE's forms, two operands the destination's the first (Tier1EmitPrepared.sse; `vex` is set too).
    bool sse;
    // The target has AVX2 (Tier1EmitPrepared.avx2): without it, a VEX target's 32-byte vectors take AVX's forms alone.
    bool avx2;
};

static uint16_t tier1_select_binary_form(X86Operation operation, uint32_t width)
{
    uint16_t form = X86_FORM_NONE;
    switch (operation)
    {
    case X86_OPERATION_ADD:
        form = width == 8 ? X86_FORM_ADD_R64_RM64 : width == 2 ? X86_FORM_ADD_R16_RM16 : width == 1 ? X86_FORM_ADD_R8_RM8 : X86_FORM_ADD_R32_RM32;
        break;
    case X86_OPERATION_SUB:
        form = width == 8 ? X86_FORM_SUB_R64_RM64 : width == 2 ? X86_FORM_SUB_R16_RM16 : width == 1 ? X86_FORM_SUB_R8_RM8 : X86_FORM_SUB_R32_RM32;
        break;
    case X86_OPERATION_AND:
        form = width == 8 ? X86_FORM_AND_R64_RM64 : X86_FORM_AND_R32_RM32;
        break;
    case X86_OPERATION_OR:
        form = width == 8 ? X86_FORM_OR_R64_RM64 : X86_FORM_OR_R32_RM32;
        break;
    case X86_OPERATION_XOR:
        form = width == 8 ? X86_FORM_XOR_R64_RM64 : X86_FORM_XOR_R32_RM32;
        break;
    case X86_OPERATION_IMUL:
        form = width == 8 ? X86_FORM_IMUL_R64_RM64 : X86_FORM_IMUL_R32_RM32;
        break;
    default:
        break;
    }
    return form;
}

static bool tier1_select_commutative(X86Operation operation)
{
    return operation == X86_OPERATION_ADD || operation == X86_OPERATION_AND || operation == X86_OPERATION_OR || operation == X86_OPERATION_XOR || operation == X86_OPERATION_IMUL;
}

// imul destination, source, value: its sign-extended imm8 form where the value fits one, else imm32's.
static X86Instruction tier1_select_multiply_immediate(uint32_t width, uint8_t destination, uint8_t source, uint64_t value)
{
    const bool small = (uint64_t)(int64_t)(int8_t)value == value || (width == 4 && (uint32_t)(int32_t)(int8_t)value == (uint32_t)value);
    const uint16_t form = width == 8 ? (small ? X86_FORM_IMUL_R64_RM64_IMM8 : X86_FORM_IMUL_R64_RM64_IMM32) : (small ? X86_FORM_IMUL_R32_RM32_IMM8 : X86_FORM_IMUL_R32_RM32_IMM32);
    X86Instruction record = tier1_select_register_record(form, destination, source);
    record.immediate = small ? (int8_t)value : (int32_t)value;
    return record;
}

// mov reg, value at `width`: mov r32, imm32 (which zero-extends), mov r64, simm32 or movabs, the shortest that holds it, as
// rematerialization chooses (without the zero idiom, which these sequences' constants never need).
static X86Instruction tier1_select_constant_move(uint32_t width, uint8_t reg, uint64_t value)
{
    const bool zero_extends = width == 4 || value <= UINT32_MAX;
    const bool sign_extends = (uint64_t)(int64_t)(int32_t)value == value;
    const X86Instruction result =
    {
        .displacement = zero_extends || sign_extends ? 0 : (int32_t)(value >> 32),
        .immediate = (int32_t)value,
        .rm = reg,
        .form = zero_extends ? X86_FORM_MOV_R32_IMM32 : sign_extends ? X86_FORM_MOV_RM64_IMM32 : X86_FORM_MOV_R64_IMM64,
    };
    return result;
}

// shr reg, count (sar for X86_OPERATION_SAR) at `width`.
static X86Instruction tier1_select_shift(X86Operation operation, uint32_t width, uint8_t reg, uint32_t count)
{
    const uint16_t form = operation == X86_OPERATION_SAR ? (width == 8 ? X86_FORM_SAR_RM64_IMM8 : X86_FORM_SAR_RM32_IMM8) : (width == 8 ? X86_FORM_SHR_RM64_IMM8 : X86_FORM_SHR_RM32_IMM8);
    const X86Instruction result = { .immediate = (int32_t)count, .rm = reg, .form = form };
    return result;
}

static void tier1_select_compare(Tier1SelectRecords* result, const Tier1SelectHoles* holes)
{
    if (holes->compare_memory)
    {
        X86Instruction record = holes->memory;
        if (holes->compare_immediate)
        {
            record.form = holes->width == 1 ? X86_FORM_CMP_RM8_IMM8 : holes->width == 2 ? X86_FORM_CMP_RM16_IMM16 :
                          holes->width == 8 ? X86_FORM_CMP_RM64_IMM32 : X86_FORM_CMP_RM32_IMM32;
            record.immediate = (int32_t)holes->immediate;
        }
        else
        {
            record.form = holes->width == 8 ? X86_FORM_CMP_R64_RM64 : X86_FORM_CMP_R32_RM32;
            record.reg = holes->left;
        }
        tier1_select_append(result, record);
    }
    else if (holes->compare_immediate)
    {
        tier1_select_request(result, X86_OPERATION_CMP, holes->width, holes->left, (int64_t)holes->immediate, tier1_select_condition_flags[holes->condition]);
    }
    else
    {
        tier1_select_append(result, tier1_select_register_record(holes->width == 8 ? X86_FORM_CMP_R64_RM64 : X86_FORM_CMP_R32_RM32, holes->left, holes->right));
    }
}

// The opmask forms of a mask of each width (#179), by the width's log2, then a row of none for any other width: kmov
// between opmask registers, from memory, to memory and from a general register (a 32-bit one, but kmovq's 64), and the
// opmask instructions the selector takes.
// kmovb, kandb and the other byte forms need AVX512DQ, the doubleword and quadword ones AVX512BW, the word ones AVX512F:
// the target's allow-list decides, as for every record.
typedef struct Tier1SelectMaskForms Tier1SelectMaskForms;
struct Tier1SelectMaskForms
{
    uint16_t move;
    uint16_t load;
    uint16_t store;
    uint16_t from_general;
    uint16_t to_general;
    uint16_t and_forms;
    uint16_t or_forms;
    uint16_t xor_forms;
    uint16_t xnor_forms;
    uint16_t not_forms;
};

static const Tier1SelectMaskForms tier1_select_mask_forms_table[5] =
{
    { X86_FORM_KMOVB_K_K, X86_FORM_KMOVB_K_M8, X86_FORM_KMOVB_M8_K, X86_FORM_KMOVB_K_R32, X86_FORM_KMOVB_R32_K, X86_FORM_KANDB_K_K_K, X86_FORM_KORB_K_K_K, X86_FORM_KXORB_K_K_K, X86_FORM_KXNORB_K_K_K, X86_FORM_KNOTB_K_K },
    { X86_FORM_KMOVW_K_K, X86_FORM_KMOVW_K_M16, X86_FORM_KMOVW_M16_K, X86_FORM_KMOVW_K_R32, X86_FORM_KMOVW_R32_K, X86_FORM_KANDW_K_K_K, X86_FORM_KORW_K_K_K, X86_FORM_KXORW_K_K_K, X86_FORM_KXNORW_K_K_K, X86_FORM_KNOTW_K_K },
    { X86_FORM_KMOVD_K_K, X86_FORM_KMOVD_K_M32, X86_FORM_KMOVD_M32_K, X86_FORM_KMOVD_K_R32, X86_FORM_KMOVD_R32_K, X86_FORM_KANDD_K_K_K, X86_FORM_KORD_K_K_K, X86_FORM_KXORD_K_K_K, X86_FORM_KXNORD_K_K_K, X86_FORM_KNOTD_K_K },
    { X86_FORM_KMOVQ_K_K, X86_FORM_KMOVQ_K_M64, X86_FORM_KMOVQ_M64_K, X86_FORM_KMOVQ_K_R64, X86_FORM_KMOVQ_R64_K, X86_FORM_KANDQ_K_K_K, X86_FORM_KORQ_K_K_K, X86_FORM_KXORQ_K_K_K, X86_FORM_KXNORQ_K_K_K, X86_FORM_KNOTQ_K_K },
    { X86_FORM_NONE, X86_FORM_NONE, X86_FORM_NONE, X86_FORM_NONE, X86_FORM_NONE, X86_FORM_NONE, X86_FORM_NONE, X86_FORM_NONE, X86_FORM_NONE, X86_FORM_NONE },
};

// A mask width's forms: 1, 2, 4 or 8 bytes. Any other width, which no mask has, gets X86_FORM_NONE throughout rather than
// another width's forms: no target's allow-list takes that form (x86_target_form_allowed), so the CFG consumer refuses a
// record of it at emission (TIER1_EMIT_TARGET).
static const Tier1SelectMaskForms* tier1_select_mask_forms(uint32_t width)
{
    const uint32_t index = width == 8 ? 3 : width == 4 ? 2 : width == 2 ? 1 : width == 1 ? 0 : 4;
    return &tier1_select_mask_forms_table[index];
}

// The scalar SSE forms of a floating value of each width (#141): single precision's (4 bytes), double precision's (8),
// then a row of none for any other width, which no target's allow-list takes (the consumer then refuses its record:
// TIER1_EMIT_TARGET). Moves from and to memory, the arithmetic in IR_FADD's order, ucomis, the bits from and to a general
// register, the conversions from a 32- or 64-bit integer (a row of its result's width) and to one, truncating (a row
// of its operand's width), and the other precision's (a row of its operand's: cvtss2sd widens, cvtsd2ss narrows).
typedef struct Tier1SelectFloatForms Tier1SelectFloatForms;
struct Tier1SelectFloatForms
{
    uint16_t load;
    uint16_t store;
    uint16_t arithmetic[4];
    uint16_t compare;
    uint16_t from_general;
    uint16_t to_general;
    uint16_t from_integer[2];
    uint16_t to_integer[2];
    uint16_t resize;
};

static const Tier1SelectFloatForms tier1_select_float_forms_table[9] =
{
    {
        X86_FORM_MOVSS_X_XM32, X86_FORM_MOVSS_XM32_X, { X86_FORM_ADDSS_X_XM32, X86_FORM_SUBSS_X_XM32, X86_FORM_MULSS_X_XM32, X86_FORM_DIVSS_X_XM32 }, X86_FORM_UCOMISS_X_XM32,
        X86_FORM_MOVD_X_RM32, X86_FORM_MOVD_RM32_X, { X86_FORM_CVTSI2SS_X_RM32, X86_FORM_CVTSI2SS_X_RM64 }, { X86_FORM_CVTTSS2SI_R32_XM32, X86_FORM_CVTTSS2SI_R64_XM32 },
        X86_FORM_CVTSS2SD_X_XM32,
    },
    {
        X86_FORM_MOVSD_X_XM64, X86_FORM_MOVSD_XM64_X, { X86_FORM_ADDSD_X_XM64, X86_FORM_SUBSD_X_XM64, X86_FORM_MULSD_X_XM64, X86_FORM_DIVSD_X_XM64 }, X86_FORM_UCOMISD_X_XM64,
        X86_FORM_MOVQ_X_RM64, X86_FORM_MOVQ_RM64_X, { X86_FORM_CVTSI2SD_X_RM32, X86_FORM_CVTSI2SD_X_RM64 }, { X86_FORM_CVTTSD2SI_R32_XM64, X86_FORM_CVTTSD2SI_R64_XM64 },
        X86_FORM_CVTSD2SS_X_XM64,
    },
    {
        X86_FORM_NONE, X86_FORM_NONE, { X86_FORM_NONE, X86_FORM_NONE, X86_FORM_NONE, X86_FORM_NONE }, X86_FORM_NONE, X86_FORM_NONE, X86_FORM_NONE, { X86_FORM_NONE, X86_FORM_NONE },
        { X86_FORM_NONE, X86_FORM_NONE }, X86_FORM_NONE,
    },
    // A vector's (#141), 16, 32 and 64 bytes: its loads, stores, spills and reloads, vmovdqu32 at its width.
    {
        X86_FORM_VMOVDQU32_X_XM128, X86_FORM_VMOVDQU32_XM128_X, { X86_FORM_NONE, X86_FORM_NONE, X86_FORM_NONE, X86_FORM_NONE }, X86_FORM_NONE, X86_FORM_NONE, X86_FORM_NONE,
        { X86_FORM_NONE, X86_FORM_NONE }, { X86_FORM_NONE, X86_FORM_NONE }, X86_FORM_NONE,
    },
    {
        X86_FORM_VMOVDQU32_Y_YM256, X86_FORM_VMOVDQU32_YM256_Y, { X86_FORM_NONE, X86_FORM_NONE, X86_FORM_NONE, X86_FORM_NONE }, X86_FORM_NONE, X86_FORM_NONE, X86_FORM_NONE,
        { X86_FORM_NONE, X86_FORM_NONE }, { X86_FORM_NONE, X86_FORM_NONE }, X86_FORM_NONE,
    },
    {
        X86_FORM_VMOVDQU32_Z_ZM512, X86_FORM_VMOVDQU32_ZM512_Z, { X86_FORM_NONE, X86_FORM_NONE, X86_FORM_NONE, X86_FORM_NONE }, X86_FORM_NONE, X86_FORM_NONE, X86_FORM_NONE,
        { X86_FORM_NONE, X86_FORM_NONE }, { X86_FORM_NONE, X86_FORM_NONE }, X86_FORM_NONE,
    },
    // A 16- and a 32-byte vector's at a target without AVX512VL (#1281): AVX's vmovdqu of its xmm or ymm register, which
    // names registers below 16 alone, as the vector class's are.
    {
        X86_FORM_VMOVDQU_X_XM128, X86_FORM_VMOVDQU_XM128_X, { X86_FORM_NONE, X86_FORM_NONE, X86_FORM_NONE, X86_FORM_NONE }, X86_FORM_NONE, X86_FORM_NONE, X86_FORM_NONE,
        { X86_FORM_NONE, X86_FORM_NONE }, { X86_FORM_NONE, X86_FORM_NONE }, X86_FORM_NONE,
    },
    {
        X86_FORM_VMOVDQU_Y_YM256, X86_FORM_VMOVDQU_YM256_Y, { X86_FORM_NONE, X86_FORM_NONE, X86_FORM_NONE, X86_FORM_NONE }, X86_FORM_NONE, X86_FORM_NONE, X86_FORM_NONE,
        { X86_FORM_NONE, X86_FORM_NONE }, { X86_FORM_NONE, X86_FORM_NONE }, X86_FORM_NONE,
    },
    // A 16-byte vector's at a target without AVX (#141's SSE targets): legacy movups, which leaves no upper half to keep
    // there (a target without AVX has no ymm register).
    {
        X86_FORM_MOVUPS_X_XM, X86_FORM_MOVUPS_XM_X, { X86_FORM_NONE, X86_FORM_NONE, X86_FORM_NONE, X86_FORM_NONE }, X86_FORM_NONE, X86_FORM_NONE, X86_FORM_NONE,
        { X86_FORM_NONE, X86_FORM_NONE }, { X86_FORM_NONE, X86_FORM_NONE }, X86_FORM_NONE,
    },
};

static const Tier1SelectFloatForms* tier1_select_float_forms(uint32_t width)
{
    return &tier1_select_float_forms_table[width == 4 ? 0 : width == 8 ? 1 : width == 16 ? 3 : width == 32 ? 4 : width == 64 ? 5 : 2];
}

// The vector class's loads, stores, spills and reloads of a value of `width` bytes: a floating value's movss or movsd, and
// a vector's vmovdqu32 at its width, or where `vex` (a target without AVX512VL, #1281) a 16- or 32-byte one's vmovdqu.
static const Tier1SelectFloatForms* tier1_select_class_forms(uint32_t width, uint32_t encoding)
{
    // At a legacy SSE target (#141) a 16-byte vector's movups, and no 32-byte vector, which the IR has none of there.
    return encoding == BUSTER_TIER1_ENCODING_SSE && (width == 16 || width == 32) ? &tier1_select_float_forms_table[width == 16 ? 8 : 2] :
           encoding != BUSTER_TIER1_ENCODING_EVEX && (width == 16 || width == 32) ? &tier1_select_float_forms_table[width == 16 ? 6 : 7] :
                                                                                  tier1_select_float_forms(width);
}

// A vector's EVEX forms at its width (#141, TIER1_RULE_VECTOR's): xmm's, ymm's and zmm's, each AVX512F's and the narrower
// two AVX512VL's too, which a target without them refuses at emission as any record's. At a target without AVX512VL
// (#1281), a 16- or 32-byte vector's VEX forms, AVX's and AVX2's, which name registers below 16 alone, as the vector
// class's are: vmovdqa, vpaddd, vpsubd, vpmulld, vpand, vpor and vpxor; its ones by vpcmpeqd of a register with itself
// (`ones`, no immediate), and its broadcast from the low doubleword of an xmm register (vmovd into it first): vpshufd's
// with 0 of an xmm's and vpbroadcastd's of a ymm's (at a target with AVX but not AVX2, TIER1_RULE_VECTOR's AVX forms
// in their place: vandps, vorps, vxorps, and vpcmpeqd or vpshufd of the xmm register with vinsertf128). VEX has no ternary logic, no compare into a mask and no blend under
// one (X86_FORM_NONE): the IR's NOT, COMPARE and SELECT come of EVEX rows alone (ir_simd_typed_operation), which such a
// target has none of.
typedef struct Tier1SelectVectorForms Tier1SelectVectorForms;
struct Tier1SelectVectorForms
{
    uint16_t move;
    uint16_t binary[6];
    uint16_t ternary;
    uint16_t compare[2];
    uint16_t blend;
    uint16_t broadcast;
    uint16_t ones;
};

static const Tier1SelectVectorForms tier1_select_vector_forms_table[7] =
{
    {
        X86_FORM_VMOVDQA64_X_XM128,
        { X86_FORM_VPADDD_X_X_XM128_EVEX, X86_FORM_VPSUBD_X_X_XM128_EVEX, X86_FORM_VPMULLD_X_X_XM128_EVEX, X86_FORM_VPANDQ_X_X_XM128, X86_FORM_VPORQ_X_X_XM128, X86_FORM_VPXORD_X_X_XM128 },
        X86_FORM_VPTERNLOGD_X_X_XM128_IMM8,
        { X86_FORM_VPCMPD_K_X_XM128_IMM8, X86_FORM_VPCMPUD_K_X_XM128_IMM8 },
        X86_FORM_VPBLENDMD_X_X_XM128,
        X86_FORM_VPBROADCASTD_X_R32,
        X86_FORM_NONE,
    },
    {
        X86_FORM_VMOVDQA64_Y_YM256,
        { X86_FORM_VPADDD_Y_Y_YM256_EVEX, X86_FORM_VPSUBD_Y_Y_YM256_EVEX, X86_FORM_VPMULLD_Y_Y_YM256_EVEX, X86_FORM_VPANDQ_Y_Y_YM256, X86_FORM_VPORQ_Y_Y_YM256, X86_FORM_VPXORD_Y_Y_YM256 },
        X86_FORM_VPTERNLOGD_Y_Y_YM256_IMM8,
        { X86_FORM_VPCMPD_K_Y_YM256_IMM8, X86_FORM_VPCMPUD_K_Y_YM256_IMM8 },
        X86_FORM_VPBLENDMD_Y_Y_YM256,
        X86_FORM_VPBROADCASTD_Y_R32,
        X86_FORM_NONE,
    },
    {
        X86_FORM_VMOVDQA64_Z_ZM512,
        { X86_FORM_VPADDD_Z_Z_ZM512, X86_FORM_VPSUBD_Z_Z_ZM512, X86_FORM_VPMULLD_Z_Z_ZM512, X86_FORM_VPANDQ_Z_Z_ZM512, X86_FORM_VPORQ_Z_Z_ZM512, X86_FORM_VPXORD_Z_Z_ZM512 },
        X86_FORM_VPTERNLOGD_Z_Z_ZM512_IMM8,
        { X86_FORM_VPCMPD_K_Z_ZM512_IMM8, X86_FORM_VPCMPUD_K_Z_ZM512_IMM8 },
        X86_FORM_VPBLENDMD_Z_Z_ZM512,
        X86_FORM_VPBROADCASTD_Z_R32,
        X86_FORM_NONE,
    },
    {
        X86_FORM_NONE,
        { X86_FORM_NONE, X86_FORM_NONE, X86_FORM_NONE, X86_FORM_NONE, X86_FORM_NONE, X86_FORM_NONE },
        X86_FORM_NONE,
        { X86_FORM_NONE, X86_FORM_NONE },
        X86_FORM_NONE,
        X86_FORM_NONE,
        X86_FORM_NONE,
    },
    {
        X86_FORM_VMOVDQA_X_XM128,
        { X86_FORM_VPADDD_X_X_XM128, X86_FORM_VPSUBD_X_X_XM128, X86_FORM_VPMULLD_X_X_XM128, X86_FORM_VPAND_X_X_XM128, X86_FORM_VPOR_X_X_XM128, X86_FORM_VPXOR_X_X_XM128 },
        X86_FORM_NONE,
        { X86_FORM_NONE, X86_FORM_NONE },
        X86_FORM_NONE,
        X86_FORM_VPSHUFD_X_XM128_IMM8,
        X86_FORM_VPCMPEQD_X_X_XM128,
    },
    {
        X86_FORM_VMOVDQA_Y_YM256,
        { X86_FORM_VPADDD_Y_Y_YM256, X86_FORM_VPSUBD_Y_Y_YM256, X86_FORM_VPMULLD_Y_Y_YM256, X86_FORM_VPAND_Y_Y_YM256, X86_FORM_VPOR_Y_Y_YM256, X86_FORM_VPXOR_Y_Y_YM256 },
        X86_FORM_NONE,
        { X86_FORM_NONE, X86_FORM_NONE },
        X86_FORM_NONE,
        X86_FORM_VPBROADCASTD_Y_XM32,
        X86_FORM_VPCMPEQD_Y_Y_YM256,
    },
    // A 16-byte vector's at a target without AVX (#141's SSE targets): legacy SSE2's, two operands, the destination's the
    // first (movaps, pxor's zero, pcmpeqd's ones, pshufd's splat); no ternary logic, compare into a mask or blend.
    {
        X86_FORM_MOVAPS_X_XM,
        { X86_FORM_PADDD_X_XM128, X86_FORM_PSUBD_X_XM128, X86_FORM_PMULLD_X_XM128, X86_FORM_PAND_X_XM128, X86_FORM_POR_X_XM128, X86_FORM_PXOR_X_XM128 },
        X86_FORM_NONE,
        { X86_FORM_NONE, X86_FORM_NONE },
        X86_FORM_NONE,
        X86_FORM_PSHUFD_X_XM128_IMM8,
        X86_FORM_PCMPEQD_X_XM128,
    },
};

// A width no vector has gets no form (X86_FORM_NONE), which no target allows, rather than another width's; a 64-byte
// vector's are EVEX's whatever `vex` is (only AVX512F has zmm registers).
static const Tier1SelectVectorForms* tier1_select_vector_forms(uint32_t width, uint32_t encoding)
{
    const bool vex = encoding != BUSTER_TIER1_ENCODING_EVEX;
    const bool sse = encoding == BUSTER_TIER1_ENCODING_SSE;
    return &tier1_select_vector_forms_table[sse && width == 16 ? 6 : sse && width == 32 ? 3 : vex && width == 16 ? 4 : vex && width == 32 ? 5 : width == 16 ? 0 : width == 32 ? 1 : width == 64 ? 2 : 3];
}

// A hole's encoding level (BUSTER_TIER1_ENCODING_*), from its vex and sse.
static uint32_t tier1_select_holes_encoding(const Tier1SelectHoles* holes)
{
    return holes->sse ? BUSTER_TIER1_ENCODING_SSE : holes->vex ? BUSTER_TIER1_ENCODING_VEX : BUSTER_TIER1_ENCODING_EVEX;
}

// A vector operation's forms at its lanes (#141's element types), three operands (reg, vvvv, rm) at 16, 32 and 64
// bytes: EVEX's of AVX-512 ([0], the rows' own, c_check_program_vectors.c), and VEX's of AVX and AVX2 at 16 and 32 ([1],
// c_check_program_vectors_baseline.c's), X86_FORM_NONE where x86 has none (bytes' products and shifts, VEX's 16-bit
// shifts and its 64-bit arithmetic right shift). The bitwise operations are I32's alone, which any lanes' bits are.
#define BUSTER_TIER1_LANE_EVEX(name) { X86_FORM_##name##_X_X_XM128_EVEX, X86_FORM_##name##_Y_Y_YM256_EVEX, X86_FORM_##name##_Z_Z_ZM512 }
#define BUSTER_TIER1_LANE_PACKED(name) { X86_FORM_##name##_X_X_XM128, X86_FORM_##name##_Y_Y_YM256, X86_FORM_##name##_Z_Z_ZM512 }
#define BUSTER_TIER1_LANE_VEX(name) { X86_FORM_##name##_X_X_XM128, X86_FORM_##name##_Y_Y_YM256, X86_FORM_NONE }
#define BUSTER_TIER1_LANE_SSE(name) { X86_FORM_##name##_X_XM128, X86_FORM_NONE, X86_FORM_NONE }
static const uint16_t tier1_select_lane_forms[3][BUSTER_TIER1_VECTOR_OPERATIONS][IR_SIMD_LANE_COUNT][3] = {
    {
        [BUSTER_TIER1_VECTOR_ADD] = { [IR_SIMD_LANE_I32] = BUSTER_TIER1_LANE_EVEX(VPADDD), [IR_SIMD_LANE_I8] = BUSTER_TIER1_LANE_EVEX(VPADDB),
                                      [IR_SIMD_LANE_I16] = BUSTER_TIER1_LANE_EVEX(VPADDW), [IR_SIMD_LANE_I64] = BUSTER_TIER1_LANE_EVEX(VPADDQ) },
        [BUSTER_TIER1_VECTOR_SUB] = { [IR_SIMD_LANE_I32] = BUSTER_TIER1_LANE_EVEX(VPSUBD), [IR_SIMD_LANE_I8] = BUSTER_TIER1_LANE_EVEX(VPSUBB),
                                      [IR_SIMD_LANE_I16] = BUSTER_TIER1_LANE_EVEX(VPSUBW), [IR_SIMD_LANE_I64] = BUSTER_TIER1_LANE_EVEX(VPSUBQ) },
        [BUSTER_TIER1_VECTOR_MUL] = { [IR_SIMD_LANE_I32] = BUSTER_TIER1_LANE_EVEX(VPMULLD), [IR_SIMD_LANE_I16] = BUSTER_TIER1_LANE_EVEX(VPMULLW),
                                      [IR_SIMD_LANE_I64] = BUSTER_TIER1_LANE_PACKED(VPMULLQ) },
        [BUSTER_TIER1_VECTOR_AND] = { [IR_SIMD_LANE_I32] = BUSTER_TIER1_LANE_PACKED(VPANDQ) },
        [BUSTER_TIER1_VECTOR_OR] = { [IR_SIMD_LANE_I32] = BUSTER_TIER1_LANE_PACKED(VPORQ) },
        [BUSTER_TIER1_VECTOR_XOR] = { [IR_SIMD_LANE_I32] = BUSTER_TIER1_LANE_PACKED(VPXORD) },
        [BUSTER_TIER1_VECTOR_SHL] = { [IR_SIMD_LANE_I32] = BUSTER_TIER1_LANE_EVEX(VPSLLVD), [IR_SIMD_LANE_I16] = BUSTER_TIER1_LANE_PACKED(VPSLLVW),
                                      [IR_SIMD_LANE_I64] = BUSTER_TIER1_LANE_EVEX(VPSLLVQ) },
        [BUSTER_TIER1_VECTOR_LSHR] = { [IR_SIMD_LANE_I32] = BUSTER_TIER1_LANE_EVEX(VPSRLVD), [IR_SIMD_LANE_I16] = BUSTER_TIER1_LANE_PACKED(VPSRLVW),
                                       [IR_SIMD_LANE_I64] = BUSTER_TIER1_LANE_EVEX(VPSRLVQ) },
        [BUSTER_TIER1_VECTOR_ASHR] = { [IR_SIMD_LANE_I32] = BUSTER_TIER1_LANE_EVEX(VPSRAVD), [IR_SIMD_LANE_I16] = BUSTER_TIER1_LANE_PACKED(VPSRAVW),
                                       [IR_SIMD_LANE_I64] = BUSTER_TIER1_LANE_PACKED(VPSRAVQ) },
        [BUSTER_TIER1_VECTOR_FADD] = { [IR_SIMD_LANE_F32] = BUSTER_TIER1_LANE_EVEX(VADDPS), [IR_SIMD_LANE_F64] = BUSTER_TIER1_LANE_EVEX(VADDPD) },
        [BUSTER_TIER1_VECTOR_FSUB] = { [IR_SIMD_LANE_F32] = BUSTER_TIER1_LANE_EVEX(VSUBPS), [IR_SIMD_LANE_F64] = BUSTER_TIER1_LANE_EVEX(VSUBPD) },
        [BUSTER_TIER1_VECTOR_FMUL] = { [IR_SIMD_LANE_F32] = BUSTER_TIER1_LANE_EVEX(VMULPS), [IR_SIMD_LANE_F64] = BUSTER_TIER1_LANE_EVEX(VMULPD) },
        [BUSTER_TIER1_VECTOR_FDIV] = { [IR_SIMD_LANE_F32] = BUSTER_TIER1_LANE_EVEX(VDIVPS), [IR_SIMD_LANE_F64] = BUSTER_TIER1_LANE_EVEX(VDIVPD) },
    },
    {
        [BUSTER_TIER1_VECTOR_ADD] = { [IR_SIMD_LANE_I32] = BUSTER_TIER1_LANE_VEX(VPADDD), [IR_SIMD_LANE_I8] = BUSTER_TIER1_LANE_VEX(VPADDB),
                                      [IR_SIMD_LANE_I16] = BUSTER_TIER1_LANE_VEX(VPADDW), [IR_SIMD_LANE_I64] = BUSTER_TIER1_LANE_VEX(VPADDQ) },
        [BUSTER_TIER1_VECTOR_SUB] = { [IR_SIMD_LANE_I32] = BUSTER_TIER1_LANE_VEX(VPSUBD), [IR_SIMD_LANE_I8] = BUSTER_TIER1_LANE_VEX(VPSUBB),
                                      [IR_SIMD_LANE_I16] = BUSTER_TIER1_LANE_VEX(VPSUBW), [IR_SIMD_LANE_I64] = BUSTER_TIER1_LANE_VEX(VPSUBQ) },
        [BUSTER_TIER1_VECTOR_MUL] = { [IR_SIMD_LANE_I32] = BUSTER_TIER1_LANE_VEX(VPMULLD), [IR_SIMD_LANE_I16] = BUSTER_TIER1_LANE_VEX(VPMULLW) },
        [BUSTER_TIER1_VECTOR_AND] = { [IR_SIMD_LANE_I32] = BUSTER_TIER1_LANE_VEX(VPAND) },
        [BUSTER_TIER1_VECTOR_OR] = { [IR_SIMD_LANE_I32] = BUSTER_TIER1_LANE_VEX(VPOR) },
        [BUSTER_TIER1_VECTOR_XOR] = { [IR_SIMD_LANE_I32] = BUSTER_TIER1_LANE_VEX(VPXOR) },
        [BUSTER_TIER1_VECTOR_SHL] = { [IR_SIMD_LANE_I32] = BUSTER_TIER1_LANE_VEX(VPSLLVD), [IR_SIMD_LANE_I64] = BUSTER_TIER1_LANE_VEX(VPSLLVQ) },
        [BUSTER_TIER1_VECTOR_LSHR] = { [IR_SIMD_LANE_I32] = BUSTER_TIER1_LANE_VEX(VPSRLVD), [IR_SIMD_LANE_I64] = BUSTER_TIER1_LANE_VEX(VPSRLVQ) },
        [BUSTER_TIER1_VECTOR_ASHR] = { [IR_SIMD_LANE_I32] = BUSTER_TIER1_LANE_VEX(VPSRAVD) },
        [BUSTER_TIER1_VECTOR_FADD] = { [IR_SIMD_LANE_F32] = BUSTER_TIER1_LANE_VEX(VADDPS), [IR_SIMD_LANE_F64] = BUSTER_TIER1_LANE_VEX(VADDPD) },
        [BUSTER_TIER1_VECTOR_FSUB] = { [IR_SIMD_LANE_F32] = BUSTER_TIER1_LANE_VEX(VSUBPS), [IR_SIMD_LANE_F64] = BUSTER_TIER1_LANE_VEX(VSUBPD) },
        [BUSTER_TIER1_VECTOR_FMUL] = { [IR_SIMD_LANE_F32] = BUSTER_TIER1_LANE_VEX(VMULPS), [IR_SIMD_LANE_F64] = BUSTER_TIER1_LANE_VEX(VMULPD) },
        [BUSTER_TIER1_VECTOR_FDIV] = { [IR_SIMD_LANE_F32] = BUSTER_TIER1_LANE_VEX(VDIVPS), [IR_SIMD_LANE_F64] = BUSTER_TIER1_LANE_VEX(VDIVPD) },
    },
    // Legacy SSE's ([2]), of an xmm register alone: SSE2's, and SSE4.1's pmulld (the decoder admits a row's form only at a
    // target that has it). Two operands, the destination's the first (TIER1_RULE_VECTOR's two-operand expansion); no
    // shift by each lane's count, which is AVX2's.
    {
        [BUSTER_TIER1_VECTOR_ADD] = { [IR_SIMD_LANE_I32] = BUSTER_TIER1_LANE_SSE(PADDD), [IR_SIMD_LANE_I8] = BUSTER_TIER1_LANE_SSE(PADDB),
                                      [IR_SIMD_LANE_I16] = BUSTER_TIER1_LANE_SSE(PADDW), [IR_SIMD_LANE_I64] = BUSTER_TIER1_LANE_SSE(PADDQ) },
        [BUSTER_TIER1_VECTOR_SUB] = { [IR_SIMD_LANE_I32] = BUSTER_TIER1_LANE_SSE(PSUBD), [IR_SIMD_LANE_I8] = BUSTER_TIER1_LANE_SSE(PSUBB),
                                      [IR_SIMD_LANE_I16] = BUSTER_TIER1_LANE_SSE(PSUBW), [IR_SIMD_LANE_I64] = BUSTER_TIER1_LANE_SSE(PSUBQ) },
        [BUSTER_TIER1_VECTOR_MUL] = { [IR_SIMD_LANE_I32] = BUSTER_TIER1_LANE_SSE(PMULLD), [IR_SIMD_LANE_I16] = BUSTER_TIER1_LANE_SSE(PMULLW) },
        [BUSTER_TIER1_VECTOR_AND] = { [IR_SIMD_LANE_I32] = BUSTER_TIER1_LANE_SSE(PAND) },
        [BUSTER_TIER1_VECTOR_OR] = { [IR_SIMD_LANE_I32] = BUSTER_TIER1_LANE_SSE(POR) },
        [BUSTER_TIER1_VECTOR_XOR] = { [IR_SIMD_LANE_I32] = BUSTER_TIER1_LANE_SSE(PXOR) },
        [BUSTER_TIER1_VECTOR_FADD] = { [IR_SIMD_LANE_F32] = BUSTER_TIER1_LANE_SSE(ADDPS), [IR_SIMD_LANE_F64] = BUSTER_TIER1_LANE_SSE(ADDPD) },
        [BUSTER_TIER1_VECTOR_FSUB] = { [IR_SIMD_LANE_F32] = BUSTER_TIER1_LANE_SSE(SUBPS), [IR_SIMD_LANE_F64] = BUSTER_TIER1_LANE_SSE(SUBPD) },
        [BUSTER_TIER1_VECTOR_FMUL] = { [IR_SIMD_LANE_F32] = BUSTER_TIER1_LANE_SSE(MULPS), [IR_SIMD_LANE_F64] = BUSTER_TIER1_LANE_SSE(MULPD) },
        [BUSTER_TIER1_VECTOR_FDIV] = { [IR_SIMD_LANE_F32] = BUSTER_TIER1_LANE_SSE(DIVPS), [IR_SIMD_LANE_F64] = BUSTER_TIER1_LANE_SSE(DIVPD) },
    },
};
// A comparison's into an opmask register at its lanes, signed ([0]) and unsigned ([1]), vpcmp[u]b to vpcmp[u]q and
// vcmpps and vcmppd (floating lanes: [0] alone), and a selection's blend under one, vpblendmb to vpblendmq by the lanes'
// width (a float's lanes are doublewords). EVEX's alone: VEX has no comparison into a mask.
static const uint16_t tier1_select_lane_compares[IR_SIMD_LANE_COUNT][2][3] = {
    [IR_SIMD_LANE_I32] = { { X86_FORM_VPCMPD_K_X_XM128_IMM8, X86_FORM_VPCMPD_K_Y_YM256_IMM8, X86_FORM_VPCMPD_K_Z_ZM512_IMM8 },
                           { X86_FORM_VPCMPUD_K_X_XM128_IMM8, X86_FORM_VPCMPUD_K_Y_YM256_IMM8, X86_FORM_VPCMPUD_K_Z_ZM512_IMM8 } },
    [IR_SIMD_LANE_I8] = { { X86_FORM_VPCMPB_K_X_XM128_IMM8, X86_FORM_VPCMPB_K_Y_YM256_IMM8, X86_FORM_VPCMPB_K_Z_ZM512_IMM8 },
                          { X86_FORM_VPCMPUB_K_X_XM128_IMM8, X86_FORM_VPCMPUB_K_Y_YM256_IMM8, X86_FORM_VPCMPUB_K_Z_ZM512_IMM8 } },
    [IR_SIMD_LANE_I16] = { { X86_FORM_VPCMPW_K_X_XM128_IMM8, X86_FORM_VPCMPW_K_Y_YM256_IMM8, X86_FORM_VPCMPW_K_Z_ZM512_IMM8 },
                           { X86_FORM_VPCMPUW_K_X_XM128_IMM8, X86_FORM_VPCMPUW_K_Y_YM256_IMM8, X86_FORM_VPCMPUW_K_Z_ZM512_IMM8 } },
    [IR_SIMD_LANE_I64] = { { X86_FORM_VPCMPQ_K_X_XM128_IMM8, X86_FORM_VPCMPQ_K_Y_YM256_IMM8, X86_FORM_VPCMPQ_K_Z_ZM512_IMM8 },
                           { X86_FORM_VPCMPUQ_K_X_XM128_IMM8, X86_FORM_VPCMPUQ_K_Y_YM256_IMM8, X86_FORM_VPCMPUQ_K_Z_ZM512_IMM8 } },
    [IR_SIMD_LANE_F32] = { { X86_FORM_VCMPPS_K_X_XM128_IMM8, X86_FORM_VCMPPS_K_Y_YM256_IMM8, X86_FORM_VCMPPS_K_Z_ZM512_IMM8 } },
    [IR_SIMD_LANE_F64] = { { X86_FORM_VCMPPD_K_X_XM128_IMM8, X86_FORM_VCMPPD_K_Y_YM256_IMM8, X86_FORM_VCMPPD_K_Z_ZM512_IMM8 } },
};
static const uint16_t tier1_select_lane_blends[IR_SIMD_LANE_COUNT][3] = {
    [IR_SIMD_LANE_I32] = { X86_FORM_VPBLENDMD_X_X_XM128, X86_FORM_VPBLENDMD_Y_Y_YM256, X86_FORM_VPBLENDMD_Z_Z_ZM512 },
    [IR_SIMD_LANE_I8] = { X86_FORM_VPBLENDMB_X_X_XM128, X86_FORM_VPBLENDMB_Y_Y_YM256, X86_FORM_VPBLENDMB_Z_Z_ZM512 },
    [IR_SIMD_LANE_I16] = { X86_FORM_VPBLENDMW_X_X_XM128, X86_FORM_VPBLENDMW_Y_Y_YM256, X86_FORM_VPBLENDMW_Z_Z_ZM512 },
    [IR_SIMD_LANE_I64] = { X86_FORM_VPBLENDMQ_X_X_XM128, X86_FORM_VPBLENDMQ_Y_Y_YM256, X86_FORM_VPBLENDMQ_Z_Z_ZM512 },
    [IR_SIMD_LANE_F32] = { X86_FORM_VPBLENDMD_X_X_XM128, X86_FORM_VPBLENDMD_Y_Y_YM256, X86_FORM_VPBLENDMD_Z_Z_ZM512 },
    [IR_SIMD_LANE_F64] = { X86_FORM_VPBLENDMQ_X_X_XM128, X86_FORM_VPBLENDMQ_Y_Y_YM256, X86_FORM_VPBLENDMQ_Z_Z_ZM512 },
};
#undef BUSTER_TIER1_LANE_EVEX
#undef BUSTER_TIER1_LANE_PACKED
#undef BUSTER_TIER1_LANE_VEX
#undef BUSTER_TIER1_LANE_SSE

// A BINARY's form (tier1_select_lane_forms) of operation `operation` (BUSTER_TIER1_VECTOR_*), lanes `lane` and `width`
// 16, 32 or 64 bytes, at `encoding` (BUSTER_TIER1_ENCODING_*: a 64-byte vector's is EVEX's whatever it is):
// X86_FORM_NONE for one x86 hasn't.
static uint16_t tier1_select_lane_form(uint32_t operation, uint32_t lane, uint32_t width, uint32_t encoding)
{
    const uint32_t index = width == 16 ? 0 : width == 32 ? 1 : width == 64 ? 2 : 3;
    const uint32_t level = width < 64 && encoding <= BUSTER_TIER1_ENCODING_SSE ? encoding : BUSTER_TIER1_ENCODING_EVEX;
    return operation < BUSTER_TIER1_VECTOR_OPERATIONS && lane < IR_SIMD_LANE_COUNT && index < 3 ? tier1_select_lane_forms[level][operation][lane][index] : (uint16_t)X86_FORM_NONE;
}

// movaps between two xmm registers: a whole register's copy, which unlike movss's and movsd's doesn't merge into its
// destination's old bits.
static X86Instruction tier1_select_float_move(uint8_t destination, uint8_t source)
{
    return tier1_select_register_record(X86_FORM_MOVAPS_X_XM, destination, source);
}

// A copy of the vector class's value of `width` bytes between two of its registers (#141): a floating value's and a
// 16-byte vector's movaps, and a wider vector's vmovdqa64 of its ymm or zmm register; where `vex` (a target without
// AVX512VL, #1281) a 32-byte vector's vmovdqa of its ymm register.
static X86Instruction tier1_select_class_move(uint8_t destination, uint8_t source, uint32_t width, uint32_t encoding)
{
    return width <= 16 ? tier1_select_float_move(destination, source) : tier1_select_register_record(tier1_select_vector_forms(width, encoding)->move, destination, source);
}

// A copy of an SSE carrier (#1280), a vector of 4 or 8 bytes, between two of the 32 vector registers, zeros past its
// bytes in the destination whatever the source has there: vmovq's of 8 bytes, and for 4 vinsertps's of the source's
// element 0 into element 0 with elements 1 to 3 zeroed (immediate 0x0e), a shuffle that raises no floating exception.
// EVEX forms, as the vector class's moves, or at a target without AVX512VL (`vex`, #1281) their VEX twins, AVX's, which
// name registers below 16 alone, as such a target's rows' roles are (an SSE carrier is admitted as any narrower vector,
// ir_typed_vector_moves).
static X86Instruction tier1_select_carrier_move(uint8_t destination, uint8_t source, uint32_t bytes, uint32_t encoding)
{
    // A carrier needs AVX's moves at least (ir_typed_vector_moves): a legacy SSE target has none.
    const bool vex = encoding != BUSTER_TIER1_ENCODING_EVEX;
    const X86Instruction result = bytes == 8 ? tier1_select_register_record(vex ? X86_FORM_VMOVQ_X_XM64 : X86_FORM_VMOVQ_X_XM64_EVEX, destination, source) :
                                               (X86Instruction){
                                                   .form = vex ? X86_FORM_VINSERTPS_X_X_XM32_IMM8 : X86_FORM_VINSERTPS_X_X_XM32_IMM8_EVEX,
                                                   .reg = destination,
                                                   .vvvv = source,
                                                   .rm = source,
                                                   .mode = BUSTER_X86_REGISTER_DIRECT,
                                                   .immediate = 0x0e,
                                               };
    return result;
}

// TIER1_RULE_INTRINSIC's record: pdep and pext read their source from vvvv and their mask from rm, bzhi and bextr their
// source from rm and their index or control from vvvv (the IR's a is the source, b the other). The bit operations
// (#1374) read their one operand from rm into reg, but bswap, which swaps its rm in place: the destination, where the
// rule's move put the operand.
static X86Instruction tier1_select_intrinsic(const Tier1SelectHoles* holes)
{
    static const uint16_t forms[BUSTER_IR_INTRINSIC_COUNT][2] = {
        [BUSTER_IR_INTRINSIC_PDEP] = { X86_FORM_PDEP_R32_R32_RM32, X86_FORM_PDEP_R64_R64_RM64 },
        [BUSTER_IR_INTRINSIC_PEXT] = { X86_FORM_PEXT_R32_R32_RM32, X86_FORM_PEXT_R64_R64_RM64 },
        [BUSTER_IR_INTRINSIC_BZHI] = { X86_FORM_BZHI_R32_RM32_R32, X86_FORM_BZHI_R64_RM64_R64 },
        [BUSTER_IR_INTRINSIC_BEXTR] = { X86_FORM_BEXTR_R32_RM32_R32, X86_FORM_BEXTR_R64_RM64_R64 },
        [BUSTER_IR_INTRINSIC_TZCNT] = { X86_FORM_TZCNT_R32_RM32, X86_FORM_TZCNT_R64_RM64 },
        [BUSTER_IR_INTRINSIC_BSF] = { X86_FORM_BSF_R32_RM32, X86_FORM_BSF_R64_RM64 },
        [BUSTER_IR_INTRINSIC_LZCNT] = { X86_FORM_LZCNT_R32_RM32, X86_FORM_LZCNT_R64_RM64 },
        [BUSTER_IR_INTRINSIC_BSR] = { X86_FORM_BSR_R32_RM32, X86_FORM_BSR_R64_RM64 },
        [BUSTER_IR_INTRINSIC_POPCNT] = { X86_FORM_POPCNT_R32_RM32, X86_FORM_POPCNT_R64_RM64 },
        [BUSTER_IR_INTRINSIC_BSWAP] = { X86_FORM_BSWAP_R32, X86_FORM_BSWAP_R64 },
    };
    const uint32_t operation = holes->immediate < BUSTER_IR_INTRINSIC_COUNT ? (uint32_t)holes->immediate : BUSTER_IR_INTRINSIC_MULHU;
    const bool deposit = operation == BUSTER_IR_INTRINSIC_PDEP || operation == BUSTER_IR_INTRINSIC_PEXT;
    const bool unary = operation >= BUSTER_IR_INTRINSIC_TZCNT && operation <= BUSTER_IR_INTRINSIC_BSWAP;
    const bool swap = operation == BUSTER_IR_INTRINSIC_BSWAP;
    return (X86Instruction){
        .form = forms[operation][holes->width == 8],
        .reg = swap ? 0 : holes->destination,
        .vvvv = unary ? 0 : deposit ? holes->left : holes->right,
        .rm = swap ? holes->destination : deposit ? holes->right : holes->left,
        .mode = BUSTER_X86_REGISTER_DIRECT,
    };
}

// TIER1_RULE_INTRINSIC's signed overflows (#1385): saddo's add and smulo's imul into the destination, which takes the left
// operand first unless it holds one of them already (both operations commute, and so do their flags), or ssubo's cmp,
// which writes no register; then the destination zeroed by a move, which keeps the flags, and seto into its low byte.
static void tier1_select_overflow(Tier1SelectRecords* result, const Tier1SelectHoles* holes)
{
    const bool wide = holes->width == 8;
    const uint8_t destination = holes->destination;
    if (holes->immediate == BUSTER_IR_INTRINSIC_SSUBO)
    {
        tier1_select_append(result, tier1_select_register_record(wide ? X86_FORM_CMP_R64_RM64 : X86_FORM_CMP_R32_RM32, holes->left, holes->right));
    }
    else
    {
        const bool multiply = holes->immediate == BUSTER_IR_INTRINSIC_SMULO;
        const uint8_t other = destination == holes->right ? holes->left : holes->right;
        if (destination != holes->left && destination != holes->right)
        {
            tier1_select_append(result, tier1_select_move(holes->width, destination, holes->left));
        }
        const uint16_t form = multiply ? (wide ? X86_FORM_IMUL_R64_RM64 : X86_FORM_IMUL_R32_RM32) : (wide ? X86_FORM_ADD_R64_RM64 : X86_FORM_ADD_R32_RM32);
        tier1_select_append(result, tier1_select_register_record(form, destination, other));
    }
    tier1_select_append(result, tier1_select_constant_move(4, destination, 0));
    tier1_select_append(result, (X86Instruction){ .rm = destination, .form = X86_FORM_SETO_RM8 });
}

// A scalar splat (#1450): EVEX broadcasts directly from a general register or a floating scalar's xmm register.
// The baseline sequence copies bits through xmm registers only: movd/movq, low unpacks for bytes/words, pshufd and,
// for AVX's 32-byte result, vinsertf128. It never interprets a NaN or a signed zero as arithmetic.
// A width change's records (#1840), EVEX's (the decoder admits one at a target with AVX512F and VL alone, and the
// emission's allow-list refuses these forms elsewhere): an extract of part 0 the narrower width's move from the source's
// register, which the destination's holds already where they are the same; a widen the narrower width's EVEX move
// (vmovdqa64), which zeroes the wider register past it, even into its own register (legacy SSE's movaps would keep the
// bytes past 16 of a register that held the wide value); an extract of another part vextracti32x4 (16 bytes of 32 or
// 64) or vextracti64x4 (32 of 64); and an insert vinserti32x4 or vinserti64x4 of values[1] into a copy of values[0].
static void tier1_select_width_change(Tier1SelectRecords* result, const Tier1SelectHoles* holes)
{
    const uint32_t kind = (uint32_t)holes->operation;
    const uint8_t x = holes->destination & 15;
    const uint8_t left = holes->left & 15;
    const uint8_t right = holes->right & 15;
    const uint32_t width = holes->width;
    const uint32_t other = holes->source_width;
    const uint32_t part = (uint32_t)holes->immediate;
    if (kind == 2)
    {
        const uint16_t form = other == 32 ? X86_FORM_VINSERTI64X4_Z_Z_YM256_IMM8 : width == 64 ? X86_FORM_VINSERTI32X4_Z_Z_XM128_IMM8 : X86_FORM_VINSERTI32X4_Y_Y_XM128_IMM8;
        tier1_select_append(result, (X86Instruction){ .form = form, .reg = x, .vvvv = left, .rm = right, .mode = BUSTER_X86_REGISTER_DIRECT, .immediate = (int32_t)part });
    }
    else if (kind == 0 && part != 0)
    {
        const uint16_t form = width == 32 ? X86_FORM_VEXTRACTI64X4_YM256_Z_IMM8 : other == 64 ? X86_FORM_VEXTRACTI32X4_XM128_Z_IMM8 : X86_FORM_VEXTRACTI32X4_XM128_Y_IMM8;
        tier1_select_append(result, (X86Instruction){ .form = form, .reg = left, .rm = x, .mode = BUSTER_X86_REGISTER_DIRECT, .immediate = (int32_t)part });
    }
    else if (kind == 1)
    {
        // EVEX's move of the narrower width, never legacy SSE's movaps, which keeps the register's bytes past 16.
        tier1_select_append(result, tier1_select_register_record(tier1_select_vector_forms(other, BUSTER_TIER1_ENCODING_EVEX)->move, x, left));
    }
    else if (x != left)
    {
        tier1_select_append(result, tier1_select_class_move(x, left, width, false));
    }
}

// A permute's EVEX form at `width` bytes (#1840), by its kind past BUSTER_IR_INTRINSIC_PERMUTE and the IR's flags, or
// X86_FORM_NONE where it has none (a doubleword's or a quadword's variable permute of 16 bytes, a quadword's of 32, a
// shuffle of 16-byte lanes of 16 bytes), which leaves its node no candidate: vpermb to vpermq by the lanes' size, vpshufb,
// valignd or valignq by the lanes' size, and vshufi64x2.
static uint16_t tier1_select_permute_form(uint32_t kind, uint32_t flags, uint32_t width)
{
    static const uint16_t permutes[4][3] = {
        { X86_FORM_VPERMB_X_X_XM128, X86_FORM_VPERMB_Y_Y_YM256, X86_FORM_VPERMB_Z_Z_ZM512 },
        { X86_FORM_VPERMW_X_X_XM128, X86_FORM_VPERMW_Y_Y_YM256, X86_FORM_VPERMW_Z_Z_ZM512 },
        { X86_FORM_NONE, X86_FORM_VPERMD_Y_Y_YM256_EVEX, X86_FORM_VPERMD_Z_Z_ZM512 },
        { X86_FORM_NONE, X86_FORM_NONE, X86_FORM_VPERMQ_Z_Z_ZM512 },
    };
    static const uint16_t shuffles[3] = { X86_FORM_VPSHUFB_X_X_XM128_EVEX, X86_FORM_VPSHUFB_Y_Y_YM256_EVEX, X86_FORM_VPSHUFB_Z_Z_ZM512 };
    static const uint16_t aligns[2][3] = {
        { X86_FORM_VALIGND_X_X_XM128_IMM8, X86_FORM_VALIGND_Y_Y_YM256_IMM8, X86_FORM_VALIGND_Z_Z_ZM512_IMM8 },
        { X86_FORM_VALIGNQ_X_X_XM128_IMM8, X86_FORM_VALIGNQ_Y_Y_YM256_IMM8, X86_FORM_VALIGNQ_Z_Z_ZM512_IMM8 },
    };
    static const uint16_t lanes[3] = { X86_FORM_NONE, X86_FORM_VSHUFI64X2_Y_Y_YM256_IMM8, X86_FORM_VSHUFI64X2_Z_Z_ZM512_IMM8 };
    const uint32_t index = width == 16 ? 0 : width == 32 ? 1 : width == 64 ? 2 : 3;
    uint16_t form = X86_FORM_NONE;
    if (index < 3)
    {
        form = kind == 0 && flags <= 3                         ? permutes[flags][index] :
               kind == 1                                       ? shuffles[index] :
               kind == 2 && (flags >> 4 == 2 || flags >> 4 == 3) ? aligns[(flags >> 4) - 2][index] :
               kind == 3                                       ? lanes[index] :
                                                                 X86_FORM_NONE;
    }
    return form;
}

// A permute's record (#1840): its form of values[0] and values[1] into the destination; the variable permutes take their
// indices, values[1], in vvvv and the lanes they move, values[0], in rm, the others values[0] in vvvv and values[1] in rm,
// and align and shuffle_lanes their immediate (an align's count, its flags' low 4 bits).
// The conversions' and the scalar shifts' forms (#1840), shared with the decoder.
#include "buster/compiler/ir_simd_convert_forms.c"

// A shift by a scalar count (#1840, BUSTER_TIER1_VECTOR_SHIFT), from the forms the decoder admits
// (ir_simd_convert_forms.c's ir_simd_shift_forms): a constant count by the immediate form, of the source (rm) into the
// destination (vvvv), any count of 255 or more 255, past every lane's bits, which gives the same 0 or sign; any other
// count by vmovd into the vector scratch register, whose bytes past it the move zeroes, and the form by that xmm count.
static void tier1_select_vector_shift(Tier1SelectRecords* result, const Tier1SelectHoles* holes)
{
    const uint32_t lanes = (uint32_t)holes->operation & 3;
    const uint32_t kind = ((uint32_t)holes->operation >> 2) & 3;
    const uint32_t width = holes->width == 16 ? 0 : holes->width == 32 ? 1 : 2;
    const uint8_t x = holes->destination & 15;
    const uint8_t left = holes->left & 15;
    if (holes->condition != 0)
    {
        const X86Instruction record = {
            .form = ir_simd_shift_immediate_forms[kind][lanes - 1][width],
            .vvvv = x,
            .rm = left,
            .mode = BUSTER_X86_REGISTER_DIRECT,
            .immediate = (int32_t)(holes->immediate < 255 ? holes->immediate : 255),
        };
        tier1_select_append(result, record);
    }
    else
    {
        const uint8_t count = BUSTER_TIER1_SELECT_FLOAT_SCRATCH;
        tier1_select_append(result, tier1_select_register_record(X86_FORM_VMOVD_X_RM32_EVEX, count, holes->right));
        const X86Instruction record = { .form = ir_simd_shift_forms[kind][lanes - 1][width], .reg = x, .vvvv = left, .rm = count, .mode = BUSTER_X86_REGISTER_DIRECT };
        tier1_select_append(result, record);
    }
}

// A mask to a vector or a vector's sign bits to a mask (#1840, BUSTER_TIER1_VECTOR_MASK_EXPAND and _SIGN_MASK), at the
// lanes in `source_width` (#141's element types: a float's are a doubleword's bits, a double's a quadword's): vpmovm2b to
// vpmovm2q of the mask's opmask register into the destination, and vpmovb2m to vpmovq2m of the vector into the
// destination's opmask register. Their doublewords' and quadwords' forms are AVX512DQ's, which every vector comparison of
// the front end needs already; at a target without it the emission's form check refuses the function to Tier 0.
static void tier1_select_mask_vector(Tier1SelectRecords* result, const Tier1SelectHoles* holes)
{
    static const uint16_t expansions[4][3] = {
        { X86_FORM_VPMOVM2B_X_K, X86_FORM_VPMOVM2B_Y_K, X86_FORM_VPMOVM2B_Z_K },
        { X86_FORM_VPMOVM2W_X_K, X86_FORM_VPMOVM2W_Y_K, X86_FORM_VPMOVM2W_Z_K },
        { X86_FORM_VPMOVM2D_X_K, X86_FORM_VPMOVM2D_Y_K, X86_FORM_VPMOVM2D_Z_K },
        { X86_FORM_VPMOVM2Q_X_K, X86_FORM_VPMOVM2Q_Y_K, X86_FORM_VPMOVM2Q_Z_K },
    };
    static const uint16_t signs[4][3] = {
        { X86_FORM_VPMOVB2M_K_X, X86_FORM_VPMOVB2M_K_Y, X86_FORM_VPMOVB2M_K_Z },
        { X86_FORM_VPMOVW2M_K_X, X86_FORM_VPMOVW2M_K_Y, X86_FORM_VPMOVW2M_K_Z },
        { X86_FORM_VPMOVD2M_K_X, X86_FORM_VPMOVD2M_K_Y, X86_FORM_VPMOVD2M_K_Z },
        { X86_FORM_VPMOVQ2M_K_X, X86_FORM_VPMOVQ2M_K_Y, X86_FORM_VPMOVQ2M_K_Z },
    };
    // IrSimdLane's order: I32, I8, I16, I64, F32, F64.
    static const uint8_t sizes[IR_SIMD_LANE_COUNT] = { 2, 0, 1, 3, 2, 3 };
    const uint32_t size = holes->source_width < IR_SIMD_LANE_COUNT ? sizes[holes->source_width] : 2;
    const uint32_t width = holes->width == 16 ? 0 : holes->width == 32 ? 1 : 2;
    const bool expand = holes->scale == BUSTER_TIER1_VECTOR_MASK_EXPAND;
    const X86Instruction record = expand ? tier1_select_register_record(expansions[size][width], holes->destination & 15, holes->left & 7) :
                                           tier1_select_register_record(signs[size][width], holes->destination & 7, holes->left & 15);
    tier1_select_append(result, record);
}

// Whether `value` is a vector constant of every byte zero (0) or every bit set (1) over `bytes`; 2 for any other value.
static uint32_t tier1_select_vector_fill(const IrFunction* function, uint32_t value, uint32_t bytes)
{
    uint32_t fill = 2;
    if (value < function->instruction_count && function->op[value] == IR_VCONST && function->a[value] < function->vector_constant_count)
    {
        const IrSimdValue* payload = &function->vector_constants[function->a[value]];
        bool zero = true;
        bool ones = true;
        for (uint32_t word = 0; word < bytes / 8; word += 1)
        {
            zero &= payload->words[word] == 0;
            ones &= payload->words[word] == UINT64_MAX;
        }
        fill = zero ? 0 : ones ? 1 : 2;
    }
    return fill;
}

static void tier1_select_permute(Tier1SelectRecords* result, const Tier1SelectHoles* holes)
{
    const uint32_t kind = (uint32_t)holes->operation;
    const uint32_t flags = (uint32_t)holes->immediate;
    const uint8_t left = holes->left & 15;
    const uint8_t right = holes->right & 15;
    const X86Instruction record = {
        .form = tier1_select_permute_form(kind, flags, holes->width),
        .reg = holes->destination & 15,
        .vvvv = kind == 0 ? right : left,
        .rm = kind == 0 ? left : right,
        .mode = BUSTER_X86_REGISTER_DIRECT,
        .immediate = kind == 2 ? (int32_t)(flags & 15) : kind == 3 ? (int32_t)flags : 0,
    };
    tier1_select_append(result, record);
}

// A permute of two tables (#1840): vpermt2b to vpermt2q (ir_simd_permute2_forms) overwrite their first table, and
// vpermi2b to vpermi2q (ir_simd_permute2_index_forms) their indices. Where the destination's register holds the first
// table, the first; where it holds the indices, the second; otherwise the destination takes the first table by a move
// first, the vector class's scratch register keeping the second table across it where the destination holds that.
static void tier1_select_permute2(Tier1SelectRecords* result, const Tier1SelectHoles* holes, uint8_t destination)
{
    const uint8_t x = destination & 15;
    const uint8_t first = holes->left & 15;
    const uint8_t indices = holes->right & 15;
    uint8_t table = holes->third & 15;
    const uint32_t width = holes->width;
    const uint32_t at = width == 16 ? 0 : width == 32 ? 1 : 2;
    if (x != first && x == indices)
    {
        const X86Instruction record = {
            .form = ir_simd_permute2_index_forms[holes->immediate & 3][at],
            .reg = x,
            .vvvv = first,
            .rm = table,
            .mode = BUSTER_X86_REGISTER_DIRECT,
        };
        tier1_select_append(result, record);
    }
    else if (x != first)
    {
        if (x == table)
        {
            tier1_select_append(result, tier1_select_class_move(BUSTER_TIER1_SELECT_FLOAT_SCRATCH, x, width, false));
            table = BUSTER_TIER1_SELECT_FLOAT_SCRATCH;
        }
        tier1_select_append(result, tier1_select_class_move(x, first, width, false));
    }
    if (x == first || x != indices)
    {
        const X86Instruction record = {
            .form = ir_simd_permute2_forms[holes->immediate & 3][at],
            .reg = x,
            .vvvv = indices,
            .rm = table,
            .mode = BUSTER_X86_REGISTER_DIRECT,
        };
        tier1_select_append(result, record);
    }
}

// A bitwise function of three vectors (#1840): vpternlogd (ir_simd_ternlog_forms) computes table bit (reg << 2 | vvvv << 1
// | rm) into its first operand's register. The operand whose register the destination already holds is the first, the
// table's bits permuted for the new order; where it holds none of them, the first operand is moved into it first.
static void tier1_select_ternlog(Tier1SelectRecords* result, const Tier1SelectHoles* holes, uint8_t destination)
{
    const uint8_t x = destination & 15;
    const uint8_t registers[3] = { (uint8_t)(holes->left & 15), (uint8_t)(holes->right & 15), (uint8_t)(holes->third & 15) };
    // The operands in reg, vvvv and rm, by their places in the IR (a, b, c).
    uint32_t order[3] = { 0, 1, 2 };
    if (x != registers[0] && x == registers[1])
    {
        order[0] = 1;
        order[1] = 0;
    }
    else if (x != registers[0] && x == registers[2])
    {
        order[0] = 2;
        order[2] = 0;
    }
    else if (x != registers[0])
    {
        tier1_select_append(result, tier1_select_class_move(x, registers[0], holes->width, false));
    }
    const uint32_t table = (uint32_t)holes->immediate;
    uint32_t permuted = 0;
    for (uint32_t index = 0; index < 8; index += 1)
    {
        uint32_t bits[3] = { 0, 0, 0 };
        bits[order[0]] = index >> 2 & 1;
        bits[order[1]] = index >> 1 & 1;
        bits[order[2]] = index & 1;
        permuted |= (table >> (bits[0] << 2 | bits[1] << 1 | bits[2]) & 1) << index;
    }
    const X86Instruction record = {
        .form = ir_simd_ternlog_forms[0][holes->width == 16 ? 0 : holes->width == 32 ? 1 : 2],
        .reg = x,
        .vvvv = registers[order[1]],
        .rm = registers[order[2]],
        .mode = BUSTER_X86_REGISTER_DIRECT,
        .immediate = (int32_t)permuted,
    };
    tier1_select_append(result, record);
}

// A compress or an expand (#1840): vpcompressb to vpcompressq (the destination in rm) and vpexpandb to vpexpandq (in reg)
// merge into their destination under the mask, so it takes the passthrough first by a move where it isn't already its
// register; where it holds the source instead, the vector class's scratch register keeps the source across that move.
static void tier1_select_compress(Tier1SelectRecords* result, const Tier1SelectHoles* holes, uint8_t destination)
{
    const uint8_t x = destination & 15;
    uint8_t source = holes->left & 15;
    const uint8_t passthrough = holes->third & 15;
    const uint32_t width = holes->width;
    const uint32_t at = width == 16 ? 0 : width == 32 ? 1 : 2;
    if (x != passthrough)
    {
        if (x == source)
        {
            tier1_select_append(result, tier1_select_class_move(BUSTER_TIER1_SELECT_FLOAT_SCRATCH, x, width, false));
            source = BUSTER_TIER1_SELECT_FLOAT_SCRATCH;
        }
        tier1_select_append(result, tier1_select_class_move(x, passthrough, width, false));
    }
    const bool expand = holes->operation != 0;
    const X86Instruction record = {
        .form = expand ? ir_simd_expand_forms[holes->immediate & 3][at] : ir_simd_compress_forms[holes->immediate & 3][at],
        .reg = expand ? x : source,
        .rm = expand ? source : x,
        .mode = BUSTER_X86_REGISTER_DIRECT,
        .decorations = (uint8_t)(holes->right & BUSTER_X86_DECORATION_MASK),
    };
    tier1_select_append(result, record);
}

// A masked load or store (#1840, TIER1_RULE_FLOAT_LOAD's or _STORE's of `condition` nonzero): vmovdqu8 to vmovdqu64 by
// the lanes, under the mask's k register. A load merges into the destination, which takes the passthrough first unless
// it holds it; a store writes the vector in `third` to the address alone.
static void tier1_select_masked_move(Tier1SelectRecords* result, const Tier1SelectHoles* holes, uint8_t destination)
{
    const uint32_t width = holes->width;
    const uint32_t at = width == 16 ? 0 : width == 32 ? 1 : 2;
    const bool store = holes->rule == TIER1_RULE_FLOAT_STORE;
    const uint8_t x = destination & 15;
    const uint8_t passthrough = holes->third & 15;
    if (!store && x != passthrough)
    {
        tier1_select_append(result, tier1_select_class_move(x, passthrough, width, false));
    }
    X86Instruction record = holes->memory;
    record.form = ir_simd_masked_moves[(holes->condition - 1u) & 3][store][at];
    record.reg = store ? passthrough : x;
    record.decorations = (uint8_t)(holes->right & BUSTER_X86_DECORATION_MASK);
    tier1_select_append(result, record);
}

// One lane of a vector or its lanes' sign bits (#1840, BUSTER_TIER1_VECTOR_LANE_EXTRACT, _LANE_INSERT and _MOVEMASK).
static void tier1_select_lane(Tier1SelectRecords* result, const Tier1SelectHoles* holes, uint8_t destination)
{
    const uint32_t width = holes->width;
    const uint32_t flags = (uint32_t)holes->immediate;
    const uint32_t size = flags & 3;
    const uint32_t index = flags >> 2;
    const uint32_t per = 16u >> size;
    const uint32_t part = index / per;
    const uint32_t sub = index % per;
    const uint8_t x = destination & 15;
    const uint8_t scratch = BUSTER_TIER1_SELECT_FLOAT_SCRATCH;
    const uint32_t kind = holes->scale;
    if (kind == BUSTER_TIER1_VECTOR_MOVEMASK)
    {
        tier1_select_append(result, tier1_select_register_record(ir_simd_movemask_forms[size][width == 32], x, holes->left & 15));
    }
    else if (kind == BUSTER_TIER1_VECTOR_LANE_EXTRACT)
    {
        uint8_t source = holes->left & 15;
        if (part != 0)
        {
            const X86Instruction extract = { .form = ir_simd_part_extract_forms[width == 64], .reg = source, .rm = scratch, .mode = BUSTER_X86_REGISTER_DIRECT, .immediate = part };
            tier1_select_append(result, extract);
            source = scratch;
        }
        const bool whole = sub == 0 && size >= 2;
        const uint16_t form = whole ? (size == 2 ? X86_FORM_VMOVD_RM32_X_EVEX : X86_FORM_VMOVQ_RM64_X_EVEX) : ir_simd_lane_extract_forms[size];
        const bool word = !whole && size == 1;
        const X86Instruction record = { .form = form, .reg = word ? x : source, .rm = word ? source : x, .mode = BUSTER_X86_REGISTER_DIRECT, .immediate = whole ? 0 : sub };
        tier1_select_append(result, record);
    }
    else
    {
        const uint8_t general = holes->right & 15;
        if (x != (holes->left & 15))
        {
            tier1_select_append(result, tier1_select_class_move(x, holes->left & 15, width, false));
        }
        const bool low = width == 16 && part == 0;
        const uint8_t target = low ? x : scratch;
        if (!low)
        {
            const X86Instruction extract = { .form = ir_simd_part_extract_forms[width == 64], .reg = x, .rm = scratch, .mode = BUSTER_X86_REGISTER_DIRECT, .immediate = part };
            tier1_select_append(result, extract);
        }
        const X86Instruction insert = { .form = ir_simd_lane_insert_forms[size], .reg = target, .vvvv = target, .rm = general, .mode = BUSTER_X86_REGISTER_DIRECT, .immediate = sub };
        tier1_select_append(result, insert);
        if (!low)
        {
            const X86Instruction back = { .form = ir_simd_part_insert_forms[width == 64], .reg = x, .vvvv = x, .rm = scratch, .mode = BUSTER_X86_REGISTER_DIRECT, .immediate = part };
            tier1_select_append(result, back);
        }
    }
}

// A conversion's EVEX form (#1840), by its kind past BUSTER_IR_INTRINSIC_EXTEND, the IR's flags, the result's bytes and
// the source's: the decoder's tables' (ir_simd_typed_extensions at the result's width, ir_simd_typed_truncations at the
// source's), or X86_FORM_NONE for a pair the flags don't name.
static uint16_t tier1_select_convert_form(uint32_t kind, uint32_t flags, uint32_t width, uint32_t source_width)
{
    const uint32_t from = flags & 3;
    const uint32_t to = (flags >> 2) & 3;
    const uint32_t mode = flags >> 4;
    const uint32_t bytes = kind == 0 ? width : source_width;
    const uint32_t index = bytes == 16 ? 0 : bytes == 32 ? 1 : bytes == 64 ? 2 : 3;
    uint16_t form = X86_FORM_NONE;
    for (uint32_t pair = 0; index < 3 && pair < 6; pair += 1)
    {
        const uint8_t* sizes = kind == 0 ? ir_simd_typed_extension_pairs[pair] : ir_simd_typed_truncation_pairs[pair];
        if (sizes[0] == from && sizes[1] == to)
        {
            form = kind == 0 && mode <= 1 ? ir_simd_typed_extensions[mode][pair][index] : kind == 1 && mode <= 2 ? ir_simd_typed_truncations[mode][pair][index] : X86_FORM_NONE;
        }
    }
    return form;
}

static void tier1_select_scalar_splat(Tier1SelectRecords* result, const Tier1SelectHoles* holes)
{
    const uint32_t type = holes->source_width;
    const uint32_t bytes = ir_splat_scalar_type(type) ? ir_type_bits[type] / 8 : 0;
    const uint8_t destination = holes->destination & 15;
    const bool floating = type == IR_F32 || type == IR_F64;
    const uint8_t source = floating ? holes->left & 15 : holes->left;
    if (holes->width < 64 && (holes->sse || holes->vex))
    {
        const bool vex = !holes->sse;
        uint8_t from = source;
        if (!floating)
        {
            const uint16_t move = bytes == 8 ? (vex ? X86_FORM_VMOVQ_X_RM64 : X86_FORM_MOVQ_X_RM64) :
                                              (vex ? X86_FORM_VMOVD_X_RM32 : X86_FORM_MOVD_X_RM32);
            tier1_select_append(result, tier1_select_register_record(move, destination, source));
            from = destination;
            for (uint32_t element = bytes; element < 4 && element != 0; element *= 2)
            {
                const uint16_t form = element == 1 ? (vex ? X86_FORM_VPUNPCKLBW_X_X_XM128 : X86_FORM_PUNPCKLBW_X_XM128) :
                                                     (vex ? X86_FORM_VPUNPCKLWD_X_X_XM128 : X86_FORM_PUNPCKLWD_X_XM128);
                tier1_select_append(result, (X86Instruction){
                    .form = form,
                    .reg = destination,
                    .vvvv = vex ? destination : 0,
                    .rm = destination,
                    .mode = BUSTER_X86_REGISTER_DIRECT,
                });
            }
        }
        X86Instruction shuffle = tier1_select_register_record(vex ? X86_FORM_VPSHUFD_X_XM128_IMM8 : X86_FORM_PSHUFD_X_XM128_IMM8, destination, from);
        shuffle.immediate = bytes == 8 ? 0x44 : 0;
        tier1_select_append(result, shuffle);
        if (holes->width == 32)
        {
            tier1_select_append(result, (X86Instruction){
                .form = X86_FORM_VINSERTF128_Y_Y_XM128_IMM8,
                .reg = destination,
                .vvvv = destination,
                .rm = destination,
                .mode = BUSTER_X86_REGISTER_DIRECT,
                .immediate = 1,
            });
        }
    }
    else
    {
        static const uint16_t forms[IR_TYPE_COUNT][3] = {
            [IR_I8] = { X86_FORM_VPBROADCASTB_X_R32, X86_FORM_VPBROADCASTB_Y_R32, X86_FORM_VPBROADCASTB_Z_R32 },
            [IR_I16] = { X86_FORM_VPBROADCASTW_X_R32, X86_FORM_VPBROADCASTW_Y_R32, X86_FORM_VPBROADCASTW_Z_R32 },
            [IR_I32] = { X86_FORM_VPBROADCASTD_X_R32, X86_FORM_VPBROADCASTD_Y_R32, X86_FORM_VPBROADCASTD_Z_R32 },
            [IR_I64] = { X86_FORM_VPBROADCASTQ_X_R64, X86_FORM_VPBROADCASTQ_Y_R64, X86_FORM_VPBROADCASTQ_Z_R64 },
            [IR_F32] = { X86_FORM_VBROADCASTSS_X_XM32_EVEX, X86_FORM_VBROADCASTSS_Y_XM32_EVEX, X86_FORM_VBROADCASTSS_Z_XM32 },
            [IR_F64] = { X86_FORM_VPBROADCASTQ_X_XM64_EVEX, X86_FORM_VBROADCASTSD_Y_XM64_EVEX, X86_FORM_VBROADCASTSD_Z_XM64 },
        };
        const uint32_t width = holes->width == 16 ? 0 : holes->width == 32 ? 1 : 2;
        tier1_select_append(result, tier1_select_register_record(bytes != 0 ? forms[type][width] : X86_FORM_NONE, destination, source));
    }
}

// A 16-byte vector's records at a target without AVX (#141's SSE targets, Tier1SelectHoles.sse): legacy SSE2's
// two-operand forms, the destination's the first operand. Zeros by pxor and every bit by pcmpeqd of the destination with
// itself, a splat's lane by mov into the scratch general register, movd and pshufd, a copy by movaps, and a BINARY by its
// form (tier1_select_lane_form's legacy column) after its left operand's copy into the destination: a commutative
// integer operation's operands swapped where the right one is in the destination, and any other's right one copied into
// xmm15, the class's scratch, first. A floating operation is never swapped, so that a NaN result is its first operand's,
// as the IR's is (ir_simd_float_operation). A NOT is pxor with pcmpeqd's every bit, in the destination where the operand
// is elsewhere, in xmm15 where it is the destination (#1425's review). COMPARE and SELECT aren't here: their masks are
// opmasks, which no SSE target has, so they take AVX-512's forms, which the target's allow-list refuses at emission
// (TIER1_EMIT_TARGET), as at a VEX target.
static void tier1_select_sse_vector(Tier1SelectRecords* result, const Tier1SelectHoles* holes, uint8_t destination)
{
    const uint8_t x = destination & 15;
    uint8_t left = holes->left & 15;
    uint8_t right = holes->right & 15;
    const uint32_t kind = holes->scale;
    if (kind == BUSTER_TIER1_VECTOR_ZERO || kind == BUSTER_TIER1_VECTOR_ONES)
    {
        tier1_select_append(result, tier1_select_register_record(kind == BUSTER_TIER1_VECTOR_ZERO ? X86_FORM_PXOR_X_XM128 : X86_FORM_PCMPEQD_X_XM128, x, x));
    }
    else if (kind == BUSTER_TIER1_VECTOR_SPLAT)
    {
        tier1_select_request(result, X86_OPERATION_MOV, 4, BUSTER_TIER1_SELECT_SCRATCH, (int64_t)(uint32_t)holes->immediate, 0);
        tier1_select_append(result, tier1_select_register_record(X86_FORM_MOVD_X_RM32, x, BUSTER_TIER1_SELECT_SCRATCH));
        tier1_select_append(result, tier1_select_register_record(X86_FORM_PSHUFD_X_XM128_IMM8, x, x));
    }
    else if (kind == BUSTER_TIER1_VECTOR_COPY && x != left)
    {
        tier1_select_append(result, tier1_select_float_move(x, left));
    }
    else if (kind == BUSTER_TIER1_VECTOR_NOT)
    {
        const uint8_t ones = x != left ? x : BUSTER_TIER1_SELECT_FLOAT_SCRATCH;
        tier1_select_append(result, tier1_select_register_record(X86_FORM_PCMPEQD_X_XM128, ones, ones));
        tier1_select_append(result, tier1_select_register_record(X86_FORM_PXOR_X_XM128, x, x != left ? left : ones));
    }
    else if (kind == BUSTER_TIER1_VECTOR_BINARY)
    {
        const uint32_t operation = holes->operation;
        const bool commutative = operation == BUSTER_TIER1_VECTOR_ADD || operation == BUSTER_TIER1_VECTOR_MUL ||
                                 (operation >= BUSTER_TIER1_VECTOR_AND && operation <= BUSTER_TIER1_VECTOR_XOR);
        if (x == right && left != right)
        {
            if (commutative)
            {
                right = left;
                left = x;
            }
            else
            {
                tier1_select_append(result, tier1_select_float_move(BUSTER_TIER1_SELECT_FLOAT_SCRATCH, right));
                right = BUSTER_TIER1_SELECT_FLOAT_SCRATCH;
            }
        }
        if (x != left)
        {
            tier1_select_append(result, tier1_select_float_move(x, left));
        }
        tier1_select_append(result, tier1_select_register_record(tier1_select_lane_form(operation, holes->source_width, 16, BUSTER_TIER1_ENCODING_SSE), x, right));
    }
}

// A load's or a store's segment override (#1447, Tier1SelectHoles.segment) on its access's records: each with a memory
// operand, as Tier 0's tier0_apply_segments gives its own (form + segment * X86_FORM_COUNT, the encoder's form with the
// override). A load's, a store's or an xchg's expansion has one such record, the access; its other records are moves of
// registers. A RIP-relative operand takes it too: `%gs:sym(%rip)` is clang's access of a global in the segment. A form
// without SS's (x86_form_has_segment: APX's EVEX forms, which no access's pattern selects) becomes NONE, which the
// target's validation refuses, rather than an access without its override.
static void tier1_select_segment(Tier1SelectRecords* records, uint32_t segment)
{
    for (uint32_t r = 0; r < records->count; r += 1)
    {
        X86Instruction* record = &records->records[r];
        if ((record->mode & BUSTER_X86_MODE_MEMORY) != 0 && record->form < X86_FORM_COUNT)
        {
            record->form = x86_form_has_segment(record->form, segment) ? (uint16_t)(record->form + segment * X86_FORM_COUNT) : (uint16_t)X86_FORM_NONE;
        }
    }
}

static Tier1SelectRecords tier1_select_expand(const Tier1SelectHoles* holes)
{
    Tier1SelectRecords result = {0};
    const uint8_t destination = holes->destination;
    const uint32_t width = holes->width;
    for (uint32_t r = 0; r < holes->nops; r += 1)
    {
        tier1_select_append(&result, (X86Instruction){ .form = X86_FORM_NOP });
    }
    switch (holes->rule)
    {
    case TIER1_RULE_LEAF:
    case TIER1_RULE_NONE:
    case TIER1_RULE_LIFETIME:
        break;
    case TIER1_RULE_CONSTANT:
        tier1_select_request(&result, X86_OPERATION_MOV, width, destination, (int64_t)holes->immediate, 0);
        break;
    case TIER1_RULE_COPY:
        // A copy in its own register is nothing. A 32-bit value's upper half is unspecified (tier1_select_width), so a
        // copy of an I32 has nothing to clear. A narrow one's bits past its width are what a narrow source's canonical
        // column says (tier1_select_canonical), and a 32-bit move wouldn't clear those either.
        if (destination != holes->left)
        {
            tier1_select_append(&result, tier1_select_move(width, destination, holes->left));
        }
        break;
    case TIER1_RULE_TRUNCATE:
    case TIER1_RULE_ZERO_EXTEND:
    {
        // Zero extensions and narrow truncations always emit: even in place they clear the bits past the width. A
        // truncation to I32 is a copy, nothing in its own register, since the upper half it would clear is unspecified.
        // Narrow zero extensions never reach this case from the planner; they select COPY above.
        const uint32_t source_width = holes->rule == TIER1_RULE_TRUNCATE ? width : holes->source_width;
        const uint16_t form = source_width == 1 ? X86_FORM_MOVZX_R32_RM8 : source_width == 2 ? X86_FORM_MOVZX_R32_RM16 : X86_FORM_MOV_R32_RM32;
        if (holes->rule == TIER1_RULE_ZERO_EXTEND || width != 4 || destination != holes->left)
        {
            tier1_select_append(&result, tier1_select_register_record(form, destination, holes->left));
        }
        break;
    }
    case TIER1_RULE_SIGN_EXTEND:
    {
        const uint16_t form = holes->source_width == 4 ? X86_FORM_MOVSXD_R64_RM32 : holes->source_width == 2 ? (width == 8 ? X86_FORM_MOVSX_R64_RM16 : X86_FORM_MOVSX_R32_RM16) : (width == 8 ? X86_FORM_MOVSX_R64_RM8 : X86_FORM_MOVSX_R32_RM8);
        tier1_select_append(&result, tier1_select_register_record(form, destination, holes->left));
        if (width == 2)
        {
            // I8 -> I16 has sixteen significant bits. Clear the rest of its canonical 32-bit storage value.
            tier1_select_append(&result, tier1_select_register_record(X86_FORM_MOVZX_R32_RM16, destination, destination));
        }
        break;
    }
    case TIER1_RULE_UNARY:
        if (destination != holes->left)
        {
            tier1_select_append(&result, tier1_select_move(width, destination, holes->left));
        }
        tier1_select_append(&result, (X86Instruction)
        {
            .rm = destination,
            .form = holes->operation == X86_OPERATION_NEG ? (width == 8 ? X86_FORM_NEG_RM64 : width == 2 ? X86_FORM_NEG_RM16 : width == 1 ? X86_FORM_NEG_RM8 : X86_FORM_NEG_RM32) :
                                                          (width == 8 ? X86_FORM_NOT_RM64 : width == 2 ? X86_FORM_NOT_RM16 : width == 1 ? X86_FORM_NOT_RM8 : X86_FORM_NOT_RM32),
        });
        break;
    case TIER1_RULE_REGISTER:
    {
        uint8_t left = holes->left;
        uint8_t right = holes->right;
        if (destination == right && left != right)
        {
            if (tier1_select_commutative(holes->operation))
            {
                right = left;
                left = destination;
            }
            else
            {
                tier1_select_append(&result, tier1_select_move(width, BUSTER_TIER1_SELECT_SCRATCH, right));
                right = BUSTER_TIER1_SELECT_SCRATCH;
            }
        }
        if (destination != left)
        {
            tier1_select_append(&result, tier1_select_move(width, destination, left));
        }
        tier1_select_append(&result, tier1_select_register_record(tier1_select_binary_form(holes->operation, width), destination, right));
        break;
    }
    case TIER1_RULE_IMMEDIATE:
    case TIER1_RULE_MULTIPLY_SHIFT:
    {
        if (holes->operation == X86_OPERATION_IMUL && holes->rule == TIER1_RULE_IMMEDIATE)
        {
            tier1_select_append(&result, tier1_select_multiply_immediate(width, destination, holes->left, holes->immediate));
        }
        else
        {
            if (destination != holes->left)
            {
                tier1_select_append(&result, tier1_select_move(width, destination, holes->left));
            }
            const bool multiply_shift = holes->rule == TIER1_RULE_MULTIPLY_SHIFT;
            const int64_t immediate = multiply_shift ? (int64_t)holes->scale : (int64_t)holes->immediate;
            const X86Operation operation = multiply_shift ? X86_OPERATION_SHL : holes->operation;
            tier1_select_request(&result, operation, width, destination, immediate, 0);
        }
        break;
    }
    case TIER1_RULE_LEA_ADD:
    case TIER1_RULE_LEA_SHIFT:
    case TIER1_RULE_MULTIPLY_LEA:
    case TIER1_RULE_ADDRESS:
    {
        X86Instruction record = holes->memory;
        // A coalesced base + index needs no separate destination: add has the same value and avoids LEA's SIB byte
        // (#1217's table-offset addition). Flags never cross an IR node boundary. An rsp base retains LEA; the CFG
        // consumer resolves folded frame operands to their final base and displacement before this decision.
        const bool add = record.mode == BUSTER_X86_MEMORY_BASE_INDEX(0) && record.displacement == 0 && record.rm != X86_REGISTER_RSP &&
                         (destination == record.rm || destination == record.index);
        if (add)
        {
            const uint8_t other = destination == record.rm ? record.index : record.rm;
            record = tier1_select_register_record(width == 8 ? X86_FORM_ADD_R64_RM64 : X86_FORM_ADD_R32_RM32, destination, other);
        }
        else
        {
            record.reg = destination;
            record.form = width == 8 ? X86_FORM_LEA_R64_M : X86_FORM_LEA_R32_M;
        }
        tier1_select_append(&result, record);
        break;
    }
    case TIER1_RULE_LOAD:
    case TIER1_RULE_LOAD_ADDRESS:
    {
        X86Instruction record = holes->memory;
        record.reg = destination;
        record.form = width == 1 ? X86_FORM_MOVZX_R32_RM8 : width == 2 ? X86_FORM_MOVZX_R32_RM16 : width == 8 ? X86_FORM_MOV_R64_RM64 : X86_FORM_MOV_R32_RM32;
        if (holes->operation == X86_OPERATION_MOVSX)
        {
            record.form = width == 4 ? X86_FORM_MOVSXD_R64_RM32 : width == 2 ? (holes->source_width == 8 ? X86_FORM_MOVSX_R64_RM16 : X86_FORM_MOVSX_R32_RM16) :
                                                                                         (holes->source_width == 8 ? X86_FORM_MOVSX_R64_RM8 : X86_FORM_MOVSX_R32_RM8);
        }
        tier1_select_append(&result, record);
        break;
    }
    case TIER1_RULE_LOAD_OPERAND:
    {
        X86Instruction memory = holes->memory;
        // Moving the left input must not destroy the address before its one memory access.
        const bool aliases_base = (memory.mode & BUSTER_X86_MODE_BASE) != 0 && memory.rm == destination;
        const bool aliases_index = (memory.mode & BUSTER_X86_MODE_INDEX) != 0 && memory.index == destination;
        if (destination != holes->left && (aliases_base || aliases_index))
        {
            tier1_select_append(&result, tier1_select_move(8, BUSTER_TIER1_SELECT_SCRATCH, destination));
            memory.rm = aliases_base ? BUSTER_TIER1_SELECT_SCRATCH : memory.rm;
            memory.index = aliases_index ? BUSTER_TIER1_SELECT_SCRATCH : memory.index;
        }
        if (destination != holes->left)
        {
            tier1_select_append(&result, tier1_select_move(width, destination, holes->left));
        }
        memory.reg = destination;
        memory.form = tier1_select_binary_form(holes->operation, width);
        tier1_select_append(&result, memory);
        break;
    }
    case TIER1_RULE_STORE:
    case TIER1_RULE_STORE_ADDRESS:
    {
        X86Instruction record = holes->memory;
        record.reg = holes->right;
        record.form = width == 1 ? X86_FORM_MOV_RM8_R8 : width == 2 ? X86_FORM_MOV_RM16_R16 : width == 8 ? X86_FORM_MOV_RM64_R64 : X86_FORM_MOV_RM32_R32;
        if (holes->operation == X86_OPERATION_XCHG)
        {
            // The desired SSA value can remain live after this store. Xchg returns the discarded old value in the
            // reserved scratch instead; keep the memory record last for a frame address's displacement patch.
            tier1_select_append(&result, tier1_select_move(8, BUSTER_TIER1_SELECT_SCRATCH, holes->right));
            record.reg = BUSTER_TIER1_SELECT_SCRATCH;
            record.form = width == 1 ? X86_FORM_XCHG_RM8_R8 : width == 2 ? X86_FORM_XCHG_RM16_R16 : width == 8 ? X86_FORM_XCHG_RM64_R64 : X86_FORM_XCHG_RM32_R32;
        }
        tier1_select_append(&result, record);
        break;
    }
    case TIER1_RULE_COMPARE:
    case TIER1_RULE_COMPARE_IMMEDIATE:
    {
        // A zeroed destination stays canonical when setcc writes its low byte. Clear before the comparison where the
        // destination is independent of its inputs. An aliased input must be read first; mov-immediate then preserves
        // the flags, unlike the xor zero idiom. Neither sequence depends on a partial-register result's movzx.
        const bool aliases = holes->compare_memory ?
                                 ((!holes->compare_immediate && destination == holes->left) ||
                                  ((holes->memory.mode & BUSTER_X86_MODE_BASE) != 0 && destination == holes->memory.rm) ||
                                  ((holes->memory.mode & BUSTER_X86_MODE_INDEX) != 0 && destination == holes->memory.index)) :
                                 destination == holes->left || (!holes->compare_immediate && destination == holes->right);
        if (!aliases)
        {
            tier1_select_append(&result, tier1_select_register_record(X86_FORM_XOR_R32_RM32, destination, destination));
        }
        tier1_select_compare(&result, holes);
        if (aliases)
        {
            tier1_select_append(&result, tier1_select_constant_move(4, destination, 0));
        }
        tier1_select_append(&result, (X86Instruction){ .rm = destination, .form = tier1_select_condition_sets[holes->condition] });
        break;
    }
    case TIER1_RULE_BRANCH_COMPARE:
        tier1_select_compare(&result, holes);
        tier1_select_append(&result, (X86Instruction){ .form = tier1_select_condition_branches[holes->condition] });
        tier1_select_append(&result, (X86Instruction){ .form = X86_FORM_JMP_REL8 });
        break;
    case TIER1_RULE_BRANCH:
        tier1_select_append(&result, tier1_select_register_record(X86_FORM_TEST_RM32_R32, holes->left, holes->left));
        tier1_select_append(&result, (X86Instruction){ .form = X86_FORM_JNE_REL8 });
        tier1_select_append(&result, (X86Instruction){ .form = X86_FORM_JMP_REL8 });
        break;
    case TIER1_RULE_SELECT:
    {
        tier1_select_append(&result, tier1_select_register_record(X86_FORM_TEST_RM32_R32, holes->left, holes->left));
        const bool inverse = destination == holes->right;
        const uint8_t initial = inverse ? holes->right : holes->third;
        const uint8_t alternate = inverse ? holes->third : holes->right;
        if (destination != initial)
        {
            tier1_select_append(&result, tier1_select_move(width, destination, initial));
        }
        const uint16_t form = width == 8 ? (inverse ? X86_FORM_CMOVE_R64_RM64 : X86_FORM_CMOVNE_R64_RM64) : (inverse ? X86_FORM_CMOVE_R32_RM32 : X86_FORM_CMOVNE_R32_RM32);
        tier1_select_append(&result, tier1_select_register_record(form, destination, alternate));
        break;
    }
    case TIER1_RULE_JUMP:
        tier1_select_append(&result, (X86Instruction){ .form = X86_FORM_JMP_REL8 });
        break;
    case TIER1_RULE_JUMP_INDIRECT:
        tier1_select_append(&result, (X86Instruction){ .form = X86_FORM_JMP_RM64, .rm = holes->left });
        break;
    case TIER1_RULE_RETURN:
        if (width != 0 && holes->left != X86_REGISTER_RAX)
        {
            tier1_select_append(&result, tier1_select_move(width, X86_REGISTER_RAX, holes->left));
        }
        tier1_select_append(&result, (X86Instruction){ .form = X86_FORM_RET });
        break;
    case TIER1_RULE_UNREACHABLE:
        tier1_select_append(&result, (X86Instruction){ .form = X86_FORM_UD2 });
        break;
    case TIER1_RULE_INTRINSIC:
        if (holes->immediate >= BUSTER_IR_INTRINSIC_SADDO && holes->immediate <= BUSTER_IR_INTRINSIC_SMULO)
        {
            tier1_select_overflow(&result, holes);
        }
        else
        {
            if (holes->immediate == BUSTER_IR_INTRINSIC_BSWAP && destination != holes->left)
            {
                tier1_select_append(&result, tier1_select_move(width, destination, holes->left));
            }
            tier1_select_append(&result, tier1_select_intrinsic(holes));
        }
        break;
    case TIER1_RULE_FENCE:
        if (holes->immediate == BUSTER_IR_FENCE_SEQ_CST)
        {
            tier1_select_append(&result, (X86Instruction){ .form = X86_FORM_MFENCE });
        }
        break;
    case TIER1_RULE_ATOMIC_RMW:
        if (holes->immediate == BUSTER_IR_RMW_COMPARE)
        {
            const uint8_t scratch = BUSTER_TIER1_SELECT_SCRATCH;
            const uint8_t address = holes->left == X86_REGISTER_RAX ? scratch : holes->left;
            const uint8_t expected = holes->right == X86_REGISTER_RAX ? scratch : holes->right;
            const uint8_t desired = holes->third == X86_REGISTER_RAX ? scratch : holes->third;
            tier1_select_append(&result, tier1_select_move(8, scratch, X86_REGISTER_RAX));
            tier1_select_append(&result, tier1_select_move(8, X86_REGISTER_RAX, expected));
            const uint32_t form = width == 1 ? X86_FORM_LOCK_CMPXCHG_M8_R8 : width == 2 ? X86_FORM_LOCK_CMPXCHG_M16_R16 : width == 8 ? X86_FORM_LOCK_CMPXCHG_M64_R64 : X86_FORM_LOCK_CMPXCHG_M32_R32;
            tier1_select_append(&result, (X86Instruction){ .form = (uint16_t)form, .reg = desired, .rm = address, .mode = BUSTER_X86_MEMORY_BASE });
            const uint32_t extension = width == 1 ? X86_FORM_MOVZX_R32_RM8 : width == 2 ? X86_FORM_MOVZX_R32_RM16 : width == 8 ? X86_FORM_MOV_R64_RM64 : X86_FORM_MOV_R32_RM32;
            tier1_select_append(&result, tier1_select_register_record(extension, destination, X86_REGISTER_RAX));
            if (destination != X86_REGISTER_RAX)
            {
                tier1_select_append(&result, tier1_select_move(8, X86_REGISTER_RAX, scratch));
            }
        }
        else
        {
            const bool add = holes->immediate == BUSTER_IR_RMW_ADD;
            tier1_select_append(&result, tier1_select_move(8, BUSTER_TIER1_SELECT_SCRATCH, holes->right));
            const uint32_t form = add ? width == 1 ? X86_FORM_LOCK_XADD_M8_R8 : width == 2 ? X86_FORM_LOCK_XADD_M16_R16 : width == 8 ? X86_FORM_LOCK_XADD_M64_R64 : X86_FORM_LOCK_XADD_M32_R32
                                      : width == 1 ? X86_FORM_XCHG_RM8_R8 : width == 2 ? X86_FORM_XCHG_RM16_R16 : width == 8 ? X86_FORM_XCHG_RM64_R64 : X86_FORM_XCHG_RM32_R32;
            tier1_select_append(&result, (X86Instruction){ .form = (uint16_t)form, .reg = BUSTER_TIER1_SELECT_SCRATCH, .rm = holes->left, .mode = BUSTER_X86_MEMORY_BASE });
            const uint32_t extension = width == 1 ? X86_FORM_MOVZX_R32_RM8 : width == 2 ? X86_FORM_MOVZX_R32_RM16 : width == 8 ? X86_FORM_MOV_R64_RM64 : X86_FORM_MOV_R32_RM32;
            tier1_select_append(&result, tier1_select_register_record(extension, destination, BUSTER_TIER1_SELECT_SCRATCH));
        }
        break;
    case TIER1_RULE_PHI:
        break;
    case TIER1_RULE_ALLOCA:
    case TIER1_RULE_ALLOCA_IMMEDIATE:
    {
        // The size rounded up to the alignment `1 << scale`, from rsp; the allocation's address is rsp after it. A
        // constant's rounded size is the subtraction's immediate, and an allocation of none moves nothing.
        const int64_t alignment = INT64_C(1) << holes->scale;
        if (holes->rule == TIER1_RULE_ALLOCA)
        {
            tier1_select_append(&result, (X86Instruction){ .form = X86_FORM_LEA_R64_M, .reg = destination, .rm = holes->left, .mode = BUSTER_X86_MEMORY_BASE, .displacement = (int32_t)(alignment - 1) });
            tier1_select_append(&result, (X86Instruction){ .form = alignment <= 128 ? X86_FORM_AND_RM64_IMM8 : X86_FORM_AND_RM64_IMM32, .rm = destination, .immediate = (int32_t)-alignment });
            tier1_select_append(&result, tier1_select_register_record(X86_FORM_SUB_R64_RM64, X86_REGISTER_RSP, destination));
        }
        else
        {
            const uint64_t rounded = (holes->immediate + (uint64_t)alignment - 1) & ~((uint64_t)alignment - 1);
            if (rounded != 0)
            {
                tier1_select_append(&result, (X86Instruction){ .form = rounded <= 127 ? X86_FORM_SUB_RM64_IMM8 : X86_FORM_SUB_RM64_IMM32, .rm = X86_REGISTER_RSP, .immediate = (int32_t)rounded });
            }
        }
        tier1_select_append(&result, tier1_select_move(8, destination, X86_REGISTER_RSP));
        break;
    }
    case TIER1_RULE_STACK_SAVE:
        tier1_select_append(&result, tier1_select_move(8, destination, X86_REGISTER_RSP));
        break;
    case TIER1_RULE_STACK_RESTORE:
        tier1_select_append(&result, tier1_select_move(8, X86_REGISTER_RSP, holes->left));
        break;
    case TIER1_RULE_FRAME_ADDRESS:
        tier1_select_append(&result, tier1_select_move(8, destination, X86_REGISTER_RBP));
        break;
    case TIER1_RULE_STACK_ADDRESS:
        tier1_select_append(&result, (X86Instruction){ .form = X86_FORM_LEA_R64_M, .reg = destination, .rm = X86_REGISTER_RSP, .mode = BUSTER_X86_MEMORY_BASE });
        break;
    case TIER1_RULE_SYMBOL_ADDRESS:
        tier1_select_append(&result, (X86Instruction){ .form = X86_FORM_LEA_R64_M | BUSTER_X86_FORM_FIXUP, .reg = destination, .mode = BUSTER_X86_MEMORY_RIP });
        break;
    case TIER1_RULE_DIVIDE_POWER:
    {
        // The bias in the scratch register: the dividend's sign through every bit, then its low `shift` bits (for a shift
        // of 1 the sign bit alone), added to the dividend by a lea, which leaves the dividend for the remainder's mask.
        const uint8_t bias = BUSTER_TIER1_SELECT_SCRATCH;
        const uint32_t bits = width * 8;
        const uint32_t shift = holes->scale;
        tier1_select_append(&result, tier1_select_move(width, bias, holes->left));
        if (shift > 1)
        {
            tier1_select_append(&result, (X86Instruction){ .form = width == 8 ? X86_FORM_SAR_RM64_IMM8 : X86_FORM_SAR_RM32_IMM8, .rm = bias, .immediate = (int32_t)(bits - 1) });
        }
        tier1_select_append(&result, (X86Instruction){ .form = width == 8 ? X86_FORM_SHR_RM64_IMM8 : X86_FORM_SHR_RM32_IMM8, .rm = bias, .immediate = (int32_t)(bits - shift) });
        tier1_select_append(&result, (X86Instruction){ .form = width == 8 ? X86_FORM_LEA_R64_M : X86_FORM_LEA_R32_M, .reg = destination, .rm = holes->left, .index = bias, .mode = (uint8_t)BUSTER_X86_MEMORY_BASE_INDEX(0) });
        if ((holes->immediate & 2) != 0)
        {
            const int32_t mask = (int32_t)((UINT64_C(1) << shift) - 1);
            const uint16_t form = mask <= 127 ? (width == 8 ? X86_FORM_AND_RM64_IMM8 : X86_FORM_AND_RM32_IMM8) : (width == 8 ? X86_FORM_AND_RM64_IMM32 : X86_FORM_AND_RM32_IMM32);
            tier1_select_append(&result, (X86Instruction){ .form = form, .rm = destination, .immediate = mask });
            tier1_select_append(&result, tier1_select_register_record(width == 8 ? X86_FORM_SUB_R64_RM64 : X86_FORM_SUB_R32_RM32, destination, bias));
        }
        else
        {
            tier1_select_append(&result, (X86Instruction){ .form = width == 8 ? X86_FORM_SAR_RM64_IMM8 : X86_FORM_SAR_RM32_IMM8, .rm = destination, .immediate = (int32_t)shift });
        }
        break;
    }
    // A mask's registers are opmask registers: a node's register is its location (class 2's), whose low three bits are
    // the k register; a memory operand's base and index are general registers.
    case TIER1_RULE_MASK_CONSTANT:
    {
        const Tier1SelectMaskForms* forms = tier1_select_mask_forms(width);
        const uint8_t k = destination & 7;
        const uint64_t every = width >= 8 ? UINT64_MAX : (UINT64_C(1) << (width * 8)) - 1;
        if (holes->immediate == 0 || holes->immediate == every)
        {
            tier1_select_append(&result, (X86Instruction){ .form = holes->immediate == 0 ? forms->xor_forms : forms->xnor_forms, .reg = k, .vvvv = k, .rm = k });
        }
        else
        {
            tier1_select_request(&result, X86_OPERATION_MOV, width == 8 ? 8 : 4, BUSTER_TIER1_SELECT_SCRATCH, (int64_t)holes->immediate, 0);
            tier1_select_append(&result, (X86Instruction){ .form = forms->from_general, .reg = k, .rm = BUSTER_TIER1_SELECT_SCRATCH });
        }
        break;
    }
    case TIER1_RULE_MASK_COPY:
        if ((destination & 7) != (holes->left & 7))
        {
            tier1_select_append(&result, (X86Instruction){ .form = tier1_select_mask_forms(width)->move, .reg = destination & 7, .rm = holes->left & 7 });
        }
        break;
    case TIER1_RULE_MASK_LOGIC:
    {
        const Tier1SelectMaskForms* forms = tier1_select_mask_forms(width);
        const uint16_t form = holes->operation == X86_OPERATION_AND ? forms->and_forms : holes->operation == X86_OPERATION_OR ? forms->or_forms : forms->xor_forms;
        tier1_select_append(&result, (X86Instruction){ .form = form, .reg = destination & 7, .vvvv = holes->left & 7, .rm = holes->right & 7 });
        break;
    }
    case TIER1_RULE_MASK_NOT:
        tier1_select_append(&result, (X86Instruction){ .form = tier1_select_mask_forms(width)->not_forms, .reg = destination & 7, .rm = holes->left & 7 });
        break;
    // The integer's general register to the mask's opmask one, and back.
    case TIER1_RULE_MASK_FROM_INTEGER:
        tier1_select_append(&result, (X86Instruction){ .form = tier1_select_mask_forms(width)->from_general, .reg = destination & 7, .rm = holes->left });
        break;
    case TIER1_RULE_MASK_TO_INTEGER:
        tier1_select_append(&result, (X86Instruction){ .form = tier1_select_mask_forms(width)->to_general, .reg = destination, .rm = holes->left & 7 });
        break;
    // One kmov each, the node's last record, which the CFG consumer's frame-object patch rewrites (`stack_object`).
    case TIER1_RULE_MASK_LOAD:
    case TIER1_RULE_MASK_LOAD_ADDRESS:
    {
        X86Instruction record = holes->memory;
        record.reg = destination & 7;
        record.form = tier1_select_mask_forms(width)->load;
        tier1_select_append(&result, record);
        break;
    }
    case TIER1_RULE_MASK_STORE:
    case TIER1_RULE_MASK_STORE_ADDRESS:
    {
        X86Instruction record = holes->memory;
        record.reg = holes->right & 7;
        record.form = tier1_select_mask_forms(width)->store;
        tier1_select_append(&result, record);
        break;
    }
    // A floating value's registers are xmm registers (#141): a node's register is its location (class 1's), whose low
    // four bits are the xmm register; a memory operand's base and index, an integer's and the scratch register are
    // general registers.
    // A vector's (#141): its EVEX forms at its width, three operands, the destination any of the class's registers; or at
    // a target without AVX512VL (`vex`, #1281) its VEX forms, the same three operands.
    case TIER1_RULE_VECTOR:
    {
        const bool vex = holes->vex && width < 64;
        const Tier1SelectVectorForms* forms = tier1_select_vector_forms(width, vex);
        // At a target with AVX but not AVX2, a ymm register's integer forms are AVX2's: there a 32-byte vector's bitwise
        // operations take the packed singles' forms (the same bits), its ones vpcmpeqd of the xmm register (which zeroes
        // the upper half) and vinsertf128 of it into the upper half, and its splat vpshufd of the xmm register and
        // vinsertf128 the same way. No record evaluates the register's old bits as floats (#1293's review: vcmpps's
        // always-true predicate would, and a signaling NaN or a denormal among them would raise MXCSR's flags or trap). Its additions, subtractions and
        // multiplications have no AVX form: the IR has them of AVX2's rows alone (ir_simd_typed_operation).
        const bool avx = vex && width == 32 && !holes->avx2;
        static const uint16_t avx_bitwise[3] = { X86_FORM_VANDPS_Y_Y_YM256, X86_FORM_VORPS_Y_Y_YM256, X86_FORM_VXORPS_Y_Y_YM256 };
        const uint8_t x = destination & 15;
        const uint8_t left = holes->left & 15;
        const uint8_t right = holes->right & 15;
        const uint32_t kind = holes->scale;
        if (kind == BUSTER_TIER1_VECTOR_SCALAR_SPLAT)
        {
            tier1_select_scalar_splat(&result, holes);
        }
        else if (kind == BUSTER_TIER1_VECTOR_WIDTH_CHANGE)
        {
            tier1_select_width_change(&result, holes);
        }
        else if (kind == BUSTER_TIER1_VECTOR_PERMUTE)
        {
            tier1_select_permute(&result, holes);
        }
        else if (kind == BUSTER_TIER1_VECTOR_SHIFT)
        {
            tier1_select_vector_shift(&result, holes);
        }
        else if (kind == BUSTER_TIER1_VECTOR_LANE_OP)
        {
            const uint32_t flags = (uint32_t)holes->operation;
            const uint32_t at = width == 16 ? 0 : width == 32 ? 1 : 2;
            const X86Instruction record = { .form = ir_simd_lane_op_forms[(flags >> 2) & 7][flags & 3][at], .reg = x, .vvvv = left, .rm = right, .mode = BUSTER_X86_REGISTER_DIRECT };
            tier1_select_append(&result, record);
        }
        else if (kind == BUSTER_TIER1_VECTOR_MASK_EXPAND || kind == BUSTER_TIER1_VECTOR_SIGN_MASK)
        {
            tier1_select_mask_vector(&result, holes);
        }
        else if (kind == BUSTER_TIER1_VECTOR_PERMUTE2)
        {
            tier1_select_permute2(&result, holes, destination);
        }
        else if (kind == BUSTER_TIER1_VECTOR_TERNLOG)
        {
            tier1_select_ternlog(&result, holes, destination);
        }
        else if (kind == BUSTER_TIER1_VECTOR_COMPRESS)
        {
            tier1_select_compress(&result, holes, destination);
        }
        else if (kind == BUSTER_TIER1_VECTOR_LANE_EXTRACT || kind == BUSTER_TIER1_VECTOR_LANE_INSERT || kind == BUSTER_TIER1_VECTOR_MOVEMASK)
        {
            tier1_select_lane(&result, holes, destination);
        }
        else if (kind == BUSTER_TIER1_VECTOR_FROUND)
        {
            const uint32_t flags = (uint32_t)holes->immediate;
            const X86Instruction record = {
                .form = ir_simd_fround_forms[flags & 3][(flags >> 2) & 1],
                .reg = x,
                .vvvv = left,
                .rm = right,
                .mode = BUSTER_X86_REGISTER_DIRECT,
                .decorations = (uint8_t)(((flags >> 3) & 3) << 5),
            };
            tier1_select_append(&result, record);
        }
        else if (kind == BUSTER_TIER1_VECTOR_AES)
        {
            const uint32_t at = width == 16 ? 0 : width == 32 ? 1 : 2;
            const uint32_t last = (uint32_t)holes->immediate & 1;
            const X86Instruction record = {
                .form = vex && at < 2 ? ir_simd_aes_vex_forms[last][at] : ir_simd_aes_forms[last][at],
                .reg = x,
                .vvvv = left,
                .rm = right,
                .mode = BUSTER_X86_REGISTER_DIRECT,
            };
            tier1_select_append(&result, record);
        }
        else if (kind == BUSTER_TIER1_VECTOR_CONVERT)
        {
            // An extension's destination in reg and its source in rm; a truncation's source in reg and its destination in
            // rm, which zeroes the register past its lanes.
            const bool extend = holes->operation == 0;
            const X86Instruction record = {
                .form = tier1_select_convert_form((uint32_t)holes->operation, (uint32_t)holes->immediate, width, holes->source_width),
                .reg = extend ? x : left,
                .rm = extend ? left : x,
                .mode = BUSTER_X86_REGISTER_DIRECT,
            };
            tier1_select_append(&result, record);
        }
        else if (holes->sse && width == 16 && kind != BUSTER_TIER1_VECTOR_COMPARE && kind != BUSTER_TIER1_VECTOR_SELECT)
        {
            tier1_select_sse_vector(&result, holes, destination);
        }
        else if (kind == BUSTER_TIER1_VECTOR_ZERO || kind == BUSTER_TIER1_VECTOR_ONES)
        {
            // VEX's zero is vpxor of the xmm register, which zeroes the ymm one's upper half too; its ones vpcmpeqd.
            const bool ones = kind == BUSTER_TIER1_VECTOR_ONES;
            const X86Instruction record = {
                .form = !ones ? (vex ? X86_FORM_VPXOR_X_X_XM128 : forms->binary[5]) : avx ? X86_FORM_VPCMPEQD_X_X_XM128 : vex ? forms->ones : forms->ternary,
                .reg = x,
                .vvvv = x,
                .rm = x,
                .mode = BUSTER_X86_REGISTER_DIRECT,
                .immediate = ones && !vex ? 0xff : 0,
            };
            tier1_select_append(&result, record);
            if (ones && avx)
            {
                const X86Instruction insert = { .form = X86_FORM_VINSERTF128_Y_Y_XM128_IMM8, .reg = x, .vvvv = x, .rm = x, .mode = BUSTER_X86_REGISTER_DIRECT, .immediate = 1 };
                tier1_select_append(&result, insert);
            }
        }
        else if (kind == BUSTER_TIER1_VECTOR_SPLAT)
        {
            // VEX broadcasts from an xmm register's low doubleword, which vmovd puts the lane in.
            tier1_select_request(&result, X86_OPERATION_MOV, 4, BUSTER_TIER1_SELECT_SCRATCH, (int64_t)(uint32_t)holes->immediate, 0);
            if (vex)
            {
                tier1_select_append(&result, tier1_select_register_record(X86_FORM_VMOVD_X_RM32, x, BUSTER_TIER1_SELECT_SCRATCH));
            }
            tier1_select_append(&result, tier1_select_register_record(avx ? X86_FORM_VPSHUFD_X_XM128_IMM8 : forms->broadcast, x, vex ? x : BUSTER_TIER1_SELECT_SCRATCH));
            if (avx)
            {
                const X86Instruction insert = { .form = X86_FORM_VINSERTF128_Y_Y_XM128_IMM8, .reg = x, .vvvv = x, .rm = x, .mode = BUSTER_X86_REGISTER_DIRECT, .immediate = 1 };
                tier1_select_append(&result, insert);
            }
        }
        else if (kind == BUSTER_TIER1_VECTOR_COPY && x != left)
        {
            tier1_select_append(&result, tier1_select_class_move(x, left, width, vex));
        }
        else if (kind == BUSTER_TIER1_VECTOR_BINARY || kind == BUSTER_TIER1_VECTOR_NOT)
        {
            // A BINARY's form at its lanes (#141's element types, `source_width`), its bitwise operations' AVX forms at
            // a target without AVX2.
            const bool complement = kind == BUSTER_TIER1_VECTOR_NOT;
            const bool bitwise = holes->operation >= BUSTER_TIER1_VECTOR_AND && holes->operation <= BUSTER_TIER1_VECTOR_XOR;
            const X86Instruction record = {
                .form = complement ? forms->ternary : avx && bitwise ? avx_bitwise[holes->operation - BUSTER_TIER1_VECTOR_AND] : tier1_select_lane_form(holes->operation, holes->source_width, width, vex),
                .reg = x,
                .vvvv = left,
                .rm = complement ? left : right,
                .mode = BUSTER_X86_REGISTER_DIRECT,
                .immediate = complement ? 0x55 : 0,
            };
            tier1_select_append(&result, record);
        }
        else if (kind == BUSTER_TIER1_VECTOR_COMPARE)
        {
            // vpcmp's predicates: 0 ==, 1 <, 2 <=, 4 !=, 5 >= (not <) and 6 > (not <=); vpcmpu's the unsigned ones; at
            // floating lanes (#141's element types) vcmpps's and vcmppd's, the typed stream's rows' (EQ_OQ, NEQ_UQ,
            // LT_OS, LE_OS, GT_OS and GE_OS: C's comparisons, NaNs and all).
            static const uint8_t predicates[IR_PREDICATE_COUNT] = { [IR_EQ] = 0, [IR_NE] = 4, [IR_LT] = 1, [IR_LE] = 2, [IR_GT] = 6, [IR_GE] = 5, [IR_ULT] = 1, [IR_ULE] = 2, [IR_UGT] = 6, [IR_UGE] = 5 };
            static const uint8_t floating[IR_GE + 1] = { [IR_EQ] = 0x00, [IR_NE] = 0x04, [IR_LT] = 0x01, [IR_LE] = 0x02, [IR_GT] = 0x0e, [IR_GE] = 0x0d };
            const uint32_t lane = holes->source_width < IR_SIMD_LANE_COUNT ? holes->source_width : IR_SIMD_LANE_I32;
            const bool real = ir_simd_lane_floating((IrSimdLane)lane);
            const uint32_t condition = holes->condition < (real ? IR_GE + 1u : (uint32_t)IR_PREDICATE_COUNT) ? holes->condition : IR_EQ;
            const uint32_t index = width == 16 ? 0 : width == 32 ? 1 : 2;
            // A masked comparison's writemask (#1847), its third value's opmask register: none, k0's encoding, without one.
            const X86Instruction record = {
                .form = tier1_select_lane_compares[lane][!real && condition >= IR_ULT][index],
                .reg = (uint8_t)(destination & 7),
                .vvvv = left,
                .rm = right,
                .mode = BUSTER_X86_REGISTER_DIRECT,
                .immediate = real ? floating[condition] : predicates[condition],
                .decorations = (uint8_t)(holes->third & BUSTER_X86_DECORATION_MASK),
            };
            tier1_select_append(&result, record);
        }
        else if (kind == BUSTER_TIER1_VECTOR_SELECT)
        {
            // vpblendmd's destination is its third operand (rm) where the mask is set, its second (vvvv) where it isn't;
            // vpblendmb to vpblendmq at its lanes' width (#141's element types), whose mask has a bit per lane.
            const uint32_t lane = holes->source_width < IR_SIMD_LANE_COUNT ? holes->source_width : IR_SIMD_LANE_I32;
            const X86Instruction record = {
                .form = tier1_select_lane_blends[lane][width == 16 ? 0 : width == 32 ? 1 : 2],
                .reg = x,
                .vvvv = (uint8_t)(holes->third & 15),
                .rm = right,
                .mode = BUSTER_X86_REGISTER_DIRECT,
                .decorations = (uint8_t)(holes->left & BUSTER_X86_DECORATION_MASK),
            };
            tier1_select_append(&result, record);
        }
        break;
    }
    case TIER1_RULE_FLOAT_CONSTANT:
    {
        const uint8_t x = destination & 15;
        if (holes->immediate == 0)
        {
            tier1_select_append(&result, tier1_select_register_record(X86_FORM_XORPS_X_XM128, x, x));
        }
        else
        {
            tier1_select_request(&result, X86_OPERATION_MOV, width, BUSTER_TIER1_SELECT_SCRATCH, (int64_t)holes->immediate, 0);
            tier1_select_append(&result, tier1_select_register_record(tier1_select_float_forms(width)->from_general, x, BUSTER_TIER1_SELECT_SCRATCH));
        }
        break;
    }
    case TIER1_RULE_FLOAT_COPY:
        if ((destination & 15) != (holes->left & 15))
        {
            tier1_select_append(&result, tier1_select_float_move(destination & 15, holes->left & 15));
        }
        break;
    case TIER1_RULE_FLOAT_BINARY:
    {
        // Two operands, the destination's the left one, as TIER1_RULE_REGISTER's: a commutative operation's (add and
        // multiply) swapped where the right one is in the destination, or the right one copied into the scratch register.
        const uint8_t x = destination & 15;
        uint8_t left = holes->left & 15;
        uint8_t right = holes->right & 15;
        const uint32_t operation = (uint32_t)holes->operation & 3;
        if (x == right && left != right)
        {
            if (operation == IR_FADD - IR_FADD || operation == IR_FMUL - IR_FADD)
            {
                right = left;
                left = x;
            }
            else
            {
                tier1_select_append(&result, tier1_select_float_move(BUSTER_TIER1_SELECT_FLOAT_SCRATCH, right));
                right = BUSTER_TIER1_SELECT_FLOAT_SCRATCH;
            }
        }
        if (x != left)
        {
            tier1_select_append(&result, tier1_select_float_move(x, left));
        }
        tier1_select_append(&result, tier1_select_register_record(tier1_select_float_forms(width)->arithmetic[operation], x, right));
        break;
    }
    case TIER1_RULE_FLOAT_LOAD_OPERAND:
    {
        // The right operand from memory: the address's registers are general ones, which the left operand's copy into the
        // destination can't overwrite.
        X86Instruction memory = holes->memory;
        const uint8_t x = destination & 15;
        if (x != (holes->left & 15))
        {
            tier1_select_append(&result, tier1_select_float_move(x, holes->left & 15));
        }
        memory.reg = x;
        memory.form = tier1_select_float_forms(width)->arithmetic[(uint32_t)holes->operation & 3];
        tier1_select_append(&result, memory);
        break;
    }
    case TIER1_RULE_FLOAT_NEGATE:
    {
        // The sign bit in the vector class's scratch register, through the scratch general register, and the value's
        // copy in the destination xor it: the value stays in its class, and its other bits as they were.
        const uint8_t x = destination & 15;
        tier1_select_request(&result, X86_OPERATION_MOV, width, BUSTER_TIER1_SELECT_SCRATCH, (int64_t)(UINT64_C(1) << (width * 8 - 1)), 0);
        tier1_select_append(&result, tier1_select_register_record(tier1_select_float_forms(width)->from_general, BUSTER_TIER1_SELECT_FLOAT_SCRATCH, BUSTER_TIER1_SELECT_SCRATCH));
        if (x != (holes->left & 15))
        {
            tier1_select_append(&result, tier1_select_float_move(x, holes->left & 15));
        }
        tier1_select_append(&result, tier1_select_register_record(X86_FORM_XORPS_X_XM128, x, BUSTER_TIER1_SELECT_FLOAT_SCRATCH));
        break;
    }
    // One movss or movsd each, the node's last record, which the CFG consumer's frame-object patch rewrites; a masked
    // load's or store's (#1840, `condition` nonzero) its own.
    case TIER1_RULE_FLOAT_LOAD:
    case TIER1_RULE_FLOAT_STORE:
        if (holes->condition != 0)
        {
            tier1_select_masked_move(&result, holes, destination);
            break;
        }
        if (holes->rule == TIER1_RULE_FLOAT_STORE)
        {
            X86Instruction record = holes->memory;
            record.reg = holes->right & 15;
            record.form = tier1_select_class_forms(width, tier1_select_holes_encoding(holes))->store;
            tier1_select_append(&result, record);
            break;
        }
        __attribute__((fallthrough));
    case TIER1_RULE_FLOAT_LOAD_ADDRESS:
    {
        X86Instruction record = holes->memory;
        record.reg = destination & 15;
        record.form = tier1_select_class_forms(width, tier1_select_holes_encoding(holes))->load;
        tier1_select_append(&result, record);
        break;
    }
    case TIER1_RULE_FLOAT_STORE_ADDRESS:
    {
        X86Instruction record = holes->memory;
        record.reg = holes->right & 15;
        record.form = tier1_select_class_forms(width, tier1_select_holes_encoding(holes))->store;
        tier1_select_append(&result, record);
        break;
    }
    case TIER1_RULE_FLOAT_COMPARE:
    {
        // ucomis sets ZF, PF and CF: 0, 0, 0 for greater, 0, 0, 1 for less, 1, 0, 0 for equal and 1, 1, 1 for unordered.
        // a > b is seta and a >= b setae; a < b and a <= b are b > a and b >= a. a == b is ZF without PF, a != b either.
        const uint32_t condition = holes->condition;
        const bool swapped = condition == IR_LT || condition == IR_LE;
        const uint8_t first = (swapped ? holes->right : holes->left) & 15;
        const uint8_t second = (swapped ? holes->left : holes->right) & 15;
        // Floating inputs are in the vector class, so clearing this general-register result cannot alias either one.
        tier1_select_append(&result, tier1_select_register_record(X86_FORM_XOR_R32_RM32, destination, destination));
        tier1_select_append(&result, tier1_select_register_record(tier1_select_float_forms(width)->compare, first, second));
        if (condition == IR_EQ || condition == IR_NE)
        {
            const bool equal = condition == IR_EQ;
            tier1_select_append(&result, (X86Instruction){ .form = equal ? X86_FORM_SETE_RM8 : X86_FORM_SETNE_RM8, .rm = destination });
            tier1_select_append(&result, (X86Instruction){ .form = equal ? X86_FORM_SETNP_RM8 : X86_FORM_SETP_RM8, .rm = BUSTER_TIER1_SELECT_SCRATCH });
            tier1_select_append(&result, tier1_select_register_record(equal ? X86_FORM_AND_RM8_R8 : X86_FORM_OR_RM8_R8, BUSTER_TIER1_SELECT_SCRATCH, destination));
        }
        else
        {
            const bool strict = condition == IR_LT || condition == IR_GT;
            tier1_select_append(&result, (X86Instruction){ .form = strict ? X86_FORM_SETA_RM8 : X86_FORM_SETAE_RM8, .rm = destination });
        }
        break;
    }
    case TIER1_RULE_FLOAT_BRANCH_COMPARE:
        tier1_select_append(&result, tier1_select_register_record(tier1_select_float_forms(width)->compare, holes->left & 15, holes->right & 15));
        tier1_select_append(&result, (X86Instruction){ .form = tier1_select_condition_branches[holes->condition] });
        tier1_select_append(&result, (X86Instruction){ .form = X86_FORM_JMP_REL8 });
        break;
    case TIER1_RULE_FLOAT_CONVERT:
    {
        const uint32_t source_width = holes->source_width;
        const uint8_t x = destination & 15;
        switch (holes->scale)
        {
        case BUSTER_TIER1_FLOAT_FROM_SIGNED:
        case BUSTER_TIER1_FLOAT_FROM_UNSIGNED:
        {
            // A signed narrow operand's sign extension, or an unsigned I32's zero extension, in the scratch register,
            // converted as 64 bits; any other operand from its own register, its 32 or 64 bits.
            const bool is_signed = holes->scale == BUSTER_TIER1_FLOAT_FROM_SIGNED;
            const bool extend = is_signed ? source_width < 4 : source_width == 4;
            uint8_t source = holes->left;
            if (extend)
            {
                const uint16_t form = !is_signed ? X86_FORM_MOV_R32_RM32 : source_width == 1 ? X86_FORM_MOVSX_R32_RM8 : X86_FORM_MOVSX_R32_RM16;
                tier1_select_append(&result, tier1_select_register_record(form, BUSTER_TIER1_SELECT_SCRATCH, source));
                source = BUSTER_TIER1_SELECT_SCRATCH;
            }
            const bool wide = source_width == 8 || (!is_signed && source_width == 4);
            tier1_select_append(&result, tier1_select_register_record(X86_FORM_XORPS_X_XM128, x, x));
            tier1_select_append(&result, tier1_select_register_record(tier1_select_float_forms(width)->from_integer[wide], x, source));
            break;
        }
        case BUSTER_TIER1_FLOAT_TO_SIGNED:
        case BUSTER_TIER1_FLOAT_TO_UNSIGNED:
        {
            // An unsigned 32-bit result is the low half of the 64-bit conversion, whose range holds it.
            const bool wide = width == 8 || (holes->scale == BUSTER_TIER1_FLOAT_TO_UNSIGNED && width == 4);
            tier1_select_append(&result, tier1_select_register_record(tier1_select_float_forms(source_width)->to_integer[wide], destination, holes->left & 15));
            if (width < 4)
            {
                tier1_select_append(&result, tier1_select_register_record(width == 1 ? X86_FORM_MOVZX_R32_RM8 : X86_FORM_MOVZX_R32_RM16, destination, destination));
            }
            break;
        }
        case BUSTER_TIER1_FLOAT_RESIZE:
            tier1_select_append(&result, tier1_select_register_record(tier1_select_float_forms(source_width)->resize, x, holes->left & 15));
            break;
        case BUSTER_TIER1_FLOAT_FROM_BITS:
            tier1_select_append(&result, tier1_select_register_record(tier1_select_float_forms(width)->from_general, x, holes->left));
            break;
        case BUSTER_TIER1_FLOAT_TO_BITS:
            tier1_select_append(&result, tier1_select_register_record(tier1_select_float_forms(source_width)->to_general, holes->left & 15, destination));
            break;
        default:
            tier1_select_append(&result, (X86Instruction){0});
            break;
        }
        break;
    }
    case TIER1_RULE_FLOAT_RETURN:
        if ((holes->left & 15) != 0)
        {
            tier1_select_append(&result, tier1_select_float_move(0, holes->left & 15));
        }
        tier1_select_append(&result, (X86Instruction){ .form = X86_FORM_RET });
        break;
    default:
        tier1_select_append(&result, (X86Instruction){0});
        break;
    }
    if (holes->sign_result)
    {
        tier1_select_append(&result, tier1_select_register_record(X86_FORM_MOVSXD_R64_RM32, destination, destination));
    }
    if (__builtin_expect(holes->segment != 0, 0))
    {
        tier1_select_segment(&result, holes->segment);
    }
    return result;
}

// The fixed registers a rule's records write (tier1_select_fixed), which its consumer saves around them where other values
// live past the node in them: rax and rdx for a division, rcx for a shift by a register, and none for any other rule.
static uint32_t tier1_select_fixed_registers(uint32_t rule)
{
    const uint32_t divide = (1u << X86_REGISTER_RAX) | (1u << X86_REGISTER_RDX);
    // An unsigned I64's conversion (#141) writes the scratch register alone, which no value lives in: nothing is saved for
    // it, but its sequence takes the path's room.
    return rule == TIER1_RULE_DIVIDE || rule == TIER1_RULE_DIVIDE_RECIPROCAL || rule == TIER1_RULE_MULTIPLY_HIGH ? divide : rule == TIER1_RULE_SHIFT ? 1u << X86_REGISTER_RCX :
           rule == TIER1_RULE_FLOAT_CONVERT_WIDE ? 1u << BUSTER_TIER1_SELECT_SCRATCH : 0;
}

// TIER1_RULE_DIVIDE_RECIPROCAL's records (Tier1SelectReciprocal), the high product in rdx: the multiplier in rax, or in rdx
// where rax holds the dividend, times the dividend's register, or for a pre-shift the multiplier in rdx times the shifted
// dividend in rax. A dividend in rax or rdx that is read after the multiplication (`add`'s sequences and a remainder's)
// is copied into the scratch register first. A remainder is the dividend less the quotient times the divisor (its
// magnitude for a signed one, whose quotient is then left unnegated). The result moves into the destination last.
static uint32_t tier1_select_reciprocal_records(const Tier1SelectHoles* holes, X86Instruction* records)
{
    const uint32_t width = holes->width;
    const bool is_signed = (holes->scale & 1) != 0;
    const bool remainder = (holes->scale & 2) != 0;
    const Tier1SelectReciprocal reciprocal = tier1_select_reciprocal(holes->immediate, width, is_signed);
    const uint8_t rax = X86_REGISTER_RAX;
    const uint8_t rdx = X86_REGISTER_RDX;
    const bool kept = remainder || reciprocal.add;
    const uint8_t dividend = kept && (holes->left == rax || holes->left == rdx) ? BUSTER_TIER1_SELECT_SCRATCH : holes->left;
    uint32_t count = 0;
    if (dividend != holes->left)
    {
        records[count++] = tier1_select_move(width, dividend, holes->left);
    }
    const uint16_t multiply = is_signed ? (width == 8 ? X86_FORM_IMUL_RM64 : X86_FORM_IMUL_RM32) : (width == 8 ? X86_FORM_MUL_RM64 : X86_FORM_MUL_RM32);
    if (reciprocal.pre_shift != 0)
    {
        if (dividend != rax)
        {
            records[count++] = tier1_select_move(width, rax, dividend);
        }
        records[count++] = tier1_select_shift(X86_OPERATION_SHR, width, rax, reciprocal.pre_shift);
        records[count++] = tier1_select_constant_move(width, rdx, reciprocal.multiplier);
        records[count++] = (X86Instruction){ .rm = rdx, .form = multiply };
    }
    else
    {
        records[count++] = tier1_select_constant_move(width, dividend == rax ? rdx : rax, reciprocal.multiplier);
        records[count++] = (X86Instruction){ .rm = dividend == rax ? rdx : dividend, .form = multiply };
    }
    uint8_t quotient = rdx;
    if (!is_signed && reciprocal.add)
    {
        records[count++] = tier1_select_move(width, rax, dividend);
        records[count++] = tier1_select_register_record(tier1_select_binary_form(X86_OPERATION_SUB, width), rax, rdx);
        records[count++] = tier1_select_shift(X86_OPERATION_SHR, width, rax, 1);
        records[count++] = tier1_select_register_record(tier1_select_binary_form(X86_OPERATION_ADD, width), rax, rdx);
        if (reciprocal.post_shift > 1)
        {
            records[count++] = tier1_select_shift(X86_OPERATION_SHR, width, rax, reciprocal.post_shift - 1u);
        }
        quotient = rax;
    }
    else if (!is_signed && reciprocal.post_shift != 0)
    {
        records[count++] = tier1_select_shift(X86_OPERATION_SHR, width, rdx, reciprocal.post_shift);
    }
    else if (is_signed)
    {
        if (reciprocal.add)
        {
            records[count++] = tier1_select_register_record(tier1_select_binary_form(X86_OPERATION_ADD, width), rdx, dividend);
        }
        if (reciprocal.post_shift != 0)
        {
            records[count++] = tier1_select_shift(X86_OPERATION_SAR, width, rdx, reciprocal.post_shift);
        }
        records[count++] = tier1_select_move(width, rax, rdx);
        records[count++] = tier1_select_shift(X86_OPERATION_SHR, width, rax, width * 8 - 1);
        records[count++] = tier1_select_register_record(tier1_select_binary_form(X86_OPERATION_ADD, width), rdx, rax);
        const bool negative = ((holes->immediate >> (width * 8 - 1)) & 1) != 0;
        if (negative && !remainder)
        {
            records[count++] = (X86Instruction){ .rm = rdx, .form = width == 8 ? X86_FORM_NEG_RM64 : X86_FORM_NEG_RM32 };
        }
    }
    if (remainder)
    {
        const uint64_t divisor = is_signed ? tier1_select_magnitude(holes->immediate, width) : holes->immediate;
        const uint8_t other = quotient == rax ? rdx : rax;
        if (tier1_select_immediate_fits(width, divisor))
        {
            records[count++] = tier1_select_multiply_immediate(width, quotient, quotient, divisor);
        }
        else
        {
            records[count++] = tier1_select_constant_move(width, other, divisor);
            records[count++] = tier1_select_register_record(tier1_select_binary_form(X86_OPERATION_IMUL, width), quotient, other);
        }
        records[count++] = (X86Instruction){ .rm = quotient, .form = width == 8 ? X86_FORM_NEG_RM64 : X86_FORM_NEG_RM32 };
        records[count++] = tier1_select_register_record(tier1_select_binary_form(X86_OPERATION_ADD, width), quotient, dividend);
    }
    if (holes->destination != quotient)
    {
        records[count++] = tier1_select_move(width, holes->destination, quotient);
    }
    return count;
}

// The records of a rule that takes x86's fixed registers (tier1_select_fixed_registers), into `records`, which has room
// for BUSTER_TIER1_SELECT_FIXED_RECORDS; none for any other rule. They read every operand before writing a fixed
// register, through the scratch register where they must, and write the destination last; their consumers save the other
// values that live past the node in those registers around them, in their call-save rows (tier1_emit_fixed). A division
// or remainder by a register (TIER1_RULE_DIVIDE): the divisor in the scratch register, the dividend in rax, its sign or
// zero through rdx, then div or idiv. A shift by a register (TIER1_RULE_SHIFT): the value in the scratch register, the
// count in cl. A division by a constant's reciprocal: tier1_select_reciprocal_records.
static uint32_t tier1_select_fixed(const Tier1SelectHoles* holes, X86Instruction* records)
{
    const uint32_t width = holes->width;
    const uint8_t scratch = BUSTER_TIER1_SELECT_SCRATCH;
    uint32_t count = 0;
    if (holes->rule == TIER1_RULE_DIVIDE)
    {
        const bool signed_divide = (holes->immediate & 1) != 0;
        const uint8_t result = (holes->immediate & 2) != 0 ? X86_REGISTER_RDX : X86_REGISTER_RAX;
        records[count++] = tier1_select_move(8, scratch, holes->right);
        if (holes->left != X86_REGISTER_RAX)
        {
            records[count++] = tier1_select_move(8, X86_REGISTER_RAX, holes->left);
        }
        records[count++] = signed_divide ? (X86Instruction){ .form = width == 8 ? X86_FORM_CQO : X86_FORM_CDQ } : tier1_select_register_record(X86_FORM_XOR_R32_RM32, X86_REGISTER_RDX, X86_REGISTER_RDX);
        records[count++] = (X86Instruction){ .rm = scratch, .form = width == 8 ? (signed_divide ? X86_FORM_IDIV_RM64 : X86_FORM_DIV_RM64) : (signed_divide ? X86_FORM_IDIV_RM32 : X86_FORM_DIV_RM32) };
        if (holes->destination != result)
        {
            records[count++] = tier1_select_move(width, holes->destination, result);
        }
    }
    else if (holes->rule == TIER1_RULE_SHIFT)
    {
        const uint16_t form = holes->operation == X86_OPERATION_SHL ? (width == 8 ? X86_FORM_SHL_RM64_CL : X86_FORM_SHL_RM32_CL) :
                              holes->operation == X86_OPERATION_SHR ? (width == 8 ? X86_FORM_SHR_RM64_CL : X86_FORM_SHR_RM32_CL) :
                                                                      (width == 8 ? X86_FORM_SAR_RM64_CL : X86_FORM_SAR_RM32_CL);
        records[count++] = tier1_select_move(8, scratch, holes->left);
        if (holes->right != X86_REGISTER_RCX)
        {
            records[count++] = tier1_select_move(4, X86_REGISTER_RCX, holes->right);
        }
        records[count++] = (X86Instruction){ .rm = scratch, .form = form };
        records[count++] = tier1_select_move(width, holes->destination, scratch);
    }
    else if (holes->rule == TIER1_RULE_DIVIDE_RECIPROCAL)
    {
        count = tier1_select_reciprocal_records(holes, records);
    }
    else if (holes->rule == TIER1_RULE_MULTIPLY_HIGH)
    {
        // The product is commutative: an operand already in rax stays there, and mul reads its other operand, which may
        // be rdx, before it writes rdx:rax.
        const uint8_t rax = X86_REGISTER_RAX;
        const uint8_t multiplier = holes->left == rax ? holes->right : holes->right == rax ? holes->left : holes->right;
        if (holes->left != rax && holes->right != rax)
        {
            records[count++] = tier1_select_move(8, rax, holes->left);
        }
        records[count++] = (X86Instruction){ .rm = multiplier, .form = X86_FORM_MUL_RM64 };
        if (holes->destination != X86_REGISTER_RDX)
        {
            records[count++] = tier1_select_move(8, holes->destination, X86_REGISTER_RDX);
        }
    }
    else if (holes->rule == TIER1_RULE_FLOAT_CONVERT_WIDE && holes->scale == BUSTER_TIER1_FLOAT_FROM_UNSIGNED)
    {
        // An unsigned I64 to a floating value (#141), the source s unchanged: s, converted as a signed one where its sign
        // bit is clear; else v = (s | (s & 1) << 1) >> 1, s halved with its last bit kept as a sticky one so that rounding
        // v rounds s, converted and doubled by adding 1 to the result's exponent (s's sign bit shifted to the exponent's
        // lowest, by paddq or paddd through the vector class's scratch register).
        const uint8_t x = holes->destination & 15;
        const uint8_t xs = BUSTER_TIER1_SELECT_FLOAT_SCRATCH;
        const Tier1SelectFloatForms* forms = tier1_select_float_forms(width);
        records[count++] = (X86Instruction){ .form = X86_FORM_LEA_R64_M, .reg = scratch, .rm = holes->left, .index = holes->left, .mode = (uint8_t)BUSTER_X86_MEMORY_BASE_INDEX(0) };
        records[count++] = (X86Instruction){ .form = X86_FORM_AND_RM32_IMM8, .rm = scratch, .immediate = 2 };
        records[count++] = tier1_select_register_record(X86_FORM_OR_R64_RM64, scratch, holes->left);
        records[count++] = (X86Instruction){ .form = X86_FORM_SHR_RM64_IMM8, .rm = scratch, .immediate = 1 };
        records[count++] = tier1_select_register_record(X86_FORM_TEST_RM64_R64, holes->left, holes->left);
        records[count++] = tier1_select_register_record(X86_FORM_CMOVNS_R64_RM64, scratch, holes->left);
        records[count++] = tier1_select_register_record(X86_FORM_XORPS_X_XM128, x, x);
        records[count++] = tier1_select_register_record(forms->from_integer[1], x, scratch);
        records[count++] = tier1_select_move(8, scratch, holes->left);
        records[count++] = (X86Instruction){ .form = X86_FORM_SHR_RM64_IMM8, .rm = scratch, .immediate = 63 };
        records[count++] = (X86Instruction){ .form = X86_FORM_SHL_RM64_IMM8, .rm = scratch, .immediate = width == 8 ? 52 : 23 };
        records[count++] = tier1_select_register_record(forms->from_general, xs, scratch);
        records[count++] = tier1_select_register_record(width == 8 ? X86_FORM_PADDQ_X_XM128 : X86_FORM_PADDD_X_XM128, x, xs);
    }
    else if (holes->rule == TIER1_RULE_FLOAT_CONVERT_WIDE)
    {
        // A floating value to an unsigned I64 (#141), the source x unchanged: d = trunc(x), and where x is 2^63 or more,
        // whose d is then 2^63 (the conversion's out-of-range result), 2^63 | trunc(x - 2^63), computed as the negation of
        // trunc(2^63 - x) (truncation is symmetric), 2^63 in the vector class's scratch register from the scratch one.
        const uint8_t x = holes->left & 15;
        const uint8_t xs = BUSTER_TIER1_SELECT_FLOAT_SCRATCH;
        const uint32_t source_width = holes->source_width;
        const Tier1SelectFloatForms* forms = tier1_select_float_forms(source_width);
        records[count++] = tier1_select_register_record(forms->to_integer[1], holes->destination, x);
        records[count++] = tier1_select_constant_move(source_width == 8 ? 8 : 4, scratch, source_width == 8 ? UINT64_C(0x43e0000000000000) : UINT64_C(0x5f000000));
        records[count++] = tier1_select_register_record(forms->from_general, xs, scratch);
        records[count++] = tier1_select_register_record(forms->arithmetic[1], xs, x);
        records[count++] = tier1_select_register_record(forms->to_integer[1], scratch, xs);
        records[count++] = (X86Instruction){ .form = X86_FORM_NEG_RM64, .rm = scratch };
        records[count++] = tier1_select_register_record(X86_FORM_OR_R64_RM64, scratch, holes->destination);
        records[count++] = tier1_select_register_record(X86_FORM_TEST_RM64_R64, holes->destination, holes->destination);
        records[count++] = tier1_select_register_record(X86_FORM_CMOVS_R64_RM64, holes->destination, scratch);
    }
    return count;
}

#define BUSTER_TIER1_SELECT_MAX_INSTRUCTIONS (1u << 20)
// The largest frame object, and the most aligned, the selector takes: the frame's offsets are 32-bit.
#define BUSTER_TIER1_SELECT_MAX_OBJECT_BYTES (UINT64_C(1) << 28)
// Match the frame planner's bound without requiring its declarations before this standalone selector.
#define BUSTER_TIER1_SELECT_MAX_OBJECT_ALIGNMENT (1u << 30)
#define BUSTER_TIER1_SELECT_MAX_CANDIDATES 8
// The most links a pattern's walk follows through a truth value's chain or an address's (tier1_select_truth,
// tier1_select_address): a bound on each node's walk, which every node of a chain takes again as its own pattern's.
#define BUSTER_TIER1_SELECT_WALK 8

// Cold per-node emission metadata. The hot rule-choice pass uses contiguous 16-lane cost arrays; the emitter consumes
// all fields of one selected pattern. `values` preserve operand roles, including absent slots, while the effective-use
// CSR is the compact, directly consumable liveness handoff. CALL uses none of the three ordinary value slots: its
// ordered argument slice, including duplicates, is in the effective-use CSR, immediate is its symbol ID, and width
// is the result width (zero for void). Memory/conversion patterns keep logical byte widths, while the allocator and
// frame consumer use tier1_select_width's 4-byte canonical storage for I8/I16. Conversion source_width is the source's
// logical byte width, except canonical I1 zero-extension reads its 32-bit storage. A signed LOAD_ADDRESS uses MOVSX as
// its operation, width as its exact memory width and source_width as its destination width (4 or 8). A zero extension
// of a canonical integer selects COPY instead, with canonical width and the source width retained; COPY of a narrow
// source carries that width only when tier1_select_canonical certified it, and any other COPY leaves source_width zero.
// A COPY's source_width is the plan's record of that certificate, which both paths' plans must agree on; the emitter
// doesn't read it, since a copy in its own register is nothing at every width (#1116, tier1_select_expand).
// A comparison with compare_memory reads values[1:2] as its address's base/index, and values[0] as the other register
// (NONE for a memory/immediate comparison). Its width is the exact access width; its predicate already accounts for
// operand swapping or a zero extension removed from the load. Its frame object is patched into the CMP, not SETcc/JMP.
// `fold_count` counts the nodes the pattern computes itself rather than reads (maximal munch's rank): which nodes the
// code leaves out follows from what the chosen patterns read (tier1_select_plan). For TIER1_RULE_ADDRESS,
// TIER1_RULE_LOAD, TIER1_RULE_LOAD_ADDRESS, TIER1_RULE_LOAD_OPERAND, TIER1_RULE_STORE, TIER1_RULE_STORE_ADDRESS and a
// mask's TIER1_RULE_MASK_LOAD_ADDRESS and TIER1_RULE_MASK_STORE_ADDRESS (#179), `stack_object` is BUSTER_IR_NONE, or the
// frame object the address walk reached, whose offset the consumer adds to the
// displacement at emission (tier1_emit_cfg.c): the base value is then BUSTER_IR_NONE, and no register is read for it.
// The original function stays immutable.
typedef struct Tier1SelectPattern Tier1SelectPattern;
struct Tier1SelectPattern
{
    uint64_t immediate;
    uint32_t values[3];
    int32_t displacement;
    uint32_t stack_object;
    uint8_t rule;
    uint8_t operation;
    uint8_t width;
    uint8_t source_width;
    uint8_t condition;
    uint8_t scale;
    uint8_t fold_count;
    uint8_t intrinsic_cost;
    bool compare_immediate;
    bool compare_memory;
    // A test's: no-ops the node's expansion writes before its rule's records, which take its sequence to its room and
    // past it (#989's review). Every pattern the selector makes has none.
    uint8_t nops;
    // A sign-preserving I32 representation uses eight bytes in spills and CFG edge moves; zero means the type width.
    uint8_t storage_width;
    // An ordinary I32 ALU operation writes its wrapping result first, then establishes signed 64-bit storage once.
    bool sign_result;
};

// The fixed registers of a node's pattern (#1839): an asm body's general registers it touches (TIER1_RULE_ASM), or its
// rule's (tier1_select_fixed_registers).
static uint32_t tier1_select_node_fixed(const IrFunction* function, const Tier1SelectPattern* pattern)
{
    const bool body = pattern->rule == TIER1_RULE_ASM && pattern->immediate < function->asm_body_count;
    return body ? function->asm_bodies[pattern->immediate].touched & 0xffffu : tier1_select_fixed_registers(pattern->rule);
}

typedef struct Tier1SelectPlan Tier1SelectPlan;
struct Tier1SelectPlan
{
    const IrFunction* function;
    Tier1SelectPattern* patterns;
    uint8_t* covered;
    uint32_t* effective_use_offsets;
    uint32_t* effective_use_values;
    uint32_t effective_use_count;
    // Candidate construction's working costs (tier1_select_pattern_cost). Both production paths build most patterns
    // before their tree children's (the walk back), so a tree child's cost is included only where its pattern was made
    // first; a covered node's unbuilt pattern has cost 0. The BURS tables choose the cover independently of this column,
    // and estimated_cost sums the selected patterns' intrinsic costs. The tests' maximal-munch reference builds children
    // first, so its column contains complete subtree costs.
    // Working the subtree costs out again children first took selection on the frozen sqlite3.c from 222.9 core cycles
    // per IR instruction to 241.3 (the 9700X, #1117's review), for a column nothing reads; a consumer that needs them on
    // either production path works them out so.
    uint32_t* costs;
    uint64_t estimated_cost;
    uint64_t arena_bytes;
    // The path that planned it, which the legacy emitter's choice of immediates' forms takes too.
    bool vector;
    // Whether `function` passed a full verification, the selector's own or its caller's (tier1_select_plan_verified):
    // every plan the selector publishes has, so the liveness build doesn't verify its function again (#1379). A plan
    // made otherwise (a test's) leaves it false.
    bool function_verified;
};

typedef struct Tier1SelectCode Tier1SelectCode;
struct Tier1SelectCode
{
    X86Instruction* records;
    uint8_t* cost_ids;
    uint32_t count;
    uint32_t* node_first;
    uint32_t* branches;
    uint32_t* targets;
    uint32_t branch_count;
    uint32_t unknown_costs;
    uint32_t error_node;
    uint64_t arena_bytes;
};

typedef struct Tier1SelectCandidates Tier1SelectCandidates;
struct Tier1SelectCandidates
{
    Tier1SelectPattern patterns[BUSTER_TIER1_SELECT_MAX_CANDIDATES];
    uint32_t costs[BUSTER_TIER1_SELECT_MAX_CANDIDATES];
    uint32_t count;
};

typedef struct Tier1SelectForest Tier1SelectForest;
struct Tier1SelectForest
{
    const IrFunction* function;
    const uint32_t* blocks;
    const uint32_t* uses;
    const uint32_t* costs;
    // Each node's last memory barrier before it, plus one (0 for none): a store, a call, a volatile or atomic load
    // (BUSTER_IR_OBSERVABLE), a fence or a move of rsp, which a load isn't folded past (tier1_select_load_folds;
    // tier1_select_barriers). An atomic access is never folded itself, and a load moved past a fence would leave the place
    // its order keeps (#1059).
    const uint32_t* barriers;
    // Each narrow node's canonical-storage answer (tier1_select_canonical): its register holds zeros past its width.
    const uint8_t* canonical;
    // Semantic range certificates: the low byte is the number of possibly nonzero bits, the high byte the smallest
    // signed width. These describe the IR value; canonical separately proves its physical register's upper bits.
    const uint16_t* known_bits;
    // Whether the function has vectors (#141, ir_function_has_vectors_path): one scan of it, so that a function without
    // one pays no node's vector checks (tier1_select_candidates).
    bool vectors;
};

static bool tier1_select_barrier(const IrFunction* function, uint32_t node)
{
    const uint32_t op = function->op[node];
    // An allocation and a restore move rsp (#141): no load folds past either, so none reads memory a restore released.
    // A fence of any order and scope (#1059), a signal fence's too, is a compiler barrier: no load folds into a consumer
    // past it, so that the access stays on the fence's side the program put it. A volatile or atomic load is one too, and
    // an atomic read/modify/write, and a masked store (#1840). These are the nodes with an effect, which the plan
    // materializes whatever their uses.
    return op == IR_STORE || op == IR_CALL || op == IR_CALL_INDIRECT || op == IR_ALLOCA || op == IR_STACK_RESTORE || op == IR_FENCE || op == IR_ATOMIC_RMW ||
           op == IR_MASKED_STORE || (op == IR_LOAD && (function->flags[node] & BUSTER_IR_OBSERVABLE) != 0);
}

// Whether a fold may not move a load past `node` (#1059's audit): a barrier, or any other load, since the typed stream
// doesn't carry a C access's volatility, so that an IR load without BUSTER_IR_VOLATILE may still be a volatile object's,
// and two volatile reads must keep their order. A load still folds into its own consumer (tier1_select_load_folds:
// the last of these before the consumer the load itself), but past no other load.
static bool tier1_select_fold_barrier(const IrFunction* function, uint32_t node)
{
    return tier1_select_barrier(function, node) || function->op[node] == IR_LOAD || function->op[node] == IR_MASKED_LOAD;
}

// Each node's barriers entry (Tier1SelectForest.barriers), a running maximum of the barriers' positions plus one: the
// scalar twin of tier1_select_barriers. A pass of its own, apart from the plan's pass over the nodes, where the store
// would make the operands' walk reload the function's columns.
static void tier1_select_barriers_scalar(const IrFunction* function, uint32_t count, uint32_t* barriers)
{
    uint32_t barrier = 0;
    for (uint32_t node = 0; node < count; node += 1)
    {
        barriers[node] = barrier;
        barrier = tier1_select_fold_barrier(function, node) ? node + 1 : barrier;
    }
}

// The same, 16 nodes at a time: their kinds compared at once, each barrier's lane its position plus one, the
// running maximum by four shifts of the lanes and the vectors' before, and each node's entry its lane before's. The
// columns are read, and the entries written, with masked loads and stores.
static void tier1_select_barriers(const IrFunction* function, uint32_t count, uint32_t* barriers)
{
    const __m512i zero = _mm512_setzero_si512();
    const __m512i lanes = _mm512_setr_epi32(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16);
    __m512i carry = zero;
    for (uint32_t first = 0; first < count; first += 16)
    {
        const uint32_t left = count - first;
        const __mmask16 active = left >= 16 ? (__mmask16)0xffff : (__mmask16)((1u << left) - 1);
        const __m512i op = _mm512_cvtepu8_epi32(_mm_maskz_loadu_epi8(active, function->op + first));
        const __mmask16 barrier = (_mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(IR_STORE)) | _mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(IR_CALL)) |
                                   _mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(IR_CALL_INDIRECT)) | _mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(IR_ALLOCA)) |
                                   _mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(IR_STACK_RESTORE)) | _mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(IR_FENCE)) |
                                   _mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(IR_ATOMIC_RMW)) | _mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(IR_LOAD)) |
                                   _mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(IR_MASKED_LOAD)) | _mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(IR_MASKED_STORE))) & active;
        __m512i at = _mm512_maskz_add_epi32(barrier, lanes, _mm512_set1_epi32((int)first));
        at = _mm512_max_epu32(at, _mm512_alignr_epi32(at, zero, 15));
        at = _mm512_max_epu32(at, _mm512_alignr_epi32(at, zero, 14));
        at = _mm512_max_epu32(at, _mm512_alignr_epi32(at, zero, 12));
        at = _mm512_max_epu32(at, _mm512_alignr_epi32(at, zero, 8));
        at = _mm512_max_epu32(at, carry);
        _mm512_mask_storeu_epi32(barriers + first, active, _mm512_alignr_epi32(at, carry, 15));
        carry = _mm512_permutexvar_epi32(_mm512_set1_epi32(15), at);
    }
}

static uint32_t tier1_select_width(uint32_t type)
{
    // Narrow SSA values occupy a zero-extended 32-bit register/spill slot; signedness is explicit in IR_SEXT. An I32's
    // upper half is unspecified, as System V leaves a parameter's and a result's (#1116): every rule reads an I32 operand
    // at 32 bits (an address's index is an I64). A zero extension to I64 keeps its 32-bit move unless selection proves
    // those upper bits zero. No ordinary copy, truncation to I32, call argument or call result clears them.
    return type == IR_I1 || type == IR_I8 || type == IR_I16 || type == IR_I32 ? 4 : type == IR_I64 || type == IR_PTR ? 8 : 0;
}

static uint32_t tier1_select_memory_width(uint32_t type)
{
    return type == IR_I8 ? 1 : type == IR_I16 ? 2 : type == IR_I32 ? 4 : type == IR_I64 || type == IR_PTR ? 8 : 0;
}

// A mask's bytes (#179): m8 to m64's 1 to 8, the width of the opmask instructions that take it; 0 for any other type.
static uint32_t tier1_select_mask_width(uint32_t type)
{
    return type == IR_M8 ? 1 : type == IR_M16 ? 2 : type == IR_M32 ? 4 : type == IR_M64 ? 8 : 0;
}

// A vector's bytes (#141): v4i32's 16, v8i32's 32 and v16i32's 64, its xmm, ymm or zmm register's; 0 for any other type.
static uint32_t tier1_select_vector_width(uint32_t type)
{
    return type == IR_V4I32 ? 16 : type == IR_V8I32 ? 32 : type == IR_V16I32 ? 64 : 0;
}

// A floating value's bytes (#141): F32's 4 and F64's 8, the ss and sd forms' widths; 0 for any other type. F80, the x87's,
// isn't taken.
static uint32_t tier1_select_float_width(uint32_t type)
{
    return type == IR_F32 ? 4 : type == IR_F64 ? 8 : 0;
}

// Whether a node computes on long doubles (#141, TIER1_RULE_X87's): one of type F80, a store of one, or a comparison of
// them or a conversion from one; a phi of them and a call returning one too, which their own rules take.
static bool tier1_select_x87_node(const IrFunction* function, uint32_t node)
{
    const uint32_t op = function->op[node];
    const uint32_t a = function->a[node];
    const uint32_t b = function->b[node];
    const bool from = (op == IR_FCMP || op == IR_FTRUNC || op == IR_FPTOSI || op == IR_FPTOUI || op == IR_RET) && a < function->instruction_count && function->type[a] == IR_F80;
    return function->type[node] == IR_F80 || (op == IR_STORE && b < function->instruction_count && function->type[b] == IR_F80) || from;
}

// Whether a function takes or returns a vector (#141), which no convention's place Tier 1 has: its vectors are its own
// values otherwise, and a call's vector argument or result tier1_select_call_supported refuses.
static bool tier1_select_vector_signature(const IrFunction* function)
{
    bool found = tier1_select_vector_width(function->result_type) != 0;
    for (uint32_t p = 0; !found && p < function->parameter_count; p += 1)
    {
        found = tier1_select_vector_width(function->parameter_types[p]) != 0;
    }
    return found;
}

// The width a narrow source's copy reads: I1's canonical storage is its 32 bits, I8 one byte and I16 two. Zero says the
// type isn't narrow.
static uint32_t tier1_select_narrow_width(uint32_t type)
{
    return type == IR_I1 ? 4 : type == IR_I8 ? 1 : type == IR_I16 ? 2 : 0;
}

#define BUSTER_TIER1_SELECT_CANONICAL_PASSES 8

#include "tier1_select_ranges.c"

// Compact, monotone range facts, shared by both selection paths. Unknown is the type's full width. Starting there
// (including at phis) is essential: a loop must never prove its own invariant from an optimistic backedge. Bounded
// sweeps handle forward block references too; facts not reached within the bound remain conservative.
static void tier1_select_known_bits_passes(const IrFunction* function, const uint32_t* blocks, uint16_t* known, bool vector, bool seed)
{
    const uint32_t count = function->instruction_count;
    // The caller's fresh scratch column is disjoint from this immutable IR and its arrays. Cache the stable array
    // addresses across known's stores, without relying on the host build's disabled type-based alias analysis.
    const uint8_t* const types = function->type;
    const uint8_t* const operations = function->op;
    const uint32_t* const inputs_a = function->a;
    const uint32_t* const inputs_b = function->b;
    const uint32_t* const inputs_c = function->c;
    const IrConstant* const constants = function->constants;
    if (seed && BUSTER_VECTOR_PATH(vector))
    {
        // Independent seed classification, 32 nodes at a time. Both loads and stores are masked at the logical tail.
        for (uint32_t first = 0; first < count; first += 32)
        {
            const uint32_t left = count - first;
            const __mmask32 active = left >= 32 ? UINT32_MAX : (UINT32_C(1) << left) - 1;
            const __m512i type = _mm512_cvtepu8_epi16(_mm256_maskz_loadu_epi8(active, types + first));
            __m512i bits = _mm512_set1_epi16(64);
            bits = _mm512_mask_mov_epi16(bits, _mm512_cmpeq_epi16_mask(type, _mm512_set1_epi16(IR_I1)), _mm512_set1_epi16(1));
            bits = _mm512_mask_mov_epi16(bits, _mm512_cmpeq_epi16_mask(type, _mm512_set1_epi16(IR_I8)), _mm512_set1_epi16(8));
            bits = _mm512_mask_mov_epi16(bits, _mm512_cmpeq_epi16_mask(type, _mm512_set1_epi16(IR_I16)), _mm512_set1_epi16(16));
            bits = _mm512_mask_mov_epi16(bits, _mm512_cmpeq_epi16_mask(type, _mm512_set1_epi16(IR_I32)), _mm512_set1_epi16(32));
            _mm512_mask_storeu_epi16(known + first, active, _mm512_or_si512(bits, _mm512_slli_epi16(bits, 8)));
        }
    }
    else if (seed)
    {
        for (uint32_t node = 0; node < count; node += 1)
        {
            const uint32_t bits = tier1_select_width(types[node]) != 0 ? ir_type_bits[types[node]] : 64;
            known[node] = (uint16_t)(bits | (bits << 8));
        }
    }
    for (uint32_t pass = 0; pass < BUSTER_TIER1_SELECT_CANONICAL_PASSES; pass += 1)
    {
        bool changed = false;
        for (uint32_t node = 0; node < count; node += 1)
        {
            const uint32_t type = types[node];
            if (tier1_select_width(type) != 0)
            {
                const uint32_t bits = ir_type_bits[type];
                const uint32_t op = operations[node];
                const uint32_t a = inputs_a[node];
                const uint32_t b = inputs_b[node];
                const uint32_t ua = a < count ? known[a] & BUSTER_TIER1_KNOWN_UNSIGNED_MASK : bits;
                const uint32_t ub = b < count ? known[b] & BUSTER_TIER1_KNOWN_UNSIGNED_MASK : bits;
                const uint32_t sa = a < count ? known[a] >> 8 : bits;
                const uint32_t sb = b < count ? known[b] >> 8 : bits;
                uint32_t unsigned_bits = bits;
                uint32_t signed_bits = bits;
                if (op == IR_CONST || op == IR_UNDEF)
                {
                    const uint64_t mask = bits == 64 ? UINT64_MAX : (UINT64_C(1) << bits) - 1;
                    const uint64_t value = op == IR_CONST ? constants[a].low & mask : 0;
                    const uint64_t magnitude = (value >> (bits - 1)) != 0 ? (~value & mask) : value;
                    unsigned_bits = value == 0 ? 0 : 64 - (uint32_t)__builtin_clzll(value);
                    signed_bits = magnitude == 0 ? 1 : 65 - (uint32_t)__builtin_clzll(magnitude);
                }
                else if (op == IR_COPY || op == IR_TRUNC || op == IR_BITCAST || op == IR_INTTOPTR || op == IR_PTRTOINT)
                {
                    // Only equal-width integer bitcasts preserve these facts; masks have no integer facts here.
                    if (a < count && tier1_select_width(types[a]) != 0)
                    {
                        unsigned_bits = ua;
                        signed_bits = sa;
                    }
                }
                else if (op == IR_ZEXT)
                {
                    unsigned_bits = ua;
                }
                else if (op == IR_SEXT)
                {
                    signed_bits = sa;
                    unsigned_bits = ua < ir_type_bits[types[a]] ? ua : bits;
                }
                else if (op == IR_ICMP || op == IR_FCMP)
                {
                    unsigned_bits = 1;
                }
                else if (op == IR_AND || op == IR_OR || op == IR_XOR)
                {
                    unsigned_bits = op == IR_AND ? (ua < ub ? ua : ub) : (ua > ub ? ua : ub);
                    signed_bits = sa > sb ? sa : sb;
                }
                else if (op == IR_NOT)
                {
                    signed_bits = sa;
                }
                else if (op == IR_ADD || op == IR_SUB || op == IR_MUL || op == IR_NEG)
                {
                    const uint32_t maximum_unsigned = ua > ub ? ua : ub;
                    const uint32_t maximum_signed = sa > sb ? sa : sb;
                    unsigned_bits = op == IR_ADD ? maximum_unsigned + 1 : op == IR_MUL ? (ua == 0 || ub == 0 ? 0 : ua + ub) : bits;
                    signed_bits = op == IR_NEG ? sa + 1 : op == IR_MUL ? sa + sb : maximum_signed + 1;
                }
                else if (op == IR_UDIV || op == IR_UREM)
                {
                    unsigned_bits = op == IR_UREM && ub < ua ? ub : ua;
                }
                else if (op == IR_LSHR || op == IR_ASHR || op == IR_SHL)
                {
                    const bool constant = b < count && operations[b] == IR_CONST;
                    const uint64_t shift = constant ? constants[inputs_a[b]].low : 0;
                    if (shift < bits && (op != IR_SHL || constant))
                    {
                        unsigned_bits = op == IR_SHL ? ua + (uint32_t)shift : op == IR_LSHR || ua < bits ? (ua > shift ? ua - (uint32_t)shift : 0) : bits;
                        signed_bits = op == IR_SHL ? sa + (uint32_t)shift : op == IR_ASHR ? (sa > shift ? sa - (uint32_t)shift : 1) : bits;
                    }
                }
                else if (op == IR_SELECT)
                {
                    const uint32_t c = inputs_c[node];
                    unsigned_bits = ub > (known[c] & BUSTER_TIER1_KNOWN_UNSIGNED_MASK) ? ub : known[c] & BUSTER_TIER1_KNOWN_UNSIGNED_MASK;
                    signed_bits = sb > (known[c] >> 8) ? sb : known[c] >> 8;
                }
                else if (op == IR_PHI)
                {
                    unsigned_bits = 0;
                    signed_bits = 1;
                    const uint32_t operands = (uint32_t)ir_operand_count(function, node);
                    for (uint32_t p = 0; p < operands; p += 1)
                    {
                        const uint32_t value = ir_operand(function, node, p, blocks[node]).value;
                        unsigned_bits = unsigned_bits > (known[value] & BUSTER_TIER1_KNOWN_UNSIGNED_MASK) ? unsigned_bits : known[value] & BUSTER_TIER1_KNOWN_UNSIGNED_MASK;
                        signed_bits = signed_bits > (known[value] >> 8) ? signed_bits : known[value] >> 8;
                    }
                }
                unsigned_bits = unsigned_bits < bits ? unsigned_bits : bits;
                signed_bits = signed_bits < bits ? signed_bits : bits;
                const uint16_t previous = known[node];
                unsigned_bits = unsigned_bits < (previous & BUSTER_TIER1_KNOWN_UNSIGNED_MASK) ? unsigned_bits : previous & BUSTER_TIER1_KNOWN_UNSIGNED_MASK;
                signed_bits = signed_bits < (previous >> 8) ? signed_bits : previous >> 8;
                // A zero sign bit is also a signed-range proof (including zero, which needs one signed bit).
                signed_bits = unsigned_bits < signed_bits ? unsigned_bits + 1 : signed_bits;
                const uint16_t endpoint = op == IR_COPY && type == IR_I32 && types[a] == IR_I32 ? known[a] & BUSTER_TIER1_KNOWN_BELOW_MAX : 0;
                const uint16_t next = (uint16_t)(unsigned_bits | (signed_bits << 8) | (previous & BUSTER_TIER1_KNOWN_BELOW_MAX) | endpoint);
                changed |= previous != next;
                known[node] = next;
            }
        }
        if (!changed)
        {
            break;
        }
    }
}

static void tier1_select_known_bits(const IrFunction* function, const IrDominance* dominance, const uint32_t* blocks, uint32_t* workspace, uint16_t* known, bool vector)
{
    tier1_select_known_bits_passes(function, blocks, known, vector, true);
    if (tier1_select_induction_bounds(function, dominance, blocks, workspace, known, vector))
    {
        tier1_select_known_bits_passes(function, blocks, known, vector, false);
    }
}

#include "tier1_select_storage.c"

// A narrow SSA value, or an I32, is canonical when its physical register holds zeros past its type's width. I64/PTR
// satisfy this vacuously. These narrow producers write all 32 bits themselves:
//   IR_CONST, IR_UNDEF   the type's masked bits, in a 32-bit move (or zero)
//   IR_PARAM             a narrow parameter's entry MOVZX, before any read (tier1_emit_cfg.c; narrow parameters keep the
//                        function out of the straight-line consumer)
//   IR_LOAD              MOVZX_R32_RM8/RM16 (byte/word loads zero-extend)
//   IR_TRUNC             MOVZX_R32_RM8/RM16 (a truncation to I8/I16)
//   IR_SEXT              MOVSX, then the I16 MOVZX
//   IR_ICMP, IR_FCMP     zeroed general-register storage, then setcc (FCMP's == and != combine the parity's too)
//   IR_CALL/INDIRECT     a narrow result's MOVZX from RAX (tier1_emit.c)
//   IR_FPTOSI/FPTOUI     cvtts*2si, then MOVZX_R32_RM8/RM16 (a narrow result's, #141)
// I32 ALU operations and ordinary 32-bit loads clear the upper half; I32 parameters, calls and rows do not promise it.
// A requested signed-load representation instead preserves the sign in all 64 bits and in eight-byte spill storage.
// A zero-count shift, multiplication/division by one and a truncation can coalesce without a 32-bit write, so they
// need their operand's proof. COPY, BITCAST, ZEXT, SELECT and PHI propagate only proven storage properties.
// Any other producer is left noncanonical: a copy of it is noncanonical too
// and a zero extension of it keeps its MOVZX, which is the safe answer for a rule this selector doesn't know yet.
// The passes are bounded; an unresolved phi chain stays noncanonical, also safely. The narrow fixtures' whole-register
// probes (tier1_narrow_test.c) check the supported producers natively. Inlined by force into the plan: the bottom-up
// rewrite's check (tier1_select_burs_test.c) calls it too, and with a second caller clang outlined it.
static inline __attribute__((always_inline)) void tier1_select_canonical(const IrFunction* function, const uint32_t* blocks, const uint16_t* known, uint8_t* canonical, uint32_t* workspace, bool vector)
{
    const uint32_t count = function->instruction_count;
    for (uint32_t node = 0; node < count; node += 1)
    {
        const uint32_t type = function->type[node];
        canonical[node] = type == IR_I64 || type == IR_PTR;
        if (tier1_select_narrow_width(type) != 0 || type == IR_I32)
        {
            switch (function->op[node])
            {
            case IR_CONST:
            case IR_UNDEF:
            case IR_LOAD:
            case IR_SEXT:
            case IR_ICMP:
            case IR_FCMP:
            case IR_FPTOSI:
                canonical[node] = 1;
                break;
            case IR_FPTOUI:
                // The unsigned I32 conversion uses a 64-bit cvtt instruction, rather than a 32-bit write.
                canonical[node] = type != IR_I32;
                break;
            case IR_PARAM:
            case IR_CALL_INDIRECT:
                canonical[node] = type != IR_I32;
                break;
            case IR_TRUNC:
                // Truncation to I32 can coalesce without writing, retaining the source's whole upper half.
                canonical[node] = type != IR_I32 || (known[function->a[node]] & BUSTER_TIER1_KNOWN_UNSIGNED_MASK) <= 32;
                break;
            case IR_ADD:
            case IR_SUB:
            case IR_AND:
            case IR_OR:
            case IR_XOR:
            case IR_NEG:
            case IR_NOT:
            case IR_INTRINSIC:
                canonical[node] = type == IR_I32;
                break;
            // A row call's narrow result (#141) is its field's, whose bits past the class are the row's: not canonical.
            case IR_CALL:
                canonical[node] = type != IR_I32 && function->calls[function->b[node]].row == 0;
                break;
            // A mask's kmovb or kmovw into a 32-bit register (TIER1_RULE_MASK_TO_INTEGER, #179) zero-extends.
            case IR_BITCAST:
                canonical[node] = function->a[node] < count && (tier1_select_mask_width(function->type[function->a[node]]) != 0 || (type == IR_I32 && function->type[function->a[node]] == IR_F32));
                break;
            default:
                break;
            }
        }
    }
    tier1_select_sign_requests(function, known, canonical, workspace, vector);
    for (uint32_t pass = 0; pass < BUSTER_TIER1_SELECT_CANONICAL_PASSES; pass += 1)
    {
        bool changed = false;
        for (uint32_t node = 0; node < count; node += 1)
        {
            if ((canonical[node] & BUSTER_TIER1_STORAGE_ZERO) == 0 &&
                (function->type[node] != IR_I32 || (canonical[node] & BUSTER_TIER1_STORAGE_SIGN) == 0) &&
                (tier1_select_narrow_width(function->type[node]) != 0 || function->type[node] == IR_I32))
            {
                const uint32_t op = function->op[node];
                const uint32_t a = function->a[node];
                bool value = false;
                if (op == IR_COPY || op == IR_BITCAST || op == IR_ZEXT)
                {
                    value = a < count && (canonical[a] & BUSTER_TIER1_STORAGE_ZERO) != 0;
                }
                else if (op == IR_SELECT)
                {
                    value = function->b[node] < count && function->c[node] < count && (canonical[function->b[node]] & BUSTER_TIER1_STORAGE_ZERO) != 0 && (canonical[function->c[node]] & BUSTER_TIER1_STORAGE_ZERO) != 0;
                }
                else if (op == IR_PHI)
                {
                    value = true;
                    const uint32_t operand_count = (uint32_t)ir_operand_count(function, node);
                    for (uint32_t p = 0; p < operand_count; p += 1)
                    {
                        const uint32_t operand = ir_operand(function, node, p, blocks[node]).value;
                        value = value && operand < count && (canonical[operand] & BUSTER_TIER1_STORAGE_ZERO) != 0;
                    }
                }
                else if (function->type[node] == IR_I32 && (op == IR_MUL || op == IR_SHL || op == IR_LSHR || op == IR_ASHR || op == IR_SDIV || op == IR_UDIV || op == IR_SREM || op == IR_UREM))
                {
                    const uint32_t b = function->b[node];
                    const bool constant = b < count && function->op[b] == IR_CONST;
                    const uint64_t immediate = constant ? (uint32_t)function->constants[function->a[b]].low : 0;
                    // A shift by zero (also a dynamic count), multiplication by one or division by one can leave
                    // the destination untouched. Their source must already have the required upper half.
                    const bool shifts = op == IR_SHL || op == IR_LSHR || op == IR_ASHR;
                    const bool identity = shifts ? !constant || (immediate & 31) == 0 : constant && immediate == 1 && op != IR_SREM && op != IR_UREM;
                    value = !identity || (canonical[a] & BUSTER_TIER1_STORAGE_ZERO) != 0;
                    if (op == IR_MUL && function->op[a] == IR_CONST && (uint32_t)function->constants[function->a[a]].low == 1)
                    {
                        value = (canonical[b] & BUSTER_TIER1_STORAGE_ZERO) != 0;
                    }
                }
                if (value)
                {
                    canonical[node] |= BUSTER_TIER1_STORAGE_ZERO;
                    changed = true;
                }
            }
            if (function->type[node] == IR_I32)
            {
                const uint8_t before = canonical[node];
                uint8_t facts = before;
                const uint32_t op = function->op[node];
                const uint32_t a = function->a[node];
                bool sign = false;
                if (op == IR_COPY && function->type[a] == IR_I32)
                {
                    sign = (canonical[a] & BUSTER_TIER1_STORAGE_SIGN) != 0;
                }
                else if (op == IR_SELECT)
                {
                    sign = (canonical[function->b[node]] & canonical[function->c[node]] & BUSTER_TIER1_STORAGE_SIGN) != 0;
                }
                else if (op == IR_PHI)
                {
                    sign = true;
                    const uint32_t operands = (uint32_t)ir_operand_count(function, node);
                    for (uint32_t operand = 0; operand < operands; operand += 1)
                    {
                        const uint32_t value = ir_operand(function, node, operand, blocks[node]).value;
                        sign &= (canonical[value] & BUSTER_TIER1_STORAGE_SIGN) != 0;
                    }
                }
                facts |= sign ? BUSTER_TIER1_STORAGE_SIGN : 0;
                if ((known[node] & BUSTER_TIER1_KNOWN_UNSIGNED_MASK) < 32 && facts != 0)
                {
                    // For a nonnegative I32, sign-extended and zero-extended storage are the same bits.
                    facts |= BUSTER_TIER1_STORAGE_ZERO | BUSTER_TIER1_STORAGE_SIGN;
                }
                changed |= facts != before;
                canonical[node] = facts;
            }
        }
        if (!changed)
        {
            break;
        }
    }
    tier1_select_storage_components(function, canonical, workspace, vector);
}

// A call's argument's type: a fixed parameter's, or an unnamed one's (a variadic call's past its parameters) its value's,
// since call_types holds the fixed parameters' alone.
static uint32_t tier1_select_argument_type(const IrFunction* function, const IrCall* call, uint32_t argument)
{
    return argument < call->parameter_count ? function->call_types[call->type_first + argument] : function->type[function->extra[call->argument_first + argument]];
}

// Where a parameter or an argument is, by the System V convention's classes (#141): the k-th integer or pointer one in
// the k-th of the six general argument registers (`index` k, tier1_select_parameter_registers'), the k-th floating one
// in xmm k of eight, and each one past its class's registers in the stack's next 8-byte slot (`index` the slot's).
#define BUSTER_TIER1_PLACE_GENERAL 0
// A static chain's general place (#148): r10, past the six argument registers.
#define BUSTER_TIER1_SELECT_CHAIN_INDEX 6
#define BUSTER_TIER1_PLACE_VECTOR 1
#define BUSTER_TIER1_PLACE_STACK 2
// A by-value aggregate's place (#789): its ABI record says where its bytes go (tier1_select_location_parts, or its stack
// slots).
#define BUSTER_TIER1_PLACE_AGGREGATE 3

typedef struct Tier1SelectPlace Tier1SelectPlace;
struct Tier1SelectPlace
{
    uint8_t kind;
    uint8_t index;
};

// The places taken so far, as a list of parameters or arguments is walked in order.
typedef struct Tier1SelectPlaces Tier1SelectPlaces;
struct Tier1SelectPlaces
{
    uint32_t general;
    uint32_t vector;
    uint32_t stack;
};

static Tier1SelectPlace tier1_select_next_place(Tier1SelectPlaces* places, uint32_t type)
{
    // A long double (#141) is MEMORY's: on the stack always, its 16 bytes in two slots aligned to 16.
    const bool x87 = type == IR_F80;
    const bool floating = type == IR_F32 || type == IR_F64;
    const bool registered = !x87 && (floating ? places->vector < BUSTER_ABI_VECTOR_ARGUMENTS : places->general < BUSTER_ABI_INTEGER_ARGUMENTS);
    places->stack = x87 ? (places->stack + 1) & ~1u : places->stack;
    const uint32_t index = !registered ? places->stack : floating ? places->vector : places->general;
    const Tier1SelectPlace place = {
        .kind = (uint8_t)(!registered ? BUSTER_TIER1_PLACE_STACK : floating ? BUSTER_TIER1_PLACE_VECTOR : BUSTER_TIER1_PLACE_GENERAL),
        .index = (uint8_t)index,
    };
    places->general += registered && !floating;
    places->vector += registered && floating;
    places->stack += !registered ? (x87 ? 2u : 1u) : 0u;
    return place;
}

// Parameter `parameter`'s ABI record (#789), where the function has them (IrFunction.parameter_passed), else 0.
static const IrPassed* tier1_select_parameter_record(const IrFunction* function, uint32_t parameter)
{
    return function->parameter_passed != 0 && parameter < function->parameter_count ? &function->passed[function->parameter_passed - 1 + parameter] : 0;
}

// A call's argument's ABI record (#789), where the call has them (IrCall.abi), else 0.
static const IrPassed* tier1_select_argument_record(const IrFunction* function, const IrCall* call, uint32_t argument)
{
    return call->abi != 0 ? &function->passed[function->abi_calls[call->abi - 1].passed + argument] : 0;
}

// Whether a call passes a static chain (#148, __builtin_call_with_static_chain's): its argument 0's record in r10. An
// indirect one calls through rax (tier1_select_callee_register), since its chain takes r10 and the moves' scratch r11.
static bool tier1_select_call_chained(const IrFunction* function, const IrCall* call)
{
    const IrPassed* record = call->argument_count != 0 ? tier1_select_argument_record(function, call, 0) : 0;
    return record != 0 && record->location.kind == BUSTER_ABI_LOCATION_REGISTERS && record->location.registers[0] == BUSTER_ABI_R10;
}

// The register an indirect call calls through: r10, which no argument takes, or with a static chain (#148) rax, which
// none does either and which a chained call's al doesn't take (ir_lower refuses a chained indirect variadic call).
static uint8_t tier1_select_callee_register(const IrFunction* function, const IrCall* call)
{
    return tier1_select_call_chained(function, call) ? X86_REGISTER_RAX : X86_REGISTER_R10;
}

// Whether an ABI record is a by-value aggregate's (#789): its bytes aren't 0, or (#141) it is passed in no place at all
// (IGNORE: an empty struct, or one of unnamed bit-fields), which only an aggregate is.
static bool tier1_select_aggregate_record(const IrPassed* record)
{
    return record != 0 && (record->size != 0 || record->location.kind == BUSTER_ABI_LOCATION_IGNORE);
}

// Whether parameter `parameter` is a by-value aggregate (#789, tier1_select_aggregate_record), whose IR value is a ptr to
// its bytes, the function's own copy (tier1_emit_cfg.c's frame object, or its stack slots in the incoming area).
static bool tier1_select_aggregate_parameter(const IrFunction* function, uint32_t parameter)
{
    return tier1_select_aggregate_record(tier1_select_parameter_record(function, parameter));
}

// Whether node `node` is a parameter where the entry puts it, in its incoming register or slot: IR_PARAM, but a by-value
// aggregate's, whose value is its copy's address, computed at its node as a frame object's is (TIER1_RULE_STACK_ADDRESS).
static bool tier1_select_entry_parameter(const IrFunction* function, uint32_t node)
{
    return function->op[node] == IR_PARAM && !tier1_select_aggregate_parameter(function, function->a[node]);
}

// The place an ABI record gives a value (#789): a by-value aggregate's own, a register's (a general one's index among
// the argument registers, an xmm one's number), or a stack slot's (its offset's 8-byte slot). A static chain (#148), in
// r10, is general index 6, past the argument registers (tier1_select_parameter_registers).
static Tier1SelectPlace tier1_select_record_place(const IrPassed* record)
{
    const uint8_t reg = record->location.registers[0];
    uint32_t index = 0;
    while (index < BUSTER_ABI_INTEGER_ARGUMENTS && abi_integer_arguments[index] != reg)
    {
        index += 1;
    }
    index = reg == BUSTER_ABI_R10 ? BUSTER_TIER1_SELECT_CHAIN_INDEX : index;
    const bool vector = reg != BUSTER_ABI_REGISTER_NONE && (reg & 0xe0) == BUSTER_ABI_REGISTER_VECTOR;
    const uint8_t kind = tier1_select_aggregate_record(record)                   ? BUSTER_TIER1_PLACE_AGGREGATE
                         : record->location.kind == BUSTER_ABI_LOCATION_STACK ? BUSTER_TIER1_PLACE_STACK
                         : vector                                              ? BUSTER_TIER1_PLACE_VECTOR
                                                                               : BUSTER_TIER1_PLACE_GENERAL;
    const uint32_t value = kind == BUSTER_TIER1_PLACE_STACK ? record->location.stack_offset / BUSTER_ABI_SLOT_BYTES : kind == BUSTER_TIER1_PLACE_VECTOR ? reg & 0x1fu : kind == BUSTER_TIER1_PLACE_GENERAL ? index : 0;
    const Tier1SelectPlace place = { .kind = kind, .index = (uint8_t)value };
    return place;
}

// Parameter `parameter`'s place: its ABI record's, where the function has them, else by the types of the function's
// parameters up to it.
static Tier1SelectPlace tier1_select_parameter_place(const IrFunction* function, uint32_t parameter)
{
    const IrPassed* record = tier1_select_parameter_record(function, parameter);
    Tier1SelectPlaces places = { 0 };
    Tier1SelectPlace place = { 0 };
    for (uint32_t p = 0; record == 0 && p <= parameter && p < function->parameter_count; p += 1)
    {
        place = tier1_select_next_place(&places, function->parameter_types[p]);
    }
    return record != 0 ? tier1_select_record_place(record) : place;
}

// A call's argument's place: its ABI record's, where the call has them, else the next of `places` by its type, as the
// arguments are walked in order.
static Tier1SelectPlace tier1_select_argument_place(const IrFunction* function, const IrCall* call, uint32_t argument, Tier1SelectPlaces* places)
{
    const IrPassed* record = tier1_select_argument_record(function, call, argument);
    return record != 0 ? tier1_select_record_place(record) : tier1_select_next_place(places, tier1_select_argument_type(function, call, argument));
}

// The bytes a call's arguments take in its outgoing area: its records' total where it has them (#789), else the 8-byte
// slots of those past their classes' registers.
static uint32_t tier1_select_call_stack_bytes(const IrFunction* function, const IrCall* call)
{
    Tier1SelectPlaces places = { 0 };
    for (uint32_t a = 0; call->abi == 0 && a < call->argument_count; a += 1)
    {
        tier1_select_next_place(&places, tier1_select_argument_type(function, call, a));
    }
    return call->abi != 0 ? function->abi_calls[call->abi - 1].call.stack_bytes : BUSTER_ABI_SLOT_BYTES * places.stack;
}

// An aggregate's part (#789): its register, an ABI one (a general register's number, or an xmm one's plus
// BUSTER_ABI_REGISTER_VECTOR), or BUSTER_ABI_REGISTER_NONE for its location's stack slot (#141: a SPLIT location's part
// with no register, or a STACK location's high or low eightbyte alone), and the bytes it holds from its offset in the
// aggregate.
typedef struct Tier1SelectPart Tier1SelectPart;
struct Tier1SelectPart
{
    uint8_t reg;
    uint8_t offset;
    uint8_t bytes;
};

// The general argument registers', and the general result registers', bits.
#define BUSTER_TIER1_SELECT_ARGUMENT_GENERAL ((1u << BUSTER_ABI_RDI) | (1u << BUSTER_ABI_RSI) | (1u << BUSTER_ABI_RDX) | (1u << BUSTER_ABI_RCX) | (1u << BUSTER_ABI_R8) | (1u << BUSTER_ABI_R9))
// A value's record may be r10 too: a static chain's (#148, tier1_select_call_chained), which no aggregate's part is.
#define BUSTER_TIER1_SELECT_VALUE_GENERAL (BUSTER_TIER1_SELECT_ARGUMENT_GENERAL | (1u << BUSTER_ABI_R10))
#define BUSTER_TIER1_SELECT_RESULT_GENERAL ((1u << BUSTER_ABI_RAX) | (1u << BUSTER_ABI_RDX))
// The largest aggregate a call copies whole into its stack slots, and the outgoing area's bytes a stack place's index
// reaches (#789).
#define BUSTER_TIER1_SELECT_MAX_STACK_AGGREGATE 512u
#define BUSTER_TIER1_SELECT_MAX_STACK_OFFSET (256u * BUSTER_ABI_SLOT_BYTES)
// The largest aggregate result a return copies through the hidden pointer, 8 bytes at a time (#789).
#define BUSTER_TIER1_SELECT_MAX_RESULT_COPY 4096u
// The largest constant size an allocation folds (#141): rounded up to any frame's alignment, it fits a sub's imm32.
#define BUSTER_TIER1_SELECT_MAX_ALLOCA_IMMEDIATE (UINT64_C(1) << 30)

// An aggregate of `size` bytes in `registers` (a location's two, or a result's first two) as its parts (#789), as Tier 0's
// tier0_parts_of has them: eightbyte k in registers[k], 8 bytes or what is left of the aggregate, and none for a
// register that is none, the high eightbyte past 8 bytes being padding; with `high` (#141) eightbyte 1 alone in
// registers[0], its low one padding; with `slot` (#141) a SPLIT location's two eightbytes, one in a register and the other
// in its stack slot, or with `high` or `low` a STACK location's one eightbyte in its slot (the low one alone of a larger
// value), each part with no register its slot's. Returns how many, into `parts`, or 0 for a shape Tier 1 doesn't take: no
// part in a register (a whole STACK location's), a second register for 8 bytes or less, more than 16 bytes (a value in
// one vector register whole, which only a vector makes, among them), or a register outside the general ones in `general`
// and the first `vectors` xmm ones.
static uint32_t tier1_select_parts_of(const uint8_t* registers, bool high, bool low, bool slot, uint32_t size, uint32_t general, uint32_t vectors, Tier1SelectPart* parts)
{
    const bool one = high || low;
    const bool first = registers[0] != BUSTER_ABI_REGISTER_NONE;
    const bool second = registers[1] != BUSTER_ABI_REGISTER_NONE;
    const bool shape = one ? !(high && low) && !second && size > 8 && first != slot : slot ? first != second && size > 8 : first && (!second || size > 8);
    // A vector register alone of 16 bytes or more holds the aggregate whole (#141), as Tier 0's tier0_parts_of has it: a
    // vector's (SSE then SSEUP), 16 bytes in an xmm register, or 32 or 64 in a ymm or zmm one where the target's registers
    // are that wide; a struct's whose high eightbyte is padding too, whose bytes past the low eightbyte it carries, and its
    // copy keeps them as padding.
    const bool vector_first = first && (registers[0] & 0xe0) == BUSTER_ABI_REGISTER_VECTOR;
    const uint32_t whole = !one && !slot && vector_first && !second && size >= 16 ? (size >= 64 ? 64 : size >= 32 ? 32 : 16) : 0;
    bool valid = size != 0 && (size <= 16 || (whole != 0 && whole == size)) && shape;
    uint32_t count = 0;
    for (uint32_t k = 0; valid && k < (one ? 1u : 2u); k += 1)
    {
        const uint8_t reg = registers[k];
        if (reg != BUSTER_ABI_REGISTER_NONE || slot)
        {
            const bool vector = reg != BUSTER_ABI_REGISTER_NONE && (reg & 0xe0) == BUSTER_ABI_REGISTER_VECTOR;
            const uint32_t offset = high ? 8 : 8 * k;
            const uint32_t left = size - offset;
            valid = reg == BUSTER_ABI_REGISTER_NONE || (vector ? (reg & 0x1fu) < vectors : reg < 16 && ((general >> reg) & 1) != 0);
            parts[count] = (Tier1SelectPart){ .reg = reg, .offset = (uint8_t)offset, .bytes = (uint8_t)(whole != 0 ? whole : left < 8 ? left : 8) };
            count += 1;
        }
    }
    return valid ? count : 0;
}

// Whether an aggregate argument's or parameter's `location` has it on the stack whole, its bytes in its slots from its
// first: STACK, but for the high or the low eightbyte alone (HIGH, LOW, #141), each in one slot.
static bool tier1_select_location_whole(AbiLocation location)
{
    return location.kind == BUSTER_ABI_LOCATION_STACK && (location.flags & (BUSTER_ABI_FLAG_HIGH | BUSTER_ABI_FLAG_LOW)) == 0;
}

// The parts of an aggregate argument or parameter of `size` bytes at `location` (#789, #141): in argument registers, a
// SPLIT location's register and stack slot, or a STACK location's high or low eightbyte alone in its slot
// (tier1_select_parts_of); none on the stack whole or ignored.
static uint32_t tier1_select_location_parts(AbiLocation location, uint32_t size, Tier1SelectPart* parts)
{
    const bool high = (location.flags & BUSTER_ABI_FLAG_HIGH) != 0;
    const bool low = (location.flags & BUSTER_ABI_FLAG_LOW) != 0;
    const bool registers = location.kind == BUSTER_ABI_LOCATION_REGISTERS && !low;
    const bool split = location.kind == BUSTER_ABI_LOCATION_SPLIT && !high && !low;
    const bool slot = location.kind == BUSTER_ABI_LOCATION_STACK && (high || low);
    const bool taken = registers || split || slot;
    return taken ? tier1_select_parts_of(location.registers, high, low, location.kind != BUSTER_ABI_LOCATION_REGISTERS, size, BUSTER_TIER1_SELECT_ARGUMENT_GENERAL, BUSTER_ABI_VECTOR_ARGUMENTS, parts) : 0;
}

// The 8-byte staging slots a part of `bytes` takes below rsp (#789, tier1_emit_staging): one an eightbyte's, and a whole
// vector register's (#141) its bytes'.
static uint32_t tier1_select_part_slots(uint32_t bytes)
{
    return bytes <= 8 ? 1 : bytes / 8;
}

// A call argument's whole 64-byte vector register part (#141) waits in zmm16 and on rather than in the red zone, whose 14
// staging slots two such parts would overflow: Tier 1 allocates nothing to registers past xmm15, a call clobbers them all,
// and only a target with AVX-512F, which has them, passes a vector in a zmm register. The first of them, by the call's
// parts in order.
#define BUSTER_TIER1_SELECT_PART_ZMM 16u

static bool tier1_select_part_in_zmm(uint32_t bytes)
{
    return bytes == 64;
}

// The red zone's staging slots a call argument's part takes (tier1_select_part_slots), none for one in zmm16 and on.
static uint32_t tier1_select_argument_part_slots(uint32_t bytes)
{
    return tier1_select_part_in_zmm(bytes) ? 0 : tier1_select_part_slots(bytes);
}

// The form that loads (`load`) or stores a vector register's part of `bytes` (#789, #141) whole: an eightbyte's or less
// movsd (its bytes past the part's padding), and a whole register's movups, vmovdqu (AVX's) or vmovdqu64 (AVX512F's), as
// the convention passes it at the target's width, whose forms the target then has.
static uint16_t tier1_select_part_form(uint32_t bytes, bool load)
{
    return bytes <= 8 ? (load ? X86_FORM_MOVSD_X_XM64 : X86_FORM_MOVSD_XM64_X) : bytes == 16 ? (load ? X86_FORM_MOVUPS_X_XM : X86_FORM_MOVUPS_XM_X) :
           bytes == 32                                                        ? (load ? X86_FORM_VMOVDQU_Y_YM256 : X86_FORM_VMOVDQU_YM256_Y) :
           bytes == 64                                                        ? (load ? X86_FORM_VMOVDQU64_Z_ZM512 : X86_FORM_VMOVDQU64_ZM512_Z) :
                                                                                X86_FORM_NONE;
}

// The most parts a result has (#1624): a vector's split across xmm0 to xmm3.
#define BUSTER_TIER1_SELECT_RESULT_PARTS 4u
// The red zone's 8-byte slots, its 128 bytes below rsp, where a function's own result parts wait on the way out
// (tier1_emit_cfg_aggregate_result, #1847): those that aren't a whole vector register's (tier1_select_part_direct), at
// most two eightbytes' in rax and rdx or xmm0 and xmm1.
#define BUSTER_TIER1_SELECT_RED_ZONE_SLOTS 16u

// Whether a function's own result part is loaded straight from the result's address on the way out (#1847): a whole
// vector register's, 16, 32 or 64 bytes of a split vector (tier1_select_result_parts), which no byte past the object's
// widens and whose load can't overwrite the address, a general register. Any other part is staged in the red zone.
static bool tier1_select_part_direct(Tier1SelectPart part)
{
    return (part.reg & 0xe0) == BUSTER_ABI_REGISTER_VECTOR && part.bytes >= 16;
}

// The parts of an aggregate result of `size` bytes in rax and rdx or xmm0 and xmm1 (#789), eightbyte 1 alone with HIGH
// (#141), or a vector's wider than the target's registers (#1624, tier0_vector_result_parts' shape, #1193): split
// across the first two or four vector registers, 16, 32 or 64 bytes each (xmm's without AVX, ymm's with it, zmm's with
// AVX512F: a 128-byte vector's, and a 256-byte one's four, #1847), part k at byte k * part_bytes of its object, which the
// function's own return loads straight from the result's address (tier1_select_part_direct). None for any other result. `parts` has room for BUSTER_TIER1_SELECT_RESULT_PARTS.
static uint32_t tier1_select_result_parts(AbiReturn result, uint32_t size, Tier1SelectPart* parts)
{
    uint32_t vectors = 0;
    while (vectors < BUSTER_ABI_RESULT_PARTS && result.registers[vectors] == (uint8_t)(BUSTER_ABI_REGISTER_VECTOR | vectors))
    {
        vectors += 1;
    }
    bool rest = true;
    for (uint32_t k = vectors; k < BUSTER_ABI_RESULT_PARTS; k += 1)
    {
        rest = rest && result.registers[k] == BUSTER_ABI_REGISTER_NONE;
    }
    const bool split = result.kind == BUSTER_ABI_RESULT_REGISTERS && (result.flags & BUSTER_ABI_FLAG_HIGH) == 0 && (vectors == 2 || vectors == 4) && rest &&
                       (result.part_bytes == 16 || result.part_bytes == 32 || result.part_bytes == 64) && size == vectors * result.part_bytes;
    const bool registers = !split && result.kind == BUSTER_ABI_RESULT_REGISTERS && result.registers[2] == BUSTER_ABI_REGISTER_NONE && result.registers[3] == BUSTER_ABI_REGISTER_NONE;
    uint32_t count = registers ? tier1_select_parts_of(result.registers, (result.flags & BUSTER_ABI_FLAG_HIGH) != 0, false, false, size, BUSTER_TIER1_SELECT_RESULT_GENERAL, 2, parts) : 0;
    for (uint32_t k = 0; split && k < vectors; k += 1)
    {
        parts[k] = (Tier1SelectPart){ .reg = result.registers[k], .offset = (uint8_t)(k * result.part_bytes), .bytes = result.part_bytes };
        count += 1;
    }
    return count;
}

// The first red-zone staging slot of result part k (#1624): the slots of the staged parts before it, each an eightbyte's
// one (tier1_select_part_slots), past those loaded straight from the result's address (#1847, tier1_select_part_direct),
// which take none. Of k = the part count, the slots they all take, which BUSTER_TIER1_SELECT_RED_ZONE_SLOTS bounds
// (tier1_select_result_staged).
static uint32_t tier1_select_result_slot(const Tier1SelectPart* parts, uint32_t k)
{
    uint32_t slot = 0;
    for (uint32_t j = 0; j < k; j += 1)
    {
        slot += tier1_select_part_direct(parts[j]) ? 0 : tier1_select_part_slots(parts[j].bytes);
    }
    return slot;
}

// Whether a function's own result parts, `count` of them, fit the red zone's slots, as its return stages them all at
// once (#1847, the #1645 review's bound): tier1_select_result_parts admits no other, and its consumer and the code's
// certificate refuse one.
static bool tier1_select_result_staged(const Tier1SelectPart* parts, uint32_t count)
{
    return count <= BUSTER_TIER1_SELECT_RESULT_PARTS && tier1_select_result_slot(parts, count) <= BUSTER_TIER1_SELECT_RED_ZONE_SLOTS;
}

// Whether Tier 1 takes an argument's or a parameter's ABI record for a value of IR type `type` (#789): a scalar in one
// register of its class (a general argument register for an integer or a pointer, xmm0 to xmm7 for a floating value) or
// in a stack slot, its extension the IR's; or a by-value aggregate, a ptr to its bytes, on the stack whole or in its
// parts (tier1_select_location_parts): in registers, and since #141 a part LLVM put in a stack slot (SPLIT) and a
// value's high or low eightbyte alone (HIGH, LOW); or one passed in no place (IGNORE, #141), which a call copies
// nothing for and a function's copy of has no bytes it must keep.
static bool tier1_select_record_supported(const IrPassed* record, uint32_t type)
{
    const AbiLocation location = record->location;
    const uint8_t reg = location.registers[0];
    const bool floating = tier1_select_float_width(type) != 0;
    const bool vector = reg != BUSTER_ABI_REGISTER_NONE && (reg & 0xe0) == BUSTER_ABI_REGISTER_VECTOR;
    const bool stacked = location.kind == BUSTER_ABI_LOCATION_STACK && location.stack_offset % BUSTER_ABI_SLOT_BYTES == 0 && location.stack_offset < BUSTER_TIER1_SELECT_MAX_STACK_OFFSET;
    Tier1SelectPart parts[2];
    bool supported = false;
    if (location.kind == BUSTER_ABI_LOCATION_IGNORE)
    {
        // Whatever its IR type: an empty struct's argument, of no bytes, is its class's value (ir_typed_call).
        supported = location.flags == 0;
    }
    else if (record->size == 0)
    {
        const bool registered = location.kind == BUSTER_ABI_LOCATION_REGISTERS && location.registers[1] == BUSTER_ABI_REGISTER_NONE &&
                                (floating ? vector && (reg & 0x1fu) < BUSTER_ABI_VECTOR_ARGUMENTS : reg < 16 && ((BUSTER_TIER1_SELECT_VALUE_GENERAL >> reg) & 1) != 0);
        // A long double (#141) on the stack alone, its object's bytes copied into its 16 (tier1_emit_cfg_stack_argument).
        supported = (((floating || type == IR_I32 || type == IR_I64 || type == IR_PTR) && (registered || stacked)) || (type == IR_F80 && stacked)) &&
                    (location.flags & ~(BUSTER_ABI_FLAG_SIGN | BUSTER_ABI_FLAG_ZERO)) == 0;
    }
    else
    {
        // A part in a stack slot has it where a whole one's first slot would be.
        const bool slotted = location.stack_offset % BUSTER_ABI_SLOT_BYTES == 0 && location.stack_offset < BUSTER_TIER1_SELECT_MAX_STACK_OFFSET;
        const bool whole = tier1_select_location_whole(location);
        const uint32_t count = tier1_select_location_parts(location, record->size, parts);
        bool placed = count != 0;
        for (uint32_t k = 0; k < count; k += 1)
        {
            placed &= parts[k].reg != BUSTER_ABI_REGISTER_NONE || slotted;
        }
        supported = type == IR_PTR && (location.flags & ~(BUSTER_ABI_FLAG_HIGH | BUSTER_ABI_FLAG_LOW)) == 0 && (placed || (whole && stacked && record->size <= BUSTER_TIER1_SELECT_MAX_STACK_AGGREGATE));
    }
    return supported;
}

// An aggregate result of `size` bytes in the x87's registers (#141): st0, a struct of a long double's, and st1 too, a
// _Complex long double's, each part 16 bytes of the aggregate (part_bytes 16: a vector of long doubles, whose parts LLVM
// packs into 10 bytes, isn't one). How many parts, or 0.
static uint32_t tier1_select_x87_parts(AbiReturn result, uint32_t size)
{
    const bool first = result.kind == BUSTER_ABI_RESULT_REGISTERS && result.registers[0] == BUSTER_ABI_REGISTER_X87 && result.part_bytes == 16 && result.registers[2] == BUSTER_ABI_REGISTER_NONE &&
                       result.registers[3] == BUSTER_ABI_REGISTER_NONE;
    const uint32_t count = !first ? 0 : result.registers[1] == (BUSTER_ABI_REGISTER_X87 | 1) ? 2 : result.registers[1] == BUSTER_ABI_REGISTER_NONE ? 1 : 0;
    return count != 0 && size > 16 * (count - 1) && size <= 16 * count ? count : 0;
}

// Whether Tier 1 takes a result as the convention returns it (#789): a scalar's (`size` 0) as the IR's result type says,
// and an aggregate's of `size` bytes in rax and rdx or xmm0 and xmm1 as its parts, a high eightbyte's alone among them
// (tier1_select_result_parts, #141), in st0 and st1 (tier1_select_x87_parts, #141), or through the hidden pointer
// (MEMORY), or nowhere (IGNORE, #141: an empty struct's, which neither the call nor the return copies). Not one LLVM
// returns through a hidden pointer that clang doesn't count (DEMOTED), which only a vector makes.
static bool tier1_select_result_supported(AbiReturn result, uint32_t size)
{
    Tier1SelectPart parts[BUSTER_TIER1_SELECT_RESULT_PARTS];
    const uint32_t high = size != 0 ? BUSTER_ABI_FLAG_HIGH : 0;
    const bool flags = (result.flags & ~(BUSTER_ABI_FLAG_SIGN | BUSTER_ABI_FLAG_ZERO | high)) == 0;
    const bool registers = tier1_select_result_parts(result, size, parts) != 0;
    return flags && (size == 0 || registers || tier1_select_x87_parts(result, size) != 0 || result.kind == BUSTER_ABI_RESULT_MEMORY || result.kind == BUSTER_ABI_RESULT_IGNORE);
}

static bool tier1_select_stack_object(const IrFunction* function, uint32_t slot);

// The type a call's argument without an ABI record may have, and its result's, and whether its arguments match its
// parameters (all of them, or a variadic call's named ones and more), in a call the selector takes
// (tier1_select_call_supported). The C driver's refusal report names a refused call's kinds by them and by the ABI's
// below (check_tier1_call_kinds), so that it follows this boundary as it grows (#1116). A long double argument (#141) is
// in its stack slots, from its object.
static bool tier1_select_call_argument(uint32_t type)
{
    return type == IR_I32 || type == IR_I64 || type == IR_PTR || tier1_select_float_width(type) != 0 || type == IR_F80;
}

// A narrow result is zero-extended after the call, and a floating one (#141) comes in xmm0. A long double's (#141) comes
// in st0, which the CFG consumer stores into the call's object.
static bool tier1_select_call_result(uint32_t type)
{
    return type == IR_VOID || type == IR_I1 || type == IR_I8 || type == IR_I16 || type == IR_I32 || type == IR_I64 || type == IR_PTR || tier1_select_float_width(type) != 0 ||
           type == IR_F80;
}

static bool tier1_select_call_arity(const IrCall* call)
{
    return call->variadic ? call->parameter_count <= call->argument_count : call->parameter_count == call->argument_count;
}

// Whether the call-wide part of a call's ABI records is one Tier 1 takes (#789), or the call has none: its result, as
// parts or through the hidden pointer into the frame object its into operand (c) is the address of, and its outgoing
// area's bytes.
static bool tier1_select_call_abi(const IrFunction* function, uint32_t node, const IrCall* call)
{
    const IrAbiCall* abi = call->abi != 0 ? &function->abi_calls[call->abi - 1] : 0;
    const uint32_t into = function->c[node];
    const bool object = into != BUSTER_IR_NONE && into < function->instruction_count && function->op[into] == IR_STACK_ADDR && tier1_select_stack_object(function, function->a[into]);
    return abi == 0 || (tier1_select_result_supported(abi->call.result, abi->result_size) && (abi->result_size == 0 || (function->type[node] == IR_VOID && object)) &&
                        abi->call.stack_bytes <= BUSTER_TIER1_SELECT_MAX_STACK_OFFSET + BUSTER_TIER1_SELECT_MAX_STACK_AGGREGATE);
}

// The red zone's staging slots a call's arguments' parts in registers may wait in (#789, tier1_emit_staging): 14, the
// argument registers'. A function whose calls need more reserves a frame object for their maximum instead (#1450).
#define BUSTER_TIER1_SELECT_STAGING_SLOTS (BUSTER_ABI_INTEGER_ARGUMENTS + BUSTER_ABI_VECTOR_ARGUMENTS)

// The staging slots an argument's parts in registers take, by an ABI record Tier 1 takes (or none): an eightbyte's one, a
// whole vector register's its bytes' (#141, tier1_select_part_slots), but a 64-byte one's none, which waits in zmm16 and
// on (tier1_select_argument_part_slots).
static uint32_t tier1_select_argument_slots(const IrPassed* record)
{
    Tier1SelectPart parts[2];
    const uint32_t count = record != 0 && record->size != 0 && !tier1_select_location_whole(record->location) ? tier1_select_location_parts(record->location, record->size, parts) : 0;
    uint32_t slots = 0;
    for (uint32_t k = 0; k < count; k += 1)
    {
        slots += parts[k].reg != BUSTER_ABI_REGISTER_NONE ? tier1_select_argument_part_slots(parts[k].bytes) : 0;
    }
    return slots;
}

static uint32_t tier1_select_call_staging_slots(const IrFunction* function, const IrCall* call)
{
    const uint32_t count = call->abi != 0 ? call->argument_count : 0;
    uint32_t slots = 0;
    for (uint32_t argument = 0; argument < count; argument += 1)
    {
        slots += tier1_select_argument_slots(tier1_select_argument_record(function, call, argument));
    }
    return slots;
}

// Whether `node` calls a function (#141): an IR_CALL of a symbol or an IR_CALL_INDIRECT, which the consumers' calls take
// (saves around it, argument moves, the outgoing area), not an IR row call (IrCall.row), which runs a machine row in
// place of a callee (TIER1_RULE_ROW) and saves nothing around it, nor a body call (IrCall.body, #1839), which runs an
// asm body there (TIER1_RULE_ASM).
static bool tier1_select_function_call(const IrFunction* function, uint32_t node)
{
    const uint32_t op = function->op[node];
    return op == IR_CALL_INDIRECT || (op == IR_CALL && function->calls[function->b[node]].row == 0 && function->calls[function->b[node]].body == 0);
}

// Whether `node` is a body call (#1839, IrCall.body), whose asm body the CFG consumer writes in its place.
static bool tier1_select_body_call(const IrFunction* function, uint32_t node)
{
    return node < function->instruction_count && function->op[node] == IR_CALL && function->calls[function->b[node]].body != 0;
}

// Whether the CFG consumer writes a body call's asm body (#1839, TIER1_RULE_ASM): its block, where it has one, an
// IR_STACK_ADDR of a frame object, whose place in the frame its records' block base becomes, and none of the registers
// that hold the frame (rsp, rbp, r15) among those it touches, as the producer has them (tier0_asm_bodies_check); and each
// register operand (#2144, IrAsmBody.locations), which the consumer moves into its register: an I32's, an I64's or a
// ptr's in a general register but the scratch one, which the moves take, or a float's or a double's in xmm0 to xmm14
// (xmm15 is the floating moves' scratch).
static bool tier1_select_asm_supported(const IrFunction* function, uint32_t node)
{
    const IrCall* call = tier1_select_body_call(function, node) ? &function->calls[function->b[node]] : 0;
    const IrAsmBody* body = call != 0 && call->body <= function->asm_body_count ? &function->asm_bodies[call->body - 1] : 0;
    const uint32_t block = body != 0 ? ir_asm_body_block(function, call, body) : BUSTER_IR_NONE;
    const uint32_t frame = (1u << X86_REGISTER_RSP) | (1u << X86_REGISTER_RBP) | (1u << X86_REGISTER_R15);
    bool supported = body != 0 && call->argument_count >= body->passed && call->argument_count - body->passed <= 1 &&
                     (block == BUSTER_IR_NONE || (block < function->instruction_count && function->op[block] == IR_STACK_ADDR)) && (body->touched & frame) == 0;
    const uint32_t skipped = supported ? call->argument_count - body->passed : 0;
    // Each register taken once (their moves are one parallel move), and no more of them than a call's arguments.
    uint64_t taken = 0;
    supported = supported && body->passed <= BUSTER_TIER1_SELECT_MAX_ARGUMENTS;
    for (uint32_t k = 0; supported && k < body->passed; k += 1)
    {
        const uint32_t value = function->extra[call->argument_first + skipped + k];
        const uint8_t reg = body->locations[k].registers[0];
        const bool vector = (reg & 0xe0) == BUSTER_ABI_REGISTER_VECTOR;
        const uint32_t n = reg & 0x1fu;
        const uint8_t type = value < function->instruction_count ? function->type[value] : IR_VOID;
        supported = vector ? (type == IR_F32 || type == IR_F64) && n < BUSTER_TIER1_SELECT_FLOAT_SCRATCH :
                             (type == IR_I32 || type == IR_I64 || type == IR_PTR) && (reg & 0xe0) == 0 && n < 16 && n != BUSTER_TIER1_SELECT_SCRATCH && ((frame >> n) & 1) == 0;
        const uint64_t bit = 1ull << (vector ? 32 + n : n);
        supported = supported && (taken & bit) == 0;
        taken |= bit;
    }
    return supported;
}

// Which of a machine row's operands its records write (#141, a bit per operand), through a field that names the operand's
// role (INTEGER(j), FLOATING(j) or MASK(j)), by its form's effects (x86_semantics.c's: a field's role written), or its
// opmask decoration where its form writes the mask (a gather's or a scatter's, whose VSIB operand clears it). A form the
// tables don't cover writes every field it names. The others stay in their own registers (tier1_emit_cfg_row).
static uint32_t tier1_select_row_written(const Tier0Operation* row)
{
    uint32_t written = 0;
    for (uint32_t r = 0; r < row->record_count; r += 1)
    {
        X86Instruction record = row->records[r];
        const Tier0Fields fields = tier0_operation_fields(&record);
        const X86Effects* effects = x86_effects(record.form);
        const bool covered = effects->operation != X86_OPERATION_NONE;
        const X86FormDescription* description = x86_form_description(record.form);
        for (uint32_t k = 0; k < fields.count; k += 1)
        {
            const uint8_t role = *fields.bytes[k];
            const uint32_t position = fields.bytes[k] == &record.reg ? BUSTER_X86_ROLE_REG : fields.bytes[k] == &record.vvvv ? BUSTER_X86_ROLE_VVVV : BUSTER_X86_ROLE_RM;
            const bool address = record.mode != BUSTER_X86_REGISTER_DIRECT && (fields.bytes[k] == &record.rm || fields.bytes[k] == &record.index);
            const bool writes = !covered || (!address && (x86_effects_role(effects, position) & BUSTER_X86_ROLE_WRITE) != 0);
            // An operand's role, in a field of its class: VECTOR(j) and the scratch and result roles aren't operands'.
            const bool vector_field = tier0_vector_register_class(fields.classes[k]);
            const bool operand = role >= BUSTER_TIER0_ROLE && (vector_field ? role >= BUSTER_TIER0_ROLE_FLOATING(0) && role < BUSTER_TIER0_ROLE_FLOATING_RESULT : (role & 0x30) == 0);
            written |= writes && operand ? 1u << (role & 7) : 0;
        }
        const bool vsib = record.mode != BUSTER_X86_REGISTER_DIRECT && (description->rm & BUSTER_X86_OPERAND_VSIB) != 0;
        const uint32_t decoration = record.decorations;
        const bool decorated_operand = (decoration & BUSTER_TIER0_DECORATION_ROLE) == BUSTER_TIER0_DECORATION_MASK(0);
        written |= decorated_operand && (vsib || !covered) ? 1u << (decoration & 7) : 0;
    }
    return written;
}

// What a machine row's records do with its VECTOR(j) roles (#1283), which tier1_emit_cfg_row places one at a time, a bit
// or an entry per role:
//   - `written`: the roles the records write, by the forms' effects as tier1_select_row_written reads them (a form the
//     tables don't cover reads and writes every field it names); `legacy`: those a legacy form writes, whose write keeps
//     the register's bits past its field.
//   - `first_write` and `last_named`: the first record that writes each role and the last that names it, by index plus
//     one (0 for none).
//   - `pure`: the roles whose first write is a pure one: one field of that record writes the role, which the form
//     doesn't read (no tied operand, no merge into the rest of the register, no merging opmask), so that the record's
//     other fields naming it read the role's earlier value, which may be in another register than its later one; and
//     `write_position`, that field's (BUSTER_X86_ROLE_REG, _RM or _VVVV).
//   - `widest_written`: the widest field each is written in, and `widest_early`, the widest each is read in up to its
//     first write, that one included but for a pure write's field (every naming where nothing writes it), in bytes (an
//     XMM field's 16, a YMM's 32, a ZMM's 64; an /is4 operand's its reg field's class, which it shares): how much of the
//     register the operand placed in it is read. A VSIB index is read.
//   - `exclusive`, a bit per record: those whose destination can't be one of their sources' registers, a gather's
//     (whose destination, index and mask must differ) and a complex multiplication's (x86_form_has_distinct_destination),
//     both #UD otherwise.
typedef struct Tier1SelectRowVectors Tier1SelectRowVectors;
struct Tier1SelectRowVectors
{
    uint32_t written;
    uint32_t legacy;
    uint32_t pure;
    uint32_t exclusive;
    uint8_t first_write[16];
    uint8_t last_named[16];
    uint8_t widest_written[16];
    uint8_t widest_early[16];
    uint8_t write_position[16];
};

static uint8_t tier1_select_vector_class_bytes(uint32_t class)
{
    return class == BUSTER_X86_CLASS_ZMM ? 64 : class == BUSTER_X86_CLASS_YMM ? 32 : 16;
}

static Tier1SelectRowVectors tier1_select_row_vectors(const Tier0Operation* row)
{
    Tier1SelectRowVectors vectors;
    memset(&vectors, 0, sizeof(vectors));
    for (uint32_t r = 0; r < row->record_count; r += 1)
    {
        X86Instruction record = row->records[r];
        const Tier0Fields fields = tier0_operation_fields(&record);
        const X86Effects* effects = x86_effects(record.form);
        const bool covered = effects->operation != X86_OPERATION_NONE;
        const X86FormDescription* description = x86_form_description(record.form);
        const bool legacy = description->family == BUSTER_X86_FAMILY_LEGACY;
        const bool vsib = record.mode != BUSTER_X86_REGISTER_DIRECT && (description->rm & BUSTER_X86_OPERAND_VSIB) != 0;
        vectors.exclusive |= vsib || x86_form_has_distinct_destination(description) ? 1u << r : 0;
        // An opmask that merges: the destination's masked-off elements keep what it held.
        const uint32_t decorations = record.decorations;
        const bool merging = ((decorations & BUSTER_X86_DECORATION_MASK) != 0 || (decorations & BUSTER_TIER0_DECORATION_ROLE) != 0) && (decorations & BUSTER_X86_DECORATION_ZEROING) == 0;
        // The roles this record writes, and those it writes in more than one field or by reading the field too.
        uint32_t writes_here = 0;
        uint32_t impure_here = 0;
        uint8_t positions[16] = { 0 };
        for (uint32_t k = 0; k < fields.count; k += 1)
        {
            const uint8_t role = *fields.bytes[k];
            // A VECTOR(j) role: a vector field's role below the floating ones (tier0_operation_register's).
            const bool vector = tier0_vector_register_class(fields.classes[k]) && role < BUSTER_TIER0_ROLE_FLOATING(0);
            const uint32_t position = fields.bytes[k] == &record.reg ? BUSTER_X86_ROLE_REG : fields.bytes[k] == &record.vvvv ? BUSTER_X86_ROLE_VVVV : BUSTER_X86_ROLE_RM;
            const bool address = record.mode != BUSTER_X86_REGISTER_DIRECT && (fields.bytes[k] == &record.rm || fields.bytes[k] == &record.index);
            const uint32_t bits = covered ? x86_effects_role(effects, position) : BUSTER_X86_ROLE_READ | BUSTER_X86_ROLE_WRITE;
            const uint32_t rest = (bits & BUSTER_X86_ROLE_REST) >> BUSTER_X86_ROLE_REST_SHIFT;
            const bool writes = vector && !address && (bits & BUSTER_X86_ROLE_WRITE) != 0;
            const bool merges = writes && (merging || (bits & BUSTER_X86_ROLE_READ) != 0 || rest == BUSTER_X86_REST_MERGE || rest == BUSTER_X86_REST_MERGE_IF_REGISTER);
            const bool reads = vector && (address || (bits & BUSTER_X86_ROLE_READ) != 0 || !writes || merges);
            const uint32_t j = role & 15u;
            const uint8_t bytes = tier1_select_vector_class_bytes(fields.classes[k]);
            // Read up to the role's first write: in an earlier record, or in the one that first writes it (whose
            // first_write is set past its fields).
            const bool early = vectors.first_write[j] == 0;
            vectors.widest_early[j] = reads && early && bytes > vectors.widest_early[j] ? bytes : vectors.widest_early[j];
            vectors.widest_written[j] = writes && bytes > vectors.widest_written[j] ? bytes : vectors.widest_written[j];
            impure_here |= writes && (merges || ((writes_here >> j) & 1) != 0) ? 1u << j : 0;
            positions[j] = writes ? (uint8_t)position : positions[j];
            writes_here |= writes ? 1u << j : 0;
            vectors.last_named[j] = vector ? (uint8_t)(r + 1) : vectors.last_named[j];
        }
        if (fields.is4)
        {
            const uint32_t j = (uint32_t)record.immediate >> 4 & 15u;
            const uint8_t bytes = tier1_select_vector_class_bytes(description->reg & BUSTER_X86_OPERAND_CLASS);
            const bool early = vectors.first_write[j] == 0;
            vectors.widest_early[j] = early && bytes > vectors.widest_early[j] ? bytes : vectors.widest_early[j];
            vectors.last_named[j] = (uint8_t)(r + 1);
        }
        // The roles this record writes first.
        for (uint32_t left = writes_here & ~vectors.written; left != 0; left &= left - 1)
        {
            const uint32_t j = (uint32_t)__builtin_ctz(left);
            vectors.first_write[j] = (uint8_t)(r + 1);
            vectors.write_position[j] = positions[j];
            vectors.pure |= ((impure_here >> j) & 1) == 0 && covered ? 1u << j : 0;
        }
        vectors.written |= writes_here;
        vectors.legacy |= legacy ? writes_here : 0;
    }
    return vectors;
}

// Whether a machine row names an opmask register pair (#141, VP2INTERSECT's k and k + 1), which its registers by number
// can't be renamed around (tier1_emit_cfg_row).
static bool tier1_select_row_pairs(const Tier0Operation* row)
{
    bool paired = false;
    for (uint32_t r = 0; r < row->record_count; r += 1)
    {
        const X86FormDescription* description = x86_form_description(row->records[r].form);
        paired |= description->reg == BUSTER_X86_OPERAND_KP || description->vvvv == BUSTER_X86_OPERAND_KP || description->rm == BUSTER_X86_OPERAND_KP;
    }
    return paired;
}

// A placed record of a machine row (#141) with the opmask registers its row names by number (`original`'s fields and
// decoration that name k1 to k7, not a role) renamed by `renamed`, k's new register at k: the fields of the opmask
// class, and the decoration's mask where it names no role. Tier 0's placement left them as they were.
static X86Instruction tier1_select_row_rename(X86Instruction placed, X86Instruction original, const uint8_t* renamed)
{
    Tier0Fields fields = tier0_operation_fields(&original);
    Tier0Fields placed_fields = tier0_operation_fields(&placed);
    for (uint32_t k = 0; k < fields.count && k < placed_fields.count; k += 1)
    {
        const uint8_t role = *fields.bytes[k];
        if (fields.classes[k] == BUSTER_X86_CLASS_OPMASK && role < 8)
        {
            *placed_fields.bytes[k] = renamed[role];
        }
    }
    if ((original.decorations & BUSTER_TIER0_DECORATION_ROLE) == 0)
    {
        const uint32_t index = original.decorations & BUSTER_X86_DECORATION_MASK;
        placed.decorations = (uint8_t)((placed.decorations & ~BUSTER_X86_DECORATION_MASK) | renamed[index]);
    }
    return placed;
}

// Whether a machine row names a floating role (FLOATING(j) or FLOATING_RESULT) in a VEX or legacy form's vector field
// (#1281: F16C's conversions), which only a register below 16 can be: the CFG consumer places such a row's floating roles
// below 16 (tier1_emit_cfg_row), as it places every row's at a target without AVX512VL.
static bool tier1_select_row_low_floating(const Tier0Operation* row)
{
    bool low = false;
    for (uint32_t r = 0; r < row->record_count; r += 1)
    {
        X86Instruction record = row->records[r];
        const Tier0Fields fields = tier0_operation_fields(&record);
        const bool evex = x86_form_description(record.form)->family == BUSTER_X86_FAMILY_EVEX;
        for (uint32_t k = 0; k < fields.count; k += 1)
        {
            low |= !evex && tier0_vector_register_class(fields.classes[k]) && *fields.bytes[k] >= BUSTER_TIER0_ROLE_FLOATING(0);
        }
    }
    return low;
}

// Whether the CFG consumer places a row call's machine row (#141, TIER1_RULE_ROW): a row Tier 0 places (tier0_operation_valid,
// which needs x86_semantics.c's tables). Its vector roles are zmm16 and on, which no value takes, and its floating ones
// past them, below 32, where every record that names a vector register is an EVEX form's; a row with a VEX or legacy
// form's vector register (tier0_operation_reach's vex_vectors) takes free xmm registers below 16 for its vector roles
// that don't stay in place where it runs (tier1_emit_cfg_row). A row with a floating role in such a form (#1281,
// tier1_select_row_low_floating) takes free xmm registers below 16 for its written floating operands and its result, and
// so does every row at a target without AVX512VL: its vector roles and its floating ones fit in 16 registers. Whether as
// many are free where it runs is the CFG consumer's to find (TIER1_EMIT_UNSUPPORTED where they aren't).
static bool tier1_select_row_supported(const IrFunction* function, uint32_t node)
{
    const IrCall* call = node < function->instruction_count && function->op[node] == IR_CALL ? &function->calls[function->b[node]] : 0;
    const Tier0Operation* row = call != 0 && call->row != 0 && call->row <= function->row_count ? &function->rows[call->row - 1] : 0;
    bool supported = row != 0 && (x86_semantics_initialized || x86_semantics_initialize()) && tier0_operation_valid(row);
    uint32_t floating = 0;
    for (uint32_t j = 0; supported && j <= row->operand_count; j += 1)
    {
        const uint32_t class = j < row->operand_count ? row->operand_classes[j] : row->result_class;
        floating += tier0_class_floating(class) ? 1 : 0;
    }
    const Tier0Reach reach = supported ? tier0_operation_reach(row) : (Tier0Reach){ 0, 0, 0 };
    supported = supported && (reach.vex_vectors != 0 ? reach.vectors <= 15 : 16 + reach.vectors + floating <= 32);
    supported = supported && (!tier1_select_row_low_floating(row) || reach.vectors + floating <= 16);
    for (uint32_t r = 0; supported && r < row->record_count; r += 1)
    {
        // An /is4 operand's register is a vector role's, which a VEX form names below 16 alone.
        X86Instruction record = row->records[r];
        const Tier0Fields fields = tier0_operation_fields(&record);
        const bool evex = x86_form_description(record.form)->family == BUSTER_X86_FAMILY_EVEX;
        supported = evex || !fields.is4 || reach.vex_vectors != 0;
    }
    return supported;
}

// This selection boundary intentionally excludes extension/truncation ABI rules for narrow integers. The shared verifier
// checks descriptor bounds and actual argument types before this helper runs.
static bool tier1_select_call_supported(const IrFunction* function, uint32_t node)
{
    // A direct call, or one through a pointer, whose callee value the CFG consumer moves with the arguments.
    bool supported = node < function->instruction_count && (function->op[node] == IR_CALL || function->op[node] == IR_CALL_INDIRECT) && function->calls[function->b[node]].row == 0 &&
                     function->calls[function->b[node]].body == 0;
    if (supported)
    {
        const IrCall call = function->calls[function->b[node]];
        // A variadic call's unnamed arguments are promoted ones, an F32 among them never: its al counts the vector
        // registers its arguments take (tier1_emit_call). A call with ABI records (#789) passes each argument where its
        // record says: its aggregates' bytes in registers or in its stack slots, and its aggregate result's into the frame
        // object its into operand is the address of, as parts or through the hidden pointer (tier1_emit_call).
        // A chained indirect variadic call (#148) isn't one (#1411's review): its callee would wait in rax
        // (tier1_select_callee_register), which its al then overwrites; the typed stream's adapter refuses it too (ir_lower).
        const bool clobbered = function->op[node] == IR_CALL_INDIRECT && call.variadic && tier1_select_call_chained(function, &call);
        supported = call.argument_count <= BUSTER_TIER1_SELECT_MAX_ARGUMENTS && tier1_select_call_arity(&call) && tier1_select_call_result(function->type[node]) &&
                    tier1_select_call_abi(function, node, &call) && !clobbered;
        // Argument staging is the frame planner's: calls past the red zone's capacity reserve a frame object. Selection
        // need not assume that the target has extra vector registers for their parts (#1450).
        for (uint32_t argument = 0; supported && argument < call.argument_count; argument += 1)
        {
            const uint32_t argument_type = tier1_select_argument_type(function, &call, argument);
            const IrPassed* record = tier1_select_argument_record(function, &call, argument);
            supported = record != 0 ? tier1_select_record_supported(record, argument_type) : tier1_select_call_argument(argument_type);
        }
    }
    return supported;
}

static X86Operation tier1_select_operation(uint32_t op)
{
    X86Operation result = X86_OPERATION_NONE;
    switch (op)
    {
    case IR_ADD:
        result = X86_OPERATION_ADD;
        break;
    case IR_SUB:
        result = X86_OPERATION_SUB;
        break;
    case IR_MUL:
        result = X86_OPERATION_IMUL;
        break;
    case IR_AND:
        result = X86_OPERATION_AND;
        break;
    case IR_OR:
        result = X86_OPERATION_OR;
        break;
    case IR_XOR:
        result = X86_OPERATION_XOR;
        break;
    case IR_SHL:
        result = X86_OPERATION_SHL;
        break;
    case IR_LSHR:
        result = X86_OPERATION_SHR;
        break;
    case IR_ASHR:
        result = X86_OPERATION_SAR;
        break;
    case IR_NEG:
        result = X86_OPERATION_NEG;
        break;
    case IR_NOT:
        result = X86_OPERATION_NOT;
        break;
    }
    return result;
}

static bool tier1_select_tree_child(const Tier1SelectForest* forest, uint32_t parent, uint32_t value)
{
    return value != BUSTER_IR_NONE && value < parent && forest->uses[value] == 1 && forest->blocks[value] == forest->blocks[parent];
}

// A load moves into its consumer's memory operand: its only use, in its block, past nodes that neither write memory, call,
// fence nor load (no barrier between them: the last before the consumer the load itself, a barrier of its own), and itself
// neither volatile nor atomic.
static inline __attribute__((always_inline)) bool tier1_select_load_folds(const Tier1SelectForest* forest, uint32_t consumer, uint32_t load)
{
    const IrFunction* function = forest->function;
    return load != BUSTER_IR_NONE && function->op[load] == IR_LOAD && function->flags[load] == 0 && tier1_select_tree_child(forest, consumer, load) &&
           forest->barriers[consumer] <= load + 1;
}

static void tier1_select_fold(const Tier1SelectForest* forest, uint32_t parent, uint32_t value, Tier1SelectPattern* pattern)
{
    pattern->fold_count += tier1_select_tree_child(forest, parent, value) ? 1 : 0;
}

// A pattern's cost at `node`: its intrinsic cost and the costs of its distinct values that are tree children, a subtree's
// cost from its children's as forest->costs has them.
static inline __attribute__((always_inline)) uint32_t tier1_select_pattern_cost(const Tier1SelectForest* forest, uint32_t node, const Tier1SelectPattern* pattern)
{
    uint32_t cost = pattern->intrinsic_cost;
    for (uint32_t operand = 0; operand < 3; operand += 1)
    {
        const uint32_t value = pattern->values[operand];
        bool repeated = false;
        for (uint32_t previous = 0; previous < operand; previous += 1)
        {
            repeated |= pattern->values[previous] == value;
        }
        if (!repeated && tier1_select_tree_child(forest, node, value))
        {
            cost += forest->costs[value];
        }
    }
    return cost;
}

static inline __attribute__((always_inline)) void tier1_select_add_candidate(const Tier1SelectForest* forest, uint32_t node, Tier1SelectCandidates* candidates, Tier1SelectPattern pattern)
{
    assert(candidates->count < BUSTER_TIER1_SELECT_MAX_CANDIDATES);
    pattern.intrinsic_cost += pattern.sign_result;
    candidates->patterns[candidates->count] = pattern;
    candidates->costs[candidates->count] = tier1_select_pattern_cost(forest, node, &pattern);
    candidates->count += 1;
}

static bool tier1_select_constant(const IrFunction* function, uint32_t value, uint64_t* bits)
{
    const bool result = value < function->instruction_count && function->op[value] == IR_CONST;
    if (result)
    {
        *bits = function->constants[function->a[value]].low;
    }
    return result;
}

// Each integer predicate's negation: !(a < b) is a >= b, and so on.
static const uint8_t tier1_select_negations[IR_PREDICATE_COUNT] = { IR_NE, IR_EQ, IR_GE, IR_GT, IR_LE, IR_LT, IR_UGE, IR_UGT, IR_ULE, IR_ULT };

// C's truth values reach the IR as integers: `a < b` is `zext (icmp lt a, b)`, which `if`, `!` and `&&` test against
// zero again, `icmp ne (zext x), 0` (`eq` for `!`), once for each operator the source nests. The walk follows such a
// chain back from `value`, an I1, to the value it tests, through links that only `root`'s pattern uses, at most
// BUSTER_TIER1_SELECT_WALK of them: each a zero extension, a zero and their comparison in consecutive nodes, right after
// the value it extends, so that nothing the pattern doesn't compute runs between the test and the root. It returns the
// node it stops at, counts the nodes it passed among the pattern's folds, and says whether an odd number of the links
// were `eq`, where the root's truth is the stopped node's negation.
static uint32_t tier1_select_truth(const Tier1SelectForest* forest, uint32_t root, uint32_t value, Tier1SelectPattern* pattern, bool* negated)
{
    const IrFunction* function = forest->function;
    bool odd = false;
    bool walking = true;
    for (uint32_t link = 0; walking && link < BUSTER_TIER1_SELECT_WALK; link += 1)
    {
        uint64_t zero = 1;
        const uint32_t extended = function->op[value] == IR_ICMP ? function->a[value] : BUSTER_IR_NONE;
        walking = extended != BUSTER_IR_NONE && function->c[value] <= IR_NE && extended + 2 == value && function->b[value] + 1 == value &&
                  tier1_select_constant(function, function->b[value], &zero) && zero == 0 && function->op[extended] == IR_ZEXT &&
                  function->type[function->a[extended]] == IR_I1 && function->a[extended] + 1 == extended && tier1_select_tree_child(forest, root, value) &&
                  tier1_select_tree_child(forest, root, extended) && tier1_select_tree_child(forest, root, function->b[value]);
        if (walking)
        {
            odd ^= function->c[value] == IR_EQ;
            pattern->fold_count += 3;
            value = function->a[extended];
        }
    }
    *negated = odd;
    return value;
}

// The comparison `tested`, an ICMP of 32- or 64-bit integers or pointers that only `root`'s pattern uses, as the pattern's
// own: its operands, their width, its predicate (with `negated`, its negation) and a constant right operand that fits as
// the immediate.
static bool tier1_select_comparison(const Tier1SelectForest* forest, uint32_t root, uint32_t tested, bool negated, Tier1SelectPattern* pattern)
{
    const IrFunction* function = forest->function;
    const uint32_t left = function->a[tested];
    const bool result = function->op[tested] == IR_ICMP && tier1_select_tree_child(forest, root, tested) &&
                        (function->type[left] == IR_I32 || function->type[left] == IR_I64 || function->type[left] == IR_PTR);
    if (result)
    {
        pattern->values[0] = left;
        pattern->values[1] = function->b[tested];
        pattern->width = (uint8_t)tier1_select_width(function->type[left]);
        pattern->condition = negated ? tier1_select_negations[function->c[tested]] : (uint8_t)function->c[tested];
        tier1_select_fold(forest, root, tested, pattern);
        uint64_t immediate = 0;
        if (tier1_select_constant(function, function->b[tested], &immediate) && tier1_select_immediate_fits(pattern->width, immediate))
        {
            pattern->values[1] = BUSTER_IR_NONE;
            pattern->immediate = immediate;
            pattern->compare_immediate = true;
            tier1_select_fold(forest, root, function->b[tested], pattern);
        }
    }
    return result;
}

static inline __attribute__((always_inline)) bool tier1_select_memory(const Tier1SelectForest* forest, uint32_t node, uint32_t pointer, Tier1SelectPattern* pattern, uint32_t first);

// Fold a comparison's single-use load at the final consumer, including a fused branch or extended truth result. A
// promoted byte/word/dword can be compared at its memory width only against a constant that the same extension
// represents exactly. In particular, signed comparisons of a zero-extended load become unsigned narrow comparisons.
// No truncation, shared extension, observable access or intervening memory operation can be hidden by this pattern.
// The grammar prices direct load folds explicitly. Promotion removal is a legality-dependent refinement: its
// grammar cost conservatively retains the extension/load even though this candidate emits neither separately.
static inline __attribute__((always_inline)) void tier1_select_add_comparison(const Tier1SelectForest* forest, uint32_t node, Tier1SelectCandidates* candidates, Tier1SelectPattern pattern)
{
    static const uint8_t swapped[IR_PREDICATE_COUNT] =
    {
        IR_EQ,
        IR_NE,
        IR_GT,
        IR_GE,
        IR_LT,
        IR_LE,
        IR_UGT,
        IR_UGE,
        IR_ULT,
        IR_ULE,
    };
    const IrFunction* function = forest->function;
    const uint32_t left = pattern.values[0];
    const uint32_t right = pattern.values[1];
    const bool immediate = pattern.compare_immediate;
    const bool right_load = !immediate && tier1_select_load_folds(forest, node, right);
    uint32_t load = right_load ? right : left;
    uint32_t extension = BUSTER_IR_NONE;
    bool zero_extended = false;
    bool representable = true;
    uint64_t bits = pattern.immediate;
    if (immediate && (function->op[load] == IR_ZEXT || function->op[load] == IR_SEXT))
    {
        extension = load;
        load = function->a[extension];
        zero_extended = function->op[extension] == IR_ZEXT;
        const uint32_t source_width = tier1_select_memory_width(function->type[load]);
        representable = source_width != 0 && source_width < pattern.width && tier1_select_tree_child(forest, node, extension);
        if (representable)
        {
            const uint64_t sign = UINT64_C(1) << (source_width * 8 - 1);
            const uint64_t mask = (sign << 1) - 1;
            const uint64_t result_mask = pattern.width == 8 ? UINT64_MAX : UINT32_MAX;
            bits &= mask;
            const uint64_t extended = zero_extended ? bits : (bits ^ sign) - sign;
            representable = (extended & result_mask) == (pattern.immediate & result_mask);
        }
    }
    const uint32_t memory_width = tier1_select_memory_width(function->type[load]);
    if (representable && memory_width != 0 && (extension != BUSTER_IR_NONE || memory_width == pattern.width) && tier1_select_load_folds(forest, node, load))
    {
        pattern.compare_memory = true;
        pattern.width = (uint8_t)memory_width;
        pattern.values[0] = immediate ? BUSTER_IR_NONE : right_load ? left : right;
        pattern.immediate = bits;
        pattern.condition = !immediate && !right_load ? swapped[pattern.condition] : pattern.condition;
        if (zero_extended && pattern.condition >= IR_LT && pattern.condition <= IR_GE)
        {
            pattern.condition += IR_ULT - IR_LT;
        }
        tier1_select_fold(forest, node, load, &pattern);
        if (extension != BUSTER_IR_NONE)
        {
            tier1_select_fold(forest, node, extension, &pattern);
        }
        tier1_select_memory(forest, node, function->a[load], &pattern, 1);
    }
    tier1_select_add_candidate(forest, node, candidates, pattern);
}

// The comparison `tested`, an FCMP of F32 or F64 values by <, <=, > or >= that only `root`'s pattern uses, as the
// pattern's own (#141, TIER1_RULE_FLOAT_BRANCH_COMPARE): ucomis of its operands, swapped for < and <=, and the unsigned
// condition that reads its flags, UGT (ja) for a strict one and UGE (jae) for the other, which are false for unordered
// operands; with `negated`, their negations ULE and ULT (jbe and jb), which are true for them, as !(a > b) and
// !(a >= b) are. == and != read the parity flag too, which a single jump can't: their truth value is tested instead.
static bool tier1_select_float_comparison(const Tier1SelectForest* forest, uint32_t root, uint32_t tested, bool negated, Tier1SelectPattern* pattern)
{
    const IrFunction* function = forest->function;
    const uint32_t left = function->a[tested];
    const uint32_t predicate = function->c[tested];
    const bool result = function->op[tested] == IR_FCMP && tier1_select_tree_child(forest, root, tested) && tier1_select_float_width(function->type[left]) != 0 && predicate >= IR_LT && predicate <= IR_GE;
    if (result)
    {
        const bool swapped = predicate == IR_LT || predicate == IR_LE;
        const uint8_t condition = predicate == IR_LT || predicate == IR_GT ? IR_UGT : IR_UGE;
        pattern->values[0] = swapped ? function->b[tested] : left;
        pattern->values[1] = swapped ? left : function->b[tested];
        pattern->width = (uint8_t)tier1_select_float_width(function->type[left]);
        pattern->condition = negated ? tier1_select_negations[condition] : condition;
        tier1_select_fold(forest, root, tested, pattern);
    }
    return result;
}

// A frame object's address the selector takes: a lea from the frame's base (TIER1_RULE_STACK_ADDRESS), or folded into
// a user's memory operand (tier1_select_address), both filled at emission from the frame's layout. The frame's offsets
// are 32-bit, and an over-aligned object realigns it. A variadic definition's register save area (#789) is one: the
// frame's own, which the prologue fills (tier1_emit_cfg.c). Its overflow area isn't (tier1_select_overflow_area).
static bool tier1_select_stack_object(const IrFunction* function, uint32_t slot)
{
    return slot < function->slot_count && (function->slots[slot].flags & BUSTER_IR_SLOT_OVERFLOW_AREA) == 0 &&
           function->slots[slot].size <= BUSTER_TIER1_SELECT_MAX_OBJECT_BYTES && function->slots[slot].alignment <= BUSTER_TIER1_SELECT_MAX_OBJECT_ALIGNMENT;
}

// A variadic definition's overflow area (#789), whose address is in its caller's frame, past the incoming stack
// arguments: a lea the CFG consumer bases on the incoming frame (TIER1_RULE_STACK_ADDRESS, tier1_emit_cfg_incoming),
// never a memory operand's base, which is the frame's.
static bool tier1_select_overflow_area(const IrFunction* function, uint32_t slot)
{
    return slot < function->slot_count && (function->slots[slot].flags & BUSTER_IR_SLOT_OVERFLOW_AREA) != 0;
}

// A memory operand's address: base + index * 2^scale + displacement, the frame object the walk reached instead of a
// base value, and the nodes its walk passed.
typedef struct Tier1SelectAddress Tier1SelectAddress;
struct Tier1SelectAddress
{
    int64_t displacement;
    uint32_t base;
    uint32_t index;
    uint32_t object;
    uint8_t scale;
    uint8_t folds;
};

// An address's index through single-use I64 arithmetic in its block, at most BUSTER_TIER1_SELECT_WALK nodes: additions
// and subtractions of constants join the displacement at the current scale; shifts and power-of-two multiplies join
// the SIB's log2 scale while it remains at most 3. Walk from the consumer: (i + c) << k scales c, but (i << k) + c does
// not. These are modulo 2^64 identities. A narrower operation and its extension stay put, since folding them would lose
// their wrap before widening. Shared arithmetic also stays put; only the address walk itself may take shared nodes.
static uint32_t tier1_select_index(const Tier1SelectForest* forest, uint32_t root, uint32_t index, uint8_t* scale, int64_t* displacement, uint8_t* folds)
{
    const IrFunction* function = forest->function;
    uint8_t index_scale = *scale;
    int64_t index_displacement = *displacement;
    uint8_t index_folds = *folds;
    bool walking = true;
    for (uint32_t step = 0; walking && step < BUSTER_TIER1_SELECT_WALK; step += 1)
    {
        const uint32_t op = function->op[index];
        const bool additive = op == IR_ADD || op == IR_SUB;
        uint64_t constant = 0;
        const bool right = (additive || op == IR_SHL || op == IR_MUL) && tier1_select_constant(function, function->b[index], &constant);
        const bool left = (op == IR_ADD || op == IR_MUL) && !right && tier1_select_constant(function, function->a[index], &constant);
        walking = (right || left) && function->type[index] == IR_I64 && tier1_select_tree_child(forest, root, index);
        if (walking && additive)
        {
            int64_t term = 0;
            int64_t sum = 0;
            walking = !__builtin_mul_overflow((int64_t)constant, INT64_C(1) << index_scale, &term) &&
                      (op == IR_SUB ? !__builtin_sub_overflow(index_displacement, term, &sum) : !__builtin_add_overflow(index_displacement, term, &sum)) && sum == (int64_t)(int32_t)sum;
            index_displacement = walking ? sum : index_displacement;
        }
        else if (walking)
        {
            const uint64_t shift = op == IR_SHL ? constant : tier1_select_log2(constant);
            walking = shift <= 3u - index_scale;
            index_scale += walking ? (uint8_t)shift : 0;
        }
        if (walking)
        {
            const uint32_t constant_node = right ? function->b[index] : function->a[index];
            index_folds = (uint8_t)(index_folds + 1 + (tier1_select_tree_child(forest, root, constant_node) ? 1 : 0));
            index = right ? function->a[index] : function->b[index];
        }
    }
    *scale = index_scale;
    *displacement = index_displacement;
    *folds = index_folds;
    return index;
}

// The address continued from `address.base` through the pure nodes `root`'s pattern can encode, at most
// BUSTER_TIER1_SELECT_WALK of them: the conversions between a pointer and its 64 bits that the typed stream's integers
// put around every address (`inttoptr`, `ptrtoint`), a 64-bit addition of a constant, and addresses, each taken whole
// while the operand has room for its index, a constant index joining the displacement, and while the displacement fits
// 32 bits. A frame object's address ends the walk: its offset joins the displacement at emission, from the frame's
// layout, with the frame's base register, so no value is read for it. A memory operand may take shared address nodes
// too: other users that need their values keep them live through the effective uses. An address-valued root keeps
// the single-use rule, so it doesn't duplicate a shared lea. The object may live in the entry block past the walk's
// block: the producer materializes every slot address there, which dominates every use. No narrowed address or
// speculative load is introduced: the walk stops at the base, the first node it can't take.
static Tier1SelectAddress tier1_select_address(const Tier1SelectForest* forest, uint32_t root, Tier1SelectAddress address, bool shared)
{
    const IrFunction* function = forest->function;
    const uint32_t root_block = forest->blocks[root];
    bool walking = address.object == BUSTER_IR_NONE;
    for (uint32_t step = 0; walking && step < BUSTER_TIER1_SELECT_WALK; step += 1)
    {
        const uint32_t node = address.base;
        const uint32_t op = function->op[node];
        const uint32_t a = function->a[node];
        const uint32_t b = function->b[node];
        Tier1SelectAddress next = address;
        uint64_t constant = 0;
        uint32_t constant_node = BUSTER_IR_NONE;
        uint8_t peeled = 0;
        const uint32_t block = forest->blocks[node];
        bool taken = node < root && block == root_block && (shared || forest->uses[node] == 1);
        if (taken && op == IR_ADDRESS)
        {
            const IrAddress operand = function->addresses[function->c[node]];
            const bool constant_index = b != BUSTER_IR_NONE && tier1_select_constant(function, b, &constant);
            const bool indexed = b != BUSTER_IR_NONE && !constant_index;
            int64_t offset = 0;
            taken = (!indexed || address.index == BUSTER_IR_NONE) && !__builtin_mul_overflow((int64_t)constant, (int64_t)operand.scale, &offset) &&
                    !__builtin_add_overflow(offset, operand.offset, &offset) && !__builtin_add_overflow(address.displacement, offset, &next.displacement);
            next.scale = indexed ? (uint8_t)tier1_select_log2(operand.scale) : address.scale;
            next.index = indexed && taken ? tier1_select_index(forest, root, b, &next.scale, &next.displacement, &peeled) : address.index;
            next.base = a;
            constant_node = constant_index ? b : BUSTER_IR_NONE;
        }
        else if (taken && op == IR_ADD && function->type[node] == IR_I64)
        {
            const bool right = tier1_select_constant(function, b, &constant);
            const bool left = !right && tier1_select_constant(function, a, &constant);
            if (right || left)
            {
                taken = !__builtin_add_overflow(address.displacement, (int64_t)constant, &next.displacement);
                next.base = right ? a : b;
                constant_node = right ? b : a;
            }
            else
            {
                // The typed stream also spells a pointer offset as inttoptr(add(ptrtoint(base), index)).
                // Its full-width addition is the memory operand's base and index, not a separate lea. Never
                // cross a narrow addition/extension or introduce a second index into an existing address.
                taken = address.index == BUSTER_IR_NONE;
                if (taken)
                {
                    next.base = a;
                    next.scale = 0;
                    next.index = tier1_select_index(forest, root, b, &next.scale, &next.displacement, &peeled);
                }
            }
        }
        else if (op == IR_STACK_ADDR && tier1_select_stack_object(function, a) && node < root && (shared || forest->uses[node] == 1) &&
                 (block == root_block || block == 0))
        {
            // A stable frame base and object offset cost no allocated register at a memory use, even when another use
            // keeps the address itself. Only this leaf may cross the entry block; longer cross-block chains stay put.
            next.object = a;
            next.base = BUSTER_IR_NONE;
            taken = true;
        }
        else
        {
            taken = taken && ((op == IR_INTTOPTR && function->type[a] == IR_I64) || (op == IR_PTRTOINT && function->type[node] == IR_I64));
            next.base = a;
        }
        walking = taken && next.displacement == (int64_t)(int32_t)next.displacement;
        if (walking)
        {
            next.folds = (uint8_t)(address.folds + 1 + peeled + (constant_node != BUSTER_IR_NONE && tier1_select_tree_child(forest, root, constant_node) ? 1 : 0));
            address = next;
            walking = address.object == BUSTER_IR_NONE;
        }
    }
    return address;
}

// Whether `value`, an operand of `node`'s AND of masks, is a comparison into a mask that the AND's pattern takes as its
// own under the other operand as its writemask (#1847): a vector comparison of the AND's mask type (the lanes' mask) and
// one of its predicates, which only the AND reads, in its block.
static bool tier1_select_masked_compare(const Tier1SelectForest* forest, uint32_t node, uint32_t value)
{
    const IrFunction* function = forest->function;
    const bool compare = value < function->instruction_count && function->op[value] == IR_VCMP && function->type[value] == function->type[node] &&
                         function->c[value] < IR_PREDICATE_COUNT && function->a[value] < function->instruction_count;
    return compare && tier1_select_vector_width(function->type[function->a[value]]) != 0 && tier1_select_tree_child(forest, node, value);
}

// The memory operand of `node`'s access through `pointer`, in values[first] and values[first + 1] (its base and index),
// with its scale and displacement. Returns whether the walk took anything.
static inline __attribute__((always_inline)) bool tier1_select_memory(const Tier1SelectForest* forest, uint32_t node, uint32_t pointer, Tier1SelectPattern* pattern, uint32_t first)
{
    const Tier1SelectAddress address = tier1_select_address(forest, node, (Tier1SelectAddress){ .base = pointer, .index = BUSTER_IR_NONE, .object = BUSTER_IR_NONE }, true);
    pattern->values[first] = address.base;
    pattern->values[first + 1] = address.index;
    pattern->stack_object = address.object;
    pattern->scale = address.scale;
    pattern->displacement = (int32_t)address.displacement;
    pattern->fold_count += address.folds;
    return address.folds != 0;
}

// Inlined at each of its two calls, the test reference's batch (tier1_select_cover_munch) and the bottom-up path's pattern
// (tier1_select_burs_pattern), and so are its helpers (tier1_select_add_candidate, tier1_select_memory,
// tier1_select_load_folds), whose inlining clang decided by their callers. With a second caller, clang outlined it, its
// 356 bytes returned through memory, and inlined fewer of its helpers: the former scalar maximal-munch path took 304 core cycles
// per IR instruction on the frozen sqlite3.c and 841 instructions, against 274 and 747 before the second caller and 276
// and 752 inlined by force, in tier1_select_cover_munch (benchmark-tier1-select-source, the 9700X). One copy out of line
// for both calls took 300 and 815.
static inline __attribute__((always_inline)) Tier1SelectCandidates tier1_select_candidates(const Tier1SelectForest* forest, uint32_t node)
{
    const IrFunction* function = forest->function;
    const uint32_t op = function->op[node];
    const uint32_t a = function->a[node];
    const uint32_t b = function->b[node];
    const uint32_t c = function->c[node];
    const uint32_t type = function->type[node];
    const uint32_t width = tier1_select_width(type);
    const bool sign_storage = type == IR_I32 && forest->canonical != 0 &&
                              (forest->canonical[node] & (BUSTER_TIER1_STORAGE_ZERO | BUSTER_TIER1_STORAGE_SIGN)) == BUSTER_TIER1_STORAGE_SIGN;
    const bool sign_result = sign_storage && (op == IR_PARAM || tier1_select_sign_birth(function, node));
    Tier1SelectCandidates candidates = {0};
    Tier1SelectPattern base =
    {
        .values = {BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE},
        .stack_object = BUSTER_IR_NONE,
        .operation = (uint8_t)tier1_select_operation(op),
        .width = (uint8_t)(sign_storage && !sign_result ? 8 : width),
        .storage_width = type == IR_I32 && forest->canonical != 0 && (forest->canonical[node] & BUSTER_TIER1_STORAGE_WIDE) != 0 ? 8 : 0,
        .sign_result = sign_result,
    };
    const bool integer = type == IR_I32 || type == IR_I64;
    // A mask's operations (#179): the stored mask's width for a store, the result's for the others; and a floating
    // value's (#141), the same way.
    const uint32_t mask_width = tier1_select_mask_width(op == IR_STORE && b < function->instruction_count ? function->type[b] : type);
    const uint32_t float_width = tier1_select_float_width(op == IR_STORE && b < function->instruction_count ? function->type[b] : type);
    // The operand's width where it is a floating value: a comparison's, a conversion's from one or a return's.
    const uint32_t operand_float = a < function->instruction_count ? tier1_select_float_width(function->type[a]) : 0;
    // A vector's (#141): the stored vector's width for a store, the result's for the others, and a comparison's operands';
    // none in a function without vectors.
    const uint32_t vector_width = forest->vectors ? tier1_select_vector_width(op == IR_STORE && b < function->instruction_count ? function->type[b] : type) : 0;
    const uint32_t compare_width = forest->vectors && op == IR_VCMP && a < function->instruction_count ? tier1_select_vector_width(function->type[a]) : 0;
    // The vector class's value's width, a floating value's or a vector's: its loads', stores' and phis' rules are one.
    const uint32_t class_width = float_width != 0 ? float_width : vector_width;
    // A vector's operations (#141), behind one test: a function without vectors pays no other here.
    const bool vector_node = (vector_width != 0 && (op == IR_VCONST || op == IR_UNDEF || op == IR_COPY || op == IR_NOT || op == IR_ADD || op == IR_SUB || op == IR_MUL || op == IR_AND ||
                                                   op == IR_OR || op == IR_XOR || op == IR_SHL || op == IR_LSHR || op == IR_ASHR || (op >= IR_FADD && op <= IR_FDIV) ||
                                                   op == IR_VSELECT || (op == IR_INTRINSIC && (c == BUSTER_IR_INTRINSIC_SPLAT || (c >= BUSTER_IR_INTRINSIC_EXTRACT && c <= BUSTER_IR_INTRINSIC_LANE_OP) || ir_intrinsic_ternary(c) ||
                                                                           c == BUSTER_IR_INTRINSIC_AES || c == BUSTER_IR_INTRINSIC_LANE_INSERT || c == BUSTER_IR_INTRINSIC_FROUND)))) ||
                             compare_width != 0;
    if (vector_node)
    {
        if (vector_width != 0 && (op == IR_VCONST || op == IR_UNDEF))
        {
            // A constant's lanes: all zeros, every bit or one lane in all, which no memory needs; any other, the CFG
            // consumer's from its payload. An undefined vector is any: zeros.
            const IrSimdValue* payload = op == IR_VCONST && a < function->vector_constant_count ? &function->vector_constants[a] : 0;
            const uint32_t first = payload != 0 ? (uint32_t)payload->words[0] : 0;
            bool zero = true;
            bool ones = true;
            bool splat = true;
            for (uint32_t lane = 0; payload != 0 && lane < vector_width / 4; lane += 1)
            {
                const uint32_t bits = (uint32_t)(payload->words[lane / 2] >> (32 * (lane & 1)));
                zero &= bits == 0;
                ones &= bits == UINT32_MAX;
                splat &= bits == first;
            }
            base.rule = TIER1_RULE_VECTOR;
            base.width = (uint8_t)vector_width;
            base.scale = (uint8_t)(zero ? BUSTER_TIER1_VECTOR_ZERO : ones ? BUSTER_TIER1_VECTOR_ONES : splat ? BUSTER_TIER1_VECTOR_SPLAT : BUSTER_TIER1_VECTOR_CONSTANT);
            base.immediate = zero || ones ? 0 : splat ? first : a;
            base.intrinsic_cost = zero || ones ? 1 : 2;
            tier1_select_add_candidate(forest, node, &candidates, base);
        }
        else if (vector_width != 0 && op == IR_INTRINSIC && c == BUSTER_IR_INTRINSIC_SPLAT)
        {
            base.rule = TIER1_RULE_VECTOR;
            base.width = (uint8_t)vector_width;
            base.scale = BUSTER_TIER1_VECTOR_SCALAR_SPLAT;
            base.source_width = function->type[a];
            base.values[0] = a;
            base.intrinsic_cost = 1;
            tier1_select_add_candidate(forest, node, &candidates, base);
        }
        else if (vector_width != 0 && op == IR_INTRINSIC && c >= BUSTER_IR_INTRINSIC_EXTRACT && c <= BUSTER_IR_INTRINSIC_INSERT)
        {
            // A width change (#1840): the other vector's width, and the part in the IR's flags.
            const uint32_t other = c == BUSTER_IR_INTRINSIC_INSERT ? b : a;
            base.rule = TIER1_RULE_VECTOR;
            base.width = (uint8_t)vector_width;
            base.scale = BUSTER_TIER1_VECTOR_WIDTH_CHANGE;
            base.operation = (uint8_t)(c - BUSTER_IR_INTRINSIC_EXTRACT);
            base.source_width = other < function->instruction_count ? (uint8_t)tier1_select_vector_width(function->type[other]) : 0;
            base.immediate = function->flags[node];
            base.values[0] = a;
            if (c == BUSTER_IR_INTRINSIC_INSERT)
            {
                base.values[1] = b;
            }
            base.intrinsic_cost = 1;
            tier1_select_add_candidate(forest, node, &candidates, base);
        }
        else if (vector_width != 0 && op == IR_INTRINSIC && c >= BUSTER_IR_INTRINSIC_PERMUTE && c <= BUSTER_IR_INTRINSIC_SHUFFLE_LANES)
        {
            // A permute (#1840): its immediate in the IR's flags; no candidate where its width has no form.
            base.rule = TIER1_RULE_VECTOR;
            base.width = (uint8_t)vector_width;
            base.scale = BUSTER_TIER1_VECTOR_PERMUTE;
            base.operation = (uint8_t)(c - BUSTER_IR_INTRINSIC_PERMUTE);
            base.immediate = function->flags[node];
            base.values[0] = a;
            base.values[1] = b;
            base.intrinsic_cost = 1;
            if (tier1_select_permute_form(c - BUSTER_IR_INTRINSIC_PERMUTE, function->flags[node], vector_width) != X86_FORM_NONE)
            {
                tier1_select_add_candidate(forest, node, &candidates, base);
            }
        }
        else if (vector_width != 0 && op == IR_INTRINSIC && (c == BUSTER_IR_INTRINSIC_EXTEND || c == BUSTER_IR_INTRINSIC_TRUNCATE))
        {
            // A conversion (#1840): its lanes and mode in the IR's flags, the source's width beside them.
            base.rule = TIER1_RULE_VECTOR;
            base.width = (uint8_t)vector_width;
            base.scale = BUSTER_TIER1_VECTOR_CONVERT;
            base.operation = (uint8_t)(c - BUSTER_IR_INTRINSIC_EXTEND);
            base.immediate = function->flags[node];
            base.source_width = a < function->instruction_count ? (uint8_t)tier1_select_vector_width(function->type[a]) : 0;
            base.values[0] = a;
            base.intrinsic_cost = 1;
            if (tier1_select_convert_form(c - BUSTER_IR_INTRINSIC_EXTEND, function->flags[node], vector_width, base.source_width) != X86_FORM_NONE)
            {
                tier1_select_add_candidate(forest, node, &candidates, base);
            }
        }
        else if (vector_width != 0 && op == IR_INTRINSIC && (ir_intrinsic_kind(c) == BUSTER_IR_INTRINSIC_COMPRESS || ir_intrinsic_kind(c) == BUSTER_IR_INTRINSIC_EXPAND) &&
                 ir_intrinsic_ternary(c))
        {
            // A compress or an expand (#1840): its mask b, its passthrough the extra array's (ir_intrinsic_third).
            base.rule = TIER1_RULE_VECTOR;
            base.width = (uint8_t)vector_width;
            base.scale = BUSTER_TIER1_VECTOR_COMPRESS;
            base.operation = (uint8_t)(ir_intrinsic_kind(c) - BUSTER_IR_INTRINSIC_COMPRESS);
            base.immediate = function->flags[node];
            base.values[0] = a;
            base.values[1] = b;
            base.values[2] = ir_intrinsic_third(function, node);
            base.intrinsic_cost = 1;
            tier1_select_add_candidate(forest, node, &candidates, base);
        }
        else if (vector_width != 0 && op == IR_INTRINSIC && c == BUSTER_IR_INTRINSIC_LANE_INSERT)
        {
            // A lane's insertion (#1840): the vector values[0], the general values[1], the lane in the IR's flags.
            base.rule = TIER1_RULE_VECTOR;
            base.width = (uint8_t)vector_width;
            base.scale = BUSTER_TIER1_VECTOR_LANE_INSERT;
            base.immediate = function->flags[node];
            base.values[0] = a;
            base.values[1] = b;
            base.intrinsic_cost = 1;
            tier1_select_add_candidate(forest, node, &candidates, base);
        }
        else if (vector_width == 64 && op == IR_INTRINSIC && c == BUSTER_IR_INTRINSIC_FROUND)
        {
            // A floating operation at its own rounding (#1840): its operation, lanes and rounding in the IR's flags.
            base.rule = TIER1_RULE_VECTOR;
            base.width = (uint8_t)vector_width;
            base.scale = BUSTER_TIER1_VECTOR_FROUND;
            base.immediate = function->flags[node];
            base.values[0] = a;
            base.values[1] = b;
            base.intrinsic_cost = 1;
            tier1_select_add_candidate(forest, node, &candidates, base);
        }
        else if (vector_width != 0 && op == IR_INTRINSIC && c == BUSTER_IR_INTRINSIC_AES)
        {
            // An AES round (#1840): its round in the IR's flags.
            base.rule = TIER1_RULE_VECTOR;
            base.width = (uint8_t)vector_width;
            base.scale = BUSTER_TIER1_VECTOR_AES;
            base.immediate = function->flags[node];
            base.values[0] = a;
            base.values[1] = b;
            base.intrinsic_cost = 1;
            tier1_select_add_candidate(forest, node, &candidates, base);
        }
        else if (vector_width != 0 && op == IR_INTRINSIC && ir_intrinsic_kind(c) == BUSTER_IR_INTRINSIC_TERNLOG && ir_intrinsic_ternary(c))
        {
            // A bitwise function of three vectors (#1840): its third operand the extra array's (ir_intrinsic_third).
            base.rule = TIER1_RULE_VECTOR;
            base.width = (uint8_t)vector_width;
            base.scale = BUSTER_TIER1_VECTOR_TERNLOG;
            base.immediate = function->flags[node];
            base.values[0] = a;
            base.values[1] = b;
            base.values[2] = ir_intrinsic_third(function, node);
            base.intrinsic_cost = 1;
            tier1_select_add_candidate(forest, node, &candidates, base);
        }
        else if (vector_width != 0 && op == IR_INTRINSIC && ir_intrinsic_kind(c) == BUSTER_IR_INTRINSIC_PERMUTE2 && ir_intrinsic_ternary(c))
        {
            // A permute of two tables (#1840): its third operand the extra array's (ir_intrinsic_third).
            base.rule = TIER1_RULE_VECTOR;
            base.width = (uint8_t)vector_width;
            base.scale = BUSTER_TIER1_VECTOR_PERMUTE2;
            base.immediate = function->flags[node];
            base.values[0] = a;
            base.values[1] = b;
            base.values[2] = ir_intrinsic_third(function, node);
            base.intrinsic_cost = 1;
            tier1_select_add_candidate(forest, node, &candidates, base);
        }
        else if (vector_width != 0 && op == IR_INTRINSIC && c == BUSTER_IR_INTRINSIC_LANE_OP)
        {
            // A lane operation (#1840): its lanes and operation in the IR's flags; no candidate where its width has no form.
            const uint32_t flags = function->flags[node];
            base.rule = TIER1_RULE_VECTOR;
            base.width = (uint8_t)vector_width;
            base.scale = BUSTER_TIER1_VECTOR_LANE_OP;
            base.operation = (uint8_t)flags;
            base.values[0] = a;
            base.values[1] = b;
            base.intrinsic_cost = 1;
            if (ir_simd_lane_op_forms[(flags >> 2) & 7][flags & 3][vector_width == 16 ? 0 : vector_width == 32 ? 1 : 2] != X86_FORM_NONE)
            {
                tier1_select_add_candidate(forest, node, &candidates, base);
            }
        }
        else if (vector_width != 0 && op == IR_INTRINSIC && c == BUSTER_IR_INTRINSIC_SHIFT)
        {
            // A shift by a scalar count (#1840): where the count is a constant, by the immediate form alone, which leaves
            // the count unused, first, as the bottom-up path's rule takes its kind's first candidate (tier1_burs_pick) and
            // munch's ranks tie; and by the count's register, vmovd and the shift.
            base.rule = TIER1_RULE_VECTOR;
            base.width = (uint8_t)vector_width;
            base.scale = BUSTER_TIER1_VECTOR_SHIFT;
            base.operation = (uint8_t)function->flags[node];
            base.values[0] = a;
            uint64_t count = 0;
            if (tier1_select_constant(function, b, &count))
            {
                Tier1SelectPattern immediate = base;
                immediate.condition = 1;
                immediate.immediate = count < 255 ? count : 255;
                immediate.intrinsic_cost = 1;
                tier1_select_add_candidate(forest, node, &candidates, immediate);
            }
            base.values[1] = b;
            base.intrinsic_cost = 2;
            tier1_select_add_candidate(forest, node, &candidates, base);
        }
        else if (vector_width != 0 && (op == IR_COPY || op == IR_NOT))
        {
            base.rule = TIER1_RULE_VECTOR;
            base.width = (uint8_t)vector_width;
            base.scale = op == IR_COPY ? BUSTER_TIER1_VECTOR_COPY : BUSTER_TIER1_VECTOR_NOT;
            base.values[0] = a;
            base.intrinsic_cost = 1;
            tier1_select_add_candidate(forest, node, &candidates, base);
        }
        else if (vector_width != 0 && (op == IR_ADD || op == IR_SUB || op == IR_MUL || op == IR_AND || op == IR_OR || op == IR_XOR || op == IR_SHL || op == IR_LSHR ||
                                       op == IR_ASHR || (op >= IR_FADD && op <= IR_FDIV)))
        {
            // Its operation (BUSTER_TIER1_VECTOR_ADD ...) and its lanes, the IR's flags, in `source_width` (#141's
            // element types), which a form exists for at its width or it has no candidate (tier1_select_lane_form).
            const uint32_t operation = op == IR_ADD ? BUSTER_TIER1_VECTOR_ADD : op == IR_SUB ? BUSTER_TIER1_VECTOR_SUB : op == IR_MUL ? BUSTER_TIER1_VECTOR_MUL :
                                       op == IR_AND ? BUSTER_TIER1_VECTOR_AND : op == IR_OR ? BUSTER_TIER1_VECTOR_OR : op == IR_XOR ? BUSTER_TIER1_VECTOR_XOR :
                                       op == IR_SHL ? BUSTER_TIER1_VECTOR_SHL : op == IR_LSHR ? BUSTER_TIER1_VECTOR_LSHR : op == IR_ASHR ? BUSTER_TIER1_VECTOR_ASHR :
                                                      BUSTER_TIER1_VECTOR_FADD + (uint32_t)(op - IR_FADD);
            const uint32_t lane = function->flags[node];
            base.rule = TIER1_RULE_VECTOR;
            base.width = (uint8_t)vector_width;
            base.scale = BUSTER_TIER1_VECTOR_BINARY;
            base.operation = (uint8_t)operation;
            base.source_width = (uint8_t)lane;
            base.values[0] = a;
            base.values[1] = b;
            base.intrinsic_cost = 1;
            if (tier1_select_lane_form(operation, lane, vector_width, false) != X86_FORM_NONE)
            {
                tier1_select_add_candidate(forest, node, &candidates, base);
            }
        }
        else if (compare_width != 0 && mask_width != 0 && c < IR_PREDICATE_COUNT)
        {
            // A comparison of signed integer lanes less than a zero vector (#1840: a vector's sign bits as a mask,
            // vpmovb2m to vpmovq2m), first, as the bottom-up path takes its kind's first candidate.
            if (c == IR_LT && function->flags[node] < IR_SIMD_LANE_F32 && tier1_select_vector_fill(function, b, compare_width) == 0)
            {
                Tier1SelectPattern sign = base;
                sign.rule = TIER1_RULE_VECTOR;
                sign.width = (uint8_t)compare_width;
                sign.scale = BUSTER_TIER1_VECTOR_SIGN_MASK;
                sign.source_width = function->flags[node];
                sign.values[0] = a;
                sign.intrinsic_cost = 1;
                tier1_select_add_candidate(forest, node, &candidates, sign);
            }
            base.rule = TIER1_RULE_VECTOR;
            base.width = (uint8_t)compare_width;
            base.scale = BUSTER_TIER1_VECTOR_COMPARE;
            base.condition = (uint8_t)c;
            base.source_width = function->flags[node];
            base.values[0] = a;
            base.values[1] = b;
            base.intrinsic_cost = 1;
            tier1_select_add_candidate(forest, node, &candidates, base);
        }
        else if (vector_width != 0 && op == IR_VSELECT)
        {
            // A select of all ones and zeros (#1840: a mask to a vector, vpmovm2b to vpmovm2q, and a C comparison's
            // expansion), first, as the bottom-up path takes its kind's first candidate.
            if (tier1_select_vector_fill(function, b, vector_width) == 1 && tier1_select_vector_fill(function, c, vector_width) == 0)
            {
                Tier1SelectPattern expand = base;
                expand.rule = TIER1_RULE_VECTOR;
                expand.width = (uint8_t)vector_width;
                expand.scale = BUSTER_TIER1_VECTOR_MASK_EXPAND;
                expand.source_width = function->flags[node];
                expand.values[0] = a;
                expand.intrinsic_cost = 1;
                tier1_select_add_candidate(forest, node, &candidates, expand);
            }
            base.rule = TIER1_RULE_VECTOR;
            base.width = (uint8_t)vector_width;
            base.scale = BUSTER_TIER1_VECTOR_SELECT;
            base.source_width = function->flags[node];
            base.values[0] = a;
            base.values[1] = b;
            base.values[2] = c;
            base.intrinsic_cost = 1;
            tier1_select_add_candidate(forest, node, &candidates, base);
        }
    }
    else if (mask_width != 0 && (op == IR_CONST || op == IR_UNDEF))
    {
        // An undefined mask is any mask: zero, kxor's.
        base.rule = TIER1_RULE_MASK_CONSTANT;
        base.width = (uint8_t)mask_width;
        base.immediate = op == IR_CONST ? function->constants[a].low & (mask_width < 8 ? (UINT64_C(1) << (mask_width * 8)) - 1 : UINT64_MAX) : 0;
        const uint64_t every = mask_width < 8 ? (UINT64_C(1) << (mask_width * 8)) - 1 : UINT64_MAX;
        base.intrinsic_cost = base.immediate == 0 || base.immediate == every ? 1 : 2;
        tier1_select_add_candidate(forest, node, &candidates, base);
    }
    else if (mask_width != 0 && (op == IR_COPY || (op == IR_BITCAST && function->type[a] == type)))
    {
        base.rule = TIER1_RULE_MASK_COPY;
        base.width = (uint8_t)mask_width;
        base.values[0] = a;
        base.intrinsic_cost = 1;
        tier1_select_add_candidate(forest, node, &candidates, base);
    }
    else if (mask_width != 0 && (op == IR_AND || op == IR_OR || op == IR_XOR))
    {
        base.rule = TIER1_RULE_MASK_LOGIC;
        base.width = (uint8_t)mask_width;
        base.values[0] = a;
        base.values[1] = b;
        base.intrinsic_cost = 1;
        tier1_select_add_candidate(forest, node, &candidates, base);
        // A comparison into a mask ANDed with another mask (#1847: the IR's form of a masked comparison's row,
        // ir_simd_typed_compare_mask, whose writemask isn't every lane, ir_typed_unmasked): the comparison under that mask
        // as its writemask, `{k}`, which leaves 0 in the lanes the writemask doesn't have, one vpcmp or vcmpps as the
        // row's was. The comparison is the AND's tree child, either operand, the left one first.
        const uint32_t compared = op != IR_AND ? BUSTER_IR_NONE : tier1_select_masked_compare(forest, node, a) ? a : tier1_select_masked_compare(forest, node, b) ? b : BUSTER_IR_NONE;
        if (compared != BUSTER_IR_NONE)
        {
            Tier1SelectPattern masked = base;
            masked.rule = TIER1_RULE_VECTOR;
            masked.width = (uint8_t)tier1_select_vector_width(function->type[function->a[compared]]);
            masked.scale = BUSTER_TIER1_VECTOR_COMPARE;
            masked.condition = (uint8_t)function->c[compared];
            masked.source_width = function->flags[compared];
            masked.values[0] = function->a[compared];
            masked.values[1] = function->b[compared];
            masked.values[2] = compared == a ? b : a;
            tier1_select_fold(forest, node, compared, &masked);
            tier1_select_add_candidate(forest, node, &candidates, masked);
        }
    }
    else if (mask_width != 0 && op == IR_NOT)
    {
        base.rule = TIER1_RULE_MASK_NOT;
        base.width = (uint8_t)mask_width;
        base.values[0] = a;
        base.intrinsic_cost = 1;
        tier1_select_add_candidate(forest, node, &candidates, base);
    }
    else if (mask_width != 0 && op == IR_BITCAST && ir_integer(function->type[a]))
    {
        // The verifier's rule: an integer of the mask's bits, i8 to i64.
        base.rule = TIER1_RULE_MASK_FROM_INTEGER;
        base.width = (uint8_t)mask_width;
        base.values[0] = a;
        base.intrinsic_cost = 1;
        tier1_select_add_candidate(forest, node, &candidates, base);
    }
    else if (op == IR_BITCAST && ir_integer(type) && tier1_select_mask_width(function->type[a]) != 0)
    {
        base.rule = TIER1_RULE_MASK_TO_INTEGER;
        base.width = (uint8_t)tier1_select_mask_width(function->type[a]);
        base.values[0] = a;
        base.intrinsic_cost = 1;
        tier1_select_add_candidate(forest, node, &candidates, base);
    }
    else if (mask_width != 0 && (op == IR_LOAD || op == IR_STORE))
    {
        // kmov of the mask's width, from or to the address, folded into its memory operand as an integer's is.
        const bool store = op == IR_STORE;
        base.rule = store ? TIER1_RULE_MASK_STORE : TIER1_RULE_MASK_LOAD;
        base.width = (uint8_t)mask_width;
        base.values[0] = a;
        base.values[2] = store ? b : BUSTER_IR_NONE;
        base.intrinsic_cost = 1;
        Tier1SelectPattern folded = base;
        const bool walked = tier1_select_memory(forest, node, a, &folded, 0);
        // Through a frame object the folded address stands alone, as an integer's does.
        if (!walked || folded.stack_object == BUSTER_IR_NONE)
        {
            tier1_select_add_candidate(forest, node, &candidates, base);
        }
        if (walked)
        {
            folded.rule = store ? TIER1_RULE_MASK_STORE_ADDRESS : TIER1_RULE_MASK_LOAD_ADDRESS;
            tier1_select_add_candidate(forest, node, &candidates, folded);
        }
    }
    else if (mask_width != 0 && op == IR_PHI)
    {
        base.rule = TIER1_RULE_PHI;
        base.width = (uint8_t)mask_width;
        tier1_select_add_candidate(forest, node, &candidates, base);
    }
    // Long double (#141): each value a frame object of its own, which the CFG consumer's x87 records read and write
    // (TIER1_RULE_X87); a phi of them its edges' copies, and a call's result and a return's operand st0.
    else if (op != IR_PHI && op != IR_CALL && op != IR_CALL_INDIRECT && op != IR_RET && tier1_select_x87_node(function, node))
    {
        const uint32_t operand = a < function->instruction_count ? function->type[a] : IR_VOID;
        base.rule = TIER1_RULE_X87;
        base.width = (uint8_t)width;
        base.intrinsic_cost = 3;
        base.scale = op == IR_CONST ? BUSTER_TIER1_X87_CONSTANT : op == IR_UNDEF ? BUSTER_TIER1_X87_UNDEF : op == IR_LOAD ? BUSTER_TIER1_X87_LOAD : op == IR_STORE ? BUSTER_TIER1_X87_STORE :
                     op == IR_COPY ? BUSTER_TIER1_X87_COPY : op == IR_FNEG ? BUSTER_TIER1_X87_NEGATE : op == IR_FCMP ? BUSTER_TIER1_X87_COMPARE :
                     op == IR_FEXT ? BUSTER_TIER1_X87_FROM_FLOAT : op == IR_FTRUNC ? BUSTER_TIER1_X87_TO_FLOAT : op == IR_SITOFP || op == IR_UITOFP ? BUSTER_TIER1_X87_FROM_INT :
                     op == IR_FPTOSI || op == IR_FPTOUI ? BUSTER_TIER1_X87_TO_INT : BUSTER_TIER1_X87_BINARY;
        base.operation = op >= IR_FADD && op <= IR_FDIV ? (uint8_t)(op - IR_FADD) : op == IR_SITOFP || op == IR_FPTOSI ? 1 : 0;
        base.immediate = op == IR_CONST ? a : 0;
        base.condition = op == IR_FCMP ? (uint8_t)c : 0;
        // The operands no register holds aren't values: the address of a load and a store, the floating or integer
        // operand of a conversion to a long double, are.
        base.values[0] = op == IR_LOAD || op == IR_STORE || op == IR_FEXT || op == IR_SITOFP || op == IR_UITOFP ? a : BUSTER_IR_NONE;
        base.source_width = (uint8_t)(op == IR_FEXT ? tier1_select_float_width(operand) : op == IR_SITOFP || op == IR_UITOFP ? tier1_select_memory_width(operand) : 0);
        base.width = (uint8_t)(op == IR_FTRUNC ? tier1_select_float_width(type) : op == IR_FPTOSI || op == IR_FPTOUI ? tier1_select_memory_width(type) : width);
        const bool supported = op == IR_CONST || op == IR_UNDEF || op == IR_LOAD || op == IR_STORE || op == IR_COPY || (op >= IR_FADD && op <= IR_FDIV) || op == IR_FNEG || op == IR_FCMP ||
                               (op == IR_FEXT && base.source_width != 0) || (op == IR_FTRUNC && base.width != 0) || ((op == IR_SITOFP || op == IR_UITOFP) && base.source_width != 0) ||
                               ((op == IR_FPTOSI || op == IR_FPTOUI) && base.width != 0);
        if (supported)
        {
            tier1_select_add_candidate(forest, node, &candidates, base);
        }
    }
    // Floating values (#141): an undefined one is any value, zero's xorps.
    else if (float_width != 0 && (op == IR_CONST || op == IR_UNDEF))
    {
        base.rule = TIER1_RULE_FLOAT_CONSTANT;
        base.width = (uint8_t)float_width;
        base.immediate = op == IR_CONST ? function->constants[a].low & (float_width == 4 ? UINT32_MAX : UINT64_MAX) : 0;
        base.intrinsic_cost = base.immediate == 0 ? 1 : 2;
        tier1_select_add_candidate(forest, node, &candidates, base);
    }
    else if (float_width != 0 && op == IR_COPY)
    {
        base.rule = TIER1_RULE_FLOAT_COPY;
        base.width = (uint8_t)float_width;
        base.values[0] = a;
        base.intrinsic_cost = 1;
        tier1_select_add_candidate(forest, node, &candidates, base);
    }
    else if (float_width != 0 && (op == IR_FADD || op == IR_FSUB || op == IR_FMUL || op == IR_FDIV))
    {
        base.rule = TIER1_RULE_FLOAT_BINARY;
        base.width = (uint8_t)float_width;
        base.operation = (uint8_t)(op - IR_FADD);
        base.values[0] = a;
        base.values[1] = b;
        base.intrinsic_cost = 3;
        tier1_select_add_candidate(forest, node, &candidates, base);
        // The right operand's load as the memory operand, or for add and multiply the left's where the right isn't a load,
        // as an integer's (TIER1_RULE_LOAD_OPERAND).
        const bool commutative = op == IR_FADD || op == IR_FMUL;
        const uint32_t load = function->op[b] == IR_LOAD ? b : commutative ? a : BUSTER_IR_NONE;
        if (tier1_select_load_folds(forest, node, load))
        {
            Tier1SelectPattern folded = base;
            folded.rule = TIER1_RULE_FLOAT_LOAD_OPERAND;
            folded.values[0] = load == b ? a : b;
            folded.values[1] = BUSTER_IR_NONE;
            folded.values[2] = BUSTER_IR_NONE;
            tier1_select_fold(forest, node, load, &folded);
            tier1_select_memory(forest, node, function->a[load], &folded, 1);
            tier1_select_add_candidate(forest, node, &candidates, folded);
        }
    }
    else if (float_width != 0 && op == IR_FNEG)
    {
        base.rule = TIER1_RULE_FLOAT_NEGATE;
        base.width = (uint8_t)float_width;
        base.values[0] = a;
        base.intrinsic_cost = 3;
        tier1_select_add_candidate(forest, node, &candidates, base);
    }
    else if (forest->vectors && (op == IR_MASKED_LOAD || op == IR_MASKED_STORE))
    {
        // A masked load or store (#1840): TIER1_RULE_FLOAT_LOAD's or _STORE's with `condition` its lanes' bytes' log2 plus
        // one, the address in values[0], unfolded, the mask in values[1] and the passthrough or the stored vector in
        // values[2] (tier1_select_masked_move).
        static const uint8_t sizes[4] = { 2, 0, 1, 3 };
        const bool store = op == IR_MASKED_STORE;
        base.rule = store ? TIER1_RULE_FLOAT_STORE : TIER1_RULE_FLOAT_LOAD;
        base.width = (uint8_t)tier1_select_vector_width(store && b < function->instruction_count ? function->type[b] : type);
        base.condition = (uint8_t)(1 + sizes[function->flags[node] & 3]);
        base.values[0] = a;
        base.values[1] = store ? c : b;
        base.values[2] = store ? b : c;
        base.intrinsic_cost = 1;
        if (base.width != 0 && function->flags[node] < 4)
        {
            tier1_select_add_candidate(forest, node, &candidates, base);
        }
    }
    else if (class_width != 0 && (op == IR_LOAD || op == IR_STORE))
    {
        const bool store = op == IR_STORE;
        base.rule = store ? TIER1_RULE_FLOAT_STORE : TIER1_RULE_FLOAT_LOAD;
        base.width = (uint8_t)class_width;
        base.values[0] = a;
        base.values[2] = store ? b : BUSTER_IR_NONE;
        base.intrinsic_cost = 1;
        Tier1SelectPattern folded = base;
        const bool walked = tier1_select_memory(forest, node, a, &folded, 0);
        // Through a frame object the folded address stands alone, as an integer's does.
        if (!walked || folded.stack_object == BUSTER_IR_NONE)
        {
            tier1_select_add_candidate(forest, node, &candidates, base);
        }
        if (walked)
        {
            folded.rule = store ? TIER1_RULE_FLOAT_STORE_ADDRESS : TIER1_RULE_FLOAT_LOAD_ADDRESS;
            tier1_select_add_candidate(forest, node, &candidates, folded);
        }
    }
    else if (class_width != 0 && op == IR_PHI)
    {
        base.rule = TIER1_RULE_PHI;
        base.width = (uint8_t)class_width;
        tier1_select_add_candidate(forest, node, &candidates, base);
    }
    else if (float_width != 0 && op == IR_PARAM && a < BUSTER_TIER1_SELECT_MAX_ARGUMENTS)
    {
        base.rule = TIER1_RULE_LEAF;
        base.width = (uint8_t)float_width;
        tier1_select_add_candidate(forest, node, &candidates, base);
    }
    else if (op == IR_FCMP && operand_float != 0)
    {
        base.rule = TIER1_RULE_FLOAT_COMPARE;
        base.width = (uint8_t)operand_float;
        base.values[0] = a;
        base.values[1] = b;
        base.condition = (uint8_t)c;
        base.intrinsic_cost = c == IR_EQ || c == IR_NE ? 5 : 3;
        tier1_select_add_candidate(forest, node, &candidates, base);
    }
    // Conversions (BUSTER_TIER1_FLOAT_FROM_SIGNED and the others): from an I8 to I64 signed, and an I1 to I64 unsigned, to
    // a floating value; from one to an I8 to I64, signed or unsigned; between the two precisions; and the bits of an F32
    // as an I32's or an F64 as an I64's, and back. An unsigned I64's, whose values past INT64_MAX no single conversion
    // takes, is TIER1_RULE_FLOAT_CONVERT_WIDE's sequence.
    else if ((float_width != 0 && ((op == IR_SITOFP && function->type[a] >= IR_I8 && function->type[a] <= IR_I64) || (op == IR_UITOFP && function->type[a] >= IR_I1 && function->type[a] <= IR_I64))) ||
             (operand_float != 0 && ((op == IR_FPTOSI && type >= IR_I8 && type <= IR_I64) || (op == IR_FPTOUI && type >= IR_I8 && type <= IR_I64))) ||
             (float_width != 0 && operand_float != 0 && (op == IR_FTRUNC || op == IR_FEXT)) ||
             (op == IR_BITCAST && float_width != 0 && function->type[a] == (float_width == 8 ? IR_I64 : IR_I32)) ||
             (op == IR_BITCAST && operand_float != 0 && type == (operand_float == 8 ? IR_I64 : IR_I32)))
    {
        const uint32_t source = function->type[a];
        const bool wide = (op == IR_UITOFP && source == IR_I64) || (op == IR_FPTOUI && type == IR_I64);
        base.rule = wide ? TIER1_RULE_FLOAT_CONVERT_WIDE : TIER1_RULE_FLOAT_CONVERT;
        base.values[0] = a;
        base.width = (uint8_t)(float_width != 0 ? float_width : tier1_select_memory_width(type));
        base.source_width = (uint8_t)(operand_float != 0 ? operand_float : source == IR_I1 ? 1 : tier1_select_memory_width(source));
        base.scale = (uint8_t)(op == IR_SITOFP ? BUSTER_TIER1_FLOAT_FROM_SIGNED : op == IR_UITOFP ? BUSTER_TIER1_FLOAT_FROM_UNSIGNED : op == IR_FPTOSI ? BUSTER_TIER1_FLOAT_TO_SIGNED :
                               op == IR_FPTOUI ? BUSTER_TIER1_FLOAT_TO_UNSIGNED : op == IR_BITCAST ? (float_width != 0 ? BUSTER_TIER1_FLOAT_FROM_BITS : BUSTER_TIER1_FLOAT_TO_BITS) : BUSTER_TIER1_FLOAT_RESIZE);
        base.intrinsic_cost = op == IR_BITCAST ? 1 : wide ? 10 : 2;
        tier1_select_add_candidate(forest, node, &candidates, base);
        // An integer constant's conversion is the floating constant it converts to, rounded as cvtsi2s* rounds under the
        // default rounding mode, to nearest: C's conversion on this host computes it so.
        uint64_t integer = 0;
        if ((op == IR_SITOFP || op == IR_UITOFP) && tier1_select_constant(function, a, &integer))
        {
            const uint32_t bits = ir_type_bits[source];
            const uint64_t magnitude = bits < 64 ? integer & ((UINT64_C(1) << bits) - 1) : integer;
            const int64_t signed_value = bits < 64 ? (int64_t)(magnitude << (64 - bits)) >> (64 - bits) : (int64_t)magnitude;
            Tier1SelectPattern folded = base;
            folded.rule = TIER1_RULE_FLOAT_CONSTANT;
            folded.values[0] = BUSTER_IR_NONE;
            folded.source_width = 0;
            folded.scale = 0;
            if (float_width == 8)
            {
                const double converted = op == IR_SITOFP ? (double)signed_value : (double)magnitude;
                memcpy(&folded.immediate, &converted, sizeof(converted));
            }
            else
            {
                const float converted = op == IR_SITOFP ? (float)signed_value : (float)magnitude;
                uint32_t pattern = 0;
                memcpy(&pattern, &converted, sizeof(converted));
                folded.immediate = pattern;
            }
            folded.intrinsic_cost = folded.immediate == 0 ? 1 : 2;
            tier1_select_fold(forest, node, a, &folded);
            tier1_select_add_candidate(forest, node, &candidates, folded);
        }
    }
    else if (op == IR_RET && a != BUSTER_IR_NONE && operand_float != 0)
    {
        base.rule = TIER1_RULE_FLOAT_RETURN;
        base.values[0] = a;
        base.width = (uint8_t)operand_float;
        base.intrinsic_cost = 2;
        tier1_select_add_candidate(forest, node, &candidates, base);
    }
    else if (op == IR_CONST && width != 0)
    {
        base.rule = TIER1_RULE_CONSTANT;
        const uint32_t bits = ir_type_bits[type];
        base.immediate = function->constants[a].low & (bits < 64 ? (UINT64_C(1) << bits) - 1 : UINT64_MAX);
        base.intrinsic_cost = 1;
        tier1_select_add_candidate(forest, node, &candidates, base);
    }
    else if (op == IR_UNDEF && width != 0)
    {
        // Any value is an undefined one's: zero, a constant like any other.
        base.rule = TIER1_RULE_CONSTANT;
        base.immediate = 0;
        base.intrinsic_cost = 1;
        tier1_select_add_candidate(forest, node, &candidates, base);
    }
    // A by-value aggregate parameter (#789): its copy's address, a frame object's past the IR's slots (or its stack slots'
    // in the incoming area, tier1_emit_cfg.c), as a frame object's is (TIER1_RULE_STACK_ADDRESS).
    else if (op == IR_PARAM && type == IR_PTR && tier1_select_aggregate_parameter(function, a))
    {
        base.rule = TIER1_RULE_STACK_ADDRESS;
        base.immediate = (uint64_t)function->slot_count + a;
        base.intrinsic_cost = 1;
        tier1_select_add_candidate(forest, node, &candidates, base);
    }
    else if (op == IR_PARAM && (integer || type == IR_PTR || type == IR_I1 || type == IR_I8 || type == IR_I16) && a < BUSTER_TIER1_SELECT_MAX_ARGUMENTS)
    {
        base.rule = TIER1_RULE_LEAF;
        tier1_select_add_candidate(forest, node, &candidates, base);
    }
    else if ((op == IR_COPY && width != 0) || (op == IR_INTTOPTR && function->type[a] == IR_I64) || (op == IR_PTRTOINT && type == IR_I64) || (op == IR_BITCAST && width != 0 && tier1_select_width(function->type[a]) == width && ir_type_bits[function->type[a]] == ir_type_bits[type]))
    {
        base.rule = TIER1_RULE_COPY;
        base.values[0] = a;
        // A copy in its own register is nothing (tier1_select_expand). The plan records a narrow source's width where the
        // planner's canonical column certifies its 32-bit storage (tier1_select_canonical: an entry MOVZX for narrow
        // parameters, the result MOVZX for narrow calls, and the producers' own full-width writes), so the copy is as
        // canonical as its source; the emitter doesn't read it.
        const uint8_t narrow_width = (uint8_t)tier1_select_narrow_width(function->type[a]);
        base.source_width = narrow_width != 0 && forest->canonical != 0 && (forest->canonical[a] & BUSTER_TIER1_STORAGE_ZERO) != 0 ? narrow_width : 0;
        base.intrinsic_cost = 1;
        // A pointer's 64 bits, or the pointer of 64 bits, from an address: the address's lea, or the copy of what the
        // conversions around it convert.
        const Tier1SelectAddress address = op == IR_INTTOPTR || op == IR_PTRTOINT ? tier1_select_address(forest, node, (Tier1SelectAddress){ .base = a, .index = BUSTER_IR_NONE, .object = BUSTER_IR_NONE }, false)
                                                                                  : (Tier1SelectAddress){ .base = a, .index = BUSTER_IR_NONE, .object = BUSTER_IR_NONE };
        // Through a frame object the walked address stands alone: its base would tie it where the object is past the
        // walk's block (a cross-block read costs nothing), and the paths would cover the object apart.
        if (address.folds == 0 || address.object == BUSTER_IR_NONE)
        {
            tier1_select_add_candidate(forest, node, &candidates, base);
        }
        if (address.folds != 0)
        {
            Tier1SelectPattern walked = base;
            // Through a frame object the address is a lea from the frame's base, never a copy of no value.
            walked.rule = address.object != BUSTER_IR_NONE || address.index != BUSTER_IR_NONE || address.displacement != 0 ? TIER1_RULE_ADDRESS : TIER1_RULE_COPY;
            walked.values[0] = address.base;
            walked.values[1] = address.index;
            walked.stack_object = address.object;
            walked.scale = address.scale;
            walked.displacement = (int32_t)address.displacement;
            walked.fold_count = address.folds;
            walked.source_width = 0;
            tier1_select_add_candidate(forest, node, &candidates, walked);
        }
    }
    else if ((op == IR_TRUNC && (type == IR_I8 || type == IR_I16 || type == IR_I32) && tier1_select_memory_width(function->type[a]) != 0 && function->type[a] != IR_PTR) ||
             (op == IR_ZEXT && (type == IR_I8 || type == IR_I16 || integer) && (function->type[a] == IR_I1 || function->type[a] == IR_I8 || function->type[a] == IR_I16 || function->type[a] == IR_I32)) ||
             (op == IR_SEXT && (type == IR_I16 || integer) && (function->type[a] == IR_I8 || function->type[a] == IR_I16 || function->type[a] == IR_I32)))
    {
        // A canonical source already has zero bits past its type, including I32 results of actual 32-bit writes.
        // A proven nonnegative signed extension is a zero extension; a truncation whose discarded bits are known zero
        // is a copy too. Neither semantic range facts alone nor an ABI's unspecified upper bits certify a copy.
        // For `zext(icmp)` the truth-value candidate below usually wins, as #140 folds the canonical comparison result;
        // this copy covers a comparison the fold leaves alone.
        const uint8_t narrow_width = (uint8_t)tier1_select_narrow_width(function->type[a]);
        const uint32_t source_bits = ir_type_bits[function->type[a]];
        const uint32_t unsigned_bits = forest->known_bits != 0 ? forest->known_bits[a] & BUSTER_TIER1_KNOWN_UNSIGNED_MASK : source_bits;
        const bool nonnegative = op == IR_SEXT && (unsigned_bits < source_bits ||
                                                  (forest->canonical != 0 && (forest->canonical[node] & BUSTER_TIER1_RANGE_NONNEGATIVE_USE) != 0));
        const bool canonical = forest->canonical != 0 && (forest->canonical[a] & BUSTER_TIER1_STORAGE_ZERO) != 0;
        const bool sign_copy = op == IR_SEXT && type == IR_I64 && function->type[a] == IR_I32 && forest->canonical != 0 &&
                               (forest->canonical[a] & BUSTER_TIER1_STORAGE_SIGN) != 0;
        const bool copy = sign_copy || (canonical && (op == IR_ZEXT || nonnegative || (op == IR_TRUNC && unsigned_bits <= ir_type_bits[type])));
        base.rule = copy ? TIER1_RULE_COPY : op == IR_TRUNC ? TIER1_RULE_TRUNCATE : op == IR_ZEXT || nonnegative ? TIER1_RULE_ZERO_EXTEND : TIER1_RULE_SIGN_EXTEND;
        base.values[0] = a;
        if (copy)
        {
            base.source_width = narrow_width != 0 ? narrow_width : (uint8_t)tier1_select_memory_width(function->type[a]);
            base.intrinsic_cost = 1;
        }
        else
        {
            base.width = sign_storage ? 8 : (uint8_t)tier1_select_memory_width(type);
            base.source_width = function->type[a] == IR_I1 ? 4 : (uint8_t)tier1_select_memory_width(function->type[a]);
            base.intrinsic_cost = op == IR_SEXT && type == IR_I16 ? 2 : 1;
        }
        tier1_select_add_candidate(forest, node, &candidates, base);
        // A narrow ALU write preserves an already-zero upper part while wrapping only the result's byte or word.
        // Copying its left input at 32 bits first (when needed) keeps that certificate. Both inputs must fit and have
        // physical zero storage, since the commutative register pattern may exchange them when the destination aliases.
        if (op == IR_TRUNC && (type == IR_I8 || type == IR_I16) && function->type[a] == IR_I32 && tier1_select_tree_child(forest, node, a) &&
            forest->known_bits != 0 && forest->canonical != 0)
        {
            const uint32_t operation = function->op[a];
            const uint32_t left = function->a[a];
            const uint32_t right = function->b[a];
            const uint32_t bits = ir_type_bits[type];
            const bool unary = operation == IR_NEG || operation == IR_NOT;
            const bool binary = operation == IR_ADD || operation == IR_SUB;
            const bool first = left < function->instruction_count && (forest->known_bits[left] & BUSTER_TIER1_KNOWN_UNSIGNED_MASK) <= bits && (forest->canonical[left] & BUSTER_TIER1_STORAGE_ZERO) != 0;
            const bool second = unary || (right < function->instruction_count && (forest->known_bits[right] & BUSTER_TIER1_KNOWN_UNSIGNED_MASK) <= bits && (forest->canonical[right] & BUSTER_TIER1_STORAGE_ZERO) != 0);
            if ((unary || binary) && first && second)
            {
                Tier1SelectPattern narrow = base;
                narrow.rule = unary ? TIER1_RULE_UNARY : TIER1_RULE_REGISTER;
                narrow.width = (uint8_t)(bits / 8);
                narrow.source_width = 0;
                narrow.values[0] = left;
                narrow.values[1] = unary ? BUSTER_IR_NONE : right;
                narrow.operation = (uint8_t)tier1_select_operation(operation);
                narrow.intrinsic_cost = 2;
                tier1_select_fold(forest, node, a, &narrow);
                tier1_select_add_candidate(forest, node, &candidates, narrow);
            }
        }
        // Undo a truncation of an already sign-extended value without manufacturing the intermediate masked value.
        // The original definition must have the destination's type and canonical storage, so future zero extensions
        // remain valid even if this copy coalesces. Effective uses keep it alive when the truncation has other users.
        const uint32_t original = op == IR_SEXT && function->op[a] == IR_TRUNC ? function->a[a] : BUSTER_IR_NONE;
        if (original < function->instruction_count && forest->known_bits != 0 && function->type[original] == type &&
            (forest->known_bits[original] >> 8) <= source_bits && (forest->canonical[original] & BUSTER_TIER1_STORAGE_ZERO) != 0 &&
            (!sign_storage || (forest->canonical[original] & BUSTER_TIER1_STORAGE_SIGN) != 0))
        {
            Tier1SelectPattern folded = base;
            folded.rule = TIER1_RULE_COPY;
            folded.values[0] = original;
            folded.width = sign_storage ? 8 : (uint8_t)tier1_select_width(type);
            folded.source_width = 0;
            folded.intrinsic_cost = 1;
            tier1_select_fold(forest, node, a, &folded);
            tier1_select_add_candidate(forest, node, &candidates, folded);
        }
        else if (original < function->instruction_count && base.rule == TIER1_RULE_SIGN_EXTEND && tier1_select_tree_child(forest, node, a))
        {
            // sext(trunc(x)) reads x's low source-width bits directly. Drop only the register truncation; a wider
            // load producing x keeps its original access width and ordering, even when all higher bits are discarded.
            Tier1SelectPattern projected = base;
            projected.values[0] = original;
            tier1_select_fold(forest, node, a, &projected);
            tier1_select_add_candidate(forest, node, &candidates, projected);
        }
        // A bounded signed offset can be computed at 64 bits directly. Every skipped I32 add/sub must be proven not
        // to overflow; the remaining base must have a physical signed-64 representation, including after spills.
        // This covers Lua's signed instruction fields: sext64((masked_i32 - bias) + 1), without assuming I32 wrapping
        // arithmetic in general is a 64-bit address expression. Eight signed imm32 additions cannot overflow int64_t.
        if (op == IR_SEXT && type == IR_I64 && function->type[a] == IR_I32 && base.rule == TIER1_RULE_SIGN_EXTEND && forest->known_bits != 0 && forest->canonical != 0)
        {
            uint32_t value = a;
            int64_t addend = 0;
            Tier1SelectPattern affine = base;
            for (uint32_t depth = 0; depth < BUSTER_TIER1_SELECT_WALK && tier1_select_tree_child(forest, node, value); depth += 1)
            {
                const uint32_t operation = function->op[value];
                uint32_t input = function->a[value];
                uint32_t constant = function->b[value];
                if (operation == IR_ADD && function->op[constant] != IR_CONST && function->op[input] == IR_CONST)
                {
                    const uint32_t swap = constant;
                    constant = input;
                    input = swap;
                }
                const bool arithmetic = (operation == IR_ADD || operation == IR_SUB) && function->type[value] == IR_I32 && function->op[constant] == IR_CONST;
                const uint32_t input_signed = arithmetic ? forest->known_bits[input] >> 8 : 32;
                const uint32_t constant_signed = arithmetic ? forest->known_bits[constant] >> 8 : 32;
                const int64_t immediate = arithmetic ? (int32_t)function->constants[function->a[constant]].low : 0;
                const int64_t delta = operation == IR_SUB ? -immediate : immediate;
                const bool plus_one = arithmetic && delta == 1 && (forest->known_bits[input] & BUSTER_TIER1_KNOWN_BELOW_MAX) != 0;
                if (!arithmetic || (input_signed >= 32 && !plus_one) || constant_signed >= 32)
                {
                    break;
                }
                addend += delta;
                tier1_select_fold(forest, node, value, &affine);
                tier1_select_fold(forest, node, constant, &affine);
                value = input;
            }
            if (value != a && function->type[value] == IR_I32 && (forest->canonical[value] & BUSTER_TIER1_STORAGE_SIGN) != 0 && addend == (int64_t)(int32_t)addend)
            {
                affine.rule = addend == 0 ? TIER1_RULE_COPY : TIER1_RULE_LEA_ADD;
                affine.width = 8;
                affine.source_width = 0;
                affine.values[0] = value;
                affine.values[1] = BUSTER_IR_NONE;
                affine.displacement = (int32_t)addend;
                affine.intrinsic_cost = 1;
                tier1_select_add_candidate(forest, node, &candidates, affine);
            }
            // Two bounded signed fields add without I32 overflow. Their actual sign-preserving storage lets one
            // 64-bit LEA compute the sum and the walked constants, without materializing a narrowed intermediate.
            if (function->op[value] == IR_ADD && tier1_select_tree_child(forest, node, value) && addend == (int64_t)(int32_t)addend)
            {
                const uint32_t left = function->a[value];
                const uint32_t right = function->b[value];
                if ((forest->known_bits[left] >> 8) < 32 && (forest->known_bits[right] >> 8) < 32 &&
                    (forest->canonical[left] & forest->canonical[right] & BUSTER_TIER1_STORAGE_SIGN) != 0)
                {
                    affine.rule = TIER1_RULE_LEA_ADD;
                    affine.width = 8;
                    affine.source_width = 0;
                    affine.values[0] = left;
                    affine.values[1] = right;
                    affine.scale = 0;
                    affine.displacement = (int32_t)addend;
                    affine.intrinsic_cost = 1;
                    tier1_select_fold(forest, node, value, &affine);
                    tier1_select_add_candidate(forest, node, &candidates, affine);
                }
            }
        }
        // Sign extension commutes with bitwise operations, NOT and arithmetic right shift. Negation also fits when
        // its operand needs fewer than 32 signed bits. Each source must already have the full signed representation;
        // no assumption about an incoming register's upper half or a wrapping I32 addition enters these patterns.
        if (op == IR_SEXT && type == IR_I64 && function->type[a] == IR_I32 && base.rule == TIER1_RULE_SIGN_EXTEND &&
            tier1_select_tree_child(forest, node, a) && forest->canonical != 0 && forest->known_bits != 0)
        {
            const uint32_t operation = function->op[a];
            const uint32_t left = function->a[a];
            const uint32_t right = function->b[a];
            const bool sign_left = left < function->instruction_count && function->type[left] == IR_I32 && (forest->canonical[left] & BUSTER_TIER1_STORAGE_SIGN) != 0;
            Tier1SelectPattern folded = base;
            folded.width = 8;
            folded.source_width = 0;
            folded.values[0] = left;
            folded.values[1] = BUSTER_IR_NONE;
            folded.operation = (uint8_t)tier1_select_operation(operation);
            folded.intrinsic_cost = 2;
            uint32_t constant = BUSTER_IR_NONE;
            bool supported = false;
            if (sign_left && (operation == IR_NOT || (operation == IR_NEG && (forest->known_bits[left] >> 8) < 32)))
            {
                folded.rule = TIER1_RULE_UNARY;
                supported = true;
            }
            else if (sign_left && operation == IR_ASHR && right < function->instruction_count && function->op[right] == IR_CONST && function->constants[function->a[right]].low < 32)
            {
                folded.immediate = function->constants[function->a[right]].low;
                folded.rule = folded.immediate == 0 ? TIER1_RULE_COPY : TIER1_RULE_IMMEDIATE;
                constant = right;
                supported = true;
            }
            else if (operation == IR_MUL && tier1_select_signed_multiply(function, forest->known_bits, a))
            {
                constant = function->op[right] == IR_CONST ? right : function->op[left] == IR_CONST ? left : BUSTER_IR_NONE;
                const uint32_t other = constant == right ? left : right;
                if (constant != BUSTER_IR_NONE && (forest->canonical[other] & BUSTER_TIER1_STORAGE_SIGN) != 0)
                {
                    folded.rule = TIER1_RULE_IMMEDIATE;
                    folded.values[0] = other;
                    folded.immediate = (uint64_t)(int64_t)(int32_t)function->constants[function->a[constant]].low;
                    folded.intrinsic_cost = 1;
                    supported = true;
                }
                else if (sign_left && (forest->canonical[right] & BUSTER_TIER1_STORAGE_SIGN) != 0)
                {
                    folded.rule = TIER1_RULE_REGISTER;
                    folded.values[1] = right;
                    folded.intrinsic_cost = 3;
                    supported = true;
                }
            }
            else if (sign_left && operation == IR_SUB && right < function->instruction_count && (forest->canonical[right] & BUSTER_TIER1_STORAGE_SIGN) != 0 &&
                     (forest->known_bits[left] >> 8) < 32 && (forest->known_bits[right] >> 8) < 32)
            {
                folded.rule = TIER1_RULE_REGISTER;
                folded.values[1] = right;
                folded.intrinsic_cost = 3;
                supported = true;
            }
            else if (operation == IR_AND || operation == IR_OR || operation == IR_XOR)
            {
                const uint32_t other = function->op[right] == IR_CONST ? left : right;
                constant = function->op[right] == IR_CONST ? right : function->op[left] == IR_CONST ? left : BUSTER_IR_NONE;
                if (constant != BUSTER_IR_NONE && (forest->canonical[other] & BUSTER_TIER1_STORAGE_SIGN) != 0)
                {
                    folded.rule = TIER1_RULE_IMMEDIATE;
                    folded.values[0] = other;
                    folded.immediate = (uint64_t)(int64_t)(int32_t)function->constants[function->a[constant]].low;
                    supported = true;
                }
                else if (sign_left && (forest->canonical[right] & BUSTER_TIER1_STORAGE_SIGN) != 0)
                {
                    folded.rule = TIER1_RULE_REGISTER;
                    folded.values[1] = right;
                    folded.intrinsic_cost = 3;
                    supported = true;
                }
            }
            if (supported)
            {
                tier1_select_fold(forest, node, a, &folded);
                tier1_select_fold(forest, node, constant, &folded);
                tier1_select_add_candidate(forest, node, &candidates, folded);
            }
        }
        // Signed widening is transitive: sext64(sext32(i8)) needs only the final extension. Following single-use
        // extensions also exposes their original load, avoiding movsbl followed by movslq for a signed-byte index.
        uint32_t signed_source = a;
        Tier1SelectPattern widened = base;
        for (uint32_t depth = 0; op == IR_SEXT && (base.rule == TIER1_RULE_SIGN_EXTEND || sign_copy) && depth < BUSTER_TIER1_SELECT_WALK &&
                                 function->op[signed_source] == IR_SEXT && tier1_select_tree_child(forest, node, signed_source); depth += 1)
        {
            tier1_select_fold(forest, node, signed_source, &widened);
            widened.rule = TIER1_RULE_SIGN_EXTEND;
            signed_source = function->a[signed_source];
            widened.values[0] = signed_source;
            widened.source_width = (uint8_t)tier1_select_memory_width(function->type[signed_source]);
        }
        if (signed_source != a)
        {
            tier1_select_add_candidate(forest, node, &candidates, widened);
        }
        // Read and extend once. The existing load-fold proof preserves exact access width and ordering and excludes
        // volatile/atomic accesses, including when pure sign extensions separated the load from this consumer.
        if (op == IR_SEXT && (type == IR_I32 || type == IR_I64) && tier1_select_load_folds(forest, node, signed_source))
        {
            Tier1SelectPattern folded = widened;
            folded.rule = TIER1_RULE_LOAD_ADDRESS;
            folded.operation = X86_OPERATION_MOVSX;
            folded.width = (uint8_t)tier1_select_memory_width(function->type[signed_source]);
            folded.source_width = sign_storage ? 8 : (uint8_t)tier1_select_memory_width(type);
            folded.values[0] = function->a[signed_source];
            folded.values[1] = BUSTER_IR_NONE;
            folded.intrinsic_cost = 1;
            tier1_select_fold(forest, node, signed_source, &folded);
            tier1_select_memory(forest, node, function->a[signed_source], &folded, 0);
            tier1_select_add_candidate(forest, node, &candidates, folded);
        }
        // A truth value as an integer: the comparison it is, through C's truth values, right before the extension, its
        // canonical truth result already holds the extension's 32 (or 64) bits.
        Tier1SelectPattern folded = base;
        bool negated = false;
        const uint32_t tested = op == IR_ZEXT && function->type[a] == IR_I1 && a + 1 == node ? tier1_select_truth(forest, node, a, &folded, &negated) : BUSTER_IR_NONE;
        if (tested != BUSTER_IR_NONE && tier1_select_comparison(forest, node, tested, negated, &folded))
        {
            folded.rule = folded.compare_immediate ? TIER1_RULE_COMPARE_IMMEDIATE : TIER1_RULE_COMPARE;
            folded.source_width = 0;
            folded.intrinsic_cost = 3;
            tier1_select_add_comparison(forest, node, &candidates, folded);
        }
    }
    else if ((op == IR_NEG || op == IR_NOT) && integer)
    {
        base.rule = TIER1_RULE_UNARY;
        base.values[0] = a;
        base.intrinsic_cost = 2;
        tier1_select_add_candidate(forest, node, &candidates, base);
    }
    else if ((op == IR_ADD || op == IR_SUB || op == IR_MUL || op == IR_AND || op == IR_OR || op == IR_XOR) && integer)
    {
        base.rule = TIER1_RULE_REGISTER;
        base.values[0] = a;
        base.values[1] = b;
        base.intrinsic_cost = 3;
        tier1_select_add_candidate(forest, node, &candidates, base);
        if (op == IR_ADD)
        {
            Tier1SelectPattern lea = base;
            lea.rule = TIER1_RULE_LEA_ADD;
            lea.intrinsic_cost = 1;
            tier1_select_add_candidate(forest, node, &candidates, lea);
        }
        uint32_t constant = b;
        uint32_t other = a;
        uint64_t immediate = 0;
        bool have_constant = tier1_select_constant(function, constant, &immediate);
        if (!have_constant && tier1_select_commutative((X86Operation)base.operation))
        {
            constant = a;
            other = b;
            have_constant = tier1_select_constant(function, constant, &immediate);
        }
        if (have_constant && tier1_select_immediate_fits(width, immediate))
        {
            Tier1SelectPattern folded = base;
            folded.rule = TIER1_RULE_IMMEDIATE;
            folded.values[0] = other;
            folded.values[1] = BUSTER_IR_NONE;
            folded.immediate = immediate;
            // A three-operand imul is one record, but pricing it below a copy plus shift chose imul even for
            // powers of two (#1217). Use a latency penalty, as division already does, so the
            // shift and 3/5/9 lea candidates win. Keep the BURS grammar's two immediate-multiply costs equal.
            folded.intrinsic_cost = op == IR_MUL ? 3 : 2;
            tier1_select_fold(forest, node, constant, &folded);
            tier1_select_add_candidate(forest, node, &candidates, folded);
            if (op == IR_ADD)
            {
                folded.rule = TIER1_RULE_LEA_ADD;
                folded.displacement = (int32_t)immediate;
                folded.intrinsic_cost = 1;
                tier1_select_add_candidate(forest, node, &candidates, folded);
            }
        }
        if (op == IR_MUL && have_constant)
        {
            Tier1SelectPattern folded = base;
            folded.values[0] = other;
            folded.values[1] = BUSTER_IR_NONE;
            tier1_select_fold(forest, node, constant, &folded);
            const uint32_t shift = tier1_select_log2(immediate);
            if (shift < width * 8)
            {
                folded.rule = TIER1_RULE_MULTIPLY_SHIFT;
                folded.scale = (uint8_t)shift;
                folded.intrinsic_cost = 2;
                tier1_select_add_candidate(forest, node, &candidates, folded);
            }
            if (immediate == 3 || immediate == 5 || immediate == 9)
            {
                folded.rule = TIER1_RULE_MULTIPLY_LEA;
                folded.values[1] = other;
                folded.scale = (uint8_t)tier1_select_log2(immediate - 1);
                folded.intrinsic_cost = 1;
                tier1_select_add_candidate(forest, node, &candidates, folded);
            }
        }
        if (op == IR_ADD)
        {
            const uint32_t shift_node = function->op[b] == IR_SHL ? b : a;
            const uint32_t other_node = shift_node == b ? a : b;
            uint64_t shift = 0;
            if (function->op[shift_node] == IR_SHL && tier1_select_tree_child(forest, node, shift_node) && tier1_select_constant(function, function->b[shift_node], &shift) && shift <= 3)
            {
                Tier1SelectPattern folded = base;
                folded.rule = TIER1_RULE_LEA_SHIFT;
                folded.values[0] = other_node;
                folded.values[1] = function->a[shift_node];
                folded.scale = (uint8_t)shift;
                folded.intrinsic_cost = 1;
                tier1_select_fold(forest, node, shift_node, &folded);
                tier1_select_fold(forest, node, function->b[shift_node], &folded);
                tier1_select_add_candidate(forest, node, &candidates, folded);
            }
        }
        // The load the memory operand takes: the right operand's, or for a commutative operation the left's where the
        // right isn't a load. Trying the left after a right load that can't fold cost the scalar twin's plan 2.5% on its
        // shapes, its inlined code's registers and frame laid out otherwise, for a case code generators rarely make.
        const uint32_t load = function->op[b] == IR_LOAD ? b : tier1_select_commutative((X86Operation)base.operation) ? a : BUSTER_IR_NONE;
        if (tier1_select_load_folds(forest, node, load))
        {
            Tier1SelectPattern folded = base;
            folded.rule = TIER1_RULE_LOAD_OPERAND;
            folded.values[0] = load == b ? a : b;
            folded.values[1] = BUSTER_IR_NONE;
            folded.values[2] = BUSTER_IR_NONE;
            tier1_select_fold(forest, node, load, &folded);
            tier1_select_memory(forest, node, function->a[load], &folded, 1);
            folded.intrinsic_cost = 3;
            tier1_select_add_candidate(forest, node, &candidates, folded);
        }
    }
    else if ((op == IR_SHL || op == IR_LSHR || op == IR_ASHR) && integer)
    {
        uint64_t immediate = 0;
        if (tier1_select_constant(function, b, &immediate) && immediate < width * 8)
        {
            base.rule = TIER1_RULE_IMMEDIATE;
            base.values[0] = a;
            base.immediate = immediate;
            base.intrinsic_cost = 2;
            tier1_select_fold(forest, node, b, &base);
            tier1_select_add_candidate(forest, node, &candidates, base);
        }
        else
        {
            base.rule = TIER1_RULE_SHIFT;
            base.values[0] = a;
            base.values[1] = b;
            base.intrinsic_cost = 4;
            tier1_select_add_candidate(forest, node, &candidates, base);
        }
    }
    else if ((op == IR_SDIV || op == IR_UDIV || op == IR_SREM || op == IR_UREM) && integer)
    {
        base.rule = TIER1_RULE_DIVIDE;
        base.values[0] = a;
        base.values[1] = b;
        base.immediate = (op == IR_SDIV || op == IR_SREM ? 1u : 0u) | (op == IR_SREM || op == IR_UREM ? 2u : 0u);
        base.intrinsic_cost = 8;
        tier1_select_add_candidate(forest, node, &candidates, base);
        // By a power of two 2^k: unsigned, a shift right by k or a mask of k bits (a copy and zero for 2^0), and signed,
        // TIER1_RULE_DIVIDE_POWER's sequence, for a positive divisor (k at most the width's bits less 2).
        const uint32_t bits = width * 8;
        uint64_t divisor = 0;
        const bool constant = tier1_select_constant(function, b, &divisor);
        divisor &= bits < 64 ? (UINT64_C(1) << bits) - 1 : UINT64_MAX;
        const uint32_t shift = constant ? tier1_select_log2(divisor) : UINT32_MAX;
        const bool is_signed = op == IR_SDIV || op == IR_SREM;
        const bool remainder = op == IR_SREM || op == IR_UREM;
        Tier1SelectPattern folded = base;
        folded.values[1] = BUSTER_IR_NONE;
        tier1_select_fold(forest, node, b, &folded);
        const bool unsigned_power = shift < bits && (!is_signed || shift == 0);
        const bool signed_power = is_signed && shift >= 1 && shift <= bits - 2;
        const bool mask = (unsigned_power || signed_power) && tier1_select_immediate_fits(width, divisor - 1);
        if (unsigned_power && !remainder && shift == 0)
        {
            folded.rule = TIER1_RULE_COPY;
            folded.intrinsic_cost = 1;
            tier1_select_add_candidate(forest, node, &candidates, folded);
        }
        else if (unsigned_power && !remainder)
        {
            folded.rule = TIER1_RULE_IMMEDIATE;
            folded.operation = X86_OPERATION_SHR;
            folded.immediate = shift;
            folded.intrinsic_cost = 2;
            tier1_select_add_candidate(forest, node, &candidates, folded);
        }
        else if (unsigned_power && mask)
        {
            folded.rule = TIER1_RULE_IMMEDIATE;
            folded.operation = X86_OPERATION_AND;
            folded.immediate = divisor - 1;
            folded.intrinsic_cost = 2;
            tier1_select_add_candidate(forest, node, &candidates, folded);
        }
        else if (signed_power && (!remainder || mask))
        {
            folded.rule = TIER1_RULE_DIVIDE_POWER;
            folded.scale = (uint8_t)shift;
            folded.intrinsic_cost = 5;
            tier1_select_add_candidate(forest, node, &candidates, folded);
        }
        // By any other constant but those div and idiv keep (tier1_select_reciprocal_divisor): its reciprocal. Its cost is
        // below TIER1_RULE_DIVIDE's though its records may outnumber div's: div's latency, which a count of records
        // doesn't see, is the larger (docs/tier1-selection.md has the 9700X's numbers).
        else if (constant && tier1_select_reciprocal_divisor(divisor, width, is_signed))
        {
            folded.rule = TIER1_RULE_DIVIDE_RECIPROCAL;
            folded.immediate = divisor;
            folded.scale = (uint8_t)base.immediate;
            folded.intrinsic_cost = 7;
            tier1_select_add_candidate(forest, node, &candidates, folded);
        }
    }
    else if (op == IR_ADDRESS)
    {
        // The address's lea, its constant index in the displacement, through what its base's walk takes.
        const IrAddress operand = function->addresses[c];
        uint64_t constant = 0;
        const bool constant_index = b != BUSTER_IR_NONE && tier1_select_constant(function, b, &constant);
        int64_t displacement = 0;
        if (!__builtin_mul_overflow((int64_t)constant, (int64_t)operand.scale, &displacement) && !__builtin_add_overflow(displacement, operand.offset, &displacement) &&
            displacement == (int64_t)(int32_t)displacement)
        {
            Tier1SelectAddress start = {
                .displacement = displacement,
                .base = a,
                .index = constant_index ? BUSTER_IR_NONE : b,
                .object = BUSTER_IR_NONE,
                .scale = constant_index ? 0 : (uint8_t)tier1_select_log2(operand.scale),
            };
            start.index = start.index != BUSTER_IR_NONE ? tier1_select_index(forest, node, b, &start.scale, &start.displacement, &start.folds) : start.index;
            const Tier1SelectAddress address = tier1_select_address(forest, node, start, false);
            base.rule = TIER1_RULE_ADDRESS;
            base.values[0] = address.base;
            base.values[1] = address.index;
            base.stack_object = address.object;
            base.displacement = (int32_t)address.displacement;
            base.scale = address.scale;
            base.fold_count = address.folds;
            base.intrinsic_cost = 1;
            if (constant_index)
            {
                tier1_select_fold(forest, node, b, &base);
            }
            tier1_select_add_candidate(forest, node, &candidates, base);
        }
        else if (constant_index && operand.offset == (int64_t)(int32_t)operand.offset)
        {
            // A constant index whose scaled value doesn't fit the displacement (#141: `p[1ull << 30]`, a struct array's
            // element past 2 GB): the constant in a register, its own node's mov, as an index the lea scales, and the
            // offset the displacement. No walk: nothing folds into it, nor it into a user, whose walk stops at it (the
            // displacement doesn't fit there either), and the bottom-up path's class for it is the REG leaf's
            // (tier1_burs_displacement), whose pattern is maximal munch's, this one.
            base.rule = TIER1_RULE_ADDRESS;
            base.values[0] = a;
            base.values[1] = b;
            base.stack_object = BUSTER_IR_NONE;
            base.displacement = (int32_t)operand.offset;
            base.scale = (uint8_t)tier1_select_log2(operand.scale);
            base.fold_count = 0;
            base.intrinsic_cost = 1;
            tier1_select_add_candidate(forest, node, &candidates, base);
        }
    }
    else if ((op == IR_LOAD && tier1_select_memory_width(type) != 0) || (op == IR_STORE && tier1_select_memory_width(function->type[b]) != 0))
    {
        const bool store = op == IR_STORE;
        base.rule = store ? TIER1_RULE_STORE : TIER1_RULE_LOAD;
        base.values[0] = a;
        base.values[2] = store ? b : BUSTER_IR_NONE;
        base.width = (uint8_t)tier1_select_memory_width(store ? function->type[b] : type);
        if (!store && sign_storage)
        {
            base.operation = X86_OPERATION_MOVSX;
            base.source_width = 8;
        }
        base.intrinsic_cost = 1;
        // A store already discards the upper bits. Materializing a single-use truncation first would clear bits the
        // exact-width memory instruction never reads; keep the access (including volatile/atomic flags) at this node.
        if (store && function->op[b] == IR_TRUNC && tier1_select_tree_child(forest, node, b))
        {
            base.values[2] = function->a[b];
            tier1_select_fold(forest, node, b, &base);
        }
        if (store && (function->flags[node] & (BUSTER_IR_ATOMIC | BUSTER_IR_ORDER)) == (BUSTER_IR_ATOMIC | (BUSTER_IR_FENCE_SEQ_CST << BUSTER_IR_ORDER_SHIFT)))
        {
            base.operation = X86_OPERATION_XCHG;
            base.intrinsic_cost = 2;
        }
        Tier1SelectPattern folded = base;
        const bool walked = tier1_select_memory(forest, node, a, &folded, 0);
        // Through a frame object the folded address stands alone, as above.
        if (!walked || folded.stack_object == BUSTER_IR_NONE)
        {
            tier1_select_add_candidate(forest, node, &candidates, base);
        }
        if (walked)
        {
            folded.rule = store ? TIER1_RULE_STORE_ADDRESS : TIER1_RULE_LOAD_ADDRESS;
            tier1_select_add_candidate(forest, node, &candidates, folded);
        }
    }
    else if (op == IR_ICMP && (function->type[a] == IR_I32 || function->type[a] == IR_I64 || function->type[a] == IR_PTR))
    {
        base.rule = TIER1_RULE_COMPARE;
        base.values[0] = a;
        base.values[1] = b;
        base.width = (uint8_t)tier1_select_width(function->type[a]);
        base.condition = (uint8_t)c;
        base.intrinsic_cost = 3;
        tier1_select_add_comparison(forest, node, &candidates, base);
        uint64_t immediate = 0;
        if (tier1_select_constant(function, b, &immediate) && tier1_select_immediate_fits(base.width, immediate))
        {
            base.rule = TIER1_RULE_COMPARE_IMMEDIATE;
            base.values[1] = BUSTER_IR_NONE;
            base.immediate = immediate;
            base.compare_immediate = true;
            tier1_select_fold(forest, node, b, &base);
            tier1_select_add_comparison(forest, node, &candidates, base);
        }
    }
    else if (op == IR_SELECT && width != 0)
    {
        base.rule = TIER1_RULE_SELECT;
        base.values[0] = a;
        base.values[1] = b;
        base.values[2] = c;
        base.intrinsic_cost = 3;
        tier1_select_add_candidate(forest, node, &candidates, base);
    }
    else if (op == IR_BR)
    {
        base.rule = TIER1_RULE_JUMP;
        base.intrinsic_cost = 1;
        tier1_select_add_candidate(forest, node, &candidates, base);
    }
    else if (op == IR_BR_INDIRECT)
    {
        base.rule = TIER1_RULE_JUMP_INDIRECT;
        base.values[0] = a;
        base.width = 8;
        base.intrinsic_cost = 1;
        tier1_select_add_candidate(forest, node, &candidates, base);
    }
    else if (op == IR_ALLOCA && function->type[a] == IR_I64)
    {
        // The dynamic stack (#141). A constant size whose rounding to any frame's alignment fits a sub's imm32 folds.
        base.rule = TIER1_RULE_ALLOCA;
        base.values[0] = a;
        base.width = 8;
        base.scale = 4;
        base.intrinsic_cost = 4;
        tier1_select_add_candidate(forest, node, &candidates, base);
        uint64_t size = 0;
        if (tier1_select_constant(function, a, &size) && size <= BUSTER_TIER1_SELECT_MAX_ALLOCA_IMMEDIATE)
        {
            base.rule = TIER1_RULE_ALLOCA_IMMEDIATE;
            base.values[0] = BUSTER_IR_NONE;
            base.immediate = size;
            base.intrinsic_cost = 2;
            tier1_select_fold(forest, node, a, &base);
            tier1_select_add_candidate(forest, node, &candidates, base);
        }
    }
    else if (op == IR_STACK_SAVE)
    {
        base.rule = function->flags[node] == BUSTER_IR_STACK_SAVE_FRAME ? TIER1_RULE_FRAME_ADDRESS : TIER1_RULE_STACK_SAVE;
        base.width = 8;
        base.intrinsic_cost = 1;
        tier1_select_add_candidate(forest, node, &candidates, base);
    }
    else if (op == IR_STACK_RESTORE && function->type[a] == IR_I64)
    {
        base.rule = TIER1_RULE_STACK_RESTORE;
        base.values[0] = a;
        base.width = 8;
        base.intrinsic_cost = 1;
        tier1_select_add_candidate(forest, node, &candidates, base);
    }
    else if (op == IR_CBR)
    {
        base.rule = TIER1_RULE_BRANCH;
        base.values[0] = a;
        base.intrinsic_cost = 3;
        tier1_select_add_candidate(forest, node, &candidates, base);
        // The comparison the condition tests, through C's truth values, right before the branch: its jcc.
        Tier1SelectPattern folded = base;
        bool negated = false;
        const uint32_t tested = a + 1 == node ? tier1_select_truth(forest, node, a, &folded, &negated) : BUSTER_IR_NONE;
        if (tested != BUSTER_IR_NONE && tier1_select_comparison(forest, node, tested, negated, &folded))
        {
            folded.rule = TIER1_RULE_BRANCH_COMPARE;
            tier1_select_add_comparison(forest, node, &candidates, folded);
        }
        else if (tested != BUSTER_IR_NONE && tier1_select_float_comparison(forest, node, tested, negated, &folded))
        {
            folded.rule = TIER1_RULE_FLOAT_BRANCH_COMPARE;
            tier1_select_add_candidate(forest, node, &candidates, folded);
        }
    }
    else if (op == IR_CALL && function->calls[b].body != 0 && tier1_select_asm_supported(function, node))
    {
        // An asm body (#1839): its records in the node's place, its operand block the frame object its argument addresses.
        base.rule = TIER1_RULE_ASM;
        base.immediate = function->calls[b].body - 1;
        base.intrinsic_cost = (uint8_t)(function->asm_bodies[function->calls[b].body - 1].count < 254 ? 1 + function->asm_bodies[function->calls[b].body - 1].count : 255);
        tier1_select_add_candidate(forest, node, &candidates, base);
    }
    else if (op == IR_CALL && function->calls[b].row != 0 && tier1_select_row_supported(function, node))
    {
        // A machine row (#141): its records, its operands' moves and its result's.
        base.rule = TIER1_RULE_ROW;
        base.immediate = function->calls[b].row - 1;
        base.intrinsic_cost = (uint8_t)(1 + function->rows[function->calls[b].row - 1].record_count);
        tier1_select_add_candidate(forest, node, &candidates, base);
    }
    else if ((op == IR_CALL || op == IR_CALL_INDIRECT) && tier1_select_call_supported(function, node))
    {
        base.rule = TIER1_RULE_CALL;
        base.immediate = a;
        // A floating result's width too (#141), which the consumer moves from xmm0.
        base.width = (uint8_t)(width | float_width);
        // Structural lowering estimate: call plus possible argument/result copies; preservation is emitter-owned.
        base.intrinsic_cost = (uint8_t)(1 + function->calls[b].argument_count + (type != IR_VOID));
        tier1_select_add_candidate(forest, node, &candidates, base);
    }
    // A narrow result leaves in its canonical zero-extended 32 bits: a caller extends it by its own type, as clang's and
    // Tier 0's do, the psABI leaving the bits past its width unspecified. An aggregate result's address (#789) moves
    // nothing here: the CFG consumer loads its parts, or copies its bytes through the hidden pointer, before the epilogue.
    else if (op == IR_RET && (a == BUSTER_IR_NONE || function->type[a] == IR_I1 || function->type[a] == IR_I8 || function->type[a] == IR_I16 || function->type[a] == IR_I32 ||
                              function->type[a] == IR_I64 || function->type[a] == IR_PTR || function->type[a] == IR_F80))
    {
        // A long double's (#141): its object's value into st0, which the CFG consumer loads before the epilogue; no value
        // of its own.
        base.rule = TIER1_RULE_RETURN;
        base.values[0] = a != BUSTER_IR_NONE && function->type[a] == IR_F80 ? BUSTER_IR_NONE : a;
        base.width = a == BUSTER_IR_NONE || function->result_size != 0 ? 0 : (uint8_t)tier1_select_width(function->type[a]);
        base.intrinsic_cost = a == BUSTER_IR_NONE ? 1 : 2;
        tier1_select_add_candidate(forest, node, &candidates, base);
    }
    else if (op == IR_UNREACHABLE)
    {
        base.rule = TIER1_RULE_UNREACHABLE;
        base.intrinsic_cost = 1;
        tier1_select_add_candidate(forest, node, &candidates, base);
    }
    else if (op == IR_PHI && (width != 0 || type == IR_F80))
    {
        base.rule = TIER1_RULE_PHI;
        tier1_select_add_candidate(forest, node, &candidates, base);
    }
    else if (op == IR_STACK_ADDR && (tier1_select_stack_object(function, a) || tier1_select_overflow_area(function, a)))
    {
        base.rule = TIER1_RULE_STACK_ADDRESS;
        base.immediate = a;
        base.intrinsic_cost = 1;
        tier1_select_add_candidate(forest, node, &candidates, base);
    }
    else if (op == IR_SYMBOL_ADDR)
    {
        base.rule = TIER1_RULE_SYMBOL_ADDRESS;
        base.immediate = a;
        base.intrinsic_cost = 1;
        tier1_select_add_candidate(forest, node, &candidates, base);
    }
    else if (forest->vectors && op == IR_INTRINSIC && (c == BUSTER_IR_INTRINSIC_LANE_EXTRACT || c == BUSTER_IR_INTRINSIC_MOVEMASK) && a < function->instruction_count)
    {
        // A vector's lane, or its lanes' sign bits, into a general register (#1840): TIER1_RULE_VECTOR's of the vector's
        // width, its destination the general one.
        base.rule = TIER1_RULE_VECTOR;
        base.width = (uint8_t)tier1_select_vector_width(function->type[a]);
        base.scale = c == BUSTER_IR_INTRINSIC_MOVEMASK ? BUSTER_TIER1_VECTOR_MOVEMASK : BUSTER_TIER1_VECTOR_LANE_EXTRACT;
        base.immediate = function->flags[node];
        base.values[0] = a;
        base.intrinsic_cost = 1;
        if (base.width != 0)
        {
            tier1_select_add_candidate(forest, node, &candidates, base);
        }
    }
    else if (op == IR_INTRINSIC && (c == BUSTER_IR_INTRINSIC_MULHU ? type == IR_I64 : c <= BUSTER_IR_INTRINSIC_SMULO && integer))
    {
        // One test in the chain for both rules, as mulhu's alone was: its mul, or the others' VEX instruction (or a signed
        // overflow's sequence, tier1_select_overflow).
        const bool high = c == BUSTER_IR_INTRINSIC_MULHU;
        base.rule = high ? TIER1_RULE_MULTIPLY_HIGH : TIER1_RULE_INTRINSIC;
        base.values[0] = a;
        base.values[1] = b;
        base.immediate = high ? 0 : c;
        base.intrinsic_cost = high ? 4 : c >= BUSTER_IR_INTRINSIC_SADDO ? 3 : 1;
        tier1_select_add_candidate(forest, node, &candidates, base);
    }
    else if (op == IR_LIFETIME)
    {
        base.rule = TIER1_RULE_LIFETIME;
        base.immediate = a;
        tier1_select_add_candidate(forest, node, &candidates, base);
    }
    else if (op == IR_FENCE)
    {
        base.rule = TIER1_RULE_FENCE;
        base.immediate = a;
        base.intrinsic_cost = a == BUSTER_IR_FENCE_SEQ_CST ? 1 : 0;
        tier1_select_add_candidate(forest, node, &candidates, base);
    }
    else if (op == IR_ATOMIC_RMW && tier1_select_memory_width(type) != 0)
    {
        // The object's bytes, as a load's, never the narrow value's 32-bit register: a byte's or a short's xchg or lock
        // xadd of 4 bytes would write past its object.
        base.rule = TIER1_RULE_ATOMIC_RMW;
        base.width = (uint8_t)tier1_select_memory_width(type);
        base.values[0] = a;
        base.values[1] = b;
        base.immediate = function->flags[node] & BUSTER_IR_RMW_OPERATION;
        base.values[2] = base.immediate == BUSTER_IR_RMW_COMPARE ? c : BUSTER_IR_NONE;
        base.intrinsic_cost = 3;
        tier1_select_add_candidate(forest, node, &candidates, base);
    }
    return candidates;
}

// Maximal munch: most covered nodes first. A switch fixes ties independently of minimum-cost selection. This is both
// paths' fallback outside the BURS grammar and the tests' independent reference cover. Both prefer a power-of-two
// multiply's shift to its three-operand immediate IMUL (#1217's latency penalty). A
// division by a register ranks below every rule for a constant divisor, which covers its constant even where the
// constant has other uses (and so doesn't count among the folds), as the costed cover always prefers them.
static inline __attribute__((always_inline)) uint32_t tier1_select_munch(const Tier1SelectCandidates* candidates)
{
    uint32_t chosen = 0;
    uint32_t priority = 0;
    for (uint32_t p = 0; p < candidates->count; p += 1)
    {
        const Tier1SelectPattern* pattern = &candidates->patterns[p];
        uint32_t rank = 0;
        switch (pattern->rule)
        {
        case TIER1_RULE_DIVIDE:
            rank = 0;
            break;
        case TIER1_RULE_MULTIPLY_SHIFT:
            rank = 9;
            break;
        case TIER1_RULE_LEA_SHIFT:
        case TIER1_RULE_BRANCH_COMPARE:
        case TIER1_RULE_LOAD_ADDRESS:
        case TIER1_RULE_STORE_ADDRESS:
        case TIER1_RULE_MASK_LOAD_ADDRESS:
        case TIER1_RULE_MASK_STORE_ADDRESS:
        case TIER1_RULE_FLOAT_BRANCH_COMPARE:
        case TIER1_RULE_FLOAT_LOAD_ADDRESS:
        case TIER1_RULE_FLOAT_STORE_ADDRESS:
            rank = 8;
            break;
        case TIER1_RULE_LOAD_OPERAND:
        case TIER1_RULE_FLOAT_LOAD_OPERAND:
            rank = 7;
            break;
        case TIER1_RULE_MULTIPLY_LEA:
            rank = 6;
            break;
        case TIER1_RULE_IMMEDIATE:
        case TIER1_RULE_COMPARE_IMMEDIATE:
            rank = 5;
            break;
        case TIER1_RULE_LEA_ADD:
            rank = 4;
            break;
        case TIER1_RULE_COPY:
            rank = 2;
            break;
        default:
            rank = 1;
            break;
        }
        rank += 16u * pattern->fold_count;
        if (rank > priority)
        {
            chosen = p;
            priority = rank;
        }
    }
    return chosen;
}

#include "buster/compiler/tier1_select_burs.c"

// The pattern of `node` on the bottom-up path (#141): the candidate its rule names (tier1_burs_pick; a node whose operator
// the grammar doesn't cover, munch's), its cost the candidate's, made once. Returns whether the node has a candidate.
// Not inlined at its three calls, so that the candidates' body it inlines is compiled once for the bottom-up path, and
// apart from the plan's own registers.
static __attribute__((noinline)) bool tier1_select_burs_pattern(const Tier1SelectForest* forest, const uint32_t* rules, uint8_t* built, Tier1SelectPlan* plan, uint32_t node)
{
    bool valid = true;
    if (built[node] == 0)
    {
        built[node] = 1;
        const Tier1SelectCandidates candidates = tier1_select_candidates(forest, node);
        bool munched = false;
        const uint32_t chosen = tier1_burs_pick(&candidates, rules[node], &munched);
        valid = chosen < candidates.count;
        if (valid)
        {
            plan->patterns[node] = candidates.patterns[chosen];
            plan->costs[node] = candidates.costs[chosen];
        }
    }
    return valid;
}

// The tests' independent maximal-munch cover: each node's candidates, a depth level at a time, 16 nodes a batch, and
// tier1_select_munch's choice. Neither production path uses this cover. Returns the first node in the levels' order
// without a candidate as the error's. A function of its own (#141): inside the plan, where the bottom-up path's columns are live
// across it, its inlined candidates spilled, 37 more instructions per IR instruction on the frozen sqlite3.c.
static __attribute__((noinline)) Tier1SelectError tier1_select_cover_munch(const Tier1SelectForest* forest, const uint32_t* level_offsets, const uint32_t* level_nodes, uint32_t maximum_depth,
                                                                           Tier1SelectPlan* plan, uint32_t* failed_node)
{
    Tier1SelectError error = TIER1_SELECT_OK;
    for (uint32_t level = 0; error == TIER1_SELECT_OK && level <= maximum_depth; level += 1)
    {
        for (uint32_t first = level_offsets[level]; error == TIER1_SELECT_OK && first < level_offsets[level + 1]; first += BUSTER_TIER1_SELECT_LANES)
        {
            const uint32_t remaining = level_offsets[level + 1] - first;
            const uint32_t lanes = remaining < BUSTER_TIER1_SELECT_LANES ? remaining : BUSTER_TIER1_SELECT_LANES;
            Tier1SelectCandidates batch[BUSTER_TIER1_SELECT_LANES];
            uint32_t chosen[BUSTER_TIER1_SELECT_LANES] = {0};
            for (uint32_t lane = 0; error == TIER1_SELECT_OK && lane < lanes; lane += 1)
            {
                const uint32_t node = level_nodes[first + lane];
                batch[lane] = tier1_select_candidates(forest, node);
                if (batch[lane].count == 0)
                {
                    error = TIER1_SELECT_ERROR_UNSUPPORTED;
                    *failed_node = node;
                }
                else
                {
                    chosen[lane] = tier1_select_munch(&batch[lane]);
                }
            }
            for (uint32_t lane = 0; error == TIER1_SELECT_OK && lane < lanes; lane += 1)
            {
                const uint32_t node = level_nodes[first + lane];
                plan->patterns[node] = batch[lane].patterns[chosen[lane]];
                plan->costs[node] = batch[lane].costs[chosen[lane]];
            }
        }
    }
    return error;
}

// Function/plan/scratch storage is disjoint. `dominance` is required for any cross-block value use: the structural IR
// verifier alone does not establish that such a definition reaches its use. No input-dependent recursion is used. With
// `verify` false, the caller has verified the IR with `dominance` (tier1_select_plan_verified).
// Marks `value` needed, and puts it on `pending` where it is new and the walk back has passed it (`passed`), to mark its
// own values from there (tier1_select_need_values). `pending` has room for every node once, and one more.
static uint32_t tier1_select_need(uint8_t* needed, uint32_t value, bool passed, uint32_t* pending, uint32_t pending_count)
{
    const bool fresh = needed[value] == 0;
    needed[value] = 1;
    pending[pending_count] = value;
    return pending_count + (fresh && passed);
}

// The arguments of a node's call that are no effective use: a body call's block (#2144, TIER1_RULE_ASM), its first where it
// has one, whose address no register holds; none of another call's.
static uint32_t tier1_select_asm_skipped(const IrFunction* function, const Tier1SelectPattern* pattern, const IrCall* call)
{
    const bool body = pattern->rule == TIER1_RULE_ASM && call->body != 0 && call->body <= function->asm_body_count;
    return body && call->argument_count > function->asm_bodies[call->body - 1].passed ? 1 : 0;
}

// Marks needed the values a needed `node`'s pattern reads: a call's arguments and an indirect call's callee, or its
// pattern's values. A value the walk back has passed (`walked`, or one past `node`, a later block's value that an
// earlier block reads, #789) goes on `pending`.
static uint32_t tier1_select_need_values(const IrFunction* function, const Tier1SelectPattern* pattern, uint32_t node, bool walked, uint8_t* needed, uint32_t* pending, uint32_t pending_count)
{
    // A row call's (#141) are its arguments too, and a body call's its register operands (#2144), past its block.
    if (pattern->rule == TIER1_RULE_CALL || pattern->rule == TIER1_RULE_ROW || pattern->rule == TIER1_RULE_ASM)
    {
        const IrCall call = function->calls[function->b[node]];
        for (uint32_t argument = tier1_select_asm_skipped(function, pattern, &call); argument < call.argument_count; argument += 1)
        {
            const uint32_t value = function->extra[call.argument_first + argument];
            pending_count = tier1_select_need(needed, value, walked || value > node, pending, pending_count);
        }
        if (function->op[node] == IR_CALL_INDIRECT)
        {
            pending_count = tier1_select_need(needed, function->a[node], walked || function->a[node] > node, pending, pending_count);
        }
    }
    for (uint32_t operand = 0; pattern->rule != TIER1_RULE_CALL && pattern->rule != TIER1_RULE_ROW && pattern->rule != TIER1_RULE_ASM && operand < 3; operand += 1)
    {
        const uint32_t value = pattern->values[operand];
        if (value != BUSTER_IR_NONE)
        {
            pending_count = tier1_select_need(needed, value, walked || value > node, pending, pending_count);
        }
    }
    // A long double operand (#141), which the CFG consumer's records read from its object, no pattern's value: the IR's a
    // and b of a long double node, and a return's.
    const uint32_t operands[2] = { function->a[node], function->b[node] };
    for (uint32_t operand = 0; (pattern->rule == TIER1_RULE_X87 || pattern->rule == TIER1_RULE_RETURN) && operand < 2; operand += 1)
    {
        const uint32_t value = operands[operand];
        const bool used = (ir_value_mask(function, node) >> operand & 1) != 0 && value < function->instruction_count && function->type[value] == IR_F80;
        if (used)
        {
            pending_count = tier1_select_need(needed, value, walked || value > node, pending, pending_count);
        }
    }
    return pending_count;
}

// `costed` is true for both public entry points. Tests may set it false for the independent maximal-munch reference;
// `vector` changes only kernel implementations, never the cover's cost model or tie-breaking.
static Tier1SelectError tier1_select_plan_checked(const IrFunction* function, const IrDominance* dominance, Arena* output, Arena* scratch, bool vector, bool verify, bool costed, Tier1SelectPlan* result, uint32_t* error_node)
{
    (void)vector;

    Tier1SelectError error = TIER1_SELECT_OK;
    uint32_t failed_node = BUSTER_IR_NONE;
    const bool arguments = function != 0 && output != 0 && scratch != 0 && output != scratch && result != 0;
    const uint64_t output_mark = arguments ? output->length : 0;
    const uint64_t scratch_mark = arguments ? scratch->length : 0;
    const uint32_t count = function != 0 ? function->instruction_count : 0;
    Tier1SelectPlan plan = { .function = function, .vector = BUSTER_VECTOR_PATH(vector) };
    if (!arguments)
    {
        error = TIER1_SELECT_ERROR_INVALID;
    }
    else if (count > BUSTER_TIER1_SELECT_MAX_INSTRUCTIONS)
    {
        error = TIER1_SELECT_ERROR_LIMIT;
    }
    else
    {
        const IrCheck check = !verify ? ir_check(IR_OK, BUSTER_IR_NONE, 0) : BUSTER_VECTOR_PATH(vector) ? ir_verify(function, scratch, dominance) : ir_verify_scalar(function, scratch, dominance);
        if (check.error != IR_OK)
        {
            error = check.error == IR_ERROR_MEMORY ? TIER1_SELECT_ERROR_MEMORY : TIER1_SELECT_ERROR_INVALID;
            failed_node = check.instruction;
        }
        else if (tier1_select_vector_signature(function))
        {
            error = TIER1_SELECT_ERROR_UNSUPPORTED;
        }
    }
    uint32_t* blocks = 0;
    uint32_t* uses = 0;
    uint32_t* barriers = 0;
    uint32_t* depths = 0;
    uint32_t* level_offsets = 0;
    uint32_t* level_nodes = 0;
    uint32_t* positions = 0;
    uint8_t* canonical = 0;
    uint16_t* known_bits = 0;
    uint32_t* pending = 0;
    // Each node's last user, its only one where it has a single use: the bottom-up path's classes read it.
    uint32_t* users = 0;
    if (error == TIER1_SELECT_OK)
    {
        bool ok = true;
        blocks = ir_allocate(scratch, count, 4, &ok);
        users = ir_allocate(scratch, count, 4, &ok);
        uses = ir_allocate(scratch, count, 4, &ok);
        barriers = ir_allocate(scratch, count, 4, &ok);
        depths = ir_allocate(scratch, count, 4, &ok);
        level_offsets = ir_allocate(scratch, (uint64_t)count + 2, 4, &ok);
        level_nodes = ir_allocate(scratch, count, 4, &ok);
        positions = ir_allocate(scratch, (uint64_t)count + 1, 4, &ok);
        canonical = ir_allocate(scratch, count, 1, &ok);
        known_bits = ir_allocate(scratch, count, sizeof(uint16_t), &ok);
        pending = ir_allocate(scratch, (uint64_t)count + 1, 4, &ok);
        plan.patterns = ir_allocate(output, count, sizeof(Tier1SelectPattern), &ok);
        plan.covered = ir_allocate(output, count, 1, &ok);
        plan.costs = ir_allocate(output, count, 4, &ok);
        plan.effective_use_offsets = ir_allocate(output, (uint64_t)count + 1, 4, &ok);
        if (!ok)
        {
            error = TIER1_SELECT_ERROR_MEMORY;
        }
    }
    uint32_t maximum_depth = 0;
    uint64_t effective_capacity = 0;
    // A function with its own ABI records (#131, #789) takes each parameter where its record says, a by-value aggregate's
    // address its copy's (tier1_select_aggregate_parameter), and returns its result as its record says: an aggregate's
    // bytes from the address its ret operand holds, as parts or through the hidden pointer (tier1_emit_cfg.c). Labels,
    // br_indirect and a variadic definition's incoming frame are selected too: the CFG consumer defines the labels'
    // symbols and fills the register save area.
    if (error == TIER1_SELECT_OK && function->parameter_passed != 0)
    {
        bool supported = function->parameter_count <= BUSTER_TIER1_SELECT_MAX_ARGUMENTS && tier1_select_result_supported(function->result_abi, function->result_size) &&
                         (function->result_size == 0 || function->result_type == IR_PTR) && function->result_size <= BUSTER_TIER1_SELECT_MAX_RESULT_COPY;
        // A parameter's record never r10, a static chain's (#148), which only a call's argument may be.
        for (uint32_t p = 0; supported && p < function->parameter_count; p += 1)
        {
            const IrPassed* record = tier1_select_parameter_record(function, p);
            supported = tier1_select_record_supported(record, function->parameter_types[p]) &&
                        !(record->location.kind == BUSTER_ABI_LOCATION_REGISTERS && record->location.registers[0] == BUSTER_ABI_R10);
        }
        error = supported ? error : TIER1_SELECT_ERROR_UNSUPPORTED;
    }
    for (uint32_t block = 0; error == TIER1_SELECT_OK && block < function->block_count; block += 1)
    {
        for (uint32_t node = function->block_offsets[block]; node < function->block_offsets[block + 1]; node += 1)
        {
            blocks[node] = block;
        }
    }
    for (uint32_t node = 0; error == TIER1_SELECT_OK && node < count; node += 1)
    {
        const uint32_t op = function->op[node];
        const bool row_call = op == IR_CALL && function->calls[function->b[node]].row != 0;
        const bool body_call = tier1_select_body_call(function, node);
        if (op == IR_SWITCH || ((op == IR_CALL || op == IR_CALL_INDIRECT) &&
                                !(row_call ? tier1_select_row_supported(function, node) : body_call ? tier1_select_asm_supported(function, node) : tier1_select_call_supported(function, node))))
        {
            error = TIER1_SELECT_ERROR_UNSUPPORTED;
        }
        // A parameter anywhere in the entry block: the straight-line consumer takes only a prefix of them, and gives the
        // others' functions to the CFG consumer, which defines every parameter at the function's start, before any
        // instruction (tier1_emit_cfg.c).
        if (op == IR_PARAM && blocks[node] != 0)
        {
            error = TIER1_SELECT_ERROR_UNSUPPORTED;
        }
        effective_capacity += (op == IR_CALL || op == IR_CALL_INDIRECT) && error == TIER1_SELECT_OK ? function->calls[function->b[node]].argument_count + 1 : 3;
        const uint32_t operand_count = error == TIER1_SELECT_OK ? (uint32_t)ir_operand_count(function, node) : 0;
        // A phi's operand reaches it along an edge: it may follow the phi (a loop's back edge), and its use counts, but
        // nothing folds into a phi, so its depth doesn't order the phi's choice. Its own loop leaves the others' as it was.
        for (uint32_t p = 0; error == TIER1_SELECT_OK && op == IR_PHI && p < operand_count; p += 1)
        {
            const IrOperand operand = ir_operand(function, node, p, blocks[node]);
            if (operand.value >= count || (blocks[operand.value] != blocks[node] && dominance == 0))
            {
                error = TIER1_SELECT_ERROR_INVALID;
            }
            else
            {
                uses[operand.value] += 1;
                users[operand.value] = node;
            }
        }
        for (uint32_t p = 0; error == TIER1_SELECT_OK && op != IR_PHI && p < operand_count; p += 1)
        {
            const IrOperand operand = ir_operand(function, node, p, blocks[node]);
            // A value of another block, whose definition the dominance shows reaches its use, may follow it in the
            // instructions' order: a computed goto's dispatch block's phis, which the lowering puts last, read in the
            // labels' blocks (#789). Only a tree child, before its consumer in its block, orders the node's choice.
            const bool across = operand.value < count && blocks[operand.value] != blocks[node];
            if (operand.value >= count || (across ? dominance == 0 : operand.value >= node))
            {
                error = TIER1_SELECT_ERROR_INVALID;
            }
            else
            {
                uses[operand.value] += 1;
                users[operand.value] = node;
                const uint32_t next = operand.value < node ? depths[operand.value] + 1 : 0;
                depths[node] = next > depths[node] ? next : depths[node];
            }
        }
        maximum_depth = depths[node] > maximum_depth ? depths[node] : maximum_depth;
        level_offsets[depths[node] + 1] += 1;
        if (error != TIER1_SELECT_OK)
        {
            failed_node = node;
        }
    }
    if (error == TIER1_SELECT_OK && BUSTER_VECTOR_PATH(vector))
    {
        tier1_select_barriers(function, count, barriers);
    }
    else if (error == TIER1_SELECT_OK)
    {
        tier1_select_barriers_scalar(function, count, barriers);
    }
    if (error == TIER1_SELECT_OK)
    {
        tier1_select_known_bits(function, dominance, blocks, positions, known_bits, vector);
        tier1_select_canonical(function, blocks, known_bits, canonical, positions, vector);
        tier1_select_scoped_ranges(function, dominance, blocks, known_bits, canonical, positions, pending, vector);
    }
    if (error == TIER1_SELECT_OK)
    {
        bool ok = effective_capacity <= UINT32_MAX;
        if (!ok)
        {
            error = TIER1_SELECT_ERROR_LIMIT;
        }
        else
        {
            plan.effective_use_values = ir_allocate(output, effective_capacity, 4, &ok);
            error = ok ? TIER1_SELECT_OK : TIER1_SELECT_ERROR_MEMORY;
        }
    }
    if (error == TIER1_SELECT_OK)
    {
        for (uint32_t level = 0; level <= maximum_depth; level += 1)
        {
            level_offsets[level + 1] += level_offsets[level];
            positions[level] = level_offsets[level];
        }
        for (uint32_t node = 0; node < count; node += 1)
        {
            level_nodes[positions[depths[node]]++] = node;
        }
    }
    const Tier1SelectForest forest =
    {
        .function = function,
        .blocks = blocks,
        .uses = uses,
        .costs = plan.costs,
        .barriers = barriers,
        .canonical = canonical,
        .known_bits = known_bits,
        .vectors = error == TIER1_SELECT_OK && ir_function_has_vectors_path(function, vector),
    };
    // Both host paths use the bottom-up rewrite's cover (tier1_select_burs.c): each node's class and visible children,
    // its state, and its rule. The independent scalar kernels visit nodes one at a time; the vector kernels label depth
    // levels in batches. Both read the same grammar's costs and tie-breaks. Nodes outside that grammar take munch's,
    // made in the levels' order on either path so that unsupported nodes are refused at the same place. Other patterns
    // are made as the code needs them, in the walk below. Only the tests' reference disables the costed cover.
    const bool burs = error == TIER1_SELECT_OK && costed;
    uint32_t* classes = 0;
    uint32_t* rules = 0;
    uint8_t* built = 0;
    uint32_t* kids = 0;
    uint32_t* states = 0;
#if BUSTER_HOST_VECTOR
    uint32_t* words = 0;
#endif
    if (burs)
    {
        bool ok = tier1_select_initialize();
        error = ok ? error : TIER1_SELECT_ERROR_LIMIT;
        classes = ir_allocate(scratch, count, 4, &ok);
        kids = ir_allocate(scratch, (uint64_t)count * 2, 4, &ok);
        states = ir_allocate(scratch, count, 4, &ok);
        rules = ir_allocate(scratch, count, 4, &ok);
        built = ir_allocate(scratch, count, 1, &ok);
#if BUSTER_HOST_VECTOR
        words = BUSTER_VECTOR_PATH(vector) ? ir_allocate(scratch, count, 4, &ok) : 0;
#endif
        error = ok || error != TIER1_SELECT_OK ? error : TIER1_SELECT_ERROR_MEMORY;
    }
    if (burs && error == TIER1_SELECT_OK)
    {
#if BUSTER_HOST_VECTOR
        if (vector)
        {
            tier1_burs_classify_vector(&forest, users, words, classes, kids);
            tier1_burs_label_vector(maximum_depth, level_offsets, level_nodes, count, classes, kids, states);
            tier1_burs_rules_vector(count, classes, states, rules);
        }
        else
#endif
        {
            tier1_burs_classify_scalar(&forest, users, classes, kids);
            tier1_burs_label_scalar(count, classes, kids, states);
            tier1_burs_rules_scalar(count, classes, states, rules);
        }
        const Tier1SelectPattern none = {
            .values = {BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE},
            .stack_object = BUSTER_IR_NONE,
        };
        for (uint32_t node = 0; node < count; node += 1)
        {
            plan.patterns[node] = none;
        }
    }
    for (uint32_t position = 0; burs && error == TIER1_SELECT_OK && position < count; position += 1)
    {
        const uint32_t node = level_nodes[position];
        if (classes[node] == TIER1_BURS_OP_REG && !tier1_select_burs_pattern(&forest, rules, built, &plan, node))
        {
            error = TIER1_SELECT_ERROR_UNSUPPORTED;
            failed_node = node;
        }
    }
    if (!burs && error == TIER1_SELECT_OK)
    {
        error = tier1_select_cover_munch(&forest, level_offsets, level_nodes, maximum_depth, &plan, &failed_node);
    }
    if (error == TIER1_SELECT_OK)
    {
        // What the code computes, from the last node back: a node with an effect (a store, a call, a terminator, a
        // volatile load), a parameter, a phi and its operands, which its edges' moves read, a node nothing uses, which the
        // optimizer kept for a fault it may raise (a load or a division), and each value a computed node's pattern reads.
        // Every other node is covered: its users' patterns compute it themselves, a constant as an immediate, a load as a
        // memory operand, an address as a displacement, a truth value's chain as a branch's comparison, however deep their
        // walks went. A node is final when the walk back reaches it if every node that reads it precedes it. One a later
        // node reads only after the walk passed it (a value of a later block that an earlier block reads, a computed goto's
        // dispatch block's, #789) goes on a worklist, which marks its values once the walk ends, and theirs (#1016's review).
        // A phi's undef operand isn't computed: its edge moves nothing for it (tier1_emit_cfg.c).
        uint8_t* needed = plan.covered;
        for (uint32_t node = 0; node < count; node += 1)
        {
            const uint32_t operand_count = function->op[node] == IR_PHI ? (uint32_t)ir_operand_count(function, node) : 0;
            needed[node] |= function->op[node] == IR_PHI ? 1 : 0;
            for (uint32_t p = 0; p < operand_count; p += 1)
            {
                const uint32_t value = ir_operand(function, node, p, blocks[node]).value;
                needed[value] |= function->op[value] != IR_UNDEF ? 1 : 0;
            }
        }
        // On the bottom-up path, a node's pattern is made as the walk finds it needed (tier1_select_burs_pattern).
        uint32_t pending_count = 0;
        for (uint32_t reverse = count; error == TIER1_SELECT_OK && reverse != 0; reverse -= 1)
        {
            const uint32_t node = reverse - 1;
            const uint32_t op = function->op[node];
            needed[node] |= tier1_select_barrier(function, node) || op == IR_BR || op == IR_CBR || op == IR_BR_INDIRECT || op == IR_RET || op == IR_UNREACHABLE || op == IR_PARAM || uses[node] == 0 ? 1 : 0;
            if (needed[node] != 0 && burs && !tier1_select_burs_pattern(&forest, rules, built, &plan, node))
            {
                error = TIER1_SELECT_ERROR_UNSUPPORTED;
                failed_node = node;
            }
            if (needed[node] != 0 && error == TIER1_SELECT_OK)
            {
                pending_count = tier1_select_need_values(function, &plan.patterns[node], node, false, needed, pending, pending_count);
            }
        }
        while (error == TIER1_SELECT_OK && pending_count != 0)
        {
            pending_count -= 1;
            const uint32_t node = pending[pending_count];
            if (burs && !tier1_select_burs_pattern(&forest, rules, built, &plan, node))
            {
                error = TIER1_SELECT_ERROR_UNSUPPORTED;
                failed_node = node;
            }
            pending_count = error == TIER1_SELECT_OK ? tier1_select_need_values(function, &plan.patterns[node], node, true, needed, pending, pending_count) : pending_count;
        }
        for (uint32_t node = 0; node < count; node += 1)
        {
            plan.covered[node] = needed[node] ^ 1;
        }
        for (uint32_t node = 0; error == TIER1_SELECT_OK && node < count; node += 1)
        {
            plan.effective_use_offsets[node] = plan.effective_use_count;
            if (plan.covered[node] == 0)
            {
                const Tier1SelectPattern* pattern = &plan.patterns[node];
                plan.estimated_cost += pattern->intrinsic_cost;
                if (pattern->rule == TIER1_RULE_CALL || pattern->rule == TIER1_RULE_ROW || pattern->rule == TIER1_RULE_ASM)
                {
                    const IrCall call = function->calls[function->b[node]];
                    for (uint32_t argument = tier1_select_asm_skipped(function, pattern, &call); argument < call.argument_count; argument += 1)
                    {
                        plan.effective_use_values[plan.effective_use_count++] = function->extra[call.argument_first + argument];
                    }
                    // An indirect call's callee after its arguments.
                    if (function->op[node] == IR_CALL_INDIRECT)
                    {
                        plan.effective_use_values[plan.effective_use_count++] = function->a[node];
                    }
                }
                else
                {
                    for (uint32_t operand = 0; operand < 3; operand += 1)
                    {
                        if (pattern->values[operand] != BUSTER_IR_NONE)
                        {
                            plan.effective_use_values[plan.effective_use_count++] = pattern->values[operand];
                        }
                    }
                }
            }
        }
        plan.effective_use_offsets[count] = plan.effective_use_count;
        plan.arena_bytes = output->length - output_mark;
        plan.function_verified = true;
        if (error == TIER1_SELECT_OK)
        {
            *result = plan;
        }
    }
    if (arguments)
    {
        arena_pop_to(scratch, scratch_mark);
        if (error != TIER1_SELECT_OK)
        {
            arena_pop_to(output, output_mark);
        }
    }
    if (error_node != 0)
    {
        *error_node = failed_node;
    }
    return error;
}

// The plan of a function that hasn't been verified: ir_verify (or ir_verify_scalar on the scalar path) with `dominance`
// first, which refuses what the plan can't read.
static Tier1SelectError tier1_select_plan(const IrFunction* function, const IrDominance* dominance, Arena* output, Arena* scratch, bool vector, Tier1SelectPlan* result, uint32_t* error_node)
{
    return tier1_select_plan_checked(function, dominance, output, scratch, vector, true, true, result, error_node);
}

// The plan of a function its caller has verified with `dominance`, as ir_optimize verifies its result: the same plan as
// tier1_select_plan's, without verifying it again. On the frozen sqlite3.c, the verification was a quarter of selection's
// cycles (#141). Unverified IR here may be read outside its columns.
static Tier1SelectError tier1_select_plan_verified(const IrFunction* function, const IrDominance* dominance, Arena* output, Arena* scratch, bool vector, Tier1SelectPlan* result, uint32_t* error_node)
{
    return tier1_select_plan_checked(function, dominance, output, scratch, vector, false, true, result, error_node);
}

static X86Instruction tier1_select_memory_holes(const Tier1SelectPattern* pattern, const uint8_t* registers, uint32_t first)
{
    const uint32_t index = pattern->values[first + 1];
    const X86Instruction result =
    {
        .displacement = pattern->displacement,
        // A frame object's base is the frame's base register, which the consumer sets at emission with the object's
        // offset (tier1_emit_cfg.c): until then the stack pointer's, as the object's lea's is.
        .rm = pattern->stack_object != BUSTER_IR_NONE ? X86_REGISTER_RSP : registers[pattern->values[first]],
        .index = index == BUSTER_IR_NONE ? 0 : registers[index],
        .mode = index == BUSTER_IR_NONE ? BUSTER_X86_MEMORY_BASE : (uint8_t)BUSTER_X86_MEMORY_BASE_INDEX(pattern->scale),
    };
    return result;
}

static Tier1SelectHoles tier1_select_holes(const Tier1SelectPlan* plan, const uint8_t* registers, uint32_t node)
{
    const Tier1SelectPattern* pattern = &plan->patterns[node];
    Tier1SelectHoles holes =
    {
        .rule = (Tier1SelectRule)pattern->rule,
        .operation = (X86Operation)pattern->operation,
        .immediate = pattern->immediate,
        .width = pattern->width,
        .source_width = pattern->source_width,
        .destination = plan->function->type[node] == IR_VOID ? 0 : registers[node],
        .left = pattern->values[0] == BUSTER_IR_NONE ? 0 : registers[pattern->values[0]],
        .right = pattern->values[1] == BUSTER_IR_NONE ? 0 : registers[pattern->values[1]],
        .third = pattern->values[2] == BUSTER_IR_NONE ? 0 : registers[pattern->values[2]],
        .condition = pattern->condition,
        .scale = pattern->scale,
        .compare_immediate = pattern->compare_immediate,
        .compare_memory = pattern->compare_memory,
        .sign_result = pattern->sign_result,
        .nops = pattern->nops,
    };
    const IrOp op = (IrOp)plan->function->op[node];
    if (__builtin_expect((plan->function->flags[node] & BUSTER_IR_SEGMENT) != 0, 0) && (op == IR_LOAD || op == IR_STORE))
    {
        holes.segment = (uint8_t)((plan->function->flags[node] & BUSTER_IR_SEGMENT) >> BUSTER_IR_SEGMENT_SHIFT);
    }
    switch (pattern->rule)
    {
    case TIER1_RULE_LEA_ADD:
    case TIER1_RULE_LEA_SHIFT:
    case TIER1_RULE_MULTIPLY_LEA:
    case TIER1_RULE_ADDRESS:
    case TIER1_RULE_LOAD:
    case TIER1_RULE_LOAD_ADDRESS:
    case TIER1_RULE_STORE:
    case TIER1_RULE_STORE_ADDRESS:
    case TIER1_RULE_MASK_LOAD:
    case TIER1_RULE_MASK_LOAD_ADDRESS:
    case TIER1_RULE_MASK_STORE:
    case TIER1_RULE_MASK_STORE_ADDRESS:
    case TIER1_RULE_FLOAT_LOAD:
    case TIER1_RULE_FLOAT_LOAD_ADDRESS:
    case TIER1_RULE_FLOAT_STORE:
    case TIER1_RULE_FLOAT_STORE_ADDRESS:
        holes.memory = tier1_select_memory_holes(pattern, registers, 0);
        if ((pattern->rule == TIER1_RULE_STORE || pattern->rule == TIER1_RULE_STORE_ADDRESS || pattern->rule == TIER1_RULE_MASK_STORE || pattern->rule == TIER1_RULE_MASK_STORE_ADDRESS ||
             pattern->rule == TIER1_RULE_FLOAT_STORE || pattern->rule == TIER1_RULE_FLOAT_STORE_ADDRESS) && pattern->condition == 0)
        {
            holes.right = holes.third;
        }
        if ((pattern->rule == TIER1_RULE_FLOAT_LOAD || pattern->rule == TIER1_RULE_FLOAT_STORE) && pattern->condition != 0)
        {
            // A masked load's or store's (#1840) address is its base alone: values[1] is its mask, not an index.
            Tier1SelectPattern unindexed = *pattern;
            unindexed.values[1] = BUSTER_IR_NONE;
            holes.memory = tier1_select_memory_holes(&unindexed, registers, 0);
        }
        break;
    case TIER1_RULE_LOAD_OPERAND:
    case TIER1_RULE_FLOAT_LOAD_OPERAND:
        holes.memory = tier1_select_memory_holes(pattern, registers, 1);
        break;
    default:
        break;
    }
    if (pattern->compare_memory)
    {
        holes.memory = tier1_select_memory_holes(pattern, registers, 1);
    }
    return holes;
}

// A leaf body's incoming System V integer arguments. Register assignment must pin these values from function entry;
// an IR_PARAM does not emit a late copy from a register that earlier instructions may already have overwritten.
// Past them, index 6: r10, a call's static chain (#148, tier1_select_record_place), which no parameter has.
static const uint8_t tier1_select_parameter_registers[7] =
{
    X86_REGISTER_RDI,
    X86_REGISTER_RSI,
    X86_REGISTER_RDX,
    X86_REGISTER_RCX,
    X86_REGISTER_R8,
    X86_REGISTER_R9,
    X86_REGISTER_R10,
};

// The plan and allocation are validated internal compiler products. All records receive the same independent target,
// operand and immediate checks; a failure is an error, never another optimization mode. This body does not spill,
// make calls, save callee-saved registers, or attach symbolic relocations.
static Tier1SelectError tier1_select_emit(const Tier1SelectPlan* plan, const uint8_t* registers, const uint8_t* allowed_forms, Arena* output, Tier1SelectCode* result, uint32_t* error_node)
{
    const bool arguments = plan != 0 && plan->function != 0 && allowed_forms != 0 && output != 0 && result != 0 && (plan->function->instruction_count == 0 || registers != 0);
    Tier1SelectError error = arguments ? TIER1_SELECT_OK : TIER1_SELECT_ERROR_INVALID;
    const uint64_t mark = arguments ? output->length : 0;
    const uint32_t count = arguments ? plan->function->instruction_count : 0;
    uint32_t failed_node = BUSTER_IR_NONE;
    Tier1SelectCode code = {0};
    if (arguments && count > BUSTER_TIER1_SELECT_MAX_INSTRUCTIONS)
    {
        error = TIER1_SELECT_ERROR_LIMIT;
    }
    // This legacy consumer has neither ABI argument moves nor call-clobber preservation or symbolic fixups, nor the
    // edge moves a phi needs, nor the saves around a fixed-register sequence's (tier1_select_fixed). Reject the complete
    // plan before allocating records, even if earlier value locations are spilled. A memory operand through a frame
    // object needs the frame's layout too, an indirect jump's labels their symbols, a variadic definition its register
    // save area, a floating value the vector class's registers (#141), a function's own ABI records its aggregates'
    // copies and parts (#789), and the dynamic stack its frame pointer and call areas (#141): the CFG consumer's.
    if (error == TIER1_SELECT_OK && (plan->function->label_count != 0 || plan->function->variadic || plan->function->parameter_passed != 0))
    {
        error = TIER1_SELECT_ERROR_UNSUPPORTED;
    }
    for (uint32_t node = 0; error == TIER1_SELECT_OK && node < count; node += 1)
    {
        if (plan->function->op[node] == IR_CALL || plan->patterns[node].rule == TIER1_RULE_CALL || plan->patterns[node].rule == TIER1_RULE_PHI ||
            plan->patterns[node].rule == TIER1_RULE_STACK_ADDRESS || plan->patterns[node].rule == TIER1_RULE_SYMBOL_ADDRESS || plan->patterns[node].rule == TIER1_RULE_LIFETIME ||
            tier1_select_fixed_registers(plan->patterns[node].rule) != 0 ||
            tier1_select_mask_rule(plan->patterns[node].rule) || tier1_select_float_rule(plan->patterns[node].rule) || plan->patterns[node].rule == TIER1_RULE_VECTOR ||
            plan->patterns[node].stack_object != BUSTER_IR_NONE ||
            plan->patterns[node].rule == TIER1_RULE_JUMP_INDIRECT || (plan->patterns[node].rule >= TIER1_RULE_ALLOCA && plan->patterns[node].rule <= TIER1_RULE_STACK_RESTORE) ||
            plan->patterns[node].rule == TIER1_RULE_FRAME_ADDRESS)
        {
            error = TIER1_SELECT_ERROR_UNSUPPORTED;
            failed_node = node;
        }
    }
    Tier1SelectRequests requests = { .vector = plan != 0 && plan->vector };
    if (error == TIER1_SELECT_OK)
    {
        bool ok = true;
        code.records = ir_allocate(output, (uint64_t)count * BUSTER_TIER1_SELECT_RECORDS_PER_NODE, sizeof(X86Instruction), &ok);
        code.cost_ids = ir_allocate(output, (uint64_t)count * BUSTER_TIER1_SELECT_RECORDS_PER_NODE, 1, &ok);
        code.node_first = ir_allocate(output, (uint64_t)count + 1, 4, &ok);
        code.branches = ir_allocate(output, (uint64_t)plan->function->block_count * 2, 4, &ok);
        code.targets = ir_allocate(output, (uint64_t)plan->function->block_count * 2, 4, &ok);
        if (!ok)
        {
            error = TIER1_SELECT_ERROR_MEMORY;
        }
    }
    // Each node's records go in as selected, their immediates' requests with them; a node whose registers allocation
    // didn't give stops the loop.
    uint32_t emitted = 0;
    for (uint32_t node = 0; error == TIER1_SELECT_OK && node < count; node += 1)
    {
        code.node_first[node] = code.count;
        if (plan->covered[node] == 0)
        {
            const IrFunction* function = plan->function;
            const Tier1SelectPattern* pattern = &plan->patterns[node];
            bool allocated = function->type[node] == IR_VOID || tier1_select_register_allowed(registers[node]);
            for (uint32_t p = 0; p < 3; p += 1)
            {
                const uint32_t value = pattern->values[p];
                allocated &= value == BUSTER_IR_NONE || tier1_select_register_allowed(registers[value]);
            }
            if (function->op[node] == IR_PARAM)
            {
                const Tier1SelectPlace place = tier1_select_parameter_place(function, function->a[node]);
                allocated &= place.kind == BUSTER_TIER1_PLACE_GENERAL && registers[node] == tier1_select_parameter_registers[place.index];
            }
            if (!allocated)
            {
                error = TIER1_SELECT_ERROR_ALLOCATION;
                failed_node = node;
            }
            else
            {
                const Tier1SelectHoles holes = tier1_select_holes(plan, registers, node);
                const Tier1SelectRecords selected = tier1_select_expand(&holes);
                memcpy(code.records + code.count, selected.records, selected.count * sizeof(X86Instruction));
                tier1_select_requests_add(&requests, &selected, code.count, code.records);
                code.count += selected.count;
                if (selected.overflowed)
                {
                    error = TIER1_SELECT_ERROR_INVALID;
                    failed_node = node;
                }
                else if (function->op[node] == IR_BR)
                {
                    code.branches[code.branch_count] = code.count - 1;
                    code.targets[code.branch_count++] = function->block_offsets[function->a[node]];
                }
                else if (function->op[node] == IR_CBR)
                {
                    code.branches[code.branch_count] = code.count - 2;
                    code.targets[code.branch_count++] = function->block_offsets[function->b[node]];
                    code.branches[code.branch_count] = code.count - 1;
                    code.targets[code.branch_count++] = function->block_offsets[function->c[node]];
                }
            }
        }
        emitted = error == TIER1_SELECT_OK ? node + 1 : node;
    }
    // The records' immediates' forms, chosen for the function in batches, then every record checked in order: an
    // invalid record or a form the target doesn't take is its node's failure, before a later node's allocation's, as
    // when each node's records were checked before the next node was selected.
    if (error == TIER1_SELECT_OK || error == TIER1_SELECT_ERROR_ALLOCATION)
    {
        tier1_select_requests_flush(&requests, code.records);
        code.node_first[emitted] = code.count;
        const uint32_t valid = (uint32_t)x86_validate(code.records, code.count);
        uint32_t r = 0;
        while (r < valid && x86_target_form_allowed(allowed_forms, code.records[r].form))
        {
            code.cost_ids[r] = x86_zen5_id(&code.records[r]);
            code.unknown_costs += code.cost_ids[r] == 0;
            r += 1;
        }
        if (r < code.count)
        {
            uint32_t node = 0;
            while (code.node_first[node + 1] <= r)
            {
                node += 1;
            }
            error = r < valid ? TIER1_SELECT_ERROR_TARGET : TIER1_SELECT_ERROR_INVALID;
            failed_node = node;
        }
    }
    if (error == TIER1_SELECT_OK)
    {
        code.node_first[count] = code.count;
        for (uint32_t branch = 0; branch < code.branch_count; branch += 1)
        {
            code.targets[branch] = code.node_first[code.targets[branch]];
        }
        code.error_node = BUSTER_IR_NONE;
        code.arena_bytes = output->length - mark;
        *result = code;
    }
    else if (arguments)
    {
        arena_pop_to(output, mark);
    }
    if (error_node != 0)
    {
        *error_node = failed_node;
    }
    return error;
}
