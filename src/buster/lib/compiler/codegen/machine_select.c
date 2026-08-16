#include <buster/lib/compiler/codegen/machine_select.h>

// This pass intentionally has no target-specific lowering.  Its outputs are
// stable source-order facts and conservative classifications that both
// machine selectors can consume.  The generated rule tree is included by
// machine.c after machine.h and before the target selector files.

BUSTER_GLOBAL_LOCAL bool machine_selection_opcode_is_address(IrOpcode opcode)
{
    return opcode == IR_OPCODE_GLOBAL || opcode == IR_OPCODE_INDEX || opcode == IR_OPCODE_FIELD || opcode == IR_OPCODE_DEREFERENCE ||
           opcode == IR_OPCODE_ADDRESS_OF || opcode == IR_OPCODE_FUNCTION || opcode == IR_OPCODE_LABEL_ADDRESS;
}

BUSTER_GLOBAL_LOCAL bool machine_selection_opcode_reads_memory(IrOpcode opcode)
{
    return opcode == IR_OPCODE_LOAD || opcode == IR_OPCODE_ATOMIC_LOAD || opcode == IR_OPCODE_ATOMIC_READ_MODIFY_WRITE ||
           opcode == IR_OPCODE_ATOMIC_COMPARE_EXCHANGE || opcode == IR_OPCODE_CALL || opcode == IR_OPCODE_VA_ARG;
}

BUSTER_GLOBAL_LOCAL bool machine_selection_opcode_writes_memory(IrOpcode opcode)
{
    return opcode == IR_OPCODE_STORE || opcode == IR_OPCODE_ATOMIC_STORE || opcode == IR_OPCODE_ATOMIC_READ_MODIFY_WRITE ||
           opcode == IR_OPCODE_ATOMIC_COMPARE_EXCHANGE || opcode == IR_OPCODE_CALL || opcode == IR_OPCODE_VA_START || opcode == IR_OPCODE_VA_COPY ||
           opcode == IR_OPCODE_VA_END;
}

BUSTER_GLOBAL_LOCAL bool machine_selection_opcode_is_atomic(IrOpcode opcode)
{
    return opcode == IR_OPCODE_ATOMIC_LOAD || opcode == IR_OPCODE_ATOMIC_STORE || opcode == IR_OPCODE_ATOMIC_READ_MODIFY_WRITE ||
           opcode == IR_OPCODE_ATOMIC_COMPARE_EXCHANGE || opcode == IR_OPCODE_ATOMIC_FENCE;
}

BUSTER_GLOBAL_LOCAL bool machine_selection_opcode_is_control(IrOpcode opcode)
{
    return opcode == IR_OPCODE_BRANCH || opcode == IR_OPCODE_BRANCH_IF || opcode == IR_OPCODE_SWITCH || opcode == IR_OPCODE_INDIRECT_BRANCH ||
           opcode == IR_OPCODE_RETURN || opcode == IR_OPCODE_UNREACHABLE || opcode == IR_OPCODE_DEBUG_TRAP;
}

