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
    CodegenAbi abi;
};

#define X64_VALUE_SLOT_SIZE 32
#define X64_VALUE_SLOT_COMPONENT_COUNT 4

BUSTER_GLOBAL_LOCAL u32 codegen_align_u32(u32 value, u32 alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

CodegenAbi codegen_abi_for_target(Target target)
{
    if (target.cpu_arch == CPU_ARCH_X86_64)
    {
        return target.os == OPERATING_SYSTEM_WINDOWS ?
            CODEGEN_ABI_X86_64_WINDOWS :
            CODEGEN_ABI_X86_64_SYSTEM_V;
    }
    if (target.cpu_arch == CPU_ARCH_AARCH64)
    {
        return target.os == OPERATING_SYSTEM_MACOS ||
            target.os == OPERATING_SYSTEM_IOS ?
            CODEGEN_ABI_AARCH64_DARWIN :
            CODEGEN_ABI_AARCH64_AAPCS64;
    }
    return CODEGEN_ABI_COUNT;
}

CodegenAbiSignature codegen_classify_signature(
    Arena* arena,
    AnalysisResult* analysis,
    AnalysisTypeId function_type_id,
    CodegenAbi abi)
{
    CodegenAbiSignature result = {0};
    AnalysisType* function_type =
        analysis_type_from_id(analysis, function_type_id);
    if (function_type->kind != ANALYSIS_TYPE_FUNCTION ||
        abi >= CODEGEN_ABI_COUNT)
    {
        return result;
    }
    Target target = {
        .cpu_arch =
            abi == CODEGEN_ABI_X86_64_SYSTEM_V ||
                abi == CODEGEN_ABI_X86_64_WINDOWS ?
                CPU_ARCH_X86_64 :
                CPU_ARCH_AARCH64,
        .os =
            abi == CODEGEN_ABI_X86_64_WINDOWS ?
                OPERATING_SYSTEM_WINDOWS :
            abi == CODEGEN_ABI_AARCH64_DARWIN ?
                OPERATING_SYSTEM_MACOS :
                OPERATING_SYSTEM_LINUX,
    };
    analysis_compute_layouts(
        analysis,
        (AnalysisLayoutOptions){
            .pointer_size = 8,
            .pointer_alignment = 8,
        });
    AnalysisFunctionAbi classified =
        analysis_classify_function_abi(
            arena,
            analysis,
            function_type_id,
            target);
    result.argument_count = classified.argument_count;
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
        destination->part_count = source->part_count;
        destination->indirect = source->indirect;
        for (u32 part_index = 0;
            part_index < source->part_count;
            part_index += 1)
        {
            AnalysisAbiPart* source_part =
                source->parts + part_index;
            CodegenAbiPart* destination_part =
                destination->parts + part_index;
            destination_part->index =
                source_part->register_index;
            destination_part->stack_offset =
                source_part->stack_offset;
            destination_part->size = source_part->size;
            destination_part->kind =
                source->indirect ?
                    CODEGEN_ABI_LOCATION_INDIRECT :
                source_part->location ==
                    ANALYSIS_ABI_LOCATION_STACK ?
                    CODEGEN_ABI_LOCATION_STACK :
                source_part->abi_class ==
                    ANALYSIS_ABI_CLASS_FLOAT ?
                    CODEGEN_ABI_LOCATION_FLOAT_REGISTER :
                    CODEGEN_ABI_LOCATION_INTEGER_REGISTER;
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
    return result;
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
        type->kind == ANALYSIS_TYPE_INFERRED_ARRAY ||
        type->kind == ANALYSIS_TYPE_STRUCT ||
        type->kind == ANALYSIS_TYPE_UNION;
}

BUSTER_GLOBAL_LOCAL bool codegen_type_is_inline_collection(
    AnalysisType* type)
{
    return type->kind == ANALYSIS_TYPE_SLICE ||
        type->kind == ANALYSIS_TYPE_RANGE;
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
            base_type->kind == ANALYSIS_TYPE_INFERRED_ARRAY)
        {
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
                    base_type->as.array.count);
            }
            else if (component == 2)
            {
                AnalysisType* element = analysis_type_from_id(
                    builder->analysis,
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
        x64_emit_store(
            builder,
            X64_REGISTER_RAX,
            x64_value_displacement(instruction->result));
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
        x64_emit_load(
            builder,
            X64_REGISTER_RAX,
            x64_value_displacement(instruction->operands[0]));
        x64_emit_store(
            builder,
            X64_REGISTER_RAX,
            x64_value_displacement_component(
                instruction->result,
                0));
        x64_emit_load(
            builder,
            X64_REGISTER_RAX,
            x64_value_displacement(instruction->operands[1]));
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
    x64_emit_load(
        builder,
        X64_REGISTER_RAX,
        x64_value_displacement(instruction->operands[0]));
    x64_emit_load(
        builder,
        X64_REGISTER_RCX,
        x64_value_displacement(instruction->operands[1]));
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
            x64_emit_load(
                builder,
                X64_REGISTER_RAX,
                x64_value_displacement(instruction->operands[0]));
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
            x64_emit_load(
                builder,
                X64_REGISTER_RAX,
                x64_value_displacement(instruction->operands[0]));
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
                x64_emit_load(
                    builder,
                    X64_REGISTER_RCX,
                    x64_value_displacement(instruction->operands[1]));
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
                codegen_classify_signature(
                    builder->arena,
                    builder->analysis,
                    builder->function->type,
                    builder->abi);
            if (argument >= signature.argument_count)
            {
                return false;
            }
            CodegenAbiLocation* location =
                signature.arguments + argument;
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
            X64Register sysv[] = {
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
            X64Register source = builder->abi ==
                CODEGEN_ABI_X86_64_WINDOWS ?
                windows[location->index] :
                sysv[location->index];
            x64_emit_store(
                builder,
                source,
                x64_value_displacement(instruction->result));
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
            x64_emit_load(
                builder,
                X64_REGISTER_RAX,
                x64_value_displacement(instruction->operands[0]));
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
            CodegenAbiSignature signature =
                codegen_classify_signature(
                    builder->arena,
                    builder->analysis,
                    function_type_id,
                    builder->abi);
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
                if (location->part_count != 1 ||
                    location->kind ==
                        CODEGEN_ABI_LOCATION_INDIRECT)
                {
                    return false;
                }
                IrValueId operand =
                    instruction->operands[index + 1];
                AnalysisType* operand_type =
                    analysis_type_from_id(
                        builder->analysis,
                        builder->function->values[
                            operand.value].type);
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
                        x64_emit_load(
                            builder,
                            X64_REGISTER_RAX,
                            x64_value_displacement(operand));
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
                    x64_emit_load(
                        builder,
                        registers[location->index],
                        x64_value_displacement(operand));
                }
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
                if (result_type->kind == ANALYSIS_TYPE_FLOAT)
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
                x64_emit_load(
                    builder,
                    X64_REGISTER_RAX,
                    x64_value_displacement(
                        instruction->operands[0]));
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
            x64_emit_load(
                builder,
                X64_REGISTER_RAX,
                x64_value_displacement(instruction->operands[0]));
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
            x64_emit_load(
                builder,
                X64_REGISTER_RAX,
                x64_value_displacement(instruction->operands[0]));
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
                    x64_emit_load(
                        builder,
                        X64_REGISTER_RCX,
                        x64_value_displacement(
                            instruction->operands[index]));
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
                    x64_emit_load(
                        builder,
                        X64_REGISTER_RCX,
                        x64_value_displacement(
                            instruction->operands[index]));
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
        case IR_OPCODE_INDEX:
        {
            AnalysisType* base = analysis_type_from_id(
                builder->analysis,
                builder->function->values[
                    instruction->operands[0].value].type);
            if (base->kind != ANALYSIS_TYPE_RANGE)
            {
                x64_emit_load(
                    builder,
                    X64_REGISTER_RCX,
                    x64_value_displacement(instruction->operands[1]));
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
            x64_emit_load(
                builder,
                X64_REGISTER_RCX,
                x64_value_displacement(instruction->operands[1]));
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
                x64_emit_load(
                    builder,
                    X64_REGISTER_RCX,
                    x64_value_displacement(
                        instruction->operands[operand_index++]));
            }
            else
            {
                codegen_emit_u8(&builder->buffer, 0x31);
                codegen_emit_u8(&builder->buffer, 0xc9);
            }
            if (has_end)
            {
                x64_emit_load(
                    builder,
                    X64_REGISTER_RDX,
                    x64_value_displacement(
                        instruction->operands[operand_index]));
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
            x64_emit_load(
                builder,
                X64_REGISTER_RAX,
                x64_value_displacement(instruction->operands[0]));
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
                x64_emit_load(
                    builder,
                    X64_REGISTER_RAX,
                    x64_value_displacement(
                        instruction->operands[0]));
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
                if (return_type->kind == ANALYSIS_TYPE_FLOAT)
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
                    x64_emit_load(
                        builder,
                        X64_REGISTER_RAX,
                        x64_value_displacement(
                            instruction->operands[0]));
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
    CodegenAbi abi)
{
    CodegenFunction result = {
        .abi = abi,
    };
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
    u32 frame_size = codegen_align_u32(frame_cursor, 16);
    u64 capacity = (u64)function->instruction_count * 96 +
        (u64)function->block_count * 64 + 64;
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
        .abi = abi,
    };
    codegen_emit_u8(&builder.buffer, 0x55);
    codegen_emit_u8(&builder.buffer, 0x48);
    codegen_emit_u8(&builder.buffer, 0x89);
    codegen_emit_u8(&builder.buffer, 0xe5);
    codegen_emit_u8(&builder.buffer, 0x48);
    codegen_emit_u8(&builder.buffer, 0x81);
    codegen_emit_u8(&builder.buffer, 0xec);
    codegen_emit_u32(&builder.buffer, frame_size);
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
        if (base_type->kind == ANALYSIS_TYPE_ARRAY)
        {
            if (component == 0)
            {
                a64_emit_load_value(buffer, target, base_id);
            }
            else if (component == 1)
            {
                a64_emit_constant(
                    buffer,
                    target,
                    base_type->as.array.count);
            }
            else if (component == 2)
            {
                AnalysisType* element = analysis_type_from_id(
                    analysis,
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
    CodegenAbi abi)
{
    CodegenFunction result = {
        .abi = abi,
    };
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
    u32 frame_size = codegen_align_u32(
        frame_cursor,
        16);
    if (frame_size > 32760)
    {
        result.error = CODEGEN_ERROR_CAPACITY;
        return result;
    }
    u64 capacity =
        (u64)function->instruction_count * 128 +
        (u64)function->block_count * 128 + 128;
    CodegenBuffer buffer = {
        .bytes = arena_allocate(arena, u8, capacity),
        .capacity = capacity,
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
                        codegen_classify_signature(
                            arena,
                            analysis,
                            function->type,
                            abi);
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
                        ANALYSIS_TYPE_FLOAT)
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
                    CodegenAbiSignature signature =
                        codegen_classify_signature(
                            arena,
                            analysis,
                            function_type_id,
                            abi);
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
                            ANALYSIS_TYPE_FLOAT)
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
                        if (result_type->kind ==
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
                        if (return_type->kind ==
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
            result.abi);
    }
    if (target.cpu_arch == CPU_ARCH_AARCH64)
    {
        return codegen_generate_aarch64(
            arena,
            analysis,
            function,
            result.abi);
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
                    result.abi) :
            target.cpu_arch == CPU_ARCH_AARCH64 ?
                codegen_generate_aarch64(
                    arena,
                    analysis,
                    function,
                    result.abi) :
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
    String8 source = S8(
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
        "}\n");
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
    target.os = OPERATING_SYSTEM_LINUX;
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
    CodegenAbiSignature system_v = codegen_classify_signature(
        arguments->arena,
        &analysis,
        function->type,
        CODEGEN_ABI_X86_64_SYSTEM_V);
    BUSTER_TEST(arguments, system_v.argument_count == 2);
    BUSTER_TEST(
        arguments,
        system_v.arguments[0].kind ==
            CODEGEN_ABI_LOCATION_INTEGER_REGISTER);
    BUSTER_TEST(arguments, system_v.arguments[0].index == 0);
    BUSTER_TEST(arguments, system_v.arguments[1].index == 1);

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
    AnalysisEntity* caller_entity =
        codegen_test_entity_find(
            &analysis,
            S8("call_chain"));
    BUSTER_TEST(arguments, caller_entity != 0);
    AnalysisEntity* call_many_entity =
        codegen_test_entity_find(
            &analysis,
            S8("call_many"));
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
    BUSTER_TEST(arguments, integer_to_float_entry != 0);
    BUSTER_TEST(arguments, float_to_integer_entry != 0);
    BUSTER_TEST(arguments, choose_entry != 0);
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
        codegen_release_executable(module_executable);
    }
#endif
    BUSTER_CHECK(arena_destroy(expression_arena, 1));
    arena_set_position(temporary.arena, temporary.position);
    return result;
}
#endif
