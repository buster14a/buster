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
                    id = instruction->next;
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
