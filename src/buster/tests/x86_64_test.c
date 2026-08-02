#include <buster/tests/x86_64_test.h>

BUSTER_TEST_F_DECL UnitTestResult x86_64_tests(UnitTestArguments* arguments)
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
    return result;
}
