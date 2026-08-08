#include <buster/lib/compiler/ir/ir.h>

#include <buster/lib/file.h>
#include <buster/lib/string.h>

IrType* ir_type_from_id(IrTypeTable* table, IrTypeId id)
{
    if (!table || id.value >= table->count)
    {
        return 0;
    }
    return table->types + id.value;
}

IrSymbol* ir_symbol_from_id(IrSymbolTable* table, IrSymbolId id)
{
    if (!table || id.value >= table->count)
    {
        return 0;
    }
    return table->symbols + id.value;
}

IrSource* ir_source_from_id(IrSourceTable* table, IrSourceId id)
{
    if (!table || id.value >= table->count)
    {
        return 0;
    }
    return table->sources + id.value;
}

typedef struct IrLowered IrLowered;
struct IrLowered
{
    IrValueId value;
    AnalysisTypeId type;
    AnalysisLocalId local;
    IrValueCategory category;
};

typedef struct IrBuilder IrBuilder;
struct IrBuilder
{
    Arena* result_arena;
    Arena* scratch_arena;
    AnalysisResult* analysis;
    AnalysisEntity* entity;
    AnalysisBody* body;
    IrFunction* function;
    IrBlockId current;
};

typedef struct IrLowerTask IrLowerTask;
typedef enum IrLowerTaskKind
{
    IR_LOWER_TASK_STATEMENT,
    IR_LOWER_TASK_SEAL_BLOCK,
} IrLowerTaskKind;

struct IrLowerTask
{
    IrLowerTask* previous;
    AstStatement* statement;
    IrBlockId block;
    IrBlockId end;
    IrBlockId break_block;
    IrBlockId continue_block;
    IrLowerTaskKind kind;
};

BUSTER_GLOBAL_LOCAL bool ir_type_id_equal(AnalysisTypeId left, AnalysisTypeId right)
{
    return left.value == right.value;
}

BUSTER_GLOBAL_LOCAL bool ir_entity_id_equal(AnalysisEntityId left, AnalysisEntityId right)
{
    return left.module.value == right.module.value && left.index.value == right.index.value;
}

BUSTER_GLOBAL_LOCAL bool ir_block_id_valid(IrFunction* function, IrBlockId block)
{
    return block.value < function->block_count;
}

BUSTER_GLOBAL_LOCAL bool ir_value_id_valid(IrFunction* function, IrValueId value)
{
    return value.value < function->value_count;
}

BUSTER_GLOBAL_LOCAL u32 ir_analysis_inline_assembly_type_class(AnalysisType* type)
{
    if (!type || type->layout.state != ANALYSIS_LAYOUT_RESOLVED || !type->layout.size || type->layout.size > 8)
    {
        return IR_INLINE_ASSEMBLY_OPERAND_CLASS_INVALID;
    }
    switch (type->kind)
    {
    case ANALYSIS_TYPE_BOOL:
    case ANALYSIS_TYPE_INTEGER:
    case ANALYSIS_TYPE_ENUM:
        return IR_INLINE_ASSEMBLY_OPERAND_CLASS_INTEGER;
    case ANALYSIS_TYPE_POINTER:
        return IR_INLINE_ASSEMBLY_OPERAND_CLASS_POINTER;
    case ANALYSIS_TYPE_POISON:
    case ANALYSIS_TYPE_VOID:
    case ANALYSIS_TYPE_FLOAT:
    case ANALYSIS_TYPE_VA_LIST:
    case ANALYSIS_TYPE_COMPILE_TIME_PARAMETER:
    case ANALYSIS_TYPE_SLICE:
    case ANALYSIS_TYPE_INFERRED_ARRAY:
    case ANALYSIS_TYPE_ARRAY:
    case ANALYSIS_TYPE_VECTOR:
    case ANALYSIS_TYPE_FUNCTION:
    case ANALYSIS_TYPE_RANGE:
    case ANALYSIS_TYPE_STRUCT:
    case ANALYSIS_TYPE_UNION:
    case ANALYSIS_TYPE_COUNT:
        break;
    }
    return IR_INLINE_ASSEMBLY_OPERAND_CLASS_INVALID;
}

BUSTER_GLOBAL_LOCAL u32 ir_canonical_inline_assembly_type_class(IrType* type)
{
    if (!type || !type->layout.resolved || !type->layout.size || type->layout.size > 8)
    {
        return IR_INLINE_ASSEMBLY_OPERAND_CLASS_INVALID;
    }
    switch (type->kind)
    {
    case IR_TYPE_BOOLEAN:
    case IR_TYPE_INTEGER:
    case IR_TYPE_ENUM:
        return IR_INLINE_ASSEMBLY_OPERAND_CLASS_INTEGER;
    case IR_TYPE_POINTER:
        return IR_INLINE_ASSEMBLY_OPERAND_CLASS_POINTER;
    case IR_TYPE_VOID:
    case IR_TYPE_FLOAT:
    case IR_TYPE_VA_LIST:
    case IR_TYPE_SLICE:
    case IR_TYPE_ARRAY:
    case IR_TYPE_VECTOR:
    case IR_TYPE_FUNCTION:
    case IR_TYPE_RANGE:
    case IR_TYPE_STRUCT:
    case IR_TYPE_UNION:
    case IR_TYPE_COUNT:
        break;
    }
    return IR_INLINE_ASSEMBLY_OPERAND_CLASS_INVALID;
}

