#pragma once

#include <buster/tests/compiler/codegen/codegen_test.h>
#include <buster/lib/compiler/codegen/machine.h>

#if BUSTER_INCLUDE_TESTS
BUSTER_GLOBAL_LOCAL bool codegen_test_debug_block_start_seed_matches(CodegenModule const* module, DebugLocationSeed const* seeds,
                                                                      u32 index, u32 start, u32 end,
                                                                      DebugLocationKind kind, DebugRegister reg)
{
    return module->debug_location_count > index && seeds[index].function_symbol.value == 7 &&
           seeds[index].local.value == 0 && seeds[index].start == start && seeds[index].end == end &&
           seeds[index].location.kind == kind &&
           (kind != DEBUG_LOCATION_REGISTER || seeds[index].location.reg == reg);
}

// An absolute oracle for the block-entry physical-register reset. v0 is
// acquired in RAX in block 0 and has no frame home. No event re-establishes it
// in block 1, so the second block must record unavailable rather than inheriting
// an edge-specific register from its predecessor. The indexed and dense
// recorders are checked independently against written values; agreement between
// the two implementations is deliberately not the oracle here.
BUSTER_GLOBAL_LOCAL UnitTestResult codegen_test_machine_debug_block_start(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    enum { EXPECTATION_ASSERTION_COUNT = 10 };

    MachineInstruction instructions[4] = {
        {.opcode = MACHINE_X64_MOV_RI},
        {.opcode = MACHINE_X64_NOP},
        {.opcode = MACHINE_X64_NOP},
        {.opcode = MACHINE_X64_NOP},
    };
    instructions[0].operands[0] = machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, 0);
    instructions[0].operands[1] = machine_ref_make(MACHINE_REF_IMMEDIATE, 0);

    MachineVirtualRegister virtual_registers[] = {
        {.definition_point = machine_point_make(0, MACHINE_POINT_NORMAL), .register_class = MACHINE_REGISTER_CLASS_GENERAL},
    };
    MachineBlock blocks[] = {
        {.first_instruction = 0, .instruction_count = 2},
        {.first_instruction = 2, .instruction_count = 2},
    };
    MachineDebugValue values[] = {
        {
            .pieces = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, 0)},
            .piece_sizes = {8},
            .local = {.value = 0},
            .first_instruction = UINT32_MAX,
            .kind = MACHINE_DEBUG_VALUE_REFERENCE,
            .piece_count = 1,
        },
    };
    MachineFunction function = {
        .instructions = instructions,
        .virtual_registers = virtual_registers,
        .blocks = blocks,
        .debug_values = values,
        .target = machine_target_x86_64(),
        .instruction_count = BUSTER_ARRAY_LENGTH(instructions),
        .virtual_register_count = BUSTER_ARRAY_LENGTH(virtual_registers),
        .block_count = BUSTER_ARRAY_LENGTH(blocks),
        .debug_value_count = BUSTER_ARRAY_LENGTH(values),
    };

    u32 virtual_offsets[] = {MACHINE_VIRTUAL_REGISTER_NO_HOME};
    u8 operand_registers[BUSTER_ARRAY_LENGTH(instructions) * MACHINE_INSTRUCTION_OPERAND_COUNT] = {0};
    operand_registers[0] = MACHINE_X64_RAX;
    MachineStackPlacement placement = {
        .virtual_register_offsets = virtual_offsets,
        .operand_registers = operand_registers,
        .valid = true,
    };
    u32 row_offsets[] = {0, 10, 20, 30};
    DebugLocationSeed indexed_seeds[4] = {0};
    DebugLocationSeed dense_seeds[4] = {0};
    CodegenModule indexed = {.debug_locations = indexed_seeds};
    CodegenModule dense = {.debug_locations = dense_seeds};
    IrFunction ir_function = {.symbol = {.value = 7}};
    Target target = {.cpu_arch = CPU_ARCH_X86_64, .os = OPERATING_SYSTEM_LINUX};

    bool indexed_ok = codegen_test_record_machine_locations(arguments->arena, &indexed,
                                                              BUSTER_ARRAY_LENGTH(indexed_seeds), &ir_function,
                                                              &function, &placement, row_offsets, 100, 140, 64, target);
    BUSTER_TEST(arguments, indexed_ok && indexed.error == CODEGEN_ERROR_NONE);
    BUSTER_TEST(arguments, indexed.debug_location_count == 3);
    BUSTER_TEST(arguments, codegen_test_debug_block_start_seed_matches(&indexed, indexed_seeds, 0, 100, 110,
                                                                       DEBUG_LOCATION_UNAVAILABLE, DEBUG_REGISTER_NONE));
    BUSTER_TEST(arguments, codegen_test_debug_block_start_seed_matches(&indexed, indexed_seeds, 1, 110, 120,
                                                                       DEBUG_LOCATION_REGISTER, DEBUG_REGISTER_X86_RAX));
    BUSTER_TEST(arguments, codegen_test_debug_block_start_seed_matches(&indexed, indexed_seeds, 2, 120, 140,
                                                                       DEBUG_LOCATION_UNAVAILABLE, DEBUG_REGISTER_NONE));

    bool dense_ok = codegen_test_record_machine_locations_dense(arguments->arena, &dense,
                                                                 BUSTER_ARRAY_LENGTH(dense_seeds), &ir_function,
                                                                 &function, &placement, row_offsets, 100, 140, 64, target);
    BUSTER_TEST(arguments, dense_ok && dense.error == CODEGEN_ERROR_NONE);
    BUSTER_TEST(arguments, dense.debug_location_count == 3);
    BUSTER_TEST(arguments, codegen_test_debug_block_start_seed_matches(&dense, dense_seeds, 0, 100, 110,
                                                                       DEBUG_LOCATION_UNAVAILABLE, DEBUG_REGISTER_NONE));
    BUSTER_TEST(arguments, codegen_test_debug_block_start_seed_matches(&dense, dense_seeds, 1, 110, 120,
                                                                       DEBUG_LOCATION_REGISTER, DEBUG_REGISTER_X86_RAX));
    BUSTER_TEST(arguments, codegen_test_debug_block_start_seed_matches(&dense, dense_seeds, 2, 120, 140,
                                                                       DEBUG_LOCATION_UNAVAILABLE, DEBUG_REGISTER_NONE));

    // Keep mutation failures from changing the number of executed assertions:
    // every expectation above is unconditional and count-safe.
    BUSTER_TEST(arguments, result.test_count == EXPECTATION_ASSERTION_COUNT);
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult codegen_tests_with_block_start_oracle(UnitTestArguments* arguments)
{
    UnitTestResult result = codegen_tests(arguments);
    UnitTestResult block_start = codegen_test_machine_debug_block_start(arguments);
    result.succeeded_test_count += block_start.succeeded_test_count;
    result.test_count += block_start.test_count;
    return result;
}
#endif
