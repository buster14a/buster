#pragma once

#include <buster/lib/arena.h>
#include <buster/lib/compiler/ir/ir.h>
#include <buster/lib/compiler/codegen/machine_select.h>
#include <buster/lib/target.h>

// Compact target-specific machine SSA representation shared by the FAST and
// QUALITY register allocators, the machine verifier, frame layout, and the
// streaming encoder. Per function, temporal-arena-owned, flat and contiguous
// after construction, integer-ID based. This header owns only the record
// shapes, static opcode metadata interface, chunked builder, verifier, and
// test-only replay; instruction selection and allocation build on top of it
// in later stages. The canonical direct emitter (`NONE`) never touches it.

// A packed operand reference: kind in the top three bits, payload in the low
// twenty-nine. Payload meaning depends on the kind (virtual/physical register
// number, immediate index, address index, stack slot, block index, overflow
// index into a cold side table).
typedef u32 MachineRef;

typedef enum MachineRefKind
{
    MACHINE_REF_NONE,
    MACHINE_REF_VIRTUAL_REGISTER,
    MACHINE_REF_PHYSICAL_REGISTER,
    MACHINE_REF_IMMEDIATE,
    MACHINE_REF_ADDRESS,
    MACHINE_REF_STACK_SLOT,
    MACHINE_REF_BLOCK,
    MACHINE_REF_EXTRA,
    MACHINE_REF_KIND_COUNT,
} MachineRefKind;

#define MACHINE_REF_KIND_BITS 3u
#define MACHINE_REF_PAYLOAD_BITS 29u
#define MACHINE_REF_PAYLOAD_LIMIT (1u << MACHINE_REF_PAYLOAD_BITS)
#define MACHINE_REF_NONE_VALUE ((MachineRef)0)
BUSTER_CT_CHECK(MACHINE_REF_KIND_COUNT <= (1u << MACHINE_REF_KIND_BITS));

// A machine program point: instruction index in the high thirty bits, phase
// in the low two. Block parameters define at entry; incoming operands use on
// edges. Capacity is validated by the builder before shifting.
typedef u32 MachinePoint;

typedef enum MachinePointPhase
{
    MACHINE_POINT_BEFORE,
    MACHINE_POINT_EARLY,
    MACHINE_POINT_NORMAL,
    MACHINE_POINT_AFTER,
    MACHINE_POINT_PHASE_COUNT,
} MachinePointPhase;

#define MACHINE_POINT_PHASE_BITS 2u
#define MACHINE_POINT_INSTRUCTION_LIMIT (1u << (32u - MACHINE_POINT_PHASE_BITS))
#define MACHINE_POINT_INVALID ((MachinePoint)UINT32_MAX)
BUSTER_CT_CHECK(MACHINE_POINT_PHASE_COUNT <= (1u << MACHINE_POINT_PHASE_BITS));

// The hot instruction row. Static per-opcode metadata supplies operand
// roles, classes, ties, early clobbers, implicit registers, regmasks, memory
// alternatives, emission recipe, and side effects; the row carries only the
// selected opcode, four inline packed operands, an immediate-or-side-table
// payload, and rare dynamic flags. No source/debug information, no linked
// pointers, no allocator state.
typedef struct MachineInstruction MachineInstruction;
struct MachineInstruction
{
    MachineRef operands[4];
    u32 payload;
    u16 opcode;
    u16 flags;
};
BUSTER_CT_CHECK(sizeof(MachineInstruction) == 24);

typedef enum MachineRegisterClass
{
    MACHINE_REGISTER_CLASS_NONE,
    MACHINE_REGISTER_CLASS_GENERAL,
    MACHINE_REGISTER_CLASS_VECTOR,
    MACHINE_REGISTER_CLASS_MASK,
    MACHINE_REGISTER_CLASS_FLAGS,
    MACHINE_REGISTER_CLASS_COUNT,
} MachineRegisterClass;

typedef struct MachineVirtualRegister MachineVirtualRegister;
struct MachineVirtualRegister
{
    MachinePoint definition_point;
    u8 register_class;
    u8 flags;
    u16 rematerialization_recipe;
    // IrValueId.value origin, or IR_ID_UNDERLYING_INVALID when synthesized.
    u32 typed_origin;
    MachineRef hint;
};
BUSTER_CT_CHECK(sizeof(MachineVirtualRegister) == 16);

typedef struct MachineBlock MachineBlock;
struct MachineBlock
{
    u32 first_instruction;
    u32 instruction_count;
    u32 predecessor_offset;
    u32 successor_offset;
    u32 parameter_offset;
    u16 predecessor_count;
    u16 successor_count;
    u16 parameter_count;
    // Loop-nesting depth as a static execution-frequency estimate, stamped
    // by machine_function_stamp_frequency_classes after selection; zero for
    // straight-line code.
    u16 frequency_class;
    u32 reserved;
};
BUSTER_CT_CHECK(sizeof(MachineBlock) == 32);

typedef struct MachineEdge MachineEdge;
struct MachineEdge
{
    u32 source_block;
    u32 destination_block;
    u32 copy_offset;
    u16 copy_count;
    u16 flags;
};
BUSTER_CT_CHECK(sizeof(MachineEdge) == 16);

// A block parameter is a virtual register defined at block entry.  Incoming
// edge values are kept separately in the edge's parallel-copy source slice;
// the destination for source i is the parameter at destination_block's
// parameter_offset + i.  Keeping both arrays function-owned makes CFG edges
// stable when a scheduled instruction stream is copied.
typedef struct MachineBlockParameter MachineBlockParameter;
struct MachineBlockParameter
{
    u32 virtual_register;
    u16 flags;
    u16 reserved;
};
BUSTER_CT_CHECK(sizeof(MachineBlockParameter) == 8);

typedef struct MachineAddress MachineAddress;
struct MachineAddress
{
    MachineRef base;
    MachineRef index;
    s32 displacement;
    u8 scale_shift;
    u8 flags;
    u16 symbol;
};
BUSTER_CT_CHECK(sizeof(MachineAddress) == 16);

// A packed [start, end) machine point range.
typedef struct MachineSegment MachineSegment;
struct MachineSegment
{
    MachinePoint start;
    MachinePoint end;
};
BUSTER_CT_CHECK(sizeof(MachineSegment) == 8);

typedef struct MachineUse MachineUse;
struct MachineUse
{
    MachinePoint point;
    u16 operand_slot;
    u16 constraint;
};
BUSTER_CT_CHECK(sizeof(MachineUse) == 8);

// Allocation output: sorted edits merged with the instruction stream during
// encoding instead of physically inserting rows.
typedef struct MachineEdit MachineEdit;
struct MachineEdit
{
    MachinePoint point;
    u16 kind;
    u16 flags;
    u32 subject;
    u32 location;
};
BUSTER_CT_CHECK(sizeof(MachineEdit) == 16);

typedef struct MachineLocationSegment MachineLocationSegment;
struct MachineLocationSegment
{
    u32 subject;
    MachinePoint start;
    MachinePoint end;
    u32 location;
};
BUSTER_CT_CHECK(sizeof(MachineLocationSegment) == 16);

// Emit recipes intentionally identify emission policy, not an architectural
// encoding form. The two high bits carry the category; the remaining bits
// are a target-local index owned by the recipe projection. Keeping this
// namespace compact and independent lets exact-form, selection, and
// scheduling identities evolve without changing MachineOpcode metadata.
typedef u16 MachineEmitRecipeId;

typedef enum MachineEmitRecipeCategory
{
    MACHINE_EMIT_RECIPE_CATEGORY_NONE,
    MACHINE_EMIT_RECIPE_CATEGORY_DIRECT,
    MACHINE_EMIT_RECIPE_CATEGORY_FAMILY,
    MACHINE_EMIT_RECIPE_CATEGORY_EXPANSION,
    MACHINE_EMIT_RECIPE_CATEGORY_COUNT,
} MachineEmitRecipeCategory;

#define MACHINE_EMIT_RECIPE_CATEGORY_BITS 2u
#define MACHINE_EMIT_RECIPE_INDEX_BITS (sizeof(MachineEmitRecipeId) * 8u - MACHINE_EMIT_RECIPE_CATEGORY_BITS)
#define MACHINE_EMIT_RECIPE_CATEGORY_SHIFT MACHINE_EMIT_RECIPE_INDEX_BITS
#define MACHINE_EMIT_RECIPE_INDEX_LIMIT (1u << MACHINE_EMIT_RECIPE_INDEX_BITS)
#define MACHINE_EMIT_RECIPE_INDEX_MASK ((MachineEmitRecipeId)(MACHINE_EMIT_RECIPE_INDEX_LIMIT - 1u))
#define MACHINE_EMIT_RECIPE_INVALID ((MachineEmitRecipeId)UINT16_MAX)
#define MACHINE_EMIT_RECIPE_NONE ((MachineEmitRecipeId)0)

// A recipe is written as its category's base plus the target-local index, so
// every identity stays a constant expression the recipe tables can hold in
// static storage. Categories occupy the two high bits, which is why the base
// and the index simply add. These are object-like constants and not
// enumerators because the shift is derived from sizeof, and an enumerator
// whose initializer contains sizeof is currently folded wrong by this
// compiler's own C frontend -- which self-hosting would then bake into the
// recipe tables.
#define MACHINE_EMIT_RECIPE_DIRECT_BASE ((MachineEmitRecipeId)(MACHINE_EMIT_RECIPE_CATEGORY_DIRECT << MACHINE_EMIT_RECIPE_CATEGORY_SHIFT))
#define MACHINE_EMIT_RECIPE_FAMILY_BASE ((MachineEmitRecipeId)(MACHINE_EMIT_RECIPE_CATEGORY_FAMILY << MACHINE_EMIT_RECIPE_CATEGORY_SHIFT))
#define MACHINE_EMIT_RECIPE_EXPANSION_BASE ((MachineEmitRecipeId)(MACHINE_EMIT_RECIPE_CATEGORY_EXPANSION << MACHINE_EMIT_RECIPE_CATEGORY_SHIFT))

BUSTER_CT_CHECK(MACHINE_EMIT_RECIPE_CATEGORY_COUNT <= (1u << MACHINE_EMIT_RECIPE_CATEGORY_BITS));
BUSTER_CT_CHECK(MACHINE_EMIT_RECIPE_INDEX_BITS < 16u);

