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

#ifndef BUSTER_IR_STORAGE_ONLY
#define BUSTER_IR_NONE UINT32_MAX
#define BUSTER_IR_VECTOR_COUNT 16
#define BUSTER_IR_VOLATILE 1
// An atomic load or store (#1059): one access of its integer's or pointer's bytes, of a C memory order (BUSTER_IR_ORDER's
// bits: 0 relaxed to 5 seq_cst, as a fence's), which no pass splits, widens, merges, eliminates, forwards or moves past
// another access, a call or a fence: a volatile access's guarantees and its order's. Its machine obligations are its
// type's ordinary access (x86-64's naturally aligned accesses of 1, 2, 4 or 8 bytes are atomic, and its total store order
// gives a load's and a release store's orders) and, for a seq_cst store, the fence of seq_cst after it, which the
// producer writes as its own IR_FENCE.
#define BUSTER_IR_ATOMIC 2
#define BUSTER_IR_ORDER_SHIFT 2
#define BUSTER_IR_ORDER (7u << BUSTER_IR_ORDER_SHIFT)
// A load's or a store's segment override (#1447): FS (clang's address space 257, __seg_fs), GS (256, __seg_gs) or SS (258,
// whose base 64-bit mode ignores but whose 36 clang emits), numbered as the typed stream's (BUSTER_TYPED_SEGMENT_FS, _GS,
// _SS) and the encoder's overrides are (form + segment * X86_FORM_COUNT).
// Its address is an offset from the segment's base, not a flat address: no pass relates it to another access's, so such
// an access is observable (BUSTER_IR_OBSERVABLE), kept as it is and where it is, and no load folds into its consumer.
// An atomic read/modify/write's operation has these bits; a function with one in a segment stays Tier 0's.
#define BUSTER_IR_SEGMENT_SHIFT 5
#define BUSTER_IR_SEGMENT (3u << BUSTER_IR_SEGMENT_SHIFT)
#define BUSTER_IR_SEGMENT_FS 1u
#define BUSTER_IR_SEGMENT_GS 2u
#define BUSTER_IR_SEGMENT_SS 3u
#define BUSTER_IR_ACCESS_FLAGS (BUSTER_IR_VOLATILE | BUSTER_IR_ATOMIC | BUSTER_IR_ORDER | BUSTER_IR_SEGMENT)
// The accesses every pass keeps as they are and where they are: a volatile one, an atomic one and one in a segment.
#define BUSTER_IR_OBSERVABLE (BUSTER_IR_VOLATILE | BUSTER_IR_ATOMIC | BUSTER_IR_SEGMENT)
// An IR_STACK_SAVE's flag (#1847): the function's frame address, __builtin_frame_address(0), rather than rsp: its frame
// pointer, rbp, the address of the caller's rbp its prologue pushed, as clang's and Tier 0's frames have it. The typed
// stream's FRAME_ADDRESS. The function keeps a frame pointer (Tier 1's frame planner, BUSTER_TIER1_FRAME_KEEP_POINTER).
#define BUSTER_IR_STACK_SAVE_FRAME 1
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
    IR_V4I32,
    IR_V8I32,
    IR_V16I32,
    IR_M8,
    IR_M16,
    IR_M32,
    IR_M64,
    IR_TYPE_COUNT,
} IrType;

_Static_assert(IR_TYPE_COUNT <= 32, "IR result admission and type shifts use 32 bits");

#include "ir_simd_model.c"

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
    IR_VCONST,
    IR_VCMP,
    IR_VSELECT,
    // A frame slot's lifetime (#177): `a` the slot, `b` 0 where its object's lifetime starts and 1 where it ends. It
    // reads and writes nothing: the slot's bytes are indeterminate after a start, and no access may come after an end
    // until the next start. A slot with no marker lives throughout its function; one with markers is live where some
    // path from a start reaches without an end, which Tier 1's frame planner colors (tier1_emit_cfg_lifetimes).
    IR_LIFETIME,
    // A memory fence (#1059): `a` its order, C's memory_order (BUSTER_IR_FENCE_ORDER: relaxed to seq_cst), and its scope
    // (BUSTER_IR_FENCE_SIGNAL: a signal fence, against a signal handler on the same thread alone; without it, a thread
    // fence). It reads and writes no value, but no pass moves a load, a store or a call across it, eliminates one past it
    // or drops it: an effect as an unknown call's on memory, without a call's registers. The typed stream's FENCE.
    IR_FENCE,
    // A pure operation of two integers of its type that C has no operator for (#141), `c` which one
    // (BUSTER_IR_INTRINSIC_*): the high 64 bits of an I64's unsigned product, a times b as 128 bits (mulhu: the high half
    // of a 128-bit integer's product, which Tier 1's split of the 128-bit integers into halves makes, ir_wide.c, and
    // x86's mul gives in rdx), and the x86 builtins' scalar bit manipulations of an I32 or an I64, BMI2's pdep and pext
    // (a's bits deposited at, or extracted from, mask b's set bits) and bzhi (a with its bits from b's low byte up
    // cleared), and BMI1's bextr (a's bits from b's low byte up, as many as its second byte says), whose x86 rows the
    // typed stream's lowering admits (ir_simd_typed.c). The unary splat (#1450) repeats scalar a's raw bits throughout
    // a vector result; its scalar type gives the lane width, b is absent and flags are zero. It does no arithmetic,
    // including on floating operands, so signed zeros and NaN payloads are unchanged. The vector width changes (#1840:
    // extract, widen and insert) move a vector's bits whole between widths, their part in flags, and the permutes (#1840:
    // permute, shuffle, align and shuffle_lanes) move lanes of two vectors, their immediate in flags; the conversions
    // (#1840: extend and truncate) widen or narrow integer lanes, their lanes and mode in flags.
    IR_INTRINSIC,
    // An atomic read/modify/write (#1059): `a` the object's address, `b` the operand, of the result's type (an integer of
    // 1, 2, 4 or 8 bytes), and its flags BUSTER_IR_ATOMIC with its C memory order and its operation
    // (BUSTER_IR_RMW_OPERATION: an exchange, an addition or a compare-exchange, whose desired value, of the result's type,
    // is its `c`, which the others haven't). It returns the object's old value and leaves the operand (an exchange), the
    // sum (an addition) or, where the old value is the operand, the desired value (a compare-exchange) in it, as one
    // locked access; no pass moves an access across it or eliminates it, as an atomic store's and load's. The typed
    // stream's ATOMIC_RMW and ATOMIC_CAS.
    IR_ATOMIC_RMW,
    // A masked vector load (#1840): `a` the address, `b` the mask and `c` the passthrough, a vector of the result's type;
    // its flags the lanes (an IrSimdLane of integers, as a vselect's), whose mask `b` is (ir_lane_mask), and its
    // alignment_log2 a load's. Its lane i is the lane at the address's lane i where b's bit i is set, and the
    // passthrough's lane i where it isn't. A lane whose bit is clear is never read, so it can't fault: the masked
    // builtins' tail reads past an array's end are defined (vmovdqu8 to vmovdqu64 zmm{k}, [p]). A load's effect on
    // memory, of the set lanes' bytes alone: no pass moves it across a store or eliminates it but as an unused load.
    IR_MASKED_LOAD,
    // A masked vector store (#1840): `a` the address, `b` the stored vector and `c` the mask, its flags the lanes as a
    // masked load's and its alignment_log2 a store's. It writes the vector's lane i at the address's lane i where c's bit
    // i is set, and no byte of a lane whose bit is clear (vmovdqu8 to vmovdqu64 [p]{k}, zmm). A store's effect.
    IR_MASKED_STORE,
    IR_OP_COUNT,
} IrOp;

// The opcode tables' entries (#1840): two byte-permute registers' worth, which vpermi2b looks up at once.
#define BUSTER_IR_OP_TABLE 128

// An IR_FENCE's operand (#1059), as the typed stream's FENCE value: its order and its scope, and seq_cst, the order a
// thread fence needs an instruction for on x86-64 (mfence).
#define BUSTER_IR_FENCE_ORDER 0x07u
#define BUSTER_IR_FENCE_SIGNAL 0x08u
#define BUSTER_IR_FENCE_SEQ_CST 5u
// An IR_ATOMIC_RMW's operation (#1059), in its flags past its order: an exchange, an addition, or a compare-exchange,
// whose desired value is its `c`.
#define BUSTER_IR_RMW_OPERATION 0x60u
#define BUSTER_IR_RMW_EXCHANGE 0x00u
#define BUSTER_IR_RMW_ADD 0x20u
#define BUSTER_IR_RMW_COMPARE 0x40u
// An IR_INTRINSIC's operation (#141), its `c`: mulhu an I64's alone, BMI operations an I32's or an I64's; splat's
// result a vector, its only operand an I8, I16, I32, I64, F32 or F64 whose bit pattern is repeated in every lane. The bit
// operations (#1374, from TZCNT on) take one operand, an I32 or an I64 of their result's type, as their x86 instructions
// give them: tzcnt's and lzcnt's counts of trailing and leading zero bits, the width for 0; bsf's index of the lowest set
// bit and bsr's of the highest, unspecified for 0 (the interpreter gives the width), which only a producer whose 0 is
// undefined or masked takes; popcnt's count of set bits; and bswap's bytes reversed. The signed overflows (#1385, from
// SADDO on) take two operands of their result's type, an I32 or an I64, and give 1 where a + b, a - b or a * b leaves its
// signed range and 0 where it doesn't, as x86's add, sub (cmp) and imul set OF: a signed overflow builtin's flag.
#define BUSTER_IR_INTRINSIC_MULHU 0u
#define BUSTER_IR_INTRINSIC_PDEP 1u
#define BUSTER_IR_INTRINSIC_PEXT 2u
#define BUSTER_IR_INTRINSIC_BZHI 3u
#define BUSTER_IR_INTRINSIC_BEXTR 4u
#define BUSTER_IR_INTRINSIC_SPLAT 5u
#define BUSTER_IR_INTRINSIC_TZCNT 6u
#define BUSTER_IR_INTRINSIC_BSF 7u
#define BUSTER_IR_INTRINSIC_LZCNT 8u
#define BUSTER_IR_INTRINSIC_BSR 9u
#define BUSTER_IR_INTRINSIC_POPCNT 10u
#define BUSTER_IR_INTRINSIC_BSWAP 11u
#define BUSTER_IR_INTRINSIC_SADDO 12u
#define BUSTER_IR_INTRINSIC_SSUBO 13u
#define BUSTER_IR_INTRINSIC_SMULO 14u
// The vector width changes (#1840), which move bits whole and read no lanes, their part in the instruction's flags (an
// intrinsic's immediate, 0 for every other kind): extract's result is a's `flags`th part of the result's width, a wider
// vector (16 bytes of 32 or 64, 32 of 64); widen's is a, a narrower vector, in its low bytes and zeros past them (flags
// 0); and insert's is a, of its type, with its `flags`th part of b's width replaced by b, a narrower vector. The x86
// builtins' casts between widths, vextracti32x4 to vinserti64x4.
#define BUSTER_IR_INTRINSIC_EXTRACT 15u
#define BUSTER_IR_INTRINSIC_WIDEN 16u
#define BUSTER_IR_INTRINSIC_INSERT 17u
// The permutes (#1840), of two vectors of the result's type, their immediate in flags: permute's lane i is a's lane that
// b's lane i's low bits name, its lanes' bytes 1 << flags (0 to 3: vpermb to vpermq); shuffle's byte i is 0 where b's byte
// i has its top bit and else a's byte in i's 16 bytes that b's byte's low 4 bits name (vpshufb, flags 0); align's lane i
// is lane i + count of b's lanes followed by a's, its lanes' bytes 1 << (flags >> 4) (2 or 3) and its count flags' low 4
// bits, below the lanes (valignd, valignq); and shuffle_lanes' 16-byte lane j is, from a for the result's low half and b
// for its high half, the lane flags' bits from j times log2 of the lanes name (vshufi64x2: 32 or 64 bytes).
#define BUSTER_IR_INTRINSIC_PERMUTE 18u
#define BUSTER_IR_INTRINSIC_SHUFFLE 19u
#define BUSTER_IR_INTRINSIC_ALIGN 20u
#define BUSTER_IR_INTRINSIC_SHUFFLE_LANES 21u
// The conversions between integer lanes (#1840), unary, their lanes in flags: the source's lanes' bytes' log2 in bits 0
// and 1, the result's in bits 2 and 3, and the mode in bits 4 and 5. extend's lane i is a's lane i, zero-extended (mode 0)
// or sign-extended (1) to a wider lane, as many lanes as the result has, from a's low bytes (vpmovzx, vpmovsx); truncate's
// lane i is a's lane i, every lane of a, narrowed by wrapping (mode 0), saturation of a signed lane to the signed range
// (1) or of an unsigned lane to the unsigned range (2), in the result's low bytes and zeros past them, the result 16 bytes
// or the lanes' bytes if more (vpmov, vpmovs, vpmovus).
#define BUSTER_IR_INTRINSIC_EXTEND 22u
#define BUSTER_IR_INTRINSIC_TRUNCATE 23u
// A shift of a vector's integer lanes by one scalar count (#1840), binary: a the vector, of the result's type, and b an
// I32, the count, read unsigned; its flags the lanes' bytes' log2 in bits 0 and 1 (1, 2 or 3: words to quadwords) and
// the shift in bits 2 and 3 (0 left, 1 logical right, 2 arithmetic right). Each lane is a's shifted by the count, and a
// count of the lane's bits or more gives 0, or every bit the sign for an arithmetic shift: vpsllw to vpsraq by a count in
// an xmm register's low quadword, which the x86 builtins' shifts by an int move there by vmovd. A count that is a
// constant is the immediate form's (tier1_select.c).
#define BUSTER_IR_INTRINSIC_SHIFT 24u
// A binary operation of two vectors' integer lanes that the IR's own opcodes lack (#1840), of a and b of the result's
// type: its flags the lanes' bytes' log2 in bits 0 and 1 (0 to 3: bytes to quadwords) and the operation in bits 2 to 4:
// the unsigned (0) and signed (1) minimum, the unsigned (2) and signed (3) maximum, and of bytes and words alone the
// unsigned (4) and signed (5) saturating sum, and the unsigned (6) and signed (7) saturating difference a - b
// (vpminub to vpmaxsq, vpaddusb to vpsubsw: clang's __builtin_elementwise_min, _max, _add_sat and _sub_sat).
#define BUSTER_IR_INTRINSIC_LANE_OP 25u
// The ternary kinds (#1840), from permute2 on, have a third operand, which no instruction field holds: their `c` is the
// kind in its low 8 bits and, past them, the index in the function's extra array of the third operand's value
// (ir_intrinsic_third), as a phi's or a call's operands are there. A binary kind's `c` is the kind alone.
// permute2's lane i is, of the lanes of a and then of the third operand, all of the result's type, the one that b's lane i's
// low bits name, as many bits as name twice the lanes, its lanes' bytes' log2 in flags (0 to 3): vpermt2b to vpermt2q
// (_mm512_permutex2var_epi32(a, idx, b): a, the indices b and the third operand).
#define BUSTER_IR_INTRINSIC_PERMUTE2 26u
// ternlog's every bit is bit (a << 2 | b << 1 | c) of flags, its immediate, of a's, b's and the third operand's same bit,
// all of the result's type: vpternlogd and vpternlogq, whose destination is a, b in vvvv and the third operand in rm
// (_mm512_ternarylogic_epi32(a, b, c, imm)).
#define BUSTER_IR_INTRINSIC_TERNLOG 27u
// compress's lanes are a's lanes whose bits b, a mask of the lanes, has set, in order from lane 0, followed by the third
// operand's lanes from the first one past them; expand's lane i is, where b's bit i is set, a's next lane from lane 0,
// and the third operand's lane i where it isn't. Their flags are the lanes' bytes' log2 (0 to 3), b the result's mask at
// those lanes, a and the third operand of the result's type: vpcompressb to vpcompressq and vpexpandb to vpexpandq into a
// register, merged into the third operand (the maskz builtins' a zero vector).
#define BUSTER_IR_INTRINSIC_COMPRESS 28u
#define BUSTER_IR_INTRINSIC_EXPAND 29u
// One past the ternary kinds, permute2 to expand.
#define BUSTER_IR_INTRINSIC_TERNARY_END 30u
// An AES round of each 16-byte lane (#1840), binary: a the state and b the round key, both of the result's type, its
// flags 0 for a round (vaesenc: ShiftRows, SubBytes and MixColumns of a, then b xor'ed) and 1 for the last one
// (vaesenclast: without MixColumns), as FIPS 197 and x86's VAES define them.
#define BUSTER_IR_INTRINSIC_AES 30u
// One lane of a vector (#1840), its flags the lane's bytes' log2 in bits 0 and 1 (0 to 3) and its index in bits 2 to 7,
// below the vector's lanes: lane_extract's result, an I32 for a lane of 1, 2 or 4 bytes and an I64 for one of 8, is a's
// lane zero-extended (vmovd, vmovq, vpextrb to vpextrq, after vextracti32x4 for a lane past the low 16 bytes); and
// lane_insert's is a, of its type, with the lane replaced by b's low bits, b an I32 for a lane of 1, 2 or 4 bytes and an
// I64 for one of 8 (vpinsrb to vpinsrq). movemask's I32 holds, from bit 0, the sign bit of each of a's lanes, a vector
// of 16 or 32 bytes, its flags the lanes' bytes' log2 (vpmovmskb's bytes 0, vmovmskps's 2 and vmovmskpd's 3).
#define BUSTER_IR_INTRINSIC_LANE_EXTRACT 31u
#define BUSTER_IR_INTRINSIC_LANE_INSERT 32u
#define BUSTER_IR_INTRINSIC_MOVEMASK 33u
// A reduction of a vector's integer lanes (#1840), unary: its result a 16-byte vector (V4I32) each of whose lanes is
// the operation of all of a's lanes, a a vector of 16, 32 or 64 bytes; its flags the lanes' bytes' log2 in bits 0 and 1
// (2 or 3: doublewords or quadwords) and the operation in bits 2 to 5 (BUSTER_IR_REDUCE_*): the unsigned (0) and signed
// (1) minimum, the unsigned (2) and signed (3) maximum, as lane_op's, then the wrapping sum (4) and product (5), and
// the and (6), or (7) and xor (8). Clang's __builtin_reduce_* code halves the vector (vextracti64x4, vextracti32x4,
// then vpshufd 0x4e and, of doublewords, 0xb1), each half combined with the other, which leaves every lane of the last
// 16 bytes the result; lane_extract's lane 0 then takes it.
#define BUSTER_IR_INTRINSIC_REDUCE 34u
#define BUSTER_IR_REDUCE_ADD 4u
#define BUSTER_IR_REDUCE_MUL 5u
#define BUSTER_IR_REDUCE_AND 6u
#define BUSTER_IR_REDUCE_OR 7u
#define BUSTER_IR_REDUCE_XOR 8u
#define BUSTER_IR_REDUCE_OPERATIONS 9u
// A unary operation of each of a vector's integer lanes (#1840), a of the result's type: its flags the lanes' bytes'
// log2 in bits 0 and 1 and the operation in bits 2 and 3 (BUSTER_IR_LANE_UNARY_*): each lane's set bits (0: vpopcntb to
// vpopcntq), its leading zeros, the lane's bits for a zero lane (1: vplzcntd, vplzcntq), or its conflicts, bit j set
// for each lane j below it that equals it (2: vpconflictd, vpconflictq); the last two of doublewords and quadwords.
#define BUSTER_IR_INTRINSIC_LANE_UNARY 35u
#define BUSTER_IR_INTRINSIC_COUNT 36u
#define BUSTER_IR_LANE_UNARY_POPCOUNT 0u
#define BUSTER_IR_LANE_UNARY_LZCNT 1u
#define BUSTER_IR_LANE_UNARY_CONFLICT 2u
#define BUSTER_IR_INTRINSIC_TERNARY BUSTER_IR_INTRINSIC_PERMUTE2
#define BUSTER_IR_INTRINSIC_KIND 0xffu
#define BUSTER_IR_INTRINSIC_EXTRA_SHIFT 8u
#define BUSTER_IR_INTRINSIC_EXTRA_LIMIT (UINT32_MAX >> BUSTER_IR_INTRINSIC_EXTRA_SHIFT)

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
// `row` is 1 + the index in IrFunction.rows of the machine row an IR_CALL runs in place of a callee (#141, a row call:
// its symbol, a, is then 0), and 0 for any other call. `body` is 1 + the index in IrFunction.asm_bodies of the asm body
// an IR_CALL runs in place of a callee (#1839, a body call: its symbol is 0 too, and its row 0), and 0 for any other.
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
    uint32_t row;
    uint32_t body;
};

