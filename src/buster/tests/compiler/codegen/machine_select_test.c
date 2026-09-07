#include <buster/tests/compiler/codegen/machine_select_test.h>

#if BUSTER_INCLUDE_TESTS

// Production selector regressions: normalized MIR survives linked-row
// reordering; checked and prevalidated entries agree; malformed shape checks
// remain fail-closed without building unused selection facts.

#include <buster/lib/compiler/codegen/machine.h>
#include <buster/lib/compiler/frontend/c/c.h>
#include <buster/lib/string.h>

BUSTER_GLOBAL_LOCAL IrProgram* machine_selection_test_compile(Arena* arena, String8 source, Target target)
{
    CPreprocessResult tokens = c_preprocess(arena, source, (CPreprocessOptions){0});
    if (tokens.error_count)
    {
        return 0;
    }
    CParseResult parse = c_parse(arena, tokens);
    if (parse.diagnostic_count)
    {
        return 0;
    }
    CIRLowerResult lowered = c_lower_to_ir(arena, S8("machine-selection.c"), tokens, parse, target);
    IrProgram* result;
    if (lowered.diagnostic_count)
    {
        result = 0;
    }
    else
    {
        result = lowered.program;
    }

    return result;
}

BUSTER_GLOBAL_LOCAL IrFunction* machine_selection_test_find(IrProgram* program, String8 name)
{
    if (program)
    {
        for (u32 module_index = 0; module_index < program->module_count; module_index += 1)
        {
            IrModule* module = program->modules + module_index;
            for (u32 function_index = 0; function_index < module->function_count; function_index += 1)
            {
                if (string_equal(module->functions[function_index].name, name))
                {
                    return module->functions + function_index;
                }
            }
        }
    }

    return 0;
}

typedef struct MachineSelectionTestOrderPair MachineSelectionTestOrderPair;
struct MachineSelectionTestOrderPair
{
    IrBlock* block;
    IrInstructionId previous;
    IrInstructionId first;
    IrInstructionId second;
};

BUSTER_GLOBAL_LOCAL MachineSelectionTestOrderPair machine_selection_test_find_order_pair(IrFunction* function)
{
    MachineSelectionTestOrderPair result = {0};
    result.previous = IR_INSTRUCTION_ID_INVALID;
    result.first = IR_INSTRUCTION_ID_INVALID;
    result.second = IR_INSTRUCTION_ID_INVALID;
    if (function)
    {
        for (u32 block_index = 0; block_index < function->block_count && result.second.value == IR_ID_UNDERLYING_INVALID; block_index += 1)
        {
            IrBlock* block = function->blocks + block_index;
            IrInstructionId previous = IR_INSTRUCTION_ID_INVALID;
            for (IrInstructionId first = block->first_instruction; first.value != IR_ID_UNDERLYING_INVALID && !result.block;
                 first = function->instructions[first.value].next)
            {
                IrInstructionId second = function->instructions[first.value].next;
                if (second.value == IR_ID_UNDERLYING_INVALID || second.value == block->last_instruction.value)
                {
                    previous = first;
                    continue;
                }
                IrInstruction* first_instruction = function->instructions + first.value;
                IrInstruction* second_instruction = function->instructions + second.value;
                bool first_reorderable = first_instruction->opcode == IR_OPCODE_CONSTANT_INTEGER || first_instruction->opcode == IR_OPCODE_CONSTANT_FLOAT ||
                                         first_instruction->opcode == IR_OPCODE_CAST || first_instruction->opcode == IR_OPCODE_UNARY ||
                                         first_instruction->opcode == IR_OPCODE_BINARY;
                bool second_reorderable = second_instruction->opcode == IR_OPCODE_CONSTANT_INTEGER || second_instruction->opcode == IR_OPCODE_CONSTANT_FLOAT ||
                                          second_instruction->opcode == IR_OPCODE_CAST || second_instruction->opcode == IR_OPCODE_UNARY ||
                                          second_instruction->opcode == IR_OPCODE_BINARY;
                bool independent = first_reorderable && second_reorderable && first_instruction->result.value != IR_ID_UNDERLYING_INVALID &&
                                   second_instruction->result.value != IR_ID_UNDERLYING_INVALID &&
                                   !first_instruction->volatile_access && !second_instruction->volatile_access;
                for (u32 operand_index = 0; independent && operand_index < first_instruction->operand_count; operand_index += 1)
                {
                    independent &= first_instruction->operands[operand_index].value != second_instruction->result.value;
                }
                for (u32 operand_index = 0; independent && operand_index < second_instruction->operand_count; operand_index += 1)
                {
                    independent &= second_instruction->operands[operand_index].value != first_instruction->result.value;
                }
                if (independent)
                {
                    result.block = block;
                    result.previous = previous;
                    result.first = first;
                    result.second = second;
                }
                previous = first;
            }
        }
    }

    return result;
}

