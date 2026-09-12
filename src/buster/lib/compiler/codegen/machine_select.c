#include <buster/lib/compiler/codegen/machine_select.h>
#include <buster/lib/compiler/codegen/machine.h>
#include <buster/lib/compiler/codegen/machine_schedule_internal.h>
#include <buster/lib/integer.h>

// Shared selector storage and validation; target opcode switches own lowering.
// machine_selection_value_facts_allocate supplies the AArch64 row walk,
// machine_type_classes_build projects the shared module type table, and
// machine_selection_validate_function guards the unvalidated public entry.
// machine_selection_certify_stack_memory publishes optional frame-object
// provenance before the selector's canonical-to-machine row spans are remapped.
// machine_selection_address memoizes only demanded canonical address chains;
// both native selectors consume its field/index shapes and normalized bases.

#define MACHINE_SELECTION_ADDRESS_CACHE_CAPACITY 64u
#define MACHINE_SELECTION_ADDRESS_CHAIN_LIMIT 64u

struct MachineSelectionAddressEntry
{
    MachineSelectionAddress address;
    u32 value_plus_one;
};
BUSTER_CT_CHECK(sizeof(MachineSelectionAddressEntry) == 64);

// Decode one canonical definition. The returned base is the immediate parent
// only for transparent address operations; all other values are opaque leaves.
BUSTER_GLOBAL_LOCAL MachineSelectionAddress machine_selection_address_step(IrProgram* program, IrFunction* function,
                                                                          MachineSelectionAddressCache const* cache, IrValueId id)
{
    MachineSelectionAddress result = {
        .base_value = id,
        .index_value = IR_VALUE_ID_INVALID,
        .object_value = IR_VALUE_ID_INVALID,
        .expression_value = id,
        .symbol = IR_SYMBOL_ID_INVALID,
        .opcode = IR_OPCODE_COUNT,
    };
    if (id.value < function->value_count)
    {
        IrValue* value = function->values + id.value;
        result.alignment = value->alignment;
        result.flags = (u16)((value->is_read_only || value->points_to_read_only ? MACHINE_SELECTION_ADDRESS_READ_ONLY : 0) |
                             (value->is_volatile ? MACHINE_SELECTION_ADDRESS_VOLATILE : 0));
        if (value->category == IR_VALUE_VALUE)
        {
            u32 kind = IR_TYPE_COUNT;
            if (cache->type_classes && value->canonical_type.value < cache->type_count)
            {
                kind = cache->type_classes[value->canonical_type.value].kind;
            }
            else
            {
                IrType* type = ir_type_from_id(&program->types, value->canonical_type);
                kind = type ? (u32)type->kind : (u32)IR_TYPE_COUNT;
            }
            result.flags |= kind == IR_TYPE_ARRAY || kind == IR_TYPE_VECTOR ? MACHINE_SELECTION_ADDRESS_STORAGE : 0;
        }
        IrInstruction* definition = value->definition.value < function->instruction_count ? function->instructions + value->definition.value : 0;
        if (definition)
        {
            result.opcode = definition->opcode;
            result.flags |= definition->volatile_access ? MACHINE_SELECTION_ADDRESS_VOLATILE : 0;
            if (definition->opcode == IR_OPCODE_LOCAL)
            {
                result.object_value = id;
                result.flags |= MACHINE_SELECTION_ADDRESS_STORAGE;
            }
            else if (definition->opcode == IR_OPCODE_GLOBAL || definition->opcode == IR_OPCODE_FUNCTION)
            {
                result.symbol = definition->symbol;
                result.object_value = definition->opcode == IR_OPCODE_GLOBAL ? id : IR_VALUE_ID_INVALID;
                IrSymbol* symbol = ir_symbol_from_id(&program->symbols, definition->symbol);
                if (symbol)
                {
                    result.flags |= symbol->is_thread_local ? MACHINE_SELECTION_ADDRESS_THREAD_LOCAL : 0;
                    result.flags |= symbol->is_definition ? MACHINE_SELECTION_ADDRESS_SYMBOL_DEFINITION : 0;
                }
            }
            else if (definition->operand_count && definition->operands && definition->operands[0].value < function->value_count)
            {
                IrValueId parent = definition->operands[0];
                if (definition->opcode == IR_OPCODE_ADDRESS_OF || definition->opcode == IR_OPCODE_DEREFERENCE)
                {
                    result.base_value = parent;
                }
                else if (definition->opcode == IR_OPCODE_FIELD && definition->immediate_count && definition->immediates)
                {
                    IrType* aggregate = ir_type_from_id(&program->types, function->values[parent.value].canonical_type);
                    u64 field = definition->immediates[0];
                    if (aggregate && aggregate->fields && field < aggregate->field_count)
                    {
                        result.base_value = parent;
                        result.field_offset = aggregate->fields[field].offset;
                        result.displacement = result.field_offset;
                        result.flags |= MACHINE_SELECTION_ADDRESS_FIELD;
                    }
                }
                else if (definition->opcode == IR_OPCODE_INDEX && definition->operand_count >= 2 &&
                         definition->operands[1].value < function->value_count)
                {
                    IrTypeId index_type_id = function->values[definition->operands[1].value].canonical_type;
                    u32 index_bits = 0;
                    bool index_signed = false;
                    if (cache->type_classes && index_type_id.value < cache->type_count &&
                        cache->type_classes[index_type_id.value].kind == IR_TYPE_INTEGER &&
                        cache->type_classes[index_type_id.value].bit_width_log2 <= 6)
                    {
                        MachineTypeClass index_class = cache->type_classes[index_type_id.value];
                        index_bits = 1u << index_class.bit_width_log2;
                        index_signed = (index_class.flags & MACHINE_TYPE_CLASS_SIGNED) != 0;
                    }
                    else
                    {
                        IrType* index_type = ir_type_from_id(&program->types, index_type_id);
                        if (index_type && index_type->kind == IR_TYPE_INTEGER)
                        {
                            index_bits = index_type->bit_width;
                            index_signed = index_type->is_signed;
                        }
                    }
                    IrType* element = ir_type_from_id(&program->types, definition->canonical_type);
                    if (index_bits && index_bits <= 64 && element && element->layout.resolved)
                    {
                        result.base_value = parent;
                        result.index_value = definition->operands[1];
                        result.scale = element->layout.size;
                        result.index_bit_width = (u8)index_bits;
                        result.flags |= MACHINE_SELECTION_ADDRESS_INDEX;
                        result.flags |= index_signed ? MACHINE_SELECTION_ADDRESS_INDEX_SIGNED : 0;
                    }
                }
            }
        }
    }
    return result;
}