// These identities are deliberately separate namespaces. Their tables will
// be added by instruction selection and scheduling work; for now only the
// invalid sentinels are reserved.
typedef u16 SelectionPatternId;
typedef u16 SchedulingClassId;
#define SELECTION_PATTERN_ID_INVALID ((SelectionPatternId)UINT16_MAX)
#define SCHEDULING_CLASS_ID_INVALID ((SchedulingClassId)UINT16_MAX)
#define MACHINE_SELECTION_PATTERN_ID_INVALID SELECTION_PATTERN_ID_INVALID
#define MACHINE_SCHEDULING_CLASS_ID_INVALID SCHEDULING_CLASS_ID_INVALID

BUSTER_CT_CHECK(sizeof(MachineEmitRecipeId) == sizeof(u16));
BUSTER_CT_CHECK(sizeof(SelectionPatternId) == sizeof(u16));
BUSTER_CT_CHECK(sizeof(SchedulingClassId) == sizeof(u16));

// Static opcode metadata. One row per opcode, never per instruction. The
// x86-64 selector stage adds the real target opcodes; the skeleton opcodes
// below exist only so the builder, verifier, and replay have hard test
// coverage before selection lands.
typedef enum MachineOpcode
{
    MACHINE_OPCODE_INVALID,
    MACHINE_OPCODE_SKELETON_NOP,
    MACHINE_OPCODE_SKELETON_COPY,
    MACHINE_OPCODE_SKELETON_RETURN,
    // x86-64 scalar subset. Operand slot 0 is the destination where one
    // exists; two-address forms tie slot 0 to the first source in metadata.
    MACHINE_X64_MOV_RI,       // def, immediate-pool ref; 64-bit materialize
    MACHINE_X64_MOV_RR,       // def, use; 64-bit register copy
    MACHINE_X64_MOV32_RR,     // def, use; 32-bit move, zero-extends
    MACHINE_X64_MOVSX8_RR,    // def, use; sign-extend from 8 bits
    MACHINE_X64_MOVSX16_RR,   // def, use; sign-extend from 16 bits
    MACHINE_X64_MOVSX32_RR,   // def, use; sign-extend from 32 bits
    MACHINE_X64_MOVZX8_RR,    // def, use; zero-extend from 8 bits
    MACHINE_X64_MOVZX16_RR,   // def, use; zero-extend from 16 bits
    MACHINE_X64_ADD32,        // def tied use, use
    MACHINE_X64_ADD64,
    MACHINE_X64_SUB32,
    MACHINE_X64_SUB64,
    MACHINE_X64_AND32,
    MACHINE_X64_AND64,
    MACHINE_X64_OR32,
    MACHINE_X64_OR64,
    MACHINE_X64_XOR32,
    MACHINE_X64_XOR64,
    MACHINE_X64_IMUL32,
    MACHINE_X64_IMUL64,
    MACHINE_X64_NEG32,        // def tied use
    MACHINE_X64_NEG64,
    MACHINE_X64_NOT32,
    MACHINE_X64_NOT64,
    MACHINE_X64_CVT_U64_TO_F32, // dest def, source use; canonical branchy round-to-nearest
    MACHINE_X64_CVT_U64_TO_F64,
    MACHINE_X64_CVT_F32_TO_U64, // dest def, source use; canonical threshold-subtract form
    MACHINE_X64_CVT_F64_TO_U64,
    MACHINE_X64_LEA_OFFSET,  // dest def, base use; payload = byte displacement
    MACHINE_X64_ADD64_IMM,   // dest use-define, immediate; folded address offset
    MACHINE_X64_IMUL64_RRI,  // dest def, source use, immediate; folded index scale
    MACHINE_X64_BSF32, // dest def, source use; undefined on zero input
    MACHINE_X64_BSF64,
    MACHINE_X64_BSR32,
    MACHINE_X64_BSR64,
    MACHINE_X64_POPCNT32, // dest def, source use; gated on the POPCNT feature
    MACHINE_X64_POPCNT64,
    MACHINE_X64_CMP32,        // use, use; defines flags
    MACHINE_X64_CMP64,
    MACHINE_X64_TEST_RR,      // use, use; defines flags (64-bit)
    MACHINE_X64_SETCC,        // def; payload = MachineX64Condition; uses flags
    MACHINE_X64_LOAD_FRAME,   // def, stack-slot ref; 64-bit read at slot + payload byte offset
    MACHINE_X64_STORE_FRAME8, // stack-slot ref, use; sized store at slot + payload byte offset
    MACHINE_X64_STORE_FRAME16,
    MACHINE_X64_STORE_FRAME32,
    MACHINE_X64_STORE_FRAME64,
    MACHINE_X64_LOAD_PTR8,    // def, use address; zero-extends
    MACHINE_X64_LOAD_PTR16,
    MACHINE_X64_LOAD_PTR32,
    MACHINE_X64_LOAD_PTR64,
    MACHINE_X64_STORE_PTR8,   // use address, use value
    MACHINE_X64_STORE_PTR16,
    MACHINE_X64_STORE_PTR32,
    MACHINE_X64_STORE_PTR64,
    MACHINE_X64_JMP,          // block ref; terminator
    MACHINE_X64_JCC,          // block ref taken, block ref fallthrough; payload = condition; terminator
    MACHINE_X64_RET,          // terminator; implicit RAX use when returning a value
    // Fixed-register forms. Shifts read their count from CL; the divide
    // family keeps the dividend/result in RAX and clobbers RDX. The stage-2
    // placement satisfies these by construction (slot 0 is RAX, slot 1 is
    // RCX); real allocators must honor them as fixed constraints.
    MACHINE_X64_SHL32,        // def tied use [RAX], use count [CL]
    MACHINE_X64_SHL64,
    MACHINE_X64_SAR32,
    MACHINE_X64_SAR64,
    MACHINE_X64_SHR32,
    MACHINE_X64_SHR64,
    MACHINE_X64_SDIV32,       // def tied use [RAX], use divisor; clobbers RDX
    MACHINE_X64_SDIV64,
    MACHINE_X64_UDIV32,
    MACHINE_X64_UDIV64,
    MACHINE_X64_SREM32,
    MACHINE_X64_SREM64,
    MACHINE_X64_UREM32,
    MACHINE_X64_UREM64,
    // Direct call: payload indexes the function's call-target side table;
    // argument placement was lowered into explicit fixed-register copies
    // before this row. Clobbers the System V caller-saved set.
    MACHINE_X64_CALL_DIRECT,
    MACHINE_X64_LEA_FRAME,    // def, stack-slot ref: address of a frame slot
    MACHINE_X64_LEA_SYMBOL,   // def; payload indexes call_targets: rip-relative symbol address
    // Thread-local symbol address, ELF local-exec form: fs-base load plus a
    // lea whose displacement carries the TPOFF relocation, exactly the
    // canonical emitter's sequence. def; payload indexes call_targets.
    MACHINE_X64_LEA_TLS,
    // Terminator compare chain: use condition, block ref default; payload
    // is the first row in the switch-case side table and flags holds the
    // case count.
    MACHINE_X64_SWITCH,
    // Aggregate chunk copies, payload = byte count, chunked 8/4/2/1 like
    // the canonical copy loops. RAX (and RDX for the pointer-destination
    // form) are internal data scratches.
    MACHINE_X64_COPY_FRAME_FROM_FRAME, // slot ref destination, slot ref source
    MACHINE_X64_COPY_FRAME_FROM_PTR,   // slot ref destination, use address
    MACHINE_X64_COPY_PTR_FROM_FRAME,   // use address destination, slot ref source
    // Scalar float operations. Float values travel as IEEE bit patterns in
    // general registers and slots exactly like the canonical path; each row
    // moves through XMM0/XMM1 internally. FARITH payload: low byte is the
    // SSE opcode (0x58 add, 0x5c sub, 0x59 mul, 0x5e div), bit 8 selects
    // the 64-bit form. FCMP_SET payload: low nibble is the setcc condition,
    // bit 8 selects 64-bit compare, bits 9-10 the NaN-parity fixup
    // (1 = and-not-parity, 2 = or-parity).
    MACHINE_X64_FARITH,       // def, use, use
    MACHINE_X64_FCMP_SET,     // def, use, use; result pinned to RAX
    MACHINE_X64_CVT_F32_TO_F64, // def, use
    MACHINE_X64_CVT_F64_TO_F32,
    MACHINE_X64_CVT_I64_TO_F32,
    MACHINE_X64_CVT_I64_TO_F64,
    MACHINE_X64_CVT_F32_TO_I64,
    MACHINE_X64_CVT_F64_TO_I64,
    // Bit-exact bridges between general registers and the low XMM file for
    // the float ABI; payload is the XMM register index.
    MACHINE_X64_MOVQ_TO_XMM,   // use general source; xmm[payload] = bits
    MACHINE_X64_MOVQ_FROM_XMM, // def general destination = xmm[payload] bits
    // Stack-argument machinery. LOAD_INCOMING reads the caller-pushed
    // argument area above the frame base (payload = byte offset past the
    // saved RBP and return address). The push rows build outgoing stack
    // arguments right to left, and SUB/ADD_RSP keep the call-site stack
    // aligned and cleaned (payload = bytes).
    MACHINE_X64_LOAD_INCOMING, // def; payload byte offset into incoming args
    // SysV x86-64 variadic machinery. VA_SAVE snapshots the incoming
    // register argument file into the selector-owned 176-byte save area;
    // VA_ARG performs the register/overflow split against one va_list value.
    // The latter's payload indexes MachineVaArg side data and its two
    // operands are the list pointer and either a GP result vreg or a frame
    // slot for an aggregate result.
    MACHINE_X64_VA_SAVE,       // slot ref; payload unused
    MACHINE_X64_VA_ARG,        // use list pointer, define scalar/slot result
    MACHINE_X64_PUSH_FRAME,    // slot ref; payload byte offset into the slot
    MACHINE_X64_PUSH_REGISTER, // use
    MACHINE_X64_SUB_RSP,       // payload bytes
    MACHINE_X64_ADD_RSP,       // payload bytes
    // Runtime stack allocation: def = the resulting aligned RSP, use = the
    // byte count. The encoder keeps the count in RCX while probing one page
    // at a time and returns the final RSP in RAX. Payload is the required
    // alignment (at least sixteen for the System V call boundary).
    MACHINE_X64_STACK_ALLOCATE,
    // Indirect call through a pointer value; the callee rides in R10 so
    // neither the argument registers nor the variadic AL setup can clobber
    // it. Same flags as CALL_DIRECT.
    MACHINE_X64_CALL_INDIRECT, // use callee pointer
    // Atomics, mirroring the canonical sequences. Payload low byte is the
    // operand size; ATOMIC_RMW carries the IrAtomicOperation in bits 8+ and
    // loops through a lock cmpxchg with R8 as the retry scratch.
    MACHINE_X64_ATOMIC_STORE_XCHG, // use address, use value; implicit-lock xchg
    MACHINE_X64_ATOMIC_RMW,        // def old, use address, use value
    MACHINE_X64_ATOMIC_CMPXCHG,    // def old, use address, use expected, use desired
    // CMPXCHG16B has a two-eightbyte value on each side.  The result,
    // expected, and desired operands stay in frame slots; the address is the
    // constrained fourth operand and is staged in the target's RSI scratch.
    // RAX:RDX and RBX:RCX are implicit inputs/outputs, so the opcode's
    // metadata carries their complete clobber set rather than pretending
    // that one virtual register represents either pair.
    MACHINE_X64_ATOMIC_CMPXCHG16,
    MACHINE_X64_MFENCE,
    MACHINE_X64_INT3, // debug trap
    MACHINE_X64_UD2,  // unreachable; terminator
    // 512-bit vector subset, mirroring the canonical AVX-512 vocabulary but
    // with values register-resident instead of round-tripping every operand
    // through its frame slot. Masks travel in GENERAL registers (the
    // vocabulary's Mask64 is a plain u64); rows that need one stage through
    // k1 internally, which no allocator models because nothing else touches
    // the k file. Every memory form is the unaligned vmovdqu8 the canonical
    // path uses — vector frame slots stay under the sixteen-byte alignment
    // contract exactly like the canonical frame layout's vector clamp.
    MACHINE_X64_VMOV_RR,      // def vec, use vec; full 512-bit copy, coalescible
    MACHINE_X64_VLOAD_FRAME,  // def vec, stack-slot ref; 64-byte read
    MACHINE_X64_VSTORE_FRAME, // stack-slot ref, use vec; 64-byte write
    MACHINE_X64_VLOAD_PTR,    // def vec, use address
    MACHINE_X64_VSTORE_PTR,   // use address, use vec
    MACHINE_X64_VLOAD_PTR_MASKED,     // def vec, use address, use mask; zeroing
    MACHINE_X64_VSTORE_PTR_MASKED,    // use address, use mask, use vec
    MACHINE_X64_VCOMPRESS_STORE_PTR,  // use address, use mask, use vec
    MACHINE_X64_VSPLATB,      // def vec, use general; vpbroadcastb from r32
    // Lane compares producing a Mask64 in a general register; payload is
    // 0 = vpcmpeqb, 1 = vpcmpub/lt-unsigned, 2 = vptestmb, 3 = vpcmpeqd
    // (16 mask bits, rest zeroed by the compare).
    MACHINE_X64_VPCMP_MASK,   // def general mask, use vec, use vec
    MACHINE_X64_VPMOVB2M,     // def general mask, use vec
    // vpermt2b overwrites its low-table register with the result, so the
    // selector copies the low table into the destination first and the row
    // ties destination to itself; zeroing under the mask.
    MACHINE_X64_VPERMT2B,     // use-def vec result/low, use mask, use vec indices, use vec high
    MACHINE_X64_VCOMPRESSB,   // def vec, use mask, use vec; zeroing
    // vpmovzxbd from the source's chosen 16-byte quarter (payload); quarters
    // past zero extract through the destination register first.
    MACHINE_X64_VPMOVZXBD,    // def vec, use vec; payload = quarter
    MACHINE_X64_VPSLLD_RI,    // def vec, use vec; payload = immediate count
    // vpternlogd reads its truth table from the destination, so the selector
    // copies operand a in first, exactly like VPERMT2B; payload = table.
    MACHINE_X64_VPTERNLOGD,   // use-def vec result/a, use vec b, use vec c
    // Element-wise three-address binary; payload low byte is the 66 0F map
    // opcode (vpaddb 0xfc family, vpand 0xdb family) chosen by the selector
    // from the operation and lane width.
    MACHINE_X64_VBINARY,      // def vec, use vec, use vec
    // AArch64 scalar subset. Three-address forms carry no ties; the only
    // constrained rows are the remainder macro-ops, whose div-then-msub
    // sequence needs three distinct registers. Operand slot 0 is the
    // destination where one exists.
    MACHINE_A64_MOV_RI,   // def, immediate-pool ref; movz/movk materialize
    MACHINE_A64_MOV_RR,   // def, use; 64-bit orr-based register copy
    MACHINE_A64_MOV32_RR, // def, use; 32-bit move, zero-extends
    MACHINE_A64_SXTB,     // def, use; sign-extend from 8 bits
    MACHINE_A64_SXTH,     // def, use; sign-extend from 16 bits
    MACHINE_A64_SXTW,     // def, use; sign-extend from 32 bits
    MACHINE_A64_UXTB,     // def, use; zero-extend from 8 bits
    MACHINE_A64_UXTH,     // def, use; zero-extend from 16 bits
    MACHINE_A64_ADD32,    // def, use, use
    MACHINE_A64_ADD64,
    MACHINE_A64_SUB32,
    MACHINE_A64_SUB64,
    MACHINE_A64_AND32,
    MACHINE_A64_AND64,
    MACHINE_A64_ORR32,
    MACHINE_A64_ORR64,
    MACHINE_A64_EOR32,
    MACHINE_A64_EOR64,
    MACHINE_A64_MUL32,
    MACHINE_A64_MUL64,
    MACHINE_A64_SDIV32,
    MACHINE_A64_SDIV64,
    MACHINE_A64_UDIV32,
    MACHINE_A64_UDIV64,
    // Remainder as div-then-msub with the destination as the quotient
    // scratch; the constrained slot layout keeps all three registers
    // distinct, which the sequence requires.
    MACHINE_A64_SREM32,
    MACHINE_A64_SREM64,
    MACHINE_A64_UREM32,
    MACHINE_A64_UREM64,
    MACHINE_A64_LSL32, // def, use, use; variable shifts take any register
    MACHINE_A64_LSL64,
    MACHINE_A64_ASR32,
    MACHINE_A64_ASR64,
    MACHINE_A64_LSR32,
    MACHINE_A64_LSR64,
    MACHINE_A64_NEG32, // def, use; subtract from the zero register
    MACHINE_A64_NEG64,
    MACHINE_A64_NOT32, // def, use; orn from the zero register
    MACHINE_A64_NOT64,
    MACHINE_A64_CMP32,     // use, use; defines flags
    MACHINE_A64_CMP64,
    MACHINE_A64_CMP_ZERO,  // use; defines flags (64-bit compare against zero)
    MACHINE_A64_CSET,      // def; payload = MachineA64Condition; uses flags
    MACHINE_A64_LOAD_FRAME, // def, stack-slot ref; 64-bit read at slot + payload byte offset
    MACHINE_A64_LOAD_FRAME32, // def, stack-slot ref; 32-bit read at slot + payload byte offset, zero-extends
    MACHINE_A64_STORE_FRAME8, // stack-slot ref, use; sized store at slot + payload byte offset
    MACHINE_A64_STORE_FRAME16,
    MACHINE_A64_STORE_FRAME32,
    MACHINE_A64_STORE_FRAME64,
    MACHINE_A64_LOAD_PTR8, // def, use address; zero-extends
    MACHINE_A64_LOAD_PTR16,
    MACHINE_A64_LOAD_PTR32,
    MACHINE_A64_LOAD_PTR64,
    MACHINE_A64_STORE_PTR8, // use address, use value
    MACHINE_A64_STORE_PTR16,
    MACHINE_A64_STORE_PTR32,
    MACHINE_A64_STORE_PTR64,
    MACHINE_A64_LEA_FRAME,  // def, stack-slot ref: address of a frame slot + payload byte offset
    MACHINE_A64_LEA_OFFSET, // def, base use; payload = byte displacement
    // Sized aggregate copies through the reserved X17 data scratch (X16
    // stays the large-offset address scratch); payload = exact byte size.
    MACHINE_A64_COPY_FRAME_FROM_FRAME, // slot ref destination, slot ref source
    MACHINE_A64_COPY_FRAME_FROM_PTR,   // slot ref destination, use address
    MACHINE_A64_COPY_PTR_FROM_FRAME,   // use address destination, slot ref source
    MACHINE_A64_B,          // block ref; terminator
    MACHINE_A64_BCC,        // block ref taken, block ref fallthrough; payload = condition; terminator
    MACHINE_A64_RET,        // terminator; emits the full canonical epilogue
    // Bit-exact bridges between general registers and the low vector file
    // for the float ABI; payload is the vector register index.
    MACHINE_A64_FMOV_TO_VEC,   // use general source; v[payload] = bits
    MACHINE_A64_FMOV_FROM_VEC, // def general destination = v[payload] bits
    MACHINE_A64_BRK, // debug trap
    MACHINE_A64_UDF, // unreachable; terminator
    // SP travels through adds, not the orr-based moves, whose register 31
    // reads as the zero register; with STACK_ALLOCATE outside the subset,
    // stack save/restore pairs reduce to exact SP copies.
    MACHINE_A64_READ_SP,  // def = current stack pointer
    MACHINE_A64_WRITE_SP, // use; stack pointer = use
    // Direct call: payload indexes the call-target side table; argument
    // placement was lowered into explicit fixed-register copies before
    // this row. Clobbers the AAPCS64 caller-saved set.
    MACHINE_A64_CALL_DIRECT,
    // Indirect call through a pointer value; the callee rides in X16 so
    // neither the argument registers nor any allocatable register can
    // clobber it, mirroring the canonical blr form.
    MACHINE_A64_CALL_INDIRECT, // use callee pointer
    // Symbol address through the canonical inline-literal form: an
    // ldr-literal over a branch over an absolute eight-byte relocation.
    // The payload indexes call_targets.
    MACHINE_A64_LEA_SYMBOL, // def
    MACHINE_OPCODE_COUNT,
} MachineOpcode;

