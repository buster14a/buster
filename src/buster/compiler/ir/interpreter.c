#include <buster/compiler/ir/interpreter.h>

#include <buster/string.h>

typedef enum IrRuntimeValueKind
{
    IR_RUNTIME_VALUE_SCALAR,
    IR_RUNTIME_VALUE_FUNCTION,
    IR_RUNTIME_VALUE_KIND_COUNT,
} IrRuntimeValueKind;

typedef struct IrRuntimeValue IrRuntimeValue;
struct IrRuntimeValue
{
    AnalysisEntityId entity;
    AnalysisInstantiationId instantiation;
    u64 bits;
    IrRuntimeValueKind kind;
    bool initialized;
    u8 reserved[3];
};

typedef struct IrExecutionFrame IrExecutionFrame;
struct IrExecutionFrame
{
    AnalysisResult* analysis;
    IrModule* module;
    IrFunction* function;
    IrRuntimeValue* values;
    IrRuntimeValue* arguments;
    IrRuntimeValue* transition_values;
    IrValueId caller_result;
    IrBlockId block;
    IrInstructionId instruction;
    u32 value_capacity;
    u32 argument_capacity;
    u32 argument_count;
};

typedef struct IrExecutionTarget IrExecutionTarget;
struct IrExecutionTarget
{
    AnalysisResult* analysis;
    IrModule* module;
    IrFunction* function;
};

BUSTER_GLOBAL_LOCAL bool ir_interpreter_entity_equal(
    AnalysisEntityId left,
    AnalysisEntityId right)
{
    return left.module.value == right.module.value &&
        left.index.value == right.index.value;
}

BUSTER_GLOBAL_LOCAL IrExecutionTarget ir_interpreter_function_find(
    AnalysisProgram* analysis,
    IrProgram* program,
    AnalysisEntityId entity,
    AnalysisInstantiationId instantiation)
{
    IrExecutionTarget target = {0};
    if (!analysis || !program ||
        analysis->module_count != program->module_count)
    {
        return target;
    }
    for (u32 module_index = 0;
        module_index < analysis->module_count;
        module_index += 1)
    {
        AnalysisResult* candidate_analysis =
            analysis->module_results[module_index];
        if (!candidate_analysis ||
            candidate_analysis->module.id.value != entity.module.value)
        {
            continue;
        }
        IrModule* candidate_module = program->modules + module_index;
        for (u32 function_index = 0;
            function_index < candidate_module->function_count;
            function_index += 1)
        {
            IrFunction* candidate =
                candidate_module->functions + function_index;
            if (ir_interpreter_entity_equal(candidate->entity, entity) &&
                candidate->instantiation.value == instantiation.value)
            {
                target = (IrExecutionTarget){
                    .analysis = candidate_analysis,
                    .module = candidate_module,
                    .function = candidate,
                };
                return target;
            }
        }
        return target;
    }
    return target;
}

BUSTER_GLOBAL_LOCAL u32 ir_interpreter_type_width(
    AnalysisResult* analysis,
    AnalysisTypeId type_id)
{
    AnalysisType* type = analysis_type_from_id(analysis, type_id);
    switch (type->kind)
    {
        case ANALYSIS_TYPE_BOOL: return 1;
        case ANALYSIS_TYPE_INTEGER: return type->as.integer.bit_width;
        case ANALYSIS_TYPE_FLOAT: return type->as.float_bit_width;
        case ANALYSIS_TYPE_ENUM:
        {
            return type->layout.size ?
                (u32)(type->layout.size * 8) : 32;
        }
        default: return 64;
    }
}

BUSTER_GLOBAL_LOCAL u64 ir_interpreter_mask(u32 width)
{
    return width >= 64 ? UINT64_MAX :
        width ? (((u64)1 << width) - 1) : 0;
}

BUSTER_GLOBAL_LOCAL u64 ir_interpreter_normalize_integer(
    AnalysisResult* analysis,
    AnalysisTypeId type,
    u64 bits)
{
    return bits & ir_interpreter_mask(
        ir_interpreter_type_width(analysis, type));
}

BUSTER_GLOBAL_LOCAL bool ir_interpreter_type_signed(
    AnalysisResult* analysis,
    AnalysisTypeId type_id)
{
    AnalysisType* type = analysis_type_from_id(analysis, type_id);
    return type->kind == ANALYSIS_TYPE_INTEGER &&
        type->as.integer.is_signed;
}

BUSTER_GLOBAL_LOCAL bool ir_interpreter_integer_negative(
    u64 bits,
    u32 width)
{
    return width && ((bits >> (width - 1)) & 1);
}

BUSTER_GLOBAL_LOCAL u64 ir_interpreter_integer_magnitude(
    u64 bits,
    u32 width)
{
    u64 mask = ir_interpreter_mask(width);
    bits &= mask;
    return ir_interpreter_integer_negative(bits, width) ?
        ((~bits) + 1) & mask : bits;
}

BUSTER_GLOBAL_LOCAL s32 ir_interpreter_signed_compare(
    u64 left,
    u64 right,
    u32 width)
{
    u64 mask = ir_interpreter_mask(width);
    left &= mask;
    right &= mask;
    bool left_negative = ir_interpreter_integer_negative(left, width);
    bool right_negative = ir_interpreter_integer_negative(right, width);
    if (left_negative != right_negative)
    {
        return left_negative ? -1 : 1;
    }
    return left < right ? -1 : left > right ? 1 : 0;
}

