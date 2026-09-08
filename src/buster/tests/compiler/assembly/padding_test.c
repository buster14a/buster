#include <buster/tests/compiler/assembly/padding_test.h>
#if BUSTER_INCLUDE_TESTS
#include <buster/lib/compiler/assembly/assembly.h>
#include <buster/lib/compiler/assembly/x86_64_metadata.h>

UnitTestResult executable_padding_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    // Independently assembled by GNU as (x86) and LLVM (both targets).
    static u8 const aarch64_nop[] = {0x1f, 0x20, 0x03, 0xd5};
    u8 actual[384];
    u8 expected[384];
    for (u32 arch = 0; arch < 2; arch += 1)
    {
        Target target = {.cpu_arch = arch ? CPU_ARCH_AARCH64 : CPU_ARCH_X86_64};
        for (u64 offset = 0; offset < 32; offset += 1)
        {
            for (u64 count = 0; count <= 256; count += 1)
            {
                memset(actual, 0xa5, sizeof(actual));
                memcpy(expected, actual, sizeof(actual));
                for (u64 index = 0; index < count; index += 1)
                {
                    u64 address = offset + index;
                    u64 word_start = address & ~(u64)3;
                    bool full_word = word_start >= offset && offset + count - word_start >= 4;
                    expected[17 + index] = arch ? (full_word ? aarch64_nop[address & 3] : 0) : 0x90;
                }
                BUSTER_TEST(arguments, assembly_fill_executable_padding(target, actual + 17, offset, count));
                BUSTER_TEST(arguments, memcmp(actual, expected, sizeof(actual)) == 0);
            }
        }
        memset(actual, 0xa5, sizeof(actual));
        memcpy(expected, actual, sizeof(actual));
        BUSTER_TEST(arguments, !assembly_fill_executable_padding(target, 0, 0, 1));
        BUSTER_TEST(arguments, !assembly_fill_executable_padding(target, actual, UINT64_MAX, 1));
        BUSTER_TEST(arguments, assembly_fill_executable_padding(target, 0, UINT64_MAX, 0));
        BUSTER_TEST(arguments, memcmp(actual, expected, sizeof(actual)) == 0);
    }
    memset(actual, 0xa5, sizeof(actual));
    memcpy(expected, actual, sizeof(actual));
    BUSTER_TEST(arguments, !assembly_fill_executable_padding((Target){.cpu_arch = CPU_ARCH_COUNT}, actual, 0, 7));
    BUSTER_TEST(arguments, memcmp(actual, expected, sizeof(actual)) == 0);
    BUSTER_TEST(arguments, buster_x86_metadata_fill_nops(0, 0));
    BUSTER_TEST(arguments, !buster_x86_metadata_fill_nops(0, 1));
    return result;
}
#endif
