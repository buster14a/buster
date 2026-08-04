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