// x86-64 encoder authority registry.  The opcode rows are a contiguous
// projection of MACHINE_X64_MOV_RI..MACHINE_X64_VBINARY; the authority and
// neutral-patch records below keep every remaining producer explicit while
// migration work moves instruction construction behind metadata.
#define MACHINE_X86_64_EMIT_REGISTRY_COUNT 122u
#define MACHINE_X86_64_EMIT_REGISTRY_DIRECT_COUNT 47u
#define MACHINE_X86_64_EMIT_REGISTRY_FAMILY_COUNT 49u
#define MACHINE_X86_64_EMIT_REGISTRY_EXPANSION_COUNT 26u
#define MACHINE_X86_64_EMIT_REGISTRY_EXACT_FORM_COUNT 77u
#define MACHINE_X86_64_EMIT_REGISTRY_EXACT_SEQUENCE_COUNT 19u
#define MACHINE_X86_64_EMIT_REGISTRY_EXACT_COUNT (MACHINE_X86_64_EMIT_REGISTRY_EXACT_FORM_COUNT + MACHINE_X86_64_EMIT_REGISTRY_EXACT_SEQUENCE_COUNT)
#define MACHINE_X86_64_EMIT_REGISTRY_EXPANSION_POLICY_COUNT 26u
#define MACHINE_X86_64_EMIT_REGISTRY_LEGACY_RAW_COUNT 0u
#define MACHINE_X86_64_CANONICAL_AUTHORITY_SITE_COUNT 5u
#define MACHINE_X86_64_NEUTRAL_PATCH_SITE_COUNT 14u

