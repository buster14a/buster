#include <buster/tests/x86_64_test.h>
#if BUSTER_INCLUDE_TESTS

UnitTestResult x86_64_tests(UnitTestArguments* arguments)
{
    BUSTER_UNUSED(arguments);
    UnitTestResult result = {0};
    struct
    {
        X86_64RegisterOperation operation;
        u32 target;
        u32 source;
        u8 bytes[4];
        u8 length;
    } cases[] = {
        {
            X86_64_REGISTER_OPERATION_MOVE,
            0,
            1,
            {0x48, 0x89, 0xc8},
            3,
        },
        {
            X86_64_REGISTER_OPERATION_MOVE,
            16,
            0,
            {0xd5, 0x18, 0x89, 0xc0},
            4,
        },
        {
            X86_64_REGISTER_OPERATION_MOVE,
            0,
            16,
            {0xd5, 0x48, 0x89, 0xc0},
            4,
        },
        {
            X86_64_REGISTER_OPERATION_ADD,
            16,
            17,
            {0xd5, 0x58, 0x01, 0xc8},
            4,
        },
        {
            X86_64_REGISTER_OPERATION_SUBTRACT,
            31,
            16,
            {0xd5, 0x59, 0x29, 0xc7},
            4,
        },
        {
            X86_64_REGISTER_OPERATION_ADD,
            24,
            25,
            {0xd5, 0x5d, 0x01, 0xc8},
            4,
        },
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(cases); index += 1)
    {
        X86_64EncodedInstruction instruction = x86_64_encode_register_operation(cases[index].operation, cases[index].target, cases[index].source);
        BUSTER_TEST(arguments, instruction.length == cases[index].length);
        BUSTER_TEST(arguments, memcmp(instruction.bytes, cases[index].bytes, cases[index].length) == 0);
    }
    BUSTER_TEST(arguments, x86_64_encode_register_operation(X86_64_REGISTER_OPERATION_MOVE, 32, 0).length == 0);

    // The source-independent bridge is also the canonical path used by
    // fixed link/JIT producers.  Keep a small byte differential here so
    // those callers cannot silently regress to hand-written opcode blobs.
    String8 all_features[] = {S8("*")};
    BusterX86MetadataPhysicalOperand rip_memory = {
        .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY,
        .width = 64,
        .memory = {
            .address_size = 64,
            .has_displacement = true,
            .rip_relative = true,
            .source_width = 64,
        },
    };
    u8 bytes[16] = {0};
    BusterX86MetadataEmitResult jmp = buster_x86_metadata_encode((BusterX86MetadataEncodeQuery){
        .physical = {
            .mnemonic = S8("JMP"),
            .operands = &rip_memory,
            .operand_count = 1,
            .features = {.names = all_features, .count = 1},
            .address_size = 64,
            .execution_mode = BUSTER_X86_METADATA_EXECUTION_MODE_64,
        },
        .output = bytes,
        .output_capacity = sizeof(bytes),
    });
    u8 const expected_jmp[] = {0xff, 0x25, 0, 0, 0, 0};
    BUSTER_TEST(arguments, jmp.status == BUSTER_X86_METADATA_ENCODE_SUCCESS);
    BUSTER_TEST(arguments, jmp.byte_count == sizeof(expected_jmp));
    BUSTER_TEST(arguments, memcmp(bytes, expected_jmp, sizeof(expected_jmp)) == 0);

    BusterX86MetadataPhysicalOperand too_many_operands[17] = {0};
    BusterX86MetadataEmitResult invalid = buster_x86_metadata_encode((BusterX86MetadataEncodeQuery){
        .physical = {
            .mnemonic = S8("JMP"),
            .operands = too_many_operands,
            .operand_count = BUSTER_ARRAY_LENGTH(too_many_operands),
            .address_size = 64,
            .execution_mode = BUSTER_X86_METADATA_EXECUTION_MODE_64,
        },
    });
    BUSTER_TEST(arguments, invalid.status == BUSTER_X86_METADATA_ENCODE_INVALID_INPUT);
    BUSTER_TEST(arguments, invalid.form_id == UINT32_MAX);
    return result;
}
#endif
