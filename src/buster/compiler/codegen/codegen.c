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
    u32* block_offsets;
    u32 frame_size;
    u32 temporary_base;
    u32 temporary_count;
    u32 local_storage_base;
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
    result.argument_count = function_type->as.function.argument_count;
    result.arguments = arena_allocate(
        arena,
        CodegenAbiLocation,
        result.argument_count);
    u32 integer_register = 0;
    u32 float_register = 0;
    u32 stack_offset =
        abi == CODEGEN_ABI_X86_64_WINDOWS ? 32 : 0;
    u32 integer_limit =
        abi == CODEGEN_ABI_X86_64_WINDOWS ? 4 :
        abi == CODEGEN_ABI_X86_64_SYSTEM_V ? 6 : 8;
    u32 float_limit =
        abi == CODEGEN_ABI_X86_64_WINDOWS ? 4 : 8;
    for (u32 index = 0; index < result.argument_count; index += 1)
    {
        AnalysisType* type = analysis_type_from_id(
            analysis,
            function_type->as.function.argument_types[index]);
        CodegenAbiLocation* location = result.arguments + index;
        bool floating = type->kind == ANALYSIS_TYPE_FLOAT;
        bool aggregate = type->kind == ANALYSIS_TYPE_ARRAY ||
            type->kind == ANALYSIS_TYPE_STRUCT ||
            type->kind == ANALYSIS_TYPE_UNION ||
            type->kind == ANALYSIS_TYPE_SLICE ||
            type->kind == ANALYSIS_TYPE_RANGE;
        if (abi == CODEGEN_ABI_X86_64_WINDOWS)
        {
            if (index < 4)
            {
                location->kind = aggregate ?
                    CODEGEN_ABI_LOCATION_INDIRECT :
                    floating ?
                        CODEGEN_ABI_LOCATION_FLOAT_REGISTER :
                        CODEGEN_ABI_LOCATION_INTEGER_REGISTER;
                location->index = index;
            }
            else
            {
                location->kind = CODEGEN_ABI_LOCATION_STACK;
                location->stack_offset = stack_offset;
                stack_offset += 8;
            }
        }
        else if (aggregate)
        {
            location->kind = CODEGEN_ABI_LOCATION_INDIRECT;
            location->index = integer_register;
            if (integer_register < integer_limit)
            {
                integer_register += 1;
            }
            else
            {
                location->kind = CODEGEN_ABI_LOCATION_STACK;
                location->stack_offset = stack_offset;
                stack_offset += 8;
            }
        }
        else if (floating && float_register < float_limit)
        {
            location->kind = CODEGEN_ABI_LOCATION_FLOAT_REGISTER;
            location->index = float_register++;
        }
        else if (!floating && integer_register < integer_limit)
        {
            location->kind = CODEGEN_ABI_LOCATION_INTEGER_REGISTER;
            location->index = integer_register++;
        }
        else
        {
            location->kind = CODEGEN_ABI_LOCATION_STACK;
            location->stack_offset = stack_offset;
            stack_offset += 8;
        }
    }
    AnalysisType* return_type = analysis_type_from_id(
        analysis,
        function_type->as.function.return_type);
    bool return_aggregate =
        return_type->kind == ANALYSIS_TYPE_ARRAY ||
        return_type->kind == ANALYSIS_TYPE_STRUCT ||
        return_type->kind == ANALYSIS_TYPE_UNION ||
        return_type->kind == ANALYSIS_TYPE_SLICE ||
        return_type->kind == ANALYSIS_TYPE_RANGE;
    result.result.kind = return_aggregate ?
        CODEGEN_ABI_LOCATION_INDIRECT :
        return_type->kind == ANALYSIS_TYPE_FLOAT ?
            CODEGEN_ABI_LOCATION_FLOAT_REGISTER :
            CODEGEN_ABI_LOCATION_INTEGER_REGISTER;
    result.stack_size = codegen_align_u32(stack_offset, 8);
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
    return -(s32)(
        builder->local_storage_base +
        (local.value + 1) * X64_VALUE_SLOT_SIZE);
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
            x64_emit_load(
                builder,
                X64_REGISTER_RAX,
                x64_value_displacement(instruction->operands[0]));
            codegen_emit_u8(&builder->buffer, 0x48);
            codegen_emit_u8(&builder->buffer, 0x8b);
            codegen_emit_u8(&builder->buffer, 0x00);
            x64_emit_store_result(builder, instruction);
        } break;
        case IR_OPCODE_STORE:
        {
            x64_emit_load(
                builder,
                X64_REGISTER_RAX,
                x64_value_displacement(instruction->operands[0]));
            x64_emit_load(
                builder,
                X64_REGISTER_RCX,
                x64_value_displacement(instruction->operands[1]));
            codegen_emit_u8(&builder->buffer, 0x48);
            codegen_emit_u8(&builder->buffer, 0x89);
            codegen_emit_u8(&builder->buffer, 0x08);
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
            if (argument_type->kind == ANALYSIS_TYPE_FLOAT)
            {
                u32 limit = builder->abi ==
                    CODEGEN_ABI_X86_64_WINDOWS ? 4 : 8;
                if (argument >= limit)
                {
                    return false;
                }
                x64_emit_float_store(
                    builder,
                    argument,
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
            u32 limit = builder->abi == CODEGEN_ABI_X86_64_WINDOWS ? 4 : 6;
            if (argument >= limit)
            {
                return false;
            }
            X64Register source = builder->abi ==
                CODEGEN_ABI_X86_64_WINDOWS ?
                windows[argument] :
                sysv[argument];
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
        case IR_OPCODE_CAST:
        {
            x64_emit_load(
                builder,
                X64_REGISTER_RAX,
                x64_value_displacement(instruction->operands[0]));
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
        case IR_OPCODE_REVERSE:
        {
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
                if (component == 2)
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
                return false;
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
                return false;
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
    u32 local_bytes =
        function->local_count * X64_VALUE_SLOT_SIZE;
    u32 frame_size = codegen_align_u32(
        value_bytes + temporary_bytes + local_bytes,
        16);
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
    return result;
}

typedef struct A64Relocation A64Relocation;
struct A64Relocation
{
    A64Relocation* next;
    IrBlockId target;
    u32 instruction_offset;
};

BUSTER_GLOBAL_LOCAL void a64_emit_instruction_word(
    CodegenBuffer* buffer,
    u32 instruction)
{
    codegen_emit_u32(buffer, instruction);
}

BUSTER_GLOBAL_LOCAL u32 a64_value_offset(IrValueId value)
{
    return value.value * 8;
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

BUSTER_GLOBAL_LOCAL void a64_relocation_add(
    Arena* arena,
    CodegenBuffer* buffer,
    A64Relocation** first,
    A64Relocation** last,
    IrBlockId target)
{
    A64Relocation* relocation = arena_allocate(
        arena,
        A64Relocation,
        1);
    *relocation = (A64Relocation){
        .target = target,
        .instruction_offset = (u32)buffer->count,
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
    a64_emit_instruction_word(buffer, 0x14000000);
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
    u32 frame_size = codegen_align_u32(
        function->value_count * 8,
        16);
    if (frame_size > 4095)
    {
        result.error = CODEGEN_ERROR_CAPACITY;
        return result;
    }
    u64 capacity =
        (u64)function->instruction_count * 32 + 64;
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
    a64_emit_instruction_word(&buffer, 0xa9bf7bfd);
    a64_emit_instruction_word(&buffer, 0x910003fd);
    if (frame_size)
    {
        a64_emit_instruction_word(
            &buffer,
            0xd10003ff | (frame_size << 10));
    }
    for (u32 block_index = 0;
        block_index < function->block_count &&
            buffer.error == CODEGEN_ERROR_NONE;
        block_index += 1)
    {
        IrBlock* block = function->blocks + block_index;
        block_offsets[block_index] = (u32)buffer.count;
        if (block->parameter_count)
        {
            buffer.error =
                CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
            break;
        }
        for (IrInstructionId id = block->first_instruction;
            id.value != IR_ID_UNDERLYING_INVALID;
            id = function->instructions[id.value].next)
        {
            IrInstruction* instruction =
                function->instructions + id.value;
            switch (instruction->opcode)
            {
                case IR_OPCODE_ARGUMENT:
                {
                    if (!instruction->immediate_count ||
                        instruction->immediates[0] >= 8 ||
                        analysis_type_from_id(
                            analysis,
                            instruction->type)->kind ==
                            ANALYSIS_TYPE_FLOAT)
                    {
                        buffer.error =
                            CODEGEN_ERROR_UNSUPPORTED_ABI;
                        break;
                    }
                    a64_emit_store_value(
                        &buffer,
                        (u32)instruction->immediates[0],
                        instruction->result);
                } break;
                case IR_OPCODE_CONSTANT_INTEGER:
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
                case IR_OPCODE_BINARY:
                {
                    a64_emit_load_value(
                        &buffer,
                        0,
                        instruction->operands[0]);
                    a64_emit_load_value(
                        &buffer,
                        1,
                        instruction->operands[1]);
                    u32 encoded =
                        instruction->binary_operation ==
                            IR_BINARY_INTEGER_ADD ?
                            0x8b010000 :
                        instruction->binary_operation ==
                            IR_BINARY_INTEGER_SUBTRACT ?
                            0xcb010000 :
                        instruction->binary_operation ==
                            IR_BINARY_INTEGER_MULTIPLY ?
                            0x9b017c00 :
                            0;
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
                case IR_OPCODE_BRANCH:
                    a64_relocation_add(
                        arena,
                        &buffer,
                        &first_relocation,
                        &last_relocation,
                        instruction->targets[0]);
                    break;
                case IR_OPCODE_RETURN:
                {
                    if (instruction->operand_count)
                    {
                        a64_emit_load_value(
                            &buffer,
                            0,
                            instruction->operands[0]);
                    }
                    if (frame_size)
                    {
                        a64_emit_instruction_word(
                            &buffer,
                            0x910003ff |
                                (frame_size << 10));
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

UnitTestResult codegen_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena temporary =
        arena_begin_temporal(arguments->arena);
    Arena* expression_arena = arena_create((ArenaCreation){0});
    BUSTER_CHECK(expression_arena);
    String8 source = S8(
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
    BUSTER_CHECK(arena_destroy(expression_arena, 1));
    arena_set_position(temporary.arena, temporary.position);
    return result;
}
#endif
