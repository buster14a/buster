#include <buster/tests/compiler/assembly/padding_test.h>
#if BUSTER_INCLUDE_TESTS
#include <buster/lib/compiler/assembly/assembly.h>
#include <buster/lib/compiler/assembly/assembly_unit_internal.h>
#include <buster/lib/compiler/assembly/x86_64_metadata.h>
#include <buster/lib/string.h>

typedef struct AssemblyUnitNumberBoundaryCase AssemblyUnitNumberBoundaryCase;
struct AssemblyUnitNumberBoundaryCase
{
    String8 maximum;
    String8 overflow;
};

BUSTER_GLOBAL_LOCAL UnitTestResult assembly_unit_number_boundary_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    static AssemblyUnitNumberBoundaryCase const cases[] = {
        {S8_INITIALIZER("18446744073709551615"), S8_INITIALIZER("18446744073709551616")},
        {S8_INITIALIZER("0xffffffffffffffff"), S8_INITIALIZER("0x10000000000000000")},
        {S8_INITIALIZER("0b1111111111111111111111111111111111111111111111111111111111111111"),
         S8_INITIALIZER("0b10000000000000000000000000000000000000000000000000000000000000000")},
        {S8_INITIALIZER("01777777777777777777777"), S8_INITIALIZER("02000000000000000000000")},
    };
    AssemblyEncodeOptions options = {.target = {.cpu_arch = CPU_ARCH_X86_64}};
    u64 const sentinel = (u64)0x0123456789abcdefULL;

    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(cases); index += 1)
    {
        AssemblyUnitNumberBoundaryCase boundary = cases[index];
        u64 parsed = 0;
        BUSTER_TEST(arguments, assembly_unit_test_parse_number(boundary.maximum, &parsed));
        BUSTER_TEST(arguments, parsed == UINT64_MAX);
        parsed = sentinel;
        BUSTER_TEST(arguments, !assembly_unit_test_parse_number(boundary.overflow, &parsed));
        BUSTER_TEST(arguments, parsed == sentinel);

        String8 maximum_source = string_format(arguments->arena, S8(".quad {S8}\n"), boundary.maximum);
        AssemblyUnitResult maximum = assembly_unit_encode(arguments->arena, maximum_source, options);
        BUSTER_TEST(arguments, maximum.diagnostic_count == 0);
        BUSTER_TEST(arguments, maximum.symbol_count == 0);
        BUSTER_TEST(arguments, maximum.relocation_count == 0);
        if (BUSTER_REQUIRE(arguments, maximum.section_count == 1 && maximum.sections[0].data.pointer && maximum.sections[0].data.length == 8))
        {
            bool bytes_match = true;
            for (u32 byte_index = 0; byte_index < 8; byte_index += 1)
            {
                bytes_match = bytes_match && maximum.sections[0].data.pointer[byte_index] == UINT8_MAX;
            }
            BUSTER_TEST(arguments, bytes_match);
        }

        String8 overflow_source = string_format(arguments->arena, S8(".quad {S8}\n"), boundary.overflow);
        AssemblyUnitResult overflow = assembly_unit_encode(arguments->arena, overflow_source, options);
        if (BUSTER_REQUIRE(arguments, overflow.diagnostic_count == 1))
        {
            BUSTER_TEST(arguments, overflow.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
            BUSTER_TEST(arguments, overflow.diagnostics[0].line == 1 && overflow.diagnostics[0].column == 1);
            BUSTER_TEST(arguments, string_first_sequence(overflow.diagnostics[0].message, S8(".quad")) < overflow.diagnostics[0].message.length);
        }
        BUSTER_TEST(arguments, overflow.symbol_count == 0);
        BUSTER_TEST(arguments, overflow.relocation_count == 0);
        if (BUSTER_REQUIRE(arguments, overflow.section_count == 1))
        {
            BUSTER_TEST(arguments, !overflow.sections[0].data.pointer && overflow.sections[0].data.length == 0 && overflow.sections[0].zero_size == 0);
        }
    }

    static String8 const padded_numbers[] = {
        S8_INITIALIZER("0000000000000000000000000000000000000000000000000000000000000001"),
        S8_INITIALIZER("0x0000000000000000000000000000000000000000000000000000000000000001"),
        S8_INITIALIZER("0b0000000000000000000000000000000000000000000000000000000000000001"),
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(padded_numbers); index += 1)
    {
        u64 parsed = 0;
        BUSTER_TEST(arguments, assembly_unit_test_parse_number(padded_numbers[index], &parsed));
        BUSTER_TEST(arguments, parsed == 1);
    }

    static String8 const invalid_numbers[] = {
        S8_INITIALIZER(""),
        S8_INITIALIZER("0x"),
        S8_INITIALIZER("0b"),
        S8_INITIALIZER("08"),
        S8_INITIALIZER("0b2"),
        S8_INITIALIZER("0xg"),
        S8_INITIALIZER("12z"),
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(invalid_numbers); index += 1)
    {
        u64 parsed = sentinel;
        BUSTER_TEST(arguments, !assembly_unit_test_parse_number(invalid_numbers[index], &parsed));
        BUSTER_TEST(arguments, parsed == sentinel);
    }

    AssemblyUnitResult expression = assembly_unit_encode(arguments->arena, S8(".quad 1+2-1\n"), options);
    BUSTER_TEST(arguments, expression.diagnostic_count == 0);
    if (BUSTER_REQUIRE(arguments, expression.section_count == 1 && expression.sections[0].data.pointer && expression.sections[0].data.length == 8))
    {
        BUSTER_TEST(arguments, expression.sections[0].data.pointer[0] == 2);
        bool upper_bytes_zero = true;
        for (u32 byte_index = 1; byte_index < 8; byte_index += 1)
        {
            upper_bytes_zero = upper_bytes_zero && expression.sections[0].data.pointer[byte_index] == 0;
        }
        BUSTER_TEST(arguments, upper_bytes_zero);
    }

    AssemblyUnitResult padded_expression = assembly_unit_encode(
        arguments->arena, S8(".quad 0000000000000000000000000000000000000000000000000000000000000001\n"), options);
    BUSTER_TEST(arguments, padded_expression.diagnostic_count == 0);
    if (BUSTER_REQUIRE(arguments, padded_expression.section_count == 1 && padded_expression.sections[0].data.pointer &&
                                      padded_expression.sections[0].data.length == 8))
    {
        BUSTER_TEST(arguments, padded_expression.sections[0].data.pointer[0] == 1);
    }

    static String8 const invalid_sources[] = {
        S8_INITIALIZER(".quad\n"),
        S8_INITIALIZER(".quad 08\n"),
        S8_INITIALIZER(".quad 0b2\n"),
        S8_INITIALIZER(".quad 0xg\n"),
        S8_INITIALIZER(".quad 12z\n"),
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(invalid_sources); index += 1)
    {
        AssemblyUnitResult invalid = assembly_unit_encode(arguments->arena, invalid_sources[index], options);
        if (BUSTER_REQUIRE(arguments, invalid.diagnostic_count == 1))
        {
            BUSTER_TEST(arguments, invalid.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
        }
        BUSTER_TEST(arguments, invalid.symbol_count == 0 && invalid.relocation_count == 0);
        if (BUSTER_REQUIRE(arguments, invalid.section_count == 1))
        {
            BUSTER_TEST(arguments, invalid.sections[0].data.length == 0 && invalid.sections[0].zero_size == 0);
        }
    }

    u32 const label_cases[] = {0, 3};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(label_cases); index += 1)
    {
        AssemblyUnitNumberBoundaryCase boundary = cases[label_cases[index]];
        String8 maximum_source = string_format(arguments->arena, S8("{S8}:\n\tjmp {S8}b\n"), boundary.maximum, boundary.maximum);
        AssemblyUnitResult maximum = assembly_unit_encode(arguments->arena, maximum_source, options);
        BUSTER_TEST(arguments, maximum.diagnostic_count == 0);
        BUSTER_TEST(arguments, maximum.symbol_count == 0);
        BUSTER_TEST(arguments, maximum.relocation_count == 0);
        if (BUSTER_REQUIRE(arguments, maximum.section_count == 1))
        {
            BUSTER_TEST(arguments, maximum.sections[0].data.pointer && maximum.sections[0].data.length > 0);
        }

        String8 overflow_source = string_format(arguments->arena, S8("{S8}:\n\tjmp {S8}b\n"), boundary.overflow, boundary.overflow);
        AssemblyUnitResult overflow = assembly_unit_encode(arguments->arena, overflow_source, options);
        if (BUSTER_REQUIRE(arguments, overflow.diagnostic_count == 1))
        {
            BUSTER_TEST(arguments, overflow.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_STATEMENT);
            BUSTER_STRING_TEST(arguments, overflow.diagnostics[0].message, S8("local label definition was not collected"));
        }
        BUSTER_TEST(arguments, overflow.section_count == 0);
        BUSTER_TEST(arguments, overflow.symbol_count == 0);
        BUSTER_TEST(arguments, overflow.relocation_count == 0);

        // Before the overflow guard, this reference wrapped to zero and bound
        // to the real `0:` label instead of reporting an invalid expression.
        String8 alias_source = string_format(arguments->arena, S8("0:\n\tjmp {S8}b\n"), boundary.overflow);
        AssemblyUnitResult alias = assembly_unit_encode(arguments->arena, alias_source, options);
        BUSTER_TEST(arguments, alias.diagnostic_count == 1);
        BUSTER_TEST(arguments, alias.relocation_count == 0);
        if (BUSTER_REQUIRE(arguments, alias.section_count == 1))
        {
            BUSTER_TEST(arguments, !alias.sections[0].data.pointer && alias.sections[0].data.length == 0 && alias.sections[0].zero_size == 0);
        }
    }

    AssemblyUnitResult ordinary_label = assembly_unit_encode(arguments->arena, S8("1:\n\tjmp 1b\n"), options);
    BUSTER_TEST(arguments, ordinary_label.diagnostic_count == 0);
    BUSTER_TEST(arguments, ordinary_label.symbol_count == 0 && ordinary_label.relocation_count == 0);
    if (BUSTER_REQUIRE(arguments, ordinary_label.section_count == 1))
    {
        BUSTER_TEST(arguments, ordinary_label.sections[0].data.pointer && ordinary_label.sections[0].data.length > 0);
    }
    return result;
}