// Join only additive address operations. A second dynamic index or a u64
// displacement overflow retains the immediate parent as the materialized base.
// In particular, this never reassociates signed pointer arithmetic, changes an
// index extension, or combines a symbol relocation with a numeric addend.
BUSTER_GLOBAL_LOCAL MachineSelectionAddress machine_selection_address_join(MachineSelectionAddress step, MachineSelectionAddress parent)
{
    step.object_value = parent.object_value;
    step.symbol = parent.symbol;
    step.flags |= parent.flags & (MACHINE_SELECTION_ADDRESS_THREAD_LOCAL | MACHINE_SELECTION_ADDRESS_READ_ONLY |
                                  MACHINE_SELECTION_ADDRESS_VOLATILE | MACHINE_SELECTION_ADDRESS_SYMBOL_DEFINITION);
    bool one_index = step.index_value.value == IR_ID_UNDERLYING_INVALID || parent.index_value.value == IR_ID_UNDERLYING_INVALID;
    if (one_index && step.displacement <= UINT64_MAX - parent.displacement)
    {
        step.base_value = parent.base_value;
        step.displacement += parent.displacement;
        if (step.index_value.value == IR_ID_UNDERLYING_INVALID)
        {
            step.index_value = parent.index_value;
            step.scale = parent.scale;
            step.index_bit_width = parent.index_bit_width;
            step.flags |= parent.flags & MACHINE_SELECTION_ADDRESS_INDEX_SIGNED;
        }
    }
    // Preserve only the alignment explicitly certified for this exact value.
    // Parent type alignment is not a pointee alignment proof for an opaque load.
    return step;
}