BUSTER_GLOBAL_LOCAL u32 machine_selection_test_virtual_origin(MachineFunction const* function, u32 virtual_register)
{
    u32 result;
    if (!function || virtual_register >= function->virtual_register_count)
    {
        result = UINT32_MAX;
    }
    else
    {
        result = function->virtual_registers[virtual_register].typed_origin;
    }

    return result;
}

BUSTER_GLOBAL_LOCAL bool machine_selection_test_ref_equal(MachineFunction const* before, MachineFunction const* after, MachineRef before_ref, MachineRef after_ref)
{
    MachineRefKind before_kind = machine_ref_kind(before_ref);
    if (before_kind != machine_ref_kind(after_ref))
    {
        return false;
    }
    if (before_kind == MACHINE_REF_VIRTUAL_REGISTER)
    {
        return machine_selection_test_virtual_origin(before, machine_ref_payload(before_ref)) ==
               machine_selection_test_virtual_origin(after, machine_ref_payload(after_ref));
    }
    if (before_kind == MACHINE_REF_IMMEDIATE)
    {
        u32 before_index = machine_ref_payload(before_ref);
        u32 after_index = machine_ref_payload(after_ref);
        return before_index < before->immediate_count && after_index < after->immediate_count && before->immediates[before_index] == after->immediates[after_index];
    }
    return before_ref == after_ref;
}

BUSTER_GLOBAL_LOCAL bool machine_selection_test_instruction_equal(MachineFunction const* before, MachineFunction const* after,
                                                                   MachineInstruction const* before_instruction, MachineInstruction const* after_instruction)
{
    if (!before_instruction || !after_instruction || before_instruction->opcode != after_instruction->opcode ||
        before_instruction->payload != after_instruction->payload || before_instruction->flags != after_instruction->flags)
    {
        return false;
    }
    MachineOpcodeInfo const* info = machine_opcode_info(before_instruction->opcode);
    u32 operand_count = info ? info->operand_count : BUSTER_ARRAY_LENGTH(before_instruction->operands);
    for (u32 operand_index = 0; operand_index < operand_count; operand_index += 1)
    {
        if (!machine_selection_test_ref_equal(before, after, before_instruction->operands[operand_index], after_instruction->operands[operand_index]))
        {
            return false;
        }
    }
    return true;
}

BUSTER_GLOBAL_LOCAL u32 machine_selection_test_instruction_origin(MachineFunction const* function, MachineInstruction const* instruction)
{
    if (function && instruction)
    {
        MachineOpcodeInfo const* info = machine_opcode_info(instruction->opcode);
        u32 operand_count = info ? info->operand_count : BUSTER_ARRAY_LENGTH(instruction->operands);
        for (u32 operand_index = 0; operand_index < operand_count; operand_index += 1)
        {
            MachineOperandRole role = (MachineOperandRole)(info ? (info->operand_info[operand_index] & ((1u << MACHINE_OPERAND_ROLE_BITS) - 1u))
                                                                : MACHINE_OPERAND_ROLE_NONE);
            if ((role == MACHINE_OPERAND_ROLE_DEFINE || role == MACHINE_OPERAND_ROLE_USE_DEFINE) &&
                machine_ref_kind(instruction->operands[operand_index]) == MACHINE_REF_VIRTUAL_REGISTER)
            {
                return machine_selection_test_virtual_origin(function, machine_ref_payload(instruction->operands[operand_index]));
            }
        }
    }

    return UINT32_MAX;
}