typedef enum MachineX64EmitProducerStatus
{
    MACHINE_X64_EMIT_PRODUCER_STATUS_LEGACY_RAW,
    MACHINE_X64_EMIT_PRODUCER_STATUS_EXACT_FORM,
    MACHINE_X64_EMIT_PRODUCER_STATUS_EXACT_SEQUENCE,
    MACHINE_X64_EMIT_PRODUCER_STATUS_EXPANSION_POLICY,
    MACHINE_X64_EMIT_PRODUCER_STATUS_COUNT,
} MachineX64EmitProducerStatus;

typedef enum MachineX64CanonicalAuthorityKind
{
    MACHINE_X64_CANONICAL_AUTHORITY_METADATA_CHECKED,
    MACHINE_X64_CANONICAL_AUTHORITY_METADATA_EXACT,
    MACHINE_X64_CANONICAL_AUTHORITY_METADATA_SEQUENCE,
    MACHINE_X64_CANONICAL_AUTHORITY_KIND_COUNT,
} MachineX64CanonicalAuthorityKind;

typedef struct MachineX64CanonicalAuthoritySite MachineX64CanonicalAuthoritySite;
struct MachineX64CanonicalAuthoritySite
{
    MachineX64CanonicalAuthorityKind authority_kind;
    String8 source_file;
    String8 owner_symbol;
};

typedef enum MachineX64NeutralPatchClass
{
    MACHINE_X64_NEUTRAL_PATCH_RELOCATION,
    MACHINE_X64_NEUTRAL_PATCH_DISPLACEMENT,
    MACHINE_X64_NEUTRAL_PATCH_DATA,
    MACHINE_X64_NEUTRAL_PATCH_TARGET_PAYLOAD,
    MACHINE_X64_NEUTRAL_PATCH_CLASS_COUNT,
} MachineX64NeutralPatchClass;

typedef struct MachineX64NeutralPatchSite MachineX64NeutralPatchSite;
struct MachineX64NeutralPatchSite
{
    MachineX64NeutralPatchClass patch_class;
    String8 source_file;
    String8 owner_symbol;
};

typedef struct MachineX64EmitRegistryEntry MachineX64EmitRegistryEntry;
struct MachineX64EmitRegistryEntry
{
    MachineOpcode opcode;
    MachineEmitRecipeId recipe;
    u16 producer_ordinal;
    u8 producer_status;
    u8 reserved;
};

// Encoding forms are deliberately target-neutral.  An opcode may expose more
// than one legal form (for example a register and a folded-memory form), so
// MachineOpcodeInfo stores a bit set rather than a single enum value.
typedef enum MachineOpcodeForm
{
    MACHINE_OPCODE_FORM_NONE,
    MACHINE_OPCODE_FORM_REGISTER,
    MACHINE_OPCODE_FORM_REGISTER_IMMEDIATE,
    MACHINE_OPCODE_FORM_MEMORY,
    MACHINE_OPCODE_FORM_BRANCH,
    MACHINE_OPCODE_FORM_CALL,
    MACHINE_OPCODE_FORM_PSEUDO,
    MACHINE_OPCODE_FORM_COUNT,
} MachineOpcodeForm;

typedef enum MachineScheduleClass
{
    MACHINE_SCHEDULE_CLASS_NONE,
    MACHINE_SCHEDULE_CLASS_ALU,
    MACHINE_SCHEDULE_CLASS_SHIFT,
    MACHINE_SCHEDULE_CLASS_MUL,
    MACHINE_SCHEDULE_CLASS_DIV,
    MACHINE_SCHEDULE_CLASS_LOAD,
    MACHINE_SCHEDULE_CLASS_STORE,
    MACHINE_SCHEDULE_CLASS_BRANCH,
    MACHINE_SCHEDULE_CLASS_CALL,
    MACHINE_SCHEDULE_CLASS_VECTOR,
    MACHINE_SCHEDULE_CLASS_ATOMIC,
    MACHINE_SCHEDULE_CLASS_BARRIER,
    MACHINE_SCHEDULE_CLASS_COUNT,
} MachineScheduleClass;

typedef enum MachineOpcodeExpansion
{
    MACHINE_OPCODE_EXPANSION_NONE,
    MACHINE_OPCODE_EXPANSION_SINGLE,
    MACHINE_OPCODE_EXPANSION_SEQUENCE,
    MACHINE_OPCODE_EXPANSION_PSEUDO,
    MACHINE_OPCODE_EXPANSION_COUNT,
} MachineOpcodeExpansion;

typedef enum MachineMemoryEffect
{
    MACHINE_MEMORY_EFFECT_NONE,
    MACHINE_MEMORY_EFFECT_READ,
    MACHINE_MEMORY_EFFECT_WRITE,
    MACHINE_MEMORY_EFFECT_READ_WRITE,
    MACHINE_MEMORY_EFFECT_VOLATILE,
    MACHINE_MEMORY_EFFECT_ATOMIC,
    MACHINE_MEMORY_EFFECT_BARRIER,
    MACHINE_MEMORY_EFFECT_COUNT,
} MachineMemoryEffect;

typedef enum MachineBundleKind
{
    MACHINE_BUNDLE_NONE,
    MACHINE_BUNDLE_HEAD,
    MACHINE_BUNDLE_MEMBER,
    MACHINE_BUNDLE_TAIL,
    MACHINE_BUNDLE_COUNT,
} MachineBundleKind;

typedef enum MachineResource
{
    MACHINE_RESOURCE_NONE,
    MACHINE_RESOURCE_FLAGS,
    MACHINE_RESOURCE_NZCV,
    MACHINE_RESOURCE_STACK_POINTER,
    MACHINE_RESOURCE_FP_ENVIRONMENT,
    MACHINE_RESOURCE_VECTOR_STATE,
    MACHINE_RESOURCE_CONTROL,
    MACHINE_RESOURCE_COUNT,
} MachineResource;

// Public spelling used by verifier/scheduler clients.  Keep the shorter
// MachineResource name as a source-compatible alias for existing target code;
// both names denote the same compact bit positions in implicit-resource
// masks.
typedef MachineResource MachineImplicitResource;
#define MACHINE_IMPLICIT_RESOURCE_NONE MACHINE_RESOURCE_NONE
#define MACHINE_IMPLICIT_RESOURCE_FLAGS MACHINE_RESOURCE_FLAGS
#define MACHINE_IMPLICIT_RESOURCE_NZCV MACHINE_RESOURCE_NZCV
#define MACHINE_IMPLICIT_RESOURCE_STACK_POINTER MACHINE_RESOURCE_STACK_POINTER
#define MACHINE_IMPLICIT_RESOURCE_FP_ENVIRONMENT MACHINE_RESOURCE_FP_ENVIRONMENT
#define MACHINE_IMPLICIT_RESOURCE_VECTOR_STATE MACHINE_RESOURCE_VECTOR_STATE
#define MACHINE_IMPLICIT_RESOURCE_CONTROL MACHINE_RESOURCE_CONTROL
#define MACHINE_IMPLICIT_RESOURCE_COUNT MACHINE_RESOURCE_COUNT

// The bit each form and each resource occupies in `MachineOpcodeInfo`'s
// `form_set` and implicit-resource masks.  Both are spelled as enumerators
// rather than a shift over the position enum because every use is a constant
// initializer in the opcode table, where a named bit reads better than the
// shift that produced it.
typedef enum MachineOpcodeFormSet
{
    MACHINE_OPCODE_FORM_SET_NONE = 1u << MACHINE_OPCODE_FORM_NONE,
    MACHINE_OPCODE_FORM_SET_REGISTER = 1u << MACHINE_OPCODE_FORM_REGISTER,
    MACHINE_OPCODE_FORM_SET_REGISTER_IMMEDIATE = 1u << MACHINE_OPCODE_FORM_REGISTER_IMMEDIATE,
    MACHINE_OPCODE_FORM_SET_MEMORY = 1u << MACHINE_OPCODE_FORM_MEMORY,
    MACHINE_OPCODE_FORM_SET_BRANCH = 1u << MACHINE_OPCODE_FORM_BRANCH,
    MACHINE_OPCODE_FORM_SET_CALL = 1u << MACHINE_OPCODE_FORM_CALL,
    MACHINE_OPCODE_FORM_SET_PSEUDO = 1u << MACHINE_OPCODE_FORM_PSEUDO,
} MachineOpcodeFormSet;

typedef enum MachineResourceMask
{
    MACHINE_RESOURCE_NONE_MASK = 1u << MACHINE_RESOURCE_NONE,
    MACHINE_RESOURCE_FLAGS_MASK = 1u << MACHINE_RESOURCE_FLAGS,
    MACHINE_RESOURCE_NZCV_MASK = 1u << MACHINE_RESOURCE_NZCV,
    MACHINE_RESOURCE_STACK_POINTER_MASK = 1u << MACHINE_RESOURCE_STACK_POINTER,
    MACHINE_RESOURCE_FP_ENVIRONMENT_MASK = 1u << MACHINE_RESOURCE_FP_ENVIRONMENT,
    MACHINE_RESOURCE_VECTOR_STATE_MASK = 1u << MACHINE_RESOURCE_VECTOR_STATE,
    MACHINE_RESOURCE_CONTROL_MASK = 1u << MACHINE_RESOURCE_CONTROL,
} MachineResourceMask;

