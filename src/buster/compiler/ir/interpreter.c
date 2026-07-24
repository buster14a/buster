#include <buster/compiler/ir/interpreter.h>

#include <buster/string.h>

typedef enum IrRuntimeValueKind
{
    IR_RUNTIME_VALUE_SCALAR,
    IR_RUNTIME_VALUE_FUNCTION,
    IR_RUNTIME_VALUE_PLACE,
    IR_RUNTIME_VALUE_AGGREGATE,
    IR_RUNTIME_VALUE_ADDRESS,
    IR_RUNTIME_VALUE_SLICE,
    IR_RUNTIME_VALUE_RANGE,
    IR_RUNTIME_VALUE_ITERATOR,
    IR_RUNTIME_VALUE_KIND_COUNT,
} IrRuntimeValueKind;

typedef struct IrRuntimeObject IrRuntimeObject;
typedef struct IrRuntimeStoredValue IrRuntimeStoredValue;
typedef struct IrRuntimeIterator IrRuntimeIterator;
typedef struct IrRuntimeValue IrRuntimeValue;
struct IrRuntimeValue
{
    AnalysisEntityId entity;
    AnalysisInstantiationId instantiation;
    AnalysisModuleId type_module;
    AnalysisTypeId type;
    IrRuntimeObject* object;
    IrRuntimeIterator* iterator;
    u64 bits;
    u64 offset;
    u64 length;
    u64 element_size;
    IrRuntimeValueKind kind;
    bool initialized;
    bool reversed;
    u8 reserved[2];
};

struct IrRuntimeObject
{
    IrRuntimeStoredValue* first_stored_value;
    u8* bytes;
    u8* initialized;
    u64 size;
};

struct IrRuntimeStoredValue
{
    IrRuntimeStoredValue* next;
    IrRuntimeValue value;
    u64 offset;
    u64 size;
};

