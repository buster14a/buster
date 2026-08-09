#include <buster/lib/compiler/codegen/machine.h>

#include <buster/lib/os.h>
#include <buster/lib/string.h>

BUSTER_F_DECL MachineRef machine_ref_make(MachineRefKind kind, u32 payload)
{
    BUSTER_CHECK(kind < MACHINE_REF_KIND_COUNT);
    BUSTER_CHECK(payload < MACHINE_REF_PAYLOAD_LIMIT);
    return ((u32)kind << MACHINE_REF_PAYLOAD_BITS) | payload;
}

BUSTER_F_DECL MachineRefKind machine_ref_kind(MachineRef ref)
{
    return (MachineRefKind)(ref >> MACHINE_REF_PAYLOAD_BITS);
}

BUSTER_F_DECL u32 machine_ref_payload(MachineRef ref)
{
    return ref & (MACHINE_REF_PAYLOAD_LIMIT - 1u);
}

BUSTER_F_DECL MachinePoint machine_point_make(u32 instruction_index, MachinePointPhase phase)
{
    BUSTER_CHECK(instruction_index < MACHINE_POINT_INSTRUCTION_LIMIT);
    BUSTER_CHECK(phase < MACHINE_POINT_PHASE_COUNT);
    return (instruction_index << MACHINE_POINT_PHASE_BITS) | (u32)phase;
}

BUSTER_F_DECL u32 machine_point_instruction(MachinePoint point)
{
    return point >> MACHINE_POINT_PHASE_BITS;
}

BUSTER_F_DECL MachinePointPhase machine_point_phase(MachinePoint point)
{
    return (MachinePointPhase)(point & ((1u << MACHINE_POINT_PHASE_BITS) - 1u));
}

#define MACHINE_OPERAND_USE_GENERAL ((u8)(MACHINE_OPERAND_ROLE_USE | (MACHINE_REGISTER_CLASS_GENERAL << MACHINE_OPERAND_CLASS_SHIFT)))
#define MACHINE_OPERAND_DEFINE_GENERAL ((u8)(MACHINE_OPERAND_ROLE_DEFINE | (MACHINE_REGISTER_CLASS_GENERAL << MACHINE_OPERAND_CLASS_SHIFT)))
#define MACHINE_OPERAND_USE_DEFINE_GENERAL ((u8)(MACHINE_OPERAND_ROLE_USE_DEFINE | (MACHINE_REGISTER_CLASS_GENERAL << MACHINE_OPERAND_CLASS_SHIFT)))

// Shorthand rows for the x86-64 scalar subset: destination-and-source
// moves, read-modify-write arithmetic, flag producers/consumers, frame and
// pointer memory forms, and terminators.
#define MACHINE_INFO_MOVE(name_literal)                                                                                                                        \
    {                                                                                                                                                          \
        .name = S8_INITIALIZER(name_literal), .operand_count = 2, .operand_info = {MACHINE_OPERAND_DEFINE_GENERAL, MACHINE_OPERAND_USE_GENERAL},               \
    }
#define MACHINE_INFO_READ_MODIFY(name_literal)                                                                                                                 \
    {                                                                                                                                                          \
        .name = S8_INITIALIZER(name_literal), .operand_count = 2, .operand_info = {MACHINE_OPERAND_USE_DEFINE_GENERAL, MACHINE_OPERAND_USE_GENERAL},           \
        .attributes = MACHINE_OPCODE_ATTRIBUTE_FLAGS_DEFINE,                                                                                                   \
    }
#define MACHINE_INFO_UNARY_READ_MODIFY(name_literal)                                                                                                           \
    {                                                                                                                                                          \
        .name = S8_INITIALIZER(name_literal), .operand_count = 1, .operand_info = {MACHINE_OPERAND_USE_DEFINE_GENERAL},                                        \
        .attributes = MACHINE_OPCODE_ATTRIBUTE_FLAGS_DEFINE,                                                                                                   \
    }