// One source mark per lowered IR instruction: the machine row where its
// rows begin and the canonical source position, consumed by the encoder's
// per-row offsets into per-function line entries.
typedef struct MachineLineMark MachineLineMark;
struct MachineLineMark
{
    u32 row;
    u32 source;
    // The byte offset inside that source, not a resolved position: the line
    // and column are recovered once per emitted row, which is the same
    // on-demand contract the canonical path follows.
    u32 offset;
    u32 reserved;
};
BUSTER_CT_CHECK(sizeof(MachineLineMark) == 16);

typedef struct MachineSwitchCase MachineSwitchCase;
struct MachineSwitchCase
{
    u64 value;
    u32 target_block;
    u32 reserved;
};
BUSTER_CT_CHECK(sizeof(MachineSwitchCase) == 16);

// AArch64 condition codes as encoded in B.cond and CSINC.
typedef enum MachineA64Condition
{
    MACHINE_A64_CONDITION_EQUAL = 0x0,
    MACHINE_A64_CONDITION_NOT_EQUAL = 0x1,
    MACHINE_A64_CONDITION_ABOVE_EQUAL = 0x2, // hs
    MACHINE_A64_CONDITION_BELOW = 0x3,       // lo
    MACHINE_A64_CONDITION_ABOVE = 0x8,       // hi
    MACHINE_A64_CONDITION_BELOW_EQUAL = 0x9, // ls
    MACHINE_A64_CONDITION_GREATER_EQUAL = 0xa,
    MACHINE_A64_CONDITION_LESS = 0xb,
    MACHINE_A64_CONDITION_GREATER = 0xc,
    MACHINE_A64_CONDITION_LESS_EQUAL = 0xd,
} MachineA64Condition;

// Per-row dynamic flag on x86-64 CALL and RET rows: a vector-class value —
// staged ZMM arguments at a call, the ZMM0 return at a return — is live in
// registers across this row, so the encoder's transition-hygiene vzeroupper
// (which zeroes bits 128+ of ZMM0-15) must not fire here. Calls keep their
// low flag bits for the variadic AL protocol; SWITCH rows, whose flags hold
// a case count, never carry this bit.
#define MACHINE_X64_INSTRUCTION_FLAG_VECTOR_LIVE (1u << 15)

// x86 condition-code low nibble as used by SETcc (0x0f 0x90+cc) and Jcc
// (0x0f 0x80+cc).
typedef enum MachineX64Condition
{
    MACHINE_X64_CONDITION_BELOW = 0x2,
    MACHINE_X64_CONDITION_ABOVE_EQUAL = 0x3,
    MACHINE_X64_CONDITION_EQUAL = 0x4,
    MACHINE_X64_CONDITION_NOT_EQUAL = 0x5,
    MACHINE_X64_CONDITION_BELOW_EQUAL = 0x6,
    MACHINE_X64_CONDITION_ABOVE = 0x7,
    MACHINE_X64_CONDITION_LESS = 0xc,
    MACHINE_X64_CONDITION_GREATER_EQUAL = 0xd,
    MACHINE_X64_CONDITION_LESS_EQUAL = 0xe,
    MACHINE_X64_CONDITION_GREATER = 0xf,
} MachineX64Condition;

typedef enum MachineOperandRole
{
    MACHINE_OPERAND_ROLE_NONE,
    MACHINE_OPERAND_ROLE_USE,
    MACHINE_OPERAND_ROLE_DEFINE,
    MACHINE_OPERAND_ROLE_USE_DEFINE,
    MACHINE_OPERAND_ROLE_COUNT,
} MachineOperandRole;

#define MACHINE_OPERAND_ROLE_BITS 2u
#define MACHINE_OPERAND_CLASS_SHIFT MACHINE_OPERAND_ROLE_BITS
BUSTER_CT_CHECK(MACHINE_OPERAND_ROLE_COUNT <= (1u << MACHINE_OPERAND_ROLE_BITS));
BUSTER_CT_CHECK(MACHINE_REGISTER_CLASS_COUNT <= (1u << 3));

#define MACHINE_OPCODE_ATTRIBUTE_TERMINATOR (1u << 0)
#define MACHINE_OPCODE_ATTRIBUTE_CALL (1u << 1)
#define MACHINE_OPCODE_ATTRIBUTE_SIDE_EFFECTS (1u << 2)
#define MACHINE_OPCODE_ATTRIBUTE_REMATERIALIZABLE (1u << 3)
#define MACHINE_OPCODE_ATTRIBUTE_FLAGS_DEFINE (1u << 4)
#define MACHINE_OPCODE_ATTRIBUTE_FLAGS_USE (1u << 5)
// The encoder sequence pins its operand registers (divides, shift counts,
// setcc bytes, switch chains, atomic layouts, copy scratches), so operands
// take the target's fixed per-slot scratch assignment and every allocator
// must stand clear of them.
#define MACHINE_OPCODE_ATTRIBUTE_CONSTRAINED (1u << 6)
#define MACHINE_OPCODE_ATTRIBUTE_MEMORY (1u << 7)
#define MACHINE_OPCODE_ATTRIBUTE_BUNDLE (1u << 8)
#define MACHINE_OPCODE_ATTRIBUTE_EXPANDS (1u << 9)

typedef struct MachineOpcodeInfo MachineOpcodeInfo;
struct MachineOpcodeInfo
{
    String8 name;
    u8 operand_count;
    // Per inline slot: role in the low two bits, register class above them.
    u8 operand_info[4];
    // Tied slot pair encoded as (destination + 1) | ((source + 1) << 4);
    // zero means no tie.
    u8 tied_pair;
    u8 early_clobber_mask;
    u16 fixed_register_set;
    u16 memory_fold_alternate;
    // Reserved layout-neutral seam. Recipe lookup is kept in a separate
    // read-only projection so opcode metadata remains constant and safe to
    // query concurrently; use machine_opcode_emit_recipe().
    MachineEmitRecipeId emit_recipe;
    u16 attributes;
    // Extra registers the opcode's encoder sequence scribbles on beyond its
    // declared operands; owners must vacate before the instruction runs.
    u64 clobber_mask;
    // Expanded target metadata.  All fields are zero for legacy rows, which
    // preserves aggregate-initializer compatibility; accessors below derive
    // conservative defaults from the old attributes when needed.
    u16 form_set;
    u8 schedule_class;
    u8 reserved_metadata;
    u16 expansion_recipe;
    u8 memory_effect;
    // Memory operand is slot + 1; zero means this opcode has no memory
    // operand, allowing slot zero to remain unambiguous in zero defaults.
    u8 memory_operand;
    u8 memory_flags;
    u8 latency;
    u8 throughput;
    u8 bundle;
    u8 fixed_register_mask;
    // Explicit fixed physical register per operand slot.  A set bit in
    // fixed_register_mask makes the corresponding byte meaningful.
    u8 fixed_registers[4];
    u64 implicit_physical_uses;
    u64 implicit_physical_defs;
    u64 implicit_resource_uses;
    u64 implicit_resource_defs;
};

#define MACHINE_OPCODE_INFO_HAS_FIXED_REGISTERS 1

// Upper bound on any target's unified register file — the general file plus
// the vector file behind it; every allocator mask is one u64 over this
// numbering, so the limit may not pass sixty-four.
#define MACHINE_TARGET_REGISTER_LIMIT 48u
// Upper bound on the callee-saved registers the QUALITY pass may pin;
// each target lists its own file in preference order and reports how much
// of it is real through `quality_pin_register_count`.
#define MACHINE_TARGET_QUALITY_PIN_LIMIT 9u

// Target-supplied allocator parameters: the register file and the opcode
// identities the shared allocators special-case. One static instance per
// machine backend; the selector stamps it into every MachineFunction it
// builds, so the allocators themselves carry no target assumptions.
typedef struct MachineTargetDescription MachineTargetDescription;
struct MachineTargetDescription
{
    // Registers the local allocator may own between instructions; the
    // stack/frame registers stay out. The callee-saved subset survives
    // calls, costs one prologue save per function that binds it, and its
    // saves carry unwind actions.
    u64 allocatable_mask;
    u64 callee_saved_mask;
    u32 register_count;
    // The fixed scratch register per inline operand slot, used by MIR_STACK
    // for every operand and by the allocators for constrained opcodes.
    u8 slot_scratch[4];
    // Full-width register copy: coalescible, and the encoder emits nothing
    // when both operands land on the same register.
    u16 copy_opcode;
    // Whole-definition constant materialization — the rematerialization
    // recipe's shape.
    u16 constant_opcode;
    // Indirect call and the fixed register its callee pointer rides in,
    // immune to the argument registers and any variadic setup.
    u16 indirect_call_opcode;
    // Table dispatch, or MACHINE_OPCODE_INVALID for a target without one.
    // Its targets cannot host per-edge repairs, so the edge contracts
    // force them cold; every other terminator classifies structurally by
    // its block-ref operand count.
    u16 switch_opcode;
    // General-to-float bridge and the fixed general register it stages
    // through, never an argument register.
    u16 float_bridge_opcode;
    u8 indirect_call_register;
    u8 float_bridge_register;
    u8 quality_pin_registers[MACHINE_TARGET_QUALITY_PIN_LIMIT];
    u8 quality_pin_register_count;
    // The vector class's registers in the unified numbering, or zero for a
    // target whose selector never produces vector virtual registers. The
    // callee-saved subset is the intersection with `callee_saved_mask`;
    // System V x86-64 has none, so every vector value dies at a call.
    u64 vector_allocatable_mask;
    // Full-width vector register copy, coalescible like `copy_opcode`.
    u16 vector_copy_opcode;
    // Prologue order: the callee-saved pushes precede the frame-pointer
    // establishment rather than following it. That is the Win64 prologue
    // Windows unwinding is defined over — its codes restore a pushed register
    // off the stack pointer they are recovered with, and only a push
    // described before UWOP_SET_FPREG is guaranteed to be reached with that
    // pointer already correct. The frame slots stay at negative offsets from
    // the frame pointer either way; only the saves move above it.
    u8 saves_precede_frame_pointer;
    u8 reserved[1];
    // The fixed vector scratch per operand slot, the MIR_STACK counterpart
    // of `slot_scratch` for vector-class operand slots.
    u8 vector_slot_scratch[4];
};

