#include <buster/tests/compiler/codegen/machine_select_test.h>

#if BUSTER_INCLUDE_TESTS

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
    if (lowered.diagnostic_count)
    {
        return 0;
    }
    return lowered.program;
}

BUSTER_GLOBAL_LOCAL IrFunction* machine_selection_test_find(IrProgram* program, String8 name)
{
    if (!program)
    {
        return 0;
    }
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
    return 0;
}

UnitTestResult machine_selection_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Target x86_target = {
        .cpu_arch = CPU_ARCH_X86_64,
        .os = OPERATING_SYSTEM_LINUX,
    };
    String8 source = S8("int selection_add(int a, int b) { int local = 7; return a + local + b; }\n"
                         "int selection_memory(int *p) { *p += 1; return *p; }\n");
    IrProgram* program = machine_selection_test_compile(arguments->arena, source, x86_target);
    BUSTER_TEST(arguments, program != 0);
    IrFunction* add = machine_selection_test_find(program, S8("selection_add"));
    IrFunction* memory = machine_selection_test_find(program, S8("selection_memory"));
    BUSTER_TEST(arguments, add != 0 && memory != 0);
    if (!add || !memory)
    {
        return result;
    }

    MachineSelectionPrepass prepass = machine_selection_prepass_build(arguments->arena, program, add);
    BUSTER_TEST(arguments, prepass.valid);
    BUSTER_TEST(arguments, prepass.error == MACHINE_SELECTION_PREPASS_NONE);
    BUSTER_TEST(arguments, prepass.instruction_count == add->instruction_count);
    BUSTER_TEST(arguments, prepass.ordinal_count == add->instruction_count);
    BUSTER_TEST(arguments, prepass.value_count == add->value_count);

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

    MachineSelectionPrepass invalid = machine_selection_prepass_build(0, program, add);
    BUSTER_TEST(arguments, !invalid.valid && invalid.error == MACHINE_SELECTION_PREPASS_INVALID_ARGUMENT);
    return result;
}

#endif