#define MACHINE_INFO_COMPARE(name_literal)                                                                                                                     \
    {                                                                                                                                                          \
        .name = S8_INITIALIZER(name_literal), .operand_count = 2, .operand_info = {MACHINE_OPERAND_USE_GENERAL, MACHINE_OPERAND_USE_GENERAL},                  \
        .attributes = MACHINE_OPCODE_ATTRIBUTE_FLAGS_DEFINE,                                                                                                   \
    }
#define MACHINE_INFO_STORE_FRAME(name_literal)                                                                                                                 \
    {                                                                                                                                                          \
        .name = S8_INITIALIZER(name_literal), .operand_count = 2, .operand_info = {0, MACHINE_OPERAND_USE_GENERAL},                                            \
    }
#define MACHINE_INFO_LOAD_POINTER(name_literal) MACHINE_INFO_MOVE(name_literal)
#define MACHINE_INFO_STORE_POINTER(name_literal)                                                                                                               \
    {                                                                                                                                                          \
        .name = S8_INITIALIZER(name_literal), .operand_count = 2, .operand_info = {MACHINE_OPERAND_USE_GENERAL, MACHINE_OPERAND_USE_GENERAL},                  \
    }

BUSTER_GLOBAL_LOCAL MachineOpcodeInfo const machine_opcode_infos[MACHINE_OPCODE_COUNT] = {
    [MACHINE_OPCODE_INVALID] = {
        .name = S8_INITIALIZER("invalid"),
    },
    [MACHINE_OPCODE_SKELETON_NOP] = {
        .name = S8_INITIALIZER("skeleton_nop"),
    },
    [MACHINE_OPCODE_SKELETON_COPY] = {
        .name = S8_INITIALIZER("skeleton_copy"),
        .operand_count = 2,
        .operand_info = {MACHINE_OPERAND_DEFINE_GENERAL, MACHINE_OPERAND_USE_GENERAL},
    },
    [MACHINE_OPCODE_SKELETON_RETURN] = {
        .name = S8_INITIALIZER("skeleton_return"),
        .attributes = MACHINE_OPCODE_ATTRIBUTE_TERMINATOR,
    },
    [MACHINE_X64_MOV_RI] = {
        .name = S8_INITIALIZER("x64_mov_ri"),
        .operand_count = 2,
        .operand_info = {MACHINE_OPERAND_DEFINE_GENERAL, 0},
        .attributes = MACHINE_OPCODE_ATTRIBUTE_REMATERIALIZABLE,
    },
    [MACHINE_X64_MOV_RR] = MACHINE_INFO_MOVE("x64_mov_rr"),
    [MACHINE_X64_MOV32_RR] = MACHINE_INFO_MOVE("x64_mov32_rr"),
    [MACHINE_X64_MOVSX8_RR] = MACHINE_INFO_MOVE("x64_movsx8_rr"),
    [MACHINE_X64_MOVSX16_RR] = MACHINE_INFO_MOVE("x64_movsx16_rr"),
    [MACHINE_X64_MOVSX32_RR] = MACHINE_INFO_MOVE("x64_movsx32_rr"),
    [MACHINE_X64_MOVZX8_RR] = MACHINE_INFO_MOVE("x64_movzx8_rr"),
    [MACHINE_X64_MOVZX16_RR] = MACHINE_INFO_MOVE("x64_movzx16_rr"),
    [MACHINE_X64_ADD32] = MACHINE_INFO_READ_MODIFY("x64_add32"),
    [MACHINE_X64_ADD64] = MACHINE_INFO_READ_MODIFY("x64_add64"),
    [MACHINE_X64_SUB32] = MACHINE_INFO_READ_MODIFY("x64_sub32"),
    [MACHINE_X64_SUB64] = MACHINE_INFO_READ_MODIFY("x64_sub64"),
    [MACHINE_X64_AND32] = MACHINE_INFO_READ_MODIFY("x64_and32"),
    [MACHINE_X64_AND64] = MACHINE_INFO_READ_MODIFY("x64_and64"),
    [MACHINE_X64_OR32] = MACHINE_INFO_READ_MODIFY("x64_or32"),
    [MACHINE_X64_OR64] = MACHINE_INFO_READ_MODIFY("x64_or64"),
    [MACHINE_X64_XOR32] = MACHINE_INFO_READ_MODIFY("x64_xor32"),
    [MACHINE_X64_XOR64] = MACHINE_INFO_READ_MODIFY("x64_xor64"),
    [MACHINE_X64_IMUL32] = MACHINE_INFO_READ_MODIFY("x64_imul32"),
    [MACHINE_X64_IMUL64] = MACHINE_INFO_READ_MODIFY("x64_imul64"),
    [MACHINE_X64_NEG32] = MACHINE_INFO_UNARY_READ_MODIFY("x64_neg32"),
    [MACHINE_X64_NEG64] = MACHINE_INFO_UNARY_READ_MODIFY("x64_neg64"),
    [MACHINE_X64_NOT32] = MACHINE_INFO_UNARY_READ_MODIFY("x64_not32"),
    [MACHINE_X64_NOT64] = MACHINE_INFO_UNARY_READ_MODIFY("x64_not64"),
    [MACHINE_X64_CMP32] = MACHINE_INFO_COMPARE("x64_cmp32"),
    [MACHINE_X64_CMP64] = MACHINE_INFO_COMPARE("x64_cmp64"),
    [MACHINE_X64_TEST_RR] = MACHINE_INFO_COMPARE("x64_test_rr"),
    [MACHINE_X64_SETCC] = {
        .name = S8_INITIALIZER("x64_setcc"),
        .operand_count = 1,
        .operand_info = {MACHINE_OPERAND_DEFINE_GENERAL},
        .attributes = MACHINE_OPCODE_ATTRIBUTE_FLAGS_USE,
    },
    [MACHINE_X64_LOAD_FRAME] = {
        .name = S8_INITIALIZER("x64_load_frame"),
        .operand_count = 2,
        .operand_info = {MACHINE_OPERAND_DEFINE_GENERAL, 0},
    },
    [MACHINE_X64_STORE_FRAME8] = MACHINE_INFO_STORE_FRAME("x64_store_frame8"),
    [MACHINE_X64_STORE_FRAME16] = MACHINE_INFO_STORE_FRAME("x64_store_frame16"),
    [MACHINE_X64_STORE_FRAME32] = MACHINE_INFO_STORE_FRAME("x64_store_frame32"),
    [MACHINE_X64_STORE_FRAME64] = MACHINE_INFO_STORE_FRAME("x64_store_frame64"),
    [MACHINE_X64_LOAD_PTR8] = MACHINE_INFO_LOAD_POINTER("x64_load_ptr8"),
    [MACHINE_X64_LOAD_PTR16] = MACHINE_INFO_LOAD_POINTER("x64_load_ptr16"),
    [MACHINE_X64_LOAD_PTR32] = MACHINE_INFO_LOAD_POINTER("x64_load_ptr32"),
    [MACHINE_X64_LOAD_PTR64] = MACHINE_INFO_LOAD_POINTER("x64_load_ptr64"),
    [MACHINE_X64_STORE_PTR8] = MACHINE_INFO_STORE_POINTER("x64_store_ptr8"),
    [MACHINE_X64_STORE_PTR16] = MACHINE_INFO_STORE_POINTER("x64_store_ptr16"),
    [MACHINE_X64_STORE_PTR32] = MACHINE_INFO_STORE_POINTER("x64_store_ptr32"),
    [MACHINE_X64_STORE_PTR64] = MACHINE_INFO_STORE_POINTER("x64_store_ptr64"),
    [MACHINE_X64_JMP] = {
        .name = S8_INITIALIZER("x64_jmp"),
        .operand_count = 1,
        .attributes = MACHINE_OPCODE_ATTRIBUTE_TERMINATOR,
    },
    [MACHINE_X64_JCC] = {
        .name = S8_INITIALIZER("x64_jcc"),
        .operand_count = 2,
        .attributes = MACHINE_OPCODE_ATTRIBUTE_TERMINATOR | MACHINE_OPCODE_ATTRIBUTE_FLAGS_USE,
    },
    [MACHINE_X64_RET] = {
        .name = S8_INITIALIZER("x64_ret"),
        .implicit_mask = 1u << MACHINE_X64_RAX,
        .attributes = MACHINE_OPCODE_ATTRIBUTE_TERMINATOR,
    },
    [MACHINE_X64_SHL32] = MACHINE_INFO_READ_MODIFY("x64_shl32"),
    [MACHINE_X64_SHL64] = MACHINE_INFO_READ_MODIFY("x64_shl64"),
    [MACHINE_X64_SAR32] = MACHINE_INFO_READ_MODIFY("x64_sar32"),
    [MACHINE_X64_SAR64] = MACHINE_INFO_READ_MODIFY("x64_sar64"),
    [MACHINE_X64_SHR32] = MACHINE_INFO_READ_MODIFY("x64_shr32"),
    [MACHINE_X64_SHR64] = MACHINE_INFO_READ_MODIFY("x64_shr64"),
    [MACHINE_X64_SDIV32] = MACHINE_INFO_READ_MODIFY("x64_sdiv32"),
    [MACHINE_X64_SDIV64] = MACHINE_INFO_READ_MODIFY("x64_sdiv64"),
    [MACHINE_X64_UDIV32] = MACHINE_INFO_READ_MODIFY("x64_udiv32"),
    [MACHINE_X64_UDIV64] = MACHINE_INFO_READ_MODIFY("x64_udiv64"),
    [MACHINE_X64_SREM32] = MACHINE_INFO_READ_MODIFY("x64_srem32"),
    [MACHINE_X64_SREM64] = MACHINE_INFO_READ_MODIFY("x64_srem64"),
    [MACHINE_X64_UREM32] = MACHINE_INFO_READ_MODIFY("x64_urem32"),
    [MACHINE_X64_UREM64] = MACHINE_INFO_READ_MODIFY("x64_urem64"),
    [MACHINE_X64_LEA_FRAME] = {
        .name = S8_INITIALIZER("x64_lea_frame"),
        .operand_count = 2,
        .operand_info = {MACHINE_OPERAND_DEFINE_GENERAL, 0},
        .attributes = MACHINE_OPCODE_ATTRIBUTE_REMATERIALIZABLE,
    },
    [MACHINE_X64_LEA_SYMBOL] = {
        .name = S8_INITIALIZER("x64_lea_symbol"),
        .operand_count = 1,
        .operand_info = {MACHINE_OPERAND_DEFINE_GENERAL},
        .attributes = MACHINE_OPCODE_ATTRIBUTE_REMATERIALIZABLE,
    },
    [MACHINE_X64_CALL_DIRECT] = {
        .name = S8_INITIALIZER("x64_call_direct"),
        .implicit_mask = (1u << MACHINE_X64_RAX) | (1u << MACHINE_X64_RCX) | (1u << MACHINE_X64_RDX) | (1u << MACHINE_X64_RSI) | (1u << MACHINE_X64_RDI) |
                         (1u << MACHINE_X64_R8) | (1u << MACHINE_X64_R9) | (1u << MACHINE_X64_R10) | (1u << MACHINE_X64_R11),
        .attributes = MACHINE_OPCODE_ATTRIBUTE_CALL | MACHINE_OPCODE_ATTRIBUTE_SIDE_EFFECTS,
    },
};