// The contiguous per-function machine streams the builder flattens into.
// Owned by whatever arena the caller passed to `machine_function_builder_finish`;
// intended to live in a temporal scope released after encoding. The
// immediate pool and stack-slot table are cold selector-owned side arrays:
// MACHINE_REF_IMMEDIATE payloads index `immediates`, MACHINE_REF_STACK_SLOT
// payloads index `stack_slot_sizes` (slot offsets are frame-layout output,
// not selection output).
typedef struct MachineFunction MachineFunction;

// Side data for the SysV x86-64 VA_ARG row.  The selector records the
// ABI-classified eightbytes once; the encoder then emits the bounded register
// save-area/overflow-area sequence without consulting IR or calling back into
// the canonical emitter.  A memory-class part has `is_memory` set and makes
// the row use the overflow path directly.
#define MACHINE_VA_ARG_PART_LIMIT 2
typedef struct MachineVaArgPart MachineVaArgPart;
struct MachineVaArgPart
{
    u32 value_offset;
    u32 save_offset;
    u8 size;
    u8 is_float;
    u8 is_memory;
    u8 reserved;
};

typedef struct MachineVaArg MachineVaArg;
struct MachineVaArg
{
    u32 size;
    u32 alignment;
    u32 stack_size;
    u32 part_count;
    MachineVaArgPart parts[MACHINE_VA_ARG_PART_LIMIT];
    u32 result_slot;
    u8 result_is_frame;
    u8 scalar_size;
    u8 reserved[2];
};

struct MachineFunction
{
    MachineInstruction* instructions;
    MachineVirtualRegister* virtual_registers;
    MachineBlock* blocks;
    MachineEdge* edges;
    MachineBlockParameter* block_parameters;
    MachineRef* edge_copy_sources;
    u64* immediates;
    u32* stack_slot_sizes;
    // Power-of-two start alignment per stack slot, at most sixteen.
    u32* stack_slot_alignments;
    // Direct-call callees, indexed by MACHINE_X64_CALL_DIRECT payloads.
    IrSymbolId* call_targets;
    MachineSwitchCase* switch_cases;
    MachineLineMark* line_marks;
    MachineVaArg* va_args;
    // The backend that selected this function; placement reads its register
    // file and special-opcode identities from here.
    MachineTargetDescription const* target;
    u32 instruction_count;
    u32 virtual_register_count;
    u32 block_count;
    u32 edge_count;
    u32 block_parameter_count;
    u32 edge_copy_source_count;
    u32 immediate_count;
    u32 stack_slot_count;
    u32 call_target_count;
    u32 switch_case_count;
    u32 line_mark_count;
    u32 va_arg_count;
    // Fixed outgoing argument area, in bytes, or zero for a function whose
    // calls need none. Win64 owns its callees' shadow space and stack
    // arguments in its own frame rather than pushing them, so the stack
    // pointer never moves inside the body and the unwind codes describe the
    // frame at every instruction. The area is a stack slot like any other,
    // pinned by the placement to the bottom of the frame so its base is
    // exactly the stack pointer a call sees.
    u32 outgoing_bytes;
    u32 outgoing_slot;
};

// AArch64 physical general registers in encoding order; 31 encodes SP or
// the zero register depending on the instruction, and is never allocatable.
typedef enum MachineA64Register
{
    MACHINE_A64_X0,
    MACHINE_A64_X1,
    MACHINE_A64_X2,
    MACHINE_A64_X3,
    MACHINE_A64_X4,
    MACHINE_A64_X5,
    MACHINE_A64_X6,
    MACHINE_A64_X7,
    MACHINE_A64_X8,
    MACHINE_A64_X9,
    MACHINE_A64_X10,
    MACHINE_A64_X11,
    MACHINE_A64_X12,
    MACHINE_A64_X13,
    MACHINE_A64_X14,
    MACHINE_A64_X15,
    MACHINE_A64_X16,
    MACHINE_A64_X17,
    MACHINE_A64_X18,
    MACHINE_A64_X19,
    MACHINE_A64_X20,
    MACHINE_A64_X21,
    MACHINE_A64_X22,
    MACHINE_A64_X23,
    MACHINE_A64_X24,
    MACHINE_A64_X25,
    MACHINE_A64_X26,
    MACHINE_A64_X27,
    MACHINE_A64_X28,
    MACHINE_A64_X29,
    MACHINE_A64_X30,
    MACHINE_A64_SP,
    MACHINE_A64_REGISTER_COUNT,
} MachineA64Register;

// x86-64 physical registers: the general file in encoding order, then the
// vector file as ZMM0-31 at indices 16-47. One unified numbering keeps every
// allocator mask a single u64 and lets the shared scan, contracts, and edit
// stream carry both classes without a second file of state; the encoder
// recovers the ZMM number by subtracting MACHINE_X64_ZMM0. ZMM16-31 exist
// wherever EVEX itself does (AVX512F in 64-bit mode), which the vector
// vocabulary's feature gate already requires before any vector virtual
// register is selected.
typedef enum MachineX64Register
{
    MACHINE_X64_RAX,
    MACHINE_X64_RCX,
    MACHINE_X64_RDX,
    MACHINE_X64_RBX,
    MACHINE_X64_RSP,
    MACHINE_X64_RBP,
    MACHINE_X64_RSI,
    MACHINE_X64_RDI,
    MACHINE_X64_R8,
    MACHINE_X64_R9,
    MACHINE_X64_R10,
    MACHINE_X64_R11,
    MACHINE_X64_R12,
    MACHINE_X64_R13,
    MACHINE_X64_R14,
    MACHINE_X64_R15,
    MACHINE_X64_ZMM0,
    MACHINE_X64_ZMM1,
    MACHINE_X64_ZMM2,
    MACHINE_X64_ZMM3,
    MACHINE_X64_ZMM4,
    MACHINE_X64_ZMM5,
    MACHINE_X64_ZMM6,
    MACHINE_X64_ZMM7,
    MACHINE_X64_ZMM8,
    MACHINE_X64_ZMM9,
    MACHINE_X64_ZMM10,
    MACHINE_X64_ZMM11,
    MACHINE_X64_ZMM12,
    MACHINE_X64_ZMM13,
    MACHINE_X64_ZMM14,
    MACHINE_X64_ZMM15,
    MACHINE_X64_ZMM16,
    MACHINE_X64_ZMM17,
    MACHINE_X64_ZMM18,
    MACHINE_X64_ZMM19,
    MACHINE_X64_ZMM20,
    MACHINE_X64_ZMM21,
    MACHINE_X64_ZMM22,
    MACHINE_X64_ZMM23,
    MACHINE_X64_ZMM24,
    MACHINE_X64_ZMM25,
    MACHINE_X64_ZMM26,
    MACHINE_X64_ZMM27,
    MACHINE_X64_ZMM28,
    MACHINE_X64_ZMM29,
    MACHINE_X64_ZMM30,
    MACHINE_X64_ZMM31,
    MACHINE_X64_REGISTER_COUNT,
} MachineX64Register;

typedef enum MachineEditKind
{
    MACHINE_EDIT_NONE,
    MACHINE_EDIT_RELOAD, // subject vreg loads into location preg at point
    MACHINE_EDIT_SPILL,  // subject vreg stores from location preg at point
    MACHINE_EDIT_COPY,   // subject preg copies into location preg at point
    // subject immediate index materializes into location preg at point:
    // the reload of a value whose whole definition is a constant.
    MACHINE_EDIT_REMATERIALIZE,
    MACHINE_EDIT_KIND_COUNT,
} MachineEditKind;

// Result of selecting one canonical typed-IR function into machine IR.
// `supported` false is an explicit per-function fallback: `failed_opcode`
// names the first construct outside the selected subset.
typedef struct MachineSelectResult MachineSelectResult;
struct MachineSelectResult
{
    MachineFunction function;
    IrOpcode failed_opcode;
    bool supported;
    bool returns_value;
    // Set only after a target selector has finished all typed-builder streams
    // and side tables. Replayed or manually assembled machine IR keeps this
    // false and must pass the structural verifier before consumption.
    bool selector_certified;
    u8 reserved;
    // Selector expansion statistics: typed instructions consumed and machine
    // rows produced. SIMD operations are counted during that same typed-IR
    // walk so accepted machine functions need no source-IR rescan.
    u32 selected_typed_instructions;
    u32 machine_instructions;
    u32 simd_operation_count;
    // Reserved matcher telemetry storage. Target selectors no longer run the
    // declarative matcher on their hot path, but retaining this cold block
    // preserves the measured favorable layout of selection results.
    MachineSelectionCounters selection_counters;
};

// Stage-9 scheduling output: a reordered copy of the input function, or the
// input itself when no row moved. Instructions, virtual registers (their
// definition points remapped), and line marks (remapped and re-sorted) are
// fresh arrays; every other side table is shared with the input, whose own
// arrays are never modified. The caller keeps whichever of the two placements
// models cheaper, so the pass cannot lose by construction.
typedef struct MachineScheduleResult MachineScheduleResult;
struct MachineScheduleResult
{
    MachineFunction function;
    bool moved;
    u8 reserved[7];
};

// MIR_STACK placement: every virtual register owns one 8-byte frame slot and
// every operand round-trips through a fixed scratch register. This is the
// selector/encoder verification mode, not an allocator.
typedef struct MachineStackPlacement MachineStackPlacement;
struct MachineStackPlacement
{
    MachineEdit* edits;
    // Frame offsets (positive displacements below the frame base) per vreg
    // slot and per selector stack slot.
    u32* virtual_register_offsets;
    u32* stack_slot_offsets;
    u32 edit_count;
    u32 frame_size;
    // Distance from the frame pointer to the caller's stack frame, beyond the
    // saved frame pointer and return address every x86-64 frame carries. It is
    // the callee-saved save area wherever the prologue pushes those registers
    // before establishing the frame pointer (Win64), because the frame pointer
    // then lands below them; zero where they are pushed after it, which is
    // what leaves the incoming arguments a fixed sixteen bytes away.
    u32 incoming_base;
    // Scratch register per operand slot for every instruction, packed four
    // u8 per instruction, parallel to the instruction array.
    u8* operand_registers;
    u32 reload_count;
    u32 spill_count;
    u32 copy_count;
    u32 rematerialize_count;
    // Values QUALITY gave a register for their whole lifetime.
    u32 pinned_register_count;
    // Subset of pinned_register_count whose reservation covers a split
    // sub-span of the value's live range rather than the whole of it.
    u32 split_register_count;
    // Subset of spill_count emitted at block boundaries — edge-contract
    // conformance today, the unconditional write-back before it — rather
    // than by eviction pressure: the two want different fixes.
    u32 boundary_spill_count;
    // Reloads and register copies the edge-contract conformance emitted,
    // split from the eviction-driven traffic for the same reason.
    u32 boundary_reload_count;
    u32 boundary_copy_count;
    // Callee-saved registers the placement assigned; the encoder pushes and
    // pops them around the frame and the unwind actions record the pushes.
    u64 callee_saved_mask;
    bool valid;
    u8 reserved[3];
};