BUSTER_GLOBAL_LOCAL UnitTestResult assembly_unit_x86_relaxation_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Target target = {.cpu_arch = CPU_ARCH_X86_64, .os = OPERATING_SYSTEM_LINUX};
    AssemblyEncodeOptions options = {.target = target};
    struct
    {
        String8 source;
        u8 bytes[16];
        u32 length;
    } const cases[] = {
        {S8_INITIALIZER("jmp .L1\nret\n.L1:\nret\njmp .L1\n"), {0xeb, 1, 0xc3, 0xc3, 0xeb, 0xfd}, 6},
        {S8_INITIALIZER("jmp .L1\n.set .L1, . - 1\n"), {0xeb, 0xff}, 2},
        {S8_INITIALIZER(".set .Lalias, .Lend\njmp .Lalias\n.Lend: ret\n"), {0xeb, 0, 0xc3}, 3},
        {S8_INITIALIZER("1: jmp 2f\nret\n2: jne 1b\n"), {0xeb, 1, 0xc3, 0x75, 0xfb}, 5},
        {S8_INITIALIZER("jmp .+2\njmp .\n"), {0xeb, 0, 0xeb, 0xfe}, 4},
        {S8_INITIALIZER("jmp .Lend\n.long .Lend - .\n.Lend: ret\n"), {0xeb, 4, 4, 0, 0, 0, 0xc3}, 7},
        {S8_INITIALIZER("jmp .Lend\ncall .Lend\n.Lend: ret\n"), {0xeb, 5, 0xe8, 0, 0, 0, 0, 0xc3}, 8},
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(cases); index += 1)
    {
        TemporalArena scope = arena_begin_temporal(arguments->arena);
        AssemblyUnitResult unit = assembly_unit_encode(arguments->arena, cases[index].source, options);
        if (BUSTER_REQUIRE(arguments, !unit.diagnostic_count && unit.section_count == 1))
        {
            BUSTER_TEST(arguments, unit.relocation_count == 0);
            BUSTER_TEST(arguments, unit.sections[0].data.length == cases[index].length &&
                                       memcmp(unit.sections[0].data.pointer, cases[index].bytes, cases[index].length) == 0);
        }
        scratch_end(scope);
    }
    // Both signed rel8 endpoints and the first byte beyond each endpoint.
    // Forward bounds use JMP and Jcc; backward bounds also account for the
    // larger near encoding when a short candidate must grow.
    for (u32 conditional = 0; conditional < 2; conditional += 1)
    {
        for (u32 backward = 0; backward < 2; backward += 1)
        {
            for (u32 outside = 0; outside < 2; outside += 1)
            {
                TemporalArena scope = arena_begin_temporal(arguments->arena);
                String8 mnemonic = conditional ? S8("jne") : S8("jmp");
                u32 gap = (backward ? 126 : 127) + outside;
                String8 source = backward
                    ? string_format(arguments->arena, S8(".Lstart:\n.zero {u32}\n{S8} .Lstart\n"), gap, mnemonic)
                    : string_format(arguments->arena, S8("{S8} .Lend\n.zero {u32}\n.Lend: ret\n"), mnemonic, gap);
                AssemblyUnitResult unit = assembly_unit_encode(arguments->arena, source, options);
                u32 width = outside ? (conditional ? 6 : 5) : 2;
                u32 offset = backward ? gap : 0;
                if (BUSTER_REQUIRE(arguments, !unit.diagnostic_count && unit.section_count == 1))
                {
                    ByteSlice data = unit.sections[0].data;
                    if (BUSTER_REQUIRE(arguments, data.length == gap + width + !backward))
                    {
                        BUSTER_TEST(arguments, data.pointer[offset] == (outside ? (conditional ? 0x0f : 0xe9) : (conditional ? 0x75 : 0xeb)));
                        s32 displacement;
                        if (outside)
                        {
                            memcpy(&displacement, data.pointer + offset + (conditional ? 2 : 1), sizeof(displacement));
                        }
                        else
                        {
                            displacement = (s8)data.pointer[offset + 1];
                        }
                        BUSTER_TEST(arguments, displacement == (backward ? -(s32)(gap + width) : (s32)gap));
                    }
                    BUSTER_TEST(arguments, unit.relocation_count == 0);
                }
                scratch_end(scope);
            }
        }
    }
    // A far JMP grows first, moving a backward Jcc past -128 on the next
    // layout. The alignment case separately recomputes a fill gap.
    String8 const layouts[] = {
        S8_INITIALIZER(".Lstart: jmp .Lfar\n.zero 124\njne .Lstart\n.zero 128\n.Lfar: ret\n"),
        S8_INITIALIZER("jmp .Lend\n.balign 16, 0x90\n.Lend: ret\n"),
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(layouts); index += 1)
    {
        AssemblyUnitResult unit = assembly_unit_encode(arguments->arena, layouts[index], options);
        if (BUSTER_REQUIRE(arguments, !unit.diagnostic_count && unit.section_count == 1))
        {
            ByteSlice data = unit.sections[0].data;
            if (BUSTER_REQUIRE(arguments, data.length == (index ? 17 : 264)))
            {
                if (index)
                {
                    BUSTER_TEST(arguments, data.pointer[0] == 0xeb && data.pointer[1] == 14 && data.pointer[16] == 0xc3);
                    for (u32 byte = 2; byte < 16; byte += 1)
                    {
                        BUSTER_TEST(arguments, data.pointer[byte] == 0x90);
                    }
                }
                else
                {
                    s32 forward;
                    s32 backward;
                    memcpy(&forward, data.pointer + 1, sizeof(forward));
                    memcpy(&backward, data.pointer + 131, sizeof(backward));
                    BUSTER_TEST(arguments, data.pointer[0] == 0xe9 && forward == 258);
                    BUSTER_TEST(arguments, data.pointer[129] == 0x0f && data.pointer[130] == 0x85 && backward == -135);
                }
            }
        }
    }
    // Target binding is decided after parsing, including late attributes.
    String8 const bindings[] = {
        S8_INITIALIZER("jmp f\nf: ret\n.globl f\n"),
        S8_INITIALIZER("jmp f\nf: ret\n.weak f\n.hidden f\n"),
        S8_INITIALIZER("jmp f\nf: ret\n.globl f\n.hidden f\n"),
        S8_INITIALIZER("jmp external\n"),
        S8_INITIALIZER("jmp f@PLT\nf: ret\n"),
        S8_INITIALIZER("jmp f\n.data\nf: .byte 0\n"),
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(bindings); index += 1)
    {
        AssemblyUnitResult unit = assembly_unit_encode(arguments->arena, bindings[index], options);
        if (BUSTER_REQUIRE(arguments, !unit.diagnostic_count && unit.section_count))
        {
            bool short_jump = index == 2;
            BUSTER_TEST(arguments, unit.sections[0].data.pointer[0] == (short_jump ? 0xeb : 0xe9));
            bool retained = index == 0 || index == 1 || index == 3 || index == 5;
            BUSTER_TEST(arguments, unit.relocation_count == (u32)retained);
            if (BUSTER_REQUIRE(arguments, !retained || unit.relocation_count == 1))
            {
                BUSTER_TEST(arguments, !retained || unit.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC32);
            }
        }
    }
    return result;
}

UnitTestResult executable_padding_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_TEST_FIXTURE(arguments, assembly_unit_number_boundary_tests);
    BUSTER_TEST_FIXTURE(arguments, assembly_unit_x86_relaxation_tests);
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