MachineSelectionAddress machine_selection_address(Arena* arena, IrProgram* program, IrFunction* function,
                                                   MachineSelectionAddressCache* cache, IrValueId value)
{
    MachineSelectionAddress result = {
        .base_value = value,
        .index_value = IR_VALUE_ID_INVALID,
        .object_value = IR_VALUE_ID_INVALID,
        .expression_value = value,
        .symbol = IR_SYMBOL_ID_INVALID,
        .opcode = IR_OPCODE_COUNT,
    };
    if (value.value < function->value_count)
    {
        if (!cache->entries)
        {
            cache->entries = arena_allocate(arena, MachineSelectionAddressEntry, MACHINE_SELECTION_ADDRESS_CACHE_CAPACITY);
            memset(cache->entries, 0, sizeof(*cache->entries) * MACHINE_SELECTION_ADDRESS_CACHE_CAPACITY);
        }
        MachineSelectionAddress pending[MACHINE_SELECTION_ADDRESS_CHAIN_LIMIT];
        u32 count = 0;
        IrValueId current = value;
        bool complete = false;
        while (!complete)
        {
            MachineSelectionAddressEntry* entry = cache->entries + (current.value & (MACHINE_SELECTION_ADDRESS_CACHE_CAPACITY - 1));
            if (entry->value_plus_one == current.value + 1)
            {
                result = entry->address;
                complete = true;
            }
            else
            {
                result = machine_selection_address_step(program, function, cache, current);
                if (result.base_value.value == current.value || count == MACHINE_SELECTION_ADDRESS_CHAIN_LIMIT)
                {
                    // At the depth bound retain the original expression;
                    // preserving its producer also preserves any cycle in
                    // malformed unvalidated input without recursive traversal.
                    if (count == MACHINE_SELECTION_ADDRESS_CHAIN_LIMIT)
                    {
                        result.base_value = current;
                        result.index_value = IR_VALUE_ID_INVALID;
                        result.displacement = 0;
                        result.object_value = IR_VALUE_ID_INVALID;
                        result.symbol = IR_SYMBOL_ID_INVALID;
                    }
                    complete = true;
                }
                else
                {
                    pending[count++] = result;
                    current = result.base_value;
                }
            }
        }
        while (count)
        {
            MachineSelectionAddressEntry* entry = cache->entries + (result.expression_value.value & (MACHINE_SELECTION_ADDRESS_CACHE_CAPACITY - 1));
            entry->address = result;
            entry->value_plus_one = result.expression_value.value + 1;
            result = machine_selection_address_join(pending[--count], result);
        }
        MachineSelectionAddressEntry* entry = cache->entries + (value.value & (MACHINE_SELECTION_ADDRESS_CACHE_CAPACITY - 1));
        entry->address = result;
        entry->value_plus_one = value.value + 1;
    }
    return result;
}

MachineSelectionValueFacts machine_selection_value_facts_allocate(Arena* arena, u32 value_count)
{
    u32 capacity = value_count ? value_count : 1;
    MachineSelectionValueFacts result = {
        .definition_blocks = arena_allocate(arena, u32, capacity),
        .use_counts = arena_allocate(arena, u32, capacity),
        .use_blocks = arena_allocate(arena, u32, capacity),
    };
    if (value_count)
    {
        memset(result.definition_blocks, 0xff, (u64)value_count * sizeof(*result.definition_blocks));
        memset(result.use_counts, 0, (u64)value_count * sizeof(*result.use_counts));
        memset(result.use_blocks, 0xff, (u64)value_count * sizeof(*result.use_blocks));
    }
    return result;
}