struct IrRuntimeIterator
{
    IrRuntimeValue iterable;
    IrRuntimeValue current;
    u64 index;
    bool has_current;
    u8 reserved[7];
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

BUSTER_GLOBAL_LOCAL AnalysisResult* ir_interpreter_analysis_find(
    AnalysisResult* analysis,
    AnalysisModuleId module)
{
    if (analysis->module.id.value == module.value)
    {
        return analysis;
    }
    for (u32 module_index = 0;
        module_index < analysis->program_module_count;
        module_index += 1)
    {
        AnalysisResult* candidate =
            analysis->program_modules[module_index];
        if (candidate &&
            candidate->module.id.value == module.value)
        {
            return candidate;
        }
    }
    return 0;
}

BUSTER_GLOBAL_LOCAL u64 ir_interpreter_type_size(
    AnalysisResult* analysis,
    AnalysisTypeId type_id)
{
    AnalysisType* type = analysis_type_from_id(analysis, type_id);
    if (type->layout.state == ANALYSIS_LAYOUT_RESOLVED)
    {
        return type->layout.size;
    }
    switch (type->kind)
    {
        case ANALYSIS_TYPE_BOOL: return 1;
        case ANALYSIS_TYPE_INTEGER:
            return (type->as.integer.bit_width + 7) / 8;
        case ANALYSIS_TYPE_FLOAT:
            return type->as.float_bit_width / 8;
        case ANALYSIS_TYPE_ENUM: return 4;
        case ANALYSIS_TYPE_POINTER:
        case ANALYSIS_TYPE_FUNCTION: return 8;
        case ANALYSIS_TYPE_SLICE: return 16;
        case ANALYSIS_TYPE_RANGE:
        {
            return ir_interpreter_type_size(
                analysis,
                type->as.element_type) * 2;
        }
        case ANALYSIS_TYPE_ARRAY:
        {
            return ir_interpreter_type_size(
                analysis,
                type->as.array.element_type) *
                type->as.array.count;
        }
        default: return 0;
    }
}

BUSTER_GLOBAL_LOCAL IrRuntimeObject* ir_interpreter_object_create(
    Arena* arena,
    u64 size)
{
    IrRuntimeObject* object = arena_allocate(
        arena,
        IrRuntimeObject,
        1);
    *object = (IrRuntimeObject){ .size = size };
    if (size)
    {
        object->bytes = arena_allocate(arena, u8, size);
        object->initialized = arena_allocate(arena, u8, size);
        for (u64 index = 0; index < size; index += 1)
        {
            object->bytes[index] = 0;
            object->initialized[index] = 0;
        }
    }
    return object;
}

BUSTER_GLOBAL_LOCAL bool ir_interpreter_object_range_valid(
    IrRuntimeObject* object,
    u64 offset,
    u64 size)
{
    return object && offset <= object->size &&
        size <= object->size - offset;
}

BUSTER_GLOBAL_LOCAL bool ir_interpreter_ranges_overlap(
    u64 left_offset,
    u64 left_size,
    u64 right_offset,
    u64 right_size)
{
    return left_size && right_size &&
        left_offset < right_offset + right_size &&
        right_offset < left_offset + left_size;
}

BUSTER_GLOBAL_LOCAL void ir_interpreter_stored_values_clear(
    IrRuntimeObject* object,
    u64 offset,
    u64 size)
{
    IrRuntimeStoredValue** link = &object->first_stored_value;
    while (*link)
    {
        IrRuntimeStoredValue* stored = *link;
        if (ir_interpreter_ranges_overlap(
                stored->offset,
                stored->size,
                offset,
                size))
        {
            *link = stored->next;
        }
        else
        {
            link = &stored->next;
        }
    }
}

BUSTER_GLOBAL_LOCAL void ir_interpreter_stored_value_add(
    Arena* arena,
    IrRuntimeObject* object,
    u64 offset,
    u64 size,
    IrRuntimeValue value)
{
    IrRuntimeStoredValue* stored = arena_allocate(
        arena,
        IrRuntimeStoredValue,
        1);
    *stored = (IrRuntimeStoredValue){
        .next = object->first_stored_value,
        .value = value,
        .offset = offset,
        .size = size,
    };
    object->first_stored_value = stored;
}

BUSTER_GLOBAL_LOCAL IrRuntimeStoredValue*
ir_interpreter_stored_value_find(
    IrRuntimeObject* object,
    u64 offset,
    u64 size)
{
    for (IrRuntimeStoredValue* stored =
            object->first_stored_value;
        stored;
        stored = stored->next)
    {
        if (stored->offset == offset && stored->size == size)
        {
            return stored;
        }
    }
    return 0;
}

BUSTER_GLOBAL_LOCAL void ir_interpreter_object_region_copy(
    Arena* arena,
    IrRuntimeObject* destination,
    u64 destination_offset,
    IrRuntimeObject* source,
    u64 source_offset,
    u64 size)
{
    for (u64 index = 0; index < size; index += 1)
    {
        destination->bytes[destination_offset + index] =
            source->bytes[source_offset + index];
        destination->initialized[destination_offset + index] =
            source->initialized[source_offset + index];
    }
    for (IrRuntimeStoredValue* stored =
            source->first_stored_value;
        stored;
        stored = stored->next)
    {
        if (stored->offset >= source_offset &&
            stored->size <=
                source_offset + size - stored->offset)
        {
            ir_interpreter_stored_value_add(
                arena,
                destination,
                destination_offset +
                    stored->offset - source_offset,
                stored->size,
                stored->value);
        }
    }
}

BUSTER_GLOBAL_LOCAL bool ir_interpreter_memory_write(
    Arena* arena,
    IrRuntimeObject* object,
    u64 offset,
    u64 size,
    IrRuntimeValue value)
{
    if (!ir_interpreter_object_range_valid(object, offset, size))
    {
        return false;
    }
    ir_interpreter_stored_values_clear(object, offset, size);
    if (!value.initialized)
    {
        for (u64 index = 0; index < size; index += 1)
        {
            object->initialized[offset + index] = 0;
        }
        return true;
    }
    if (value.kind == IR_RUNTIME_VALUE_SCALAR)
    {
        for (u64 index = 0; index < size; index += 1)
        {
            object->bytes[offset + index] =
                index < 8 ?
                    (u8)(value.bits >> (u32)(index * 8)) : 0;
            object->initialized[offset + index] = 1;
        }
        return true;
    }
    if (value.kind == IR_RUNTIME_VALUE_AGGREGATE &&
        ir_interpreter_object_range_valid(
            value.object,
            value.offset,
            size))
    {
        ir_interpreter_object_region_copy(
            arena,
            object,
            offset,
            value.object,
            value.offset,
            size);
        return true;
    }
    for (u64 index = 0; index < size; index += 1)
    {
        object->bytes[offset + index] = 0;
        object->initialized[offset + index] = 1;
    }
    ir_interpreter_stored_value_add(
        arena,
        object,
        offset,
        size,
        value);
    return true;
}

BUSTER_GLOBAL_LOCAL bool ir_interpreter_memory_read(
    Arena* arena,
    AnalysisResult* analysis,
    AnalysisTypeId type_id,
    IrRuntimeObject* object,
    u64 offset,
    IrRuntimeValue* value_out)
{
    u64 size = ir_interpreter_type_size(analysis, type_id);
    if (!ir_interpreter_object_range_valid(object, offset, size))
    {
        return false;
    }
    AnalysisType* type = analysis_type_from_id(analysis, type_id);
    bool scalar =
        type->kind == ANALYSIS_TYPE_BOOL ||
        type->kind == ANALYSIS_TYPE_INTEGER ||
        type->kind == ANALYSIS_TYPE_FLOAT ||
        type->kind == ANALYSIS_TYPE_ENUM;
    if (scalar)
    {
        u64 bits = 0;
        for (u64 index = 0; index < size; index += 1)
        {
            if (!object->initialized[offset + index])
            {
                return false;
            }
            if (index < 8)
            {
                bits |= (u64)object->bytes[offset + index] <<
                    (u32)(index * 8);
            }
        }
        *value_out = (IrRuntimeValue){
            .bits = bits,
            .kind = IR_RUNTIME_VALUE_SCALAR,
            .initialized = true,
        };
        return true;
    }
    if (type->kind == ANALYSIS_TYPE_ARRAY ||
        type->kind == ANALYSIS_TYPE_STRUCT ||
        type->kind == ANALYSIS_TYPE_UNION)
    {
        IrRuntimeObject* copy =
            ir_interpreter_object_create(arena, size);
        ir_interpreter_object_region_copy(
            arena,
            copy,
            0,
            object,
            offset,
            size);
        *value_out = (IrRuntimeValue){
            .object = copy,
            .length = size,
            .kind = IR_RUNTIME_VALUE_AGGREGATE,
            .initialized = true,
        };
        return true;
    }
    IrRuntimeStoredValue* stored =
        ir_interpreter_stored_value_find(object, offset, size);
    if (!stored || !stored->value.initialized)
    {
        return false;
    }
    *value_out = stored->value;
    return true;
}

BUSTER_GLOBAL_LOCAL bool ir_interpreter_field(
    IrExecutionFrame* frame,
    AnalysisTypeId aggregate_type_id,
    u32 field_index,
    AnalysisField** field_out)
{
    AnalysisType* aggregate_type = analysis_type_from_id(
        frame->analysis,
        aggregate_type_id);
    if (aggregate_type->kind != ANALYSIS_TYPE_STRUCT &&
        aggregate_type->kind != ANALYSIS_TYPE_UNION)
    {
        return false;
    }
    AnalysisResult* declaration_analysis =
        ir_interpreter_analysis_find(
            frame->analysis,
            aggregate_type->as.declaration.module);
    if (!declaration_analysis ||
        aggregate_type->as.declaration.index.value >=
            declaration_analysis->module.entity_count)
    {
        return false;
    }
    AnalysisEntitySemantic* semantic =
        declaration_analysis->module.semantics +
        aggregate_type->as.declaration.index.value;
    if (field_index >= semantic->field_count)
    {
        return false;
    }
    *field_out = semantic->fields + field_index;
    return true;
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
            (!frame->values[operand.value].initialized &&
             !(instruction->opcode == IR_OPCODE_STORE &&
               operand_index == 1)))
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
    u64 value = 0;
    switch (instruction->binary_operation)
    {
        case IR_BINARY_INTEGER_ADD: value = left + right; break;
        case IR_BINARY_INTEGER_SUBTRACT: value = left - right; break;
        case IR_BINARY_INTEGER_MULTIPLY: value = left * right; break;
        case IR_BINARY_SIGNED_DIVIDE:
        case IR_BINARY_UNSIGNED_DIVIDE:
        case IR_BINARY_SIGNED_REMAINDER:
        case IR_BINARY_UNSIGNED_REMAINDER:
        {
            if (!right)
            {
                *trap_out = IR_EXECUTION_TRAP_DIVISION_BY_ZERO;
                return false;
            }
            bool divide =
                instruction->binary_operation == IR_BINARY_SIGNED_DIVIDE ||
                instruction->binary_operation == IR_BINARY_UNSIGNED_DIVIDE;
            bool signed_operation =
                instruction->binary_operation == IR_BINARY_SIGNED_DIVIDE ||
                instruction->binary_operation == IR_BINARY_SIGNED_REMAINDER;
            if (signed_operation)
            {
                bool left_negative =
                    ir_interpreter_integer_negative(left, width);
                bool right_negative =
                    ir_interpreter_integer_negative(right, width);
                u64 left_magnitude =
                    ir_interpreter_integer_magnitude(left, width);
                u64 right_magnitude =
                    ir_interpreter_integer_magnitude(right, width);
                if (divide)
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
                value = divide ? left / right : left % right;
            }
        } break;
        case IR_BINARY_SHIFT_LEFT:
        case IR_BINARY_SIGNED_SHIFT_RIGHT:
        case IR_BINARY_UNSIGNED_SHIFT_RIGHT:
        {
            if (right >= width)
            {
                *trap_out = IR_EXECUTION_TRAP_INVALID_SHIFT;
                return false;
            }
            if (instruction->binary_operation == IR_BINARY_SHIFT_LEFT)
            {
                value = left << (u32)right;
            }
            else if (instruction->binary_operation ==
                    IR_BINARY_SIGNED_SHIFT_RIGHT &&
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
        case IR_BINARY_INTEGER_EQUAL:
        case IR_BINARY_BOOLEAN_EQUAL:
            value = left == right;
            break;
        case IR_BINARY_INTEGER_NOT_EQUAL:
        case IR_BINARY_BOOLEAN_NOT_EQUAL:
            value = left != right;
            break;
        case IR_BINARY_SIGNED_LESS:
        case IR_BINARY_SIGNED_LESS_EQUAL:
        case IR_BINARY_SIGNED_GREATER:
        case IR_BINARY_SIGNED_GREATER_EQUAL:
        case IR_BINARY_UNSIGNED_LESS:
        case IR_BINARY_UNSIGNED_LESS_EQUAL:
        case IR_BINARY_UNSIGNED_GREATER:
        case IR_BINARY_UNSIGNED_GREATER_EQUAL:
        {
            bool signed_operation =
                instruction->binary_operation >= IR_BINARY_SIGNED_LESS &&
                instruction->binary_operation <= IR_BINARY_SIGNED_GREATER_EQUAL;
            s32 order = signed_operation ?
                ir_interpreter_signed_compare(left, right, width) :
                left < right ? -1 : left > right ? 1 : 0;
            IrBinaryOperation relative = signed_operation ?
                (IrBinaryOperation)(
                    instruction->binary_operation -
                    IR_BINARY_SIGNED_LESS) :
                (IrBinaryOperation)(
                    instruction->binary_operation -
                    IR_BINARY_UNSIGNED_LESS);
            value = relative == 0 ? order < 0 :
                relative == 1 ? order <= 0 :
                relative == 2 ? order > 0 :
                order >= 0;
        } break;
        case IR_BINARY_INTEGER_BITWISE_AND:
        case IR_BINARY_BOOLEAN_AND:
        {
            value = left & right;
        } break;
        case IR_BINARY_INTEGER_BITWISE_OR:
        case IR_BINARY_BOOLEAN_OR:
        {
            value = left | right;
        } break;
        case IR_BINARY_INTEGER_BITWISE_XOR: value = left ^ right; break;
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
    switch (instruction->binary_operation)
    {
        case IR_BINARY_FLOAT_ADD: value = left + right; break;
        case IR_BINARY_FLOAT_SUBTRACT: value = left - right; break;
        case IR_BINARY_FLOAT_MULTIPLY: value = left * right; break;
        case IR_BINARY_FLOAT_DIVIDE:
        {
            if (right == 0.0)
            {
                *trap_out = IR_EXECUTION_TRAP_DIVISION_BY_ZERO;
                return false;
            }
            value = left / right;
        } break;
        case IR_BINARY_FLOAT_EQUAL:
            comparison = true;
            comparison_value = left == right;
            break;
        case IR_BINARY_FLOAT_NOT_EQUAL:
            comparison = true;
            comparison_value = left != right;
            break;
        case IR_BINARY_FLOAT_LESS:
            comparison = true;
            comparison_value = left < right;
            break;
        case IR_BINARY_FLOAT_LESS_EQUAL:
            comparison = true;
            comparison_value = left <= right;
            break;
        case IR_BINARY_FLOAT_GREATER:
            comparison = true;
            comparison_value = left > right;
            break;
        case IR_BINARY_FLOAT_GREATER_EQUAL:
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

BUSTER_GLOBAL_LOCAL bool ir_interpreter_collection(
    IrExecutionFrame* frame,
    AnalysisTypeId type_id,
    IrRuntimeValue value,
    IrRuntimeObject** object_out,
    u64* offset_out,
    u64* count_out,
    u64* element_size_out)
{
    AnalysisType* type = analysis_type_from_id(
        frame->analysis,
        type_id);
    if (type->kind == ANALYSIS_TYPE_SLICE &&
        value.kind == IR_RUNTIME_VALUE_PLACE)
    {
        IrRuntimeStoredValue* stored =
            ir_interpreter_stored_value_find(
                value.object,
                value.offset,
                ir_interpreter_type_size(
                    frame->analysis,
                    type_id));
        if (!stored)
        {
            return false;
        }
        value = stored->value;
    }
    if (type->kind == ANALYSIS_TYPE_SLICE &&
        value.kind == IR_RUNTIME_VALUE_SLICE)
    {
        *object_out = value.object;
        *offset_out = value.offset;
        *count_out = value.length;
        *element_size_out = value.element_size;
        return true;
    }
    if (type->kind == ANALYSIS_TYPE_ARRAY ||
        type->kind == ANALYSIS_TYPE_INFERRED_ARRAY)
    {
        AnalysisTypeId element = type->kind == ANALYSIS_TYPE_ARRAY ?
            type->as.array.element_type : type->as.element_type;
        u64 element_size = ir_interpreter_type_size(
            frame->analysis,
            element);
        u64 count = type->kind == ANALYSIS_TYPE_ARRAY ?
            type->as.array.count : value.length;
        if ((value.kind != IR_RUNTIME_VALUE_AGGREGATE &&
             value.kind != IR_RUNTIME_VALUE_PLACE) ||
            !value.object || !element_size)
        {
            return false;
        }
        *object_out = value.object;
        *offset_out = value.offset;
        *count_out = count;
        *element_size_out = element_size;
        return true;
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool ir_interpreter_iterator_next(
    Arena* arena,
    IrExecutionFrame* frame,
    IrRuntimeIterator* iterator,
    IrExecutionTrap* trap_out)
{
    IrRuntimeValue iterable = iterator->iterable;
    AnalysisType* type = analysis_type_from_id(
        frame->analysis,
        iterable.type);
    iterator->has_current = false;
    if (type->kind == ANALYSIS_TYPE_RANGE &&
        iterable.kind == IR_RUNTIME_VALUE_RANGE)
    {
        AnalysisTypeId element_type = type->as.element_type;
        u32 width = ir_interpreter_type_width(
            frame->analysis,
            element_type);
        u64 mask = ir_interpreter_mask(width);
        u64 start = iterable.bits & mask;
        u64 end = iterable.length & mask;
        bool is_signed = ir_interpreter_type_signed(
            frame->analysis,
            element_type);
        s32 order = is_signed ?
            ir_interpreter_signed_compare(start, end, width) :
            start < end ? -1 : start > end ? 1 : 0;
        if (order > 0)
        {
            *trap_out = IR_EXECUTION_TRAP_INVALID_MEMORY;
            return false;
        }
        u64 count = (end - start) & mask;
        if (iterator->index >= count)
        {
            return true;
        }
        u64 element_index = iterable.reversed ?
            count - iterator->index - 1 : iterator->index;
        iterator->current = (IrRuntimeValue){
            .type_module = frame->analysis->module.id,
            .type = element_type,
            .bits = (start + element_index) & mask,
            .kind = IR_RUNTIME_VALUE_SCALAR,
            .initialized = true,
        };
        iterator->index += 1;
        iterator->has_current = true;
        return true;
    }

    IrRuntimeObject* object = 0;
    u64 offset = 0;
    u64 count = 0;
    u64 element_size = 0;
    if (!ir_interpreter_collection(
            frame,
            iterable.type,
            iterable,
            &object,
            &offset,
            &count,
            &element_size))
    {
        *trap_out = IR_EXECUTION_TRAP_UNSUPPORTED_INSTRUCTION;
        return false;
    }
    if (iterator->index >= count)
    {
        return true;
    }
    u64 element_index = iterable.reversed ?
        count - iterator->index - 1 : iterator->index;
    AnalysisTypeId element_type =
        type->kind == ANALYSIS_TYPE_ARRAY ?
            type->as.array.element_type : type->as.element_type;
    if (!ir_interpreter_memory_read(
            arena,
            frame->analysis,
            element_type,
            object,
            offset + element_index * element_size,
            &iterator->current))
    {
        *trap_out = IR_EXECUTION_TRAP_UNINITIALIZED_VALUE;
        return false;
    }
    iterator->index += 1;
    iterator->has_current = true;
    return true;
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
            case IR_OPCODE_CONSTANT_STRING:
            {
                AnalysisType* string_type = analysis_type_from_id(
                    frame->analysis,
                    instruction->type);
                if (string_type->kind != ANALYSIS_TYPE_SLICE &&
                    string_type->kind != ANALYSIS_TYPE_ARRAY &&
                    string_type->kind !=
                        ANALYSIS_TYPE_INFERRED_ARRAY)
                {
                    operation_trap =
                        IR_EXECUTION_TRAP_INVALID_PROGRAM;
                    break;
                }
                IrRuntimeObject* object =
                    ir_interpreter_object_create(
                        scratch.arena,
                        instruction->literal.length);
                for (u64 index = 0;
                    index < instruction->literal.length;
                    index += 1)
                {
                    object->bytes[index] =
                        instruction->literal.pointer[index];
                    object->initialized[index] = 1;
                }
                produced = (IrRuntimeValue){
                    .object = object,
                    .length = instruction->literal.length,
                    .element_size = 1,
                    .kind = string_type->kind ==
                        ANALYSIS_TYPE_SLICE ?
                        IR_RUNTIME_VALUE_SLICE :
                        IR_RUNTIME_VALUE_AGGREGATE,
                    .initialized = true,
                };
            } break;
            case IR_OPCODE_UNDEFINED:
            {
                produced.initialized = false;
            } break;
            case IR_OPCODE_LOCAL:
            {
                u64 size = ir_interpreter_type_size(
                    frame->analysis,
                    instruction->type);
                if (!size)
                {
                    operation_trap =
                        IR_EXECUTION_TRAP_INVALID_MEMORY;
                    break;
                }
                produced = (IrRuntimeValue){
                    .object = ir_interpreter_object_create(
                        scratch.arena,
                        size),
                    .length = size,
                    .kind = IR_RUNTIME_VALUE_PLACE,
                    .initialized = true,
                };
            } break;
            case IR_OPCODE_LOAD:
            {
                IrRuntimeValue place = frame->values[
                    instruction->operands[0].value];
                if (place.kind != IR_RUNTIME_VALUE_PLACE ||
                    !ir_interpreter_memory_read(
                        scratch.arena,
                        frame->analysis,
                        instruction->type,
                        place.object,
                        place.offset,
                        &produced))
                {
                    operation_trap =
                        IR_EXECUTION_TRAP_UNINITIALIZED_VALUE;
                }
            } break;
            case IR_OPCODE_STORE:
            {
                IrRuntimeValue place = frame->values[
                    instruction->operands[0].value];
                IrRuntimeValue stored = frame->values[
                    instruction->operands[1].value];
                AnalysisTypeId stored_type =
                    frame->function->values[
                        instruction->operands[1].value].type;
                u64 size = ir_interpreter_type_size(
                    frame->analysis,
                    stored_type);
                if (place.kind != IR_RUNTIME_VALUE_PLACE ||
                    !size ||
                    !ir_interpreter_memory_write(
                        scratch.arena,
                        place.object,
                        place.offset,
                        size,
                        stored))
                {
                    operation_trap =
                        IR_EXECUTION_TRAP_INVALID_MEMORY;
                }
            } break;
            case IR_OPCODE_ARRAY:
            {
                AnalysisType* array_type = analysis_type_from_id(
                    frame->analysis,
                    instruction->type);
                AnalysisTypeId element_type =
                    array_type->kind == ANALYSIS_TYPE_ARRAY ?
                        array_type->as.array.element_type :
                        array_type->as.element_type;
                u64 element_size = ir_interpreter_type_size(
                    frame->analysis,
                    element_type);
                u64 count = instruction->operand_count;
                u64 size = element_size * count;
                IrRuntimeObject* object =
                    ir_interpreter_object_create(
                        scratch.arena,
                        size);
                for (u32 element_index = 0;
                    element_index < instruction->operand_count;
                    element_index += 1)
                {
                    if (!element_size ||
                        !ir_interpreter_memory_write(
                            scratch.arena,
                            object,
                            (u64)element_index * element_size,
                            element_size,
                            frame->values[instruction->operands[
                                element_index].value]))
                    {
                        operation_trap =
                            IR_EXECUTION_TRAP_INVALID_MEMORY;
                        break;
                    }
                }
                produced = (IrRuntimeValue){
                    .object = object,
                    .length = count,
                    .element_size = element_size,
                    .kind = IR_RUNTIME_VALUE_AGGREGATE,
                    .initialized = operation_trap ==
                        IR_EXECUTION_TRAP_NONE,
                };
            } break;
            case IR_OPCODE_AGGREGATE:
            {
                u64 size = ir_interpreter_type_size(
                    frame->analysis,
                    instruction->type);
                IrRuntimeObject* object =
                    ir_interpreter_object_create(
                        scratch.arena,
                        size);
                for (u32 operand_index = 0;
                    operand_index < instruction->operand_count;
                    operand_index += 1)
                {
                    AnalysisField* field = 0;
                    if (operand_index >=
                            instruction->immediate_count ||
                        !ir_interpreter_field(
                            frame,
                            instruction->type,
                            (u32)instruction->immediates[
                                operand_index],
                            &field))
                    {
                        operation_trap =
                            IR_EXECUTION_TRAP_INVALID_MEMORY;
                        break;
                    }
                    u64 field_size = ir_interpreter_type_size(
                        frame->analysis,
                        field->type);
                    if (!ir_interpreter_memory_write(
                            scratch.arena,
                            object,
                            field->offset,
                            field_size,
                            frame->values[instruction->operands[
                                operand_index].value]))
                    {
                        operation_trap =
                            IR_EXECUTION_TRAP_INVALID_MEMORY;
                        break;
                    }
                }
                produced = (IrRuntimeValue){
                    .object = object,
                    .length = size,
                    .kind = IR_RUNTIME_VALUE_AGGREGATE,
                    .initialized = operation_trap ==
                        IR_EXECUTION_TRAP_NONE,
                };
            } break;
            case IR_OPCODE_INDEX:
            {
                IrValueId base_id = instruction->operands[0];
                IrRuntimeValue base = frame->values[base_id.value];
                u64 index = frame->values[
                    instruction->operands[1].value].bits;
                IrRuntimeObject* object = 0;
                u64 offset = 0;
                u64 count = 0;
                u64 element_size = 0;
                if (!ir_interpreter_collection(
                        frame,
                        frame->function->values[base_id.value].type,
                        base,
                        &object,
                        &offset,
                        &count,
                        &element_size) ||
                    index >= count)
                {
                    operation_trap =
                        IR_EXECUTION_TRAP_OUT_OF_BOUNDS;
                    break;
                }
                produced = (IrRuntimeValue){
                    .object = object,
                    .offset = offset + index * element_size,
                    .length = element_size,
                    .kind = IR_RUNTIME_VALUE_PLACE,
                    .initialized = true,
                };
                if (frame->function->values[
                        instruction->result.value].category ==
                    IR_VALUE_VALUE)
                {
                    if (!ir_interpreter_memory_read(
                            scratch.arena,
                            frame->analysis,
                            instruction->type,
                            produced.object,
                            produced.offset,
                            &produced))
                    {
                        operation_trap =
                            IR_EXECUTION_TRAP_UNINITIALIZED_VALUE;
                    }
                }
            } break;
            case IR_OPCODE_SLICE:
            {
                IrValueId base_id = instruction->operands[0];
                IrRuntimeObject* object = 0;
                u64 offset = 0;
                u64 count = 0;
                u64 element_size = 0;
                if (instruction->immediate_count != 2 ||
                    !ir_interpreter_collection(
                        frame,
                        frame->function->values[base_id.value].type,
                        frame->values[base_id.value],
                        &object,
                        &offset,
                        &count,
                        &element_size))
                {
                    operation_trap =
                        IR_EXECUTION_TRAP_INVALID_MEMORY;
                    break;
                }
                bool has_start = instruction->immediates[0] != 0;
                bool has_end = instruction->immediates[1] != 0;
                u32 operand_index = 1;
                u64 start = has_start ?
                    frame->values[instruction->operands[
                        operand_index++].value].bits : 0;
                u64 end = has_end ?
                    frame->values[instruction->operands[
                        operand_index].value].bits : count;
                if (start > end || end > count)
                {
                    operation_trap =
                        IR_EXECUTION_TRAP_OUT_OF_BOUNDS;
                    break;
                }
                produced = (IrRuntimeValue){
                    .object = object,
                    .offset = offset + start * element_size,
                    .length = end - start,
                    .element_size = element_size,
                    .kind = IR_RUNTIME_VALUE_SLICE,
                    .initialized = true,
                };
            } break;
            case IR_OPCODE_FIELD:
            {
                IrValueId base_id = instruction->operands[0];
                IrRuntimeValue base = frame->values[base_id.value];
                AnalysisField* field = 0;
                if (instruction->immediate_count != 1 ||
                    !ir_interpreter_field(
                        frame,
                        frame->function->values[base_id.value].type,
                        (u32)instruction->immediates[0],
                        &field) ||
                    (base.kind != IR_RUNTIME_VALUE_PLACE &&
                     base.kind != IR_RUNTIME_VALUE_AGGREGATE))
                {
                    operation_trap =
                        IR_EXECUTION_TRAP_INVALID_MEMORY;
                    break;
                }
                produced = (IrRuntimeValue){
                    .object = base.object,
                    .offset = base.offset + field->offset,
                    .length = ir_interpreter_type_size(
                        frame->analysis,
                        field->type),
                    .kind = IR_RUNTIME_VALUE_PLACE,
                    .initialized = true,
                };
                if (frame->function->values[
                        instruction->result.value].category ==
                    IR_VALUE_VALUE)
                {
                    if (!ir_interpreter_memory_read(
                            scratch.arena,
                            frame->analysis,
                            instruction->type,
                            produced.object,
                            produced.offset,
                            &produced))
                    {
                        operation_trap =
                            IR_EXECUTION_TRAP_UNINITIALIZED_VALUE;
                    }
                }
            } break;
            case IR_OPCODE_FUNCTION:
            {
                produced.kind = IR_RUNTIME_VALUE_FUNCTION;
                produced.entity = instruction->entity;
                produced.instantiation = instruction->instantiation;
            } break;
            case IR_OPCODE_CAST:
            {
                IrRuntimeValue operand = frame->values[
                    instruction->operands[0].value];
                AnalysisType* target_type = analysis_type_from_id(
                    frame->analysis,
                    instruction->type);
                if (operand.kind == IR_RUNTIME_VALUE_ADDRESS &&
                    target_type->kind == ANALYSIS_TYPE_POINTER)
                {
                    produced = operand;
                }
                else if (!ir_interpreter_cast(
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
                switch (instruction->unary_operation)
                {
                    case IR_UNARY_FLOAT_NEGATE:
                    {
                        AnalysisType* type = analysis_type_from_id(
                            frame->analysis,
                            instruction->type);
                        f64 value = ir_interpreter_float_read(
                            operand,
                            type->as.float_bit_width);
                        produced.bits = ir_interpreter_float_write(
                            -value,
                            type->as.float_bit_width);
                    } break;
                    case IR_UNARY_INTEGER_NEGATE:
                        produced.bits = 0 - operand;
                        break;
                    case IR_UNARY_BOOLEAN_NOT:
                        produced.bits = !operand;
                        break;
                    case IR_UNARY_INTEGER_BITWISE_NOT:
                        produced.bits = ~operand;
                        break;
                    case IR_UNARY_COUNT:
                    {
                        operation_trap =
                            IR_EXECUTION_TRAP_UNSUPPORTED_INSTRUCTION;
                    } break;
                }
                if (instruction->unary_operation !=
                    IR_UNARY_FLOAT_NEGATE)
                {
                    produced.bits = ir_interpreter_normalize_integer(
                        frame->analysis,
                        instruction->type,
                        produced.bits);
                }
            } break;
            case IR_OPCODE_BINARY:
            {
                IrRuntimeValue left_value = frame->values[
                    instruction->operands[0].value];
                IrRuntimeValue right_value = frame->values[
                    instruction->operands[1].value];
                if (left_value.kind == IR_RUNTIME_VALUE_ADDRESS ||
                    right_value.kind == IR_RUNTIME_VALUE_ADDRESS)
                {
                    if (left_value.kind !=
                            IR_RUNTIME_VALUE_ADDRESS ||
                        right_value.kind !=
                            IR_RUNTIME_VALUE_ADDRESS)
                    {
                        operation_trap =
                            IR_EXECUTION_TRAP_INVALID_MEMORY;
                        break;
                    }
                    bool same_object =
                        left_value.object == right_value.object;
                    switch (instruction->binary_operation)
                    {
                        case IR_BINARY_POINTER_EQUAL:
                            produced.bits = same_object &&
                                left_value.offset ==
                                    right_value.offset;
                            break;
                        case IR_BINARY_POINTER_NOT_EQUAL:
                            produced.bits = !same_object ||
                                left_value.offset !=
                                    right_value.offset;
                            break;
                        default:
                        {
                            operation_trap =
                                IR_EXECUTION_TRAP_UNSUPPORTED_INSTRUCTION;
                        } break;
                    }
                    break;
                }
                AnalysisType* operand_type = analysis_type_from_id(
                    frame->analysis,
                    frame->function->values[
                        instruction->operands[0].value].type);
                if (instruction->binary_operation == IR_BINARY_RANGE)
                {
                    produced = (IrRuntimeValue){
                        .bits = frame->values[
                            instruction->operands[0].value].bits,
                        .length = frame->values[
                            instruction->operands[1].value].bits,
                        .element_size = ir_interpreter_type_size(
                            frame->analysis,
                            operand_type->id),
                        .kind = IR_RUNTIME_VALUE_RANGE,
                        .initialized = true,
                    };
                    break;
                }
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
            case IR_OPCODE_ADDRESS_OF:
            {
                IrRuntimeValue place = frame->values[
                    instruction->operands[0].value];
                if (place.kind != IR_RUNTIME_VALUE_PLACE)
                {
                    operation_trap =
                        IR_EXECUTION_TRAP_INVALID_MEMORY;
                    break;
                }
                produced = place;
                produced.kind = IR_RUNTIME_VALUE_ADDRESS;
            } break;
            case IR_OPCODE_DEREFERENCE:
            {
                IrRuntimeValue address = frame->values[
                    instruction->operands[0].value];
                if (address.kind != IR_RUNTIME_VALUE_ADDRESS ||
                    !address.object)
                {
                    operation_trap =
                        IR_EXECUTION_TRAP_INVALID_MEMORY;
                    break;
                }
                produced = address;
                produced.kind = IR_RUNTIME_VALUE_PLACE;
                produced.length = ir_interpreter_type_size(
                    frame->analysis,
                    instruction->type);
                if (!ir_interpreter_object_range_valid(
                        produced.object,
                        produced.offset,
                        produced.length))
                {
                    operation_trap =
                        IR_EXECUTION_TRAP_INVALID_MEMORY;
                }
            } break;
            case IR_OPCODE_REVERSE:
            {
                produced = frame->values[
                    instruction->operands[0].value];
                if (produced.kind != IR_RUNTIME_VALUE_RANGE &&
                    produced.kind != IR_RUNTIME_VALUE_SLICE &&
                    produced.kind != IR_RUNTIME_VALUE_AGGREGATE &&
                    produced.kind != IR_RUNTIME_VALUE_PLACE)
                {
                    operation_trap =
                        IR_EXECUTION_TRAP_UNSUPPORTED_INSTRUCTION;
                    break;
                }
                produced.reversed = !produced.reversed;
            } break;
            case IR_OPCODE_ITERATOR_BEGIN:
            {
                IrRuntimeIterator* iterator = arena_allocate(
                    scratch.arena,
                    IrRuntimeIterator,
                    1);
                *iterator = (IrRuntimeIterator){
                    .iterable = frame->values[
                        instruction->operands[0].value],
                };
                produced = (IrRuntimeValue){
                    .iterator = iterator,
                    .kind = IR_RUNTIME_VALUE_ITERATOR,
                    .initialized = true,
                };
            } break;
            case IR_OPCODE_ITERATOR_NEXT:
            {
                IrRuntimeValue iterator_value = frame->values[
                    instruction->operands[0].value];
                if (iterator_value.kind !=
                        IR_RUNTIME_VALUE_ITERATOR ||
                    !iterator_value.iterator ||
                    !ir_interpreter_iterator_next(
                        scratch.arena,
                        frame,
                        iterator_value.iterator,
                        &operation_trap))
                {
                    if (operation_trap ==
                        IR_EXECUTION_TRAP_NONE)
                    {
                        operation_trap =
                            IR_EXECUTION_TRAP_INVALID_MEMORY;
                    }
                    break;
                }
                produced.bits =
                    iterator_value.iterator->has_current;
            } break;
            case IR_OPCODE_ITERATOR_VALUE:
            {
                IrRuntimeValue iterator_value = frame->values[
                    instruction->operands[0].value];
                if (iterator_value.kind !=
                        IR_RUNTIME_VALUE_ITERATOR ||
                    !iterator_value.iterator ||
                    !iterator_value.iterator->has_current)
                {
                    operation_trap =
                        IR_EXECUTION_TRAP_INVALID_MEMORY;
                    break;
                }
                produced = iterator_value.iterator->current;
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
                    returned.type_module = caller->analysis->module.id;
                    returned.type =
                        caller->function->values[
                            caller_result.value].type;
                    caller->values[caller_result.value] = returned;
                }
                advance = false;
            } break;
            case IR_OPCODE_UNREACHABLE:
            {
                operation_trap =
                    IR_EXECUTION_TRAP_UNREACHABLE;
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
            produced.type_module = frame->analysis->module.id;
            produced.type = instruction->type;
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
        "type Pair = struct\n"
        "{\n"
        "    a: s32,\n"
        "    b: s32,\n"
        "}\n"
        "type Number = union\n"
        "{\n"
        "    signed_value: s32,\n"
        "    unsigned_value: u32,\n"
        "}\n"
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
        "code memory : fn () s32\n"
        "{\n"
        "    data values: [3]s32 = [ 2, 3, 4 ];\n"
        "    data pair: Pair = { .a = 5, .b = 7 };\n"
        "    data pointer: &s32 = &values[1];\n"
        "    data same_pointer: &s32 = &values[1];\n"
        "    pointer.& += pair.a;\n"
        "    data selected: []s32 = values[1..];\n"
        "    selected[1] += 1;\n"
        "    data total: s32 = 0;\n"
        "    for (data value = @reverse(selected))\n"
        "    {\n"
        "        total += value;\n"
        "    }\n"
        "    if (pointer == same_pointer)\n"
        "    {\n"
        "        total += 1;\n"
        "    }\n"
        "    return total + pair.b;\n"
        "}\n"
        "code range_total : fn () s32\n"
        "{\n"
        "    data total: s32 = 0;\n"
        "    for (data value = 0 .. 4)\n"
        "    {\n"
        "        total += value;\n"
        "    }\n"
        "    for (data value = @reverse(0 .. 4))\n"
        "    {\n"
        "        total += value;\n"
        "    }\n"
        "    return total;\n"
        "}\n"
        "code array_total : fn () s32\n"
        "{\n"
        "    data values: [3]s32 = [ 1, 2, 3 ];\n"
        "    data total: s32 = 0;\n"
        "    for (data value = values)\n"
        "    {\n"
        "        total += value;\n"
        "    }\n"
        "    return total;\n"
        "}\n"
        "code aggregate_copy : fn () s32\n"
        "{\n"
        "    data first: Pair = { .a = 5, .b = 7 };\n"
        "    data second: Pair = first;\n"
        "    second.a += 1;\n"
        "    return first.a * 10 + second.a;\n"
        "}\n"
        "code pointer_storage : fn () s32\n"
        "{\n"
        "    data value: s32 = 4;\n"
        "    data pointer: &s32 = &value;\n"
        "    data pointer_pointer: &&s32 = &pointer;\n"
        "    pointer_pointer.&.& += 3;\n"
        "    return value;\n"
        "}\n"
        "code union_value : fn () s32\n"
        "{\n"
        "    data number: Number = { .signed_value = 9 };\n"
        "    return number.signed_value;\n"
        "}\n"
        "code string_value : fn () s32\n"
        "{\n"
        "    data text = \"hello\";\n"
        "    return @cast(text[1] - 'e');\n"
        "}\n"
        "code indexed : fn (index: s32) s32\n"
        "{\n"
        "    data values: [1]s32 = [ 9 ];\n"
        "    return values[index];\n"
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
    analysis_compute_layouts(
        &scalar_analysis,
        (AnalysisLayoutOptions){
            .pointer_size = 8,
            .pointer_alignment = 8,
        });
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

    AnalysisEntity* memory_entity = ir_interpreter_test_entity_find(
        &scalar_analysis,
        S8("memory"));
    BUSTER_TEST(arguments, memory_entity != 0);
    if (memory_entity)
    {
        IrExecutionResult memory_result = ir_execute(
            expression_arena,
            &scalar_program_analysis,
            &scalar_program,
            memory_entity->id,
            ANALYSIS_INSTANTIATION_ID_INVALID,
            0,
            0,
            (IrExecutionOptions){0});
        BUSTER_TEST(arguments,
            memory_result.trap == IR_EXECUTION_TRAP_NONE);
        BUSTER_TEST(arguments, memory_result.bits == 21);
    }

    AnalysisEntity* range_entity = ir_interpreter_test_entity_find(
        &scalar_analysis,
        S8("range_total"));
    BUSTER_TEST(arguments, range_entity != 0);
    if (range_entity)
    {
        IrExecutionResult range_result = ir_execute(
            expression_arena,
            &scalar_program_analysis,
            &scalar_program,
            range_entity->id,
            ANALYSIS_INSTANTIATION_ID_INVALID,
            0,
            0,
            (IrExecutionOptions){0});
        BUSTER_TEST(arguments,
            range_result.trap == IR_EXECUTION_TRAP_NONE);
        BUSTER_TEST(arguments, range_result.bits == 12);
    }

    AnalysisEntity* array_entity = ir_interpreter_test_entity_find(
        &scalar_analysis,
        S8("array_total"));
    BUSTER_TEST(arguments, array_entity != 0);
    if (array_entity)
    {
        IrExecutionResult array_result = ir_execute(
            expression_arena,
            &scalar_program_analysis,
            &scalar_program,
            array_entity->id,
            ANALYSIS_INSTANTIATION_ID_INVALID,
            0,
            0,
            (IrExecutionOptions){0});
        BUSTER_TEST(arguments,
            array_result.trap == IR_EXECUTION_TRAP_NONE);
        BUSTER_TEST(arguments, array_result.bits == 6);
    }

    AnalysisEntity* aggregate_copy_entity =
        ir_interpreter_test_entity_find(
            &scalar_analysis,
            S8("aggregate_copy"));
    BUSTER_TEST(arguments, aggregate_copy_entity != 0);
    if (aggregate_copy_entity)
    {
        IrExecutionResult aggregate_copy_result = ir_execute(
            expression_arena,
            &scalar_program_analysis,
            &scalar_program,
            aggregate_copy_entity->id,
            ANALYSIS_INSTANTIATION_ID_INVALID,
            0,
            0,
            (IrExecutionOptions){0});
        BUSTER_TEST(arguments,
            aggregate_copy_result.trap ==
                IR_EXECUTION_TRAP_NONE);
        BUSTER_TEST(arguments,
            aggregate_copy_result.bits == 56);
    }

    AnalysisEntity* pointer_storage_entity =
        ir_interpreter_test_entity_find(
            &scalar_analysis,
            S8("pointer_storage"));
    BUSTER_TEST(arguments, pointer_storage_entity != 0);
    if (pointer_storage_entity)
    {
        IrExecutionResult pointer_storage_result = ir_execute(
            expression_arena,
            &scalar_program_analysis,
            &scalar_program,
            pointer_storage_entity->id,
            ANALYSIS_INSTANTIATION_ID_INVALID,
            0,
            0,
            (IrExecutionOptions){0});
        BUSTER_TEST(arguments,
            pointer_storage_result.trap ==
                IR_EXECUTION_TRAP_NONE);
        BUSTER_TEST(arguments,
            pointer_storage_result.bits == 7);
    }

    AnalysisEntity* union_entity = ir_interpreter_test_entity_find(
        &scalar_analysis,
        S8("union_value"));
    BUSTER_TEST(arguments, union_entity != 0);
    if (union_entity)
    {
        IrExecutionResult union_result = ir_execute(
            expression_arena,
            &scalar_program_analysis,
            &scalar_program,
            union_entity->id,
            ANALYSIS_INSTANTIATION_ID_INVALID,
            0,
            0,
            (IrExecutionOptions){0});
        BUSTER_TEST(arguments,
            union_result.trap == IR_EXECUTION_TRAP_NONE);
        BUSTER_TEST(arguments, union_result.bits == 9);
    }

    AnalysisEntity* string_entity = ir_interpreter_test_entity_find(
        &scalar_analysis,
        S8("string_value"));
    BUSTER_TEST(arguments, string_entity != 0);
    if (string_entity)
    {
        IrExecutionResult string_result = ir_execute(
            expression_arena,
            &scalar_program_analysis,
            &scalar_program,
            string_entity->id,
            ANALYSIS_INSTANTIATION_ID_INVALID,
            0,
            0,
            (IrExecutionOptions){0});
        BUSTER_TEST(arguments,
            string_result.trap == IR_EXECUTION_TRAP_NONE);
        BUSTER_TEST(arguments, string_result.bits == 0);
    }

    AnalysisEntity* indexed_entity = ir_interpreter_test_entity_find(
        &scalar_analysis,
        S8("indexed"));
    BUSTER_TEST(arguments, indexed_entity != 0);
    if (indexed_entity)
    {
        IrExecutionArgument outside = { .bits = 2 };
        IrExecutionResult bounds_result = ir_execute(
            expression_arena,
            &scalar_program_analysis,
            &scalar_program,
            indexed_entity->id,
            ANALYSIS_INSTANTIATION_ID_INVALID,
            &outside,
            1,
            (IrExecutionOptions){0});
        BUSTER_TEST(arguments,
            bounds_result.trap ==
                IR_EXECUTION_TRAP_OUT_OF_BOUNDS);
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