// A relocation site: the function-relative offset of the field to patch
// and the call-target index it must resolve to. x86-64 sites are rel32
// fields; AArch64 sites are branch words, or eight-byte inline literals
// when `absolute` is set.
typedef struct MachineCallSite MachineCallSite;
struct MachineCallSite
{
    u32 code_offset;
    u32 target;
    u32 absolute;
    // The patched field resolves thread-locally (x86-64 TPOFF); the module
    // relocation row carries the flag through unchanged.
    u32 is_thread_local;
};

typedef struct MachineEncodeResult MachineEncodeResult;
struct MachineEncodeResult
{
    u8* bytes;
    u32 byte_count;
    u32* block_offsets;
    // Function-relative offset of every instruction row's first byte,
    // ahead of its reload edits, parallel to the instruction array.
    u32* row_offsets;
    MachineCallSite* call_sites;
    // Function-relative offset of each emitted epilogue's first
    // instruction, one per return row; the AArch64 encoder fills these for
    // the Windows unwind data, the x86-64 encoder leaves them empty.
    u32* epilog_offsets;
    u32 call_site_count;
    u32 epilog_count;
    bool valid;
    u8 reserved[3];
    // x86 exact-form encoder telemetry. A failed encode still returns the
    // attempted counts so the caller can aggregate them before falling back.
    u32 exact_attempts;
    u32 exact_successes;
    u32 exact_failures;
};

// Chunked construction: one selection pass appends rows into fixed-size arena
// chunks while exact counts accumulate, then one sequential flatten produces
// the contiguous final arrays. This avoids both per-row heap allocation and
// gross contiguous over-reservation, which commits pages under Buster's
// arenas. Chunk size is a measured default, not a contract.
#define MACHINE_BUILDER_CHUNK_BYTES BUSTER_KB(16)

typedef struct MachineBuilderChunk MachineBuilderChunk;
struct MachineBuilderChunk
{
    MachineBuilderChunk* next;
    u32 count;
    u32 reserved;
};

typedef struct MachineBuilderStream MachineBuilderStream;
struct MachineBuilderStream
{
    MachineBuilderChunk* first;
    MachineBuilderChunk* last;
    u32 total_count;
    u32 element_size;
    u32 chunk_capacity;
    u32 reserved;
};

typedef struct MachineFunctionBuilder MachineFunctionBuilder;
struct MachineFunctionBuilder
{
    Arena* arena;
    MachineBuilderStream instructions;
    MachineInstruction* instruction_cursor;
    MachineInstruction* instruction_end;
    MachineBuilderStream virtual_registers;
    MachineBuilderStream blocks;
    MachineBuilderStream edges;
    MachineBuilderStream block_parameters;
    MachineBuilderStream edge_copy_sources;
    u32 open_block;
    u32 open_block_first_instruction;
    bool block_is_open;
    bool point_capacity_exceeded;
    u8 reserved[6];
};

typedef enum MachineVerifyError
{
    MACHINE_VERIFY_NONE,
    MACHINE_VERIFY_BLOCK_RANGE,
    MACHINE_VERIFY_INSTRUCTION_COVERAGE,
    MACHINE_VERIFY_OPCODE,
    MACHINE_VERIFY_OPERAND_REFERENCE,
    MACHINE_VERIFY_OPERAND_SLOT,
    MACHINE_VERIFY_TERMINATOR,
    MACHINE_VERIFY_VIRTUAL_REGISTER_DEFINITION,
    MACHINE_VERIFY_POINT_CAPACITY,
    MACHINE_VERIFY_EDGE_RANGE,
    MACHINE_VERIFY_EDGE_COPY,
    MACHINE_VERIFY_BLOCK_PARAMETER,
    MACHINE_VERIFY_CONSTRAINT,
    MACHINE_VERIFY_COUNT,
} MachineVerifyError;

typedef struct MachineVerifyResult MachineVerifyResult;
struct MachineVerifyResult
{
    MachineVerifyError error;
    u32 block;
    u32 instruction;
    u32 operand;
};

// Test-only replay serialization. Versioned; readers reject unknown versions
// rather than guessing. Never used in production compilation.
#define MACHINE_REPLAY_MAGIC 0x52494d42u // "BMIR"
#define MACHINE_REPLAY_VERSION 2u

BUSTER_F_DECL MachineRef machine_ref_make(MachineRefKind kind, u32 payload);
BUSTER_F_DECL MachineRefKind machine_ref_kind(MachineRef ref);
BUSTER_F_DECL u32 machine_ref_payload(MachineRef ref);
BUSTER_F_DECL MachinePoint machine_point_make(u32 instruction_index, MachinePointPhase phase);
BUSTER_F_DECL u32 machine_point_instruction(MachinePoint point);
BUSTER_F_DECL MachinePointPhase machine_point_phase(MachinePoint point);
BUSTER_F_DECL MachineEmitRecipeCategory machine_emit_recipe_category(MachineEmitRecipeId recipe);
BUSTER_F_DECL u16 machine_emit_recipe_index(MachineEmitRecipeId recipe);
BUSTER_F_DECL bool machine_emit_recipe_is_valid(MachineEmitRecipeId recipe);
BUSTER_F_DECL MachineEmitRecipeId machine_opcode_emit_recipe(u16 opcode);
BUSTER_F_DECL u32 machine_x86_64_emit_registry_count(void);
BUSTER_F_DECL MachineX64EmitRegistryEntry const* machine_x86_64_emit_registry_entry(u32 ordinal);
BUSTER_F_DECL MachineX64EmitRegistryEntry const* machine_x86_64_emit_registry_find(MachineOpcode opcode);
BUSTER_F_DECL u32 machine_x86_64_canonical_authority_site_count(void);
BUSTER_F_DECL MachineX64CanonicalAuthoritySite const* machine_x86_64_canonical_authority_site(u32 ordinal);
BUSTER_F_DECL u32 machine_x86_64_neutral_patch_site_count(void);
BUSTER_F_DECL MachineX64NeutralPatchSite const* machine_x86_64_neutral_patch_site(u32 ordinal);
BUSTER_F_DECL void machine_x86_64_exact_prewarm(void);
BUSTER_F_DECL MachineOpcodeInfo const* machine_opcode_info(u16 opcode);
BUSTER_F_DECL u16 machine_opcode_form_set(MachineOpcodeInfo const* info);
BUSTER_F_DECL MachineScheduleClass machine_opcode_schedule_class(MachineOpcodeInfo const* info);
BUSTER_F_DECL MachineOpcodeExpansion machine_opcode_expansion(MachineOpcodeInfo const* info);
BUSTER_F_DECL MachineMemoryEffect machine_opcode_memory_effect(MachineOpcodeInfo const* info);
BUSTER_F_DECL MachineBundleKind machine_opcode_bundle(MachineOpcodeInfo const* info);
BUSTER_F_DECL bool machine_opcode_is_memory(MachineOpcodeInfo const* info);
BUSTER_F_DECL u32 machine_opcode_fixed_register(MachineOpcodeInfo const* info, u32 slot);
BUSTER_F_DECL u32 machine_opcode_memory_operand(MachineOpcodeInfo const* info);
BUSTER_F_DECL bool machine_opcode_operand_is_tied(MachineOpcodeInfo const* info, u32 destination_slot, u32 source_slot);
BUSTER_F_DECL bool machine_opcode_operand_is_early_clobber(MachineOpcodeInfo const* info, u32 slot);
BUSTER_F_DECL bool machine_opcode_has_constraints(MachineOpcodeInfo const* info);
BUSTER_F_DECL MachineTargetDescription const* machine_target_x86_64(void);
// The Win64 register file: the same allocatable set with RSI and RDI moved
// into the callee-saved half, and only the volatile vector registers.
BUSTER_F_DECL MachineTargetDescription const* machine_target_x86_64_windows(void);
BUSTER_F_DECL MachineTargetDescription const* machine_target_aarch64(void);
BUSTER_F_DECL void machine_stream_initialize(MachineBuilderStream* stream, u64 element_size);
BUSTER_F_DECL void* machine_stream_append(Arena* arena, MachineBuilderStream* stream);
BUSTER_F_DECL void machine_stream_flatten(MachineBuilderStream* stream, void* destination);
BUSTER_F_DECL MachineFunctionBuilder machine_function_builder_begin(Arena* arena);
BUSTER_F_DECL u32 machine_builder_virtual_register(MachineFunctionBuilder* builder, MachineVirtualRegister virtual_register);
BUSTER_F_DECL u32 machine_builder_block_begin(MachineFunctionBuilder* builder);
BUSTER_F_DECL u32 machine_builder_instruction(MachineFunctionBuilder* builder, MachineInstruction instruction);
BUSTER_F_DECL void machine_builder_block_end(MachineFunctionBuilder* builder, MachineBlock block);
BUSTER_F_DECL u32 machine_builder_block_parameter(MachineFunctionBuilder* builder, MachineBlockParameter parameter);
BUSTER_F_DECL u32 machine_builder_edge_copy_source(MachineFunctionBuilder* builder, MachineRef source);
BUSTER_F_DECL u32 machine_builder_edge(MachineFunctionBuilder* builder, MachineEdge edge);
BUSTER_F_DECL MachineFunction machine_function_builder_finish(Arena* arena, MachineFunctionBuilder* builder);
// Stamps every block's `frequency_class` with its loop-nesting depth,
// derived from backward block references (block-ref operands and
// switch-case targets naming a block at or before their own). The class is
// a static execution-frequency estimate consumed only by QUALITY's pin
// economics, which runs the stamp itself before pricing traffic — FAST and
// MIR_STACK never read a class and never pay for the walk. Idempotent, so
// re-stamping a scheduled function that shares its blocks array with the
// original is safe.
BUSTER_F_DECL void machine_function_stamp_frequency_classes(MachineFunction* function);
BUSTER_F_DECL MachineVerifyResult machine_verify_function(MachineFunction* function);
BUSTER_F_DECL ByteSlice machine_replay_serialize(Arena* arena, MachineFunction* function);
BUSTER_F_DECL bool machine_replay_deserialize(Arena* arena, ByteSlice bytes, MachineFunction* function);
// Dispatches on the target architecture; each backend rejects a target it
// does not own with an explicit unsupported result.
BUSTER_F_DECL MachineSelectResult machine_select_canonical_function(Arena* arena, IrProgram* program, IrFunction* function, Target target);
// Codegen calls this only after the canonical validator or a private producer
// certificate has established ownership and value integrity. The target pass
// can then accumulate its compact value facts inside an existing row walk.
BUSTER_F_DECL MachineSelectResult machine_select_validated_canonical_function(Arena* arena, IrProgram* program, IrFunction* function, Target target);
BUSTER_F_DECL MachineSelectResult machine_select_canonical_function_x86_64(Arena* arena, IrProgram* program, IrFunction* function, Target target,
                                                                           bool assume_validated);