BUSTER_GLOBAL_LOCAL bool machine_selection_test_stream_equal(Arena* arena, MachineSelectResult* before, MachineSelectResult* after)
{
    // Swapping independent IR rows may renumber virtual registers and reorder
    // their immediate side-table entries. Match every complete machine row by
    // its stable defining value, then compare opcode, flags, payload, and all
    // live operands (with virtual/immediate refs normalized).
    if (!arena || !before || !after || !before->supported || !after->supported ||
        machine_verify_function(&before->function).error != MACHINE_VERIFY_NONE || machine_verify_function(&after->function).error != MACHINE_VERIFY_NONE)
    {
        return false;
    }
    if (before->function.virtual_register_count != after->function.virtual_register_count ||
        before->function.instruction_count != after->function.instruction_count)
    {
        return false;
    }
    u8* matched = arena_allocate(arena, u8, after->function.instruction_count ? after->function.instruction_count : 1);
    for (u32 instruction_index = 0; instruction_index < after->function.instruction_count; instruction_index += 1)
    {
        matched[instruction_index] = 0;
    }
    for (u32 before_index = 0; before_index < before->function.instruction_count; before_index += 1)
    {
        MachineInstruction const* before_instruction = before->function.instructions + before_index;
        u32 before_origin = machine_selection_test_instruction_origin(&before->function, before_instruction);
        u32 after_index = 0;
        for (; after_index < after->function.instruction_count; after_index += 1)
        {
            MachineInstruction const* after_instruction = after->function.instructions + after_index;
            if (!matched[after_index] && machine_selection_test_instruction_origin(&after->function, after_instruction) == before_origin &&
                machine_selection_test_instruction_equal(&before->function, &after->function, before_instruction, after_instruction))
            {
                matched[after_index] = 1;
                break;
            }
        }
        if (after_index == after->function.instruction_count)
        {
            return false;
        }
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool machine_selection_test_order_divergence(Arena* arena, IrProgram* program, IrFunction* function, Target target)
{
    bool equivalent = false;
    if (arena && program && function && machine_selection_validate_function(arena, program, function) == MACHINE_SELECTION_VALIDATION_NONE)
    {
        MachineSelectionTestOrderPair pair = machine_selection_test_find_order_pair(function);
        if (pair.block)
        {
            MachineSelectResult before = machine_select_canonical_function(arena, program, function, target);
            IrInstructionId saved_first = pair.block->first_instruction;
            IrInstructionId saved_previous_next = pair.previous.value == IR_ID_UNDERLYING_INVALID ? IR_INSTRUCTION_ID_INVALID : function->instructions[pair.previous.value].next;
            IrInstructionId saved_first_next = function->instructions[pair.first.value].next;
            IrInstructionId saved_second_next = function->instructions[pair.second.value].next;
            if (pair.previous.value == IR_ID_UNDERLYING_INVALID)
            {
                pair.block->first_instruction = pair.second;
            }
            else
            {
                function->instructions[pair.previous.value].next = pair.second;
            }
            function->instructions[pair.second.value].next = pair.first;
            function->instructions[pair.first.value].next = saved_second_next;

            MachineSelectResult after = machine_select_canonical_function(arena, program, function, target);
            equivalent = before.supported == after.supported && before.failed_opcode == after.failed_opcode &&
                         machine_selection_test_stream_equal(arena, &before, &after);

            pair.block->first_instruction = saved_first;
            if (pair.previous.value != IR_ID_UNDERLYING_INVALID)
            {
                function->instructions[pair.previous.value].next = saved_previous_next;
            }
            function->instructions[pair.first.value].next = saved_first_next;
            function->instructions[pair.second.value].next = saved_second_next;
        }
    }
    return equivalent;
}

BUSTER_GLOBAL_LOCAL bool machine_selection_test_rejected(Arena* arena, IrProgram* program, IrFunction* function, Target target,
                                                        MachineSelectionValidationError expected)
{
    MachineSelectionValidationError error = machine_selection_validate_function(arena, program, function);
    MachineSelectResult selected = machine_select_canonical_function(arena, program, function, target);
    return error == expected && !selected.supported;
}

UnitTestResult machine_selection_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    String8 source = S8("int selection_add(int a, int b) { int local = 7; return a + local + b; }\n"
                         "int selection_memory(int *p) { *p += 1; return *p; }\n"
                         "int selection_order(void) { return 1 + 2; }\n");
    CpuArch arches[] = {CPU_ARCH_X86_64, CPU_ARCH_AARCH64};
    for (u32 arch_index = 0; arch_index < BUSTER_ARRAY_LENGTH(arches); arch_index += 1)
    {
        Target target = {.cpu_arch = arches[arch_index], .os = OPERATING_SYSTEM_LINUX};
        IrProgram* program = machine_selection_test_compile(arguments->arena, source, target);
        IrFunction* add = machine_selection_test_find(program, S8("selection_add"));
        IrFunction* memory = machine_selection_test_find(program, S8("selection_memory"));
        IrFunction* order = machine_selection_test_find(program, S8("selection_order"));
        BUSTER_TEST(arguments, program != 0 && add != 0 && memory != 0 && order != 0);
        if (add && memory && order)
        {
            IrFunction* functions[] = {add, memory, order};
            for (u32 function_index = 0; function_index < BUSTER_ARRAY_LENGTH(functions); function_index += 1)
            {
                IrFunction* function = functions[function_index];
                u64 before = arguments->arena->position;
                BUSTER_TEST(arguments, machine_selection_validate_function(arguments->arena, program, function) == MACHINE_SELECTION_VALIDATION_NONE);
                // One byte per row plus one per value replaces the four
                // discarded u32 fact arrays and the old visited-row array.
                BUSTER_TEST(arguments, arguments->arena->position - before == (u64)function->instruction_count + function->value_count);
                MachineSelectResult checked = machine_select_canonical_function(arguments->arena, program, function, target);
                MachineSelectResult validated = machine_select_validated_canonical_function(arguments->arena, program, function, target, false, 0);
                BUSTER_TEST(arguments, checked.supported && validated.supported);
                BUSTER_TEST(arguments, checked.failed_opcode == validated.failed_opcode);
                BUSTER_TEST(arguments, machine_selection_test_stream_equal(arguments->arena, &checked, &validated));
            }
            BUSTER_TEST(arguments, machine_selection_test_order_divergence(arguments->arena, program, order, target));

            IrInstruction* operand_probe = 0;
            IrInstruction* definition_probe = 0;
            IrInstruction* second_definition = 0;
            for (u32 instruction_index = 0; instruction_index < add->instruction_count; instruction_index += 1)
            {
                IrInstruction* instruction = add->instructions + instruction_index;
                if (instruction->operand_count && !operand_probe)
                {
                    operand_probe = instruction;
                }
                if (instruction->result.value != IR_ID_UNDERLYING_INVALID)
                {
                    if (!definition_probe)
                    {
                        definition_probe = instruction;
                    }
                    else if (!second_definition)
                    {
                        second_definition = instruction;
                    }
                }
            }
            BUSTER_TEST(arguments, operand_probe && definition_probe && second_definition && add->block_count);
            if (operand_probe && definition_probe && second_definition && add->block_count)
            {
                IrValueId* saved_operands = operand_probe->operands;
                operand_probe->operands = 0;
                BUSTER_TEST(arguments, machine_selection_test_rejected(arguments->arena, program, add, target, MACHINE_SELECTION_VALIDATION_INVALID_VALUE));
                operand_probe->operands = saved_operands;

                IrValueId saved_operand = operand_probe->operands[0];
                operand_probe->operands[0] = (IrValueId){.value = add->value_count};
                BUSTER_TEST(arguments, machine_selection_test_rejected(arguments->arena, program, add, target, MACHINE_SELECTION_VALIDATION_INVALID_VALUE));
                operand_probe->operands[0] = saved_operand;

                IrValueId saved_result = definition_probe->result;
                definition_probe->result = (IrValueId){.value = add->value_count};
                BUSTER_TEST(arguments, machine_selection_test_rejected(arguments->arena, program, add, target, MACHINE_SELECTION_VALIDATION_INVALID_VALUE));
                definition_probe->result = second_definition->result;
                BUSTER_TEST(arguments, machine_selection_test_rejected(arguments->arena, program, add, target, MACHINE_SELECTION_VALIDATION_DUPLICATE_DEFINITION));
                definition_probe->result = saved_result;

                u8 saved_opcode = operand_probe->opcode;
                operand_probe->opcode = UINT8_MAX;
                BUSTER_TEST(arguments, machine_selection_test_rejected(arguments->arena, program, add, target, MACHINE_SELECTION_VALIDATION_INVALID_OPCODE));
                operand_probe->opcode = saved_opcode;

                IrBlock* block = add->blocks;
                IrInstruction* first = add->instructions + block->first_instruction.value;
                IrInstructionId saved_next = first->next;
                first->next = (IrInstructionId){.value = add->instruction_count};
                BUSTER_TEST(arguments, machine_selection_test_rejected(arguments->arena, program, add, target, MACHINE_SELECTION_VALIDATION_OWNERSHIP));
                first->next = block->first_instruction;
                BUSTER_TEST(arguments, machine_selection_test_rejected(arguments->arena, program, add, target, MACHINE_SELECTION_VALIDATION_OWNERSHIP));
                first->next = saved_next;

                IrInstructionId saved_last = block->last_instruction;
                block->last_instruction = IR_INSTRUCTION_ID_INVALID;
                BUSTER_TEST(arguments, machine_selection_test_rejected(arguments->arena, program, add, target, MACHINE_SELECTION_VALIDATION_OWNERSHIP));
                block->last_instruction = saved_last;

                IrInstructionId saved_first = block->first_instruction;
                block->first_instruction = first->next;
                BUSTER_TEST(arguments, machine_selection_test_rejected(arguments->arena, program, add, target, MACHINE_SELECTION_VALIDATION_OWNERSHIP));
                block->first_instruction = saved_first;

                BUSTER_TEST(arguments, machine_selection_validate_function(arguments->arena, program, add) == MACHINE_SELECTION_VALIDATION_NONE);
            }
            BUSTER_TEST(arguments, machine_selection_validate_function(0, program, add) == MACHINE_SELECTION_VALIDATION_INVALID_ARGUMENT);
            BUSTER_TEST(arguments, machine_selection_validate_function(arguments->arena, 0, add) == MACHINE_SELECTION_VALIDATION_INVALID_ARGUMENT);
            BUSTER_TEST(arguments, machine_selection_validate_function(arguments->arena, program, 0) == MACHINE_SELECTION_VALIDATION_INVALID_ARGUMENT);
        }
    }
    return result;
}

#endif
