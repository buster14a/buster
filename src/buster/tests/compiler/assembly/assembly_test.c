#include <buster/tests/compiler/assembly/assembly_test.h>

BUSTER_TEST_F_DECL UnitTestResult assembly_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Target x86_target = {
        .cpu_arch = CPU_ARCH_X86_64,
        .os = OPERATING_SYSTEM_LINUX,
    };
    AssemblyEncodeResult x86 = assembly_encode(arguments->arena, S8("start:\n nop\n call external\n jmp start\n ret\n"),
                                                (AssemblyEncodeOptions){
                                                    .target = x86_target,
                                                    .syntax = ASSEMBLY_SYNTAX_INTEL,
                                                });
    u8 expected_x86[] = {
        0x90, 0xe8, 0x00, 0x00, 0x00, 0x00, 0xe9, 0xf5, 0xff, 0xff, 0xff, 0xc3,
    };
    BUSTER_TEST(arguments, x86.diagnostic_count == 0);
    BUSTER_TEST(arguments, x86.bytes.length == sizeof(expected_x86) && memcmp(x86.bytes.pointer, expected_x86, sizeof(expected_x86)) == 0);
    BUSTER_TEST(arguments, x86.symbol_count == 2 && x86.symbols[0].defined && x86.symbols[0].offset == 0 &&
                               string_equal(x86.symbols[0].name, S8("start")) && !x86.symbols[1].defined &&
                               string_equal(x86.symbols[1].name, S8("external")));
    BUSTER_TEST(arguments, x86.relocation_count == 1 && x86.relocations[0].offset == 2 && x86.relocations[0].symbol == 1 &&
                               x86.relocations[0].addend == -4 && x86.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC32);

    u8 expected_x86_register_forms[] = {
        0x48, 0x89, 0xd8,
        0x49, 0xb8, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11,
        0x48, 0x83, 0xc0, 0x07,
        0x45, 0x29, 0xd1,
        0x66, 0x21, 0xc8,
        0x45, 0x30, 0xc8,
        0x4d, 0x0f, 0xaf, 0xdc,
        0x41, 0x57,
        0x41, 0x5f,
        0x48, 0xff, 0xc0,
        0x41, 0xf7, 0xd8,
        0x48, 0xc1, 0xe0, 0x03,
        0x66, 0x98,
        0x98,
        0x48, 0x98,
        0x41, 0xff, 0xd3,
        0x4d, 0x01, 0xec,
        0x09, 0xc8,
        0x49, 0x81, 0xff, 0x7f, 0xff, 0xff, 0xff,
        0x41, 0xf7, 0xc0, 0x78, 0x56, 0x34, 0x12,
        0x48, 0xff, 0xcb,
        0x66, 0x41, 0xf7, 0xd1,
        0x41, 0xc1, 0xea, 0x04,
        0x49, 0xd1, 0xfb,
        0x41, 0xff, 0xe6,
    };
    String8 x86_intel_source =
        S8("mov rax, rbx\n"
           "mov r8, 0x1122334455667788\n"
           "add rax, 7\n"
           "sub r9d, r10d\n"
           "and ax, cx\n"
           "xor r8b, r9b\n"
           "imul r11, r12\n"
           "push r15\n"
           "pop r15\n"
           "inc rax\n"
           "neg r8d\n"
           "shl rax, 3\n"
           "cbw\n"
           "cwde\n"
           "cdqe\n"
           "call r11\n"
           "add r12, r13\n"
           "or eax, ecx\n"
           "cmp r15, -129\n"
           "test r8d, 0x12345678\n"
           "dec rbx\n"
           "not r9w\n"
           "shr r10d, 4\n"
           "sar r11, 1\n"
           "jmp r14\n");
    AssemblyEncodeResult x86_intel = assembly_encode(arguments->arena, x86_intel_source,
                                                      (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, x86_intel.diagnostic_count == 0);
    BUSTER_TEST(arguments, x86_intel.bytes.length == sizeof(expected_x86_register_forms) &&
                               memcmp(x86_intel.bytes.pointer, expected_x86_register_forms, sizeof(expected_x86_register_forms)) == 0);
    String8 x86_att_source =
        S8("movq %rbx, %rax\n"
           "movq $0x1122334455667788, %r8\n"
           "addq $7, %rax\n"
           "subl %r10d, %r9d\n"
           "andw %cx, %ax\n"
           "xorb %r9b, %r8b\n"
           "imulq %r12, %r11\n"
           "pushq %r15\n"
           "popq %r15\n"
           "incq %rax\n"
           "negl %r8d\n"
           "shlq $3, %rax\n"
           "cbtw\n"
           "cwtl\n"
           "cltq\n"
           "callq *%r11\n"
           "addq %r13, %r12\n"
           "orl %ecx, %eax\n"
           "cmpq $-129, %r15\n"
           "testl $0x12345678, %r8d\n"
           "decq %rbx\n"
           "notw %r9w\n"
           "shrl $4, %r10d\n"
           "sarq $1, %r11\n"
           "jmpq *%r14\n");
    AssemblyEncodeResult x86_att = assembly_encode(arguments->arena, x86_att_source,
                                                    (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, x86_att.diagnostic_count == 0);
    BUSTER_TEST(arguments, x86_att.bytes.length == sizeof(expected_x86_register_forms) &&
                               memcmp(x86_att.bytes.pointer, expected_x86_register_forms, sizeof(expected_x86_register_forms)) == 0);
    u8 expected_x86_memory_forms[] = {
        0x48, 0x8b, 0x44, 0x8b, 0x10,
        0x47, 0x89, 0x54, 0xcc, 0xe0,
        0x66, 0x03, 0x45, 0x00,
        0x49, 0x83, 0x6d, 0x7f, 0x05,
        0x80, 0x36, 0x7f,
        0x4c, 0x0f, 0xaf, 0x1d, 0x00, 0x00, 0x00, 0x00,
        0x41, 0xff, 0x00,
        0x48, 0xd1, 0x64, 0x24, 0x08,
        0xff, 0x50, 0x18,
        0xc7, 0x05, 0x00, 0x00, 0x00, 0x00, 0x78, 0x56, 0x34, 0x12,
        0xc3,
    };
    String8 x86_intel_memory_source =
        S8("mov rax, [rbx + rcx*4 + 16]\n"
           "mov [r12 + r9*8 - 32], r10d\n"
           "add ax, word ptr [rbp]\n"
           "sub qword ptr [r13 + 127], 5\n"
           "xor byte ptr [rsi], 0x7f\n"
           "imul r11, [rip + external]\n"
           "inc dword ptr [r8]\n"
           "shl qword ptr [rsp + 8], 1\n"
           "call qword ptr [rax + 24]\n"
           "mov dword ptr [rip + local], 0x12345678\n"
           "local:\n"
           "ret\n");
    AssemblyEncodeResult x86_intel_memory = assembly_encode(
        arguments->arena, x86_intel_memory_source, (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, x86_intel_memory.diagnostic_count == 0);
    BUSTER_TEST(arguments, x86_intel_memory.bytes.length == sizeof(expected_x86_memory_forms) &&
                               memcmp(x86_intel_memory.bytes.pointer, expected_x86_memory_forms, sizeof(expected_x86_memory_forms)) == 0);
    BUSTER_TEST(arguments, x86_intel_memory.relocation_count == 1 && x86_intel_memory.relocations[0].offset == 26 &&
                               x86_intel_memory.relocations[0].addend == -4 &&
                               x86_intel_memory.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC32 &&
                               string_equal(x86_intel_memory.symbols[x86_intel_memory.relocations[0].symbol].name, S8("external")));
    String8 x86_att_memory_source =
        S8("movq 16(%rbx,%rcx,4), %rax\n"
           "movl %r10d, -32(%r12,%r9,8)\n"
           "addw (%rbp), %ax\n"
           "subq $5, 127(%r13)\n"
           "xorb $0x7f, (%rsi)\n"
           "imulq external(%rip), %r11\n"
           "incl (%r8)\n"
           "shlq $1, 8(%rsp)\n"
           "callq *24(%rax)\n"
           "movl $0x12345678, local(%rip)\n"
           "local:\n"
           "ret\n");
    AssemblyEncodeResult x86_att_memory = assembly_encode(
        arguments->arena, x86_att_memory_source, (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, x86_att_memory.diagnostic_count == 0);
    BUSTER_TEST(arguments, x86_att_memory.bytes.length == sizeof(expected_x86_memory_forms) &&
                               memcmp(x86_att_memory.bytes.pointer, expected_x86_memory_forms, sizeof(expected_x86_memory_forms)) == 0);
    BUSTER_TEST(arguments, x86_att_memory.relocation_count == 1 && x86_att_memory.relocations[0].offset == 26 &&
                               x86_att_memory.relocations[0].addend == -4 &&
                               x86_att_memory.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC32);
    AssemblyEncodeResult x86_absolute_memory = assembly_encode(
        arguments->arena, S8("mov rax, [rbx + external + 8]\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_x86_absolute_memory[] = {0x48, 0x8b, 0x83, 0x00, 0x00, 0x00, 0x00};
    BUSTER_TEST(arguments, x86_absolute_memory.diagnostic_count == 0 &&
                               x86_absolute_memory.bytes.length == sizeof(expected_x86_absolute_memory) &&
                               memcmp(x86_absolute_memory.bytes.pointer, expected_x86_absolute_memory, sizeof(expected_x86_absolute_memory)) == 0);
    BUSTER_TEST(arguments, x86_absolute_memory.relocation_count == 1 && x86_absolute_memory.relocations[0].offset == 3 &&
                               x86_absolute_memory.relocations[0].addend == 8 &&
                               x86_absolute_memory.relocations[0].kind == ASSEMBLY_RELOCATION_X86_32);
    AssemblyEncodeResult invalid_x86_forms =
        assembly_encode(arguments->arena, S8("mov rax, eax\nadd rax, 0x80000000\nnopq\n"),
                        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_x86_forms.diagnostic_count == 3);
    AssemblyEncodeResult invalid_att_forms =
        assembly_encode(arguments->arena, S8("movq %rbx, rax\naddq 3, %rax\ncallq %r11\n"),
                        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, invalid_att_forms.diagnostic_count == 3);
    AssemblyEncodeResult invalid_x86_memory =
        assembly_encode(arguments->arena, S8("mov rax, [rsp*2]\nmov rax, [rip + rbx]\ninc [rax]\nmov rax, [rbx + 0x80000000]\n"),
                        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_x86_memory.diagnostic_count == 4);

    Target aarch64_target = {
        .cpu_arch = CPU_ARCH_AARCH64,
        .os = OPERATING_SYSTEM_LINUX,
    };
    AssemblyEncodeResult aarch64 = assembly_encode(arguments->arena, S8("entry:\n nop\n bl external\n b entry\n ret\n"),
                                                    (AssemblyEncodeOptions){
                                                        .target = aarch64_target,
                                                    });
    u8 expected_aarch64[] = {
        0x1f, 0x20, 0x03, 0xd5, 0x00, 0x00, 0x00, 0x94,
        0xfe, 0xff, 0xff, 0x17, 0xc0, 0x03, 0x5f, 0xd6,
    };
    BUSTER_TEST(arguments, aarch64.diagnostic_count == 0);
    BUSTER_TEST(arguments, aarch64.bytes.length == sizeof(expected_aarch64) &&
                               memcmp(aarch64.bytes.pointer, expected_aarch64, sizeof(expected_aarch64)) == 0);
    BUSTER_TEST(arguments, aarch64.relocation_count == 1 && aarch64.relocations[0].offset == 4 &&
                               aarch64.relocations[0].kind == ASSEMBLY_RELOCATION_AARCH64_BRANCH26);

    AssemblyEncodeResult invalid = assembly_encode(arguments->arena, S8("same:\n same: nop\n ret x0\n unknown\n"),
                                                    (AssemblyEncodeOptions){
                                                        .target = aarch64_target,
                                                    });
    BUSTER_TEST(arguments, invalid.diagnostic_count == 3);
    if (invalid.diagnostic_count == 3)
    {
        BUSTER_TEST(arguments, invalid.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_DUPLICATE_SYMBOL);
        BUSTER_TEST(arguments, invalid.diagnostics[1].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
        BUSTER_TEST(arguments, invalid.diagnostics[2].kind == ASSEMBLY_DIAGNOSTIC_UNKNOWN_INSTRUCTION);
    }
    AssemblyEncodeResult invalid_syntax = assembly_encode(arguments->arena, S8("nop"),
                                                           (AssemblyEncodeOptions){
                                                               .target = aarch64_target,
                                                               .syntax = ASSEMBLY_SYNTAX_ATT,
                                                           });
    BUSTER_TEST(arguments, invalid_syntax.diagnostic_count == 1 &&
                               invalid_syntax.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_SYNTAX);
    return result;
}
