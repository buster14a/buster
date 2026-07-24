#include <buster/compiler/codegen/codegen.h>

#include <buster/compiler/ir/interpreter.h>
#include <buster/os.h>
#include <buster/string.h>

typedef struct CodegenBuffer CodegenBuffer;
struct CodegenBuffer
{
    u8* bytes;
    u64 count;
    u64 capacity;
    u8* value_registers;
    u8 allocated_register_base;
    CodegenError error;
};

typedef struct CodegenRelocation CodegenRelocation;
struct CodegenRelocation
{
    CodegenRelocation* next;
    IrBlockId target;
    u32 displacement_offset;
};

typedef enum X64Register
{
    X64_REGISTER_RAX,
    X64_REGISTER_RCX,
    X64_REGISTER_RDX,
    X64_REGISTER_RBX,
    X64_REGISTER_RSP,
    X64_REGISTER_RBP,
    X64_REGISTER_RSI,
    X64_REGISTER_RDI,
    X64_REGISTER_R8,
    X64_REGISTER_R9,
    X64_REGISTER_R10,
    X64_REGISTER_R11,
} X64Register;

typedef struct X64Builder X64Builder;
struct X64Builder
{
    Arena* arena;
    AnalysisResult* analysis;
    IrFunction* function;
    CodegenBuffer buffer;
    CodegenRelocation* first_relocation;
    CodegenRelocation* last_relocation;
    CodegenCallRelocation* first_call_relocation;
    CodegenCallRelocation* last_call_relocation;
    u32* block_offsets;
    u32 frame_size;
    u32 temporary_base;
    u32 temporary_count;
    u32 local_storage_base;
    u32* value_storage_offsets;
    u32* local_storage_offsets;
    u8* value_registers;
    u8* vector_registers;
    s32 hidden_result_displacement;
    s32 va_register_save_displacement;
    CodegenAbiSignature signature;
    CodegenAbi abi;
    Target target;
    u32 native_vector_operation_count;
    u32 split_vector_operation_count;
};

#define X64_VALUE_SLOT_SIZE 32
#define X64_VALUE_SLOT_COMPONENT_COUNT 4

BUSTER_GLOBAL_LOCAL u32 codegen_align_u32(u32 value, u32 alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

CodegenAbi codegen_abi_for_target(Target target)
{
    switch (target.cpu_arch)
    {
        break; case CPU_ARCH_X86_64:
        {
            switch (target.os)
            {
                break; case OPERATING_SYSTEM_WINDOWS:
                    return CODEGEN_ABI_X86_64_WINDOWS;
                break; case OPERATING_SYSTEM_UEFI:
                    return CODEGEN_ABI_X86_64_WINDOWS;
                break; case OPERATING_SYSTEM_LINUX:
                    return CODEGEN_ABI_X86_64_SYSTEM_V;
                break; case OPERATING_SYSTEM_MACOS:
                    return CODEGEN_ABI_X86_64_SYSTEM_V;
                break; case OPERATING_SYSTEM_ANDROID:
                    return CODEGEN_ABI_X86_64_SYSTEM_V;
                break; case OPERATING_SYSTEM_IOS:
                    return CODEGEN_ABI_X86_64_SYSTEM_V;
                break; case OPERATING_SYSTEM_FREESTANDING:
                    return CODEGEN_ABI_X86_64_SYSTEM_V;
                break; case OPERATING_SYSTEM_COUNT:
                    return CODEGEN_ABI_COUNT;
            }
        } break;
        break; case CPU_ARCH_AARCH64:
        {
            switch (target.os)
            {
                break; case OPERATING_SYSTEM_WINDOWS:
                    return CODEGEN_ABI_AARCH64_WINDOWS;
                break; case OPERATING_SYSTEM_MACOS:
                    return CODEGEN_ABI_AARCH64_DARWIN;
                break; case OPERATING_SYSTEM_IOS:
                    return CODEGEN_ABI_AARCH64_DARWIN;
                break; case OPERATING_SYSTEM_LINUX:
                    return CODEGEN_ABI_AARCH64_AAPCS64;
                break; case OPERATING_SYSTEM_UEFI:
                    return CODEGEN_ABI_AARCH64_AAPCS64;
                break; case OPERATING_SYSTEM_ANDROID:
                    return CODEGEN_ABI_AARCH64_AAPCS64;
                break; case OPERATING_SYSTEM_FREESTANDING:
                    return CODEGEN_ABI_AARCH64_AAPCS64;
                break; case OPERATING_SYSTEM_COUNT:
                    return CODEGEN_ABI_COUNT;
            }
        } break;
        break; case CPU_ARCH_COUNT:
            return CODEGEN_ABI_COUNT;
    }
    return CODEGEN_ABI_COUNT;
}

BUSTER_GLOBAL_LOCAL AnalysisAbiConvention
codegen_analysis_abi_convention(CodegenAbi abi)
{
    return
        abi == CODEGEN_ABI_X86_64_SYSTEM_V ?
            ANALYSIS_ABI_CONVENTION_SYSTEMV_X86_64 :
        abi == CODEGEN_ABI_X86_64_WINDOWS ?
            ANALYSIS_ABI_CONVENTION_WIN64_X86_64 :
        abi == CODEGEN_ABI_AARCH64_DARWIN ?
            ANALYSIS_ABI_CONVENTION_APPLE_AARCH64 :
        abi == CODEGEN_ABI_AARCH64_WINDOWS ?
            ANALYSIS_ABI_CONVENTION_WINDOWS_AARCH64 :
            ANALYSIS_ABI_CONVENTION_AAPCS64;
}

BUSTER_GLOBAL_LOCAL CodegenAbiSignature codegen_classify_signature_with_arguments(
    Arena* arena,
    AnalysisResult* analysis,
    AnalysisTypeId function_type_id,
    AnalysisTypeId* argument_types,
    u32 argument_count,
    Target target)
{
    CodegenAbiSignature result = {0};
    CodegenAbi abi = codegen_abi_for_target(target);
    AnalysisType* function_type =
        analysis_type_from_id(analysis, function_type_id);
    if (function_type->kind != ANALYSIS_TYPE_FUNCTION ||
        abi >= CODEGEN_ABI_COUNT)
    {
        return result;
    }
    analysis_compute_layouts(
        analysis,
        (AnalysisLayoutOptions){
            .pointer_size = 8,
            .pointer_alignment = 8,
        });
    AnalysisFunctionAbi classified = argument_types ?
        analysis_classify_call_abi(
            arena,
            analysis,
            function_type_id,
            argument_types,
            argument_count,
            target) :
        analysis_classify_function_abi(
            arena,
            analysis,
            function_type_id,
            target);
    AnalysisAbiConvention expected =
        abi == CODEGEN_ABI_X86_64_SYSTEM_V ?
            ANALYSIS_ABI_CONVENTION_SYSTEMV_X86_64 :
        abi == CODEGEN_ABI_X86_64_WINDOWS ?
            ANALYSIS_ABI_CONVENTION_WIN64_X86_64 :
        abi == CODEGEN_ABI_AARCH64_DARWIN ?
            ANALYSIS_ABI_CONVENTION_APPLE_AARCH64 :
        abi == CODEGEN_ABI_AARCH64_WINDOWS ?
            ANALYSIS_ABI_CONVENTION_WINDOWS_AARCH64 :
            ANALYSIS_ABI_CONVENTION_AAPCS64;
    if (classified.convention != expected)
    {
        return result;
    }
    result.argument_count = classified.argument_count;
    result.indirect_result_register =
        classified.indirect_result_register;
    result.arguments = arena_allocate(
        arena,
        CodegenAbiLocation,
        result.argument_count);
    for (u32 value_index = 0;
        value_index <= result.argument_count;
        value_index += 1)
    {
        AnalysisAbiValue* source =
            value_index == result.argument_count ?
                &classified.result :
                classified.arguments + value_index;
        CodegenAbiLocation* destination =
            value_index == result.argument_count ?
                &result.result :
                result.arguments + value_index;
        AnalysisTypeId value_type_id =
            value_index == result.argument_count ?
                function_type->as.function.return_type :
                (argument_types ?
                    argument_types[value_index] :
                    function_type->as.function
                        .argument_types[value_index]);
        AnalysisType* value_type =
            analysis_type_from_id(
                analysis,
                value_type_id);
        if (source->part_count > CODEGEN_ABI_MAX_PARTS)
        {
            return result;
        }
        destination->part_count = source->part_count;
        destination->indirect = source->indirect;
        destination->indirect_copy_offset =
            source->indirect_copy_offset;
        if (source->indirect &&
            value_index != result.argument_count &&
            source->indirect_copy_offset +
                value_type->layout.size >
                classified.stack_size)
        {
            return result;
        }
        for (u32 part_index = 0;
            part_index < source->part_count;
            part_index += 1)
        {
            AnalysisAbiPart* source_part =
                source->parts + part_index;
            CodegenAbiPart* destination_part =
                destination->parts + part_index;
            if (!source_part->size ||
                source_part->value_offset +
                    source_part->size >
                    value_type->layout.size)
            {
                return result;
            }
            destination_part->index =
                source_part->register_index;
            destination_part->stack_offset =
                source_part->stack_offset;
            destination_part->value_offset =
                source_part->value_offset;
            destination_part->size = source_part->size;
            destination_part->kind =
                source_part->location ==
                    ANALYSIS_ABI_LOCATION_STACK ?
                    CODEGEN_ABI_LOCATION_STACK :
                source_part->abi_class ==
                        ANALYSIS_ABI_CLASS_FLOAT ||
                    source_part->abi_class ==
                        ANALYSIS_ABI_CLASS_VECTOR ?
                    CODEGEN_ABI_LOCATION_FLOAT_REGISTER :
                    CODEGEN_ABI_LOCATION_INTEGER_REGISTER;
            if (destination_part->kind ==
                    CODEGEN_ABI_LOCATION_STACK &&
                destination_part->stack_offset +
                    destination_part->size >
                    classified.stack_size)
            {
                return result;
            }
            if (destination_part->kind !=
                CODEGEN_ABI_LOCATION_STACK)
            {
                u32 limit =
                    abi == CODEGEN_ABI_X86_64_SYSTEM_V ?
                        (destination_part->kind ==
                                CODEGEN_ABI_LOCATION_FLOAT_REGISTER ?
                            8 :
                            (value_index ==
                                    result.argument_count ?
                                2 :
                                6)) :
                    abi == CODEGEN_ABI_X86_64_WINDOWS ?
                        (value_index ==
                                result.argument_count ?
                            1 :
                            4) :
                        8;
                if (destination_part->index >= limit)
                {
                    return result;
                }
            }
        }
        if (destination->part_count)
        {
            destination->index =
                destination->parts[0].index;
            destination->stack_offset =
                destination->parts[0].stack_offset;
            destination->kind =
                destination->parts[0].kind;
        }
    }
    result.stack_size = classified.stack_size;
    result.valid = true;
    return result;
}

BUSTER_GLOBAL_LOCAL Target codegen_target_for_abi(CodegenAbi abi)
{
    bool x86 =
        abi == CODEGEN_ABI_X86_64_SYSTEM_V ||
        abi == CODEGEN_ABI_X86_64_WINDOWS;
    Target result = {
        .cpu_arch = x86 ?
            CPU_ARCH_X86_64 :
            CPU_ARCH_AARCH64,
        .os =
            abi == CODEGEN_ABI_X86_64_WINDOWS ?
                OPERATING_SYSTEM_WINDOWS :
            abi == CODEGEN_ABI_AARCH64_DARWIN ?
                OPERATING_SYSTEM_MACOS :
            abi == CODEGEN_ABI_AARCH64_WINDOWS ?
                OPERATING_SYSTEM_WINDOWS :
                OPERATING_SYSTEM_LINUX,
        .cpu_features_explicit = true,
        .cpu_features = x86 ?
            TARGET_CPU_FEATURE_X86_SSE2 |
                TARGET_CPU_FEATURE_X86_AVX |
                TARGET_CPU_FEATURE_X86_AVX2 |
                TARGET_CPU_FEATURE_X86_AVX512F |
                TARGET_CPU_FEATURE_X86_AVX512VL :
            TARGET_CPU_FEATURE_AARCH64_NEON,
    };
    return result;
}

BUSTER_GLOBAL_LOCAL CodegenAbiSignature
codegen_classify_signature_for_target(
    Arena* arena,
    AnalysisResult* analysis,
    AnalysisTypeId function_type_id,
    Target target)
{
    return codegen_classify_signature_with_arguments(
        arena,
        analysis,
        function_type_id,
        0,
        0,
        target);
}

CodegenAbiSignature codegen_classify_signature(
    Arena* arena,
    AnalysisResult* analysis,
    AnalysisTypeId function_type_id,
    CodegenAbi abi)
{
    return codegen_classify_signature_with_arguments(
        arena,
        analysis,
        function_type_id,
        0,
        0,
        codegen_target_for_abi(abi));
}

BUSTER_GLOBAL_LOCAL void codegen_emit_u8(
    CodegenBuffer* buffer,
    u8 value)
{
    if (buffer->count >= buffer->capacity)
    {
        buffer->error = CODEGEN_ERROR_CAPACITY;
        return;
    }
    buffer->bytes[buffer->count++] = value;
}

BUSTER_GLOBAL_LOCAL void codegen_emit_u32(
    CodegenBuffer* buffer,
    u32 value)
{
    for (u32 index = 0; index < 4; index += 1)
    {
        codegen_emit_u8(buffer, (u8)(value >> (index * 8)));
    }
}

BUSTER_GLOBAL_LOCAL void codegen_emit_u64(
    CodegenBuffer* buffer,
    u64 value)
{
    for (u32 index = 0; index < 8; index += 1)
    {
        codegen_emit_u8(buffer, (u8)(value >> (index * 8)));
    }
}

BUSTER_GLOBAL_LOCAL s32 x64_value_displacement_component(
    IrValueId value,
    u32 component)
{
    return -(s32)(
        value.value * X64_VALUE_SLOT_SIZE +
        (component + 1) * 8);
}

BUSTER_GLOBAL_LOCAL s32 x64_value_displacement(IrValueId value)
{
    return x64_value_displacement_component(value, 0);
}

BUSTER_GLOBAL_LOCAL s32 x64_temporary_displacement(
    X64Builder* builder,
    u32 index)
{
    return -(s32)(
        builder->temporary_base +
        index * X64_VALUE_SLOT_SIZE +
        8);
}

BUSTER_GLOBAL_LOCAL s32 x64_local_storage_displacement(
    X64Builder* builder,
    AnalysisLocalId local)
{
    return -(s32)builder->local_storage_offsets[local.value];
}

BUSTER_GLOBAL_LOCAL bool codegen_type_is_indirect_value(
    AnalysisType* type)
{
    return type->kind == ANALYSIS_TYPE_ARRAY ||
        type->kind == ANALYSIS_TYPE_VECTOR ||
        type->kind == ANALYSIS_TYPE_INFERRED_ARRAY ||
        type->kind == ANALYSIS_TYPE_STRUCT ||
        type->kind == ANALYSIS_TYPE_UNION;
}

BUSTER_GLOBAL_LOCAL bool codegen_type_is_inline_collection(
    AnalysisType* type)
{
    return type->kind == ANALYSIS_TYPE_SLICE ||
        type->kind == ANALYSIS_TYPE_RANGE ||
        type->kind == ANALYSIS_TYPE_VA_LIST;
}

BUSTER_GLOBAL_LOCAL u32 codegen_type_storage_size(
    AnalysisType* type)
{
    u64 size = type->layout.size;
    if (!size)
    {
        size = 8;
    }
    if (size > UINT32_MAX)
    {
        return 0;
    }
    return (u32)size;
}

BUSTER_GLOBAL_LOCAL AnalysisEntitySemantic* codegen_type_semantic(
    AnalysisResult* analysis,
    AnalysisType* type)
{
    if (type->kind != ANALYSIS_TYPE_STRUCT &&
        type->kind != ANALYSIS_TYPE_UNION)
    {
        return 0;
    }
    AnalysisResult* owner = analysis;
    if (type->as.declaration.module.value !=
        analysis->module.id.value)
    {
        owner = 0;
        for (u32 index = 0;
            index < analysis->program_module_count;
            index += 1)
        {
            AnalysisResult* candidate =
                analysis->program_modules[index];
            if (candidate &&
                candidate->module.id.value ==
                    type->as.declaration.module.value)
            {
                owner = candidate;
                break;
            }
        }
    }
    if (!owner ||
        type->as.declaration.index.value >=
            owner->module.entity_count)
    {
        return 0;
    }
    return owner->module.semantics +
        type->as.declaration.index.value;
}

typedef struct CodegenRegisterAllocation
    CodegenRegisterAllocation;
struct CodegenRegisterAllocation
{
    u8* registers;
    u32 allocated_count;
    u32 spilled_count;
};

#define CODEGEN_REGISTER_UNALLOCATED UINT8_MAX

BUSTER_GLOBAL_LOCAL bool codegen_register_type_eligible(
    AnalysisType* type)
{
    return type->kind == ANALYSIS_TYPE_BOOL ||
        type->kind == ANALYSIS_TYPE_INTEGER ||
        type->kind == ANALYSIS_TYPE_POINTER ||
        type->kind == ANALYSIS_TYPE_ENUM;
}

BUSTER_GLOBAL_LOCAL CodegenRegisterAllocation
codegen_allocate_registers(
    Arena* arena,
    AnalysisResult* analysis,
    IrFunction* function,
    u32 register_count,
    bool vectors)
{
    CodegenRegisterAllocation result = {
        .registers = arena_allocate(
            arena,
            u8,
            function->value_count),
    };
    u32* starts = arena_allocate(
        arena,
        u32,
        function->value_count);
    u32* ends = arena_allocate(
        arena,
        u32,
        function->value_count);
    u32* blocks = arena_allocate(
        arena,
        u32,
        function->value_count);
    u32* instruction_blocks = arena_allocate(
        arena,
        u32,
        function->instruction_count);
    bool* candidates = arena_allocate(
        arena,
        bool,
        function->value_count);
    bool* eligible = arena_allocate(
        arena,
        bool,
        function->value_count);
    memset(
        candidates,
        0,
        sizeof(*candidates) * function->value_count);
    memset(
        eligible,
        0,
        sizeof(*eligible) * function->value_count);
    for (u32 value_index = 0;
        value_index < function->value_count;
        value_index += 1)
    {
        result.registers[value_index] =
            CODEGEN_REGISTER_UNALLOCATED;
        blocks[value_index] = UINT32_MAX;
    }
    for (u32 block_index = 0;
        block_index < function->block_count;
        block_index += 1)
    {
        IrBlock* block = function->blocks + block_index;
        for (IrInstructionId id = block->first_instruction;
            id.value != IR_ID_UNDERLYING_INVALID;
            id = function->instructions[id.value].next)
        {
            instruction_blocks[id.value] = block_index;
            IrInstruction* instruction =
                function->instructions + id.value;
            if (instruction->result.value ==
                IR_ID_UNDERLYING_INVALID)
            {
                continue;
            }
            IrValueId value_id = instruction->result;
            IrValue* value =
                function->values + value_id.value;
            AnalysisType* type = analysis_type_from_id(
                analysis,
                value->type);
            bool eligible_type = vectors ?
                type->kind == ANALYSIS_TYPE_VECTOR &&
                    type->layout.size <= 16 :
                codegen_register_type_eligible(type);
            bool eligible_instruction = !vectors ||
                instruction->opcode == IR_OPCODE_UNARY ||
                instruction->opcode == IR_OPCODE_BINARY;
            candidates[value_id.value] =
                value->category == IR_VALUE_VALUE &&
                eligible_type &&
                eligible_instruction;
            eligible[value_id.value] =
                candidates[value_id.value];
            starts[value_id.value] = id.value;
            ends[value_id.value] = id.value;
            blocks[value_id.value] = block_index;
        }
    }
    for (u32 instruction_index = 0;
        instruction_index < function->instruction_count;
        instruction_index += 1)
    {
        IrInstruction* instruction =
            function->instructions + instruction_index;
        for (u32 operand_index = 0;
            operand_index < instruction->operand_count;
            operand_index += 1)
        {
            IrValueId operand =
                instruction->operands[operand_index];
            if (operand.value >= function->value_count ||
                !candidates[operand.value])
            {
                continue;
            }
            if (blocks[operand.value] !=
                instruction_blocks[instruction_index])
            {
                candidates[operand.value] = false;
                continue;
            }
            ends[operand.value] = BUSTER_MAX(
                ends[operand.value],
                instruction_index);
        }
    }
    for (u32 block_index = 0;
        block_index < function->block_count;
        block_index += 1)
    {
        for (IrBlockParameter* parameter =
                function->blocks[block_index].first_parameter;
            parameter;
            parameter = parameter->next)
        {
            for (IrIncoming* incoming =
                    parameter->first_incoming;
                incoming;
                incoming = incoming->next)
            {
                if (incoming->value.value <
                    function->value_count)
                {
                    candidates[incoming->value.value] =
                        false;
                }
            }
        }
    }
    for (u32 value_index = 0;
        value_index < function->value_count;
        value_index += 1)
    {
        if (!candidates[value_index] ||
            starts[value_index] == ends[value_index])
        {
            candidates[value_index] = false;
            continue;
        }
        for (u32 instruction_index =
                starts[value_index] + 1;
            instruction_index < ends[value_index];
            instruction_index += 1)
        {
            if (function->instructions[
                    instruction_index].opcode ==
                IR_OPCODE_CALL)
            {
                candidates[value_index] = false;
                break;
            }
        }
    }
    IrValueId* active_values = arena_allocate(
        arena,
        IrValueId,
        register_count);
    u32* active_ends = arena_allocate(
        arena,
        u32,
        register_count);
    for (u32 register_index = 0;
        register_index < register_count;
        register_index += 1)
    {
        active_values[register_index] =
            IR_VALUE_ID_INVALID;
    }
    for (u32 instruction_index = 0;
        instruction_index < function->instruction_count;
        instruction_index += 1)
    {
        for (u32 register_index = 0;
            register_index < register_count;
            register_index += 1)
        {
            if (active_values[register_index].value !=
                    IR_ID_UNDERLYING_INVALID &&
                active_ends[register_index] <
                    instruction_index)
            {
                active_values[register_index] =
                    IR_VALUE_ID_INVALID;
            }
        }
        IrInstruction* instruction =
            function->instructions + instruction_index;
        if (instruction->result.value ==
                IR_ID_UNDERLYING_INVALID ||
            !candidates[instruction->result.value])
        {
            continue;
        }
        bool assigned = false;
        for (u32 register_index = 0;
            register_index < register_count;
            register_index += 1)
        {
            if (active_values[register_index].value !=
                IR_ID_UNDERLYING_INVALID)
            {
                continue;
            }
            active_values[register_index] =
                instruction->result;
            active_ends[register_index] =
                ends[instruction->result.value];
            result.registers[
                instruction->result.value] =
                (u8)register_index;
            result.allocated_count += 1;
            assigned = true;
            break;
        }
        if (!assigned)
        {
            continue;
        }
    }
    for (u32 value_index = 0;
        value_index < function->value_count;
        value_index += 1)
    {
        if (eligible[value_index] &&
            starts[value_index] != ends[value_index] &&
            result.registers[value_index] ==
                CODEGEN_REGISTER_UNALLOCATED)
        {
            result.spilled_count += 1;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void x64_emit_load(
    X64Builder* builder,
    X64Register target,
    s32 displacement)
{
    u8 rex = target >= X64_REGISTER_R8 ? 0x4c : 0x48;
    u8 register_bits = (u8)(target & 7);
    codegen_emit_u8(&builder->buffer, rex);
    codegen_emit_u8(&builder->buffer, 0x8b);
    codegen_emit_u8(
        &builder->buffer,
        (u8)(0x85 | (register_bits << 3)));
    codegen_emit_u32(&builder->buffer, (u32)displacement);
}

BUSTER_GLOBAL_LOCAL void x64_emit_store(
    X64Builder* builder,
    X64Register source,
    s32 displacement)
{
    u8 rex = source >= X64_REGISTER_R8 ? 0x4c : 0x48;
    u8 register_bits = (u8)(source & 7);
    codegen_emit_u8(&builder->buffer, rex);
    codegen_emit_u8(&builder->buffer, 0x89);
    codegen_emit_u8(
        &builder->buffer,
        (u8)(0x85 | (register_bits << 3)));
    codegen_emit_u32(&builder->buffer, (u32)displacement);
}

BUSTER_GLOBAL_LOCAL void x64_emit_move_register(
    X64Builder* builder,
    X64Register target,
    X64Register source)
{
    if (target == source)
    {
        return;
    }
    u8 rex = 0x48;
    rex |= source >= X64_REGISTER_R8 ? 0x04 : 0;
    rex |= target >= X64_REGISTER_R8 ? 0x01 : 0;
    codegen_emit_u8(&builder->buffer, rex);
    codegen_emit_u8(&builder->buffer, 0x89);
    codegen_emit_u8(
        &builder->buffer,
        (u8)(0xc0 |
            ((source & 7) << 3) |
            (target & 7)));
}

BUSTER_GLOBAL_LOCAL X64Register x64_allocated_register(
    X64Builder* builder,
    IrValueId value)
{
    BUSTER_CHECK(
        builder->value_registers[value.value] == 0);
    return X64_REGISTER_R10;
}

BUSTER_GLOBAL_LOCAL void x64_emit_load_value(
    X64Builder* builder,
    X64Register target,
    IrValueId value)
{
    if (builder->value_registers[value.value] !=
        CODEGEN_REGISTER_UNALLOCATED)
    {
        x64_emit_move_register(
            builder,
            target,
            x64_allocated_register(builder, value));
        return;
    }
    x64_emit_load(
        builder,
        target,
        x64_value_displacement(value));
}

BUSTER_GLOBAL_LOCAL void x64_emit_constant_register(
    X64Builder* builder,
    X64Register target,
    u64 value)
{
    codegen_emit_u8(
        &builder->buffer,
        target >= X64_REGISTER_R8 ? 0x49 : 0x48);
    codegen_emit_u8(
        &builder->buffer,
        (u8)(0xb8 + (target & 7)));
    codegen_emit_u64(&builder->buffer, value);
}

BUSTER_GLOBAL_LOCAL void x64_emit_address(
    X64Builder* builder,
    X64Register target,
    s32 displacement)
{
    u8 rex = target >= X64_REGISTER_R8 ? 0x4c : 0x48;
    u8 register_bits = (u8)(target & 7);
    codegen_emit_u8(&builder->buffer, rex);
    codegen_emit_u8(&builder->buffer, 0x8d);
    codegen_emit_u8(
        &builder->buffer,
        (u8)(0x85 | (register_bits << 3)));
    codegen_emit_u32(&builder->buffer, (u32)displacement);
}

BUSTER_GLOBAL_LOCAL void x64_emit_copy_memory(
    X64Builder* builder,
    u32 size)
{
    u32 offset = 0;
    while (size - offset >= 8)
    {
        codegen_emit_u8(&builder->buffer, 0x48);
        codegen_emit_u8(&builder->buffer, 0x8b);
        codegen_emit_u8(&builder->buffer, 0x91);
        codegen_emit_u32(&builder->buffer, offset);
        codegen_emit_u8(&builder->buffer, 0x48);
        codegen_emit_u8(&builder->buffer, 0x89);
        codegen_emit_u8(&builder->buffer, 0x90);
        codegen_emit_u32(&builder->buffer, offset);
        offset += 8;
    }
    if (size - offset >= 4)
    {
        codegen_emit_u8(&builder->buffer, 0x8b);
        codegen_emit_u8(&builder->buffer, 0x91);
        codegen_emit_u32(&builder->buffer, offset);
        codegen_emit_u8(&builder->buffer, 0x89);
        codegen_emit_u8(&builder->buffer, 0x90);
        codegen_emit_u32(&builder->buffer, offset);
        offset += 4;
    }
    if (size - offset >= 2)
    {
        codegen_emit_u8(&builder->buffer, 0x66);
        codegen_emit_u8(&builder->buffer, 0x8b);
        codegen_emit_u8(&builder->buffer, 0x91);
        codegen_emit_u32(&builder->buffer, offset);
        codegen_emit_u8(&builder->buffer, 0x66);
        codegen_emit_u8(&builder->buffer, 0x89);
        codegen_emit_u8(&builder->buffer, 0x90);
        codegen_emit_u32(&builder->buffer, offset);
        offset += 2;
    }
    if (size != offset)
    {
        codegen_emit_u8(&builder->buffer, 0x8a);
        codegen_emit_u8(&builder->buffer, 0x91);
        codegen_emit_u32(&builder->buffer, offset);
        codegen_emit_u8(&builder->buffer, 0x88);
        codegen_emit_u8(&builder->buffer, 0x90);
        codegen_emit_u32(&builder->buffer, offset);
    }
}

BUSTER_GLOBAL_LOCAL void x64_emit_load_memory_rax(
    X64Builder* builder,
    u32 size)
{
    if (size == 1)
    {
        codegen_emit_u8(&builder->buffer, 0x0f);
        codegen_emit_u8(&builder->buffer, 0xb6);
        codegen_emit_u8(&builder->buffer, 0x00);
    }
    else if (size == 2)
    {
        codegen_emit_u8(&builder->buffer, 0x0f);
        codegen_emit_u8(&builder->buffer, 0xb7);
        codegen_emit_u8(&builder->buffer, 0x00);
    }
    else if (size == 4)
    {
        codegen_emit_u8(&builder->buffer, 0x8b);
        codegen_emit_u8(&builder->buffer, 0x00);
    }
    else
    {
        codegen_emit_u8(&builder->buffer, 0x48);
        codegen_emit_u8(&builder->buffer, 0x8b);
        codegen_emit_u8(&builder->buffer, 0x00);
    }
}

BUSTER_GLOBAL_LOCAL void x64_emit_store_memory_rcx(
    X64Builder* builder,
    u32 size)
{
    if (size == 1)
    {
        codegen_emit_u8(&builder->buffer, 0x88);
        codegen_emit_u8(&builder->buffer, 0x08);
    }
    else if (size == 2)
    {
        codegen_emit_u8(&builder->buffer, 0x66);
        codegen_emit_u8(&builder->buffer, 0x89);
        codegen_emit_u8(&builder->buffer, 0x08);
    }
    else if (size == 4)
    {
        codegen_emit_u8(&builder->buffer, 0x89);
        codegen_emit_u8(&builder->buffer, 0x08);
    }
    else
    {
        codegen_emit_u8(&builder->buffer, 0x48);
        codegen_emit_u8(&builder->buffer, 0x89);
        codegen_emit_u8(&builder->buffer, 0x08);
    }
}

BUSTER_GLOBAL_LOCAL void x64_emit_load_memory(
    X64Builder* builder,
    X64Register target,
    X64Register base,
    u32 offset,
    u32 size)
{
    u8 rex = 0x40;
    rex |= target >= X64_REGISTER_R8 ? 0x04 : 0;
    rex |= base >= X64_REGISTER_R8 ? 0x01 : 0;
    if (size == 8)
    {
        rex |= 0x08;
    }
    if (size == 1 || size == 2)
    {
        if (size == 2)
        {
            codegen_emit_u8(&builder->buffer, 0x66);
        }
        if (rex != 0x40)
        {
            codegen_emit_u8(&builder->buffer, rex);
        }
        codegen_emit_u8(&builder->buffer, 0x0f);
        codegen_emit_u8(
            &builder->buffer,
            size == 1 ? 0xb6 : 0xb7);
    }
    else
    {
        if (rex != 0x40)
        {
            codegen_emit_u8(&builder->buffer, rex);
        }
        codegen_emit_u8(&builder->buffer, 0x8b);
    }
    codegen_emit_u8(
        &builder->buffer,
        (u8)(0x80 |
            ((target & 7) << 3) |
            (base & 7)));
    codegen_emit_u32(&builder->buffer, offset);
}

BUSTER_GLOBAL_LOCAL void x64_emit_store_memory(
    X64Builder* builder,
    X64Register base,
    u32 offset,
    X64Register source,
    u32 size)
{
    if (size == 2)
    {
        codegen_emit_u8(&builder->buffer, 0x66);
    }
    u8 rex = 0x40;
    rex |= source >= X64_REGISTER_R8 ? 0x04 : 0;
    rex |= base >= X64_REGISTER_R8 ? 0x01 : 0;
    rex |= size == 8 ? 0x08 : 0;
    if (rex != 0x40)
    {
        codegen_emit_u8(&builder->buffer, rex);
    }
    codegen_emit_u8(
        &builder->buffer,
        size == 1 ? 0x88 : 0x89);
    codegen_emit_u8(
        &builder->buffer,
        (u8)(0x80 |
            ((source & 7) << 3) |
            (base & 7)));
    codegen_emit_u32(&builder->buffer, offset);
}

BUSTER_GLOBAL_LOCAL void x64_emit_load_float_bits(
    X64Builder* builder,
    u32 target,
    X64Register base,
    u32 offset,
    u32 size)
{
    codegen_emit_u8(&builder->buffer, 0xf3);
    if (base >= X64_REGISTER_R8)
    {
        codegen_emit_u8(&builder->buffer, 0x41);
    }
    codegen_emit_u8(&builder->buffer, 0x0f);
    codegen_emit_u8(
        &builder->buffer,
        size <= 4 ? 0x10 : 0x7e);
    codegen_emit_u8(
        &builder->buffer,
        (u8)(0x80 |
            ((target & 7) << 3) |
            (base & 7)));
    codegen_emit_u32(&builder->buffer, offset);
}

BUSTER_GLOBAL_LOCAL void x64_emit_store_float_bits(
    X64Builder* builder,
    X64Register base,
    u32 offset,
    u32 source,
    u32 size)
{
    codegen_emit_u8(
        &builder->buffer,
        size <= 4 ? 0xf3 : 0x66);
    if (base >= X64_REGISTER_R8)
    {
        codegen_emit_u8(&builder->buffer, 0x41);
    }
    codegen_emit_u8(&builder->buffer, 0x0f);
    codegen_emit_u8(
        &builder->buffer,
        size <= 4 ? 0x11 : 0xd6);
    codegen_emit_u8(
        &builder->buffer,
        (u8)(0x80 |
            ((source & 7) << 3) |
            (base & 7)));
    codegen_emit_u32(&builder->buffer, offset);
}

BUSTER_GLOBAL_LOCAL void x64_emit_rsp_address(
    X64Builder* builder,
    X64Register target,
    u32 offset)
{
    codegen_emit_u8(
        &builder->buffer,
        target >= X64_REGISTER_R8 ? 0x4c : 0x48);
    codegen_emit_u8(&builder->buffer, 0x8d);
    codegen_emit_u8(
        &builder->buffer,
        (u8)(0x84 | ((target & 7) << 3)));
    codegen_emit_u8(&builder->buffer, 0x24);
    codegen_emit_u32(&builder->buffer, offset);
}

BUSTER_GLOBAL_LOCAL bool x64_emit_collection_component(
    X64Builder* builder,
    IrValueId base_id,
    u32 component,
    X64Register target)
{
    IrValue* base_value =
        builder->function->values + base_id.value;
    AnalysisType* base_type = analysis_type_from_id(
        builder->analysis,
        base_value->type);
    if (base_value->category == IR_VALUE_PLACE)
    {
        if (base_type->kind == ANALYSIS_TYPE_ARRAY ||
            base_type->kind == ANALYSIS_TYPE_VECTOR ||
            base_type->kind == ANALYSIS_TYPE_INFERRED_ARRAY)
        {
            bool vector = base_type->kind == ANALYSIS_TYPE_VECTOR;
            if (component == 0)
            {
                x64_emit_load(
                    builder,
                    target,
                    x64_value_displacement(base_id));
            }
            else if (component == 1)
            {
                x64_emit_constant_register(
                    builder,
                    target,
                    vector ?
                        base_type->as.vector.count :
                        base_type->as.array.count);
            }
            else if (component == 2)
            {
                AnalysisType* element = analysis_type_from_id(
                    builder->analysis,
                    vector ?
                        base_type->as.vector.element_type :
                        base_type->as.array.element_type);
                x64_emit_constant_register(
                    builder,
                    target,
                    codegen_type_storage_size(element));
            }
            else
            {
                x64_emit_constant_register(builder, target, 0);
            }
            return true;
        }
        if (base_type->kind == ANALYSIS_TYPE_SLICE)
        {
            x64_emit_load(
                builder,
                X64_REGISTER_RAX,
                x64_value_displacement(base_id));
            u8 rex = target >= X64_REGISTER_R8 ? 0x4c : 0x48;
            u8 register_bits = (u8)(target & 7);
            codegen_emit_u8(&builder->buffer, rex);
            codegen_emit_u8(&builder->buffer, 0x8b);
            codegen_emit_u8(
                &builder->buffer,
                (u8)(0x80 | (register_bits << 3)));
            codegen_emit_u32(
                &builder->buffer,
                component * 8);
            return true;
        }
    }
    x64_emit_load(
        builder,
        target,
        x64_value_displacement_component(
            base_id,
            component));
    return true;
}

BUSTER_GLOBAL_LOCAL void x64_emit_float_load(
    X64Builder* builder,
    u32 register_index,
    s32 displacement,
    u32 width)
{
    codegen_emit_u8(
        &builder->buffer,
        width == 32 ? 0xf3 : 0xf2);
    codegen_emit_u8(&builder->buffer, 0x0f);
    codegen_emit_u8(&builder->buffer, 0x10);
    codegen_emit_u8(
        &builder->buffer,
        (u8)(0x85 | ((register_index & 7) << 3)));
    codegen_emit_u32(&builder->buffer, (u32)displacement);
}

BUSTER_GLOBAL_LOCAL void x64_emit_float_store(
    X64Builder* builder,
    u32 register_index,
    s32 displacement,
    u32 width)
{
    codegen_emit_u8(
        &builder->buffer,
        width == 32 ? 0xf3 : 0xf2);
    codegen_emit_u8(&builder->buffer, 0x0f);
    codegen_emit_u8(&builder->buffer, 0x11);
    codegen_emit_u8(
        &builder->buffer,
        (u8)(0x85 | ((register_index & 7) << 3)));
    codegen_emit_u32(&builder->buffer, (u32)displacement);
}

BUSTER_GLOBAL_LOCAL X64Register x64_abi_integer_argument_register(
    CodegenAbi abi,
    u32 index)
{
    X64Register system_v[] = {
        X64_REGISTER_RDI,
        X64_REGISTER_RSI,
        X64_REGISTER_RDX,
        X64_REGISTER_RCX,
        X64_REGISTER_R8,
        X64_REGISTER_R9,
    };
    X64Register windows[] = {
        X64_REGISTER_RCX,
        X64_REGISTER_RDX,
        X64_REGISTER_R8,
        X64_REGISTER_R9,
    };
    return abi == CODEGEN_ABI_X86_64_WINDOWS ?
        windows[index] :
        system_v[index];
}

BUSTER_GLOBAL_LOCAL void x64_emit_initialize_aggregate_result(
    X64Builder* builder,
    IrValueId value)
{
    x64_emit_address(
        builder,
        X64_REGISTER_RAX,
        -(s32)builder->value_storage_offsets[value.value]);
    x64_emit_store(
        builder,
        X64_REGISTER_RAX,
        x64_value_displacement_component(value, 0));
}

BUSTER_GLOBAL_LOCAL bool x64_emit_vector_abi_move(
    X64Builder* builder,
    bool store,
    u32 vector_register,
    X64Register base,
    u32 offset,
    u32 size)
{
    if (vector_register >= 8 ||
        base >= X64_REGISTER_R8 ||
        size > target_vector_register_size(builder->target) ||
        (size != 8 && size != 16 &&
            size != 32 && size != 64))
    {
        return false;
    }
    if (size == 64)
    {
        codegen_emit_u8(&builder->buffer, 0x62);
        codegen_emit_u8(&builder->buffer, 0xf1);
        codegen_emit_u8(&builder->buffer, 0x7c);
        codegen_emit_u8(&builder->buffer, 0x48);
    }
    else if (size == 32)
    {
        codegen_emit_u8(&builder->buffer, 0xc5);
        codegen_emit_u8(&builder->buffer, 0xfc);
    }
    else
    {
        if (size == 8)
        {
            codegen_emit_u8(
                &builder->buffer,
                store ? 0x66 : 0xf3);
        }
        codegen_emit_u8(&builder->buffer, 0x0f);
    }
    codegen_emit_u8(
        &builder->buffer,
        size == 8 ?
            (store ? 0xd6 : 0x7e) :
            (store ? 0x11 : 0x10));
    codegen_emit_u8(
        &builder->buffer,
        (u8)(0x80 |
            ((vector_register & 7) << 3) |
            (base & 7)));
    codegen_emit_u32(&builder->buffer, offset);
    return true;
}

BUSTER_GLOBAL_LOCAL void x64_emit_load_abi_part(
    X64Builder* builder,
    IrValueId value,
    AnalysisType* type,
    CodegenAbiPart* part,
    X64Register integer_target,
    u32 float_target)
{
    if (codegen_type_is_indirect_value(type))
    {
        if (type->kind == ANALYSIS_TYPE_VECTOR &&
            part->kind ==
                CODEGEN_ABI_LOCATION_FLOAT_REGISTER)
        {
            x64_emit_load(
                builder,
                X64_REGISTER_RAX,
                x64_value_displacement_component(
                    value,
                    0));
            if (!x64_emit_vector_abi_move(
                    builder,
                    false,
                    float_target,
                    X64_REGISTER_RAX,
                    part->value_offset,
                    part->size))
            {
                builder->buffer.error =
                    CODEGEN_ERROR_UNSUPPORTED_ABI;
            }
            return;
        }
        x64_emit_load(
            builder,
            X64_REGISTER_R11,
            x64_value_displacement_component(value, 0));
        if (part->kind ==
            CODEGEN_ABI_LOCATION_FLOAT_REGISTER)
        {
            x64_emit_load_float_bits(
                builder,
                float_target,
                X64_REGISTER_R11,
                part->value_offset,
                part->size);
        }
        else
        {
            x64_emit_load_memory(
                builder,
                integer_target,
                X64_REGISTER_R11,
                part->value_offset,
                part->size);
        }
        return;
    }
    if (codegen_type_is_inline_collection(type))
    {
        u32 component = part->value_offset / 8;
        if (type->kind == ANALYSIS_TYPE_RANGE)
        {
            AnalysisType* element = analysis_type_from_id(
                builder->analysis,
                type->as.element_type);
            component = part->value_offset /
                BUSTER_MAX(
                    codegen_type_storage_size(element),
                    1);
        }
        if (part->kind ==
            CODEGEN_ABI_LOCATION_FLOAT_REGISTER)
        {
            x64_emit_float_load(
                builder,
                float_target,
                x64_value_displacement_component(
                    value,
                    component),
                part->size * 8);
        }
        else
        {
            x64_emit_load(
                builder,
                integer_target,
                x64_value_displacement_component(
                    value,
                    component));
            if (type->kind == ANALYSIS_TYPE_RANGE)
            {
                AnalysisType* element =
                    analysis_type_from_id(
                        builder->analysis,
                        type->as.element_type);
                u32 element_size =
                    codegen_type_storage_size(element);
                if (element_size < 8 &&
                    part->size > element_size)
                {
                    u8 target_rex =
                        integer_target >=
                                X64_REGISTER_R8 ?
                            0x49 :
                            0x48;
                    u8 target_bits =
                        (u8)(integer_target & 7);
                    u8 shift =
                        (u8)(element_size * 8);
                    codegen_emit_u8(
                        &builder->buffer,
                        target_rex);
                    codegen_emit_u8(
                        &builder->buffer,
                        0xc1);
                    codegen_emit_u8(
                        &builder->buffer,
                        (u8)(0xe0 | target_bits));
                    codegen_emit_u8(
                        &builder->buffer,
                        (u8)(64 - shift));
                    codegen_emit_u8(
                        &builder->buffer,
                        target_rex);
                    codegen_emit_u8(
                        &builder->buffer,
                        0xc1);
                    codegen_emit_u8(
                        &builder->buffer,
                        (u8)(0xe8 | target_bits));
                    codegen_emit_u8(
                        &builder->buffer,
                        (u8)(64 - shift));
                    x64_emit_load(
                        builder,
                        X64_REGISTER_R11,
                        x64_value_displacement_component(
                            value,
                            component + 1));
                    codegen_emit_u8(
                        &builder->buffer,
                        0x49);
                    codegen_emit_u8(
                        &builder->buffer,
                        0xc1);
                    codegen_emit_u8(
                        &builder->buffer,
                        0xe3);
                    codegen_emit_u8(
                        &builder->buffer,
                        shift);
                    codegen_emit_u8(
                        &builder->buffer,
                        integer_target >=
                                X64_REGISTER_R8 ?
                            0x4d :
                            0x4c);
                    codegen_emit_u8(
                        &builder->buffer,
                        0x09);
                    codegen_emit_u8(
                        &builder->buffer,
                        (u8)(0xd8 | target_bits));
                }
            }
        }
        return;
    }
    if (part->kind == CODEGEN_ABI_LOCATION_FLOAT_REGISTER)
    {
        x64_emit_float_load(
            builder,
            float_target,
            x64_value_displacement(value),
            type->as.float_bit_width);
    }
    else
    {
        x64_emit_load_value(builder, integer_target, value);
    }
}

BUSTER_GLOBAL_LOCAL void x64_emit_store_abi_part(
    X64Builder* builder,
    IrValueId value,
    AnalysisType* type,
    CodegenAbiPart* part,
    X64Register integer_source,
    u32 float_source)
{
    if (codegen_type_is_indirect_value(type))
    {
        if (type->kind == ANALYSIS_TYPE_VECTOR &&
            part->kind ==
                CODEGEN_ABI_LOCATION_FLOAT_REGISTER)
        {
            x64_emit_load(
                builder,
                X64_REGISTER_RAX,
                x64_value_displacement_component(
                    value,
                    0));
            if (!x64_emit_vector_abi_move(
                    builder,
                    true,
                    float_source,
                    X64_REGISTER_RAX,
                    part->value_offset,
                    part->size))
            {
                builder->buffer.error =
                    CODEGEN_ERROR_UNSUPPORTED_ABI;
            }
            return;
        }
        x64_emit_load(
            builder,
            X64_REGISTER_R11,
            x64_value_displacement_component(value, 0));
        if (part->kind ==
            CODEGEN_ABI_LOCATION_FLOAT_REGISTER)
        {
            x64_emit_store_float_bits(
                builder,
                X64_REGISTER_R11,
                part->value_offset,
                float_source,
                part->size);
        }
        else
        {
            x64_emit_store_memory(
                builder,
                X64_REGISTER_R11,
                part->value_offset,
                integer_source,
                part->size);
        }
        return;
    }
    if (codegen_type_is_inline_collection(type))
    {
        u32 component = part->value_offset / 8;
        if (type->kind == ANALYSIS_TYPE_RANGE)
        {
            AnalysisType* element = analysis_type_from_id(
                builder->analysis,
                type->as.element_type);
            component = part->value_offset /
                BUSTER_MAX(
                    codegen_type_storage_size(element),
                    1);
        }
        if (part->kind ==
            CODEGEN_ABI_LOCATION_FLOAT_REGISTER)
        {
            x64_emit_float_store(
                builder,
                float_source,
                x64_value_displacement_component(
                    value,
                    component),
                part->size * 8);
        }
        else
        {
            x64_emit_store(
                builder,
                integer_source,
                x64_value_displacement_component(
                    value,
                    component));
            if (type->kind == ANALYSIS_TYPE_RANGE)
            {
                AnalysisType* element =
                    analysis_type_from_id(
                        builder->analysis,
                        type->as.element_type);
                u32 element_size =
                    codegen_type_storage_size(element);
                if (element_size < 8 &&
                    part->size > element_size)
                {
                    x64_emit_move_register(
                        builder,
                        X64_REGISTER_R11,
                        integer_source);
                    codegen_emit_u8(
                        &builder->buffer,
                        0x49);
                    codegen_emit_u8(
                        &builder->buffer,
                        0xc1);
                    codegen_emit_u8(
                        &builder->buffer,
                        0xeb);
                    codegen_emit_u8(
                        &builder->buffer,
                        (u8)(element_size * 8));
                    x64_emit_store(
                        builder,
                        X64_REGISTER_R11,
                        x64_value_displacement_component(
                            value,
                            component + 1));
                }
            }
        }
    }
}

BUSTER_GLOBAL_LOCAL void x64_emit_stack_adjust(
    X64Builder* builder,
    u32 size,
    bool subtract)
{
    if (!size)
    {
        return;
    }
    codegen_emit_u8(&builder->buffer, 0x48);
    codegen_emit_u8(&builder->buffer, 0x81);
    codegen_emit_u8(
        &builder->buffer,
        subtract ? 0xec : 0xc4);
    codegen_emit_u32(&builder->buffer, size);
}

BUSTER_GLOBAL_LOCAL void x64_emit_store_rsp(
    X64Builder* builder,
    X64Register source,
    u32 offset)
{
    u8 rex = source >= X64_REGISTER_R8 ? 0x4c : 0x48;
    u8 register_bits = (u8)(source & 7);
    codegen_emit_u8(&builder->buffer, rex);
    codegen_emit_u8(&builder->buffer, 0x89);
    codegen_emit_u8(
        &builder->buffer,
        (u8)(0x84 | (register_bits << 3)));
    codegen_emit_u8(&builder->buffer, 0x24);
    codegen_emit_u32(&builder->buffer, offset);
}

BUSTER_GLOBAL_LOCAL void x64_emit_float_store_rsp(
    X64Builder* builder,
    u32 register_index,
    u32 offset,
    u32 width)
{
    codegen_emit_u8(
        &builder->buffer,
        width == 32 ? 0xf3 : 0xf2);
    codegen_emit_u8(&builder->buffer, 0x0f);
    codegen_emit_u8(&builder->buffer, 0x11);
    codegen_emit_u8(
        &builder->buffer,
        (u8)(0x84 | ((register_index & 7) << 3)));
    codegen_emit_u8(&builder->buffer, 0x24);
    codegen_emit_u32(&builder->buffer, offset);
}

BUSTER_GLOBAL_LOCAL void x64_emit_store_result(
    X64Builder* builder,
    IrInstruction* instruction)
{
    if (instruction->result.value != IR_ID_UNDERLYING_INVALID)
    {
        if (builder->value_registers[
                instruction->result.value] !=
            CODEGEN_REGISTER_UNALLOCATED)
        {
            x64_emit_move_register(
                builder,
                x64_allocated_register(
                    builder,
                    instruction->result),
                X64_REGISTER_RAX);
        }
        else
        {
            x64_emit_store(
                builder,
                X64_REGISTER_RAX,
                x64_value_displacement(
                    instruction->result));
        }
    }
}

BUSTER_GLOBAL_LOCAL void x64_relocation_add(
    X64Builder* builder,
    IrBlockId target)
{
    CodegenRelocation* relocation = arena_allocate(
        builder->arena,
        CodegenRelocation,
        1);
    *relocation = (CodegenRelocation){
        .target = target,
        .displacement_offset = (u32)builder->buffer.count,
    };
    if (builder->last_relocation)
    {
        builder->last_relocation->next = relocation;
    }
    else
    {
        builder->first_relocation = relocation;
    }
    builder->last_relocation = relocation;
    codegen_emit_u32(&builder->buffer, 0);
}

BUSTER_GLOBAL_LOCAL void x64_emit_jump(
    X64Builder* builder,
    IrBlockId target)
{
    codegen_emit_u8(&builder->buffer, 0xe9);
    x64_relocation_add(builder, target);
}

BUSTER_GLOBAL_LOCAL void x64_call_relocation_add(
    X64Builder* builder,
    IrInstruction* instruction)
{
    CodegenCallRelocation* relocation = arena_allocate(
        builder->arena,
        CodegenCallRelocation,
        1);
    *relocation = (CodegenCallRelocation){
        .entity = instruction->entity,
        .instantiation = instruction->instantiation,
        .displacement_offset = (u32)builder->buffer.count,
    };
    if (builder->last_call_relocation)
    {
        builder->last_call_relocation->next = relocation;
    }
    else
    {
        builder->first_call_relocation = relocation;
    }
    builder->last_call_relocation = relocation;
    codegen_emit_u32(&builder->buffer, 0);
}

BUSTER_GLOBAL_LOCAL IrValueId x64_parameter_incoming(
    IrBlockParameter* parameter,
    IrBlockId predecessor)
{
    for (IrIncoming* incoming = parameter->first_incoming;
        incoming;
        incoming = incoming->next)
    {
        if (incoming->predecessor.value == predecessor.value)
        {
            return incoming->value;
        }
    }
    return IR_VALUE_ID_INVALID;
}

BUSTER_GLOBAL_LOCAL void x64_emit_edge_copies(
    X64Builder* builder,
    IrBlockId predecessor,
    IrBlockId target)
{
    IrBlock* block = builder->function->blocks + target.value;
    u32 index = 0;
    for (IrBlockParameter* parameter = block->first_parameter;
        parameter;
        parameter = parameter->next)
    {
        IrValueId incoming =
            x64_parameter_incoming(parameter, predecessor);
        if (incoming.value == IR_ID_UNDERLYING_INVALID)
        {
            builder->buffer.error = CODEGEN_ERROR_INVALID_IR;
            return;
        }
        for (u32 component = 0;
            component < X64_VALUE_SLOT_COMPONENT_COUNT;
            component += 1)
        {
            x64_emit_load(
                builder,
                X64_REGISTER_RAX,
                x64_value_displacement_component(
                    incoming,
                    component));
            x64_emit_store(
                builder,
                X64_REGISTER_RAX,
                x64_temporary_displacement(builder, index) -
                    (s32)(component * 8));
        }
        index += 1;
    }
    index = 0;
    for (IrBlockParameter* parameter = block->first_parameter;
        parameter;
        parameter = parameter->next)
    {
        for (u32 component = 0;
            component < X64_VALUE_SLOT_COMPONENT_COUNT;
            component += 1)
        {
            x64_emit_load(
                builder,
                X64_REGISTER_RAX,
                x64_temporary_displacement(builder, index) -
                    (s32)(component * 8));
            x64_emit_store(
                builder,
                X64_REGISTER_RAX,
                x64_value_displacement_component(
                    parameter->value,
                    component));
        }
        index += 1;
    }
}

BUSTER_GLOBAL_LOCAL void x64_emit_return(X64Builder* builder)
{
    codegen_emit_u8(&builder->buffer, 0xc9);
    codegen_emit_u8(&builder->buffer, 0xc3);
}

BUSTER_GLOBAL_LOCAL void x64_emit_set_condition(
    X64Builder* builder,
    u8 condition)
{
    codegen_emit_u8(&builder->buffer, 0x0f);
    codegen_emit_u8(&builder->buffer, condition);
    codegen_emit_u8(&builder->buffer, 0xc0);
    codegen_emit_u8(&builder->buffer, 0x0f);
    codegen_emit_u8(&builder->buffer, 0xb6);
    codegen_emit_u8(&builder->buffer, 0xc0);
}

BUSTER_GLOBAL_LOCAL bool x64_emit_integer_binary(
    X64Builder* builder,
    IrInstruction* instruction)
{
    if (instruction->binary_operation == IR_BINARY_RANGE)
    {
        x64_emit_load_value(
            builder,
            X64_REGISTER_RAX,
            instruction->operands[0]);
        x64_emit_store(
            builder,
            X64_REGISTER_RAX,
            x64_value_displacement_component(
                instruction->result,
                0));
        x64_emit_load_value(
            builder,
            X64_REGISTER_RAX,
            instruction->operands[1]);
        x64_emit_store(
            builder,
            X64_REGISTER_RAX,
            x64_value_displacement_component(
                instruction->result,
                1));
        codegen_emit_u8(&builder->buffer, 0x31);
        codegen_emit_u8(&builder->buffer, 0xc0);
        x64_emit_store(
            builder,
            X64_REGISTER_RAX,
            x64_value_displacement_component(
                instruction->result,
                2));
        return true;
    }
    x64_emit_load_value(
        builder,
        X64_REGISTER_RAX,
        instruction->operands[0]);
    x64_emit_load_value(
        builder,
        X64_REGISTER_RCX,
        instruction->operands[1]);
    switch (instruction->binary_operation)
    {
        case IR_BINARY_INTEGER_ADD:
            codegen_emit_u8(&builder->buffer, 0x48);
            codegen_emit_u8(&builder->buffer, 0x01);
            codegen_emit_u8(&builder->buffer, 0xc8);
            break;
        case IR_BINARY_INTEGER_SUBTRACT:
            codegen_emit_u8(&builder->buffer, 0x48);
            codegen_emit_u8(&builder->buffer, 0x29);
            codegen_emit_u8(&builder->buffer, 0xc8);
            break;
        case IR_BINARY_INTEGER_MULTIPLY:
            codegen_emit_u8(&builder->buffer, 0x48);
            codegen_emit_u8(&builder->buffer, 0x0f);
            codegen_emit_u8(&builder->buffer, 0xaf);
            codegen_emit_u8(&builder->buffer, 0xc1);
            break;
        case IR_BINARY_INTEGER_BITWISE_AND:
        case IR_BINARY_BOOLEAN_AND:
            codegen_emit_u8(&builder->buffer, 0x48);
            codegen_emit_u8(&builder->buffer, 0x21);
            codegen_emit_u8(&builder->buffer, 0xc8);
            break;
        case IR_BINARY_INTEGER_BITWISE_OR:
        case IR_BINARY_BOOLEAN_OR:
            codegen_emit_u8(&builder->buffer, 0x48);
            codegen_emit_u8(&builder->buffer, 0x09);
            codegen_emit_u8(&builder->buffer, 0xc8);
            break;
        case IR_BINARY_INTEGER_BITWISE_XOR:
            codegen_emit_u8(&builder->buffer, 0x48);
            codegen_emit_u8(&builder->buffer, 0x31);
            codegen_emit_u8(&builder->buffer, 0xc8);
            break;
        case IR_BINARY_SHIFT_LEFT:
        case IR_BINARY_SIGNED_SHIFT_RIGHT:
        case IR_BINARY_UNSIGNED_SHIFT_RIGHT:
            codegen_emit_u8(&builder->buffer, 0x48);
            codegen_emit_u8(&builder->buffer, 0xd3);
            codegen_emit_u8(
                &builder->buffer,
                instruction->binary_operation == IR_BINARY_SHIFT_LEFT ?
                    0xe0 :
                    instruction->binary_operation ==
                        IR_BINARY_SIGNED_SHIFT_RIGHT ?
                        0xf8 :
                        0xe8);
            break;
        case IR_BINARY_SIGNED_DIVIDE:
        case IR_BINARY_SIGNED_REMAINDER:
            codegen_emit_u8(&builder->buffer, 0x48);
            codegen_emit_u8(&builder->buffer, 0x99);
            codegen_emit_u8(&builder->buffer, 0x48);
            codegen_emit_u8(&builder->buffer, 0xf7);
            codegen_emit_u8(&builder->buffer, 0xf9);
            if (instruction->binary_operation ==
                IR_BINARY_SIGNED_REMAINDER)
            {
                codegen_emit_u8(&builder->buffer, 0x48);
                codegen_emit_u8(&builder->buffer, 0x89);
                codegen_emit_u8(&builder->buffer, 0xd0);
            }
            break;
        case IR_BINARY_UNSIGNED_DIVIDE:
        case IR_BINARY_UNSIGNED_REMAINDER:
            codegen_emit_u8(&builder->buffer, 0x31);
            codegen_emit_u8(&builder->buffer, 0xd2);
            codegen_emit_u8(&builder->buffer, 0x48);
            codegen_emit_u8(&builder->buffer, 0xf7);
            codegen_emit_u8(&builder->buffer, 0xf1);
            if (instruction->binary_operation ==
                IR_BINARY_UNSIGNED_REMAINDER)
            {
                codegen_emit_u8(&builder->buffer, 0x48);
                codegen_emit_u8(&builder->buffer, 0x89);
                codegen_emit_u8(&builder->buffer, 0xd0);
            }
            break;
        case IR_BINARY_INTEGER_EQUAL:
        case IR_BINARY_BOOLEAN_EQUAL:
        case IR_BINARY_POINTER_EQUAL:
        case IR_BINARY_INTEGER_NOT_EQUAL:
        case IR_BINARY_BOOLEAN_NOT_EQUAL:
        case IR_BINARY_POINTER_NOT_EQUAL:
        case IR_BINARY_SIGNED_LESS:
        case IR_BINARY_SIGNED_LESS_EQUAL:
        case IR_BINARY_SIGNED_GREATER:
        case IR_BINARY_SIGNED_GREATER_EQUAL:
        case IR_BINARY_UNSIGNED_LESS:
        case IR_BINARY_UNSIGNED_LESS_EQUAL:
        case IR_BINARY_UNSIGNED_GREATER:
        case IR_BINARY_UNSIGNED_GREATER_EQUAL:
        {
            codegen_emit_u8(&builder->buffer, 0x48);
            codegen_emit_u8(&builder->buffer, 0x39);
            codegen_emit_u8(&builder->buffer, 0xc8);
            u8 condition =
                instruction->binary_operation == IR_BINARY_INTEGER_EQUAL ||
                    instruction->binary_operation == IR_BINARY_BOOLEAN_EQUAL ||
                    instruction->binary_operation == IR_BINARY_POINTER_EQUAL ?
                    0x94 :
                instruction->binary_operation == IR_BINARY_INTEGER_NOT_EQUAL ||
                    instruction->binary_operation == IR_BINARY_BOOLEAN_NOT_EQUAL ||
                    instruction->binary_operation == IR_BINARY_POINTER_NOT_EQUAL ?
                    0x95 :
                instruction->binary_operation == IR_BINARY_SIGNED_LESS ?
                    0x9c :
                instruction->binary_operation == IR_BINARY_SIGNED_LESS_EQUAL ?
                    0x9e :
                instruction->binary_operation == IR_BINARY_SIGNED_GREATER ?
                    0x9f :
                instruction->binary_operation == IR_BINARY_SIGNED_GREATER_EQUAL ?
                    0x9d :
                instruction->binary_operation == IR_BINARY_UNSIGNED_LESS ?
                    0x92 :
                instruction->binary_operation == IR_BINARY_UNSIGNED_LESS_EQUAL ?
                    0x96 :
                instruction->binary_operation == IR_BINARY_UNSIGNED_GREATER ?
                    0x97 :
                    0x93;
            x64_emit_set_condition(builder, condition);
        } break;
        default: return false;
    }
    x64_emit_store_result(builder, instruction);
    return true;
}

BUSTER_GLOBAL_LOCAL bool x64_emit_float_binary(
    X64Builder* builder,
    IrInstruction* instruction)
{
    AnalysisType* operand_type = analysis_type_from_id(
        builder->analysis,
        builder->function->values[
            instruction->operands[0].value].type);
    u32 width = operand_type->as.float_bit_width;
    x64_emit_float_load(
        builder,
        0,
        x64_value_displacement(instruction->operands[0]),
        width);
    x64_emit_float_load(
        builder,
        1,
        x64_value_displacement(instruction->operands[1]),
        width);
    if (instruction->binary_operation >= IR_BINARY_FLOAT_ADD &&
        instruction->binary_operation <= IR_BINARY_FLOAT_DIVIDE)
    {
        codegen_emit_u8(
            &builder->buffer,
            width == 32 ? 0xf3 : 0xf2);
        codegen_emit_u8(&builder->buffer, 0x0f);
        codegen_emit_u8(
            &builder->buffer,
            instruction->binary_operation == IR_BINARY_FLOAT_ADD ?
                0x58 :
            instruction->binary_operation == IR_BINARY_FLOAT_SUBTRACT ?
                0x5c :
            instruction->binary_operation == IR_BINARY_FLOAT_MULTIPLY ?
                0x59 :
                0x5e);
        codegen_emit_u8(&builder->buffer, 0xc1);
        x64_emit_float_store(
            builder,
            0,
            x64_value_displacement(instruction->result),
            width);
        return true;
    }
    if (width == 64)
    {
        codegen_emit_u8(&builder->buffer, 0x66);
    }
    codegen_emit_u8(&builder->buffer, 0x0f);
    codegen_emit_u8(&builder->buffer, 0x2e);
    codegen_emit_u8(&builder->buffer, 0xc1);
    u8 condition =
        instruction->binary_operation == IR_BINARY_FLOAT_EQUAL ?
            0x94 :
        instruction->binary_operation == IR_BINARY_FLOAT_NOT_EQUAL ?
            0x95 :
        instruction->binary_operation == IR_BINARY_FLOAT_LESS ?
            0x92 :
        instruction->binary_operation == IR_BINARY_FLOAT_LESS_EQUAL ?
            0x96 :
        instruction->binary_operation == IR_BINARY_FLOAT_GREATER ?
            0x97 :
        instruction->binary_operation == IR_BINARY_FLOAT_GREATER_EQUAL ?
            0x93 :
            0;
    if (!condition)
    {
        return false;
    }
    codegen_emit_u8(&builder->buffer, 0x0f);
    codegen_emit_u8(&builder->buffer, condition);
    codegen_emit_u8(&builder->buffer, 0xc0);
    if (instruction->binary_operation ==
            IR_BINARY_FLOAT_EQUAL ||
        instruction->binary_operation ==
            IR_BINARY_FLOAT_LESS ||
        instruction->binary_operation ==
            IR_BINARY_FLOAT_LESS_EQUAL)
    {
        codegen_emit_u8(&builder->buffer, 0x0f);
        codegen_emit_u8(&builder->buffer, 0x9b);
        codegen_emit_u8(&builder->buffer, 0xc2);
        codegen_emit_u8(&builder->buffer, 0x20);
        codegen_emit_u8(&builder->buffer, 0xd0);
    }
    else if (instruction->binary_operation ==
        IR_BINARY_FLOAT_NOT_EQUAL)
    {
        codegen_emit_u8(&builder->buffer, 0x0f);
        codegen_emit_u8(&builder->buffer, 0x9a);
        codegen_emit_u8(&builder->buffer, 0xc2);
        codegen_emit_u8(&builder->buffer, 0x08);
        codegen_emit_u8(&builder->buffer, 0xd0);
    }
    codegen_emit_u8(&builder->buffer, 0x0f);
    codegen_emit_u8(&builder->buffer, 0xb6);
    codegen_emit_u8(&builder->buffer, 0xc0);
    x64_emit_store_result(builder, instruction);
    return true;
}

BUSTER_GLOBAL_LOCAL void x64_emit_vector_memory_operation(
    X64Builder* builder,
    u8 prefix,
    u8 opcode,
    u32 vector_register,
    X64Register base)
{
    if (prefix)
    {
        codegen_emit_u8(&builder->buffer, prefix);
    }
    if (vector_register >= 8 || base >= X64_REGISTER_R8)
    {
        codegen_emit_u8(
            &builder->buffer,
            (u8)(0x40 |
                (vector_register >= 8 ? 0x04 : 0) |
                (base >= X64_REGISTER_R8 ? 0x01 : 0)));
    }
    codegen_emit_u8(&builder->buffer, 0x0f);
    codegen_emit_u8(&builder->buffer, opcode);
    codegen_emit_u8(
        &builder->buffer,
        (u8)(((vector_register & 7) << 3) | (base & 7)));
}

BUSTER_GLOBAL_LOCAL void x64_emit_vector_native_memory(
    X64Builder* builder,
    bool store,
    u32 size,
    X64Register base)
{
    if (size == 64)
    {
        codegen_emit_u8(&builder->buffer, 0x62);
        codegen_emit_u8(&builder->buffer, 0xf1);
        codegen_emit_u8(&builder->buffer, 0x7c);
        codegen_emit_u8(&builder->buffer, 0x48);
    }
    else
    {
        codegen_emit_u8(&builder->buffer, 0xc5);
        codegen_emit_u8(&builder->buffer, 0xfc);
    }
    codegen_emit_u8(
        &builder->buffer,
        store ? 0x11 : 0x10);
    codegen_emit_u8(
        &builder->buffer,
        (u8)(base & 7));
}

BUSTER_GLOBAL_LOCAL void x64_emit_vector_native_binary_operation(
    X64Builder* builder,
    u8 prefix,
    u8 opcode,
    u32 size,
    X64Register base)
{
    u8 packed_prefix = prefix == 0x66 ? 1 : 0;
    if (size == 64)
    {
        codegen_emit_u8(&builder->buffer, 0x62);
        codegen_emit_u8(&builder->buffer, 0xf1);
        codegen_emit_u8(
            &builder->buffer,
            (u8)(0x7c | packed_prefix));
        codegen_emit_u8(&builder->buffer, 0x48);
    }
    else
    {
        codegen_emit_u8(&builder->buffer, 0xc5);
        codegen_emit_u8(
            &builder->buffer,
            (u8)(0xfc | packed_prefix));
    }
    codegen_emit_u8(&builder->buffer, opcode);
    codegen_emit_u8(
        &builder->buffer,
        (u8)(base & 7));
}

BUSTER_GLOBAL_LOCAL bool x64_vector_comparison_condition(
    IrBinaryOperation operation,
    u8* condition_out,
    bool* ordered_out,
    bool* unordered_out)
{
    u8 condition = 0;
    bool ordered = false;
    bool unordered = false;
    switch (operation)
    {
        case IR_BINARY_VECTOR_INTEGER_EQUAL:
        case IR_BINARY_VECTOR_FLOAT_EQUAL:
            condition = 0x94;
            ordered = operation ==
                IR_BINARY_VECTOR_FLOAT_EQUAL;
            break;
        case IR_BINARY_VECTOR_INTEGER_NOT_EQUAL:
        case IR_BINARY_VECTOR_FLOAT_NOT_EQUAL:
            condition = 0x95;
            unordered = operation ==
                IR_BINARY_VECTOR_FLOAT_NOT_EQUAL;
            break;
        case IR_BINARY_VECTOR_SIGNED_LESS:
            condition = 0x9c;
            break;
        case IR_BINARY_VECTOR_SIGNED_LESS_EQUAL:
            condition = 0x9e;
            break;
        case IR_BINARY_VECTOR_SIGNED_GREATER:
            condition = 0x9f;
            break;
        case IR_BINARY_VECTOR_SIGNED_GREATER_EQUAL:
            condition = 0x9d;
            break;
        case IR_BINARY_VECTOR_UNSIGNED_LESS:
        case IR_BINARY_VECTOR_FLOAT_LESS:
            condition = 0x92;
            ordered = operation ==
                IR_BINARY_VECTOR_FLOAT_LESS;
            break;
        case IR_BINARY_VECTOR_UNSIGNED_LESS_EQUAL:
        case IR_BINARY_VECTOR_FLOAT_LESS_EQUAL:
            condition = 0x96;
            ordered = operation ==
                IR_BINARY_VECTOR_FLOAT_LESS_EQUAL;
            break;
        case IR_BINARY_VECTOR_UNSIGNED_GREATER:
        case IR_BINARY_VECTOR_FLOAT_GREATER:
            condition = 0x97;
            break;
        case IR_BINARY_VECTOR_UNSIGNED_GREATER_EQUAL:
        case IR_BINARY_VECTOR_FLOAT_GREATER_EQUAL:
            condition = 0x93;
            break;
        default:
            return false;
    }
    *condition_out = condition;
    *ordered_out = ordered;
    *unordered_out = unordered;
    return true;
}

BUSTER_GLOBAL_LOCAL bool x64_emit_vector_comparison(
    X64Builder* builder,
    IrInstruction* instruction,
    AnalysisType* vector,
    AnalysisType* element)
{
    u8 condition = 0;
    bool ordered = false;
    bool unordered = false;
    if (!x64_vector_comparison_condition(
            instruction->binary_operation,
            &condition,
            &ordered,
            &unordered))
    {
        return false;
    }
    u32 width = element->kind == ANALYSIS_TYPE_FLOAT ?
        element->as.float_bit_width :
        element->kind == ANALYSIS_TYPE_INTEGER ?
            element->as.integer.bit_width : 0;
    u32 lane_size = width / 8;
    if (!lane_size)
    {
        return false;
    }
    x64_emit_initialize_aggregate_result(
        builder,
        instruction->result);
    x64_emit_load(
        builder,
        X64_REGISTER_R8,
        x64_value_displacement_component(
            instruction->operands[0],
            0));
    x64_emit_load(
        builder,
        X64_REGISTER_R9,
        x64_value_displacement_component(
            instruction->operands[1],
            0));
    x64_emit_load(
        builder,
        X64_REGISTER_R10,
        x64_value_displacement_component(
            instruction->result,
            0));
    for (u32 lane = 0;
        lane < vector->as.vector.count;
        lane += 1)
    {
        u32 offset = lane * lane_size;
        if (element->kind == ANALYSIS_TYPE_FLOAT)
        {
            x64_emit_load_float_bits(
                builder,
                0,
                X64_REGISTER_R8,
                offset,
                lane_size);
            x64_emit_load_float_bits(
                builder,
                1,
                X64_REGISTER_R9,
                offset,
                lane_size);
            if (width == 64)
            {
                codegen_emit_u8(
                    &builder->buffer,
                    0x66);
            }
            codegen_emit_u8(&builder->buffer, 0x0f);
            codegen_emit_u8(&builder->buffer, 0x2e);
            codegen_emit_u8(&builder->buffer, 0xc1);
        }
        else
        {
            x64_emit_load_memory(
                builder,
                X64_REGISTER_RAX,
                X64_REGISTER_R8,
                offset,
                lane_size);
            x64_emit_load_memory(
                builder,
                X64_REGISTER_RCX,
                X64_REGISTER_R9,
                offset,
                lane_size);
            if (lane_size == 2)
            {
                codegen_emit_u8(
                    &builder->buffer,
                    0x66);
            }
            if (lane_size == 8)
            {
                codegen_emit_u8(
                    &builder->buffer,
                    0x48);
            }
            codegen_emit_u8(
                &builder->buffer,
                lane_size == 1 ? 0x38 : 0x39);
            codegen_emit_u8(&builder->buffer, 0xc8);
        }
        codegen_emit_u8(&builder->buffer, 0x0f);
        codegen_emit_u8(&builder->buffer, condition);
        codegen_emit_u8(&builder->buffer, 0xc0);
        if (ordered || unordered)
        {
            codegen_emit_u8(&builder->buffer, 0x0f);
            codegen_emit_u8(
                &builder->buffer,
                unordered ? 0x9a : 0x9b);
            codegen_emit_u8(&builder->buffer, 0xc2);
            codegen_emit_u8(
                &builder->buffer,
                unordered ? 0x08 : 0x20);
            codegen_emit_u8(&builder->buffer, 0xd0);
        }
        codegen_emit_u8(&builder->buffer, 0x0f);
        codegen_emit_u8(&builder->buffer, 0xb6);
        codegen_emit_u8(&builder->buffer, 0xc0);
        codegen_emit_u8(&builder->buffer, 0x48);
        codegen_emit_u8(&builder->buffer, 0xf7);
        codegen_emit_u8(&builder->buffer, 0xd8);
        x64_emit_store_memory(
            builder,
            X64_REGISTER_R10,
            offset,
            X64_REGISTER_RAX,
            lane_size);
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool x64_emit_vector_binary(
    X64Builder* builder,
    IrInstruction* instruction)
{
    AnalysisType* vector = analysis_type_from_id(
        builder->analysis,
        builder->function->values[
            instruction->operands[0].value].type);
    if (vector->kind != ANALYSIS_TYPE_VECTOR)
    {
        return false;
    }
    AnalysisType* element = analysis_type_from_id(
        builder->analysis,
        vector->as.vector.element_type);
    if (instruction->binary_operation >=
            IR_BINARY_VECTOR_INTEGER_EQUAL &&
        instruction->binary_operation <=
            IR_BINARY_VECTOR_FLOAT_GREATER_EQUAL)
    {
        return x64_emit_vector_comparison(
            builder,
            instruction,
            vector,
            element);
    }
    u32 width = element->kind == ANALYSIS_TYPE_FLOAT ?
        element->as.float_bit_width :
        element->kind == ANALYSIS_TYPE_INTEGER ?
            element->as.integer.bit_width : 0;
    if (!width || vector->layout.size > 64)
    {
        return false;
    }
    u8 opcode = 0;
    u8 prefix = 0;
    switch (instruction->binary_operation)
    {
        case IR_BINARY_VECTOR_FLOAT_ADD: opcode = 0x58; break;
        case IR_BINARY_VECTOR_FLOAT_SUBTRACT: opcode = 0x5c; break;
        case IR_BINARY_VECTOR_FLOAT_MULTIPLY: opcode = 0x59; break;
        case IR_BINARY_VECTOR_FLOAT_DIVIDE: opcode = 0x5e; break;
        case IR_BINARY_VECTOR_INTEGER_ADD:
        {
            prefix = 0x66;
            opcode = width == 8 ? 0xfc :
                width == 16 ? 0xfd :
                width == 32 ? 0xfe :
                width == 64 ? 0xd4 : 0;
        } break;
        case IR_BINARY_VECTOR_INTEGER_SUBTRACT:
        {
            prefix = 0x66;
            opcode = width == 8 ? 0xf8 :
                width == 16 ? 0xf9 :
                width == 32 ? 0xfa :
                width == 64 ? 0xfb : 0;
        } break;
        case IR_BINARY_VECTOR_INTEGER_BITWISE_AND:
            prefix = 0x66;
            opcode = 0xdb;
            break;
        case IR_BINARY_VECTOR_INTEGER_BITWISE_OR:
            prefix = 0x66;
            opcode = 0xeb;
            break;
        case IR_BINARY_VECTOR_INTEGER_BITWISE_XOR:
            prefix = 0x66;
            opcode = 0xef;
            break;
        default: return false;
    }
    if (!opcode)
    {
        return false;
    }
    if (element->kind == ANALYSIS_TYPE_FLOAT &&
        width == 64)
    {
        prefix = 0x66;
    }
    x64_emit_initialize_aggregate_result(
        builder,
        instruction->result);
    x64_emit_load(
        builder,
        X64_REGISTER_RAX,
        x64_value_displacement_component(
            instruction->operands[0],
            0));
    x64_emit_load(
        builder,
        X64_REGISTER_RCX,
        x64_value_displacement_component(
            instruction->operands[1],
            0));
    x64_emit_load(
        builder,
        X64_REGISTER_RDX,
        x64_value_displacement_component(
            instruction->result,
            0));
    bool native_width =
        vector->layout.size > 16 &&
        vector->layout.size <=
            target_vector_register_size(builder->target);
    bool integer_operation =
        element->kind == ANALYSIS_TYPE_INTEGER;
    if (native_width && integer_operation &&
        vector->layout.size == 32 &&
        !target_cpu_feature_has(
            builder->target,
            TARGET_CPU_FEATURE_X86_AVX2))
    {
        native_width = false;
    }
    if (native_width)
    {
        builder->native_vector_operation_count += 1;
        x64_emit_vector_native_memory(
            builder,
            false,
            (u32)vector->layout.size,
            X64_REGISTER_RAX);
        x64_emit_vector_native_binary_operation(
            builder,
            prefix,
            opcode,
            (u32)vector->layout.size,
            X64_REGISTER_RCX);
        x64_emit_vector_native_memory(
            builder,
            true,
            (u32)vector->layout.size,
            X64_REGISTER_RDX);
        return true;
    }
    u32 chunk_count = (u32)((vector->layout.size + 15) / 16);
    if (vector->layout.size > 16)
    {
        builder->split_vector_operation_count += 1;
    }
    for (u32 chunk = 0; chunk < chunk_count; chunk += 1)
    {
        u32 offset = chunk * 16;
        u32 target_register = 0;
        if (vector->layout.size <= 16 &&
            builder->vector_registers[
                instruction->result.value] !=
                    CODEGEN_REGISTER_UNALLOCATED)
        {
            target_register = 3 +
                builder->vector_registers[
                    instruction->result.value];
        }
        u8 load_prefix =
            vector->layout.size - offset < 16 ?
                0xf3 : 0;
        u8 load_opcode =
            vector->layout.size - offset < 16 ?
                0x7e : 0x10;
        x64_emit_vector_memory_operation(
            builder,
            load_prefix,
            load_opcode,
            target_register,
            X64_REGISTER_RAX);
        x64_emit_vector_memory_operation(
            builder,
            prefix,
            opcode,
            target_register,
            X64_REGISTER_RCX);
        u8 store_prefix =
            vector->layout.size - offset < 16 ?
                0x66 : 0;
        u8 store_opcode =
            vector->layout.size - offset < 16 ?
                0xd6 : 0x11;
        x64_emit_vector_memory_operation(
            builder,
            store_prefix,
            store_opcode,
            target_register,
            X64_REGISTER_RDX);
        if (chunk + 1 < chunk_count)
        {
            codegen_emit_u8(&builder->buffer, 0x48);
            codegen_emit_u8(&builder->buffer, 0x83);
            codegen_emit_u8(&builder->buffer, 0xc0);
            codegen_emit_u8(&builder->buffer, 16);
            codegen_emit_u8(&builder->buffer, 0x48);
            codegen_emit_u8(&builder->buffer, 0x83);
            codegen_emit_u8(&builder->buffer, 0xc1);
            codegen_emit_u8(&builder->buffer, 16);
            codegen_emit_u8(&builder->buffer, 0x48);
            codegen_emit_u8(&builder->buffer, 0x83);
            codegen_emit_u8(&builder->buffer, 0xc2);
            codegen_emit_u8(&builder->buffer, 16);
        }
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool x64_emit_vector_unary(
    X64Builder* builder,
    IrInstruction* instruction)
{
    AnalysisType* vector = analysis_type_from_id(
        builder->analysis,
        instruction->type);
    AnalysisType* element = vector->kind ==
            ANALYSIS_TYPE_VECTOR ?
        analysis_type_from_id(
            builder->analysis,
            vector->as.vector.element_type) :
        0;
    u32 width = element &&
            element->kind == ANALYSIS_TYPE_FLOAT ?
        element->as.float_bit_width :
        element &&
            element->kind == ANALYSIS_TYPE_INTEGER ?
        element->as.integer.bit_width : 0;
    if (!width || vector->layout.size > 64)
    {
        return false;
    }
    x64_emit_initialize_aggregate_result(
        builder,
        instruction->result);
    x64_emit_load(
        builder,
        X64_REGISTER_RAX,
        x64_value_displacement_component(
            instruction->operands[0],
            0));
    x64_emit_load(
        builder,
        X64_REGISTER_RDX,
        x64_value_displacement_component(
            instruction->result,
            0));
    u32 chunk_count =
        (u32)((vector->layout.size + 15) / 16);
    for (u32 chunk = 0;
        chunk < chunk_count;
        chunk += 1)
    {
        u32 target = 0;
        if (vector->layout.size <= 16 &&
            builder->vector_registers[
                instruction->result.value] !=
                    CODEGEN_REGISTER_UNALLOCATED)
        {
            target = 3 +
                builder->vector_registers[
                    instruction->result.value];
        }
        bool short_chunk =
            vector->layout.size - chunk * 16 < 16;
        x64_emit_vector_memory_operation(
            builder,
            short_chunk ? 0xf3 : 0,
            short_chunk ? 0x7e : 0x10,
            target,
            X64_REGISTER_RAX);
        if (instruction->unary_operation ==
            IR_UNARY_VECTOR_FLOAT_NEGATE)
        {
            if (width != 32 && width != 64)
            {
                return false;
            }
            codegen_emit_u8(&builder->buffer, 0x66);
            codegen_emit_u8(&builder->buffer, 0x0f);
            codegen_emit_u8(&builder->buffer, 0x76);
            codegen_emit_u8(&builder->buffer, 0xc9);
            codegen_emit_u8(&builder->buffer, 0x66);
            codegen_emit_u8(&builder->buffer, 0x0f);
            codegen_emit_u8(
                &builder->buffer,
                width == 32 ? 0x72 : 0x73);
            codegen_emit_u8(&builder->buffer, 0xf1);
            codegen_emit_u8(
                &builder->buffer,
                width == 32 ? 31 : 63);
            if (width == 64)
            {
                codegen_emit_u8(&builder->buffer, 0x66);
            }
            codegen_emit_u8(&builder->buffer, 0x0f);
            codegen_emit_u8(&builder->buffer, 0x57);
            codegen_emit_u8(
                &builder->buffer,
                (u8)(0xc1 | (target << 3)));
        }
        else if (instruction->unary_operation ==
            IR_UNARY_VECTOR_INTEGER_BITWISE_NOT)
        {
            codegen_emit_u8(&builder->buffer, 0x66);
            codegen_emit_u8(&builder->buffer, 0x0f);
            codegen_emit_u8(&builder->buffer, 0x76);
            codegen_emit_u8(&builder->buffer, 0xc9);
            codegen_emit_u8(&builder->buffer, 0x66);
            codegen_emit_u8(&builder->buffer, 0x0f);
            codegen_emit_u8(&builder->buffer, 0xef);
            codegen_emit_u8(
                &builder->buffer,
                (u8)(0xc1 | (target << 3)));
        }
        else if (instruction->unary_operation ==
            IR_UNARY_VECTOR_INTEGER_NEGATE)
        {
            u8 subtract = width == 8 ? 0xf8 :
                width == 16 ? 0xf9 :
                width == 32 ? 0xfa :
                width == 64 ? 0xfb : 0;
            if (!subtract)
            {
                return false;
            }
            codegen_emit_u8(&builder->buffer, 0x66);
            codegen_emit_u8(&builder->buffer, 0x0f);
            codegen_emit_u8(&builder->buffer, 0xef);
            codegen_emit_u8(&builder->buffer, 0xc9);
            codegen_emit_u8(&builder->buffer, 0x66);
            codegen_emit_u8(&builder->buffer, 0x0f);
            codegen_emit_u8(&builder->buffer, subtract);
            codegen_emit_u8(
                &builder->buffer,
                (u8)(0xc8 | target));
            codegen_emit_u8(&builder->buffer, 0x66);
            codegen_emit_u8(&builder->buffer, 0x0f);
            codegen_emit_u8(&builder->buffer, 0x6f);
            codegen_emit_u8(
                &builder->buffer,
                (u8)(0xc1 | (target << 3)));
        }
        else
        {
            return false;
        }
        x64_emit_vector_memory_operation(
            builder,
            short_chunk ? 0x66 : 0,
            short_chunk ? 0xd6 : 0x11,
            target,
            X64_REGISTER_RDX);
        if (chunk + 1 < chunk_count)
        {
            codegen_emit_u8(&builder->buffer, 0x48);
            codegen_emit_u8(&builder->buffer, 0x83);
            codegen_emit_u8(&builder->buffer, 0xc0);
            codegen_emit_u8(&builder->buffer, 16);
            codegen_emit_u8(&builder->buffer, 0x48);
            codegen_emit_u8(&builder->buffer, 0x83);
            codegen_emit_u8(&builder->buffer, 0xc2);
            codegen_emit_u8(&builder->buffer, 16);
        }
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool codegen_binary_is_float(
    IrBinaryOperation operation)
{
    return (operation >= IR_BINARY_FLOAT_ADD &&
            operation <= IR_BINARY_FLOAT_DIVIDE) ||
        operation == IR_BINARY_FLOAT_EQUAL ||
        operation == IR_BINARY_FLOAT_NOT_EQUAL ||
        (operation >= IR_BINARY_FLOAT_LESS &&
            operation <= IR_BINARY_FLOAT_GREATER_EQUAL);
}

BUSTER_GLOBAL_LOCAL bool x64_emit_instruction(
    X64Builder* builder,
    IrBlockId block,
    IrInstruction* instruction)
{
    switch (instruction->opcode)
    {
        case IR_OPCODE_LOCAL:
        {
            if (instruction->local.value >=
                builder->function->local_count)
            {
                return false;
            }
            codegen_emit_u8(&builder->buffer, 0x48);
            codegen_emit_u8(&builder->buffer, 0x8d);
            codegen_emit_u8(&builder->buffer, 0x85);
            codegen_emit_u32(
                &builder->buffer,
                (u32)x64_local_storage_displacement(
                    builder,
                    instruction->local));
            x64_emit_store_result(builder, instruction);
        } break;
        case IR_OPCODE_LOAD:
        {
            AnalysisType* type = analysis_type_from_id(
                builder->analysis,
                instruction->type);
            x64_emit_load_value(
                builder,
                X64_REGISTER_RAX,
                instruction->operands[0]);
            if (codegen_type_is_indirect_value(type))
            {
                x64_emit_store_result(builder, instruction);
                x64_emit_store(
                    builder,
                    X64_REGISTER_RAX,
                    x64_value_displacement_component(
                        instruction->result,
                        0));
            }
            else if (codegen_type_is_inline_collection(type))
            {
                for (u32 component = 0;
                    component < X64_VALUE_SLOT_COMPONENT_COUNT;
                    component += 1)
                {
                    codegen_emit_u8(&builder->buffer, 0x48);
                    codegen_emit_u8(&builder->buffer, 0x8b);
                    codegen_emit_u8(&builder->buffer, 0x90);
                    codegen_emit_u32(
                        &builder->buffer,
                        component * 8);
                    x64_emit_store(
                        builder,
                        X64_REGISTER_RDX,
                        x64_value_displacement_component(
                            instruction->result,
                            component));
                }
            }
            else
            {
                x64_emit_load_memory_rax(
                    builder,
                    codegen_type_storage_size(type));
                x64_emit_store_result(builder, instruction);
            }
        } break;
        case IR_OPCODE_STORE:
        {
            AnalysisType* type = analysis_type_from_id(
                builder->analysis,
                builder->function->values[
                    instruction->operands[1].value].type);
            x64_emit_load_value(
                builder,
                X64_REGISTER_RAX,
                instruction->operands[0]);
            if (codegen_type_is_indirect_value(type))
            {
                x64_emit_load(
                    builder,
                    X64_REGISTER_RCX,
                    x64_value_displacement_component(
                        instruction->operands[1],
                        0));
                x64_emit_copy_memory(
                    builder,
                    codegen_type_storage_size(type));
            }
            else if (codegen_type_is_inline_collection(type))
            {
                for (u32 component = 0;
                    component < X64_VALUE_SLOT_COMPONENT_COUNT;
                    component += 1)
                {
                    x64_emit_load(
                        builder,
                        X64_REGISTER_RDX,
                        x64_value_displacement_component(
                            instruction->operands[1],
                            component));
                    codegen_emit_u8(&builder->buffer, 0x48);
                    codegen_emit_u8(&builder->buffer, 0x89);
                    codegen_emit_u8(&builder->buffer, 0x90);
                    codegen_emit_u32(
                        &builder->buffer,
                        component * 8);
                }
            }
            else
            {
                x64_emit_load_value(
                    builder,
                    X64_REGISTER_RCX,
                    instruction->operands[1]);
                x64_emit_store_memory_rcx(
                    builder,
                    codegen_type_storage_size(type));
            }
        } break;
        case IR_OPCODE_ARGUMENT:
        {
            if (!instruction->immediate_count)
            {
                return false;
            }
            u32 argument = (u32)instruction->immediates[0];
            AnalysisType* argument_type = analysis_type_from_id(
                builder->analysis,
                instruction->type);
            CodegenAbiSignature signature =
                codegen_classify_signature_for_target(
                    builder->arena,
                    builder->analysis,
                    builder->function->type,
                    builder->target);
            if (argument >= signature.argument_count)
            {
                return false;
            }
            CodegenAbiLocation* location =
                signature.arguments + argument;
            if (codegen_type_is_indirect_value(
                    argument_type) ||
                codegen_type_is_inline_collection(
                    argument_type))
            {
                if (codegen_type_is_indirect_value(
                        argument_type))
                {
                    x64_emit_initialize_aggregate_result(
                        builder,
                        instruction->result);
                }
                if (location->indirect)
                {
                    CodegenAbiPart* part =
                        location->parts;
                    if (part->kind ==
                        CODEGEN_ABI_LOCATION_STACK)
                    {
                        x64_emit_load(
                            builder,
                            X64_REGISTER_RCX,
                            (s32)part->stack_offset + 16);
                    }
                    else
                    {
                        x64_emit_move_register(
                            builder,
                            X64_REGISTER_RCX,
                            x64_abi_integer_argument_register(
                                builder->abi,
                                part->index));
                    }
                    x64_emit_load(
                        builder,
                        X64_REGISTER_RAX,
                        x64_value_displacement_component(
                            instruction->result,
                            0));
                    x64_emit_copy_memory(
                        builder,
                        codegen_type_storage_size(
                            argument_type));
                    break;
                }
                if (codegen_type_is_indirect_value(
                        argument_type) &&
                    location->part_count &&
                    location->parts[0].kind ==
                        CODEGEN_ABI_LOCATION_STACK)
                {
                    x64_emit_load(
                        builder,
                        X64_REGISTER_RAX,
                        x64_value_displacement_component(
                            instruction->result,
                            0));
                    x64_emit_address(
                        builder,
                        X64_REGISTER_RCX,
                        (s32)location->parts[0]
                            .stack_offset + 16);
                    x64_emit_copy_memory(
                        builder,
                        codegen_type_storage_size(
                            argument_type));
                    break;
                }
                for (u32 part_index = 0;
                    part_index < location->part_count;
                    part_index += 1)
                {
                    CodegenAbiPart* part =
                        location->parts + part_index;
                    if (part->kind ==
                        CODEGEN_ABI_LOCATION_STACK)
                    {
                        x64_emit_load(
                            builder,
                            X64_REGISTER_RDX,
                            (s32)part->stack_offset + 16);
                    }
                    else if (part->kind ==
                        CODEGEN_ABI_LOCATION_INTEGER_REGISTER)
                    {
                        x64_emit_move_register(
                            builder,
                            X64_REGISTER_RDX,
                            x64_abi_integer_argument_register(
                                builder->abi,
                                part->index));
                    }
                    else
                    {
                        x64_emit_store_abi_part(
                            builder,
                            instruction->result,
                            argument_type,
                            part,
                            X64_REGISTER_RDX,
                            part->index);
                        continue;
                    }
                    x64_emit_store_abi_part(
                        builder,
                        instruction->result,
                        argument_type,
                        part,
                        X64_REGISTER_RDX,
                        0);
                }
                break;
            }
            if (location->part_count != 1)
            {
                return false;
            }
            if (location->kind ==
                CODEGEN_ABI_LOCATION_FLOAT_REGISTER)
            {
                x64_emit_float_store(
                    builder,
                    location->index,
                    x64_value_displacement(instruction->result),
                    argument_type->as.float_bit_width);
                break;
            }
            if (location->kind ==
                CODEGEN_ABI_LOCATION_STACK)
            {
                s32 displacement =
                    (s32)location->stack_offset + 16;
                if (argument_type->kind ==
                    ANALYSIS_TYPE_FLOAT)
                {
                    x64_emit_float_load(
                        builder,
                        0,
                        displacement,
                        argument_type->as.float_bit_width);
                    x64_emit_float_store(
                        builder,
                        0,
                        x64_value_displacement(
                            instruction->result),
                        argument_type->as.float_bit_width);
                }
                else
                {
                    x64_emit_load(
                        builder,
                        X64_REGISTER_RAX,
                        displacement);
                    x64_emit_store_result(
                        builder,
                        instruction);
                }
                break;
            }
            if (location->kind !=
                    CODEGEN_ABI_LOCATION_INTEGER_REGISTER &&
                location->kind !=
                    CODEGEN_ABI_LOCATION_INDIRECT)
            {
                return false;
            }
            X64Register source =
                x64_abi_integer_argument_register(
                    builder->abi,
                    location->index);
            x64_emit_move_register(
                builder,
                X64_REGISTER_RAX,
                source);
            x64_emit_store_result(builder, instruction);
        } break;
        case IR_OPCODE_CONSTANT_INTEGER:
        case IR_OPCODE_ENUM:
        {
            if (!instruction->immediate_count)
            {
                return false;
            }
            u64 value = instruction->immediates[0];
            if (instruction->immediate_is_negative)
            {
                value = 0 - value;
            }
            codegen_emit_u8(&builder->buffer, 0x48);
            codegen_emit_u8(&builder->buffer, 0xb8);
            codegen_emit_u64(&builder->buffer, value);
            x64_emit_store_result(builder, instruction);
        } break;
        case IR_OPCODE_CONSTANT_FLOAT:
        {
            if (!instruction->immediate_count)
            {
                return false;
            }
            codegen_emit_u8(&builder->buffer, 0x48);
            codegen_emit_u8(&builder->buffer, 0xb8);
            codegen_emit_u64(
                &builder->buffer,
                instruction->immediates[0]);
            x64_emit_store_result(builder, instruction);
        } break;
        case IR_OPCODE_UNDEFINED:
        {
            codegen_emit_u8(&builder->buffer, 0x31);
            codegen_emit_u8(&builder->buffer, 0xc0);
            x64_emit_store_result(builder, instruction);
        } break;
        case IR_OPCODE_UNARY:
        {
            AnalysisType* unary_type =
                analysis_type_from_id(
                    builder->analysis,
                    instruction->type);
            if (unary_type->kind ==
                ANALYSIS_TYPE_VECTOR)
            {
                return x64_emit_vector_unary(
                    builder,
                    instruction);
            }
            x64_emit_load_value(
                builder,
                X64_REGISTER_RAX,
                instruction->operands[0]);
            if (instruction->unary_operation ==
                IR_UNARY_FLOAT_NEGATE)
            {
                AnalysisType* type = analysis_type_from_id(
                    builder->analysis,
                    instruction->type);
                u64 sign = type->as.float_bit_width == 32 ?
                    ((u64)1 << 31) :
                    ((u64)1 << 63);
                codegen_emit_u8(&builder->buffer, 0x48);
                codegen_emit_u8(&builder->buffer, 0xb9);
                codegen_emit_u64(&builder->buffer, sign);
                codegen_emit_u8(&builder->buffer, 0x48);
                codegen_emit_u8(&builder->buffer, 0x31);
                codegen_emit_u8(&builder->buffer, 0xc8);
            }
            else if (instruction->unary_operation ==
                IR_UNARY_INTEGER_NEGATE)
            {
                codegen_emit_u8(&builder->buffer, 0x48);
                codegen_emit_u8(&builder->buffer, 0xf7);
                codegen_emit_u8(&builder->buffer, 0xd8);
            }
            else if (instruction->unary_operation ==
                IR_UNARY_INTEGER_BITWISE_NOT)
            {
                codegen_emit_u8(&builder->buffer, 0x48);
                codegen_emit_u8(&builder->buffer, 0xf7);
                codegen_emit_u8(&builder->buffer, 0xd0);
            }
            else if (instruction->unary_operation ==
                IR_UNARY_BOOLEAN_NOT)
            {
                codegen_emit_u8(&builder->buffer, 0x48);
                codegen_emit_u8(&builder->buffer, 0x85);
                codegen_emit_u8(&builder->buffer, 0xc0);
                x64_emit_set_condition(builder, 0x94);
            }
            else
            {
                return false;
            }
            x64_emit_store_result(builder, instruction);
        } break;
        case IR_OPCODE_BINARY:
        {
            AnalysisType* result_type = analysis_type_from_id(
                builder->analysis,
                instruction->type);
            if (result_type->kind == ANALYSIS_TYPE_VECTOR)
            {
                return x64_emit_vector_binary(
                    builder,
                    instruction);
            }
            if (codegen_binary_is_float(
                    instruction->binary_operation))
            {
                return x64_emit_float_binary(
                    builder,
                    instruction);
            }
            return x64_emit_integer_binary(builder, instruction);
        }
        case IR_OPCODE_FUNCTION:
        {
            codegen_emit_u8(&builder->buffer, 0x31);
            codegen_emit_u8(&builder->buffer, 0xc0);
            x64_emit_store_result(builder, instruction);
        } break;
        case IR_OPCODE_CALL:
        {
            if (!instruction->operand_count)
            {
                return false;
            }
            AnalysisTypeId function_type_id =
                builder->function->values[
                    instruction->operands[0].value].type;
            AnalysisType* function_type =
                analysis_type_from_id(
                    builder->analysis,
                    function_type_id);
            u32 call_argument_count =
                instruction->operand_count - 1;
            AnalysisTypeId* call_argument_types =
                arena_allocate(
                    builder->arena,
                    AnalysisTypeId,
                    call_argument_count);
            for (u32 index = 0;
                index < call_argument_count;
                index += 1)
            {
                call_argument_types[index] =
                    builder->function->values[
                        instruction->operands[index + 1].value]
                        .type;
            }
            CodegenAbiSignature signature =
                codegen_classify_signature_with_arguments(
                    builder->arena,
                    builder->analysis,
                    function_type_id,
                    function_type->as.function.is_variadic ?
                        call_argument_types : 0,
                    call_argument_count,
                    builder->target);
            if (!signature.valid)
            {
                return false;
            }
            if (signature.argument_count !=
                instruction->operand_count - 1)
            {
                return false;
            }
            X64Register system_v_registers[] = {
                X64_REGISTER_RDI,
                X64_REGISTER_RSI,
                X64_REGISTER_RDX,
                X64_REGISTER_RCX,
                X64_REGISTER_R8,
                X64_REGISTER_R9,
            };
            X64Register windows_registers[] = {
                X64_REGISTER_RCX,
                X64_REGISTER_RDX,
                X64_REGISTER_R8,
                X64_REGISTER_R9,
            };
            AnalysisType* call_result_type =
                instruction->result.value !=
                        IR_ID_UNDERLYING_INVALID ?
                    analysis_type_from_id(
                        builder->analysis,
                        instruction->type) :
                    0;
            if (call_result_type &&
                (codegen_type_is_indirect_value(
                    call_result_type) ||
                 codegen_type_is_inline_collection(
                    call_result_type)))
            {
                if (codegen_type_is_indirect_value(
                        call_result_type))
                {
                    x64_emit_initialize_aggregate_result(
                        builder,
                        instruction->result);
                }
                if (signature.result.indirect)
                {
                    X64Register hidden_register =
                        x64_abi_integer_argument_register(
                            builder->abi,
                            signature
                                .indirect_result_register);
                    if (codegen_type_is_indirect_value(
                            call_result_type))
                    {
                        x64_emit_load(
                            builder,
                            hidden_register,
                            x64_value_displacement_component(
                                instruction->result,
                                0));
                    }
                }
            }
            x64_emit_stack_adjust(
                builder,
                signature.stack_size,
                true);
            for (u32 index = 0;
                index < signature.argument_count;
                index += 1)
            {
                CodegenAbiLocation* location =
                    signature.arguments + index;
                AnalysisType* operand_type =
                    analysis_type_from_id(
                        builder->analysis,
                        builder->function->values[
                            instruction->operands[
                                index + 1].value].type);
                IrValueId operand =
                    instruction->operands[index + 1];
                if (location->indirect)
                {
                    x64_emit_rsp_address(
                        builder,
                        X64_REGISTER_RAX,
                        location->indirect_copy_offset);
                    x64_emit_load(
                        builder,
                        X64_REGISTER_RCX,
                        x64_value_displacement_component(
                            operand,
                            0));
                    x64_emit_copy_memory(
                        builder,
                        codegen_type_storage_size(
                            operand_type));
                    CodegenAbiPart* part =
                        location->parts;
                    if (part->kind ==
                        CODEGEN_ABI_LOCATION_STACK)
                    {
                        x64_emit_store_rsp(
                            builder,
                            X64_REGISTER_RAX,
                            part->stack_offset);
                    }
                    else
                    {
                        x64_emit_move_register(
                            builder,
                            x64_abi_integer_argument_register(
                                builder->abi,
                                part->index),
                            X64_REGISTER_RAX);
                    }
                    continue;
                }
                if (codegen_type_is_indirect_value(
                        operand_type) &&
                    location->part_count &&
                    location->parts[0].kind ==
                        CODEGEN_ABI_LOCATION_STACK)
                {
                    x64_emit_rsp_address(
                        builder,
                        X64_REGISTER_RAX,
                        location->parts[0].stack_offset);
                    x64_emit_load(
                        builder,
                        X64_REGISTER_RCX,
                        x64_value_displacement_component(
                            operand,
                            0));
                    x64_emit_copy_memory(
                        builder,
                        codegen_type_storage_size(
                            operand_type));
                    continue;
                }
                if (location->part_count != 1 &&
                    !codegen_type_is_indirect_value(
                        operand_type) &&
                    !codegen_type_is_inline_collection(
                        operand_type))
                {
                    return false;
                }
                if (codegen_type_is_indirect_value(
                        operand_type) ||
                    codegen_type_is_inline_collection(
                        operand_type))
                {
                    for (u32 part_index = 0;
                        part_index < location->part_count;
                        part_index += 1)
                    {
                        CodegenAbiPart* part =
                            location->parts + part_index;
                        if (part->kind ==
                            CODEGEN_ABI_LOCATION_FLOAT_REGISTER)
                        {
                            x64_emit_load_abi_part(
                                builder,
                                operand,
                                operand_type,
                                part,
                                X64_REGISTER_RAX,
                                part->index);
                        }
                        else
                        {
                            x64_emit_load_abi_part(
                                builder,
                                operand,
                                operand_type,
                                part,
                                X64_REGISTER_RAX,
                                0);
                            if (part->kind ==
                                CODEGEN_ABI_LOCATION_STACK)
                            {
                                x64_emit_store_rsp(
                                    builder,
                                    X64_REGISTER_RAX,
                                    part->stack_offset);
                            }
                            else
                            {
                                x64_emit_move_register(
                                    builder,
                                    x64_abi_integer_argument_register(
                                        builder->abi,
                                        part->index),
                                    X64_REGISTER_RAX);
                            }
                        }
                    }
                    continue;
                }
                if (location->kind ==
                    CODEGEN_ABI_LOCATION_STACK)
                {
                    if (operand_type->kind ==
                        ANALYSIS_TYPE_FLOAT)
                    {
                        x64_emit_float_load(
                            builder,
                            0,
                            x64_value_displacement(operand),
                            operand_type->as.float_bit_width);
                        x64_emit_float_store_rsp(
                            builder,
                            0,
                            location->stack_offset,
                            operand_type->as.float_bit_width);
                    }
                    else
                    {
                        x64_emit_load_value(
                            builder,
                            X64_REGISTER_RAX,
                            operand);
                        x64_emit_store_rsp(
                            builder,
                            X64_REGISTER_RAX,
                            location->stack_offset);
                    }
                }
                else if (location->kind ==
                    CODEGEN_ABI_LOCATION_FLOAT_REGISTER)
                {
                    x64_emit_float_load(
                        builder,
                        location->index,
                        x64_value_displacement(operand),
                        operand_type->as.float_bit_width);
                    if (builder->abi ==
                            CODEGEN_ABI_X86_64_WINDOWS &&
                        function_type->as.function.is_variadic)
                    {
                        x64_emit_load_value(
                            builder,
                            windows_registers[location->index],
                            operand);
                    }
                }
                else
                {
                    X64Register* registers =
                        builder->abi ==
                            CODEGEN_ABI_X86_64_WINDOWS ?
                            windows_registers :
                            system_v_registers;
                    u32 register_count =
                        builder->abi ==
                            CODEGEN_ABI_X86_64_WINDOWS ?
                            BUSTER_ARRAY_LENGTH(
                                windows_registers) :
                            BUSTER_ARRAY_LENGTH(
                                system_v_registers);
                    if (location->index >= register_count)
                    {
                        return false;
                    }
                    x64_emit_load_value(
                        builder,
                        registers[location->index],
                        operand);
                }
            }
            if (builder->abi ==
                    CODEGEN_ABI_X86_64_SYSTEM_V &&
                function_type->as.function.is_variadic)
            {
                u32 vector_register_count = 0;
                for (u32 index = 0;
                    index < signature.argument_count;
                    index += 1)
                {
                    CodegenAbiLocation* location =
                        signature.arguments + index;
                    for (u32 part = 0;
                        part < location->part_count;
                        part += 1)
                    {
                        if (location->parts[part].kind ==
                            CODEGEN_ABI_LOCATION_FLOAT_REGISTER)
                        {
                            vector_register_count = BUSTER_MAX(
                                vector_register_count,
                                location->parts[part].index + 1);
                        }
                    }
                }
                codegen_emit_u8(&builder->buffer, 0xb8);
                codegen_emit_u32(
                    &builder->buffer,
                    vector_register_count);
            }
            codegen_emit_u8(&builder->buffer, 0xe8);
            x64_call_relocation_add(builder, instruction);
            x64_emit_stack_adjust(
                builder,
                signature.stack_size,
                false);
            if (instruction->result.value !=
                IR_ID_UNDERLYING_INVALID)
            {
                AnalysisType* result_type =
                    analysis_type_from_id(
                        builder->analysis,
                        instruction->type);
                if (signature.result.indirect)
                {
                    if (builder->abi ==
                        CODEGEN_ABI_X86_64_WINDOWS)
                    {
                        x64_emit_load(
                            builder,
                            X64_REGISTER_RAX,
                            x64_value_displacement_component(
                                instruction->result,
                                0));
                    }
                }
                else if (codegen_type_is_indirect_value(
                            result_type) ||
                         codegen_type_is_inline_collection(
                            result_type))
                {
                    X64Register integer_results[] = {
                        X64_REGISTER_RAX,
                        X64_REGISTER_RDX,
                    };
                    for (u32 part_index = 0;
                        part_index <
                            signature.result.part_count;
                        part_index += 1)
                    {
                        CodegenAbiPart* part =
                            signature.result.parts +
                            part_index;
                        x64_emit_store_abi_part(
                            builder,
                            instruction->result,
                            result_type,
                            part,
                            integer_results[part->index],
                            part->index);
                    }
                }
                else if (result_type->kind ==
                    ANALYSIS_TYPE_FLOAT)
                {
                    x64_emit_float_store(
                        builder,
                        0,
                        x64_value_displacement(
                            instruction->result),
                        result_type->as.float_bit_width);
                }
                else if (!codegen_type_is_indirect_value(
                        result_type))
                {
                    x64_emit_store_result(
                        builder,
                        instruction);
                }
                else
                {
                    return false;
                }
            }
        } break;
        case IR_OPCODE_CAST:
        {
            AnalysisType* source = analysis_type_from_id(
                builder->analysis,
                builder->function->values[
                    instruction->operands[0].value].type);
            AnalysisType* target = analysis_type_from_id(
                builder->analysis,
                instruction->type);
            u32 source_width = source->kind == ANALYSIS_TYPE_INTEGER ?
                source->as.integer.bit_width : 64;
            u32 target_width = target->kind == ANALYSIS_TYPE_INTEGER ?
                target->as.integer.bit_width : 64;
            if (instruction->conversion_operation ==
                    IR_CONVERSION_FLOAT_EXTEND ||
                instruction->conversion_operation ==
                    IR_CONVERSION_FLOAT_TRUNCATE)
            {
                x64_emit_float_load(
                    builder,
                    0,
                    x64_value_displacement(
                        instruction->operands[0]),
                    source->as.float_bit_width);
                codegen_emit_u8(
                    &builder->buffer,
                    instruction->conversion_operation ==
                        IR_CONVERSION_FLOAT_EXTEND ?
                        0xf3 : 0xf2);
                codegen_emit_u8(&builder->buffer, 0x0f);
                codegen_emit_u8(&builder->buffer, 0x5a);
                codegen_emit_u8(&builder->buffer, 0xc0);
                x64_emit_float_store(
                    builder,
                    0,
                    x64_value_displacement(
                        instruction->result),
                    target->as.float_bit_width);
                break;
            }
            if (instruction->conversion_operation ==
                    IR_CONVERSION_SIGNED_INTEGER_TO_FLOAT ||
                instruction->conversion_operation ==
                    IR_CONVERSION_UNSIGNED_INTEGER_TO_FLOAT)
            {
                x64_emit_load_value(
                    builder,
                    X64_REGISTER_RAX,
                    instruction->operands[0]);
                if (instruction->conversion_operation ==
                    IR_CONVERSION_SIGNED_INTEGER_TO_FLOAT)
                {
                    if (source_width == 8)
                    {
                        codegen_emit_u8(
                            &builder->buffer,
                            0x48);
                        codegen_emit_u8(
                            &builder->buffer,
                            0x0f);
                        codegen_emit_u8(
                            &builder->buffer,
                            0xbe);
                        codegen_emit_u8(
                            &builder->buffer,
                            0xc0);
                    }
                    else if (source_width == 16)
                    {
                        codegen_emit_u8(
                            &builder->buffer,
                            0x48);
                        codegen_emit_u8(
                            &builder->buffer,
                            0x0f);
                        codegen_emit_u8(
                            &builder->buffer,
                            0xbf);
                        codegen_emit_u8(
                            &builder->buffer,
                            0xc0);
                    }
                    else if (source_width == 32)
                    {
                        codegen_emit_u8(
                            &builder->buffer,
                            0x48);
                        codegen_emit_u8(
                            &builder->buffer,
                            0x63);
                        codegen_emit_u8(
                            &builder->buffer,
                            0xc0);
                    }
                }
                if (instruction->conversion_operation ==
                        IR_CONVERSION_UNSIGNED_INTEGER_TO_FLOAT &&
                    source_width == 64)
                {
                    return false;
                }
                codegen_emit_u8(
                    &builder->buffer,
                    target->as.float_bit_width == 32 ?
                        0xf3 : 0xf2);
                codegen_emit_u8(&builder->buffer, 0x48);
                codegen_emit_u8(&builder->buffer, 0x0f);
                codegen_emit_u8(&builder->buffer, 0x2a);
                codegen_emit_u8(&builder->buffer, 0xc0);
                x64_emit_float_store(
                    builder,
                    0,
                    x64_value_displacement(
                        instruction->result),
                    target->as.float_bit_width);
                break;
            }
            if (instruction->conversion_operation ==
                    IR_CONVERSION_FLOAT_TO_SIGNED_INTEGER)
            {
                x64_emit_float_load(
                    builder,
                    0,
                    x64_value_displacement(
                        instruction->operands[0]),
                    source->as.float_bit_width);
                codegen_emit_u8(
                    &builder->buffer,
                    source->as.float_bit_width == 32 ?
                        0xf3 : 0xf2);
                codegen_emit_u8(&builder->buffer, 0x48);
                codegen_emit_u8(&builder->buffer, 0x0f);
                codegen_emit_u8(&builder->buffer, 0x2c);
                codegen_emit_u8(&builder->buffer, 0xc0);
                x64_emit_store_result(builder, instruction);
                break;
            }
            if (instruction->conversion_operation ==
                IR_CONVERSION_FLOAT_TO_UNSIGNED_INTEGER)
            {
                return false;
            }
            x64_emit_load_value(
                builder,
                X64_REGISTER_RAX,
                instruction->operands[0]);
            if (instruction->conversion_operation ==
                IR_CONVERSION_INTEGER_SIGN_EXTEND)
            {
                codegen_emit_u8(&builder->buffer, 0x48);
                if (source_width == 8)
                {
                    codegen_emit_u8(&builder->buffer, 0x0f);
                    codegen_emit_u8(&builder->buffer, 0xbe);
                    codegen_emit_u8(&builder->buffer, 0xc0);
                }
                else if (source_width == 16)
                {
                    codegen_emit_u8(&builder->buffer, 0x0f);
                    codegen_emit_u8(&builder->buffer, 0xbf);
                    codegen_emit_u8(&builder->buffer, 0xc0);
                }
                else if (source_width == 32)
                {
                    codegen_emit_u8(&builder->buffer, 0x63);
                    codegen_emit_u8(&builder->buffer, 0xc0);
                }
            }
            else if (instruction->conversion_operation ==
                    IR_CONVERSION_INTEGER_ZERO_EXTEND ||
                instruction->conversion_operation ==
                    IR_CONVERSION_INTEGER_TRUNCATE ||
                instruction->conversion_operation ==
                    IR_CONVERSION_INTEGER_REINTERPRET)
            {
                if (target_width == 8)
                {
                    codegen_emit_u8(&builder->buffer, 0x0f);
                    codegen_emit_u8(&builder->buffer, 0xb6);
                    codegen_emit_u8(&builder->buffer, 0xc0);
                }
                else if (target_width == 16)
                {
                    codegen_emit_u8(&builder->buffer, 0x0f);
                    codegen_emit_u8(&builder->buffer, 0xb7);
                    codegen_emit_u8(&builder->buffer, 0xc0);
                }
                else if (target_width == 32)
                {
                    codegen_emit_u8(&builder->buffer, 0x89);
                    codegen_emit_u8(&builder->buffer, 0xc0);
                }
            }
            else if (instruction->conversion_operation !=
                IR_CONVERSION_IDENTITY)
            {
                return false;
            }
            x64_emit_store_result(builder, instruction);
        } break;
        case IR_OPCODE_ADDRESS_OF:
        case IR_OPCODE_DEREFERENCE:
        {
            x64_emit_load_value(
                builder,
                X64_REGISTER_RAX,
                instruction->operands[0]);
            x64_emit_store_result(builder, instruction);
        } break;
        case IR_OPCODE_ARRAY:
        {
            AnalysisType* type = analysis_type_from_id(
                builder->analysis,
                instruction->type);
            AnalysisTypeId element_type_id =
                type->kind == ANALYSIS_TYPE_ARRAY ?
                    type->as.array.element_type :
                type->kind == ANALYSIS_TYPE_VECTOR ?
                    type->as.vector.element_type :
                    type->as.element_type;
            AnalysisType* element_type = analysis_type_from_id(
                builder->analysis,
                element_type_id);
            u32 element_size =
                codegen_type_storage_size(element_type);
            if (!element_size ||
                !builder->value_storage_offsets[
                    instruction->result.value])
            {
                return false;
            }
            s32 storage = -(s32)builder->value_storage_offsets[
                instruction->result.value];
            x64_emit_address(
                builder,
                X64_REGISTER_RAX,
                storage);
            x64_emit_store(
                builder,
                X64_REGISTER_RAX,
                x64_value_displacement_component(
                    instruction->result,
                    0));
            for (u32 index = 0;
                index < instruction->operand_count;
                index += 1)
            {
                x64_emit_address(
                    builder,
                    X64_REGISTER_RAX,
                    storage + (s32)(index * element_size));
                if (codegen_type_is_indirect_value(element_type))
                {
                    x64_emit_load(
                        builder,
                        X64_REGISTER_RCX,
                        x64_value_displacement_component(
                            instruction->operands[index],
                            0));
                    x64_emit_copy_memory(builder, element_size);
                }
                else
                {
                    x64_emit_load_value(
                        builder,
                        X64_REGISTER_RCX,
                        instruction->operands[index]);
                    x64_emit_store_memory_rcx(
                        builder,
                        element_size);
                }
            }
            codegen_emit_u8(&builder->buffer, 0x48);
            codegen_emit_u8(&builder->buffer, 0xb8);
            codegen_emit_u64(
                &builder->buffer,
                instruction->operand_count);
            x64_emit_store(
                builder,
                X64_REGISTER_RAX,
                x64_value_displacement_component(
                    instruction->result,
                    1));
            codegen_emit_u8(&builder->buffer, 0x48);
            codegen_emit_u8(&builder->buffer, 0xb8);
            codegen_emit_u64(&builder->buffer, element_size);
            x64_emit_store(
                builder,
                X64_REGISTER_RAX,
                x64_value_displacement_component(
                    instruction->result,
                    2));
            codegen_emit_u8(&builder->buffer, 0x31);
            codegen_emit_u8(&builder->buffer, 0xc0);
            x64_emit_store(
                builder,
                X64_REGISTER_RAX,
                x64_value_displacement_component(
                    instruction->result,
                    3));
        } break;
        case IR_OPCODE_AGGREGATE:
        {
            AnalysisType* type = analysis_type_from_id(
                builder->analysis,
                instruction->type);
            AnalysisEntitySemantic* semantic =
                codegen_type_semantic(builder->analysis, type);
            if (!semantic ||
                instruction->immediate_count !=
                    instruction->operand_count ||
                !builder->value_storage_offsets[
                    instruction->result.value])
            {
                return false;
            }
            s32 storage = -(s32)builder->value_storage_offsets[
                instruction->result.value];
            x64_emit_address(
                builder,
                X64_REGISTER_RAX,
                storage);
            x64_emit_store_result(builder, instruction);
            for (u32 index = 0;
                index < instruction->operand_count;
                index += 1)
            {
                u64 field_index = instruction->immediates[index];
                if (field_index >= semantic->field_count)
                {
                    return false;
                }
                AnalysisField* field =
                    semantic->fields + field_index;
                AnalysisType* field_type =
                    analysis_type_from_id(
                        builder->analysis,
                        field->type);
                u32 field_size =
                    codegen_type_storage_size(field_type);
                x64_emit_address(
                    builder,
                    X64_REGISTER_RAX,
                    storage + (s32)field->offset);
                if (codegen_type_is_indirect_value(field_type))
                {
                    x64_emit_load(
                        builder,
                        X64_REGISTER_RCX,
                        x64_value_displacement_component(
                            instruction->operands[index],
                            0));
                    x64_emit_copy_memory(builder, field_size);
                }
                else
                {
                    x64_emit_load_value(
                        builder,
                        X64_REGISTER_RCX,
                        instruction->operands[index]);
                    x64_emit_store_memory_rcx(
                        builder,
                        field_size);
                }
            }
        } break;
        case IR_OPCODE_REVERSE:
        {
            AnalysisType* type = analysis_type_from_id(
                builder->analysis,
                instruction->type);
            u32 reverse_component =
                type->kind == ANALYSIS_TYPE_RANGE ? 2 : 3;
            for (u32 component = 0;
                component < X64_VALUE_SLOT_COMPONENT_COUNT;
                component += 1)
            {
                x64_emit_load(
                    builder,
                    X64_REGISTER_RAX,
                    x64_value_displacement_component(
                        instruction->operands[0],
                        component));
                if (component == reverse_component)
                {
                    codegen_emit_u8(&builder->buffer, 0x48);
                    codegen_emit_u8(&builder->buffer, 0x83);
                    codegen_emit_u8(&builder->buffer, 0xf0);
                    codegen_emit_u8(&builder->buffer, 0x01);
                }
                x64_emit_store(
                    builder,
                    X64_REGISTER_RAX,
                    x64_value_displacement_component(
                        instruction->result,
                        component));
            }
        } break;
        case IR_OPCODE_LENGTH:
        {
            AnalysisType* base = analysis_type_from_id(
                builder->analysis,
                builder->function->values[
                    instruction->operands[0].value].type);
            if (base->kind != ANALYSIS_TYPE_RANGE)
            {
                x64_emit_collection_component(
                    builder,
                    instruction->operands[0],
                    1,
                    X64_REGISTER_RAX);
                x64_emit_store_result(builder, instruction);
                break;
            }
            x64_emit_load(
                builder,
                X64_REGISTER_RAX,
                x64_value_displacement_component(
                    instruction->operands[0],
                    1));
            x64_emit_load(
                builder,
                X64_REGISTER_RCX,
                x64_value_displacement_component(
                    instruction->operands[0],
                    0));
            codegen_emit_u8(&builder->buffer, 0x48);
            codegen_emit_u8(&builder->buffer, 0x29);
            codegen_emit_u8(&builder->buffer, 0xc8);
            x64_emit_store_result(builder, instruction);
        } break;
        case IR_OPCODE_VA_START:
        {
            AnalysisType* function_type = analysis_type_from_id(
                builder->analysis,
                builder->function->type);
            u32 integer_register_count =
                builder->signature.result.indirect ? 1 : 0;
            u32 float_register_count = 0;
            u32 stack_end = 0;
            for (u32 argument = 0;
                argument < function_type->as.function.argument_count;
                argument += 1)
            {
                CodegenAbiLocation* location =
                    builder->signature.arguments + argument;
                for (u32 part = 0;
                    part < location->part_count;
                    part += 1)
                {
                    CodegenAbiPart* abi_part =
                        location->parts + part;
                    if (abi_part->kind ==
                        CODEGEN_ABI_LOCATION_FLOAT_REGISTER)
                    {
                        float_register_count = BUSTER_MAX(
                            float_register_count,
                            abi_part->index + 1);
                    }
                    else if (abi_part->kind ==
                        CODEGEN_ABI_LOCATION_INTEGER_REGISTER)
                    {
                        integer_register_count = BUSTER_MAX(
                            integer_register_count,
                            abi_part->index + 1);
                    }
                    else
                    {
                        stack_end = BUSTER_MAX(
                            stack_end,
                            abi_part->stack_offset +
                                codegen_align_u32(
                                    abi_part->size,
                                    8));
                    }
                }
            }
            if (builder->abi == CODEGEN_ABI_X86_64_SYSTEM_V)
            {
                u64 offsets = (u64)(integer_register_count * 8) |
                    ((u64)(48 + float_register_count * 16) << 32);
                codegen_emit_u8(&builder->buffer, 0x48);
                codegen_emit_u8(&builder->buffer, 0xb8);
                codegen_emit_u64(&builder->buffer, offsets);
                x64_emit_store(
                    builder,
                    X64_REGISTER_RAX,
                    x64_value_displacement_component(
                        instruction->result,
                        0));
                codegen_emit_u8(&builder->buffer, 0x48);
                codegen_emit_u8(&builder->buffer, 0x8d);
                codegen_emit_u8(&builder->buffer, 0x85);
                codegen_emit_u32(
                    &builder->buffer,
                    16 + stack_end);
                x64_emit_store(
                    builder,
                    X64_REGISTER_RAX,
                    x64_value_displacement_component(
                        instruction->result,
                        1));
                codegen_emit_u8(&builder->buffer, 0x48);
                codegen_emit_u8(&builder->buffer, 0x8d);
                codegen_emit_u8(&builder->buffer, 0x85);
                codegen_emit_u32(
                    &builder->buffer,
                    (u32)builder->va_register_save_displacement);
                x64_emit_store(
                    builder,
                    X64_REGISTER_RAX,
                    x64_value_displacement_component(
                        instruction->result,
                        2));
            }
            else
            {
                u32 slot = function_type->as.function.argument_count +
                    (builder->signature.result.indirect ? 1 : 0);
                codegen_emit_u8(&builder->buffer, 0x48);
                codegen_emit_u8(&builder->buffer, 0x8d);
                codegen_emit_u8(&builder->buffer, 0x85);
                codegen_emit_u32(
                    &builder->buffer,
                    16 + slot * 8);
                x64_emit_store(
                    builder,
                    X64_REGISTER_RAX,
                    x64_value_displacement_component(
                        instruction->result,
                        0));
                codegen_emit_u8(&builder->buffer, 0x31);
                codegen_emit_u8(&builder->buffer, 0xc0);
                for (u32 component = 1; component < 3; component += 1)
                {
                    x64_emit_store(
                        builder,
                        X64_REGISTER_RAX,
                        x64_value_displacement_component(
                            instruction->result,
                            component));
                }
            }
            codegen_emit_u8(&builder->buffer, 0x31);
            codegen_emit_u8(&builder->buffer, 0xc0);
            x64_emit_store(
                builder,
                X64_REGISTER_RAX,
                x64_value_displacement_component(
                    instruction->result,
                    3));
        } break;
        case IR_OPCODE_VA_COPY:
        {
            x64_emit_load_value(
                builder,
                X64_REGISTER_RAX,
                instruction->operands[0]);
            for (u32 component = 0; component < 4; component += 1)
            {
                x64_emit_load_memory(
                    builder,
                    X64_REGISTER_RDX,
                    X64_REGISTER_RAX,
                    component * 8,
                    8);
                x64_emit_store(
                    builder,
                    X64_REGISTER_RDX,
                    x64_value_displacement_component(
                        instruction->result,
                        component));
            }
        } break;
        case IR_OPCODE_VA_END:
        {
            x64_emit_load_value(
                builder,
                X64_REGISTER_RAX,
                instruction->operands[0]);
            codegen_emit_u8(&builder->buffer, 0xba);
            codegen_emit_u32(&builder->buffer, 1);
            x64_emit_store_memory(
                builder,
                X64_REGISTER_RAX,
                24,
                X64_REGISTER_RDX,
                8);
        } break;
        case IR_OPCODE_VA_ARG:
        {
            AnalysisType* type =
                analysis_type_from_id(
                    builder->analysis,
                    instruction->type);
            u32 size = codegen_type_storage_size(type);
            bool aggregate = codegen_type_is_indirect_value(type);
            bool scalar =
                type->kind == ANALYSIS_TYPE_INTEGER ||
                type->kind == ANALYSIS_TYPE_FLOAT ||
                type->kind == ANALYSIS_TYPE_BOOL ||
                type->kind == ANALYSIS_TYPE_POINTER ||
                type->kind == ANALYSIS_TYPE_ENUM;
            if (!size ||
                (!aggregate && (!scalar || size > 8)))
            {
                return false;
            }
            AnalysisAbiValue abi_value =
                analysis_abi_value_classify(
                    builder->arena,
                    builder->analysis,
                    instruction->type,
                    codegen_analysis_abi_convention(
                        builder->abi),
                    false);
            if (!abi_value.part_count)
            {
                return false;
            }
            if (aggregate)
            {
                x64_emit_initialize_aggregate_result(
                    builder,
                    instruction->result);
                x64_emit_load_value(
                    builder,
                    X64_REGISTER_RAX,
                    instruction->operands[0]);
                if (builder->abi ==
                    CODEGEN_ABI_X86_64_WINDOWS)
                {
                    x64_emit_load_memory(
                        builder,
                        X64_REGISTER_RDX,
                        X64_REGISTER_RAX,
                        0,
                        8);
                    codegen_emit_u8(&builder->buffer, 0x48);
                    codegen_emit_u8(&builder->buffer, 0x83);
                    codegen_emit_u8(&builder->buffer, 0xc2);
                    codegen_emit_u8(&builder->buffer, 8);
                    x64_emit_store_memory(
                        builder,
                        X64_REGISTER_RAX,
                        0,
                        X64_REGISTER_RDX,
                        8);
                    codegen_emit_u8(&builder->buffer, 0x48);
                    codegen_emit_u8(&builder->buffer, 0x83);
                    codegen_emit_u8(&builder->buffer, 0xea);
                    codegen_emit_u8(&builder->buffer, 8);
                    if (abi_value.indirect)
                    {
                        x64_emit_load_memory(
                            builder,
                            X64_REGISTER_RCX,
                            X64_REGISTER_RDX,
                            0,
                            8);
                    }
                    else
                    {
                        x64_emit_move_register(
                            builder,
                            X64_REGISTER_RCX,
                            X64_REGISTER_RDX);
                    }
                    x64_emit_load(
                        builder,
                        X64_REGISTER_RAX,
                        x64_value_displacement_component(
                            instruction->result,
                            0));
                    x64_emit_copy_memory(builder, size);
                    break;
                }

                u32 integer_parts = 0;
                u32 float_parts = 0;
                bool register_value =
                    abi_value.parts[0].location !=
                        ANALYSIS_ABI_LOCATION_STACK;
                for (u32 part = 0;
                    part < abi_value.part_count;
                    part += 1)
                {
                    if (abi_value.parts[part].abi_class ==
                        ANALYSIS_ABI_CLASS_FLOAT)
                    {
                        float_parts += 1;
                    }
                    else if (abi_value.parts[part].abi_class ==
                        ANALYSIS_ABI_CLASS_INTEGER ||
                        abi_value.parts[part].abi_class ==
                            ANALYSIS_ABI_CLASS_POINTER)
                    {
                        integer_parts += 1;
                    }
                    else
                    {
                        register_value = false;
                    }
                }
                u32 overflow_patches[2] = {0};
                u32 overflow_patch_count = 0;
                if (register_value && integer_parts)
                {
                    x64_emit_load_memory(
                        builder,
                        X64_REGISTER_RCX,
                        X64_REGISTER_RAX,
                        0,
                        4);
                    codegen_emit_u8(&builder->buffer, 0x81);
                    codegen_emit_u8(&builder->buffer, 0xf9);
                    codegen_emit_u32(
                        &builder->buffer,
                        48 - integer_parts * 8);
                    codegen_emit_u8(&builder->buffer, 0x0f);
                    codegen_emit_u8(&builder->buffer, 0x87);
                    overflow_patches[overflow_patch_count++] =
                        (u32)builder->buffer.count;
                    codegen_emit_u32(&builder->buffer, 0);
                }
                if (register_value && float_parts)
                {
                    x64_emit_load_memory(
                        builder,
                        X64_REGISTER_RCX,
                        X64_REGISTER_RAX,
                        4,
                        4);
                    codegen_emit_u8(&builder->buffer, 0x81);
                    codegen_emit_u8(&builder->buffer, 0xf9);
                    codegen_emit_u32(
                        &builder->buffer,
                        176 - float_parts * 16);
                    codegen_emit_u8(&builder->buffer, 0x0f);
                    codegen_emit_u8(&builder->buffer, 0x87);
                    overflow_patches[overflow_patch_count++] =
                        (u32)builder->buffer.count;
                    codegen_emit_u32(&builder->buffer, 0);
                }
                if (register_value)
                {
                    for (u32 part = 0;
                        part < abi_value.part_count;
                        part += 1)
                    {
                        AnalysisAbiPart* abi_part =
                            abi_value.parts + part;
                        u32 offset =
                            abi_part->abi_class ==
                                    ANALYSIS_ABI_CLASS_FLOAT ?
                                4 : 0;
                        u32 increment =
                            abi_part->abi_class ==
                                    ANALYSIS_ABI_CLASS_FLOAT ?
                                16 : 8;
                        x64_emit_load_value(
                            builder,
                            X64_REGISTER_RAX,
                            instruction->operands[0]);
                        x64_emit_load_memory(
                            builder,
                            X64_REGISTER_RCX,
                            X64_REGISTER_RAX,
                            offset,
                            4);
                        x64_emit_load_memory(
                            builder,
                            X64_REGISTER_RDX,
                            X64_REGISTER_RAX,
                            16,
                            8);
                        codegen_emit_u8(
                            &builder->buffer,
                            0x48);
                        codegen_emit_u8(
                            &builder->buffer,
                            0x01);
                        codegen_emit_u8(
                            &builder->buffer,
                            0xca);
                        x64_emit_load_memory(
                            builder,
                            X64_REGISTER_R8,
                            X64_REGISTER_RDX,
                            0,
                            abi_part->size);
                        x64_emit_load(
                            builder,
                            X64_REGISTER_R11,
                            x64_value_displacement_component(
                                instruction->result,
                                0));
                        x64_emit_store_memory(
                            builder,
                            X64_REGISTER_R11,
                            abi_part->value_offset,
                            X64_REGISTER_R8,
                            abi_part->size);
                        codegen_emit_u8(
                            &builder->buffer,
                            0x83);
                        codegen_emit_u8(
                            &builder->buffer,
                            0x80);
                        codegen_emit_u32(
                            &builder->buffer,
                            offset);
                        codegen_emit_u8(
                            &builder->buffer,
                            (u8)increment);
                    }
                    codegen_emit_u8(&builder->buffer, 0xe9);
                }
                u32 end_patch = register_value ?
                    (u32)builder->buffer.count : 0;
                if (register_value)
                {
                    codegen_emit_u32(&builder->buffer, 0);
                }
                u32 overflow_offset =
                    (u32)builder->buffer.count;
                for (u32 patch = 0;
                    patch < overflow_patch_count;
                    patch += 1)
                {
                    s32 displacement = (s32)(
                        overflow_offset -
                        (overflow_patches[patch] + 4));
                    memcpy(
                        builder->buffer.bytes +
                            overflow_patches[patch],
                        &displacement,
                        sizeof(displacement));
                }
                x64_emit_load_value(
                    builder,
                    X64_REGISTER_RAX,
                    instruction->operands[0]);
                x64_emit_load_memory(
                    builder,
                    X64_REGISTER_RDX,
                    X64_REGISTER_RAX,
                    8,
                    8);
                u32 alignment = (u32)BUSTER_MAX(
                    type->layout.alignment,
                    8);
                if (alignment > 8)
                {
                    codegen_emit_u8(&builder->buffer, 0x48);
                    codegen_emit_u8(&builder->buffer, 0x83);
                    codegen_emit_u8(&builder->buffer, 0xc2);
                    codegen_emit_u8(
                        &builder->buffer,
                        (u8)(alignment - 1));
                    codegen_emit_u8(&builder->buffer, 0x48);
                    codegen_emit_u8(&builder->buffer, 0x83);
                    codegen_emit_u8(&builder->buffer, 0xe2);
                    codegen_emit_u8(
                        &builder->buffer,
                        (u8)(0 - alignment));
                }
                x64_emit_move_register(
                    builder,
                    X64_REGISTER_RCX,
                    X64_REGISTER_RDX);
                u32 stack_size =
                    codegen_align_u32(size, 8);
                if (stack_size <= 127)
                {
                    codegen_emit_u8(&builder->buffer, 0x48);
                    codegen_emit_u8(&builder->buffer, 0x83);
                    codegen_emit_u8(&builder->buffer, 0xc2);
                    codegen_emit_u8(
                        &builder->buffer,
                        (u8)stack_size);
                }
                else
                {
                    codegen_emit_u8(&builder->buffer, 0x48);
                    codegen_emit_u8(&builder->buffer, 0x81);
                    codegen_emit_u8(&builder->buffer, 0xc2);
                    codegen_emit_u32(
                        &builder->buffer,
                        stack_size);
                }
                x64_emit_store_memory(
                    builder,
                    X64_REGISTER_RAX,
                    8,
                    X64_REGISTER_RDX,
                    8);
                x64_emit_load(
                    builder,
                    X64_REGISTER_RAX,
                    x64_value_displacement_component(
                        instruction->result,
                        0));
                x64_emit_copy_memory(builder, size);
                if (register_value)
                {
                    u32 end_offset =
                        (u32)builder->buffer.count;
                    s32 displacement = (s32)(
                        end_offset - (end_patch + 4));
                    memcpy(
                        builder->buffer.bytes + end_patch,
                        &displacement,
                        sizeof(displacement));
                }
                break;
            }
            x64_emit_load_value(
                builder,
                X64_REGISTER_RAX,
                instruction->operands[0]);
            if (builder->abi == CODEGEN_ABI_X86_64_WINDOWS)
            {
                x64_emit_load_memory(
                    builder,
                    X64_REGISTER_RDX,
                    X64_REGISTER_RAX,
                    0,
                    8);
                codegen_emit_u8(&builder->buffer, 0x48);
                codegen_emit_u8(&builder->buffer, 0x83);
                codegen_emit_u8(&builder->buffer, 0xc2);
                codegen_emit_u8(&builder->buffer, 8);
                x64_emit_store_memory(
                    builder,
                    X64_REGISTER_RAX,
                    0,
                    X64_REGISTER_RDX,
                    8);
                codegen_emit_u8(&builder->buffer, 0x48);
                codegen_emit_u8(&builder->buffer, 0x83);
                codegen_emit_u8(&builder->buffer, 0xea);
                codegen_emit_u8(&builder->buffer, 8);
            }
            else
            {
                bool floating = type->kind == ANALYSIS_TYPE_FLOAT;
                u32 offset = floating ? 4 : 0;
                u32 limit = floating ? 160 : 40;
                u32 increment = floating ? 16 : 8;
                x64_emit_load_memory(
                    builder,
                    X64_REGISTER_RCX,
                    X64_REGISTER_RAX,
                    offset,
                    4);
                codegen_emit_u8(&builder->buffer, 0x81);
                codegen_emit_u8(&builder->buffer, 0xf9);
                codegen_emit_u32(&builder->buffer, limit);
                codegen_emit_u8(&builder->buffer, 0x0f);
                codegen_emit_u8(&builder->buffer, 0x87);
                u32 overflow_patch = (u32)builder->buffer.count;
                codegen_emit_u32(&builder->buffer, 0);
                x64_emit_load_memory(
                    builder,
                    X64_REGISTER_RDX,
                    X64_REGISTER_RAX,
                    16,
                    8);
                codegen_emit_u8(&builder->buffer, 0x48);
                codegen_emit_u8(&builder->buffer, 0x01);
                codegen_emit_u8(&builder->buffer, 0xca);
                codegen_emit_u8(&builder->buffer, 0x83);
                codegen_emit_u8(&builder->buffer, (u8)(offset ? 0x40 : 0x00));
                if (offset)
                {
                    codegen_emit_u8(&builder->buffer, (u8)offset);
                }
                codegen_emit_u8(&builder->buffer, (u8)increment);
                codegen_emit_u8(&builder->buffer, 0xe9);
                u32 end_patch = (u32)builder->buffer.count;
                codegen_emit_u32(&builder->buffer, 0);
                u32 overflow_offset = (u32)builder->buffer.count;
                x64_emit_load_memory(
                    builder,
                    X64_REGISTER_RDX,
                    X64_REGISTER_RAX,
                    8,
                    8);
                codegen_emit_u8(&builder->buffer, 0x48);
                codegen_emit_u8(&builder->buffer, 0x83);
                codegen_emit_u8(&builder->buffer, 0xc2);
                codegen_emit_u8(&builder->buffer, 8);
                x64_emit_store_memory(
                    builder,
                    X64_REGISTER_RAX,
                    8,
                    X64_REGISTER_RDX,
                    8);
                codegen_emit_u8(&builder->buffer, 0x48);
                codegen_emit_u8(&builder->buffer, 0x83);
                codegen_emit_u8(&builder->buffer, 0xea);
                codegen_emit_u8(&builder->buffer, 8);
                u32 end_offset = (u32)builder->buffer.count;
                s32 overflow_displacement =
                    (s32)(overflow_offset - (overflow_patch + 4));
                s32 end_displacement =
                    (s32)(end_offset - (end_patch + 4));
                memcpy(
                    builder->buffer.bytes + overflow_patch,
                    &overflow_displacement,
                    sizeof(overflow_displacement));
                memcpy(
                    builder->buffer.bytes + end_patch,
                    &end_displacement,
                    sizeof(end_displacement));
            }
            x64_emit_load_memory(
                builder,
                X64_REGISTER_RAX,
                X64_REGISTER_RDX,
                0,
                size);
            x64_emit_store_result(builder, instruction);
        } break;
        case IR_OPCODE_INDEX:
        {
            AnalysisType* base = analysis_type_from_id(
                builder->analysis,
                builder->function->values[
                    instruction->operands[0].value].type);
            if (base->kind != ANALYSIS_TYPE_RANGE)
            {
                x64_emit_load_value(
                    builder,
                    X64_REGISTER_RCX,
                    instruction->operands[1]);
                x64_emit_collection_component(
                    builder,
                    instruction->operands[0],
                    1,
                    X64_REGISTER_RDX);
                x64_emit_collection_component(
                    builder,
                    instruction->operands[0],
                    3,
                    X64_REGISTER_RAX);
                codegen_emit_u8(&builder->buffer, 0x48);
                codegen_emit_u8(&builder->buffer, 0x85);
                codegen_emit_u8(&builder->buffer, 0xc0);
                codegen_emit_u8(&builder->buffer, 0x74);
                u32 forward_jump = (u32)builder->buffer.count;
                codegen_emit_u8(&builder->buffer, 0);
                codegen_emit_u8(&builder->buffer, 0x48);
                codegen_emit_u8(&builder->buffer, 0xff);
                codegen_emit_u8(&builder->buffer, 0xca);
                codegen_emit_u8(&builder->buffer, 0x48);
                codegen_emit_u8(&builder->buffer, 0x29);
                codegen_emit_u8(&builder->buffer, 0xca);
                codegen_emit_u8(&builder->buffer, 0x48);
                codegen_emit_u8(&builder->buffer, 0x89);
                codegen_emit_u8(&builder->buffer, 0xd1);
                u32 forward_path = (u32)builder->buffer.count;
                u32 forward_delta =
                    forward_path - forward_jump - 1;
                if (forward_delta > UINT8_MAX)
                {
                    return false;
                }
                builder->buffer.bytes[forward_jump] =
                    (u8)forward_delta;
                x64_emit_collection_component(
                    builder,
                    instruction->operands[0],
                    2,
                    X64_REGISTER_RDX);
                codegen_emit_u8(&builder->buffer, 0x48);
                codegen_emit_u8(&builder->buffer, 0x0f);
                codegen_emit_u8(&builder->buffer, 0xaf);
                codegen_emit_u8(&builder->buffer, 0xca);
                x64_emit_collection_component(
                    builder,
                    instruction->operands[0],
                    0,
                    X64_REGISTER_RAX);
                codegen_emit_u8(&builder->buffer, 0x48);
                codegen_emit_u8(&builder->buffer, 0x01);
                codegen_emit_u8(&builder->buffer, 0xc8);
                if (builder->function->values[
                        instruction->result.value].category ==
                    IR_VALUE_PLACE)
                {
                    x64_emit_store_result(builder, instruction);
                }
                else
                {
                    AnalysisType* result_type =
                        analysis_type_from_id(
                            builder->analysis,
                            instruction->type);
                    if (codegen_type_is_indirect_value(
                            result_type))
                    {
                        x64_emit_store_result(
                            builder,
                            instruction);
                    }
                    else
                    {
                        x64_emit_load_memory_rax(
                            builder,
                            codegen_type_storage_size(
                                result_type));
                        x64_emit_store_result(
                            builder,
                            instruction);
                    }
                }
                break;
            }
            x64_emit_load_value(
                builder,
                X64_REGISTER_RCX,
                instruction->operands[1]);
            x64_emit_load(
                builder,
                X64_REGISTER_RDX,
                x64_value_displacement_component(
                    instruction->operands[0],
                    1));
            x64_emit_load(
                builder,
                X64_REGISTER_RAX,
                x64_value_displacement_component(
                    instruction->operands[0],
                    0));
            codegen_emit_u8(&builder->buffer, 0x48);
            codegen_emit_u8(&builder->buffer, 0x29);
            codegen_emit_u8(&builder->buffer, 0xc2);
            x64_emit_load(
                builder,
                X64_REGISTER_RAX,
                x64_value_displacement_component(
                    instruction->operands[0],
                    2));
            codegen_emit_u8(&builder->buffer, 0x48);
            codegen_emit_u8(&builder->buffer, 0x85);
            codegen_emit_u8(&builder->buffer, 0xc0);
            codegen_emit_u8(&builder->buffer, 0x74);
            u32 forward_jump = (u32)builder->buffer.count;
            codegen_emit_u8(&builder->buffer, 0);
            codegen_emit_u8(&builder->buffer, 0x48);
            codegen_emit_u8(&builder->buffer, 0xff);
            codegen_emit_u8(&builder->buffer, 0xca);
            codegen_emit_u8(&builder->buffer, 0x48);
            codegen_emit_u8(&builder->buffer, 0x29);
            codegen_emit_u8(&builder->buffer, 0xca);
            codegen_emit_u8(&builder->buffer, 0x48);
            codegen_emit_u8(&builder->buffer, 0x89);
            codegen_emit_u8(&builder->buffer, 0xd1);
            u32 forward_path = (u32)builder->buffer.count;
            u32 forward_delta = forward_path - forward_jump - 1;
            if (forward_delta > UINT8_MAX)
            {
                return false;
            }
            builder->buffer.bytes[forward_jump] =
                (u8)forward_delta;
            x64_emit_load(
                builder,
                X64_REGISTER_RAX,
                x64_value_displacement_component(
                    instruction->operands[0],
                    0));
            codegen_emit_u8(&builder->buffer, 0x48);
            codegen_emit_u8(&builder->buffer, 0x01);
            codegen_emit_u8(&builder->buffer, 0xc8);
            x64_emit_store_result(builder, instruction);
        } break;
        case IR_OPCODE_SLICE:
        {
            if (instruction->immediate_count != 2)
            {
                return false;
            }
            bool has_start = instruction->immediates[0] != 0;
            bool has_end = instruction->immediates[1] != 0;
            u32 operand_index = 1;
            if (has_start)
            {
                x64_emit_load_value(
                    builder,
                    X64_REGISTER_RCX,
                    instruction->operands[operand_index++]);
            }
            else
            {
                codegen_emit_u8(&builder->buffer, 0x31);
                codegen_emit_u8(&builder->buffer, 0xc9);
            }
            if (has_end)
            {
                x64_emit_load_value(
                    builder,
                    X64_REGISTER_RDX,
                    instruction->operands[operand_index]);
            }
            else
            {
                x64_emit_collection_component(
                    builder,
                    instruction->operands[0],
                    1,
                    X64_REGISTER_RDX);
            }
            codegen_emit_u8(&builder->buffer, 0x48);
            codegen_emit_u8(&builder->buffer, 0x29);
            codegen_emit_u8(&builder->buffer, 0xca);
            x64_emit_collection_component(
                builder,
                instruction->operands[0],
                2,
                X64_REGISTER_RAX);
            codegen_emit_u8(&builder->buffer, 0x49);
            codegen_emit_u8(&builder->buffer, 0x89);
            codegen_emit_u8(&builder->buffer, 0xc0);
            codegen_emit_u8(&builder->buffer, 0x49);
            codegen_emit_u8(&builder->buffer, 0x0f);
            codegen_emit_u8(&builder->buffer, 0xaf);
            codegen_emit_u8(&builder->buffer, 0xc8);
            x64_emit_collection_component(
                builder,
                instruction->operands[0],
                0,
                X64_REGISTER_RAX);
            codegen_emit_u8(&builder->buffer, 0x48);
            codegen_emit_u8(&builder->buffer, 0x01);
            codegen_emit_u8(&builder->buffer, 0xc8);
            x64_emit_store(
                builder,
                X64_REGISTER_RAX,
                x64_value_displacement_component(
                    instruction->result,
                    0));
            x64_emit_store(
                builder,
                X64_REGISTER_RDX,
                x64_value_displacement_component(
                    instruction->result,
                    1));
            x64_emit_collection_component(
                builder,
                instruction->operands[0],
                2,
                X64_REGISTER_RAX);
            x64_emit_store(
                builder,
                X64_REGISTER_RAX,
                x64_value_displacement_component(
                    instruction->result,
                    2));
            codegen_emit_u8(&builder->buffer, 0x31);
            codegen_emit_u8(&builder->buffer, 0xc0);
            x64_emit_store(
                builder,
                X64_REGISTER_RAX,
                x64_value_displacement_component(
                    instruction->result,
                    3));
        } break;
        case IR_OPCODE_FIELD:
        {
            AnalysisType* base_type = analysis_type_from_id(
                builder->analysis,
                builder->function->values[
                    instruction->operands[0].value].type);
            AnalysisEntitySemantic* semantic =
                codegen_type_semantic(builder->analysis, base_type);
            if (!semantic ||
                instruction->immediate_count != 1 ||
                instruction->immediates[0] >=
                    semantic->field_count)
            {
                return false;
            }
            AnalysisField* field = semantic->fields +
                instruction->immediates[0];
            x64_emit_load(
                builder,
                X64_REGISTER_RAX,
                x64_value_displacement_component(
                    instruction->operands[0],
                    0));
            if (field->offset)
            {
                codegen_emit_u8(&builder->buffer, 0x48);
                codegen_emit_u8(&builder->buffer, 0x05);
                codegen_emit_u32(
                    &builder->buffer,
                    (u32)field->offset);
            }
            if (builder->function->values[
                    instruction->result.value].category ==
                IR_VALUE_PLACE)
            {
                x64_emit_store_result(builder, instruction);
            }
            else
            {
                AnalysisType* result_type =
                    analysis_type_from_id(
                        builder->analysis,
                        instruction->type);
                if (!codegen_type_is_indirect_value(result_type))
                {
                    x64_emit_load_memory_rax(
                        builder,
                        codegen_type_storage_size(
                            result_type));
                }
                x64_emit_store_result(builder, instruction);
            }
        } break;
        case IR_OPCODE_BRANCH:
        {
            x64_emit_edge_copies(
                builder,
                block,
                instruction->targets[0]);
            x64_emit_jump(builder, instruction->targets[0]);
        } break;
        case IR_OPCODE_BRANCH_IF:
        {
            x64_emit_load_value(
                builder,
                X64_REGISTER_RAX,
                instruction->operands[0]);
            codegen_emit_u8(&builder->buffer, 0x48);
            codegen_emit_u8(&builder->buffer, 0x85);
            codegen_emit_u8(&builder->buffer, 0xc0);
            codegen_emit_u8(&builder->buffer, 0x0f);
            codegen_emit_u8(&builder->buffer, 0x84);
            u32 false_jump = (u32)builder->buffer.count;
            codegen_emit_u32(&builder->buffer, 0);
            x64_emit_edge_copies(
                builder,
                block,
                instruction->targets[0]);
            x64_emit_jump(builder, instruction->targets[0]);
            u32 false_path = (u32)builder->buffer.count;
            s64 false_displacement =
                (s64)false_path - (s64)(false_jump + 4);
            memcpy(
                builder->buffer.bytes + false_jump,
                &false_displacement,
                4);
            x64_emit_edge_copies(
                builder,
                block,
                instruction->targets[1]);
            x64_emit_jump(builder, instruction->targets[1]);
        } break;
        case IR_OPCODE_SWITCH:
        {
            if (!instruction->operand_count ||
                instruction->target_count !=
                    instruction->immediate_count + 1)
            {
                return false;
            }
            for (u32 index = 0;
                index < instruction->immediate_count;
                index += 1)
            {
                x64_emit_load_value(
                    builder,
                    X64_REGISTER_RAX,
                    instruction->operands[0]);
                x64_emit_constant_register(
                    builder,
                    X64_REGISTER_RCX,
                    instruction->immediates[index]);
                codegen_emit_u8(&builder->buffer, 0x48);
                codegen_emit_u8(&builder->buffer, 0x39);
                codegen_emit_u8(&builder->buffer, 0xc8);
                codegen_emit_u8(&builder->buffer, 0x0f);
                codegen_emit_u8(&builder->buffer, 0x85);
                u32 next_case =
                    (u32)builder->buffer.count;
                codegen_emit_u32(&builder->buffer, 0);
                x64_emit_edge_copies(
                    builder,
                    block,
                    instruction->targets[index]);
                x64_emit_jump(
                    builder,
                    instruction->targets[index]);
                s64 displacement =
                    (s64)builder->buffer.count -
                    (s64)(next_case + 4);
                s32 displacement_32 =
                    (s32)displacement;
                memcpy(
                    builder->buffer.bytes + next_case,
                    &displacement_32,
                    sizeof(displacement_32));
            }
            IrBlockId default_target =
                instruction->targets[
                    instruction->immediate_count];
            x64_emit_edge_copies(
                builder,
                block,
                default_target);
            x64_emit_jump(builder, default_target);
        } break;
        case IR_OPCODE_RETURN:
        {
            if (instruction->operand_count)
            {
                AnalysisType* return_type = analysis_type_from_id(
                    builder->analysis,
                    builder->function->values[
                        instruction->operands[0].value].type);
                IrValueId return_value =
                    instruction->operands[0];
                if (builder->signature.result.indirect)
                {
                    x64_emit_load(
                        builder,
                        X64_REGISTER_RAX,
                        builder->hidden_result_displacement);
                    x64_emit_load(
                        builder,
                        X64_REGISTER_RCX,
                        x64_value_displacement_component(
                            return_value,
                            0));
                    x64_emit_copy_memory(
                        builder,
                        codegen_type_storage_size(
                            return_type));
                    if (builder->abi ==
                            CODEGEN_ABI_X86_64_SYSTEM_V ||
                        builder->abi ==
                            CODEGEN_ABI_X86_64_WINDOWS)
                    {
                        x64_emit_load(
                            builder,
                            X64_REGISTER_RAX,
                            builder->
                                hidden_result_displacement);
                    }
                }
                else if (codegen_type_is_indirect_value(
                            return_type) ||
                         codegen_type_is_inline_collection(
                            return_type))
                {
                    X64Register integer_results[] = {
                        X64_REGISTER_RAX,
                        X64_REGISTER_RDX,
                    };
                    for (u32 part_index = 0;
                        part_index <
                            builder->signature.result
                                .part_count;
                        part_index += 1)
                    {
                        CodegenAbiPart* part =
                            builder->signature.result.parts +
                            part_index;
                        x64_emit_load_abi_part(
                            builder,
                            return_value,
                            return_type,
                            part,
                            integer_results[part->index],
                            part->index);
                    }
                }
                else if (return_type->kind ==
                    ANALYSIS_TYPE_FLOAT)
                {
                    x64_emit_float_load(
                        builder,
                        0,
                        x64_value_displacement(
                            instruction->operands[0]),
                        return_type->as.float_bit_width);
                }
                else
                {
                    x64_emit_load_value(
                        builder,
                        X64_REGISTER_RAX,
                        instruction->operands[0]);
                }
            }
            x64_emit_return(builder);
        } break;
        case IR_OPCODE_UNREACHABLE:
            codegen_emit_u8(&builder->buffer, 0x0f);
            codegen_emit_u8(&builder->buffer, 0x0b);
            break;
        default: return false;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL CodegenFunction codegen_generate_x86_64(
    Arena* arena,
    AnalysisResult* analysis,
    IrFunction* function,
    Target target)
{
    CodegenAbi abi = codegen_abi_for_target(target);
    CodegenFunction result = {
        .abi = abi,
    };
    CodegenAbiSignature signature =
        codegen_classify_signature_for_target(
            arena,
            analysis,
            function->type,
            target);
    if (!signature.valid)
    {
        result.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
        return result;
    }
    u32 maximum_parameters = 0;
    for (u32 block_index = 0;
        block_index < function->block_count;
        block_index += 1)
    {
        maximum_parameters = BUSTER_MAX(
            maximum_parameters,
            function->blocks[block_index].parameter_count);
    }
    u32 value_bytes =
        function->value_count * X64_VALUE_SLOT_SIZE;
    u32 temporary_bytes =
        maximum_parameters * X64_VALUE_SLOT_SIZE;
    u32* local_storage_offsets = arena_allocate(
        arena,
        u32,
        function->local_count);
    u32* local_storage_sizes = arena_allocate(
        arena,
        u32,
        function->local_count);
    for (u32 index = 0;
        index < function->local_count;
        index += 1)
    {
        local_storage_sizes[index] = X64_VALUE_SLOT_SIZE;
    }
    for (u32 index = 0;
        index < function->instruction_count;
        index += 1)
    {
        IrInstruction* instruction =
            function->instructions + index;
        if (instruction->opcode == IR_OPCODE_LOCAL &&
            instruction->local.value < function->local_count)
        {
            AnalysisType* type = analysis_type_from_id(
                analysis,
                instruction->type);
            u32 size = codegen_type_is_inline_collection(type) ?
                X64_VALUE_SLOT_SIZE :
                codegen_type_storage_size(type);
            local_storage_sizes[instruction->local.value] =
                BUSTER_MAX(size, 8);
        }
    }
    u32 frame_cursor = value_bytes + temporary_bytes;
    for (u32 index = 0;
        index < function->local_count;
        index += 1)
    {
        frame_cursor = codegen_align_u32(
            frame_cursor,
            8);
        frame_cursor += local_storage_sizes[index];
        local_storage_offsets[index] = frame_cursor;
    }
    u32* value_storage_offsets = arena_allocate(
        arena,
        u32,
        function->value_count);
    for (u32 index = 0;
        index < function->value_count;
        index += 1)
    {
        AnalysisType* type = analysis_type_from_id(
            analysis,
            function->values[index].type);
        if (!codegen_type_is_indirect_value(type))
        {
            continue;
        }
        u32 size = codegen_type_storage_size(type);
        u32 alignment = BUSTER_MAX(type->layout.alignment, 1);
        if (type->kind == ANALYSIS_TYPE_VECTOR)
        {
            alignment = BUSTER_MIN(alignment, 16);
        }
        if (!size || alignment > 16)
        {
            result.error = CODEGEN_ERROR_CAPACITY;
            return result;
        }
        frame_cursor = codegen_align_u32(
            frame_cursor,
            alignment);
        frame_cursor += size;
        value_storage_offsets[index] = frame_cursor;
    }
    s32 hidden_result_displacement = 0;
    if (signature.result.indirect)
    {
        frame_cursor = codegen_align_u32(frame_cursor, 8);
        frame_cursor += 8;
        hidden_result_displacement = -(s32)frame_cursor;
    }
    s32 va_register_save_displacement = 0;
    AnalysisType* generated_function_type =
        analysis_type_from_id(analysis, function->type);
    if (generated_function_type->as.function.is_variadic &&
        abi == CODEGEN_ABI_X86_64_SYSTEM_V)
    {
        frame_cursor = codegen_align_u32(frame_cursor, 16);
        frame_cursor += 176;
        va_register_save_displacement = -(s32)frame_cursor;
    }
    u32 frame_size = codegen_align_u32(frame_cursor, 16);
    u64 capacity = (u64)function->instruction_count * 256 +
        (u64)function->block_count * 128 + 128;
    CodegenRegisterAllocation allocation =
        codegen_allocate_registers(
            arena,
            analysis,
            function,
            1,
            false);
    CodegenRegisterAllocation vector_allocation =
        codegen_allocate_registers(
            arena,
            analysis,
            function,
            5,
            true);
    X64Builder builder = {
        .arena = arena,
        .analysis = analysis,
        .function = function,
        .buffer = {
            .bytes = arena_allocate(arena, u8, capacity),
            .capacity = capacity,
        },
        .block_offsets = arena_allocate(
            arena,
            u32,
            function->block_count),
        .frame_size = frame_size,
        .temporary_base = value_bytes,
        .temporary_count = maximum_parameters,
        .local_storage_base = value_bytes + temporary_bytes,
        .value_storage_offsets = value_storage_offsets,
        .local_storage_offsets = local_storage_offsets,
        .value_registers = allocation.registers,
        .vector_registers = vector_allocation.registers,
        .hidden_result_displacement =
            hidden_result_displacement,
        .va_register_save_displacement =
            va_register_save_displacement,
        .signature = signature,
        .abi = abi,
        .target = target,
    };
    codegen_emit_u8(&builder.buffer, 0x55);
    codegen_emit_u8(&builder.buffer, 0x48);
    codegen_emit_u8(&builder.buffer, 0x89);
    codegen_emit_u8(&builder.buffer, 0xe5);
    codegen_emit_u8(&builder.buffer, 0x48);
    codegen_emit_u8(&builder.buffer, 0x81);
    codegen_emit_u8(&builder.buffer, 0xec);
    codegen_emit_u32(&builder.buffer, frame_size);
    if (signature.result.indirect)
    {
        X64Register source =
            abi == CODEGEN_ABI_X86_64_WINDOWS ?
                X64_REGISTER_RCX :
                X64_REGISTER_RDI;
        x64_emit_store(
            &builder,
            source,
            hidden_result_displacement);
    }
    if (generated_function_type->as.function.is_variadic)
    {
        X64Register registers[] = {
            X64_REGISTER_RDI,
            X64_REGISTER_RSI,
            X64_REGISTER_RDX,
            X64_REGISTER_RCX,
            X64_REGISTER_R8,
            X64_REGISTER_R9,
        };
        if (abi == CODEGEN_ABI_X86_64_SYSTEM_V)
        {
            for (u32 index = 0;
                index < BUSTER_ARRAY_LENGTH(registers);
                index += 1)
            {
                x64_emit_store(
                    &builder,
                    registers[index],
                    va_register_save_displacement +
                        (s32)(index * 8));
            }
            for (u32 index = 0; index < 8; index += 1)
            {
                x64_emit_float_store(
                    &builder,
                    index,
                    va_register_save_displacement +
                        48 + (s32)(index * 16),
                    64);
            }
        }
        else
        {
            X64Register windows[] = {
                X64_REGISTER_RCX,
                X64_REGISTER_RDX,
                X64_REGISTER_R8,
                X64_REGISTER_R9,
            };
            for (u32 index = 0;
                index < BUSTER_ARRAY_LENGTH(windows);
                index += 1)
            {
                x64_emit_store_memory(
                    &builder,
                    X64_REGISTER_RBP,
                    16 + index * 8,
                    windows[index],
                    8);
            }
        }
    }
    for (u32 block_index = 0;
        block_index < function->block_count &&
            builder.buffer.error == CODEGEN_ERROR_NONE;
        block_index += 1)
    {
        IrBlock* block = function->blocks + block_index;
        builder.block_offsets[block_index] =
            (u32)builder.buffer.count;
        for (IrInstructionId id = block->first_instruction;
            id.value != IR_ID_UNDERLYING_INVALID;
            id = function->instructions[id.value].next)
        {
            if (!x64_emit_instruction(
                    &builder,
                    block->id,
                    function->instructions + id.value))
            {
                builder.buffer.error =
                    CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                break;
            }
        }
    }
    for (CodegenRelocation* relocation = builder.first_relocation;
        relocation && builder.buffer.error == CODEGEN_ERROR_NONE;
        relocation = relocation->next)
    {
        if (relocation->target.value >= function->block_count)
        {
            builder.buffer.error = CODEGEN_ERROR_INVALID_IR;
            break;
        }
        s64 displacement =
            (s64)builder.block_offsets[relocation->target.value] -
            (s64)(relocation->displacement_offset + 4);
        if (displacement < INT32_MIN || displacement > INT32_MAX)
        {
            builder.buffer.error = CODEGEN_ERROR_CAPACITY;
            break;
        }
        s32 displacement_32 = (s32)displacement;
        memcpy(
            builder.buffer.bytes + relocation->displacement_offset,
            &displacement_32,
            sizeof(displacement_32));
    }
    result.code = (ByteSlice){
        .pointer = builder.buffer.bytes,
        .length = builder.buffer.count,
    };
    result.error = builder.buffer.error;
    result.stack_frame_size = frame_size;
    result.first_call_relocation =
        builder.first_call_relocation;
    result.register_value_count =
        allocation.allocated_count +
        vector_allocation.allocated_count;
    result.spilled_value_count =
        allocation.spilled_count +
        vector_allocation.spilled_count;
    result.native_vector_operation_count =
        builder.native_vector_operation_count;
    result.split_vector_operation_count =
        builder.split_vector_operation_count;
    return result;
}

typedef struct A64Relocation A64Relocation;
struct A64Relocation
{
    A64Relocation* next;
    IrBlockId target;
    u32 instruction_offset;
    bool conditional;
    u8 reserved[3];
};

#define A64_VALUE_SLOT_SIZE 32
#define A64_VALUE_SLOT_COMPONENT_COUNT 4

BUSTER_GLOBAL_LOCAL void a64_emit_instruction_word(
    CodegenBuffer* buffer,
    u32 instruction)
{
    codegen_emit_u32(buffer, instruction);
}

BUSTER_GLOBAL_LOCAL u32 a64_value_offset(IrValueId value)
{
    return value.value * A64_VALUE_SLOT_SIZE;
}

BUSTER_GLOBAL_LOCAL u32 a64_value_component_offset(
    IrValueId value,
    u32 component)
{
    return a64_value_offset(value) + component * 8;
}

BUSTER_GLOBAL_LOCAL void a64_emit_load_value(
    CodegenBuffer* buffer,
    u32 target,
    IrValueId value)
{
    if (buffer->value_registers &&
        buffer->value_registers[value.value] !=
            CODEGEN_REGISTER_UNALLOCATED)
    {
        u32 source = buffer->allocated_register_base +
            buffer->value_registers[value.value];
        if (target != source)
        {
            a64_emit_instruction_word(
                buffer,
                0xaa0003e0 |
                    (source << 16) |
                    target);
        }
        return;
    }
    u32 offset = a64_value_offset(value);
    if (offset > 32760)
    {
        buffer->error = CODEGEN_ERROR_CAPACITY;
        return;
    }
    a64_emit_instruction_word(
        buffer,
        0xf94003e0 |
            ((offset / 8) << 10) |
            target);
}

BUSTER_GLOBAL_LOCAL void a64_emit_store_value(
    CodegenBuffer* buffer,
    u32 source,
    IrValueId value)
{
    if (buffer->value_registers &&
        buffer->value_registers[value.value] !=
            CODEGEN_REGISTER_UNALLOCATED)
    {
        u32 target = buffer->allocated_register_base +
            buffer->value_registers[value.value];
        if (target != source)
        {
            a64_emit_instruction_word(
                buffer,
                0xaa0003e0 |
                    (source << 16) |
                    target);
        }
        return;
    }
    u32 offset = a64_value_offset(value);
    if (offset > 32760)
    {
        buffer->error = CODEGEN_ERROR_CAPACITY;
        return;
    }
    a64_emit_instruction_word(
        buffer,
        0xf90003e0 |
            ((offset / 8) << 10) |
            source);
}

BUSTER_GLOBAL_LOCAL void a64_emit_load_offset(
    CodegenBuffer* buffer,
    u32 target,
    u32 offset)
{
    if (offset > 32760)
    {
        buffer->error = CODEGEN_ERROR_CAPACITY;
        return;
    }
    a64_emit_instruction_word(
        buffer,
        0xf94003e0 |
            ((offset / 8) << 10) |
            target);
}

BUSTER_GLOBAL_LOCAL void a64_emit_store_offset(
    CodegenBuffer* buffer,
    u32 source,
    u32 offset)
{
    if (offset > 32760)
    {
        buffer->error = CODEGEN_ERROR_CAPACITY;
        return;
    }
    a64_emit_instruction_word(
        buffer,
        0xf90003e0 |
            ((offset / 8) << 10) |
            source);
}

BUSTER_GLOBAL_LOCAL void a64_emit_load_value_component(
    CodegenBuffer* buffer,
    u32 target,
    IrValueId value,
    u32 component)
{
    a64_emit_load_offset(
        buffer,
        target,
        a64_value_component_offset(value, component));
}

BUSTER_GLOBAL_LOCAL void a64_emit_store_value_component(
    CodegenBuffer* buffer,
    u32 source,
    IrValueId value,
    u32 component)
{
    a64_emit_store_offset(
        buffer,
        source,
        a64_value_component_offset(value, component));
}

BUSTER_GLOBAL_LOCAL void a64_emit_float_load_value(
    CodegenBuffer* buffer,
    u32 target,
    IrValueId value,
    u32 width)
{
    u32 offset = a64_value_offset(value);
    u32 scale = width == 32 ? 4 : 8;
    if (offset / scale > 4095)
    {
        buffer->error = CODEGEN_ERROR_CAPACITY;
        return;
    }
    a64_emit_instruction_word(
        buffer,
        (width == 32 ? 0xbd4003e0 : 0xfd4003e0) |
            ((offset / scale) << 10) |
            target);
}

BUSTER_GLOBAL_LOCAL void a64_emit_float_store_value(
    CodegenBuffer* buffer,
    u32 source,
    IrValueId value,
    u32 width)
{
    u32 offset = a64_value_offset(value);
    u32 scale = width == 32 ? 4 : 8;
    if (offset / scale > 4095)
    {
        buffer->error = CODEGEN_ERROR_CAPACITY;
        return;
    }
    a64_emit_instruction_word(
        buffer,
        (width == 32 ? 0xbd0003e0 : 0xfd0003e0) |
            ((offset / scale) << 10) |
            source);
}

BUSTER_GLOBAL_LOCAL void a64_emit_constant(
    CodegenBuffer* buffer,
    u32 target,
    u64 value)
{
    a64_emit_instruction_word(
        buffer,
        0xd2800000 |
            ((u32)(value & 0xffff) << 5) |
            target);
    for (u32 shift = 16; shift < 64; shift += 16)
    {
        a64_emit_instruction_word(
            buffer,
            0xf2800000 |
                ((shift / 16) << 21) |
                ((u32)((value >> shift) & 0xffff) << 5) |
                target);
    }
}

BUSTER_GLOBAL_LOCAL void a64_emit_stack_adjust(
    CodegenBuffer* buffer,
    u32 size,
    bool subtract)
{
    while (size)
    {
        u32 chunk = BUSTER_MIN(size, 4095);
        a64_emit_instruction_word(
            buffer,
            (subtract ? 0xd10003ff : 0x910003ff) |
                (chunk << 10));
        size -= chunk;
    }
}

BUSTER_GLOBAL_LOCAL void a64_emit_stack_address(
    CodegenBuffer* buffer,
    u32 target,
    u32 offset)
{
    a64_emit_instruction_word(
        buffer,
        0x910003e0 | target);
    while (offset)
    {
        u32 chunk = BUSTER_MIN(offset, 4095);
        a64_emit_instruction_word(
            buffer,
            0x91000000 |
                target |
                (target << 5) |
                (chunk << 10));
        offset -= chunk;
    }
}

BUSTER_GLOBAL_LOCAL void a64_emit_load_pointer(
    CodegenBuffer* buffer,
    u32 target,
    u32 address,
    u32 size)
{
    u32 encoded =
        size == 1 ? 0x39400000 :
        size == 2 ? 0x79400000 :
        size == 4 ? 0xb9400000 :
        size == 8 ? 0xf9400000 :
        0;
    if (!encoded)
    {
        buffer->error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
        return;
    }
    a64_emit_instruction_word(
        buffer,
        encoded | (address << 5) | target);
}

BUSTER_GLOBAL_LOCAL void a64_emit_store_pointer(
    CodegenBuffer* buffer,
    u32 source,
    u32 address,
    u32 size)
{
    u32 encoded =
        size == 1 ? 0x39000000 :
        size == 2 ? 0x79000000 :
        size == 4 ? 0xb9000000 :
        size == 8 ? 0xf9000000 :
        0;
    if (!encoded)
    {
        buffer->error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
        return;
    }
    a64_emit_instruction_word(
        buffer,
        encoded | (address << 5) | source);
}

BUSTER_GLOBAL_LOCAL void a64_emit_load_pointer_offset(
    CodegenBuffer* buffer,
    u32 target,
    u32 address,
    u32 offset,
    u32 size)
{
    u32 scale =
        size == 1 ? 1 :
        size == 2 ? 2 :
        size == 4 ? 4 : 8;
    u32 encoded =
        size == 1 ? 0x39400000 :
        size == 2 ? 0x79400000 :
        size == 4 ? 0xb9400000 :
        size == 8 ? 0xf9400000 :
        0;
    if (!encoded || offset % scale ||
        offset / scale > 4095)
    {
        buffer->error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
        return;
    }
    a64_emit_instruction_word(
        buffer,
        encoded |
            ((offset / scale) << 10) |
            (address << 5) |
            target);
}

BUSTER_GLOBAL_LOCAL void a64_emit_store_pointer_offset(
    CodegenBuffer* buffer,
    u32 source,
    u32 address,
    u32 offset,
    u32 size)
{
    u32 scale =
        size == 1 ? 1 :
        size == 2 ? 2 :
        size == 4 ? 4 : 8;
    u32 encoded =
        size == 1 ? 0x39000000 :
        size == 2 ? 0x79000000 :
        size == 4 ? 0xb9000000 :
        size == 8 ? 0xf9000000 :
        0;
    if (!encoded || offset % scale ||
        offset / scale > 4095)
    {
        buffer->error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
        return;
    }
    a64_emit_instruction_word(
        buffer,
        encoded |
            ((offset / scale) << 10) |
            (address << 5) |
            source);
}

BUSTER_GLOBAL_LOCAL void a64_emit_float_pointer_offset(
    CodegenBuffer* buffer,
    u32 target,
    u32 address,
    u32 offset,
    u32 size,
    bool store)
{
    u32 scale = size <= 4 ? 4 : 8;
    if (offset % scale || offset / scale > 4095)
    {
        buffer->error = CODEGEN_ERROR_CAPACITY;
        return;
    }
    u32 encoded =
        size <= 4 ?
            (store ? 0xbd000000 : 0xbd400000) :
            (store ? 0xfd000000 : 0xfd400000);
    a64_emit_instruction_word(
        buffer,
        encoded |
            ((offset / scale) << 10) |
            (address << 5) |
            target);
}

BUSTER_GLOBAL_LOCAL void a64_emit_copy_memory(
    CodegenBuffer* buffer,
    u32 size)
{
    u32 offset = 0;
    while (size - offset >= 8)
    {
        a64_emit_instruction_word(
            buffer,
            0xf9400022 | ((offset / 8) << 10));
        a64_emit_instruction_word(
            buffer,
            0xf9000002 | ((offset / 8) << 10));
        offset += 8;
    }
    if (size - offset >= 4)
    {
        a64_emit_instruction_word(
            buffer,
            0xb9400022 | ((offset / 4) << 10));
        a64_emit_instruction_word(
            buffer,
            0xb9000002 | ((offset / 4) << 10));
        offset += 4;
    }
    if (size - offset >= 2)
    {
        a64_emit_instruction_word(
            buffer,
            0x79400022 | ((offset / 2) << 10));
        a64_emit_instruction_word(
            buffer,
            0x79000002 | ((offset / 2) << 10));
        offset += 2;
    }
    if (size != offset)
    {
        a64_emit_instruction_word(
            buffer,
            0x39400022 | (offset << 10));
        a64_emit_instruction_word(
            buffer,
            0x39000002 | (offset << 10));
    }
}

BUSTER_GLOBAL_LOCAL void a64_emit_initialize_aggregate_result(
    CodegenBuffer* buffer,
    u32* value_storage_offsets,
    IrValueId value)
{
    a64_emit_stack_address(
        buffer,
        0,
        value_storage_offsets[value.value]);
    a64_emit_store_value_component(buffer, 0, value, 0);
}

BUSTER_GLOBAL_LOCAL bool a64_emit_vector_binary(
    CodegenBuffer* buffer,
    AnalysisResult* analysis,
    Target target,
    AnalysisTypeId operand_type_id,
    u32* value_storage_offsets,
    u8* vector_registers,
    IrInstruction* instruction)
{
    if (!target_cpu_feature_has(
            target,
            TARGET_CPU_FEATURE_AARCH64_NEON))
    {
        return false;
    }
    AnalysisType* vector = analysis_type_from_id(
        analysis,
        operand_type_id);
    if (vector->kind != ANALYSIS_TYPE_VECTOR ||
        (vector->layout.size != 8 &&
            vector->layout.size != 16 &&
            vector->layout.size != 32 &&
            vector->layout.size != 64))
    {
        return false;
    }
    AnalysisType* element = analysis_type_from_id(
        analysis,
        vector->as.vector.element_type);
    u32 width = element->kind == ANALYSIS_TYPE_FLOAT ?
        element->as.float_bit_width :
        element->kind == ANALYSIS_TYPE_INTEGER ?
            element->as.integer.bit_width : 0;
    if (!width)
    {
        return false;
    }
    u32 operation = 0;
    bool lane_sized = true;
    bool comparison = false;
    bool swap_operands = false;
    bool invert_result = false;
    switch (instruction->binary_operation)
    {
        case IR_BINARY_VECTOR_FLOAT_ADD: operation = 0x0e20d400; break;
        case IR_BINARY_VECTOR_FLOAT_SUBTRACT: operation = 0x0ea0d400; break;
        case IR_BINARY_VECTOR_FLOAT_MULTIPLY: operation = 0x2e20dc00; break;
        case IR_BINARY_VECTOR_FLOAT_DIVIDE: operation = 0x2e20fc00; break;
        case IR_BINARY_VECTOR_INTEGER_ADD: operation = 0x0e208400; break;
        case IR_BINARY_VECTOR_INTEGER_SUBTRACT: operation = 0x2e208400; break;
        case IR_BINARY_VECTOR_INTEGER_BITWISE_AND:
            operation = 0x0e201c00;
            lane_sized = false;
            break;
        case IR_BINARY_VECTOR_INTEGER_BITWISE_OR:
            operation = 0x0ea01c00;
            lane_sized = false;
            break;
        case IR_BINARY_VECTOR_INTEGER_BITWISE_XOR:
            operation = 0x2e201c00;
            lane_sized = false;
            break;
        case IR_BINARY_VECTOR_INTEGER_EQUAL:
            operation = 0x2e208c00;
            comparison = true;
            break;
        case IR_BINARY_VECTOR_INTEGER_NOT_EQUAL:
            operation = 0x2e208c00;
            comparison = true;
            invert_result = true;
            break;
        case IR_BINARY_VECTOR_SIGNED_LESS:
            operation = 0x0e203400;
            comparison = true;
            swap_operands = true;
            break;
        case IR_BINARY_VECTOR_SIGNED_LESS_EQUAL:
            operation = 0x0e203c00;
            comparison = true;
            swap_operands = true;
            break;
        case IR_BINARY_VECTOR_SIGNED_GREATER:
            operation = 0x0e203400;
            comparison = true;
            break;
        case IR_BINARY_VECTOR_SIGNED_GREATER_EQUAL:
            operation = 0x0e203c00;
            comparison = true;
            break;
        case IR_BINARY_VECTOR_UNSIGNED_LESS:
            operation = 0x2e203400;
            comparison = true;
            swap_operands = true;
            break;
        case IR_BINARY_VECTOR_UNSIGNED_LESS_EQUAL:
            operation = 0x2e203c00;
            comparison = true;
            swap_operands = true;
            break;
        case IR_BINARY_VECTOR_UNSIGNED_GREATER:
            operation = 0x2e203400;
            comparison = true;
            break;
        case IR_BINARY_VECTOR_UNSIGNED_GREATER_EQUAL:
            operation = 0x2e203c00;
            comparison = true;
            break;
        case IR_BINARY_VECTOR_FLOAT_EQUAL:
            operation = 0x0e20e400;
            comparison = true;
            break;
        case IR_BINARY_VECTOR_FLOAT_NOT_EQUAL:
            operation = 0x0e20e400;
            comparison = true;
            invert_result = true;
            break;
        case IR_BINARY_VECTOR_FLOAT_LESS:
            operation = 0x2ea0e400;
            comparison = true;
            swap_operands = true;
            break;
        case IR_BINARY_VECTOR_FLOAT_LESS_EQUAL:
            operation = 0x2e20e400;
            comparison = true;
            swap_operands = true;
            break;
        case IR_BINARY_VECTOR_FLOAT_GREATER:
            operation = 0x2ea0e400;
            comparison = true;
            break;
        case IR_BINARY_VECTOR_FLOAT_GREATER_EQUAL:
            operation = 0x2e20e400;
            comparison = true;
            break;
        default: return false;
    }
    if (element->kind == ANALYSIS_TYPE_FLOAT)
    {
        if (width == 64)
        {
            operation |= 0x00400000;
        }
        else if (width != 32)
        {
            return false;
        }
    }
    else if (lane_sized)
    {
        u32 size = width == 8 ? 0 :
            width == 16 ? 1 :
            width == 32 ? 2 :
            width == 64 ? 3 : UINT32_MAX;
        if (size == UINT32_MAX)
        {
            return false;
        }
        operation |= size << 22;
    }
    u32 target_register = 0;
    if (vector->layout.size <= 16 &&
        vector_registers[instruction->result.value] !=
        CODEGEN_REGISTER_UNALLOCATED)
    {
        target_register = 2 +
            vector_registers[instruction->result.value];
    }
    a64_emit_initialize_aggregate_result(
        buffer,
        value_storage_offsets,
        instruction->result);
    a64_emit_load_value_component(
        buffer,
        0,
        instruction->operands[0],
        0);
    a64_emit_load_value_component(
        buffer,
        1,
        instruction->operands[1],
        0);
    a64_emit_load_value_component(
        buffer,
        2,
        instruction->result,
        0);
    u32 chunk_count =
        (u32)((vector->layout.size + 15) / 16);
    for (u32 chunk = 0; chunk < chunk_count; chunk += 1)
    {
        u32 chunk_size =
            vector->layout.size - (u64)chunk * 16 >= 16 ?
                16 : 8;
        u32 load = chunk_size == 16 ?
            0x3dc00000 : 0xfd400000;
        u32 store = chunk_size == 16 ?
            0x3d800000 : 0xfd000000;
        u32 chunk_operation = operation |
            (chunk_size == 16 ? 0x40000000 : 0);
        a64_emit_instruction_word(
            buffer,
            load | (0 << 5) | target_register);
        a64_emit_instruction_word(
            buffer,
            load | (1 << 5) | 1);
        a64_emit_instruction_word(
            buffer,
            chunk_operation |
                ((swap_operands ?
                    target_register : 1) << 16) |
                ((swap_operands ?
                    1 : target_register) << 5) |
                target_register);
        if (comparison && invert_result)
        {
            a64_emit_instruction_word(
                buffer,
                0x2e205800 |
                    (chunk_size == 16 ?
                        0x40000000 : 0) |
                    (target_register << 5) |
                    target_register);
        }
        a64_emit_instruction_word(
            buffer,
            store | (2 << 5) | target_register);
        if (chunk + 1 < chunk_count)
        {
            a64_emit_instruction_word(
                buffer,
                0x91004000);
            a64_emit_instruction_word(
                buffer,
                0x91004021);
            a64_emit_instruction_word(
                buffer,
                0x91004042);
        }
    }
    return true;
}

BUSTER_GLOBAL_LOCAL void a64_emit_load_abi_part(
    CodegenBuffer* buffer,
    AnalysisResult* analysis,
    IrFunction* function,
    IrValueId value,
    AnalysisType* type,
    CodegenAbiPart* part,
    u32 integer_target,
    u32 float_target)
{
    if (codegen_type_is_indirect_value(type))
    {
        a64_emit_load_value_component(buffer, 16, value, 0);
        if (type->kind == ANALYSIS_TYPE_VECTOR &&
            part->kind ==
                CODEGEN_ABI_LOCATION_FLOAT_REGISTER &&
            (part->size == 8 || part->size == 16))
        {
            a64_emit_instruction_word(
                buffer,
                (part->size == 16 ?
                    0x3dc00200 :
                    0xfd400200) |
                    float_target);
            return;
        }
        if (part->kind ==
            CODEGEN_ABI_LOCATION_FLOAT_REGISTER)
        {
            a64_emit_float_pointer_offset(
                buffer,
                float_target,
                16,
                part->value_offset,
                part->size,
                false);
        }
        else
        {
            a64_emit_load_pointer_offset(
                buffer,
                integer_target,
                16,
                part->value_offset,
                part->size);
        }
        return;
    }
    if (codegen_type_is_inline_collection(type))
    {
        u32 component = part->value_offset / 8;
        if (type->kind == ANALYSIS_TYPE_RANGE)
        {
            AnalysisType* element = analysis_type_from_id(
                analysis,
                type->as.element_type);
            component = part->value_offset /
                BUSTER_MAX(
                    codegen_type_storage_size(element),
                    1);
        }
        if (part->kind ==
            CODEGEN_ABI_LOCATION_FLOAT_REGISTER)
        {
            u32 offset =
                a64_value_component_offset(value, component);
            u32 scale = part->size <= 4 ? 4 : 8;
            a64_emit_instruction_word(
                buffer,
                (part->size <= 4 ?
                    0xbd4003e0 :
                    0xfd4003e0) |
                    ((offset / scale) << 10) |
                    float_target);
        }
        else
        {
            a64_emit_load_value_component(
                buffer,
                integer_target,
                value,
                component);
            if (type->kind == ANALYSIS_TYPE_RANGE)
            {
                AnalysisType* element =
                    analysis_type_from_id(
                        analysis,
                        type->as.element_type);
                u32 element_size =
                    codegen_type_storage_size(element);
                if (element_size < 8 &&
                    part->size > element_size)
                {
                    u32 shift = element_size * 8;
                    a64_emit_instruction_word(
                        buffer,
                        0xd3400000 |
                            ((shift - 1) << 10) |
                            (integer_target << 5) |
                            integer_target);
                    a64_emit_load_value_component(
                        buffer,
                        16,
                        value,
                        component + 1);
                    a64_emit_instruction_word(
                        buffer,
                        0xd3400000 |
                            ((64 - shift) << 16) |
                            ((63 - shift) << 10) |
                            (16 << 5) |
                            16);
                    a64_emit_instruction_word(
                        buffer,
                        0xaa000000 |
                            (16 << 16) |
                            (integer_target << 5) |
                            integer_target);
                }
            }
        }
        return;
    }
    BUSTER_UNUSED(function);
}

BUSTER_GLOBAL_LOCAL void a64_emit_store_abi_part(
    CodegenBuffer* buffer,
    AnalysisResult* analysis,
    IrFunction* function,
    IrValueId value,
    AnalysisType* type,
    CodegenAbiPart* part,
    u32 integer_source,
    u32 float_source)
{
    if (codegen_type_is_indirect_value(type))
    {
        a64_emit_load_value_component(buffer, 16, value, 0);
        if (type->kind == ANALYSIS_TYPE_VECTOR &&
            part->kind ==
                CODEGEN_ABI_LOCATION_FLOAT_REGISTER &&
            (part->size == 8 || part->size == 16))
        {
            a64_emit_instruction_word(
                buffer,
                (part->size == 16 ?
                    0x3d800200 :
                    0xfd000200) |
                    float_source);
            return;
        }
        if (part->kind ==
            CODEGEN_ABI_LOCATION_FLOAT_REGISTER)
        {
            a64_emit_float_pointer_offset(
                buffer,
                float_source,
                16,
                part->value_offset,
                part->size,
                true);
        }
        else
        {
            a64_emit_store_pointer_offset(
                buffer,
                integer_source,
                16,
                part->value_offset,
                part->size);
        }
        return;
    }
    if (codegen_type_is_inline_collection(type))
    {
        u32 component = part->value_offset / 8;
        if (type->kind == ANALYSIS_TYPE_RANGE)
        {
            AnalysisType* element = analysis_type_from_id(
                analysis,
                type->as.element_type);
            component = part->value_offset /
                BUSTER_MAX(
                    codegen_type_storage_size(element),
                    1);
        }
        if (part->kind ==
            CODEGEN_ABI_LOCATION_FLOAT_REGISTER)
        {
            u32 offset =
                a64_value_component_offset(value, component);
            u32 scale = part->size <= 4 ? 4 : 8;
            a64_emit_instruction_word(
                buffer,
                (part->size <= 4 ?
                    0xbd0003e0 :
                    0xfd0003e0) |
                    ((offset / scale) << 10) |
                    float_source);
        }
        else
        {
            a64_emit_store_value_component(
                buffer,
                integer_source,
                value,
                component);
            if (type->kind == ANALYSIS_TYPE_RANGE)
            {
                AnalysisType* element =
                    analysis_type_from_id(
                        analysis,
                        type->as.element_type);
                u32 element_size =
                    codegen_type_storage_size(element);
                if (element_size < 8 &&
                    part->size > element_size)
                {
                    u32 shift = element_size * 8;
                    a64_emit_instruction_word(
                        buffer,
                        0xaa0003f0 |
                            (integer_source << 16));
                    a64_emit_instruction_word(
                        buffer,
                        0xd3400000 |
                            (shift << 16) |
                            (63 << 10) |
                            (16 << 5) |
                            16);
                    a64_emit_store_value_component(
                        buffer,
                        16,
                        value,
                        component + 1);
                }
            }
        }
        return;
    }
    BUSTER_UNUSED(function);
}

BUSTER_GLOBAL_LOCAL void a64_emit_collection_component(
    CodegenBuffer* buffer,
    AnalysisResult* analysis,
    IrFunction* function,
    IrValueId base_id,
    u32 component,
    u32 target)
{
    IrValue* base_value = function->values + base_id.value;
    AnalysisType* base_type = analysis_type_from_id(
        analysis,
        base_value->type);
    if (base_value->category == IR_VALUE_PLACE)
    {
        if (base_type->kind == ANALYSIS_TYPE_ARRAY ||
            base_type->kind == ANALYSIS_TYPE_VECTOR)
        {
            bool vector = base_type->kind == ANALYSIS_TYPE_VECTOR;
            if (component == 0)
            {
                a64_emit_load_value(buffer, target, base_id);
            }
            else if (component == 1)
            {
                a64_emit_constant(
                    buffer,
                    target,
                    vector ?
                        base_type->as.vector.count :
                        base_type->as.array.count);
            }
            else if (component == 2)
            {
                AnalysisType* element = analysis_type_from_id(
                    analysis,
                    vector ?
                        base_type->as.vector.element_type :
                        base_type->as.array.element_type);
                a64_emit_constant(
                    buffer,
                    target,
                    codegen_type_storage_size(element));
            }
            else
            {
                a64_emit_constant(buffer, target, 0);
            }
            return;
        }
        if (base_type->kind == ANALYSIS_TYPE_SLICE)
        {
            a64_emit_load_value(buffer, 4, base_id);
            a64_emit_instruction_word(
                buffer,
                0xf9400000 |
                    (4 << 5) |
                    ((component * 8 / 8) << 10) |
                    target);
            return;
        }
    }
    a64_emit_load_value_component(
        buffer,
        target,
        base_id,
        component);
}

BUSTER_GLOBAL_LOCAL void a64_relocation_add(
    Arena* arena,
    CodegenBuffer* buffer,
    A64Relocation** first,
    A64Relocation** last,
    IrBlockId target,
    bool conditional)
{
    A64Relocation* relocation = arena_allocate(
        arena,
        A64Relocation,
        1);
    *relocation = (A64Relocation){
        .target = target,
        .instruction_offset = (u32)buffer->count,
        .conditional = conditional,
    };
    if (*last)
    {
        (*last)->next = relocation;
    }
    else
    {
        *first = relocation;
    }
    *last = relocation;
    a64_emit_instruction_word(
        buffer,
        conditional ? 0xb4000000 : 0x14000000);
}

BUSTER_GLOBAL_LOCAL void a64_emit_edge_copies(
    CodegenBuffer* buffer,
    IrFunction* function,
    IrBlockId predecessor,
    IrBlockId target,
    u32 temporary_base)
{
    IrBlock* block = function->blocks + target.value;
    u32 index = 0;
    for (IrBlockParameter* parameter = block->first_parameter;
        parameter;
        parameter = parameter->next)
    {
        IrValueId incoming =
            x64_parameter_incoming(parameter, predecessor);
        if (incoming.value == IR_ID_UNDERLYING_INVALID)
        {
            buffer->error = CODEGEN_ERROR_INVALID_IR;
            return;
        }
        for (u32 component = 0;
            component < A64_VALUE_SLOT_COMPONENT_COUNT;
            component += 1)
        {
            a64_emit_load_value_component(
                buffer,
                0,
                incoming,
                component);
            a64_emit_store_offset(
                buffer,
                0,
                temporary_base +
                    index * A64_VALUE_SLOT_SIZE +
                    component * 8);
        }
        index += 1;
    }
    index = 0;
    for (IrBlockParameter* parameter = block->first_parameter;
        parameter;
        parameter = parameter->next)
    {
        for (u32 component = 0;
            component < A64_VALUE_SLOT_COMPONENT_COUNT;
            component += 1)
        {
            a64_emit_load_offset(
                buffer,
                0,
                temporary_base +
                    index * A64_VALUE_SLOT_SIZE +
                    component * 8);
            a64_emit_store_value_component(
                buffer,
                0,
                parameter->value,
                component);
        }
        index += 1;
    }
}

BUSTER_GLOBAL_LOCAL void a64_call_relocation_add(
    Arena* arena,
    CodegenBuffer* buffer,
    CodegenCallRelocation** first,
    CodegenCallRelocation** last,
    IrInstruction* instruction)
{
    CodegenCallRelocation* relocation = arena_allocate(
        arena,
        CodegenCallRelocation,
        1);
    *relocation = (CodegenCallRelocation){
        .entity = instruction->entity,
        .instantiation = instruction->instantiation,
        .displacement_offset = (u32)buffer->count,
        .aarch64 = true,
    };
    if (*last)
    {
        (*last)->next = relocation;
    }
    else
    {
        *first = relocation;
    }
    *last = relocation;
    a64_emit_instruction_word(buffer, 0x94000000);
}

BUSTER_GLOBAL_LOCAL CodegenFunction codegen_generate_aarch64(
    Arena* arena,
    AnalysisResult* analysis,
    IrFunction* function,
    Target target)
{
    CodegenAbi abi = codegen_abi_for_target(target);
    CodegenFunction result = {
        .abi = abi,
    };
    CodegenAbiSignature function_signature =
        codegen_classify_signature_for_target(
            arena,
            analysis,
            function->type,
            target);
    if (!function_signature.valid)
    {
        result.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
        return result;
    }
    CodegenRegisterAllocation allocation =
        codegen_allocate_registers(
            arena,
            analysis,
            function,
            7,
            false);
    CodegenRegisterAllocation vector_allocation =
        codegen_allocate_registers(
            arena,
            analysis,
            function,
            6,
            true);
    u32 maximum_parameters = 0;
    for (u32 block_index = 0;
        block_index < function->block_count;
        block_index += 1)
    {
        maximum_parameters = BUSTER_MAX(
            maximum_parameters,
            function->blocks[block_index].parameter_count);
    }
    u32 temporary_base =
        function->value_count * A64_VALUE_SLOT_SIZE;
    u32 local_base =
        temporary_base +
        maximum_parameters * A64_VALUE_SLOT_SIZE;
    u32* local_storage_offsets = arena_allocate(
        arena,
        u32,
        function->local_count);
    u32* local_storage_sizes = arena_allocate(
        arena,
        u32,
        function->local_count);
    for (u32 index = 0;
        index < function->local_count;
        index += 1)
    {
        local_storage_sizes[index] = 8;
    }
    for (u32 index = 0;
        index < function->instruction_count;
        index += 1)
    {
        IrInstruction* instruction =
            function->instructions + index;
        if (instruction->opcode == IR_OPCODE_LOCAL &&
            instruction->local.value <
                function->local_count)
        {
            AnalysisType* type = analysis_type_from_id(
                analysis,
                instruction->type);
            u32 size = codegen_type_is_inline_collection(type) ?
                A64_VALUE_SLOT_SIZE :
                codegen_type_storage_size(type);
            local_storage_sizes[instruction->local.value] =
                BUSTER_MAX(size, 8);
        }
    }
    u32 frame_cursor = local_base;
    for (u32 index = 0;
        index < function->local_count;
        index += 1)
    {
        frame_cursor = codegen_align_u32(
            frame_cursor,
            8);
        local_storage_offsets[index] = frame_cursor;
        frame_cursor += local_storage_sizes[index];
    }
    u32* value_storage_offsets = arena_allocate(
        arena,
        u32,
        function->value_count);
    for (u32 index = 0;
        index < function->value_count;
        index += 1)
    {
        AnalysisType* type = analysis_type_from_id(
            analysis,
            function->values[index].type);
        if (!codegen_type_is_indirect_value(type))
        {
            continue;
        }
        u32 size = codegen_type_storage_size(type);
        u32 alignment = BUSTER_MAX(
            type->layout.alignment,
            1);
        if (type->kind == ANALYSIS_TYPE_VECTOR)
        {
            alignment = BUSTER_MIN(alignment, 16);
        }
        if (!size || alignment > 16)
        {
            result.error = CODEGEN_ERROR_CAPACITY;
            return result;
        }
        frame_cursor = codegen_align_u32(
            frame_cursor,
            alignment);
        value_storage_offsets[index] = frame_cursor;
        frame_cursor += size;
    }
    u32 hidden_result_offset = 0;
    if (function_signature.result.indirect)
    {
        frame_cursor = codegen_align_u32(frame_cursor, 8);
        hidden_result_offset = frame_cursor;
        frame_cursor += 8;
    }
    AnalysisType* generated_function_type =
        analysis_type_from_id(analysis, function->type);
    u32 va_register_save_base = 0;
    if (generated_function_type->as.function.is_variadic &&
        (abi == CODEGEN_ABI_AARCH64_AAPCS64 ||
         abi == CODEGEN_ABI_AARCH64_WINDOWS))
    {
        frame_cursor = codegen_align_u32(frame_cursor, 16);
        va_register_save_base = frame_cursor;
        frame_cursor +=
            abi == CODEGEN_ABI_AARCH64_AAPCS64 ?
                192 :
                64;
    }
    u32 frame_size = codegen_align_u32(
        frame_cursor,
        16);
    if (frame_size > 32760)
    {
        result.error = CODEGEN_ERROR_CAPACITY;
        return result;
    }
    u64 capacity =
        (u64)function->instruction_count * 256 +
        (u64)function->block_count * 192 + 192;
    CodegenBuffer buffer = {
        .bytes = arena_allocate(arena, u8, capacity),
        .capacity = capacity,
        .value_registers = allocation.registers,
        .allocated_register_base = 9,
    };
    u32* block_offsets = arena_allocate(
        arena,
        u32,
        function->block_count);
    A64Relocation* first_relocation = 0;
    A64Relocation* last_relocation = 0;
    CodegenCallRelocation* first_call_relocation = 0;
    CodegenCallRelocation* last_call_relocation = 0;
    a64_emit_instruction_word(&buffer, 0xa9bf7bfd);
    a64_emit_instruction_word(&buffer, 0x910003fd);
    if (frame_size)
    {
        a64_emit_stack_adjust(
            &buffer,
            frame_size,
            true);
    }
    if (function_signature.result.indirect)
    {
        a64_emit_store_offset(
            &buffer,
            8,
            hidden_result_offset);
    }
    if (generated_function_type->as.function.is_variadic &&
        (abi == CODEGEN_ABI_AARCH64_AAPCS64 ||
         abi == CODEGEN_ABI_AARCH64_WINDOWS))
    {
        for (u32 index = 0; index < 8; index += 1)
        {
            a64_emit_store_offset(
                &buffer,
                index,
                va_register_save_base + index * 8);
            if (abi == CODEGEN_ABI_AARCH64_AAPCS64)
            {
                a64_emit_instruction_word(
                    &buffer,
                    0xfd0003e0 |
                        (((va_register_save_base + 64 +
                            index * 16) / 8) << 10) |
                        index);
            }
        }
    }
    for (u32 block_index = 0;
        block_index < function->block_count &&
            buffer.error == CODEGEN_ERROR_NONE;
        block_index += 1)
    {
        IrBlock* block = function->blocks + block_index;
        block_offsets[block_index] = (u32)buffer.count;
        for (IrInstructionId id = block->first_instruction;
            id.value != IR_ID_UNDERLYING_INVALID;
            id = function->instructions[id.value].next)
        {
            IrInstruction* instruction =
                function->instructions + id.value;
            switch (instruction->opcode)
            {
                case IR_OPCODE_LOCAL:
                {
                    if (instruction->local.value >=
                        function->local_count)
                    {
                        buffer.error =
                            CODEGEN_ERROR_CAPACITY;
                        break;
                    }
                    a64_emit_stack_address(
                        &buffer,
                        0,
                        local_storage_offsets[
                            instruction->local.value]);
                    a64_emit_store_value(
                        &buffer,
                        0,
                        instruction->result);
                } break;
                case IR_OPCODE_LOAD:
                {
                    AnalysisType* type = analysis_type_from_id(
                        analysis,
                        instruction->type);
                    a64_emit_load_value(
                        &buffer,
                        0,
                        instruction->operands[0]);
                    if (codegen_type_is_indirect_value(type))
                    {
                        a64_emit_store_value(
                            &buffer,
                            0,
                            instruction->result);
                    }
                    else if (codegen_type_is_inline_collection(
                            type))
                    {
                        for (u32 component = 0;
                            component <
                                A64_VALUE_SLOT_COMPONENT_COUNT;
                            component += 1)
                        {
                            a64_emit_instruction_word(
                                &buffer,
                                0xf9400001 |
                                    (component << 10));
                            a64_emit_store_value_component(
                                &buffer,
                                1,
                                instruction->result,
                                component);
                        }
                    }
                    else
                    {
                        a64_emit_load_pointer(
                            &buffer,
                            0,
                            0,
                            codegen_type_storage_size(type));
                        a64_emit_store_value(
                            &buffer,
                            0,
                            instruction->result);
                    }
                } break;
                case IR_OPCODE_STORE:
                {
                    AnalysisType* type = analysis_type_from_id(
                        analysis,
                        function->values[
                            instruction->operands[1].value].type);
                    a64_emit_load_value(
                        &buffer,
                        0,
                        instruction->operands[0]);
                    if (codegen_type_is_indirect_value(type))
                    {
                        a64_emit_load_value_component(
                            &buffer,
                            1,
                            instruction->operands[1],
                            0);
                        a64_emit_copy_memory(
                            &buffer,
                            codegen_type_storage_size(type));
                    }
                    else if (codegen_type_is_inline_collection(
                            type))
                    {
                        for (u32 component = 0;
                            component <
                                A64_VALUE_SLOT_COMPONENT_COUNT;
                            component += 1)
                        {
                            a64_emit_load_value_component(
                                &buffer,
                                1,
                                instruction->operands[1],
                                component);
                            a64_emit_instruction_word(
                                &buffer,
                                0xf9000001 |
                                    (component << 10));
                        }
                    }
                    else
                    {
                        a64_emit_load_value(
                            &buffer,
                            1,
                            instruction->operands[1]);
                        a64_emit_store_pointer(
                            &buffer,
                            1,
                            0,
                            codegen_type_storage_size(type));
                    }
                } break;
                case IR_OPCODE_ARGUMENT:
                {
                    if (!instruction->immediate_count)
                    {
                        buffer.error =
                            CODEGEN_ERROR_UNSUPPORTED_ABI;
                        break;
                    }
                    u32 argument =
                        (u32)instruction->immediates[0];
                    CodegenAbiSignature signature =
                        codegen_classify_signature_for_target(
                            arena,
                            analysis,
                            function->type,
                            target);
                    if (argument >= signature.argument_count)
                    {
                        buffer.error =
                            CODEGEN_ERROR_UNSUPPORTED_ABI;
                        break;
                    }
                    CodegenAbiLocation* location =
                        signature.arguments + argument;
                    AnalysisType* type =
                        analysis_type_from_id(
                            analysis,
                            instruction->type);
                    if (codegen_type_is_indirect_value(type) ||
                        codegen_type_is_inline_collection(type))
                    {
                        if (codegen_type_is_indirect_value(type))
                        {
                            a64_emit_initialize_aggregate_result(
                                &buffer,
                                value_storage_offsets,
                                instruction->result);
                        }
                        if (location->indirect)
                        {
                            CodegenAbiPart* part =
                                location->parts;
                            if (part->kind ==
                                CODEGEN_ABI_LOCATION_STACK)
                            {
                                a64_emit_load_offset(
                                    &buffer,
                                    1,
                                    frame_size + 16 +
                                        part->stack_offset);
                            }
                            else
                            {
                                a64_emit_instruction_word(
                                    &buffer,
                                    0xaa0003e1 |
                                        (part->index << 16));
                            }
                            a64_emit_load_value_component(
                                &buffer,
                                0,
                                instruction->result,
                                0);
                            a64_emit_copy_memory(
                                &buffer,
                                codegen_type_storage_size(
                                    type));
                            break;
                        }
                        if (codegen_type_is_indirect_value(type) &&
                            location->part_count &&
                            location->parts[0].kind ==
                                CODEGEN_ABI_LOCATION_STACK)
                        {
                            a64_emit_load_value_component(
                                &buffer,
                                0,
                                instruction->result,
                                0);
                            a64_emit_stack_address(
                                &buffer,
                                1,
                                frame_size + 16 +
                                    location->parts[0]
                                        .stack_offset);
                            a64_emit_copy_memory(
                                &buffer,
                                codegen_type_storage_size(
                                    type));
                            break;
                        }
                        for (u32 part_index = 0;
                            part_index <
                                location->part_count;
                            part_index += 1)
                        {
                            CodegenAbiPart* part =
                                location->parts + part_index;
                            if (part->kind ==
                                CODEGEN_ABI_LOCATION_STACK)
                            {
                                a64_emit_load_offset(
                                    &buffer,
                                    1,
                                    frame_size + 16 +
                                        part->stack_offset);
                            }
                            a64_emit_store_abi_part(
                                &buffer,
                                analysis,
                                function,
                                instruction->result,
                                type,
                                part,
                                part->kind ==
                                    CODEGEN_ABI_LOCATION_STACK ?
                                    1 :
                                    part->index,
                                part->index);
                        }
                        break;
                    }
                    if (location->part_count != 1 ||
                        (location->kind !=
                                CODEGEN_ABI_LOCATION_STACK &&
                         location->index >= 8))
                    {
                        buffer.error =
                            CODEGEN_ERROR_UNSUPPORTED_ABI;
                        break;
                    }
                    if (location->kind ==
                        CODEGEN_ABI_LOCATION_STACK)
                    {
                        u32 incoming_offset =
                            frame_size + 16 +
                            location->stack_offset;
                        if (type->kind ==
                            ANALYSIS_TYPE_FLOAT)
                        {
                            u32 scale =
                                type->as.float_bit_width ==
                                    32 ? 4 : 8;
                            if (incoming_offset / scale >
                                4095)
                            {
                                buffer.error =
                                    CODEGEN_ERROR_CAPACITY;
                                break;
                            }
                            a64_emit_instruction_word(
                                &buffer,
                                (type->as.float_bit_width ==
                                        32 ?
                                        0xbd4003e0 :
                                        0xfd4003e0) |
                                    ((incoming_offset /
                                        scale) << 10));
                            a64_emit_float_store_value(
                                &buffer,
                                0,
                                instruction->result,
                                type->as.float_bit_width);
                        }
                        else
                        {
                            a64_emit_load_offset(
                                &buffer,
                                0,
                                incoming_offset);
                            a64_emit_store_value(
                                &buffer,
                                0,
                                instruction->result);
                        }
                    }
                    else if (type->kind ==
                            ANALYSIS_TYPE_FLOAT &&
                        location->kind ==
                            CODEGEN_ABI_LOCATION_FLOAT_REGISTER)
                    {
                        a64_emit_float_store_value(
                            &buffer,
                            location->index,
                            instruction->result,
                            type->as.float_bit_width);
                    }
                    else
                    {
                        a64_emit_store_value(
                            &buffer,
                            location->index,
                            instruction->result);
                    }
                } break;
                case IR_OPCODE_CONSTANT_INTEGER:
                case IR_OPCODE_CONSTANT_FLOAT:
                case IR_OPCODE_ENUM:
                {
                    if (!instruction->immediate_count)
                    {
                        buffer.error =
                            CODEGEN_ERROR_INVALID_IR;
                        break;
                    }
                    u64 value = instruction->immediates[0];
                    if (instruction->immediate_is_negative)
                    {
                        value = 0 - value;
                    }
                    a64_emit_constant(&buffer, 0, value);
                    a64_emit_store_value(
                        &buffer,
                        0,
                        instruction->result);
                } break;
                case IR_OPCODE_UNDEFINED:
                    a64_emit_constant(&buffer, 0, 0);
                    a64_emit_store_value(
                        &buffer,
                        0,
                        instruction->result);
                    break;
                case IR_OPCODE_ARRAY:
                {
                    AnalysisType* type = analysis_type_from_id(
                        analysis,
                        instruction->type);
                    AnalysisTypeId element_type_id =
                        type->kind == ANALYSIS_TYPE_ARRAY ?
                            type->as.array.element_type :
                        type->kind == ANALYSIS_TYPE_VECTOR ?
                            type->as.vector.element_type :
                            type->as.element_type;
                    AnalysisType* element_type =
                        analysis_type_from_id(
                            analysis,
                            element_type_id);
                    u32 element_size =
                        codegen_type_storage_size(
                            element_type);
                    u32 storage =
                        value_storage_offsets[
                            instruction->result.value];
                    if (!storage || !element_size)
                    {
                        buffer.error =
                            CODEGEN_ERROR_INVALID_IR;
                        break;
                    }
                    a64_emit_stack_address(
                        &buffer,
                        0,
                        storage);
                    a64_emit_store_value_component(
                        &buffer,
                        0,
                        instruction->result,
                        0);
                    for (u32 index = 0;
                        index < instruction->operand_count;
                        index += 1)
                    {
                        a64_emit_stack_address(
                            &buffer,
                            0,
                            storage +
                                index * element_size);
                        if (codegen_type_is_indirect_value(
                                element_type))
                        {
                            a64_emit_load_value_component(
                                &buffer,
                                1,
                                instruction->operands[index],
                                0);
                            a64_emit_copy_memory(
                                &buffer,
                                element_size);
                        }
                        else
                        {
                            a64_emit_load_value(
                                &buffer,
                                1,
                                instruction->operands[index]);
                            a64_emit_store_pointer(
                                &buffer,
                                1,
                                0,
                                element_size);
                        }
                    }
                    a64_emit_constant(
                        &buffer,
                        0,
                        instruction->operand_count);
                    a64_emit_store_value_component(
                        &buffer,
                        0,
                        instruction->result,
                        1);
                    a64_emit_constant(
                        &buffer,
                        0,
                        element_size);
                    a64_emit_store_value_component(
                        &buffer,
                        0,
                        instruction->result,
                        2);
                    a64_emit_constant(&buffer, 0, 0);
                    a64_emit_store_value_component(
                        &buffer,
                        0,
                        instruction->result,
                        3);
                } break;
                case IR_OPCODE_AGGREGATE:
                {
                    AnalysisType* type = analysis_type_from_id(
                        analysis,
                        instruction->type);
                    AnalysisEntitySemantic* semantic =
                        codegen_type_semantic(
                            analysis,
                            type);
                    u32 storage =
                        value_storage_offsets[
                            instruction->result.value];
                    if (!semantic || !storage ||
                        instruction->immediate_count !=
                            instruction->operand_count)
                    {
                        buffer.error =
                            CODEGEN_ERROR_INVALID_IR;
                        break;
                    }
                    a64_emit_stack_address(
                        &buffer,
                        0,
                        storage);
                    a64_emit_store_value(
                        &buffer,
                        0,
                        instruction->result);
                    for (u32 index = 0;
                        index < instruction->operand_count;
                        index += 1)
                    {
                        u64 field_index =
                            instruction->immediates[index];
                        if (field_index >=
                            semantic->field_count)
                        {
                            buffer.error =
                                CODEGEN_ERROR_INVALID_IR;
                            break;
                        }
                        AnalysisField* field =
                            semantic->fields + field_index;
                        AnalysisType* field_type =
                            analysis_type_from_id(
                                analysis,
                                field->type);
                        u32 field_size =
                            codegen_type_storage_size(
                                field_type);
                        a64_emit_stack_address(
                            &buffer,
                            0,
                            storage + (u32)field->offset);
                        if (codegen_type_is_indirect_value(
                                field_type))
                        {
                            a64_emit_load_value_component(
                                &buffer,
                                1,
                                instruction->operands[index],
                                0);
                            a64_emit_copy_memory(
                                &buffer,
                                field_size);
                        }
                        else
                        {
                            a64_emit_load_value(
                                &buffer,
                                1,
                                instruction->operands[index]);
                            a64_emit_store_pointer(
                                &buffer,
                                1,
                                0,
                                field_size);
                        }
                    }
                } break;
                case IR_OPCODE_FUNCTION:
                    a64_emit_constant(&buffer, 0, 0);
                    a64_emit_store_value(
                        &buffer,
                        0,
                        instruction->result);
                    break;
                case IR_OPCODE_CALL:
                {
                    if (!instruction->operand_count)
                    {
                        buffer.error =
                            CODEGEN_ERROR_INVALID_IR;
                        break;
                    }
                    AnalysisTypeId function_type_id =
                        function->values[
                            instruction->operands[0].value].type;
                    AnalysisType* function_type =
                        analysis_type_from_id(
                            analysis,
                            function_type_id);
                    u32 call_argument_count =
                        instruction->operand_count - 1;
                    AnalysisTypeId* call_argument_types =
                        arena_allocate(
                            arena,
                            AnalysisTypeId,
                            call_argument_count);
                    for (u32 index = 0;
                        index < call_argument_count;
                        index += 1)
                    {
                        call_argument_types[index] =
                            function->values[
                                instruction->operands[index + 1].value]
                                .type;
                    }
                    CodegenAbiSignature signature =
                        codegen_classify_signature_with_arguments(
                            arena,
                            analysis,
                            function_type_id,
                        function_type->as.function.is_variadic ?
                            call_argument_types : 0,
                        call_argument_count,
                        target);
                    if (!signature.valid)
                    {
                        buffer.error =
                            CODEGEN_ERROR_UNSUPPORTED_ABI;
                        break;
                    }
                    if (signature.argument_count !=
                        instruction->operand_count - 1)
                    {
                        buffer.error =
                            CODEGEN_ERROR_UNSUPPORTED_ABI;
                        break;
                    }
                    if (signature.stack_size > 4095)
                    {
                        buffer.error =
                            CODEGEN_ERROR_CAPACITY;
                        break;
                    }
                    AnalysisType* call_result_type =
                        instruction->result.value !=
                                IR_ID_UNDERLYING_INVALID ?
                            analysis_type_from_id(
                                analysis,
                                instruction->type) :
                            0;
                    if (call_result_type &&
                        (codegen_type_is_indirect_value(
                            call_result_type) ||
                         codegen_type_is_inline_collection(
                            call_result_type)))
                    {
                        if (codegen_type_is_indirect_value(
                                call_result_type))
                        {
                            a64_emit_initialize_aggregate_result(
                                &buffer,
                                value_storage_offsets,
                                instruction->result);
                        }
                        if (signature.result.indirect)
                        {
                            a64_emit_load_value_component(
                                &buffer,
                                8,
                                instruction->result,
                                0);
                        }
                    }
                    if (signature.stack_size)
                    {
                        a64_emit_instruction_word(
                            &buffer,
                            0xd10003ff |
                                (signature.stack_size << 10));
                    }
                    for (u32 argument_index = 0;
                        argument_index <
                            signature.argument_count;
                        argument_index += 1)
                    {
                        CodegenAbiLocation* location =
                            signature.arguments +
                            argument_index;
                        IrValueId operand =
                            instruction->operands[
                                argument_index + 1];
                        AnalysisType* operand_type =
                            analysis_type_from_id(
                                analysis,
                                function->values[
                                    operand.value].type);
                        if (location->indirect)
                        {
                            a64_emit_stack_address(
                                &buffer,
                                0,
                                location->
                                    indirect_copy_offset);
                            a64_emit_load_value_component(
                                &buffer,
                                1,
                                operand,
                                0);
                            a64_emit_copy_memory(
                                &buffer,
                                codegen_type_storage_size(
                                    operand_type));
                            CodegenAbiPart* part =
                                location->parts;
                            if (part->kind ==
                                CODEGEN_ABI_LOCATION_STACK)
                            {
                                a64_emit_store_offset(
                                    &buffer,
                                    0,
                                    part->stack_offset);
                            }
                            else if (part->index)
                            {
                                a64_emit_instruction_word(
                                    &buffer,
                                    0xaa0003e0 |
                                        (0 << 16) |
                                        part->index);
                            }
                            continue;
                        }
                        if (codegen_type_is_indirect_value(
                                operand_type) &&
                            location->part_count &&
                            location->parts[0].kind ==
                                CODEGEN_ABI_LOCATION_STACK)
                        {
                            a64_emit_stack_address(
                                &buffer,
                                0,
                                location->parts[0]
                                    .stack_offset);
                            a64_emit_load_value_component(
                                &buffer,
                                1,
                                operand,
                                0);
                            a64_emit_copy_memory(
                                &buffer,
                                codegen_type_storage_size(
                                    operand_type));
                            continue;
                        }
                        if (codegen_type_is_indirect_value(
                                operand_type) ||
                            codegen_type_is_inline_collection(
                                operand_type))
                        {
                            for (u32 part_index = 0;
                                part_index <
                                    location->part_count;
                                part_index += 1)
                            {
                                CodegenAbiPart* part =
                                    location->parts +
                                    part_index;
                                if (part->kind ==
                                    CODEGEN_ABI_LOCATION_FLOAT_REGISTER)
                                {
                                    a64_emit_load_abi_part(
                                        &buffer,
                                        analysis,
                                        function,
                                        operand,
                                        operand_type,
                                        part,
                                        0,
                                        part->index);
                                }
                                else
                                {
                                    a64_emit_load_abi_part(
                                        &buffer,
                                        analysis,
                                        function,
                                        operand,
                                        operand_type,
                                        part,
                                        0,
                                        0);
                                    if (part->kind ==
                                        CODEGEN_ABI_LOCATION_STACK)
                                    {
                                        a64_emit_store_offset(
                                            &buffer,
                                            0,
                                            part->stack_offset);
                                    }
                                    else if (part->index)
                                    {
                                        a64_emit_instruction_word(
                                            &buffer,
                                            0xaa0003e0 |
                                                part->index);
                                    }
                                }
                            }
                            continue;
                        }
                        if (location->part_count != 1 ||
                            (location->kind !=
                                    CODEGEN_ABI_LOCATION_STACK &&
                             location->index >= 8) ||
                            location->kind ==
                                CODEGEN_ABI_LOCATION_INDIRECT)
                        {
                            buffer.error =
                                CODEGEN_ERROR_UNSUPPORTED_ABI;
                            break;
                        }
                        if (location->kind ==
                            CODEGEN_ABI_LOCATION_STACK)
                        {
                            if (operand_type->kind ==
                                ANALYSIS_TYPE_FLOAT)
                            {
                                a64_emit_float_load_value(
                                    &buffer,
                                    0,
                                    operand,
                                    operand_type->
                                        as.float_bit_width);
                                u32 scale =
                                    operand_type->
                                        as.float_bit_width ==
                                        32 ? 4 : 8;
                                a64_emit_instruction_word(
                                    &buffer,
                                    (operand_type->
                                            as.float_bit_width ==
                                            32 ?
                                            0xbd0003e0 :
                                            0xfd0003e0) |
                                        ((location->
                                            stack_offset /
                                            scale) << 10));
                            }
                            else
                            {
                                a64_emit_load_value(
                                    &buffer,
                                    0,
                                    operand);
                                a64_emit_store_offset(
                                    &buffer,
                                    0,
                                    location->stack_offset);
                            }
                        }
                        else if (operand_type->kind ==
                                ANALYSIS_TYPE_FLOAT &&
                            location->kind ==
                                CODEGEN_ABI_LOCATION_FLOAT_REGISTER)
                        {
                            a64_emit_float_load_value(
                                &buffer,
                                location->index,
                                operand,
                                operand_type->
                                    as.float_bit_width);
                        }
                        else
                        {
                            a64_emit_load_value(
                                &buffer,
                                location->index,
                                operand);
                        }
                    }
                    if (buffer.error !=
                        CODEGEN_ERROR_NONE)
                    {
                        break;
                    }
                    a64_call_relocation_add(
                        arena,
                        &buffer,
                        &first_call_relocation,
                        &last_call_relocation,
                        instruction);
                    if (signature.stack_size)
                    {
                        a64_emit_instruction_word(
                            &buffer,
                            0x910003ff |
                                (signature.stack_size << 10));
                    }
                    if (instruction->result.value !=
                        IR_ID_UNDERLYING_INVALID)
                    {
                        AnalysisType* result_type =
                            analysis_type_from_id(
                                analysis,
                                instruction->type);
                        if (signature.result.indirect)
                        {
                            if (abi ==
                                CODEGEN_ABI_AARCH64_DARWIN)
                            {
                                a64_emit_load_value_component(
                                    &buffer,
                                    0,
                                    instruction->result,
                                    0);
                            }
                        }
                        else if (codegen_type_is_indirect_value(
                                    result_type) ||
                                 codegen_type_is_inline_collection(
                                    result_type))
                        {
                            for (u32 part_index = 0;
                                part_index <
                                    signature.result.part_count;
                                part_index += 1)
                            {
                                CodegenAbiPart* part =
                                    signature.result.parts +
                                    part_index;
                                a64_emit_store_abi_part(
                                    &buffer,
                                    analysis,
                                    function,
                                    instruction->result,
                                    result_type,
                                    part,
                                    part->index,
                                    part->index);
                            }
                        }
                        else if (result_type->kind ==
                            ANALYSIS_TYPE_FLOAT)
                        {
                            a64_emit_float_store_value(
                                &buffer,
                                0,
                                instruction->result,
                                result_type->
                                    as.float_bit_width);
                        }
                        else
                        {
                            a64_emit_store_value(
                                &buffer,
                                0,
                                instruction->result);
                        }
                    }
                } break;
                case IR_OPCODE_UNARY:
                {
                    AnalysisType* unary_type =
                        analysis_type_from_id(
                            analysis,
                            instruction->type);
                    if (unary_type->kind ==
                        ANALYSIS_TYPE_VECTOR)
                    {
                        if (!target_cpu_feature_has(
                                target,
                                TARGET_CPU_FEATURE_AARCH64_NEON))
                        {
                            buffer.error =
                                CODEGEN_ERROR_UNSUPPORTED_TARGET;
                            break;
                        }
                        if (unary_type->layout.size != 8 &&
                            unary_type->layout.size != 16 &&
                            unary_type->layout.size != 32 &&
                            unary_type->layout.size != 64)
                        {
                            buffer.error =
                                CODEGEN_ERROR_UNSUPPORTED_TARGET;
                            break;
                        }
                        AnalysisType* element =
                            analysis_type_from_id(
                                analysis,
                                unary_type->
                                    as.vector.element_type);
                        u32 width =
                            element->kind ==
                                    ANALYSIS_TYPE_FLOAT ?
                                element->
                                    as.float_bit_width :
                            element->kind ==
                                    ANALYSIS_TYPE_INTEGER ?
                                element->
                                    as.integer.bit_width :
                                0;
                        u32 target_register = 0;
                        if (unary_type->layout.size <= 16 &&
                            vector_allocation.registers[
                                instruction->result.value] !=
                            CODEGEN_REGISTER_UNALLOCATED)
                        {
                            target_register = 2 +
                                vector_allocation.registers[
                                    instruction->result.value];
                        }
                        a64_emit_initialize_aggregate_result(
                            &buffer,
                            value_storage_offsets,
                            instruction->result);
                        a64_emit_load_value_component(
                            &buffer,
                            0,
                            instruction->operands[0],
                            0);
                        a64_emit_load_value_component(
                            &buffer,
                            1,
                            instruction->result,
                            0);
                        u32 encoded = 0;
                        if (instruction->
                                unary_operation ==
                            IR_UNARY_VECTOR_FLOAT_NEGATE)
                        {
                            if (width != 32 &&
                                width != 64)
                            {
                                buffer.error =
                                    CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                break;
                            }
                            encoded = 0x2ea0f800 |
                                (width == 64 ?
                                    0x00400000 : 0);
                        }
                        else if (instruction->
                                unary_operation ==
                            IR_UNARY_VECTOR_INTEGER_NEGATE)
                        {
                            u32 size =
                                width == 8 ? 0 :
                                width == 16 ? 1 :
                                width == 32 ? 2 :
                                width == 64 ? 3 :
                                UINT32_MAX;
                            if (size == UINT32_MAX)
                            {
                                buffer.error =
                                    CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                break;
                            }
                            encoded =
                                0x2e20b800 |
                                (size << 22);
                        }
                        else if (instruction->
                                unary_operation ==
                            IR_UNARY_VECTOR_INTEGER_BITWISE_NOT)
                        {
                            encoded = 0x2e205800;
                        }
                        else
                        {
                            buffer.error =
                                CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                            break;
                        }
                        u32 chunk_count = (u32)(
                            (unary_type->layout.size + 15) /
                            16);
                        for (u32 chunk = 0;
                            chunk < chunk_count;
                            chunk += 1)
                        {
                            u32 chunk_size =
                                unary_type->layout.size -
                                        (u64)chunk * 16 >=
                                    16 ?
                                    16 : 8;
                            u32 load =
                                chunk_size == 16 ?
                                    0x3dc00000 :
                                    0xfd400000;
                            u32 store =
                                chunk_size == 16 ?
                                    0x3d800000 :
                                    0xfd000000;
                            a64_emit_instruction_word(
                                &buffer,
                                load | target_register);
                            a64_emit_instruction_word(
                                &buffer,
                                encoded |
                                    (chunk_size == 16 ?
                                        0x40000000 : 0) |
                                    (target_register << 5) |
                                    target_register);
                            a64_emit_instruction_word(
                                &buffer,
                                store |
                                    (1 << 5) |
                                    target_register);
                            if (chunk + 1 < chunk_count)
                            {
                                a64_emit_instruction_word(
                                    &buffer,
                                    0x91004000);
                                a64_emit_instruction_word(
                                    &buffer,
                                    0x91004021);
                            }
                        }
                        break;
                    }
                    if (instruction->unary_operation ==
                        IR_UNARY_FLOAT_NEGATE)
                    {
                        u32 width =
                            unary_type->as.float_bit_width;
                        a64_emit_float_load_value(
                            &buffer,
                            0,
                            instruction->operands[0],
                            width);
                        a64_emit_instruction_word(
                            &buffer,
                            width == 32 ?
                                0x1e214000 :
                                0x1e614000);
                        a64_emit_float_store_value(
                            &buffer,
                            0,
                            instruction->result,
                            width);
                        break;
                    }
                    a64_emit_load_value(
                        &buffer,
                        0,
                        instruction->operands[0]);
                    u32 encoded =
                        instruction->unary_operation ==
                            IR_UNARY_INTEGER_NEGATE ?
                            0xcb0003e0 :
                        instruction->unary_operation ==
                            IR_UNARY_INTEGER_BITWISE_NOT ?
                            0xaa2003e0 :
                        instruction->unary_operation ==
                            IR_UNARY_BOOLEAN_NOT ?
                            0 :
                            UINT32_MAX;
                    if (encoded == UINT32_MAX)
                    {
                        buffer.error =
                            CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                        break;
                    }
                    if (!encoded)
                    {
                        a64_emit_instruction_word(
                            &buffer,
                            0xf100001f);
                        encoded = 0x9a9f17e0;
                    }
                    a64_emit_instruction_word(
                        &buffer,
                        encoded);
                    a64_emit_store_value(
                        &buffer,
                        0,
                        instruction->result);
                } break;
                case IR_OPCODE_BINARY:
                {
                    AnalysisType* binary_type =
                        analysis_type_from_id(
                            analysis,
                            instruction->type);
                    if (binary_type->kind ==
                        ANALYSIS_TYPE_VECTOR)
                    {
                        if (!a64_emit_vector_binary(
                                &buffer,
                                analysis,
                                target,
                                function->values[
                                    instruction->operands[0]
                                        .value].type,
                                value_storage_offsets,
                                vector_allocation.registers,
                                instruction))
                        {
                            buffer.error =
                                CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                        }
                        break;
                    }
                    if (codegen_binary_is_float(
                            instruction->binary_operation))
                    {
                        AnalysisType* operand_type =
                            analysis_type_from_id(
                                analysis,
                                function->values[
                                    instruction->operands[0]
                                        .value].type);
                        u32 width =
                            operand_type->as.float_bit_width;
                        a64_emit_float_load_value(
                            &buffer,
                            0,
                            instruction->operands[0],
                            width);
                        a64_emit_float_load_value(
                            &buffer,
                            1,
                            instruction->operands[1],
                            width);
                        IrBinaryOperation operation =
                            instruction->binary_operation;
                        if (operation >=
                                IR_BINARY_FLOAT_ADD &&
                            operation <=
                                IR_BINARY_FLOAT_DIVIDE)
                        {
                            u32 encoded =
                                operation ==
                                    IR_BINARY_FLOAT_ADD ?
                                    0x1e212800 :
                                operation ==
                                    IR_BINARY_FLOAT_SUBTRACT ?
                                    0x1e213800 :
                                operation ==
                                    IR_BINARY_FLOAT_MULTIPLY ?
                                    0x1e210800 :
                                    0x1e211800;
                            if (width == 64)
                            {
                                encoded |= 0x00400000;
                            }
                            a64_emit_instruction_word(
                                &buffer,
                                encoded);
                            a64_emit_float_store_value(
                                &buffer,
                                0,
                                instruction->result,
                                width);
                            break;
                        }
                        a64_emit_instruction_word(
                            &buffer,
                            (width == 32 ?
                                0x1e212000 :
                                0x1e612000));
                        u32 condition =
                            operation ==
                                IR_BINARY_FLOAT_EQUAL ?
                                0 :
                            operation ==
                                IR_BINARY_FLOAT_NOT_EQUAL ?
                                1 :
                            operation ==
                                IR_BINARY_FLOAT_LESS ?
                                11 :
                            operation ==
                                IR_BINARY_FLOAT_LESS_EQUAL ?
                                13 :
                            operation ==
                                IR_BINARY_FLOAT_GREATER ?
                                12 :
                                10;
                        a64_emit_instruction_word(
                            &buffer,
                            0x9a9f07e0 |
                                ((condition ^ 1) << 12));
                        a64_emit_store_value(
                            &buffer,
                            0,
                            instruction->result);
                        break;
                    }
                    if (instruction->binary_operation ==
                            IR_BINARY_RANGE)
                    {
                        a64_emit_load_value(
                            &buffer,
                            0,
                            instruction->operands[0]);
                        a64_emit_store_value_component(
                            &buffer,
                            0,
                            instruction->result,
                            0);
                        a64_emit_load_value(
                            &buffer,
                            0,
                            instruction->operands[1]);
                        a64_emit_store_value_component(
                            &buffer,
                            0,
                            instruction->result,
                            1);
                        a64_emit_constant(&buffer, 0, 0);
                        a64_emit_store_value_component(
                            &buffer,
                            0,
                            instruction->result,
                            2);
                        break;
                    }
                    a64_emit_load_value(
                        &buffer,
                        0,
                        instruction->operands[0]);
                    a64_emit_load_value(
                        &buffer,
                        1,
                        instruction->operands[1]);
                    IrBinaryOperation operation =
                        instruction->binary_operation;
                    if (operation ==
                            IR_BINARY_SIGNED_REMAINDER ||
                        operation ==
                            IR_BINARY_UNSIGNED_REMAINDER)
                    {
                        a64_emit_instruction_word(
                            &buffer,
                            (operation ==
                                IR_BINARY_SIGNED_REMAINDER ?
                                0x9ac10c00 :
                                0x9ac10800) |
                                2);
                        a64_emit_instruction_word(
                            &buffer,
                            0x9b018040);
                        a64_emit_store_value(
                            &buffer,
                            0,
                            instruction->result);
                        break;
                    }
                    u32 encoded =
                        operation ==
                            IR_BINARY_INTEGER_ADD ?
                            0x8b010000 :
                        operation ==
                            IR_BINARY_INTEGER_SUBTRACT ?
                            0xcb010000 :
                        operation ==
                            IR_BINARY_INTEGER_MULTIPLY ?
                            0x9b017c00 :
                        operation ==
                            IR_BINARY_SIGNED_DIVIDE ?
                            0x9ac10c00 :
                        operation ==
                            IR_BINARY_UNSIGNED_DIVIDE ?
                            0x9ac10800 :
                        operation ==
                            IR_BINARY_SHIFT_LEFT ?
                            0x9ac12000 :
                        operation ==
                            IR_BINARY_SIGNED_SHIFT_RIGHT ?
                            0x9ac12800 :
                        operation ==
                            IR_BINARY_UNSIGNED_SHIFT_RIGHT ?
                            0x9ac12400 :
                        operation ==
                            IR_BINARY_INTEGER_BITWISE_AND ||
                        operation ==
                            IR_BINARY_BOOLEAN_AND ?
                            0x8a010000 :
                        operation ==
                            IR_BINARY_INTEGER_BITWISE_OR ||
                        operation ==
                            IR_BINARY_BOOLEAN_OR ?
                            0xaa010000 :
                        operation ==
                            IR_BINARY_INTEGER_BITWISE_XOR ?
                            0xca010000 :
                            0;
                    bool comparison =
                        operation ==
                            IR_BINARY_INTEGER_EQUAL ||
                        operation ==
                            IR_BINARY_INTEGER_NOT_EQUAL ||
                        operation ==
                            IR_BINARY_POINTER_EQUAL ||
                        operation ==
                            IR_BINARY_POINTER_NOT_EQUAL ||
                        operation ==
                            IR_BINARY_BOOLEAN_EQUAL ||
                        operation ==
                            IR_BINARY_BOOLEAN_NOT_EQUAL ||
                        (operation >=
                            IR_BINARY_SIGNED_LESS &&
                         operation <=
                            IR_BINARY_UNSIGNED_GREATER_EQUAL);
                    if (comparison)
                    {
                        a64_emit_instruction_word(
                            &buffer,
                            0xeb01001f);
                        u32 condition =
                            operation ==
                                    IR_BINARY_INTEGER_EQUAL ||
                                operation ==
                                    IR_BINARY_BOOLEAN_EQUAL ||
                                operation ==
                                    IR_BINARY_POINTER_EQUAL ?
                                0 :
                            operation ==
                                    IR_BINARY_INTEGER_NOT_EQUAL ||
                                operation ==
                                    IR_BINARY_BOOLEAN_NOT_EQUAL ||
                                operation ==
                                    IR_BINARY_POINTER_NOT_EQUAL ?
                                1 :
                            operation ==
                                IR_BINARY_SIGNED_LESS ?
                                11 :
                            operation ==
                                IR_BINARY_SIGNED_LESS_EQUAL ?
                                13 :
                            operation ==
                                IR_BINARY_SIGNED_GREATER ?
                                12 :
                            operation ==
                                IR_BINARY_SIGNED_GREATER_EQUAL ?
                                10 :
                            operation ==
                                IR_BINARY_UNSIGNED_LESS ?
                                3 :
                            operation ==
                                IR_BINARY_UNSIGNED_LESS_EQUAL ?
                                9 :
                            operation ==
                                IR_BINARY_UNSIGNED_GREATER ?
                                8 :
                                2;
                        encoded = 0x9a9f07e0 |
                            ((condition ^ 1) << 12);
                    }
                    if (!encoded)
                    {
                        buffer.error =
                            CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                        break;
                    }
                    a64_emit_instruction_word(
                        &buffer,
                        encoded);
                    a64_emit_store_value(
                        &buffer,
                        0,
                        instruction->result);
                } break;
                case IR_OPCODE_REVERSE:
                {
                    AnalysisType* type =
                        analysis_type_from_id(
                            analysis,
                            instruction->type);
                    u32 reverse_component =
                        type->kind ==
                            ANALYSIS_TYPE_RANGE ? 2 : 3;
                    for (u32 component = 0;
                        component <
                            A64_VALUE_SLOT_COMPONENT_COUNT;
                        component += 1)
                    {
                        a64_emit_load_value_component(
                            &buffer,
                            0,
                            instruction->operands[0],
                            component);
                        if (component ==
                            reverse_component)
                        {
                            a64_emit_instruction_word(
                                &buffer,
                                0xd2400000);
                        }
                        a64_emit_store_value_component(
                            &buffer,
                            0,
                            instruction->result,
                            component);
                    }
                } break;
                case IR_OPCODE_LENGTH:
                {
                    AnalysisType* base =
                        analysis_type_from_id(
                            analysis,
                            function->values[
                                instruction->operands[0]
                                    .value].type);
                    if (base->kind !=
                        ANALYSIS_TYPE_RANGE)
                    {
                        a64_emit_collection_component(
                            &buffer,
                            analysis,
                            function,
                            instruction->operands[0],
                            1,
                            0);
                    }
                    else
                    {
                        a64_emit_load_value_component(
                            &buffer,
                            0,
                            instruction->operands[0],
                            1);
                        a64_emit_load_value_component(
                            &buffer,
                            1,
                            instruction->operands[0],
                            0);
                        a64_emit_instruction_word(
                            &buffer,
                            0xcb010000);
                    }
                    a64_emit_store_value(
                        &buffer,
                        0,
                        instruction->result);
                } break;
                case IR_OPCODE_VA_START:
                {
                    u32 integer_register_count =
                        function_signature.result.indirect ? 1 : 0;
                    u32 float_register_count = 0;
                    u32 stack_end = 0;
                    for (u32 argument = 0;
                        argument <
                            generated_function_type->as.function.argument_count;
                        argument += 1)
                    {
                        CodegenAbiLocation* location =
                            function_signature.arguments + argument;
                        for (u32 part = 0;
                            part < location->part_count;
                            part += 1)
                        {
                            CodegenAbiPart* abi_part =
                                location->parts + part;
                            if (abi_part->kind ==
                                CODEGEN_ABI_LOCATION_FLOAT_REGISTER)
                            {
                                float_register_count = BUSTER_MAX(
                                    float_register_count,
                                    abi_part->index + 1);
                            }
                            else if (abi_part->kind ==
                                CODEGEN_ABI_LOCATION_INTEGER_REGISTER)
                            {
                                integer_register_count = BUSTER_MAX(
                                    integer_register_count,
                                    abi_part->index + 1);
                            }
                            else
                            {
                                stack_end = BUSTER_MAX(
                                    stack_end,
                                    abi_part->stack_offset +
                                        codegen_align_u32(
                                            abi_part->size,
                                            8));
                            }
                        }
                    }
                    u32 incoming_offset = 16 + stack_end;
                    if (incoming_offset > 4095)
                    {
                        buffer.error = CODEGEN_ERROR_CAPACITY;
                        break;
                    }
                    a64_emit_instruction_word(
                        &buffer,
                        0x91000000 |
                            (incoming_offset << 10) |
                            (29 << 5));
                    a64_emit_store_value_component(
                        &buffer,
                        0,
                        instruction->result,
                        0);
                    if (abi == CODEGEN_ABI_AARCH64_AAPCS64 ||
                        abi == CODEGEN_ABI_AARCH64_WINDOWS)
                    {
                        a64_emit_stack_address(
                            &buffer,
                            0,
                            va_register_save_base + 64);
                        a64_emit_store_value_component(
                            &buffer,
                            0,
                            instruction->result,
                            1);
                        if (abi == CODEGEN_ABI_AARCH64_AAPCS64)
                        {
                            a64_emit_stack_address(
                                &buffer,
                                0,
                                va_register_save_base + 192);
                        }
                        else
                        {
                            a64_emit_constant(&buffer, 0, 0);
                        }
                        a64_emit_store_value_component(
                            &buffer,
                            0,
                            instruction->result,
                            2);
                        s32 gr_offset =
                            -(s32)((8 - integer_register_count) * 8);
                        s32 vr_offset =
                            abi == CODEGEN_ABI_AARCH64_AAPCS64 ?
                                -(s32)((8 -
                                    float_register_count) * 16) :
                                0;
                        u64 packed = (u32)gr_offset |
                            ((u64)(u32)vr_offset << 32);
                        a64_emit_constant(&buffer, 0, packed);
                        a64_emit_store_value_component(
                            &buffer,
                            0,
                            instruction->result,
                            3);
                    }
                    else
                    {
                        a64_emit_constant(&buffer, 0, 0);
                        for (u32 component = 1;
                            component < 4;
                            component += 1)
                        {
                            a64_emit_store_value_component(
                                &buffer,
                                0,
                                instruction->result,
                                component);
                        }
                    }
                } break;
                case IR_OPCODE_VA_COPY:
                {
                    a64_emit_load_value(
                        &buffer,
                        0,
                        instruction->operands[0]);
                    for (u32 component = 0;
                        component < 4;
                        component += 1)
                    {
                        a64_emit_load_pointer_offset(
                            &buffer,
                            1,
                            0,
                            component * 8,
                            8);
                        a64_emit_store_value_component(
                            &buffer,
                            1,
                            instruction->result,
                            component);
                    }
                } break;
                case IR_OPCODE_VA_END:
                {
                    a64_emit_load_value(
                        &buffer,
                        0,
                        instruction->operands[0]);
                    a64_emit_constant(&buffer, 1, 1);
                    a64_emit_store_pointer_offset(
                        &buffer,
                        1,
                        0,
                        24,
                        8);
                } break;
                case IR_OPCODE_VA_ARG:
                {
                    AnalysisType* type =
                        analysis_type_from_id(
                            analysis,
                            instruction->type);
                    u32 size = codegen_type_storage_size(type);
                    bool aggregate =
                        codegen_type_is_indirect_value(type);
                    bool scalar =
                        type->kind == ANALYSIS_TYPE_INTEGER ||
                        type->kind == ANALYSIS_TYPE_FLOAT ||
                        type->kind == ANALYSIS_TYPE_BOOL ||
                        type->kind == ANALYSIS_TYPE_POINTER ||
                        type->kind == ANALYSIS_TYPE_ENUM;
                    if (!size ||
                        (!aggregate && (!scalar || size > 8)))
                    {
                        buffer.error =
                            CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                        break;
                    }
                    AnalysisAbiConvention convention =
                        codegen_analysis_abi_convention(abi);
                    AnalysisAbiValue abi_value =
                        abi == CODEGEN_ABI_AARCH64_WINDOWS ?
                            analysis_abi_value_classify_variadic_argument(
                                arena,
                                analysis,
                                instruction->type,
                                convention) :
                            analysis_abi_value_classify(
                                arena,
                                analysis,
                                instruction->type,
                                convention,
                                false);
                    if (!abi_value.part_count)
                    {
                        buffer.error =
                            CODEGEN_ERROR_UNSUPPORTED_ABI;
                        break;
                    }
                    if (aggregate)
                    {
                        a64_emit_initialize_aggregate_result(
                            &buffer,
                            value_storage_offsets,
                            instruction->result);
                        bool register_value =
                            (abi ==
                                CODEGEN_ABI_AARCH64_AAPCS64 ||
                             abi ==
                                CODEGEN_ABI_AARCH64_WINDOWS) &&
                            abi_value.parts[0].location !=
                                ANALYSIS_ABI_LOCATION_STACK;
                        u32 register_offset = 24;
                        u32 register_top_offset = 8;
                        u32 register_increment = 8;
                        if (register_value &&
                            abi_value.parts[0].abi_class ==
                                ANALYSIS_ABI_CLASS_FLOAT)
                        {
                            register_offset = 28;
                            register_top_offset = 16;
                            register_increment = 16;
                        }
                        u32 overflow_patch = 0;
                        if (register_value)
                        {
                            u32 required =
                                abi_value.part_count *
                                    register_increment;
                            a64_emit_load_value(
                                &buffer,
                                0,
                                instruction->operands[0]);
                            a64_emit_load_pointer_offset(
                                &buffer,
                                1,
                                0,
                                register_offset,
                                4);
                            a64_emit_instruction_word(
                                &buffer,
                                0x11000023 |
                                    (required << 10));
                            a64_emit_instruction_word(
                                &buffer,
                                0x7100007f);
                            overflow_patch =
                                (u32)buffer.count;
                            a64_emit_instruction_word(
                                &buffer,
                                0x5400000c);
                            a64_emit_store_pointer_offset(
                                &buffer,
                                3,
                                0,
                                register_offset,
                                4);
                            a64_emit_instruction_word(
                                &buffer,
                                0x93407c21);
                            a64_emit_load_pointer_offset(
                                &buffer,
                                2,
                                0,
                                register_top_offset,
                                8);
                            a64_emit_instruction_word(
                                &buffer,
                                0x8b010042);
                            if (abi_value.indirect)
                            {
                                a64_emit_load_pointer(
                                    &buffer,
                                    1,
                                    2,
                                    8);
                                a64_emit_load_value_component(
                                    &buffer,
                                    0,
                                    instruction->result,
                                    0);
                                a64_emit_copy_memory(
                                    &buffer,
                                    size);
                            }
                            else
                            {
                                for (u32 part = 0;
                                    part <
                                        abi_value.part_count;
                                    part += 1)
                                {
                                    AnalysisAbiPart* abi_part =
                                        abi_value.parts + part;
                                    a64_emit_load_pointer_offset(
                                        &buffer,
                                        3,
                                        2,
                                        part *
                                            register_increment,
                                        abi_part->size);
                                    a64_emit_load_value_component(
                                        &buffer,
                                        0,
                                        instruction->result,
                                        0);
                                    a64_emit_store_pointer_offset(
                                        &buffer,
                                        3,
                                        0,
                                        abi_part->value_offset,
                                        abi_part->size);
                                }
                            }
                            u32 end_branch =
                                (u32)buffer.count;
                            a64_emit_instruction_word(
                                &buffer,
                                0x14000000);
                            u32 overflow_offset =
                                (u32)buffer.count;
                            u32 test_instruction =
                                0x5400000c |
                                (((overflow_offset -
                                    overflow_patch) /
                                    4) << 5);
                            memcpy(
                                buffer.bytes +
                                    overflow_patch,
                                &test_instruction,
                                sizeof(test_instruction));

                            a64_emit_load_value(
                                &buffer,
                                0,
                                instruction->operands[0]);
                            a64_emit_constant(
                                &buffer,
                                3,
                                0);
                            a64_emit_store_pointer_offset(
                                &buffer,
                                3,
                                0,
                                register_offset,
                                4);
                            a64_emit_load_pointer_offset(
                                &buffer,
                                1,
                                0,
                                0,
                                8);
                            u32 alignment =
                                abi_value.indirect ?
                                    8 :
                                    (u32)BUSTER_MAX(
                                        type->layout.alignment,
                                        8);
                            if (alignment > 8)
                            {
                                a64_emit_constant(
                                    &buffer,
                                    3,
                                    alignment - 1);
                                a64_emit_instruction_word(
                                    &buffer,
                                    0x8b030021);
                                a64_emit_constant(
                                    &buffer,
                                    3,
                                    ~(u64)(alignment - 1));
                                a64_emit_instruction_word(
                                    &buffer,
                                    0x8a030021);
                            }
                            a64_emit_instruction_word(
                                &buffer,
                                0xaa0103e2);
                            u32 stack_size =
                                codegen_align_u32(
                                    abi_value.indirect ?
                                        8 :
                                        size,
                                    8);
                            while (stack_size)
                            {
                                u32 chunk =
                                    BUSTER_MIN(
                                        stack_size,
                                        4095);
                                a64_emit_instruction_word(
                                    &buffer,
                                    0x91000042 |
                                        (chunk << 10));
                                stack_size -= chunk;
                            }
                            a64_emit_store_pointer_offset(
                                &buffer,
                                2,
                                0,
                                0,
                                8);
                            if (abi_value.indirect)
                            {
                                a64_emit_load_pointer(
                                    &buffer,
                                    1,
                                    1,
                                    8);
                            }
                            a64_emit_load_value_component(
                                &buffer,
                                0,
                                instruction->result,
                                0);
                            a64_emit_copy_memory(
                                &buffer,
                                size);
                            u32 end_offset =
                                (u32)buffer.count;
                            u32 end_instruction =
                                0x14000000 |
                                ((end_offset -
                                    end_branch) /
                                    4);
                            memcpy(
                                buffer.bytes +
                                    end_branch,
                                &end_instruction,
                                sizeof(end_instruction));
                            break;
                        }

                        a64_emit_load_value(
                            &buffer,
                            0,
                            instruction->operands[0]);
                        a64_emit_load_pointer_offset(
                            &buffer,
                            1,
                            0,
                            0,
                            8);
                        u32 alignment =
                            abi_value.indirect ?
                                8 :
                            abi ==
                                    CODEGEN_ABI_AARCH64_DARWIN ?
                                (u32)BUSTER_MAX(
                                    type->layout.alignment,
                                    1) :
                                (u32)BUSTER_MAX(
                                    type->layout.alignment,
                                    8);
                        if (alignment > 1)
                        {
                            a64_emit_constant(
                                &buffer,
                                3,
                                alignment - 1);
                            a64_emit_instruction_word(
                                &buffer,
                                0x8b030021);
                            a64_emit_constant(
                                &buffer,
                                3,
                                ~(u64)(alignment - 1));
                            a64_emit_instruction_word(
                                &buffer,
                                0x8a030021);
                        }
                        a64_emit_instruction_word(
                            &buffer,
                            0xaa0103e2);
                        u32 stack_size = codegen_align_u32(
                            abi_value.indirect ? 8 : size,
                            abi ==
                                    CODEGEN_ABI_AARCH64_DARWIN ?
                                alignment : 8);
                        while (stack_size)
                        {
                            u32 chunk =
                                BUSTER_MIN(stack_size, 4095);
                            a64_emit_instruction_word(
                                &buffer,
                                0x91000042 |
                                    (chunk << 10));
                            stack_size -= chunk;
                        }
                        a64_emit_store_pointer_offset(
                            &buffer,
                            2,
                            0,
                            0,
                            8);
                        if (abi_value.indirect)
                        {
                            a64_emit_load_pointer(
                                &buffer,
                                1,
                                1,
                                8);
                        }
                        a64_emit_load_value_component(
                            &buffer,
                            0,
                            instruction->result,
                            0);
                        a64_emit_copy_memory(
                            &buffer,
                            size);
                        break;
                    }
                    a64_emit_load_value(
                        &buffer,
                        0,
                        instruction->operands[0]);
                    if (abi == CODEGEN_ABI_AARCH64_DARWIN)
                    {
                        a64_emit_load_pointer_offset(
                            &buffer,
                            1,
                            0,
                            0,
                            8);
                        u32 alignment = (u32)BUSTER_MAX(
                            type->layout.alignment,
                            1);
                        if (alignment > 1)
                        {
                            a64_emit_constant(
                                &buffer,
                                3,
                                alignment - 1);
                            a64_emit_instruction_word(
                                &buffer,
                                0x8b030021);
                            a64_emit_constant(
                                &buffer,
                                3,
                                ~(u64)(alignment - 1));
                            a64_emit_instruction_word(
                                &buffer,
                                0x8a030021);
                        }
                        a64_emit_instruction_word(
                            &buffer,
                            0xaa0103e2);
                        u32 stack_size =
                            codegen_align_u32(
                                size,
                                alignment);
                        a64_emit_instruction_word(
                            &buffer,
                            0x91000042 |
                                (stack_size << 10));
                        a64_emit_store_pointer_offset(
                            &buffer,
                            2,
                            0,
                            0,
                            8);
                    }
                    else
                    {
                        bool floating =
                            type->kind == ANALYSIS_TYPE_FLOAT &&
                            abi != CODEGEN_ABI_AARCH64_WINDOWS;
                        u32 offset = floating ? 28 : 24;
                        u32 top_offset = floating ? 16 : 8;
                        u32 increment = floating ? 16 : 8;
                        a64_emit_load_pointer_offset(
                            &buffer,
                            1,
                            0,
                            offset,
                            4);
                        u32 test_offset = (u32)buffer.count;
                        a64_emit_instruction_word(
                            &buffer,
                            0x36f80001);
                        a64_emit_instruction_word(
                            &buffer,
                            0x93407c21);
                        a64_emit_load_pointer_offset(
                            &buffer,
                            2,
                            0,
                            top_offset,
                            8);
                        a64_emit_instruction_word(
                            &buffer,
                            0x8b010042);
                        a64_emit_instruction_word(
                            &buffer,
                            0x11000021 |
                                (increment << 10));
                        a64_emit_store_pointer_offset(
                            &buffer,
                            1,
                            0,
                            offset,
                            4);
                        u32 end_branch = (u32)buffer.count;
                        a64_emit_instruction_word(
                            &buffer,
                            0x14000000);
                        u32 overflow_offset =
                            (u32)buffer.count;
                        a64_emit_load_pointer_offset(
                            &buffer,
                            2,
                            0,
                            0,
                            8);
                        a64_emit_instruction_word(
                            &buffer,
                            0x91002043);
                        a64_emit_store_pointer_offset(
                            &buffer,
                            3,
                            0,
                            0,
                            8);
                        u32 end_offset = (u32)buffer.count;
                        a64_emit_instruction_word(
                            &buffer,
                            0xaa0203e1);
                        u32 test_instruction = 0x36f80001 |
                            (((overflow_offset - test_offset) / 4) << 5);
                        u32 end_instruction = 0x14000000 |
                            ((end_offset - end_branch) / 4);
                        memcpy(
                            buffer.bytes + test_offset,
                            &test_instruction,
                            sizeof(test_instruction));
                        memcpy(
                            buffer.bytes + end_branch,
                            &end_instruction,
                            sizeof(end_instruction));
                    }
                    a64_emit_load_pointer(
                        &buffer,
                        0,
                        1,
                        size);
                    a64_emit_store_value(
                        &buffer,
                        0,
                        instruction->result);
                } break;
                case IR_OPCODE_INDEX:
                {
                    AnalysisType* base =
                        analysis_type_from_id(
                            analysis,
                            function->values[
                                instruction->operands[0]
                                    .value].type);
                    if (base->kind !=
                        ANALYSIS_TYPE_RANGE)
                    {
                        a64_emit_collection_component(
                            &buffer,
                            analysis,
                            function,
                            instruction->operands[0],
                            1,
                            1);
                        a64_emit_load_value(
                            &buffer,
                            2,
                            instruction->operands[1]);
                        a64_emit_collection_component(
                            &buffer,
                            analysis,
                            function,
                            instruction->operands[0],
                            3,
                            3);
                        a64_emit_instruction_word(
                            &buffer,
                            0xcb020024);
                        a64_emit_instruction_word(
                            &buffer,
                            0xd1000484);
                        a64_emit_instruction_word(
                            &buffer,
                            0xf100007f);
                        a64_emit_instruction_word(
                            &buffer,
                            0x9a821082);
                        a64_emit_collection_component(
                            &buffer,
                            analysis,
                            function,
                            instruction->operands[0],
                            2,
                            3);
                        a64_emit_instruction_word(
                            &buffer,
                            0x9b037c42);
                        a64_emit_collection_component(
                            &buffer,
                            analysis,
                            function,
                            instruction->operands[0],
                            0,
                            0);
                        a64_emit_instruction_word(
                            &buffer,
                            0x8b020000);
                        if (function->values[
                                instruction->result.value]
                                .category ==
                            IR_VALUE_PLACE)
                        {
                            a64_emit_store_value(
                                &buffer,
                                0,
                                instruction->result);
                        }
                        else
                        {
                            AnalysisType* result_type =
                                analysis_type_from_id(
                                    analysis,
                                    instruction->type);
                            if (!codegen_type_is_indirect_value(
                                    result_type))
                            {
                                a64_emit_load_pointer(
                                    &buffer,
                                    0,
                                    0,
                                    codegen_type_storage_size(
                                        result_type));
                            }
                            a64_emit_store_value(
                                &buffer,
                                0,
                                instruction->result);
                        }
                        break;
                    }
                    a64_emit_load_value_component(
                        &buffer,
                        0,
                        instruction->operands[0],
                        0);
                    a64_emit_load_value_component(
                        &buffer,
                        1,
                        instruction->operands[0],
                        1);
                    a64_emit_load_value(
                        &buffer,
                        2,
                        instruction->operands[1]);
                    a64_emit_load_value_component(
                        &buffer,
                        3,
                        instruction->operands[0],
                        2);
                    a64_emit_instruction_word(
                        &buffer,
                        0xcb000021);
                    a64_emit_instruction_word(
                        &buffer,
                        0xcb020024);
                    a64_emit_instruction_word(
                        &buffer,
                        0xd1000484);
                    a64_emit_instruction_word(
                        &buffer,
                        0xf100007f);
                    a64_emit_instruction_word(
                        &buffer,
                        0x9a821082);
                    a64_emit_load_value_component(
                        &buffer,
                        0,
                        instruction->operands[0],
                        0);
                    a64_emit_instruction_word(
                        &buffer,
                        0x8b020000);
                    a64_emit_store_value(
                        &buffer,
                        0,
                        instruction->result);
                } break;
                case IR_OPCODE_SLICE:
                {
                    if (instruction->immediate_count != 2)
                    {
                        buffer.error =
                            CODEGEN_ERROR_INVALID_IR;
                        break;
                    }
                    bool has_start =
                        instruction->immediates[0] != 0;
                    bool has_end =
                        instruction->immediates[1] != 0;
                    u32 operand_index = 1;
                    if (has_start)
                    {
                        a64_emit_load_value(
                            &buffer,
                            1,
                            instruction->operands[
                                operand_index++]);
                    }
                    else
                    {
                        a64_emit_constant(&buffer, 1, 0);
                    }
                    if (has_end)
                    {
                        a64_emit_load_value(
                            &buffer,
                            2,
                            instruction->operands[
                                operand_index]);
                    }
                    else
                    {
                        a64_emit_collection_component(
                            &buffer,
                            analysis,
                            function,
                            instruction->operands[0],
                            1,
                            2);
                    }
                    a64_emit_instruction_word(
                        &buffer,
                        0xcb010042);
                    a64_emit_collection_component(
                        &buffer,
                        analysis,
                        function,
                        instruction->operands[0],
                        2,
                        3);
                    a64_emit_instruction_word(
                        &buffer,
                        0x9b037c21);
                    a64_emit_collection_component(
                        &buffer,
                        analysis,
                        function,
                        instruction->operands[0],
                        0,
                        0);
                    a64_emit_instruction_word(
                        &buffer,
                        0x8b010000);
                    a64_emit_store_value_component(
                        &buffer,
                        0,
                        instruction->result,
                        0);
                    a64_emit_store_value_component(
                        &buffer,
                        2,
                        instruction->result,
                        1);
                    a64_emit_store_value_component(
                        &buffer,
                        3,
                        instruction->result,
                        2);
                    a64_emit_constant(&buffer, 0, 0);
                    a64_emit_store_value_component(
                        &buffer,
                        0,
                        instruction->result,
                        3);
                } break;
                case IR_OPCODE_FIELD:
                {
                    AnalysisType* base_type =
                        analysis_type_from_id(
                            analysis,
                            function->values[
                                instruction->operands[0]
                                    .value].type);
                    AnalysisEntitySemantic* semantic =
                        codegen_type_semantic(
                            analysis,
                            base_type);
                    if (!semantic ||
                        instruction->immediate_count != 1 ||
                        instruction->immediates[0] >=
                            semantic->field_count)
                    {
                        buffer.error =
                            CODEGEN_ERROR_INVALID_IR;
                        break;
                    }
                    AnalysisField* field = semantic->fields +
                        instruction->immediates[0];
                    a64_emit_load_value_component(
                        &buffer,
                        0,
                        instruction->operands[0],
                        0);
                    if (field->offset)
                    {
                        a64_emit_constant(
                            &buffer,
                            1,
                            field->offset);
                        a64_emit_instruction_word(
                            &buffer,
                            0x8b010000);
                    }
                    if (function->values[
                            instruction->result.value].category ==
                        IR_VALUE_PLACE)
                    {
                        a64_emit_store_value(
                            &buffer,
                            0,
                            instruction->result);
                    }
                    else
                    {
                        AnalysisType* result_type =
                            analysis_type_from_id(
                                analysis,
                                instruction->type);
                        if (!codegen_type_is_indirect_value(
                                result_type))
                        {
                            a64_emit_load_pointer(
                                &buffer,
                                0,
                                0,
                                codegen_type_storage_size(
                                    result_type));
                        }
                        a64_emit_store_value(
                            &buffer,
                            0,
                            instruction->result);
                    }
                } break;
                case IR_OPCODE_BRANCH:
                    a64_emit_edge_copies(
                        &buffer,
                        function,
                        block->id,
                        instruction->targets[0],
                        temporary_base);
                    a64_relocation_add(
                        arena,
                        &buffer,
                        &first_relocation,
                        &last_relocation,
                        instruction->targets[0],
                        false);
                    break;
                case IR_OPCODE_BRANCH_IF:
                {
                    a64_emit_load_value(
                        &buffer,
                        0,
                        instruction->operands[0]);
                    u32 false_branch_offset =
                        (u32)buffer.count;
                    a64_emit_instruction_word(
                        &buffer,
                        0xb4000000);
                    a64_emit_edge_copies(
                        &buffer,
                        function,
                        block->id,
                        instruction->targets[0],
                        temporary_base);
                    a64_relocation_add(
                        arena,
                        &buffer,
                        &first_relocation,
                        &last_relocation,
                        instruction->targets[0],
                        false);
                    s64 byte_delta =
                        (s64)buffer.count -
                        (s64)false_branch_offset;
                    s64 instruction_delta =
                        byte_delta / 4;
                    if (instruction_delta >= (1 << 18))
                    {
                        buffer.error =
                            CODEGEN_ERROR_CAPACITY;
                        break;
                    }
                    u32 encoded = 0xb4000000 |
                        (((u32)instruction_delta &
                            0x7ffff) << 5);
                    memcpy(
                        buffer.bytes + false_branch_offset,
                        &encoded,
                        sizeof(encoded));
                    a64_emit_edge_copies(
                        &buffer,
                        function,
                        block->id,
                        instruction->targets[1],
                        temporary_base);
                    a64_relocation_add(
                        arena,
                        &buffer,
                        &first_relocation,
                        &last_relocation,
                        instruction->targets[1],
                        false);
                } break;
                case IR_OPCODE_SWITCH:
                {
                    if (!instruction->operand_count ||
                        instruction->target_count !=
                            instruction->immediate_count + 1)
                    {
                        buffer.error =
                            CODEGEN_ERROR_INVALID_IR;
                        break;
                    }
                    for (u32 index = 0;
                        index < instruction->immediate_count;
                        index += 1)
                    {
                        a64_emit_load_value(
                            &buffer,
                            0,
                            instruction->operands[0]);
                        a64_emit_constant(
                            &buffer,
                            1,
                            instruction->immediates[index]);
                        a64_emit_instruction_word(
                            &buffer,
                            0xeb01001f);
                        u32 next_case =
                            (u32)buffer.count;
                        a64_emit_instruction_word(
                            &buffer,
                            0x54000001);
                        a64_emit_edge_copies(
                            &buffer,
                            function,
                            block->id,
                            instruction->targets[index],
                            temporary_base);
                        a64_relocation_add(
                            arena,
                            &buffer,
                            &first_relocation,
                            &last_relocation,
                            instruction->targets[index],
                            false);
                        s64 instruction_delta =
                            ((s64)buffer.count -
                             (s64)next_case) / 4;
                        if (instruction_delta >=
                            (1 << 18))
                        {
                            buffer.error =
                                CODEGEN_ERROR_CAPACITY;
                            break;
                        }
                        u32 encoded = 0x54000001 |
                            (((u32)instruction_delta &
                                0x7ffff) << 5);
                        memcpy(
                            buffer.bytes + next_case,
                            &encoded,
                            sizeof(encoded));
                    }
                    if (buffer.error !=
                        CODEGEN_ERROR_NONE)
                    {
                        break;
                    }
                    IrBlockId default_target =
                        instruction->targets[
                            instruction->immediate_count];
                    a64_emit_edge_copies(
                        &buffer,
                        function,
                        block->id,
                        default_target,
                        temporary_base);
                    a64_relocation_add(
                        arena,
                        &buffer,
                        &first_relocation,
                        &last_relocation,
                        default_target,
                        false);
                } break;
                case IR_OPCODE_ADDRESS_OF:
                case IR_OPCODE_DEREFERENCE:
                    a64_emit_load_value(
                        &buffer,
                        0,
                        instruction->operands[0]);
                    a64_emit_store_value(
                        &buffer,
                        0,
                        instruction->result);
                    break;
                case IR_OPCODE_CAST:
                {
                    AnalysisType* source_type =
                        analysis_type_from_id(
                            analysis,
                            function->values[
                                instruction->operands[0].value]
                                .type);
                    AnalysisType* target_type =
                        analysis_type_from_id(
                            analysis,
                            instruction->type);
                    IrConversionOperation conversion =
                        instruction->conversion_operation;
                    if (conversion ==
                            IR_CONVERSION_FLOAT_EXTEND ||
                        conversion ==
                            IR_CONVERSION_FLOAT_TRUNCATE)
                    {
                        a64_emit_float_load_value(
                            &buffer,
                            0,
                            instruction->operands[0],
                            source_type->as.float_bit_width);
                        a64_emit_instruction_word(
                            &buffer,
                            conversion ==
                                IR_CONVERSION_FLOAT_EXTEND ?
                                0x1e22c000 :
                                0x1e624000);
                        a64_emit_float_store_value(
                            &buffer,
                            0,
                            instruction->result,
                            target_type->as.float_bit_width);
                    }
                    else if (conversion ==
                                IR_CONVERSION_SIGNED_INTEGER_TO_FLOAT ||
                        conversion ==
                                IR_CONVERSION_UNSIGNED_INTEGER_TO_FLOAT)
                    {
                        a64_emit_load_value(
                            &buffer,
                            0,
                            instruction->operands[0]);
                        u32 encoded =
                            conversion ==
                                IR_CONVERSION_SIGNED_INTEGER_TO_FLOAT ?
                                0x9e220000 :
                                0x9e230000;
                        if (target_type->as.float_bit_width == 64)
                        {
                            encoded |= 0x00400000;
                        }
                        a64_emit_instruction_word(
                            &buffer,
                            encoded);
                        a64_emit_float_store_value(
                            &buffer,
                            0,
                            instruction->result,
                            target_type->as.float_bit_width);
                    }
                    else if (conversion ==
                                IR_CONVERSION_FLOAT_TO_SIGNED_INTEGER ||
                        conversion ==
                                IR_CONVERSION_FLOAT_TO_UNSIGNED_INTEGER)
                    {
                        a64_emit_float_load_value(
                            &buffer,
                            0,
                            instruction->operands[0],
                            source_type->as.float_bit_width);
                        u32 encoded =
                            conversion ==
                                IR_CONVERSION_FLOAT_TO_SIGNED_INTEGER ?
                                0x9e380000 :
                                0x9e390000;
                        if (source_type->as.float_bit_width == 64)
                        {
                            encoded |= 0x00400000;
                        }
                        a64_emit_instruction_word(
                            &buffer,
                            encoded);
                        a64_emit_store_value(
                            &buffer,
                            0,
                            instruction->result);
                    }
                    else
                    {
                        a64_emit_load_value(
                            &buffer,
                            0,
                            instruction->operands[0]);
                        a64_emit_store_value(
                            &buffer,
                            0,
                            instruction->result);
                    }
                } break;
                case IR_OPCODE_RETURN:
                {
                    if (instruction->operand_count)
                    {
                        AnalysisType* return_type =
                            analysis_type_from_id(
                                analysis,
                                function->values[
                                    instruction->operands[0]
                                        .value].type);
                        IrValueId return_value =
                            instruction->operands[0];
                        if (function_signature.result.indirect)
                        {
                            a64_emit_load_offset(
                                &buffer,
                                0,
                                hidden_result_offset);
                            a64_emit_load_value_component(
                                &buffer,
                                1,
                                return_value,
                                0);
                            a64_emit_copy_memory(
                                &buffer,
                                codegen_type_storage_size(
                                    return_type));
                        }
                        else if (codegen_type_is_indirect_value(
                                    return_type) ||
                                 codegen_type_is_inline_collection(
                                    return_type))
                        {
                            for (u32 part_index = 0;
                                part_index <
                                    function_signature.result
                                        .part_count;
                                part_index += 1)
                            {
                                CodegenAbiPart* part =
                                    function_signature.result.parts +
                                    part_index;
                                a64_emit_load_abi_part(
                                    &buffer,
                                    analysis,
                                    function,
                                    return_value,
                                    return_type,
                                    part,
                                    part->index,
                                    part->index);
                            }
                        }
                        else if (return_type->kind ==
                            ANALYSIS_TYPE_FLOAT)
                        {
                            a64_emit_float_load_value(
                                &buffer,
                                0,
                                instruction->operands[0],
                                return_type->as.float_bit_width);
                        }
                        else
                        {
                            a64_emit_load_value(
                                &buffer,
                                0,
                                instruction->operands[0]);
                        }
                    }
                    if (frame_size)
                    {
                        a64_emit_stack_adjust(
                            &buffer,
                            frame_size,
                            false);
                    }
                    a64_emit_instruction_word(
                        &buffer,
                        0xa8c17bfd);
                    a64_emit_instruction_word(
                        &buffer,
                        0xd65f03c0);
                } break;
                case IR_OPCODE_UNREACHABLE:
                    a64_emit_instruction_word(
                        &buffer,
                        0xd4200000);
                    break;
                default:
                    buffer.error =
                        CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                    break;
            }
            if (buffer.error != CODEGEN_ERROR_NONE)
            {
                break;
            }
        }
    }
    for (A64Relocation* relocation = first_relocation;
        relocation && buffer.error == CODEGEN_ERROR_NONE;
        relocation = relocation->next)
    {
        if (relocation->target.value >= function->block_count)
        {
            buffer.error = CODEGEN_ERROR_INVALID_IR;
            break;
        }
        s64 byte_delta =
            (s64)block_offsets[relocation->target.value] -
            (s64)relocation->instruction_offset;
        s64 instruction_delta = byte_delta / 4;
        if (byte_delta % 4 ||
            instruction_delta < -(1 << 25) ||
            instruction_delta >= (1 << 25))
        {
            buffer.error = CODEGEN_ERROR_CAPACITY;
            break;
        }
        u32 encoded = 0x14000000 |
            ((u32)instruction_delta & 0x03ffffff);
        memcpy(
            buffer.bytes + relocation->instruction_offset,
            &encoded,
            sizeof(encoded));
    }
    result.code = (ByteSlice){
        .pointer = buffer.bytes,
        .length = buffer.count,
    };
    result.error = buffer.error;
    result.stack_frame_size = frame_size;
    result.first_call_relocation =
        first_call_relocation;
    result.register_value_count =
        allocation.allocated_count +
        vector_allocation.allocated_count;
    result.spilled_value_count =
        allocation.spilled_count +
        vector_allocation.spilled_count;
    return result;
}

CodegenFunction codegen_generate_function(
    Arena* arena,
    AnalysisResult* analysis,
    IrFunction* function,
    Target target)
{
    CodegenFunction result = {
        .error = CODEGEN_ERROR_UNSUPPORTED_TARGET,
        .abi = codegen_abi_for_target(target),
    };
    if (!analysis || !function ||
        function->state != IR_FUNCTION_LOWERED)
    {
        result.error = CODEGEN_ERROR_INVALID_IR;
        return result;
    }
    analysis_compute_layouts(
        analysis,
        (AnalysisLayoutOptions){
            .pointer_size = 8,
            .pointer_alignment = 8,
        });
    IrModule validation_module = {
        .functions = function,
        .function_count = 1,
    };
    IrValidationResult validation =
        ir_validate_module(analysis, &validation_module);
    if (validation.error != IR_VALIDATION_NONE)
    {
        result.error = CODEGEN_ERROR_INVALID_IR;
        return result;
    }
    if (target.cpu_arch == CPU_ARCH_X86_64)
    {
        return codegen_generate_x86_64(
            arena,
            analysis,
            function,
            target);
    }
    if (target.cpu_arch == CPU_ARCH_AARCH64)
    {
        return codegen_generate_aarch64(
            arena,
            analysis,
            function,
            target);
    }
    return result;
}

CodegenModule codegen_generate_module(
    Arena* arena,
    AnalysisResult* analysis,
    IrModule* module,
    Target target)
{
    CodegenModule result = {
        .abi = codegen_abi_for_target(target),
    };
    if (!analysis || !module ||
        result.abi >= CODEGEN_ABI_COUNT)
    {
        result.error = CODEGEN_ERROR_INVALID_IR;
        return result;
    }
    analysis_compute_layouts(
        analysis,
        (AnalysisLayoutOptions){
            .pointer_size = 8,
            .pointer_alignment = 8,
        });
    IrValidationResult validation =
        ir_validate_module(analysis, module);
    if (validation.error != IR_VALIDATION_NONE)
    {
        result.error = CODEGEN_ERROR_INVALID_IR;
        return result;
    }
    CodegenFunction* generated = arena_allocate(
        arena,
        CodegenFunction,
        module->function_count);
    result.entries = arena_allocate(
        arena,
        CodegenModuleEntry,
        module->function_count);
    u64 total_size = 0;
    for (u32 index = 0;
        index < module->function_count;
        index += 1)
    {
        IrFunction* function = module->functions + index;
        if (function->state != IR_FUNCTION_LOWERED)
        {
            continue;
        }
        generated[index] =
            target.cpu_arch == CPU_ARCH_X86_64 ?
                codegen_generate_x86_64(
                    arena,
                    analysis,
                    function,
                    target) :
            target.cpu_arch == CPU_ARCH_AARCH64 ?
                codegen_generate_aarch64(
                    arena,
                    analysis,
                    function,
                    target) :
                (CodegenFunction){
                    .error =
                        CODEGEN_ERROR_UNSUPPORTED_TARGET,
                };
        if (generated[index].error != CODEGEN_ERROR_NONE)
        {
            result.error = generated[index].error;
            return result;
        }
        total_size = codegen_align_u32(
            (u32)total_size,
            16);
        result.entries[result.entry_count++] =
            (CodegenModuleEntry){
                .entity = function->entity,
                .instantiation = function->instantiation,
                .offset = (u32)total_size,
            };
        total_size += generated[index].code.length;
        if (total_size > UINT32_MAX)
        {
            result.error = CODEGEN_ERROR_CAPACITY;
            return result;
        }
    }
    CodegenBuffer buffer = {
        .bytes = arena_allocate(arena, u8, total_size),
        .capacity = total_size,
    };
    u32 entry_index = 0;
    for (u32 function_index = 0;
        function_index < module->function_count;
        function_index += 1)
    {
        CodegenFunction* function =
            generated + function_index;
        if (!function->code.length)
        {
            continue;
        }
        while (buffer.count <
            result.entries[entry_index].offset)
        {
            codegen_emit_u8(&buffer, 0x90);
        }
        u32 function_offset = (u32)buffer.count;
        for (u64 byte_index = 0;
            byte_index < function->code.length;
            byte_index += 1)
        {
            codegen_emit_u8(
                &buffer,
                function->code.pointer[byte_index]);
        }
        for (CodegenCallRelocation* relocation =
                function->first_call_relocation;
            relocation;
            relocation = relocation->next)
        {
            CodegenModuleEntry* target_entry = 0;
            for (u32 target_index = 0;
                target_index < result.entry_count;
                target_index += 1)
            {
                CodegenModuleEntry* candidate =
                    result.entries + target_index;
                if (candidate->entity.module.value ==
                        relocation->entity.module.value &&
                    candidate->entity.index.value ==
                        relocation->entity.index.value &&
                    candidate->instantiation.value ==
                        relocation->instantiation.value)
                {
                    target_entry = candidate;
                    break;
                }
            }
            if (!target_entry)
            {
                result.error =
                    CODEGEN_ERROR_INVALID_IR;
                return result;
            }
            u32 displacement_offset = function_offset +
                relocation->displacement_offset;
            s64 displacement = (s64)target_entry->offset -
                (s64)(relocation->aarch64 ?
                    displacement_offset :
                    displacement_offset + 4);
            if (relocation->aarch64)
            {
                s64 instruction_delta = displacement / 4;
                if (displacement % 4 ||
                    instruction_delta < -(1 << 25) ||
                    instruction_delta >= (1 << 25))
                {
                    result.error = CODEGEN_ERROR_CAPACITY;
                    return result;
                }
                u32 encoded = 0x94000000 |
                    ((u32)instruction_delta &
                        0x03ffffff);
                memcpy(
                    buffer.bytes + displacement_offset,
                    &encoded,
                    sizeof(encoded));
            }
            else
            {
                if (displacement < INT32_MIN ||
                    displacement > INT32_MAX)
                {
                    result.error = CODEGEN_ERROR_CAPACITY;
                    return result;
                }
                s32 displacement_32 = (s32)displacement;
                memcpy(
                    buffer.bytes + displacement_offset,
                    &displacement_32,
                    sizeof(displacement_32));
            }
        }
        entry_index += 1;
    }
    result.code = (ByteSlice){
        .pointer = buffer.bytes,
        .length = buffer.count,
    };
    result.error = buffer.error;
    return result;
}

CodegenExecutable codegen_make_executable(CodegenFunction function)
{
    CodegenExecutable result = {0};
    if (function.error != CODEGEN_ERROR_NONE ||
        !function.code.length)
    {
        result.error = function.error ?
            function.error :
            CODEGEN_ERROR_INVALID_IR;
        return result;
    }
    u64 page_size = os_get_page_size();
    u64 allocation_size =
        (function.code.length + page_size - 1) &
        ~(page_size - 1);
    void* address = os_reserve(
        0,
        allocation_size,
        (ProtectionFlags){ .read = 1, .write = 1 },
        (MapFlags){ .priv = 1, .anonymous = 1 });
    if (!address)
    {
        result.error = CODEGEN_ERROR_EXECUTABLE_MEMORY;
        return result;
    }
    memcpy(address, function.code.pointer, function.code.length);
    if (!os_commit(
            address,
            allocation_size,
            (ProtectionFlags){ .read = 1, .execute = 1 },
            false))
    {
        os_unreserve(address, allocation_size);
        result.error = CODEGEN_ERROR_EXECUTABLE_MEMORY;
        return result;
    }
    if (!os_flush_instruction_cache(
            address,
            function.code.length))
    {
        os_unreserve(address, allocation_size);
        result.error = CODEGEN_ERROR_EXECUTABLE_MEMORY;
        return result;
    }
    result.address = address;
    result.allocation_size = allocation_size;
    return result;
}

void codegen_release_executable(CodegenExecutable executable)
{
    if (executable.address && executable.allocation_size)
    {
        os_unreserve(
            executable.address,
            executable.allocation_size);
    }
}

#if BUSTER_INCLUDE_TESTS
typedef u64 CodegenTestFunction2(u64 left, u64 right);
typedef u64 CodegenTestFunction1(u64 value);
typedef f64 CodegenTestIntegerToFloatFunction(s32 value);
typedef s64 CodegenTestFloatToIntegerFunction(f64 value);
typedef u64 CodegenTestFunction0(void);
typedef f64 CodegenTestFloatFunction2(f64 left, f64 right);
typedef f64 CodegenTestFloatFunction0(void);
typedef struct CodegenTestAbiPair
{
    s64 left;
    s64 right;
} CodegenTestAbiPair;
typedef struct CodegenTestAbiMixed
{
    f64 value;
    s64 count;
} CodegenTestAbiMixed;
typedef struct CodegenTestAbiLarge
{
    s64 first;
    s64 second;
    s64 third;
} CodegenTestAbiLarge;
typedef s64 CodegenTestAbiPairSumFunction(CodegenTestAbiPair pair);
typedef CodegenTestAbiPair CodegenTestAbiPairMakeFunction(
    s64 left,
    s64 right);
typedef f64 CodegenTestAbiMixedSumFunction(
    CodegenTestAbiMixed mixed);
typedef s64 CodegenTestAbiLargeSumFunction(
    CodegenTestAbiLarge large);
typedef CodegenTestAbiLarge CodegenTestAbiLargeMakeFunction(
    s64 first,
    s64 second,
    s64 third);

BUSTER_GLOBAL_LOCAL AnalysisEntity* codegen_test_entity_find(
    AnalysisResult* analysis,
    String8 name)
{
    for (u32 index = 0;
        index < analysis->module.entity_count;
        index += 1)
    {
        AnalysisEntity* entity =
            analysis->module.entities + index;
        if (entity->kind == ANALYSIS_ENTITY_CODE &&
            string_equal(entity->name, name))
        {
            return entity;
        }
    }
    return 0;
}

BUSTER_GLOBAL_LOCAL IrFunction* codegen_test_function_find(
    IrModule* module,
    AnalysisEntityId entity)
{
    for (u32 index = 0; index < module->function_count; index += 1)
    {
        if (module->functions[index].entity.module.value ==
                entity.module.value &&
            module->functions[index].entity.index.value ==
                entity.index.value)
        {
            return module->functions + index;
        }
    }
    return 0;
}

BUSTER_GLOBAL_LOCAL CodegenModuleEntry*
codegen_test_module_entry_find(
    CodegenModule* module,
    AnalysisEntityId entity)
{
    for (u32 index = 0;
        index < module->entry_count;
        index += 1)
    {
        CodegenModuleEntry* entry =
            module->entries + index;
        if (entry->entity.module.value ==
                entity.module.value &&
            entry->entity.index.value ==
                entity.index.value)
        {
            return entry;
        }
    }
    return 0;
}

UnitTestResult codegen_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena temporary =
        arena_begin_temporal(arguments->arena);
    Arena* expression_arena = arena_create((ArenaCreation){0});
    BUSTER_CHECK(expression_arena);
    String8 source_parts[] = { S8(
        "type CodegenPair = struct\n"
        "{\n"
        "    left: s32,\n"
        "    right: s32,\n"
        "}\n"
        "type CodegenNumber = union\n"
        "{\n"
        "    signed_value: s32,\n"
        "    unsigned_value: u32,\n"
        "}\n"
        "type CodegenChoice = enum\n"
        "{\n"
        "    first,\n"
        "    second,\n"
        "}\n"
        "type CodegenAbiPair = struct\n"
        "{\n"
        "    left: s64,\n"
        "    right: s64,\n"
        "}\n"
        "type CodegenAbiMixed = struct\n"
        "{\n"
        "    value: f64,\n"
        "    count: s64,\n"
        "}\n"
        "type CodegenAbiLarge = struct\n"
        "{\n"
        "    first: s64,\n"
        "    second: s64,\n"
        "    third: s64,\n"
        "}\n"
        "type CodegenFloat8 = vector[8]f32\n"
        "type CodegenFloat16 = vector[16]f32\n"
        "code arithmetic : fn (left: s64, right: s64) s64\n"
        "{\n"
        "    data value: s64 = left * 3;\n"
        "    if (left < right)\n"
        "    {\n"
        "        value += right;\n"
        "    }\n"
        "    else\n"
        "    {\n"
        "        value -= right;\n"
        "    }\n"
        "    return value;\n"
        "}\n"
        "code float_arithmetic : fn (left: f64, right: f64) f64\n"
        "{\n"
        "    return -left * 2.0 + right;\n"
        "}\n"
    ), S8(
        "code vector_arithmetic : fn () s32\n"
        "{\n"
        "    data left: vector[4]f32 = [ 1.0, 2.0, 3.0, 4.0 ];\n"
        "    data right: vector[4]f32 = [ 4.0, 3.0, 2.0, 1.0 ];\n"
        "    data sum: vector[4]f32 = left + right;\n"
        "    data negated: vector[4]f32 = -sum;\n"
        "    return @cast(negated[0]);\n"
        "}\n"
        "code vector_identity : fn (value: vector[4]f32) vector[4]f32\n"
        "{\n"
        "    return value;\n"
        "}\n"
        "code vector_integer_arithmetic : fn () s32\n"
        "{\n"
        "    data left: vector[4]s32 = [ 1, 2, 3, 4 ];\n"
        "    data right: vector[4]s32 = [ 4, 3, 2, 1 ];\n"
        "    data masked: vector[4]s32 = (left + right) & [ 7, 7, 7, 7 ];\n"
        "    return masked[0];\n"
        "}\n"
    ), S8(
        "code vector_float_comparison : fn () u32\n"
        "{\n"
        "    data left: vector[4]f32 = [ 1.0, 5.0, -3.0, 8.0 ];\n"
        "    data right: vector[4]f32 = [ 2.0, 4.0, -3.0, 9.0 ];\n"
        "    data mask: vector[4]u32 = left < right;\n"
        "    return mask[0];\n"
        "}\n"
        "code vector_integer_comparison : fn () u32\n"
        "{\n"
        "    data left: vector[4]s32 = [ 1, 5, -3, 8 ];\n"
        "    data right: vector[4]s32 = [ 2, 4, -3, 9 ];\n"
        "    data mask: vector[4]u32 = left > right;\n"
        "    return mask[1];\n"
        "}\n"
        "code vector_256_arithmetic : fn () s32\n"
        "{\n"
        "    data left: CodegenFloat8 = [ 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0 ];\n"
        "    data right: CodegenFloat8 = [ 8.0, 7.0, 6.0, 5.0, 4.0, 3.0, 2.0, 1.0 ];\n"
        "    data sum: CodegenFloat8 = left + right;\n"
        "    return @cast(sum[7]);\n"
        "}\n"
        "code vector_256_identity : fn (value: CodegenFloat8) CodegenFloat8\n"
        "{\n"
        "    return value;\n"
        "}\n"
        "code vector_512_arithmetic : fn () s32\n"
        "{\n"
        "    data left: vector[16]s32 = [ 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 ];\n"
        "    data right: vector[16]s32 = [ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 ];\n"
        "    data sum: vector[16]s32 = left + right;\n"
        "    return sum[15];\n"
        "}\n"
        "code vector_512_identity : fn (value: CodegenFloat16) CodegenFloat16\n"
        "{\n"
        "    return value;\n"
        "}\n"
    ), S8(
        "code pointer_arithmetic : fn () s64\n"
        "{\n"
        "    data value: s64 = 4;\n"
        "    data pointer: &s64 = &value;\n"
        "    pointer.& += 3;\n"
        "    return value;\n"
        "}\n"
        "code straight_arithmetic : fn (left: s64, right: s64) s64\n"
        "{\n"
        "    return left * 3 + right;\n"
        "}\n"
        "code register_pressure : fn (a: s64, b: s64, c: s64, d: s64, e: s64, f: s64, g: s64, h: s64, i: s64) s64\n"
        "{\n"
        "    return a + b + c + d + e + f + g + h + i;\n"
        "}\n"
        "code abi_pair_sum : fn (pair: CodegenAbiPair) s64\n"
        "{\n"
        "    return pair.left + pair.right;\n"
        "}\n"
        "code abi_pair_make : fn (left: s64, right: s64) CodegenAbiPair\n"
        "{\n"
        "    return { .left = left, .right = right };\n"
        "}\n"
        "code abi_pair_round_trip : fn () s64\n"
        "{\n"
        "    data pair: CodegenAbiPair = abi_pair_make(19, 23);\n"
        "    return abi_pair_sum(pair);\n"
        "}\n"
        "code abi_mixed_sum : fn (mixed: CodegenAbiMixed) f64\n"
        "{\n"
        "    return mixed.value + @cast(mixed.count);\n"
        "}\n"
        "code abi_mixed_round_trip : fn () f64\n"
        "{\n"
        "    data mixed: CodegenAbiMixed = { .value = 1.5, .count = 2 };\n"
        "    return abi_mixed_sum(mixed);\n"
        "}\n"
        "code abi_large_make : fn (first: s64, second: s64, third: s64) CodegenAbiLarge\n"
        "{\n"
        "    return { .first = first, .second = second, .third = third };\n"
        "}\n"
        "code abi_large_sum : fn (large: CodegenAbiLarge) s64\n"
        "{\n"
        "    return large.first + large.second + large.third;\n"
        "}\n"
        "code abi_large_round_trip : fn () s64\n"
        "{\n"
        "    data large: CodegenAbiLarge = abi_large_make(5, 7, 11);\n"
        "    return abi_large_sum(large);\n"
        "}\n"
        "code range_sum : fn () s32\n"
        "{\n"
        "    data total: s32 = 0;\n"
        "    for (data value: s32 = 0 .. 4)\n"
        "    {\n"
        "        total += value;\n"
        "    }\n"
        "    for (data value: s32 = @reverse(0 .. 4))\n"
        "    {\n"
        "        total += value;\n"
        "    }\n"
        "    return total;\n"
        "}\n"
        "code union_value : fn () s32\n"
        "{\n"
        "    data number: CodegenNumber = { .signed_value = 17 };\n"
        "    return number.signed_value;\n"
        "}\n"
        "code aggregate_sum : fn () s32\n"
        "{\n"
        "    data values: [3]s32 = [ 2, 3, 4 ];\n"
        "    data selected: []s32 = values[1..];\n"
        "    data pair: CodegenPair = { .left = 2, .right = selected[1] };\n"
        "    pair.left += selected[0];\n"
        "    return pair.left + pair.right;\n"
        "}\n"
        "code collection_sum : fn () s32\n"
        "{\n"
        "    data values: [3]s32 = [ 2, 3, 4 ];\n"
        "    data total: s32 = 0;\n"
        "    for (data value: s32 = values[..])\n"
        "    {\n"
        "        total += value;\n"
        "    }\n"
        "    for (data value: s32 = @reverse(values[..]))\n"
        "    {\n"
        "        total += value;\n"
        "    }\n"
        "    return total;\n"
        "}\n"
        "code add_one : fn (value: s64) s64\n"
        "{\n"
        "    return value + 1;\n"
        "}\n"
        "code call_chain : fn (value: s64) s64\n"
        "{\n"
        "    return add_one(value) * 2;\n"
        "}\n"
        "code sum_seven : fn (a: s64, b: s64, c: s64, d: s64, e: s64, f: s64, g: s64) s64\n"
        "{\n"
        "    return a + b + c + d + e + f + g;\n"
        "}\n"
        "code call_many : fn () s64\n"
        "{\n"
        "    return sum_seven(1, 2, 3, 4, 5, 6, 7);\n"
        "}\n"
        "code integer_to_float : fn (value: s32) f64\n"
        "{\n"
        "    return @cast(value);\n"
        "}\n"
        "code float_to_integer : fn (value: f64) s64\n"
        "{\n"
        "    return @cast(value);\n"
        "}\n"
        "code choose : fn (value: CodegenChoice) s64\n"
        "{\n"
        "    switch (value)\n"
        "    {\n"
        "        .first => { return 11; },\n"
        "        else => { return 22; },\n"
        "    }\n"
        "}\n"
        "code variadic_sum : fn (first: s64, ...) s64\n"
        "{\n"
        "    data arguments = @va_start();\n"
        "    data copy = @va_copy(&arguments);\n"
        "    data second: s64 = @va_arg(&copy, s64);\n"
        "    data third: s64 = @va_arg(&copy, s64);\n"
        "    @va_end(&copy);\n"
        "    @va_end(&arguments);\n"
        "    return first + second + third;\n"
        "}\n"
        "code variadic_call : fn () s64\n"
        "{\n"
        "    return variadic_sum(10, 20, 12);\n"
        "}\n"), S8(
        "code variadic_float : fn (first: s64, ...) f64\n"
        "{\n"
        "    data arguments = @va_start();\n"
        "    data value: f64 = @va_arg(&arguments, f64);\n"
        "    @va_end(&arguments);\n"
        "    return value;\n"
        "}\n"
        "code variadic_float_call : fn () f64\n"
        "{\n"
        "    data value: f32 = 5.25;\n"
        "    return variadic_float(0, value);\n"
        "}\n"
        "code variadic_promoted : fn (...) s64\n"
        "{\n"
        "    data arguments = @va_start();\n"
        "    data value: s32 = @va_arg(&arguments, s32);\n"
        "    @va_end(&arguments);\n"
        "    return @cast(value);\n"
        "}\n"
        "code variadic_promoted_call : fn () s64\n"
        "{\n"
        "    data value: u8 = 42;\n"
        "    return variadic_promoted(value);\n"
        "}\n"
        "code variadic_pair : fn (...) s64\n"
        "{\n"
        "    data arguments = @va_start();\n"
        "    data value: CodegenAbiPair = @va_arg(&arguments, CodegenAbiPair);\n"
        "    @va_end(&arguments);\n"
        "    return value.left + value.right;\n"
        "}\n"
        "code variadic_pair_call : fn () s64\n"
        "{\n"
        "    data value: CodegenAbiPair = { .left = 19, .right = 23 };\n"
        "    return variadic_pair(value);\n"
        "}\n"
        "code variadic_mixed : fn (...) f64\n"
        "{\n"
        "    data arguments = @va_start();\n"
        "    data value: CodegenAbiMixed = @va_arg(&arguments, CodegenAbiMixed);\n"
        "    @va_end(&arguments);\n"
        "    return value.value + @cast(value.count);\n"
        "}\n"
        "code variadic_mixed_call : fn () f64\n"
        "{\n"
        "    data value: CodegenAbiMixed = { .value = 2.25, .count = 3 };\n"
        "    return variadic_mixed(value);\n"
        "}\n"
        "code variadic_large : fn (...) s64\n"
        "{\n"
        "    data arguments = @va_start();\n"
        "    data value: CodegenAbiLarge = @va_arg(&arguments, CodegenAbiLarge);\n"
        "    @va_end(&arguments);\n"
        "    return value.first + value.second + value.third;\n"
        "}\n"
        "code variadic_large_call : fn () s64\n"
        "{\n"
        "    data value: CodegenAbiLarge = { .first = 7, .second = 11, .third = 13 };\n"
        "    return variadic_large(value);\n"
        "}\n"), S8(
        "type CodegenAbiHfa = struct\n"
        "{\n"
        "    first: f64,\n"
        "    second: f64,\n"
        "}\n"
        "code abi_exhaust_float : fn (a: f64, b: f64, c: f64, d: f64, e: f64, f: f64, g: f64, pair: CodegenAbiHfa, tail: f64) f64\n"
        "{\n"
        "    return a + pair.first + tail;\n"
        "}\n"
        "code abi_exhaust_integer : fn (a: s64, b: s64, c: s64, d: s64, e: s64, f: s64, g: s64, pair: CodegenAbiPair, tail: s64) s64\n"
        "{\n"
        "    return a + pair.left + tail;\n"
        "}\n"
        "code variadic_fixed_float : fn (first: f64, ...) f64\n"
        "{\n"
        "    data arguments = @va_start();\n"
        "    @va_end(&arguments);\n"
        "    return first;\n"
        "}\n"
        "code variadic_fixed_float_call : fn () f64\n"
        "{\n"
        "    return variadic_fixed_float(3.5, 1);\n"
        "}\n"
        "code variadic_large_stack : fn (a: s64, b: s64, c: s64, d: s64, e: s64, f: s64, g: s64, h: s64, ...) s64\n"
        "{\n"
        "    data arguments = @va_start();\n"
        "    data value: CodegenAbiLarge = @va_arg(&arguments, CodegenAbiLarge);\n"
        "    @va_end(&arguments);\n"
        "    return value.first + value.second + value.third;\n"
        "}\n"
        "code variadic_large_stack_call : fn () s64\n"
        "{\n"
        "    data value: CodegenAbiLarge = { .first = 7, .second = 11, .third = 13 };\n"
        "    return variadic_large_stack(0, 1, 2, 3, 4, 5, 6, 7, value);\n"
        "}\n") };
    String8 source = string_join_arena(
        arguments->arena,
        (SliceString8)BUSTER_ARRAY_TO_SLICE(source_parts),
        false);
    TokenizerResult tokens = tokenize(
        arguments->arena,
        source.pointer,
        source.length);
    ParserResult parser = parser_parse(
        arguments->arena,
        expression_arena,
        source,
        tokens);
    BUSTER_TEST(arguments, tokens.error_count == 0);
    BUSTER_TEST(arguments, parser.diagnostic_count == 0);
    AnalysisSourceInput input = {
        .path = S8("codegen-x86-64.bbb"),
        .parser = &parser,
    };
    AnalysisResult analysis = analysis_index_module(
        arguments->arena,
        (AnalysisModuleId){ .value = 800 },
        S8("codegen-x86-64"),
        &input,
        1);
    analysis_resolve_module_interfaces(
        arguments->arena,
        &analysis);
    IrModule module = ir_analyze_and_generate_module(
        arguments->arena,
        &analysis);
    BUSTER_TEST(arguments, analysis.diagnostic_count == 0);
    AnalysisEntity* entity =
        codegen_test_entity_find(&analysis, S8("arithmetic"));
    BUSTER_TEST(arguments, entity != 0);
    IrFunction* function = entity ?
        codegen_test_function_find(&module, entity->id) : 0;
    BUSTER_TEST(arguments, function != 0);
    Target target = target_native;
    target.cpu_arch = CPU_ARCH_X86_64;
    Target baseline_target = {
        .cpu_arch = CPU_ARCH_X86_64,
        .cpu_model = CPU_MODEL_BASELINE,
        .os = OPERATING_SYSTEM_LINUX,
    };
    Target avx2_target = baseline_target;
    avx2_target.cpu_features_explicit = true;
    avx2_target.cpu_features =
        TARGET_CPU_FEATURE_X86_SSE2 |
        TARGET_CPU_FEATURE_X86_AVX |
        TARGET_CPU_FEATURE_X86_AVX2;
    Target avx10_target = avx2_target;
    avx10_target.cpu_features |=
        TARGET_CPU_FEATURE_X86_AVX512F |
        TARGET_CPU_FEATURE_X86_AVX512VL |
        TARGET_CPU_FEATURE_X86_AVX10_1 |
        TARGET_CPU_FEATURE_X86_AVX10_2 |
        TARGET_CPU_FEATURE_X86_AVX10_512 |
        TARGET_CPU_FEATURE_X86_APX;
    BUSTER_TEST(arguments,
        target_vector_register_size(
            baseline_target) == 16);
    BUSTER_TEST(arguments,
        target_vector_register_size(
            avx2_target) == 32);
    BUSTER_TEST(arguments,
        target_vector_register_size(
            avx10_target) == 64);
    BUSTER_TEST(arguments,
        target_cpu_feature_has(
            avx10_target,
            TARGET_CPU_FEATURE_X86_APX));
    BUSTER_TEST(arguments,
        target_vector_register_size(
            (Target){
                .cpu_arch = CPU_ARCH_AARCH64,
                .cpu_model = CPU_MODEL_BASELINE,
            }) == 16);
    typedef struct CodegenTargetAbiCase
    {
        CpuArch cpu_arch;
        OperatingSystem os;
        CodegenAbi abi;
    } CodegenTargetAbiCase;
    CodegenTargetAbiCase target_abi_cases[] = {
        { CPU_ARCH_X86_64, OPERATING_SYSTEM_LINUX,
            CODEGEN_ABI_X86_64_SYSTEM_V },
        { CPU_ARCH_X86_64, OPERATING_SYSTEM_MACOS,
            CODEGEN_ABI_X86_64_SYSTEM_V },
        { CPU_ARCH_X86_64, OPERATING_SYSTEM_WINDOWS,
            CODEGEN_ABI_X86_64_WINDOWS },
        { CPU_ARCH_X86_64, OPERATING_SYSTEM_UEFI,
            CODEGEN_ABI_X86_64_WINDOWS },
        { CPU_ARCH_X86_64, OPERATING_SYSTEM_ANDROID,
            CODEGEN_ABI_X86_64_SYSTEM_V },
        { CPU_ARCH_X86_64, OPERATING_SYSTEM_IOS,
            CODEGEN_ABI_X86_64_SYSTEM_V },
        { CPU_ARCH_X86_64, OPERATING_SYSTEM_FREESTANDING,
            CODEGEN_ABI_X86_64_SYSTEM_V },
        { CPU_ARCH_AARCH64, OPERATING_SYSTEM_LINUX,
            CODEGEN_ABI_AARCH64_AAPCS64 },
        { CPU_ARCH_AARCH64, OPERATING_SYSTEM_MACOS,
            CODEGEN_ABI_AARCH64_DARWIN },
        { CPU_ARCH_AARCH64, OPERATING_SYSTEM_WINDOWS,
            CODEGEN_ABI_AARCH64_WINDOWS },
        { CPU_ARCH_AARCH64, OPERATING_SYSTEM_UEFI,
            CODEGEN_ABI_AARCH64_AAPCS64 },
        { CPU_ARCH_AARCH64, OPERATING_SYSTEM_ANDROID,
            CODEGEN_ABI_AARCH64_AAPCS64 },
        { CPU_ARCH_AARCH64, OPERATING_SYSTEM_IOS,
            CODEGEN_ABI_AARCH64_DARWIN },
        { CPU_ARCH_AARCH64, OPERATING_SYSTEM_FREESTANDING,
            CODEGEN_ABI_AARCH64_AAPCS64 },
    };
    for (u32 index = 0;
        index < BUSTER_ARRAY_LENGTH(target_abi_cases);
        index += 1)
    {
        CodegenTargetAbiCase* test =
            target_abi_cases + index;
        BUSTER_TEST(
            arguments,
            codegen_abi_for_target(
                (Target){
                    .cpu_arch = test->cpu_arch,
                    .os = test->os,
                }) == test->abi);
    }
    CodegenFunction generated = function ?
        codegen_generate_function(
            arguments->arena,
            &analysis,
            function,
            target) :
        (CodegenFunction){ .error = CODEGEN_ERROR_INVALID_IR };
    BUSTER_TEST(
        arguments,
        generated.error == CODEGEN_ERROR_NONE);
    BUSTER_TEST(arguments, generated.code.length > 0);
    BUSTER_TEST(arguments, generated.register_value_count > 0);
    BUSTER_TEST(arguments, generated.spilled_value_count > 0);
#if BUSTER_CPU_ARCH_X86_64 && !BUSTER_SANITIZE
    CodegenExecutable executable =
        codegen_make_executable(generated);
    BUSTER_TEST(
        arguments,
        executable.error == CODEGEN_ERROR_NONE);
    if (executable.address)
    {
        CodegenTestFunction2* native = 0;
        BUSTER_CT_CHECK(sizeof(native) == sizeof(executable.address));
        memcpy(
            &native,
            &executable.address,
            sizeof(native));
        u64 first = native(2, 5);
        u64 second = native(7, 3);
        AnalysisResult* analysis_modules[] = { &analysis };
        AnalysisProgram analysis_program = {
            .module_results = analysis_modules,
            .module_count = 1,
        };
        IrProgram ir_program = {
            .modules = &module,
            .module_count = 1,
        };
        IrExecutionArgument first_arguments[] = {
            { .bits = 2 },
            { .bits = 5 },
        };
        IrExecutionArgument second_arguments[] = {
            { .bits = 7 },
            { .bits = 3 },
        };
        IrExecutionResult first_interpreted = ir_execute(
            expression_arena,
            &analysis_program,
            &ir_program,
            entity->id,
            ANALYSIS_INSTANTIATION_ID_INVALID,
            first_arguments,
            BUSTER_ARRAY_LENGTH(first_arguments),
            (IrExecutionOptions){0});
        IrExecutionResult second_interpreted = ir_execute(
            expression_arena,
            &analysis_program,
            &ir_program,
            entity->id,
            ANALYSIS_INSTANTIATION_ID_INVALID,
            second_arguments,
            BUSTER_ARRAY_LENGTH(second_arguments),
            (IrExecutionOptions){0});
        BUSTER_TEST(
            arguments,
            first_interpreted.trap == IR_EXECUTION_TRAP_NONE);
        BUSTER_TEST(
            arguments,
            second_interpreted.trap == IR_EXECUTION_TRAP_NONE);
        BUSTER_TEST(arguments, first == first_interpreted.bits);
        BUSTER_TEST(arguments, second == second_interpreted.bits);
        codegen_release_executable(executable);
    }
#endif
    if (function)
    {
        CodegenAbiSignature system_v =
            codegen_classify_signature(
                arguments->arena,
                &analysis,
                function->type,
                CODEGEN_ABI_X86_64_SYSTEM_V);
        BUSTER_TEST(arguments, system_v.argument_count == 2);
        BUSTER_TEST(arguments, system_v.arguments != 0);
        if (system_v.arguments && system_v.argument_count >= 2)
        {
            BUSTER_TEST(
                arguments,
                system_v.arguments[0].kind ==
                    CODEGEN_ABI_LOCATION_INTEGER_REGISTER);
            BUSTER_TEST(
                arguments,
                system_v.arguments[0].index == 0);
            BUSTER_TEST(
                arguments,
                system_v.arguments[1].index == 1);
        }
    }
    AnalysisEntity* pair_sum_abi_entity =
        codegen_test_entity_find(&analysis, S8("abi_pair_sum"));
    AnalysisEntity* mixed_sum_abi_entity =
        codegen_test_entity_find(&analysis, S8("abi_mixed_sum"));
    AnalysisEntity* large_make_abi_entity =
        codegen_test_entity_find(&analysis, S8("abi_large_make"));
    IrFunction* pair_sum_abi_function =
        pair_sum_abi_entity ?
            codegen_test_function_find(
                &module,
                pair_sum_abi_entity->id) :
            0;
    IrFunction* mixed_sum_abi_function =
        mixed_sum_abi_entity ?
            codegen_test_function_find(
                &module,
                mixed_sum_abi_entity->id) :
            0;
    IrFunction* large_make_abi_function =
        large_make_abi_entity ?
            codegen_test_function_find(
                &module,
                large_make_abi_entity->id) :
            0;
    BUSTER_TEST(arguments, pair_sum_abi_function != 0);
    BUSTER_TEST(arguments, mixed_sum_abi_function != 0);
    BUSTER_TEST(arguments, large_make_abi_function != 0);
    if (pair_sum_abi_function &&
        mixed_sum_abi_function &&
        large_make_abi_function)
    {
        CodegenAbiSignature pair_system_v =
            codegen_classify_signature(
                arguments->arena,
                &analysis,
                pair_sum_abi_function->type,
                CODEGEN_ABI_X86_64_SYSTEM_V);
        CodegenAbiSignature pair_windows =
            codegen_classify_signature(
                arguments->arena,
                &analysis,
                pair_sum_abi_function->type,
                CODEGEN_ABI_X86_64_WINDOWS);
        CodegenAbiSignature pair_aapcs =
            codegen_classify_signature(
                arguments->arena,
                &analysis,
                pair_sum_abi_function->type,
                CODEGEN_ABI_AARCH64_AAPCS64);
        CodegenAbiSignature pair_windows_aarch64 =
            codegen_classify_signature(
                arguments->arena,
                &analysis,
                pair_sum_abi_function->type,
                CODEGEN_ABI_AARCH64_WINDOWS);
        BUSTER_TEST(arguments, pair_system_v.valid);
        BUSTER_TEST(
            arguments,
            pair_system_v.arguments[0].part_count == 2);
        BUSTER_TEST(
            arguments,
            pair_system_v.arguments[0].parts[0].index == 0);
        BUSTER_TEST(
            arguments,
            pair_system_v.arguments[0].parts[1].index == 1);
        BUSTER_TEST(arguments, pair_windows.valid);
        BUSTER_TEST(arguments, pair_windows.arguments[0].indirect);
        BUSTER_TEST(
            arguments,
            pair_windows.arguments[0]
                .indirect_copy_offset >= 32);
        BUSTER_TEST(arguments, pair_aapcs.valid);
        BUSTER_TEST(
            arguments,
            pair_aapcs.arguments[0].part_count == 2);
        BUSTER_TEST(arguments, pair_windows_aarch64.valid);
        BUSTER_TEST(
            arguments,
            pair_windows_aarch64.arguments[0]
                .part_count == 2);
        CodegenAbiSignature mixed_system_v =
            codegen_classify_signature(
                arguments->arena,
                &analysis,
                mixed_sum_abi_function->type,
                CODEGEN_ABI_X86_64_SYSTEM_V);
        BUSTER_TEST(arguments, mixed_system_v.valid);
        BUSTER_TEST(arguments, mixed_system_v.argument_count == 1);
        BUSTER_TEST(arguments, mixed_system_v.arguments != 0);
        if (mixed_system_v.arguments &&
            mixed_system_v.argument_count >= 1)
        {
            BUSTER_TEST(
                arguments,
                mixed_system_v.arguments[0].part_count == 2);
        }
        if (mixed_system_v.arguments &&
            mixed_system_v.argument_count >= 1 &&
            mixed_system_v.arguments[0].part_count >= 2)
        {
            BUSTER_TEST(
                arguments,
                mixed_system_v.arguments[0].parts[0].kind ==
                    CODEGEN_ABI_LOCATION_FLOAT_REGISTER);
            BUSTER_TEST(
                arguments,
                mixed_system_v.arguments[0].parts[1].kind ==
                    CODEGEN_ABI_LOCATION_INTEGER_REGISTER);
        }
        CodegenAbiSignature large_system_v =
            codegen_classify_signature(
                arguments->arena,
                &analysis,
                large_make_abi_function->type,
                CODEGEN_ABI_X86_64_SYSTEM_V);
        CodegenAbiSignature large_windows =
            codegen_classify_signature(
                arguments->arena,
                &analysis,
                large_make_abi_function->type,
                CODEGEN_ABI_X86_64_WINDOWS);
        CodegenAbiSignature large_aapcs =
            codegen_classify_signature(
                arguments->arena,
                &analysis,
                large_make_abi_function->type,
                CODEGEN_ABI_AARCH64_AAPCS64);
        BUSTER_TEST(arguments, large_system_v.result.indirect);
        BUSTER_TEST(
            arguments,
            large_system_v.indirect_result_register == 0);
        BUSTER_TEST(
            arguments,
            large_system_v.arguments[0].index == 1);
        BUSTER_TEST(arguments, large_windows.result.indirect);
        BUSTER_TEST(
            arguments,
            large_windows.indirect_result_register == 0);
        BUSTER_TEST(
            arguments,
            large_windows.arguments[0].index == 1);
        BUSTER_TEST(arguments, large_aapcs.result.indirect);
        BUSTER_TEST(
            arguments,
            large_aapcs.indirect_result_register == 8);
        BUSTER_TEST(
            arguments,
            large_aapcs.arguments[0].index == 0);
    }

    AnalysisEntity* exhaust_float_entity =
        codegen_test_entity_find(
            &analysis,
            S8("abi_exhaust_float"));
    AnalysisEntity* exhaust_integer_entity =
        codegen_test_entity_find(
            &analysis,
            S8("abi_exhaust_integer"));
    IrFunction* exhaust_float_function =
        exhaust_float_entity ?
            codegen_test_function_find(
                &module,
                exhaust_float_entity->id) :
            0;
    IrFunction* exhaust_integer_function =
        exhaust_integer_entity ?
            codegen_test_function_find(
                &module,
                exhaust_integer_entity->id) :
            0;
    BUSTER_TEST(arguments, exhaust_float_function != 0);
    BUSTER_TEST(arguments, exhaust_integer_function != 0);
    if (exhaust_float_function && exhaust_integer_function)
    {
        CodegenAbiSignature exhaust_float =
            codegen_classify_signature(
                arguments->arena,
                &analysis,
                exhaust_float_function->type,
                CODEGEN_ABI_AARCH64_AAPCS64);
        CodegenAbiSignature exhaust_integer =
            codegen_classify_signature(
                arguments->arena,
                &analysis,
                exhaust_integer_function->type,
                CODEGEN_ABI_AARCH64_WINDOWS);
        BUSTER_TEST(arguments, exhaust_float.valid);
        BUSTER_TEST(arguments, exhaust_float.argument_count == 9);
        BUSTER_TEST(
            arguments,
            exhaust_float.arguments[7].kind ==
                CODEGEN_ABI_LOCATION_STACK);
        BUSTER_TEST(
            arguments,
            exhaust_float.arguments[8].kind ==
                CODEGEN_ABI_LOCATION_STACK);
        BUSTER_TEST(arguments, exhaust_integer.valid);
        BUSTER_TEST(
            arguments,
            exhaust_integer.arguments[7].kind ==
                CODEGEN_ABI_LOCATION_STACK);
        BUSTER_TEST(
            arguments,
            exhaust_integer.arguments[8].kind ==
                CODEGEN_ABI_LOCATION_STACK);
    }

    AnalysisEntity* variadic_float_abi_entity =
        codegen_test_entity_find(
            &analysis,
            S8("variadic_float"));
    IrFunction* variadic_float_abi_function =
        variadic_float_abi_entity ?
            codegen_test_function_find(
                &module,
                variadic_float_abi_entity->id) :
            0;
    BUSTER_TEST(arguments, variadic_float_abi_function != 0);
    if (variadic_float_abi_function)
    {
        AnalysisTypeId variadic_argument_types[] = {
            analysis.types.builtin.s64_type,
            analysis.types.builtin.f64_type,
        };
        CodegenAbiSignature windows_aarch64_variadic =
            codegen_classify_signature_with_arguments(
                arguments->arena,
                &analysis,
                variadic_float_abi_function->type,
                variadic_argument_types,
                BUSTER_ARRAY_LENGTH(
                    variadic_argument_types),
                codegen_target_for_abi(
                    CODEGEN_ABI_AARCH64_WINDOWS));
        BUSTER_TEST(arguments, windows_aarch64_variadic.valid);
        BUSTER_TEST(
            arguments,
            windows_aarch64_variadic.argument_count == 2);
        BUSTER_TEST(
            arguments,
            windows_aarch64_variadic.arguments[0].kind ==
                CODEGEN_ABI_LOCATION_INTEGER_REGISTER);
        BUSTER_TEST(
            arguments,
            windows_aarch64_variadic.arguments[1].kind ==
                CODEGEN_ABI_LOCATION_INTEGER_REGISTER);
        BUSTER_TEST(
            arguments,
            windows_aarch64_variadic.arguments[1].index == 1);
    }

    AnalysisType* vector_128 = 0;
    AnalysisType* vector_256 = 0;
    AnalysisType* vector_512 = 0;
    for (u32 type_index = 0;
        type_index < analysis.types.count;
        type_index += 1)
    {
        AnalysisType* candidate =
            analysis.types.types + type_index;
        if (candidate->kind != ANALYSIS_TYPE_VECTOR)
        {
            continue;
        }
        if (candidate->layout.size == 16)
        {
            vector_128 = candidate;
        }
        else if (candidate->layout.size == 32)
        {
            vector_256 = candidate;
        }
        else if (candidate->layout.size == 64)
        {
            vector_512 = candidate;
        }
    }
    BUSTER_TEST(arguments, vector_128 != 0);
    BUSTER_TEST(arguments, vector_256 != 0);
    BUSTER_TEST(arguments, vector_512 != 0);
    if (vector_128 && vector_256 && vector_512)
    {
        AnalysisAbiValue systemv_128 =
            analysis_abi_value_classify(
                arguments->arena,
                &analysis,
                vector_128->id,
                ANALYSIS_ABI_CONVENTION_SYSTEMV_X86_64,
                false);
        AnalysisAbiValue systemv_256 =
            analysis_abi_value_classify(
                arguments->arena,
                &analysis,
                vector_256->id,
                ANALYSIS_ABI_CONVENTION_SYSTEMV_X86_64,
                false);
        AnalysisAbiValue systemv_512 =
            analysis_abi_value_classify(
                arguments->arena,
                &analysis,
                vector_512->id,
                ANALYSIS_ABI_CONVENTION_SYSTEMV_X86_64,
                false);
        BUSTER_TEST(arguments,
            systemv_128.part_count == 1 &&
            systemv_128.parts[0].abi_class ==
                ANALYSIS_ABI_CLASS_VECTOR &&
            systemv_128.parts[0].size == 16);
        BUSTER_TEST(arguments,
            systemv_256.part_count == 1 &&
            systemv_256.parts[0].abi_class ==
                ANALYSIS_ABI_CLASS_VECTOR &&
            systemv_256.parts[0].size == 32);
        BUSTER_TEST(arguments,
            systemv_512.part_count == 1 &&
            systemv_512.parts[0].abi_class ==
                ANALYSIS_ABI_CLASS_VECTOR &&
            systemv_512.parts[0].size == 64);
        AnalysisAbiValue systemv_variadic_256 =
            analysis_abi_value_classify_variadic_argument(
                arguments->arena,
                &analysis,
                vector_256->id,
                ANALYSIS_ABI_CONVENTION_SYSTEMV_X86_64);
        BUSTER_TEST(arguments,
            systemv_variadic_256.parts[0].location ==
                ANALYSIS_ABI_LOCATION_STACK);
        AnalysisAbiValue windows_128 =
            analysis_abi_value_classify(
                arguments->arena,
                &analysis,
                vector_128->id,
                ANALYSIS_ABI_CONVENTION_WIN64_X86_64,
                false);
        BUSTER_TEST(arguments,
            windows_128.indirect &&
            windows_128.parts[0].abi_class ==
                ANALYSIS_ABI_CLASS_POINTER);
        AnalysisAbiValue aapcs_128 =
            analysis_abi_value_classify(
                arguments->arena,
                &analysis,
                vector_128->id,
                ANALYSIS_ABI_CONVENTION_AAPCS64,
                false);
        BUSTER_TEST(arguments,
            !aapcs_128.indirect &&
            aapcs_128.parts[0].abi_class ==
                ANALYSIS_ABI_CLASS_VECTOR);
        AnalysisAbiValue aapcs_256 =
            analysis_abi_value_classify(
                arguments->arena,
                &analysis,
                vector_256->id,
                ANALYSIS_ABI_CONVENTION_AAPCS64,
                false);
        BUSTER_TEST(arguments,
            aapcs_256.indirect &&
            aapcs_256.parts[0].abi_class ==
                ANALYSIS_ABI_CLASS_POINTER);

        String8 identity_names[] = {
            S8_INITIALIZER("vector_256_identity"),
            S8_INITIALIZER("vector_512_identity"),
        };
        Target identity_split_targets[] = {
            baseline_target,
            avx2_target,
        };
        Target identity_native_targets[] = {
            avx2_target,
            avx10_target,
        };
        for (u32 identity_index = 0;
            identity_index <
                BUSTER_ARRAY_LENGTH(identity_names);
            identity_index += 1)
        {
            AnalysisEntity* identity_entity =
                codegen_test_entity_find(
                    &analysis,
                    identity_names[identity_index]);
            BUSTER_TEST(arguments, identity_entity != 0);
            if (!identity_entity)
            {
                continue;
            }
            AnalysisTypeId identity_type =
                analysis.module.semantics[
                    identity_entity->id.index.value].type;
            AnalysisFunctionAbi split_abi =
                analysis_classify_function_abi(
                    arguments->arena,
                    &analysis,
                    identity_type,
                    identity_split_targets[
                        identity_index]);
            AnalysisFunctionAbi native_abi =
                analysis_classify_function_abi(
                    arguments->arena,
                    &analysis,
                    identity_type,
                    identity_native_targets[
                        identity_index]);
            BUSTER_TEST(arguments,
                split_abi.result.indirect);
            BUSTER_TEST(arguments,
                split_abi.arguments[0].parts[0].location ==
                    ANALYSIS_ABI_LOCATION_STACK);
            BUSTER_TEST(arguments,
                !native_abi.result.indirect);
            BUSTER_TEST(arguments,
                native_abi.result.parts[0].abi_class ==
                    ANALYSIS_ABI_CLASS_VECTOR);
            BUSTER_TEST(arguments,
                native_abi.arguments[0].parts[0].location ==
                    ANALYSIS_ABI_LOCATION_REGISTER);
        }
    }

    AnalysisEntity* float_entity =
        codegen_test_entity_find(
            &analysis,
            S8("float_arithmetic"));
    BUSTER_TEST(arguments, float_entity != 0);
    IrFunction* float_function = float_entity ?
        codegen_test_function_find(
            &module,
            float_entity->id) :
        0;
    BUSTER_TEST(arguments, float_function != 0);
    CodegenFunction float_generated = float_function ?
        codegen_generate_function(
            arguments->arena,
            &analysis,
            float_function,
            target) :
        (CodegenFunction){
            .error = CODEGEN_ERROR_INVALID_IR,
        };
    BUSTER_TEST(
        arguments,
        float_generated.error == CODEGEN_ERROR_NONE);
#if BUSTER_CPU_ARCH_X86_64 && !BUSTER_SANITIZE
    CodegenExecutable float_executable =
        codegen_make_executable(float_generated);
    BUSTER_TEST(
        arguments,
        float_executable.error == CODEGEN_ERROR_NONE);
    if (float_executable.address)
    {
        CodegenTestFloatFunction2* native_float = 0;
        BUSTER_CT_CHECK(
            sizeof(native_float) ==
            sizeof(float_executable.address));
        memcpy(
            &native_float,
            &float_executable.address,
            sizeof(native_float));
        f64 native_value = native_float(3.0, 1.5);
        AnalysisResult* float_analysis_modules[] = {
            &analysis,
        };
        AnalysisProgram float_analysis_program = {
            .module_results = float_analysis_modules,
            .module_count = 1,
        };
        IrProgram float_ir_program = {
            .modules = &module,
            .module_count = 1,
        };
        u64 left_bits = 0;
        u64 right_bits = 0;
        f64 left = 3.0;
        f64 right = 1.5;
        memcpy(&left_bits, &left, sizeof(left_bits));
        memcpy(&right_bits, &right, sizeof(right_bits));
        IrExecutionArgument float_arguments[] = {
            { .bits = left_bits },
            { .bits = right_bits },
        };
        IrExecutionResult interpreted_float = ir_execute(
            expression_arena,
            &float_analysis_program,
            &float_ir_program,
            float_entity->id,
            ANALYSIS_INSTANTIATION_ID_INVALID,
            float_arguments,
            BUSTER_ARRAY_LENGTH(float_arguments),
            (IrExecutionOptions){0});
        f64 interpreted_value = 0.0;
        memcpy(
            &interpreted_value,
            &interpreted_float.bits,
            sizeof(interpreted_value));
        BUSTER_TEST(
            arguments,
            interpreted_float.trap ==
                IR_EXECUTION_TRAP_NONE);
        BUSTER_TEST(
            arguments,
            native_value == interpreted_value);
        codegen_release_executable(float_executable);
    }
#endif

    AnalysisEntity* pointer_entity =
        codegen_test_entity_find(
            &analysis,
            S8("pointer_arithmetic"));
    BUSTER_TEST(arguments, pointer_entity != 0);
    IrFunction* pointer_function = pointer_entity ?
        codegen_test_function_find(
            &module,
            pointer_entity->id) :
        0;
    BUSTER_TEST(arguments, pointer_function != 0);
    CodegenFunction pointer_generated = pointer_function ?
        codegen_generate_function(
            arguments->arena,
            &analysis,
            pointer_function,
            target) :
        (CodegenFunction){
            .error = CODEGEN_ERROR_INVALID_IR,
        };
    BUSTER_TEST(
        arguments,
        pointer_generated.error == CODEGEN_ERROR_NONE);
#if BUSTER_CPU_ARCH_X86_64 && !BUSTER_SANITIZE
    CodegenExecutable pointer_executable =
        codegen_make_executable(pointer_generated);
    BUSTER_TEST(
        arguments,
        pointer_executable.error == CODEGEN_ERROR_NONE);
    if (pointer_executable.address)
    {
        CodegenTestFunction0* native_pointer = 0;
        BUSTER_CT_CHECK(
            sizeof(native_pointer) ==
            sizeof(pointer_executable.address));
        memcpy(
            &native_pointer,
            &pointer_executable.address,
            sizeof(native_pointer));
        BUSTER_TEST(arguments, native_pointer() == 7);
        codegen_release_executable(pointer_executable);
    }
#endif

    AnalysisEntity* straight_entity =
        codegen_test_entity_find(
            &analysis,
            S8("straight_arithmetic"));
    BUSTER_TEST(arguments, straight_entity != 0);
    IrFunction* straight_function = straight_entity ?
        codegen_test_function_find(
            &module,
            straight_entity->id) :
        0;
    BUSTER_TEST(arguments, straight_function != 0);
    Target aarch64_target = target;
    aarch64_target.cpu_arch = CPU_ARCH_AARCH64;
    aarch64_target.cpu_features_explicit = true;
    aarch64_target.cpu_features =
        TARGET_CPU_FEATURE_AARCH64_NEON;
    CodegenFunction aarch64_generated = straight_function ?
        codegen_generate_function(
            arguments->arena,
            &analysis,
            straight_function,
            aarch64_target) :
        (CodegenFunction){
            .error = CODEGEN_ERROR_INVALID_IR,
        };
    BUSTER_TEST(
        arguments,
        aarch64_generated.error == CODEGEN_ERROR_NONE);
    BUSTER_TEST(
        arguments,
        aarch64_generated.code.length >= 4);
    BUSTER_TEST(
        arguments,
        aarch64_generated.register_value_count > 0);
    AnalysisEntity* pressure_entity =
        codegen_test_entity_find(
            &analysis,
            S8("register_pressure"));
    IrFunction* pressure_function = pressure_entity ?
        codegen_test_function_find(
            &module,
            pressure_entity->id) :
        0;
    BUSTER_TEST(arguments, pressure_function != 0);
    CodegenFunction x86_64_pressure_generated =
        pressure_function ?
            codegen_generate_function(
                arguments->arena,
                &analysis,
                pressure_function,
                target) :
            (CodegenFunction){
                .error = CODEGEN_ERROR_INVALID_IR,
            };
    CodegenFunction aarch64_pressure_generated =
        pressure_function ?
            codegen_generate_function(
                arguments->arena,
                &analysis,
                pressure_function,
                aarch64_target) :
            (CodegenFunction){
                .error = CODEGEN_ERROR_INVALID_IR,
            };
    BUSTER_TEST(
        arguments,
        x86_64_pressure_generated.error ==
            CODEGEN_ERROR_NONE);
    BUSTER_TEST(
        arguments,
        x86_64_pressure_generated.register_value_count > 0);
    BUSTER_TEST(
        arguments,
        x86_64_pressure_generated.spilled_value_count > 0);
    BUSTER_TEST(
        arguments,
        aarch64_pressure_generated.error ==
            CODEGEN_ERROR_NONE);
    BUSTER_TEST(
        arguments,
        aarch64_pressure_generated.register_value_count > 0);
    BUSTER_TEST(
        arguments,
        aarch64_pressure_generated.spilled_value_count > 0);
    CodegenFunction aarch64_cfg_generated =
        function ?
            codegen_generate_function(
                arguments->arena,
                &analysis,
                function,
                aarch64_target) :
            (CodegenFunction){
                .error = CODEGEN_ERROR_INVALID_IR,
            };
    BUSTER_TEST(
        arguments,
        aarch64_cfg_generated.error ==
            CODEGEN_ERROR_NONE);
    CodegenFunction aarch64_float_generated =
        float_function ?
            codegen_generate_function(
                arguments->arena,
                &analysis,
                float_function,
                aarch64_target) :
            (CodegenFunction){
                .error = CODEGEN_ERROR_INVALID_IR,
            };
    BUSTER_TEST(
        arguments,
        aarch64_float_generated.error ==
            CODEGEN_ERROR_NONE);
#if BUSTER_CPU_ARCH_AARCH64 && !BUSTER_SANITIZE
    CodegenExecutable aarch64_executable =
        codegen_make_executable(aarch64_generated);
    BUSTER_TEST(
        arguments,
        aarch64_executable.error == CODEGEN_ERROR_NONE);
    if (aarch64_executable.address)
    {
        CodegenTestFunction2* native_aarch64 = 0;
        BUSTER_CT_CHECK(
            sizeof(native_aarch64) ==
            sizeof(aarch64_executable.address));
        memcpy(
            &native_aarch64,
            &aarch64_executable.address,
            sizeof(native_aarch64));
        BUSTER_TEST(
            arguments,
            native_aarch64(2, 5) == 11);
        codegen_release_executable(aarch64_executable);
    }
#endif

    AnalysisEntity* range_entity =
        codegen_test_entity_find(
            &analysis,
            S8("range_sum"));
    BUSTER_TEST(arguments, range_entity != 0);
    IrFunction* range_function = range_entity ?
        codegen_test_function_find(
            &module,
            range_entity->id) :
        0;
    BUSTER_TEST(arguments, range_function != 0);
    CodegenFunction range_generated = range_function ?
        codegen_generate_function(
            arguments->arena,
            &analysis,
            range_function,
            target) :
        (CodegenFunction){
            .error = CODEGEN_ERROR_INVALID_IR,
        };
    BUSTER_TEST(
        arguments,
        range_generated.error == CODEGEN_ERROR_NONE);
    CodegenFunction aarch64_range_generated =
        range_function ?
            codegen_generate_function(
                arguments->arena,
                &analysis,
                range_function,
                aarch64_target) :
            (CodegenFunction){
                .error = CODEGEN_ERROR_INVALID_IR,
            };
    BUSTER_TEST(
        arguments,
        aarch64_range_generated.error ==
            CODEGEN_ERROR_NONE);
#if BUSTER_CPU_ARCH_AARCH64 && !BUSTER_SANITIZE
    CodegenExecutable aarch64_range_executable =
        codegen_make_executable(aarch64_range_generated);
    BUSTER_TEST(
        arguments,
        aarch64_range_executable.error ==
            CODEGEN_ERROR_NONE);
    if (aarch64_range_executable.address)
    {
        CodegenTestFunction0* native_aarch64_range = 0;
        memcpy(
            &native_aarch64_range,
            &aarch64_range_executable.address,
            sizeof(native_aarch64_range));
        BUSTER_TEST(
            arguments,
            native_aarch64_range() == 12);
        codegen_release_executable(
            aarch64_range_executable);
    }
#endif
#if BUSTER_CPU_ARCH_X86_64 && !BUSTER_SANITIZE
    CodegenExecutable range_executable =
        codegen_make_executable(range_generated);
    BUSTER_TEST(
        arguments,
        range_executable.error == CODEGEN_ERROR_NONE);
    if (range_executable.address)
    {
        CodegenTestFunction0* native_range = 0;
        BUSTER_CT_CHECK(
            sizeof(native_range) ==
            sizeof(range_executable.address));
        memcpy(
            &native_range,
            &range_executable.address,
            sizeof(native_range));
        BUSTER_TEST(arguments, native_range() == 12);
        codegen_release_executable(range_executable);
    }
#endif
    AnalysisEntity* aggregate_entity =
        codegen_test_entity_find(
            &analysis,
            S8("aggregate_sum"));
    BUSTER_TEST(arguments, aggregate_entity != 0);
    IrFunction* aggregate_function = aggregate_entity ?
        codegen_test_function_find(
            &module,
            aggregate_entity->id) :
        0;
    BUSTER_TEST(arguments, aggregate_function != 0);
    CodegenFunction aggregate_generated =
        aggregate_function ?
            codegen_generate_function(
                arguments->arena,
                &analysis,
                aggregate_function,
                target) :
            (CodegenFunction){
                .error = CODEGEN_ERROR_INVALID_IR,
            };
    BUSTER_TEST(
        arguments,
        aggregate_generated.error == CODEGEN_ERROR_NONE);
    CodegenFunction aarch64_aggregate_generated =
        aggregate_function ?
            codegen_generate_function(
                arguments->arena,
                &analysis,
                aggregate_function,
                aarch64_target) :
            (CodegenFunction){
                .error = CODEGEN_ERROR_INVALID_IR,
            };
    BUSTER_TEST(
        arguments,
        aarch64_aggregate_generated.error ==
            CODEGEN_ERROR_NONE);
#if BUSTER_CPU_ARCH_AARCH64 && !BUSTER_SANITIZE
    CodegenExecutable aarch64_aggregate_executable =
        codegen_make_executable(
            aarch64_aggregate_generated);
    BUSTER_TEST(
        arguments,
        aarch64_aggregate_executable.error ==
            CODEGEN_ERROR_NONE);
    if (aarch64_aggregate_executable.address)
    {
        CodegenTestFunction0* native_aarch64_aggregate = 0;
        memcpy(
            &native_aarch64_aggregate,
            &aarch64_aggregate_executable.address,
            sizeof(native_aarch64_aggregate));
        BUSTER_TEST(
            arguments,
            native_aarch64_aggregate() == 9);
        codegen_release_executable(
            aarch64_aggregate_executable);
    }
#endif
#if BUSTER_CPU_ARCH_X86_64 && !BUSTER_SANITIZE
    CodegenExecutable aggregate_executable =
        codegen_make_executable(aggregate_generated);
    BUSTER_TEST(
        arguments,
        aggregate_executable.error == CODEGEN_ERROR_NONE);
    if (aggregate_executable.address)
    {
        CodegenTestFunction0* native_aggregate = 0;
        BUSTER_CT_CHECK(
            sizeof(native_aggregate) ==
            sizeof(aggregate_executable.address));
        memcpy(
            &native_aggregate,
            &aggregate_executable.address,
            sizeof(native_aggregate));
        BUSTER_TEST(arguments, native_aggregate() == 9);
        codegen_release_executable(aggregate_executable);
    }
#endif
    AnalysisEntity* union_entity =
        codegen_test_entity_find(
            &analysis,
            S8("union_value"));
    IrFunction* union_function = union_entity ?
        codegen_test_function_find(
            &module,
            union_entity->id) :
        0;
    BUSTER_TEST(arguments, union_function != 0);
    CodegenFunction union_generated = union_function ?
        codegen_generate_function(
            arguments->arena,
            &analysis,
            union_function,
            target) :
        (CodegenFunction){
            .error = CODEGEN_ERROR_INVALID_IR,
        };
    CodegenFunction aarch64_union_generated =
        union_function ?
            codegen_generate_function(
                arguments->arena,
                &analysis,
                union_function,
                aarch64_target) :
            (CodegenFunction){
                .error = CODEGEN_ERROR_INVALID_IR,
            };
    BUSTER_TEST(
        arguments,
        union_generated.error == CODEGEN_ERROR_NONE);
    BUSTER_TEST(
        arguments,
        aarch64_union_generated.error ==
            CODEGEN_ERROR_NONE);
#if BUSTER_CPU_ARCH_X86_64 && !BUSTER_SANITIZE
    CodegenExecutable union_executable =
        codegen_make_executable(union_generated);
    BUSTER_TEST(
        arguments,
        union_executable.error == CODEGEN_ERROR_NONE);
    if (union_executable.address)
    {
        CodegenTestFunction0* native_union = 0;
        memcpy(
            &native_union,
            &union_executable.address,
            sizeof(native_union));
        BUSTER_TEST(arguments, native_union() == 17);
        codegen_release_executable(union_executable);
    }
#endif
#if BUSTER_CPU_ARCH_AARCH64 && !BUSTER_SANITIZE
    CodegenExecutable aarch64_union_executable =
        codegen_make_executable(aarch64_union_generated);
    BUSTER_TEST(
        arguments,
        aarch64_union_executable.error ==
            CODEGEN_ERROR_NONE);
    if (aarch64_union_executable.address)
    {
        CodegenTestFunction0* native_aarch64_union = 0;
        memcpy(
            &native_aarch64_union,
            &aarch64_union_executable.address,
            sizeof(native_aarch64_union));
        BUSTER_TEST(
            arguments,
            native_aarch64_union() == 17);
        codegen_release_executable(
            aarch64_union_executable);
    }
#endif
    AnalysisEntity* vector_entity =
        codegen_test_entity_find(
            &analysis,
            S8("vector_arithmetic"));
    IrFunction* vector_function = vector_entity ?
        codegen_test_function_find(
            &module,
            vector_entity->id) :
        0;
    BUSTER_TEST(arguments, vector_function != 0);
    CodegenFunction vector_generated =
        vector_function ?
            codegen_generate_function(
                arguments->arena,
                &analysis,
                vector_function,
                target) :
            (CodegenFunction){
                .error = CODEGEN_ERROR_INVALID_IR,
            };
    CodegenFunction aarch64_vector_generated =
        vector_function ?
            codegen_generate_function(
                arguments->arena,
                &analysis,
                vector_function,
                aarch64_target) :
            (CodegenFunction){
                .error = CODEGEN_ERROR_INVALID_IR,
            };
    Target aarch64_without_neon = aarch64_target;
    aarch64_without_neon.cpu_features = 0;
    CodegenFunction aarch64_without_neon_generated =
        vector_function ?
            codegen_generate_function(
                arguments->arena,
                &analysis,
                vector_function,
                aarch64_without_neon) :
            (CodegenFunction){
                .error = CODEGEN_ERROR_INVALID_IR,
            };
    BUSTER_TEST(
        arguments,
        vector_generated.error ==
            CODEGEN_ERROR_NONE);
    BUSTER_TEST(
        arguments,
        aarch64_vector_generated.error ==
            CODEGEN_ERROR_NONE);
    BUSTER_TEST(
        arguments,
        aarch64_without_neon_generated.error ==
            CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION);
    BUSTER_TEST(
        arguments,
        vector_generated.register_value_count > 0);
#if BUSTER_CPU_ARCH_X86_64 && !BUSTER_SANITIZE
    CodegenExecutable vector_executable =
        codegen_make_executable(vector_generated);
    BUSTER_TEST(
        arguments,
        vector_executable.error == CODEGEN_ERROR_NONE);
    if (vector_executable.address)
    {
        CodegenTestFunction0* native_vector = 0;
        memcpy(
            &native_vector,
            &vector_executable.address,
            sizeof(native_vector));
        BUSTER_TEST(
            arguments,
            native_vector() == UINT64_MAX - 4);
        codegen_release_executable(vector_executable);
    }
#endif
#if BUSTER_CPU_ARCH_AARCH64 && !BUSTER_SANITIZE
    CodegenExecutable aarch64_vector_executable =
        codegen_make_executable(
            aarch64_vector_generated);
    BUSTER_TEST(
        arguments,
        aarch64_vector_executable.error ==
            CODEGEN_ERROR_NONE);
    if (aarch64_vector_executable.address)
    {
        CodegenTestFunction0* native_aarch64_vector = 0;
        memcpy(
            &native_aarch64_vector,
            &aarch64_vector_executable.address,
            sizeof(native_aarch64_vector));
        BUSTER_TEST(
            arguments,
            native_aarch64_vector() ==
                UINT64_MAX - 4);
        codegen_release_executable(
            aarch64_vector_executable);
    }
#endif
    AnalysisEntity* vector_integer_entity =
        codegen_test_entity_find(
            &analysis,
            S8("vector_integer_arithmetic"));
    IrFunction* vector_integer_function =
        vector_integer_entity ?
            codegen_test_function_find(
                &module,
                vector_integer_entity->id) :
            0;
    BUSTER_TEST(
        arguments,
        vector_integer_function != 0);
    CodegenFunction vector_integer_generated =
        vector_integer_function ?
            codegen_generate_function(
                arguments->arena,
                &analysis,
                vector_integer_function,
                target) :
            (CodegenFunction){
                .error = CODEGEN_ERROR_INVALID_IR,
            };
    CodegenFunction aarch64_vector_integer_generated =
        vector_integer_function ?
            codegen_generate_function(
                arguments->arena,
                &analysis,
                vector_integer_function,
                aarch64_target) :
            (CodegenFunction){
                .error = CODEGEN_ERROR_INVALID_IR,
            };
    BUSTER_TEST(arguments,
        vector_integer_generated.error ==
            CODEGEN_ERROR_NONE);
    BUSTER_TEST(arguments,
        aarch64_vector_integer_generated.error ==
            CODEGEN_ERROR_NONE);
#if BUSTER_CPU_ARCH_X86_64 && !BUSTER_SANITIZE
    CodegenExecutable vector_integer_executable =
        codegen_make_executable(
            vector_integer_generated);
    BUSTER_TEST(arguments,
        vector_integer_executable.error ==
            CODEGEN_ERROR_NONE);
    if (vector_integer_executable.address)
    {
        CodegenTestFunction0* native_vector_integer = 0;
        memcpy(
            &native_vector_integer,
            &vector_integer_executable.address,
            sizeof(native_vector_integer));
        BUSTER_TEST(
            arguments,
            native_vector_integer() == 5);
        codegen_release_executable(
            vector_integer_executable);
    }
#endif
#if BUSTER_CPU_ARCH_AARCH64 && !BUSTER_SANITIZE
    CodegenExecutable aarch64_vector_integer_executable =
        codegen_make_executable(
            aarch64_vector_integer_generated);
    BUSTER_TEST(arguments,
        aarch64_vector_integer_executable.error ==
            CODEGEN_ERROR_NONE);
    if (aarch64_vector_integer_executable.address)
    {
        CodegenTestFunction0*
            native_aarch64_vector_integer = 0;
        memcpy(
            &native_aarch64_vector_integer,
            &aarch64_vector_integer_executable.address,
            sizeof(native_aarch64_vector_integer));
        BUSTER_TEST(
            arguments,
            native_aarch64_vector_integer() == 5);
        codegen_release_executable(
            aarch64_vector_integer_executable);
    }
#endif
    String8 vector_comparison_names[] =
    {
        S8_INITIALIZER("vector_float_comparison"),
        S8_INITIALIZER("vector_integer_comparison"),
    };
    for (u32 comparison_index = 0;
        comparison_index < 2;
        comparison_index += 1)
    {
        AnalysisEntity* comparison_entity =
            codegen_test_entity_find(
                &analysis,
                vector_comparison_names[
                    comparison_index]);
        IrFunction* comparison_function =
            comparison_entity ?
                codegen_test_function_find(
                    &module,
                    comparison_entity->id) :
                0;
        BUSTER_TEST(
            arguments,
            comparison_function != 0);
        CodegenFunction comparison_generated =
            comparison_function ?
                codegen_generate_function(
                    arguments->arena,
                    &analysis,
                    comparison_function,
                    target) :
                (CodegenFunction){
                    .error = CODEGEN_ERROR_INVALID_IR,
                };
        CodegenFunction aarch64_comparison_generated =
            comparison_function ?
                codegen_generate_function(
                    arguments->arena,
                    &analysis,
                    comparison_function,
                    aarch64_target) :
                (CodegenFunction){
                    .error = CODEGEN_ERROR_INVALID_IR,
                };
        BUSTER_TEST(arguments,
            comparison_generated.error ==
                CODEGEN_ERROR_NONE);
        BUSTER_TEST(arguments,
            aarch64_comparison_generated.error ==
                CODEGEN_ERROR_NONE);
#if BUSTER_CPU_ARCH_X86_64 && !BUSTER_SANITIZE
        CodegenExecutable comparison_executable =
            codegen_make_executable(
                comparison_generated);
        BUSTER_TEST(arguments,
            comparison_executable.error ==
                CODEGEN_ERROR_NONE);
        if (comparison_executable.address)
        {
            CodegenTestFunction0* native_comparison = 0;
            memcpy(
                &native_comparison,
                &comparison_executable.address,
                sizeof(native_comparison));
            BUSTER_TEST(
                arguments,
                native_comparison() == UINT32_MAX);
            codegen_release_executable(
                comparison_executable);
        }
#endif
#if BUSTER_CPU_ARCH_AARCH64 && !BUSTER_SANITIZE
        CodegenExecutable aarch64_comparison_executable =
            codegen_make_executable(
                aarch64_comparison_generated);
        BUSTER_TEST(arguments,
            aarch64_comparison_executable.error ==
                CODEGEN_ERROR_NONE);
        if (aarch64_comparison_executable.address)
        {
            CodegenTestFunction0*
                native_aarch64_comparison = 0;
            memcpy(
                &native_aarch64_comparison,
                &aarch64_comparison_executable.address,
                sizeof(native_aarch64_comparison));
            BUSTER_TEST(
                arguments,
                native_aarch64_comparison() ==
                    UINT32_MAX);
            codegen_release_executable(
                aarch64_comparison_executable);
        }
#endif
    }
    String8 wide_vector_names[] =
    {
        S8_INITIALIZER("vector_256_arithmetic"),
        S8_INITIALIZER("vector_512_arithmetic"),
    };
    u64 wide_vector_results[] = { 9, 17 };
    for (u32 wide_index = 0;
        wide_index < 2;
        wide_index += 1)
    {
        AnalysisEntity* wide_entity =
            codegen_test_entity_find(
                &analysis,
                wide_vector_names[wide_index]);
        IrFunction* wide_function = wide_entity ?
            codegen_test_function_find(
                &module,
                wide_entity->id) :
            0;
        BUSTER_TEST(arguments, wide_function != 0);
        CodegenFunction wide_generated = wide_function ?
            codegen_generate_function(
                arguments->arena,
                &analysis,
                wide_function,
                target) :
            (CodegenFunction){
                .error = CODEGEN_ERROR_INVALID_IR,
            };
        CodegenFunction aarch64_wide_generated =
            wide_function ?
                codegen_generate_function(
                    arguments->arena,
                    &analysis,
                    wide_function,
                    aarch64_target) :
                (CodegenFunction){
                    .error = CODEGEN_ERROR_INVALID_IR,
                };
        BUSTER_TEST(
            arguments,
            wide_generated.error ==
                CODEGEN_ERROR_NONE);
        BUSTER_TEST(
            arguments,
            aarch64_wide_generated.error ==
                CODEGEN_ERROR_NONE);
        Target split_target =
            wide_index == 0 ?
                baseline_target : avx2_target;
        Target native_target =
            wide_index == 0 ?
                avx2_target : avx10_target;
        CodegenFunction split_generated =
            wide_function ?
                codegen_generate_function(
                    arguments->arena,
                    &analysis,
                    wide_function,
                    split_target) :
                (CodegenFunction){
                    .error = CODEGEN_ERROR_INVALID_IR,
                };
        CodegenFunction native_generated =
            wide_function ?
                codegen_generate_function(
                    arguments->arena,
                    &analysis,
                    wide_function,
                    native_target) :
                (CodegenFunction){
                    .error = CODEGEN_ERROR_INVALID_IR,
                };
        BUSTER_TEST(
            arguments,
            split_generated.error ==
                CODEGEN_ERROR_NONE);
        BUSTER_TEST(
            arguments,
            split_generated.native_vector_operation_count ==
                0);
        BUSTER_TEST(
            arguments,
            split_generated.split_vector_operation_count >
                0);
        BUSTER_TEST(
            arguments,
            native_generated.error ==
                CODEGEN_ERROR_NONE);
        BUSTER_TEST(
            arguments,
            native_generated.native_vector_operation_count >
                0);
        BUSTER_TEST(
            arguments,
            native_generated.split_vector_operation_count ==
                0);
#if BUSTER_CPU_ARCH_X86_64 && !BUSTER_SANITIZE
        CodegenExecutable wide_executable =
            codegen_make_executable(wide_generated);
        BUSTER_TEST(
            arguments,
            wide_executable.error ==
                CODEGEN_ERROR_NONE);
        if (wide_executable.address)
        {
            CodegenTestFunction0* native_wide = 0;
            memcpy(
                &native_wide,
                &wide_executable.address,
                sizeof(native_wide));
            BUSTER_TEST(
                arguments,
                native_wide() ==
                    wide_vector_results[wide_index]);
            codegen_release_executable(
                wide_executable);
        }
#else
        BUSTER_UNUSED(wide_vector_results);
#endif
    }
    AnalysisEntity* collection_entity =
        codegen_test_entity_find(
            &analysis,
            S8("collection_sum"));
    IrFunction* collection_function = collection_entity ?
        codegen_test_function_find(
            &module,
            collection_entity->id) :
        0;
    BUSTER_TEST(arguments, collection_function != 0);
    CodegenFunction collection_generated =
        collection_function ?
            codegen_generate_function(
                arguments->arena,
                &analysis,
                collection_function,
                target) :
            (CodegenFunction){
                .error = CODEGEN_ERROR_INVALID_IR,
            };
    CodegenFunction aarch64_collection_generated =
        collection_function ?
            codegen_generate_function(
                arguments->arena,
                &analysis,
                collection_function,
                aarch64_target) :
            (CodegenFunction){
                .error = CODEGEN_ERROR_INVALID_IR,
            };
    BUSTER_TEST(
        arguments,
        collection_generated.error ==
            CODEGEN_ERROR_NONE);
    BUSTER_TEST(
        arguments,
        aarch64_collection_generated.error ==
            CODEGEN_ERROR_NONE);
#if BUSTER_CPU_ARCH_X86_64 && !BUSTER_SANITIZE
    CodegenExecutable collection_executable =
        codegen_make_executable(collection_generated);
    BUSTER_TEST(
        arguments,
        collection_executable.error ==
            CODEGEN_ERROR_NONE);
    if (collection_executable.address)
    {
        CodegenTestFunction0* native_collection = 0;
        memcpy(
            &native_collection,
            &collection_executable.address,
            sizeof(native_collection));
        BUSTER_TEST(arguments, native_collection() == 18);
        codegen_release_executable(collection_executable);
    }
#endif
#if BUSTER_CPU_ARCH_AARCH64 && !BUSTER_SANITIZE
    CodegenExecutable aarch64_collection_executable =
        codegen_make_executable(
            aarch64_collection_generated);
    BUSTER_TEST(
        arguments,
        aarch64_collection_executable.error ==
            CODEGEN_ERROR_NONE);
    if (aarch64_collection_executable.address)
    {
        CodegenTestFunction0* native_aarch64_collection = 0;
        memcpy(
            &native_aarch64_collection,
            &aarch64_collection_executable.address,
            sizeof(native_aarch64_collection));
        BUSTER_TEST(
            arguments,
            native_aarch64_collection() == 18);
        codegen_release_executable(
            aarch64_collection_executable);
    }
#endif
    CodegenModule generated_module =
        codegen_generate_module(
            arguments->arena,
            &analysis,
            &module,
            target);
    BUSTER_TEST(
        arguments,
        generated_module.error == CODEGEN_ERROR_NONE);
    Target windows_target = target;
    windows_target.os = OPERATING_SYSTEM_WINDOWS;
    CodegenModule windows_abi_module =
        codegen_generate_module(
            arguments->arena,
            &analysis,
            &module,
            windows_target);
    BUSTER_TEST(
        arguments,
        windows_abi_module.error == CODEGEN_ERROR_NONE);
    CodegenModule aapcs64_abi_module =
        codegen_generate_module(
            arguments->arena,
            &analysis,
            &module,
            aarch64_target);
    BUSTER_TEST(
        arguments,
        aapcs64_abi_module.error == CODEGEN_ERROR_NONE);
    Target darwin_target = aarch64_target;
    darwin_target.os = OPERATING_SYSTEM_MACOS;
    CodegenModule darwin_abi_module =
        codegen_generate_module(
            arguments->arena,
            &analysis,
            &module,
            darwin_target);
    BUSTER_TEST(
        arguments,
        darwin_abi_module.error == CODEGEN_ERROR_NONE);
    Target windows_aarch64_target = aarch64_target;
    windows_aarch64_target.os = OPERATING_SYSTEM_WINDOWS;
    CodegenModule windows_aarch64_abi_module =
        codegen_generate_module(
            arguments->arena,
            &analysis,
            &module,
            windows_aarch64_target);
    BUSTER_TEST(
        arguments,
        windows_aarch64_abi_module.error ==
            CODEGEN_ERROR_NONE);
    AnalysisEntity* caller_entity =
        codegen_test_entity_find(
            &analysis,
            S8("call_chain"));
    BUSTER_TEST(arguments, caller_entity != 0);
    AnalysisEntity* call_many_entity =
        codegen_test_entity_find(
            &analysis,
            S8("call_many"));
    AnalysisEntity* variadic_call_entity =
        codegen_test_entity_find(
            &analysis,
            S8("variadic_call"));
    AnalysisEntity* variadic_float_call_entity =
        codegen_test_entity_find(
            &analysis,
            S8("variadic_float_call"));
    AnalysisEntity* variadic_promoted_call_entity =
        codegen_test_entity_find(
            &analysis,
            S8("variadic_promoted_call"));
    AnalysisEntity* variadic_pair_call_entity =
        codegen_test_entity_find(
            &analysis,
            S8("variadic_pair_call"));
    AnalysisEntity* variadic_mixed_call_entity =
        codegen_test_entity_find(
            &analysis,
            S8("variadic_mixed_call"));
    AnalysisEntity* variadic_large_call_entity =
        codegen_test_entity_find(
            &analysis,
            S8("variadic_large_call"));
    AnalysisEntity* add_one_entity =
        codegen_test_entity_find(
            &analysis,
            S8("add_one"));
    IrFunction* add_one_function = add_one_entity ?
        codegen_test_function_find(
            &module,
            add_one_entity->id) :
        0;
    IrFunction* caller_function = caller_entity ?
        codegen_test_function_find(
            &module,
            caller_entity->id) :
        0;
    BUSTER_TEST(arguments, add_one_function != 0);
    BUSTER_TEST(arguments, caller_function != 0);
    CodegenModuleEntry* caller_entry = 0;
    CodegenModuleEntry* call_many_entry = 0;
    CodegenModuleEntry* variadic_call_entry =
        variadic_call_entity ?
            codegen_test_module_entry_find(
                &generated_module,
                variadic_call_entity->id) :
            0;
    CodegenModuleEntry* variadic_float_call_entry =
        variadic_float_call_entity ?
            codegen_test_module_entry_find(
                &generated_module,
                variadic_float_call_entity->id) :
            0;
    CodegenModuleEntry* variadic_promoted_call_entry =
        variadic_promoted_call_entity ?
            codegen_test_module_entry_find(
                &generated_module,
                variadic_promoted_call_entity->id) :
            0;
    CodegenModuleEntry* variadic_pair_call_entry =
        variadic_pair_call_entity ?
            codegen_test_module_entry_find(
                &generated_module,
                variadic_pair_call_entity->id) :
            0;
    CodegenModuleEntry* variadic_mixed_call_entry =
        variadic_mixed_call_entity ?
            codegen_test_module_entry_find(
                &generated_module,
                variadic_mixed_call_entity->id) :
            0;
    CodegenModuleEntry* variadic_large_call_entry =
        variadic_large_call_entity ?
            codegen_test_module_entry_find(
                &generated_module,
                variadic_large_call_entity->id) :
            0;
    if (caller_entity)
    {
        for (u32 index = 0;
            index < generated_module.entry_count;
            index += 1)
        {
            if (generated_module.entries[index]
                    .entity.module.value ==
                    caller_entity->id.module.value &&
                generated_module.entries[index]
                    .entity.index.value ==
                    caller_entity->id.index.value)
            {
                caller_entry =
                    generated_module.entries + index;
                break;
            }
        }
    }
    if (call_many_entity)
    {
        for (u32 index = 0;
            index < generated_module.entry_count;
            index += 1)
        {
            if (generated_module.entries[index]
                    .entity.module.value ==
                    call_many_entity->id.module.value &&
                generated_module.entries[index]
                    .entity.index.value ==
                    call_many_entity->id.index.value)
            {
                call_many_entry =
                    generated_module.entries + index;
                break;
            }
        }
    }
    BUSTER_TEST(arguments, caller_entry != 0);
    BUSTER_TEST(arguments, call_many_entry != 0);
    BUSTER_TEST(arguments, variadic_call_entry != 0);
    BUSTER_TEST(arguments, variadic_float_call_entry != 0);
    BUSTER_TEST(arguments, variadic_promoted_call_entry != 0);
    BUSTER_TEST(arguments, variadic_pair_call_entry != 0);
    BUSTER_TEST(arguments, variadic_mixed_call_entry != 0);
    BUSTER_TEST(arguments, variadic_large_call_entry != 0);
    AnalysisEntity* integer_to_float_entity =
        codegen_test_entity_find(
            &analysis,
            S8("integer_to_float"));
    AnalysisEntity* float_to_integer_entity =
        codegen_test_entity_find(
            &analysis,
            S8("float_to_integer"));
    AnalysisEntity* choose_entity =
        codegen_test_entity_find(
            &analysis,
            S8("choose"));
    CodegenModuleEntry* integer_to_float_entry =
        integer_to_float_entity ?
            codegen_test_module_entry_find(
                &generated_module,
                integer_to_float_entity->id) :
            0;
    CodegenModuleEntry* float_to_integer_entry =
        float_to_integer_entity ?
            codegen_test_module_entry_find(
                &generated_module,
                float_to_integer_entity->id) :
            0;
    CodegenModuleEntry* choose_entry = choose_entity ?
        codegen_test_module_entry_find(
            &generated_module,
            choose_entity->id) :
        0;
    AnalysisEntity* abi_pair_round_trip_entity =
        codegen_test_entity_find(
            &analysis,
            S8("abi_pair_round_trip"));
    AnalysisEntity* abi_mixed_round_trip_entity =
        codegen_test_entity_find(
            &analysis,
            S8("abi_mixed_round_trip"));
    AnalysisEntity* abi_large_round_trip_entity =
        codegen_test_entity_find(
            &analysis,
            S8("abi_large_round_trip"));
    AnalysisEntity* abi_pair_sum_entity =
        codegen_test_entity_find(&analysis, S8("abi_pair_sum"));
    AnalysisEntity* abi_pair_make_entity =
        codegen_test_entity_find(&analysis, S8("abi_pair_make"));
    AnalysisEntity* abi_mixed_sum_entity =
        codegen_test_entity_find(&analysis, S8("abi_mixed_sum"));
    AnalysisEntity* abi_large_sum_entity =
        codegen_test_entity_find(&analysis, S8("abi_large_sum"));
    AnalysisEntity* abi_large_make_entity =
        codegen_test_entity_find(&analysis, S8("abi_large_make"));
    CodegenModuleEntry* abi_pair_round_trip_entry =
        abi_pair_round_trip_entity ?
            codegen_test_module_entry_find(
                &generated_module,
                abi_pair_round_trip_entity->id) :
            0;
    CodegenModuleEntry* abi_mixed_round_trip_entry =
        abi_mixed_round_trip_entity ?
            codegen_test_module_entry_find(
                &generated_module,
                abi_mixed_round_trip_entity->id) :
            0;
    CodegenModuleEntry* abi_large_round_trip_entry =
        abi_large_round_trip_entity ?
            codegen_test_module_entry_find(
                &generated_module,
                abi_large_round_trip_entity->id) :
            0;
    CodegenModuleEntry* abi_pair_sum_entry =
        abi_pair_sum_entity ?
            codegen_test_module_entry_find(
                &generated_module,
                abi_pair_sum_entity->id) :
            0;
    CodegenModuleEntry* abi_pair_make_entry =
        abi_pair_make_entity ?
            codegen_test_module_entry_find(
                &generated_module,
                abi_pair_make_entity->id) :
            0;
    CodegenModuleEntry* abi_mixed_sum_entry =
        abi_mixed_sum_entity ?
            codegen_test_module_entry_find(
                &generated_module,
                abi_mixed_sum_entity->id) :
            0;
    CodegenModuleEntry* abi_large_sum_entry =
        abi_large_sum_entity ?
            codegen_test_module_entry_find(
                &generated_module,
                abi_large_sum_entity->id) :
            0;
    CodegenModuleEntry* abi_large_make_entry =
        abi_large_make_entity ?
            codegen_test_module_entry_find(
                &generated_module,
                abi_large_make_entity->id) :
            0;
    BUSTER_TEST(arguments, integer_to_float_entry != 0);
    BUSTER_TEST(arguments, float_to_integer_entry != 0);
    BUSTER_TEST(arguments, choose_entry != 0);
    BUSTER_TEST(arguments, abi_pair_round_trip_entry != 0);
    BUSTER_TEST(arguments, abi_mixed_round_trip_entry != 0);
    BUSTER_TEST(arguments, abi_large_round_trip_entry != 0);
    BUSTER_TEST(arguments, abi_pair_sum_entry != 0);
    BUSTER_TEST(arguments, abi_pair_make_entry != 0);
    BUSTER_TEST(arguments, abi_mixed_sum_entry != 0);
    BUSTER_TEST(arguments, abi_large_sum_entry != 0);
    BUSTER_TEST(arguments, abi_large_make_entry != 0);
    IrFunction* choose_function = choose_entity ?
        codegen_test_function_find(
            &module,
            choose_entity->id) :
        0;
    CodegenFunction aarch64_switch_generated =
        choose_function ?
            codegen_generate_function(
                arguments->arena,
                &analysis,
                choose_function,
                aarch64_target) :
            (CodegenFunction){
                .error = CODEGEN_ERROR_INVALID_IR,
            };
    BUSTER_TEST(
        arguments,
        aarch64_switch_generated.error ==
            CODEGEN_ERROR_NONE);
    if (add_one_function && caller_function)
    {
        IrFunction* call_functions = arena_allocate(
            arguments->arena,
            IrFunction,
            2);
        call_functions[0] = *add_one_function;
        call_functions[1] = *caller_function;
        IrModule call_module = {
            .functions = call_functions,
            .function_count = 2,
            .lowered_function_count = 2,
        };
        CodegenModule aarch64_call_module =
            codegen_generate_module(
                arguments->arena,
                &analysis,
                &call_module,
                aarch64_target);
        BUSTER_TEST(
            arguments,
            aarch64_call_module.error ==
                CODEGEN_ERROR_NONE);
        bool found_link = false;
        for (u64 offset = 0;
            offset + 4 <=
                aarch64_call_module.code.length;
            offset += 4)
        {
            u32 encoded = 0;
            memcpy(
                &encoded,
                aarch64_call_module.code.pointer + offset,
                sizeof(encoded));
            found_link |=
                (encoded & 0xfc000000) ==
                0x94000000;
        }
        BUSTER_TEST(arguments, found_link);
    }
#if BUSTER_CPU_ARCH_X86_64 && !BUSTER_SANITIZE
    CodegenExecutable module_executable =
        codegen_make_executable(
            (CodegenFunction){
                .code = generated_module.code,
                .error = generated_module.error,
            });
    BUSTER_TEST(
        arguments,
        module_executable.error == CODEGEN_ERROR_NONE);
    if (module_executable.address && caller_entry)
    {
        void* caller_address =
            (u8*)module_executable.address +
            caller_entry->offset;
        CodegenTestFunction1* native_caller = 0;
        BUSTER_CT_CHECK(
            sizeof(native_caller) ==
            sizeof(caller_address));
        memcpy(
            &native_caller,
            &caller_address,
            sizeof(native_caller));
        BUSTER_TEST(arguments, native_caller(20) == 42);
        if (call_many_entry)
        {
            void* call_many_address =
                (u8*)module_executable.address +
                call_many_entry->offset;
            CodegenTestFunction0* native_call_many = 0;
            memcpy(
                &native_call_many,
                &call_many_address,
                sizeof(native_call_many));
            BUSTER_TEST(
                arguments,
                native_call_many() == 28);
        }
        if (variadic_call_entry)
        {
            void* variadic_call_address =
                (u8*)module_executable.address +
                variadic_call_entry->offset;
            CodegenTestFunction0* native_variadic_call = 0;
            memcpy(
                &native_variadic_call,
                &variadic_call_address,
                sizeof(native_variadic_call));
            BUSTER_TEST(
                arguments,
                native_variadic_call() == 42);
        }
        if (variadic_float_call_entry)
        {
            void* address =
                (u8*)module_executable.address +
                variadic_float_call_entry->offset;
            CodegenTestFloatFunction0* native = 0;
            memcpy(&native, &address, sizeof(native));
            BUSTER_TEST(arguments, native() == 5.25);
        }
        if (variadic_promoted_call_entry)
        {
            void* address =
                (u8*)module_executable.address +
                variadic_promoted_call_entry->offset;
            CodegenTestFunction0* native = 0;
            memcpy(&native, &address, sizeof(native));
            BUSTER_TEST(arguments, native() == 42);
        }
        if (variadic_pair_call_entry)
        {
            void* address =
                (u8*)module_executable.address +
                variadic_pair_call_entry->offset;
            CodegenTestFunction0* native = 0;
            memcpy(&native, &address, sizeof(native));
            BUSTER_TEST(arguments, native() == 42);
        }
        if (variadic_mixed_call_entry)
        {
            void* address =
                (u8*)module_executable.address +
                variadic_mixed_call_entry->offset;
            CodegenTestFloatFunction0* native = 0;
            memcpy(&native, &address, sizeof(native));
            BUSTER_TEST(arguments, native() == 5.25);
        }
        if (variadic_large_call_entry)
        {
            void* address =
                (u8*)module_executable.address +
                variadic_large_call_entry->offset;
            CodegenTestFunction0* native = 0;
            memcpy(&native, &address, sizeof(native));
            BUSTER_TEST(arguments, native() == 31);
        }
        if (integer_to_float_entry &&
            float_to_integer_entry)
        {
            void* integer_to_float_address =
                (u8*)module_executable.address +
                integer_to_float_entry->offset;
            void* float_to_integer_address =
                (u8*)module_executable.address +
                float_to_integer_entry->offset;
            CodegenTestIntegerToFloatFunction*
                native_integer_to_float = 0;
            CodegenTestFloatToIntegerFunction*
                native_float_to_integer = 0;
            memcpy(
                &native_integer_to_float,
                &integer_to_float_address,
                sizeof(native_integer_to_float));
            memcpy(
                &native_float_to_integer,
                &float_to_integer_address,
                sizeof(native_float_to_integer));
            BUSTER_TEST(
                arguments,
                native_integer_to_float(-7) == -7.0);
            BUSTER_TEST(
                arguments,
                native_float_to_integer(8.75) == 8);
        }
        if (choose_entry)
        {
            void* choose_address =
                (u8*)module_executable.address +
                choose_entry->offset;
            CodegenTestFunction1* native_choose = 0;
            memcpy(
                &native_choose,
                &choose_address,
                sizeof(native_choose));
            BUSTER_TEST(arguments, native_choose(0) == 11);
            BUSTER_TEST(arguments, native_choose(1) == 22);
        }
        if (abi_pair_round_trip_entry &&
            abi_mixed_round_trip_entry &&
            abi_large_round_trip_entry)
        {
            void* pair_address =
                (u8*)module_executable.address +
                abi_pair_round_trip_entry->offset;
            void* mixed_address =
                (u8*)module_executable.address +
                abi_mixed_round_trip_entry->offset;
            void* large_address =
                (u8*)module_executable.address +
                abi_large_round_trip_entry->offset;
            CodegenTestFunction0* native_pair = 0;
            CodegenTestFunction0* native_large = 0;
            CodegenTestFloatFunction0* native_mixed = 0;
            memcpy(
                &native_pair,
                &pair_address,
                sizeof(native_pair));
            memcpy(
                &native_large,
                &large_address,
                sizeof(native_large));
            memcpy(
                &native_mixed,
                &mixed_address,
                sizeof(native_mixed));
            u64 native_pair_value = native_pair();
            BUSTER_TEST(arguments, native_pair_value == 42);
            BUSTER_TEST(arguments, native_large() == 23);
            BUSTER_TEST(arguments, native_mixed() == 3.5);
        }
#if !BUSTER_COMPILER_TCC
        if (abi_pair_sum_entry &&
            abi_pair_make_entry &&
            abi_mixed_sum_entry &&
            abi_large_sum_entry &&
            abi_large_make_entry)
        {
            void* pair_sum_address =
                (u8*)module_executable.address +
                abi_pair_sum_entry->offset;
            void* pair_make_address =
                (u8*)module_executable.address +
                abi_pair_make_entry->offset;
            void* mixed_sum_address =
                (u8*)module_executable.address +
                abi_mixed_sum_entry->offset;
            void* large_sum_address =
                (u8*)module_executable.address +
                abi_large_sum_entry->offset;
            void* large_make_address =
                (u8*)module_executable.address +
                abi_large_make_entry->offset;
            CodegenTestAbiPairSumFunction* pair_sum = 0;
            CodegenTestAbiPairMakeFunction* pair_make = 0;
            CodegenTestAbiMixedSumFunction* mixed_sum = 0;
            CodegenTestAbiLargeSumFunction* large_sum = 0;
            CodegenTestAbiLargeMakeFunction* large_make = 0;
            memcpy(
                &pair_sum,
                &pair_sum_address,
                sizeof(pair_sum));
            memcpy(
                &pair_make,
                &pair_make_address,
                sizeof(pair_make));
            memcpy(
                &mixed_sum,
                &mixed_sum_address,
                sizeof(mixed_sum));
            memcpy(
                &large_sum,
                &large_sum_address,
                sizeof(large_sum));
            memcpy(
                &large_make,
                &large_make_address,
                sizeof(large_make));
            CodegenTestAbiPair pair = { 13, 17 };
            BUSTER_TEST(arguments, pair_sum(pair) == 30);
            pair = pair_make(29, 31);
            BUSTER_TEST(arguments, pair.left == 29);
            BUSTER_TEST(arguments, pair.right == 31);
            CodegenTestAbiMixed mixed = { 2.25, 3 };
            BUSTER_TEST(arguments, mixed_sum(mixed) == 5.25);
            CodegenTestAbiLarge large = { 2, 3, 5 };
            BUSTER_TEST(arguments, large_sum(large) == 10);
            large = large_make(7, 11, 13);
            BUSTER_TEST(arguments, large.first == 7);
            BUSTER_TEST(arguments, large.second == 11);
            BUSTER_TEST(arguments, large.third == 13);
        }
#endif
        codegen_release_executable(module_executable);
    }
#endif
#if BUSTER_CPU_ARCH_AARCH64 && !BUSTER_SANITIZE
    CodegenModule native_aarch64_module =
        codegen_generate_module(
            arguments->arena,
            &analysis,
            &module,
            aarch64_target);
    BUSTER_TEST(
        arguments,
        native_aarch64_module.error ==
            CODEGEN_ERROR_NONE);
    CodegenModuleEntry* native_aarch64_pair_sum_entry =
        abi_pair_sum_entity ?
            codegen_test_module_entry_find(
                &native_aarch64_module,
                abi_pair_sum_entity->id) :
            0;
    CodegenModuleEntry* native_aarch64_pair_make_entry =
        abi_pair_make_entity ?
            codegen_test_module_entry_find(
                &native_aarch64_module,
                abi_pair_make_entity->id) :
            0;
    CodegenModuleEntry* native_aarch64_mixed_sum_entry =
        abi_mixed_sum_entity ?
            codegen_test_module_entry_find(
                &native_aarch64_module,
                abi_mixed_sum_entity->id) :
            0;
    CodegenModuleEntry* native_aarch64_large_sum_entry =
        abi_large_sum_entity ?
            codegen_test_module_entry_find(
                &native_aarch64_module,
                abi_large_sum_entity->id) :
            0;
    CodegenModuleEntry* native_aarch64_large_make_entry =
        abi_large_make_entity ?
            codegen_test_module_entry_find(
                &native_aarch64_module,
                abi_large_make_entity->id) :
            0;
    CodegenExecutable native_aarch64_executable =
        codegen_make_executable(
            (CodegenFunction){
                .code = native_aarch64_module.code,
                .error = native_aarch64_module.error,
            });
    BUSTER_TEST(
        arguments,
        native_aarch64_executable.error ==
            CODEGEN_ERROR_NONE);
    if (native_aarch64_executable.address &&
        native_aarch64_pair_sum_entry &&
        native_aarch64_pair_make_entry &&
        native_aarch64_mixed_sum_entry &&
        native_aarch64_large_sum_entry &&
        native_aarch64_large_make_entry)
    {
        void* pair_sum_address =
            (u8*)native_aarch64_executable.address +
            native_aarch64_pair_sum_entry->offset;
        void* pair_make_address =
            (u8*)native_aarch64_executable.address +
            native_aarch64_pair_make_entry->offset;
        void* mixed_sum_address =
            (u8*)native_aarch64_executable.address +
            native_aarch64_mixed_sum_entry->offset;
        void* large_sum_address =
            (u8*)native_aarch64_executable.address +
            native_aarch64_large_sum_entry->offset;
        void* large_make_address =
            (u8*)native_aarch64_executable.address +
            native_aarch64_large_make_entry->offset;
        CodegenTestAbiPairSumFunction* pair_sum = 0;
        CodegenTestAbiPairMakeFunction* pair_make = 0;
        CodegenTestAbiMixedSumFunction* mixed_sum = 0;
        CodegenTestAbiLargeSumFunction* large_sum = 0;
        CodegenTestAbiLargeMakeFunction* large_make = 0;
        memcpy(&pair_sum, &pair_sum_address, sizeof(pair_sum));
        memcpy(&pair_make, &pair_make_address, sizeof(pair_make));
        memcpy(&mixed_sum, &mixed_sum_address, sizeof(mixed_sum));
        memcpy(&large_sum, &large_sum_address, sizeof(large_sum));
        memcpy(&large_make, &large_make_address, sizeof(large_make));
        BUSTER_TEST(
            arguments,
            pair_sum((CodegenTestAbiPair){ 13, 17 }) == 30);
        CodegenTestAbiPair pair = pair_make(29, 31);
        BUSTER_TEST(arguments, pair.left == 29);
        BUSTER_TEST(arguments, pair.right == 31);
        BUSTER_TEST(
            arguments,
            mixed_sum((CodegenTestAbiMixed){ 2.25, 3 }) ==
                5.25);
        BUSTER_TEST(
            arguments,
            large_sum((CodegenTestAbiLarge){ 2, 3, 5 }) ==
                10);
        CodegenTestAbiLarge large =
            large_make(7, 11, 13);
        BUSTER_TEST(arguments, large.first == 7);
        BUSTER_TEST(arguments, large.second == 11);
        BUSTER_TEST(arguments, large.third == 13);
    }
    codegen_release_executable(native_aarch64_executable);
#endif
    BUSTER_CHECK(arena_destroy(expression_arena, 1));
    arena_set_position(temporary.arena, temporary.position);
    return result;
}
#endif
