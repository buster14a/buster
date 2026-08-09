#include <buster/tests/compiler/codegen/machine_test.h>
#if BUSTER_INCLUDE_TESTS

#include <buster/lib/compiler/codegen/codegen.h>
#include <buster/lib/compiler/ir/ir.h>
#include <buster/lib/string.h>

// Size census for the hot records the register-allocator project depends
// on. The machine rows are all-integer and hold everywhere; the typed IR and
// codegen records carry pointers, so their checks apply to 64-bit builds
// only. A designed size change must update these checks in the same change —
// they exist so design documents can never drift from the code the way the
// historical IrInstructionExtra comment did.
BUSTER_CT_CHECK(sizeof(MachineInstruction) == 24);
BUSTER_CT_CHECK(sizeof(MachineVirtualRegister) == 16);
BUSTER_CT_CHECK(sizeof(MachineBlock) == 32);
BUSTER_CT_CHECK(sizeof(MachineEdge) == 16);
BUSTER_CT_CHECK(sizeof(MachineAddress) == 16);
BUSTER_CT_CHECK(sizeof(MachineSegment) == 8);
BUSTER_CT_CHECK(sizeof(MachineUse) == 8);
BUSTER_CT_CHECK(sizeof(MachineEdit) == 16);
BUSTER_CT_CHECK(sizeof(MachineLocationSegment) == 16);
BUSTER_CT_CHECK(sizeof(void*) != 8 || sizeof(IrInstruction) == 112);
BUSTER_CT_CHECK(sizeof(void*) != 8 || sizeof(IrValue) == 24);
BUSTER_CT_CHECK(sizeof(void*) != 8 || sizeof(IrBlock) == 64);
BUSTER_CT_CHECK(sizeof(void*) != 8 || sizeof(IrBlockParameter) == 48);
BUSTER_CT_CHECK(sizeof(void*) != 8 || sizeof(IrIncoming) == 16);
BUSTER_CT_CHECK(sizeof(CodegenModuleOptions) == 8);

BUSTER_GLOBAL_LOCAL MachineFunction machine_test_build_function(Arena* arena)
{
    MachineFunctionBuilder builder = machine_function_builder_begin(arena);
    u32 source = machine_builder_virtual_register(&builder, (MachineVirtualRegister){
                                                                .definition_point = MACHINE_POINT_INVALID,
                                                                .register_class = MACHINE_REGISTER_CLASS_GENERAL,
                                                                .typed_origin = IR_ID_UNDERLYING_INVALID,
                                                            });
    u32 destination = machine_builder_virtual_register(&builder, (MachineVirtualRegister){
                                                                     .definition_point = machine_point_make(0, MACHINE_POINT_AFTER),
                                                                     .register_class = MACHINE_REGISTER_CLASS_GENERAL,
                                                                     .typed_origin = IR_ID_UNDERLYING_INVALID,
                                                                 });
    machine_builder_block_begin(&builder);
    machine_builder_instruction(&builder, (MachineInstruction){
                                              .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, destination),
                                                           machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, source)},
                                              .opcode = MACHINE_OPCODE_SKELETON_COPY,
                                          });
    machine_builder_instruction(&builder, (MachineInstruction){
                                              .opcode = MACHINE_OPCODE_SKELETON_RETURN,
                                          });
    machine_builder_block_end(&builder, (MachineBlock){0});
    machine_builder_block_begin(&builder);
    machine_builder_instruction(&builder, (MachineInstruction){
                                              .opcode = MACHINE_OPCODE_SKELETON_NOP,
                                          });
    machine_builder_instruction(&builder, (MachineInstruction){
                                              .opcode = MACHINE_OPCODE_SKELETON_RETURN,
                                          });
    machine_builder_block_end(&builder, (MachineBlock){0});
    return machine_function_builder_finish(arena, &builder);
}

