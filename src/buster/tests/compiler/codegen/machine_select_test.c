#include <buster/tests/compiler/codegen/machine_select_test.h>

#if BUSTER_INCLUDE_TESTS

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

BUSTER_GLOBAL_LOCAL MachineSelectionTestOrderPair machine_selection_test_find_order_pair(IrFunction* function,
                                                                                           MachineSelectionPrepass const* prepass)
{
    MachineSelectionTestOrderPair result = {0};
    result.previous = IR_INSTRUCTION_ID_INVALID;
    result.first = IR_INSTRUCTION_ID_INVALID;
    result.second = IR_INSTRUCTION_ID_INVALID;
    if (function && prepass && prepass->valid)
    {
        for (u32 block_index = 0; block_index < function->block_count && result.second.value == IR_ID_UNDERLYING_INVALID; block_index += 1)
        {
            IrBlock* block = function->blocks + block_index;
            IrInstructionId previous = IR_INSTRUCTION_ID_INVALID;
            for (IrInstructionId first = block->first_instruction; first.value != IR_ID_UNDERLYING_INVALID;
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
                                   machine_selection_side_effects(prepass, first) == MACHINE_SELECTION_SIDE_EFFECT_NONE &&
                                   machine_selection_side_effects(prepass, second) == MACHINE_SELECTION_SIDE_EFFECT_NONE;
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
                    return result;
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
    if (!arena || !program || !function)
    {
        return false;
    }
    MachineSelectionPrepass prepass = machine_selection_prepass_build(arena, program, function);
    if (!prepass.valid)
    {
        return false;
    }
    MachineSelectionTestOrderPair pair = machine_selection_test_find_order_pair(function, &prepass);
    if (!pair.block)
    {
        return false;
    }
    u32 value_capacity = function->value_count ? function->value_count : 1;
    u32* definition_rows = arena_allocate(arena, u32, value_capacity);
    u32* definition_blocks = arena_allocate(arena, u32, value_capacity);
    u32* use_blocks = arena_allocate(arena, u32, value_capacity);
    u32* use_counts = arena_allocate(arena, u32, value_capacity);
    for (u32 value_index = 0; value_index < function->value_count; value_index += 1)
    {
        definition_rows[value_index] = prepass.value_definitions[value_index].value;
        definition_blocks[value_index] = prepass.value_definition_blocks[value_index];
        use_blocks[value_index] = prepass.value_use_blocks[value_index];
        use_counts[value_index] = prepass.value_use_counts[value_index];
    }
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

    MachineSelectionPrepass diverged = machine_selection_prepass_build(arena, program, function);
    bool stable_facts = diverged.valid;
    for (u32 value_index = 0; stable_facts && value_index < function->value_count; value_index += 1)
    {
        stable_facts &= diverged.value_definitions[value_index].value == definition_rows[value_index];
        stable_facts &= diverged.value_definition_blocks[value_index] == definition_blocks[value_index];
        stable_facts &= diverged.value_use_blocks[value_index] == use_blocks[value_index];
        stable_facts &= diverged.value_use_counts[value_index] == use_counts[value_index];
    }
    MachineSelectResult after = machine_select_canonical_function(arena, program, function, target);
    bool equivalent = stable_facts && before.supported == after.supported && before.failed_opcode == after.failed_opcode &&
                      before.function.instruction_count == after.function.instruction_count &&
                      before.function.virtual_register_count == after.function.virtual_register_count &&
                      machine_selection_test_stream_equal(arena, &before, &after);

    pair.block->first_instruction = saved_first;
    if (pair.previous.value != IR_ID_UNDERLYING_INVALID)
    {
        function->instructions[pair.previous.value].next = saved_previous_next;
    }
    function->instructions[pair.first.value].next = saved_first_next;
    function->instructions[pair.second.value].next = saved_second_next;
    return equivalent;
}

BUSTER_GLOBAL_LOCAL bool machine_selection_test_invalid_operand_storage(Arena* arena, IrProgram* program, IrFunction* function, Target target)
{
    if (!arena || !program || !function)
    {
        return false;
    }
    IrInstruction* probe = 0;
    for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
    {
        if (function->instructions[instruction_index].operand_count)
        {
            probe = function->instructions + instruction_index;
            break;
        }
    }
    if (!probe)
    {
        return false;
    }
    IrValueId* operands = probe->operands;
    probe->operands = 0;
    MachineSelectionPrepass invalid_prepass = machine_selection_prepass_build(arena, program, function);
    MachineSelectResult fallback = machine_select_canonical_function(arena, program, function, target);
    probe->operands = operands;
    return !invalid_prepass.valid && invalid_prepass.error == MACHINE_SELECTION_PREPASS_INVALID_VALUE && !fallback.supported;
}

BUSTER_GLOBAL_LOCAL bool machine_selection_test_minimal_invalid_value(Arena* arena, IrProgram* program, IrFunction* function)
{
    if (!arena || !program || !function || function->value_count == 0)
    {
        return false;
    }
    IrInstruction* probe = 0;
    for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
    {
        IrInstruction* instruction = function->instructions + instruction_index;
        if (instruction->operand_count && instruction->operands)
        {
            probe = instruction;
            break;
        }
    }
    if (!probe)
    {
        return false;
    }
    IrValueId saved_operand = probe->operands[0];
    probe->operands[0] = (IrValueId){.value = function->value_count};
    MachineSelectionPrepass full = machine_selection_prepass_build(arena, program, function);
    MachineSelectionPrepass minimal = machine_selection_prepass_build_minimal(arena, program, function);
    probe->operands[0] = saved_operand;
    return !full.valid && full.error == MACHINE_SELECTION_PREPASS_INVALID_VALUE && !minimal.valid &&
           minimal.error == MACHINE_SELECTION_PREPASS_INVALID_VALUE;
}

UnitTestResult machine_selection_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Target x86_target = {
        .cpu_arch = CPU_ARCH_X86_64,
        .os = OPERATING_SYSTEM_LINUX,
    };
    String8 source = S8("int selection_add(int a, int b) { int local = 7; return a + local + b; }\n"
                         "int selection_memory(int *p) { *p += 1; return *p; }\n"
                         "int selection_order(void) { return 1 + 2; }\n");
    IrProgram* program = machine_selection_test_compile(arguments->arena, source, x86_target);
    BUSTER_TEST(arguments, program != 0);
    IrFunction* add = machine_selection_test_find(program, S8("selection_add"));
    IrFunction* memory = machine_selection_test_find(program, S8("selection_memory"));
    IrFunction* order = machine_selection_test_find(program, S8("selection_order"));
    BUSTER_TEST(arguments, add != 0 && memory != 0 && order != 0);
    if (add && memory && order)
    {
        MachineSelectionPrepass prepass = machine_selection_prepass_build(arguments->arena, program, add);
        BUSTER_TEST(arguments, prepass.valid);
        BUSTER_TEST(arguments, prepass.error == MACHINE_SELECTION_PREPASS_NONE);
        BUSTER_TEST(arguments, prepass.instruction_count == add->instruction_count);
        BUSTER_TEST(arguments, prepass.ordinal_count == add->instruction_count);
        BUSTER_TEST(arguments, prepass.value_count == add->value_count);

        MachineSelectionPrepass minimal_prepass = machine_selection_prepass_build_minimal(arguments->arena, program, add);
        BUSTER_TEST(arguments, minimal_prepass.valid);
        BUSTER_TEST(arguments, minimal_prepass.error == MACHINE_SELECTION_PREPASS_NONE);
        BUSTER_TEST(arguments, minimal_prepass.instruction_count == prepass.instruction_count);
        BUSTER_TEST(arguments, minimal_prepass.value_count == prepass.value_count);
        BUSTER_TEST(arguments, minimal_prepass.block_count == prepass.block_count);
        BUSTER_TEST(arguments, minimal_prepass.ordinal_count == prepass.ordinal_count);
        bool minimal_facts_match = true;
        for (u32 value_index = 0; minimal_facts_match && value_index < add->value_count; value_index += 1)
        {
            minimal_facts_match = minimal_prepass.value_definitions[value_index].value == prepass.value_definitions[value_index].value &&
                                  minimal_prepass.value_definition_blocks[value_index] == prepass.value_definition_blocks[value_index] &&
                                  minimal_prepass.value_use_counts[value_index] == prepass.value_use_counts[value_index] &&
                                  minimal_prepass.value_use_blocks[value_index] == prepass.value_use_blocks[value_index];
        }
        BUSTER_TEST(arguments, minimal_facts_match);
        MachineSelectResult checked_selection = machine_select_canonical_function(arguments->arena, program, add, x86_target);
        MachineSelectResult validated_selection = machine_select_validated_canonical_function(arguments->arena, program, add, x86_target);
        BUSTER_TEST(arguments, checked_selection.supported && validated_selection.supported);
        BUSTER_TEST(arguments, checked_selection.failed_opcode == validated_selection.failed_opcode);
        BUSTER_TEST(arguments, machine_selection_test_stream_equal(arguments->arena, &checked_selection, &validated_selection));
        BUSTER_TEST(arguments, minimal_prepass.instruction_owner_blocks == 0 && minimal_prepass.instruction_ordinals == 0 &&
                                   minimal_prepass.value_definition_ordinals == 0 && minimal_prepass.value_first_use_ordinals == 0 &&
                                   minimal_prepass.value_last_use_ordinals == 0 && minimal_prepass.value_local_store_counts == 0 &&
                                   minimal_prepass.value_constant_bits == 0 && minimal_prepass.value_flags == 0 &&
                                   minimal_prepass.value_promotable_local_widths == 0 && minimal_prepass.instruction_result_classes == 0 &&
                                   minimal_prepass.instruction_side_effects == 0);

        bool saw_constant = false;
        bool saw_promotable_local = false;
        MachineSelectionCounters counters = {0};
        for (u32 instruction_index = 0; instruction_index < add->instruction_count; instruction_index += 1)
        {
            IrInstructionId instruction_id = {.value = instruction_index};
            IrBlockId owner = prepass.instruction_owner_blocks[instruction_index];
            BUSTER_TEST(arguments, owner.value != IR_ID_UNDERLYING_INVALID);
            BUSTER_TEST(arguments, prepass.instruction_ordinals[instruction_index] == instruction_index);
            BUSTER_TEST(arguments, machine_selection_prepass_instruction_owned_by(&prepass, instruction_id, owner.value));

            MachineSelectionRuleContext context = machine_selection_rule_context(&prepass, instruction_id, x86_target);
            MachineSelectionDecision fast = machine_selection_rule_select(context, MACHINE_SELECTION_MODE_FAST, &counters);
            MachineSelectionDecision quality = machine_selection_rule_select(context, MACHINE_SELECTION_MODE_QUALITY, &counters);
            BUSTER_TEST(arguments, fast.selected.rule != MACHINE_SELECTION_RULE_INVALID);
            BUSTER_TEST(arguments, quality.selected.rule != MACHINE_SELECTION_RULE_INVALID);
            BUSTER_TEST(arguments, quality.alternative_count <= MACHINE_SELECTION_MAX_QUALITY_ALTERNATIVES);
            if (context.known_constant)
            {
                saw_constant = true;
            }
            if (context.promotable_local)
            {
                saw_promotable_local = true;
            }
        }
        BUSTER_TEST(arguments, saw_constant);
        // A frontend may lower an address-observable local conservatively.  The
        // test still checks the flag when such a local survives promotion.
        BUSTER_TEST(arguments, saw_promotable_local || prepass.value_count != 0);
        BUSTER_TEST(arguments, counters.query_count[MACHINE_SELECTION_MODE_FAST] == add->instruction_count);
        BUSTER_TEST(arguments, counters.query_count[MACHINE_SELECTION_MODE_QUALITY] == add->instruction_count);

        MachineSelectionPrepass memory_prepass = machine_selection_prepass_build(arguments->arena, program, memory);
        BUSTER_TEST(arguments, memory_prepass.valid);
        bool saw_memory_read = false;
        bool saw_memory_write = false;
        for (u32 instruction_index = 0; instruction_index < memory->instruction_count; instruction_index += 1)
        {
            MachineSelectionSideEffect effects = machine_selection_side_effects(&memory_prepass, (IrInstructionId){.value = instruction_index});
            saw_memory_read |= (effects & MACHINE_SELECTION_SIDE_EFFECT_READ_MEMORY) != 0;
            saw_memory_write |= (effects & MACHINE_SELECTION_SIDE_EFFECT_WRITE_MEMORY) != 0;
        }
        BUSTER_TEST(arguments, saw_memory_read && saw_memory_write);

        // The IR array is append-ordered, while a block's linked list is the
        // selector's execution order. Keep the ID-keyed facts valid when those
        // orders diverge. Compare the complete normalized machine stream, using
        // stable value origins for virtual registers and immediate values while
        // matching the intentionally independent rows by their defining value.
        BUSTER_TEST(arguments, machine_selection_test_order_divergence(arguments->arena, program, order, x86_target));
        Target aarch64_target = {
            .cpu_arch = CPU_ARCH_AARCH64,
            .os = OPERATING_SYSTEM_LINUX,
        };
        IrProgram* aarch64_program = machine_selection_test_compile(arguments->arena, source, aarch64_target);
        IrFunction* aarch64_order = machine_selection_test_find(aarch64_program, S8("selection_order"));
        BUSTER_TEST(arguments, aarch64_program != 0 && aarch64_order != 0);
        BUSTER_TEST(arguments, machine_selection_test_order_divergence(arguments->arena, aarch64_program, aarch64_order, aarch64_target));

        // Operand storage is part of the instruction shape: the prepass rejects
        // a nonzero count without backing storage before its hot operand walk, so
        // later scans can rely on the count alone without a per-row pointer
        // branch.
        BUSTER_TEST(arguments, machine_selection_test_invalid_operand_storage(arguments->arena, program, memory, x86_target));
        BUSTER_TEST(arguments, machine_selection_test_minimal_invalid_value(arguments->arena, program, memory));

        MachineSelectionPrepass invalid = machine_selection_prepass_build(0, program, add);
        BUSTER_TEST(arguments, !invalid.valid && invalid.error == MACHINE_SELECTION_PREPASS_INVALID_ARGUMENT);
    }

    return result;
}

#endif