// An asm statement placed in its caller (#1839, Tier0AsmBody): the records a body call runs in its place, which read the
// statement's operand block through BUSTER_TIER0_ASM_BLOCK's base, their own branches (relative to the first record; a
// target may be `count`, the body's end) and the registers they write (`touched`, BUSTER_TIER0_XMM's layout: rax to r15
// in bits 0 to 15, xmm0 to xmm15 in bits 16 to 31). The block is the call's first argument, a ptr that is the block's
// IR_STACK_ADDR, and a body without one has none: every pass takes the call as an unknown callee's of that pointer, so
// the block escapes into it, its stores stay before it and no access moves across it. The register operands follow it
// (#2144, CheckProgramAsm.inline_passed): `passed` arguments, the call's last, each a value its records read in its
// register, `locations[k].registers[0]` (a general register's number, or BUSTER_ABI_REGISTER_VECTOR | n for xmm n), as
// Tier 0's call row has them. The records and locations are borrowed from the producer, trusted as a row's are; the
// verifier checks the columns' shape.
typedef struct IrAsmBody IrAsmBody;
struct IrAsmBody
{
    const X86Instruction* records;
    const uint32_t* branches;
    const uint32_t* targets;
    uint32_t count;
    uint32_t branch_count;
    uint32_t touched;
    // The opmask registers the records write (k0 to k7, a bit each; Tier0AsmBody.masks), whose live masks Tier 1's CFG
    // consumer saves around the body (tier1_emit_cfg_asm).
    uint8_t masks;
    // Whether it moves an input into rsp (#1846: rsp's explicit register variable, Tier0AsmBody.stack): its function's
    // frame is then the program's, which the code certificate leaves uncertified.
    bool stack;
    // Its register operands' registers (#2144), one location per argument past the block, `passed` of them.
    const AbiLocation* locations;
    uint32_t passed;
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
    uint64_t vector_constant_count;
    uint64_t row_count;
    uint64_t asm_body_count;
};

typedef struct IrFunction IrFunction;
typedef enum IrDebugScalarKind
{
    IR_DEBUG_PARAMETER = 1,
    IR_DEBUG_CONSTANT,
} IrDebugScalarKind;

// Immutable whole values proven by the original typed replay, not rediscovered after optimization. A constant's
// entry is its initializing store's completion point in the first source block; a parameter has no such entry.
typedef struct IrDebugScalar IrDebugScalar;
struct IrDebugScalar
{
    uint64_t value;
    uint32_t local;
    uint32_t entry;
    uint8_t kind;
    uint8_t bytes;
};

typedef enum IrDebugBindingKind
{
    IR_DEBUG_BIND_VALUE = 1,
    IR_DEBUG_BIND_CONSTANT,
    IR_DEBUG_BIND_UNAVAILABLE,
} IrDebugBindingKind;

#define BUSTER_IR_DEBUG_BIND_ENTRY 1u
#define BUSTER_IR_DEBUG_BIND_REGIONS 2u

// Source assignments survive even when their value has no reconstruction. `entry` is the original typed completion
// point; NONE is the initial parameter binding. `value` is a current IR node only for VALUE. `anchor` is an optional
// current RHS definition whose completion can conservatively kill older bindings. It can precede the assignment,
// so it never establishes source visibility by itself. Events are not semantic uses and preserve no machine lifetime.
typedef struct IrDebugBinding IrDebugBinding;
struct IrDebugBinding
{
    uint64_t literal;
    uint32_t local;
    uint32_t entry;
    uint32_t value;
    uint32_t anchor;
    uint8_t kind;
    uint8_t bytes;
    uint8_t type;
};

// A real source read or assignment witnesses one unchanged local value within an original typed block. `block`
// follows the current IR's exact block remaps, while [begin, end) and block_end stay in original typed-entry coordinates. `next`
// names the next original assignment binding in that block, including after a lifetime gap, or NONE if there is
// none. It supplies a conservative completion kill when scheduling moves an older source instruction after it.
typedef struct IrDebugRegion IrDebugRegion;
struct IrDebugRegion
{
    uint64_t literal;
    uint32_t local;
    uint32_t block;
    uint32_t begin;
    uint32_t end;
    uint32_t block_end;
    uint32_t value;
    uint32_t next;
    uint8_t kind;
    uint8_t bytes;
    uint8_t type;
};

// Optional source identities (#1114), separate from the instruction/slot columns that execution consumes. An origin
// is a typed-stream entry; a local is its original typed local. NONE means generated/unknown, never a nearby source.
// Rewriters keep the original bounds and rebuild the current lengths before their old arenas can be retired.
typedef struct IrDebugInfo IrDebugInfo;
struct IrDebugInfo
{
    uint32_t* origins;
    uint32_t* locals;
    uint32_t instruction_count;
    uint32_t slot_count;
    uint32_t entry_count;
    uint32_t local_count;
    IrDebugScalar* scalars;
    uint32_t scalar_count;
    IrDebugBinding* bindings;
    uint32_t binding_count;
    IrDebugRegion* regions;
    uint32_t region_count;
    // Original-local flags: ENTRY proves a normal scalar with no escape/volatile access and all assignments in the
    // first source block, with no edge back into it. REGIONS admits other normal private scalars only through their
    // witnessed source regions. The flags are mutually exclusive; neither flag admits an escaped/volatile local.
    uint8_t* binding_flags;
    uint32_t first_block_end;
};

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
    IrSimdValue* vector_constants;
    uint32_t vector_constant_count;
    // The machine rows its row calls run (#141, IrCall.row): the typed stream's X86 rows (Tier0Operation, #717) that the
    // IR has no operation of its own for, the vector builtins' and the vector operators' on elements other than 32-bit
    // integers. A row call is as opaque to the IR's passes as a call of an unknown function, which reads and writes
    // memory: its arguments are the row's operands in order and its result the row's, of the types ir_row_type gives
    // their classes. Its records are the producer's, trusted as IrPassed's locations are: the verifier checks the row's
    // counts and classes against its calls, and Tier 1 the records themselves (tier0_operation_valid) before it places
    // them.
    Tier0Operation* rows;
    uint32_t row_count;
    // The asm bodies its body calls run (#1839, IrCall.body), one per call.
    IrAsmBody* asm_bodies;
    uint32_t asm_body_count;
    uint8_t result_type;
    // Exclusive symbol bound supplied by the translation unit. UINT32_MAX permits any non-sentinel SymbolId.
    uint32_t symbol_count;
    // The labels (GNU C's && and goto *, and the producer's jump tables' cases): label k's block, at whose code the
    // producer defines its k-th label symbol, and which a br_indirect may go to through its explicit list or all-label
    // targets; or BUSTER_IR_NONE for a label no block holds and no indirect branch reaches (a backend defines its
    // symbol anywhere in the function).
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
    IrDebugInfo* debug;
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
    IR_ERROR_UNSUPPORTED,
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
    [IR_VOID] = 0,
    [IR_I1] = 1,
    [IR_I8] = 8,
    [IR_I16] = 16,
    [IR_I32] = 32,
    [IR_I64] = 64,
    [IR_I128] = 128,
    [IR_F32] = 32,
    [IR_F64] = 64,
    [IR_F80] = 80,
    [IR_PTR] = 64,
    [IR_V4I32] = 128,
    [IR_V8I32] = 256,
    [IR_V16I32] = 512,
    [IR_M8] = 8,
    [IR_M16] = 16,
    [IR_M32] = 32,
    [IR_M64] = 64,
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
    "v4i32",
    "v8i32",
    "v16i32",
    "m8",
    "m16",
    "m32",
    "m64",
};
// A vector operation's lanes' names (#141's element types), by IrSimdLane, and its vector type's, by its width (16, 32 or
// 64 bytes) and lanes: `add.v16i8` is an add of v4i32's 16 bytes as 8-bit lanes, its flags I8.
static const char* const ir_lane_names[IR_SIMD_LANE_COUNT] = { "i32", "i8", "i16", "i64", "f32", "f64" };
static const char* const ir_lane_type_names[3][IR_SIMD_LANE_COUNT] = {
    { "v4i32", "v16i8", "v8i16", "v2i64", "v4f32", "v2f64" },
    { "v8i32", "v32i8", "v16i16", "v4i64", "v8f32", "v4f64" },
    { "v16i32", "v64i8", "v32i16", "v8i64", "v16f32", "v8f64" },
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
    "vconst",
    "vcmp",
    "vselect",
    "lifetime",
    "fence",
    "intrinsic",
    "rmw",
    "masked_load",
    "masked_store",
};

// The text form's names of a machine row's classes (#141, IrFunction.rows): the typed stream's, a vector's and a mask's
// followed by their bytes (vector64, mask8).
static const char* const ir_row_class_names[BUSTER_TYPED_CLASS_LIMIT] = {
    "void",
    "i8",
    "u8",
    "i16",
    "u16",
    "i32",
    "u32",
    "i64",
    "u64",
    "f32",
    "f64",
    "f80",
    "i128",
    "u128",
    "vector",
    "mask",
};

// The x86 forms' names in a machine row's records (#141), X86Form's without X86_FORM_, so that the text names a form by
// what it is rather than by its place in the table, which grows.
#define BUSTER_IR_FORM_NAME(name, ...) #name,
static const char* const ir_form_names[X86_FORM_COUNT] = { BUSTER_X86_FORMS(BUSTER_IR_FORM_NAME, BUSTER_IR_FORM_NAME) };
#undef BUSTER_IR_FORM_NAME

// An intrinsic's operations' names in the text form (BUSTER_IR_INTRINSIC_*), after its operands as a comparison's
// predicate is.
static const char* const ir_intrinsic_names[BUSTER_IR_INTRINSIC_COUNT] = {
    "mulhu",
    "pdep",
    "pext",
    "bzhi",
    "bextr",
    "splat",
    "tzcnt",
    "bsf",
    "lzcnt",
    "bsr",
    "popcnt",
    "bswap",
    "saddo",
    "ssubo",
    "smulo",
    "extract",
    "widen",
    "insert",
    "permute",
    "shuffle",
    "align",
    "shuffle_lanes",
    "extend",
    "truncate",
    "shift",
    "lane_op",
    "permute2",
    "ternlog",
    "compress",
    "expand",
    "aes",
    "lane_extract",
    "lane_insert",
    "movemask",
    "reduce",
    "lane_unary",
};

// A fence's orders' names in the text form, C's memory_order's (BUSTER_IR_FENCE_ORDER), and its signal scope's word.
static const char* const ir_fence_orders[BUSTER_IR_FENCE_SEQ_CST + 1] = {
    "relaxed",
    "consume",
    "acquire",
    "release",
    "acq_rel",
    "seq_cst",
};

static bool ir_integer(uint32_t type)
{
    return type >= IR_I1 && type <= IR_I128;
}

static bool ir_floating(uint32_t type)
{
    return type >= IR_F32 && type <= IR_F80;
}

// Whether a load's or a store's flags (BUSTER_IR_ACCESS_FLAGS) give an order to an atomic access alone, and one its kind
// takes: a load's relaxed, consume, acquire or seq_cst (0x27's bits), a store's relaxed, release or seq_cst (0x29's).
static bool ir_access_flags_valid(uint32_t op, uint8_t flags)
{
    const uint32_t order = (flags & BUSTER_IR_ORDER) >> BUSTER_IR_ORDER_SHIFT;
    const uint32_t orders = op == IR_LOAD ? 0x27u : 0x29u;
    return (flags & ~BUSTER_IR_ACCESS_FLAGS) == 0 && ((flags & BUSTER_IR_ATOMIC) != 0 ? (orders >> order & 1) != 0 : order == 0);
}

// Whether a masked load's or store's flags are its lanes (#1840): an IrSimdLane of integers, whose mask is its operand.
static bool ir_masked_lanes_valid(uint8_t flags)
{
    return flags == IR_SIMD_LANE_I8 || flags == IR_SIMD_LANE_I16 || flags == IR_SIMD_LANE_I32 || flags == IR_SIMD_LANE_I64;
}

// Whether an atomic read/modify/write's flags are an atomic access's of a C order, any of them (#1059): the locked access
// is a full barrier, which every order takes; and of an exchange, an addition or a compare-exchange.
static bool ir_rmw_flags_valid(uint8_t flags)
{
    return (flags & ~(BUSTER_IR_ATOMIC | BUSTER_IR_ORDER | BUSTER_IR_RMW_OPERATION)) == 0 && (flags & BUSTER_IR_ATOMIC) != 0 &&
           (flags & BUSTER_IR_ORDER) >> BUSTER_IR_ORDER_SHIFT <= 5 && (flags & BUSTER_IR_RMW_OPERATION) <= BUSTER_IR_RMW_COMPARE;
}

