#pragma once

#include <buster/lib/arena.h>

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
    MACHINE_OPCODE_COUNT,
} MachineOpcode;

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
// intended to live in a temporal scope released after encoding.
typedef struct MachineFunction MachineFunction;
struct MachineFunction
{
    MachineInstruction* instructions;
    MachineVirtualRegister* virtual_registers;
    MachineBlock* blocks;
    u32 instruction_count;
    u32 virtual_register_count;
    u32 block_count;
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
BUSTER_F_DECL MachineFunctionBuilder machine_function_builder_begin(Arena* arena);
BUSTER_F_DECL u32 machine_builder_virtual_register(MachineFunctionBuilder* builder, MachineVirtualRegister virtual_register);
BUSTER_F_DECL u32 machine_builder_block_begin(MachineFunctionBuilder* builder);
BUSTER_F_DECL u32 machine_builder_instruction(MachineFunctionBuilder* builder, MachineInstruction instruction);
BUSTER_F_DECL void machine_builder_block_end(MachineFunctionBuilder* builder, MachineBlock block);
BUSTER_F_DECL MachineFunction machine_function_builder_finish(Arena* arena, MachineFunctionBuilder* builder);
BUSTER_F_DECL MachineVerifyResult machine_verify_function(MachineFunction* function);
BUSTER_F_DECL ByteSlice machine_replay_serialize(Arena* arena, MachineFunction* function);
BUSTER_F_DECL bool machine_replay_deserialize(Arena* arena, ByteSlice bytes, MachineFunction* function);