BUSTER_F_DECL MachineSelectResult machine_select_canonical_function_aarch64(Arena* arena, IrProgram* program, IrFunction* function, Target target,
                                                                            bool assume_validated);
BUSTER_F_DECL MachineScheduleResult machine_schedule_function(Arena* arena, MachineFunction* function);
BUSTER_F_DECL MachineStackPlacement machine_stack_placement_build(Arena* arena, MachineFunction* function);
BUSTER_F_DECL MachineStackPlacement machine_fast_placement_build(Arena* arena, MachineFunction* function);

// A split pin's handoff plan, built and validated by QUALITY. The entry
// names the block whose contract installs the value into its pinned
// register, so every edge entering the span conforms it there through the
// ordinary edge machinery — and a backward edge, which the span itself
// covers, installs nothing. A store names a landing-pad row — the head of
// a block every path out of the span passes, whose predecessors all sit
// inside the span and which no loop region contains — where the value
// writes back to its slot while its register still holds it.
typedef struct MachinePinSplitEntry MachinePinSplitEntry;
struct MachinePinSplitEntry
{
    u32 block;
    u32 virtual_register;
};

typedef struct MachinePinSplitStore MachinePinSplitStore;
struct MachinePinSplitStore
{
    u32 row;
    u32 virtual_register;
};

// The FAST scan with explicit pins: `pinned_registers` holds a physical
// register per virtual register or UINT32_MAX and `pinned_mask` collects
// them. `pin_active_masks` — one register mask per instruction — scopes
// each reservation to the span that wants it, so the local scan owns the
// register everywhere outside; null reserves `pinned_mask` for the whole
// function. `pin_span_starts`/`pin_span_ends` give each pinned value its
// span as an inclusive instruction range; null means every span covers
// every occurrence of its value, which is the stage-8 shape. A span that
// covers only part of its value's live range is a split: the value is an
// ordinary scan citizen outside the span, and the caller supplies the
// boundary plan — `split_entries` (contract installs, one per split) and
// `split_stores` (landing-pad write-backs for values living past their
// span, sorted by row). QUALITY derives its pins through this.
BUSTER_F_DECL MachineStackPlacement machine_fast_placement_build_pinned(Arena* arena, MachineFunction* function, u32 const* pinned_registers,
                                                                        u64 pinned_mask, u64 const* pin_active_masks, u32 const* pin_span_starts,
                                                                        u32 const* pin_span_ends, MachinePinSplitEntry const* split_entries,
                                                                        u32 split_entry_count, MachinePinSplitStore const* split_stores,
                                                                        u32 split_store_count);
// Everything the FAST scan derives from the function alone, independent of
// any pin set: rematerialization recipes, block adjacency and cold entries,
// liveness (defining blocks, last uses, escapes), per-row next calls, the
// class-trimmed register-file width, and — for QUALITY's global layer —
// whole-function touch intervals with their constrained-opcode
// disqualifications and the raw backward-edge spans. It also folds the
// callee-saved subset of implicit opcode clobbers into that walk. Computed
// once and shared across every scan of the same function: QUALITY's baseline
// and pinned runs read one prepass instead of re-deriving it all per run.
typedef struct MachineFastPrepass MachineFastPrepass;
struct MachineFastPrepass
{
    u32* rematerialize_immediates;
    u32* definition_blocks;
    u32* last_use;
    u8* escapes;
    u32* next_call;
    // One compact SoA word per instruction. Six four-bit lane masks record
    // physical, virtual, block, use, define, and use-define operands after
    // the prepass has classified the row once. A high state bit separates
    // unconstrained virtual-only dataflow from irregular rows; FAST and
    // QUALITY consume the compact homogeneous facts instead of repeatedly
    // decoding tagged refs and opcode policy.
    u32* operand_masks;
    u32* predecessor_offsets;
    u32* predecessor_list;
    u8* cold_blocks;
    u32* interval_starts;
    u32* interval_ends;
    u8* disqualified;
    // Backward-edge spans packed (start << 32) | end in block walk order,
    // unsorted; QUALITY sorts and merges its own copy.
    u64* loop_spans;
    u32 loop_span_count;
    u32 active_register_count;
    // Callee-saved subset of implicit opcode clobbers, folded into the
    // prepass instruction walk so each FAST placement need not rescan rows.
    u64 callee_saved_clobber_mask;
    bool valid;
    u8 reserved[3];
};
BUSTER_F_DECL MachineFastPrepass machine_fast_prepass_build(Arena* arena, MachineFunction* function);
// The pinned scan against an already-built prepass of the same function,
// same pin/span/split contract as `machine_fast_placement_build_pinned`.
BUSTER_F_DECL MachineStackPlacement machine_fast_placement_build_prepassed(Arena* arena, MachineFunction* function, MachineFastPrepass const* prepass,
                                                                           u32 const* pinned_registers, u64 pinned_mask, u64 const* pin_active_masks,
                                                                           u32 const* pin_span_starts, u32 const* pin_span_ends,
                                                                           MachinePinSplitEntry const* split_entries, u32 split_entry_count,
                                                                           MachinePinSplitStore const* split_stores, u32 split_store_count);
// QUALITY: a global pass pins the highest-weight non-overlapping live
// intervals to registers for their loop-extended live spans — split down
// to one hot loop region when the whole interval cannot be placed — then
// the same local scan places everything else around them.
BUSTER_F_DECL MachineStackPlacement machine_quality_placement_build(Arena* arena, MachineFunction* function);
BUSTER_F_DECL MachineEncodeResult machine_encode_x86_64(Arena* arena, MachineFunction* function, MachineStackPlacement* placement);
BUSTER_F_DECL MachineEncodeResult machine_encode_aarch64(Arena* arena, MachineFunction* function, MachineStackPlacement* placement);

#if BUSTER_INCLUDE_TESTS
typedef enum MachineFastPickerTestCase
{
    MACHINE_FAST_PICK_TEST_PREFERRED_FREE = 1u << 0,
    MACHINE_FAST_PICK_TEST_LOWEST_OTHER_FREE = 1u << 1,
    MACHINE_FAST_PICK_TEST_DEAD_FIRST = 1u << 2,
    MACHINE_FAST_PICK_TEST_LRU_ORDER = 1u << 3,
    MACHINE_FAST_PICK_TEST_CTZ_GUARD = 1u << 4,
} MachineFastPickerTestCase;
BUSTER_F_DECL u32 machine_fast_picker_test_cases(void);

// Test-only byte seam for the AArch64 unsigned memory helpers. It keeps the
// generated-form differential tests independent of the private encoder state
// while preserving the production-only machine API surface.
BUSTER_F_DECL bool machine_a64_test_emit_unsigned_memory(u8* bytes, u32 capacity, u32 register_number, u32 base_register, u32 offset, u32 size,
                                                         bool store, bool frame_relative, u32* byte_count, bool* error);
// Test-only byte seam for the generated scalar/register/SP/RET/FMOV rows.
// Operands are physical register numbers in slots 0..2; payload carries the
// immediate/condition/vector register used by the selected opcode. The seam
// invokes the same MachineOpcode-to-production-form mapping as the encoder,
// while keeping that mapping out of the production API when tests are absent.
BUSTER_F_DECL bool machine_a64_test_emit_generated_opcode(u8* bytes, u32 capacity, u16 opcode, u32 operand0, u32 operand1, u32 operand2,
                                                          u32 payload, u32* byte_count, bool* error);
// Test-only branch-relaxation seams.  These wrappers exercise the private
// fixed-size position-independent transfer and its B/BCC tier classifier
// without changing the production machine API.
BUSTER_F_DECL bool machine_a64_test_emit_long_branch(u8* bytes, u32 capacity, s64 displacement, u32* byte_count);
BUSTER_F_DECL u8 machine_a64_test_branch_relaxation_tier(u16 opcode, u32 condition, s64 displacement);
typedef struct MachineA64TestSparseFixup MachineA64TestSparseFixup;
struct MachineA64TestSparseFixup
{
    u32 source_offset;
    u32 target_offset;
    u32 row_offset;
    u32 call_offset;
    u32 epilog_offset;
    u16 opcode;
    u8 condition;
    u8 tier;
};
BUSTER_F_DECL bool machine_a64_test_relax_sparse(Arena* arena, u32 code_size, MachineA64TestSparseFixup* fixups, u32 fixup_count,
                                                 u32* final_code_size);
typedef struct MachineX64ExactMapAudit MachineX64ExactMapAudit;
struct MachineX64ExactMapAudit
{
    u32 registry_rows;
    u32 exact_rows;
    u32 exact_plan_valid_rows;
    u32 sequence_rows;
    u32 sequence_variant_valid_rows;
    u32 expansion_rows;
    u32 expansion_nonexact_rows;
    u32 dense_encoding_tables;
    u32 immediate_patch_tables;
    u32 memory_base_tables;
    u32 displacement_patch_tables;
    u32 variable_memory_encoding_tables;
    bool valid;
    u8 reserved[3];
};
BUSTER_F_DECL MachineX64ExactMapAudit machine_x86_64_exact_map_audit(void);
typedef struct MachineX64MetadataShapeCacheAudit MachineX64MetadataShapeCacheAudit;
struct MachineX64MetadataShapeCacheAudit
{
    u32 prepared_rows;
    u32 invalid_rows;
    bool valid;
    u8 reserved[3];
};
BUSTER_F_DECL MachineX64MetadataShapeCacheAudit machine_x86_64_metadata_shape_cache_audit(void);
#endif