BUSTER_GLOBAL_LOCAL u8 machine_selection_instruction_side_effects(IrInstruction* instruction)
{
    if (!instruction)
    {
        return MACHINE_SELECTION_SIDE_EFFECT_UNKNOWN;
    }
    IrOpcode opcode = instruction->opcode;
    u8 result = MACHINE_SELECTION_SIDE_EFFECT_NONE;
    if (machine_selection_opcode_reads_memory(opcode))
    {
        result |= MACHINE_SELECTION_SIDE_EFFECT_READ_MEMORY;
    }
    if (machine_selection_opcode_writes_memory(opcode))
    {
        result |= MACHINE_SELECTION_SIDE_EFFECT_WRITE_MEMORY;
    }
    if (machine_selection_opcode_is_atomic(opcode))
    {
        result |= MACHINE_SELECTION_SIDE_EFFECT_ATOMIC | MACHINE_SELECTION_SIDE_EFFECT_BARRIER;
    }
    if (opcode == IR_OPCODE_CALL)
    {
        result |= MACHINE_SELECTION_SIDE_EFFECT_CALL | MACHINE_SELECTION_SIDE_EFFECT_BARRIER;
    }
    if (machine_selection_opcode_is_control(opcode))
    {
        result |= MACHINE_SELECTION_SIDE_EFFECT_CONTROL | MACHINE_SELECTION_SIDE_EFFECT_BARRIER;
    }
    if (opcode == IR_OPCODE_INLINE_ASSEMBLY || opcode == IR_OPCODE_CLEAR_INSTRUCTION_CACHE)
    {
        result |= MACHINE_SELECTION_SIDE_EFFECT_UNKNOWN | MACHINE_SELECTION_SIDE_EFFECT_BARRIER;
    }
    if (instruction->volatile_access)
    {
        result |= MACHINE_SELECTION_SIDE_EFFECT_BARRIER;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL MachineSelectionResultClass machine_selection_instruction_result_class(IrProgram* program, IrFunction* function,
                                                                                             IrInstruction* instruction)
{
    if (!instruction || instruction->result.value == IR_ID_UNDERLYING_INVALID)
    {
        return MACHINE_SELECTION_RESULT_NONE;
    }
    IrType* type = program ? ir_type_from_id(&program->types, instruction->canonical_type) : 0;
    if (!type || type->kind == IR_TYPE_VOID)
    {
        return MACHINE_SELECTION_RESULT_VOID;
    }
    if (machine_selection_opcode_is_address(instruction->opcode) || instruction->opcode == IR_OPCODE_GLOBAL ||
        instruction->opcode == IR_OPCODE_FUNCTION)
    {
        return MACHINE_SELECTION_RESULT_ADDRESS;
    }
    if (type->kind == IR_TYPE_VECTOR)
    {
        return MACHINE_SELECTION_RESULT_VECTOR;
    }
    if (type->kind == IR_TYPE_STRUCT || type->kind == IR_TYPE_UNION || type->kind == IR_TYPE_ARRAY || type->kind == IR_TYPE_SLICE ||
        (type->kind == IR_TYPE_INTEGER && type->bit_width > 64))
    {
        return MACHINE_SELECTION_RESULT_AGGREGATE;
    }
    (void)function;
    return MACHINE_SELECTION_RESULT_SCALAR;
}

BUSTER_GLOBAL_LOCAL bool machine_selection_local_type_promotable(IrProgram* program, IrValue* value)
{
    if (!program || !value)
    {
        return false;
    }
    IrType* type = ir_type_from_id(&program->types, value->canonical_type);
    if (!type || !type->layout.resolved || (type->layout.size != 4 && type->layout.size != 8))
    {
        return false;
    }
    return type->kind == IR_TYPE_BOOLEAN || type->kind == IR_TYPE_INTEGER || type->kind == IR_TYPE_POINTER || type->kind == IR_TYPE_ENUM;
}

MachineSelectionPrepass machine_selection_prepass_build(Arena* arena, IrProgram* program, IrFunction* function)
{
    MachineSelectionPrepass result = {
        .program = program,
        .function = function,
        .error = MACHINE_SELECTION_PREPASS_NONE,
        .valid = false,
    };
    if (!arena || !program || !function || (function->instruction_count && !function->instructions) ||
        (function->block_count && !function->blocks) || (function->value_count && !function->values))
    {
        result.error = MACHINE_SELECTION_PREPASS_INVALID_ARGUMENT;
        return result;
    }
    result.instruction_count = function->instruction_count;
    result.value_count = function->value_count;
    result.block_count = function->block_count;
    u32 instruction_capacity = function->instruction_count ? function->instruction_count : 1;
    u32 value_capacity = function->value_count ? function->value_count : 1;
    result.instruction_owner_blocks = arena_allocate(arena, IrBlockId, instruction_capacity);
    result.instruction_ordinals = arena_allocate(arena, u32, instruction_capacity);
    result.value_definitions = arena_allocate(arena, IrInstructionId, value_capacity);
    result.value_definition_blocks = arena_allocate(arena, u32, value_capacity);
    result.value_definition_ordinals = arena_allocate(arena, u32, value_capacity);
    result.value_first_use_ordinals = arena_allocate(arena, u32, value_capacity);
    result.value_last_use_ordinals = arena_allocate(arena, u32, value_capacity);
    result.value_use_counts = arena_allocate(arena, u32, value_capacity);
    result.value_use_blocks = arena_allocate(arena, u32, value_capacity);
    result.value_local_store_counts = arena_allocate(arena, u32, value_capacity);
    result.value_constant_bits = arena_allocate(arena, u64, value_capacity);
    result.value_flags = arena_allocate(arena, u8, value_capacity);
    result.value_promotable_local_widths = arena_allocate(arena, u8, value_capacity);
    result.instruction_result_classes = arena_allocate(arena, u8, instruction_capacity);
    result.instruction_side_effects = arena_allocate(arena, u8, instruction_capacity);
    for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
    {
        result.instruction_owner_blocks[instruction_index] = IR_BLOCK_ID_INVALID;
        result.instruction_ordinals[instruction_index] = MACHINE_SELECTION_INVALID_INDEX;
        result.instruction_result_classes[instruction_index] = MACHINE_SELECTION_RESULT_NONE;
        result.instruction_side_effects[instruction_index] = MACHINE_SELECTION_SIDE_EFFECT_UNKNOWN;
    }
    for (u32 value_index = 0; value_index < function->value_count; value_index += 1)
    {
        result.value_definitions[value_index] = IR_INSTRUCTION_ID_INVALID;
        result.value_definition_blocks[value_index] = MACHINE_SELECTION_INVALID_INDEX;
        result.value_definition_ordinals[value_index] = MACHINE_SELECTION_INVALID_INDEX;
        result.value_first_use_ordinals[value_index] = MACHINE_SELECTION_INVALID_INDEX;
        result.value_last_use_ordinals[value_index] = 0;
        result.value_use_counts[value_index] = 0;
        result.value_use_blocks[value_index] = MACHINE_SELECTION_INVALID_INDEX;
        result.value_local_store_counts[value_index] = 0;
        result.value_constant_bits[value_index] = 0;
        result.value_flags[value_index] = 0;
        result.value_promotable_local_widths[value_index] = 0;
    }
    IrInstructionOwnership ownership = ir_function_instruction_owners(function, result.instruction_owner_blocks);
    if (ownership.error != IR_VALIDATION_NONE)
    {
        result.error = MACHINE_SELECTION_PREPASS_OWNERSHIP;
        return result;
    }
    u32 ordinal = 0;
    u32 promotable_candidate_count = 0;
    for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
    {
        IrInstruction* instruction = function->instructions + instruction_index;
        IrBlockId owner = result.instruction_owner_blocks[instruction_index];
        u32 block_index = owner.value;
        result.instruction_ordinals[instruction_index] = ordinal;
        result.instruction_result_classes[instruction_index] = (u8)machine_selection_instruction_result_class(program, function, instruction);
        result.instruction_side_effects[instruction_index] = machine_selection_instruction_side_effects(instruction);
        if (instruction->result.value != IR_ID_UNDERLYING_INVALID)
        {
            u32 value_index = instruction->result.value;
            if (value_index >= function->value_count)
            {
                result.error = MACHINE_SELECTION_PREPASS_INVALID_VALUE;
                return result;
            }
            if (result.value_definitions[value_index].value != IR_ID_UNDERLYING_INVALID)
            {
                result.error = MACHINE_SELECTION_PREPASS_DUPLICATE_DEFINITION;
                return result;
            }
            result.value_definitions[value_index] = (IrInstructionId){.value = instruction_index};
            result.value_definition_blocks[value_index] = block_index;
            result.value_definition_ordinals[value_index] = ordinal;
            if (instruction->opcode == IR_OPCODE_CONSTANT_INTEGER || instruction->opcode == IR_OPCODE_CONSTANT_FLOAT)
            {
                if (instruction->immediate_count && instruction->immediates)
                {
                    u64 bits = instruction->immediates[0];
                    if (instruction->opcode == IR_OPCODE_CONSTANT_INTEGER && instruction->immediate_is_negative)
                    {
                        bits = 0 - bits;
                    }
                    result.value_constant_bits[value_index] = bits;
                    result.value_flags[value_index] |= MACHINE_SELECTION_VALUE_KNOWN_CONSTANT;
                    if (bits == 0)
                    {
                        result.value_flags[value_index] |= MACHINE_SELECTION_VALUE_KNOWN_ZERO;
                    }
                    else
                    {
                        result.value_flags[value_index] |= MACHINE_SELECTION_VALUE_KNOWN_NONZERO;
                    }
                }
            }
            if (instruction->opcode == IR_OPCODE_LOCAL && machine_selection_local_type_promotable(program, function->values + value_index))
            {
                IrType* local_type = ir_type_from_id(&program->types, function->values[value_index].canonical_type);
                result.value_promotable_local_widths[value_index] = local_type ? (u8)local_type->layout.size : 0;
                result.value_flags[value_index] |= MACHINE_SELECTION_VALUE_PROMOTABLE_LOCAL;
                promotable_candidate_count += 1;
            }
            if (function->values[value_index].is_read_only)
            {
                result.value_flags[value_index] |= MACHINE_SELECTION_VALUE_READ_ONLY;
            }
            if (machine_selection_opcode_is_address(instruction->opcode))
            {
                result.value_flags[value_index] |= MACHINE_SELECTION_VALUE_ADDRESS_CANDIDATE;
            }
        }
        ordinal += 1;
        for (u32 operand_index = 0; operand_index < instruction->operand_count; operand_index += 1)
        {
            IrValueId value = instruction->operands ? instruction->operands[operand_index] : IR_VALUE_ID_INVALID;
            if (value.value >= function->value_count)
            {
                result.error = MACHINE_SELECTION_PREPASS_INVALID_VALUE;
                return result;
            }
            if (result.value_first_use_ordinals[value.value] == MACHINE_SELECTION_INVALID_INDEX)
            {
                result.value_first_use_ordinals[value.value] = result.instruction_ordinals[instruction_index];
            }
            result.value_last_use_ordinals[value.value] = result.instruction_ordinals[instruction_index];
            result.value_use_counts[value.value] += 1;
            if (result.value_use_blocks[value.value] == MACHINE_SELECTION_INVALID_INDEX)
            {
                result.value_use_blocks[value.value] = block_index;
            }
            else if (result.value_use_blocks[value.value] != block_index)
            {
                result.value_use_blocks[value.value] = MACHINE_SELECTION_MULTIPLE_BLOCKS;
            }
            if (instruction->opcode == IR_OPCODE_STORE && operand_index == 0 && (result.value_flags[value.value] & MACHINE_SELECTION_VALUE_PROMOTABLE_LOCAL))
            {
                result.value_local_store_counts[value.value] += 1;
            }
            if (operand_index == 0 && (instruction->opcode == IR_OPCODE_LOAD || instruction->opcode == IR_OPCODE_STORE ||
                                       instruction->opcode == IR_OPCODE_ATOMIC_LOAD || instruction->opcode == IR_OPCODE_ATOMIC_STORE))
            {
                result.value_flags[value.value] |= MACHINE_SELECTION_VALUE_ADDRESS_CANDIDATE;
            }
        }
    }
    result.ordinal_count = ordinal;
    // Any use other than a non-volatile, same-width load/store place makes
    // taking the address observable and therefore disqualifies promotion.
    // This is intentionally conservative and shared by both targets;
    // target-specific vector/ABI details remain in selectors.  One pass over
    // every operand decides it for all candidates at once: asking the same
    // question per candidate rescanned the whole function once per local that
    // survived, which is quadratic in a function's size.
    if (promotable_candidate_count)
    {
        for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
        {
            IrInstruction* instruction = function->instructions + instruction_index;
            if (!instruction->operands)
            {
                continue;
            }
            for (u32 operand_index = 0; operand_index < instruction->operand_count; operand_index += 1)
            {
                u32 value_index = instruction->operands[operand_index].value;
                if (value_index >= function->value_count ||
                    (result.value_flags[value_index] & MACHINE_SELECTION_VALUE_PROMOTABLE_LOCAL) == 0)
                {
                    continue;
                }
                bool legal_place = !instruction->volatile_access && operand_index == 0 &&
                                   (instruction->opcode == IR_OPCODE_LOAD || instruction->opcode == IR_OPCODE_STORE);
                if (legal_place)
                {
                    IrTypeId access_type_id = instruction->opcode == IR_OPCODE_LOAD
                                                  ? instruction->canonical_type
                                                  : instruction->operand_count > 1 && instruction->operands[1].value < function->value_count
                                                        ? function->values[instruction->operands[1].value].canonical_type
                                                        : IR_TYPE_ID_INVALID;
                    IrType* access_type = ir_type_from_id(&program->types, access_type_id);
                    legal_place = access_type && access_type->layout.resolved &&
                                  access_type->layout.size == result.value_promotable_local_widths[value_index];
                }
                if (!legal_place)
                {
                    result.value_flags[value_index] &= (u8)~MACHINE_SELECTION_VALUE_PROMOTABLE_LOCAL;
                    result.value_promotable_local_widths[value_index] = 0;
                }
            }
        }
    }
    result.valid = true;
    return result;
}

bool machine_selection_prepass_value_flag(MachineSelectionPrepass const* prepass, IrValueId value, MachineSelectionValueFlag flag)
{
    return prepass && value.value < prepass->value_count && (prepass->value_flags[value.value] & (u8)flag) != 0;
}

bool machine_selection_prepass_instruction_owned_by(MachineSelectionPrepass const* prepass, IrInstructionId instruction, u32 block_index)
{
    return prepass && instruction.value < prepass->instruction_count && prepass->instruction_owner_blocks[instruction.value].value == block_index;
}

MachineSelectionResultClass machine_selection_result_class(MachineSelectionPrepass const* prepass, IrInstructionId instruction)
{
    return prepass && instruction.value < prepass->instruction_count ? (MachineSelectionResultClass)prepass->instruction_result_classes[instruction.value]
                                                                       : MACHINE_SELECTION_RESULT_NONE;
}

MachineSelectionSideEffect machine_selection_side_effects(MachineSelectionPrepass const* prepass, IrInstructionId instruction)
{
    return prepass && instruction.value < prepass->instruction_count ? (MachineSelectionSideEffect)prepass->instruction_side_effects[instruction.value]
                                                                       : MACHINE_SELECTION_SIDE_EFFECT_UNKNOWN;
}

MachineSelectionRuleContext machine_selection_rule_context(MachineSelectionPrepass const* prepass, IrInstructionId instruction,
                                                           Target target)
{
    MachineSelectionRuleContext context = {.opcode = IR_OPCODE_COUNT, .target = target};
    if (!prepass || !prepass->function || instruction.value >= prepass->instruction_count)
    {
        return context;
    }
    IrInstruction* row = prepass->function->instructions + instruction.value;
    context.opcode = row->opcode;
    context.result_class = machine_selection_result_class(prepass, instruction);
    context.side_effects = machine_selection_side_effects(prepass, instruction);
    context.operand_count = row->operand_count;
    context.target_count = row->target_count;
    context.immediate_count = row->immediate_count;
    context.volatile_access = row->volatile_access;
    context.vector_features = target.cpu_arch == CPU_ARCH_AARCH64 ||
                              (target.cpu_arch == CPU_ARCH_X86_64 && target_cpu_feature_has(target, TARGET_CPU_FEATURE_X86_AVX512F) &&
                               target_cpu_feature_has(target, TARGET_CPU_FEATURE_X86_AVX512BW));
    if (row->result.value < prepass->value_count)
    {
        context.known_constant = machine_selection_prepass_value_flag(prepass, row->result, MACHINE_SELECTION_VALUE_KNOWN_CONSTANT);
        context.address_candidate = machine_selection_prepass_value_flag(prepass, row->result, MACHINE_SELECTION_VALUE_ADDRESS_CANDIDATE);
        context.promotable_local = machine_selection_prepass_value_flag(prepass, row->result, MACHINE_SELECTION_VALUE_PROMOTABLE_LOCAL);
    }
    return context;
}

void machine_selection_counters_reset(MachineSelectionCounters* counters)
{
    if (counters)
    {
        memset(counters, 0, sizeof(*counters));
    }
}

// Generated from machine_select_rules.h.  Keeping the generated decision
// tree in this implementation unit means the machine module remains one
// unity-included source and no build-graph edit is required for new rules.
#include <buster/lib/compiler/codegen/machine_select_generated.c>