UnitTestResult machine_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};

    MachineRef ref = machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, MACHINE_REF_PAYLOAD_LIMIT - 1u);
    BUSTER_TEST(arguments, machine_ref_kind(ref) == MACHINE_REF_VIRTUAL_REGISTER);
    BUSTER_TEST(arguments, machine_ref_payload(ref) == MACHINE_REF_PAYLOAD_LIMIT - 1u);
    BUSTER_TEST(arguments, machine_ref_kind(MACHINE_REF_NONE_VALUE) == MACHINE_REF_NONE);
    BUSTER_TEST(arguments, machine_ref_payload(MACHINE_REF_NONE_VALUE) == 0);

    MachinePoint point = machine_point_make(MACHINE_POINT_INSTRUCTION_LIMIT - 1u, MACHINE_POINT_AFTER);
    BUSTER_TEST(arguments, machine_point_instruction(point) == MACHINE_POINT_INSTRUCTION_LIMIT - 1u);
    BUSTER_TEST(arguments, machine_point_phase(point) == MACHINE_POINT_AFTER);
    BUSTER_TEST(arguments, machine_point_phase(machine_point_make(7, MACHINE_POINT_BEFORE)) == MACHINE_POINT_BEFORE);
    // Phase order is the overlap contract: BEFORE < EARLY < NORMAL < AFTER
    // within one instruction, and the next instruction's BEFORE follows.
    BUSTER_TEST(arguments, machine_point_make(3, MACHINE_POINT_BEFORE) < machine_point_make(3, MACHINE_POINT_EARLY));
    BUSTER_TEST(arguments, machine_point_make(3, MACHINE_POINT_AFTER) < machine_point_make(4, MACHINE_POINT_BEFORE));

    MachineOpcodeInfo const* copy_info = machine_opcode_info(MACHINE_OPCODE_SKELETON_COPY);
    BUSTER_TEST(arguments, copy_info && copy_info->operand_count == 2);
    BUSTER_TEST(arguments, copy_info && (copy_info->operand_info[0] & ((1u << MACHINE_OPERAND_ROLE_BITS) - 1u)) == MACHINE_OPERAND_ROLE_DEFINE);
    BUSTER_TEST(arguments, copy_info && (copy_info->operand_info[1] & ((1u << MACHINE_OPERAND_ROLE_BITS) - 1u)) == MACHINE_OPERAND_ROLE_USE);
    MachineOpcodeInfo const* return_info = machine_opcode_info(MACHINE_OPCODE_SKELETON_RETURN);
    BUSTER_TEST(arguments, return_info && (return_info->attributes & MACHINE_OPCODE_ATTRIBUTE_TERMINATOR));
    BUSTER_TEST(arguments, machine_opcode_info(MACHINE_OPCODE_COUNT) == 0);

    MachineFunction function = machine_test_build_function(arguments->arena);
    BUSTER_TEST(arguments, function.instruction_count == 4);
    BUSTER_TEST(arguments, function.virtual_register_count == 2);
    BUSTER_TEST(arguments, function.block_count == 2);
    BUSTER_TEST(arguments, function.blocks[0].first_instruction == 0 && function.blocks[0].instruction_count == 2);
    BUSTER_TEST(arguments, function.blocks[1].first_instruction == 2 && function.blocks[1].instruction_count == 2);
    BUSTER_TEST(arguments, function.instructions[0].opcode == MACHINE_OPCODE_SKELETON_COPY);
    BUSTER_TEST(arguments, machine_verify_function(&function).error == MACHINE_VERIFY_NONE);

    // The chunked builder must survive chunk boundaries: enough rows to
    // spill across several 16 KiB chunks, flattened back in exact order.
    u32 large_count = 3000;
    MachineFunctionBuilder large_builder = machine_function_builder_begin(arguments->arena);
    machine_builder_block_begin(&large_builder);
    for (u32 index = 0; index < large_count - 1; index += 1)
    {
        machine_builder_instruction(&large_builder, (MachineInstruction){
                                                        .payload = index,
                                                        .opcode = MACHINE_OPCODE_SKELETON_NOP,
                                                    });
    }
    machine_builder_instruction(&large_builder, (MachineInstruction){
                                                    .payload = large_count - 1,
                                                    .opcode = MACHINE_OPCODE_SKELETON_RETURN,
                                                });
    machine_builder_block_end(&large_builder, (MachineBlock){0});
    MachineFunction large = machine_function_builder_finish(arguments->arena, &large_builder);
    BUSTER_TEST(arguments, large.instruction_count == large_count);
    bool payloads_ordered = true;
    for (u32 index = 0; index < large.instruction_count; index += 1)
    {
        payloads_ordered &= large.instructions[index].payload == index;
    }
    BUSTER_TEST(arguments, payloads_ordered);
    BUSTER_TEST(arguments, machine_verify_function(&large).error == MACHINE_VERIFY_NONE);

    // Verifier rejections. Each case mutates a fresh valid function so a
    // single detected defect cannot mask another.
    MachineFunction bad_opcode = machine_test_build_function(arguments->arena);
    bad_opcode.instructions[0].opcode = MACHINE_OPCODE_INVALID;
    BUSTER_TEST(arguments, machine_verify_function(&bad_opcode).error == MACHINE_VERIFY_OPCODE);
    MachineFunction bad_opcode_range = machine_test_build_function(arguments->arena);
    bad_opcode_range.instructions[0].opcode = MACHINE_OPCODE_COUNT;
    BUSTER_TEST(arguments, machine_verify_function(&bad_opcode_range).error == MACHINE_VERIFY_OPCODE);

    MachineFunction early_terminator = machine_test_build_function(arguments->arena);
    early_terminator.instructions[0].opcode = MACHINE_OPCODE_SKELETON_RETURN;
    early_terminator.instructions[0].operands[0] = MACHINE_REF_NONE_VALUE;
    early_terminator.instructions[0].operands[1] = MACHINE_REF_NONE_VALUE;
    BUSTER_TEST(arguments, machine_verify_function(&early_terminator).error == MACHINE_VERIFY_TERMINATOR);
    MachineFunction missing_terminator = machine_test_build_function(arguments->arena);
    missing_terminator.instructions[3].opcode = MACHINE_OPCODE_SKELETON_NOP;
    BUSTER_TEST(arguments, machine_verify_function(&missing_terminator).error == MACHINE_VERIFY_TERMINATOR);

    MachineFunction bad_reference = machine_test_build_function(arguments->arena);
    bad_reference.instructions[0].operands[1] = machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, bad_reference.virtual_register_count);
    MachineVerifyResult reference_result = machine_verify_function(&bad_reference);
    BUSTER_TEST(arguments, reference_result.error == MACHINE_VERIFY_OPERAND_REFERENCE && reference_result.operand == 1);

    MachineFunction dirty_slot = machine_test_build_function(arguments->arena);
    dirty_slot.instructions[0].operands[2] = machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, 0);
    BUSTER_TEST(arguments, machine_verify_function(&dirty_slot).error == MACHINE_VERIFY_OPERAND_SLOT);

    MachineFunction bad_range = machine_test_build_function(arguments->arena);
    bad_range.blocks[1].first_instruction = 3;
    BUSTER_TEST(arguments, machine_verify_function(&bad_range).error == MACHINE_VERIFY_BLOCK_RANGE);
    MachineFunction uncovered = machine_test_build_function(arguments->arena);
    uncovered.blocks[1].instruction_count = 1;
    BUSTER_TEST(arguments, machine_verify_function(&uncovered).error != MACHINE_VERIFY_NONE);

    MachineFunction bad_definition = machine_test_build_function(arguments->arena);
    bad_definition.virtual_registers[1].definition_point = machine_point_make(function.instruction_count, MACHINE_POINT_AFTER);
    BUSTER_TEST(arguments, machine_verify_function(&bad_definition).error == MACHINE_VERIFY_VIRTUAL_REGISTER_DEFINITION);

    // Replay round-trip: byte-exact reconstruction, then hard rejection of
    // corrupted headers and truncated payloads.
    ByteSlice replay = machine_replay_serialize(arguments->arena, &function);
    MachineFunction replayed = {0};
    BUSTER_TEST(arguments, machine_replay_deserialize(arguments->arena, replay, &replayed));
    BUSTER_TEST(arguments, replayed.instruction_count == function.instruction_count);
    BUSTER_TEST(arguments, replayed.virtual_register_count == function.virtual_register_count);
    BUSTER_TEST(arguments, replayed.block_count == function.block_count);
    BUSTER_TEST(arguments,
                memcmp(replayed.instructions, function.instructions, function.instruction_count * sizeof(MachineInstruction)) == 0);
    BUSTER_TEST(arguments,
                memcmp(replayed.virtual_registers, function.virtual_registers, function.virtual_register_count * sizeof(MachineVirtualRegister)) == 0);
    BUSTER_TEST(arguments, memcmp(replayed.blocks, function.blocks, function.block_count * sizeof(MachineBlock)) == 0);
    BUSTER_TEST(arguments, machine_verify_function(&replayed).error == MACHINE_VERIFY_NONE);

    MachineFunction rejected = {0};
    ByteSlice truncated = replay;
    truncated.length -= 1;
    BUSTER_TEST(arguments, !machine_replay_deserialize(arguments->arena, truncated, &rejected));
    u8* corrupt_bytes = arena_allocate(arguments->arena, u8, replay.length);
    memcpy(corrupt_bytes, replay.pointer, replay.length);
    corrupt_bytes[0] ^= 0xff;
    BUSTER_TEST(arguments, !machine_replay_deserialize(arguments->arena, (ByteSlice){.pointer = corrupt_bytes, .length = replay.length}, &rejected));
    BUSTER_TEST(arguments, !machine_replay_deserialize(arguments->arena, (ByteSlice){0}, &rejected));

    // Mode plumbing: the driver-facing enum and its report spelling.
    BUSTER_STRING_TEST(arguments, codegen_register_allocator_mode_string(CODEGEN_REGISTER_ALLOCATOR_NONE), S8("none"));
    BUSTER_STRING_TEST(arguments, codegen_register_allocator_mode_string(CODEGEN_REGISTER_ALLOCATOR_MIR_STACK), S8("mir-stack"));
    BUSTER_STRING_TEST(arguments, codegen_register_allocator_mode_string(CODEGEN_REGISTER_ALLOCATOR_FAST), S8("fast"));
    BUSTER_STRING_TEST(arguments, codegen_register_allocator_mode_string(CODEGEN_REGISTER_ALLOCATOR_QUALITY), S8("quality"));

    return result;
}
#endif
