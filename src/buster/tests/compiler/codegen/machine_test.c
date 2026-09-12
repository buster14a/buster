BUSTER_GLOBAL_LOCAL UnitTestResult machine_test_compiler_barrier(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    ByteSlice input = file_read(arguments->arena, S8("tests/basic_c_compiler_barrier.c"), (FileReadOptions){0});
    String8 source = {.pointer = (char8*)input.pointer, .length = input.length};
    BUSTER_TEST(arguments, input.length != 0);
    CpuArch architectures[] = {CPU_ARCH_X86_64, CPU_ARCH_AARCH64};
    for (u32 architecture = 0; architecture < BUSTER_ARRAY_LENGTH(architectures); architecture += 1)
    {
        Target target = {.cpu_arch = architectures[architecture], .cpu_model = CPU_MODEL_BASELINE, .os = OPERATING_SYSTEM_LINUX};
        for (u32 memory_form = 0; memory_form < 2; memory_form += 1)
        {
            TemporalArena temporary = scratch_begin(&arguments->arena, 1);
            IrProgram* program = machine_test_compile_c_with_options(temporary.arena, S8("compiler-barrier.c"), source, target,
                                                                     (CIRLowerOptions){.disable_direct_ssa = memory_form != 0});
            BUSTER_TEST(arguments, program && program->module_count == 1);
            if (program && program->module_count == 1)
            {
                IrFunction* function = machine_test_ir_function_find(program->modules, S8("compiler_barrier_memory"));
                BUSTER_TEST(arguments, function != 0);
                if (function)
                {
                    MachineSelectResult selected = machine_select_canonical_function(temporary.arena, program, function, target);
                    BUSTER_TEST(arguments, selected.supported);
                    if (selected.supported)
                    {
                        BUSTER_TEST(arguments, machine_verify_function(&selected.function).error == MACHINE_VERIFY_NONE);
                        u16 barrier_opcode = architectures[architecture] == CPU_ARCH_X86_64 ? MACHINE_X64_COMPILER_BARRIER
                                                                                           : MACHINE_A64_COMPILER_BARRIER;
                        u32 barrier_count = 0;
                        for (u32 row = 0; row < selected.function.instruction_count; row += 1)
                        {
                            barrier_count += selected.function.instructions[row].opcode == barrier_opcode;
                        }
                        BUSTER_TEST(arguments, barrier_count == 1);
                        MachineOpcodeInfo const* info = machine_opcode_info(barrier_opcode);
                        BUSTER_TEST(arguments, info && (info->attributes & MACHINE_OPCODE_ATTRIBUTE_SIDE_EFFECTS) != 0 &&
                                                   machine_opcode_memory_effect(info) == MACHINE_MEMORY_EFFECT_BARRIER);
                    }
                }
            }
            scratch_end(temporary);
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult machine_test_native_variadic(UnitTestArguments* arguments)