// Whether an atomic access may be of `type` aligned to 1 << `alignment_log2` bytes: an integer's of 1, 2, 4 or 8 bytes,
// or a pointer's, aligned at least to its size, as x86 makes only a naturally aligned access single-copy atomic.
static bool ir_atomic_access(uint32_t type, uint8_t alignment_log2)
{
    const bool sized = type == IR_I8 || type == IR_I16 || type == IR_I32 || type == IR_I64 || type == IR_PTR;
    return sized && alignment_log2 >= (uint32_t)__builtin_ctz(ir_type_bits[type] >> 3);
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
        sizes->label_count > UINT32_MAX || sizes->abi_call_count > UINT32_MAX || sizes->passed_count > UINT32_MAX || sizes->vector_constant_count > UINT32_MAX || sizes->row_count > UINT32_MAX ||
        sizes->asm_body_count > UINT32_MAX)
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
            .vector_constant_count = (uint32_t)sizes->vector_constant_count,
            .row_count = (uint32_t)sizes->row_count,
            .asm_body_count = (uint32_t)sizes->asm_body_count,
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
        candidate.vector_constants = ir_allocate(arena, sizes->vector_constant_count, sizeof(IrSimdValue), &ok);
        candidate.rows = ir_allocate(arena, sizes->row_count, sizeof(Tier0Operation), &ok);
        candidate.asm_bodies = ir_allocate(arena, sizes->asm_body_count, sizeof(IrAsmBody), &ok);
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

// Allocate a fresh sidecar for a derived function. A source-less pipeline allocates and initializes nothing here.
// New instructions and slots start without identities; the transformation fills only the mappings it proves.
static bool ir_debug_create(Arena* arena, IrFunction* function, const IrDebugInfo* source)
{
    const uint64_t mark = arena->length;
    bool ok = true;
    IrDebugInfo* debug = NULL;
    if (source != NULL)
    {
        debug = ir_allocate(arena, 1, sizeof(IrDebugInfo), &ok);
        uint32_t* origins = ir_allocate(arena, function->instruction_count, sizeof(uint32_t), &ok);
        uint32_t* locals = ir_allocate(arena, function->slot_count, sizeof(uint32_t), &ok);
        IrDebugScalar* scalars = ir_allocate(arena, source->scalar_count, sizeof(IrDebugScalar), &ok);
        IrDebugBinding* bindings = ir_allocate(arena, source->binding_count, sizeof(IrDebugBinding), &ok);
        IrDebugRegion* regions = ir_allocate(arena, source->region_count, sizeof(IrDebugRegion), &ok);
        uint8_t* binding_flags = ir_allocate(arena, source->binding_flags != NULL ? source->local_count : 0, 1, &ok);
        if (ok)
        {
            *debug = (IrDebugInfo){
                .origins = origins,
                .locals = locals,
                .instruction_count = function->instruction_count,
                .slot_count = function->slot_count,
                .entry_count = source->entry_count,
                .local_count = source->local_count,
                .scalars = scalars,
                .scalar_count = source->scalar_count,
                .bindings = bindings,
                .binding_count = source->binding_count,
                .regions = regions,
                .region_count = source->region_count,
                .binding_flags = binding_flags,
                .first_block_end = source->first_block_end,
            };
            if (function->instruction_count != 0)
            {
                memset(origins, 0xff, (uint64_t)function->instruction_count * sizeof(uint32_t));
            }
            if (function->slot_count != 0)
            {
                memset(locals, 0xff, (uint64_t)function->slot_count * sizeof(uint32_t));
            }
            if (source->scalar_count != 0)
            {
                memcpy(scalars, source->scalars, (uint64_t)source->scalar_count * sizeof(IrDebugScalar));
            }
            if (source->binding_count != 0)
            {
                memcpy(bindings, source->bindings, (uint64_t)source->binding_count * sizeof(IrDebugBinding));
            }
            if (source->region_count != 0)
            {
                memcpy(regions, source->regions, (uint64_t)source->region_count * sizeof(IrDebugRegion));
            }
            if (source->binding_flags != NULL && source->local_count != 0)
            {
                memcpy(binding_flags, source->binding_flags, source->local_count);
            }
        }
    }
    if (ok)
    {
        function->debug = debug;
        function->arena_bytes += arena->length - mark;
    }
    else
    {
        arena_pop_to(arena, mark);
    }
    return ok;
}

// The old-to-new instruction, slot and block maps belong to the transformation. A null map means unchanged numbering;
// NONE removes that identity. Block maps preserve each surviving block's identity, never redirect a removed block
// to its threaded successor or merge source blocks; a merger must omit or split those source regions explicitly.
// Call only after a fresh sidecar was allocated for the result.
static void ir_debug_remap(const IrFunction* source, IrFunction* result, const uint32_t* instructions, const uint32_t* slots, const uint32_t* blocks)
{
    if (source->debug != NULL && result->debug != NULL)
    {
        const uint32_t* const origins = source->debug->origins;
        const uint32_t* const locals = source->debug->locals;
        uint32_t* const rewritten_origins = result->debug->origins;
        uint32_t* const rewritten_locals = result->debug->locals;
        for (uint32_t i = 0; i < source->instruction_count; i += 1)
        {
            const uint32_t out = instructions != NULL ? instructions[i] : i;
            if (out != BUSTER_IR_NONE)
            {
                rewritten_origins[out] = origins[i];
            }
        }
        for (uint32_t s = 0; s < source->slot_count; s += 1)
        {
            const uint32_t out = slots != NULL ? slots[s] : s;
            if (out != BUSTER_IR_NONE)
            {
                rewritten_locals[out] = locals[s];
            }
        }
        for (uint32_t k = 0; k < source->debug->binding_count; k += 1)
        {
            const IrDebugBinding binding = source->debug->bindings[k];
            IrDebugBinding* rewritten = result->debug->bindings + k;
            rewritten->anchor = binding.anchor != BUSTER_IR_NONE && instructions != NULL ? instructions[binding.anchor] : binding.anchor;
            if (binding.kind == IR_DEBUG_BIND_VALUE)
            {
                rewritten->value = instructions != NULL ? instructions[binding.value] : binding.value;
                rewritten->kind = rewritten->value == BUSTER_IR_NONE ? IR_DEBUG_BIND_UNAVAILABLE : IR_DEBUG_BIND_VALUE;
            }
        }
        for (uint32_t k = 0; k < source->debug->region_count; k += 1)
        {
            const IrDebugRegion region = source->debug->regions[k];
            IrDebugRegion* rewritten = result->debug->regions + k;
            rewritten->block = region.block != BUSTER_IR_NONE && blocks != NULL ? blocks[region.block] : region.block;
            if (region.kind == IR_DEBUG_BIND_VALUE)
            {
                rewritten->value = instructions != NULL ? instructions[region.value] : region.value;
                rewritten->kind = rewritten->value == BUSTER_IR_NONE ? IR_DEBUG_BIND_UNAVAILABLE : IR_DEBUG_BIND_VALUE;
            }
            if (rewritten->block == BUSTER_IR_NONE)
            {
                rewritten->kind = IR_DEBUG_BIND_UNAVAILABLE;
                rewritten->value = BUSTER_IR_NONE;
            }
        }
    }
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
    IR_RULE_VECTOR_CONSTANT,
    IR_RULE_VECTOR_COMPARE,
    IR_RULE_VECTOR_SELECT,
    IR_RULE_FENCE,
    IR_RULE_ATOMIC_RMW,
    IR_RULE_INTRINSIC,
    IR_RULE_MASKED_LOAD,
    IR_RULE_MASKED_STORE,
} IrRule;

#define BUSTER_IR_INTS (((1u << (IR_I128 + 1)) - 1) & ~1u)
#define BUSTER_IR_FLOATS ((1u << IR_F32) | (1u << IR_F64) | (1u << IR_F80))
#define BUSTER_IR_VECTORS ((1u << IR_V4I32) | (1u << IR_V8I32) | (1u << IR_V16I32))
#define BUSTER_IR_MASKS ((1u << IR_M8) | (1u << IR_M16) | (1u << IR_M32) | (1u << IR_M64))
#define BUSTER_IR_TYPES (UINT32_MAX >> (32 - IR_TYPE_COUNT))
#define BUSTER_IR_VALUES (BUSTER_IR_TYPES & ~1u)
// op, SSA operands, occupied fields, result type set, semantic rule, terminator. These tables also drive use enumeration.
#define BUSTER_IR_ROWS(X) \
    X(IR_CONST,         0, 1, BUSTER_IR_VALUES & ~BUSTER_IR_VECTORS, IR_RULE_CONSTANT, 0) \
    X(IR_UNDEF,         0, 0, BUSTER_IR_VALUES, IR_RULE_NONE, 0) \
    X(IR_PARAM,         0, 1, BUSTER_IR_VALUES, IR_RULE_PARAMETER, 0) \
    X(IR_COPY,          1, 1, BUSTER_IR_VALUES, IR_RULE_SAME, 0) \
    X(IR_ADD,           3, 3, BUSTER_IR_INTS | BUSTER_IR_VECTORS, IR_RULE_SAME, 0) \
    X(IR_SUB,           3, 3, BUSTER_IR_INTS | BUSTER_IR_VECTORS, IR_RULE_SAME, 0) \
    X(IR_MUL,           3, 3, BUSTER_IR_INTS | BUSTER_IR_VECTORS, IR_RULE_SAME, 0) \
    X(IR_SDIV,          3, 3, BUSTER_IR_INTS, IR_RULE_SAME, 0) \
    X(IR_UDIV,          3, 3, BUSTER_IR_INTS, IR_RULE_SAME, 0) \
    X(IR_SREM,          3, 3, BUSTER_IR_INTS, IR_RULE_SAME, 0) \
    X(IR_UREM,          3, 3, BUSTER_IR_INTS, IR_RULE_SAME, 0) \
    X(IR_AND,           3, 3, BUSTER_IR_INTS | BUSTER_IR_VECTORS | BUSTER_IR_MASKS, IR_RULE_SAME, 0) \
    X(IR_OR,            3, 3, BUSTER_IR_INTS | BUSTER_IR_VECTORS | BUSTER_IR_MASKS, IR_RULE_SAME, 0) \
    X(IR_XOR,           3, 3, BUSTER_IR_INTS | BUSTER_IR_VECTORS | BUSTER_IR_MASKS, IR_RULE_SAME, 0) \
    X(IR_SHL,           3, 3, BUSTER_IR_INTS | BUSTER_IR_VECTORS, IR_RULE_SHIFT, 0) \
    X(IR_LSHR,          3, 3, BUSTER_IR_INTS | BUSTER_IR_VECTORS, IR_RULE_SHIFT, 0) \
    X(IR_ASHR,          3, 3, BUSTER_IR_INTS | BUSTER_IR_VECTORS, IR_RULE_SHIFT, 0) \
    X(IR_NEG,           1, 1, BUSTER_IR_INTS, IR_RULE_SAME, 0) \
    X(IR_NOT,           1, 1, BUSTER_IR_INTS | BUSTER_IR_VECTORS | BUSTER_IR_MASKS, IR_RULE_SAME, 0) \
    X(IR_FADD,          3, 3, BUSTER_IR_FLOATS | BUSTER_IR_VECTORS, IR_RULE_SAME, 0) \
    X(IR_FSUB,          3, 3, BUSTER_IR_FLOATS | BUSTER_IR_VECTORS, IR_RULE_SAME, 0) \
    X(IR_FMUL,          3, 3, BUSTER_IR_FLOATS | BUSTER_IR_VECTORS, IR_RULE_SAME, 0) \
    X(IR_FDIV,          3, 3, BUSTER_IR_FLOATS | BUSTER_IR_VECTORS, IR_RULE_SAME, 0) \
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
    X(IR_BR_INDIRECT,   1, 7, 1u << IR_VOID, IR_RULE_INDIRECT, 1) \
    X(IR_VCONST,        0, 1, BUSTER_IR_VECTORS, IR_RULE_VECTOR_CONSTANT, 0) \
    X(IR_VCMP,          3, 7, BUSTER_IR_MASKS, IR_RULE_VECTOR_COMPARE, 0) \
    X(IR_VSELECT,       7, 7, BUSTER_IR_VECTORS, IR_RULE_VECTOR_SELECT, 0) \
    X(IR_LIFETIME,      0, 3, 1u << IR_VOID, IR_RULE_SLOT, 0) \
    X(IR_FENCE,         0, 1, 1u << IR_VOID, IR_RULE_FENCE, 0) \
    X(IR_INTRINSIC,     1, 7, (1u << IR_I32) | (1u << IR_I64) | BUSTER_IR_VECTORS, IR_RULE_INTRINSIC, 0) \
    X(IR_ATOMIC_RMW,    3, 7, (1u << IR_I8) | (1u << IR_I16) | (1u << IR_I32) | (1u << IR_I64), IR_RULE_ATOMIC_RMW, 0) \
    X(IR_MASKED_LOAD,   7, 7, BUSTER_IR_VECTORS, IR_RULE_MASKED_LOAD, 0) \
    X(IR_MASKED_STORE,  7, 7, 1u << IR_VOID, IR_RULE_MASKED_STORE, 0)

#define BUSTER_IR_OPERAND_ROW(op, uses, fields, types, rule, term) [op] = uses,
static const uint8_t ir_operand_table[BUSTER_IR_OP_TABLE] = { BUSTER_IR_ROWS(BUSTER_IR_OPERAND_ROW) };
#undef BUSTER_IR_OPERAND_ROW
// The operands that are values where they aren't BUSTER_IR_NONE, as ir_operand_table's bits: an address's index (b),
// a return's value (a), a call's destination for its aggregate result (c) and an atomic compare-exchange's desired
// value (c, #1059), and an intrinsic's second input (b, absent for splat alone).
static const uint8_t ir_optional_table[BUSTER_IR_OP_TABLE] = {
    [IR_ADDRESS] = 2,
    [IR_RET] = 1,
    [IR_CALL] = 4,
    [IR_CALL_INDIRECT] = 4,
    [IR_ATOMIC_RMW] = 4,
    [IR_INTRINSIC] = 2,
};
#define BUSTER_IR_FIELD_ROW(op, uses, fields, types, rule, term) [op] = fields,
static const uint8_t ir_field_table[BUSTER_IR_OP_TABLE] = { BUSTER_IR_ROWS(BUSTER_IR_FIELD_ROW) };
#undef BUSTER_IR_FIELD_ROW
#define BUSTER_IR_TYPE_ROW(op, uses, fields, types, rule, term) [op] = types,
static const uint32_t ir_result_table[BUSTER_IR_OP_TABLE] = { BUSTER_IR_ROWS(BUSTER_IR_TYPE_ROW) };
#undef BUSTER_IR_TYPE_ROW
#define BUSTER_IR_RULE_ROW(op, uses, fields, types, rule, term) [op] = rule,
static const uint8_t ir_rule_table[BUSTER_IR_OP_TABLE] = { BUSTER_IR_ROWS(BUSTER_IR_RULE_ROW) };
#undef BUSTER_IR_RULE_ROW
#define BUSTER_IR_TERM_ROW(op, uses, fields, types, rule, term) [op] = term,
static const uint8_t ir_terminator_table[BUSTER_IR_OP_TABLE] = { BUSTER_IR_ROWS(BUSTER_IR_TERM_ROW) };
#undef BUSTER_IR_TERM_ROW
#undef BUSTER_IR_ROWS
_Static_assert(IR_OP_COUNT <= BUSTER_IR_OP_TABLE, "IR opcode tables must fit two byte-permute registers");

static bool ir_vector_type(uint32_t type)
{
    const bool result = type >= IR_V4I32 && type <= IR_V16I32;
    return result;
}

static bool ir_mask_type(uint32_t type)
{
    const bool result = type >= IR_M8 && type <= IR_M64;
    return result;
}

static IrSimdShape ir_simd_shape(uint32_t type)
{
    const IrSimdShape result = type >= IR_V4I32 && type <= IR_M64 ? (IrSimdShape)(type - IR_V4I32 + IR_SIMD_V4I32) : IR_SIMD_NONE;
    return result;
}

static bool ir_splat_scalar_type(uint32_t type)
{
    return (type >= IR_I8 && type <= IR_I64) || type == IR_F32 || type == IR_F64;
}

// A bit operation's value (#1374, BUSTER_IR_INTRINSIC_TZCNT to BUSTER_IR_INTRINSIC_BSWAP) of `a`'s low `bits`, 32 or 64,
// which the interpreters of the IR and of the typed stream share: bsf's and bsr's of 0, unspecified, are the width, as
// tzcnt's and lzcnt's are.
static uint64_t ir_intrinsic_bits(uint32_t operation, uint32_t bits, uint64_t a)
{
    const uint64_t x = bits == 64 ? a : a & ((UINT64_C(1) << bits) - 1);
    uint64_t result = 0;
    switch (operation)
    {
        case BUSTER_IR_INTRINSIC_TZCNT:
        case BUSTER_IR_INTRINSIC_BSF: result = x == 0 ? bits : (uint64_t)__builtin_ctzll(x); break;
        case BUSTER_IR_INTRINSIC_LZCNT: result = x == 0 ? bits : (uint64_t)__builtin_clzll(x) - (64 - bits); break;
        case BUSTER_IR_INTRINSIC_BSR: result = x == 0 ? bits : 63 - (uint64_t)__builtin_clzll(x); break;
        case BUSTER_IR_INTRINSIC_POPCNT: result = (uint64_t)__builtin_popcountll(x); break;
        case BUSTER_IR_INTRINSIC_BSWAP: result = __builtin_bswap64(x) >> (64 - bits); break;
        default: break;
    }
    return result;
}

