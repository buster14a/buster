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

    AssemblyEncodeResult x86_syntax_switches = assembly_encode(
        arguments->arena,
        S8("mov rax, rbx ; Intel comment\n"
           ".att_syntax prefix\n"
           "movq %rcx, %rdx # AT&T comment\n"
           ".intel_syntax noprefix\n"
           "add r8, r9 // common comment\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_x86_syntax_switches[] = {
        0x48, 0x89, 0xd8,
        0x48, 0x89, 0xca,
        0x4d, 0x01, 0xc8,
    };
    BUSTER_TEST(arguments, x86_syntax_switches.diagnostic_count == 0 &&
                               x86_syntax_switches.bytes.length == sizeof(expected_x86_syntax_switches) &&
                               memcmp(x86_syntax_switches.bytes.pointer, expected_x86_syntax_switches,
                                      sizeof(expected_x86_syntax_switches)) == 0);
    AssemblyEncodeResult invalid_x86_syntax_switches = assembly_encode(
        arguments->arena, S8(".intel_syntax prefix\n.att_syntax noprefix\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_x86_syntax_switches.diagnostic_count == 2 &&
                               invalid_x86_syntax_switches.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_SYNTAX &&
                               invalid_x86_syntax_switches.diagnostics[1].kind == ASSEMBLY_DIAGNOSTIC_INVALID_SYNTAX);
    Target aarch64_syntax_target = {
        .cpu_arch = CPU_ARCH_AARCH64,
        .os = OPERATING_SYSTEM_LINUX,
    };
    AssemblyEncodeResult invalid_aarch64_syntax_switch = assembly_encode(
        arguments->arena, S8(".intel_syntax noprefix\n"),
        (AssemblyEncodeOptions){.target = aarch64_syntax_target, .syntax = ASSEMBLY_SYNTAX_DEFAULT});
    BUSTER_TEST(arguments, invalid_aarch64_syntax_switch.diagnostic_count == 1 &&
                               invalid_aarch64_syntax_switch.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_SYNTAX);

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

    u8 expected_x86_adc_sbb[] = {
        0x10, 0xd8,
        0x66, 0x11, 0xc8,
        0x11, 0xc8,
        0x4d, 0x11, 0xc8,
        0x45, 0x10, 0x4c, 0x24, 0x08,
        0x66, 0x45, 0x13, 0x55, 0x7f,
        0x48, 0x83, 0xd0, 0x7f,
        0x48, 0x81, 0xd3, 0x80, 0x00, 0x00, 0x00,
        0x18, 0xd8,
        0x66, 0x19, 0xc8,
        0x19, 0xc8,
        0x4d, 0x19, 0xc8,
        0x45, 0x18, 0x4c, 0x24, 0x08,
        0x66, 0x45, 0x1b, 0x55, 0x7f,
        0x48, 0x83, 0xd8, 0x7f,
        0x48, 0x81, 0xdb, 0x80, 0x00, 0x00, 0x00,
    };
    String8 x86_intel_adc_sbb_source =
        S8("adc al, bl\n"
           "adc ax, cx\n"
           "adc eax, ecx\n"
           "adc r8, r9\n"
           "adc byte ptr [r12 + 8], r9b\n"
           "adc r10w, word ptr [r13 + 127]\n"
           "adc rax, 127\n"
           "adc rbx, 128\n"
           "sbb al, bl\n"
           "sbb ax, cx\n"
           "sbb eax, ecx\n"
           "sbb r8, r9\n"
           "sbb byte ptr [r12 + 8], r9b\n"
           "sbb r10w, word ptr [r13 + 127]\n"
           "sbb rax, 127\n"
           "sbb rbx, 128\n");
    AssemblyEncodeResult x86_intel_adc_sbb = assembly_encode(
        arguments->arena, x86_intel_adc_sbb_source, (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, x86_intel_adc_sbb.diagnostic_count == 0 &&
                               x86_intel_adc_sbb.bytes.length == sizeof(expected_x86_adc_sbb) &&
                               memcmp(x86_intel_adc_sbb.bytes.pointer, expected_x86_adc_sbb, sizeof(expected_x86_adc_sbb)) == 0);
    String8 x86_att_adc_sbb_source =
        S8("adcb %bl, %al\n"
           "adcw %cx, %ax\n"
           "adcl %ecx, %eax\n"
           "adcq %r9, %r8\n"
           "adcb %r9b, 8(%r12)\n"
           "adcw 127(%r13), %r10w\n"
           "adcq $127, %rax\n"
           "adcq $128, %rbx\n"
           "sbbb %bl, %al\n"
           "sbbw %cx, %ax\n"
           "sbbl %ecx, %eax\n"
           "sbbq %r9, %r8\n"
           "sbbb %r9b, 8(%r12)\n"
           "sbbw 127(%r13), %r10w\n"
           "sbbq $127, %rax\n"
           "sbbq $128, %rbx\n");
    AssemblyEncodeResult x86_att_adc_sbb = assembly_encode(
        arguments->arena, x86_att_adc_sbb_source, (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, x86_att_adc_sbb.diagnostic_count == 0 &&
                               x86_att_adc_sbb.bytes.length == sizeof(expected_x86_adc_sbb) &&
                               memcmp(x86_att_adc_sbb.bytes.pointer, expected_x86_adc_sbb, sizeof(expected_x86_adc_sbb)) == 0);

    u8 expected_x86_unary_integer[] = {
        0xf6, 0xe0,
        0x66, 0xf7, 0xe1,
        0xf7, 0xe2,
        0x49, 0xf7, 0xe0,
        0xf6, 0xeb,
        0x66, 0xf7, 0xe9,
        0xf7, 0xea,
        0x49, 0xf7, 0xe9,
        0x41, 0xf6, 0x64, 0x24, 0x08,
        0x66, 0x41, 0xf7, 0x6d, 0x10,
        0x41, 0xf7, 0x30,
        0x49, 0xf7, 0x79, 0x7f,
        0xf6, 0xf1,
        0x66, 0xf7, 0xf6,
        0xf7, 0xf6,
        0x49, 0xf7, 0xf2,
        0xf6, 0xf9,
        0x66, 0xf7, 0xff,
        0xf7, 0xfe,
        0x49, 0xf7, 0xfb,
    };
    String8 x86_intel_unary_integer_source =
        S8("mul al\n"
           "mul cx\n"
           "mul edx\n"
           "mul r8\n"
           "imul bl\n"
           "imul cx\n"
           "imul edx\n"
           "imul r9\n"
           "mul byte ptr [r12 + 8]\n"
           "imul word ptr [r13 + 16]\n"
           "div dword ptr [r8]\n"
           "idiv qword ptr [r9 + 127]\n"
           "div cl\n"
           "div si\n"
           "div esi\n"
           "div r10\n"
           "idiv cl\n"
           "idiv di\n"
           "idiv esi\n"
           "idiv r11\n");
    AssemblyEncodeResult x86_intel_unary_integer = assembly_encode(
        arguments->arena, x86_intel_unary_integer_source,
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, x86_intel_unary_integer.diagnostic_count == 0 &&
                               x86_intel_unary_integer.bytes.length == sizeof(expected_x86_unary_integer) &&
                               memcmp(x86_intel_unary_integer.bytes.pointer, expected_x86_unary_integer,
                                      sizeof(expected_x86_unary_integer)) == 0);
    String8 x86_att_unary_integer_source =
        S8("mulb %al\n"
           "mulw %cx\n"
           "mull %edx\n"
           "mulq %r8\n"
           "imulb %bl\n"
           "imulw %cx\n"
           "imull %edx\n"
           "imulq %r9\n"
           "mulb 8(%r12)\n"
           "imulw 16(%r13)\n"
           "divl (%r8)\n"
           "idivq 127(%r9)\n"
           "divb %cl\n"
           "divw %si\n"
           "divl %esi\n"
           "divq %r10\n"
           "idivb %cl\n"
           "idivw %di\n"
           "idivl %esi\n"
           "idivq %r11\n");
    AssemblyEncodeResult x86_att_unary_integer = assembly_encode(
        arguments->arena, x86_att_unary_integer_source,
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, x86_att_unary_integer.diagnostic_count == 0 &&
                               x86_att_unary_integer.bytes.length == sizeof(expected_x86_unary_integer) &&
                               memcmp(x86_att_unary_integer.bytes.pointer, expected_x86_unary_integer,
                                      sizeof(expected_x86_unary_integer)) == 0);

    u8 expected_x86_imul_integer[] = {
        0x66, 0x0f, 0xaf, 0xc1,
        0x41, 0x0f, 0xaf, 0xc1,
        0x4d, 0x0f, 0xaf, 0xd3,
        0x66, 0x45, 0x0f, 0xaf, 0x65, 0x20,
        0x66, 0x6b, 0xc0, 0x80,
        0x6b, 0xc0, 0x7f,
        0x4d, 0x69, 0xc0, 0x80, 0x00, 0x00, 0x00,
        0x4d, 0x69, 0xc9, 0x7f, 0xff, 0xff, 0xff,
        0x66, 0x6b, 0xc1, 0x80,
        0x41, 0x6b, 0x44, 0x24, 0x08, 0x7f,
        0x4d, 0x69, 0xc1, 0x80, 0x00, 0x00, 0x00,
        0x4d, 0x69, 0x54, 0x24, 0x08, 0x7f, 0xff, 0xff, 0xff,
    };
    String8 x86_intel_imul_integer_source =
        S8("imul ax, cx\n"
           "imul eax, r9d\n"
           "imul r10, r11\n"
           "imul r12w, word ptr [r13 + 32]\n"
           "imul ax, -128\n"
           "imul eax, 127\n"
           "imul r8, 128\n"
           "imul r9, -129\n"
           "imul ax, cx, -128\n"
           "imul eax, dword ptr [r12 + 8], 127\n"
           "imul r8, r9, 128\n"
           "imul r10, qword ptr [r12 + 8], -129\n");
    AssemblyEncodeResult x86_intel_imul_integer = assembly_encode(
        arguments->arena, x86_intel_imul_integer_source,
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, x86_intel_imul_integer.diagnostic_count == 0 &&
                               x86_intel_imul_integer.bytes.length == sizeof(expected_x86_imul_integer) &&
                               memcmp(x86_intel_imul_integer.bytes.pointer, expected_x86_imul_integer,
                                      sizeof(expected_x86_imul_integer)) == 0);
    String8 x86_att_imul_integer_source =
        S8("imulw %cx, %ax\n"
           "imull %r9d, %eax\n"
           "imulq %r11, %r10\n"
           "imulw 32(%r13), %r12w\n"
           "imulw $-128, %ax\n"
           "imull $127, %eax\n"
           "imulq $128, %r8\n"
           "imulq $-129, %r9\n"
           "imulw $-128, %cx, %ax\n"
           "imull $127, 8(%r12), %eax\n"
           "imulq $128, %r9, %r8\n"
           "imulq $-129, 8(%r12), %r10\n");
    AssemblyEncodeResult x86_att_imul_integer = assembly_encode(
        arguments->arena, x86_att_imul_integer_source,
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, x86_att_imul_integer.diagnostic_count == 0 &&
                               x86_att_imul_integer.bytes.length == sizeof(expected_x86_imul_integer) &&
                               memcmp(x86_att_imul_integer.bytes.pointer, expected_x86_imul_integer,
                                      sizeof(expected_x86_imul_integer)) == 0);

    u8 expected_x86_cwd_cdq_cqo[] = {0x66, 0x99, 0x99, 0x48, 0x99};
    AssemblyEncodeResult x86_intel_cwd_cdq_cqo = assembly_encode(
        arguments->arena, S8("cwd\ncdq\ncqo\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, x86_intel_cwd_cdq_cqo.diagnostic_count == 0 &&
                               x86_intel_cwd_cdq_cqo.bytes.length == sizeof(expected_x86_cwd_cdq_cqo) &&
                               memcmp(x86_intel_cwd_cdq_cqo.bytes.pointer, expected_x86_cwd_cdq_cqo,
                                      sizeof(expected_x86_cwd_cdq_cqo)) == 0);
    AssemblyEncodeResult x86_att_cwd_cdq_cqo = assembly_encode(
        arguments->arena, S8("cwtd\ncltd\ncqto\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, x86_att_cwd_cdq_cqo.diagnostic_count == 0 &&
                               x86_att_cwd_cdq_cqo.bytes.length == sizeof(expected_x86_cwd_cdq_cqo) &&
                               memcmp(x86_att_cwd_cdq_cqo.bytes.pointer, expected_x86_cwd_cdq_cqo,
                                      sizeof(expected_x86_cwd_cdq_cqo)) == 0);

    u8 expected_x86_imul_rip_relative[] = {0x4c, 0x0f, 0xaf, 0x15, 0x00, 0x00, 0x00, 0x00};
    AssemblyEncodeResult x86_intel_imul_rip_relative = assembly_encode(
        arguments->arena, S8("imul r10, qword ptr [rip + external]\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, x86_intel_imul_rip_relative.diagnostic_count == 0 &&
                               x86_intel_imul_rip_relative.bytes.length == sizeof(expected_x86_imul_rip_relative) &&
                               memcmp(x86_intel_imul_rip_relative.bytes.pointer, expected_x86_imul_rip_relative,
                                      sizeof(expected_x86_imul_rip_relative)) == 0);
    BUSTER_TEST(arguments, x86_intel_imul_rip_relative.relocation_count == 1 &&
                               x86_intel_imul_rip_relative.relocations[0].offset == 4 &&
                               x86_intel_imul_rip_relative.relocations[0].addend == -4 &&
                               x86_intel_imul_rip_relative.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC32 &&
                               string_equal(x86_intel_imul_rip_relative.symbols[x86_intel_imul_rip_relative.relocations[0].symbol].name,
                                            S8("external")));
    AssemblyEncodeResult x86_att_imul_rip_relative = assembly_encode(
        arguments->arena, S8("imulq external(%rip), %r10\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, x86_att_imul_rip_relative.diagnostic_count == 0 &&
                               x86_att_imul_rip_relative.bytes.length == sizeof(expected_x86_imul_rip_relative) &&
                               memcmp(x86_att_imul_rip_relative.bytes.pointer, expected_x86_imul_rip_relative,
                                      sizeof(expected_x86_imul_rip_relative)) == 0 &&
                               x86_att_imul_rip_relative.relocation_count == 1 &&
                               x86_att_imul_rip_relative.relocations[0].offset == 4 &&
                               x86_att_imul_rip_relative.relocations[0].addend == -4 &&
                               x86_att_imul_rip_relative.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC32);

    AssemblyEncodeResult invalid_x86_integer_increment = assembly_encode(
        arguments->arena,
        S8("adc eax, rbx\n"
           "sbb rax, external\n"
           "mul eax, ecx\n"
           "imul al, bl\n"
           "imul al, bl, 1\n"
           "imul eax, ebx, external\n"
           "imul eax, ebx, 0x80000000\n"
           "cwd eax\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_x86_integer_increment.diagnostic_count == 8);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_x86_integer_increment.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, invalid_x86_integer_increment.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }
    AssemblyEncodeResult invalid_att_integer_increment = assembly_encode(
        arguments->arena,
        S8("adcq %rax, %eax\n"
           "sbbq external, %rax\n"
           "mulq $1, %rax\n"
           "imulb %bl, %al\n"
           "imulb $1, %bl, %al\n"
           "imulq $external, %rax\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, invalid_att_integer_increment.diagnostic_count == 6);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_att_integer_increment.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, invalid_att_integer_increment.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }
    u8 expected_x86_sse2[] = {
        0x0f, 0x28, 0xc1,
        0x47, 0x0f, 0x10, 0x44, 0x4c, 0x20,
        0x66, 0x45, 0x0f, 0x29, 0x7d, 0x00,
        0x66, 0x0f, 0x10, 0x15, 0x00, 0x00, 0x00, 0x00,
        0x66, 0x0f, 0x6f, 0xdc,
        0xf3, 0x44, 0x0f, 0x7f, 0x48, 0x01,
        0x45, 0x0f, 0x57, 0xd3,
        0x66, 0x44, 0x0f, 0x57, 0x65, 0x00,
        0x66, 0x0f, 0xef, 0xca,
        0x0f, 0x58, 0xdc,
        0x66, 0x0f, 0x58, 0xee,
        0xf3, 0x41, 0x0f, 0x58, 0xf8,
        0xf2, 0x45, 0x0f, 0x58, 0xca,
        0x45, 0x0f, 0x5c, 0xdc,
        0x66, 0x45, 0x0f, 0x5c, 0xee,
        0x44, 0x0f, 0x59, 0xf8,
        0x66, 0x0f, 0x59, 0xca,
        0x0f, 0x5e, 0xdc,
        0x66, 0x0f, 0x5e, 0xee,
    };
    String8 x86_intel_sse2_source =
        S8("movaps xmm0, xmm1\n"
           "movups xmm8, [r12 + r9*2 + 32]\n"
           "movapd [r13], xmm15\n"
           "movupd xmm2, [rip + external]\n"
           "movdqa xmm3, xmm4\n"
           "movdqu [rax + 1], xmm9\n"
           "xorps xmm10, xmm11\n"
           "xorpd xmm12, [rbp]\n"
           "pxor xmm1, xmm2\n"
           "addps xmm3, xmm4\n"
           "addpd xmm5, xmm6\n"
           "addss xmm7, xmm8\n"
           "addsd xmm9, xmm10\n"
           "subps xmm11, xmm12\n"
           "subpd xmm13, xmm14\n"
           "mulps xmm15, xmm0\n"
           "mulpd xmm1, xmm2\n"
           "divps xmm3, xmm4\n"
           "divpd xmm5, xmm6\n");
    AssemblyEncodeResult x86_intel_sse2 = assembly_encode(
        arguments->arena, x86_intel_sse2_source, (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, x86_intel_sse2.diagnostic_count == 0 && x86_intel_sse2.bytes.length == sizeof(expected_x86_sse2) &&
                               memcmp(x86_intel_sse2.bytes.pointer, expected_x86_sse2, sizeof(expected_x86_sse2)) == 0);
    BUSTER_TEST(arguments, x86_intel_sse2.relocation_count == 1 && x86_intel_sse2.relocations[0].offset == 19 &&
                               x86_intel_sse2.relocations[0].addend == -4 &&
                               x86_intel_sse2.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC32);
    String8 x86_att_sse2_source =
        S8("movaps %xmm1, %xmm0\n"
           "movups 32(%r12,%r9,2), %xmm8\n"
           "movapd %xmm15, (%r13)\n"
           "movupd external(%rip), %xmm2\n"
           "movdqa %xmm4, %xmm3\n"
           "movdqu %xmm9, 1(%rax)\n"
           "xorps %xmm11, %xmm10\n"
           "xorpd (%rbp), %xmm12\n"
           "pxor %xmm2, %xmm1\n"
           "addps %xmm4, %xmm3\n"
           "addpd %xmm6, %xmm5\n"
           "addss %xmm8, %xmm7\n"
           "addsd %xmm10, %xmm9\n"
           "subps %xmm12, %xmm11\n"
           "subpd %xmm14, %xmm13\n"
           "mulps %xmm0, %xmm15\n"
           "mulpd %xmm2, %xmm1\n"
           "divps %xmm4, %xmm3\n"
           "divpd %xmm6, %xmm5\n");
    AssemblyEncodeResult x86_att_sse2 = assembly_encode(
        arguments->arena, x86_att_sse2_source, (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, x86_att_sse2.diagnostic_count == 0 && x86_att_sse2.bytes.length == sizeof(expected_x86_sse2) &&
                               memcmp(x86_att_sse2.bytes.pointer, expected_x86_sse2, sizeof(expected_x86_sse2)) == 0);
    Target x86_without_sse2 = x86_target;
    x86_without_sse2.cpu_features_explicit = true;
    x86_without_sse2.cpu_features = 0;
    AssemblyEncodeResult unsupported_sse2 = assembly_encode(
        arguments->arena, S8("pxor xmm0, xmm0\n"),
        (AssemblyEncodeOptions){.target = x86_without_sse2, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, unsupported_sse2.diagnostic_count == 1 &&
                               unsupported_sse2.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
    u8 expected_x86_mmx[] = {
        0x0f, 0x6f, 0xc1,
        0x41, 0x0f, 0x7f, 0x7c, 0x24, 0x08,
        0x0f, 0xfc, 0xd3,
        0x0f, 0xfd, 0xe5,
        0x0f, 0xfe, 0xf7,
        0x0f, 0xd4, 0x45, 0x00,
        0x0f, 0xf8, 0xca,
        0x0f, 0xf9, 0xdc,
        0x0f, 0xfa, 0xee,
        0x0f, 0xfb, 0xf8,
        0x0f, 0xdb, 0xca,
        0x0f, 0xeb, 0xdc,
        0x0f, 0xef, 0xee,
        0x0f, 0x74, 0xf8,
        0x0f, 0x75, 0xca,
        0x0f, 0x76, 0xdc,
        0x0f, 0x64, 0xee,
        0x0f, 0x65, 0xf8,
        0x0f, 0x66, 0xca,
        0x0f, 0xd5, 0xdc,
        0x0f, 0x77,
    };
    String8 x86_intel_mmx_source =
        S8("movq mm0, mm1\n"
           "movq [r12 + 8], mm7\n"
           "paddb mm2, mm3\n"
           "paddw mm4, mm5\n"
           "paddd mm6, mm7\n"
           "paddq mm0, [rbp]\n"
           "psubb mm1, mm2\n"
           "psubw mm3, mm4\n"
           "psubd mm5, mm6\n"
           "psubq mm7, mm0\n"
           "pand mm1, mm2\n"
           "por mm3, mm4\n"
           "pxor mm5, mm6\n"
           "pcmpeqb mm7, mm0\n"
           "pcmpeqw mm1, mm2\n"
           "pcmpeqd mm3, mm4\n"
           "pcmpgtb mm5, mm6\n"
           "pcmpgtw mm7, mm0\n"
           "pcmpgtd mm1, mm2\n"
           "pmullw mm3, mm4\n"
           "emms\n");
    AssemblyEncodeResult x86_intel_mmx = assembly_encode(
        arguments->arena, x86_intel_mmx_source, (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, x86_intel_mmx.diagnostic_count == 0 && x86_intel_mmx.bytes.length == sizeof(expected_x86_mmx) &&
                               memcmp(x86_intel_mmx.bytes.pointer, expected_x86_mmx, sizeof(expected_x86_mmx)) == 0);
    String8 x86_att_mmx_source =
        S8("movq %mm1, %mm0\n"
           "movq %mm7, 8(%r12)\n"
           "paddb %mm3, %mm2\n"
           "paddw %mm5, %mm4\n"
           "paddd %mm7, %mm6\n"
           "paddq (%rbp), %mm0\n"
           "psubb %mm2, %mm1\n"
           "psubw %mm4, %mm3\n"
           "psubd %mm6, %mm5\n"
           "psubq %mm0, %mm7\n"
           "pand %mm2, %mm1\n"
           "por %mm4, %mm3\n"
           "pxor %mm6, %mm5\n"
           "pcmpeqb %mm0, %mm7\n"
           "pcmpeqw %mm2, %mm1\n"
           "pcmpeqd %mm4, %mm3\n"
           "pcmpgtb %mm6, %mm5\n"
           "pcmpgtw %mm0, %mm7\n"
           "pcmpgtd %mm2, %mm1\n"
           "pmullw %mm4, %mm3\n"
           "emms\n");
    AssemblyEncodeResult x86_att_mmx = assembly_encode(
        arguments->arena, x86_att_mmx_source, (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, x86_att_mmx.diagnostic_count == 0 && x86_att_mmx.bytes.length == sizeof(expected_x86_mmx) &&
                               memcmp(x86_att_mmx.bytes.pointer, expected_x86_mmx, sizeof(expected_x86_mmx)) == 0);
    u8 expected_x86_x87[] = {
        0xd9, 0xc3, 0xdd, 0xd4, 0xdd, 0xdd, 0xd9, 0xce,
        0xd8, 0xc2, 0xdc, 0xcb, 0xd8, 0xe4, 0xdc, 0xe5,
        0xd8, 0xf6, 0xdc, 0xf7, 0xde, 0xc1, 0xde, 0xca,
        0xde, 0xeb, 0xde, 0xe4, 0xde, 0xfd, 0xde, 0xf6,
        0xd9, 0x40, 0x08, 0x41, 0xdd, 0x01, 0x41, 0xdb, 0x2c, 0x24,
        0x41, 0xd9, 0x55, 0x10, 0x41, 0xdd, 0x16, 0x41, 0xd9, 0x1f,
        0xdd, 0x1b, 0xdb, 0x39, 0xdf, 0x02, 0xdb, 0x06, 0xdf, 0x2f,
        0x41, 0xdf, 0x10, 0xdb, 0x55, 0x00, 0xdf, 0x1c, 0x24,
        0x41, 0xdb, 0x1a, 0x41, 0xdf, 0x3b,
        0xd8, 0x00, 0xdc, 0x09, 0xd8, 0x22, 0xdc, 0x2b, 0xd8, 0x36, 0xdc, 0x3f,
        0xd9, 0xf0, 0xd9, 0xe1, 0xd9, 0xe0, 0xd9, 0xe8, 0xd9, 0xee,
        0xd9, 0xeb, 0xd9, 0xea, 0xd9, 0xe9, 0xd9, 0xec, 0xd9, 0xed,
        0xd9, 0xfa, 0xd9, 0xfe, 0xd9, 0xff, 0xd9, 0xfb, 0xd9, 0xf2,
        0xd9, 0xf3, 0xd9, 0xf1, 0xd9, 0xf9, 0xd9, 0xfc, 0xd9, 0xfd,
        0xd9, 0xf8, 0xd9, 0xf5, 0xd9, 0xf4, 0xd9, 0xe4, 0xd9, 0xe5,
        0xd9, 0xd0, 0x9b, 0xdb, 0xe3, 0xdb, 0xe3, 0x9b, 0xdb, 0xe2, 0xdb, 0xe2, 0x9b,
    };
    String8 x86_intel_x87_source =
        S8("fld st(3)\n"
           "fst st(4)\n"
           "fstp st(5)\n"
           "fxch st(6)\n"
           "fadd st(0), st(2)\n"
           "fmul st(3), st(0)\n"
           "fsub st(0), st(4)\n"
           "fsubr st(5), st(0)\n"
           "fdiv st(0), st(6)\n"
           "fdivr st(7), st(0)\n"
           "faddp st(1), st(0)\n"
           "fmulp st(2), st(0)\n"
           "fsubp st(3), st(0)\n"
           "fsubrp st(4), st(0)\n"
           "fdivp st(5), st(0)\n"
           "fdivrp st(6), st(0)\n"
           "fld dword ptr [rax + 8]\n"
           "fld qword ptr [r9]\n"
           "fld tbyte ptr [r12]\n"
           "fst dword ptr [r13 + 16]\n"
           "fst qword ptr [r14]\n"
           "fstp dword ptr [r15]\n"
           "fstp qword ptr [rbx]\n"
           "fstp tbyte ptr [rcx]\n"
           "fild word ptr [rdx]\n"
           "fild dword ptr [rsi]\n"
           "fild qword ptr [rdi]\n"
           "fist word ptr [r8]\n"
           "fist dword ptr [rbp]\n"
           "fistp word ptr [rsp]\n"
           "fistp dword ptr [r10]\n"
           "fistp qword ptr [r11]\n"
           "fadd dword ptr [rax]\n"
           "fmul qword ptr [rcx]\n"
           "fsub dword ptr [rdx]\n"
           "fsubr qword ptr [rbx]\n"
           "fdiv dword ptr [rsi]\n"
           "fdivr qword ptr [rdi]\n"
           "f2xm1\n"
           "fabs\n"
           "fchs\n"
           "fld1\n"
           "fldz\n"
           "fldpi\n"
           "fldl2e\n"
           "fldl2t\n"
           "fldlg2\n"
           "fldln2\n"
           "fsqrt\n"
           "fsin\n"
           "fcos\n"
           "fsincos\n"
           "fptan\n"
           "fpatan\n"
           "fyl2x\n"
           "fyl2xp1\n"
           "frndint\n"
           "fscale\n"
           "fprem\n"
           "fprem1\n"
           "fxtract\n"
           "ftst\n"
           "fxam\n"
           "fnop\n"
           "finit\n"
           "fninit\n"
           "fclex\n"
           "fnclex\n"
           "fwait\n");
    AssemblyEncodeResult x86_intel_x87 = assembly_encode(
        arguments->arena, x86_intel_x87_source, (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, x86_intel_x87.diagnostic_count == 0 && x86_intel_x87.bytes.length == sizeof(expected_x86_x87) &&
                               memcmp(x86_intel_x87.bytes.pointer, expected_x86_x87, sizeof(expected_x86_x87)) == 0);
    String8 x86_att_x87_source =
        S8("fld %st(3)\n"
           "fst %st(4)\n"
           "fstp %st(5)\n"
           "fxch %st(6)\n"
           "fadd %st(2), %st\n"
           "fmul %st, %st(3)\n"
           "fsub %st(4), %st\n"
           "fsub %st, %st(5)\n"
           "fdiv %st(6), %st\n"
           "fdiv %st, %st(7)\n"
           "faddp %st, %st(1)\n"
           "fmulp %st, %st(2)\n"
           "fsubrp %st, %st(3)\n"
           "fsubp %st, %st(4)\n"
           "fdivrp %st, %st(5)\n"
           "fdivp %st, %st(6)\n"
           "flds 8(%rax)\n"
           "fldl (%r9)\n"
           "fldt (%r12)\n"
           "fsts 16(%r13)\n"
           "fstl (%r14)\n"
           "fstps (%r15)\n"
           "fstpl (%rbx)\n"
           "fstpt (%rcx)\n"
           "filds (%rdx)\n"
           "fildl (%rsi)\n"
           "fildq (%rdi)\n"
           "fists (%r8)\n"
           "fistl (%rbp)\n"
           "fistps (%rsp)\n"
           "fistpl (%r10)\n"
           "fistpq (%r11)\n"
           "fadds (%rax)\n"
           "fmull (%rcx)\n"
           "fsubs (%rdx)\n"
           "fsubrl (%rbx)\n"
           "fdivs (%rsi)\n"
           "fdivrl (%rdi)\n"
           "f2xm1\n"
           "fabs\n"
           "fchs\n"
           "fld1\n"
           "fldz\n"
           "fldpi\n"
           "fldl2e\n"
           "fldl2t\n"
           "fldlg2\n"
           "fldln2\n"
           "fsqrt\n"
           "fsin\n"
           "fcos\n"
           "fsincos\n"
           "fptan\n"
           "fpatan\n"
           "fyl2x\n"
           "fyl2xp1\n"
           "frndint\n"
           "fscale\n"
           "fprem\n"
           "fprem1\n"
           "fxtract\n"
           "ftst\n"
           "fxam\n"
           "fnop\n"
           "finit\n"
           "fninit\n"
           "fclex\n"
           "fnclex\n"
           "fwait\n");
    AssemblyEncodeResult x86_att_x87 = assembly_encode(
        arguments->arena, x86_att_x87_source, (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, x86_att_x87.diagnostic_count == 0 && x86_att_x87.bytes.length == sizeof(expected_x86_x87) &&
                               memcmp(x86_att_x87.bytes.pointer, expected_x86_x87, sizeof(expected_x86_x87)) == 0);
    AssemblyEncodeResult x86_x87_relocation = assembly_encode(
        arguments->arena, S8("fld qword ptr [rip + external_x87]\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_x86_x87_relocation[] = {0xdd, 0x05, 0x00, 0x00, 0x00, 0x00};
    BUSTER_TEST(arguments, x86_x87_relocation.diagnostic_count == 0 &&
                               x86_x87_relocation.bytes.length == sizeof(expected_x86_x87_relocation) &&
                               memcmp(x86_x87_relocation.bytes.pointer, expected_x86_x87_relocation,
                                      sizeof(expected_x86_x87_relocation)) == 0);
    BUSTER_TEST(arguments, x86_x87_relocation.relocation_count == 1 && x86_x87_relocation.relocations[0].offset == 2 &&
                               x86_x87_relocation.relocations[0].addend == -4 &&
                               x86_x87_relocation.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC32 &&
                               string_equal(x86_x87_relocation.symbols[x86_x87_relocation.relocations[0].symbol].name,
                                            S8("external_x87")));
    u8 expected_x86_x87_state[] = {
        0xd9, 0x28, 0xd9, 0x39, 0x9b, 0xd9, 0x3a, 0xd9, 0x23, 0xd9, 0x34, 0x24, 0x9b, 0xd9, 0x75, 0x00,
        0xdd, 0x26, 0xdd, 0x37, 0x9b, 0x41, 0xdd, 0x30, 0xdf, 0xe0, 0x9b, 0xdf, 0xe0, 0x41, 0xdd, 0x39,
        0x9b, 0x41, 0xdd, 0x3a, 0x41, 0xdf, 0x23, 0x41, 0xdf, 0x34, 0x24, 0xdd, 0xc3, 0xdf, 0xc4,
        0xd9, 0xf7, 0xd9, 0xf6,
    };
    String8 x86_intel_x87_state_source =
        S8("fldcw word ptr [rax]\n"
           "fnstcw word ptr [rcx]\n"
           "fstcw word ptr [rdx]\n"
           "fldenv [rbx]\n"
           "fnstenv [rsp]\n"
           "fstenv [rbp]\n"
           "frstor [rsi]\n"
           "fnsave [rdi]\n"
           "fsave [r8]\n"
           "fnstsw ax\n"
           "fstsw ax\n"
           "fnstsw word ptr [r9]\n"
           "fstsw word ptr [r10]\n"
           "fbld tbyte ptr [r11]\n"
           "fbstp tbyte ptr [r12]\n"
           "ffree st(3)\n"
           "ffreep st(4)\n"
           "fincstp\n"
           "fdecstp\n");
    AssemblyEncodeResult x86_intel_x87_state = assembly_encode(
        arguments->arena, x86_intel_x87_state_source, (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, x86_intel_x87_state.diagnostic_count == 0 &&
                               x86_intel_x87_state.bytes.length == sizeof(expected_x86_x87_state) &&
                               memcmp(x86_intel_x87_state.bytes.pointer, expected_x86_x87_state, sizeof(expected_x86_x87_state)) == 0);
    String8 x86_att_x87_state_source =
        S8("fldcw (%rax)\n"
           "fnstcw (%rcx)\n"
           "fstcw (%rdx)\n"
           "fldenv (%rbx)\n"
           "fnstenv (%rsp)\n"
           "fstenv (%rbp)\n"
           "frstor (%rsi)\n"
           "fnsave (%rdi)\n"
           "fsave (%r8)\n"
           "fnstsw %ax\n"
           "fstsw %ax\n"
           "fnstsw (%r9)\n"
           "fstsw (%r10)\n"
           "fbld (%r11)\n"
           "fbstp (%r12)\n"
           "ffree %st(3)\n"
           "ffreep %st(4)\n"
           "fincstp\n"
           "fdecstp\n");
    AssemblyEncodeResult x86_att_x87_state = assembly_encode(
        arguments->arena, x86_att_x87_state_source, (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, x86_att_x87_state.diagnostic_count == 0 &&
                               x86_att_x87_state.bytes.length == sizeof(expected_x86_x87_state) &&
                               memcmp(x86_att_x87_state.bytes.pointer, expected_x86_x87_state, sizeof(expected_x86_x87_state)) == 0);
    AssemblyEncodeResult x86_x87_state_relocation = assembly_encode(
        arguments->arena, S8("fldcw word ptr [rip + external_state]\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_x86_x87_state_relocation[] = {0xd9, 0x2d, 0x00, 0x00, 0x00, 0x00};
    BUSTER_TEST(arguments, x86_x87_state_relocation.diagnostic_count == 0 &&
                               x86_x87_state_relocation.bytes.length == sizeof(expected_x86_x87_state_relocation) &&
                               memcmp(x86_x87_state_relocation.bytes.pointer, expected_x86_x87_state_relocation,
                                      sizeof(expected_x86_x87_state_relocation)) == 0);
    BUSTER_TEST(arguments, x86_x87_state_relocation.relocation_count == 1 &&
                               x86_x87_state_relocation.relocations[0].offset == 2 &&
                               x86_x87_state_relocation.relocations[0].addend == -4 &&
                               x86_x87_state_relocation.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC32 &&
                               string_equal(x86_x87_state_relocation.symbols[x86_x87_state_relocation.relocations[0].symbol].name,
                                            S8("external_state")));
    String8 x86_x87_alias_source_prefix =
        S8("fxch\n"
           "fcom\n"
           "fcomp\n"
           "fucom\n"
           "fucomp\n"
           "fadd\n"
           "fmul\n"
           "fsub\n"
           "fsubr\n"
           "fdiv\n"
           "fdivr\n"
           "faddp\n"
           "fmulp\n"
           "fsubp\n"
           "fsubrp\n"
           "fdivp\n"
           "fdivrp\n");
    String8 x86_intel_x87_alias_source = string_format(
        arguments->arena,
        S8("{S8}"
           "fadd st(2)\n"
           "fmul st(3)\n"
           "fsub st(4)\n"
           "fsubr st(5)\n"
           "fdiv st(6)\n"
           "fdivr st(7)\n"
           "faddp st(2)\n"
           "fmulp st(3)\n"
           "fsubp st(4)\n"
           "fsubrp st(5)\n"
           "fdivp st(6)\n"
           "fdivrp st(7)\n"),
        x86_x87_alias_source_prefix);
    u8 expected_x86_intel_x87_alias[] = {
        0xd9, 0xc9, 0xd8, 0xd1, 0xd8, 0xd9, 0xdd, 0xe1, 0xdd, 0xe9,
        0xde, 0xc1, 0xde, 0xc9, 0xde, 0xe9, 0xde, 0xe1, 0xde, 0xf9, 0xde, 0xf1,
        0xde, 0xc1, 0xde, 0xc9, 0xde, 0xe9, 0xde, 0xe1, 0xde, 0xf9, 0xde, 0xf1,
        0xd8, 0xc2, 0xd8, 0xcb, 0xd8, 0xe4, 0xd8, 0xed, 0xd8, 0xf6, 0xd8, 0xff,
        0xde, 0xc2, 0xde, 0xcb, 0xde, 0xec, 0xde, 0xe5, 0xde, 0xfe, 0xde, 0xf7,
    };
    AssemblyEncodeResult x86_intel_x87_alias = assembly_encode(
        arguments->arena, x86_intel_x87_alias_source, (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, x86_intel_x87_alias.diagnostic_count == 0 &&
                               x86_intel_x87_alias.bytes.length == sizeof(expected_x86_intel_x87_alias) &&
                               memcmp(x86_intel_x87_alias.bytes.pointer, expected_x86_intel_x87_alias,
                                      sizeof(expected_x86_intel_x87_alias)) == 0);
    String8 x86_att_x87_alias_source = string_format(
        arguments->arena,
        S8("{S8}"
           "fadd %st(2)\n"
           "fmul %st(3)\n"
           "fsub %st(4)\n"
           "fsubr %st(5)\n"
           "fdiv %st(6)\n"
           "fdivr %st(7)\n"
           "faddp %st(2)\n"
           "fmulp %st(3)\n"
           "fsubp %st(4)\n"
           "fsubrp %st(5)\n"
           "fdivp %st(6)\n"
           "fdivrp %st(7)\n"),
        x86_x87_alias_source_prefix);
    u8 expected_x86_att_x87_alias[] = {
        0xd9, 0xc9, 0xd8, 0xd1, 0xd8, 0xd9, 0xdd, 0xe1, 0xdd, 0xe9,
        0xde, 0xc1, 0xde, 0xc9, 0xde, 0xe1, 0xde, 0xe9, 0xde, 0xf1, 0xde, 0xf9,
        0xde, 0xc1, 0xde, 0xc9, 0xde, 0xe1, 0xde, 0xe9, 0xde, 0xf1, 0xde, 0xf9,
        0xd8, 0xc2, 0xd8, 0xcb, 0xd8, 0xe4, 0xd8, 0xed, 0xd8, 0xf6, 0xd8, 0xff,
        0xde, 0xc2, 0xde, 0xcb, 0xde, 0xe4, 0xde, 0xed, 0xde, 0xf6, 0xde, 0xff,
    };
    AssemblyEncodeResult x86_att_x87_alias = assembly_encode(
        arguments->arena, x86_att_x87_alias_source, (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, x86_att_x87_alias.diagnostic_count == 0 &&
                               x86_att_x87_alias.bytes.length == sizeof(expected_x86_att_x87_alias) &&
                               memcmp(x86_att_x87_alias.bytes.pointer, expected_x86_att_x87_alias,
                                      sizeof(expected_x86_att_x87_alias)) == 0);
    Target x86_sse3_target = x86_target;
    x86_sse3_target.cpu_features_explicit = true;
    x86_sse3_target.cpu_features = TARGET_CPU_FEATURE_X86_SSE2 | TARGET_CPU_FEATURE_X86_SSE3;
    u8 expected_x86_x87_compare_integer[] = {
        0xd8, 0xd3, 0xd8, 0xdc, 0xd8, 0x10, 0x41, 0xdc, 0x19,
        0xde, 0xd9, 0xdd, 0xe5, 0xdd, 0xee, 0xda, 0xe9,
        0xdb, 0xf1, 0xdf, 0xf2, 0xdb, 0xeb, 0xdf, 0xec,
        0xda, 0xc1, 0xda, 0xca, 0xda, 0xd3, 0xda, 0xdc,
        0xdb, 0xc5, 0xdb, 0xce, 0xdb, 0xd7, 0xdb, 0xd9,
        0xde, 0x00, 0xda, 0x01, 0xde, 0x0a, 0xda, 0x0b,
        0xde, 0x24, 0x24, 0xda, 0x65, 0x00, 0xde, 0x2e, 0xda, 0x2f,
        0x41, 0xde, 0x30, 0x41, 0xda, 0x31, 0x41, 0xde, 0x3a, 0x41, 0xda, 0x3b,
        0x41, 0xdf, 0x0c, 0x24, 0x41, 0xdb, 0x4d, 0x00, 0x41, 0xdd, 0x0e,
    };
    String8 x86_intel_x87_compare_integer_source =
        S8("fcom st(3)\n"
           "fcomp st(4)\n"
           "fcom dword ptr [rax]\n"
           "fcomp qword ptr [r9]\n"
           "fcompp\n"
           "fucom st(5)\n"
           "fucomp st(6)\n"
           "fucompp\n"
           "fcomi st(0), st(1)\n"
           "fcomip st(0), st(2)\n"
           "fucomi st(0), st(3)\n"
           "fucomip st(0), st(4)\n"
           "fcmovb st(0), st(1)\n"
           "fcmove st(0), st(2)\n"
           "fcmovbe st(0), st(3)\n"
           "fcmovu st(0), st(4)\n"
           "fcmovnb st(0), st(5)\n"
           "fcmovne st(0), st(6)\n"
           "fcmovnbe st(0), st(7)\n"
           "fcmovnu st(0), st(1)\n"
           "fiadd word ptr [rax]\n"
           "fiadd dword ptr [rcx]\n"
           "fimul word ptr [rdx]\n"
           "fimul dword ptr [rbx]\n"
           "fisub word ptr [rsp]\n"
           "fisub dword ptr [rbp]\n"
           "fisubr word ptr [rsi]\n"
           "fisubr dword ptr [rdi]\n"
           "fidiv word ptr [r8]\n"
           "fidiv dword ptr [r9]\n"
           "fidivr word ptr [r10]\n"
           "fidivr dword ptr [r11]\n"
           "fisttp word ptr [r12]\n"
           "fisttp dword ptr [r13]\n"
           "fisttp qword ptr [r14]\n");
    AssemblyEncodeResult x86_intel_x87_compare_integer = assembly_encode(
        arguments->arena, x86_intel_x87_compare_integer_source,
        (AssemblyEncodeOptions){.target = x86_sse3_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, x86_intel_x87_compare_integer.diagnostic_count == 0 &&
                               x86_intel_x87_compare_integer.bytes.length == sizeof(expected_x86_x87_compare_integer) &&
                               memcmp(x86_intel_x87_compare_integer.bytes.pointer, expected_x86_x87_compare_integer,
                                      sizeof(expected_x86_x87_compare_integer)) == 0);
    String8 x86_att_x87_compare_integer_source =
        S8("fcom %st(3)\n"
           "fcomp %st(4)\n"
           "fcoms (%rax)\n"
           "fcompl (%r9)\n"
           "fcompp\n"
           "fucom %st(5)\n"
           "fucomp %st(6)\n"
           "fucompp\n"
           "fcomi %st(1), %st\n"
           "fcomip %st(2), %st\n"
           "fucomi %st(3), %st\n"
           "fucomip %st(4), %st\n"
           "fcmovb %st(1), %st\n"
           "fcmove %st(2), %st\n"
           "fcmovbe %st(3), %st\n"
           "fcmovu %st(4), %st\n"
           "fcmovnb %st(5), %st\n"
           "fcmovne %st(6), %st\n"
           "fcmovnbe %st(7), %st\n"
           "fcmovnu %st(1), %st\n"
           "fiadds (%rax)\n"
           "fiaddl (%rcx)\n"
           "fimuls (%rdx)\n"
           "fimull (%rbx)\n"
           "fisubs (%rsp)\n"
           "fisubl (%rbp)\n"
           "fisubrs (%rsi)\n"
           "fisubrl (%rdi)\n"
           "fidivs (%r8)\n"
           "fidivl (%r9)\n"
           "fidivrs (%r10)\n"
           "fidivrl (%r11)\n"
           "fisttps (%r12)\n"
           "fisttpl (%r13)\n"
           "fisttpq (%r14)\n");
    AssemblyEncodeResult x86_att_x87_compare_integer = assembly_encode(
        arguments->arena, x86_att_x87_compare_integer_source,
        (AssemblyEncodeOptions){.target = x86_sse3_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, x86_att_x87_compare_integer.diagnostic_count == 0 &&
                               x86_att_x87_compare_integer.bytes.length == sizeof(expected_x86_x87_compare_integer) &&
                               memcmp(x86_att_x87_compare_integer.bytes.pointer, expected_x86_x87_compare_integer,
                                      sizeof(expected_x86_x87_compare_integer)) == 0);
    AssemblyEncodeResult unsupported_x86_fisttp = assembly_encode(
        arguments->arena, S8("fisttp word ptr [rax]\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, unsupported_x86_fisttp.diagnostic_count == 1 &&
                               unsupported_x86_fisttp.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
    AssemblyEncodeResult invalid_x86_x87_compare_integer = assembly_encode(
        arguments->arena,
        S8("fcom [rax]\n"
           "fcomp tbyte ptr [rax]\n"
           "fucom qword ptr [rax]\n"
           "fcomi st(1), st(0)\n"
           "fcomi st(0), rax\n"
           "fcmovb st(1), st(0)\n"
           "fiadd qword ptr [rax]\n"
           "fiadd st(0)\n"
           "fisttp tbyte ptr [rax]\n"
           "fisttp st(0)\n"),
        (AssemblyEncodeOptions){.target = x86_sse3_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_x86_x87_compare_integer.diagnostic_count == 10);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_x86_x87_compare_integer.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments,
                    invalid_x86_x87_compare_integer.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }
    AssemblyEncodeResult invalid_x86_x87 = assembly_encode(
        arguments->arena,
        S8("fld [rax]\n"
           "fstp word ptr [rax]\n"
           "fild tbyte ptr [rax]\n"
           "fistp tbyte ptr [rax]\n"
           "fadd st(2), st(3)\n"
           "fadd qword ptr [rax], st(0)\n"
           "faddp st(2), st(1)\n"
           "fxch rax\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_x86_x87.diagnostic_count == 8);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_x86_x87.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, invalid_x86_x87.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }
    AssemblyEncodeResult invalid_x86_x87_state = assembly_encode(
        arguments->arena,
        S8("fldcw dword ptr [rax]\n"
           "fldenv qword ptr [rax]\n"
           "fnstsw rax\n"
           "fstsw bx\n"
           "fstcw ax\n"
           "fbld qword ptr [rax]\n"
           "fbstp st(0)\n"
           "ffree rax\n"
           "fincstp st(0)\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_x86_x87_state.diagnostic_count == 9);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_x86_x87_state.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, invalid_x86_x87_state.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }
    AssemblyEncodeResult x86_xmm_packed = assembly_encode(
        arguments->arena, S8("paddd xmm0, xmm1\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_x86_xmm_packed[] = {0x66, 0x0f, 0xfe, 0xc1};
    BUSTER_TEST(arguments, x86_xmm_packed.diagnostic_count == 0 && x86_xmm_packed.bytes.length == sizeof(expected_x86_xmm_packed) &&
                               memcmp(x86_xmm_packed.bytes.pointer, expected_x86_xmm_packed, sizeof(expected_x86_xmm_packed)) == 0);
    AssemblyEncodeResult unsupported_xmm_packed = assembly_encode(
        arguments->arena, S8("paddd xmm0, xmm1\n"),
        (AssemblyEncodeOptions){.target = x86_without_sse2, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, unsupported_xmm_packed.diagnostic_count == 1 &&
                               unsupported_xmm_packed.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
    u8 expected_x86_conditions[] = {
        0x0f, 0x84, 0x00, 0x00, 0x00, 0x00,
        0x0f, 0x85, 0x00, 0x00, 0x00, 0x00,
        0x0f, 0x95, 0xc0,
        0x41, 0x0f, 0x92, 0xc1,
        0x41, 0x0f, 0x9f, 0x45, 0x08,
        0x48, 0x0f, 0x44, 0xc3,
        0x47, 0x0f, 0x42, 0x14, 0x8c,
        0x66, 0x45, 0x0f, 0x49, 0xdc,
    };
    String8 x86_intel_condition_source =
        S8("je external\n"
           "jnz external2\n"
           "setne al\n"
           "setb r9b\n"
           "setg byte ptr [r13 + 8]\n"
           "cmove rax, rbx\n"
           "cmovb r10d, [r12 + r9*4]\n"
           "cmovns r11w, r12w\n");
    AssemblyEncodeResult x86_intel_conditions = assembly_encode(
        arguments->arena, x86_intel_condition_source, (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, x86_intel_conditions.diagnostic_count == 0 &&
                               x86_intel_conditions.bytes.length == sizeof(expected_x86_conditions) &&
                               memcmp(x86_intel_conditions.bytes.pointer, expected_x86_conditions, sizeof(expected_x86_conditions)) == 0);
    BUSTER_TEST(arguments, x86_intel_conditions.relocation_count == 2 && x86_intel_conditions.relocations[0].offset == 2 &&
                               x86_intel_conditions.relocations[1].offset == 8 && x86_intel_conditions.relocations[0].addend == -4 &&
                               x86_intel_conditions.relocations[1].addend == -4 &&
                               x86_intel_conditions.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC32 &&
                               x86_intel_conditions.relocations[1].kind == ASSEMBLY_RELOCATION_X86_PC32);
    String8 x86_att_condition_source =
        S8("je external\n"
           "jnz external2\n"
           "setne %al\n"
           "setb %r9b\n"
           "setg 8(%r13)\n"
           "cmoveq %rbx, %rax\n"
           "cmovbl (%r12,%r9,4), %r10d\n"
           "cmovnsw %r12w, %r11w\n");
    AssemblyEncodeResult x86_att_conditions = assembly_encode(
        arguments->arena, x86_att_condition_source, (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, x86_att_conditions.diagnostic_count == 0 && x86_att_conditions.bytes.length == sizeof(expected_x86_conditions) &&
                               memcmp(x86_att_conditions.bytes.pointer, expected_x86_conditions, sizeof(expected_x86_conditions)) == 0);
    Target x86_avx_target = x86_target;
    x86_avx_target.cpu_features_explicit = true;
    x86_avx_target.cpu_features = TARGET_CPU_FEATURE_X86_SSE2 | TARGET_CPU_FEATURE_X86_AVX;
    u8 expected_x86_avx[] = {
        0xc5, 0xfc, 0x28, 0xc1,
        0xc4, 0x01, 0x7c, 0x10, 0x44, 0x4c, 0x20,
        0xc4, 0x41, 0x7d, 0x29, 0x7d, 0x00,
        0xc5, 0xf9, 0x10, 0x15, 0x00, 0x00, 0x00, 0x00,
        0xc4, 0x41, 0x24, 0x57, 0xd4,
        0xc5, 0xe9, 0x57, 0xcb,
        0xc5, 0xdc, 0x58, 0xdd,
        0xc5, 0xc5, 0x58, 0x75, 0x00,
        0xc4, 0xc1, 0x3a, 0x58, 0xf9,
        0xc4, 0x41, 0x2b, 0x58, 0xcb,
        0xc4, 0x41, 0x1c, 0x5c, 0xdd,
        0xc4, 0x41, 0x09, 0x5c, 0xef,
        0xc5, 0x7c, 0x59, 0xf9,
        0xc5, 0xe9, 0x59, 0xcb,
        0xc5, 0xdc, 0x5e, 0xdd,
        0xc5, 0xc9, 0x5e, 0xef,
    };
    String8 x86_intel_avx_source =
        S8("vmovaps ymm0, ymm1\n"
           "vmovups ymm8, [r12 + r9*2 + 32]\n"
           "vmovapd [r13], ymm15\n"
           "vmovupd xmm2, [rip + external]\n"
           "vxorps ymm10, ymm11, ymm12\n"
           "vxorpd xmm1, xmm2, xmm3\n"
           "vaddps ymm3, ymm4, ymm5\n"
           "vaddpd ymm6, ymm7, [rbp]\n"
           "vaddss xmm7, xmm8, xmm9\n"
           "vaddsd xmm9, xmm10, xmm11\n"
           "vsubps ymm11, ymm12, ymm13\n"
           "vsubpd xmm13, xmm14, xmm15\n"
           "vmulps ymm15, ymm0, ymm1\n"
           "vmulpd xmm1, xmm2, xmm3\n"
           "vdivps ymm3, ymm4, ymm5\n"
           "vdivpd xmm5, xmm6, xmm7\n");
    AssemblyEncodeResult x86_intel_avx = assembly_encode(
        arguments->arena, x86_intel_avx_source, (AssemblyEncodeOptions){.target = x86_avx_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, x86_intel_avx.diagnostic_count == 0 && x86_intel_avx.bytes.length == sizeof(expected_x86_avx) &&
                               memcmp(x86_intel_avx.bytes.pointer, expected_x86_avx, sizeof(expected_x86_avx)) == 0);
    BUSTER_TEST(arguments, x86_intel_avx.relocation_count == 1 && x86_intel_avx.relocations[0].offset == 21 &&
                               x86_intel_avx.relocations[0].addend == -4 &&
                               x86_intel_avx.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC32);
    String8 x86_att_avx_source =
        S8("vmovaps %ymm1, %ymm0\n"
           "vmovups 32(%r12,%r9,2), %ymm8\n"
           "vmovapd %ymm15, (%r13)\n"
           "vmovupd external(%rip), %xmm2\n"
           "vxorps %ymm12, %ymm11, %ymm10\n"
           "vxorpd %xmm3, %xmm2, %xmm1\n"
           "vaddps %ymm5, %ymm4, %ymm3\n"
           "vaddpd (%rbp), %ymm7, %ymm6\n"
           "vaddss %xmm9, %xmm8, %xmm7\n"
           "vaddsd %xmm11, %xmm10, %xmm9\n"
           "vsubps %ymm13, %ymm12, %ymm11\n"
           "vsubpd %xmm15, %xmm14, %xmm13\n"
           "vmulps %ymm1, %ymm0, %ymm15\n"
           "vmulpd %xmm3, %xmm2, %xmm1\n"
           "vdivps %ymm5, %ymm4, %ymm3\n"
           "vdivpd %xmm7, %xmm6, %xmm5\n");
    AssemblyEncodeResult x86_att_avx = assembly_encode(
        arguments->arena, x86_att_avx_source, (AssemblyEncodeOptions){.target = x86_avx_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, x86_att_avx.diagnostic_count == 0 && x86_att_avx.bytes.length == sizeof(expected_x86_avx) &&
                               memcmp(x86_att_avx.bytes.pointer, expected_x86_avx, sizeof(expected_x86_avx)) == 0);
    AssemblyEncodeResult unsupported_avx = assembly_encode(
        arguments->arena, S8("vaddps ymm0, ymm1, ymm2\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, unsupported_avx.diagnostic_count == 1 &&
                               unsupported_avx.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
    Target x86_avx2_target = x86_avx_target;
    x86_avx2_target.cpu_features |= TARGET_CPU_FEATURE_X86_AVX2;
    u8 expected_x86_avx2[] = {
        0xc5, 0xfd, 0x6f, 0xc1,
        0xc4, 0x01, 0x7e, 0x7f, 0x44, 0x4c, 0x20,
        0xc5, 0xe5, 0xfc, 0xd4,
        0xc5, 0xc9, 0xfd, 0xef,
        0xc4, 0x41, 0x35, 0xfe, 0x45, 0x00,
        0xc4, 0x41, 0x25, 0xd4, 0xd4,
        0xc4, 0x41, 0x0d, 0xf8, 0xef,
        0xc5, 0xf1, 0xf9, 0xc2,
        0xc5, 0xdd, 0xfa, 0xdd,
        0xc4, 0xc1, 0x41, 0xfb, 0xf0,
        0xc4, 0x41, 0x2d, 0xdb, 0xcb,
        0xc4, 0x41, 0x11, 0xeb, 0xe6,
        0xc5, 0x7d, 0xef, 0xf9,
        0xc5, 0xe1, 0x74, 0xd4,
        0xc5, 0xcd, 0x75, 0xef,
        0xc4, 0x41, 0x31, 0x76, 0xc2,
        0xc4, 0x42, 0x1d, 0x29, 0xdd,
        0xc5, 0x01, 0x64, 0xf0,
        0xc5, 0xed, 0x65, 0xcb,
        0xc5, 0xd1, 0x66, 0xe6,
        0xc4, 0xc2, 0x3d, 0x37, 0xf9,
        0xc4, 0x41, 0x21, 0xd5, 0xd4,
        0xc4, 0x42, 0x0d, 0x40, 0xef,
    };
    String8 x86_intel_avx2_source =
        S8("vmovdqa ymm0, ymm1\n"
           "vmovdqu [r12 + r9*2 + 32], ymm8\n"
           "vpaddb ymm2, ymm3, ymm4\n"
           "vpaddw xmm5, xmm6, xmm7\n"
           "vpaddd ymm8, ymm9, [r13]\n"
           "vpaddq ymm10, ymm11, ymm12\n"
           "vpsubb ymm13, ymm14, ymm15\n"
           "vpsubw xmm0, xmm1, xmm2\n"
           "vpsubd ymm3, ymm4, ymm5\n"
           "vpsubq xmm6, xmm7, xmm8\n"
           "vpand ymm9, ymm10, ymm11\n"
           "vpor xmm12, xmm13, xmm14\n"
           "vpxor ymm15, ymm0, ymm1\n"
           "vpcmpeqb xmm2, xmm3, xmm4\n"
           "vpcmpeqw ymm5, ymm6, ymm7\n"
           "vpcmpeqd xmm8, xmm9, xmm10\n"
           "vpcmpeqq ymm11, ymm12, ymm13\n"
           "vpcmpgtb xmm14, xmm15, xmm0\n"
           "vpcmpgtw ymm1, ymm2, ymm3\n"
           "vpcmpgtd xmm4, xmm5, xmm6\n"
           "vpcmpgtq ymm7, ymm8, ymm9\n"
           "vpmullw xmm10, xmm11, xmm12\n"
           "vpmulld ymm13, ymm14, ymm15\n");
    AssemblyEncodeResult x86_intel_avx2 = assembly_encode(
        arguments->arena, x86_intel_avx2_source, (AssemblyEncodeOptions){.target = x86_avx2_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, x86_intel_avx2.diagnostic_count == 0 && x86_intel_avx2.bytes.length == sizeof(expected_x86_avx2) &&
                               memcmp(x86_intel_avx2.bytes.pointer, expected_x86_avx2, sizeof(expected_x86_avx2)) == 0);
    String8 x86_att_avx2_source =
        S8("vmovdqa %ymm1, %ymm0\n"
           "vmovdqu %ymm8, 32(%r12,%r9,2)\n"
           "vpaddb %ymm4, %ymm3, %ymm2\n"
           "vpaddw %xmm7, %xmm6, %xmm5\n"
           "vpaddd (%r13), %ymm9, %ymm8\n"
           "vpaddq %ymm12, %ymm11, %ymm10\n"
           "vpsubb %ymm15, %ymm14, %ymm13\n"
           "vpsubw %xmm2, %xmm1, %xmm0\n"
           "vpsubd %ymm5, %ymm4, %ymm3\n"
           "vpsubq %xmm8, %xmm7, %xmm6\n"
           "vpand %ymm11, %ymm10, %ymm9\n"
           "vpor %xmm14, %xmm13, %xmm12\n"
           "vpxor %ymm1, %ymm0, %ymm15\n"
           "vpcmpeqb %xmm4, %xmm3, %xmm2\n"
           "vpcmpeqw %ymm7, %ymm6, %ymm5\n"
           "vpcmpeqd %xmm10, %xmm9, %xmm8\n"
           "vpcmpeqq %ymm13, %ymm12, %ymm11\n"
           "vpcmpgtb %xmm0, %xmm15, %xmm14\n"
           "vpcmpgtw %ymm3, %ymm2, %ymm1\n"
           "vpcmpgtd %xmm6, %xmm5, %xmm4\n"
           "vpcmpgtq %ymm9, %ymm8, %ymm7\n"
           "vpmullw %xmm12, %xmm11, %xmm10\n"
           "vpmulld %ymm15, %ymm14, %ymm13\n");
    AssemblyEncodeResult x86_att_avx2 = assembly_encode(
        arguments->arena, x86_att_avx2_source, (AssemblyEncodeOptions){.target = x86_avx2_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, x86_att_avx2.diagnostic_count == 0 && x86_att_avx2.bytes.length == sizeof(expected_x86_avx2) &&
                               memcmp(x86_att_avx2.bytes.pointer, expected_x86_avx2, sizeof(expected_x86_avx2)) == 0);
    AssemblyEncodeResult x86_avx_integer_128 = assembly_encode(
        arguments->arena, S8("vpaddd xmm0, xmm1, xmm2\n"),
        (AssemblyEncodeOptions){.target = x86_avx_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_x86_avx_integer_128[] = {0xc5, 0xf1, 0xfe, 0xc2};
    BUSTER_TEST(arguments, x86_avx_integer_128.diagnostic_count == 0 &&
                               x86_avx_integer_128.bytes.length == sizeof(expected_x86_avx_integer_128) &&
                               memcmp(x86_avx_integer_128.bytes.pointer, expected_x86_avx_integer_128,
                                      sizeof(expected_x86_avx_integer_128)) == 0);
    AssemblyEncodeResult unsupported_avx2 = assembly_encode(
        arguments->arena, S8("vpaddd ymm0, ymm1, ymm2\n"),
        (AssemblyEncodeOptions){.target = x86_avx_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, unsupported_avx2.diagnostic_count == 1 &&
                               unsupported_avx2.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);

    Target x86_bit_atomic_target = x86_target;
    x86_bit_atomic_target.cpu_model = CPU_MODEL_BASELINE;
    x86_bit_atomic_target.cpu_features_explicit = true;
    x86_bit_atomic_target.cpu_features = TARGET_CPU_FEATURE_X86_SSE2 | TARGET_CPU_FEATURE_X86_POPCNT |
                                         TARGET_CPU_FEATURE_X86_LZCNT | TARGET_CPU_FEATURE_X86_BMI1 | TARGET_CPU_FEATURE_X86_CX16;
    u8 expected_x86_bit_atomic[] = {
        0x66, 0x0f, 0xbc, 0xc1,
        0x0f, 0xbd, 0xc2,
        0x4f, 0x0f, 0xbc, 0x44, 0x8c, 0x10,
        0x0f, 0xc8,
        0x49, 0x0f, 0xc8,
        0x66, 0x0f, 0xa3, 0xc8,
        0x41, 0x0f, 0xbb, 0x54, 0x24, 0x08,
        0x4d, 0x0f, 0xb3, 0xc8,
        0x48, 0x0f, 0xba, 0x2d, 0x00, 0x00, 0x00, 0x00, 0x3f,
        0x86, 0xc3,
        0x66, 0x41, 0x90,
        0x93,
        0x4d, 0x87, 0xc1,
        0x4f, 0x87, 0x44, 0x4c, 0x20,
        0x45, 0x0f, 0xc0, 0x4d, 0x00,
        0x66, 0x45, 0x0f, 0xc1, 0xda,
        0x0f, 0xb1, 0xc8,
        0x4c, 0x0f, 0xb1, 0x05, 0x00, 0x00, 0x00, 0x00,
        0x41, 0x0f, 0xc7, 0x4d, 0x00,
        0x49, 0x0f, 0xc7, 0x0e,
        0xf3, 0x4d, 0x0f, 0xb8, 0x04, 0x24,
        0xf3, 0x0f, 0xbd, 0xca,
        0xf3, 0x48, 0x0f, 0xbc, 0xc3,
    };
    String8 x86_intel_bit_atomic_source =
        S8("bsf ax, cx\n"
           "bsr eax, edx\n"
           "bsf r8, qword ptr [r12 + r9*4 + 16]\n"
           "bswap eax\n"
           "bswap r8\n"
           "bt ax, cx\n"
           "btc dword ptr [r12 + 8], edx\n"
           "btr r8, r9\n"
           "bts qword ptr [rip + external], 63\n"
           "xchg al, bl\n"
           "xchg ax, r8w\n"
           "xchg eax, ebx\n"
           "xchg r8, r9\n"
           "xchg qword ptr [r12 + r9*2 + 32], r8\n"
           "xadd byte ptr [r13], r9b\n"
           "xadd r10w, r11w\n"
           "cmpxchg eax, ecx\n"
           "cmpxchg qword ptr [rip + external2], r8\n"
           "cmpxchg8b qword ptr [r13]\n"
           "cmpxchg16b xmmword ptr [r14]\n"
           "popcnt r8, qword ptr [r12]\n"
           "lzcnt ecx, edx\n"
           "tzcnt rax, rbx\n");
    AssemblyEncodeResult x86_intel_bit_atomic = assembly_encode(
        arguments->arena, x86_intel_bit_atomic_source,
        (AssemblyEncodeOptions){.target = x86_bit_atomic_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, x86_intel_bit_atomic.diagnostic_count == 0 &&
                               x86_intel_bit_atomic.bytes.length == sizeof(expected_x86_bit_atomic) &&
                               memcmp(x86_intel_bit_atomic.bytes.pointer, expected_x86_bit_atomic,
                                      sizeof(expected_x86_bit_atomic)) == 0);
    BUSTER_TEST(arguments, x86_intel_bit_atomic.relocation_count == 2 && x86_intel_bit_atomic.relocations[0].offset == 36 &&
                               x86_intel_bit_atomic.relocations[1].offset == 72 && x86_intel_bit_atomic.relocations[0].addend == -4 &&
                               x86_intel_bit_atomic.relocations[1].addend == -4 &&
                               x86_intel_bit_atomic.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC32 &&
                               x86_intel_bit_atomic.relocations[1].kind == ASSEMBLY_RELOCATION_X86_PC32 &&
                               string_equal(x86_intel_bit_atomic.symbols[x86_intel_bit_atomic.relocations[0].symbol].name,
                                            S8("external")) &&
                               string_equal(x86_intel_bit_atomic.symbols[x86_intel_bit_atomic.relocations[1].symbol].name,
                                            S8("external2")));
    String8 x86_att_bit_atomic_source =
        S8("bsfw %cx, %ax\n"
           "bsrl %edx, %eax\n"
           "bsfq 16(%r12,%r9,4), %r8\n"
           "bswapl %eax\n"
           "bswapq %r8\n"
           "btw %cx, %ax\n"
           "btcl %edx, 8(%r12)\n"
           "btrq %r9, %r8\n"
           "btsq $63, external(%rip)\n"
           "xchgb %bl, %al\n"
           "xchgw %r8w, %ax\n"
           "xchgl %ebx, %eax\n"
           "xchgq %r9, %r8\n"
           "xchgq %r8, 32(%r12,%r9,2)\n"
           "xaddb %r9b, (%r13)\n"
           "xaddw %r11w, %r10w\n"
           "cmpxchgl %ecx, %eax\n"
           "cmpxchgq %r8, external2(%rip)\n"
           "cmpxchg8b (%r13)\n"
           "cmpxchg16b (%r14)\n"
           "popcntq (%r12), %r8\n"
           "lzcntl %edx, %ecx\n"
           "tzcntq %rbx, %rax\n");
    AssemblyEncodeResult x86_att_bit_atomic = assembly_encode(
        arguments->arena, x86_att_bit_atomic_source,
        (AssemblyEncodeOptions){.target = x86_bit_atomic_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, x86_att_bit_atomic.diagnostic_count == 0 && x86_att_bit_atomic.bytes.length == sizeof(expected_x86_bit_atomic) &&
                               memcmp(x86_att_bit_atomic.bytes.pointer, expected_x86_bit_atomic,
                                      sizeof(expected_x86_bit_atomic)) == 0);
    BUSTER_TEST(arguments, x86_att_bit_atomic.relocation_count == 2 && x86_att_bit_atomic.relocations[0].offset == 36 &&
                               x86_att_bit_atomic.relocations[1].offset == 72);

    u8 expected_x86_locked_bit_atomic[] = {
        0xf0, 0x01, 0x08,
        0xf0, 0x0f, 0xbb, 0x08,
        0xf0, 0x0f, 0xba, 0x30, 0x03,
        0xf0, 0x48, 0x0f, 0xba, 0x2d, 0x00, 0x00, 0x00, 0x00, 0x07,
        0xf0, 0x41, 0x87, 0x08,
        0xf0, 0x41, 0x0f, 0xc1, 0x08,
        0xf0, 0x41, 0x0f, 0xb1, 0x08,
        0xf0, 0x41, 0x0f, 0xc7, 0x08,
    };
    String8 x86_intel_locked_bit_atomic_source =
        S8("lock add dword ptr [rax], ecx\n"
           "lock btc dword ptr [rax], ecx\n"
           "lock btr dword ptr [rax], 3\n"
           "lock bts qword ptr [rip + lock_external], 7\n"
           "lock xchg dword ptr [r8], ecx\n"
           "lock xadd dword ptr [r8], ecx\n"
           "lock cmpxchg dword ptr [r8], ecx\n"
           "lock cmpxchg8b qword ptr [r8]\n");
    AssemblyEncodeResult x86_intel_locked_bit_atomic = assembly_encode(
        arguments->arena, x86_intel_locked_bit_atomic_source,
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, x86_intel_locked_bit_atomic.diagnostic_count == 0 &&
                               x86_intel_locked_bit_atomic.bytes.length == sizeof(expected_x86_locked_bit_atomic) &&
                               memcmp(x86_intel_locked_bit_atomic.bytes.pointer, expected_x86_locked_bit_atomic,
                                      sizeof(expected_x86_locked_bit_atomic)) == 0);
    BUSTER_TEST(arguments, x86_intel_locked_bit_atomic.relocation_count == 1 &&
                               x86_intel_locked_bit_atomic.relocations[0].offset == 17 &&
                               x86_intel_locked_bit_atomic.relocations[0].addend == -4);
    AssemblyEncodeResult x86_att_locked_bit_atomic = assembly_encode(
        arguments->arena,
        S8("lock addl %ecx, (%rax)\n"
           "lock btcl %ecx, (%rax)\n"
           "lock btrl $3, (%rax)\n"
           "lock btsq $7, lock_external(%rip)\n"
           "lock xchgl %ecx, (%r8)\n"
           "lock xaddl %ecx, (%r8)\n"
           "lock cmpxchgl %ecx, (%r8)\n"
           "lock cmpxchg8b (%r8)\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, x86_att_locked_bit_atomic.diagnostic_count == 0 &&
                               x86_att_locked_bit_atomic.bytes.length == sizeof(expected_x86_locked_bit_atomic) &&
                               memcmp(x86_att_locked_bit_atomic.bytes.pointer, expected_x86_locked_bit_atomic,
                                      sizeof(expected_x86_locked_bit_atomic)) == 0);

    u8 expected_x86_high_byte_atomic[] = {
        0x86, 0xe0,
        0x0f, 0xc0, 0xc4,
        0x0f, 0xb0, 0xc4,
    };
    AssemblyEncodeResult x86_high_byte_atomic = assembly_encode(
        arguments->arena, S8("xchg ah, al\nxadd ah, al\ncmpxchg ah, al\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, x86_high_byte_atomic.diagnostic_count == 0 &&
                               x86_high_byte_atomic.bytes.length == sizeof(expected_x86_high_byte_atomic) &&
                               memcmp(x86_high_byte_atomic.bytes.pointer, expected_x86_high_byte_atomic,
                                      sizeof(expected_x86_high_byte_atomic)) == 0);
    AssemblyEncodeResult invalid_x86_high_byte_atomic = assembly_encode(
        arguments->arena,
        S8("xchg ah, r8b\n"
           "xadd ah, r8b\n"
           "cmpxchg ah, r8b\n"
           "mov byte ptr [r8], ah\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_x86_high_byte_atomic.diagnostic_count == 4);

    AssemblyEncodeResult unsupported_x86_bit_atomic = assembly_encode(
        arguments->arena,
        S8("popcnt eax, ebx\n"
           "lzcnt eax, ebx\n"
           "tzcnt eax, ebx\n"
           "cmpxchg16b xmmword ptr [rax]\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, unsupported_x86_bit_atomic.diagnostic_count == 4);
    for (u32 diagnostic_index = 0; diagnostic_index < unsupported_x86_bit_atomic.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, unsupported_x86_bit_atomic.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
    }
    AssemblyEncodeResult invalid_x86_bit_atomic = assembly_encode(
        arguments->arena,
        S8("bsf al, bl\n"
           "bswap ax\n"
           "bt qword ptr [rax], 256\n"
           "xadd dword ptr [rax], dword ptr [rbx]\n"
           "cmpxchg8b rax\n"
           "lock bt dword ptr [rax], ecx\n"
           "lock xadd eax, ecx\n"
           "lock mov dword ptr [rax], ecx\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_x86_bit_atomic.diagnostic_count == 8);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_x86_bit_atomic.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, invalid_x86_bit_atomic.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }
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
    AssemblyEncodeResult invalid_x86_sse2 =
        assembly_encode(arguments->arena, S8("mov rax, xmm0\naddps xmm0, rax\nmovaps [rax], [rbx]\naddps [rax], xmm0\n"),
                        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_x86_sse2.diagnostic_count == 4);
    AssemblyEncodeResult invalid_x86_conditions =
        assembly_encode(arguments->arena, S8("seteb %al\nsete %rax\ncmove %al, %bl\ncmoveq (%rax), (%rbx)\nje %rax\n"),
                        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, invalid_x86_conditions.diagnostic_count == 5);
    AssemblyEncodeResult invalid_x86_avx =
        assembly_encode(arguments->arena,
                        S8("vaddps ymm0, xmm1, ymm2\nvaddss ymm0, ymm1, ymm2\nvmovaps [rax], [rbx]\nvaddps rax, ymm1, ymm2\n"),
                        (AssemblyEncodeOptions){.target = x86_avx_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_x86_avx.diagnostic_count == 4);

    u8 expected_x86_lea[] = {
        0x66, 0x8d, 0x44, 0x8b, 0x10,
        0x43, 0x8d, 0x44, 0xc8, 0xe0,
        0x4e, 0x8d, 0x7c, 0x64, 0x7f,
        0x44, 0x8d, 0x45, 0x00,
        0x4d, 0x8d, 0x8c, 0x24, 0x78, 0x56, 0x34, 0x12,
    };
    String8 x86_intel_lea_source =
        S8("lea ax, [rbx + rcx*4 + 16]\n"
           "lea eax, [r8 + r9*8 - 32]\n"
           "lea r15, [rsp + r12*2 + 127]\n"
           "lea r8d, [rbp]\n"
           "lea r9, [r12 + 0x12345678]\n");
    AssemblyEncodeResult x86_intel_lea = assembly_encode(
        arguments->arena, x86_intel_lea_source, (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, x86_intel_lea.diagnostic_count == 0 && x86_intel_lea.bytes.length == sizeof(expected_x86_lea) &&
                               memcmp(x86_intel_lea.bytes.pointer, expected_x86_lea, sizeof(expected_x86_lea)) == 0);
    String8 x86_att_lea_source =
        S8("leaw 16(%rbx,%rcx,4), %ax\n"
           "leal -32(%r8,%r9,8), %eax\n"
           "leaq 127(%rsp,%r12,2), %r15\n"
           "leal (%rbp), %r8d\n"
           "leaq 0x12345678(%r12), %r9\n");
    AssemblyEncodeResult x86_att_lea =
        assembly_encode(arguments->arena, x86_att_lea_source, (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, x86_att_lea.diagnostic_count == 0 && x86_att_lea.bytes.length == sizeof(expected_x86_lea) &&
                               memcmp(x86_att_lea.bytes.pointer, expected_x86_lea, sizeof(expected_x86_lea)) == 0);
    u8 expected_x86_lea_rip[] = {0x48, 0x8d, 0x05, 0x00, 0x00, 0x00, 0x00};
    AssemblyEncodeResult x86_lea_rip = assembly_encode(
        arguments->arena, S8("lea rax, [rip + external]\n"), (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, x86_lea_rip.diagnostic_count == 0 && x86_lea_rip.bytes.length == sizeof(expected_x86_lea_rip) &&
                               memcmp(x86_lea_rip.bytes.pointer, expected_x86_lea_rip, sizeof(expected_x86_lea_rip)) == 0 &&
                               x86_lea_rip.relocation_count == 1 && x86_lea_rip.relocations[0].offset == 3 &&
                               x86_lea_rip.relocations[0].addend == -4 && x86_lea_rip.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC32);
    u8 expected_x86_lea_absolute[] = {0x4d, 0x8d, 0x8c, 0x24, 0x00, 0x00, 0x00, 0x00};
    AssemblyEncodeResult x86_lea_absolute = assembly_encode(
        arguments->arena, S8("lea r9, [r12 + external + 8]\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, x86_lea_absolute.diagnostic_count == 0 && x86_lea_absolute.bytes.length == sizeof(expected_x86_lea_absolute) &&
                               memcmp(x86_lea_absolute.bytes.pointer, expected_x86_lea_absolute, sizeof(expected_x86_lea_absolute)) == 0 &&
                               x86_lea_absolute.relocation_count == 1 && x86_lea_absolute.relocations[0].offset == 4 &&
                               x86_lea_absolute.relocations[0].addend == 8 && x86_lea_absolute.relocations[0].kind == ASSEMBLY_RELOCATION_X86_32);

    u8 expected_x86_scalar_extend[] = {
        0x66, 0x0f, 0xb6, 0xc0,
        0x0f, 0xb6, 0xc4,
        0x44, 0x0f, 0xb6, 0xc4,
        0x66, 0x40, 0x0f, 0xb6, 0xc4,
        0x4f, 0x0f, 0xb7, 0x4c, 0x94, 0x08,
        0x66, 0x0f, 0xbe, 0xc7,
        0x48, 0x0f, 0xbe, 0x06,
        0x4d, 0x63, 0x41, 0x10,
        0x48, 0x63, 0xc0,
    };
    String8 x86_intel_scalar_extend_source =
        S8("movzx ax, al\n"
           "movzx eax, ah\n"
           "movzx r8d, spl\n"
           "movzx ax, spl\n"
           "movzx r9, word ptr [r12 + r10*4 + 8]\n"
           "movsx ax, bh\n"
           "movsx rax, byte ptr [rsi]\n"
           "movsxd r8, dword ptr [r9 + 16]\n"
           "movsxd rax, eax\n");
    AssemblyEncodeResult x86_intel_scalar_extend = assembly_encode(
        arguments->arena, x86_intel_scalar_extend_source,
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, x86_intel_scalar_extend.diagnostic_count == 0 &&
                               x86_intel_scalar_extend.bytes.length == sizeof(expected_x86_scalar_extend) &&
                               memcmp(x86_intel_scalar_extend.bytes.pointer, expected_x86_scalar_extend,
                                      sizeof(expected_x86_scalar_extend)) == 0);
    u8 expected_x86_att_scalar_extend[] = {
        0x66, 0x0f, 0xb6, 0xc0,
        0x0f, 0xb6, 0xc4,
        0x48, 0x0f, 0xb6, 0xc4,
        0x66, 0x40, 0x0f, 0xb6, 0xc4,
        0x47, 0x0f, 0xb7, 0x4c, 0x94, 0x08,
        0x66, 0x0f, 0xbe, 0xc7,
        0x44, 0x0f, 0xbe, 0x06,
        0x4c, 0x0f, 0xbe, 0x0e,
        0x45, 0x0f, 0xbf, 0xda,
        0x4d, 0x0f, 0xbf, 0xec,
        0x4d, 0x63, 0xfe,
    };
    String8 x86_att_scalar_extend_source =
        S8("movzbw %al, %ax\n"
           "movzbl %ah, %eax\n"
           "movzbq %spl, %rax\n"
           "movzbw %spl, %ax\n"
           "movzwl 8(%r12,%r10,4), %r9d\n"
           "movsbw %bh, %ax\n"
           "movsbl (%rsi), %r8d\n"
           "movsbq (%rsi), %r9\n"
           "movswl %r10w, %r11d\n"
           "movswq %r12w, %r13\n"
           "movslq %r14d, %r15\n");
    AssemblyEncodeResult x86_att_scalar_extend = assembly_encode(
        arguments->arena, x86_att_scalar_extend_source,
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, x86_att_scalar_extend.diagnostic_count == 0 &&
                               x86_att_scalar_extend.bytes.length == sizeof(expected_x86_att_scalar_extend) &&
                               memcmp(x86_att_scalar_extend.bytes.pointer, expected_x86_att_scalar_extend,
                                      sizeof(expected_x86_att_scalar_extend)) == 0);
    u8 expected_x86_scalar_extend_rip[] = {0x48, 0x0f, 0xbe, 0x05, 0x00, 0x00, 0x00, 0x00};
    AssemblyEncodeResult x86_scalar_extend_rip = assembly_encode(
        arguments->arena, S8("movsx rax, byte ptr [rip + external]\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, x86_scalar_extend_rip.diagnostic_count == 0 &&
                               x86_scalar_extend_rip.bytes.length == sizeof(expected_x86_scalar_extend_rip) &&
                               memcmp(x86_scalar_extend_rip.bytes.pointer, expected_x86_scalar_extend_rip,
                                      sizeof(expected_x86_scalar_extend_rip)) == 0 &&
                               x86_scalar_extend_rip.relocation_count == 1 && x86_scalar_extend_rip.relocations[0].offset == 4 &&
                               x86_scalar_extend_rip.relocations[0].addend == -4 &&
                               x86_scalar_extend_rip.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC32);
    u8 expected_x86_scalar_extend_memory_widths[] = {
        0x0f, 0xb6, 0x06,
        0x4c, 0x0f, 0xb6, 0x06,
        0x0f, 0xbf, 0x06,
        0x4c, 0x0f, 0xbf, 0x0e,
        0x48, 0x63, 0x06,
    };
    AssemblyEncodeResult x86_intel_scalar_extend_memory_widths = assembly_encode(
        arguments->arena,
        S8("movzx eax, byte ptr [rsi]\n"
           "movzx r8, byte ptr [rsi]\n"
           "movsx eax, word ptr [rsi]\n"
           "movsx r9, word ptr [rsi]\n"
           "movsxd rax, dword ptr [rsi]\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, x86_intel_scalar_extend_memory_widths.diagnostic_count == 0 &&
                               x86_intel_scalar_extend_memory_widths.bytes.length == sizeof(expected_x86_scalar_extend_memory_widths) &&
                               memcmp(x86_intel_scalar_extend_memory_widths.bytes.pointer, expected_x86_scalar_extend_memory_widths,
                                      sizeof(expected_x86_scalar_extend_memory_widths)) == 0);
    AssemblyEncodeResult x86_att_scalar_extend_memory_widths = assembly_encode(
        arguments->arena,
        S8("movzbl (%rsi), %eax\n"
           "movzbq (%rsi), %r8\n"
           "movswl (%rsi), %eax\n"
           "movswq (%rsi), %r9\n"
           "movslq (%rsi), %rax\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, x86_att_scalar_extend_memory_widths.diagnostic_count == 0 &&
                               x86_att_scalar_extend_memory_widths.bytes.length == sizeof(expected_x86_scalar_extend_memory_widths) &&
                               memcmp(x86_att_scalar_extend_memory_widths.bytes.pointer, expected_x86_scalar_extend_memory_widths,
                                      sizeof(expected_x86_scalar_extend_memory_widths)) == 0);
    u8 expected_x86_high_byte_extend[] = {
        0x0f, 0xb6, 0xc4,
        0x0f, 0xb6, 0xcd,
        0x0f, 0xbe, 0xd6,
        0x0f, 0xb6, 0xdf,
    };
    AssemblyEncodeResult x86_high_byte_extend = assembly_encode(
        arguments->arena,
        S8("movzx eax, ah\nmovzx ecx, ch\nmovsx edx, dh\nmovzx ebx, bh\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, x86_high_byte_extend.diagnostic_count == 0 &&
                               x86_high_byte_extend.bytes.length == sizeof(expected_x86_high_byte_extend) &&
                               memcmp(x86_high_byte_extend.bytes.pointer, expected_x86_high_byte_extend,
                                      sizeof(expected_x86_high_byte_extend)) == 0);

    u8 expected_x86_rotate[] = {
        0xd0, 0xc0,
        0x66, 0xd3, 0xc8,
        0xc1, 0xd0, 0x7f,
        0x49, 0xc1, 0xd8, 0xff,
        0x43, 0xd0, 0x44, 0x51, 0x08,
        0x66, 0xd3, 0x0d, 0x00, 0x00, 0x00, 0x00,
        0xc1, 0x54, 0x24, 0x10, 0x07,
        0x49, 0xd3, 0x1c, 0x24,
        0xd0, 0xc4,
        0x41, 0xc0, 0xcf, 0xff,
    };
    String8 x86_intel_rotate_source =
        S8("rol al, 1\n"
           "ror ax, cl\n"
           "rcl eax, 0x7f\n"
           "rcr r8, 0xff\n"
           "rol byte ptr [r9 + r10*2 + 8], 1\n"
           "ror word ptr [rip + rotate_external], cl\n"
           "rcl dword ptr [rsp + 16], 7\n"
           "rcr qword ptr [r12], cl\n"
           "rol ah, 1\n"
           "ror r15b, -1\n");
    AssemblyEncodeResult x86_intel_rotate = assembly_encode(
        arguments->arena, x86_intel_rotate_source,
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, x86_intel_rotate.diagnostic_count == 0 && x86_intel_rotate.bytes.length == sizeof(expected_x86_rotate) &&
                               memcmp(x86_intel_rotate.bytes.pointer, expected_x86_rotate, sizeof(expected_x86_rotate)) == 0 &&
                               x86_intel_rotate.relocation_count == 1 && x86_intel_rotate.relocations[0].offset == 20 &&
                               x86_intel_rotate.relocations[0].addend == -4 &&
                               x86_intel_rotate.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC32);
    String8 x86_att_rotate_source =
        S8("rolb $1, %al\n"
           "rorw %cl, %ax\n"
           "rcll $0x7f, %eax\n"
           "rcrq $-1, %r8\n"
           "rolb $1, 8(%r9,%r10,2)\n"
           "rorw %cl, rotate_external(%rip)\n"
           "rcll $7, 16(%rsp)\n"
           "rcrq %cl, (%r12)\n"
           "rolb $1, %ah\n"
           "rorb $-1, %r15b\n");
    AssemblyEncodeResult x86_att_rotate = assembly_encode(
        arguments->arena, x86_att_rotate_source, (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, x86_att_rotate.diagnostic_count == 0 && x86_att_rotate.bytes.length == sizeof(expected_x86_rotate) &&
                               memcmp(x86_att_rotate.bytes.pointer, expected_x86_rotate, sizeof(expected_x86_rotate)) == 0 &&
                               x86_att_rotate.relocation_count == 1 && x86_att_rotate.relocations[0].offset == 20 &&
                               x86_att_rotate.relocations[0].addend == -4 &&
                               x86_att_rotate.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC32);

    u8 expected_x86_double_shift[] = {
        0x66, 0x0f, 0xa5, 0xd8,
        0x44, 0x0f, 0xa4, 0xc0, 0x07,
        0x4d, 0x0f, 0xa4, 0xd1, 0xff,
        0x66, 0x47, 0x0f, 0xad, 0x74, 0xac, 0x08,
        0x0f, 0xac, 0x6c, 0x24, 0x10, 0x01,
        0x45, 0x0f, 0xad, 0xda,
    };
    String8 x86_intel_double_shift_source =
        S8("shld ax, bx, cl\n"
           "shld eax, r8d, 7\n"
           "shld r9, r10, 255\n"
           "shrd word ptr [r12 + r13*4 + 8], r14w, cl\n"
           "shrd dword ptr [rsp + 16], ebp, 1\n"
           "shrd r10d, r11d, cl\n");
    AssemblyEncodeResult x86_intel_double_shift = assembly_encode(
        arguments->arena, x86_intel_double_shift_source,
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, x86_intel_double_shift.diagnostic_count == 0 &&
                               x86_intel_double_shift.bytes.length == sizeof(expected_x86_double_shift) &&
                               memcmp(x86_intel_double_shift.bytes.pointer, expected_x86_double_shift,
                                      sizeof(expected_x86_double_shift)) == 0);
    String8 x86_att_double_shift_source =
        S8("shldw %cl, %bx, %ax\n"
           "shldl $7, %r8d, %eax\n"
           "shldq $255, %r10, %r9\n"
           "shrdw %cl, %r14w, 8(%r12,%r13,4)\n"
           "shrdl $1, %ebp, 16(%rsp)\n"
           "shrdl %cl, %r11d, %r10d\n");
    AssemblyEncodeResult x86_att_double_shift = assembly_encode(
        arguments->arena, x86_att_double_shift_source, (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, x86_att_double_shift.diagnostic_count == 0 &&
                               x86_att_double_shift.bytes.length == sizeof(expected_x86_double_shift) &&
                               memcmp(x86_att_double_shift.bytes.pointer, expected_x86_double_shift,
                                      sizeof(expected_x86_double_shift)) == 0);
    u8 expected_x86_double_shift_rip[] = {0x4c, 0x0f, 0xa4, 0x05, 0x00, 0x00, 0x00, 0x00, 0x80};
    AssemblyEncodeResult x86_double_shift_rip = assembly_encode(
        arguments->arena, S8("shld qword ptr [rip + shift_external], r8, 0x80\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, x86_double_shift_rip.diagnostic_count == 0 &&
                               x86_double_shift_rip.bytes.length == sizeof(expected_x86_double_shift_rip) &&
                               memcmp(x86_double_shift_rip.bytes.pointer, expected_x86_double_shift_rip,
                                      sizeof(expected_x86_double_shift_rip)) == 0 &&
                               x86_double_shift_rip.relocation_count == 1 && x86_double_shift_rip.relocations[0].offset == 4 &&
                               x86_double_shift_rip.relocations[0].addend == -5 &&
                               x86_double_shift_rip.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC32);
    u8 expected_x86_double_shift_rip_shrd[] = {0x4c, 0x0f, 0xac, 0x05, 0x00, 0x00, 0x00, 0x00, 0x80};
    AssemblyEncodeResult x86_double_shift_rip_shrd = assembly_encode(
        arguments->arena, S8("shrd qword ptr [rip + shift_external], r8, 0x80\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, x86_double_shift_rip_shrd.diagnostic_count == 0 &&
                               x86_double_shift_rip_shrd.bytes.length == sizeof(expected_x86_double_shift_rip_shrd) &&
                               memcmp(x86_double_shift_rip_shrd.bytes.pointer, expected_x86_double_shift_rip_shrd,
                                      sizeof(expected_x86_double_shift_rip_shrd)) == 0 &&
                               x86_double_shift_rip_shrd.relocation_count == 1 &&
                               x86_double_shift_rip_shrd.relocations[0].offset == 4 &&
                               x86_double_shift_rip_shrd.relocations[0].addend == -5 &&
                               x86_double_shift_rip_shrd.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC32);

    AssemblyEncodeResult invalid_x86_scalar_integer_family = assembly_encode(
        arguments->arena,
        S8("lea r8b, [rax]\n"
           "lea eax, rbx\n"
           "movzx al, byte ptr [rax]\n"
           "movzx eax, [rax]\n"
           "movsx eax, dword ptr [rax]\n"
           "movsxd eax, dword ptr [rax]\n"
           "movzx r8d, ah\n"
           "movzx r9d, ch\n"
           "movsx r10d, dh\n"
           "movsx rax, bh\n"
           "movzx ax, ax\n"
           "movsx ax, word ptr [rax]\n"
           "movsx al, byte ptr [rax]\n"
           "movzx eax, dword ptr [rax]\n"
           "movsx eax, ebx\n"
           "movsxd rax, ax\n"
           "movsxd r8d, eax\n"
           "rol eax, edx\n"
           "ror [rax], 1\n"
           "rcl eax, external\n"
           "shld eax, ebx, dl\n"
           "shrd eax, [rbx], cl\n"
           "shld eax, ebx, 256\n"
           "shrd eax, ebx, -129\n"
           "lock lea rax, [rbx]\n"
           "lock movzx eax, byte ptr [rbx]\n"
           "lock rol eax, 1\n"
           "lock shld eax, ebx, cl\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_x86_scalar_integer_family.diagnostic_count == 28);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_x86_scalar_integer_family.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, invalid_x86_scalar_integer_family.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }

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
    Target advanced_target = x86_target;
    advanced_target.cpu_features_explicit = true;
    advanced_target.cpu_features = TARGET_CPU_FEATURE_X86_SSE2 | TARGET_CPU_FEATURE_X86_AVX |
                                   TARGET_CPU_FEATURE_X86_AVX512F | TARGET_CPU_FEATURE_X86_AVX512VL |
                                   TARGET_CPU_FEATURE_X86_AVX512BW | TARGET_CPU_FEATURE_X86_AVX512DQ |
                                   TARGET_CPU_FEATURE_X86_APX | TARGET_CPU_FEATURE_X86_AMX_TILE |
                                   TARGET_CPU_FEATURE_X86_AMX_BF16 | TARGET_CPU_FEATURE_X86_AMX_INT8;
    AssemblyEncodeResult advanced_evex = assembly_encode(
        arguments->arena,
        S8("vaddps zmm0 {k1}{z}, zmm2, zmm3\n"
           "vaddps zmm0 {k1}, zmm1, dword ptr [rax]{1to16}\n"
           "vcmpps k1, zmm2, zmm3, 7\n"
           "vmovdqa64 zmm31 {k7}{z}, zmmword ptr [r15+64]\n"
           "vpcmpq k7, zmm30, zmm31, 7\n"
           "kmovw k1, k2\n"
           "kmovd k1, k2\n"
           "kmovq k1, k2\n"
           "kaddw k1, k2, k3\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_advanced_evex[] = {
        0x62, 0xf1, 0x6c, 0xc9, 0x58, 0xc3,
        0x62, 0xf1, 0x74, 0x59, 0x58, 0x00,
        0x62, 0xf1, 0x6c, 0x48, 0xc2, 0xcb, 0x07,
        0x62, 0x41, 0xfd, 0xcf, 0x6f, 0x7f, 0x01,
        0x62, 0x93, 0x8d, 0x40, 0x1f, 0xff, 0x07,
        0xc5, 0xf8, 0x90, 0xca,
        0xc4, 0xe1, 0xf9, 0x90, 0xca,
        0xc4, 0xe1, 0xf8, 0x90, 0xca,
        0xc5, 0xec, 0x4a, 0xcb,
    };
    BUSTER_TEST(arguments, advanced_evex.diagnostic_count == 0 &&
                               advanced_evex.bytes.length == sizeof(expected_advanced_evex) &&
                               memcmp(advanced_evex.bytes.pointer, expected_advanced_evex, sizeof(expected_advanced_evex)) == 0);

    AssemblyEncodeResult advanced_vmovdqu = assembly_encode(
        arguments->arena,
        S8("vmovdqu8 zmm1, zmm2\n"
           "vmovdqu16 zmm1, zmm2\n"
           "vmovdqu16 zmmword ptr [rax], zmm1\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_advanced_vmovdqu[] = {
        0x62, 0xf1, 0x7f, 0x48, 0x6f, 0xca,
        0x62, 0xf1, 0xff, 0x48, 0x6f, 0xca,
        0x62, 0xf1, 0xff, 0x48, 0x7f, 0x08,
    };
    BUSTER_TEST(arguments, advanced_vmovdqu.diagnostic_count == 0 &&
                               advanced_vmovdqu.bytes.length == sizeof(expected_advanced_vmovdqu) &&
                               memcmp(advanced_vmovdqu.bytes.pointer, expected_advanced_vmovdqu,
                                      sizeof(expected_advanced_vmovdqu)) == 0);

    AssemblyEncodeResult advanced_opmask_binary = assembly_encode(
        arguments->arena, S8("kandw k1, k2, k3\nkorw k1, k2, k3\nkxorw k1, k2, k3\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_advanced_opmask_binary[] = {
        0xc5, 0xec, 0x41, 0xcb,
        0xc5, 0xec, 0x45, 0xcb,
        0xc5, 0xec, 0x47, 0xcb,
    };
    BUSTER_TEST(arguments, advanced_opmask_binary.diagnostic_count == 0 &&
                               advanced_opmask_binary.bytes.length == sizeof(expected_advanced_opmask_binary) &&
                               memcmp(advanced_opmask_binary.bytes.pointer, expected_advanced_opmask_binary,
                                      sizeof(expected_advanced_opmask_binary)) == 0);

    AssemblyEncodeResult advanced_evex_memory = assembly_encode(
        arguments->arena, S8("vaddps zmm0, zmm1, zmmword ptr [r15+r14*4+64]\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_advanced_evex_memory[] = {0x62, 0x91, 0x74, 0x48, 0x58, 0x44, 0xb7, 0x01};
    BUSTER_TEST(arguments, advanced_evex_memory.diagnostic_count == 0 &&
                               advanced_evex_memory.bytes.length == sizeof(expected_advanced_evex_memory) &&
                               memcmp(advanced_evex_memory.bytes.pointer, expected_advanced_evex_memory,
                                      sizeof(expected_advanced_evex_memory)) == 0);

    AssemblyEncodeResult advanced_evex_egpr_base = assembly_encode(
        arguments->arena, S8("vaddps zmm0, zmm1, zmmword ptr [r16]\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_advanced_evex_egpr_base[] = {0x62, 0xf9, 0x74, 0x48, 0x58, 0x00};
    BUSTER_TEST(arguments, advanced_evex_egpr_base.diagnostic_count == 0 &&
                               advanced_evex_egpr_base.bytes.length == sizeof(expected_advanced_evex_egpr_base) &&
                               memcmp(advanced_evex_egpr_base.bytes.pointer, expected_advanced_evex_egpr_base,
                                      sizeof(expected_advanced_evex_egpr_base)) == 0);

    AssemblyEncodeResult advanced_evex_egpr_sib = assembly_encode(
        arguments->arena, S8("vaddps zmm0, zmm1, zmmword ptr [r24+r25*4+64]\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_advanced_evex_egpr_sib[] = {0x62, 0x99, 0x70, 0x48, 0x58, 0x44, 0x88, 0x01};
    BUSTER_TEST(arguments, advanced_evex_egpr_sib.diagnostic_count == 0 &&
                               advanced_evex_egpr_sib.bytes.length == sizeof(expected_advanced_evex_egpr_sib) &&
                               memcmp(advanced_evex_egpr_sib.bytes.pointer, expected_advanced_evex_egpr_sib,
                                      sizeof(expected_advanced_evex_egpr_sib)) == 0);

    AssemblyEncodeResult advanced_evex_egpr_index = assembly_encode(
        arguments->arena, S8("vaddps zmm0, zmm1, zmmword ptr [r25*4+64]\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_advanced_evex_egpr_index[] = {
        0x62, 0xb1, 0x70, 0x48, 0x58, 0x04, 0x8d, 0x40, 0x00, 0x00, 0x00,
    };
    BUSTER_TEST(arguments, advanced_evex_egpr_index.diagnostic_count == 0 &&
                               advanced_evex_egpr_index.bytes.length == sizeof(expected_advanced_evex_egpr_index) &&
                               memcmp(advanced_evex_egpr_index.bytes.pointer, expected_advanced_evex_egpr_index,
                                      sizeof(expected_advanced_evex_egpr_index)) == 0);

    AssemblyEncodeResult advanced_evex_rip = assembly_encode(
        arguments->arena, S8("vaddps zmm0, zmm1, zmmword ptr [rip+external]\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_advanced_evex_rip[] = {0x62, 0xf1, 0x74, 0x48, 0x58, 0x05, 0x00, 0x00, 0x00, 0x00};
    BUSTER_TEST(arguments, advanced_evex_rip.diagnostic_count == 0 &&
                               advanced_evex_rip.bytes.length == sizeof(expected_advanced_evex_rip) &&
                               memcmp(advanced_evex_rip.bytes.pointer, expected_advanced_evex_rip,
                                      sizeof(expected_advanced_evex_rip)) == 0 &&
                               advanced_evex_rip.relocation_count == 1 && advanced_evex_rip.relocations[0].offset == 6 &&
                               advanced_evex_rip.relocations[0].addend == -4 &&
                               advanced_evex_rip.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC32);

    AssemblyEncodeResult advanced_evex_cmp_rip = assembly_encode(
        arguments->arena, S8("vcmpps k1, zmm2, zmmword ptr [rip+cmp_external], 7\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_advanced_evex_cmp_rip[] = {0x62, 0xf1, 0x6c, 0x48, 0xc2, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x07};
    BUSTER_TEST(arguments, advanced_evex_cmp_rip.diagnostic_count == 0 &&
                               advanced_evex_cmp_rip.bytes.length == sizeof(expected_advanced_evex_cmp_rip) &&
                               memcmp(advanced_evex_cmp_rip.bytes.pointer, expected_advanced_evex_cmp_rip,
                                      sizeof(expected_advanced_evex_cmp_rip)) == 0 &&
                               advanced_evex_cmp_rip.relocation_count == 1 && advanced_evex_cmp_rip.relocations[0].offset == 6 &&
                               advanced_evex_cmp_rip.relocations[0].addend == -5 &&
                               advanced_evex_cmp_rip.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC32);

    AssemblyEncodeResult advanced_evex_cmpd_rip = assembly_encode(
        arguments->arena, S8("vcmppd k1, zmm2, zmmword ptr [rip+cmpd_external], 7\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_advanced_evex_cmpd_rip[] = {0x62, 0xf1, 0xed, 0x48, 0xc2, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x07};
    BUSTER_TEST(arguments, advanced_evex_cmpd_rip.diagnostic_count == 0 &&
                               advanced_evex_cmpd_rip.bytes.length == sizeof(expected_advanced_evex_cmpd_rip) &&
                               memcmp(advanced_evex_cmpd_rip.bytes.pointer, expected_advanced_evex_cmpd_rip,
                                      sizeof(expected_advanced_evex_cmpd_rip)) == 0 &&
                               advanced_evex_cmpd_rip.relocation_count == 1 && advanced_evex_cmpd_rip.relocations[0].offset == 6 &&
                               advanced_evex_cmpd_rip.relocations[0].addend == -5 &&
                               advanced_evex_cmpd_rip.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC32);

    AssemblyEncodeResult advanced_evex_pcmpq_rip = assembly_encode(
        arguments->arena, S8("vpcmpq k1, zmm2, zmmword ptr [rip+pcmpq_external], 7\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_advanced_evex_pcmpq_rip[] = {0x62, 0xf3, 0xed, 0x48, 0x1f, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x07};
    BUSTER_TEST(arguments, advanced_evex_pcmpq_rip.diagnostic_count == 0 &&
                               advanced_evex_pcmpq_rip.bytes.length == sizeof(expected_advanced_evex_pcmpq_rip) &&
                               memcmp(advanced_evex_pcmpq_rip.bytes.pointer, expected_advanced_evex_pcmpq_rip,
                                      sizeof(expected_advanced_evex_pcmpq_rip)) == 0 &&
                               advanced_evex_pcmpq_rip.relocation_count == 1 && advanced_evex_pcmpq_rip.relocations[0].offset == 6 &&
                               advanced_evex_pcmpq_rip.relocations[0].addend == -5 &&
                               advanced_evex_pcmpq_rip.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC32);

    AssemblyEncodeResult advanced_evex_pcmpud_rip = assembly_encode(
        arguments->arena, S8("vpcmpud k1, zmm2, zmmword ptr [rip+pcmpud_external], 7\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_advanced_evex_pcmpud_rip[] = {0x62, 0xf3, 0x6d, 0x48, 0x1e, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x07};
    BUSTER_TEST(arguments, advanced_evex_pcmpud_rip.diagnostic_count == 0 &&
                               advanced_evex_pcmpud_rip.bytes.length == sizeof(expected_advanced_evex_pcmpud_rip) &&
                               memcmp(advanced_evex_pcmpud_rip.bytes.pointer, expected_advanced_evex_pcmpud_rip,
                                      sizeof(expected_advanced_evex_pcmpud_rip)) == 0 &&
                               advanced_evex_pcmpud_rip.relocation_count == 1 && advanced_evex_pcmpud_rip.relocations[0].offset == 6 &&
                               advanced_evex_pcmpud_rip.relocations[0].addend == -5 &&
                               advanced_evex_pcmpud_rip.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC32);

    AssemblyEncodeResult advanced_evex_round_rip = assembly_encode(
        arguments->arena, S8("vrndscaleps zmm0, zmmword ptr [rip+round_external], 4\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_advanced_evex_round_rip[] = {0x62, 0xf3, 0x7d, 0x48, 0x08, 0x05, 0x00, 0x00, 0x00, 0x00, 0x04};
    BUSTER_TEST(arguments, advanced_evex_round_rip.diagnostic_count == 0 &&
                               advanced_evex_round_rip.bytes.length == sizeof(expected_advanced_evex_round_rip) &&
                               memcmp(advanced_evex_round_rip.bytes.pointer, expected_advanced_evex_round_rip,
                                      sizeof(expected_advanced_evex_round_rip)) == 0 &&
                               advanced_evex_round_rip.relocation_count == 1 && advanced_evex_round_rip.relocations[0].offset == 6 &&
                               advanced_evex_round_rip.relocations[0].addend == -5 &&
                               advanced_evex_round_rip.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC32);

    AssemblyEncodeResult advanced_evex_roundd_rip = assembly_encode(
        arguments->arena, S8("vrndscalepd zmm0, zmmword ptr [rip+roundd_external], 4\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_advanced_evex_roundd_rip[] = {0x62, 0xf3, 0xfd, 0x48, 0x09, 0x05, 0x00, 0x00, 0x00, 0x00, 0x04};
    BUSTER_TEST(arguments, advanced_evex_roundd_rip.diagnostic_count == 0 &&
                               advanced_evex_roundd_rip.bytes.length == sizeof(expected_advanced_evex_roundd_rip) &&
                               memcmp(advanced_evex_roundd_rip.bytes.pointer, expected_advanced_evex_roundd_rip,
                                      sizeof(expected_advanced_evex_roundd_rip)) == 0 &&
                               advanced_evex_roundd_rip.relocation_count == 1 && advanced_evex_roundd_rip.relocations[0].offset == 6 &&
                               advanced_evex_roundd_rip.relocations[0].addend == -5 &&
                               advanced_evex_roundd_rip.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC32);

    AssemblyEncodeResult advanced_evex_cmp_att_rip = assembly_encode(
        arguments->arena, S8("vcmpps $7, cmp_att_external(%rip), %zmm2, %k1\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, advanced_evex_cmp_att_rip.diagnostic_count == 0 &&
                               advanced_evex_cmp_att_rip.bytes.length == sizeof(expected_advanced_evex_cmp_rip) &&
                               memcmp(advanced_evex_cmp_att_rip.bytes.pointer, expected_advanced_evex_cmp_rip,
                                      sizeof(expected_advanced_evex_cmp_rip)) == 0 &&
                               advanced_evex_cmp_att_rip.relocation_count == 1 &&
                               advanced_evex_cmp_att_rip.relocations[0].offset == 6 &&
                               advanced_evex_cmp_att_rip.relocations[0].addend == -5 &&
                               advanced_evex_cmp_att_rip.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC32);

    AssemblyEncodeResult advanced_att = assembly_encode(
        arguments->arena,
        S8("vaddps %zmm3, %zmm2, %zmm0\n"
           "vcmpPS $7, %zmm3, %zmm2, %k1\n"
           "vrndscaleps $4, %zmm30, %zmm31\n"
           "vaddps {rn-sae}, %zmm2, %zmm1, %zmm0 {%k1}{z}\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    u8 expected_advanced_att[] = {
        0x62, 0xf1, 0x6c, 0x48, 0x58, 0xc3,
        0x62, 0xf1, 0x6c, 0x48, 0xc2, 0xcb, 0x07,
        0x62, 0x03, 0x7d, 0x48, 0x08, 0xfe, 0x04,
        0x62, 0xf1, 0x74, 0x99, 0x58, 0xc2,
    };
    BUSTER_TEST(arguments, advanced_att.diagnostic_count == 0 && advanced_att.bytes.length == sizeof(expected_advanced_att) &&
                               memcmp(advanced_att.bytes.pointer, expected_advanced_att, sizeof(expected_advanced_att)) == 0);

    AssemblyEncodeResult advanced_vectors = assembly_encode(
        arguments->arena,
        S8("vaddps xmm16, xmm17, xmm18\n"
           "vaddps ymm16, ymm17, ymm18\n"
           "vaddps zmm16, zmm17, zmm18\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_advanced_vectors[] = {
        0x62, 0xa1, 0x74, 0x00, 0x58, 0xc2,
        0x62, 0xa1, 0x74, 0x20, 0x58, 0xc2,
        0x62, 0xa1, 0x74, 0x40, 0x58, 0xc2,
    };
    BUSTER_TEST(arguments, advanced_vectors.diagnostic_count == 0 &&
                               advanced_vectors.bytes.length == sizeof(expected_advanced_vectors) &&
                               memcmp(advanced_vectors.bytes.pointer, expected_advanced_vectors, sizeof(expected_advanced_vectors)) == 0);

    AssemblyEncodeResult advanced_apx = assembly_encode(
        arguments->arena,
        S8("add r16d, r17d, r18d\n"
           "add{nf} r16d, r17d, r18d\n"
           "{nf} add r16d, r17d\n"
           "addnf r16d, r17d\n"
           "{nf} add dword ptr [r16], r17d\n"
           "{nf} add dword ptr [r16+r17*4], r18d\n"
           "{nf} add dword ptr [r24+r25*4+64], r26d\n"
           "add r24, r25, r26\n"
           "mov r16d, r17d\n"
           "add r16d, dword ptr [r17]\n"
           "push r16\n"
           "pop r17\n"
           "push2 r16, r17\n"
           "pop2 r16, r17\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_advanced_apx[] = {
        0x62, 0xec, 0x7c, 0x10, 0x01, 0xd1,
        0x62, 0xec, 0x7c, 0x90, 0x01, 0xd1,
        0x62, 0xec, 0x7c, 0x0c, 0x01, 0xc8,
        0x62, 0xec, 0x7c, 0x0c, 0x01, 0xc8,
        0x62, 0xec, 0x7c, 0x0c, 0x01, 0x08,
        0x62, 0xec, 0x78, 0x0c, 0x01, 0x14, 0x88,
        0x62, 0x0c, 0x78, 0x0c, 0x01, 0x54, 0x88, 0x40,
        0x62, 0x4c, 0xbc, 0x10, 0x01, 0xd1,
        0xd5, 0x50, 0x89, 0xc8,
        0xd5, 0x50, 0x03, 0x01,
        0xd5, 0x10, 0x50,
        0xd5, 0x10, 0x59,
        0x62, 0xfc, 0x7c, 0x10, 0xff, 0xf1,
        0x62, 0xfc, 0x7c, 0x10, 0x8f, 0xc1,
    };
    BUSTER_TEST(arguments, advanced_apx.diagnostic_count == 0 && advanced_apx.bytes.length == sizeof(expected_advanced_apx) &&
                               memcmp(advanced_apx.bytes.pointer, expected_advanced_apx, sizeof(expected_advanced_apx)) == 0);

    AssemblyEncodeResult advanced_apx_att_nf = assembly_encode(
        arguments->arena,
        S8("{nf} addl %r18d, (%r16,%r17,4)\n"
           "{nf} addl %r26d, 64(%r24,%r25,4)\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    u8 expected_advanced_apx_att_nf[] = {
        0x62, 0xec, 0x78, 0x0c, 0x01, 0x14, 0x88,
        0x62, 0x0c, 0x78, 0x0c, 0x01, 0x54, 0x88, 0x40,
    };
    BUSTER_TEST(arguments, advanced_apx_att_nf.diagnostic_count == 0 &&
                               advanced_apx_att_nf.bytes.length == sizeof(expected_advanced_apx_att_nf) &&
                               memcmp(advanced_apx_att_nf.bytes.pointer, expected_advanced_apx_att_nf,
                                      sizeof(expected_advanced_apx_att_nf)) == 0);

    AssemblyEncodeResult advanced_apx_legacy_memory = assembly_encode(
        arguments->arena,
        S8("mov qword ptr [r16], r17\n"
           "add qword ptr [r16], r17\n"
           "mov qword ptr [r24+r25*4+64], r26\n"
           "add qword ptr [r24+r25*4+64], r26\n"
           "mov qword ptr [r16+apx_external], r17\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_advanced_apx_legacy_memory[] = {
        0xd5, 0x58, 0x89, 0x08,
        0xd5, 0x58, 0x01, 0x08,
        0xd5, 0x7f, 0x89, 0x54, 0x88, 0x40,
        0xd5, 0x7f, 0x01, 0x54, 0x88, 0x40,
        0xd5, 0x58, 0x89, 0x88, 0x00, 0x00, 0x00, 0x00,
    };
    BUSTER_TEST(arguments, advanced_apx_legacy_memory.diagnostic_count == 0 &&
                               advanced_apx_legacy_memory.bytes.length == sizeof(expected_advanced_apx_legacy_memory) &&
                               memcmp(advanced_apx_legacy_memory.bytes.pointer, expected_advanced_apx_legacy_memory,
                                      sizeof(expected_advanced_apx_legacy_memory)) == 0 &&
                               advanced_apx_legacy_memory.relocation_count == 1 &&
                               advanced_apx_legacy_memory.relocations[0].offset == 24 &&
                               advanced_apx_legacy_memory.relocations[0].addend == 0 &&
                               advanced_apx_legacy_memory.relocations[0].kind == ASSEMBLY_RELOCATION_X86_32 &&
                               string_equal(advanced_apx_legacy_memory.symbols[advanced_apx_legacy_memory.relocations[0].symbol].name,
                                            S8("apx_external")));

    AssemblyEncodeResult advanced_apx_legacy_memory_att = assembly_encode(
        arguments->arena,
        S8("movq %r17, (%r16)\n"
           "addq %r17, (%r16)\n"
           "movq %r26, 64(%r24,%r25,4)\n"
           "addq %r26, 64(%r24,%r25,4)\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    u8 expected_advanced_apx_legacy_memory_att[] = {
        0xd5, 0x58, 0x89, 0x08,
        0xd5, 0x58, 0x01, 0x08,
        0xd5, 0x7f, 0x89, 0x54, 0x88, 0x40,
        0xd5, 0x7f, 0x01, 0x54, 0x88, 0x40,
    };
    BUSTER_TEST(arguments, advanced_apx_legacy_memory_att.diagnostic_count == 0 &&
                               advanced_apx_legacy_memory_att.bytes.length == sizeof(expected_advanced_apx_legacy_memory_att) &&
                               memcmp(advanced_apx_legacy_memory_att.bytes.pointer, expected_advanced_apx_legacy_memory_att,
                                      sizeof(expected_advanced_apx_legacy_memory_att)) == 0);

    AssemblyEncodeResult invalid_apx_legacy_memory = assembly_encode(
        arguments->arena,
        S8("mov qword ptr [r16], r17d\n"
           "mov qword ptr [r16], ah\n"
           "add qword ptr [r16], dh\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_apx_legacy_memory.diagnostic_count == 3);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_apx_legacy_memory.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, invalid_apx_legacy_memory.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }

    AssemblyEncodeResult advanced_amx = assembly_encode(
        arguments->arena,
        S8("ldtilecfg [rax]\n"
           "sttilecfg [rax]\n"
           "tileloadd tmm1, [rax+rbx*4]\n"
           "tileloaddt1 tmm2, [rax]\n"
           "tilestored [r14], tmm7\n"
           "tilezero tmm0\n"
           "tdpbf16ps tmm0, tmm1, tmm2\n"
           "tdpbssd tmm0, tmm1, tmm2\n"
           "tdpbsud tmm0, tmm1, tmm2\n"
           "tdpbusd tmm0, tmm1, tmm2\n"
           "tdpbuud tmm0, tmm1, tmm2\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_advanced_amx[] = {
        0xc4, 0xe2, 0x78, 0x49, 0x00,
        0xc4, 0xe2, 0x79, 0x49, 0x00,
        0xc4, 0xe2, 0x7b, 0x4b, 0x0c, 0x98,
        0xc4, 0xe2, 0x79, 0x4b, 0x14, 0x20,
        0xc4, 0xc2, 0x7a, 0x4b, 0x3c, 0x26,
        0xc4, 0xe2, 0x7b, 0x49, 0xc0,
        0xc4, 0xe2, 0x6a, 0x5c, 0xc1,
        0xc4, 0xe2, 0x6b, 0x5e, 0xc1,
        0xc4, 0xe2, 0x6a, 0x5e, 0xc1,
        0xc4, 0xe2, 0x69, 0x5e, 0xc1,
        0xc4, 0xe2, 0x68, 0x5e, 0xc1,
    };
    BUSTER_TEST(arguments, advanced_amx.diagnostic_count == 0 && advanced_amx.bytes.length == sizeof(expected_advanced_amx) &&
                               memcmp(advanced_amx.bytes.pointer, expected_advanced_amx, sizeof(expected_advanced_amx)) == 0);

    AssemblyEncodeResult advanced_amx_egpr = assembly_encode(
        arguments->arena, S8("tileloadd tmm0, [r16]\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_advanced_amx_egpr[] = {0x62, 0xfa, 0x7f, 0x08, 0x4b, 0x04, 0x20};
    BUSTER_TEST(arguments, advanced_amx_egpr.diagnostic_count == 0 &&
                               advanced_amx_egpr.bytes.length == sizeof(expected_advanced_amx_egpr) &&
                               memcmp(advanced_amx_egpr.bytes.pointer, expected_advanced_amx_egpr,
                                      sizeof(expected_advanced_amx_egpr)) == 0);

    AssemblyEncodeResult advanced_amx_att = assembly_encode(
        arguments->arena, S8("tileloadd (%r16), %tmm0\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    u8 expected_advanced_amx_att[] = {0x62, 0xfa, 0x7f, 0x08, 0x4b, 0x04, 0x20};
    BUSTER_TEST(arguments, advanced_amx_att.diagnostic_count == 0 &&
                               advanced_amx_att.bytes.length == sizeof(expected_advanced_amx_att) &&
                               memcmp(advanced_amx_att.bytes.pointer, expected_advanced_amx_att,
                                      sizeof(expected_advanced_amx_att)) == 0);

    AssemblyEncodeResult advanced_amx_egpr_sib = assembly_encode(
        arguments->arena, S8("tileloadd tmm0, [r24+r25*4+64]\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_advanced_amx_egpr_sib[] = {0x62, 0x9a, 0x7b, 0x08, 0x4b, 0x44, 0x88, 0x40};
    BUSTER_TEST(arguments, advanced_amx_egpr_sib.diagnostic_count == 0 &&
                               advanced_amx_egpr_sib.bytes.length == sizeof(expected_advanced_amx_egpr_sib) &&
                               memcmp(advanced_amx_egpr_sib.bytes.pointer, expected_advanced_amx_egpr_sib,
                                      sizeof(expected_advanced_amx_egpr_sib)) == 0);

    Target avx10_1_target = x86_target;
    avx10_1_target.cpu_features_explicit = true;
    avx10_1_target.cpu_features = TARGET_CPU_FEATURE_X86_SSE2 | TARGET_CPU_FEATURE_X86_AVX |
                                   TARGET_CPU_FEATURE_X86_AVX10_1;
    AssemblyEncodeResult avx10_1 = assembly_encode(arguments->arena, S8("vmovdqa32 ymm0, ymm1\nvmovdqa32 zmm0, zmm1\n"),
                                                     (AssemblyEncodeOptions){.target = avx10_1_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_avx10_1[] = {0x62, 0xf1, 0x7d, 0x28, 0x6f, 0xc1};
    BUSTER_TEST(arguments, avx10_1.diagnostic_count == 1 && avx10_1.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE &&
                               avx10_1.bytes.length == sizeof(expected_avx10_1) &&
                               memcmp(avx10_1.bytes.pointer, expected_avx10_1, sizeof(expected_avx10_1)) == 0);
    avx10_1_target.cpu_features |= TARGET_CPU_FEATURE_X86_AVX10_512;
    AssemblyEncodeResult avx10_512 = assembly_encode(arguments->arena, S8("vmovdqa32 zmm0, zmm1\n"),
                                                      (AssemblyEncodeOptions){.target = avx10_1_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_avx10_512[] = {0x62, 0xf1, 0x7d, 0x48, 0x6f, 0xc1};
    BUSTER_TEST(arguments, avx10_512.diagnostic_count == 0 && avx10_512.bytes.length == sizeof(expected_avx10_512) &&
                               memcmp(avx10_512.bytes.pointer, expected_avx10_512, sizeof(expected_avx10_512)) == 0);

    AssemblyEncodeResult invalid_advanced_features = assembly_encode(
        arguments->arena,
        S8("vaddps zmm0, zmm1, zmm2\n"
           "vmovdqa32 xmm0, xmm1\n"
           "kmovw k1, k2\n"
           "add r16d, r17d, r18d\n"
           "ldtilecfg [rax]\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_advanced_features.diagnostic_count == 5);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_advanced_features.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, invalid_advanced_features.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
    }

    AssemblyEncodeResult invalid_advanced_operands = assembly_encode(
        arguments->arena,
        S8("vaddps zmm0 {z}, zmm1, zmm2\n"
           "vaddps zmm0, zmm1 {k1}, zmm2\n"
           "vaddps zmm0, zmm1, dword ptr [rax]{1to8}\n"
           "vcmpps k1, zmm2, zmm3, 32\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_advanced_operands.diagnostic_count == 4);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_advanced_operands.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, invalid_advanced_operands.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }

    AssemblyEncodeResult invalid_advanced_decorators = assembly_encode(
        arguments->arena,
        S8("vaddps zmm0 {k1}{k2}, zmm1, zmm2\n"
           "vaddps zmm0 {z}{z}, zmm1, zmm2\n"
           "vaddps zmm0 {1to16}, zmm1, zmm2\n"
           "vaddps zmm0 {sae}, zmm1, zmm2\n"
           "vxorps zmm0 {rn-sae}, zmm1, zmm2\n"
           "vaddps zmm0, zmm1, zmmword ptr [rax]{1to16}{1to8}\n"
           "vcmpps k1 {k2}, zmm2, zmm3, 7\n"
           "vcmpps k1, zmm2 {k2}, zmm3, 7\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_advanced_decorators.diagnostic_count == 8);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_advanced_decorators.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, invalid_advanced_decorators.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }

    AssemblyEncodeResult invalid_advanced_alias = assembly_encode(
        arguments->arena, S8("vroundscaleps zmm0, zmm1, 4\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_advanced_alias.diagnostic_count == 1 &&
                               invalid_advanced_alias.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNKNOWN_INSTRUCTION);

    AssemblyEncodeResult invalid_vmovdqa_aliases = assembly_encode(
        arguments->arena,
        S8("vmovdqa8 zmm0, zmm1\n"
           "vmovdqa16 zmm0, zmm1\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_vmovdqa_aliases.diagnostic_count == 2);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_vmovdqa_aliases.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, invalid_vmovdqa_aliases.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_UNKNOWN_INSTRUCTION);
    }

    Target missing_kmov_width_target = advanced_target;
    missing_kmov_width_target.cpu_features &= ~TARGET_CPU_FEATURE_X86_AVX512BW;
    AssemblyEncodeResult invalid_kmov_width_features = assembly_encode(
        arguments->arena,
        S8("kmovd k1, k2\n"
           "kmovq k1, k2\n"),
        (AssemblyEncodeOptions){.target = missing_kmov_width_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_kmov_width_features.diagnostic_count == 2);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_kmov_width_features.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, invalid_kmov_width_features.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
    }

    Target missing_kadd_width_target = advanced_target;
    missing_kadd_width_target.cpu_features &= ~TARGET_CPU_FEATURE_X86_AVX512DQ;
    AssemblyEncodeResult invalid_kadd_width_features = assembly_encode(
        arguments->arena, S8("kaddw k1, k2, k3\n"),
        (AssemblyEncodeOptions){.target = missing_kadd_width_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_kadd_width_features.diagnostic_count == 1 &&
                               invalid_kadd_width_features.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);

    Target amx_missing_target = advanced_target;
    amx_missing_target.cpu_features &= ~(TARGET_CPU_FEATURE_X86_AMX_TILE | TARGET_CPU_FEATURE_X86_AMX_BF16 |
                                         TARGET_CPU_FEATURE_X86_AMX_INT8);
    AssemblyEncodeResult invalid_amx_features = assembly_encode(
        arguments->arena,
        S8("tilezero tmm0\n"
           "tdpbf16ps tmm0, tmm1, tmm2\n"
           "tdpbssd tmm0, tmm1, tmm2\n"),
        (AssemblyEncodeOptions){.target = amx_missing_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_amx_features.diagnostic_count == 3);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_amx_features.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, invalid_amx_features.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
    }

    AssemblyEncodeResult invalid_apx_high_byte = assembly_encode(
        arguments->arena,
        S8("mov r16b, ah\n"
           "mov byte ptr [r16], ah\n"
           "add r16b, dh\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_apx_high_byte.diagnostic_count == 3);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_apx_high_byte.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, invalid_apx_high_byte.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }
    return result;
}