BUSTER_F_DECL MachineOpcodeInfo const* machine_opcode_info(u16 opcode)
{
    if (opcode >= MACHINE_OPCODE_COUNT)
    {
        return 0;
    }
    return machine_opcode_infos + opcode;
}

BUSTER_F_DECL void machine_stream_initialize(MachineBuilderStream* stream, u64 element_size)
{
    stream->first = 0;
    stream->last = 0;
    stream->total_count = 0;
    stream->element_size = (u32)element_size;
    stream->chunk_capacity = (u32)((MACHINE_BUILDER_CHUNK_BYTES - sizeof(MachineBuilderChunk)) / element_size);
    stream->reserved = 0;
}

BUSTER_F_DECL void* machine_stream_append(Arena* arena, MachineBuilderStream* stream)
{
    MachineBuilderChunk* chunk = stream->last;
    if (!chunk || chunk->count == stream->chunk_capacity)
    {
        chunk = (MachineBuilderChunk*)arena_allocate_bytes(arena, sizeof(MachineBuilderChunk) + (u64)stream->chunk_capacity * stream->element_size,
                                                           BUSTER_ALIGN_OF(MachineBuilderChunk));
        chunk->next = 0;
        chunk->count = 0;
        chunk->reserved = 0;
        if (stream->last)
        {
            stream->last->next = chunk;
        }
        else
        {
            stream->first = chunk;
        }
        stream->last = chunk;
    }
    void* row = (u8*)(chunk + 1) + (u64)chunk->count * stream->element_size;
    chunk->count += 1;
    stream->total_count += 1;
    return row;
}