// A signed overflow's value (#1385, BUSTER_IR_INTRINSIC_SADDO to BUSTER_IR_INTRINSIC_SMULO) of `a` and `b`'s low `bits`, 32
// or 64, as signed integers, which the interpreters of the IR and of the typed stream share: 1 where the exact sum,
// difference or product leaves the range of `bits`, else 0.
static uint64_t ir_intrinsic_overflow(uint32_t operation, uint32_t bits, uint64_t a, uint64_t b)
{
    const uint32_t shift = 64 - bits;
    const int64_t x = (int64_t)(a << shift) >> shift;
    const int64_t y = (int64_t)(b << shift) >> shift;
    const __int128 exact = operation == BUSTER_IR_INTRINSIC_SADDO ? (__int128)x + y : operation == BUSTER_IR_INTRINSIC_SSUBO ? (__int128)x - y : (__int128)x * y;
    const __int128 limit = (__int128)1 << (bits - 1);
    return exact < -limit || exact >= limit ? 1 : 0;
}

// A splat copies bits, never evaluates a floating value. The independent lane model writes every lane and keeps
// the bytes past the vector's width zero. A failed request leaves the caller's result unchanged.
static bool ir_splat_value(uint32_t type, uint32_t scalar_type, uint64_t bits, IrSimdValue* result)
{
    const bool valid = ir_vector_type(type) && ir_splat_scalar_type(scalar_type) && result != NULL;
    if (valid)
    {
        IrSimdValue value = {0};
        const uint32_t lane_bits = ir_type_bits[scalar_type];
        const uint32_t lanes = ir_type_bits[type] / lane_bits;
        for (uint32_t lane = 0; lane < lanes; lane += 1)
        {
            ir_simd_lane_write(&value, lane_bits, lane, bits);
        }
        *result = value;
    }
    return valid;
}

// A vector operation's lanes (#141's element types), its `flags`: an IrSimdLane. The IR's vector types are containers of
// 16, 32 or 64 bytes whatever their elements (the typed stream's VECTOR class is bytes alone, so loads, stores, copies,
// phis, selects and calls carry a vector as its bytes), and each operation that reads its operands as elements names
// them: add, sub, mul and the shifts integer lanes of 8, 16, 32 or 64 bits, fadd, fsub, fmul and fdiv floats or doubles,
// and vcmp and vselect any of them. I32 (0) is the default, so that the v4i32, v8i32 and v16i32 operations before the
// lanes keep their meaning; every other operation's flags stay 0 (a load's and a store's are their access's).
static IrSimdOperation ir_lane_operation(uint32_t op)
{
    IrSimdOperation result = IR_SIMD_OPERATION_COUNT;
    switch (op)
    {
        case IR_ADD: result = IR_SIMD_ADD; break;
        case IR_SUB: result = IR_SIMD_SUB; break;
        case IR_MUL: result = IR_SIMD_MUL; break;
        case IR_SHL: result = IR_SIMD_SHL; break;
        case IR_LSHR: result = IR_SIMD_LSHR; break;
        case IR_ASHR: result = IR_SIMD_ASHR; break;
        case IR_FADD: result = IR_SIMD_FADD; break;
        case IR_FSUB: result = IR_SIMD_FSUB; break;
        case IR_FMUL: result = IR_SIMD_FMUL; break;
        case IR_FDIV: result = IR_SIMD_FDIV; break;
        case IR_VCMP: result = IR_SIMD_COMPARE; break;
        case IR_VSELECT: result = IR_SIMD_SELECT; break;
        default: break;
    }
    return result;
}

// Whether instruction flags `lane` are valid lanes of `op` on a vector: lanes its operation reads (ir_simd_lane_valid).
static bool ir_lane_valid(uint32_t op, uint32_t lane)
{
    const IrSimdOperation operation = ir_lane_operation(op);
    return operation != IR_SIMD_OPERATION_COUNT && lane < IR_SIMD_LANE_COUNT && ir_simd_lane_valid(operation, (IrSimdLane)lane);
}

// Whether a permute's flags (#1840, BUSTER_IR_INTRINSIC_PERMUTE to _SHUFFLE_LANES) are its kind's at vector type `type`:
// permute's lanes' bytes' log2 (0 to 3), shuffle's 0, align's lanes' bytes' log2 (2 or 3) in bits 4 and 5 and its count,
// below its lanes, in the low 4, and shuffle_lanes' immediate, of a 32-byte vector's two lanes' bits or a 64-byte one's.
static bool ir_permute_flags_valid(uint32_t kind, uint32_t type, uint32_t flags)
{
    const uint32_t bits = ir_type_bits[type];
    const uint32_t size = flags >> 4;
    const bool valid = kind == BUSTER_IR_INTRINSIC_PERMUTE ? flags <= 3 :
                       kind == BUSTER_IR_INTRINSIC_SHUFFLE ? flags == 0 :
                       kind == BUSTER_IR_INTRINSIC_ALIGN   ? (size == 2 || size == 3) && (flags & 15) < bits / (8u << size) :
                                                             (bits == 256 && flags < 4) || bits == 512;
    return valid;
}

// Whether a conversion's flags (#1840, BUSTER_IR_INTRINSIC_EXTEND and _TRUNCATE) are valid from vector type `from` to
// vector type `type`: an extend's to wider lanes, zero or sign, of no more of the source's bytes than it has; a truncate's
// to narrower lanes, of every one of the source's lanes, wrapping or saturating, into 16 bytes or the lanes' bytes if more.
static bool ir_convert_flags_valid(uint32_t kind, uint32_t from, uint32_t type, uint32_t flags)
{
    const uint32_t source = flags & 3;
    const uint32_t result = (flags >> 2) & 3;
    const uint32_t mode = flags >> 4;
    bool valid = false;
    if (kind == BUSTER_IR_INTRINSIC_EXTEND)
    {
        const uint32_t lanes = ir_type_bits[type] / (8u << result);
        valid = result > source && mode <= 1 && lanes * (8u << source) <= ir_type_bits[from];
    }
    else
    {
        const uint32_t lanes = ir_type_bits[from] / (8u << source);
        const uint32_t bits = lanes * (8u << result);
        valid = result < source && mode <= 2 && ir_type_bits[type] == (bits > 128 ? bits : 128);
    }
    return valid;
}

// Whether a scalar shift's flags (#1840, BUSTER_IR_INTRINSIC_SHIFT) are valid: lanes of 2, 4 or 8 bytes, and a left, a
// logical right or an arithmetic right shift.
static bool ir_shift_flags_valid(uint32_t flags)
{
    const uint32_t lanes = flags & 3;
    const uint32_t shift = (flags >> 2) & 3;
    return lanes != 0 && shift <= 2 && (flags >> 4) == 0;
}

// Whether a lane operation's flags (#1840, BUSTER_IR_INTRINSIC_LANE_OP) are valid: a minimum or a maximum of any integer
// lanes, a saturating sum or difference of bytes or words.
static bool ir_lane_op_flags_valid(uint32_t flags)
{
    const uint32_t operation = (flags >> 2) & 7;
    return (flags >> 5) == 0 && (operation < 4 || (flags & 3) <= 1);
}

// The lanes whose bytes' log2 an intrinsic's flags hold (#1840, compress and expand): bytes to quadwords.
static const uint8_t ir_intrinsic_lanes[4] = { IR_SIMD_LANE_I8, IR_SIMD_LANE_I16, IR_SIMD_LANE_I32, IR_SIMD_LANE_I64 };

// An intrinsic's kind (#1840) from its `c`, and whether that kind has a third operand in the extra array.
static uint32_t ir_intrinsic_kind(uint32_t c)
{
    return c & BUSTER_IR_INTRINSIC_KIND;
}

static bool ir_intrinsic_ternary(uint32_t c)
{
    const uint32_t kind = ir_intrinsic_kind(c);
    return kind >= BUSTER_IR_INTRINSIC_TERNARY && kind < BUSTER_IR_INTRINSIC_TERNARY_END;
}

// A ternary intrinsic's `c` for kind `kind` (a `c` whose kind bits are taken alone) with its third operand at
// extra[extra], and the index of that operand in a ternary intrinsic's extra array. An index past
// BUSTER_IR_INTRINSIC_EXTRA_LIMIT gives a `c` the verifier refuses (IR_ERROR_OPERAND), never another operand's.
static uint32_t ir_intrinsic_ternary_c(uint32_t kind, uint32_t extra)
{
    return extra <= BUSTER_IR_INTRINSIC_EXTRA_LIMIT ? ir_intrinsic_kind(kind) | extra << BUSTER_IR_INTRINSIC_EXTRA_SHIFT : UINT32_MAX;
}

static uint32_t ir_intrinsic_extra(uint32_t c)
{
    return c >> BUSTER_IR_INTRINSIC_EXTRA_SHIFT;
}

// A ternary intrinsic's third operand (#1840), instruction i's.
static uint32_t ir_intrinsic_third(const IrFunction* f, uint32_t i)
{
    return f->extra[ir_intrinsic_extra(f->c[i])];
}

// Whether instruction i is an intrinsic with a third operand.
static bool ir_ternary_instruction(const IrFunction* f, uint32_t i)
{
    return f->op[i] == IR_INTRINSIC && ir_intrinsic_ternary(f->c[i]);
}

// The mask type of a comparison of vector type `type`'s lanes `lane`, a bit per lane (m8 for 8 lanes or fewer): IR_VOID
// for a type that isn't a vector.
static uint32_t ir_lane_mask(uint32_t type, uint32_t lane)
{
    const IrSimdShape shape = lane < IR_SIMD_LANE_COUNT ? ir_simd_lane_mask(ir_simd_shape(type), (IrSimdLane)lane) : IR_SIMD_NONE;
    const uint32_t result = shape == IR_SIMD_NONE ? IR_VOID : IR_M8 + (uint32_t)(shape - IR_SIMD_M8);
    return result;
}

// How many lanes `lane` vector type `type` has.
static uint32_t ir_lane_count(uint32_t type, uint32_t lane)
{
    const uint32_t bits = lane < IR_SIMD_LANE_COUNT ? ir_simd_lane_bits((IrSimdLane)lane) : 0;
    return ir_vector_type(type) && bits != 0 ? ir_type_bits[type] / bits : 0;
}

#endif
// How many of this thread's scans for SIMD values or vectors (ir_function_has_simd_path, ir_function_has_vectors_path)
// took the vector path: test-tier1-masks and test-ir-simd read it to check that a scalar run takes each scan's scalar
// twin, as --scalar promises (#141's review). main.c's units share it, as lane.c's arena_small_floor, so that a command
// whose scans run in a home's copy of the code (ir_source's, the checker's) counts them in the same variable
// (./build.sh check-call-graph's split state).
#if defined(BUSTER_UNIT) && BUSTER_UNIT != BUSTER_UNIT_DRIVER
extern _Thread_local uint64_t ir_scan_vector_paths;
#elif defined(BUSTER_UNIT)
_Thread_local uint64_t ir_scan_vector_paths;
#else
static _Thread_local uint64_t ir_scan_vector_paths;
#endif

#ifndef BUSTER_IR_STORAGE_ONLY
// Consumers call only after structural verification. Refuse the entire function before allocating/rebuilding
// scalar-only state, including SIMD ABI metadata and unused vector payloads rather than only executable rows. The
// instructions' types and operations, the scan's bulk, go 64 at a time on the vector path and one at a time on its scalar
// twin, which must agree; the other columns are a few entries each.
static bool ir_function_has_simd_path(const IrFunction* function, bool vector)
{
    (void)vector;

    // A machine row (#141) is the IR's vectors' and masks' too, as opaque to the passes that take no SIMD value.
    bool found = function->vector_constant_count != 0 || function->row_count != 0 || function->result_type >= IR_V4I32;
    if (BUSTER_VECTOR_PATH(vector))
    {
        ir_scan_vector_paths += 1;
        for (uint32_t first = 0; !found && first < function->instruction_count; first += 64)
        {
            const uint32_t count = function->instruction_count - first < 64 ? function->instruction_count - first : 64;
            const __mmask64 active = count == 64 ? UINT64_MAX : (UINT64_C(1) << count) - 1;
            const __m512i types = _mm512_maskz_loadu_epi8(active, function->type + first);
            const __m512i ops = _mm512_maskz_loadu_epi8(active, function->op + first);
            const __mmask64 simd = _mm512_mask_cmpge_epu8_mask(active, types, _mm512_set1_epi8(IR_V4I32)) | _mm512_mask_cmpeq_epi8_mask(active, ops, _mm512_set1_epi8(IR_VCONST)) |
                                   _mm512_mask_cmpeq_epi8_mask(active, ops, _mm512_set1_epi8(IR_VCMP)) | _mm512_mask_cmpeq_epi8_mask(active, ops, _mm512_set1_epi8(IR_VSELECT));
            found = simd != 0;
        }
    }
    else
    {
        for (uint32_t i = 0; !found && i < function->instruction_count; i += 1)
        {
            found = function->type[i] >= IR_V4I32 || function->op[i] == IR_VCONST || function->op[i] == IR_VCMP || function->op[i] == IR_VSELECT;
        }
    }
    for (uint32_t i = 0; !found && i < function->parameter_count; i += 1)
    {
        found = function->parameter_types[i] >= IR_V4I32;
    }
    for (uint32_t i = 0; !found && i < function->call_type_count; i += 1)
    {
        found = function->call_types[i] >= IR_V4I32;
    }
    for (uint32_t i = 0; !found && i < function->call_count; i += 1)
    {
        found = function->calls[i].result_type >= IR_V4I32;
    }
    for (uint32_t i = 0; !found && i < function->slot_count; i += 1)
    {
        found = function->slots[i].type >= IR_V4I32;
    }
    return found;
}

// Whether a function has vectors (#141's, which the SSA promoter, the optimizers and Tier 1's selector refuse): a vector
// type anywhere, its constants or its operations, in its instructions, its parameters, calls and slots. A function whose
// only SIMD values are masks (#179) has none. The instructions 64 at a time on the vector path, one at a time on its
// scalar twin, which must agree.
static bool ir_function_has_vectors_path(const IrFunction* function, bool vector)
{
    (void)vector;

    bool found = function->vector_constant_count != 0 || function->row_count != 0 || ir_vector_type(function->result_type);
    if (BUSTER_VECTOR_PATH(vector))
    {
        ir_scan_vector_paths += 1;
        for (uint32_t first = 0; !found && first < function->instruction_count; first += 64)
        {
            const uint32_t count = function->instruction_count - first < 64 ? function->instruction_count - first : 64;
            const __mmask64 active = count == 64 ? UINT64_MAX : (UINT64_C(1) << count) - 1;
            const __m512i types = _mm512_maskz_loadu_epi8(active, function->type + first);
            const __m512i ops = _mm512_maskz_loadu_epi8(active, function->op + first);
            const __mmask64 vectors = _mm512_mask_cmpge_epu8_mask(active, types, _mm512_set1_epi8(IR_V4I32)) & ~_mm512_mask_cmpge_epu8_mask(active, types, _mm512_set1_epi8(IR_V16I32 + 1));
            const __mmask64 operations = _mm512_mask_cmpeq_epi8_mask(active, ops, _mm512_set1_epi8(IR_VCONST)) | _mm512_mask_cmpeq_epi8_mask(active, ops, _mm512_set1_epi8(IR_VCMP)) |
                                         _mm512_mask_cmpeq_epi8_mask(active, ops, _mm512_set1_epi8(IR_VSELECT));
            found = (vectors | operations) != 0;
        }
    }
    else
    {
        for (uint32_t i = 0; !found && i < function->instruction_count; i += 1)
        {
            found = ir_vector_type(function->type[i]) || function->op[i] == IR_VCONST || function->op[i] == IR_VCMP || function->op[i] == IR_VSELECT;
        }
    }
    for (uint32_t i = 0; !found && i < function->parameter_count; i += 1)
    {
        found = ir_vector_type(function->parameter_types[i]);
    }
    for (uint32_t i = 0; !found && i < function->call_type_count; i += 1)
    {
        found = ir_vector_type(function->call_types[i]);
    }
    for (uint32_t i = 0; !found && i < function->call_count; i += 1)
    {
        found = ir_vector_type(function->calls[i].result_type);
    }
    for (uint32_t i = 0; !found && i < function->slot_count; i += 1)
    {
        found = ir_vector_type(function->slots[i].type);
    }
    return found;
}

static __attribute__((unused)) bool ir_function_has_vectors(const IrFunction* function)
{
#if !BUSTER_HOST_VECTOR
    return ir_function_has_vectors_path(function, false);
#else
    return ir_function_has_vectors_path(function, true);
#endif
}

static __attribute__((unused)) bool ir_function_has_simd_scalar(const IrFunction* function);

static __attribute__((unused)) bool ir_function_has_simd(const IrFunction* function)
{
#if !BUSTER_HOST_VECTOR
    return ir_function_has_simd_scalar(function);
#else
    return ir_function_has_simd_path(function, true);
#endif
}

