#pragma once

#include <buster/lib/arena.h>
#include <buster/lib/compiler/ir/ir.h>
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
// alternatives, encoding form, and side effects; the row carries only the
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
    MACHINE_X64_BSF32, // dest def, source use; undefined on zero input
    MACHINE_X64_BSF64,
    MACHINE_X64_BSR32,
    MACHINE_X64_BSR64,
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
    MACHINE_X64_PUSH_FRAME,    // slot ref; payload byte offset into the slot
    MACHINE_X64_PUSH_REGISTER, // use
    MACHINE_X64_SUB_RSP,       // payload bytes
    MACHINE_X64_ADD_RSP,       // payload bytes
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
    MACHINE_X64_MFENCE,
    MACHINE_X64_INT3, // debug trap
    MACHINE_X64_UD2,  // unreachable; terminator
    MACHINE_OPCODE_COUNT,
} MachineOpcode;

// One source mark per lowered IR instruction: the machine row where its
// rows begin and the canonical source position, consumed by the encoder's
// per-row offsets into per-function line entries.
typedef struct MachineLineMark MachineLineMark;
struct MachineLineMark
{
    u32 row;
    u32 source;
    u32 line;
    u32 column;
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
    u16 implicit_mask;
    u16 memory_fold_alternate;
    u16 encoding_form;
    u16 attributes;
};

// The contiguous per-function machine streams the builder flattens into.
// Owned by whatever arena the caller passed to `machine_function_builder_finish`;
// intended to live in a temporal scope released after encoding. The
// immediate pool and stack-slot table are cold selector-owned side arrays:
// MACHINE_REF_IMMEDIATE payloads index `immediates`, MACHINE_REF_STACK_SLOT
// payloads index `stack_slot_sizes` (slot offsets are frame-layout output,
// not selection output).
typedef struct MachineFunction MachineFunction;
struct MachineFunction
{
    MachineInstruction* instructions;
    MachineVirtualRegister* virtual_registers;
    MachineBlock* blocks;
    u64* immediates;
    u32* stack_slot_sizes;
    // Power-of-two start alignment per stack slot, at most sixteen.
    u32* stack_slot_alignments;
    // Direct-call callees, indexed by MACHINE_X64_CALL_DIRECT payloads.
    IrSymbolId* call_targets;
    MachineSwitchCase* switch_cases;
    MachineLineMark* line_marks;
    u32 instruction_count;
    u32 virtual_register_count;
    u32 block_count;
    u32 immediate_count;
    u32 stack_slot_count;
    u32 call_target_count;
    u32 switch_case_count;
    u32 line_mark_count;
    u32 reserved;
};

// x86-64 physical general registers in encoding order.
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
    u8 reserved[2];
    // Selector expansion statistics: typed instructions consumed and machine
    // rows produced.
    u32 selected_typed_instructions;
    u32 machine_instructions;
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
    // Scratch register per operand slot for every instruction, packed four
    // u8 per instruction, parallel to the instruction array.
    u8* operand_registers;
    u32 reload_count;
    u32 spill_count;
    u32 copy_count;
    u32 rematerialize_count;
    // Subset of spill_count emitted by the block-boundary write-back
    // rather than by eviction pressure: the two want different fixes.
    u32 boundary_spill_count;
    // Callee-saved registers the placement assigned; the encoder pushes and
    // pops them around the frame and the unwind actions record the pushes.
    u32 callee_saved_mask;
    bool valid;
    u8 reserved[3];
};

// A direct-call relocation site: the function-relative offset of the rel32
// field and the call-target index it must resolve to.
typedef struct MachineCallSite MachineCallSite;
struct MachineCallSite
{
    u32 code_offset;
    u32 target;
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
    u32 call_site_count;
    bool valid;
    u8 reserved[3];
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
    MachineBuilderStream virtual_registers;
    MachineBuilderStream blocks;
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
#define MACHINE_REPLAY_VERSION 1u

BUSTER_F_DECL MachineRef machine_ref_make(MachineRefKind kind, u32 payload);
BUSTER_F_DECL MachineRefKind machine_ref_kind(MachineRef ref);
BUSTER_F_DECL u32 machine_ref_payload(MachineRef ref);
BUSTER_F_DECL MachinePoint machine_point_make(u32 instruction_index, MachinePointPhase phase);
BUSTER_F_DECL u32 machine_point_instruction(MachinePoint point);
BUSTER_F_DECL MachinePointPhase machine_point_phase(MachinePoint point);
BUSTER_F_DECL MachineOpcodeInfo const* machine_opcode_info(u16 opcode);
BUSTER_F_DECL void machine_stream_initialize(MachineBuilderStream* stream, u64 element_size);
BUSTER_F_DECL void* machine_stream_append(Arena* arena, MachineBuilderStream* stream);
BUSTER_F_DECL void machine_stream_flatten(MachineBuilderStream* stream, void* destination);
BUSTER_F_DECL MachineFunctionBuilder machine_function_builder_begin(Arena* arena);
BUSTER_F_DECL u32 machine_builder_virtual_register(MachineFunctionBuilder* builder, MachineVirtualRegister virtual_register);
BUSTER_F_DECL u32 machine_builder_block_begin(MachineFunctionBuilder* builder);
BUSTER_F_DECL u32 machine_builder_instruction(MachineFunctionBuilder* builder, MachineInstruction instruction);
BUSTER_F_DECL void machine_builder_block_end(MachineFunctionBuilder* builder, MachineBlock block);
BUSTER_F_DECL MachineFunction machine_function_builder_finish(Arena* arena, MachineFunctionBuilder* builder);
BUSTER_F_DECL MachineVerifyResult machine_verify_function(MachineFunction* function);
BUSTER_F_DECL ByteSlice machine_replay_serialize(Arena* arena, MachineFunction* function);
BUSTER_F_DECL bool machine_replay_deserialize(Arena* arena, ByteSlice bytes, MachineFunction* function);
BUSTER_F_DECL MachineSelectResult machine_select_canonical_function(Arena* arena, IrProgram* program, IrFunction* function, Target target);
BUSTER_F_DECL MachineStackPlacement machine_stack_placement_build(Arena* arena, MachineFunction* function);
BUSTER_F_DECL MachineStackPlacement machine_fast_placement_build(Arena* arena, MachineFunction* function);
BUSTER_F_DECL MachineEncodeResult machine_encode_x86_64(Arena* arena, MachineFunction* function, MachineStackPlacement* placement);