BUSTER_GLOBAL_LOCAL bool ir_inline_assembly_constraint_shape_valid(u64 constraint, u32 operand_index, u32 operand_count, u32* match_index_out)
{
    if ((constraint & ~IR_INLINE_ASSEMBLY_CONSTRAINT_KNOWN_MASK) || (constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_CLASS_MASK) >= IR_INLINE_ASSEMBLY_CONSTRAINT_COUNT)
    {
        return false;
    }
    bool output = (constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_OUTPUT) != 0;
    bool read_write = (constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_READ_WRITE) != 0;
    bool matching = (constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH) != 0;
    u64 match_bits = constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH_INDEX_MASK;
    if ((read_write && !output) || (matching && (output || read_write)))
    {
        return false;
    }
    if (!matching)
    {
        return match_bits == 0;
    }
    u32 match_index = IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH_INDEX(constraint);
    if (match_index >= operand_index || match_index >= operand_count)
    {
        return false;
    }
    if (match_index_out)
    {
        *match_index_out = match_index;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool ir_analysis_inline_assembly_types_compatible(AnalysisType* output, AnalysisType* input)
{
    u32 output_class = ir_analysis_inline_assembly_type_class(output);
    u32 input_class = ir_analysis_inline_assembly_type_class(input);
    return output_class != IR_INLINE_ASSEMBLY_OPERAND_CLASS_INVALID && output_class == input_class && output->layout.size == input->layout.size;
}

BUSTER_GLOBAL_LOCAL bool ir_canonical_inline_assembly_types_compatible(IrType* output, IrType* input)
{
    u32 output_class = ir_canonical_inline_assembly_type_class(output);
    u32 input_class = ir_canonical_inline_assembly_type_class(input);
    return output_class != IR_INLINE_ASSEMBLY_OPERAND_CLASS_INVALID && output_class == input_class && output->layout.size == input->layout.size;
}

BUSTER_GLOBAL_LOCAL bool ir_analysis_inline_assembly_valid(AnalysisResult* analysis, IrFunction* function, IrInstruction* instruction)
{
    bool valid = instruction->operand_count == instruction->immediate_count && (instruction->target_count == 0 || instruction->target_count >= 2) &&
                 instruction->label_name_count == (instruction->target_count ? instruction->target_count - 1 : 0) &&
                 (!instruction->label_name_count || instruction->label_names) && instruction->operand_name_count == instruction->operand_count &&
                 (!instruction->operand_name_count || instruction->operand_names) && (!instruction->clobber_count || instruction->clobbers) &&
                 instruction->result.value == IR_ID_UNDERLYING_INVALID;
    for (u32 operand_index = 0; valid && operand_index < instruction->operand_count; operand_index += 1)
    {
        IrValueId operand_id = instruction->operands ? instruction->operands[operand_index] : IR_VALUE_ID_INVALID;
        if (operand_id.value >= function->value_count)
        {
            valid = false;
            break;
        }
        u64 constraint = instruction->immediates ? instruction->immediates[operand_index] : UINT64_MAX;
        bool output = (constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_OUTPUT) != 0;
        valid = ir_inline_assembly_constraint_shape_valid(constraint, operand_index, instruction->operand_count, 0) &&
                function->values[operand_id.value].category == (output ? IR_VALUE_PLACE : IR_VALUE_VALUE);
        if (!valid)
        {
            break;
        }
        AnalysisType* operand_type = analysis_type_from_id(analysis, function->values[operand_id.value].type);
        if ((constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH) != 0)
        {
            valid = ir_analysis_inline_assembly_type_class(operand_type) != IR_INLINE_ASSEMBLY_OPERAND_CLASS_INVALID;
            if (!valid)
            {
                break;
            }
            u32 match_index = IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH_INDEX(constraint);
            IrValueId output_id = instruction->operands[match_index];
            u64 output_constraint = instruction->immediates[match_index];
            AnalysisType* output_type = analysis_type_from_id(analysis, function->values[output_id.value].type);
            valid &= (output_constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_OUTPUT) != 0 &&
                     (output_constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_READ_WRITE) == 0 &&
                     (constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_CLASS_MASK) == (output_constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_CLASS_MASK) &&
                     ir_analysis_inline_assembly_types_compatible(output_type, operand_type);
            for (u32 previous_index = 0; valid && previous_index < operand_index; previous_index += 1)
            {
                u64 previous_constraint = instruction->immediates[previous_index];
                valid &= !(previous_constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH) ||
                         IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH_INDEX(previous_constraint) != match_index;
            }
        }
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool ir_canonical_inline_assembly_valid(IrProgram* program, IrFunction* function, IrInstruction* instruction)
{
    bool valid = instruction->canonical_type.value < program->types.count && ir_type_from_id(&program->types, instruction->canonical_type)->kind == IR_TYPE_VOID &&
                 instruction->operand_count == instruction->immediate_count && (instruction->target_count == 0 || instruction->target_count >= 2) &&
                 instruction->label_name_count == (instruction->target_count ? instruction->target_count - 1 : 0) &&
                 (!instruction->label_name_count || instruction->label_names) && instruction->operand_name_count == instruction->operand_count &&
                 (!instruction->operand_name_count || instruction->operand_names) && (!instruction->clobber_count || instruction->clobbers) &&
                 instruction->result.value == IR_ID_UNDERLYING_INVALID;
    for (u32 target_index = 0; valid && target_index < instruction->target_count; target_index += 1)
    {
        valid = instruction->targets && instruction->targets[target_index].value < function->block_count;
    }
    for (u32 operand_index = 0; valid && operand_index < instruction->operand_count; operand_index += 1)
    {
        IrValueId operand_id = instruction->operands ? instruction->operands[operand_index] : IR_VALUE_ID_INVALID;
        valid = operand_id.value < function->value_count;
        if (!valid)
        {
            break;
        }
        u64 constraint = instruction->immediates ? instruction->immediates[operand_index] : UINT64_MAX;
        bool output = (constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_OUTPUT) != 0;
        valid = ir_inline_assembly_constraint_shape_valid(constraint, operand_index, instruction->operand_count, 0) &&
                function->values[operand_id.value].category == (output ? IR_VALUE_PLACE : IR_VALUE_VALUE);
        if (!valid)
        {
            break;
        }
        IrType* operand_type = ir_type_from_id(&program->types, function->values[operand_id.value].canonical_type);
        if ((constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH) != 0)
        {
            valid = ir_canonical_inline_assembly_type_class(operand_type) != IR_INLINE_ASSEMBLY_OPERAND_CLASS_INVALID;
            if (!valid)
            {
                break;
            }
            u32 match_index = IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH_INDEX(constraint);
            IrValueId output_id = instruction->operands[match_index];
            u64 output_constraint = instruction->immediates[match_index];
            IrType* output_type = ir_type_from_id(&program->types, function->values[output_id.value].canonical_type);
            valid &= (output_constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_OUTPUT) != 0 &&
                     (output_constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_READ_WRITE) == 0 &&
                     (constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_CLASS_MASK) == (output_constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_CLASS_MASK) &&
                     ir_canonical_inline_assembly_types_compatible(output_type, operand_type);
            for (u32 previous_index = 0; valid && previous_index < operand_index; previous_index += 1)
            {
                u64 previous_constraint = instruction->immediates[previous_index];
                valid &= !(previous_constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH) ||
                         IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH_INDEX(previous_constraint) != match_index;
            }
        }
    }
    return valid;
}

IrValueLabelMetadata* ir_value_label_metadata_find(IrFunction* function, IrValueId value)
{
    if (!function || !function->label_metadata_count)
    {
        return 0;
    }
    u32 low = 0;
    u32 high = function->label_metadata_count;
    while (low < high)
    {
        u32 middle = low + (high - low) / 2;
        if (function->label_metadata_values[middle].value < value.value)
        {
            low = middle + 1;
        }
        else
        {
            high = middle;
        }
    }
    if (low < function->label_metadata_count && function->label_metadata_values[low].value == value.value)
    {
        return function->label_metadata + low;
    }
    return 0;
}

IrValueLabelMetadata ir_value_label_metadata(IrFunction* function, IrValueId value)
{
    IrValueLabelMetadata* entry = ir_value_label_metadata_find(function, value);
    IrValueLabelMetadata zero = {0};
    return entry ? *entry : zero;
}

IrValueLabelMetadata* ir_value_label_metadata_ensure(Arena* arena, IrFunction* function, IrValueId value)
{
    if (!arena || !function)
    {
        return 0;
    }
    u32 low = 0;
    u32 high = function->label_metadata_count;
    while (low < high)
    {
        u32 middle = low + (high - low) / 2;
        if (function->label_metadata_values[middle].value < value.value)
        {
            low = middle + 1;
        }
        else
        {
            high = middle;
        }
    }
    if (low < function->label_metadata_count && function->label_metadata_values[low].value == value.value)
    {
        return function->label_metadata + low;
    }
    if (function->label_metadata_count == function->label_metadata_capacity)
    {
        u32 new_capacity = function->label_metadata_capacity ? function->label_metadata_capacity * 2 : 8;
        IrValueId* new_values = arena_allocate(arena, IrValueId, new_capacity);
        IrValueLabelMetadata* new_entries = arena_allocate(arena, IrValueLabelMetadata, new_capacity);
        if (function->label_metadata_count)
        {
            memcpy(new_values, function->label_metadata_values, sizeof(*new_values) * function->label_metadata_count);
            memcpy(new_entries, function->label_metadata, sizeof(*new_entries) * function->label_metadata_count);
        }
        function->label_metadata_values = new_values;
        function->label_metadata = new_entries;
        function->label_metadata_capacity = new_capacity;
    }
    u32 tail = function->label_metadata_count - low;
    if (tail)
    {
        memmove(function->label_metadata_values + low + 1, function->label_metadata_values + low, sizeof(*function->label_metadata_values) * tail);
        memmove(function->label_metadata + low + 1, function->label_metadata + low, sizeof(*function->label_metadata) * tail);
    }
    function->label_metadata_values[low] = value;
    function->label_metadata[low] = (IrValueLabelMetadata){0};
    function->label_metadata_count += 1;
    return function->label_metadata + low;
}

bool ir_label_provenance_valid(IrValueLabelMetadata* value)
{
    if (!value || !value->is_label_value || value->has_label_provenance || value->has_non_label_provenance || !value->label_block_count || !value->label_blocks)
    {
        return false;
    }
    for (u32 left = 0; left < value->label_block_count; left += 1)
    {
        for (u32 right = left + 1; right < value->label_block_count; right += 1)
        {
            if (value->label_blocks[left].value == value->label_blocks[right].value)
            {
                return false;
            }
        }
    }
    return true;
}

bool ir_label_storage_provenance_valid(IrValueLabelMetadata* value)
{
    if (!value || !value->has_label_provenance || value->is_label_value || !value->label_block_count || !value->label_blocks)
    {
        return false;
    }
    for (u32 left = 0; left < value->label_block_count; left += 1)
    {
        for (u32 right = left + 1; right < value->label_block_count; right += 1)
        {
            if (value->label_blocks[left].value == value->label_blocks[right].value)
            {
                return false;
            }
        }
    }
    return true;
}

bool ir_block_id_array_unique(IrBlockId* blocks, u32 count)
{
    if (count && !blocks)
    {
        return false;
    }
    for (u32 left = 0; left < count; left += 1)
    {
        for (u32 right = left + 1; right < count; right += 1)
        {
            if (blocks[left].value == blocks[right].value)
            {
                return false;
            }
        }
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool ir_analysis_void_pointer_type(AnalysisResult* analysis, AnalysisTypeId type_id)
{
    AnalysisType* type = analysis_type_from_id(analysis, type_id);
    AnalysisType* element = type && type->kind == ANALYSIS_TYPE_POINTER ? analysis_type_from_id(analysis, type->as.element_type) : 0;
    return element && element->kind == ANALYSIS_TYPE_VOID;
}

BUSTER_GLOBAL_LOCAL bool ir_canonical_void_pointer_type(IrProgram* program, IrTypeId type_id)
{
    IrType* type = ir_type_from_id(&program->types, type_id);
    IrType* element = type && type->kind == IR_TYPE_POINTER ? ir_type_from_id(&program->types, type->element_type) : 0;
    return element && element->kind == IR_TYPE_VOID;
}

BUSTER_GLOBAL_LOCAL IrFunction* ir_module_function_for_symbol(IrModule* module, IrSymbolId symbol)
{
    if (!module || !module->functions)
    {
        return 0;
    }
    for (u32 function_index = 0; function_index < module->function_count; function_index += 1)
    {
        IrFunction* function = module->functions + function_index;
        if (function->symbol.value == symbol.value)
        {
            return function;
        }
    }
    return 0;
}

BUSTER_GLOBAL_LOCAL bool ir_label_block_set_contains(IrValueLabelMetadata* value, IrBlockId block)
{
    if (!value || !value->label_blocks)
    {
        return false;
    }
    for (u32 index = 0; index < value->label_block_count; index += 1)
    {
        if (value->label_blocks[index].value == block.value)
        {
            return true;
        }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool ir_label_path_contains_block(IrLabelProvenancePath* path, IrBlockId block)
{
    if (!path || !path->label_blocks)
    {
        return false;
    }
    for (u32 index = 0; index < path->label_block_count; index += 1)
    {
        if (path->label_blocks[index].value == block.value)
        {
            return true;
        }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool ir_label_block_sets_equal(IrValueLabelMetadata* left, IrValueLabelMetadata* right)
{
    if (!left || !right || left->label_block_count != right->label_block_count)
    {
        return false;
    }
    for (u32 index = 0; index < left->label_block_count; index += 1)
    {
        if (!ir_label_block_set_contains(right, left->label_blocks[index]))
        {
            return false;
        }
    }
    for (u32 index = 0; index < right->label_block_count; index += 1)
    {
        if (!ir_label_block_set_contains(left, right->label_blocks[index]))
        {
            return false;
        }
    }
    return true;
}

BUSTER_F_DECL bool ir_label_metadata_shape_valid(IrProgram* program, IrFunction* function, IrValueId value_id)
{
    IrValue* value_slot = function && value_id.value < function->value_count ? function->values + value_id.value : 0;
    if (function && !function->label_metadata_count)
    {
        // With no metadata anywhere in the function every label-specific
        // clause below is vacuous; only the value/type-layout requirements
        // remain.
        IrType* empty_value_type = program && value_slot ? ir_type_from_id(&program->types, value_slot->canonical_type) : 0;
        return value_slot && (!program || (empty_value_type && empty_value_type->layout.resolved));
    }
    IrValueLabelMetadata metadata = ir_value_label_metadata(function, value_id);
    IrValueLabelMetadata* value = value_slot ? &metadata : 0;
    IrType* value_type = program && value_slot ? ir_type_from_id(&program->types, value_slot->canonical_type) : 0;
    u64 pointer_size = program ? program->data_layout.pointer.size : 0;
    bool valid = function && value && (!program || (value_type && value_type->layout.resolved)) && (value->label_block_count != 0) == (value->label_blocks != 0) &&
           (value->label_path_count != 0) == (value->label_paths != 0) &&
           (!value->is_label_value || value->label_block_count != 0) &&
           (!value->has_label_provenance || value->label_block_count != 0) &&
           (!value->label_block_count || value->is_label_value || value->has_label_provenance) &&
           ((!value->is_label_value && !value->has_label_provenance && value->label_block_count == 0) ||
            value->is_label_value || value->has_label_provenance || value->has_non_label_provenance) &&
           !(value->is_label_value && value->has_label_provenance);
    for (u32 index = 0; valid && index < value->label_block_count; index += 1)
    {
        valid = value->label_blocks[index].value < function->block_count;
        for (u32 previous = 0; valid && previous < index; previous += 1)
        {
            valid = value->label_blocks[previous].value != value->label_blocks[index].value;
        }
    }
    bool path_has_non_label = false;
    bool path_has_label = false;
    for (u32 index = 0; valid && index < value->label_path_count; index += 1)
    {
        IrLabelProvenancePath* path = value->label_paths + index;
        valid = path->size != 0 && path->offset <= UINT64_MAX - path->size && (!program || path->offset + path->size <= value_type->layout.size) &&
                (path->label_block_count != 0) == (path->label_blocks != 0) &&
                (!path->is_non_label || path->label_block_count == 0) && (path->is_non_label || path->label_block_count != 0);
        if (valid && !path->is_non_label)
        {
            valid = !program || (pointer_size != 0 && path->size == pointer_size);
        }
        path_has_non_label |= path->is_non_label;
        path_has_label |= !path->is_non_label;
        for (u32 block_index = 0; valid && block_index < path->label_block_count; block_index += 1)
        {
            valid = path->label_blocks[block_index].value < function->block_count && ir_label_block_set_contains(value, path->label_blocks[block_index]);
            for (u32 previous = 0; valid && previous < block_index; previous += 1)
            {
                valid = path->label_blocks[previous].value != path->label_blocks[block_index].value;
            }
        }
        for (u32 previous_index = 0; valid && previous_index < index; previous_index += 1)
        {
            IrLabelProvenancePath* previous = value->label_paths + previous_index;
            u64 previous_end = previous->offset + previous->size;
            u64 path_end = path->offset + path->size;
            valid = !(previous->offset < path_end && path->offset < previous_end);
        }
    }
    if (valid && value->label_path_count)
    {
        valid = !value->is_label_value && path_has_label == value->has_label_provenance && (!path_has_non_label || value->has_non_label_provenance);
    }
    if (valid && value->has_label_provenance)
    {
        for (u32 block_index = 0; block_index < value->label_block_count; block_index += 1)
        {
            bool found = false;
            for (u32 path_index = 0; path_index < value->label_path_count; path_index += 1)
            {
                IrLabelProvenancePath* path = value->label_paths + path_index;
                found |= !path->is_non_label && ir_label_path_contains_block(path, value->label_blocks[block_index]);
            }
            valid &= found;
        }
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool ir_label_block_set_subset(IrValueLabelMetadata* subset, IrValueLabelMetadata* superset)
{
    if (!subset || !superset)
    {
        return false;
    }
    for (u32 index = 0; index < subset->label_block_count; index += 1)
    {
        if (!ir_label_block_set_contains(superset, subset->label_blocks[index]))
        {
            return false;
        }
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool ir_value_has_non_label_path(IrValueLabelMetadata* value)
{
    if (!value)
    {
        return false;
    }
    bool result = value->has_non_label_provenance;
    for (u32 path_index = 0; path_index < value->label_path_count; path_index += 1)
    {
        result |= value->label_paths[path_index].is_non_label;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool ir_label_path_blocks_subset(IrLabelProvenancePath* subset, IrLabelProvenancePath* superset)
{
    if (!subset || !superset || subset->is_non_label || superset->is_non_label)
    {
        return subset && superset && subset->is_non_label == superset->is_non_label;
    }
    for (u32 block_index = 0; block_index < subset->label_block_count; block_index += 1)
    {
        if (!ir_label_path_contains_block(superset, subset->label_blocks[block_index]))
        {
            return false;
        }
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool ir_label_path_blocks_equal(IrLabelProvenancePath* left, IrLabelProvenancePath* right)
{
    return left && right && ir_label_path_blocks_subset(left, right) && ir_label_path_blocks_subset(right, left);
}

BUSTER_GLOBAL_LOCAL bool ir_label_metadata_paths_transfer_exact(IrValueLabelMetadata* result, IrValueLabelMetadata* source, u64 base_offset, u64 base_size)
{
    if (!result || !source || base_offset > UINT64_MAX - base_size)
    {
        return false;
    }
    u64 base_end = base_offset + base_size;
    for (u32 source_index = 0; source_index < source->label_path_count; source_index += 1)
    {
        IrLabelProvenancePath* source_path = source->label_paths + source_index;
        if (source_path->offset > UINT64_MAX - source_path->size)
        {
            return false;
        }
        u64 source_end = source_path->offset + source_path->size;
        if (source_path->offset < base_offset || source_end > base_end)
        {
            continue;
        }
        bool found = false;
        for (u32 result_index = 0; result_index < result->label_path_count; result_index += 1)
        {
            IrLabelProvenancePath* result_path = result->label_paths + result_index;
            if (result_path->offset == source_path->offset - base_offset && result_path->size == source_path->size &&
                ir_label_path_blocks_equal(source_path, result_path))
            {
                found = true;
                break;
            }
        }
        if (!found)
        {
            return false;
        }
    }
    for (u32 result_index = 0; result_index < result->label_path_count; result_index += 1)
    {
        IrLabelProvenancePath* result_path = result->label_paths + result_index;
        bool found = false;
        for (u32 source_index = 0; source_index < source->label_path_count; source_index += 1)
        {
            IrLabelProvenancePath* source_path = source->label_paths + source_index;
            if (source_path->offset >= base_offset && source_path->offset <= UINT64_MAX - source_path->size &&
                source_path->offset + source_path->size <= base_end && source_path->offset - base_offset == result_path->offset &&
                source_path->size == result_path->size && ir_label_path_blocks_equal(source_path, result_path))
            {
                found = true;
                break;
            }
        }
        if (!found)
        {
            return false;
        }
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool ir_constant_index_value(IrFunction* function, IrValueId value, u64* index_out)
{
    if (!function || !index_out || value.value >= function->value_count)
    {
        return false;
    }
    IrInstructionId definition = function->values[value.value].definition;
    if (definition.value >= function->instruction_count)
    {
        return false;
    }
    IrInstruction* instruction = function->instructions + definition.value;
    if (instruction->opcode != IR_OPCODE_CONSTANT_INTEGER || instruction->immediate_count != 1 || !instruction->immediates || instruction->immediate_is_negative)
    {
        return false;
    }
    *index_out = instruction->immediates[0];
    return true;
}

BUSTER_GLOBAL_LOCAL bool ir_label_metadata_has_label(IrValueLabelMetadata* value)
{
    if (!value)
    {
        return false;
    }
    if (value->is_label_value || value->has_label_provenance)
    {
        return true;
    }
    for (u32 path_index = 0; path_index < value->label_path_count; path_index += 1)
    {
        if (!value->label_paths[path_index].is_non_label && value->label_paths[path_index].label_block_count)
        {
            return true;
        }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool ir_label_metadata_storage_transfer_valid(IrProgram* program, IrValueLabelMetadata* result, IrValueLabelMetadata* source)
{
    BUSTER_UNUSED(program);
    if (!result || !source || result->is_label_value)
    {
        return false;
    }
    if (!ir_label_metadata_has_label(result) && !ir_label_metadata_has_label(source))
    {
        return true;
    }
    if (ir_label_metadata_has_label(result) && !ir_label_metadata_has_label(source))
    {
        return false;
    }
    if (result->has_label_provenance && !ir_label_block_set_subset(result, source))
    {
        return false;
    }
    if (result->label_path_count && !source->label_path_count)
    {
        return false;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool ir_label_metadata_dynamic_index_transfer_valid(IrValueLabelMetadata* result, IrValueLabelMetadata* source, u64 array_size, u64 element_size)
{
    if (!result || !source)
    {
        return false;
    }
    if (!ir_label_metadata_has_label(result) && !ir_label_metadata_has_label(source))
    {
        return true;
    }
    if (!element_size || array_size < element_size || !source->label_path_count)
    {
        return !ir_label_metadata_has_label(result);
    }
    bool source_non_label = source->has_non_label_provenance;
    bool source_label = false;
    for (u32 source_index = 0; source_index < source->label_path_count; source_index += 1)
    {
        IrLabelProvenancePath* source_path = source->label_paths + source_index;
        if (source_path->offset > UINT64_MAX - source_path->size || source_path->offset + source_path->size > array_size)
        {
            return false;
        }
        source_non_label |= source_path->is_non_label;
        source_label |= !source_path->is_non_label;
        bool found = false;
        for (u32 result_index = 0; result_index < result->label_path_count; result_index += 1)
        {
            IrLabelProvenancePath* result_path = result->label_paths + result_index;
            if (result_path->offset != 0 || result_path->size != element_size)
            {
                continue;
            }
            if (source_path->is_non_label)
            {
                found |= result_path->is_non_label || result->has_non_label_provenance;
            }
            else
            {
                bool blocks_match = !result_path->is_non_label;
                for (u32 block_index = 0; blocks_match && block_index < source_path->label_block_count; block_index += 1)
                {
                    blocks_match = ir_label_path_contains_block(result_path, source_path->label_blocks[block_index]);
                }
                found |= blocks_match;
            }
        }
        if (!found)
        {
            return false;
        }
    }
    for (u32 result_index = 0; result_index < result->label_path_count; result_index += 1)
    {
        IrLabelProvenancePath* result_path = result->label_paths + result_index;
        if (result_path->offset != 0 || result_path->size != element_size)
        {
            return false;
        }
        if (result_path->is_non_label)
        {
            if (!source_non_label)
            {
                return false;
            }
        }
        else
        {
            for (u32 block_index = 0; block_index < result_path->label_block_count; block_index += 1)
            {
                bool found = false;
                for (u32 source_index = 0; source_index < source->label_path_count; source_index += 1)
                {
                    IrLabelProvenancePath* source_path = source->label_paths + source_index;
                    found |= !source_path->is_non_label && source_path->offset <= UINT64_MAX - source_path->size &&
                             source_path->offset + source_path->size <= array_size &&
                             ir_label_path_contains_block(source_path, result_path->label_blocks[block_index]);
                }
                if (!found)
                {
                    return false;
                }
            }
        }
    }
    if (source_non_label && !result->has_non_label_provenance)
    {
        return false;
    }
    if (source_label && !ir_label_metadata_has_label(result))
    {
        return false;
    }
    for (u32 source_index = 0; source_index < source->label_block_count; source_index += 1)
    {
        if (!ir_label_block_set_contains(result, source->label_blocks[source_index]))
        {
            return false;
        }
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool ir_label_metadata_aggregate_transfer_valid(IrProgram* program, IrFunction* function, IrInstruction* definition,
                                                                     IrValueLabelMetadata* result)
{
    if (!program || !function || !definition || !result || result->is_label_value)
    {
        return false;
    }
    bool result_has_label = ir_label_metadata_has_label(result);
    bool source_has_label = false;
    for (u32 operand_index = 0; operand_index < definition->operand_count; operand_index += 1)
    {
        if (!definition->operands || definition->operands[operand_index].value >= function->value_count)
        {
            return false;
        }
        IrValueLabelMetadata source = ir_value_label_metadata(function, definition->operands[operand_index]);
        source_has_label |= ir_label_metadata_has_label(&source);
    }
    if (!result_has_label && !source_has_label)
    {
        return true;
    }
    IrType* aggregate = ir_type_from_id(&program->types, definition->canonical_type);
    if (!aggregate || (definition->opcode == IR_OPCODE_ARRAY && aggregate->kind != IR_TYPE_ARRAY && aggregate->kind != IR_TYPE_VECTOR) ||
        (definition->opcode == IR_OPCODE_AGGREGATE && aggregate->kind != IR_TYPE_STRUCT && aggregate->kind != IR_TYPE_UNION))
    {
        return false;
    }
    bool source_non_label = false;
    bool source_label = false;
    for (u32 operand_index = 0; operand_index < definition->operand_count; operand_index += 1)
    {
        if (!definition->operands || definition->operands[operand_index].value >= function->value_count)
        {
            return false;
        }
        IrValueLabelMetadata source_metadata = ir_value_label_metadata(function, definition->operands[operand_index]);
        IrValueLabelMetadata* source = &source_metadata;
        IrType* source_type = ir_type_from_id(&program->types, function->values[definition->operands[operand_index].value].canonical_type);
        u64 base_offset = 0;
        u64 source_size = 0;
        if (definition->opcode == IR_OPCODE_ARRAY)
        {
            IrType* element = ir_type_from_id(&program->types, aggregate->element_type);
            if (!element || !element->layout.resolved || operand_index >= aggregate->element_count ||
                operand_index > UINT64_MAX / element->layout.size)
            {
                return false;
            }
            base_offset = operand_index * element->layout.size;
            source_size = element->layout.size;
        }
        else
        {
            u64 field_index = definition->immediate_count == definition->operand_count && definition->immediates ? definition->immediates[operand_index] : UINT64_MAX;
            if (field_index >= aggregate->field_count)
            {
                return false;
            }
            IrField* field = aggregate->fields + field_index;
            IrType* field_type = ir_type_from_id(&program->types, field->type);
            if (!field_type || !field_type->layout.resolved)
            {
                return false;
            }
            base_offset = field->offset;
            source_size = field_type->layout.size;
        }
        if (!source_type || !source_type->layout.resolved || base_offset > aggregate->layout.size || source_size > aggregate->layout.size - base_offset)
        {
            return false;
        }
        source_non_label |= ir_value_has_non_label_path(source);
        source_label |= ir_label_metadata_has_label(source);
        for (u32 path_index = 0; path_index < source->label_path_count; path_index += 1)
        {
            IrLabelProvenancePath* source_path = source->label_paths + path_index;
            if (source_path->offset > UINT64_MAX - source_path->size || source_path->offset + source_path->size > source_size ||
                base_offset > UINT64_MAX - source_path->offset)
            {
                return false;
            }
            u64 expected_offset = base_offset + source_path->offset;
            bool found = false;
            for (u32 result_index = 0; result_index < result->label_path_count; result_index += 1)
            {
                IrLabelProvenancePath* result_path = result->label_paths + result_index;
                if (result_path->offset != expected_offset || result_path->size != source_path->size)
                {
                    continue;
                }
                if (source_path->is_non_label)
                {
                    found |= result_path->is_non_label || result->has_non_label_provenance;
                }
                else
                {
                    bool blocks_match = !result_path->is_non_label;
                    for (u32 block_index = 0; blocks_match && block_index < source_path->label_block_count; block_index += 1)
                    {
                        blocks_match = ir_label_path_contains_block(result_path, source_path->label_blocks[block_index]);
                    }
                    found |= blocks_match;
                }
            }
            if (!found)
            {
                return false;
            }
        }
        if (source->is_label_value)
        {
            bool found = false;
            for (u32 result_index = 0; result_index < result->label_path_count; result_index += 1)
            {
                IrLabelProvenancePath* result_path = result->label_paths + result_index;
                if (result_path->offset != base_offset || result_path->size != source_size || result_path->is_non_label)
                {
                    continue;
                }
                bool blocks_match = source->label_block_count <= result_path->label_block_count;
                for (u32 block_index = 0; blocks_match && block_index < source->label_block_count; block_index += 1)
                {
                    blocks_match = ir_label_path_contains_block(result_path, source->label_blocks[block_index]);
                }
                found |= blocks_match;
            }
            if (!found)
            {
                return false;
            }
        }
    }
    if (source_non_label && !result->has_non_label_provenance)
    {
        return false;
    }
    if (source_label && !ir_label_metadata_has_label(result))
    {
        return false;
    }
    for (u32 result_index = 0; result_index < result->label_path_count; result_index += 1)
    {
        IrLabelProvenancePath* result_path = result->label_paths + result_index;
        bool found = false;
        for (u32 operand_index = 0; operand_index < definition->operand_count && !found; operand_index += 1)
        {
            IrValueLabelMetadata source_metadata = ir_value_label_metadata(function, definition->operands[operand_index]);
            IrValueLabelMetadata* source = &source_metadata;
            u64 base_offset = 0;
            u64 source_size = 0;
            if (definition->opcode == IR_OPCODE_ARRAY)
            {
                IrType* element = ir_type_from_id(&program->types, aggregate->element_type);
                base_offset = operand_index * element->layout.size;
                source_size = element->layout.size;
            }
            else
            {
                IrField* field = aggregate->fields + definition->immediates[operand_index];
                IrType* field_type = ir_type_from_id(&program->types, field->type);
                base_offset = field->offset;
                source_size = field_type->layout.size;
            }
            for (u32 path_index = 0; path_index < source->label_path_count; path_index += 1)
            {
                IrLabelProvenancePath* source_path = source->label_paths + path_index;
                if (source_path->offset <= UINT64_MAX - source_path->size && source_path->offset + source_path->size <= source_size &&
                    base_offset <= UINT64_MAX - source_path->offset && result_path->offset == base_offset + source_path->offset &&
                    result_path->size == source_path->size)
                {
                    if (source_path->is_non_label)
                    {
                        found |= result_path->is_non_label || result->has_non_label_provenance;
                    }
                    else
                    {
                        bool blocks_match = !result_path->is_non_label;
                        for (u32 block_index = 0; blocks_match && block_index < result_path->label_block_count; block_index += 1)
                        {
                            blocks_match = ir_label_path_contains_block(source_path, result_path->label_blocks[block_index]);
                        }
                        found |= blocks_match;
                    }
                }
            }
            if (source->is_label_value && result_path->offset == base_offset && result_path->size == source_size && !result_path->is_non_label)
            {
                bool blocks_match = source->label_block_count <= result_path->label_block_count;
                for (u32 block_index = 0; blocks_match && block_index < source->label_block_count; block_index += 1)
                {
                    blocks_match &= ir_label_path_contains_block(result_path, source->label_blocks[block_index]);
                }
                found |= blocks_match;
            }
        }
        if (!found)
        {
            return false;
        }
    }
    return true;
}

BUSTER_F_DECL bool ir_label_metadata_transfer_valid(IrProgram* program, IrFunction* function, IrValueId value_id)
{
    IrValue* result_slot = function && value_id.value < function->value_count ? function->values + value_id.value : 0;
    if (!function || !result_slot)
    {
        return false;
    }
    if (result_slot->definition.value >= function->instruction_count)
    {
        return true;
    }
    IrInstruction* definition = function->instructions + result_slot->definition.value;
    IrValue* first_slot = definition->operand_count && definition->operands && definition->operands[0].value < function->value_count
                              ? function->values + definition->operands[0].value
                              : 0;
    if (!function->label_metadata_count)
    {
        // With no metadata anywhere in the function, the full checks below
        // reduce to the operand-existence requirements of each transfer rule
        // (and LABEL_ADDRESS can never validate a metadata-free result).
        switch (definition->opcode)
        {
        case IR_OPCODE_LOAD:
        case IR_OPCODE_ATOMIC_LOAD:
        case IR_OPCODE_CAST:
        case IR_OPCODE_FIELD:
        case IR_OPCODE_INDEX:
        case IR_OPCODE_DEREFERENCE:
            return first_slot != 0;
        case IR_OPCODE_ARRAY:
        case IR_OPCODE_AGGREGATE:
        {
            if (!program)
            {
                return true;
            }
            for (u32 operand_index = 0; operand_index < definition->operand_count; operand_index += 1)
            {
                if (!definition->operands || definition->operands[operand_index].value >= function->value_count)
                {
                    return false;
                }
            }
            return true;
        }
        case IR_OPCODE_LABEL_ADDRESS:
            return false;
        default:
            return true;
        }
    }
    IrValueLabelMetadata result_metadata = ir_value_label_metadata(function, value_id);
    IrValueLabelMetadata* result = &result_metadata;
    IrValueLabelMetadata first_metadata = first_slot ? ir_value_label_metadata(function, definition->operands[0]) : (IrValueLabelMetadata){0};
    IrValueLabelMetadata* first = first_slot ? &first_metadata : 0;
    switch (definition->opcode)
    {
    case IR_OPCODE_ADDRESS_OF:
        return !result->is_label_value && !result->has_label_provenance && !result->label_blocks &&
               !result->label_block_count && !result->label_paths && !result->label_path_count;
    case IR_OPCODE_LOAD:
    case IR_OPCODE_ATOMIC_LOAD:
        if (!first)
        {
            return false;
        }
        if (result->is_label_value)
        {
            return first->has_label_provenance && !first->has_non_label_provenance && !ir_value_has_non_label_path(first) && !result->label_path_count &&
                   ir_label_block_sets_equal(result, first);
        }
        if (!ir_label_metadata_storage_transfer_valid(program, result, first))
        {
            return false;
        }
        if (!ir_label_metadata_has_label(first) && !ir_label_metadata_has_label(result))
        {
            return true;
        }
        IrType* load_source_type = program ? ir_type_from_id(&program->types, first_slot->canonical_type) : 0;
        return !program || !load_source_type || !load_source_type->layout.resolved ||
               ir_label_metadata_paths_transfer_exact(result, first, 0, load_source_type->layout.size);
    case IR_OPCODE_CAST:
        if (!first)
        {
            return false;
        }
        if (ir_label_metadata_has_label(first) || ir_label_metadata_has_label(result))
        {
            if (!program || !ir_canonical_void_pointer_type(program, first_slot->canonical_type) ||
                !ir_canonical_void_pointer_type(program, result_slot->canonical_type) || first_slot->canonical_type.value != result_slot->canonical_type.value ||
                definition->conversion_operation != IR_CONVERSION_IDENTITY)
            {
                return false;
            }
        }
        if (result->is_label_value)
        {
            return first->is_label_value && !first->has_non_label_provenance && !ir_value_has_non_label_path(first) && !result->label_path_count &&
                   ir_label_block_sets_equal(result, first);
        }
        if (!ir_label_metadata_storage_transfer_valid(program, result, first))
        {
            return false;
        }
        if (!ir_label_metadata_has_label(first) && !ir_label_metadata_has_label(result))
        {
            return true;
        }
        IrType* cast_source_type = program ? ir_type_from_id(&program->types, first_slot->canonical_type) : 0;
        return !program || !cast_source_type || !cast_source_type->layout.resolved ||
               ir_label_metadata_paths_transfer_exact(result, first, 0, cast_source_type->layout.size);
    case IR_OPCODE_FIELD:
        if (!first || result->is_label_value)
        {
            return false;
        }
        if (!ir_label_metadata_has_label(first) && !ir_label_metadata_has_label(result))
        {
            return true;
        }
        if (!program)
        {
            return !ir_label_metadata_has_label(result) || (ir_label_metadata_has_label(first) && ir_label_block_set_subset(result, first));
        }
        {
            IrType* aggregate = ir_type_from_id(&program->types, first_slot->canonical_type);
            u64 field_index = definition->immediate_count == 1 && definition->immediates ? definition->immediates[0] : UINT64_MAX;
            if (!aggregate || (aggregate->kind != IR_TYPE_STRUCT && aggregate->kind != IR_TYPE_UNION) || field_index >= aggregate->field_count)
            {
                return !ir_label_metadata_has_label(result) && !result->label_path_count;
            }
            IrField* field = aggregate->fields + field_index;
            IrType* field_type = ir_type_from_id(&program->types, field->type);
            if (!field_type || !field_type->layout.resolved)
            {
                return !ir_label_metadata_has_label(result) && !result->label_path_count;
            }
            if (aggregate->kind == IR_TYPE_UNION && ir_label_metadata_has_label(first) &&
                !(field_type->kind == IR_TYPE_POINTER && ir_canonical_void_pointer_type(program, field->type)))
            {
                return false;
            }
            if (!ir_label_metadata_storage_transfer_valid(program, result, first))
            {
                return false;
            }
            if (ir_label_metadata_has_label(result) && !first->label_path_count)
            {
                return false;
            }
            return ir_label_metadata_paths_transfer_exact(result, first, field->offset, field_type->layout.size);
        }
    case IR_OPCODE_INDEX:
        if (!first || result->is_label_value)
        {
            return false;
        }
        if (!ir_label_metadata_has_label(first) && !ir_label_metadata_has_label(result))
        {
            return true;
        }
        if (!program)
        {
            return !ir_label_metadata_has_label(result) || (ir_label_metadata_has_label(first) && ir_label_block_set_subset(result, first));
        }
        {
            IrType* base_type = ir_type_from_id(&program->types, first_slot->canonical_type);
            IrType* element_type = base_type ? ir_type_from_id(&program->types, base_type->element_type) : 0;
            u64 index = 0;
            bool constant = definition->operand_count == 2 && definition->operands && ir_constant_index_value(function, definition->operands[1], &index);
            if (!base_type || !element_type || !element_type->layout.resolved || !element_type->layout.size)
            {
                return !ir_label_metadata_has_label(result) && !result->label_path_count;
            }
            if (base_type->kind == IR_TYPE_POINTER && ir_label_metadata_has_label(first))
            {
                return false;
            }
            if (!ir_label_metadata_storage_transfer_valid(program, result, first))
            {
                return false;
            }
            if (constant)
            {
                if ((base_type->kind != IR_TYPE_ARRAY && base_type->kind != IR_TYPE_VECTOR) || index >= base_type->element_count ||
                    index > UINT64_MAX / element_type->layout.size)
                {
                    return !ir_label_metadata_has_label(result) && !result->label_path_count;
                }
                u64 offset = index * element_type->layout.size;
                if (offset > base_type->layout.size || element_type->layout.size > base_type->layout.size - offset)
                {
                    return false;
                }
                if (ir_label_metadata_has_label(result) && !first->label_path_count)
                {
                    return false;
                }
                return ir_label_metadata_paths_transfer_exact(result, first, offset, element_type->layout.size);
            }
            if (base_type->kind != IR_TYPE_ARRAY && base_type->kind != IR_TYPE_VECTOR)
            {
                return !ir_label_metadata_has_label(result) && !result->label_path_count;
            }
            if (!base_type->element_count || element_type->layout.size > UINT64_MAX / base_type->element_count)
            {
                return !ir_label_metadata_has_label(result) && !result->label_path_count;
            }
            u64 array_size = element_type->layout.size * base_type->element_count;
            if (array_size > base_type->layout.size)
            {
                return false;
            }
            if (element_type->kind == IR_TYPE_ARRAY || element_type->kind == IR_TYPE_STRUCT || element_type->kind == IR_TYPE_UNION)
            {
                // A dynamic aggregate element may contain label-bearing
                // subobjects at offsets that are not representable by the
                // scalar transfer below.  Do not accept either a forged
                // result or a silently incomplete transfer: the frontend
                // rejects this case until the metadata model grows a
                // wildcard/subobject representation.
                return !ir_label_metadata_has_label(first) && !ir_label_metadata_has_label(result);
            }
            return ir_label_metadata_dynamic_index_transfer_valid(result, first, array_size, element_type->layout.size);
        }
    case IR_OPCODE_DEREFERENCE:
        return first && !ir_label_metadata_has_label(first) && !ir_label_metadata_has_label(result);
    case IR_OPCODE_ARRAY:
    case IR_OPCODE_AGGREGATE:
    {
        if (program)
        {
            return ir_label_metadata_aggregate_transfer_valid(program, function, definition, result);
        }
        for (u32 block_index = 0; block_index < result->label_block_count; block_index += 1)
        {
            bool found = false;
            for (u32 operand_index = 0; operand_index < definition->operand_count && !found; operand_index += 1)
            {
                if (definition->operands && definition->operands[operand_index].value < function->value_count)
                {
                    IrValueLabelMetadata operand_metadata = ir_value_label_metadata(function, definition->operands[operand_index]);
                    found = ir_label_block_set_contains(&operand_metadata, result->label_blocks[block_index]);
                }
            }
            if (!found)
            {
                return false;
            }
        }
        return !result->is_label_value;
    }
    case IR_OPCODE_LABEL_ADDRESS:
        return (!program || ir_canonical_void_pointer_type(program, definition->canonical_type)) && ir_label_provenance_valid(result) &&
               definition->target_count == 1 && definition->targets && result->label_block_count == 1 &&
               result->label_blocks[0].value == definition->targets[0].value;
    case IR_OPCODE_LOCAL:
    case IR_OPCODE_GLOBAL:
        return !result->is_label_value;
    default:
        return !result->is_label_value && !result->has_label_provenance && !result->label_block_count && !result->label_blocks && !result->label_path_count &&
               !result->label_paths;
    }
}

BUSTER_F_DECL bool ir_label_block_parameter_provenance_valid(IrFunction* function, IrBlockParameter* parameter)
{
    if (!function || !parameter || parameter->value.value >= function->value_count)
    {
        return false;
    }
    IrValueLabelMetadata destination_metadata = ir_value_label_metadata(function, parameter->value);
    IrValueLabelMetadata* destination = &destination_metadata;
    bool incoming_non_label = false;
    bool incoming_label = false;
    bool all_incoming_pure_labels = true;
    bool incoming_paths = false;
    u32 incoming_block_count = 0;
    for (IrIncoming* incoming = parameter->first_incoming; incoming; incoming = incoming->next)
    {
        if (incoming->value.value >= function->value_count)
        {
            return false;
        }
        IrValueLabelMetadata source_metadata = ir_value_label_metadata(function, incoming->value);
        IrValueLabelMetadata* source = &source_metadata;
        bool source_non_label = source->has_non_label_provenance;
        for (u32 path_index = 0; path_index < source->label_path_count; path_index += 1)
        {
            source_non_label |= source->label_paths[path_index].is_non_label;
        }
        bool source_label = source->is_label_value || source->has_label_provenance || source->label_block_count != 0 || source->label_path_count != 0;
        incoming_label |= source_label;
        incoming_non_label |= source_non_label;
        incoming_paths |= source->label_path_count != 0;
        all_incoming_pure_labels &= source->is_label_value && !source->has_label_provenance && !source_non_label && !source->label_path_count;
        for (u32 block_index = 0; block_index < source->label_block_count; block_index += 1)
        {
            bool found = false;
            for (IrIncoming* previous = parameter->first_incoming; previous && previous != incoming; previous = previous->next)
            {
                IrValueLabelMetadata previous_metadata = ir_value_label_metadata(function, previous->value);
                found |= previous->value.value < function->value_count && ir_label_block_set_contains(&previous_metadata, source->label_blocks[block_index]);
            }
            if (!found)
            {
                incoming_block_count += 1;
            }
        }
        for (u32 path_index = 0; path_index < source->label_path_count; path_index += 1)
        {
            IrLabelProvenancePath* source_path = source->label_paths + path_index;
            bool found = false;
            for (u32 destination_path_index = 0; destination_path_index < destination->label_path_count; destination_path_index += 1)
            {
                IrLabelProvenancePath* destination_path = destination->label_paths + destination_path_index;
                bool blocks_match = source_path->is_non_label ||
                                     (!destination_path->is_non_label && source_path->label_block_count <= destination_path->label_block_count);
                if (blocks_match && destination_path->offset == source_path->offset && destination_path->size == source_path->size)
                {
                    for (u32 block_index = 0; blocks_match && block_index < source_path->label_block_count; block_index += 1)
                    {
                        blocks_match = ir_label_path_contains_block(destination_path, source_path->label_blocks[block_index]);
                    }
                    found |= blocks_match;
                }
            }
            if (!found)
            {
                return false;
            }
        }
    }
    bool destination_has_labels = destination->label_block_count != 0 || destination->is_label_value || destination->has_label_provenance || destination->label_path_count != 0;
    bool destination_non_label = destination->has_non_label_provenance;
    if (destination->is_label_value)
    {
        if (!all_incoming_pure_labels || destination_non_label || destination->has_label_provenance || destination->label_path_count != 0)
        {
            return false;
        }
    }
    else if (destination_has_labels != incoming_label || destination->has_label_provenance != (incoming_block_count != 0) ||
             destination_non_label != incoming_non_label)
    {
        return false;
    }
    if (destination->label_block_count != incoming_block_count)
    {
        return false;
    }
    for (u32 destination_block_index = 0; destination_block_index < destination->label_block_count; destination_block_index += 1)
    {
        bool found = false;
        for (IrIncoming* incoming = parameter->first_incoming; incoming && !found; incoming = incoming->next)
        {
            IrValueLabelMetadata source_metadata = ir_value_label_metadata(function, incoming->value);
            found = incoming->value.value < function->value_count && ir_label_block_set_contains(&source_metadata, destination->label_blocks[destination_block_index]);
        }
        if (!found)
        {
            return false;
        }
    }
    if (!incoming_paths && destination->label_path_count)
    {
        return false;
    }
    for (u32 destination_path_index = 0; destination_path_index < destination->label_path_count; destination_path_index += 1)
    {
        IrLabelProvenancePath* destination_path = destination->label_paths + destination_path_index;
        bool incoming_path = false;
        bool incoming_non_label_path = false;
        bool incoming_label_path = false;
        for (IrIncoming* incoming = parameter->first_incoming; incoming; incoming = incoming->next)
        {
            IrValueLabelMetadata source = incoming->value.value < function->value_count ? ir_value_label_metadata(function, incoming->value)
                                                                                        : (IrValueLabelMetadata){0};
            for (u32 path_index = 0; path_index < source.label_path_count; path_index += 1)
            {
                IrLabelProvenancePath* source_path = source.label_paths + path_index;
                if (source_path->offset == destination_path->offset && source_path->size == destination_path->size)
                {
                    incoming_path = true;
                    incoming_non_label_path |= source_path->is_non_label;
                    incoming_label_path |= !source_path->is_non_label && source_path->label_block_count != 0;
                }
            }
        }
        if (!incoming_path || (destination_path->is_non_label ? !incoming_non_label_path || incoming_label_path : !incoming_label_path))
        {
            return false;
        }
        if (!destination_path->is_non_label)
        {
            for (u32 block_index = 0; block_index < destination_path->label_block_count; block_index += 1)
            {
                bool found = false;
                for (IrIncoming* incoming = parameter->first_incoming; incoming && !found; incoming = incoming->next)
                {
                    IrValueLabelMetadata source = incoming->value.value < function->value_count ? ir_value_label_metadata(function, incoming->value)
                                                                                                : (IrValueLabelMetadata){0};
                    for (u32 path_index = 0; path_index < source.label_path_count; path_index += 1)
                    {
                        IrLabelProvenancePath* source_path = source.label_paths + path_index;
                        found |= source_path->offset == destination_path->offset && source_path->size == destination_path->size && !source_path->is_non_label &&
                                 ir_label_path_contains_block(source_path, destination_path->label_blocks[block_index]);
                    }
                }
                if (!found)
                {
                    return false;
                }
            }
        }
    }
    return true;
}

bool ir_label_provenance_contains(IrValueLabelMetadata* value, IrBlockId block)
{
    if (!ir_label_provenance_valid(value))
    {
        return false;
    }
    for (u32 index = 0; index < value->label_block_count; index += 1)
    {
        if (value->label_blocks[index].value == block.value)
        {
            return true;
        }
    }
    return false;
}

void ir_label_provenance_union(Arena* arena, IrFunction* function, IrValueId destination_id, IrValueId source_id)
{
    if (!arena || !function || destination_id.value >= function->value_count || source_id.value >= function->value_count)
    {
        return;
    }
    IrValueLabelMetadata source = ir_value_label_metadata(function, source_id);
    bool source_blocks_present = source.is_label_value && source.label_block_count && source.label_blocks;
    if (!source.has_non_label_provenance && !source_blocks_present)
    {
        return;
    }
    IrValueLabelMetadata* destination = ir_value_label_metadata_ensure(arena, function, destination_id);
    if (!destination)
    {
        return;
    }
    destination->has_non_label_provenance |= source.has_non_label_provenance;
    if (!source_blocks_present)
    {
        return;
    }
    u32 existing_count = destination->is_label_value && destination->label_blocks ? destination->label_block_count : 0;
    u32 capacity = existing_count + source.label_block_count;
    IrBlockId* blocks = arena_allocate(arena, IrBlockId, capacity);
    u32 count = 0;
    for (u32 index = 0; index < existing_count; index += 1)
    {
        blocks[count++] = destination->label_blocks[index];
    }
    for (u32 source_index = 0; source_index < source.label_block_count; source_index += 1)
    {
        IrBlockId block = source.label_blocks[source_index];
        bool found = false;
        for (u32 index = 0; index < count; index += 1)
        {
            found |= blocks[index].value == block.value;
        }
        if (!found)
        {
            blocks[count++] = block;
        }
    }
    bool has_non_label = destination->has_non_label_provenance || source.has_non_label_provenance;
    destination->is_label_value = count != 0;
    destination->has_label_provenance = false;
    destination->has_non_label_provenance = has_non_label;
    destination->label_blocks = blocks;
    destination->label_block_count = count;
    destination->label_paths = 0;
    destination->label_path_count = 0;
}

BUSTER_GLOBAL_LOCAL void ir_label_storage_union_source(Arena* arena, IrFunction* function, IrValueId destination_id, IrValueId source_id);

void ir_label_provenance_copy(Arena* arena, IrFunction* function, IrValueId destination_id, IrValueId source_id)
{
    if (!function || destination_id.value >= function->value_count)
    {
        return;
    }
    IrValueLabelMetadata* existing = ir_value_label_metadata_find(function, destination_id);
    if (existing)
    {
        *existing = (IrValueLabelMetadata){0};
    }
    if (source_id.value >= function->value_count)
    {
        return;
    }
    IrValueLabelMetadata source = ir_value_label_metadata(function, source_id);
    if (source.has_non_label_provenance)
    {
        IrValueLabelMetadata* destination = arena ? ir_value_label_metadata_ensure(arena, function, destination_id) : existing;
        if (destination)
        {
            destination->has_non_label_provenance = true;
        }
    }
    if (source.has_label_provenance || source.label_path_count)
    {
        ir_label_storage_union_source(arena, function, destination_id, source_id);
    }
    else
    {
        ir_label_provenance_union(arena, function, destination_id, source_id);
    }
}

BUSTER_GLOBAL_LOCAL void ir_label_storage_path_append(Arena* arena, IrValueLabelMetadata* destination, IrLabelProvenancePath* source_path)
{
    if (!arena || !destination || !source_path)
    {
        return;
    }
    for (u32 path_index = 0; path_index < destination->label_path_count; path_index += 1)
    {
        IrLabelProvenancePath* destination_path = destination->label_paths + path_index;
        if (destination_path->offset != source_path->offset || destination_path->size != source_path->size)
        {
            continue;
        }
        if (destination_path->is_non_label && source_path->is_non_label)
        {
            return;
        }
        if (destination_path->is_non_label != source_path->is_non_label)
        {
            destination->has_non_label_provenance = true;
            if (destination_path->is_non_label)
            {
                destination_path->is_non_label = false;
                destination_path->label_blocks = arena_allocate(arena, IrBlockId, source_path->label_block_count);
                destination_path->label_block_count = 0;
            }
        }
        u32 existing_count = destination_path->label_block_count;
        IrBlockId* merged = arena_allocate(arena, IrBlockId, existing_count + source_path->label_block_count);
        u32 merged_count = 0;
        for (u32 block_index = 0; block_index < existing_count; block_index += 1)
        {
            merged[merged_count++] = destination_path->label_blocks[block_index];
        }
        for (u32 block_index = 0; block_index < source_path->label_block_count; block_index += 1)
        {
            bool found = false;
            for (u32 merged_index = 0; merged_index < merged_count; merged_index += 1)
            {
                found |= merged[merged_index].value == source_path->label_blocks[block_index].value;
            }
            if (!found)
            {
                merged[merged_count++] = source_path->label_blocks[block_index];
            }
        }
        destination_path->label_blocks = merged_count ? merged : 0;
        destination_path->label_block_count = merged_count;
        return;
    }
    IrLabelProvenancePath* paths = arena_allocate(arena, IrLabelProvenancePath, destination->label_path_count + 1);
    for (u32 path_index = 0; path_index < destination->label_path_count; path_index += 1)
    {
        paths[path_index] = destination->label_paths[path_index];
    }
    paths[destination->label_path_count] = *source_path;
    destination->label_paths = paths;
    destination->label_path_count += 1;
}

BUSTER_GLOBAL_LOCAL void ir_label_storage_union_source(Arena* arena, IrFunction* function, IrValueId destination_id, IrValueId source_id)
{
    if (!arena || !function || destination_id.value >= function->value_count || source_id.value >= function->value_count)
    {
        return;
    }
    IrValueLabelMetadata source = ir_value_label_metadata(function, source_id);
    bool source_blocks_present = (source.is_label_value || source.has_label_provenance) && source.label_block_count && source.label_blocks;
    bool source_has_content = source.has_non_label_provenance || source.label_path_count || source_blocks_present;
    IrValueLabelMetadata* destination =
        source_has_content ? ir_value_label_metadata_ensure(arena, function, destination_id) : ir_value_label_metadata_find(function, destination_id);
    if (!destination)
    {
        return;
    }
    // Preserve ordinary-pointer alternatives even when the source contributes
    // no label blocks; otherwise a later load can become falsely label-only.
    destination->has_non_label_provenance |= source.has_non_label_provenance;
    for (u32 path_index = 0; path_index < source.label_path_count; path_index += 1)
    {
        IrLabelProvenancePath* source_path = source.label_paths + path_index;
        destination->has_non_label_provenance |= source_path->is_non_label;
        ir_label_storage_path_append(arena, destination, source_path);
    }
    if (!source_blocks_present)
    {
        if (destination->is_label_value && destination->has_non_label_provenance)
        {
            destination->is_label_value = false;
            destination->has_label_provenance = destination->label_block_count != 0;
        }
        return;
    }
    u32 existing_count = destination->has_label_provenance && destination->label_blocks ? destination->label_block_count
                                                                                           : destination->is_label_value && destination->label_blocks
                                                                                                 ? destination->label_block_count
                                                                                                 : 0;
    IrBlockId* blocks = arena_allocate(arena, IrBlockId, existing_count + source.label_block_count);
    u32 count = 0;
    for (u32 index = 0; index < existing_count; index += 1)
    {
        blocks[count++] = destination->label_blocks[index];
    }
    for (u32 source_index = 0; source_index < source.label_block_count; source_index += 1)
    {
        IrBlockId block = source.label_blocks[source_index];
        bool found = false;
        for (u32 index = 0; index < count; index += 1)
        {
            found |= blocks[index].value == block.value;
        }
        if (!found)
        {
            blocks[count++] = block;
        }
    }
    destination->is_label_value = false;
    destination->has_label_provenance = count != 0;
    destination->label_blocks = blocks;
    destination->label_block_count = count;
}

void ir_label_storage_provenance_union(Arena* arena, IrFunction* function, IrValueId destination_id, IrValueId source_id)
{
    ir_label_storage_union_source(arena, function, destination_id, source_id);
}

void ir_label_storage_provenance_copy(Arena* arena, IrFunction* function, IrValueId destination_id, IrValueId source_id)
{
    if (!function || destination_id.value >= function->value_count)
    {
        return;
    }
    IrValueLabelMetadata* existing = ir_value_label_metadata_find(function, destination_id);
    if (existing)
    {
        *existing = (IrValueLabelMetadata){0};
    }
    ir_label_storage_union_source(arena, function, destination_id, source_id);
}

void ir_label_provenance_load(Arena* arena, IrFunction* function, IrValueId destination_id, IrValueId source_id)
{
    if (!function || destination_id.value >= function->value_count)
    {
        return;
    }
    IrValueLabelMetadata* existing = ir_value_label_metadata_find(function, destination_id);
    if (existing)
    {
        *existing = (IrValueLabelMetadata){0};
    }
    if (source_id.value >= function->value_count)
    {
        return;
    }
    IrValueLabelMetadata source = ir_value_label_metadata(function, source_id);
    if (source.has_non_label_provenance)
    {
        IrValueLabelMetadata* non_label_destination = arena ? ir_value_label_metadata_ensure(arena, function, destination_id) : existing;
        if (non_label_destination)
        {
            non_label_destination->has_non_label_provenance = true;
        }
    }
    if (!arena || !source.label_block_count || !source.label_blocks)
    {
        return;
    }
    bool scalar_label_paths = !source.has_non_label_provenance;
    for (u32 path_index = 0; scalar_label_paths && path_index < source.label_path_count; path_index += 1)
    {
        IrLabelProvenancePath* path = source.label_paths + path_index;
        scalar_label_paths = !path->is_non_label && path->offset == 0 && path->label_block_count != 0;
    }
    if (scalar_label_paths)
    {
        IrValueLabelMetadata* destination = ir_value_label_metadata_ensure(arena, function, destination_id);
        if (!destination)
        {
            return;
        }
        destination->is_label_value = true;
        destination->label_blocks = arena_allocate(arena, IrBlockId, source.label_block_count);
        memcpy(destination->label_blocks, source.label_blocks, sizeof(IrBlockId) * source.label_block_count);
        destination->label_block_count = source.label_block_count;
    }
    else
    {
        ir_label_storage_provenance_copy(arena, function, destination_id, source_id);
    }
}

u32 ir_inline_assembly_label_operand_base(IrInstruction* instruction)
{
    if (!instruction || instruction->operand_count != instruction->immediate_count ||
        (instruction->operand_count && !instruction->immediates))
    {
        return UINT32_MAX;
    }
    u64 result = instruction->operand_count;
    for (u32 index = 0; index < instruction->operand_count; index += 1)
    {
        u64 constraint = instruction->immediates[index];
        if ((constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_OUTPUT) && (constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_READ_WRITE))
        {
            result += 1;
        }
    }
    return result > UINT32_MAX ? UINT32_MAX : (u32)result;
}

bool ir_inline_assembly_jump_target(IrInstruction* instruction, String8 literal, String8 prefix, u32* target_index_out)
{
    if (!instruction || !target_index_out || !literal.pointer || !prefix.pointer || literal.length <= prefix.length || instruction->target_count < 2)
    {
        return false;
    }
    for (u64 index = 0; index < prefix.length; index += 1)
    {
        if (literal.pointer[index] != prefix.pointer[index])
        {
            return false;
        }
    }
    u64 suffix_start = prefix.length;
    u64 suffix_length = literal.length - suffix_start;
    u32 label_count = instruction->target_count - 1;
    if (suffix_length >= 3 && literal.pointer[suffix_start] == '[' && literal.pointer[literal.length - 1] == ']')
    {
        if (instruction->label_name_count != label_count || !instruction->label_names)
        {
            return false;
        }
        String8 name = {
            .pointer = literal.pointer + suffix_start + 1,
            .length = suffix_length - 2,
        };
        for (u32 label_index = 0; label_index < instruction->label_name_count; label_index += 1)
        {
            if (string_equal(name, instruction->label_names[label_index]))
            {
                *target_index_out = label_index + 1;
                return true;
            }
        }
        return false;
    }
    u32 operand_base = ir_inline_assembly_label_operand_base(instruction);
    if (operand_base == UINT32_MAX)
    {
        return false;
    }
    u64 operand_index = 0;
    for (u64 index = suffix_start; index < literal.length; index += 1)
    {
        u8 digit = (u8)literal.pointer[index];
        if (digit < '0' || digit > '9' || operand_index > (UINT64_MAX - (digit - '0')) / 10)
        {
            return false;
        }
        operand_index = operand_index * 10 + (digit - '0');
    }
    if (operand_index < operand_base)
    {
        return false;
    }
    u64 label_index = operand_index - operand_base;
    if (label_index >= label_count)
    {
        return false;
    }
    *target_index_out = 1 + (u32)label_index;
    return true;
}

BUSTER_GLOBAL_LOCAL u32 ir_node_arity(AstNode* node)
{
    switch (node->id)
    {
    case AST_NODE_CONSTANT_INTEGER:
    case AST_NODE_CONSTANT_FLOAT:
    case AST_NODE_CONSTANT_CHARACTER:
    case AST_NODE_CONSTANT_STRING:
    case AST_NODE_IDENTIFIER:
    case AST_NODE_UNDEFINED:
    case AST_NODE_ENUM_LITERAL:
        return 0;
    case AST_NODE_ARRAY_LITERAL:
        return node->array_literal.element_count;
    case AST_NODE_ARRAY_INDEX:
        return 2;
    case AST_NODE_ARRAY_SLICE:
    {
        return 1 + (u32)node->array_slice.has_start + (u32)node->array_slice.has_end;
    }
    case AST_NODE_AGGREGATE_LITERAL:
        return node->aggregate_literal.field_count;
    case AST_NODE_CALL:
        return 1 + node->call.argument_count;
    case AST_NODE_INTRINSIC_CALL:
        return node->intrinsic_call.argument_count;
    case AST_NODE_MEMBER_ACCESS:
    case AST_NODE_UNARY_MINUS:
    case AST_NODE_UNARY_PLUS:
    case AST_NODE_UNARY_LOGICAL_NOT:
    case AST_NODE_UNARY_BITWISE_NOT:
    case AST_NODE_ADDRESS_OF:
    case AST_NODE_DEREFERENCE:
        return 1;
    case AST_NODE_BINARY_PLUS:
    case AST_NODE_BINARY_MINUS:
    case AST_NODE_BINARY_ASTERISK:
    case AST_NODE_BINARY_SLASH:
    case AST_NODE_BINARY_PERCENT:
    case AST_NODE_BINARY_SHIFT_LEFT:
    case AST_NODE_BINARY_SHIFT_RIGHT:
    case AST_NODE_BINARY_EQUAL:
    case AST_NODE_BINARY_NOT_EQUAL:
    case AST_NODE_BINARY_LESS:
    case AST_NODE_BINARY_LESS_EQUAL:
    case AST_NODE_BINARY_GREATER:
    case AST_NODE_BINARY_GREATER_EQUAL:
    case AST_NODE_BINARY_AMPERSAND:
    case AST_NODE_BINARY_BAR:
    case AST_NODE_BINARY_CARET:
    case AST_NODE_BINARY_BOOLEAN_AND:
    case AST_NODE_BINARY_BOOLEAN_OR:
    case AST_NODE_BINARY_BOOLEAN_AND_SHORT_CIRCUIT:
    case AST_NODE_BINARY_BOOLEAN_OR_SHORT_CIRCUIT:
    case AST_NODE_BINARY_RANGE:
        return 2;
    case AST_NODE_COUNT:
        break;
    }
    return 0;
}

BUSTER_GLOBAL_LOCAL IrBlockId ir_block_create(IrBuilder* builder)
{
    IrFunction* function = builder->function;
    BUSTER_CHECK(function->block_count < function->block_capacity);
    IrBlockId id = {.value = function->block_count};
    function->blocks[function->block_count] = (IrBlock){
        .local_values = arena_allocate(builder->result_arena, IrValueId, function->local_count),
        .first_instruction = IR_INSTRUCTION_ID_INVALID,
        .last_instruction = IR_INSTRUCTION_ID_INVALID,
        .id = id,
    };
    for (u32 local_index = 0; local_index < function->local_count; local_index += 1)
    {
        function->blocks[function->block_count].local_values[local_index] = IR_VALUE_ID_INVALID;
    }
    function->block_count += 1;
    return id;
}

BUSTER_GLOBAL_LOCAL IrValueId ir_value_create(IrBuilder* builder, AnalysisTypeId type, IrInstructionId definition, IrValueCategory category)
{
    IrFunction* function = builder->function;
    BUSTER_CHECK(function->value_count < function->value_capacity);
    IrValueId id = {.value = function->value_count};
    function->values[function->value_count] = (IrValue){
        .type = type,
        .canonical_type = IR_TYPE_ID_INVALID,
        .definition = definition,
        .category = category,
    };
    function->value_count += 1;
    return id;
}

BUSTER_GLOBAL_LOCAL void ir_predecessor_add(IrBuilder* builder, IrBlockId target, IrBlockId predecessor)
{
    IrBlock* block = builder->function->blocks + target.value;
    BUSTER_CHECK(!block->sealed);
    IrPredecessor* edge = arena_allocate(builder->result_arena, IrPredecessor, 1);
    *edge = (IrPredecessor){.block = predecessor};
    if (block->last_predecessor)
    {
        block->last_predecessor->next = edge;
    }
    else
    {
        block->first_predecessor = edge;
    }
    block->last_predecessor = edge;
    block->predecessor_count += 1;
}

BUSTER_GLOBAL_LOCAL IrBlockParameter* ir_block_parameter_create(IrBuilder* builder, IrBlockId block_id, AnalysisLocalId local, AnalysisTypeId type)
{
    IrBlock* block = builder->function->blocks + block_id.value;
    IrBlockParameter* parameter = arena_allocate(builder->result_arena, IrBlockParameter, 1);
    *parameter = (IrBlockParameter){
        .type = type,
        .local = local,
        .canonical_type = IR_TYPE_ID_INVALID,
        .canonical_local = IR_LOCAL_ID_INVALID,
    };
    parameter->value = ir_value_create(builder, type, IR_INSTRUCTION_ID_INVALID, IR_VALUE_VALUE);
    if (block->last_parameter)
    {
        block->last_parameter->next = parameter;
    }
    else
    {
        block->first_parameter = parameter;
    }
    block->last_parameter = parameter;
    block->parameter_count += 1;
    if (local.value != ANALYSIS_ID_UNDERLYING_INVALID)
    {
        block->local_values[local.value] = parameter->value;
    }
    return parameter;
}

BUSTER_GLOBAL_LOCAL void ir_block_parameter_incoming_add(IrBuilder* builder, IrBlockParameter* parameter, IrBlockId predecessor, IrValueId value)
{
    IrIncoming* incoming = arena_allocate(builder->result_arena, IrIncoming, 1);
    *incoming = (IrIncoming){.predecessor = predecessor, .value = value};
    if (parameter->last_incoming)
    {
        parameter->last_incoming->next = incoming;
    }
    else
    {
        parameter->first_incoming = incoming;
    }
    parameter->last_incoming = incoming;
    parameter->incoming_count += 1;
    if (value.value < builder->function->value_count && parameter->value.value < builder->function->value_count)
    {
        IrValueLabelMetadata* destination = ir_value_label_metadata_find(builder->function, parameter->value);
        IrValueLabelMetadata source = ir_value_label_metadata(builder->function, value);
        bool source_storage = source.has_label_provenance || source.label_path_count || source.has_non_label_provenance;
        bool destination_storage =
            destination && (destination->has_label_provenance || destination->label_path_count || destination->has_non_label_provenance);
        if (!source_storage && !destination_storage && source.is_label_value)
        {
            ir_label_provenance_union(builder->result_arena, builder->function, parameter->value, value);
        }
        else
        {
            if (destination && destination->is_label_value)
            {
                destination->is_label_value = false;
                destination->has_label_provenance = destination->label_block_count != 0;
            }
            ir_label_storage_union_source(builder->result_arena, builder->function, parameter->value, value);
        }
        // A label-only incoming edge may be added after an ordinary pointer
        // edge.  The union helper deliberately keeps the block set, but a
        // mixed value is storage provenance, never a pure LABEL_ADDRESS value.
        destination = ir_value_label_metadata_find(builder->function, parameter->value);
        if (destination && destination->is_label_value && destination->has_non_label_provenance)
        {
            destination->is_label_value = false;
            destination->has_label_provenance = destination->label_block_count != 0;
        }
    }
}

typedef enum IrSsaReadState
{
    IR_SSA_READ_BEGIN,
    IR_SSA_READ_WAIT_SINGLE,
    IR_SSA_READ_MULTI,
    IR_SSA_READ_WAIT_MULTI,
} IrSsaReadState;

typedef struct IrSsaReadFrame IrSsaReadFrame;
struct IrSsaReadFrame
{
    IrPredecessor* predecessor;
    IrBlockParameter* parameter;
    IrBlockId block;
    IrBlockId pending_predecessor;
    IrSsaReadState state;
};

BUSTER_GLOBAL_LOCAL IrValueId ir_ssa_read(IrBuilder* builder, IrBlockId block_id, AnalysisLocalId local)
{
    IrFunction* function = builder->function;
    IrSsaReadFrame* frames = arena_allocate(builder->scratch_arena, IrSsaReadFrame, function->block_count + 1);
    u32 depth = 1;
    IrValueId completed = IR_VALUE_ID_INVALID;
    frames[0] = (IrSsaReadFrame){.block = block_id};
    while (depth)
    {
        IrSsaReadFrame* frame = frames + depth - 1;
        IrBlock* block = function->blocks + frame->block.value;
        if (frame->state == IR_SSA_READ_BEGIN)
        {
            IrValueId existing = block->local_values[local.value];
            if (existing.value != IR_ID_UNDERLYING_INVALID)
            {
                completed = existing;
                depth -= 1;
                continue;
            }
            AnalysisTypeId type = builder->body->locals[local.value].type;
            if (!block->sealed)
            {
                completed = ir_block_parameter_create(builder, frame->block, local, type)->value;
                depth -= 1;
                continue;
            }
            if (!block->predecessor_count)
            {
                completed = IR_VALUE_ID_INVALID;
                depth -= 1;
                continue;
            }
            if (block->predecessor_count == 1)
            {
                frame->state = IR_SSA_READ_WAIT_SINGLE;
                BUSTER_CHECK(depth < function->block_count + 1);
                frames[depth] = (IrSsaReadFrame){
                    .block = block->first_predecessor->block,
                };
                depth += 1;
                continue;
            }
            frame->parameter = ir_block_parameter_create(builder, frame->block, local, type);
            frame->predecessor = block->first_predecessor;
            frame->state = IR_SSA_READ_MULTI;
        }
        else if (frame->state == IR_SSA_READ_WAIT_SINGLE)
        {
            block->local_values[local.value] = completed;
            depth -= 1;
        }
        else if (frame->state == IR_SSA_READ_MULTI)
        {
            if (!frame->predecessor)
            {
                completed = frame->parameter->value;
                depth -= 1;
                continue;
            }
            frame->pending_predecessor = frame->predecessor->block;
            frame->predecessor = frame->predecessor->next;
            frame->state = IR_SSA_READ_WAIT_MULTI;
            BUSTER_CHECK(depth < function->block_count + 1);
            frames[depth] = (IrSsaReadFrame){.block = frame->pending_predecessor};
            depth += 1;
        }
        else
        {
            BUSTER_CHECK(completed.value != IR_ID_UNDERLYING_INVALID);
            ir_block_parameter_incoming_add(builder, frame->parameter, frame->pending_predecessor, completed);
            frame->state = IR_SSA_READ_MULTI;
        }
    }
    return completed;
}

BUSTER_GLOBAL_LOCAL void ir_ssa_write(IrBuilder* builder, IrBlockId block, AnalysisLocalId local, IrValueId value)
{
    BUSTER_CHECK(local.value < builder->function->local_count);
    builder->function->blocks[block.value].local_values[local.value] = value;
}

BUSTER_GLOBAL_LOCAL void ir_block_seal(IrBuilder* builder, IrBlockId block_id)
{
    IrBlock* block = builder->function->blocks + block_id.value;
    if (block->sealed)
    {
        return;
    }
    block->sealed = true;
    for (IrBlockParameter* parameter = block->first_parameter; parameter; parameter = parameter->next)
    {
        if (parameter->incoming_count)
        {
            continue;
        }
        for (IrPredecessor* predecessor = block->first_predecessor; predecessor; predecessor = predecessor->next)
        {
            IrValueId incoming = ir_ssa_read(builder, predecessor->block, parameter->local);
            BUSTER_CHECK(incoming.value != IR_ID_UNDERLYING_INVALID);
            ir_block_parameter_incoming_add(builder, parameter, predecessor->block, incoming);
        }
    }
}

BUSTER_GLOBAL_LOCAL IrInstruction* ir_emit(IrBuilder* builder, IrOpcode opcode, AnalysisTypeId type, IrValueCategory category, ParserSourceRange source,
                                           IrValueId* operands, u32 operand_count, bool produces_value)
{
    IrFunction* function = builder->function;
    BUSTER_CHECK(ir_block_id_valid(function, builder->current));
    IrBlock* block = function->blocks + builder->current.value;
    BUSTER_CHECK(!block->terminated);
    BUSTER_CHECK(function->instruction_count < function->instruction_capacity);
    IrInstructionId id = {.value = function->instruction_count};
    IrInstruction* instruction = function->instructions + function->instruction_count;
    *instruction = (IrInstruction){
        .type = type,
        .canonical_type = IR_TYPE_ID_INVALID,
        .entity = ANALYSIS_ENTITY_ID_INVALID,
        .symbol = IR_SYMBOL_ID_INVALID,
        .instantiation = ANALYSIS_INSTANTIATION_ID_INVALID,
        .local = ANALYSIS_LOCAL_ID_INVALID,
        .canonical_local = IR_LOCAL_ID_INVALID,
        .id = id,
        .next = IR_INSTRUCTION_ID_INVALID,
        .result = IR_VALUE_ID_INVALID,
        .source = source,
        .opcode = opcode,
        .conversion_operation = IR_CONVERSION_COUNT,
        .unary_operation = IR_UNARY_COUNT,
        .binary_operation = IR_BINARY_COUNT,
        .memory_order = IR_MEMORY_ORDER_COUNT,
        .failure_memory_order = IR_MEMORY_ORDER_COUNT,
        .atomic_operation = IR_ATOMIC_OPERATION_COUNT,
        .operand_count = operand_count,
    };
    if (operand_count)
    {
        instruction->operands = arena_allocate(builder->result_arena, IrValueId, operand_count);
        for (u32 index = 0; index < operand_count; index += 1)
        {
            instruction->operands[index] = operands[index];
        }
    }
    if (produces_value)
    {
        instruction->result = ir_value_create(builder, type, id, category);
    }
    if (block->last_instruction.value != IR_ID_UNDERLYING_INVALID)
    {
        function->instructions[block->last_instruction.value].next = id;
    }
    else
    {
        block->first_instruction = id;
    }
    block->last_instruction = id;
    function->instruction_count += 1;
    return instruction;
}

BUSTER_GLOBAL_LOCAL void ir_terminate(IrBuilder* builder, IrOpcode opcode, ParserSourceRange source, IrValueId* operands, u32 operand_count, IrBlockId* targets,
                                      u32 target_count)
{
    IrInstruction* instruction = ir_emit(builder, opcode, builder->analysis->types.builtin.void_type, IR_VALUE_VALUE, source, operands, operand_count, false);
    if (target_count)
    {
        instruction->targets = arena_allocate(builder->result_arena, IrBlockId, target_count);
        for (u32 index = 0; index < target_count; index += 1)
        {
            instruction->targets[index] = targets[index];
        }
        instruction->target_count = target_count;
        for (u32 index = 0; index < target_count; index += 1)
        {
            ir_predecessor_add(builder, targets[index], builder->current);
        }
    }
    builder->function->blocks[builder->current.value].terminated = true;
}

BUSTER_GLOBAL_LOCAL void ir_branch(IrBuilder* builder, IrBlockId target, ParserSourceRange source)
{
    ir_terminate(builder, IR_OPCODE_BRANCH, source, 0, 0, &target, 1);
}

BUSTER_GLOBAL_LOCAL AnalysisTypedExpression* ir_typed_expression_find(AnalysisBody* body, AstExpression ast)
{
    for (AnalysisTypedExpression* expression = body->first_expression; expression; expression = expression->next)
    {
        if (expression->ast.nodes == ast.nodes && expression->ast.count == ast.count)
        {
            return expression;
        }
    }
    return 0;
}

BUSTER_GLOBAL_LOCAL IrValueId ir_materialize(IrBuilder* builder, IrLowered lowered, ParserSourceRange source)
{
    if (lowered.category == IR_VALUE_PLACE)
    {
        IrInstruction* load = ir_emit(builder, IR_OPCODE_LOAD, lowered.type, IR_VALUE_VALUE, source, &lowered.value, 1, true);
        return load->result;
    }
    return lowered.value;
}

BUSTER_GLOBAL_LOCAL u32 ir_field_index(AnalysisResult* analysis, AnalysisTypeId aggregate_type, String8 name)
{
    AnalysisType* type = analysis_type_from_id(analysis, aggregate_type);
    if (type->kind == ANALYSIS_TYPE_STRUCT || type->kind == ANALYSIS_TYPE_UNION)
    {
        AnalysisResult* declaration_module = 0;
        if (analysis->module.id.value == type->as.declaration.module.value)
        {
            declaration_module = analysis;
        }
        else
        {
            for (u32 index = 0; index < analysis->program_module_count; index += 1)
            {
                AnalysisResult* candidate = analysis->program_modules[index];
                if (candidate && candidate->module.id.value == type->as.declaration.module.value)
                {
                    declaration_module = candidate;
                    break;
                }
            }
        }
        if (!declaration_module || type->as.declaration.index.value >= declaration_module->module.entity_count)
        {
            return UINT32_MAX;
        }
        AnalysisEntitySemantic* semantic = declaration_module->module.semantics + type->as.declaration.index.value;
        for (u32 index = 0; index < semantic->field_count; index += 1)
        {
            if (string_equal(semantic->fields[index].name, name))
            {
                return index;
            }
        }
    }
    return UINT32_MAX;
}

typedef enum IrExpressionTaskKind
{
    IR_EXPRESSION_TASK_VISIT,
    IR_EXPRESSION_TASK_EMIT,
    IR_EXPRESSION_TASK_SHORT_AFTER_LEFT,
    IR_EXPRESSION_TASK_SHORT_AFTER_RIGHT,
} IrExpressionTaskKind;

typedef struct IrExpressionTask IrExpressionTask;
struct IrExpressionTask
{
    IrBlockId merge;
    IrBlockId short_block;
    IrValueId short_value;
    u32 node_index;
    u32 right_index;
    IrExpressionTaskKind kind;
};

BUSTER_GLOBAL_LOCAL void ir_expression_child_roots(AnalysisTypedExpression* expression, u32 node_index, u32* roots, u32 arity)
{
    u32 cursor = node_index;
    for (u32 child = arity; child > 0; child -= 1)
    {
        cursor -= 1;
        roots[child - 1] = cursor;
        cursor = expression->nodes[cursor].subtree_start;
    }
}

BUSTER_GLOBAL_LOCAL AnalysisResult* ir_analysis_module_from_id(AnalysisResult* analysis, AnalysisModuleId id)
{
    if (analysis->module.id.value == id.value)
    {
        return analysis;
    }
    for (u32 index = 0; index < analysis->program_module_count; index += 1)
    {
        AnalysisResult* candidate = analysis->program_modules[index];
        if (candidate && candidate->module.id.value == id.value)
        {
            return candidate;
        }
    }
    return 0;
}

BUSTER_GLOBAL_LOCAL bool ir_call_argument_is_compile_time(IrBuilder* builder, AnalysisTypedExpression* expression, u32 callee_root, u32 argument_index)
{
    AnalysisTypedNode* callee = expression->nodes + callee_root;
    AnalysisResult* module = ir_analysis_module_from_id(builder->analysis, callee->entity.module);
    if (!module || callee->entity.index.value >= module->module.entity_count)
    {
        return false;
    }
    AnalysisEntity* entity = module->module.entities + callee->entity.index.value;
    if (entity->kind != ANALYSIS_ENTITY_CODE)
    {
        return false;
    }
    AstTypeArgument* argument = entity->ast.code->type->function.first_argument;
    for (u32 index = 0; argument && index < argument_index; index += 1)
    {
        argument = argument->next;
    }
    return argument && argument->is_compile_time;
}

BUSTER_GLOBAL_LOCAL bool ir_type_is_signed_integer(AnalysisResult* analysis, AnalysisTypeId type)
{
    AnalysisType* resolved = analysis_type_from_id(analysis, type);
    return resolved->kind == ANALYSIS_TYPE_INTEGER && resolved->as.integer.is_signed;
}

BUSTER_GLOBAL_LOCAL bool ir_type_is_integer_domain(AnalysisResult* analysis, AnalysisTypeId type)
{
    AnalysisTypeKind kind = analysis_type_from_id(analysis, type)->kind;
    return kind == ANALYSIS_TYPE_BOOL || kind == ANALYSIS_TYPE_INTEGER || kind == ANALYSIS_TYPE_ENUM;
}

BUSTER_GLOBAL_LOCAL u32 ir_type_bit_width(AnalysisResult* analysis, AnalysisTypeId type_id)
{
    AnalysisType* type = analysis_type_from_id(analysis, type_id);
    switch (type->kind)
    {
    case ANALYSIS_TYPE_BOOL:
        return 1;
    case ANALYSIS_TYPE_INTEGER:
        return type->as.integer.bit_width;
    case ANALYSIS_TYPE_FLOAT:
        return type->as.float_bit_width;
    case ANALYSIS_TYPE_ENUM:
        return type->layout.size ? (u32)(type->layout.size * 8) : 32;
    case ANALYSIS_TYPE_POINTER:
        return (u32)(type->layout.size * 8);
    default:
        break;
    }
    return 0;
}

BUSTER_GLOBAL_LOCAL IrConversionOperation ir_conversion_operation(AnalysisResult* analysis, AnalysisTypeId source_id, AnalysisTypeId target_id)
{
    if (ir_type_id_equal(source_id, target_id))
    {
        return IR_CONVERSION_IDENTITY;
    }
    AnalysisType* source = analysis_type_from_id(analysis, source_id);
    AnalysisType* target = analysis_type_from_id(analysis, target_id);
    bool source_integer = ir_type_is_integer_domain(analysis, source_id);
    bool target_integer = ir_type_is_integer_domain(analysis, target_id);
    if (source_integer && target_integer)
    {
        u32 source_width = ir_type_bit_width(analysis, source_id);
        u32 target_width = ir_type_bit_width(analysis, target_id);
        if (source_width < target_width)
        {
            return ir_type_is_signed_integer(analysis, source_id) ? IR_CONVERSION_INTEGER_SIGN_EXTEND : IR_CONVERSION_INTEGER_ZERO_EXTEND;
        }
        return source_width > target_width ? IR_CONVERSION_INTEGER_TRUNCATE : IR_CONVERSION_INTEGER_REINTERPRET;
    }
    if (source->kind == ANALYSIS_TYPE_FLOAT && target->kind == ANALYSIS_TYPE_FLOAT)
    {
        return source->as.float_bit_width < target->as.float_bit_width ? IR_CONVERSION_FLOAT_EXTEND : IR_CONVERSION_FLOAT_TRUNCATE;
    }
    if (source_integer && target->kind == ANALYSIS_TYPE_FLOAT)
    {
        return ir_type_is_signed_integer(analysis, source_id) ? IR_CONVERSION_SIGNED_INTEGER_TO_FLOAT : IR_CONVERSION_UNSIGNED_INTEGER_TO_FLOAT;
    }
    if (source->kind == ANALYSIS_TYPE_FLOAT && target_integer)
    {
        return ir_type_is_signed_integer(analysis, target_id) ? IR_CONVERSION_FLOAT_TO_SIGNED_INTEGER : IR_CONVERSION_FLOAT_TO_UNSIGNED_INTEGER;
    }
    if (source->kind == ANALYSIS_TYPE_POINTER && target->kind == ANALYSIS_TYPE_POINTER)
    {
        return IR_CONVERSION_POINTER_REINTERPRET;
    }
    if (source->kind == ANALYSIS_TYPE_POINTER && target_integer)
    {
        return IR_CONVERSION_POINTER_TO_INTEGER;
    }
    if (source_integer && target->kind == ANALYSIS_TYPE_POINTER)
    {
        return IR_CONVERSION_INTEGER_TO_POINTER;
    }
    return IR_CONVERSION_COUNT;
}

BUSTER_GLOBAL_LOCAL IrUnaryOperation ir_unary_operation(AnalysisResult* analysis, AstNodeId operation, AnalysisTypeId operand_type)
{
    AnalysisType* type = analysis_type_from_id(analysis, operand_type);
    AnalysisTypeKind kind = type->kind;
    AnalysisTypeKind element_kind = kind == ANALYSIS_TYPE_VECTOR ? analysis_type_from_id(analysis, type->as.vector.element_type)->kind : kind;
    switch (operation)
    {
    case AST_NODE_UNARY_MINUS:
    {
        return kind == ANALYSIS_TYPE_VECTOR  ? (element_kind == ANALYSIS_TYPE_FLOAT ? IR_UNARY_VECTOR_FLOAT_NEGATE : IR_UNARY_VECTOR_INTEGER_NEGATE)
               : kind == ANALYSIS_TYPE_FLOAT ? IR_UNARY_FLOAT_NEGATE
                                             : IR_UNARY_INTEGER_NEGATE;
    }
    case AST_NODE_UNARY_LOGICAL_NOT:
        return IR_UNARY_BOOLEAN_NOT;
    case AST_NODE_UNARY_BITWISE_NOT:
        return kind == ANALYSIS_TYPE_VECTOR ? IR_UNARY_VECTOR_INTEGER_BITWISE_NOT : IR_UNARY_INTEGER_BITWISE_NOT;
    default:
        break;
    }
    return IR_UNARY_COUNT;
}

BUSTER_GLOBAL_LOCAL IrBinaryOperation ir_equality_operation(AnalysisResult* analysis, AstNodeId operation, AnalysisTypeId operand_type)
{
    AnalysisTypeKind kind = analysis_type_from_id(analysis, operand_type)->kind;
    bool equal = operation == AST_NODE_BINARY_EQUAL;
    if (kind == ANALYSIS_TYPE_VECTOR)
    {
        return equal ? IR_BINARY_VECTOR_INTEGER_EQUAL : IR_BINARY_VECTOR_INTEGER_NOT_EQUAL;
    }
    switch (kind)
    {
    case ANALYSIS_TYPE_FLOAT:
        return equal ? IR_BINARY_FLOAT_EQUAL : IR_BINARY_FLOAT_NOT_EQUAL;
    case ANALYSIS_TYPE_POINTER:
        return equal ? IR_BINARY_POINTER_EQUAL : IR_BINARY_POINTER_NOT_EQUAL;
    case ANALYSIS_TYPE_BOOL:
        return equal ? IR_BINARY_BOOLEAN_EQUAL : IR_BINARY_BOOLEAN_NOT_EQUAL;
    default:
        return equal ? IR_BINARY_INTEGER_EQUAL : IR_BINARY_INTEGER_NOT_EQUAL;
    }
}

BUSTER_GLOBAL_LOCAL IrBinaryOperation ir_ordering_operation(AnalysisResult* analysis, AstNodeId operation, AnalysisTypeId operand_type)
{
    AnalysisTypeKind kind = analysis_type_from_id(analysis, operand_type)->kind;
    AnalysisTypeId scalar_type = kind == ANALYSIS_TYPE_VECTOR ? analysis_type_from_id(analysis, operand_type)->as.vector.element_type : operand_type;
    AnalysisTypeKind scalar_kind = analysis_type_from_id(analysis, scalar_type)->kind;
    IrBinaryOperation less;
    if (kind == ANALYSIS_TYPE_VECTOR)
    {
        less = scalar_kind == ANALYSIS_TYPE_FLOAT                 ? IR_BINARY_VECTOR_FLOAT_LESS
               : ir_type_is_signed_integer(analysis, scalar_type) ? IR_BINARY_VECTOR_SIGNED_LESS
                                                                  : IR_BINARY_VECTOR_UNSIGNED_LESS;
    }
    else if (kind == ANALYSIS_TYPE_FLOAT)
    {
        less = IR_BINARY_FLOAT_LESS;
    }
    else
    {
        less = ir_type_is_signed_integer(analysis, operand_type) ? IR_BINARY_SIGNED_LESS : IR_BINARY_UNSIGNED_LESS;
    }
    switch (operation)
    {
    case AST_NODE_BINARY_LESS:
        return less;
    case AST_NODE_BINARY_LESS_EQUAL:
        return (IrBinaryOperation)(less + 1);
    case AST_NODE_BINARY_GREATER:
        return (IrBinaryOperation)(less + 2);
    case AST_NODE_BINARY_GREATER_EQUAL:
        return (IrBinaryOperation)(less + 3);
    default:
        break;
    }
    return IR_BINARY_COUNT;
}

BUSTER_GLOBAL_LOCAL IrBinaryOperation ir_binary_operation(AnalysisResult* analysis, AstNodeId operation, AnalysisTypeId operand_type)
{
    AnalysisType* type = analysis_type_from_id(analysis, operand_type);
    AnalysisTypeKind kind = type->kind;
    AnalysisTypeId scalar_type = kind == ANALYSIS_TYPE_VECTOR ? type->as.vector.element_type : operand_type;
    AnalysisTypeKind scalar_kind = analysis_type_from_id(analysis, scalar_type)->kind;
    bool is_vector = kind == ANALYSIS_TYPE_VECTOR;
    bool is_float = scalar_kind == ANALYSIS_TYPE_FLOAT;
    bool is_signed = ir_type_is_signed_integer(analysis, scalar_type);
    switch (operation)
    {
    case AST_NODE_BINARY_PLUS:
        return is_vector ? (is_float ? IR_BINARY_VECTOR_FLOAT_ADD : IR_BINARY_VECTOR_INTEGER_ADD) : is_float ? IR_BINARY_FLOAT_ADD : IR_BINARY_INTEGER_ADD;
    case AST_NODE_BINARY_MINUS:
        return is_vector  ? (is_float ? IR_BINARY_VECTOR_FLOAT_SUBTRACT : IR_BINARY_VECTOR_INTEGER_SUBTRACT)
               : is_float ? IR_BINARY_FLOAT_SUBTRACT
                          : IR_BINARY_INTEGER_SUBTRACT;
    case AST_NODE_BINARY_ASTERISK:
        return is_vector  ? (is_float ? IR_BINARY_VECTOR_FLOAT_MULTIPLY : IR_BINARY_VECTOR_INTEGER_MULTIPLY)
               : is_float ? IR_BINARY_FLOAT_MULTIPLY
                          : IR_BINARY_INTEGER_MULTIPLY;
    case AST_NODE_BINARY_SLASH:
        return is_vector   ? (is_float    ? IR_BINARY_VECTOR_FLOAT_DIVIDE
                              : is_signed ? IR_BINARY_VECTOR_SIGNED_DIVIDE
                                          : IR_BINARY_VECTOR_UNSIGNED_DIVIDE)
               : is_float  ? IR_BINARY_FLOAT_DIVIDE
               : is_signed ? IR_BINARY_SIGNED_DIVIDE
                           : IR_BINARY_UNSIGNED_DIVIDE;
    case AST_NODE_BINARY_PERCENT:
        return is_vector   ? (is_signed ? IR_BINARY_VECTOR_SIGNED_REMAINDER : IR_BINARY_VECTOR_UNSIGNED_REMAINDER)
               : is_signed ? IR_BINARY_SIGNED_REMAINDER
                           : IR_BINARY_UNSIGNED_REMAINDER;
    case AST_NODE_BINARY_SHIFT_LEFT:
        return is_vector ? IR_BINARY_VECTOR_SHIFT_LEFT : IR_BINARY_SHIFT_LEFT;
    case AST_NODE_BINARY_SHIFT_RIGHT:
        return is_vector   ? (is_signed ? IR_BINARY_VECTOR_SIGNED_SHIFT_RIGHT : IR_BINARY_VECTOR_UNSIGNED_SHIFT_RIGHT)
               : is_signed ? IR_BINARY_SIGNED_SHIFT_RIGHT
                           : IR_BINARY_UNSIGNED_SHIFT_RIGHT;
    case AST_NODE_BINARY_EQUAL:
    case AST_NODE_BINARY_NOT_EQUAL:
        if (is_vector && is_float)
        {
            return operation == AST_NODE_BINARY_EQUAL ? IR_BINARY_VECTOR_FLOAT_EQUAL : IR_BINARY_VECTOR_FLOAT_NOT_EQUAL;
        }
        return ir_equality_operation(analysis, operation, operand_type);
    case AST_NODE_BINARY_LESS:
    case AST_NODE_BINARY_LESS_EQUAL:
    case AST_NODE_BINARY_GREATER:
    case AST_NODE_BINARY_GREATER_EQUAL:
        return ir_ordering_operation(analysis, operation, operand_type);
    case AST_NODE_BINARY_AMPERSAND:
        return is_vector ? IR_BINARY_VECTOR_INTEGER_BITWISE_AND : IR_BINARY_INTEGER_BITWISE_AND;
    case AST_NODE_BINARY_BAR:
        return is_vector ? IR_BINARY_VECTOR_INTEGER_BITWISE_OR : IR_BINARY_INTEGER_BITWISE_OR;
    case AST_NODE_BINARY_CARET:
        return is_vector ? IR_BINARY_VECTOR_INTEGER_BITWISE_XOR : IR_BINARY_INTEGER_BITWISE_XOR;
    case AST_NODE_BINARY_BOOLEAN_AND:
        return IR_BINARY_BOOLEAN_AND;
    case AST_NODE_BINARY_BOOLEAN_OR:
        return IR_BINARY_BOOLEAN_OR;
    case AST_NODE_BINARY_RANGE:
        return IR_BINARY_RANGE;
    default:
        break;
    }
    return IR_BINARY_COUNT;
}

BUSTER_GLOBAL_LOCAL IrLowered ir_lower_expression(IrBuilder* builder, AstExpression ast)
{
    IrLowered invalid = {
        .value = IR_VALUE_ID_INVALID,
        .type = builder->analysis->types.builtin.poison,
        .local = ANALYSIS_LOCAL_ID_INVALID,
        .category = IR_VALUE_VALUE,
    };
    if (!ast.count)
    {
        return invalid;
    }
    AnalysisTypedExpression* expression = ir_typed_expression_find(builder->body, ast);
    BUSTER_CHECK(expression);
    IrLowered* results = arena_allocate(builder->scratch_arena, IrLowered, ast.count);
    IrExpressionTask* tasks = arena_allocate(builder->scratch_arena, IrExpressionTask, ast.count * 3 + 1);
    u32 task_count = 1;
    tasks[0] = (IrExpressionTask){
        .node_index = ast.count - 1,
        .kind = IR_EXPRESSION_TASK_VISIT,
    };
    while (task_count)
    {
        IrExpressionTask task = tasks[task_count - 1];
        task_count -= 1;
        u32 node_index = task.node_index;
        AstNode* node = ast.nodes + node_index;
        AnalysisTypedNode* typed = expression->nodes + node_index;
        u32 arity = ir_node_arity(node);
        u32* roots = arena_allocate(builder->scratch_arena, u32, arity);
        ir_expression_child_roots(expression, node_index, roots, arity);
        bool short_circuit = node->id == AST_NODE_BINARY_BOOLEAN_AND_SHORT_CIRCUIT || node->id == AST_NODE_BINARY_BOOLEAN_OR_SHORT_CIRCUIT;
        if (task.kind == IR_EXPRESSION_TASK_VISIT)
        {
            BUSTER_CHECK(task_count + arity + 1 <= ast.count * 3 + 1);
            if (short_circuit)
            {
                BUSTER_CHECK(arity == 2);
                tasks[task_count++] = (IrExpressionTask){
                    .node_index = node_index,
                    .right_index = roots[1],
                    .kind = IR_EXPRESSION_TASK_SHORT_AFTER_LEFT,
                };
                tasks[task_count++] = (IrExpressionTask){
                    .node_index = roots[0],
                    .kind = IR_EXPRESSION_TASK_VISIT,
                };
            }
            else
            {
                tasks[task_count++] = (IrExpressionTask){
                    .node_index = node_index,
                    .kind = IR_EXPRESSION_TASK_EMIT,
                };
                for (u32 child = arity; child > 0; child -= 1)
                {
                    if (node->id == AST_NODE_CALL && typed->instantiation.value != ANALYSIS_ID_UNDERLYING_INVALID && child > 1 &&
                        ir_call_argument_is_compile_time(builder, expression, roots[0], child - 2))
                    {
                        continue;
                    }
                    tasks[task_count++] = (IrExpressionTask){
                        .node_index = roots[child - 1],
                        .kind = IR_EXPRESSION_TASK_VISIT,
                    };
                }
            }
            continue;
        }
        if (task.kind == IR_EXPRESSION_TASK_SHORT_AFTER_LEFT)
        {
            IrValueId condition = ir_materialize(builder, results[roots[0]], builder->entity->range);
            IrBlockId right_block = ir_block_create(builder);
            IrBlockId short_block = ir_block_create(builder);
            IrBlockId merge = ir_block_create(builder);
            IrBlockId targets[2] = {right_block, short_block};
            bool is_or = node->id == AST_NODE_BINARY_BOOLEAN_OR_SHORT_CIRCUIT;
            if (is_or)
            {
                targets[0] = short_block;
                targets[1] = right_block;
            }
            ir_terminate(builder, IR_OPCODE_BRANCH_IF, builder->entity->range, &condition, 1, targets, 2);
            builder->current = short_block;
            ir_block_seal(builder, short_block);
            IrInstruction* constant = ir_emit(builder, IR_OPCODE_CONSTANT_INTEGER, typed->type, IR_VALUE_VALUE, builder->entity->range, 0, 0, true);
            constant->immediates = arena_allocate(builder->result_arena, u64, 1);
            constant->immediates[0] = is_or;
            constant->immediate_count = 1;
            IrBlockId short_predecessor = builder->current;
            ir_branch(builder, merge, builder->entity->range);
            builder->current = right_block;
            ir_block_seal(builder, right_block);
            tasks[task_count++] = (IrExpressionTask){
                .merge = merge,
                .short_block = short_predecessor,
                .short_value = constant->result,
                .node_index = node_index,
                .right_index = task.right_index,
                .kind = IR_EXPRESSION_TASK_SHORT_AFTER_RIGHT,
            };
            tasks[task_count++] = (IrExpressionTask){
                .node_index = task.right_index,
                .kind = IR_EXPRESSION_TASK_VISIT,
            };
            continue;
        }
        if (task.kind == IR_EXPRESSION_TASK_SHORT_AFTER_RIGHT)
        {
            IrValueId right = ir_materialize(builder, results[task.right_index], builder->entity->range);
            IrBlockId right_predecessor = builder->current;
            ir_branch(builder, task.merge, builder->entity->range);
            ir_block_seal(builder, task.merge);
            IrBlockParameter* parameter = ir_block_parameter_create(builder, task.merge, ANALYSIS_LOCAL_ID_INVALID, typed->type);
            ir_block_parameter_incoming_add(builder, parameter, task.short_block, task.short_value);
            ir_block_parameter_incoming_add(builder, parameter, right_predecessor, right);
            results[node_index] = (IrLowered){
                .value = parameter->value,
                .type = typed->type,
                .local = ANALYSIS_LOCAL_ID_INVALID,
                .category = IR_VALUE_VALUE,
            };
            builder->current = task.merge;
            continue;
        }
        IrLowered lowered = invalid;
        lowered.type = typed->type;
        lowered.local = typed->local;
        bool semantic_place = typed->category == ANALYSIS_VALUE_CATEGORY_IMMUTABLE_PLACE || typed->category == ANALYSIS_VALUE_CATEGORY_MUTABLE_PLACE;
        lowered.category = semantic_place ? IR_VALUE_PLACE : IR_VALUE_VALUE;
        IrInstruction* instruction = 0;
        ParserSourceRange source = builder->entity->range;
        switch (node->id)
        {
        case AST_NODE_CONSTANT_INTEGER:
        case AST_NODE_CONSTANT_CHARACTER:
        {
            instruction = ir_emit(builder, IR_OPCODE_CONSTANT_INTEGER, typed->type, IR_VALUE_VALUE, source, 0, 0, true);
            instruction->immediates = arena_allocate(builder->result_arena, u64, 1);
            instruction->immediates[0] = typed->constant.integer;
            instruction->immediate_count = 1;
            instruction->immediate_is_negative = typed->constant.is_negative;
        }
        break;
        case AST_NODE_CONSTANT_FLOAT:
        {
            instruction = ir_emit(builder, IR_OPCODE_CONSTANT_FLOAT, typed->type, IR_VALUE_VALUE, source, 0, 0, true);
            instruction->literal = node->floating.spelling;
            instruction->immediates = arena_allocate(builder->result_arena, u64, 1);
            AnalysisType* float_type = analysis_type_from_id(builder->analysis, typed->type);
            BUSTER_CHECK(typed->constant.kind == ANALYSIS_CONSTANT_FLOAT && float_type->kind == ANALYSIS_TYPE_FLOAT);
            if (float_type->as.float_bit_width == 32)
            {
                f32 value = (f32)typed->constant.floating;
                u32 bits = 0;
                memcpy(&bits, &value, sizeof(bits));
                instruction->immediates[0] = bits;
            }
            else
            {
                f64 value = typed->constant.floating;
                BUSTER_CHECK(float_type->as.float_bit_width == 64);
                memcpy(instruction->immediates, &value, sizeof(value));
            }
            instruction->immediate_count = 1;
        }
        break;
        case AST_NODE_CONSTANT_STRING:
        {
            instruction = ir_emit(builder, IR_OPCODE_CONSTANT_STRING, typed->type, IR_VALUE_VALUE, source, 0, 0, true);
            instruction->literal = node->string.value;
        }
        break;
        case AST_NODE_IDENTIFIER:
        {
            if (typed->is_namespace)
            {
                lowered.value = IR_VALUE_ID_INVALID;
                lowered.category = IR_VALUE_VALUE;
            }
            else if (typed->local.value != ANALYSIS_ID_UNDERLYING_INVALID && builder->body->locals[typed->local.value].is_compile_time)
            {
                BUSTER_CHECK(typed->constant.kind != ANALYSIS_CONSTANT_NONE);
                instruction = ir_emit(builder, IR_OPCODE_CONSTANT_INTEGER, typed->type, IR_VALUE_VALUE, source, 0, 0, true);
                instruction->immediates = arena_allocate(builder->result_arena, u64, 1);
                instruction->immediates[0] = typed->constant.integer;
                instruction->immediate_count = 1;
                instruction->immediate_is_negative = typed->constant.is_negative;
            }
            else if (typed->local.value != ANALYSIS_ID_UNDERLYING_INVALID)
            {
                if (builder->function->local_uses_memory[typed->local.value])
                {
                    lowered.value = builder->function->local_places[typed->local.value];
                    lowered.category = IR_VALUE_PLACE;
                }
                else
                {
                    lowered.value = ir_ssa_read(builder, builder->current, typed->local);
                    BUSTER_CHECK(lowered.value.value != IR_ID_UNDERLYING_INVALID);
                    lowered.category = IR_VALUE_VALUE;
                }
            }
            else if (typed->constant.kind != ANALYSIS_CONSTANT_NONE)
            {
                instruction = ir_emit(builder, IR_OPCODE_CONSTANT_INTEGER, typed->type, IR_VALUE_VALUE, source, 0, 0, true);
                instruction->immediates = arena_allocate(builder->result_arena, u64, 1);
                instruction->immediates[0] = typed->constant.integer;
                instruction->immediate_count = 1;
            }
            else
            {
                instruction = ir_emit(builder, IR_OPCODE_FUNCTION, typed->type, IR_VALUE_VALUE, source, 0, 0, true);
                instruction->entity = typed->entity;
            }
        }
        break;
        case AST_NODE_UNDEFINED:
        {
            instruction = ir_emit(builder, IR_OPCODE_UNDEFINED, typed->type, IR_VALUE_VALUE, source, 0, 0, true);
        }
        break;
        case AST_NODE_ENUM_LITERAL:
        {
            instruction = ir_emit(builder, IR_OPCODE_ENUM, typed->type, IR_VALUE_VALUE, node->enum_literal.range, 0, 0, true);
            instruction->immediates = arena_allocate(builder->result_arena, u64, 1);
            instruction->immediates[0] = typed->constant.integer;
            instruction->immediate_count = 1;
        }
        break;
        case AST_NODE_ARRAY_LITERAL:
        case AST_NODE_AGGREGATE_LITERAL:
        {
            IrValueId* operands = arena_allocate(builder->scratch_arena, IrValueId, arity);
            for (u32 index = 0; index < arity; index += 1)
            {
                operands[index] = ir_materialize(builder, results[roots[index]], source);
            }
            instruction = ir_emit(builder, node->id == AST_NODE_ARRAY_LITERAL ? IR_OPCODE_ARRAY : IR_OPCODE_AGGREGATE, typed->type, IR_VALUE_VALUE, source,
                                  operands, arity, true);
            if (node->id == AST_NODE_AGGREGATE_LITERAL && arity)
            {
                instruction->immediates = arena_allocate(builder->result_arena, u64, arity);
                u32 field_index = 0;
                for (AstAggregateLiteralField* field = node->aggregate_literal.first_field; field; field = field->next)
                {
                    BUSTER_CHECK(field_index < arity);
                    instruction->immediates[field_index] = ir_field_index(builder->analysis, typed->type, field->name.text);
                    field_index += 1;
                }
                BUSTER_CHECK(field_index == arity);
                instruction->immediate_count = arity;
            }
        }
        break;
        case AST_NODE_ARRAY_INDEX:
        {
            IrValueId operands[2] = {
                results[roots[0]].value,
                ir_materialize(builder, results[roots[1]], source),
            };
            instruction = ir_emit(builder, IR_OPCODE_INDEX, typed->type, lowered.category, node->array_index.range, operands, 2, true);
        }
        break;
        case AST_NODE_ARRAY_SLICE:
        {
            IrValueId* operands = arena_allocate(builder->scratch_arena, IrValueId, arity);
            operands[0] = results[roots[0]].value;
            for (u32 index = 1; index < arity; index += 1)
            {
                operands[index] = ir_materialize(builder, results[roots[index]], source);
            }
            instruction = ir_emit(builder, IR_OPCODE_SLICE, typed->type, IR_VALUE_VALUE, node->array_slice.range, operands, arity, true);
            instruction->immediates = arena_allocate(builder->result_arena, u64, 2);
            instruction->immediates[0] = node->array_slice.has_start;
            instruction->immediates[1] = node->array_slice.has_end;
            instruction->immediate_count = 2;
        }
        break;
        case AST_NODE_MEMBER_ACCESS:
        {
            AnalysisTypedNode* base_typed = expression->nodes + roots[0];
            if (base_typed->is_namespace && typed->constant.kind != ANALYSIS_CONSTANT_NONE)
            {
                instruction = ir_emit(builder, IR_OPCODE_CONSTANT_INTEGER, typed->type, IR_VALUE_VALUE, node->member_access.range, 0, 0, true);
                instruction->immediates = arena_allocate(builder->result_arena, u64, 1);
                instruction->immediates[0] = typed->constant.integer;
                instruction->immediate_count = 1;
                instruction->immediate_is_negative = typed->constant.is_negative;
            }
            else if (base_typed->is_namespace)
            {
                instruction = ir_emit(builder, IR_OPCODE_FUNCTION, typed->type, IR_VALUE_VALUE, node->member_access.range, 0, 0, true);
                instruction->entity = typed->entity;
            }
            else
            {
                IrValueId operand = results[roots[0]].value;
                instruction = ir_emit(builder, IR_OPCODE_FIELD, typed->type, lowered.category, node->member_access.range, &operand, 1, true);
                instruction->immediates = arena_allocate(builder->result_arena, u64, 1);
                instruction->immediates[0] = ir_field_index(builder->analysis, results[roots[0]].type, node->member_access.member.text);
                instruction->immediate_count = 1;
            }
        }
        break;
        case AST_NODE_CALL:
        {
            IrValueId* operands = arena_allocate(builder->scratch_arena, IrValueId, arity);
            u32 operand_count = 0;
            operands[operand_count++] = ir_materialize(builder, results[roots[0]], source);
            AnalysisTypedNode* callee_typed = expression->nodes + roots[0];
            AnalysisResult* callee_module = 0;
            AnalysisEntity* callee_entity = 0;
            if (typed->instantiation.value != ANALYSIS_ID_UNDERLYING_INVALID)
            {
                callee_module = builder->analysis;
                if (callee_typed->entity.module.value != builder->analysis->module.id.value)
                {
                    for (u32 module_index = 0; module_index < builder->analysis->program_module_count; module_index += 1)
                    {
                        AnalysisResult* candidate = builder->analysis->program_modules[module_index];
                        if (candidate && candidate->module.id.value == callee_typed->entity.module.value)
                        {
                            callee_module = candidate;
                            break;
                        }
                    }
                }
                if (callee_module && callee_typed->entity.index.value < callee_module->module.entity_count)
                {
                    callee_entity = callee_module->module.entities + callee_typed->entity.index.value;
                }
            }
            AstTypeArgument* argument = callee_entity ? callee_entity->ast.code->type->function.first_argument : 0;
            AnalysisType* callee_signature = analysis_type_from_id(builder->analysis, callee_typed->type);
            for (u32 index = 1; index < arity; index += 1)
            {
                bool compile_time = argument && argument->is_compile_time;
                if (!compile_time)
                {
                    IrValueId value = ir_materialize(builder, results[roots[index]], source);
                    u32 source_argument = index - 1;
                    if (callee_signature->kind == ANALYSIS_TYPE_FUNCTION && callee_signature->as.function.is_variadic &&
                        source_argument >= callee_signature->as.function.argument_count)
                    {
                        AnalysisTypeId source_type = builder->function->values[value.value].type;
                        AnalysisType* source_value_type = analysis_type_from_id(builder->analysis, source_type);
                        AnalysisTypeId promoted = source_type;
                        if (source_value_type->kind == ANALYSIS_TYPE_FLOAT && source_value_type->as.float_bit_width == 32)
                        {
                            promoted = builder->analysis->types.builtin.f64_type;
                        }
                        else if (source_value_type->kind == ANALYSIS_TYPE_BOOL ||
                                 (source_value_type->kind == ANALYSIS_TYPE_INTEGER && source_value_type->as.integer.bit_width < 32))
                        {
                            promoted = builder->analysis->types.builtin.s32_type;
                        }
                        if (!ir_type_id_equal(source_type, promoted))
                        {
                            IrInstruction* conversion = ir_emit(builder, IR_OPCODE_CAST, promoted, IR_VALUE_VALUE, source, &value, 1, true);
                            conversion->conversion_operation = ir_conversion_operation(builder->analysis, source_type, promoted);
                            value = conversion->result;
                        }
                    }
                    operands[operand_count++] = value;
                }
                if (argument)
                {
                    argument = argument->next;
                }
            }
            instruction = ir_emit(builder, IR_OPCODE_CALL, typed->type, IR_VALUE_VALUE, node->call.range, operands, operand_count,
                                  analysis_type_from_id(builder->analysis, typed->type)->kind != ANALYSIS_TYPE_VOID);
            instruction->entity = callee_typed->entity;
            instruction->instantiation = typed->instantiation;
            IrValue* callee_value = builder->function->values + operands[0].value;
            if (callee_value->definition.value != IR_ID_UNDERLYING_INVALID)
            {
                IrInstruction* reference = builder->function->instructions + callee_value->definition.value;
                if (reference->opcode == IR_OPCODE_FUNCTION)
                {
                    reference->instantiation = typed->instantiation;
                }
            }
        }
        break;
        case AST_NODE_INTRINSIC_CALL:
        {
            IrValueId* operands = arena_allocate(builder->scratch_arena, IrValueId, arity);
            for (u32 index = 0; index < arity; index += 1)
            {
                operands[index] = ir_materialize(builder, results[roots[index]], source);
            }
            IrOpcode opcode = IR_OPCODE_REVERSE;
            if (string_equal(node->intrinsic_call.name.text, S8("cast")))
            {
                opcode = IR_OPCODE_CAST;
            }
            else if (string_equal(node->intrinsic_call.name.text, S8("va_start")))
            {
                opcode = IR_OPCODE_VA_START;
            }
            else if (string_equal(node->intrinsic_call.name.text, S8("va_copy")))
            {
                opcode = IR_OPCODE_VA_COPY;
            }
            else if (string_equal(node->intrinsic_call.name.text, S8("va_end")))
            {
                opcode = IR_OPCODE_VA_END;
            }
            else if (string_equal(node->intrinsic_call.name.text, S8("va_arg")))
            {
                opcode = IR_OPCODE_VA_ARG;
            }
            instruction = ir_emit(builder, opcode, typed->type, IR_VALUE_VALUE, node->intrinsic_call.range, operands, arity,
                                  analysis_type_from_id(builder->analysis, typed->type)->kind != ANALYSIS_TYPE_VOID);
            if (opcode == IR_OPCODE_CAST)
            {
                BUSTER_CHECK(arity == 1);
                instruction->conversion_operation = ir_conversion_operation(builder->analysis, expression->nodes[roots[0]].type, typed->type);
            }
        }
        break;
        case AST_NODE_ADDRESS_OF:
        {
            IrValueId operand = results[roots[0]].value;
            instruction = ir_emit(builder, IR_OPCODE_ADDRESS_OF, typed->type, IR_VALUE_VALUE, node->pointer_operator.range, &operand, 1, true);
        }
        break;
        case AST_NODE_DEREFERENCE:
        {
            IrValueId operand = ir_materialize(builder, results[roots[0]], source);
            instruction = ir_emit(builder, IR_OPCODE_DEREFERENCE, typed->type, IR_VALUE_PLACE, node->pointer_operator.range, &operand, 1, true);
        }
        break;
        case AST_NODE_UNARY_MINUS:
        case AST_NODE_UNARY_LOGICAL_NOT:
        case AST_NODE_UNARY_BITWISE_NOT:
        {
            IrValueId operand = ir_materialize(builder, results[roots[0]], source);
            instruction = ir_emit(builder, IR_OPCODE_UNARY, typed->type, IR_VALUE_VALUE, source, &operand, 1, true);
            instruction->unary_operation = ir_unary_operation(builder->analysis, node->id, expression->nodes[roots[0]].type);
        }
        break;
        case AST_NODE_UNARY_PLUS:
        {
            lowered = results[roots[0]];
        }
        break;
        case AST_NODE_BINARY_PLUS:
        case AST_NODE_BINARY_MINUS:
        case AST_NODE_BINARY_ASTERISK:
        case AST_NODE_BINARY_SLASH:
        case AST_NODE_BINARY_PERCENT:
        case AST_NODE_BINARY_SHIFT_LEFT:
        case AST_NODE_BINARY_SHIFT_RIGHT:
        case AST_NODE_BINARY_EQUAL:
        case AST_NODE_BINARY_NOT_EQUAL:
        case AST_NODE_BINARY_LESS:
        case AST_NODE_BINARY_LESS_EQUAL:
        case AST_NODE_BINARY_GREATER:
        case AST_NODE_BINARY_GREATER_EQUAL:
        case AST_NODE_BINARY_AMPERSAND:
        case AST_NODE_BINARY_BAR:
        case AST_NODE_BINARY_CARET:
        case AST_NODE_BINARY_BOOLEAN_AND:
        case AST_NODE_BINARY_BOOLEAN_OR:
        case AST_NODE_BINARY_BOOLEAN_AND_SHORT_CIRCUIT:
        case AST_NODE_BINARY_BOOLEAN_OR_SHORT_CIRCUIT:
        case AST_NODE_BINARY_RANGE:
        {
            IrValueId operands[2] = {
                ir_materialize(builder, results[roots[0]], source),
                ir_materialize(builder, results[roots[1]], source),
            };
            instruction = ir_emit(builder, IR_OPCODE_BINARY, typed->type, IR_VALUE_VALUE, source, operands, 2, true);
            instruction->binary_operation = ir_binary_operation(builder->analysis, node->id, expression->nodes[roots[0]].type);
        }
        break;
        case AST_NODE_COUNT:
            break;
        }
        if (instruction)
        {
            lowered.value = instruction->result;
            lowered.category =
                instruction->result.value == IR_ID_UNDERLYING_INVALID ? IR_VALUE_VALUE : builder->function->values[instruction->result.value].category;
        }
        results[node_index] = lowered;
    }
    return results[ast.count - 1];
}

BUSTER_GLOBAL_LOCAL AnalysisLocal* ir_local_find(AnalysisBody* body, AstIdentifier identifier)
{
    for (u32 index = 0; index < body->local_count; index += 1)
    {
        AnalysisLocal* local = body->locals + index;
        if (local->range.offset == identifier.range.offset && string_equal(local->name, identifier.text))
        {
            return local;
        }
    }
    return 0;
}

BUSTER_GLOBAL_LOCAL void ir_task_push(Arena* arena, IrLowerTask** top, AstStatement* statement, IrBlockId block, IrBlockId end, IrBlockId break_block,
                                      IrBlockId continue_block)
{
    IrLowerTask* task = arena_allocate(arena, IrLowerTask, 1);
    *task = (IrLowerTask){
        .previous = *top,
        .statement = statement,
        .block = block,
        .end = end,
        .break_block = break_block,
        .continue_block = continue_block,
        .kind = IR_LOWER_TASK_STATEMENT,
    };
    *top = task;
}

BUSTER_GLOBAL_LOCAL void ir_seal_task_push(Arena* arena, IrLowerTask** top, IrBlockId block)
{
    IrLowerTask* task = arena_allocate(arena, IrLowerTask, 1);
    *task = (IrLowerTask){
        .previous = *top,
        .block = block,
        .kind = IR_LOWER_TASK_SEAL_BLOCK,
    };
    *top = task;
}

BUSTER_GLOBAL_LOCAL AstNodeId ir_assignment_ast_operation(AstAssignmentOperator operation)
{
    switch (operation)
    {
    case AST_ASSIGNMENT_PLUS_EQUAL:
        return AST_NODE_BINARY_PLUS;
    case AST_ASSIGNMENT_MINUS_EQUAL:
        return AST_NODE_BINARY_MINUS;
    case AST_ASSIGNMENT_MULTIPLY_EQUAL:
        return AST_NODE_BINARY_ASTERISK;
    case AST_ASSIGNMENT_DIVIDE_EQUAL:
        return AST_NODE_BINARY_SLASH;
    case AST_ASSIGNMENT_MODULO_EQUAL:
        return AST_NODE_BINARY_PERCENT;
    case AST_ASSIGNMENT_SHIFT_LEFT_EQUAL:
        return AST_NODE_BINARY_SHIFT_LEFT;
    case AST_ASSIGNMENT_SHIFT_RIGHT_EQUAL:
        return AST_NODE_BINARY_SHIFT_RIGHT;
    case AST_ASSIGNMENT_BITWISE_AND_EQUAL:
        return AST_NODE_BINARY_AMPERSAND;
    case AST_ASSIGNMENT_BITWISE_OR_EQUAL:
        return AST_NODE_BINARY_BAR;
    case AST_ASSIGNMENT_BITWISE_XOR_EQUAL:
        return AST_NODE_BINARY_CARET;
    case AST_ASSIGNMENT_EQUAL:
    case AST_ASSIGNMENT_COUNT:
        break;
    }
    return AST_NODE_COUNT;
}

BUSTER_GLOBAL_LOCAL void ir_lower_statement_task(IrBuilder* builder, IrLowerTask** top, IrLowerTask* task)
{
    if (task->kind == IR_LOWER_TASK_SEAL_BLOCK)
    {
        ir_block_seal(builder, task->block);
        return;
    }
    ir_block_seal(builder, task->block);
    builder->current = task->block;
    if (!task->statement)
    {
        if (!builder->function->blocks[task->block.value].terminated)
        {
            ir_branch(builder, task->end, builder->entity->range);
        }
        return;
    }
    AstStatement* statement = task->statement;
    switch (statement->id)
    {
    case AST_STATEMENT_RETURN:
    {
        IrValueId operand = IR_VALUE_ID_INVALID;
        u32 operand_count = 0;
        if (statement->return_statement.expression.count)
        {
            IrLowered lowered = ir_lower_expression(builder, statement->return_statement.expression);
            operand = ir_materialize(builder, lowered, statement->range);
            operand_count = 1;
        }
        ir_terminate(builder, IR_OPCODE_RETURN, statement->range, operand_count ? &operand : 0, operand_count, 0, 0);
    }
    break;
    case AST_STATEMENT_DATA:
    {
        AstDataStatement* data = &statement->data_statement;
        AnalysisLocal* local = ir_local_find(builder->body, data->name);
        BUSTER_CHECK(local);
        IrLowered initializer = ir_lower_expression(builder, data->initializer);
        IrValueId operands[2] = {
            builder->function->local_places[local->id.value],
            ir_materialize(builder, initializer, statement->range),
        };
        if (builder->function->local_uses_memory[local->id.value])
        {
            ir_emit(builder, IR_OPCODE_STORE, builder->analysis->types.builtin.void_type, IR_VALUE_VALUE, statement->range, operands, 2, false);
        }
        else
        {
            ir_ssa_write(builder, task->block, local->id, operands[1]);
        }
        ir_task_push(builder->scratch_arena, top, statement->next, task->block, task->end, task->break_block, task->continue_block);
    }
    break;
    case AST_STATEMENT_EXPRESSION:
    {
        IrLowered expression = ir_lower_expression(builder, statement->expression_statement.expression);
        if (expression.value.value != IR_ID_UNDERLYING_INVALID && expression.category == IR_VALUE_PLACE)
        {
            ir_materialize(builder, expression, statement->range);
        }
        ir_task_push(builder->scratch_arena, top, statement->next, task->block, task->end, task->break_block, task->continue_block);
    }
    break;
    case AST_STATEMENT_ASSIGNMENT:
    {
        AstAssignmentStatement* assignment = &statement->assignment_statement;
        IrLowered target = ir_lower_expression(builder, assignment->target);
        IrLowered value = ir_lower_expression(builder, assignment->value);
        IrValueId stored = ir_materialize(builder, value, statement->range);
        if (assignment->operator!= AST_ASSIGNMENT_EQUAL)
        {
            IrValueId operands[2] = {
                ir_materialize(builder, target, statement->range),
                stored,
            };
            IrInstruction* binary = ir_emit(builder, IR_OPCODE_BINARY, target.type, IR_VALUE_VALUE, statement->range, operands, 2, true);
            binary->binary_operation = ir_binary_operation(builder->analysis, ir_assignment_ast_operation(assignment->operator), target.type);
            stored = binary->result;
        }
        if (target.local.value != ANALYSIS_ID_UNDERLYING_INVALID && !builder->function->local_uses_memory[target.local.value])
        {
            ir_ssa_write(builder, task->block, target.local, stored);
        }
        else
        {
            IrValueId operands[2] = {target.value, stored};
            ir_emit(builder, IR_OPCODE_STORE, builder->analysis->types.builtin.void_type, IR_VALUE_VALUE, statement->range, operands, 2, false);
        }
        ir_task_push(builder->scratch_arena, top, statement->next, task->block, task->end, task->break_block, task->continue_block);
    }
    break;
    case AST_STATEMENT_IF:
    {
        AstIfStatement* conditional = &statement->if_statement;
        IrLowered condition = ir_lower_expression(builder, conditional->condition);
        IrValueId condition_value = ir_materialize(builder, condition, statement->range);
        IrBlockId then_block = ir_block_create(builder);
        IrBlockId else_block = ir_block_create(builder);
        IrBlockId merge = ir_block_create(builder);
        IrBlockId targets[2] = {then_block, else_block};
        ir_terminate(builder, IR_OPCODE_BRANCH_IF, statement->range, &condition_value, 1, targets, 2);
        ir_task_push(builder->scratch_arena, top, statement->next, merge, task->end, task->break_block, task->continue_block);
        if (conditional->alternative == AST_IF_ALTERNATIVE_IF)
        {
            ir_task_push(builder->scratch_arena, top, conditional->else_if, else_block, merge, task->break_block, task->continue_block);
        }
        else
        {
            AstStatement* alternative = conditional->alternative == AST_IF_ALTERNATIVE_BLOCK ? conditional->else_block.first_statement : 0;
            ir_task_push(builder->scratch_arena, top, alternative, else_block, merge, task->break_block, task->continue_block);
        }
        ir_task_push(builder->scratch_arena, top, conditional->then_block.first_statement, then_block, merge, task->break_block, task->continue_block);
    }
    break;
    case AST_STATEMENT_SWITCH:
    {
        AstSwitchStatement* switch_statement = &statement->switch_statement;
        IrLowered switched = ir_lower_expression(builder, switch_statement->expression);
        IrValueId switched_value = ir_materialize(builder, switched, statement->range);
        IrBlockId merge = ir_block_create(builder);
        u32 non_else_count = switch_statement->case_count - (switch_statement->else_case ? 1u : 0u);
        IrBlockId* targets = arena_allocate(builder->scratch_arena, IrBlockId, non_else_count + 1);
        u64* values = arena_allocate(builder->scratch_arena, u64, non_else_count);
        AstSwitchCase** cases = arena_allocate(builder->scratch_arena, AstSwitchCase*, switch_statement->case_count);
        IrBlockId* case_blocks = arena_allocate(builder->scratch_arena, IrBlockId, switch_statement->case_count);
        u32 value_index = 0;
        u32 case_index = 0;
        IrBlockId default_block = merge;
        for (AstSwitchCase* switch_case = switch_statement->first_case; switch_case; switch_case = switch_case->next)
        {
            IrBlockId case_block = ir_block_create(builder);
            cases[case_index] = switch_case;
            case_blocks[case_index] = case_block;
            case_index += 1;
            if (switch_case->is_else)
            {
                default_block = case_block;
            }
            else
            {
                AnalysisTypedExpression* case_expression = ir_typed_expression_find(builder->body, switch_case->expression);
                BUSTER_CHECK(case_expression && case_expression->ast.count);
                AnalysisTypedNode* root = case_expression->nodes + case_expression->ast.count - 1;
                targets[value_index] = case_block;
                values[value_index] = root->constant.integer;
                value_index += 1;
            }
        }
        BUSTER_CHECK(value_index == non_else_count);
        targets[non_else_count] = default_block;
        IrInstruction* switch_ir =
            ir_emit(builder, IR_OPCODE_SWITCH, builder->analysis->types.builtin.void_type, IR_VALUE_VALUE, statement->range, &switched_value, 1, false);
        switch_ir->targets = arena_allocate(builder->result_arena, IrBlockId, non_else_count + 1);
        switch_ir->immediates = arena_allocate(builder->result_arena, u64, non_else_count);
        for (u32 index = 0; index < non_else_count; index += 1)
        {
            switch_ir->targets[index] = targets[index];
            switch_ir->immediates[index] = values[index];
        }
        switch_ir->targets[non_else_count] = targets[non_else_count];
        switch_ir->target_count = non_else_count + 1;
        switch_ir->immediate_count = non_else_count;
        for (u32 index = 0; index < switch_ir->target_count; index += 1)
        {
            ir_predecessor_add(builder, switch_ir->targets[index], builder->current);
        }
        builder->function->blocks[builder->current.value].terminated = true;
        ir_task_push(builder->scratch_arena, top, statement->next, merge, task->end, task->break_block, task->continue_block);
        for (u32 index = case_index; index > 0; index -= 1)
        {
            ir_task_push(builder->scratch_arena, top, cases[index - 1]->body.first_statement, case_blocks[index - 1], merge, task->break_block,
                         task->continue_block);
        }
    }
    break;
    case AST_STATEMENT_FOR:
    {
        AstForStatement* for_statement = &statement->for_statement;
        IrLowered iterable = ir_lower_expression(builder, for_statement->iterable);
        IrValueId iterable_value = ir_materialize(builder, iterable, statement->range);
        IrInstruction* length =
            ir_emit(builder, IR_OPCODE_LENGTH, builder->analysis->types.builtin.u64_type, IR_VALUE_VALUE, statement->range, &iterable_value, 1, true);
        IrInstruction* zero =
            ir_emit(builder, IR_OPCODE_CONSTANT_INTEGER, builder->analysis->types.builtin.u64_type, IR_VALUE_VALUE, statement->range, 0, 0, true);
        zero->immediates = arena_allocate(builder->result_arena, u64, 1);
        zero->immediates[0] = 0;
        zero->immediate_count = 1;
        IrBlockId initial = builder->current;
        IrBlockId header = ir_block_create(builder);
        IrBlockId body_block = ir_block_create(builder);
        IrBlockId latch = ir_block_create(builder);
        IrBlockId exit = ir_block_create(builder);
        ir_branch(builder, header, statement->range);
        builder->current = header;
        IrBlockParameter* index = ir_block_parameter_create(builder, header, ANALYSIS_LOCAL_ID_INVALID, builder->analysis->types.builtin.u64_type);
        ir_block_parameter_incoming_add(builder, index, initial, zero->result);
        IrValueId comparison_operands[2] = {
            index->value,
            length->result,
        };
        IrInstruction* condition =
            ir_emit(builder, IR_OPCODE_BINARY, builder->analysis->types.builtin.bool_type, IR_VALUE_VALUE, statement->range, comparison_operands, 2, true);
        condition->binary_operation = IR_BINARY_UNSIGNED_LESS;
        IrBlockId targets[2] = {body_block, exit};
        ir_terminate(builder, IR_OPCODE_BRANCH_IF, statement->range, &condition->result, 1, targets, 2);
        builder->current = latch;
        IrInstruction* one =
            ir_emit(builder, IR_OPCODE_CONSTANT_INTEGER, builder->analysis->types.builtin.u64_type, IR_VALUE_VALUE, statement->range, 0, 0, true);
        one->immediates = arena_allocate(builder->result_arena, u64, 1);
        one->immediates[0] = 1;
        one->immediate_count = 1;
        IrValueId increment_operands[2] = {
            index->value,
            one->result,
        };
        IrInstruction* increment =
            ir_emit(builder, IR_OPCODE_BINARY, builder->analysis->types.builtin.u64_type, IR_VALUE_VALUE, statement->range, increment_operands, 2, true);
        increment->binary_operation = IR_BINARY_INTEGER_ADD;
        ir_branch(builder, header, statement->range);
        ir_block_parameter_incoming_add(builder, index, latch, increment->result);
        builder->current = body_block;
        AnalysisLocal* local = ir_local_find(builder->body, for_statement->name);
        BUSTER_CHECK(local);
        IrValueId element_operands[2] = {
            iterable_value,
            index->value,
        };
        IrInstruction* element = ir_emit(builder, IR_OPCODE_INDEX, local->type, IR_VALUE_VALUE, statement->range, element_operands, 2, true);
        IrValueId store_operands[2] = {
            builder->function->local_places[local->id.value],
            element->result,
        };
        if (builder->function->local_uses_memory[local->id.value])
        {
            ir_emit(builder, IR_OPCODE_STORE, builder->analysis->types.builtin.void_type, IR_VALUE_VALUE, statement->range, store_operands, 2, false);
        }
        else
        {
            ir_ssa_write(builder, body_block, local->id, element->result);
        }
        ir_task_push(builder->scratch_arena, top, statement->next, exit, task->end, task->break_block, task->continue_block);
        ir_seal_task_push(builder->scratch_arena, top, header);
        ir_task_push(builder->scratch_arena, top, for_statement->body.first_statement, body_block, latch, exit, latch);
    }
    break;
    case AST_STATEMENT_LOOP:
    {
        AstLoopStatement* loop = &statement->loop_statement;
        IrBlockId header = ir_block_create(builder);
        IrBlockId body_block = ir_block_create(builder);
        IrBlockId exit = ir_block_create(builder);
        ir_branch(builder, header, statement->range);
        builder->current = header;
        if (loop->has_condition)
        {
            IrLowered condition = ir_lower_expression(builder, loop->condition);
            IrValueId condition_value = ir_materialize(builder, condition, statement->range);
            IrBlockId targets[2] = {body_block, exit};
            ir_terminate(builder, IR_OPCODE_BRANCH_IF, statement->range, &condition_value, 1, targets, 2);
        }
        else
        {
            ir_branch(builder, body_block, statement->range);
        }
        ir_task_push(builder->scratch_arena, top, statement->next, exit, task->end, task->break_block, task->continue_block);
        ir_seal_task_push(builder->scratch_arena, top, header);
        ir_task_push(builder->scratch_arena, top, loop->body.first_statement, body_block, header, exit, header);
    }
    break;
    case AST_STATEMENT_BREAK:
    {
        BUSTER_CHECK(ir_block_id_valid(builder->function, task->break_block));
        ir_branch(builder, task->break_block, statement->range);
    }
    break;
    case AST_STATEMENT_CONTINUE:
    {
        BUSTER_CHECK(ir_block_id_valid(builder->function, task->continue_block));
        ir_branch(builder, task->continue_block, statement->range);
    }
    break;
    case AST_STATEMENT_COUNT:
        break;
    }
}

bool ir_entity_has_diagnostic(AnalysisResult* analysis, AnalysisEntityId entity)
{
    for (AnalysisDiagnostic* diagnostic = analysis->first_diagnostic; diagnostic; diagnostic = diagnostic->next)
    {
        if (ir_entity_id_equal(diagnostic->entity, entity))
        {
            return true;
        }
    }
    return false;
}

typedef struct IrMeasureTask IrMeasureTask;
struct IrMeasureTask
{
    IrMeasureTask* previous;
    AstStatement* statement;
};

BUSTER_GLOBAL_LOCAL void ir_measure_task_push(Arena* arena, IrMeasureTask** top, AstStatement* statement)
{
    if (statement)
    {
        IrMeasureTask* task = arena_allocate(arena, IrMeasureTask, 1);
        *task = (IrMeasureTask){.previous = *top, .statement = statement};
        *top = task;
    }
}

BUSTER_GLOBAL_LOCAL void ir_function_measure(Arena* scratch_arena, AstCode* code, AnalysisBody* body, u32* instruction_capacity, u32* block_capacity,
                                             u32* value_capacity)
{
    u32 node_count = 0;
    u32 expression_control_block_count = 0;
    for (AnalysisTypedExpression* expression = body->first_expression; expression; expression = expression->next)
    {
        node_count += expression->ast.count;
        for (u32 node_index = 0; node_index < expression->ast.count; node_index += 1)
        {
            AstNodeId id = expression->ast.nodes[node_index].id;
            if (id == AST_NODE_BINARY_BOOLEAN_AND_SHORT_CIRCUIT || id == AST_NODE_BINARY_BOOLEAN_OR_SHORT_CIRCUIT)
            {
                expression_control_block_count += 3;
            }
        }
    }
    u32 statement_count = 0;
    u32 control_block_count = 0;
    IrMeasureTask* top = 0;
    ir_measure_task_push(scratch_arena, &top, code->body.first_statement);
    while (top)
    {
        IrMeasureTask* task = top;
        top = task->previous;
        AstStatement* statement = task->statement;
        statement_count += 1;
        ir_measure_task_push(scratch_arena, &top, statement->next);
        switch (statement->id)
        {
        case AST_STATEMENT_IF:
        {
            control_block_count += 3;
            ir_measure_task_push(scratch_arena, &top, statement->if_statement.then_block.first_statement);
            if (statement->if_statement.alternative == AST_IF_ALTERNATIVE_BLOCK)
            {
                ir_measure_task_push(scratch_arena, &top, statement->if_statement.else_block.first_statement);
            }
            else if (statement->if_statement.alternative == AST_IF_ALTERNATIVE_IF)
            {
                ir_measure_task_push(scratch_arena, &top, statement->if_statement.else_if);
            }
        }
        break;
        case AST_STATEMENT_SWITCH:
        {
            control_block_count += statement->switch_statement.case_count + 1;
            for (AstSwitchCase* switch_case = statement->switch_statement.first_case; switch_case; switch_case = switch_case->next)
            {
                ir_measure_task_push(scratch_arena, &top, switch_case->body.first_statement);
            }
        }
        break;
        case AST_STATEMENT_FOR:
        {
            control_block_count += 4;
            ir_measure_task_push(scratch_arena, &top, statement->for_statement.body.first_statement);
        }
        break;
        case AST_STATEMENT_LOOP:
        {
            control_block_count += 3;
            ir_measure_task_push(scratch_arena, &top, statement->loop_statement.body.first_statement);
        }
        break;
        case AST_STATEMENT_RETURN:
        case AST_STATEMENT_DATA:
        case AST_STATEMENT_EXPRESSION:
        case AST_STATEMENT_ASSIGNMENT:
        case AST_STATEMENT_BREAK:
        case AST_STATEMENT_CONTINUE:
        case AST_STATEMENT_COUNT:
            break;
        }
    }
    *block_capacity = 2 + control_block_count + expression_control_block_count;
    *instruction_capacity = node_count * 4 + statement_count * 8 + body->local_count * 3 + *block_capacity * 2 + 32;
    *value_capacity = *instruction_capacity;
}

BUSTER_GLOBAL_LOCAL void ir_lower_function(Arena* result_arena, Arena* scratch_arena, AnalysisResult* analysis, AnalysisEntity* entity,
                                           AnalysisInstantiation* instantiation, IrFunction* function)
{
    u32 entity_index = entity->id.index.value;
    AnalysisBody* body = instantiation ? &instantiation->body : analysis->module.bodies + entity_index;
    AnalysisTypeId function_type_id = instantiation ? instantiation->function_type : analysis->module.semantics[entity_index].type;
    AnalysisType* function_type = analysis_type_from_id(analysis, function_type_id);
    BUSTER_CHECK(function_type->kind == ANALYSIS_TYPE_FUNCTION);
    function->name = instantiation ? instantiation->symbol_name : entity->name;
    function->entity = entity->id;
    function->instantiation = instantiation ? instantiation->id : ANALYSIS_INSTANTIATION_ID_INVALID;
    function->type = function_type_id;
    function->local_count = body->local_count;
    ir_function_measure(scratch_arena, entity->ast.code, body, &function->instruction_capacity, &function->block_capacity, &function->value_capacity);
    function->blocks = arena_allocate(result_arena, IrBlock, function->block_capacity);
    function->instructions = arena_allocate(result_arena, IrInstruction, function->instruction_capacity);
    function->values = arena_allocate(result_arena, IrValue, function->value_capacity);
    function->local_places = arena_allocate(result_arena, IrValueId, function->local_count);
    function->local_uses_memory = arena_allocate(result_arena, bool, function->local_count);
    IrBuilder builder = {
        .result_arena = result_arena,
        .scratch_arena = scratch_arena,
        .analysis = analysis,
        .entity = entity,
        .body = body,
        .function = function,
    };
    function->entry = ir_block_create(&builder);
    builder.current = function->entry;
    for (u32 local_index = 0; local_index < body->local_count; local_index += 1)
    {
        AnalysisLocal* local = body->locals + local_index;
        function->local_places[local_index] = IR_VALUE_ID_INVALID;
        function->local_uses_memory[local_index] = local->address_taken || local->requires_storage;
        if (!function->local_uses_memory[local_index])
        {
            continue;
        }
        IrInstruction* storage = ir_emit(&builder, IR_OPCODE_LOCAL, local->type, IR_VALUE_PLACE, local->range, 0, 0, true);
        storage->local = local->id;
        function->local_places[local_index] = storage->result;
    }
    u32 argument_index = 0;
    for (u32 local_index = 0; local_index < body->local_count; local_index += 1)
    {
        AnalysisLocal* local = body->locals + local_index;
        if (local->kind != ANALYSIS_LOCAL_ARGUMENT)
        {
            continue;
        }
        if (local->is_compile_time)
        {
            continue;
        }
        IrInstruction* argument = ir_emit(&builder, IR_OPCODE_ARGUMENT, local->type, IR_VALUE_VALUE, local->range, 0, 0, true);
        argument->immediates = arena_allocate(result_arena, u64, 1);
        argument->immediates[0] = argument_index;
        argument->immediate_count = 1;
        IrValueId operands[2] = {function->local_places[local_index], argument->result};
        if (function->local_uses_memory[local_index])
        {
            ir_emit(&builder, IR_OPCODE_STORE, analysis->types.builtin.void_type, IR_VALUE_VALUE, local->range, operands, 2, false);
        }
        else
        {
            ir_ssa_write(&builder, function->entry, local->id, argument->result);
        }
        argument_index += 1;
    }
    BUSTER_CHECK(argument_index == function_type->as.function.argument_count);
    IrBlockId exit = ir_block_create(&builder);
    builder.current = exit;
    if (analysis_type_from_id(analysis, function_type->as.function.return_type)->kind == ANALYSIS_TYPE_VOID)
    {
        ir_terminate(&builder, IR_OPCODE_RETURN, entity->range, 0, 0, 0, 0);
    }
    else
    {
        ir_terminate(&builder, IR_OPCODE_UNREACHABLE, entity->range, 0, 0, 0, 0);
    }
    IrLowerTask* top = 0;
    ir_task_push(scratch_arena, &top, entity->ast.code->body.first_statement, function->entry, exit, IR_BLOCK_ID_INVALID, IR_BLOCK_ID_INVALID);
    while (top)
    {
        IrLowerTask* task = top;
        top = task->previous;
        ir_lower_statement_task(&builder, &top, task);
    }
    for (u32 block_index = 0; block_index < function->block_count; block_index += 1)
    {
        ir_block_seal(&builder, (IrBlockId){.value = block_index});
    }
    function->state = IR_FUNCTION_LOWERED;
}

BUSTER_GLOBAL_LOCAL IrModule ir_module_initialize(Arena* result_arena, AnalysisResult* analysis)
{
    Arena* conflicts[] = {result_arena};
    TemporalArena scratch = scratch_begin(conflicts, BUSTER_ARRAY_LENGTH(conflicts));
    u32 ordinary_function_count = 0;
    for (u32 entity_index = 0; entity_index < analysis->module.entity_count; entity_index += 1)
    {
        AnalysisEntity* entity = analysis->module.entities + entity_index;
        if (entity->kind == ANALYSIS_ENTITY_CODE && !analysis_entity_is_generic(scratch.arena, analysis, entity))
        {
            ordinary_function_count += 1;
        }
    }
    u32 owned_specialization_count = 0;
    for (AnalysisInstantiation* instantiation = analysis->first_instantiation; instantiation; instantiation = instantiation->next)
    {
        owned_specialization_count += instantiation->codegen_owner.value == analysis->module.id.value;
    }
    IrModule module = {
        .name = analysis->module.name,
        .function_count = ordinary_function_count + owned_specialization_count,
    };
    module.functions = arena_allocate(result_arena, IrFunction, module.function_count);
    u32 function_index = 0;
    for (u32 entity_index = 0; entity_index < analysis->module.entity_count; entity_index += 1)
    {
        AnalysisEntity* entity = analysis->module.entities + entity_index;
        if (entity->kind != ANALYSIS_ENTITY_CODE)
        {
            continue;
        }
        if (analysis_entity_is_generic(scratch.arena, analysis, entity))
        {
            continue;
        }
        module.functions[function_index] = (IrFunction){
            .name = entity->name,
            .symbol = IR_SYMBOL_ID_INVALID,
            .source =
                {
                    .source = {.value = entity->source.value},
                    .offset = entity->range.offset,
                    .length = entity->range.length,
                    .line = entity->range.line + 1,
                    .column = entity->range.column + 1,
                },
            .canonical_type = IR_TYPE_ID_INVALID,
            .entity = entity->id,
            .instantiation = ANALYSIS_INSTANTIATION_ID_INVALID,
            .type = analysis->module.semantics[entity_index].type,
            .id = {.value = function_index},
            .entry = IR_BLOCK_ID_INVALID,
            .state = entity->ast.code->has_body ? IR_FUNCTION_NOT_LOWERED : IR_FUNCTION_DECLARATION,
        };
        function_index += 1;
    }
    AnalysisInstantiation** specializations = arena_allocate(scratch.arena, AnalysisInstantiation*, analysis->instantiation_count);
    u32 specialization_count = 0;
    for (AnalysisInstantiation* instantiation = analysis->first_instantiation; instantiation; instantiation = instantiation->next)
    {
        if (instantiation->codegen_owner.value == analysis->module.id.value)
        {
            specializations[specialization_count++] = instantiation;
        }
    }
    BUSTER_CHECK(specialization_count == owned_specialization_count);
    for (u32 index = 1; index < specialization_count; index += 1)
    {
        AnalysisInstantiation* instantiation = specializations[index];
        u32 insertion = index;
        while (insertion)
        {
            String8 left = specializations[insertion - 1]->canonical_key;
            String8 right = instantiation->canonical_key;
            u64 common = BUSTER_MIN(left.length, right.length);
            s32 order = memcmp(left.pointer, right.pointer, (size_t)common);
            if (!order)
            {
                order = left.length > right.length ? 1 : left.length < right.length ? -1 : 0;
            }
            if (order <= 0)
            {
                break;
            }
            specializations[insertion] = specializations[insertion - 1];
            insertion -= 1;
        }
        specializations[insertion] = instantiation;
    }
    for (u32 index = 0; index < specialization_count; index += 1)
    {
        AnalysisInstantiation* instantiation = specializations[index];
        AnalysisEntity* entity = analysis->module.entities + instantiation->generic_entity.index.value;
        module.functions[function_index] = (IrFunction){
            .name = instantiation->symbol_name,
            .symbol = IR_SYMBOL_ID_INVALID,
            .source =
                {
                    .source = {.value = entity->source.value},
                    .offset = entity->range.offset,
                    .length = entity->range.length,
                    .line = entity->range.line + 1,
                    .column = entity->range.column + 1,
                },
            .canonical_type = IR_TYPE_ID_INVALID,
            .entity = entity->id,
            .instantiation = instantiation->id,
            .type = instantiation->function_type,
            .id = {.value = function_index},
            .entry = IR_BLOCK_ID_INVALID,
            .state = entity->ast.code->has_body ? IR_FUNCTION_NOT_LOWERED : IR_FUNCTION_DECLARATION,
        };
        function_index += 1;
    }
    BUSTER_CHECK(function_index == module.function_count);
    scratch_end(scratch);
    return module;
}

AnalysisInstantiation* ir_instantiation_from_id(AnalysisResult* analysis, AnalysisInstantiationId id)
{
    for (AnalysisInstantiation* instantiation = analysis->first_instantiation; instantiation; instantiation = instantiation->next)
    {
        if (instantiation->id.value == id.value)
        {
            return instantiation;
        }
    }
    return 0;
}

IrModule ir_generate_module(Arena* result_arena, AnalysisResult* analysis)
{
    BUSTER_CHECK(analysis && analysis->types.types);
    IrModule module = ir_module_initialize(result_arena, analysis);
    Arena* conflicts[] = {result_arena};
    TemporalArena scratch = scratch_begin(conflicts, BUSTER_ARRAY_LENGTH(conflicts));
    for (u32 function_index = 0; function_index < module.function_count; function_index += 1)
    {
        IrFunction* function = module.functions + function_index;
        AnalysisEntity* entity = analysis->module.entities + function->entity.index.value;
        AnalysisInstantiation* instantiation =
            function->instantiation.value == ANALYSIS_ID_UNDERLYING_INVALID ? 0 : ir_instantiation_from_id(analysis, function->instantiation);
        if (!entity->ast.code->has_body)
        {
            BUSTER_CHECK(function->state == IR_FUNCTION_DECLARATION);
        }
        else if ((!instantiation && !analysis->module.bodies[entity->id.index.value].analyzed) || (instantiation && !instantiation->body.analyzed) ||
                 ir_entity_has_diagnostic(analysis, entity->id))
        {
            function->state = IR_FUNCTION_REJECTED;
            module.rejected_function_count += 1;
        }
        else
        {
            ir_lower_function(result_arena, scratch.arena, analysis, entity, instantiation, function);
            module.lowered_function_count += 1;
        }
    }
    scratch_end(scratch);
    return module;
}

IrModule ir_analyze_and_generate_module(Arena* result_arena, AnalysisResult* analysis)
{
    BUSTER_CHECK(analysis && analysis->types.types);
    analysis_analyze_bodies(result_arena, analysis);
    return ir_generate_module(result_arena, analysis);
}

BUSTER_GLOBAL_LOCAL IrTypeKind ir_type_kind_from_analysis(AnalysisTypeKind kind)
{
    switch (kind)
    {
    case ANALYSIS_TYPE_VOID:
        return IR_TYPE_VOID;
    case ANALYSIS_TYPE_BOOL:
        return IR_TYPE_BOOLEAN;
    case ANALYSIS_TYPE_INTEGER:
        return IR_TYPE_INTEGER;
    case ANALYSIS_TYPE_FLOAT:
        return IR_TYPE_FLOAT;
    case ANALYSIS_TYPE_VA_LIST:
        return IR_TYPE_VA_LIST;
    case ANALYSIS_TYPE_POINTER:
        return IR_TYPE_POINTER;
    case ANALYSIS_TYPE_SLICE:
        return IR_TYPE_SLICE;
    case ANALYSIS_TYPE_ARRAY:
        return IR_TYPE_ARRAY;
    case ANALYSIS_TYPE_VECTOR:
        return IR_TYPE_VECTOR;
    case ANALYSIS_TYPE_FUNCTION:
        return IR_TYPE_FUNCTION;
    case ANALYSIS_TYPE_RANGE:
        return IR_TYPE_RANGE;
    case ANALYSIS_TYPE_STRUCT:
        return IR_TYPE_STRUCT;
    case ANALYSIS_TYPE_UNION:
        return IR_TYPE_UNION;
    case ANALYSIS_TYPE_ENUM:
        return IR_TYPE_ENUM;
    case ANALYSIS_TYPE_POISON:
    case ANALYSIS_TYPE_COMPILE_TIME_PARAMETER:
    case ANALYSIS_TYPE_INFERRED_ARRAY:
    case ANALYSIS_TYPE_COUNT:
        break;
    }
    return IR_TYPE_COUNT;
}

BUSTER_GLOBAL_LOCAL IrAbiClass ir_abi_class_from_analysis(AnalysisAbiClass abi_class)
{
    switch (abi_class)
    {
    case ANALYSIS_ABI_CLASS_NONE:
        return IR_ABI_CLASS_NONE;
    case ANALYSIS_ABI_CLASS_INTEGER:
        return IR_ABI_CLASS_INTEGER;
    case ANALYSIS_ABI_CLASS_FLOAT:
        return IR_ABI_CLASS_FLOAT;
    case ANALYSIS_ABI_CLASS_VECTOR:
        return IR_ABI_CLASS_VECTOR;
    case ANALYSIS_ABI_CLASS_POINTER:
        return IR_ABI_CLASS_POINTER;
    case ANALYSIS_ABI_CLASS_AGGREGATE:
        return IR_ABI_CLASS_AGGREGATE;
    case ANALYSIS_ABI_CLASS_MEMORY:
        return IR_ABI_CLASS_MEMORY;
    case ANALYSIS_ABI_CLASS_COUNT:
        break;
    }
    return IR_ABI_CLASS_COUNT;
}

BUSTER_GLOBAL_LOCAL IrCallingConvention ir_calling_convention_from_ast(AstCallingConvention calling_convention)
{
    switch (calling_convention)
    {
    case AST_CALLING_CONVENTION_C:
        return IR_CALLING_CONVENTION_C;
    case AST_CALLING_CONVENTION_SYSTEMV:
        return IR_CALLING_CONVENTION_SYSTEMV;
    case AST_CALLING_CONVENTION_WIN64:
        return IR_CALLING_CONVENTION_WIN64;
    case AST_CALLING_CONVENTION_COUNT:
        break;
    }
    return IR_CALLING_CONVENTION_COUNT;
}

IrAbiConvention ir_abi_convention_for_target(Target target)
{
    if (target.cpu_arch == CPU_ARCH_X86_64)
    {
        return target.os == OPERATING_SYSTEM_WINDOWS || target.os == OPERATING_SYSTEM_UEFI ? IR_ABI_CONVENTION_WIN64_X86_64
                                                                                           : IR_ABI_CONVENTION_SYSTEMV_X86_64;
    }
    if (target.cpu_arch == CPU_ARCH_AARCH64)
    {
        if (target.os == OPERATING_SYSTEM_WINDOWS)
        {
            return IR_ABI_CONVENTION_WINDOWS_AARCH64;
        }
        if (target.os == OPERATING_SYSTEM_MACOS || target.os == OPERATING_SYSTEM_IOS)
        {
            return IR_ABI_CONVENTION_DARWIN_AARCH64;
        }
        return IR_ABI_CONVENTION_AAPCS64;
    }
    return IR_ABI_CONVENTION_SYSTEMV_X86_64;
}

BUSTER_GLOBAL_LOCAL IrSymbolKind ir_symbol_kind_from_analysis(AnalysisEntityKind kind)
{
    switch (kind)
    {
    case ANALYSIS_ENTITY_TYPE:
        return IR_SYMBOL_TYPE;
    case ANALYSIS_ENTITY_CODE:
        return IR_SYMBOL_FUNCTION;
    case ANALYSIS_ENTITY_DATA:
        return IR_SYMBOL_DATA;
    case ANALYSIS_ENTITY_COUNT:
        break;
    }
    return IR_SYMBOL_COUNT;
}

// Parser lines and columns are zero-based; canonical IR source ranges are
// one-based, matching the C frontend and DWARF.
BUSTER_GLOBAL_LOCAL IrSourceRange ir_source_range_from_parser(IrSourceId source, ParserSourceRange range)
{
    return (IrSourceRange){
        .source = source,
        .offset = range.offset,
        .length = range.length,
        .line = range.line + 1,
        .column = range.column + 1,
    };
}

BUSTER_GLOBAL_LOCAL IrTypeId ir_program_type_map(IrProgram* program, AnalysisModuleId module, AnalysisTypeId type)
{
    if (module.value >= program->module_count)
    {
        return IR_TYPE_ID_INVALID;
    }
    IrModule* ir_module = program->modules + module.value;
    if (type.value >= ir_module->frontend_type_count)
    {
        return IR_TYPE_ID_INVALID;
    }
    return ir_module->frontend_type_map[type.value];
}

IrProgram ir_program_initialize(Arena* arena, u32 module_count, u32 type_capacity, u32 symbol_capacity, u32 source_capacity)
{
    IrProgram program = {0};
    if (!arena)
    {
        return program;
    }
    program.arena = arena;
    program.data_layout = target_data_layout(target_native);
    program.modules = arena_allocate(arena, IrModule, module_count);
    program.module_count = module_count;
    program.types = (IrTypeTable){
        .types = arena_allocate(arena, IrType, type_capacity),
        .capacity = type_capacity,
    };
    program.symbols = (IrSymbolTable){
        .symbols = arena_allocate(arena, IrSymbol, symbol_capacity),
        .capacity = symbol_capacity,
    };
    program.sources = (IrSourceTable){
        .sources = arena_allocate(arena, IrSource, source_capacity),
        .capacity = source_capacity,
    };
    for (u32 index = 0; index < module_count; index += 1)
    {
        program.modules[index] = (IrModule){0};
    }
    return program;
}

typedef struct IrAbiClassificationTask IrAbiClassificationTask;
struct IrAbiClassificationTask
{
    IrTypeId type;
    u64 offset;
};

BUSTER_GLOBAL_LOCAL bool ir_system_v_abi_classes(IrProgram* program, IrTypeId root_type, IrAbiClass classes[2])
{
    IrType* root = ir_type_from_id(&program->types, root_type);
    if (!root || !root->layout.resolved || !root->layout.size || root->layout.size > 16)
    {
        return false;
    }
    TemporalArena temporary = scratch_begin(0, 0);
    u32 capacity = BUSTER_MAX(program->types.count * 16, 16);
    IrAbiClassificationTask* tasks = arena_allocate(temporary.arena, IrAbiClassificationTask, capacity);
    u32 count = 1;
    tasks[0] = (IrAbiClassificationTask){
        .type = root_type,
    };
    bool valid = true;
    while (count && valid)
    {
        IrAbiClassificationTask task = tasks[--count];
        IrType* type = ir_type_from_id(&program->types, task.type);
        if (!type || !type->layout.resolved || task.offset + type->layout.size > 16 || (type->layout.alignment && task.offset % type->layout.alignment))
        {
            valid = false;
            break;
        }
        if (type->kind == IR_TYPE_STRUCT || type->kind == IR_TYPE_UNION)
        {
            if (count + type->field_count > capacity)
            {
                valid = false;
                break;
            }
            for (u32 index = 0; index < type->field_count; index += 1)
            {
                IrField* field = type->fields + index;
                tasks[count++] = (IrAbiClassificationTask){
                    .type = field->type,
                    .offset = task.offset + field->offset,
                };
            }
            continue;
        }
        if (type->kind == IR_TYPE_ARRAY)
        {
            IrType* element = ir_type_from_id(&program->types, type->element_type);
            if (!element || !element->layout.resolved || type->element_count > capacity - count)
            {
                valid = false;
                break;
            }
            for (u64 index = 0; index < type->element_count; index += 1)
            {
                tasks[count++] = (IrAbiClassificationTask){
                    .type = type->element_type,
                    .offset = task.offset + index * element->layout.size,
                };
            }
            continue;
        }
        IrAbiClass abi_class = type->kind == IR_TYPE_FLOAT || type->kind == IR_TYPE_VECTOR ? IR_ABI_CLASS_FLOAT : IR_ABI_CLASS_INTEGER;
        bool scalar = type->kind == IR_TYPE_BOOLEAN || type->kind == IR_TYPE_INTEGER || type->kind == IR_TYPE_FLOAT || type->kind == IR_TYPE_POINTER ||
                      type->kind == IR_TYPE_FUNCTION || type->kind == IR_TYPE_VECTOR || type->kind == IR_TYPE_ENUM;
        if (!scalar)
        {
            valid = false;
            break;
        }
        u32 first = (u32)(task.offset / 8);
        u32 last = (u32)((task.offset + BUSTER_MAX(type->layout.size, (u64)1) - 1) / 8);
        for (u32 part = first; part <= last; part += 1)
        {
            if (part >= 2)
            {
                valid = false;
                break;
            }
            if (classes[part] == IR_ABI_CLASS_NONE)
            {
                classes[part] = abi_class;
            }
            else if (classes[part] != abi_class)
            {
                classes[part] = IR_ABI_CLASS_INTEGER;
            }
        }
    }
    scratch_end(temporary);
    return valid;
}

BUSTER_GLOBAL_LOCAL bool ir_homogeneous_float_abi(IrProgram* program, IrTypeId root_type, IrTypeId* element_out, u32* count_out)
{
    TemporalArena temporary = scratch_begin(0, 0);
    u32 capacity = BUSTER_MAX(program->types.count * 16, 16);
    IrTypeId* tasks = arena_allocate(temporary.arena, IrTypeId, capacity);
    u32 task_count = 1;
    u32 count = 0;
    IrTypeId element = IR_TYPE_ID_INVALID;
    tasks[0] = root_type;
    bool valid = true;
    while (task_count && valid)
    {
        IrTypeId type_id = tasks[--task_count];
        IrType* type = ir_type_from_id(&program->types, type_id);
        if (!type)
        {
            valid = false;
        }
        else if (type->kind == IR_TYPE_FLOAT)
        {
            if (element.value != IR_ID_UNDERLYING_INVALID && element.value != type_id.value)
            {
                valid = false;
                break;
            }
            element = type_id;
            count += 1;
            if (count > IR_ABI_MAX_PARTS)
            {
                valid = false;
            }
        }
        else if (type->kind == IR_TYPE_ARRAY)
        {
            if (!type->element_count || type->element_count > capacity - task_count)
            {
                valid = false;
                break;
            }
            for (u64 index = 0; index < type->element_count; index += 1)
            {
                tasks[task_count++] = type->element_type;
            }
        }
        else if (type->kind == IR_TYPE_STRUCT && type->field_count && type->field_count <= capacity - task_count)
        {
            for (u32 index = 0; index < type->field_count; index += 1)
            {
                tasks[task_count++] = type->fields[index].type;
            }
        }
        else
        {
            valid = false;
        }
    }
    scratch_end(temporary);
    if (!valid || !count)
    {
        return false;
    }
    *element_out = element;
    *count_out = count;
    return true;
}

BUSTER_GLOBAL_LOCAL IrAbiValue ir_classify_abi_value(IrProgram* program, IrTypeId type_id, IrAbiConvention convention, bool is_result,
                                                      bool variadic_argument)
{
    IrAbiValue value = {0};
    IrType* type = ir_type_from_id(&program->types, type_id);
    if (!type || !type->layout.resolved || convention >= IR_ABI_CONVENTION_COUNT)
    {
        return value;
    }
    u64 size = type->layout.size;
    bool aggregate = type->kind == IR_TYPE_STRUCT || type->kind == IR_TYPE_UNION || type->kind == IR_TYPE_ARRAY || type->kind == IR_TYPE_VA_LIST;
    if (type->kind == IR_TYPE_VOID)
    {
        return value;
    }
    if (!aggregate && type->kind != IR_TYPE_VECTOR)
    {
        if (type->kind == IR_TYPE_FLOAT)
        {
            if (type->bit_width > 64)
            {
                value.part_count = 1;
                value.indirect = is_result;
                value.memory = !is_result;
                value.parts[0] = (IrAbiPart){
                    .abi_class = is_result ? IR_ABI_CLASS_POINTER : IR_ABI_CLASS_MEMORY,
                    .size = is_result ? 8 : (u32)size,
                };
            }
            else
            {
                value.part_count = 1;
                value.parts[0] = (IrAbiPart){
                    .abi_class = convention == IR_ABI_CONVENTION_WINDOWS_AARCH64 && variadic_argument ? IR_ABI_CLASS_INTEGER : IR_ABI_CLASS_FLOAT,
                    .size = (u32)size,
                };
            }
            return value;
        }
        if (type->kind == IR_TYPE_POINTER || type->kind == IR_TYPE_FUNCTION)
        {
            value.part_count = 1;
            value.parts[0] = (IrAbiPart){.abi_class = IR_ABI_CLASS_POINTER, .size = (u32)size};
            return value;
        }
        if (type->kind == IR_TYPE_INTEGER || type->kind == IR_TYPE_BOOLEAN || type->kind == IR_TYPE_ENUM)
        {
            if (size <= 8)
            {
                value.part_count = 1;
                value.parts[0] = (IrAbiPart){.abi_class = IR_ABI_CLASS_INTEGER, .size = (u32)size};
                return value;
            }
            if (size <= 16 && convention != IR_ABI_CONVENTION_WIN64_X86_64)
            {
                value.part_count = (u32)((size + 7) / 8);
                for (u32 part = 0; part < value.part_count; part += 1)
                {
                    value.parts[part] = (IrAbiPart){
                        .abi_class = IR_ABI_CLASS_INTEGER,
                        .value_offset = part * 8,
                        .size = (u32)BUSTER_MIN((u64)8, size - (u64)part * 8),
                    };
                }
                return value;
            }
            value.part_count = 1;
            value.indirect = true;
            value.memory = !is_result;
            value.parts[0] = (IrAbiPart){
                .abi_class = is_result ? IR_ABI_CLASS_POINTER : IR_ABI_CLASS_MEMORY,
                .size = is_result ? 8 : (u32)size,
            };
            return value;
        }
        return value;
    }
    if (type->kind == IR_TYPE_VECTOR)
    {
        bool aarch64 = convention == IR_ABI_CONVENTION_AAPCS64 || convention == IR_ABI_CONVENTION_DARWIN_AARCH64 ||
                       convention == IR_ABI_CONVENTION_WINDOWS_AARCH64;
        if (convention == IR_ABI_CONVENTION_WIN64_X86_64)
        {
            if (!is_result || (size != 8 && size != 16))
            {
                value.part_count = 1;
                value.indirect = true;
                value.parts[0] = (IrAbiPart){
                    .abi_class = IR_ABI_CLASS_POINTER,
                    .size = 8,
                };
            }
            else
            {
                value.part_count = 1;
                value.parts[0] = (IrAbiPart){
                    .abi_class = IR_ABI_CLASS_VECTOR,
                    .size = (u32)size,
                };
            }
            return value;
        }
        if ((aarch64 && size > 16) || (convention == IR_ABI_CONVENTION_SYSTEMV_X86_64 && size > 64))
        {
            value.part_count = 1;
            value.indirect = is_result;
            value.memory = !is_result;
            value.parts[0] = (IrAbiPart){
                .abi_class = is_result ? IR_ABI_CLASS_POINTER : IR_ABI_CLASS_MEMORY,
                .size = is_result ? 8 : (u32)size,
            };
            return value;
        }
        if (convention == IR_ABI_CONVENTION_WINDOWS_AARCH64 && variadic_argument)
        {
            value.part_count = (u32)((size + 7) / 8);
            for (u32 part = 0; part < value.part_count; part += 1)
            {
                value.parts[part] = (IrAbiPart){
                    .abi_class = IR_ABI_CLASS_INTEGER,
                    .value_offset = part * 8,
                    .size = (u32)BUSTER_MIN((u64)8, size - (u64)part * 8),
                };
            }
            return value;
        }
        if (convention == IR_ABI_CONVENTION_SYSTEMV_X86_64 && variadic_argument && size > 16)
        {
            value.part_count = 1;
            value.memory = true;
            value.parts[0] = (IrAbiPart){
                .abi_class = IR_ABI_CLASS_MEMORY,
                .size = (u32)size,
            };
            return value;
        }
        value.part_count = 1;
        value.parts[0] = (IrAbiPart){.abi_class = IR_ABI_CLASS_VECTOR, .size = (u32)size};
        return value;
    }
    if (convention == IR_ABI_CONVENTION_WIN64_X86_64)
    {
        value.part_count = 1;
        if (size == 1 || size == 2 || size == 4 || size == 8)
        {
            value.parts[0] = (IrAbiPart){.abi_class = IR_ABI_CLASS_INTEGER, .size = (u32)size};
        }
        else
        {
            value.indirect = true;
            value.parts[0] = (IrAbiPart){.abi_class = IR_ABI_CLASS_POINTER, .size = 8};
        }
        return value;
    }
    bool aarch64 = convention == IR_ABI_CONVENTION_AAPCS64 || convention == IR_ABI_CONVENTION_DARWIN_AARCH64 ||
                   convention == IR_ABI_CONVENTION_WINDOWS_AARCH64;
    if (aarch64)
    {
        IrTypeId element = IR_TYPE_ID_INVALID;
        u32 count = 0;
        if (!(convention == IR_ABI_CONVENTION_WINDOWS_AARCH64 && variadic_argument) && ir_homogeneous_float_abi(program, type_id, &element, &count))
        {
            IrType* element_type = ir_type_from_id(&program->types, element);
            value.part_count = count;
            for (u32 part = 0; part < count; part += 1)
            {
                value.parts[part] = (IrAbiPart){
                    .abi_class = IR_ABI_CLASS_FLOAT,
                    .value_offset = part * (u32)element_type->layout.size,
                    .size = (u32)element_type->layout.size,
                };
            }
        }
        else if (size <= 16)
        {
            value.part_count = (u32)((size + 7) / 8);
            for (u32 part = 0; part < value.part_count; part += 1)
            {
                value.parts[part] = (IrAbiPart){
                    .abi_class = IR_ABI_CLASS_INTEGER,
                    .value_offset = part * 8,
                    .size = (u32)BUSTER_MIN((u64)8, size - (u64)part * 8),
                };
            }
        }
        else
        {
            value.part_count = 1;
            value.indirect = true;
            value.parts[0] = (IrAbiPart){.abi_class = IR_ABI_CLASS_POINTER, .size = 8};
        }
        return value;
    }
    if (size > 16)
    {
        value.part_count = 1;
        value.indirect = is_result;
        value.memory = !is_result;
        value.parts[0] = (IrAbiPart){
            .abi_class = is_result ? IR_ABI_CLASS_POINTER : IR_ABI_CLASS_MEMORY,
            .size = is_result ? 8 : (u32)size,
        };
        return value;
    }
    IrAbiClass classes[2] = {0};
    if (!ir_system_v_abi_classes(program, type_id, classes))
    {
        value.part_count = 1;
        value.indirect = is_result;
        value.memory = !is_result;
        value.parts[0] = (IrAbiPart){
            .abi_class = is_result ? IR_ABI_CLASS_POINTER : IR_ABI_CLASS_MEMORY,
            .size = is_result ? 8 : (u32)size,
        };
        return value;
    }
    value.part_count = (u32)((size + 7) / 8);
    for (u32 part = 0; part < value.part_count; part += 1)
    {
        value.parts[part] = (IrAbiPart){
            .abi_class = classes[part] == IR_ABI_CLASS_NONE ? IR_ABI_CLASS_INTEGER : classes[part],
            .value_offset = part * 8,
            .size = (u32)BUSTER_MIN((u64)8, size - (u64)part * 8),
        };
    }
    return value;
}

BUSTER_GLOBAL_LOCAL void ir_resolve_type_abi(IrProgram* program, IrTypeId type_id, IrAbiConvention convention)
{
    IrType* type = program ? ir_type_from_id(&program->types, type_id) : 0;
    if (!type || !program->arena || convention >= IR_ABI_CONVENTION_COUNT || !type->layout.resolved)
    {
        return;
    }
    if (!type->abi)
    {
        type->abi = arena_allocate(program->arena, IrTypeAbi, 1);
        *type->abi = (IrTypeAbi){0};
    }
    type->abi->values[convention][IR_ABI_USE_ARGUMENT] = ir_classify_abi_value(program, type_id, convention, false, false);
    type->abi->values[convention][IR_ABI_USE_RESULT] = ir_classify_abi_value(program, type_id, convention, true, false);
    type->abi->values[convention][IR_ABI_USE_VARIADIC_ARGUMENT] =
        convention == IR_ABI_CONVENTION_WINDOWS_AARCH64 ? ir_classify_abi_value(program, type_id, convention, false, true)
                                                        : type->abi->values[convention][IR_ABI_USE_ARGUMENT];
    type->abi->resolved[convention] = true;
}

void ir_prepare_program_abi(IrProgram* program, IrAbiConvention convention)
{
    if (!program || convention >= IR_ABI_CONVENTION_COUNT)
    {
        return;
    }
    for (u32 type_index = 0; type_index < program->types.count; type_index += 1)
    {
        IrType* type = program->types.types + type_index;
        if (!type->abi || !type->abi->resolved[convention])
        {
            ir_resolve_type_abi(program, type->id, convention);
        }
    }
}

IrAbiValue ir_type_abi_value(IrProgram* program, IrTypeId type_id, IrAbiConvention convention, IrAbiUse use)
{
    IrType* type = program ? ir_type_from_id(&program->types, type_id) : 0;
    if (!type || convention >= IR_ABI_CONVENTION_COUNT || use >= IR_ABI_USE_COUNT)
    {
        return (IrAbiValue){0};
    }
    if (!type->abi || !type->abi->resolved[convention])
    {
        ir_resolve_type_abi(program, type_id, convention);
    }
    return type->abi && type->abi->resolved[convention] ? type->abi->values[convention][use] : (IrAbiValue){0};
}

IrTypeId ir_program_add_type(IrProgram* program, IrType type)
{
    if (!program || program->types.count >= program->types.capacity)
    {
        return IR_TYPE_ID_INVALID;
    }
    IrTypeId id = {
        .value = program->types.count++,
    };
    type.id = id;
    program->types.types[id.value] = type;
    return id;
}

IrSymbolId ir_program_add_symbol(IrProgram* program, IrSymbol symbol)
{
    if (!program || program->symbols.count >= program->symbols.capacity)
    {
        return IR_SYMBOL_ID_INVALID;
    }
    IrSymbolId id = {
        .value = program->symbols.count++,
    };
    symbol.id = id;
    program->symbols.symbols[id.value] = symbol;
    return id;
}

IrSourceId ir_program_add_source(IrProgram* program, IrSource source)
{
    if (!program || program->sources.count >= program->sources.capacity)
    {
        return IR_SOURCE_ID_INVALID;
    }
    IrSourceId id = {
        .value = program->sources.count++,
    };
    source.id = id;
    program->sources.sources[id.value] = source;
    return id;
}

IrFunction* ir_module_add_function(Arena* arena, IrModule* module, IrFunction function)
{
    if (!arena || !module)
    {
        return 0;
    }
    if (module->function_count >= module->function_capacity)
    {
        u32 capacity = module->function_capacity ? module->function_capacity * 2 : 8;
        IrFunction* functions = arena_allocate(arena, IrFunction, capacity);
        if (module->function_count)
        {
            memcpy(functions, module->functions, sizeof(IrFunction) * module->function_count);
        }
        module->functions = functions;
        module->function_capacity = capacity;
    }
    function.id = (IrFunctionId){
        .value = module->function_count,
    };
    module->functions[module->function_count++] = function;
    return &module->functions[module->function_count - 1];
}

IrGlobal* ir_module_add_global(Arena* arena, IrModule* module, IrGlobal global)
{
    if (!arena || !module)
    {
        return 0;
    }
    if (module->global_count >= module->global_capacity)
    {
        u32 old_capacity = module->global_capacity;
        u32 capacity = old_capacity ? old_capacity * 2 : 8;
        IrGlobal* globals = arena_allocate(arena, IrGlobal, capacity);
        if (module->global_count)
        {
            memcpy(globals, module->globals, (u64)module->global_count * sizeof(*globals));
        }
        module->globals = globals;
        module->global_capacity = capacity;
    }
    IrGlobal* result = module->globals + module->global_count++;
    *result = global;
    return result;
}

IrBlock* ir_function_add_block(Arena* arena, IrFunction* function, IrBlock block)
{
    if (!arena || !function)
    {
        return 0;
    }
    if (function->block_count >= function->block_capacity)
    {
        u32 capacity = function->block_capacity ? function->block_capacity * 2 : 8;
        IrBlock* blocks = arena_allocate(arena, IrBlock, capacity);
        if (function->block_count)
        {
            memcpy(blocks, function->blocks, sizeof(IrBlock) * function->block_count);
        }
        function->blocks = blocks;
        function->block_capacity = capacity;
    }
    block.id = (IrBlockId){
        .value = function->block_count,
    };
    function->blocks[function->block_count++] = block;
    return &function->blocks[function->block_count - 1];
}

IrValueId ir_function_add_value(Arena* arena, IrFunction* function, IrValue value)
{
    if (!arena || !function)
    {
        return IR_VALUE_ID_INVALID;
    }
    if (function->value_count >= function->value_capacity)
    {
        u32 capacity = function->value_capacity ? function->value_capacity * 2 : 16;
        IrValue* values = arena_allocate(arena, IrValue, capacity);
        if (function->value_count)
        {
            memcpy(values, function->values, sizeof(IrValue) * function->value_count);
        }
        function->values = values;
        function->value_capacity = capacity;
    }
    IrValueId id = {
        .value = function->value_count++,
    };
    function->values[id.value] = value;
    return id;
}

IrInstructionId ir_function_add_instruction(Arena* arena, IrFunction* function, IrInstruction instruction)
{
    if (!arena || !function)
    {
        return IR_INSTRUCTION_ID_INVALID;
    }
    if (function->instruction_count >= function->instruction_capacity)
    {
        u32 capacity = function->instruction_capacity ? function->instruction_capacity * 2 : 16;
        IrInstruction* instructions = arena_allocate(arena, IrInstruction, capacity);
        if (function->instruction_count)
        {
            memcpy(instructions, function->instructions, sizeof(IrInstruction) * function->instruction_count);
        }
        function->instructions = instructions;
        function->instruction_capacity = capacity;
    }
    IrInstructionId id = {
        .value = function->instruction_count++,
    };
    instruction.id = id;
    function->instructions[id.value] = instruction;
    return id;
}

BUSTER_GLOBAL_LOCAL void ir_program_metadata_initialize(Arena* arena, AnalysisProgram* analysis, IrProgram* program)
{
    u32 type_capacity = 0;
    u32 symbol_capacity = 0;
    u32 source_capacity = 0;
    for (u32 module_index = 0; module_index < analysis->module_count; module_index += 1)
    {
        AnalysisResult* module = analysis->module_results[module_index];
        if (!module)
        {
            continue;
        }
        type_capacity += module->types.count;
        symbol_capacity += module->module.entity_count + module->instantiation_count;
        source_capacity += module->module.source_count;
    }
    program->types = (IrTypeTable){
        .types = arena_allocate(arena, IrType, type_capacity),
        .capacity = type_capacity,
    };
    program->symbols = (IrSymbolTable){
        .symbols = arena_allocate(arena, IrSymbol, symbol_capacity),
        .capacity = symbol_capacity,
    };
    program->sources = (IrSourceTable){
        .sources = arena_allocate(arena, IrSource, source_capacity),
        .capacity = source_capacity,
    };

    for (u32 module_index = 0; module_index < analysis->module_count; module_index += 1)
    {
        AnalysisResult* module = analysis->module_results[module_index];
        if (!module)
        {
            continue;
        }
        IrModule* ir_module = program->modules + module_index;
        ir_module->frontend_type_count = module->types.count;
        ir_module->frontend_symbol_count = module->module.entity_count;
        ir_module->frontend_source_count = module->module.source_count;
        ir_module->frontend_type_map = arena_allocate(arena, IrTypeId, ir_module->frontend_type_count);
        ir_module->frontend_symbol_map = arena_allocate(arena, IrSymbolId, ir_module->frontend_symbol_count);
        ir_module->frontend_source_map = arena_allocate(arena, IrSourceId, ir_module->frontend_source_count);
        for (u32 type_index = 0; type_index < ir_module->frontend_type_count; type_index += 1)
        {
            ir_module->frontend_type_map[type_index] = IR_TYPE_ID_INVALID;
        }
        for (u32 source_index = 0; source_index < module->module.source_count; source_index += 1)
        {
            AnalysisSource* source = module->module.sources + source_index;
            IrSourceId id = {
                .value = program->sources.count++,
            };
            program->sources.sources[id.value] = (IrSource){
                .path = source->path,
                .id = id,
            };
            ir_module->frontend_source_map[source_index] = id;
        }
        for (u32 entity_index = 0; entity_index < module->module.entity_count; entity_index += 1)
        {
            AnalysisEntity* entity = module->module.entities + entity_index;
            IrSourceId source =
                entity->source.value < ir_module->frontend_source_count ? ir_module->frontend_source_map[entity->source.value] : IR_SOURCE_ID_INVALID;
            IrSymbolId id = {
                .value = program->symbols.count++,
            };
            bool exported = entity->kind == ANALYSIS_ENTITY_CODE && entity->ast.code->exported;
            program->symbols.symbols[id.value] = (IrSymbol){
                .name = entity->name,
                .link_name = entity->name,
                .source = ir_source_range_from_parser(source, entity->range),
                .type = IR_TYPE_ID_INVALID,
                .id = id,
                .kind = ir_symbol_kind_from_analysis(entity->kind),
                .linkage = exported ? IR_LINKAGE_EXTERNAL : IR_LINKAGE_INTERNAL,
                .is_definition = entity->kind != ANALYSIS_ENTITY_CODE || entity->ast.code->has_body,
            };
            ir_module->frontend_symbol_map[entity_index] = id;
        }
        for (u32 type_index = 0; type_index < module->types.count; type_index += 1)
        {
            AnalysisType* analysis_type = module->types.types + type_index;
            IrTypeKind kind = ir_type_kind_from_analysis(analysis_type->kind);
            if (kind == IR_TYPE_COUNT)
            {
                continue;
            }
            IrTypeId id = {
                .value = program->types.count++,
            };
            ir_module->frontend_type_map[type_index] = id;
            program->types.types[id.value] = (IrType){
                .name = analysis_type->name,
                .id = id,
                .element_type = IR_TYPE_ID_INVALID,
                .return_type = IR_TYPE_ID_INVALID,
                .layout =
                    {
                        .size = analysis_type->layout.size,
                        .alignment = analysis_type->layout.alignment,
                        .abi_class = ir_abi_class_from_analysis(analysis_type->layout.abi_class),
                        .resolved = analysis_type->layout.state == ANALYSIS_LAYOUT_RESOLVED,
                    },
                .kind = kind,
            };
        }
    }

    for (u32 module_index = 0; module_index < analysis->module_count; module_index += 1)
    {
        AnalysisResult* module = analysis->module_results[module_index];
        if (!module)
        {
            continue;
        }
        IrModule* ir_module = program->modules + module_index;
        for (u32 type_index = 0; type_index < module->types.count; type_index += 1)
        {
            IrTypeId id = ir_module->frontend_type_map[type_index];
            IrType* type = ir_type_from_id(&program->types, id);
            if (!type)
            {
                continue;
            }
            AnalysisType* analysis_type = module->types.types + type_index;
            switch (analysis_type->kind)
            {
            case ANALYSIS_TYPE_INTEGER:
            {
                type->bit_width = analysis_type->as.integer.bit_width;
                type->is_signed = analysis_type->as.integer.is_signed;
            }
            break;
            case ANALYSIS_TYPE_FLOAT:
            {
                type->bit_width = analysis_type->as.float_bit_width;
            }
            break;
            case ANALYSIS_TYPE_POINTER:
            case ANALYSIS_TYPE_SLICE:
            case ANALYSIS_TYPE_RANGE:
            {
                type->element_type = ir_program_type_map(program, module->module.id, analysis_type->as.element_type);
            }
            break;
            case ANALYSIS_TYPE_ARRAY:
            {
                type->element_type = ir_program_type_map(program, module->module.id, analysis_type->as.array.element_type);
                type->element_count = analysis_type->as.array.count;
            }
            break;
            case ANALYSIS_TYPE_VECTOR:
            {
                type->element_type = ir_program_type_map(program, module->module.id, analysis_type->as.vector.element_type);
                type->element_count = analysis_type->as.vector.count;
            }
            break;
            case ANALYSIS_TYPE_FUNCTION:
            {
                type->parameter_count = analysis_type->as.function.argument_count;
                type->parameter_types = arena_allocate(arena, IrTypeId, type->parameter_count);
                for (u32 parameter_index = 0; parameter_index < type->parameter_count; parameter_index += 1)
                {
                    type->parameter_types[parameter_index] =
                        ir_program_type_map(program, module->module.id, analysis_type->as.function.argument_types[parameter_index]);
                }
                type->return_type = ir_program_type_map(program, module->module.id, analysis_type->as.function.return_type);
                type->calling_convention = ir_calling_convention_from_ast(analysis_type->as.function.calling_convention);
                type->is_variadic = analysis_type->as.function.is_variadic;
            }
            break;
            case ANALYSIS_TYPE_STRUCT:
            case ANALYSIS_TYPE_UNION:
            case ANALYSIS_TYPE_ENUM:
            {
                AnalysisEntityId declaration = analysis_type->as.declaration;
                if (declaration.module.value >= analysis->module_count)
                {
                    break;
                }
                AnalysisResult* declaration_module = analysis->module_results[declaration.module.value];
                if (!declaration_module || declaration.index.value >= declaration_module->module.entity_count)
                {
                    break;
                }
                IrModule* declaration_ir_module = program->modules + declaration.module.value;
                AnalysisEntity* declaration_entity = declaration_module->module.entities + declaration.index.value;
                AnalysisEntitySemantic* semantic = declaration_module->module.semantics + declaration.index.value;
                IrSourceId source = declaration_entity->source.value < declaration_ir_module->frontend_source_count
                                        ? declaration_ir_module->frontend_source_map[declaration_entity->source.value]
                                        : IR_SOURCE_ID_INVALID;
                type->field_count = semantic->field_count;
                type->fields = arena_allocate(arena, IrField, type->field_count);
                for (u32 field_index = 0; field_index < type->field_count; field_index += 1)
                {
                    AnalysisField* field = semantic->fields + field_index;
                    type->fields[field_index] = (IrField){
                        .name = field->name,
                        .source = ir_source_range_from_parser(source, field->range),
                        .type = ir_program_type_map(program, declaration.module, field->type),
                        .offset = field->offset,
                    };
                }
                type->enum_member_count = semantic->enum_member_count;
                type->enum_members = arena_allocate(arena, IrEnumMember, type->enum_member_count);
                for (u32 member_index = 0; member_index < type->enum_member_count; member_index += 1)
                {
                    AnalysisEnumMember* member = semantic->enum_members + member_index;
                    type->enum_members[member_index] = (IrEnumMember){
                        .name = member->name,
                        .source = ir_source_range_from_parser(source, member->range),
                        .value = member->value,
                    };
                }
            }
            break;
            case ANALYSIS_TYPE_VOID:
            case ANALYSIS_TYPE_BOOL:
            case ANALYSIS_TYPE_VA_LIST:
            case ANALYSIS_TYPE_POISON:
            case ANALYSIS_TYPE_COMPILE_TIME_PARAMETER:
            case ANALYSIS_TYPE_INFERRED_ARRAY:
            case ANALYSIS_TYPE_COUNT:
                break;
            }
        }
        for (u32 entity_index = 0; entity_index < module->module.entity_count; entity_index += 1)
        {
            AnalysisEntitySemantic* semantic = module->module.semantics + entity_index;
            IrSymbolId symbol_id = ir_module->frontend_symbol_map[entity_index];
            IrSymbol* symbol = ir_symbol_from_id(&program->symbols, symbol_id);
            symbol->type = ir_program_type_map(program, module->module.id, semantic->type);
        }
    }
}

BUSTER_GLOBAL_LOCAL IrSymbolId ir_program_symbol_from_analysis(IrProgram* program, AnalysisEntityId entity, AnalysisInstantiationId instantiation)
{
    if (instantiation.value == ANALYSIS_ID_UNDERLYING_INVALID)
    {
        if (entity.module.value >= program->module_count)
        {
            return IR_SYMBOL_ID_INVALID;
        }
        IrModule* module = program->modules + entity.module.value;
        if (entity.index.value >= module->frontend_symbol_count)
        {
            return IR_SYMBOL_ID_INVALID;
        }
        return module->frontend_symbol_map[entity.index.value];
    }
    for (u32 module_index = 0; module_index < program->module_count; module_index += 1)
    {
        IrModule* module = program->modules + module_index;
        for (u32 function_index = 0; function_index < module->function_count; function_index += 1)
        {
            IrFunction* function = module->functions + function_index;
            if (function->entity.module.value == entity.module.value && function->entity.index.value == entity.index.value &&
                function->instantiation.value == instantiation.value)
            {
                return function->symbol;
            }
        }
    }
    return IR_SYMBOL_ID_INVALID;
}

BUSTER_GLOBAL_LOCAL void ir_program_canonicalize_module(IrProgram* program, AnalysisProgram* analysis, u32 module_index)
{
    AnalysisResult* module_analysis = analysis->module_results[module_index];
    IrModule* module = program->modules + module_index;
    if (!module_analysis)
    {
        return;
    }
    for (u32 function_index = 0; function_index < module->function_count; function_index += 1)
    {
        IrFunction* function = module->functions + function_index;
        function->canonical_type = ir_program_type_map(program, module_analysis->module.id, function->type);
        IrSourceId source = IR_SOURCE_ID_INVALID;
        if (function->entity.index.value < module_analysis->module.entity_count)
        {
            AnalysisEntity* entity = module_analysis->module.entities + function->entity.index.value;
            if (entity->source.value < module->frontend_source_count)
            {
                source = module->frontend_source_map[entity->source.value];
            }
            function->source = ir_source_range_from_parser(source, entity->range);
        }
        AnalysisBody* debug_body = 0;
        if (function->instantiation.value == ANALYSIS_ID_UNDERLYING_INVALID)
        {
            if (function->entity.index.value < module_analysis->module.entity_count && module_analysis->module.bodies)
            {
                debug_body = module_analysis->module.bodies + function->entity.index.value;
            }
        }
        else
        {
            AnalysisInstantiation* instantiation = ir_instantiation_from_id(module_analysis, function->instantiation);
            if (instantiation)
            {
                debug_body = &instantiation->body;
            }
        }
        if (debug_body)
        {
            function->debug_local_count = debug_body->local_count;
            function->debug_locals = arena_allocate(program->arena, IrDebugLocal, function->debug_local_count);
            for (u32 local_index = 0; local_index < debug_body->local_count; local_index += 1)
            {
                AnalysisLocal* local = debug_body->locals + local_index;
                function->debug_locals[local_index] = (IrDebugLocal){
                    .name = local->name,
                    .source = ir_source_range_from_parser(source, local->range),
                    .type = ir_program_type_map(program, module_analysis->module.id, local->type),
                    .id = (IrLocalId){.value = local->id.value},
                    .scope_depth = local->scope_depth,
                    .is_parameter = local->kind == ANALYSIS_LOCAL_ARGUMENT,
                };
            }
        }
        for (u32 value_index = 0; value_index < function->value_count; value_index += 1)
        {
            IrValue* value = function->values + value_index;
            value->canonical_type = ir_program_type_map(program, module_analysis->module.id, value->type);
        }
        for (u32 block_index = 0; block_index < function->block_count; block_index += 1)
        {
            IrBlock* block = function->blocks + block_index;
            for (IrBlockParameter* parameter = block->first_parameter; parameter; parameter = parameter->next)
            {
                parameter->canonical_type = ir_program_type_map(program, module_analysis->module.id, parameter->type);
                parameter->canonical_local = parameter->local.value == ANALYSIS_ID_UNDERLYING_INVALID ? IR_LOCAL_ID_INVALID
                                                                                                      : (IrLocalId){
                                                                                                            .value = parameter->local.value,
                                                                                                        };
            }
        }
        for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
        {
            IrInstruction* instruction = function->instructions + instruction_index;
            instruction->canonical_type = ir_program_type_map(program, module_analysis->module.id, instruction->type);
            instruction->canonical_source = ir_source_range_from_parser(source, instruction->source);
            instruction->canonical_local = instruction->local.value == ANALYSIS_ID_UNDERLYING_INVALID ? IR_LOCAL_ID_INVALID
                                                                                                      : (IrLocalId){
                                                                                                            .value = instruction->local.value,
                                                                                                        };
            instruction->symbol = ir_program_symbol_from_analysis(program, instruction->entity, instruction->instantiation);
        }
    }
}

IrProgram ir_generate_program(Arena* result_arena, AnalysisProgram* analysis)
{
    IrProgram program = {
        .arena = result_arena,
        .data_layout = target_data_layout(target_native),
        .module_count = analysis->module_count,
    };
    for (u32 module_index = 0; module_index < analysis->module_count; module_index += 1)
    {
        AnalysisResult* module = analysis->module_results[module_index];
        if (module && target_data_layout_is_valid(module->data_layout))
        {
            program.data_layout = module->data_layout;
            break;
        }
    }
    program.modules = arena_allocate(result_arena, IrModule, program.module_count);
    ir_program_metadata_initialize(result_arena, analysis, &program);
    for (u32 module_index = 0; module_index < program.module_count; module_index += 1)
    {
        AnalysisResult* module = analysis->module_results[module_index];
        if (!module)
        {
            continue;
        }
        IrModule metadata = program.modules[module_index];
        IrModule generated = ir_generate_module(result_arena, module);
        generated.frontend_type_map = metadata.frontend_type_map;
        generated.frontend_symbol_map = metadata.frontend_symbol_map;
        generated.frontend_source_map = metadata.frontend_source_map;
        generated.frontend_type_count = metadata.frontend_type_count;
        generated.frontend_symbol_count = metadata.frontend_symbol_count;
        generated.frontend_source_count = metadata.frontend_source_count;
        program.modules[module_index] = generated;
        for (u32 function_index = 0; function_index < generated.function_count; function_index += 1)
        {
            IrFunction* function = generated.functions + function_index;
            if (function->instantiation.value == ANALYSIS_ID_UNDERLYING_INVALID && function->entity.index.value < generated.frontend_symbol_count)
            {
                function->symbol = generated.frontend_symbol_map[function->entity.index.value];
            }
            else if (function->instantiation.value != ANALYSIS_ID_UNDERLYING_INVALID)
            {
                IrSourceId source = IR_SOURCE_ID_INVALID;
                if (function->entity.index.value < module->module.entity_count)
                {
                    AnalysisEntity* entity = module->module.entities + function->entity.index.value;
                    if (entity->source.value < generated.frontend_source_count)
                    {
                        source = generated.frontend_source_map[entity->source.value];
                    }
                }
                IrSymbolId symbol = {
                    .value = program.symbols.count++,
                };
                BUSTER_CHECK(symbol.value < program.symbols.capacity);
                program.symbols.symbols[symbol.value] = (IrSymbol){
                    .name = function->name,
                    .link_name = function->name,
                    .source = ir_source_range_from_parser(source, module->module.entities[function->entity.index.value].range),
                    .type = ir_program_type_map(&program, module->module.id, function->type),
                    .id = symbol,
                    .kind = IR_SYMBOL_FUNCTION,
                    .linkage = IR_LINKAGE_INTERNAL,
                    .is_definition = function->state != IR_FUNCTION_DECLARATION,
                };
                function->symbol = symbol;
            }
        }
        program.lowered_function_count += program.modules[module_index].lowered_function_count;
        program.rejected_function_count += program.modules[module_index].rejected_function_count;
    }
    for (u32 module_index = 0; module_index < program.module_count; module_index += 1)
    {
        ir_program_canonicalize_module(&program, analysis, module_index);
    }
    return program;
}

BUSTER_GLOBAL_LOCAL IrValidationResult ir_validation_error(IrValidationError error, IrFunction* function, IrBlockId block, IrInstructionId instruction)
{
    return (IrValidationResult){
        .error = error,
        .function = function->id,
        .block = block,
        .instruction = instruction,
    };
}

BUSTER_GLOBAL_LOCAL bool ir_canonical_conversion_valid(IrType* source, IrType* destination, IrConversionOperation operation)
{
    if (!source || !destination)
    {
        return false;
    }
    if (source->id.value == destination->id.value)
    {
        return operation == IR_CONVERSION_IDENTITY;
    }
    if (source->kind == IR_TYPE_BOOLEAN && destination->kind == IR_TYPE_INTEGER)
    {
        return operation == IR_CONVERSION_INTEGER_ZERO_EXTEND;
    }
    if (source->kind == IR_TYPE_INTEGER && destination->kind == IR_TYPE_INTEGER)
    {
        IrConversionOperation expected = source->bit_width < destination->bit_width
                                             ? (source->is_signed ? IR_CONVERSION_INTEGER_SIGN_EXTEND : IR_CONVERSION_INTEGER_ZERO_EXTEND)
                                         : source->bit_width > destination->bit_width ? IR_CONVERSION_INTEGER_TRUNCATE
                                                                                      : IR_CONVERSION_INTEGER_REINTERPRET;
        return operation == expected;
    }
    if (source->kind == IR_TYPE_FLOAT && destination->kind == IR_TYPE_FLOAT)
    {
        return operation == (source->bit_width < destination->bit_width   ? IR_CONVERSION_FLOAT_EXTEND
                             : source->bit_width > destination->bit_width ? IR_CONVERSION_FLOAT_TRUNCATE
                                                                          : IR_CONVERSION_IDENTITY);
    }
    if (source->kind == IR_TYPE_INTEGER && destination->kind == IR_TYPE_FLOAT)
    {
        return operation == (source->is_signed ? IR_CONVERSION_SIGNED_INTEGER_TO_FLOAT : IR_CONVERSION_UNSIGNED_INTEGER_TO_FLOAT);
    }
    if (source->kind == IR_TYPE_FLOAT && destination->kind == IR_TYPE_INTEGER)
    {
        return operation == (destination->is_signed ? IR_CONVERSION_FLOAT_TO_SIGNED_INTEGER : IR_CONVERSION_FLOAT_TO_UNSIGNED_INTEGER);
    }
    if (source->kind == IR_TYPE_POINTER && destination->kind == IR_TYPE_POINTER)
    {
        return operation == IR_CONVERSION_POINTER_REINTERPRET;
    }
    if (source->kind == IR_TYPE_POINTER && destination->kind == IR_TYPE_INTEGER)
    {
        return operation == IR_CONVERSION_POINTER_TO_INTEGER;
    }
    if (source->kind == IR_TYPE_INTEGER && destination->kind == IR_TYPE_POINTER)
    {
        return operation == IR_CONVERSION_INTEGER_TO_POINTER;
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool ir_instruction_operation_valid(AnalysisResult* analysis, IrFunction* function, IrInstruction* instruction)
{
    if (!analysis || !function || !instruction || instruction->opcode >= IR_OPCODE_COUNT)
    {
        return false;
    }
    if (instruction->opcode == IR_OPCODE_DEBUG_TRAP)
    {
        return instruction->operand_count == 0 && instruction->immediate_count == 0 && instruction->result.value == IR_ID_UNDERLYING_INVALID &&
               ir_type_id_equal(instruction->type, analysis->types.builtin.void_type);
    }
    if (instruction->opcode == IR_OPCODE_LABEL_ADDRESS)
    {
        AnalysisType* type = analysis_type_from_id(analysis, instruction->type);
        bool result_in_range = instruction->result.value < function->value_count;
        IrValueLabelMetadata result_metadata = result_in_range ? ir_value_label_metadata(function, instruction->result) : (IrValueLabelMetadata){0};
        IrValueLabelMetadata* result = result_in_range ? &result_metadata : 0;
        return type && type->kind == ANALYSIS_TYPE_POINTER && instruction->operand_count == 0 && instruction->target_count == 1 &&
               instruction->targets && instruction->targets[0].value < function->block_count && instruction->immediate_count == 0 &&
               instruction->result.value != IR_ID_UNDERLYING_INVALID && instruction->result.value < function->value_count &&
               ir_analysis_void_pointer_type(analysis, instruction->type) && result && !result->has_non_label_provenance && !result->has_label_provenance &&
               !result->label_paths && !result->label_path_count && ir_label_provenance_valid(result) && result->label_block_count == 1 && result->label_blocks &&
               result->label_blocks[0].value == instruction->targets[0].value;
    }
    if (instruction->opcode == IR_OPCODE_INDIRECT_BRANCH)
    {
        AnalysisType* type = analysis_type_from_id(analysis, instruction->type);
        IrValue* operand_slot = instruction->operand_count == 1 && instruction->operands && instruction->operands[0].value < function->value_count
                                    ? function->values + instruction->operands[0].value
                                    : 0;
        IrValueLabelMetadata operand_metadata = operand_slot ? ir_value_label_metadata(function, instruction->operands[0]) : (IrValueLabelMetadata){0};
        IrValueLabelMetadata* operand = operand_slot ? &operand_metadata : 0;
        AnalysisType* operand_type = operand_slot ? analysis_type_from_id(analysis, operand_slot->type) : 0;
        bool label_targets = operand && ir_label_provenance_valid(operand) && instruction->targets && instruction->target_count == operand->label_block_count &&
                             instruction->target_count != 0 && ir_block_id_array_unique(instruction->targets, instruction->target_count);
        for (u32 label_index = 0; label_targets && label_index < operand->label_block_count; label_index += 1)
        {
            bool found = false;
            for (u32 target_index = 0; target_index < instruction->target_count; target_index += 1)
            {
                found |= instruction->targets[target_index].value == operand->label_blocks[label_index].value;
            }
            label_targets &= operand->label_blocks[label_index].value < function->block_count && found;
        }
        for (u32 target_index = 0; label_targets && target_index < instruction->target_count; target_index += 1)
        {
            bool found = false;
            for (u32 label_index = 0; label_index < operand->label_block_count; label_index += 1)
            {
                found |= instruction->targets[target_index].value == operand->label_blocks[label_index].value;
            }
            label_targets &= found;
        }
        return type && type->kind == ANALYSIS_TYPE_VOID && operand_type && ir_analysis_void_pointer_type(analysis, operand_slot->type) && !operand->has_non_label_provenance && label_targets &&
               instruction->operand_count == 1 && instruction->target_count != 0 && instruction->immediate_count == 0 &&
               instruction->result.value == IR_ID_UNDERLYING_INVALID;
    }
    if (instruction->opcode == IR_OPCODE_INLINE_ASSEMBLY)
    {
        return ir_analysis_inline_assembly_valid(analysis, function, instruction);
    }
    if (instruction->opcode == IR_OPCODE_LENGTH || instruction->opcode == IR_OPCODE_REVERSE)
    {
        if (instruction->operand_count != 1 || instruction->conversion_operation != IR_CONVERSION_COUNT || instruction->unary_operation != IR_UNARY_COUNT ||
            instruction->binary_operation != IR_BINARY_COUNT)
        {
            return false;
        }
        AnalysisTypeId operand_type = function->values[instruction->operands[0].value].type;
        AnalysisTypeKind kind = analysis_type_from_id(analysis, operand_type)->kind;
        bool iterable = kind == ANALYSIS_TYPE_RANGE || kind == ANALYSIS_TYPE_SLICE || kind == ANALYSIS_TYPE_ARRAY || kind == ANALYSIS_TYPE_INFERRED_ARRAY;
        return iterable && (instruction->opcode == IR_OPCODE_LENGTH ? ir_type_id_equal(instruction->type, analysis->types.builtin.u64_type)
                                                                    : ir_type_id_equal(instruction->type, operand_type));
    }
    if (instruction->opcode == IR_OPCODE_INDEX)
    {
        if (instruction->operand_count != 2 || instruction->conversion_operation != IR_CONVERSION_COUNT || instruction->unary_operation != IR_UNARY_COUNT ||
            instruction->binary_operation != IR_BINARY_COUNT)
        {
            return false;
        }
        AnalysisTypeId base_type = function->values[instruction->operands[0].value].type;
        AnalysisTypeId index_type = function->values[instruction->operands[1].value].type;
        AnalysisType* base = analysis_type_from_id(analysis, base_type);
        AnalysisTypeId element = ANALYSIS_TYPE_ID_INVALID;
        if (base->kind == ANALYSIS_TYPE_ARRAY)
        {
            element = base->as.array.element_type;
        }
        else if (base->kind == ANALYSIS_TYPE_VECTOR)
        {
            element = base->as.vector.element_type;
        }
        else if (base->kind == ANALYSIS_TYPE_RANGE || base->kind == ANALYSIS_TYPE_SLICE || base->kind == ANALYSIS_TYPE_INFERRED_ARRAY)
        {
            element = base->as.element_type;
        }
        return element.value != ANALYSIS_ID_UNDERLYING_INVALID && ir_type_is_integer_domain(analysis, index_type) &&
               ir_type_id_equal(instruction->type, element);
    }
    if (instruction->opcode == IR_OPCODE_CAST)
    {
        if (instruction->operand_count != 1 || instruction->conversion_operation >= IR_CONVERSION_COUNT || instruction->unary_operation != IR_UNARY_COUNT ||
            instruction->binary_operation != IR_BINARY_COUNT)
        {
            return false;
        }
        AnalysisTypeId source = function->values[instruction->operands[0].value].type;
        return instruction->conversion_operation == ir_conversion_operation(analysis, source, instruction->type);
    }
    if (instruction->opcode == IR_OPCODE_VA_START)
    {
        AnalysisType* signature = analysis_type_from_id(analysis, function->type);
        return instruction->operand_count == 0 && signature->kind == ANALYSIS_TYPE_FUNCTION && signature->as.function.is_variadic &&
               ir_type_id_equal(instruction->type, analysis->types.builtin.va_list_type);
    }
    if (instruction->opcode == IR_OPCODE_VA_COPY || instruction->opcode == IR_OPCODE_VA_END || instruction->opcode == IR_OPCODE_VA_ARG)
    {
        if (instruction->operand_count != 1)
        {
            return false;
        }
        AnalysisType* operand = analysis_type_from_id(analysis, function->values[instruction->operands[0].value].type);
        if (operand->kind != ANALYSIS_TYPE_POINTER || !ir_type_id_equal(operand->as.element_type, analysis->types.builtin.va_list_type))
        {
            return false;
        }
        return instruction->opcode == IR_OPCODE_VA_COPY  ? ir_type_id_equal(instruction->type, analysis->types.builtin.va_list_type)
               : instruction->opcode == IR_OPCODE_VA_END ? ir_type_id_equal(instruction->type, analysis->types.builtin.void_type)
                                                         : !ir_type_id_equal(instruction->type, analysis->types.builtin.void_type);
    }
    if (instruction->conversion_operation != IR_CONVERSION_COUNT)
    {
        return false;
    }
    if (instruction->opcode == IR_OPCODE_UNARY)
    {
        if (instruction->operand_count != 1 || instruction->unary_operation >= IR_UNARY_COUNT || instruction->binary_operation != IR_BINARY_COUNT)
        {
            return false;
        }
        AnalysisTypeId operand_type = function->values[instruction->operands[0].value].type;
        AnalysisTypeKind kind = analysis_type_from_id(analysis, operand_type)->kind;
        AnalysisTypeKind vector_element_kind = ANALYSIS_TYPE_COUNT;
        if (kind == ANALYSIS_TYPE_VECTOR)
        {
            AnalysisType* vector = analysis_type_from_id(analysis, operand_type);
            vector_element_kind = analysis_type_from_id(analysis, vector->as.vector.element_type)->kind;
        }
        bool domain_matches =
            (instruction->unary_operation == IR_UNARY_INTEGER_NEGATE && kind == ANALYSIS_TYPE_INTEGER) ||
            (instruction->unary_operation == IR_UNARY_FLOAT_NEGATE && kind == ANALYSIS_TYPE_FLOAT) ||
            (instruction->unary_operation == IR_UNARY_INTEGER_BITWISE_NOT && kind == ANALYSIS_TYPE_INTEGER) ||
            ((instruction->unary_operation == IR_UNARY_INTEGER_COUNT_LEADING_ZEROS || instruction->unary_operation == IR_UNARY_INTEGER_COUNT_TRAILING_ZEROS) &&
             kind == ANALYSIS_TYPE_INTEGER) ||
            (instruction->unary_operation == IR_UNARY_BOOLEAN_NOT && kind == ANALYSIS_TYPE_BOOL) ||
            (instruction->unary_operation == IR_UNARY_VECTOR_INTEGER_NEGATE && kind == ANALYSIS_TYPE_VECTOR && vector_element_kind == ANALYSIS_TYPE_INTEGER) ||
            (instruction->unary_operation == IR_UNARY_VECTOR_FLOAT_NEGATE && kind == ANALYSIS_TYPE_VECTOR && vector_element_kind == ANALYSIS_TYPE_FLOAT) ||
            (instruction->unary_operation == IR_UNARY_VECTOR_INTEGER_BITWISE_NOT && kind == ANALYSIS_TYPE_VECTOR &&
             vector_element_kind == ANALYSIS_TYPE_INTEGER);
        return domain_matches && ir_type_id_equal(instruction->type, operand_type) && instruction->unary_operation < IR_UNARY_COUNT &&
               instruction->binary_operation == IR_BINARY_COUNT;
    }
    if (instruction->opcode == IR_OPCODE_BINARY)
    {
        if (instruction->operand_count != 2 || instruction->unary_operation != IR_UNARY_COUNT || instruction->binary_operation >= IR_BINARY_COUNT)
        {
            return false;
        }
        AnalysisTypeId left_type = function->values[instruction->operands[0].value].type;
        AnalysisTypeId right_type = function->values[instruction->operands[1].value].type;
        if (!ir_type_id_equal(left_type, right_type))
        {
            return false;
        }
        AnalysisTypeKind operand_kind = analysis_type_from_id(analysis, left_type)->kind;
        IrBinaryOperation operation = instruction->binary_operation;
        bool integer_operation = operation <= IR_BINARY_UNSIGNED_DIVIDE ||
                                 (operation >= IR_BINARY_SIGNED_REMAINDER && operation <= IR_BINARY_INTEGER_BITWISE_XOR) ||
                                 (operation >= IR_BINARY_SIGNED_LESS && operation <= IR_BINARY_UNSIGNED_GREATER_EQUAL);
        bool float_operation = (operation >= IR_BINARY_FLOAT_ADD && operation <= IR_BINARY_FLOAT_DIVIDE) ||
                               (operation >= IR_BINARY_FLOAT_EQUAL && operation <= IR_BINARY_FLOAT_NOT_EQUAL) ||
                               (operation >= IR_BINARY_FLOAT_LESS && operation <= IR_BINARY_FLOAT_GREATER_EQUAL);
        bool boolean_operation = (operation >= IR_BINARY_BOOLEAN_AND && operation <= IR_BINARY_BOOLEAN_OR) ||
                                 (operation >= IR_BINARY_BOOLEAN_EQUAL && operation <= IR_BINARY_BOOLEAN_NOT_EQUAL);
        bool pointer_operation = operation >= IR_BINARY_POINTER_EQUAL && operation <= IR_BINARY_POINTER_NOT_EQUAL;
        bool integer_equality = operation >= IR_BINARY_INTEGER_EQUAL && operation <= IR_BINARY_INTEGER_NOT_EQUAL;
        bool signed_semantics = operation == IR_BINARY_SIGNED_DIVIDE || operation == IR_BINARY_SIGNED_REMAINDER || operation == IR_BINARY_SIGNED_SHIFT_RIGHT ||
                                (operation >= IR_BINARY_SIGNED_LESS && operation <= IR_BINARY_SIGNED_GREATER_EQUAL);
        bool unsigned_semantics = operation == IR_BINARY_UNSIGNED_DIVIDE || operation == IR_BINARY_UNSIGNED_REMAINDER ||
                                  operation == IR_BINARY_UNSIGNED_SHIFT_RIGHT ||
                                  (operation >= IR_BINARY_UNSIGNED_LESS && operation <= IR_BINARY_UNSIGNED_GREATER_EQUAL);
        bool signedness_matches = operand_kind != ANALYSIS_TYPE_INTEGER || (!signed_semantics && !unsigned_semantics) ||
                                  (signed_semantics == analysis_type_from_id(analysis, left_type)->as.integer.is_signed);
        bool comparison = operation >= IR_BINARY_INTEGER_EQUAL && operation <= IR_BINARY_FLOAT_GREATER_EQUAL;
        bool range = operation == IR_BINARY_RANGE;
        bool vector_operation = operation >= IR_BINARY_VECTOR_INTEGER_ADD && operation <= IR_BINARY_VECTOR_FLOAT_GREATER_EQUAL;
        AnalysisTypeKind vector_element_kind = ANALYSIS_TYPE_COUNT;
        if (operand_kind == ANALYSIS_TYPE_VECTOR)
        {
            AnalysisType* vector = analysis_type_from_id(analysis, left_type);
            vector_element_kind = analysis_type_from_id(analysis, vector->as.vector.element_type)->kind;
        }
        bool vector_float_operation = (operation >= IR_BINARY_VECTOR_FLOAT_ADD && operation <= IR_BINARY_VECTOR_FLOAT_DIVIDE) ||
                                      (operation >= IR_BINARY_VECTOR_FLOAT_EQUAL && operation <= IR_BINARY_VECTOR_FLOAT_GREATER_EQUAL);
        bool vector_signed_semantics = operation == IR_BINARY_VECTOR_SIGNED_DIVIDE || operation == IR_BINARY_VECTOR_SIGNED_REMAINDER ||
                                       operation == IR_BINARY_VECTOR_SIGNED_SHIFT_RIGHT ||
                                       (operation >= IR_BINARY_VECTOR_SIGNED_LESS && operation <= IR_BINARY_VECTOR_SIGNED_GREATER_EQUAL);
        bool vector_unsigned_semantics = operation == IR_BINARY_VECTOR_UNSIGNED_DIVIDE || operation == IR_BINARY_VECTOR_UNSIGNED_REMAINDER ||
                                         operation == IR_BINARY_VECTOR_UNSIGNED_SHIFT_RIGHT ||
                                         (operation >= IR_BINARY_VECTOR_UNSIGNED_LESS && operation <= IR_BINARY_VECTOR_UNSIGNED_GREATER_EQUAL);
        if (operand_kind == ANALYSIS_TYPE_VECTOR && vector_element_kind == ANALYSIS_TYPE_INTEGER)
        {
            AnalysisType* vector = analysis_type_from_id(analysis, left_type);
            AnalysisType* element = analysis_type_from_id(analysis, vector->as.vector.element_type);
            signedness_matches = (!vector_signed_semantics && !vector_unsigned_semantics) || (vector_signed_semantics == element->as.integer.is_signed);
        }
        bool domain_matches = (integer_operation && operand_kind == ANALYSIS_TYPE_INTEGER) ||
                              (integer_equality && (operand_kind == ANALYSIS_TYPE_INTEGER || operand_kind == ANALYSIS_TYPE_ENUM)) ||
                              (float_operation && operand_kind == ANALYSIS_TYPE_FLOAT) || (boolean_operation && operand_kind == ANALYSIS_TYPE_BOOL) ||
                              (pointer_operation && operand_kind == ANALYSIS_TYPE_POINTER) || (range && operand_kind == ANALYSIS_TYPE_INTEGER) ||
                              (vector_operation && operand_kind == ANALYSIS_TYPE_VECTOR &&
                               (vector_float_operation ? vector_element_kind == ANALYSIS_TYPE_FLOAT : vector_element_kind == ANALYSIS_TYPE_INTEGER));
        AnalysisTypeKind result_kind = analysis_type_from_id(analysis, instruction->type)->kind;
        bool vector_comparison = operation >= IR_BINARY_VECTOR_INTEGER_EQUAL && operation <= IR_BINARY_VECTOR_FLOAT_GREATER_EQUAL;
        bool vector_mask_result = false;
        if (vector_comparison && operand_kind == ANALYSIS_TYPE_VECTOR && result_kind == ANALYSIS_TYPE_VECTOR)
        {
            AnalysisType* operand_vector = analysis_type_from_id(analysis, left_type);
            AnalysisType* result_vector = analysis_type_from_id(analysis, instruction->type);
            AnalysisType* operand_element = analysis_type_from_id(analysis, operand_vector->as.vector.element_type);
            AnalysisType* result_element = analysis_type_from_id(analysis, result_vector->as.vector.element_type);
            u32 operand_width = operand_element->kind == ANALYSIS_TYPE_FLOAT ? operand_element->as.float_bit_width : operand_element->as.integer.bit_width;
            vector_mask_result = operand_vector->as.vector.count == result_vector->as.vector.count && result_element->kind == ANALYSIS_TYPE_INTEGER &&
                                 !result_element->as.integer.is_signed && result_element->as.integer.bit_width == operand_width;
        }
        bool result_matches =
            vector_comparison ? vector_mask_result
            : comparison      ? result_kind == ANALYSIS_TYPE_BOOL
            : range ? result_kind == ANALYSIS_TYPE_RANGE && ir_type_id_equal(analysis_type_from_id(analysis, instruction->type)->as.element_type, left_type)
                    : ir_type_id_equal(instruction->type, left_type);
        return domain_matches && signedness_matches && result_matches;
    }
    return instruction->unary_operation == IR_UNARY_COUNT && instruction->binary_operation == IR_BINARY_COUNT;
}

IrValidationResult ir_validate_module(AnalysisResult* analysis, IrModule* module)
{
    IrValidationResult result = {
        .function = IR_FUNCTION_ID_INVALID,
        .block = IR_BLOCK_ID_INVALID,
        .instruction = IR_INSTRUCTION_ID_INVALID,
    };
    for (u32 function_index = 0; function_index < module->function_count; function_index += 1)
    {
        IrFunction* function = module->functions + function_index;
        if (function->state != IR_FUNCTION_LOWERED)
        {
            continue;
        }
        if (!ir_block_id_valid(function, function->entry))
        {
            return ir_validation_error(IR_VALIDATION_INVALID_ID, function, IR_BLOCK_ID_INVALID, IR_INSTRUCTION_ID_INVALID);
        }
        AnalysisType* function_type = analysis_type_from_id(analysis, function->type);
        for (u32 value_index = 0; value_index < function->value_count; value_index += 1)
        {
            IrValue* value = function->values + value_index;
            IrValueId value_id = {.value = value_index};
            IrValueLabelMetadata metadata = ir_value_label_metadata(function, value_id);
            if ((metadata.is_label_value && !metadata.has_label_provenance && !metadata.has_non_label_provenance && !ir_label_provenance_valid(&metadata)) ||
                (metadata.has_label_provenance && !ir_label_storage_provenance_valid(&metadata)) ||
                !ir_label_metadata_transfer_valid(0, function, value_id) || !ir_label_metadata_shape_valid(0, function, value_id))
            {
                return ir_validation_error(IR_VALIDATION_OPERATION, function, IR_BLOCK_ID_INVALID, value->definition);
            }
            if (ir_label_provenance_valid(&metadata) || ir_label_storage_provenance_valid(&metadata))
            {
                for (u32 label_index = 0; label_index < metadata.label_block_count; label_index += 1)
                {
                    if (metadata.label_blocks[label_index].value >= function->block_count)
                    {
                        return ir_validation_error(IR_VALIDATION_BRANCH_TARGET, function, IR_BLOCK_ID_INVALID, value->definition);
                    }
                }
            }
        }
        for (u32 block_index = 0; block_index < function->block_count; block_index += 1)
        {
            IrBlock* block = function->blocks + block_index;
            if (!block->terminated)
            {
                return ir_validation_error(IR_VALIDATION_UNTERMINATED_BLOCK, function, block->id, block->last_instruction);
            }
            if (!block->sealed)
            {
                return ir_validation_error(IR_VALIDATION_BLOCK_PARAMETER, function, block->id, IR_INSTRUCTION_ID_INVALID);
            }
            for (IrBlockParameter* parameter = block->first_parameter; parameter; parameter = parameter->next)
            {
                if (!ir_value_id_valid(function, parameter->value) || parameter->incoming_count != block->predecessor_count ||
                    !ir_type_id_equal(function->values[parameter->value.value].type, parameter->type))
                {
                    return ir_validation_error(IR_VALIDATION_BLOCK_PARAMETER, function, block->id, IR_INSTRUCTION_ID_INVALID);
                }
                IrIncoming* incoming = parameter->first_incoming;
                IrPredecessor* predecessor = block->first_predecessor;
                while (incoming && predecessor)
                {
                    if (incoming->predecessor.value != predecessor->block.value || !ir_value_id_valid(function, incoming->value) ||
                        !ir_type_id_equal(function->values[incoming->value.value].type, parameter->type))
                    {
                        return ir_validation_error(IR_VALIDATION_BLOCK_PARAMETER, function, block->id, IR_INSTRUCTION_ID_INVALID);
                    }
                    incoming = incoming->next;
                    predecessor = predecessor->next;
                }
                if (incoming || predecessor)
                {
                    return ir_validation_error(IR_VALIDATION_BLOCK_PARAMETER, function, block->id, IR_INSTRUCTION_ID_INVALID);
                }
                if (!ir_label_block_parameter_provenance_valid(function, parameter))
                {
                    return ir_validation_error(IR_VALIDATION_OPERATION, function, block->id, IR_INSTRUCTION_ID_INVALID);
                }
            }
            IrInstructionId instruction_id = block->first_instruction;
            bool saw_terminator = false;
            while (instruction_id.value != IR_ID_UNDERLYING_INVALID)
            {
                if (instruction_id.value >= function->instruction_count)
                {
                    return ir_validation_error(IR_VALIDATION_INVALID_ID, function, block->id, instruction_id);
                }
                IrInstruction* instruction = function->instructions + instruction_id.value;
                if (saw_terminator)
                {
                    return ir_validation_error(IR_VALIDATION_INSTRUCTION_AFTER_TERMINATOR, function, block->id, instruction_id);
                }
                if ((instruction->operand_count && !instruction->operands) || (instruction->target_count && !instruction->targets) ||
                    (instruction->immediate_count && !instruction->immediates))
                {
                    return ir_validation_error(IR_VALIDATION_OPERATION, function, block->id, instruction_id);
                }
                for (u32 operand_index = 0; operand_index < instruction->operand_count; operand_index += 1)
                {
                    if (!ir_value_id_valid(function, instruction->operands[operand_index]))
                    {
                        return ir_validation_error(IR_VALIDATION_INVALID_ID, function, block->id, instruction_id);
                    }
                }
                if (!ir_instruction_operation_valid(analysis, function, instruction))
                {
                    return ir_validation_error(IR_VALIDATION_OPERATION, function, block->id, instruction_id);
                }
                for (u32 target_index = 0; target_index < instruction->target_count; target_index += 1)
                {
                    if (!ir_block_id_valid(function, instruction->targets[target_index]))
                    {
                        return ir_validation_error(IR_VALIDATION_BRANCH_TARGET, function, block->id, instruction_id);
                    }
                }
                if (instruction->result.value != IR_ID_UNDERLYING_INVALID)
                {
                    if (!ir_value_id_valid(function, instruction->result) ||
                        !ir_type_id_equal(function->values[instruction->result.value].type, instruction->type))
                    {
                        return ir_validation_error(IR_VALIDATION_RESULT_TYPE, function, block->id, instruction_id);
                    }
                }
                if (instruction->opcode == IR_OPCODE_LOAD)
                {
                    IrValue* place = function->values + instruction->operands[0].value;
                    if (place->category != IR_VALUE_PLACE || !ir_type_id_equal(place->type, instruction->type))
                    {
                        return ir_validation_error(IR_VALIDATION_OPERAND_TYPE, function, block->id, instruction_id);
                    }
                }
                else if (instruction->opcode == IR_OPCODE_STORE)
                {
                    IrValue* place = function->values + instruction->operands[0].value;
                    IrValue* value = function->values + instruction->operands[1].value;
                    if (place->category != IR_VALUE_PLACE || !ir_type_id_equal(place->type, value->type))
                    {
                        return ir_validation_error(IR_VALIDATION_OPERAND_TYPE, function, block->id, instruction_id);
                    }
                }
                else if (instruction->opcode == IR_OPCODE_CALL)
                {
                    if (!instruction->operand_count || instruction->entity.module.value == ANALYSIS_ID_UNDERLYING_INVALID ||
                        instruction->entity.index.value == ANALYSIS_ID_UNDERLYING_INVALID)
                    {
                        return ir_validation_error(IR_VALIDATION_CALL_TARGET, function, block->id, instruction_id);
                    }
                    IrValue* callee = function->values + instruction->operands[0].value;
                    AnalysisType* signature = analysis_type_from_id(analysis, callee->type);
                    u32 call_argument_count = instruction->operand_count - 1;
                    if (signature->kind != ANALYSIS_TYPE_FUNCTION ||
                        (!signature->as.function.is_variadic && call_argument_count != signature->as.function.argument_count) ||
                        (signature->as.function.is_variadic && call_argument_count < signature->as.function.argument_count) ||
                        !ir_type_id_equal(instruction->type, signature->as.function.return_type))
                    {
                        return ir_validation_error(IR_VALIDATION_CALL_SIGNATURE, function, block->id, instruction_id);
                    }
                    for (u32 argument_index = 0; argument_index < signature->as.function.argument_count; argument_index += 1)
                    {
                        IrValue* argument = function->values + instruction->operands[argument_index + 1].value;
                        if (!ir_type_id_equal(argument->type, signature->as.function.argument_types[argument_index]))
                        {
                            return ir_validation_error(IR_VALIDATION_CALL_SIGNATURE, function, block->id, instruction_id);
                        }
                    }
                    if (callee->definition.value == IR_ID_UNDERLYING_INVALID)
                    {
                        return ir_validation_error(IR_VALIDATION_CALL_TARGET, function, block->id, instruction_id);
                    }
                    IrInstruction* reference = function->instructions + callee->definition.value;
                    if (reference->opcode != IR_OPCODE_FUNCTION || !ir_entity_id_equal(reference->entity, instruction->entity) ||
                        reference->instantiation.value != instruction->instantiation.value)
                    {
                        return ir_validation_error(IR_VALIDATION_CALL_TARGET, function, block->id, instruction_id);
                    }
                    AnalysisResult* target_module = ir_analysis_module_from_id(analysis, instruction->entity.module);
                    if (!target_module || instruction->entity.index.value >= target_module->module.entity_count)
                    {
                        return ir_validation_error(IR_VALIDATION_CALL_TARGET, function, block->id, instruction_id);
                    }
                    AnalysisEntity* target = target_module->module.entities + instruction->entity.index.value;
                    if (target->kind != ANALYSIS_ENTITY_CODE)
                    {
                        return ir_validation_error(IR_VALIDATION_CALL_TARGET, function, block->id, instruction_id);
                    }
                    if (instruction->instantiation.value != ANALYSIS_ID_UNDERLYING_INVALID)
                    {
                        AnalysisInstantiation* instantiation = ir_instantiation_from_id(target_module, instruction->instantiation);
                        if (!instantiation || instantiation->generic_entity.module.value != instruction->entity.module.value ||
                            instantiation->generic_entity.index.value != instruction->entity.index.value ||
                            instantiation->codegen_owner.value != target_module->module.id.value)
                        {
                            return ir_validation_error(IR_VALIDATION_CALL_TARGET, function, block->id, instruction_id);
                        }
                    }
                    else
                    {
                        TemporalArena validation_scratch = scratch_begin(0, 0);
                        bool generic = analysis_entity_is_generic(validation_scratch.arena, target_module, target);
                        scratch_end(validation_scratch);
                        if (generic)
                        {
                            return ir_validation_error(IR_VALIDATION_CALL_TARGET, function, block->id, instruction_id);
                        }
                    }
                }
                else if (instruction->opcode == IR_OPCODE_RETURN)
                {
                    AnalysisTypeId return_type = function_type->as.function.return_type;
                    AnalysisTypeKind return_kind = analysis_type_from_id(analysis, return_type)->kind;
                    bool valid_return =
                        return_kind == ANALYSIS_TYPE_VOID
                            ? instruction->operand_count == 0
                            : instruction->operand_count == 1 && ir_type_id_equal(function->values[instruction->operands[0].value].type, return_type);
                    if (!valid_return)
                    {
                        return ir_validation_error(IR_VALIDATION_RETURN_TYPE, function, block->id, instruction_id);
                    }
                }
                bool terminator = instruction->opcode == IR_OPCODE_BRANCH || instruction->opcode == IR_OPCODE_BRANCH_IF ||
                                  instruction->opcode == IR_OPCODE_SWITCH || instruction->opcode == IR_OPCODE_INDIRECT_BRANCH ||
                                  instruction->opcode == IR_OPCODE_RETURN || instruction->opcode == IR_OPCODE_UNREACHABLE ||
                                  (instruction->opcode == IR_OPCODE_INLINE_ASSEMBLY && instruction->target_count != 0);
                saw_terminator = terminator;
                instruction_id = instruction->next;
            }
            if (!saw_terminator)
            {
                return ir_validation_error(IR_VALIDATION_UNTERMINATED_BLOCK, function, block->id, block->last_instruction);
            }
        }
    }
    return result;
}

IrValidationResult ir_validate_canonical_module(IrProgram* program, IrModule* module)
{
    IrValidationResult result = {
        .function = IR_FUNCTION_ID_INVALID,
        .block = IR_BLOCK_ID_INVALID,
        .instruction = IR_INSTRUCTION_ID_INVALID,
    };
    if (!program || !module)
    {
        result.error = IR_VALIDATION_INVALID_ID;
        return result;
    }
    for (u32 global_index = 0; global_index < module->global_count; global_index += 1)
    {
        IrGlobal* global = module->globals + global_index;
        IrSymbol* symbol = ir_symbol_from_id(&program->symbols, global->symbol);
        IrType* type = ir_type_from_id(&program->types, global->type);
        if (type && global->alignment &&
            ((global->alignment & (global->alignment - 1)) || !type->layout.resolved || global->alignment < type->layout.alignment))
        {
            result.error = IR_VALIDATION_ALIGNMENT;
            return result;
        }
        bool initializer_valid = false;
        if (symbol && type && type->layout.resolved && symbol->kind == IR_SYMBOL_DATA && symbol->is_definition && symbol->type.value == global->type.value)
        {
            switch (global->initializer_kind)
            {
            case IR_GLOBAL_INITIALIZER_ZERO:
            {
                initializer_valid = true;
            }
            break;
            case IR_GLOBAL_INITIALIZER_INTEGER:
            {
                initializer_valid = type->kind == IR_TYPE_INTEGER || type->kind == IR_TYPE_BOOLEAN || type->kind == IR_TYPE_ENUM;
            }
            break;
            case IR_GLOBAL_INITIALIZER_FLOAT:
            {
                initializer_valid = type->kind == IR_TYPE_FLOAT;
            }
            break;
            case IR_GLOBAL_INITIALIZER_BYTES:
            {
                initializer_valid = global->bytes.length == type->layout.size;
            }
            break;
            case IR_GLOBAL_INITIALIZER_SYMBOL_ADDRESS:
            {
                initializer_valid = type->kind == IR_TYPE_POINTER && ir_symbol_from_id(&program->symbols, global->initializer_symbol);
            }
            break;
            case IR_GLOBAL_INITIALIZER_NONE:
            case IR_GLOBAL_INITIALIZER_COUNT:
            {
                initializer_valid = false;
            }
            break;
            }
        }
        if (!initializer_valid)
        {
            result.error = IR_VALIDATION_OPERATION;
            return result;
        }
        if (global->relocation_count && !global->relocations)
        {
            result.error = IR_VALIDATION_OPERATION;
            return result;
        }
        u64 pointer_size = program->data_layout.pointer.size;
        for (u32 relocation_index = 0; relocation_index < global->relocation_count; relocation_index += 1)
        {
            IrGlobalRelocation* relocation = global->relocations + relocation_index;
            IrSymbol* relocation_symbol = ir_symbol_from_id(&program->symbols, relocation->symbol);
            bool offset_valid = pointer_size != 0 && relocation->offset <= type->layout.size && pointer_size <= type->layout.size - relocation->offset;
            bool bytes_valid = global->bytes.pointer && global->bytes.length == type->layout.size && relocation->offset <= global->bytes.length &&
                               pointer_size <= global->bytes.length - relocation->offset;
            bool overlap_free = true;
            for (u32 previous_index = 0; previous_index < relocation_index; previous_index += 1)
            {
                IrGlobalRelocation* previous = global->relocations + previous_index;
                bool previous_end_valid = previous->offset <= UINT64_MAX - pointer_size;
                bool relocation_end_valid = relocation->offset <= UINT64_MAX - pointer_size;
                overlap_free &= previous_end_valid && relocation_end_valid &&
                                (previous->offset >= relocation->offset + pointer_size || relocation->offset >= previous->offset + pointer_size);
            }
            if (!relocation_symbol || !offset_valid || !bytes_valid || !overlap_free)
            {
                result.error = IR_VALIDATION_OPERATION;
                return result;
            }
            if (relocation->is_label_address)
            {
                IrFunction* owner = ir_module_function_for_symbol(module, relocation->symbol);
                if (relocation_symbol->kind != IR_SYMBOL_FUNCTION || !relocation_symbol->is_definition || !owner ||
                    owner->state != IR_FUNCTION_LOWERED || relocation->label_block.value >= owner->block_count || relocation->addend != 0)
                {
                    result.error = IR_VALIDATION_OPERATION;
                    return result;
                }
            }
        }
    }
    for (u32 function_index = 0; function_index < module->function_count; function_index += 1)
    {
        IrFunction* function = module->functions + function_index;
        if (function->state != IR_FUNCTION_LOWERED)
        {
            continue;
        }
        IrType* signature = ir_type_from_id(&program->types, function->canonical_type);
        if (!signature || signature->kind != IR_TYPE_FUNCTION || function->entry.value >= function->block_count)
        {
            return ir_validation_error(IR_VALIDATION_INVALID_ID, function, IR_BLOCK_ID_INVALID, IR_INSTRUCTION_ID_INVALID);
        }
        for (u32 value_index = 0; value_index < function->value_count; value_index += 1)
        {
            IrValue* value = function->values + value_index;
            IrValueId value_id = {.value = value_index};
            IrType* value_type = ir_type_from_id(&program->types, value->canonical_type);
            if (!value_type || value->definition.value >= function->instruction_count)
            {
                return ir_validation_error(IR_VALIDATION_INVALID_ID, function, IR_BLOCK_ID_INVALID, value->definition);
            }
            IrValueLabelMetadata metadata = ir_value_label_metadata(function, value_id);
            bool transfer_valid = ir_label_metadata_transfer_valid(program, function, value_id);
            bool shape_valid = ir_label_metadata_shape_valid(program, function, value_id);
            if ((metadata.is_label_value && !metadata.has_label_provenance && !metadata.has_non_label_provenance && !ir_label_provenance_valid(&metadata)) ||
                (metadata.has_label_provenance && !ir_label_storage_provenance_valid(&metadata)) || !transfer_valid || !shape_valid)
            {
                return ir_validation_error(IR_VALIDATION_OPERATION, function, IR_BLOCK_ID_INVALID, value->definition);
            }
            if (ir_label_provenance_valid(&metadata) || ir_label_storage_provenance_valid(&metadata))
            {
                for (u32 label_index = 0; label_index < metadata.label_block_count; label_index += 1)
                {
                    if (metadata.label_blocks[label_index].value >= function->block_count)
                    {
                        return ir_validation_error(IR_VALIDATION_BRANCH_TARGET, function, IR_BLOCK_ID_INVALID, value->definition);
                    }
                }
            }
            if (value->alignment &&
                ((value->alignment & (value->alignment - 1)) || !value_type->layout.resolved || value->alignment < value_type->layout.alignment))
            {
                return ir_validation_error(IR_VALIDATION_ALIGNMENT, function, IR_BLOCK_ID_INVALID, value->definition);
            }
        }
        for (u32 block_index = 0; block_index < function->block_count; block_index += 1)
        {
            IrBlock* block = function->blocks + block_index;
            if (!block->terminated || !block->sealed)
            {
                return ir_validation_error(!block->terminated ? IR_VALIDATION_UNTERMINATED_BLOCK : IR_VALIDATION_BLOCK_PARAMETER, function, block->id,
                                           block->last_instruction);
            }
            for (IrBlockParameter* parameter = block->first_parameter; parameter; parameter = parameter->next)
            {
                if (parameter->value.value >= function->value_count || parameter->incoming_count != block->predecessor_count ||
                    function->values[parameter->value.value].canonical_type.value != parameter->type.value)
                {
                    return ir_validation_error(IR_VALIDATION_BLOCK_PARAMETER, function, block->id, IR_INSTRUCTION_ID_INVALID);
                }
                IrIncoming* incoming = parameter->first_incoming;
                IrPredecessor* predecessor = block->first_predecessor;
                while (incoming && predecessor)
                {
                    if (incoming->predecessor.value != predecessor->block.value || incoming->value.value >= function->value_count ||
                        function->values[incoming->value.value].canonical_type.value != parameter->type.value)
                    {
                        return ir_validation_error(IR_VALIDATION_BLOCK_PARAMETER, function, block->id, IR_INSTRUCTION_ID_INVALID);
                    }
                    incoming = incoming->next;
                    predecessor = predecessor->next;
                }
                if (incoming || predecessor || !ir_label_block_parameter_provenance_valid(function, parameter))
                {
                    return ir_validation_error(IR_VALIDATION_BLOCK_PARAMETER, function, block->id, IR_INSTRUCTION_ID_INVALID);
                }
            }
            IrInstructionId instruction_id = block->first_instruction;
            bool terminated = false;
            u32 visited = 0;
            while (instruction_id.value != IR_ID_UNDERLYING_INVALID)
            {
                if (instruction_id.value >= function->instruction_count || visited++ >= function->instruction_count)
                {
                    return ir_validation_error(IR_VALIDATION_INVALID_ID, function, block->id, instruction_id);
                }
                IrInstruction* instruction = function->instructions + instruction_id.value;
                if (terminated || instruction->opcode >= IR_OPCODE_COUNT || !ir_type_from_id(&program->types, instruction->canonical_type))
                {
                    return ir_validation_error(terminated ? IR_VALIDATION_INSTRUCTION_AFTER_TERMINATOR : IR_VALIDATION_INVALID_ID, function, block->id,
                                               instruction_id);
                }
                if ((instruction->operand_count && !instruction->operands) || (instruction->target_count && !instruction->targets) ||
                    (instruction->immediate_count && !instruction->immediates))
                {
                    return ir_validation_error(IR_VALIDATION_OPERATION, function, block->id, instruction_id);
                }
                for (u32 operand_index = 0; operand_index < instruction->operand_count; operand_index += 1)
                {
                    if (instruction->operands[operand_index].value >= function->value_count)
                    {
                        return ir_validation_error(IR_VALIDATION_INVALID_ID, function, block->id, instruction_id);
                    }
                }
                for (u32 target_index = 0; target_index < instruction->target_count; target_index += 1)
                {
                    if (instruction->targets[target_index].value >= function->block_count)
                    {
                        return ir_validation_error(IR_VALIDATION_BRANCH_TARGET, function, block->id, instruction_id);
                    }
                }
                if (instruction->result.value != IR_ID_UNDERLYING_INVALID)
                {
                    if (instruction->result.value >= function->value_count ||
                        function->values[instruction->result.value].definition.value != instruction_id.value ||
                        function->values[instruction->result.value].canonical_type.value != instruction->canonical_type.value)
                    {
                        return ir_validation_error(IR_VALIDATION_RESULT_TYPE, function, block->id, instruction_id);
                    }
                }
                if (instruction->opcode == IR_OPCODE_ARGUMENT)
                {
                    u64 argument_index = instruction->immediate_count == 1 ? instruction->immediates[0] : UINT64_MAX;
                    if (argument_index >= signature->parameter_count || signature->parameter_types[argument_index].value != instruction->canonical_type.value ||
                        instruction->operand_count != 0 || instruction->result.value == IR_ID_UNDERLYING_INVALID)
                    {
                        return ir_validation_error(IR_VALIDATION_OPERATION, function, block->id, instruction_id);
                    }
                }
                else if (instruction->opcode == IR_OPCODE_LOCAL)
                {
                    IrType* type = ir_type_from_id(&program->types, instruction->canonical_type);
                    if (!type || instruction->operand_count != 0 || instruction->result.value == IR_ID_UNDERLYING_INVALID ||
                        function->values[instruction->result.value].category != IR_VALUE_PLACE || instruction->canonical_local.value >= function->local_count)
                    {
                        return ir_validation_error(IR_VALIDATION_OPERATION, function, block->id, instruction_id);
                    }
                }
                else if (instruction->opcode == IR_OPCODE_STACK_ALLOCATE)
                {
                    IrType* pointer = ir_type_from_id(&program->types, instruction->canonical_type);
                    IrType* size_type =
                        instruction->operand_count == 1 ? ir_type_from_id(&program->types, function->values[instruction->operands[0].value].canonical_type) : 0;
                    u64 alignment = instruction->immediate_count == 1 ? instruction->immediates[0] : 0;
                    if (!pointer || pointer->kind != IR_TYPE_POINTER || !size_type || size_type->kind != IR_TYPE_INTEGER ||
                        instruction->result.value == IR_ID_UNDERLYING_INVALID || function->values[instruction->result.value].category != IR_VALUE_VALUE ||
                        !alignment || alignment > UINT32_MAX || (alignment & (alignment - 1)))
                    {
                        return ir_validation_error(IR_VALIDATION_OPERATION, function, block->id, instruction_id);
                    }
                }
                else if (instruction->opcode == IR_OPCODE_STACK_SAVE)
                {
                    IrType* pointer = ir_type_from_id(&program->types, instruction->canonical_type);
                    if (!pointer || pointer->kind != IR_TYPE_POINTER || instruction->operand_count != 0 || instruction->immediate_count != 0 ||
                        instruction->result.value == IR_ID_UNDERLYING_INVALID || function->values[instruction->result.value].category != IR_VALUE_VALUE)
                    {
                        return ir_validation_error(IR_VALIDATION_OPERATION, function, block->id, instruction_id);
                    }
                }
                else if (instruction->opcode == IR_OPCODE_STACK_RESTORE)
                {
                    IrType* restored =
                        instruction->operand_count == 1 ? ir_type_from_id(&program->types, function->values[instruction->operands[0].value].canonical_type) : 0;
                    IrType* result_type = ir_type_from_id(&program->types, instruction->canonical_type);
                    if (!restored || restored->kind != IR_TYPE_POINTER || !result_type || result_type->kind != IR_TYPE_VOID ||
                        instruction->immediate_count != 0 || instruction->result.value != IR_ID_UNDERLYING_INVALID)
                    {
                        return ir_validation_error(IR_VALIDATION_OPERATION, function, block->id, instruction_id);
                    }
                }
                else if (instruction->opcode == IR_OPCODE_GLOBAL)
                {
                    IrSymbol* symbol = ir_symbol_from_id(&program->symbols, instruction->symbol);
                    if (!symbol || symbol->kind != IR_SYMBOL_DATA || symbol->type.value != instruction->canonical_type.value ||
                        instruction->operand_count != 0 || instruction->result.value == IR_ID_UNDERLYING_INVALID ||
                        function->values[instruction->result.value].category != IR_VALUE_PLACE)
                    {
                        return ir_validation_error(IR_VALIDATION_OPERATION, function, block->id, instruction_id);
                    }
                }
                else if (instruction->opcode == IR_OPCODE_LOAD)
                {
                    IrValue* place = instruction->operand_count ? function->values + instruction->operands[0].value : 0;
                    if (!place || place->category != IR_VALUE_PLACE || place->canonical_type.value != instruction->canonical_type.value ||
                        instruction->operand_count != 1 || instruction->result.value == IR_ID_UNDERLYING_INVALID)
                    {
                        return ir_validation_error(IR_VALIDATION_OPERAND_TYPE, function, block->id, instruction_id);
                    }
                }
                else if (instruction->opcode == IR_OPCODE_STORE)
                {
                    IrValue* place = instruction->operand_count ? function->values + instruction->operands[0].value : 0;
                    IrValue* value = instruction->operand_count == 2 ? function->values + instruction->operands[1].value : 0;
                    IrType* instruction_type = ir_type_from_id(&program->types, instruction->canonical_type);
                    if (!place || !value || place->category != IR_VALUE_PLACE || value->category != IR_VALUE_VALUE ||
                        place->canonical_type.value != value->canonical_type.value || !instruction_type || instruction_type->kind != IR_TYPE_VOID ||
                        instruction->result.value != IR_ID_UNDERLYING_INVALID)
                    {
                        return ir_validation_error(IR_VALIDATION_OPERAND_TYPE, function, block->id, instruction_id);
                    }
                }
                else if (instruction->opcode == IR_OPCODE_CONSTANT_INTEGER)
                {
                    IrType* type = ir_type_from_id(&program->types, instruction->canonical_type);
                    if (!type || (type->kind != IR_TYPE_INTEGER && type->kind != IR_TYPE_BOOLEAN && type->kind != IR_TYPE_ENUM) ||
                        instruction->immediate_count != 1 || instruction->operand_count != 0 || instruction->result.value == IR_ID_UNDERLYING_INVALID)
                    {
                        return ir_validation_error(IR_VALIDATION_OPERATION, function, block->id, instruction_id);
                    }
                }
                else if (instruction->opcode == IR_OPCODE_CONSTANT_FLOAT)
                {
                    IrType* type = ir_type_from_id(&program->types, instruction->canonical_type);
                    if (!type || type->kind != IR_TYPE_FLOAT || (type->bit_width != 32 && type->bit_width != 64) || instruction->immediate_count != 1 ||
                        instruction->operand_count != 0 || instruction->result.value == IR_ID_UNDERLYING_INVALID)
                    {
                        return ir_validation_error(IR_VALIDATION_OPERATION, function, block->id, instruction_id);
                    }
                }
                else if (instruction->opcode == IR_OPCODE_FUNCTION)
                {
                    IrSymbol* symbol = ir_symbol_from_id(&program->symbols, instruction->symbol);
                    IrType* reference_type = ir_type_from_id(&program->types, instruction->canonical_type);
                    bool type_matches =
                        symbol && (symbol->type.value == instruction->canonical_type.value ||
                                   (reference_type && reference_type->kind == IR_TYPE_POINTER && reference_type->element_type.value == symbol->type.value));
                    if (!symbol || symbol->kind != IR_SYMBOL_FUNCTION || !type_matches || instruction->operand_count != 0 ||
                        instruction->result.value == IR_ID_UNDERLYING_INVALID)
                    {
                        return ir_validation_error(IR_VALIDATION_CALL_TARGET, function, block->id, instruction_id);
                    }
                }
                else if (instruction->opcode == IR_OPCODE_ATOMIC_LOAD)
                {
                    IrValue* place = instruction->operand_count ? function->values + instruction->operands[0].value : 0;
                    IrType* place_type = place ? ir_type_from_id(&program->types, place->canonical_type) : 0;
                    bool valid_order = instruction->memory_order == IR_MEMORY_ORDER_RELAXED || instruction->memory_order == IR_MEMORY_ORDER_CONSUME ||
                                       instruction->memory_order == IR_MEMORY_ORDER_ACQUIRE || instruction->memory_order == IR_MEMORY_ORDER_SEQUENTIAL;
                    if (!place || place->category != IR_VALUE_PLACE || !place_type || !place_type->is_atomic ||
                        place_type->unqualified_type.value != instruction->canonical_type.value || instruction->operand_count != 1 ||
                        instruction->result.value == IR_ID_UNDERLYING_INVALID || !valid_order)
                    {
                        return ir_validation_error(IR_VALIDATION_OPERAND_TYPE, function, block->id, instruction_id);
                    }
                }
                else if (instruction->opcode == IR_OPCODE_ATOMIC_STORE)
                {
                    IrValue* place = instruction->operand_count ? function->values + instruction->operands[0].value : 0;
                    IrValue* value = instruction->operand_count == 2 ? function->values + instruction->operands[1].value : 0;
                    IrType* place_type = place ? ir_type_from_id(&program->types, place->canonical_type) : 0;
                    IrType* instruction_type = ir_type_from_id(&program->types, instruction->canonical_type);
                    bool valid_order = instruction->memory_order == IR_MEMORY_ORDER_RELAXED || instruction->memory_order == IR_MEMORY_ORDER_RELEASE ||
                                       instruction->memory_order == IR_MEMORY_ORDER_SEQUENTIAL;
                    if (!place || !value || place->category != IR_VALUE_PLACE || value->category != IR_VALUE_VALUE || !place_type || !place_type->is_atomic ||
                        place_type->unqualified_type.value != value->canonical_type.value || !instruction_type || instruction_type->kind != IR_TYPE_VOID ||
                        instruction->result.value != IR_ID_UNDERLYING_INVALID || !valid_order)
                    {
                        return ir_validation_error(IR_VALIDATION_OPERAND_TYPE, function, block->id, instruction_id);
                    }
                }
                else if (instruction->opcode == IR_OPCODE_ATOMIC_READ_MODIFY_WRITE)
                {
                    IrValue* place = instruction->operand_count ? function->values + instruction->operands[0].value : 0;
                    IrValue* value = instruction->operand_count == 2 ? function->values + instruction->operands[1].value : 0;
                    IrType* place_type = place ? ir_type_from_id(&program->types, place->canonical_type) : 0;
                    IrType* value_type = value ? ir_type_from_id(&program->types, value->canonical_type) : 0;
                    IrType* result_type = ir_type_from_id(&program->types, instruction->canonical_type);
                    bool valid_order = instruction->memory_order < IR_MEMORY_ORDER_COUNT;
                    bool pointer_arithmetic = result_type && result_type->kind == IR_TYPE_POINTER &&
                                              (instruction->atomic_operation == IR_ATOMIC_ADD || instruction->atomic_operation == IR_ATOMIC_SUBTRACT);
                    if (!place || !value || place->category != IR_VALUE_PLACE || value->category != IR_VALUE_VALUE || !place_type || !place_type->is_atomic ||
                        place_type->unqualified_type.value != instruction->canonical_type.value || !value_type ||
                        (!pointer_arithmetic && value->canonical_type.value != instruction->canonical_type.value) ||
                        (pointer_arithmetic && (value_type->kind != IR_TYPE_INTEGER || !result_type || value_type->layout.size != result_type->layout.size)) ||
                        (!pointer_arithmetic && value_type->kind != IR_TYPE_INTEGER &&
                         (instruction->atomic_operation != IR_ATOMIC_EXCHANGE ||
                          (value_type->kind != IR_TYPE_BOOLEAN && value_type->kind != IR_TYPE_POINTER))) ||
                        instruction->atomic_operation >= IR_ATOMIC_OPERATION_COUNT || instruction->operand_count != 2 ||
                        instruction->result.value == IR_ID_UNDERLYING_INVALID || !valid_order)
                    {
                        return ir_validation_error(IR_VALIDATION_OPERAND_TYPE, function, block->id, instruction_id);
                    }
                }
                else if (instruction->opcode == IR_OPCODE_ATOMIC_COMPARE_EXCHANGE)
                {
                    IrValue* place = instruction->operand_count ? function->values + instruction->operands[0].value : 0;
                    IrValue* expected = instruction->operand_count == 3 ? function->values + instruction->operands[1].value : 0;
                    IrValue* desired = instruction->operand_count == 3 ? function->values + instruction->operands[2].value : 0;
                    IrType* place_type = place ? ir_type_from_id(&program->types, place->canonical_type) : 0;
                    IrType* value_type = expected ? ir_type_from_id(&program->types, expected->canonical_type) : 0;
                    IrMemoryOrder success = instruction->memory_order;
                    IrMemoryOrder failure = instruction->failure_memory_order;
                    bool valid_failure = failure == IR_MEMORY_ORDER_RELAXED || failure == IR_MEMORY_ORDER_CONSUME || failure == IR_MEMORY_ORDER_ACQUIRE ||
                                         failure == IR_MEMORY_ORDER_SEQUENTIAL;
                    bool compatible_orders =
                        success == IR_MEMORY_ORDER_SEQUENTIAL || (success == IR_MEMORY_ORDER_ACQUIRE_RELEASE && failure != IR_MEMORY_ORDER_SEQUENTIAL) ||
                        (success == IR_MEMORY_ORDER_ACQUIRE && failure != IR_MEMORY_ORDER_SEQUENTIAL) ||
                        (success == IR_MEMORY_ORDER_CONSUME && (failure == IR_MEMORY_ORDER_RELAXED || failure == IR_MEMORY_ORDER_CONSUME)) ||
                        (success == IR_MEMORY_ORDER_RELEASE && failure == IR_MEMORY_ORDER_RELAXED) ||
                        (success == IR_MEMORY_ORDER_RELAXED && failure == IR_MEMORY_ORDER_RELAXED);
                    if (!place || !expected || !desired || place->category != IR_VALUE_PLACE || expected->category != IR_VALUE_VALUE ||
                        desired->category != IR_VALUE_VALUE || !place_type || !place_type->is_atomic ||
                        place_type->unqualified_type.value != expected->canonical_type.value ||
                        expected->canonical_type.value != desired->canonical_type.value ||
                        instruction->canonical_type.value != expected->canonical_type.value || !value_type ||
                        (value_type->kind != IR_TYPE_INTEGER && value_type->kind != IR_TYPE_POINTER) || instruction->operand_count != 3 ||
                        instruction->result.value == IR_ID_UNDERLYING_INVALID || !valid_failure || !compatible_orders)
                    {
                        return ir_validation_error(IR_VALIDATION_OPERAND_TYPE, function, block->id, instruction_id);
                    }
                }
                else if (instruction->opcode == IR_OPCODE_ATOMIC_FENCE)
                {
                    IrType* instruction_type = ir_type_from_id(&program->types, instruction->canonical_type);
                    if (!instruction_type || instruction_type->kind != IR_TYPE_VOID || instruction->operand_count != 0 ||
                        instruction->result.value != IR_ID_UNDERLYING_INVALID || instruction->memory_order >= IR_MEMORY_ORDER_COUNT)
                    {
                        return ir_validation_error(IR_VALIDATION_OPERAND_TYPE, function, block->id, instruction_id);
                    }
                }
                else if (instruction->opcode == IR_OPCODE_CLEAR_INSTRUCTION_CACHE)
                {
                    IrType* instruction_type = ir_type_from_id(&program->types, instruction->canonical_type);
                    IrValue* begin = instruction->operand_count == 2 ? function->values + instruction->operands[0].value : 0;
                    IrValue* end = instruction->operand_count == 2 ? function->values + instruction->operands[1].value : 0;
                    IrType* begin_type = begin ? ir_type_from_id(&program->types, begin->canonical_type) : 0;
                    IrType* end_type = end ? ir_type_from_id(&program->types, end->canonical_type) : 0;
                    if (!instruction_type || instruction_type->kind != IR_TYPE_VOID || !begin_type || begin_type->kind != IR_TYPE_POINTER || !end_type ||
                        end_type->kind != IR_TYPE_POINTER || instruction->operand_count != 2 || instruction->result.value != IR_ID_UNDERLYING_INVALID)
                    {
                        return ir_validation_error(IR_VALIDATION_OPERAND_TYPE, function, block->id, instruction_id);
                    }
                }
                else if (instruction->opcode == IR_OPCODE_CALL)
                {
                    IrValue* callee = instruction->operand_count ? function->values + instruction->operands[0].value : 0;
                    IrType* callee_type = callee ? ir_type_from_id(&program->types, callee->canonical_type) : 0;
                    bool indirect = callee_type && callee_type->kind == IR_TYPE_POINTER;
                    IrType* signature_type = indirect ? ir_type_from_id(&program->types, callee_type->element_type) : callee_type;
                    IrInstruction* reference =
                        callee && callee->definition.value < function->instruction_count ? function->instructions + callee->definition.value : 0;
                    if (!signature_type || signature_type->kind != IR_TYPE_FUNCTION ||
                        (!signature_type->is_variadic && instruction->operand_count != signature_type->parameter_count + 1) ||
                        (signature_type->is_variadic && instruction->operand_count < signature_type->parameter_count + 1) ||
                        signature_type->return_type.value != instruction->canonical_type.value || !reference ||
                        (!indirect && (reference->opcode != IR_OPCODE_FUNCTION || reference->symbol.value != instruction->symbol.value)) ||
                        (indirect && instruction->symbol.value != IR_ID_UNDERLYING_INVALID) ||
                        ((ir_type_from_id(&program->types, signature_type->return_type)->kind == IR_TYPE_VOID) !=
                         (instruction->result.value == IR_ID_UNDERLYING_INVALID)))
                    {
                        return ir_validation_error(IR_VALIDATION_CALL_SIGNATURE, function, block->id, instruction_id);
                    }
                    for (u32 argument_index = 0; argument_index < signature_type->parameter_count; argument_index += 1)
                    {
                        if (function->values[instruction->operands[argument_index + 1].value].canonical_type.value !=
                            signature_type->parameter_types[argument_index].value)
                        {
                            return ir_validation_error(IR_VALIDATION_CALL_SIGNATURE, function, block->id, instruction_id);
                        }
                    }
                }
                else if (instruction->opcode == IR_OPCODE_ADDRESS_OF)
                {
                    IrValue* object = instruction->operand_count == 1 ? function->values + instruction->operands[0].value : 0;
                    IrType* pointer = ir_type_from_id(&program->types, instruction->canonical_type);
                    IrValue* result_value = instruction->result.value < function->value_count ? function->values + instruction->result.value : 0;
                    IrValueLabelMetadata result_metadata = result_value ? ir_value_label_metadata(function, instruction->result) : (IrValueLabelMetadata){0};
                    if (!object || object->category != IR_VALUE_PLACE || !pointer || pointer->kind != IR_TYPE_POINTER ||
                        pointer->element_type.value != object->canonical_type.value || instruction->result.value == IR_ID_UNDERLYING_INVALID || !result_value ||
                        (result_metadata.is_label_value || result_metadata.has_label_provenance || result_metadata.label_blocks || result_metadata.label_block_count))
                    {
                        return ir_validation_error(IR_VALIDATION_OPERATION, function, block->id, instruction_id);
                    }
                }
                else if (instruction->opcode == IR_OPCODE_DEREFERENCE)
                {
                    IrValue* address = instruction->operand_count == 1 ? function->values + instruction->operands[0].value : 0;
                    IrType* pointer = address ? ir_type_from_id(&program->types, address->canonical_type) : 0;
                    IrValue* place = instruction->result.value < function->value_count ? function->values + instruction->result.value : 0;
                    IrValueLabelMetadata address_metadata = address ? ir_value_label_metadata(function, instruction->operands[0]) : (IrValueLabelMetadata){0};
                    IrValueLabelMetadata place_metadata = place ? ir_value_label_metadata(function, instruction->result) : (IrValueLabelMetadata){0};
                    if (!address || address->category != IR_VALUE_VALUE || !pointer || pointer->kind != IR_TYPE_POINTER ||
                        pointer->element_type.value != instruction->canonical_type.value || !place || place->category != IR_VALUE_PLACE ||
                        ir_label_metadata_has_label(&address_metadata) || ir_label_metadata_has_label(&place_metadata))
                    {
                        return ir_validation_error(IR_VALIDATION_OPERATION, function, block->id, instruction_id);
                    }
                }
                else if (instruction->opcode == IR_OPCODE_UNARY)
                {
                    IrType* type = ir_type_from_id(&program->types, instruction->canonical_type);
                    IrType* vector_element = type && type->kind == IR_TYPE_VECTOR ? ir_type_from_id(&program->types, type->element_type) : 0;
                    bool valid_operation =
                        (type && type->kind == IR_TYPE_INTEGER &&
                         (instruction->unary_operation == IR_UNARY_INTEGER_NEGATE || instruction->unary_operation == IR_UNARY_INTEGER_BITWISE_NOT ||
                          instruction->unary_operation == IR_UNARY_INTEGER_COUNT_LEADING_ZEROS ||
                          instruction->unary_operation == IR_UNARY_INTEGER_COUNT_TRAILING_ZEROS)) ||
                        (type && type->kind == IR_TYPE_FLOAT && instruction->unary_operation == IR_UNARY_FLOAT_NEGATE) ||
                        (type && type->kind == IR_TYPE_BOOLEAN && instruction->unary_operation == IR_UNARY_BOOLEAN_NOT) ||
                        (vector_element && vector_element->kind == IR_TYPE_INTEGER &&
                         (instruction->unary_operation == IR_UNARY_VECTOR_INTEGER_NEGATE ||
                          instruction->unary_operation == IR_UNARY_VECTOR_INTEGER_BITWISE_NOT)) ||
                        (vector_element && vector_element->kind == IR_TYPE_FLOAT && instruction->unary_operation == IR_UNARY_VECTOR_FLOAT_NEGATE);
                    if (!type || instruction->operand_count != 1 ||
                        function->values[instruction->operands[0].value].canonical_type.value != instruction->canonical_type.value || !valid_operation ||
                        instruction->result.value == IR_ID_UNDERLYING_INVALID)
                    {
                        return ir_validation_error(IR_VALIDATION_OPERATION, function, block->id, instruction_id);
                    }
                }
                else if (instruction->opcode == IR_OPCODE_BINARY)
                {
                    IrType* result_type = ir_type_from_id(&program->types, instruction->canonical_type);
                    IrValue* left = instruction->operand_count == 2 ? function->values + instruction->operands[0].value : 0;
                    IrValue* right = instruction->operand_count == 2 ? function->values + instruction->operands[1].value : 0;
                    IrType* operand_type = left ? ir_type_from_id(&program->types, left->canonical_type) : 0;
                    bool arithmetic =
                        instruction->binary_operation <= IR_BINARY_FLOAT_DIVIDE ||
                        (instruction->binary_operation >= IR_BINARY_SIGNED_REMAINDER && instruction->binary_operation <= IR_BINARY_INTEGER_BITWISE_XOR);
                    bool comparison =
                        instruction->binary_operation == IR_BINARY_INTEGER_EQUAL || instruction->binary_operation == IR_BINARY_INTEGER_NOT_EQUAL ||
                        instruction->binary_operation == IR_BINARY_FLOAT_EQUAL || instruction->binary_operation == IR_BINARY_FLOAT_NOT_EQUAL ||
                        (instruction->binary_operation >= IR_BINARY_SIGNED_LESS && instruction->binary_operation <= IR_BINARY_FLOAT_GREATER_EQUAL);
                    bool vector_operation =
                        instruction->binary_operation >= IR_BINARY_VECTOR_INTEGER_ADD && instruction->binary_operation <= IR_BINARY_VECTOR_FLOAT_GREATER_EQUAL;
                    bool vector_comparison = instruction->binary_operation >= IR_BINARY_VECTOR_INTEGER_EQUAL &&
                                             instruction->binary_operation <= IR_BINARY_VECTOR_FLOAT_GREATER_EQUAL;
                    bool matching_operands = left && right && left->canonical_type.value == right->canonical_type.value;
                    bool valid_arithmetic = arithmetic && result_type && (result_type->kind == IR_TYPE_INTEGER || result_type->kind == IR_TYPE_FLOAT) &&
                                            matching_operands && left->canonical_type.value == instruction->canonical_type.value;
                    bool valid_comparison = comparison && result_type && result_type->kind == IR_TYPE_BOOLEAN && matching_operands && operand_type &&
                                            (operand_type->kind == IR_TYPE_INTEGER || operand_type->kind == IR_TYPE_FLOAT);
                    IrType* operand_element =
                        operand_type && operand_type->kind == IR_TYPE_VECTOR ? ir_type_from_id(&program->types, operand_type->element_type) : 0;
                    IrType* result_element =
                        result_type && result_type->kind == IR_TYPE_VECTOR ? ir_type_from_id(&program->types, result_type->element_type) : 0;
                    bool vector_float_operation =
                        (instruction->binary_operation >= IR_BINARY_VECTOR_FLOAT_ADD && instruction->binary_operation <= IR_BINARY_VECTOR_FLOAT_DIVIDE) ||
                        (instruction->binary_operation >= IR_BINARY_VECTOR_FLOAT_EQUAL &&
                         instruction->binary_operation <= IR_BINARY_VECTOR_FLOAT_GREATER_EQUAL);
                    bool valid_vector_result = !vector_comparison ? result_type == operand_type
                                                                  : result_type && operand_type && operand_element && result_element &&
                                                                        result_element->kind == IR_TYPE_INTEGER && result_element->is_signed &&
                                                                        result_type->element_count == operand_type->element_count &&
                                                                        result_type->layout.size == operand_type->layout.size &&
                                                                        result_element->bit_width == operand_element->bit_width;
                    bool valid_vector_operation = vector_operation && matching_operands && operand_type && operand_type->kind == IR_TYPE_VECTOR &&
                                                  operand_element &&
                                                  ((vector_float_operation && operand_element->kind == IR_TYPE_FLOAT) ||
                                                   (!vector_float_operation && operand_element->kind == IR_TYPE_INTEGER)) &&
                                                  valid_vector_result;
                    bool valid_pointer_comparison =
                        (instruction->binary_operation == IR_BINARY_POINTER_EQUAL || instruction->binary_operation == IR_BINARY_POINTER_NOT_EQUAL) &&
                        result_type && result_type->kind == IR_TYPE_BOOLEAN && matching_operands && operand_type && operand_type->kind == IR_TYPE_POINTER;
                    if ((!valid_arithmetic && !valid_comparison && !valid_vector_operation && !valid_pointer_comparison) ||
                        instruction->result.value == IR_ID_UNDERLYING_INVALID)
                    {
                        return ir_validation_error(IR_VALIDATION_OPERATION, function, block->id, instruction_id);
                    }
                }
                else if (instruction->opcode == IR_OPCODE_CAST)
                {
                    IrType* destination = ir_type_from_id(&program->types, instruction->canonical_type);
                    IrValue* operand_slot = instruction->operand_count == 1 ? function->values + instruction->operands[0].value : 0;
                    IrType* source = operand_slot ? ir_type_from_id(&program->types, operand_slot->canonical_type) : 0;
                    bool result_in_range = instruction->result.value < function->value_count;
                    IrValueLabelMetadata operand_metadata =
                        operand_slot ? ir_value_label_metadata(function, instruction->operands[0]) : (IrValueLabelMetadata){0};
                    IrValueLabelMetadata result_metadata = result_in_range ? ir_value_label_metadata(function, instruction->result) : (IrValueLabelMetadata){0};
                    IrValueLabelMetadata* operand = operand_slot ? &operand_metadata : 0;
                    IrValueLabelMetadata* label_result = result_in_range ? &result_metadata : 0;
                    bool label_conversion_valid = true;
                    if ((operand && ir_label_metadata_has_label(operand)) || (label_result && ir_label_metadata_has_label(label_result)))
                    {
                        label_conversion_valid = operand && source && destination && ir_canonical_void_pointer_type(program, operand_slot->canonical_type) &&
                                                 ir_canonical_void_pointer_type(program, instruction->canonical_type) && source->id.value == destination->id.value &&
                                                 instruction->conversion_operation == IR_CONVERSION_IDENTITY && label_result &&
                                                 ir_label_provenance_valid(operand) && ir_label_provenance_valid(label_result) &&
                                                 label_result->label_block_count == operand->label_block_count;
                        for (u32 label_index = 0; label_conversion_valid && label_index < operand->label_block_count; label_index += 1)
                        {
                            label_conversion_valid = ir_label_provenance_contains(label_result, operand->label_blocks[label_index]);
                        }
                    }
                    if (!ir_canonical_conversion_valid(source, destination, instruction->conversion_operation) ||
                        instruction->result.value == IR_ID_UNDERLYING_INVALID || !label_conversion_valid)
                    {
                        return ir_validation_error(IR_VALIDATION_OPERATION, function, block->id, instruction_id);
                    }
                }
                else if (instruction->opcode == IR_OPCODE_ARRAY)
                {
                    IrType* array = ir_type_from_id(&program->types, instruction->canonical_type);
                    bool valid = array && (array->kind == IR_TYPE_ARRAY || array->kind == IR_TYPE_VECTOR) &&
                                 instruction->operand_count == array->element_count && instruction->immediate_count == 0 &&
                                 instruction->result.value != IR_ID_UNDERLYING_INVALID;
                    for (u32 operand_index = 0; valid && operand_index < instruction->operand_count; operand_index += 1)
                    {
                        valid = function->values[instruction->operands[operand_index].value].canonical_type.value == array->element_type.value;
                    }
                    if (!valid)
                    {
                        return ir_validation_error(IR_VALIDATION_OPERATION, function, block->id, instruction_id);
                    }
                }
                else if (instruction->opcode == IR_OPCODE_AGGREGATE)
                {
                    IrType* aggregate = ir_type_from_id(&program->types, instruction->canonical_type);
                    bool valid = aggregate && (aggregate->kind == IR_TYPE_STRUCT || aggregate->kind == IR_TYPE_UNION) &&
                                 instruction->operand_count == instruction->immediate_count && instruction->result.value != IR_ID_UNDERLYING_INVALID &&
                                 (aggregate->kind == IR_TYPE_UNION ? instruction->operand_count <= 1 : instruction->operand_count == aggregate->field_count);
                    for (u32 operand_index = 0; valid && operand_index < instruction->operand_count; operand_index += 1)
                    {
                        u64 field_index = instruction->immediates[operand_index];
                        valid = field_index < aggregate->field_count &&
                                function->values[instruction->operands[operand_index].value].canonical_type.value == aggregate->fields[field_index].type.value;
                        for (u32 previous = 0; valid && previous < operand_index; previous += 1)
                        {
                            valid = instruction->immediates[previous] != field_index;
                        }
                    }
                    if (!valid)
                    {
                        return ir_validation_error(IR_VALIDATION_OPERATION, function, block->id, instruction_id);
                    }
                }
                else if (instruction->opcode == IR_OPCODE_FIELD)
                {
                    IrValue* base = instruction->operand_count == 1 ? function->values + instruction->operands[0].value : 0;
                    IrType* base_type = base ? ir_type_from_id(&program->types, base->canonical_type) : 0;
                    u64 field_index = instruction->immediate_count == 1 ? instruction->immediates[0] : UINT64_MAX;
                    bool valid_field = base_type && (base_type->kind == IR_TYPE_STRUCT || base_type->kind == IR_TYPE_UNION) &&
                                       field_index < base_type->field_count && instruction->result.value != IR_ID_UNDERLYING_INVALID &&
                                       function->values[instruction->result.value].category == IR_VALUE_PLACE &&
                                       instruction->canonical_type.value == base_type->fields[field_index].type.value;
                    if (!valid_field)
                    {
                        return ir_validation_error(IR_VALIDATION_OPERATION, function, block->id, instruction_id);
                    }
                }
                else if (instruction->opcode == IR_OPCODE_INDEX)
                {
                    IrValue* base = instruction->operand_count == 2 ? function->values + instruction->operands[0].value : 0;
                    IrValue* index = instruction->operand_count == 2 ? function->values + instruction->operands[1].value : 0;
                    IrType* base_type = base ? ir_type_from_id(&program->types, base->canonical_type) : 0;
                    IrType* index_type = index ? ir_type_from_id(&program->types, index->canonical_type) : 0;
                    bool valid_index =
                        base_type && (base_type->kind == IR_TYPE_ARRAY || base_type->kind == IR_TYPE_VECTOR || base_type->kind == IR_TYPE_POINTER) &&
                        index_type && index_type->kind == IR_TYPE_INTEGER && instruction->immediate_count == 0 &&
                        instruction->result.value != IR_ID_UNDERLYING_INVALID && function->values[instruction->result.value].category == IR_VALUE_PLACE &&
                        instruction->canonical_type.value == base_type->element_type.value;
                    if (!valid_index)
                    {
                        return ir_validation_error(IR_VALIDATION_OPERATION, function, block->id, instruction_id);
                    }
                }
                else if (instruction->opcode == IR_OPCODE_VA_START || instruction->opcode == IR_OPCODE_VA_COPY || instruction->opcode == IR_OPCODE_VA_END ||
                         instruction->opcode == IR_OPCODE_VA_ARG)
                {
                    IrType* result_type = ir_type_from_id(&program->types, instruction->canonical_type);
                    IrType* function_type = ir_type_from_id(&program->types, function->canonical_type);
                    bool start = instruction->opcode == IR_OPCODE_VA_START;
                    bool end = instruction->opcode == IR_OPCODE_VA_END;
                    bool valid = result_type && function_type && function_type->kind == IR_TYPE_FUNCTION && instruction->immediate_count == 0;
                    if (start)
                    {
                        valid &= function_type->is_variadic && result_type->kind == IR_TYPE_VA_LIST && instruction->operand_count == 0 &&
                                 instruction->result.value != IR_ID_UNDERLYING_INVALID;
                    }
                    else
                    {
                        IrValue* operand = instruction->operand_count == 1 ? &function->values[instruction->operands[0].value] : 0;
                        IrType* pointer = operand ? ir_type_from_id(&program->types, operand->canonical_type) : 0;
                        IrType* pointee = pointer && pointer->kind == IR_TYPE_POINTER ? ir_type_from_id(&program->types, pointer->element_type) : 0;
                        valid &= operand && pointer && pointee && pointee->kind == IR_TYPE_VA_LIST;
                        if (end)
                        {
                            valid &= result_type->kind == IR_TYPE_VOID && instruction->result.value == IR_ID_UNDERLYING_INVALID;
                        }
                        else
                        {
                            valid &= instruction->result.value != IR_ID_UNDERLYING_INVALID;
                            if (instruction->opcode == IR_OPCODE_VA_COPY)
                            {
                                valid &= result_type->kind == IR_TYPE_VA_LIST;
                            }
                            else
                            {
                                valid &= result_type->kind != IR_TYPE_VOID;
                            }
                        }
                    }
                    if (!valid)
                    {
                        return ir_validation_error(IR_VALIDATION_OPERATION, function, block->id, instruction_id);
                    }
                }
                else if (instruction->opcode == IR_OPCODE_INLINE_ASSEMBLY)
                {
                    if (!ir_canonical_inline_assembly_valid(program, function, instruction))
                    {
                        return ir_validation_error(IR_VALIDATION_OPERATION, function, block->id, instruction_id);
                    }
                }
                else if (instruction->opcode == IR_OPCODE_LABEL_ADDRESS)
                {
                    IrType* type = ir_type_from_id(&program->types, instruction->canonical_type);
                    bool result_in_range = instruction->result.value < function->value_count;
                    IrValueLabelMetadata result_metadata = result_in_range ? ir_value_label_metadata(function, instruction->result) : (IrValueLabelMetadata){0};
                    IrValueLabelMetadata* label_result = result_in_range ? &result_metadata : 0;
                    bool valid = type && ir_canonical_void_pointer_type(program, instruction->canonical_type) && instruction->operand_count == 0 && instruction->target_count == 1 &&
                                 instruction->targets && instruction->targets[0].value < function->block_count && instruction->immediate_count == 0 &&
                                 label_result && instruction->result.value != IR_ID_UNDERLYING_INVALID && !label_result->has_non_label_provenance &&
                                 !label_result->has_label_provenance && !label_result->label_paths && !label_result->label_path_count && ir_label_provenance_valid(label_result) &&
                                 label_result->label_block_count == 1 && label_result->label_blocks && label_result->label_blocks[0].value == instruction->targets[0].value;
                    if (!valid)
                    {
                        return ir_validation_error(IR_VALIDATION_OPERATION, function, block->id, instruction_id);
                    }
                }
                else if (instruction->opcode == IR_OPCODE_DEBUG_TRAP)
                {
                    IrType* type = ir_type_from_id(&program->types, instruction->canonical_type);
                    if (!type || type->kind != IR_TYPE_VOID || instruction->operand_count != 0 || instruction->immediate_count != 0 ||
                        instruction->result.value != IR_ID_UNDERLYING_INVALID)
                    {
                        return ir_validation_error(IR_VALIDATION_OPERATION, function, block->id, instruction_id);
                    }
                }
                else if (instruction->opcode == IR_OPCODE_BRANCH)
                {
                    if (instruction->operand_count != 0 || instruction->target_count != 1 || instruction->targets[0].value >= function->block_count ||
                        instruction->result.value != IR_ID_UNDERLYING_INVALID)
                    {
                        return ir_validation_error(IR_VALIDATION_BRANCH_TARGET, function, block->id, instruction_id);
                    }
                }
                else if (instruction->opcode == IR_OPCODE_BRANCH_IF)
                {
                    IrValue* condition = instruction->operand_count == 1 ? function->values + instruction->operands[0].value : 0;
                    IrType* condition_type = condition ? ir_type_from_id(&program->types, condition->canonical_type) : 0;
                    if (!condition_type || condition_type->kind != IR_TYPE_BOOLEAN || instruction->target_count != 2 ||
                        instruction->targets[0].value >= function->block_count || instruction->targets[1].value >= function->block_count ||
                        instruction->result.value != IR_ID_UNDERLYING_INVALID)
                    {
                        return ir_validation_error(IR_VALIDATION_BRANCH_TARGET, function, block->id, instruction_id);
                    }
                }
                else if (instruction->opcode == IR_OPCODE_INDIRECT_BRANCH)
                {
                    IrValue* target_slot = instruction->operand_count == 1 && instruction->operands && instruction->operands[0].value < function->value_count
                                               ? function->values + instruction->operands[0].value
                                               : 0;
                    IrValueLabelMetadata target_metadata = target_slot ? ir_value_label_metadata(function, instruction->operands[0]) : (IrValueLabelMetadata){0};
                    IrValueLabelMetadata* target = target_slot ? &target_metadata : 0;
                    IrType* target_type = target_slot ? ir_type_from_id(&program->types, target_slot->canonical_type) : 0;
                    bool valid = target && target_type && ir_canonical_void_pointer_type(program, target_slot->canonical_type) && !target->has_non_label_provenance &&
                                 ir_label_provenance_valid(target) && instruction->target_count == target->label_block_count &&
                                 instruction->operand_count == 1 && instruction->target_count != 0 && instruction->targets &&
                                 instruction->result.value == IR_ID_UNDERLYING_INVALID && ir_block_id_array_unique(instruction->targets, instruction->target_count);
                    bool label_targets = valid;
                    for (u32 label_index = 0; label_targets && label_index < target->label_block_count; label_index += 1)
                    {
                        bool found = false;
                        for (u32 target_index = 0; target_index < instruction->target_count; target_index += 1)
                        {
                            found |= instruction->targets[target_index].value == target->label_blocks[label_index].value;
                        }
                        label_targets &= target->label_blocks[label_index].value < function->block_count && found;
                    }
                    for (u32 target_index = 0; valid && target_index < instruction->target_count; target_index += 1)
                    {
                        bool found = false;
                        for (u32 label_index = 0; label_index < target->label_block_count; label_index += 1)
                        {
                            found |= instruction->targets[target_index].value == target->label_blocks[label_index].value;
                        }
                        valid &= found;
                    }
                    valid &= label_targets;
                    if (!valid)
                    {
                        return ir_validation_error(IR_VALIDATION_BRANCH_TARGET, function, block->id, instruction_id);
                    }
                }
                else if (instruction->opcode == IR_OPCODE_SWITCH)
                {
                    IrValue* switched = instruction->operand_count == 1 ? function->values + instruction->operands[0].value : 0;
                    IrType* switched_type = switched ? ir_type_from_id(&program->types, switched->canonical_type) : 0;
                    bool valid_targets = instruction->target_count == instruction->immediate_count + 1;
                    for (u32 target_index = 0; valid_targets && target_index < instruction->target_count; target_index += 1)
                    {
                        valid_targets = instruction->targets[target_index].value < function->block_count;
                    }
                    if (!switched_type || switched_type->kind != IR_TYPE_INTEGER || !valid_targets || instruction->result.value != IR_ID_UNDERLYING_INVALID)
                    {
                        return ir_validation_error(IR_VALIDATION_BRANCH_TARGET, function, block->id, instruction_id);
                    }
                }
                else if (instruction->opcode == IR_OPCODE_RETURN)
                {
                    IrType* return_type = ir_type_from_id(&program->types, signature->return_type);
                    bool valid_return =
                        return_type && (return_type->kind == IR_TYPE_VOID
                                            ? instruction->operand_count == 0
                                            : instruction->operand_count == 1 &&
                                                  function->values[instruction->operands[0].value].canonical_type.value == signature->return_type.value);
                    if (!valid_return)
                    {
                        return ir_validation_error(IR_VALIDATION_RETURN_TYPE, function, block->id, instruction_id);
                    }
                }
                terminated = instruction->opcode == IR_OPCODE_BRANCH || instruction->opcode == IR_OPCODE_BRANCH_IF || instruction->opcode == IR_OPCODE_SWITCH ||
                             instruction->opcode == IR_OPCODE_INDIRECT_BRANCH || instruction->opcode == IR_OPCODE_RETURN || instruction->opcode == IR_OPCODE_UNREACHABLE ||
                             (instruction->opcode == IR_OPCODE_INLINE_ASSEMBLY && instruction->target_count != 0);
                instruction_id = instruction->next;
            }
            if (!terminated)
            {
                return ir_validation_error(IR_VALIDATION_UNTERMINATED_BLOCK, function, block->id, block->last_instruction);
            }
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL String8 ir_opcode_name(IrOpcode opcode)
{
    switch (opcode)
    {
    case IR_OPCODE_ARGUMENT:
        return S8("argument");
    case IR_OPCODE_LOCAL:
        return S8("local");
    case IR_OPCODE_STACK_ALLOCATE:
        return S8("stack_allocate");
    case IR_OPCODE_STACK_SAVE:
        return S8("stack_save");
    case IR_OPCODE_STACK_RESTORE:
        return S8("stack_restore");
    case IR_OPCODE_GLOBAL:
        return S8("global");
    case IR_OPCODE_LOAD:
        return S8("load");
    case IR_OPCODE_STORE:
        return S8("store");
    case IR_OPCODE_ATOMIC_LOAD:
        return S8("atomic_load");
    case IR_OPCODE_ATOMIC_STORE:
        return S8("atomic_store");
    case IR_OPCODE_ATOMIC_READ_MODIFY_WRITE:
        return S8("atomic_read_modify_write");
    case IR_OPCODE_ATOMIC_COMPARE_EXCHANGE:
        return S8("atomic_compare_exchange");
    case IR_OPCODE_ATOMIC_FENCE:
        return S8("atomic_fence");
    case IR_OPCODE_CLEAR_INSTRUCTION_CACHE:
        return S8("clear_instruction_cache");
    case IR_OPCODE_CONSTANT_INTEGER:
        return S8("constant_integer");
    case IR_OPCODE_CONSTANT_FLOAT:
        return S8("constant_float");
    case IR_OPCODE_CONSTANT_STRING:
        return S8("constant_string");
    case IR_OPCODE_UNDEFINED:
        return S8("undefined");
    case IR_OPCODE_FUNCTION:
        return S8("function_reference");
    case IR_OPCODE_ARRAY:
        return S8("array");
    case IR_OPCODE_AGGREGATE:
        return S8("aggregate");
    case IR_OPCODE_LENGTH:
        return S8("length");
    case IR_OPCODE_INDEX:
        return S8("index");
    case IR_OPCODE_SLICE:
        return S8("slice");
    case IR_OPCODE_FIELD:
        return S8("field");
    case IR_OPCODE_ENUM:
        return S8("enum");
    case IR_OPCODE_CALL:
        return S8("call");
    case IR_OPCODE_CAST:
        return S8("cast");
    case IR_OPCODE_ADDRESS_OF:
        return S8("address_of");
    case IR_OPCODE_DEREFERENCE:
        return S8("dereference");
    case IR_OPCODE_UNARY:
        return S8("unary");
    case IR_OPCODE_BINARY:
        return S8("binary");
    case IR_OPCODE_REVERSE:
        return S8("reverse");
    case IR_OPCODE_VA_START:
        return S8("va_start");
    case IR_OPCODE_VA_COPY:
        return S8("va_copy");
    case IR_OPCODE_VA_END:
        return S8("va_end");
    case IR_OPCODE_VA_ARG:
        return S8("va_arg");
    case IR_OPCODE_INLINE_ASSEMBLY:
        return S8("inline_assembly");
    case IR_OPCODE_LABEL_ADDRESS:
        return S8("label_address");
    case IR_OPCODE_BRANCH:
        return S8("branch");
    case IR_OPCODE_BRANCH_IF:
        return S8("branch_if");
    case IR_OPCODE_SWITCH:
        return S8("switch");
    case IR_OPCODE_INDIRECT_BRANCH:
        return S8("indirect_branch");
    case IR_OPCODE_RETURN:
        return S8("return");
    case IR_OPCODE_DEBUG_TRAP:
        return S8("debug_trap");
    case IR_OPCODE_UNREACHABLE:
        return S8("unreachable");
    case IR_OPCODE_COUNT:
        break;
    }
    return S8("invalid");
}

BUSTER_GLOBAL_LOCAL String8 ir_conversion_operation_name(IrConversionOperation operation)
{
    switch (operation)
    {
    case IR_CONVERSION_IDENTITY:
        return S8("identity");
    case IR_CONVERSION_INTEGER_SIGN_EXTEND:
        return S8("integer_sign_extend");
    case IR_CONVERSION_INTEGER_ZERO_EXTEND:
        return S8("integer_zero_extend");
    case IR_CONVERSION_INTEGER_TRUNCATE:
        return S8("integer_truncate");
    case IR_CONVERSION_INTEGER_REINTERPRET:
        return S8("integer_reinterpret");
    case IR_CONVERSION_FLOAT_EXTEND:
        return S8("float_extend");
    case IR_CONVERSION_FLOAT_TRUNCATE:
        return S8("float_truncate");
    case IR_CONVERSION_SIGNED_INTEGER_TO_FLOAT:
        return S8("signed_integer_to_float");
    case IR_CONVERSION_UNSIGNED_INTEGER_TO_FLOAT:
        return S8("unsigned_integer_to_float");
    case IR_CONVERSION_FLOAT_TO_SIGNED_INTEGER:
        return S8("float_to_signed_integer");
    case IR_CONVERSION_FLOAT_TO_UNSIGNED_INTEGER:
        return S8("float_to_unsigned_integer");
    case IR_CONVERSION_POINTER_REINTERPRET:
        return S8("pointer_reinterpret");
    case IR_CONVERSION_POINTER_TO_INTEGER:
        return S8("pointer_to_integer");
    case IR_CONVERSION_INTEGER_TO_POINTER:
        return S8("integer_to_pointer");
    case IR_CONVERSION_COUNT:
        break;
    }
    return S8("invalid");
}

BUSTER_GLOBAL_LOCAL String8 ir_unary_operation_name(IrUnaryOperation operation)
{
    switch (operation)
    {
    case IR_UNARY_INTEGER_NEGATE:
        return S8("integer_negate");
    case IR_UNARY_FLOAT_NEGATE:
        return S8("float_negate");
    case IR_UNARY_INTEGER_BITWISE_NOT:
        return S8("integer_bitwise_not");
    case IR_UNARY_INTEGER_COUNT_LEADING_ZEROS:
        return S8("integer_count_leading_zeros");
    case IR_UNARY_INTEGER_COUNT_TRAILING_ZEROS:
        return S8("integer_count_trailing_zeros");
    case IR_UNARY_BOOLEAN_NOT:
        return S8("boolean_not");
    case IR_UNARY_VECTOR_INTEGER_NEGATE:
        return S8("vector_integer_negate");
    case IR_UNARY_VECTOR_FLOAT_NEGATE:
        return S8("vector_float_negate");
    case IR_UNARY_VECTOR_INTEGER_BITWISE_NOT:
        return S8("vector_integer_bitwise_not");
    case IR_UNARY_COUNT:
        break;
    }
    return S8("invalid");
}

BUSTER_GLOBAL_LOCAL String8 ir_binary_operation_name(IrBinaryOperation operation)
{
    switch (operation)
    {
    case IR_BINARY_INTEGER_ADD:
        return S8("integer_add");
    case IR_BINARY_INTEGER_SUBTRACT:
        return S8("integer_subtract");
    case IR_BINARY_INTEGER_MULTIPLY:
        return S8("integer_multiply");
    case IR_BINARY_SIGNED_DIVIDE:
        return S8("signed_divide");
    case IR_BINARY_UNSIGNED_DIVIDE:
        return S8("unsigned_divide");
    case IR_BINARY_FLOAT_ADD:
        return S8("float_add");
    case IR_BINARY_FLOAT_SUBTRACT:
        return S8("float_subtract");
    case IR_BINARY_FLOAT_MULTIPLY:
        return S8("float_multiply");
    case IR_BINARY_FLOAT_DIVIDE:
        return S8("float_divide");
    case IR_BINARY_SIGNED_REMAINDER:
        return S8("signed_remainder");
    case IR_BINARY_UNSIGNED_REMAINDER:
        return S8("unsigned_remainder");
    case IR_BINARY_SHIFT_LEFT:
        return S8("shift_left");
    case IR_BINARY_SIGNED_SHIFT_RIGHT:
        return S8("signed_shift_right");
    case IR_BINARY_UNSIGNED_SHIFT_RIGHT:
        return S8("unsigned_shift_right");
    case IR_BINARY_INTEGER_BITWISE_AND:
        return S8("integer_bitwise_and");
    case IR_BINARY_INTEGER_BITWISE_OR:
        return S8("integer_bitwise_or");
    case IR_BINARY_INTEGER_BITWISE_XOR:
        return S8("integer_bitwise_xor");
    case IR_BINARY_BOOLEAN_AND:
        return S8("boolean_and");
    case IR_BINARY_BOOLEAN_OR:
        return S8("boolean_or");
    case IR_BINARY_INTEGER_EQUAL:
        return S8("integer_equal");
    case IR_BINARY_INTEGER_NOT_EQUAL:
        return S8("integer_not_equal");
    case IR_BINARY_FLOAT_EQUAL:
        return S8("float_equal");
    case IR_BINARY_FLOAT_NOT_EQUAL:
        return S8("float_not_equal");
    case IR_BINARY_POINTER_EQUAL:
        return S8("pointer_equal");
    case IR_BINARY_POINTER_NOT_EQUAL:
        return S8("pointer_not_equal");
    case IR_BINARY_BOOLEAN_EQUAL:
        return S8("boolean_equal");
    case IR_BINARY_BOOLEAN_NOT_EQUAL:
        return S8("boolean_not_equal");
    case IR_BINARY_SIGNED_LESS:
        return S8("signed_less");
    case IR_BINARY_SIGNED_LESS_EQUAL:
        return S8("signed_less_equal");
    case IR_BINARY_SIGNED_GREATER:
        return S8("signed_greater");
    case IR_BINARY_SIGNED_GREATER_EQUAL:
        return S8("signed_greater_equal");
    case IR_BINARY_UNSIGNED_LESS:
        return S8("unsigned_less");
    case IR_BINARY_UNSIGNED_LESS_EQUAL:
        return S8("unsigned_less_equal");
    case IR_BINARY_UNSIGNED_GREATER:
        return S8("unsigned_greater");
    case IR_BINARY_UNSIGNED_GREATER_EQUAL:
        return S8("unsigned_greater_equal");
    case IR_BINARY_FLOAT_LESS:
        return S8("float_less");
    case IR_BINARY_FLOAT_LESS_EQUAL:
        return S8("float_less_equal");
    case IR_BINARY_FLOAT_GREATER:
        return S8("float_greater");
    case IR_BINARY_FLOAT_GREATER_EQUAL:
        return S8("float_greater_equal");
    case IR_BINARY_RANGE:
        return S8("range");
    case IR_BINARY_VECTOR_INTEGER_ADD:
        return S8("vector_integer_add");
    case IR_BINARY_VECTOR_INTEGER_SUBTRACT:
        return S8("vector_integer_subtract");
    case IR_BINARY_VECTOR_INTEGER_MULTIPLY:
        return S8("vector_integer_multiply");
    case IR_BINARY_VECTOR_SIGNED_DIVIDE:
        return S8("vector_signed_divide");
    case IR_BINARY_VECTOR_UNSIGNED_DIVIDE:
        return S8("vector_unsigned_divide");
    case IR_BINARY_VECTOR_FLOAT_ADD:
        return S8("vector_float_add");
    case IR_BINARY_VECTOR_FLOAT_SUBTRACT:
        return S8("vector_float_subtract");
    case IR_BINARY_VECTOR_FLOAT_MULTIPLY:
        return S8("vector_float_multiply");
    case IR_BINARY_VECTOR_FLOAT_DIVIDE:
        return S8("vector_float_divide");
    case IR_BINARY_VECTOR_SIGNED_REMAINDER:
        return S8("vector_signed_remainder");
    case IR_BINARY_VECTOR_UNSIGNED_REMAINDER:
        return S8("vector_unsigned_remainder");
    case IR_BINARY_VECTOR_SHIFT_LEFT:
        return S8("vector_shift_left");
    case IR_BINARY_VECTOR_SIGNED_SHIFT_RIGHT:
        return S8("vector_signed_shift_right");
    case IR_BINARY_VECTOR_UNSIGNED_SHIFT_RIGHT:
        return S8("vector_unsigned_shift_right");
    case IR_BINARY_VECTOR_INTEGER_BITWISE_AND:
        return S8("vector_integer_bitwise_and");
    case IR_BINARY_VECTOR_INTEGER_BITWISE_OR:
        return S8("vector_integer_bitwise_or");
    case IR_BINARY_VECTOR_INTEGER_BITWISE_XOR:
        return S8("vector_integer_bitwise_xor");
    case IR_BINARY_VECTOR_INTEGER_EQUAL:
        return S8("vector_integer_equal");
    case IR_BINARY_VECTOR_INTEGER_NOT_EQUAL:
        return S8("vector_integer_not_equal");
    case IR_BINARY_VECTOR_SIGNED_LESS:
        return S8("vector_signed_less");
    case IR_BINARY_VECTOR_SIGNED_LESS_EQUAL:
        return S8("vector_signed_less_equal");
    case IR_BINARY_VECTOR_SIGNED_GREATER:
        return S8("vector_signed_greater");
    case IR_BINARY_VECTOR_SIGNED_GREATER_EQUAL:
        return S8("vector_signed_greater_equal");
    case IR_BINARY_VECTOR_UNSIGNED_LESS:
        return S8("vector_unsigned_less");
    case IR_BINARY_VECTOR_UNSIGNED_LESS_EQUAL:
        return S8("vector_unsigned_less_equal");
    case IR_BINARY_VECTOR_UNSIGNED_GREATER:
        return S8("vector_unsigned_greater");
    case IR_BINARY_VECTOR_UNSIGNED_GREATER_EQUAL:
        return S8("vector_unsigned_greater_equal");
    case IR_BINARY_VECTOR_FLOAT_EQUAL:
        return S8("vector_float_equal");
    case IR_BINARY_VECTOR_FLOAT_NOT_EQUAL:
        return S8("vector_float_not_equal");
    case IR_BINARY_VECTOR_FLOAT_LESS:
        return S8("vector_float_less");
    case IR_BINARY_VECTOR_FLOAT_LESS_EQUAL:
        return S8("vector_float_less_equal");
    case IR_BINARY_VECTOR_FLOAT_GREATER:
        return S8("vector_float_greater");
    case IR_BINARY_VECTOR_FLOAT_GREATER_EQUAL:
        return S8("vector_float_greater_equal");
    case IR_BINARY_COUNT:
        break;
    }
    return S8("invalid");
}

String8 ir_print_module(Arena* arena, AnalysisResult* analysis, IrModule* module)
{
    BUSTER_UNUSED(analysis);
    u32 part_capacity = 2;
    for (u32 function_index = 0; function_index < module->function_count; function_index += 1)
    {
        IrFunction* function = module->functions + function_index;
        part_capacity += function->block_count + function->instruction_count + 2;
        for (u32 block_index = 0; block_index < function->block_count; block_index += 1)
        {
            part_capacity += function->blocks[block_index].parameter_count;
        }
    }
    String8* parts = arena_allocate(arena, String8, part_capacity);
    u32 part_count = 0;
    parts[part_count++] = string_format(arena, S8("module {S8}\n"), module->name);
    for (u32 function_index = 0; function_index < module->function_count; function_index += 1)
    {
        IrFunction* function = module->functions + function_index;
        parts[part_count++] = string_format(arena, S8("function {S8} state={u32}\n"), function->name, (u32)function->state);
        if (function->state != IR_FUNCTION_LOWERED)
        {
            continue;
        }
        for (u32 block_index = 0; block_index < function->block_count; block_index += 1)
        {
            IrBlock* block = function->blocks + block_index;
            parts[part_count++] = string_format(arena, S8("  block {u32}:\n"), block_index);
            for (IrBlockParameter* parameter = block->first_parameter; parameter; parameter = parameter->next)
            {
                parts[part_count++] = string_format(arena, S8("    %{u32} = parameter local={u32} type={u32} incoming={u32}\n"), parameter->value.value,
                                                    parameter->local.value, parameter->type.value, parameter->incoming_count);
            }
            for (IrInstructionId id = block->first_instruction; id.value != IR_ID_UNDERLYING_INVALID; id = function->instructions[id.value].next)
            {
                IrInstruction* instruction = function->instructions + id.value;
                String8 operation = instruction->opcode == IR_OPCODE_CAST     ? ir_conversion_operation_name(instruction->conversion_operation)
                                    : instruction->opcode == IR_OPCODE_UNARY  ? ir_unary_operation_name(instruction->unary_operation)
                                    : instruction->opcode == IR_OPCODE_BINARY ? ir_binary_operation_name(instruction->binary_operation)
                                                                              : S8("");
                parts[part_count++] = instruction->result.value == IR_ID_UNDERLYING_INVALID
                                          ? string_format(arena, S8("    {S8} {S8} operands={u32} targets={u32}\n"), ir_opcode_name(instruction->opcode),
                                                          operation, instruction->operand_count, instruction->target_count)
                                          : string_format(arena, S8("    %{u32} = {S8} {S8} type={u32} operands={u32}\n"), instruction->result.value,
                                                          ir_opcode_name(instruction->opcode), operation, instruction->type.value, instruction->operand_count);
            }
        }
    }
    BUSTER_CHECK(part_count <= part_capacity);
    return string_join_arena(arena, (SliceString8){.pointer = parts, .length = part_count}, false);
}