BUSTER_F_DECL void machine_stream_flatten(MachineBuilderStream* stream, void* destination)
{
    u64 offset = 0;
    for (MachineBuilderChunk* chunk = stream->first; chunk; chunk = chunk->next)
    {
        u64 chunk_bytes = (u64)chunk->count * stream->element_size;
        memcpy((u8*)destination + offset, chunk + 1, chunk_bytes);
        offset += chunk_bytes;
    }
}

BUSTER_F_DECL MachineFunctionBuilder machine_function_builder_begin(Arena* arena)
{
    MachineFunctionBuilder builder = {
        .arena = arena,
        .open_block = UINT32_MAX,
    };
    machine_stream_initialize(&builder.instructions, sizeof(MachineInstruction));
    machine_stream_initialize(&builder.virtual_registers, sizeof(MachineVirtualRegister));
    machine_stream_initialize(&builder.blocks, sizeof(MachineBlock));
    return builder;
}

BUSTER_F_DECL u32 machine_builder_virtual_register(MachineFunctionBuilder* builder, MachineVirtualRegister virtual_register)
{
    u32 index = builder->virtual_registers.total_count;
    MachineVirtualRegister* row = (MachineVirtualRegister*)machine_stream_append(builder->arena, &builder->virtual_registers);
    *row = virtual_register;
    return index;
}