static __attribute__((unused)) bool ir_function_has_simd_scalar(const IrFunction* function)
{
    return ir_function_has_simd_path(function, false);
}

static IrCheck ir_check(IrError error, uint32_t instruction, uint32_t detail)
{
    IrCheck result = {
        .error = error,
        .instruction = instruction,
        .detail = detail,
    };
    return result;
}

// Inlined into the verifier's per-instruction oracle, which every constant takes: as a call, its growth past the
// inliner's budget cost --tier1 -c of sqlite3.c 0.47% more instructions (#141's intrinsics, buster-performance's
// tier1-intrinsics-141-2026-11-03).
static inline __attribute__((always_inline)) bool ir_constant_fits(IrConstant value, uint8_t type)
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

// The IR type of a machine row's operand or result (#141, IrFunction.rows) of the typed stream's class `class` and, a
// vector's or a mask's, `bytes`: an integer's of its bytes, whatever its signedness, a float's or a double's, a
// vector's container of its bytes (v4i32, v8i32 or v16i32, whatever its elements: the row's records give its bits their
// meaning; an SSE carrier's of 4 or 8 bytes f32 or f64, #1280, ir_typed_stack_type), a mask's of its bytes, and void
// for a void result; IR_TYPE_COUNT for a class a row call doesn't take: a long double, a 128-bit integer, and a vector
// of any other width.
static uint32_t ir_row_type(uint32_t class, uint32_t bytes)
{
    uint32_t type = IR_TYPE_COUNT;
    switch (class)
    {
        case BUSTER_TYPED_CLASS_VOID: type = IR_VOID; break;
        case BUSTER_TYPED_CLASS_I8:
        case BUSTER_TYPED_CLASS_U8: type = IR_I8; break;
        case BUSTER_TYPED_CLASS_I16:
        case BUSTER_TYPED_CLASS_U16: type = IR_I16; break;
        case BUSTER_TYPED_CLASS_I32:
        case BUSTER_TYPED_CLASS_U32: type = IR_I32; break;
        case BUSTER_TYPED_CLASS_I64:
        case BUSTER_TYPED_CLASS_U64: type = IR_I64; break;
        case BUSTER_TYPED_CLASS_F32: type = IR_F32; break;
        case BUSTER_TYPED_CLASS_F64: type = IR_F64; break;
        case BUSTER_TYPED_CLASS_VECTOR:
            type = bytes == 4 ? IR_F32 : bytes == 8 ? IR_F64 : bytes == 16 ? IR_V4I32 : bytes == 32 ? IR_V8I32 : bytes == 64 ? IR_V16I32 : IR_TYPE_COUNT;
            break;
        case BUSTER_TYPED_CLASS_MASK: type = bytes == 1 ? IR_M8 : bytes == 2 ? IR_M16 : bytes == 4 ? IR_M32 : bytes == 8 ? IR_M64 : IR_TYPE_COUNT; break;
        default: break;
    }
    return type;
}

// Whether a row call's descriptor fits its row (#141): within the row's columns, no ABI records and no variadic
// arguments, one fixed argument per operand, each of its operand's type, and the call's result the row's.
static bool ir_row_call_valid(const IrFunction* f, const IrCall* call)
{
    const Tier0Operation* row = call->row <= f->row_count ? &f->rows[call->row - 1] : 0;
    bool valid = row != 0 && row->record_count <= BUSTER_TIER0_OPERATION_RECORDS && row->operand_count <= BUSTER_TIER0_OPERATION_OPERANDS && call->abi == 0 && !call->variadic &&
                 call->argument_count == row->operand_count && call->parameter_count == row->operand_count &&
                 ir_row_type(row->result_class, row->result_bytes) == call->result_type;
    for (uint32_t j = 0; valid && j < row->operand_count; j += 1)
    {
        const uint32_t type = ir_row_type(row->operand_classes[j], row->operand_bytes[j]);
        valid = type != IR_VOID && type == f->call_types[call->type_first + j];
    }
    return valid;
}

// A body call's block argument (#1839, IrAsmBody): its first, where it has more arguments than register operands, or
// BUSTER_IR_NONE.
static uint32_t ir_asm_body_block(const IrFunction* f, const IrCall* call, const IrAsmBody* body)
{
    return call->argument_count > body->passed ? f->extra[call->argument_first] : BUSTER_IR_NONE;
}

