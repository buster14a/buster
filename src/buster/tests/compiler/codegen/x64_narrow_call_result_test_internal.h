#pragma once

// Selection coverage complements the independent mixed-object driver execution.
BUSTER_GLOBAL_LOCAL UnitTestResult machine_test_x64_narrow_call_results(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    String8 source = S8(
        "extern _Bool foreign_bool_false(void);\n"
        "long long direct_bool_false(void) { return foreign_bool_false(); }\n"
        "long long indirect_bool_false(_Bool (*probe)(void)) { return probe(); }\n"
        "extern _Bool foreign_bool_true(void);\n"
        "long long direct_bool_true(void) { return foreign_bool_true(); }\n"
        "long long indirect_bool_true(_Bool (*probe)(void)) { return probe(); }\n"
        "extern signed char foreign_s8_min(void);\n"
        "long long direct_s8_min(void) { return foreign_s8_min(); }\n"
        "long long indirect_s8_min(signed char (*probe)(void)) { return probe(); }\n"
        "extern signed char foreign_s8_max(void);\n"
        "long long direct_s8_max(void) { return foreign_s8_max(); }\n"
        "long long indirect_s8_max(signed char (*probe)(void)) { return probe(); }\n"
        "extern unsigned char foreign_u8_max(void);\n"
        "long long direct_u8_max(void) { return foreign_u8_max(); }\n"
        "long long indirect_u8_max(unsigned char (*probe)(void)) { return probe(); }\n"
        "extern short foreign_s16_min(void);\n"
        "long long direct_s16_min(void) { return foreign_s16_min(); }\n"
        "long long indirect_s16_min(short (*probe)(void)) { return probe(); }\n"
        "extern short foreign_s16_max(void);\n"
        "long long direct_s16_max(void) { return foreign_s16_max(); }\n"
        "long long indirect_s16_max(short (*probe)(void)) { return probe(); }\n"
        "extern unsigned short foreign_u16_max(void);\n"
        "long long direct_u16_max(void) { return foreign_u16_max(); }\n"
        "long long indirect_u16_max(unsigned short (*probe)(void)) { return probe(); }\n"
        "extern int foreign_s32_min(void);\n"
        "long long direct_s32_min(void) { return foreign_s32_min(); }\n"
        "long long indirect_s32_min(int (*probe)(void)) { return probe(); }\n"
        "extern unsigned int foreign_u32_max(void);\n"
        "long long direct_u32_max(void) { return foreign_u32_max(); }\n"
        "long long indirect_u32_max(unsigned int (*probe)(void)) { return probe(); }\n"
        "extern unsigned long long foreign_u64_value(void);\n"
        "long long direct_u64_value(void) { return foreign_u64_value(); }\n"
        "long long indirect_u64_value(unsigned long long (*probe)(void)) { return probe(); }\n");
    String8 names[] = {S8("bool_false"), S8("bool_true"), S8("s8_min"), S8("s8_max"), S8("u8_max"), S8("s16_min"), S8("s16_max"), S8("u16_max"), S8("s32_min"), S8("u32_max"), S8("u64_value")};
    u16 expected[] = {MACHINE_X64_MOVZX8_RR, MACHINE_X64_MOVZX8_RR, MACHINE_X64_MOVSX8_RR, MACHINE_X64_MOVSX8_RR, MACHINE_X64_MOVZX8_RR, MACHINE_X64_MOVSX16_RR, MACHINE_X64_MOVSX16_RR, MACHINE_X64_MOVZX16_RR, MACHINE_X64_MOV_RR, MACHINE_X64_MOV_RR, MACHINE_X64_MOV_RR};
    OperatingSystem systems[] = {OPERATING_SYSTEM_LINUX, OPERATING_SYSTEM_MACOS, OPERATING_SYSTEM_WINDOWS,
        OPERATING_SYSTEM_ANDROID, OPERATING_SYSTEM_IOS};
    for (u32 system = 0; system < BUSTER_ARRAY_LENGTH(systems); system += 1)
    {
        Target target = {.cpu_arch = CPU_ARCH_X86_64, .os = systems[system]};
        for (u32 memory_form = 0; memory_form < 2; memory_form += 1)
        {
            TemporalArena temporary = scratch_begin(&arguments->arena, 1);
            IrProgram* program = machine_test_compile_c_with_options(temporary.arena, S8("x64-narrow-call-results.c"), source, target,
                (CIRLowerOptions){.disable_direct_ssa = memory_form != 0});
            BUSTER_TEST(arguments, program && program->module_count == 1);
            if (program && program->module_count == 1)
            {
                IrModule* module = program->modules;
                IrValidationResult prepared = ir_prepare_canonical_module(program, module, false);
                BUSTER_TEST(arguments, prepared.error == IR_VALIDATION_NONE);
                for (u32 name = 0; prepared.error == IR_VALIDATION_NONE && name < BUSTER_ARRAY_LENGTH(names); name += 1)
                {
                    for (u32 indirect = 0; indirect < 2; indirect += 1)
                    {
                        String8 function_name = string_format(temporary.arena, S8("{S8}_{S8}"),
                            indirect ? S8("indirect") : S8("direct"), names[name]);
                        IrFunction* function = machine_test_ir_function_find(module, function_name);
                        BUSTER_TEST_RAW(arguments, function != 0, function_name);
                        if (function)
                        {
                            MachineSelectResult selected = machine_select_canonical_function(temporary.arena, program, function, target);
                            BUSTER_TEST_RAW(arguments, selected.supported, function_name);
                            if (selected.supported)
                            {
                                BUSTER_TEST(arguments, machine_verify_function(&selected.function).error == MACHINE_VERIFY_NONE);
                                u32 captures = 0;
                                for (u32 row = 0; row < selected.function.instruction_count; row += 1)
                                {
                                    MachineInstruction instruction = selected.function.instructions[row];
                                    if (machine_ref_kind(instruction.operands[0]) == MACHINE_REF_VIRTUAL_REGISTER &&
                                        instruction.operands[1] == machine_ref_make(MACHINE_REF_PHYSICAL_REGISTER, MACHINE_X64_RAX))
                                    {
                                        BUSTER_TEST_RAW(arguments, instruction.opcode == expected[name], function_name);
                                        captures += 1;
                                    }
                                }
                                BUSTER_TEST_RAW(arguments, captures == 1, function_name);
                            }
                        }
                    }
                }
                for (u32 mode = 0; prepared.error == IR_VALIDATION_NONE && mode < CODEGEN_REGISTER_ALLOCATOR_MODE_COUNT; mode += 1)
                {
                    CodegenModule generated = codegen_generate_canonical_module(temporary.arena, program, module, target,
                        (CodegenModuleOptions){.register_allocator = (u8)mode, .verify_invariants = true});
                    BUSTER_TEST(arguments, generated.error == CODEGEN_ERROR_NONE);
                    BUSTER_TEST(arguments, generated.statistics.fallback_function_count == 0);
                }
            }
            c_test_scratch_end(temporary);
        }
    }
    return result;
}