MachineTypeClass* machine_type_classes_build(Arena* arena, IrTypeTable const* types)
{
    MachineTypeClass* classes = arena_allocate(arena, MachineTypeClass, types->count ? types->count : 1);
    for (u32 type_index = 0; type_index < types->count; type_index += 1)
    {
        IrType const* type = types->types + type_index;
        bool resolved = type->layout.resolved;
        bool scalar_kind = type->kind == IR_TYPE_BOOLEAN || type->kind == IR_TYPE_INTEGER || type->kind == IR_TYPE_POINTER || type->kind == IR_TYPE_ENUM;
        u32 flags = 0;
        flags |= resolved ? MACHINE_TYPE_CLASS_RESOLVED : 0;
        flags |= resolved && scalar_kind && type->layout.size <= 8 ? MACHINE_TYPE_CLASS_SCALAR_REGISTER : 0;
        flags |= type->kind == IR_TYPE_FLOAT && (type->bit_width == 32 || type->bit_width == 64) ? MACHINE_TYPE_CLASS_FLOAT_SCALAR : 0;
        flags |= resolved && type->kind == IR_TYPE_VECTOR && type->layout.size == 64 ? MACHINE_TYPE_CLASS_VECTOR_REGISTER : 0;
        flags |= type->kind == IR_TYPE_POINTER || type->kind == IR_TYPE_FUNCTION || (type->kind == IR_TYPE_INTEGER && type->bit_width > 32)
                     ? MACHINE_TYPE_CLASS_WIDE
                     : 0;
        flags |= type->is_signed ? MACHINE_TYPE_CLASS_SIGNED : 0;
        flags |= type->kind == IR_TYPE_INTEGER && type->bit_width == 128 ? MACHINE_TYPE_CLASS_INTEGER128 : 0;
        flags |= type->kind == IR_TYPE_STRUCT || type->kind == IR_TYPE_UNION || type->kind == IR_TYPE_SLICE ? MACHINE_TYPE_CLASS_AGGREGATE : 0;
        u32 size_log2 = MACHINE_TYPE_CLASS_NO_LOG2;
        if (resolved && BUSTER_IS_POWER_OF_TWO(type->layout.size))
        {
            size_log2 = trailing_zeroes_u64(type->layout.size);
        }
        u32 bit_width_log2 = MACHINE_TYPE_CLASS_NO_LOG2;
        if (BUSTER_IS_POWER_OF_TWO(type->bit_width))
        {
            bit_width_log2 = trailing_zeroes_u64(type->bit_width);
        }
        classes[type_index] = (MachineTypeClass){
            .flags = (u8)flags,
            .kind = (u8)type->kind,
            .size_log2 = (u8)size_log2,
            .bit_width_log2 = (u8)bit_width_log2,
        };
    }
    return classes;
}

MachineSelectionValidationError machine_selection_validate_function(Arena* arena, IrProgram* program, IrFunction* function)
{
    MachineSelectionValidationError error = MACHINE_SELECTION_VALIDATION_NONE;
    if (!arena || !program || !function || (function->instruction_count && !function->instructions) ||
        (function->block_count && !function->blocks) || (function->value_count && !function->values))
    {
        error = MACHINE_SELECTION_VALIDATION_INVALID_ARGUMENT;
    }
    else
    {
        // Validation needs presence, not definition ids or use facts. These
        // marks are private scratch; selectors gather the facts they consume
        // in their own existing row walks after validation succeeds.
        u32 instruction_capacity = function->instruction_count ? function->instruction_count : 1;
        u32 value_capacity = function->value_count ? function->value_count : 1;
        u64 scratch_count = (u64)instruction_capacity + value_capacity;
        u8* visited = arena_allocate(arena, u8, scratch_count);
        u8* defined = visited + instruction_capacity;
        memset(visited, 0, scratch_count);
        u32 visited_count = 0;
        for (u32 block_index = 0; block_index < function->block_count && error == MACHINE_SELECTION_VALIDATION_NONE; block_index += 1)
        {
            IrBlock* block = function->blocks + block_index;
            IrInstructionId tail = IR_INSTRUCTION_ID_INVALID;
            IrInstructionId id = block->first_instruction;
            while (id.value != IR_ID_UNDERLYING_INVALID && error == MACHINE_SELECTION_VALIDATION_NONE)
            {
                if (id.value >= function->instruction_count || visited[id.value])
                {
                    error = MACHINE_SELECTION_VALIDATION_OWNERSHIP;
                }
                else
                {
                    visited[id.value] = 1;
                    visited_count += 1;
                    IrInstruction* instruction = function->instructions + id.value;
                    if (instruction->opcode >= IR_OPCODE_COUNT)
                    {
                        error = MACHINE_SELECTION_VALIDATION_INVALID_OPCODE;
                    }
                    else if (instruction->operand_count && !instruction->operands)
                    {
                        error = MACHINE_SELECTION_VALIDATION_INVALID_VALUE;
                    }
                    else
                    {
                        if (instruction->result.value != IR_ID_UNDERLYING_INVALID)
                        {
                            u32 value_index = instruction->result.value;
                            if (value_index >= function->value_count)
                            {
                                error = MACHINE_SELECTION_VALIDATION_INVALID_VALUE;
                            }
                            else if (defined[value_index])
                            {
                                error = MACHINE_SELECTION_VALIDATION_DUPLICATE_DEFINITION;
                            }
                            else
                            {
                                defined[value_index] = 1;
                            }
                        }
                        for (u32 operand_index = 0; operand_index < instruction->operand_count && error == MACHINE_SELECTION_VALIDATION_NONE;
                             operand_index += 1)
                        {
                            if (instruction->operands[operand_index].value >= function->value_count)
                            {
                                error = MACHINE_SELECTION_VALIDATION_INVALID_VALUE;
                            }
                        }
                    }
                    tail = id;
                    id = ir_block_next_instruction(function, block, id);
                }
            }
            if (error == MACHINE_SELECTION_VALIDATION_NONE && tail.value != block->last_instruction.value)
            {
                error = MACHINE_SELECTION_VALIDATION_OWNERSHIP;
            }
        }
        if (error == MACHINE_SELECTION_VALIDATION_NONE && visited_count != function->instruction_count)
        {
            error = MACHINE_SELECTION_VALIDATION_OWNERSHIP;
        }
    }
    return error;
}