BUSTER_F_DECL u32 machine_builder_block_begin(MachineFunctionBuilder* builder)
{
    BUSTER_CHECK(!builder->block_is_open);
    builder->block_is_open = true;
    builder->open_block = builder->blocks.total_count;
    builder->open_block_first_instruction = builder->instructions.total_count;
    return builder->open_block;
}

BUSTER_F_DECL u32 machine_builder_instruction(MachineFunctionBuilder* builder, MachineInstruction instruction)
{
    BUSTER_CHECK(builder->block_is_open);
    u32 index = builder->instructions.total_count;
    if (index >= MACHINE_POINT_INSTRUCTION_LIMIT)
    {
        builder->point_capacity_exceeded = true;
    }
    MachineInstruction* row = (MachineInstruction*)machine_stream_append(builder->arena, &builder->instructions);
    *row = instruction;
    return index;
}

BUSTER_F_DECL void machine_builder_block_end(MachineFunctionBuilder* builder, MachineBlock block)
{
    BUSTER_CHECK(builder->block_is_open);
    block.first_instruction = builder->open_block_first_instruction;
    block.instruction_count = builder->instructions.total_count - builder->open_block_first_instruction;
    MachineBlock* row = (MachineBlock*)machine_stream_append(builder->arena, &builder->blocks);
    *row = block;
    builder->block_is_open = false;
    builder->open_block = UINT32_MAX;
}

BUSTER_F_DECL MachineFunction machine_function_builder_finish(Arena* arena, MachineFunctionBuilder* builder)
{
    BUSTER_CHECK(!builder->block_is_open);
    MachineFunction function = {
        .instructions = arena_allocate(arena, MachineInstruction, builder->instructions.total_count),
        .virtual_registers = arena_allocate(arena, MachineVirtualRegister, builder->virtual_registers.total_count),
        .blocks = arena_allocate(arena, MachineBlock, builder->blocks.total_count),
        .instruction_count = builder->instructions.total_count,
        .virtual_register_count = builder->virtual_registers.total_count,
        .block_count = builder->blocks.total_count,
    };
    machine_stream_flatten(&builder->instructions, function.instructions);
    machine_stream_flatten(&builder->virtual_registers, function.virtual_registers);
    machine_stream_flatten(&builder->blocks, function.blocks);
    return function;
}