// Whether a body call's descriptor fits its asm body (#1839): within the bodies, of no row, no ABI records and no variadic
// arguments, void, and of one ptr argument, its block's address, or of none, then one argument of a scalar type per
// register operand (#2144), each in a register; and the body's branch columns in order, each branch a record of it and
// each target one of its records or its end.
static bool ir_body_call_valid(const IrFunction* f, const IrCall* call)
{
    const IrAsmBody* body = call->body <= f->asm_body_count ? &f->asm_bodies[call->body - 1] : 0;
    const bool block = body != 0 && call->argument_count > body->passed;
    bool valid = body != 0 && call->row == 0 && call->abi == 0 && !call->variadic && call->result_type == IR_VOID && call->argument_count - body->passed <= 1 &&
                 call->argument_count >= body->passed && call->parameter_count == call->argument_count && (!block || f->call_types[call->type_first] == IR_PTR) &&
                 (body->passed == 0 || body->locations != 0) &&
                 (body->count == 0 || body->records != 0) && body->branch_count <= body->count && (body->branch_count == 0 || (body->branches != 0 && body->targets != 0));
    for (uint32_t k = 0; valid && k < body->passed; k += 1)
    {
        const uint8_t type = f->call_types[call->type_first + (block ? 1 : 0) + k];
        valid = body->locations[k].kind == BUSTER_ABI_LOCATION_REGISTERS && type != IR_VOID && type < IR_TYPE_COUNT;
    }
    for (uint32_t k = 0; valid && k < body->branch_count; k += 1)
    {
        valid = body->branches[k] < body->count && (k == 0 || body->branches[k - 1] < body->branches[k]) && body->targets[k] <= body->count;
    }
    return valid;
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
    if (check.error == IR_OK && ((f->vector_constant_count != 0 && f->vector_constants == NULL) || (f->row_count != 0 && f->rows == NULL) ||
                                 (f->asm_body_count != 0 && f->asm_bodies == NULL)))
    {
        check.error = IR_ERROR_STRUCTURE;
    }
    const IrDebugInfo* const debug = f->debug;
    if (check.error == IR_OK && debug != NULL && (debug->instruction_count != n || debug->slot_count != f->slot_count ||
        (n != 0 && debug->origins == NULL) || (f->slot_count != 0 && debug->locals == NULL) || debug->scalar_count > debug->local_count ||
        (debug->scalar_count != 0 && debug->scalars == NULL) || debug->first_block_end > debug->entry_count ||
        debug->binding_count > (uint64_t)debug->entry_count + f->parameter_count ||
        (debug->binding_count != 0 && (debug->bindings == NULL || debug->binding_flags == NULL)) ||
        debug->region_count > debug->entry_count || (debug->region_count != 0 && (debug->regions == NULL || debug->binding_flags == NULL))))
    {
        check.error = IR_ERROR_METADATA;
    }
    for (uint32_t i = 0; check.error == IR_OK && debug != NULL && i < n; i += 1)
    {
        if (debug->origins[i] != BUSTER_IR_NONE && debug->origins[i] >= debug->entry_count)
        {
            check = ir_check(IR_ERROR_METADATA, i, debug->origins[i]);
        }
    }
    for (uint32_t s = 0; check.error == IR_OK && debug != NULL && s < f->slot_count; s += 1)
    {
        if (debug->locals[s] != BUSTER_IR_NONE && debug->locals[s] >= debug->local_count)
        {
            check = ir_check(IR_ERROR_METADATA, BUSTER_IR_NONE, s);
        }
    }
    for (uint32_t s = 0; check.error == IR_OK && debug != NULL && s < debug->scalar_count; s += 1)
    {
        const IrDebugScalar scalar = debug->scalars[s];
        const bool kind = scalar.kind == IR_DEBUG_PARAMETER ? scalar.local < f->parameter_count && scalar.entry == BUSTER_IR_NONE :
                          scalar.kind == IR_DEBUG_CONSTANT && scalar.entry < debug->entry_count;
        if (!kind || scalar.local >= debug->local_count || (s != 0 && scalar.local <= debug->scalars[s - 1].local) ||
            (scalar.bytes != 1 && scalar.bytes != 2 && scalar.bytes != 4 && scalar.bytes != 8))
        {
            check = ir_check(IR_ERROR_METADATA, BUSTER_IR_NONE, s);
        }
    }
    for (uint32_t local = 0; check.error == IR_OK && debug != NULL && debug->binding_flags != NULL && local < debug->local_count; local += 1)
    {
        const uint32_t flags = debug->binding_flags[local];
        if (flags != 0 && flags != BUSTER_IR_DEBUG_BIND_ENTRY && flags != BUSTER_IR_DEBUG_BIND_REGIONS)
        {
            check = ir_check(IR_ERROR_METADATA, BUSTER_IR_NONE, local);
        }
    }
    for (uint32_t k = 0; check.error == IR_OK && debug != NULL && k < debug->binding_count; k += 1)
    {
        const IrDebugBinding binding = debug->bindings[k];
        const bool initial = binding.entry == BUSTER_IR_NONE;
        const bool ordered = k == 0 || (initial ? debug->bindings[k - 1].entry == BUSTER_IR_NONE && debug->bindings[k - 1].local < binding.local :
                                      debug->bindings[k - 1].entry == BUSTER_IR_NONE || debug->bindings[k - 1].entry < binding.entry);
        const bool scalar = binding.type == IR_I8 || binding.type == IR_I16 || binding.type == IR_I32 || binding.type == IR_I64 ||
                            binding.type == IR_PTR || binding.type == IR_F32 || binding.type == IR_F64;
        const bool width = scalar && binding.bytes * 8u == ir_type_bits[binding.type];
        const bool unavailable = binding.kind == IR_DEBUG_BIND_UNAVAILABLE;
        const bool value = binding.kind == IR_DEBUG_BIND_VALUE && binding.value < n && f->type[binding.value] == binding.type;
        const bool kind = value || binding.kind == IR_DEBUG_BIND_CONSTANT || unavailable;
        const bool anchor = binding.anchor == BUSTER_IR_NONE || (binding.anchor < n && f->type[binding.anchor] == binding.type);
        const bool local = binding.local < debug->local_count;
        const bool entry = initial ? binding.local < f->parameter_count : binding.entry < debug->entry_count;
        const bool admitted = local && (debug->binding_flags[binding.local] & BUSTER_IR_DEBUG_BIND_ENTRY) != 0;
        if (!kind || !local || !entry || !ordered || !anchor || (!width && !(unavailable && binding.bytes == 0 && binding.type == IR_VOID)) ||
            (!value && binding.value != BUSTER_IR_NONE) || (admitted && !initial && binding.entry >= debug->first_block_end))
        {
            check = ir_check(IR_ERROR_METADATA, BUSTER_IR_NONE, k);
        }
    }
    for (uint32_t k = 0; check.error == IR_OK && debug != NULL && k < debug->region_count; k += 1)
    {
        const IrDebugRegion region = debug->regions[k];
        const bool local = region.local < debug->local_count && debug->binding_flags[region.local] == BUSTER_IR_DEBUG_BIND_REGIONS;
        const bool scalar = region.type == IR_I8 || region.type == IR_I16 || region.type == IR_I32 || region.type == IR_I64 ||
                            region.type == IR_PTR || region.type == IR_F32 || region.type == IR_F64;
        const bool width = scalar && region.bytes * 8u == ir_type_bits[region.type];
        const bool value = region.kind == IR_DEBUG_BIND_VALUE && region.value < n && f->type[region.value] == region.type;
        const bool kind = value || region.kind == IR_DEBUG_BIND_CONSTANT || region.kind == IR_DEBUG_BIND_UNAVAILABLE;
        const bool block = region.block < f->block_count || (region.block == BUSTER_IR_NONE && region.kind == IR_DEBUG_BIND_UNAVAILABLE);
        const bool ordered = k == 0 || debug->regions[k - 1].local < region.local ||
                             (debug->regions[k - 1].local == region.local && debug->regions[k - 1].end <= region.begin);
        const bool next = region.next == BUSTER_IR_NONE || (region.next < debug->binding_count &&
                          debug->bindings[region.next].local == region.local && debug->bindings[region.next].entry != BUSTER_IR_NONE &&
                          debug->bindings[region.next].entry >= region.end && debug->bindings[region.next].entry < region.block_end);
        if (!local || !width || !kind || !block || !ordered || !next || region.begin >= region.end || region.end > region.block_end || region.block_end > debug->entry_count ||
            (!value && region.value != BUSTER_IR_NONE))
        {
            check = ir_check(IR_ERROR_METADATA, BUSTER_IR_NONE, k);
        }
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
            (call.abi != 0 && (abi == 0 || !ir_slice(abi->passed, call.argument_count, f->passed_count) || (abi->result_size != 0 && call.result_type != IR_VOID))) ||
            (call.row != 0 && !ir_row_call_valid(f, &call)) || (call.body != 0 && !ir_body_call_valid(f, &call)))
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
    else if (((op == IR_LOAD || op == IR_STORE) && (!ir_access_flags_valid(op, f->flags[i]) || f->alignment_log2[i] > 31)) ||
             (op == IR_ATOMIC_RMW && (!ir_rmw_flags_valid(f->flags[i]) || f->alignment_log2[i] > 31)) ||
             ((op == IR_MASKED_LOAD || op == IR_MASKED_STORE) && (!ir_masked_lanes_valid(f->flags[i]) || f->alignment_log2[i] > 31)) ||
             ((op != IR_LOAD && op != IR_STORE && op != IR_ATOMIC_RMW && op != IR_MASKED_LOAD && op != IR_MASKED_STORE) && ((f->flags[i] != 0 && op != IR_INTRINSIC && !ir_lane_valid(op, f->flags[i]) && (op != IR_STACK_SAVE || f->flags[i] != BUSTER_IR_STACK_SAVE_FRAME)) ||
              f->alignment_log2[i] != 0)))
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
            // A vector's arithmetic reads lanes its operation takes (ir_lane_valid: fadd's floats or doubles, never the
            // default I32), and the bitwise operations none; a scalar's, none.
            case IR_RULE_SAME:
                valid = ta == type && ((ir_operand_table[op] & 2) == 0 || tb == type) &&
                        (ir_vector_type(type) && ir_lane_operation(op) != IR_SIMD_OPERATION_COUNT ? ir_lane_valid(op, f->flags[i]) : f->flags[i] == 0);
                break;
            // A vector's shift (#141's element types) is each lane's by the same lane of b, a vector of its type.
            case IR_RULE_SHIFT: valid = ta == type && (ir_vector_type(type) ? tb == type : ir_integer(tb) && f->flags[i] == 0); break;
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
            // Bits for bits of the same width; a mask (#179) only to or from an integer of its width (i8 to i64, the
            // boundary an opmask's kmov to and from a general register is), or to itself.
            case IR_RULE_BITCAST:
                valid = ir_type_bits[ta] == ir_type_bits[type] &&
                        ((!ir_mask_type(ta) && !ir_mask_type(type)) || ta == type || (ir_mask_type(ta) && ir_integer(type)) || (ir_integer(ta) && ir_mask_type(type)));
                break;
            case IR_RULE_VECTOR_CONSTANT: valid = a < f->vector_constant_count && ir_simd_value_valid(f->vector_constants + a, ir_simd_shape(type)); break;
            // A mask of a bit per lane (ir_lane_mask); floating lanes' predicates C's alone (eq to ge), as fcmp's.
            case IR_RULE_VECTOR_COMPARE:
                valid = ir_vector_type(ta) && ta == tb && c < (ir_simd_lane_floating((IrSimdLane)f->flags[i]) ? IR_GE + 1u : (uint32_t)IR_PREDICATE_COUNT) &&
                        type == ir_lane_mask(ta, f->flags[i]);
                break;
            case IR_RULE_VECTOR_SELECT: valid = ir_vector_type(type) && ta == ir_lane_mask(type, f->flags[i]) && tb == type && operand_types[2] == type; break;
            case IR_RULE_SLOT: valid = a < f->slot_count && (op != IR_LIFETIME || b <= 1); break;
            case IR_RULE_FENCE: valid = (a & ~(BUSTER_IR_FENCE_ORDER | BUSTER_IR_FENCE_SIGNAL)) == 0 && (a & BUSTER_IR_FENCE_ORDER) <= BUSTER_IR_FENCE_SEQ_CST; break;
            case IR_RULE_INTRINSIC:
            {
                // An intrinsic's flags are its immediate: a width change's part (#1840), and 0 for every other kind.
                const uint32_t part = f->flags[i];
                if (c > BUSTER_IR_INTRINSIC_KIND || ir_intrinsic_ternary(c))
                {
                    // A ternary kind's third operand (#1840), in the extra array.
                    const uint32_t third = ir_intrinsic_ternary(c) && ir_slice(ir_intrinsic_extra(c), 1, f->extra_count) ? f->extra[ir_intrinsic_extra(c)] : BUSTER_IR_NONE;
                    if (b >= f->instruction_count || f->type[b] == IR_VOID || f->type[b] >= IR_TYPE_COUNT)
                    {
                        check = ir_check(IR_ERROR_OPERAND, i, 1);
                    }
                    else if (third >= f->instruction_count || f->type[third] == IR_VOID || f->type[third] >= IR_TYPE_COUNT)
                    {
                        check = ir_check(IR_ERROR_OPERAND, i, 3);
                    }
                    else
                    {
                        // permute2's flags its lanes' bytes' log2, b of its type; ternlog's any immediate, b of its type;
                        // compress's and expand's their lanes' bytes' log2, b the mask of those lanes.
                        const uint32_t kind = ir_intrinsic_kind(c);
                        const bool masked = kind == BUSTER_IR_INTRINSIC_COMPRESS || kind == BUSTER_IR_INTRINSIC_EXPAND;
                        const uint32_t expected_b = masked && part <= 3 ? ir_lane_mask(type, ir_intrinsic_lanes[part]) : type;
                        valid = ir_vector_type(type) && ta == type && f->type[b] == expected_b && f->type[third] == type && (kind == BUSTER_IR_INTRINSIC_TERNLOG || part <= 3);
                    }
                }
                else if (c == BUSTER_IR_INTRINSIC_EXTRACT || c == BUSTER_IR_INTRINSIC_WIDEN)
                {
                    const uint32_t narrow = c == BUSTER_IR_INTRINSIC_EXTRACT ? type : ta;
                    const uint32_t wide = c == BUSTER_IR_INTRINSIC_EXTRACT ? ta : type;
                    valid = ir_vector_type(type) && ir_vector_type(ta) && ir_type_bits[narrow] < ir_type_bits[wide] && b == BUSTER_IR_NONE &&
                            (c == BUSTER_IR_INTRINSIC_EXTRACT ? part < ir_type_bits[wide] / ir_type_bits[narrow] : part == 0);
                }
                else if (c == BUSTER_IR_INTRINSIC_INSERT)
                {
                    if (b >= f->instruction_count || f->type[b] == IR_VOID || f->type[b] >= IR_TYPE_COUNT)
                    {
                        check = ir_check(IR_ERROR_OPERAND, i, 1);
                    }
                    else
                    {
                        const uint32_t tb_part = f->type[b];
                        valid = ir_vector_type(type) && ta == type && ir_vector_type(tb_part) && ir_type_bits[tb_part] < ir_type_bits[type] &&
                                part < ir_type_bits[type] / ir_type_bits[tb_part];
                    }
                }
                else if (c == BUSTER_IR_INTRINSIC_EXTEND || c == BUSTER_IR_INTRINSIC_TRUNCATE)
                {
                    valid = ir_vector_type(type) && ir_vector_type(ta) && b == BUSTER_IR_NONE && ir_convert_flags_valid(c, ta, type, part);
                }
                else if (c >= BUSTER_IR_INTRINSIC_PERMUTE && c <= BUSTER_IR_INTRINSIC_SHUFFLE_LANES)
                {
                    if (b >= f->instruction_count || f->type[b] == IR_VOID || f->type[b] >= IR_TYPE_COUNT)
                    {
                        check = ir_check(IR_ERROR_OPERAND, i, 1);
                    }
                    else
                    {
                        valid = ir_vector_type(type) && ta == type && f->type[b] == type && ir_permute_flags_valid(c, type, part);
                    }
                }
                else if (c == BUSTER_IR_INTRINSIC_LANE_OP)
                {
                    if (b >= f->instruction_count || f->type[b] == IR_VOID || f->type[b] >= IR_TYPE_COUNT)
                    {
                        check = ir_check(IR_ERROR_OPERAND, i, 1);
                    }
                    else
                    {
                        valid = ir_vector_type(type) && ta == type && f->type[b] == type && ir_lane_op_flags_valid(part);
                    }
                }
                else if (c == BUSTER_IR_INTRINSIC_LANE_EXTRACT || c == BUSTER_IR_INTRINSIC_MOVEMASK)
                {
                    // One lane's zero-extended bits, or the lanes' sign bits, of a vector: a lane's index below its lanes.
                    const uint32_t size = part & 3;
                    const uint32_t lanes = ir_vector_type(ta) ? ir_type_bits[ta] / (8u << size) : 0;
                    valid = b == BUSTER_IR_NONE && lanes != 0 &&
                            (c == BUSTER_IR_INTRINSIC_LANE_EXTRACT ? type == (size == 3 ? IR_I64 : IR_I32) && (part >> 2) < lanes :
                                                                    type == IR_I32 && part != 1 && part <= 3 && ir_type_bits[ta] <= 256);
                }
                else if (c == BUSTER_IR_INTRINSIC_REDUCE)
                {
                    // A reduction of doublewords or quadwords into each lane of 16 bytes.
                    valid = b == BUSTER_IR_NONE && type == IR_V4I32 && ir_vector_type(ta) && (part & 3) >= 2 && (part >> 2) < BUSTER_IR_REDUCE_OPERATIONS;
                }
                else if (c == BUSTER_IR_INTRINSIC_LANE_UNARY)
                {
                    // A population count of any lanes; a leading zero count or conflicts of doublewords or quadwords.
                    valid = b == BUSTER_IR_NONE && ir_vector_type(type) && ta == type && (part >> 2) <= BUSTER_IR_LANE_UNARY_CONFLICT &&
                            ((part >> 2) == BUSTER_IR_LANE_UNARY_POPCOUNT || (part & 3) >= 2);
                }
                else if (c == BUSTER_IR_INTRINSIC_LANE_INSERT)
                {
                    if (b >= f->instruction_count || f->type[b] == IR_VOID || f->type[b] >= IR_TYPE_COUNT)
                    {
                        check = ir_check(IR_ERROR_OPERAND, i, 1);
                    }
                    else
                    {
                        const uint32_t size = part & 3;
                        valid = ir_vector_type(type) && ta == type && f->type[b] == (size == 3 ? IR_I64 : IR_I32) && (part >> 2) < ir_type_bits[type] / (8u << size);
                    }
                }
                else if (c == BUSTER_IR_INTRINSIC_AES)
                {
                    if (b >= f->instruction_count || f->type[b] == IR_VOID || f->type[b] >= IR_TYPE_COUNT)
                    {
                        check = ir_check(IR_ERROR_OPERAND, i, 1);
                    }
                    else
                    {
                        valid = ir_vector_type(type) && ta == type && f->type[b] == type && part <= 1;
                    }
                }
                else if (c == BUSTER_IR_INTRINSIC_SHIFT)
                {
                    if (b >= f->instruction_count || f->type[b] == IR_VOID || f->type[b] >= IR_TYPE_COUNT)
                    {
                        check = ir_check(IR_ERROR_OPERAND, i, 1);
                    }
                    else
                    {
                        valid = ir_vector_type(type) && ta == type && f->type[b] == IR_I32 && ir_shift_flags_valid(part);
                    }
                }
                else if (part != 0)
                {
                    valid = false;
                }
                else if (c == BUSTER_IR_INTRINSIC_SPLAT)
                {
                    valid = ir_vector_type(type) && ir_splat_scalar_type(ta) && b == BUSTER_IR_NONE;
                }
                else if (c >= BUSTER_IR_INTRINSIC_TZCNT && c <= BUSTER_IR_INTRINSIC_BSWAP)
                {
                    valid = (type == IR_I32 || type == IR_I64) && ta == type && b == BUSTER_IR_NONE;
                }
                else if (b >= f->instruction_count || f->type[b] == IR_VOID || f->type[b] >= IR_TYPE_COUNT)
                {
                    check = ir_check(IR_ERROR_OPERAND, i, 1);
                }
                else
                {
                    valid = (type == IR_I32 || type == IR_I64) && ta == type && f->type[b] == type &&
                            (c < BUSTER_IR_INTRINSIC_SPLAT || (c >= BUSTER_IR_INTRINSIC_SADDO && c <= BUSTER_IR_INTRINSIC_SMULO)) &&
                            (c != BUSTER_IR_INTRINSIC_MULHU || type == IR_I64);
                }
            } break;
            case IR_RULE_ATOMIC_RMW:
            {
                // A compare-exchange's desired value of its type (an optional use, which the operand loop leaves
                // unchecked), and the others' none.
                const bool compare = (f->flags[i] & BUSTER_IR_RMW_OPERATION) == BUSTER_IR_RMW_COMPARE;
                valid = ta == IR_PTR && tb == type && (compare ? c < f->instruction_count && f->type[c] == type : c == BUSTER_IR_NONE) &&
                        ir_atomic_access(type, f->alignment_log2[i]);
            } break;
            // A masked load's mask at its lanes and passthrough of its type, and a masked store's vector and mask so.
            case IR_RULE_MASKED_LOAD: valid = ta == IR_PTR && ir_vector_type(type) && tb == ir_lane_mask(type, f->flags[i]) && operand_types[2] == type; break;
            case IR_RULE_MASKED_STORE: valid = ta == IR_PTR && ir_vector_type(tb) && operand_types[2] == ir_lane_mask(tb, f->flags[i]); break;
            case IR_RULE_SYMBOL: valid = a != 0 && a < f->symbol_count; break;
            case IR_RULE_ADDRESS:
            {
                valid = ta == IR_PTR && c < f->address_count && (b == BUSTER_IR_NONE || (b < f->instruction_count && f->type[b] == IR_I64));
            } break;
            case IR_RULE_LOAD:
            case IR_RULE_STORE: valid = ta == IR_PTR && ((f->flags[i] & BUSTER_IR_ATOMIC) == 0 || ir_atomic_access(op == IR_LOAD ? type : tb, f->alignment_log2[i])); break;
            case IR_RULE_CALL:
            {
                // A row call (#141, IrCall.row) and a body call (#1839, IrCall.body) are a direct call's, of no symbol.
                valid = b < f->call_count && (op == IR_CALL ? (f->calls[b].row != 0 || f->calls[b].body != 0 ? a == 0 : a != 0 && a < f->symbol_count) :
                                                              ta == IR_PTR && f->calls[b].row == 0 && f->calls[b].body == 0);
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

// An opcode table's (BUSTER_IR_OP_TABLE bytes) entries of 16 opcodes, one vpermi2b of its two halves.
static __m512i ir_lookup_bytes(const uint8_t* table, __m128i op)
{
    return _mm512_cvtepu8_epi32(_mm512_castsi512_si128(_mm512_permutex2var_epi8(_mm512_loadu_si512(table), _mm512_zextsi128_si512(op), _mm512_loadu_si512(table + 64))));
}

// The types of one operand column's `valid` lanes, and zero in the others. `low` and `high` hold the 128 types from
// `base`, which end with the batch's own; `head` holds the function's first 64, its parameters' and its entry's
// constants'. A value in neither, rare in a producer's local code, is a load of its own (#786).
static __m512i ir_operand_types(const IrFunction* f, __m512i low, __m512i high, __m512i head, uint32_t base, __m512i value, __mmask16 valid)
{
    // A function of at most 128 instructions has all its types in the window, from 0.
    const __m512i local = f->instruction_count <= 2 * 64 ? value : _mm512_sub_epi32(value, _mm512_set1_epi32((int)base));
    const __mmask16 near = f->instruction_count <= 2 * 64 ? valid : valid & _mm512_cmplt_epu32_mask(local, _mm512_set1_epi32(2 * 64));
    __m512i result = _mm512_maskz_cvtepu8_epi32(near, _mm512_castsi512_si128(_mm512_permutex2var_epi8(low, _mm512_zextsi128_si512(_mm512_cvtepi32_epi8(local)), high)));
    const __mmask16 outside = valid & ~near;
    if (outside != 0)
    {
        const __mmask16 early = outside & _mm512_cmplt_epu32_mask(value, _mm512_set1_epi32(64));
        result = _mm512_mask_mov_epi32(result, early, _mm512_cvtepu8_epi32(_mm512_castsi512_si128(_mm512_permutexvar_epi8(_mm512_zextsi128_si512(_mm512_cvtepi32_epi8(value)), head))));
        uint32_t far = outside & ~early;
        if (far != 0)
        {
            uint32_t values[BUSTER_IR_VECTOR_COUNT];
            uint32_t types[BUSTER_IR_VECTOR_COUNT];
            _mm512_storeu_si512(values, value);
            const __mmask16 loaded = (__mmask16)far;
            while (far != 0)
            {
                const uint32_t lane = (uint32_t)__builtin_ctz(far);
                far &= far - 1;
                types[lane] = f->type[values[lane]];
            }
            result = _mm512_mask_loadu_epi32(result, loaded, types);
        }
    }
    return result;
}

// A bit for every row needing the scalar semantic check. The common arithmetic, comparison, selection and memory
// rules are checked here. Operand types are in-register byte permutes (ir_operand_types), whose invalid and unused
// lanes are zero as the scalar gather's were.
static __mmask16 ir_check_batch(const IrFunction* f, __m512i head, uint32_t first, uint32_t count, __mmask16* terminators)
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
    const __m512i operands = ir_lookup_bytes(ir_operand_table, op_bytes);
    const __m512i fields = ir_lookup_bytes(ir_field_table, op_bytes);
    const __m512i rules = ir_lookup_bytes(ir_rule_table, op_bytes);
    const __m512i terms = ir_lookup_bytes(ir_terminator_table, op_bytes);
    // The result types' 128 words, 32 opcodes per permute, chosen by the opcode's bits 5 and 6.
    const __m512i allowed_first = _mm512_permutex2var_epi32(_mm512_loadu_si512(ir_result_table), op, _mm512_loadu_si512(ir_result_table + 16));
    const __m512i allowed_second = _mm512_permutex2var_epi32(_mm512_loadu_si512(ir_result_table + 32), op, _mm512_loadu_si512(ir_result_table + 48));
    const __m512i allowed_third = _mm512_permutex2var_epi32(_mm512_loadu_si512(ir_result_table + 64), op, _mm512_loadu_si512(ir_result_table + 80));
    const __m512i allowed_fourth = _mm512_permutex2var_epi32(_mm512_loadu_si512(ir_result_table + 96), op, _mm512_loadu_si512(ir_result_table + 112));
    const __mmask16 odd = _mm512_test_epi32_mask(op, _mm512_set1_epi32(32));
    const __m512i allowed_low = _mm512_mask_blend_epi32(odd, allowed_first, allowed_second);
    const __m512i allowed_high = _mm512_mask_blend_epi32(odd, allowed_third, allowed_fourth);
    const __m512i allowed = _mm512_mask_blend_epi32(_mm512_test_epi32_mask(op, _mm512_set1_epi32(64)), allowed_low, allowed_high);
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
    const __mmask16 over_a = _mm512_cmpge_epu32_mask(a, limit);
    const __mmask16 over_b = _mm512_cmpge_epu32_mask(b, limit);
    const __mmask16 over_c = _mm512_cmpge_epu32_mask(c, limit);
    bad |= use_a & over_a;
    bad |= use_b & over_b;
    bad |= use_c & over_c;
    // The gather's lane condition, computed once: a used operand whose index is in bounds.
    const __mmask16 valid_a = use_a & ~over_a;
    const __mmask16 valid_b = use_b & ~over_b;
    const __mmask16 valid_c = use_c & ~over_c;
    const __mmask16 memory = _mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(IR_LOAD)) | _mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(IR_STORE));
    bad |= memory & (_mm512_test_epi32_mask(flags, _mm512_set1_epi32(~BUSTER_IR_VOLATILE)) | _mm512_cmpgt_epu32_mask(align, _mm512_set1_epi32(31)));
    bad |= ~memory & (_mm512_test_epi32_mask(flags, none) | _mm512_test_epi32_mask(align, none));
    // The 128 types that end with the batch's own: all of a function of at most 128 instructions.
    const uint32_t base = first + count > 2 * 64 ? first + count - 2 * 64 : 0;
    const uint32_t window = f->instruction_count - base;
    const __mmask64 low_types = window >= 64 ? (__mmask64)-1 : ((__mmask64)1 << window) - 1;
    const __mmask64 high_types = window >= 2 * 64 ? (__mmask64)-1 : window > 64 ? ((__mmask64)1 << (window - 64)) - 1 : 0;
    const __m512i types_low = _mm512_maskz_loadu_epi8(low_types, f->type + base);
    const __m512i types_high = _mm512_maskz_loadu_epi8(high_types, f->type + base + (window < 64 ? window : 64));
    const __m512i ta = ir_operand_types(f, types_low, types_high, head, base, a, valid_a);
    const __m512i tb = ir_operand_types(f, types_low, types_high, head, base, b, valid_b);
    const __m512i tc = ir_operand_types(f, types_low, types_high, head, base, c, valid_c);
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
    // Intrinsics have operation-specific operands: two integers for BMI/mulhu, one scalar for a vector splat.
    // Their uncommon rows take the scalar semantic check, including the optional second operand's bounds.
    // A vector's floating arithmetic (#141's element types) reads the lanes its flags name, which the scalar oracle checks
    // (a nonzero flag is deferred above; this is fadd's default I32, which no lanes are).
    const __mmask16 vector_result = _mm512_cmpge_epu32_mask(type, _mm512_set1_epi32(IR_V4I32)) & _mm512_cmple_epu32_mask(type, _mm512_set1_epi32(IR_V16I32));
    bad |= vector_result & _mm512_cmpge_epu32_mask(op, _mm512_set1_epi32(IR_FADD)) & _mm512_cmple_epu32_mask(op, _mm512_set1_epi32(IR_FDIV));
    const __mmask16 defer = ~(same | shift | icmp | fcmp | select | memory | no_rule);
    return active & (bad | defer);
}