BUSTER_GLOBAL_LOCAL f64 ir_interpreter_float_read(
    u64 bits,
    u32 width)
{
    if (width == 32)
    {
        u32 bits32 = (u32)bits;
        f32 value = 0.0f;
        memcpy(&value, &bits32, sizeof(value));
        return (f64)value;
    }
    f64 value = 0.0;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

BUSTER_GLOBAL_LOCAL u64 ir_interpreter_float_write(
    f64 value,
    u32 width)
{
    u64 bits = 0;
    if (width == 32)
    {
        f32 value32 = (f32)value;
        u32 bits32 = 0;
        memcpy(&bits32, &value32, sizeof(value32));
        bits = bits32;
    }
    else
    {
        memcpy(&bits, &value, sizeof(value));
    }
    return bits;
}

BUSTER_GLOBAL_LOCAL bool ir_interpreter_frame_prepare(
    Arena* scratch_arena,
    IrExecutionFrame* frame,
    IrExecutionTarget target,
    IrRuntimeValue* arguments,
    u32 argument_count,
    IrValueId caller_result)
{
    AnalysisType* function_type = analysis_type_from_id(
        target.analysis,
        target.function->type);
    if (function_type->kind != ANALYSIS_TYPE_FUNCTION ||
        function_type->as.function.argument_count != argument_count ||
        target.function->state != IR_FUNCTION_LOWERED)
    {
        return false;
    }
    if (frame->value_capacity < target.function->value_count)
    {
        frame->values = arena_allocate(
            scratch_arena,
            IrRuntimeValue,
            target.function->value_count);
        frame->transition_values = arena_allocate(
            scratch_arena,
            IrRuntimeValue,
            target.function->value_count);
        frame->value_capacity = target.function->value_count;
    }
    if (frame->argument_capacity < argument_count)
    {
        frame->arguments = arena_allocate(
            scratch_arena,
            IrRuntimeValue,
            argument_count);
        frame->argument_capacity = argument_count;
    }
    for (u32 value_index = 0;
        value_index < target.function->value_count;
        value_index += 1)
    {
        frame->values[value_index] = (IrRuntimeValue){0};
    }
    for (u32 argument_index = 0;
        argument_index < argument_count;
        argument_index += 1)
    {
        frame->arguments[argument_index] = arguments[argument_index];
    }
    frame->analysis = target.analysis;
    frame->module = target.module;
    frame->function = target.function;
    frame->caller_result = caller_result;
    frame->block = target.function->entry;
    frame->instruction =
        target.function->blocks[target.function->entry.value]
            .first_instruction;
    frame->argument_count = argument_count;
    return true;
}

BUSTER_GLOBAL_LOCAL bool ir_interpreter_block_enter(
    IrExecutionFrame* frame,
    IrBlockId target,
    IrBlockId predecessor)
{
    if (target.value >= frame->function->block_count)
    {
        return false;
    }
    IrBlock* block = frame->function->blocks + target.value;
    u32 parameter_index = 0;
    for (IrBlockParameter* parameter = block->first_parameter;
        parameter;
        parameter = parameter->next)
    {
        IrIncoming* selected = 0;
        for (IrIncoming* incoming = parameter->first_incoming;
            incoming;
            incoming = incoming->next)
        {
            if (incoming->predecessor.value == predecessor.value)
            {
                selected = incoming;
                break;
            }
        }
        if (!selected ||
            selected->value.value >= frame->function->value_count ||
            !frame->values[selected->value.value].initialized)
        {
            return false;
        }
        frame->transition_values[parameter_index++] =
            frame->values[selected->value.value];
    }
    parameter_index = 0;
    for (IrBlockParameter* parameter = block->first_parameter;
        parameter;
        parameter = parameter->next)
    {
        frame->values[parameter->value.value] =
            frame->transition_values[parameter_index++];
    }
    frame->block = target;
    frame->instruction = block->first_instruction;
    return true;
}

BUSTER_GLOBAL_LOCAL bool ir_interpreter_operands_ready(
    IrExecutionFrame* frame,
    IrInstruction* instruction)
{
    for (u32 operand_index = 0;
        operand_index < instruction->operand_count;
        operand_index += 1)
    {
        IrValueId operand = instruction->operands[operand_index];
        if (operand.value >= frame->function->value_count ||
            !frame->values[operand.value].initialized)
        {
            return false;
        }
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool ir_interpreter_integer_binary(
    IrExecutionFrame* frame,
    IrInstruction* instruction,
    u64* bits_out,
    IrExecutionTrap* trap_out)
{
    IrValueId left_id = instruction->operands[0];
    IrValueId right_id = instruction->operands[1];
    u64 left = frame->values[left_id.value].bits;
    u64 right = frame->values[right_id.value].bits;
    AnalysisTypeId operand_type =
        frame->function->values[left_id.value].type;
    u32 width = ir_interpreter_type_width(frame->analysis, operand_type);
    u64 mask = ir_interpreter_mask(width);
    left &= mask;
    right &= mask;
    bool is_signed = ir_interpreter_type_signed(
        frame->analysis,
        operand_type);
    u64 value = 0;
    switch (instruction->ast_operation)
    {
        case AST_NODE_BINARY_PLUS: value = left + right; break;
        case AST_NODE_BINARY_MINUS: value = left - right; break;
        case AST_NODE_BINARY_ASTERISK: value = left * right; break;
        case AST_NODE_BINARY_SLASH:
        case AST_NODE_BINARY_PERCENT:
        {
            if (!right)
            {
                *trap_out = IR_EXECUTION_TRAP_DIVISION_BY_ZERO;
                return false;
            }
            if (is_signed)
            {
                bool left_negative =
                    ir_interpreter_integer_negative(left, width);
                bool right_negative =
                    ir_interpreter_integer_negative(right, width);
                u64 left_magnitude =
                    ir_interpreter_integer_magnitude(left, width);
                u64 right_magnitude =
                    ir_interpreter_integer_magnitude(right, width);
                if (instruction->ast_operation ==
                    AST_NODE_BINARY_SLASH)
                {
                    value = left_magnitude / right_magnitude;
                    if (left_negative != right_negative)
                    {
                        value = (0 - value) & mask;
                    }
                }
                else
                {
                    value = left_magnitude % right_magnitude;
                    if (left_negative)
                    {
                        value = (0 - value) & mask;
                    }
                }
            }
            else
            {
                value = instruction->ast_operation ==
                    AST_NODE_BINARY_SLASH ?
                    left / right : left % right;
            }
        } break;
        case AST_NODE_BINARY_SHIFT_LEFT:
        case AST_NODE_BINARY_SHIFT_RIGHT:
        {
            if (right >= width)
            {
                *trap_out = IR_EXECUTION_TRAP_INVALID_SHIFT;
                return false;
            }
            if (instruction->ast_operation ==
                AST_NODE_BINARY_SHIFT_LEFT)
            {
                value = left << (u32)right;
            }
            else if (is_signed &&
                ir_interpreter_integer_negative(left, width) &&
                right)
            {
                value = (left >> (u32)right) |
                    (mask ^ (mask >> (u32)right));
            }
            else
            {
                value = left >> (u32)right;
            }
        } break;
        case AST_NODE_BINARY_EQUAL: value = left == right; break;
        case AST_NODE_BINARY_NOT_EQUAL: value = left != right; break;
        case AST_NODE_BINARY_LESS:
        case AST_NODE_BINARY_LESS_EQUAL:
        case AST_NODE_BINARY_GREATER:
        case AST_NODE_BINARY_GREATER_EQUAL:
        {
            s32 order = is_signed ?
                ir_interpreter_signed_compare(left, right, width) :
                left < right ? -1 : left > right ? 1 : 0;
            value = instruction->ast_operation ==
                    AST_NODE_BINARY_LESS ? order < 0 :
                instruction->ast_operation ==
                    AST_NODE_BINARY_LESS_EQUAL ? order <= 0 :
                instruction->ast_operation ==
                    AST_NODE_BINARY_GREATER ? order > 0 :
                    order >= 0;
        } break;
        case AST_NODE_BINARY_AMPERSAND:
        case AST_NODE_BINARY_BOOLEAN_AND:
        case AST_NODE_BINARY_BOOLEAN_AND_SHORT_CIRCUIT:
        {
            value = left & right;
        } break;
        case AST_NODE_BINARY_BAR:
        case AST_NODE_BINARY_BOOLEAN_OR:
        case AST_NODE_BINARY_BOOLEAN_OR_SHORT_CIRCUIT:
        {
            value = left | right;
        } break;
        case AST_NODE_BINARY_CARET: value = left ^ right; break;
        default:
        {
            *trap_out = IR_EXECUTION_TRAP_UNSUPPORTED_INSTRUCTION;
            return false;
        }
    }
    *bits_out = ir_interpreter_normalize_integer(
        frame->analysis,
        instruction->type,
        value);
    return true;
}

BUSTER_GLOBAL_LOCAL bool ir_interpreter_float_binary(
    IrExecutionFrame* frame,
    IrInstruction* instruction,
    u64* bits_out,
    IrExecutionTrap* trap_out)
{
    IrValueId left_id = instruction->operands[0];
    IrValueId right_id = instruction->operands[1];
    AnalysisTypeId operand_type =
        frame->function->values[left_id.value].type;
    u32 width = ir_interpreter_type_width(
        frame->analysis,
        operand_type);
    f64 left = ir_interpreter_float_read(
        frame->values[left_id.value].bits,
        width);
    f64 right = ir_interpreter_float_read(
        frame->values[right_id.value].bits,
        width);
    f64 value = 0.0;
    bool comparison = false;
    bool comparison_value = false;
    switch (instruction->ast_operation)
    {
        case AST_NODE_BINARY_PLUS: value = left + right; break;
        case AST_NODE_BINARY_MINUS: value = left - right; break;
        case AST_NODE_BINARY_ASTERISK: value = left * right; break;
        case AST_NODE_BINARY_SLASH:
        {
            if (right == 0.0)
            {
                *trap_out = IR_EXECUTION_TRAP_DIVISION_BY_ZERO;
                return false;
            }
            value = left / right;
        } break;
        case AST_NODE_BINARY_EQUAL:
            comparison = true;
            comparison_value = left == right;
            break;
        case AST_NODE_BINARY_NOT_EQUAL:
            comparison = true;
            comparison_value = left != right;
            break;
        case AST_NODE_BINARY_LESS:
            comparison = true;
            comparison_value = left < right;
            break;
        case AST_NODE_BINARY_LESS_EQUAL:
            comparison = true;
            comparison_value = left <= right;
            break;
        case AST_NODE_BINARY_GREATER:
            comparison = true;
            comparison_value = left > right;
            break;
        case AST_NODE_BINARY_GREATER_EQUAL:
            comparison = true;
            comparison_value = left >= right;
            break;
        default:
        {
            *trap_out = IR_EXECUTION_TRAP_UNSUPPORTED_INSTRUCTION;
            return false;
        }
    }
    *bits_out = comparison ?
        comparison_value :
        ir_interpreter_float_write(value, width);
    return true;
}

BUSTER_GLOBAL_LOCAL bool ir_interpreter_cast(
    IrExecutionFrame* frame,
    IrInstruction* instruction,
    u64* bits_out)
{
    IrValueId operand_id = instruction->operands[0];
    AnalysisType* source = analysis_type_from_id(
        frame->analysis,
        frame->function->values[operand_id.value].type);
    AnalysisType* target = analysis_type_from_id(
        frame->analysis,
        instruction->type);
    u64 bits = frame->values[operand_id.value].bits;
    bool source_integer =
        source->kind == ANALYSIS_TYPE_BOOL ||
        source->kind == ANALYSIS_TYPE_INTEGER ||
        source->kind == ANALYSIS_TYPE_ENUM;
    bool target_integer =
        target->kind == ANALYSIS_TYPE_BOOL ||
        target->kind == ANALYSIS_TYPE_INTEGER ||
        target->kind == ANALYSIS_TYPE_ENUM;
    if (source_integer && target_integer)
    {
        u32 source_width = ir_interpreter_type_width(
            frame->analysis,
            source->id);
        u32 target_width = ir_interpreter_type_width(
            frame->analysis,
            target->id);
        bits &= ir_interpreter_mask(source_width);
        if (source->kind == ANALYSIS_TYPE_INTEGER &&
            source->as.integer.is_signed &&
            target_width > source_width &&
            ir_interpreter_integer_negative(bits, source_width))
        {
            bits |= ~ir_interpreter_mask(source_width);
        }
        *bits_out = bits & ir_interpreter_mask(target_width);
        return true;
    }
    if (source->kind == ANALYSIS_TYPE_FLOAT &&
        target->kind == ANALYSIS_TYPE_FLOAT)
    {
        *bits_out = ir_interpreter_float_write(
            ir_interpreter_float_read(
                bits,
                source->as.float_bit_width),
            target->as.float_bit_width);
        return true;
    }
    if (source_integer && target->kind == ANALYSIS_TYPE_FLOAT)
    {
        u32 source_width = ir_interpreter_type_width(
            frame->analysis,
            source->id);
        bits &= ir_interpreter_mask(source_width);
        f64 value = 0.0;
        if (source->kind == ANALYSIS_TYPE_INTEGER &&
            source->as.integer.is_signed &&
            ir_interpreter_integer_negative(bits, source_width))
        {
            value = -(f64)ir_interpreter_integer_magnitude(
                bits,
                source_width);
        }
        else
        {
            value = (f64)bits;
        }
        *bits_out = ir_interpreter_float_write(
            value,
            target->as.float_bit_width);
        return true;
    }
    if (source->kind == ANALYSIS_TYPE_FLOAT && target_integer)
    {
        f64 value = ir_interpreter_float_read(
            bits,
            source->as.float_bit_width);
        u32 target_width = ir_interpreter_type_width(
            frame->analysis,
            target->id);
        if (target->kind == ANALYSIS_TYPE_INTEGER &&
            target->as.integer.is_signed && value < 0.0)
        {
            u64 magnitude = (u64)(-value);
            *bits_out = (0 - magnitude) &
                ir_interpreter_mask(target_width);
        }
        else
        {
            *bits_out = (u64)value &
                ir_interpreter_mask(target_width);
        }
        return true;
    }
    return false;
}

BUSTER_GLOBAL_LOCAL IrExecutionResult ir_interpreter_trap(
    IrExecutionFrame* frame,
    IrExecutionTrap trap,
    u64 step_count)
{
    return (IrExecutionResult){
        .function = frame ? frame->function->entity :
            ANALYSIS_ENTITY_ID_INVALID,
        .instantiation = frame ? frame->function->instantiation :
            ANALYSIS_INSTANTIATION_ID_INVALID,
        .instruction = frame ? frame->instruction :
            IR_INSTRUCTION_ID_INVALID,
        .step_count = step_count,
        .trap = trap,
    };
}

IrExecutionResult ir_execute(
    Arena* execution_arena,
    AnalysisProgram* analysis,
    IrProgram* program,
    AnalysisEntityId entry,
    AnalysisInstantiationId instantiation,
    IrExecutionArgument* arguments,
    u32 argument_count,
    IrExecutionOptions options)
{
    if (!execution_arena ||
        !analysis || !program || !analysis->module_results ||
        !program->modules ||
        analysis->module_count != program->module_count ||
        (argument_count && !arguments))
    {
        return ir_interpreter_trap(
            0,
            IR_EXECUTION_TRAP_INVALID_PROGRAM,
            0);
    }
    u64 max_steps = options.max_steps ?
        options.max_steps : UINT64_C(1000000);
    u32 max_call_depth = options.max_call_depth ?
        options.max_call_depth : 1024;
    TemporalArena scratch = arena_begin_temporal(execution_arena);
    IrExecutionFrame* frames = arena_allocate(
        scratch.arena,
        IrExecutionFrame,
        max_call_depth);
    for (u32 frame_index = 0;
        frame_index < max_call_depth;
        frame_index += 1)
    {
        frames[frame_index] = (IrExecutionFrame){0};
    }
    IrExecutionTarget entry_target = ir_interpreter_function_find(
        analysis,
        program,
        entry,
        instantiation);
    if (!entry_target.function)
    {
        arena_set_position(scratch.arena, scratch.position);
        return ir_interpreter_trap(
            0,
            IR_EXECUTION_TRAP_FUNCTION_NOT_FOUND,
            0);
    }
    if (entry_target.function->state != IR_FUNCTION_LOWERED)
    {
        IrExecutionResult result = ir_interpreter_trap(
            0,
            IR_EXECUTION_TRAP_FUNCTION_NOT_LOWERED,
            0);
        result.function = entry;
        result.instantiation = instantiation;
        arena_set_position(scratch.arena, scratch.position);
        return result;
    }
    AnalysisType* entry_type = analysis_type_from_id(
        entry_target.analysis,
        entry_target.function->type);
    if (entry_type->kind != ANALYSIS_TYPE_FUNCTION ||
        entry_type->as.function.argument_count != argument_count)
    {
        IrExecutionResult result = ir_interpreter_trap(
            0,
            IR_EXECUTION_TRAP_ARGUMENT_COUNT,
            0);
        result.function = entry;
        result.instantiation = instantiation;
        arena_set_position(scratch.arena, scratch.position);
        return result;
    }
    IrRuntimeValue* entry_arguments = arena_allocate(
        scratch.arena,
        IrRuntimeValue,
        argument_count);
    for (u32 argument_index = 0;
        argument_index < argument_count;
        argument_index += 1)
    {
        entry_arguments[argument_index] = (IrRuntimeValue){
            .bits = arguments[argument_index].bits,
            .kind = IR_RUNTIME_VALUE_SCALAR,
            .initialized = true,
        };
    }
    if (!ir_interpreter_frame_prepare(
            scratch.arena,
            frames,
            entry_target,
            entry_arguments,
            argument_count,
            IR_VALUE_ID_INVALID))
    {
        IrExecutionResult result = ir_interpreter_trap(
            0,
            IR_EXECUTION_TRAP_ARGUMENT_COUNT,
            0);
        result.function = entry;
        result.instantiation = instantiation;
        arena_set_position(scratch.arena, scratch.position);
        return result;
    }

    u32 depth = 1;
    u64 step_count = 0;
    while (depth)
    {
        IrExecutionFrame* frame = frames + depth - 1;
        if (step_count >= max_steps)
        {
            IrExecutionResult result = ir_interpreter_trap(
                frame,
                IR_EXECUTION_TRAP_STEP_LIMIT,
                step_count);
            arena_set_position(scratch.arena, scratch.position);
            return result;
        }
        if (frame->instruction.value >=
            frame->function->instruction_count)
        {
            IrExecutionResult result = ir_interpreter_trap(
                frame,
                IR_EXECUTION_TRAP_INVALID_PROGRAM,
                step_count);
            arena_set_position(scratch.arena, scratch.position);
            return result;
        }
        IrInstruction* instruction =
            frame->function->instructions + frame->instruction.value;
        step_count += 1;
        if (!ir_interpreter_operands_ready(frame, instruction))
        {
            IrExecutionResult result = ir_interpreter_trap(
                frame,
                IR_EXECUTION_TRAP_UNINITIALIZED_VALUE,
                step_count);
            arena_set_position(scratch.arena, scratch.position);
            return result;
        }

        IrRuntimeValue produced = {
            .kind = IR_RUNTIME_VALUE_SCALAR,
            .initialized = true,
        };
        bool advance = true;
        IrExecutionTrap operation_trap = IR_EXECUTION_TRAP_NONE;
        switch (instruction->opcode)
        {
            case IR_OPCODE_ARGUMENT:
            {
                if (instruction->immediate_count != 1 ||
                    instruction->immediates[0] >=
                        frame->argument_count)
                {
                    operation_trap =
                        IR_EXECUTION_TRAP_ARGUMENT_COUNT;
                    break;
                }
                produced = frame->arguments[
                    instruction->immediates[0]];
            } break;
            case IR_OPCODE_CONSTANT_INTEGER:
            case IR_OPCODE_ENUM:
            {
                if (instruction->immediate_count != 1)
                {
                    operation_trap =
                        IR_EXECUTION_TRAP_INVALID_PROGRAM;
                    break;
                }
                produced.bits = instruction->immediate_is_negative ?
                    0 - instruction->immediates[0] :
                    instruction->immediates[0];
                produced.bits = ir_interpreter_normalize_integer(
                    frame->analysis,
                    instruction->type,
                    produced.bits);
            } break;
            case IR_OPCODE_CONSTANT_FLOAT:
            {
                if (instruction->immediate_count != 1)
                {
                    operation_trap =
                        IR_EXECUTION_TRAP_INVALID_PROGRAM;
                    break;
                }
                produced.bits = instruction->immediates[0];
            } break;
            case IR_OPCODE_FUNCTION:
            {
                produced.kind = IR_RUNTIME_VALUE_FUNCTION;
                produced.entity = instruction->entity;
                produced.instantiation = instruction->instantiation;
            } break;
            case IR_OPCODE_CAST:
            {
                if (!ir_interpreter_cast(
                        frame,
                        instruction,
                        &produced.bits))
                {
                    operation_trap =
                        IR_EXECUTION_TRAP_UNSUPPORTED_INSTRUCTION;
                }
            } break;
            case IR_OPCODE_UNARY:
            {
                IrValueId operand_id = instruction->operands[0];
                u64 operand = frame->values[operand_id.value].bits;
                AnalysisType* type = analysis_type_from_id(
                    frame->analysis,
                    instruction->type);
                if (type->kind == ANALYSIS_TYPE_FLOAT)
                {
                    f64 value = ir_interpreter_float_read(
                        operand,
                        type->as.float_bit_width);
                    if (instruction->ast_operation ==
                        AST_NODE_UNARY_MINUS)
                    {
                        value = -value;
                    }
                    else if (instruction->ast_operation !=
                        AST_NODE_UNARY_PLUS)
                    {
                        operation_trap =
                            IR_EXECUTION_TRAP_UNSUPPORTED_INSTRUCTION;
                    }
                    produced.bits = ir_interpreter_float_write(
                        value,
                        type->as.float_bit_width);
                }
                else
                {
                    switch (instruction->ast_operation)
                    {
                        case AST_NODE_UNARY_MINUS:
                            produced.bits = 0 - operand;
                            break;
                        case AST_NODE_UNARY_PLUS:
                            produced.bits = operand;
                            break;
                        case AST_NODE_UNARY_LOGICAL_NOT:
                            produced.bits = !operand;
                            break;
                        case AST_NODE_UNARY_BITWISE_NOT:
                            produced.bits = ~operand;
                            break;
                        default:
                            operation_trap =
                                IR_EXECUTION_TRAP_UNSUPPORTED_INSTRUCTION;
                            break;
                    }
                    produced.bits = ir_interpreter_normalize_integer(
                        frame->analysis,
                        instruction->type,
                        produced.bits);
                }
            } break;
            case IR_OPCODE_BINARY:
            {
                AnalysisType* operand_type = analysis_type_from_id(
                    frame->analysis,
                    frame->function->values[
                        instruction->operands[0].value].type);
                bool success = operand_type->kind ==
                    ANALYSIS_TYPE_FLOAT ?
                    ir_interpreter_float_binary(
                        frame,
                        instruction,
                        &produced.bits,
                        &operation_trap) :
                    ir_interpreter_integer_binary(
                        frame,
                        instruction,
                        &produced.bits,
                        &operation_trap);
                BUSTER_UNUSED(success);
            } break;
            case IR_OPCODE_CALL:
            {
                if (depth >= max_call_depth)
                {
                    operation_trap =
                        IR_EXECUTION_TRAP_CALL_DEPTH_LIMIT;
                    break;
                }
                IrExecutionTarget callee =
                    ir_interpreter_function_find(
                        analysis,
                        program,
                        instruction->entity,
                        instruction->instantiation);
                if (!callee.function)
                {
                    operation_trap =
                        IR_EXECUTION_TRAP_FUNCTION_NOT_FOUND;
                    break;
                }
                if (callee.function->state != IR_FUNCTION_LOWERED)
                {
                    operation_trap =
                        IR_EXECUTION_TRAP_FUNCTION_NOT_LOWERED;
                    break;
                }
                u32 callee_argument_count =
                    instruction->operand_count - 1;
                for (u32 argument_index = 0;
                    argument_index < callee_argument_count;
                    argument_index += 1)
                {
                    frame->transition_values[argument_index] =
                        frame->values[instruction->operands[
                            argument_index + 1].value];
                }
                IrExecutionFrame* callee_frame = frames + depth;
                if (!ir_interpreter_frame_prepare(
                        scratch.arena,
                        callee_frame,
                        callee,
                        frame->transition_values,
                        callee_argument_count,
                        instruction->result))
                {
                    operation_trap =
                        IR_EXECUTION_TRAP_ARGUMENT_COUNT;
                    break;
                }
                frame->instruction = instruction->next;
                depth += 1;
                advance = false;
            } break;
            case IR_OPCODE_BRANCH:
            {
                IrBlockId predecessor = frame->block;
                if (instruction->target_count != 1 ||
                    !ir_interpreter_block_enter(
                        frame,
                        instruction->targets[0],
                        predecessor))
                {
                    operation_trap =
                        IR_EXECUTION_TRAP_INVALID_PROGRAM;
                }
                advance = false;
            } break;
            case IR_OPCODE_BRANCH_IF:
            {
                if (instruction->target_count != 2)
                {
                    operation_trap =
                        IR_EXECUTION_TRAP_INVALID_PROGRAM;
                    break;
                }
                IrBlockId predecessor = frame->block;
                u64 condition = frame->values[
                    instruction->operands[0].value].bits;
                IrBlockId target =
                    instruction->targets[condition ? 0 : 1];
                if (!ir_interpreter_block_enter(
                        frame,
                        target,
                        predecessor))
                {
                    operation_trap =
                        IR_EXECUTION_TRAP_INVALID_PROGRAM;
                }
                advance = false;
            } break;
            case IR_OPCODE_SWITCH:
            {
                if (!instruction->target_count ||
                    instruction->target_count !=
                        instruction->immediate_count + 1)
                {
                    operation_trap =
                        IR_EXECUTION_TRAP_INVALID_PROGRAM;
                    break;
                }
                u64 switched = frame->values[
                    instruction->operands[0].value].bits;
                u32 target_index = instruction->immediate_count;
                for (u32 value_index = 0;
                    value_index < instruction->immediate_count;
                    value_index += 1)
                {
                    if (instruction->immediates[value_index] ==
                        switched)
                    {
                        target_index = value_index;
                        break;
                    }
                }
                IrBlockId predecessor = frame->block;
                if (!ir_interpreter_block_enter(
                        frame,
                        instruction->targets[target_index],
                        predecessor))
                {
                    operation_trap =
                        IR_EXECUTION_TRAP_INVALID_PROGRAM;
                }
                advance = false;
            } break;
            case IR_OPCODE_RETURN:
            {
                IrRuntimeValue returned = {0};
                bool has_value = instruction->operand_count == 1;
                if (has_value)
                {
                    returned = frame->values[
                        instruction->operands[0].value];
                }
                IrValueId caller_result = frame->caller_result;
                AnalysisType* function_type = analysis_type_from_id(
                    frame->analysis,
                    frame->function->type);
                AnalysisModuleId type_module =
                    frame->analysis->module.id;
                AnalysisTypeId return_type =
                    function_type->as.function.return_type;
                depth -= 1;
                if (!depth)
                {
                    IrExecutionResult result = {
                        .function = entry,
                        .instantiation = instantiation,
                        .type_module = type_module,
                        .type = return_type,
                        .instruction = instruction->id,
                        .bits = returned.bits,
                        .step_count = step_count,
                        .trap = IR_EXECUTION_TRAP_NONE,
                        .has_value = has_value,
                    };
                    arena_set_position(
                        scratch.arena,
                        scratch.position);
                    return result;
                }
                IrExecutionFrame* caller = frames + depth - 1;
                if (caller_result.value != IR_ID_UNDERLYING_INVALID)
                {
                    caller->values[caller_result.value] = returned;
                }
                advance = false;
            } break;
            case IR_OPCODE_UNREACHABLE:
            {
                operation_trap =
                    IR_EXECUTION_TRAP_UNREACHABLE;
            } break;
            case IR_OPCODE_LOCAL:
            case IR_OPCODE_LOAD:
            case IR_OPCODE_STORE:
            case IR_OPCODE_CONSTANT_STRING:
            case IR_OPCODE_ARRAY:
            case IR_OPCODE_AGGREGATE:
            case IR_OPCODE_INDEX:
            case IR_OPCODE_SLICE:
            case IR_OPCODE_FIELD:
            case IR_OPCODE_ADDRESS_OF:
            case IR_OPCODE_DEREFERENCE:
            case IR_OPCODE_REVERSE:
            case IR_OPCODE_ITERATOR_BEGIN:
            case IR_OPCODE_ITERATOR_NEXT:
            case IR_OPCODE_ITERATOR_VALUE:
            {
                operation_trap =
                    IR_EXECUTION_TRAP_UNSUPPORTED_INSTRUCTION;
            } break;
            case IR_OPCODE_UNDEFINED:
            {
                operation_trap =
                    IR_EXECUTION_TRAP_UNINITIALIZED_VALUE;
            } break;
            case IR_OPCODE_COUNT:
            {
                operation_trap =
                    IR_EXECUTION_TRAP_INVALID_PROGRAM;
            } break;
        }
        if (operation_trap != IR_EXECUTION_TRAP_NONE)
        {
            IrExecutionResult result = ir_interpreter_trap(
                frame,
                operation_trap,
                step_count);
            arena_set_position(scratch.arena, scratch.position);
            return result;
        }
        if (instruction->result.value !=
                IR_ID_UNDERLYING_INVALID &&
            instruction->opcode != IR_OPCODE_CALL)
        {
            frame->values[instruction->result.value] = produced;
        }
        if (advance)
        {
            frame->instruction = instruction->next;
        }
    }
    arena_set_position(scratch.arena, scratch.position);
    return ir_interpreter_trap(
        0,
        IR_EXECUTION_TRAP_INVALID_PROGRAM,
        step_count);
}

#if BUSTER_INCLUDE_TESTS
BUSTER_GLOBAL_LOCAL AnalysisEntity* ir_interpreter_test_entity_find(
    AnalysisResult* analysis,
    String8 name)
{
    for (u32 entity_index = 0;
        entity_index < analysis->module.entity_count;
        entity_index += 1)
    {
        AnalysisEntity* entity =
            analysis->module.entities + entity_index;
        if (entity->kind == ANALYSIS_ENTITY_CODE &&
            string_equal(entity->name, name))
        {
            return entity;
        }
    }
    return 0;
}

UnitTestResult ir_interpreter_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena temporary = arena_begin_temporal(arguments->arena);
    Arena* expression_arena = arena_create((ArenaCreation){0});
    BUSTER_CHECK(expression_arena);

    String8 scalar_source = S8(
        "code choose : fn (a: s32, b: s32) s32\n"
        "{\n"
        "    data value: s32 = a;\n"
        "    if (a < b)\n"
        "    {\n"
        "        value += b;\n"
        "    }\n"
        "    else\n"
        "    {\n"
        "        value -= b;\n"
        "    }\n"
        "    return value;\n"
        "}\n"
        "code divide : fn (value: s32) s32\n"
        "{\n"
        "    return 12 / value;\n"
        "}\n"
        "code float_value : fn () f64\n"
        "{\n"
        "    return 1.5 * 2.0;\n"
        "}\n"
        "code forever : fn () void\n"
        "{\n"
        "    loop\n"
        "    {\n"
        "    }\n"
        "}\n"
        "code main : fn () s32\n"
        "{\n"
        "    return choose(3, 4) * 2;\n"
        "}\n");
    TokenizerResult scalar_tokens = tokenize(
        arguments->arena,
        scalar_source.pointer,
        scalar_source.length);
    ParserResult scalar_parser = parser_parse(
        arguments->arena,
        expression_arena,
        scalar_source,
        scalar_tokens);
    BUSTER_TEST(arguments, scalar_tokens.error_count == 0);
    BUSTER_TEST(arguments, scalar_parser.diagnostic_count == 0);
    AnalysisSourceInput scalar_input = {
        .path = S8("interpreter-scalar.bbb"),
        .parser = &scalar_parser,
    };
    AnalysisResult scalar_analysis = analysis_index_module(
        arguments->arena,
        (AnalysisModuleId){ .value = 700 },
        S8("interpreter-scalar"),
        &scalar_input,
        1);
    analysis_resolve_module_interfaces(
        arguments->arena,
        &scalar_analysis);
    analysis_analyze_bodies(arguments->arena, &scalar_analysis);
    AnalysisResult* scalar_modules[] = { &scalar_analysis };
    AnalysisProgram scalar_program_analysis = {
        .module_results = scalar_modules,
        .module_count = BUSTER_ARRAY_LENGTH(scalar_modules),
    };
    IrProgram scalar_program = ir_generate_program(
        arguments->arena,
        &scalar_program_analysis);
    BUSTER_TEST(arguments, scalar_analysis.diagnostic_count == 0);
    BUSTER_TEST(arguments, scalar_program.rejected_function_count == 0);
    IrValidationResult scalar_validation = ir_validate_module(
        &scalar_analysis,
        scalar_program.modules);
    BUSTER_TEST(arguments,
        scalar_validation.error == IR_VALIDATION_NONE);

    AnalysisEntity* main_entity = ir_interpreter_test_entity_find(
        &scalar_analysis,
        S8("main"));
    BUSTER_TEST(arguments, main_entity != 0);
    if (main_entity)
    {
        IrExecutionResult executed = ir_execute(
            expression_arena,
            &scalar_program_analysis,
            &scalar_program,
            main_entity->id,
            ANALYSIS_INSTANTIATION_ID_INVALID,
            0,
            0,
            (IrExecutionOptions){0});
        BUSTER_TEST(arguments,
            executed.trap == IR_EXECUTION_TRAP_NONE);
        BUSTER_TEST(arguments, executed.has_value);
        BUSTER_TEST(arguments, executed.bits == 14);

        IrExecutionResult depth_limited = ir_execute(
            expression_arena,
            &scalar_program_analysis,
            &scalar_program,
            main_entity->id,
            ANALYSIS_INSTANTIATION_ID_INVALID,
            0,
            0,
            (IrExecutionOptions){
                .max_call_depth = 1,
            });
        BUSTER_TEST(arguments,
            depth_limited.trap ==
                IR_EXECUTION_TRAP_CALL_DEPTH_LIMIT);
    }

    AnalysisEntity* choose_entity = ir_interpreter_test_entity_find(
        &scalar_analysis,
        S8("choose"));
    BUSTER_TEST(arguments, choose_entity != 0);
    if (choose_entity)
    {
        IrExecutionArgument choose_less_arguments[] = {
            { .bits = 3 },
            { .bits = 4 },
        };
        IrExecutionResult chose_less = ir_execute(
            expression_arena,
            &scalar_program_analysis,
            &scalar_program,
            choose_entity->id,
            ANALYSIS_INSTANTIATION_ID_INVALID,
            choose_less_arguments,
            BUSTER_ARRAY_LENGTH(choose_less_arguments),
            (IrExecutionOptions){0});
        BUSTER_TEST(arguments,
            chose_less.trap == IR_EXECUTION_TRAP_NONE);
        BUSTER_TEST(arguments, chose_less.bits == 7);

        IrExecutionArgument choose_greater_arguments[] = {
            { .bits = 8 },
            { .bits = 3 },
        };
        IrExecutionResult chose_greater = ir_execute(
            expression_arena,
            &scalar_program_analysis,
            &scalar_program,
            choose_entity->id,
            ANALYSIS_INSTANTIATION_ID_INVALID,
            choose_greater_arguments,
            BUSTER_ARRAY_LENGTH(choose_greater_arguments),
            (IrExecutionOptions){0});
        BUSTER_TEST(arguments,
            chose_greater.trap == IR_EXECUTION_TRAP_NONE);
        BUSTER_TEST(arguments, chose_greater.bits == 5);
    }

    AnalysisEntity* divide_entity = ir_interpreter_test_entity_find(
        &scalar_analysis,
        S8("divide"));
    BUSTER_TEST(arguments, divide_entity != 0);
    if (divide_entity)
    {
        IrExecutionArgument zero = {0};
        IrExecutionResult divided_by_zero = ir_execute(
            expression_arena,
            &scalar_program_analysis,
            &scalar_program,
            divide_entity->id,
            ANALYSIS_INSTANTIATION_ID_INVALID,
            &zero,
            1,
            (IrExecutionOptions){0});
        BUSTER_TEST(arguments,
            divided_by_zero.trap ==
                IR_EXECUTION_TRAP_DIVISION_BY_ZERO);
    }

    AnalysisEntity* float_entity = ir_interpreter_test_entity_find(
        &scalar_analysis,
        S8("float_value"));
    BUSTER_TEST(arguments, float_entity != 0);
    if (float_entity)
    {
        IrExecutionResult float_result = ir_execute(
            expression_arena,
            &scalar_program_analysis,
            &scalar_program,
            float_entity->id,
            ANALYSIS_INSTANTIATION_ID_INVALID,
            0,
            0,
            (IrExecutionOptions){0});
        BUSTER_TEST(arguments,
            float_result.trap == IR_EXECUTION_TRAP_NONE);
        BUSTER_TEST(arguments,
            ir_interpreter_float_read(float_result.bits, 64) ==
                3.0);
    }

    AnalysisEntity* forever_entity = ir_interpreter_test_entity_find(
        &scalar_analysis,
        S8("forever"));
    BUSTER_TEST(arguments, forever_entity != 0);
    if (forever_entity)
    {
        IrExecutionResult step_limited = ir_execute(
            expression_arena,
            &scalar_program_analysis,
            &scalar_program,
            forever_entity->id,
            ANALYSIS_INSTANTIATION_ID_INVALID,
            0,
            0,
            (IrExecutionOptions){
                .max_steps = 32,
            });
        BUSTER_TEST(arguments,
            step_limited.trap ==
                IR_EXECUTION_TRAP_STEP_LIMIT);
        BUSTER_TEST(arguments, step_limited.step_count == 32);
    }

    String8 math_source = S8(
        "code identity : fn (value: $T) $T\n"
        "{\n"
        "    return value;\n"
        "}\n");
    String8 app_source = S8(
        "import math = \"core/math\";\n"
        "code main : fn () s32\n"
        "{\n"
        "    return math.identity(40) + 2;\n"
        "}\n");
    TokenizerResult math_tokens = tokenize(
        arguments->arena,
        math_source.pointer,
        math_source.length);
    ParserResult math_parser = parser_parse(
        arguments->arena,
        expression_arena,
        math_source,
        math_tokens);
    TokenizerResult app_tokens = tokenize(
        arguments->arena,
        app_source.pointer,
        app_source.length);
    ParserResult app_parser = parser_parse(
        arguments->arena,
        expression_arena,
        app_source,
        app_tokens);
    BUSTER_TEST(arguments, math_tokens.error_count == 0);
    BUSTER_TEST(arguments, math_parser.diagnostic_count == 0);
    BUSTER_TEST(arguments, app_tokens.error_count == 0);
    BUSTER_TEST(arguments, app_parser.diagnostic_count == 0);
    AnalysisSourceInput math_input = {
        .path = S8("math.bbb"),
        .parser = &math_parser,
    };
    AnalysisSourceInput app_input = {
        .path = S8("app.bbb"),
        .parser = &app_parser,
    };
    AnalysisResult math_analysis = analysis_index_module(
        arguments->arena,
        (AnalysisModuleId){ .value = 710 },
        S8("core/math"),
        &math_input,
        1);
    AnalysisResult app_analysis = analysis_index_module(
        arguments->arena,
        (AnalysisModuleId){ .value = 711 },
        S8("app"),
        &app_input,
        1);
    AnalysisResult* cross_modules[] = {
        &app_analysis,
        &math_analysis,
    };
    analysis_resolve_program_interfaces(
        arguments->arena,
        cross_modules,
        BUSTER_ARRAY_LENGTH(cross_modules));
    analysis_analyze_bodies(arguments->arena, &app_analysis);
    analysis_analyze_bodies(arguments->arena, &math_analysis);
    AnalysisProgram cross_analysis = {
        .module_results = cross_modules,
        .module_count = BUSTER_ARRAY_LENGTH(cross_modules),
    };
    IrProgram cross_program = ir_generate_program(
        arguments->arena,
        &cross_analysis);
    BUSTER_TEST(arguments, app_analysis.diagnostic_count == 0);
    BUSTER_TEST(arguments, math_analysis.diagnostic_count == 0);
    BUSTER_TEST(arguments, math_analysis.instantiation_count == 1);
    AnalysisEntity* cross_main = ir_interpreter_test_entity_find(
        &app_analysis,
        S8("main"));
    BUSTER_TEST(arguments, cross_main != 0);
    if (cross_main)
    {
        IrExecutionResult cross_result = ir_execute(
            expression_arena,
            &cross_analysis,
            &cross_program,
            cross_main->id,
            ANALYSIS_INSTANTIATION_ID_INVALID,
            0,
            0,
            (IrExecutionOptions){0});
        BUSTER_TEST(arguments,
            cross_result.trap == IR_EXECUTION_TRAP_NONE);
        BUSTER_TEST(arguments, cross_result.has_value);
        BUSTER_TEST(arguments, cross_result.bits == 42);
    }

    BUSTER_CHECK(arena_destroy(expression_arena, 1));
    arena_set_position(temporary.arena, temporary.position);
    return result;
}
#endif