BUSTER_GLOBAL_LOCAL bool machine_verify_reference(MachineFunction* function, MachineRef ref)
{
    MachineRefKind kind = machine_ref_kind(ref);
    u32 payload = machine_ref_payload(ref);
    switch (kind)
    {
        case MACHINE_REF_NONE:
            return payload == 0;
        case MACHINE_REF_VIRTUAL_REGISTER:
            return payload < function->virtual_register_count;
        case MACHINE_REF_BLOCK:
            return payload < function->block_count;
        case MACHINE_REF_IMMEDIATE:
            return payload < function->immediate_count;
        case MACHINE_REF_STACK_SLOT:
            return payload < function->stack_slot_count;
        case MACHINE_REF_PHYSICAL_REGISTER:
        case MACHINE_REF_ADDRESS:
        case MACHINE_REF_EXTRA:
            // The address and overflow side tables arrive with later stages;
            // physical-register payloads are validated against the target
            // register file once multiple targets exist.
            return true;
        case MACHINE_REF_KIND_COUNT:
            return false;
    }
    return false;
}

BUSTER_F_DECL MachineVerifyResult machine_verify_function(MachineFunction* function)
{
    MachineVerifyResult result = {0};
    if (function->instruction_count >= MACHINE_POINT_INSTRUCTION_LIMIT)
    {
        result.error = MACHINE_VERIFY_POINT_CAPACITY;
        return result;
    }
    u32 covered_instruction_count = 0;
    for (u32 block_index = 0; block_index < function->block_count; block_index += 1)
    {
        MachineBlock* block = function->blocks + block_index;
        result.block = block_index;
        // Blocks must partition the instruction stream contiguously in
        // machine program order.
        if (block->first_instruction != covered_instruction_count || block->instruction_count > function->instruction_count - covered_instruction_count)
        {
            result.error = MACHINE_VERIFY_BLOCK_RANGE;
            return result;
        }
        covered_instruction_count += block->instruction_count;
        for (u32 offset = 0; offset < block->instruction_count; offset += 1)
        {
            u32 instruction_index = block->first_instruction + offset;
            result.instruction = instruction_index;
            MachineInstruction* instruction = function->instructions + instruction_index;
            MachineOpcodeInfo const* info = machine_opcode_info(instruction->opcode);
            if (!info || instruction->opcode == MACHINE_OPCODE_INVALID)
            {
                result.error = MACHINE_VERIFY_OPCODE;
                return result;
            }
            bool is_terminator = (info->attributes & MACHINE_OPCODE_ATTRIBUTE_TERMINATOR) != 0;
            bool is_last = offset == block->instruction_count - 1;
            if (is_terminator != is_last)
            {
                result.error = MACHINE_VERIFY_TERMINATOR;
                return result;
            }
            for (u32 operand_index = 0; operand_index < BUSTER_ARRAY_LENGTH(instruction->operands); operand_index += 1)
            {
                result.operand = operand_index;
                MachineRef ref = instruction->operands[operand_index];
                if (operand_index >= info->operand_count)
                {
                    // Unused inline slots must stay empty so scans can trust
                    // the static operand count.
                    if (ref != MACHINE_REF_NONE_VALUE)
                    {
                        result.error = MACHINE_VERIFY_OPERAND_SLOT;
                        return result;
                    }
                    continue;
                }
                if (!machine_verify_reference(function, ref))
                {
                    result.error = MACHINE_VERIFY_OPERAND_REFERENCE;
                    return result;
                }
            }
            result.operand = 0;
        }
    }
    result.block = 0;
    result.instruction = 0;
    if (covered_instruction_count != function->instruction_count)
    {
        result.error = MACHINE_VERIFY_INSTRUCTION_COVERAGE;
        return result;
    }
    for (u32 register_index = 0; register_index < function->virtual_register_count; register_index += 1)
    {
        MachineVirtualRegister* virtual_register = function->virtual_registers + register_index;
        if (virtual_register->definition_point != MACHINE_POINT_INVALID &&
            machine_point_instruction(virtual_register->definition_point) >= function->instruction_count)
        {
            result.operand = register_index;
            result.error = MACHINE_VERIFY_VIRTUAL_REGISTER_DEFINITION;
            return result;
        }
    }
    return result;
}