// The verifier's and the derived arrays' hot functions (this one, ir_cfg_validated, ir_check_dominance, ir_verify_path
// and ir_uses_path) stay out of line and start lines of their own, so that where the code before them ends doesn't
// move their loops within lines, which changed their times between builds of the same code (#786).
static __attribute__((noinline, aligned(64))) IrCheck ir_check_rows(const IrFunction* f, bool vector)
{
    (void)vector;

    IrCheck check = ir_check(IR_OK, BUSTER_IR_NONE, 0);
    uint32_t block = 0;
    uint32_t first = 0;
    // The first 64 types, which only a function of more than 128 instructions looks up apart (ir_operand_types).
    __m512i head = _mm512_setzero_si512();
    if (BUSTER_VECTOR_PATH(vector) && f->instruction_count > 2 * 64)
    {
        head = _mm512_loadu_si512(f->type);
    }
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
            pending = ir_check_batch(f, head, first, count, &actual);
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

// A terminator's successor source by operation: none, one target in a, two in b and c, a switch's
// case list in extra, or a br_indirect's labels. The op-indexed read keeps each block's dispatch a
// load where the equivalent compare chain lowered to a jump table's indirect branch (#786).
static const uint8_t ir_successor_table[BUSTER_IR_OP_TABLE] = {
    [IR_BR] = 1,
    [IR_CBR] = 2,
    [IR_SWITCH] = 3,
    [IR_BR_INDIRECT] = 4,
};

static uint32_t ir_successor_count(const IrFunction* f, uint32_t i)
{
    const uint32_t kind = ir_successor_table[f->op[i]];
    return kind <= 2 ? kind : kind == 3 ? f->c[i] + 1 : (f->b[i] == BUSTER_IR_NONE ? f->label_count : f->c[i]);
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
static __attribute__((noinline, aligned(64))) IrError ir_cfg_validated(const IrFunction* f, Arena* arena, Arena* scratch, IrCfg* result)
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

// Instruction i's value in its `c` place: its c where its value mask has it, a ternary intrinsic's third operand (#1840),
// which the extra array holds, or BUSTER_IR_NONE; and the value mask with that place.
static uint32_t ir_value_third(const IrFunction* f, uint32_t i)
{
    return ir_ternary_instruction(f, i) ? ir_intrinsic_third(f, i) : (ir_value_mask(f, i) & 4) != 0 ? f->c[i] : BUSTER_IR_NONE;
}

static uint32_t ir_value_mask3(const IrFunction* f, uint32_t i)
{
    return ir_value_mask(f, i) | (ir_ternary_instruction(f, i) ? 4u : 0u);
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
    else if (ir_ternary_instruction(f, i))
    {
        result += 1;
    }
    return result;
}

// The places of a value mask's bits in order: an instruction's n-th direct operand is a, b or c by
// ir_operand_positions[mask][n], without a loop that clears the n bits before it (#786).
static const uint8_t ir_operand_positions[8][3] = {
    [1] = { 0 },
    [2] = { 1 },
    [3] = { 0, 1 },
    [4] = { 2 },
    [5] = { 0, 2 },
    [6] = { 1, 2 },
    [7] = { 0, 1, 2 },
};

static IrOperand ir_operand(const IrFunction* f, uint32_t i, uint32_t number, uint32_t block)
{
    const uint32_t mask = ir_value_mask(f, i);
    const uint32_t direct = (uint32_t)__builtin_popcount(mask);
    IrOperand result = {
        .block = block,
    };
    if (number < direct)
    {
        result.position = ir_operand_positions[mask][number];
        result.value = result.position == 0 ? f->a[i] : result.position == 1 ? f->b[i] : f->c[i];
    }
    else if (f->op[i] == IR_PHI)
    {
        const uint32_t p = number - direct;
        result.position = 3 + p;
        result.block = f->extra[f->a[i] + p * 2];
        result.value = f->extra[f->a[i] + p * 2 + 1];
    }
    else if (f->op[i] == IR_INTRINSIC)
    {
        result.position = 3;
        result.value = ir_intrinsic_third(f, i);
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

// The exact check of instruction i's operands, in ir_operand's order: each defined before its use within a block and,
// with a dominator tree, in a block that dominates the use's. A phi's incoming value is used at its predecessor's end.
// The scalar twin checks every instruction so, and the vector path the lanes its batch leaves to it.
static IrCheck ir_check_operands(const IrFunction* f, const IrCfg* cfg, const IrDominance* dominance, uint32_t i)
{
    IrCheck check = ir_check(IR_OK, BUSTER_IR_NONE, 0);
    const uint64_t count = ir_operand_count(f, i);
    const bool phi = f->op[i] == IR_PHI;
    for (uint32_t p = 0; check.error == IR_OK && p < count; p += 1)
    {
        const IrOperand operand = ir_operand(f, i, p, cfg->instruction_blocks[i]);
        const uint32_t definition = cfg->instruction_blocks[operand.value];
        const uint32_t position = phi ? f->block_offsets[operand.block + 1] - 1 : i;
        bool invalid = operand.value >= position;
        if (definition != operand.block)
        {
            invalid = dominance != 0 && (dominance->leave[definition] == 0 || dominance->leave[operand.block] == 0 || dominance->enter[definition] > dominance->enter[operand.block] || dominance->leave[definition] < dominance->leave[operand.block]);
        }
        if (invalid)
        {
            check = ir_check(IR_ERROR_DOMINANCE, i, operand.position);
        }
    }
    return check;
}

// The lanes of one operand column whose value is defined in an earlier block (`earlier`) and whose definition's block
// doesn't dominate the use's: its block and its tree interval are gathered, the use's are the lanes' own.
static __mmask16 ir_dominance_earlier(const IrCfg* cfg, const IrDominance* dominance, __mmask16 earlier, __m512i value, __m512i use_enter, __m512i use_leave)
{
    __mmask16 bad = 0;
    if (earlier != 0)
    {
        const __m512i zero = _mm512_setzero_si512();
        const __m512i block = _mm512_mask_i32gather_epi32(zero, earlier, value, cfg->instruction_blocks, 4);
        const __m512i enter = _mm512_mask_i32gather_epi32(zero, earlier, block, dominance->enter, 4);
        const __m512i leave = _mm512_mask_i32gather_epi32(zero, earlier, block, dominance->leave, 4);
        bad = _mm512_cmpeq_epi32_mask(leave, zero) | _mm512_cmpeq_epi32_mask(use_leave, zero) | _mm512_cmpgt_epu32_mask(enter, use_enter) | _mm512_cmplt_epu32_mask(leave, use_leave);
        bad &= earlier;
    }
    return bad;
}

// The lanes of 16 instructions, from `first`, whose operands the exact check must decide: a phi's and a call's, whose
// operands are in extra; a use of a value at or after its own instruction, which is an error within the block; and,
// with a dominator tree, a use of an earlier block's value whose block doesn't dominate the use's. Any other use is of
// an earlier value of its own block, which needs nothing more (#786).
static __mmask16 ir_dominance_batch(const IrFunction* f, const IrCfg* cfg, const IrDominance* dominance, uint32_t first, uint32_t count)
{
    const __mmask16 active = (__mmask16)((1u << count) - 1u);
    const __m128i op = _mm_maskz_loadu_epi8(active, f->op + first);
    const __m512i operands = ir_lookup_bytes(ir_operand_table, op);
    const __m512i optional = ir_lookup_bytes(ir_optional_table, op);
    const __m512i a = _mm512_maskz_loadu_epi32(active, f->a + first);
    const __m512i b = _mm512_maskz_loadu_epi32(active, f->b + first);
    const __m512i c = _mm512_maskz_loadu_epi32(active, f->c + first);
    const __m512i none = _mm512_set1_epi32(-1);
    const __m512i one = _mm512_set1_epi32(1);
    const __m512i two = _mm512_set1_epi32(2);
    const __m512i four = _mm512_set1_epi32(4);
    const __m512i index = _mm512_add_epi32(_mm512_set1_epi32((int)first), _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15));
    const __mmask16 extra = _mm_mask_cmpeq_epi8_mask(active, op, _mm_set1_epi8(IR_PHI)) | _mm_mask_cmpeq_epi8_mask(active, op, _mm_set1_epi8(IR_CALL)) | _mm_mask_cmpeq_epi8_mask(active, op, _mm_set1_epi8(IR_CALL_INDIRECT));
    const __mmask16 direct = active & ~extra;
    const __mmask16 use_a = direct & (_mm512_test_epi32_mask(operands, one) | (_mm512_test_epi32_mask(optional, one) & _mm512_cmpneq_epi32_mask(a, none)));
    const __mmask16 use_b = direct & (_mm512_test_epi32_mask(operands, two) | (_mm512_test_epi32_mask(optional, two) & _mm512_cmpneq_epi32_mask(b, none)));
    const __mmask16 use_c = direct & (_mm512_test_epi32_mask(operands, four) | (_mm512_test_epi32_mask(optional, four) & _mm512_cmpneq_epi32_mask(c, none)));
    __mmask16 pending = extra | (use_a & _mm512_cmpge_epu32_mask(a, index)) | (use_b & _mm512_cmpge_epu32_mask(b, index)) | (use_c & _mm512_cmpge_epu32_mask(c, index));
    if (dominance != 0)
    {
        // Sixteen instructions are in at most sixteen consecutive blocks from the first's: their starts and tree
        // intervals are one load each, which each lane's block picks by a permute.
        const uint32_t base = cfg->instruction_blocks[first];
        const uint32_t span = f->block_count - base;
        const __mmask16 held = span >= BUSTER_IR_VECTOR_COUNT ? (__mmask16)0xffff : (__mmask16)((1u << span) - 1u);
        const __m512i local = _mm512_sub_epi32(_mm512_maskz_loadu_epi32(active, cfg->instruction_blocks + first), _mm512_set1_epi32((int)base));
        const __m512i start = _mm512_permutexvar_epi32(local, _mm512_maskz_loadu_epi32(held, f->block_offsets + base));
        const __m512i use_enter = _mm512_permutexvar_epi32(local, _mm512_maskz_loadu_epi32(held, dominance->enter + base));
        const __m512i use_leave = _mm512_permutexvar_epi32(local, _mm512_maskz_loadu_epi32(held, dominance->leave + base));
        pending |= ir_dominance_earlier(cfg, dominance, use_a & _mm512_cmplt_epu32_mask(a, start), a, use_enter, use_leave);
        pending |= ir_dominance_earlier(cfg, dominance, use_b & _mm512_cmplt_epu32_mask(b, start), b, use_enter, use_leave);
        pending |= ir_dominance_earlier(cfg, dominance, use_c & _mm512_cmplt_epu32_mask(c, start), c, use_enter, use_leave);
    }
    return pending;
}

static __attribute__((noinline, aligned(64))) IrCheck ir_check_dominance(const IrFunction* f, const IrCfg* cfg, const IrDominance* dominance, bool vector)
{
    (void)vector;

    IrCheck check = ir_check(IR_OK, BUSTER_IR_NONE, 0);
    const uint32_t step = BUSTER_VECTOR_PATH(vector) ? BUSTER_IR_VECTOR_COUNT : 1;
    for (uint32_t first = 0; check.error == IR_OK && first < f->instruction_count; first += step)
    {
        const uint32_t remaining = f->instruction_count - first;
        uint32_t pending = 1;
        if (BUSTER_VECTOR_PATH(vector))
        {
            pending = ir_dominance_batch(f, cfg, dominance, first, remaining < BUSTER_IR_VECTOR_COUNT ? remaining : BUSTER_IR_VECTOR_COUNT);
        }
        while (check.error == IR_OK && pending != 0)
        {
            const uint32_t lane = (uint32_t)__builtin_ctz(pending);
            pending &= pending - 1;
            check = ir_check_operands(f, cfg, dominance, first + lane);
        }
    }
    return check;
}

// A phi's and a switch's own rules. Phis start a block other than the entry, one incoming value per predecessor, each
// from a distinct predecessor and of the phi's type; a switch's cases are distinct. A phi after a non-phi of its block
// is the first misplaced one, since one after a misplaced phi comes after that phi's error.
static IrCheck ir_check_join(const IrFunction* f, const IrCfg* cfg, Arena* scratch, uint32_t* seen, uint32_t i)
{
    IrCheck check = ir_check(IR_OK, i, 0);
    if (f->op[i] == IR_PHI)
    {
        const uint32_t b = cfg->instruction_blocks[i];
        const bool misplaced = i != f->block_offsets[b] && f->op[i - 1] != IR_PHI;
        // Initial function entry has no predecessor from which a phi can select a value. A producer needing a loop at
        // entry must add a real preheader with an explicit initial value before introducing phis.
        if (b == 0 || misplaced || f->b[i] != cfg->predecessor_offsets[b + 1] - cfg->predecessor_offsets[b])
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
        check = ir_check(ir_switch_unique(f, i, scratch), i, 0);
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
    // The phis and switches, which the vector path finds 64 operations at a time.
    const uint32_t step = BUSTER_VECTOR_PATH(vector) ? 64 : 1;
    for (uint32_t first = 0; check.error == IR_OK && first < f->instruction_count; first += step)
    {
        const uint32_t remaining = f->instruction_count - first;
        uint64_t pending = 0;
        if (BUSTER_VECTOR_PATH(vector))
        {
            const __mmask64 active = remaining >= 64 ? (__mmask64)-1 : ((__mmask64)1 << remaining) - 1;
            const __m512i op = _mm512_maskz_loadu_epi8(active, f->op + first);
            pending = _mm512_mask_cmpeq_epi8_mask(active, op, _mm512_set1_epi8(IR_PHI)) | _mm512_mask_cmpeq_epi8_mask(active, op, _mm512_set1_epi8(IR_SWITCH));
        }
        else
        {
            pending = f->op[first] == IR_PHI || f->op[first] == IR_SWITCH;
        }
        while (check.error == IR_OK && pending != 0)
        {
            const uint32_t lane = (uint32_t)__builtin_ctzll(pending);
            pending &= pending - 1;
            check = ir_check_join(f, cfg, scratch, seen, first + lane);
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

static __attribute__((noinline, aligned(64))) IrCheck ir_verify_path(const IrFunction* f, Arena* scratch, const IrDominance* dominance, bool vector)
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

// ir_verify_path's steps past the shape and rows (#1379): the validated CFG and the graph against `dominance`, for a
// function whose shape and rows a verification on the same path has already passed and which hasn't changed since. Both
// checks read the function alone, so its result is the whole verification's.
static __attribute__((unused)) IrCheck ir_verify_graph(const IrFunction* f, Arena* scratch, const IrDominance* dominance, bool vector)
{
    const uint64_t mark = scratch->length;
    IrCfg cfg = { 0 };
    IrCheck check = ir_check(ir_cfg_validated(f, scratch, scratch, &cfg), BUSTER_IR_NONE, 0);
    if (check.error == IR_OK)
    {
        check = ir_check_graph(f, &cfg, scratch, dominance, BUSTER_VECTOR_PATH(vector));
    }
    arena_pop_to(scratch, mark);
    return check;
}

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

// #1634: the vector twin's place for each of a batch's 48 fixed operands: bit k of the batch's operand mask is column
// k % 3 (a, b, c) of lane k / 3, so ascending bits run in ir_operand's order. Each entry is lane << 2 | column.
static const uint8_t ir_uses_slots[48] = {
    0, 1, 2, 4, 5, 6, 8, 9, 10, 12, 13, 14, 16, 17, 18, 20,
    21, 22, 24, 25, 26, 28, 29, 30, 32, 33, 34, 36, 37, 38, 40, 41,
    42, 44, 45, 46, 48, 49, 50, 52, 53, 54, 56, 57, 58, 60, 61, 62,
};
// Bit 3 * lane of the 48, where pdep deposits a 16-lane mask's bit for that lane's operand a; b's are one bit higher.
#define BUSTER_IR_USES_LANE_BITS UINT64_C(0x249249249249)

// One batch of up to 16 instructions from first, for either pass of the vector twin: with uses zero, counts each
// operand's value in `counts` (offsets + 1); otherwise places each use at its value's cursor in `counts`. One byte
// permute of the opcode tables gives every instruction's fixed operands, and pdep spreads the three column masks into
// one 48-bit mask whose ascending bits are ir_operand's order; the loops read each operand from its column, with no
// per-operand decode and no staging buffer. A phi's inputs or a call's arguments follow its own fixed operands, so the
// mask is walked in segments that end at each such instruction. Returns the batch's operand count.
static uint64_t ir_uses_batch(const IrFunction* f, uint32_t first, uint32_t count, uint32_t* counts, IrUse* uses)
{
    const __mmask16 active = (__mmask16)((1u << count) - 1u);
    const __m128i op_bytes = _mm_maskz_loadu_epi8(active, f->op + first);
    const __m512i op = _mm512_cvtepu8_epi32(op_bytes);
    const __m512i operands = ir_lookup_bytes(ir_operand_table, op_bytes);
    const __m512i optional = ir_lookup_bytes(ir_optional_table, op_bytes);
    const __m512i none = _mm512_set1_epi32(-1);
    const __m512i one = _mm512_set1_epi32(1);
    const __m512i two = _mm512_set1_epi32(2);
    const __m512i four = _mm512_set1_epi32(4);
    // ir_value_mask: an opcode's operands, and its optional ones where they aren't BUSTER_IR_NONE.
    const __mmask16 use_a = active & (_mm512_test_epi32_mask(operands, one) | (_mm512_test_epi32_mask(optional, one) & _mm512_cmpneq_epi32_mask(_mm512_maskz_loadu_epi32(active, f->a + first), none)));
    const __mmask16 use_b = active & (_mm512_test_epi32_mask(operands, two) | (_mm512_test_epi32_mask(optional, two) & _mm512_cmpneq_epi32_mask(_mm512_maskz_loadu_epi32(active, f->b + first), none)));
    const __mmask16 use_c = active & (_mm512_test_epi32_mask(operands, four) | (_mm512_test_epi32_mask(optional, four) & _mm512_cmpneq_epi32_mask(_mm512_maskz_loadu_epi32(active, f->c + first), none)));
    // A ternary intrinsic's third operand (#1840) is in the extra array, as a phi's and a call's operands are.
    const __m512i kinds = _mm512_and_si512(_mm512_maskz_loadu_epi32(active, f->c + first), _mm512_set1_epi32(BUSTER_IR_INTRINSIC_KIND));
    const __mmask16 ternary = _mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(IR_INTRINSIC)) & _mm512_cmpge_epu32_mask(kinds, _mm512_set1_epi32(BUSTER_IR_INTRINSIC_TERNARY)) &
                              _mm512_cmplt_epu32_mask(kinds, _mm512_set1_epi32(BUSTER_IR_INTRINSIC_TERNARY_END));
    __mmask16 variadic = active & (_mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(IR_PHI)) | _mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(IR_CALL)) | _mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(IR_CALL_INDIRECT)) | ternary);
    const uint64_t present = _pdep_u64(use_a, BUSTER_IR_USES_LANE_BITS) | _pdep_u64(use_b, BUSTER_IR_USES_LANE_BITS << 1) | _pdep_u64(use_c, BUSTER_IR_USES_LANE_BITS << 2);
    const uint32_t* const columns[4] = { f->a + first, f->b + first, f->c + first, f->c + first };
    uint64_t total = 0;
    uint32_t lane = 0;
    bool more = true;
    while (more)
    {
        // The fixed operands of lanes lane..last, where last is the next phi or call, or the batch's last lane.
        const uint32_t last = variadic != 0 ? (uint32_t)__builtin_ctz(variadic) : count - 1;
        const uint64_t segment = ((UINT64_C(1) << (3 * (last + 1))) - 1) & ~((UINT64_C(1) << (3 * lane)) - 1);
        uint64_t taken = present & segment;
        total += (uint64_t)__builtin_popcountll(taken);
        if (uses == 0)
        {
            while (taken != 0)
            {
                const uint32_t slot = ir_uses_slots[__builtin_ctzll(taken)];
                counts[columns[slot & 3][slot >> 2]] += 1;
                taken &= taken - 1;
            }
        }
        else
        {
            while (taken != 0)
            {
                const uint32_t slot = ir_uses_slots[__builtin_ctzll(taken)];
                const uint32_t value = columns[slot & 3][slot >> 2];
                const uint32_t place = counts[value];
                counts[value] = place + 1;
                uses[place] = (IrUse){
                    .instruction = first + (slot >> 2),
                    .operand = slot & 3,
                };
                taken &= taken - 1;
            }
        }
        if (variadic != 0)
        {
            const uint32_t i = first + last;
            const bool phi = f->op[i] == IR_PHI;
            const bool third = f->op[i] == IR_INTRINSIC;
            const uint32_t amount = phi ? f->b[i] : third ? 1 : f->calls[f->b[i]].argument_count;
            const uint32_t* const extra = phi ? f->extra + f->a[i] + 1 : third ? f->extra + ir_intrinsic_extra(f->c[i]) : f->extra + f->calls[f->b[i]].argument_first;
            const uint32_t stride = phi ? 2 : 1;
            for (uint32_t p = 0; p < amount; p += 1)
            {
                const uint32_t value = extra[(uint64_t)p * stride];
                if (uses == 0)
                {
                    counts[value] += 1;
                }
                else
                {
                    const uint32_t place = counts[value];
                    counts[value] = place + 1;
                    uses[place] = (IrUse){
                        .instruction = i,
                        .operand = 3 + p,
                    };
                }
            }
            total += amount;
            variadic &= variadic - 1;
        }
        lane = last + 1;
        more = lane < count;
    }
    return total;
}

// The use lists of a verified function, which ir_build_uses verifies first; a caller that has just verified it, as the
// optimizer has, builds them without verifying it again (#786). Arena and scratch are disjoint. The scalar twin decodes
// each operand with ir_operand; the vector twin (#1634) decodes 16 instructions at once in ir_uses_batch. Both count
// each value's uses, sum the counts into offsets, then place every use in instruction order, then operand order.
static __attribute__((noinline, aligned(64))) IrError ir_uses_path(const IrFunction* f, Arena* arena, Arena* scratch, IrUses* result, bool vector)
{
    (void)vector;

    const uint64_t mark = arena->length;
    const uint64_t temporary = scratch->length;
    IrCheck check = ir_check(IR_OK, BUSTER_IR_NONE, 0);
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
    if (check.error == IR_OK && BUSTER_VECTOR_PATH(vector))
    {
        // A count can't wrap before the total passes UINT32_MAX, which refuses the function as the scalar twin does.
        for (uint32_t first = 0; first < f->instruction_count; first += BUSTER_IR_VECTOR_COUNT)
        {
            const uint32_t count = f->instruction_count - first < BUSTER_IR_VECTOR_COUNT ? f->instruction_count - first : BUSTER_IR_VECTOR_COUNT;
            total += ir_uses_batch(f, first, count, uses.offsets + 1, 0);
        }
        check.error = total > UINT32_MAX ? IR_ERROR_LIMIT : IR_OK;
    }
    for (uint32_t i = 0; check.error == IR_OK && !BUSTER_VECTOR_PATH(vector) && i < f->instruction_count; i += 1)
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
        for (uint32_t first = 0; BUSTER_VECTOR_PATH(vector) && first < f->instruction_count; first += BUSTER_IR_VECTOR_COUNT)
        {
            const uint32_t count = f->instruction_count - first < BUSTER_IR_VECTOR_COUNT ? f->instruction_count - first : BUSTER_IR_VECTOR_COUNT;
            ir_uses_batch(f, first, count, cursor, uses.uses);
        }
        for (uint32_t i = 0; !BUSTER_VECTOR_PATH(vector) && i < f->instruction_count; i += 1)
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

static __attribute__((unused)) IrError ir_uses_validated_scalar(const IrFunction* f, Arena* arena, Arena* scratch, IrUses* result)
{
    return ir_uses_path(f, arena, scratch, result, false);
}

static IrError ir_uses_validated(const IrFunction* f, Arena* arena, Arena* scratch, IrUses* result)
{
    return ir_uses_path(f, arena, scratch, result, true);
}

static IrError ir_build_uses(const IrFunction* f, Arena* arena, Arena* scratch, IrUses* result)
{
    const IrCheck check = arena == scratch ? ir_check(IR_ERROR_STRUCTURE, BUSTER_IR_NONE, 0) : ir_verify(f, scratch, 0);
    IrError error = check.error;
    if (error == IR_OK)
    {
        error = ir_uses_validated(f, arena, scratch, result);
    }
    return error;
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

// A machine row's class (#141): its name, a vector's and a mask's bytes after it, after `separator`.
static void ir_print_row_class(FILE* output, const char* separator, uint32_t class, uint32_t bytes)
{
    fprintf(output, "%s%s", separator, class < BUSTER_TYPED_CLASS_LIMIT ? ir_row_class_names[class] : "invalid");
    if (class == BUSTER_TYPED_CLASS_VECTOR || class == BUSTER_TYPED_CLASS_MASK)
    {
        fprintf(output, "%" PRIu32, bytes);
    }
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
        // The machine rows (#141): `row #k (<operand classes>) -> <result class>` and each record as
        // [<form> reg vvvv rm index mode decorations immediate displacement].
        for (uint32_t r = 0; r < f->row_count; r += 1)
        {
            const Tier0Operation* row = &f->rows[r];
            fprintf(output, "  row #%" PRIu32 " (", r);
            for (uint32_t j = 0; j < row->operand_count; j += 1)
            {
                ir_print_row_class(output, j == 0 ? "" : ", ", row->operand_classes[j], row->operand_bytes[j]);
            }
            fprintf(output, ") ->");
            ir_print_row_class(output, " ", row->result_class, row->result_bytes);
            for (uint32_t k = 0; k < row->record_count; k += 1)
            {
                const X86Instruction* record = &row->records[k];
                fprintf(output, " [%s 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x %" PRId32 " %" PRId32 "]", record->form < X86_FORM_COUNT ? ir_form_names[record->form] : "INVALID", record->reg,
                        record->vvvv, record->rm, record->index, record->mode, record->decorations, record->immediate, record->displacement);
            }
            fputc('\n', output);
        }
        // The asm bodies (#1839): `asm #k records=<count> branches=<count> touched=<mask>`, a form the parser doesn't read.
        for (uint32_t k = 0; k < f->asm_body_count; k += 1)
        {
            const IrAsmBody* body = &f->asm_bodies[k];
            fprintf(output, "  asm #%" PRIu32 " records=%" PRIu32 " branches=%" PRIu32 " touched=0x%08" PRIx32 "\n", k, body->count, body->branch_count, body->touched);
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
                // A vector operation's lanes (#141's element types) in its type's name, but a comparison's, whose type is
                // its mask: `lanes=` after its predicate.
                const bool laned = op != IR_VCMP && ir_vector_type(f->type[i]) && f->flags[i] != 0 && f->flags[i] < IR_SIMD_LANE_COUNT && ir_lane_operation(op) != IR_SIMD_OPERATION_COUNT;
                fprintf(output, "  %%%" PRIu32 " = %s.%s", i, ir_op_names[op],
                        laned ? ir_lane_type_names[f->type[i] - IR_V4I32][f->flags[i]] : ir_type_names[f->type[i]]);
                switch (op)
                {
                    case IR_CONST:
                    {
                        fputc(' ', output);
                        ir_print_constant(output, f->constants[a]);
                    } break;
                    case IR_VCONST:
                    {
                        fputs(" 0x", output);
                        for (uint32_t word = ir_type_bits[f->type[i]] / 64; word-- > 0;)
                        {
                            fprintf(output, "%016" PRIx64, f->vector_constants[a].words[word]);
                        }
                    } break;
                    case IR_PARAM: fprintf(output, " %" PRIu32, a); break;
                    case IR_STACK_ADDR: fprintf(output, " $%" PRIu32, a); break;
                    case IR_LIFETIME: fprintf(output, " $%" PRIu32 " %s", a, b != 0 ? "end" : "start"); break;
                    case IR_FENCE: fprintf(output, " %s%s", ir_fence_orders[a & BUSTER_IR_FENCE_ORDER], (a & BUSTER_IR_FENCE_SIGNAL) != 0 ? " signal" : ""); break;
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
                        if (call.row != 0)
                        {
                            fprintf(output, " row #%" PRIu32 "(", call.row - 1);
                        }
                        else if (call.body != 0)
                        {
                            fprintf(output, " asm #%" PRIu32 "(", call.body - 1);
                        }
                        else
                        {
                            fprintf(output, " %c%" PRIu32 "(", op == IR_CALL ? '@' : '%', a);
                        }
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
                        if (op == IR_ICMP || op == IR_FCMP || op == IR_VCMP)
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
                            if (op == IR_VCMP && f->flags[i] != 0)
                            {
                                fprintf(output, " lanes=%s", f->flags[i] < IR_SIMD_LANE_COUNT ? ir_lane_names[f->flags[i]] : "invalid");
                            }
                        }
                        if (op == IR_INTRINSIC)
                        {
                            if (ir_intrinsic_ternary(c) && ir_slice(ir_intrinsic_extra(c), 1, f->extra_count))
                            {
                                fprintf(output, ", %%%" PRIu32, ir_intrinsic_third(f, i));
                            }
                            fprintf(output, " %s", ir_intrinsic_kind(c) < BUSTER_IR_INTRINSIC_COUNT && (c <= BUSTER_IR_INTRINSIC_KIND || ir_intrinsic_ternary(c)) ? ir_intrinsic_names[ir_intrinsic_kind(c)] : "invalid");
                            // A width change's part (#1840), its immediate.
                            if (c == BUSTER_IR_INTRINSIC_EXTRACT || c == BUSTER_IR_INTRINSIC_INSERT || c == BUSTER_IR_INTRINSIC_PERMUTE || c == BUSTER_IR_INTRINSIC_ALIGN ||
                                c == BUSTER_IR_INTRINSIC_SHUFFLE_LANES || c == BUSTER_IR_INTRINSIC_EXTEND || c == BUSTER_IR_INTRINSIC_TRUNCATE ||
                                c == BUSTER_IR_INTRINSIC_SHIFT || c == BUSTER_IR_INTRINSIC_LANE_OP || ir_intrinsic_ternary(c) || c == BUSTER_IR_INTRINSIC_AES ||
                                c == BUSTER_IR_INTRINSIC_LANE_EXTRACT || c == BUSTER_IR_INTRINSIC_LANE_INSERT || c == BUSTER_IR_INTRINSIC_MOVEMASK ||
                                c == BUSTER_IR_INTRINSIC_REDUCE || c == BUSTER_IR_INTRINSIC_LANE_UNARY)
                            {
                                fprintf(output, " %u", (unsigned)f->flags[i]);
                            }
                        }
                        if (op == IR_STACK_SAVE && f->flags[i] == BUSTER_IR_STACK_SAVE_FRAME)
                        {
                            fprintf(output, " frame");
                        }
                        if (op == IR_ATOMIC_RMW)
                        {
                            const uint32_t order = (f->flags[i] & BUSTER_IR_ORDER) >> BUSTER_IR_ORDER_SHIFT;
                            const uint32_t operation = f->flags[i] & BUSTER_IR_RMW_OPERATION;
                            fprintf(output, " align=%" PRIu32 " %s %s", UINT32_C(1) << f->alignment_log2[i],
                                    operation == BUSTER_IR_RMW_ADD ? "add" : operation == BUSTER_IR_RMW_COMPARE ? "cas" : operation == BUSTER_IR_RMW_EXCHANGE ? "xchg" : "invalid",
                                    order <= BUSTER_IR_FENCE_SEQ_CST ? ir_fence_orders[order] : "invalid");
                        }
                        if (op == IR_MASKED_LOAD || op == IR_MASKED_STORE)
                        {
                            // A masked access's alignment and lanes (#1840), always named: its mask's type depends on them.
                            fprintf(output, " align=%" PRIu32 " lanes=%s", UINT32_C(1) << f->alignment_log2[i], f->flags[i] < IR_SIMD_LANE_COUNT ? ir_lane_names[f->flags[i]] : "invalid");
                        }
                        if (op == IR_LOAD || op == IR_STORE)
                        {
                            fprintf(output, " align=%" PRIu32 "%s", UINT32_C(1) << f->alignment_log2[i], (f->flags[i] & BUSTER_IR_VOLATILE) != 0 ? " volatile" : "");
                            const uint32_t order = (f->flags[i] & BUSTER_IR_ORDER) >> BUSTER_IR_ORDER_SHIFT;
                            if ((f->flags[i] & BUSTER_IR_ATOMIC) != 0)
                            {
                                fprintf(output, " atomic %s", order <= BUSTER_IR_FENCE_SEQ_CST ? ir_fence_orders[order] : "invalid");
                            }
                            const uint32_t segment = (f->flags[i] & BUSTER_IR_SEGMENT) >> BUSTER_IR_SEGMENT_SHIFT;
                            if (segment != 0)
                            {
                                fprintf(output, " %s", segment == BUSTER_IR_SEGMENT_FS ? "fs" : segment == BUSTER_IR_SEGMENT_GS ? "gs" : "ss");
                            }
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
#endif