void machine_selection_certify_stack_memory(Arena* arena, MachineFunction* machine, IrFunction const* source)
{
    machine->stack_slot_memory_flags = 0;
    bool valid = !machine->nonvolatile_memory_certified && machine->stack_slot_count && machine->stack_slot_sizes &&
                 machine->instructions && machine->line_mark_count && machine->line_marks && source && source->instructions;
    bool volatile_seen = false;
    for (u32 mark_index = 0; valid && mark_index < machine->line_mark_count; mark_index += 1)
    {
        MachineLineMark mark = machine->line_marks[mark_index];
        valid = mark.instruction < source->instruction_count && mark.row <= machine->instruction_count &&
                (!mark_index || machine->line_marks[mark_index - 1].row <= mark.row);
        if (valid)
        {
            volatile_seen |= source->instructions[mark.instruction].volatile_access;
        }
    }
    if (valid && volatile_seen)
    {
        u8* flags = arena_allocate(arena, u8, machine->stack_slot_count);
        memset(flags, MACHINE_STACK_SLOT_MEMORY_NONVOLATILE, machine->stack_slot_count);
        for (u32 mark_index = 0; mark_index < machine->line_mark_count; mark_index += 1)
        {
            MachineLineMark mark = machine->line_marks[mark_index];
            if (source->instructions[mark.instruction].volatile_access)
            {
                u32 end = mark_index + 1 < machine->line_mark_count ? machine->line_marks[mark_index + 1].row : machine->instruction_count;
                for (u32 row = mark.row; row < end; row += 1)
                {
                    // All frame operands are tainted, including helper loads,
                    // stores and addresses. This may lose precision, but cannot
                    // accidentally certify one piece of a split volatile access.
                    MachineInstruction const* instruction = machine->instructions + row;
                    MachineScheduleMemoryAccess access = machine_schedule_frame_access(machine, instruction);
                    if (access.kind == MACHINE_SCHEDULE_MEMORY_STACK_RANGE)
                    {
                        flags[access.stack_slot] = 0;
                    }
                    for (u32 operand = 0; operand < MACHINE_INSTRUCTION_OPERAND_COUNT; operand += 1)
                    {
                        MachineRef reference = instruction->operands[operand];
                        if (machine_ref_kind(reference) == MACHINE_REF_STACK_SLOT)
                        {
                            u32 slot = machine_ref_payload(reference);
                            if (slot < machine->stack_slot_count)
                            {
                                flags[slot] = 0;
                            }
                        }
                    }
                }
            }
        }
        // Incoming ABI rows before the first source mark are nonvolatile;
        // volatile accesses through pointers remain UNKNOWN to the scheduler.
        machine->stack_slot_memory_flags = flags;
    }
}