typedef struct MachineReplayHeader MachineReplayHeader;
struct MachineReplayHeader
{
    u32 magic;
    u32 version;
    u32 instruction_count;
    u32 virtual_register_count;
    u32 block_count;
    u32 reserved;
};
BUSTER_CT_CHECK(sizeof(MachineReplayHeader) == 24);

BUSTER_F_DECL ByteSlice machine_replay_serialize(Arena* arena, MachineFunction* function)
{
    u64 instruction_bytes = (u64)function->instruction_count * sizeof(MachineInstruction);
    u64 register_bytes = (u64)function->virtual_register_count * sizeof(MachineVirtualRegister);
    u64 block_bytes = (u64)function->block_count * sizeof(MachineBlock);
    u64 total = sizeof(MachineReplayHeader) + instruction_bytes + register_bytes + block_bytes;
    u8* bytes = arena_allocate(arena, u8, total);
    MachineReplayHeader header = {
        .magic = MACHINE_REPLAY_MAGIC,
        .version = MACHINE_REPLAY_VERSION,
        .instruction_count = function->instruction_count,
        .virtual_register_count = function->virtual_register_count,
        .block_count = function->block_count,
    };
    u64 offset = 0;
    memcpy(bytes + offset, &header, sizeof(header));
    offset += sizeof(header);
    if (instruction_bytes)
    {
        memcpy(bytes + offset, function->instructions, instruction_bytes);
        offset += instruction_bytes;
    }
    if (register_bytes)
    {
        memcpy(bytes + offset, function->virtual_registers, register_bytes);
        offset += register_bytes;
    }
    if (block_bytes)
    {
        memcpy(bytes + offset, function->blocks, block_bytes);
        offset += block_bytes;
    }
    return (ByteSlice){
        .pointer = bytes,
        .length = total,
    };
}

BUSTER_F_DECL bool machine_replay_deserialize(Arena* arena, ByteSlice bytes, MachineFunction* function)
{
    if (bytes.length < sizeof(MachineReplayHeader))
    {
        return false;
    }
    MachineReplayHeader header;
    memcpy(&header, bytes.pointer, sizeof(header));
    if (header.magic != MACHINE_REPLAY_MAGIC || header.version != MACHINE_REPLAY_VERSION)
    {
        return false;
    }
    u64 instruction_bytes = (u64)header.instruction_count * sizeof(MachineInstruction);
    u64 register_bytes = (u64)header.virtual_register_count * sizeof(MachineVirtualRegister);
    u64 block_bytes = (u64)header.block_count * sizeof(MachineBlock);
    u64 total = sizeof(MachineReplayHeader) + instruction_bytes + register_bytes + block_bytes;
    if (bytes.length != total)
    {
        return false;
    }
    MachineFunction read = {
        .instructions = arena_allocate(arena, MachineInstruction, header.instruction_count),
        .virtual_registers = arena_allocate(arena, MachineVirtualRegister, header.virtual_register_count),
        .blocks = arena_allocate(arena, MachineBlock, header.block_count),
        .instruction_count = header.instruction_count,
        .virtual_register_count = header.virtual_register_count,
        .block_count = header.block_count,
    };
    u64 offset = sizeof(MachineReplayHeader);
    if (instruction_bytes)
    {
        memcpy(read.instructions, bytes.pointer + offset, instruction_bytes);
        offset += instruction_bytes;
    }
    if (register_bytes)
    {
        memcpy(read.virtual_registers, bytes.pointer + offset, register_bytes);
        offset += register_bytes;
    }
    if (block_bytes)
    {
        memcpy(read.blocks, bytes.pointer + offset, block_bytes);
    }
    *function = read;
    return true;
}

#include <buster/lib/compiler/codegen/machine_x86_64.c>
