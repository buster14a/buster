#include <buster/tests/compiler/assembly/assembly_test.h>
#if BUSTER_INCLUDE_TESTS

BUSTER_GLOBAL_LOCAL bool assembly_test_bytes_equal(ByteSlice actual, u8 const* expected, u32 expected_count)
{
    return actual.length == expected_count && (!expected_count || (actual.pointer && expected &&
                                                                     memcmp(actual.pointer, expected, expected_count) == 0));
}

UnitTestResult assembly_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Target x86_target = {
        .cpu_arch = CPU_ARCH_X86_64,
        .os = OPERATING_SYSTEM_LINUX,
    };
    Target ace_target = x86_target;
    ace_target.cpu_model = CPU_MODEL_BASELINE;
    ace_target.cpu_features_explicit = true;
    ace_target.cpu_features = target_cpu_features_from_array((TargetCpuFeature const[]){TARGET_CPU_FEATURE_X86_SSE2,
                                                                                         TARGET_CPU_FEATURE_X86_ACE_1},
                                                              2);
    BUSTER_TEST(arguments, target_cpu_features_are_valid(ace_target));
    AssemblyEncodeResult ace_intel = assembly_encode(arguments->arena, S8("bsrmovf zmm0, zmm0\n"),
                                                      (AssemblyEncodeOptions){.target = ace_target,
                                                                               .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_ace_bsr_movf[] = {0x62, 0xf6, 0xfc, 0x48, 0x95, 0xc0};
    BUSTER_TEST(arguments, ace_intel.diagnostic_count == 0 &&
                               assembly_test_bytes_equal(ace_intel.bytes, expected_ace_bsr_movf,
                                                         BUSTER_ARRAY_LENGTH(expected_ace_bsr_movf)));
    AssemblyEncodeResult ace_att = assembly_encode(arguments->arena, S8("bsrmovf %zmm0, %zmm0\n"),
                                                    (AssemblyEncodeOptions){.target = ace_target,
                                                                             .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, ace_att.diagnostic_count == 0 &&
                               assembly_test_bytes_equal(ace_att.bytes, expected_ace_bsr_movf,
                                                         BUSTER_ARRAY_LENGTH(expected_ace_bsr_movf)));
    AssemblyEncodeResult ace_init_intel = assembly_encode(arguments->arena, S8("bsrinit\n"),
                                                           (AssemblyEncodeOptions){.target = ace_target,
                                                                                    .syntax = ASSEMBLY_SYNTAX_INTEL});
    AssemblyEncodeResult ace_init_att = assembly_encode(arguments->arena, S8("bsrinit\n"),
                                                         (AssemblyEncodeOptions){.target = ace_target,
                                                                                  .syntax = ASSEMBLY_SYNTAX_ATT});
    AssemblyEncodeResult ace_init_explicit_bsr0 = assembly_encode(
        arguments->arena, S8("bsrinit bsr0\n"),
        (AssemblyEncodeOptions){.target = ace_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_ace_bsr_init[] = {0xc4, 0xe2, 0xfb, 0x49, 0xc0};
    BUSTER_TEST(arguments, ace_init_intel.diagnostic_count == 0 &&
                               assembly_test_bytes_equal(ace_init_intel.bytes, expected_ace_bsr_init,
                                                         BUSTER_ARRAY_LENGTH(expected_ace_bsr_init)));
    BUSTER_TEST(arguments, ace_init_att.diagnostic_count == 0 &&
                               assembly_test_bytes_equal(ace_init_att.bytes, expected_ace_bsr_init,
                                                         BUSTER_ARRAY_LENGTH(expected_ace_bsr_init)));
    BUSTER_TEST(arguments, ace_init_explicit_bsr0.diagnostic_count == 1 &&
                               ace_init_explicit_bsr0.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);

    // Every ACE-1 BSRMOV form is exposed through both source dialects.  The
    // explicit BSR0 operand carries the direction for the H/L rows; memory
    // rows use qword ptr in Intel and the fixed u64 schema normalization in
    // AT&T.  Distinct ZMM registers on BSRMOVF make operand topology visible
    // rather than allowing an accidental same-register encoding to pass.
    typedef struct AceAssemblyCase AceAssemblyCase;
    struct AceAssemblyCase
    {
        String8 source;
        AssemblySyntax syntax;
        u8 expected[6];
    };
    static AceAssemblyCase const ace_front_door_cases[] = {
        {S8_INITIALIZER("bsrmovf zmm1, zmm2\n"), ASSEMBLY_SYNTAX_INTEL, {0x62, 0xf6, 0xf4, 0x48, 0x95, 0xc2}},
        {S8_INITIALIZER("bsrmovf zmm1, qword ptr [rax]\n"), ASSEMBLY_SYNTAX_INTEL, {0x62, 0xf6, 0xf4, 0x48, 0x95, 0x00}},
        {S8_INITIALIZER("bsrmovh bsr0, zmm1\n"), ASSEMBLY_SYNTAX_INTEL, {0x62, 0xf6, 0xff, 0x48, 0x95, 0xc1}},
        {S8_INITIALIZER("bsrmovh bsr0, qword ptr [rax]\n"), ASSEMBLY_SYNTAX_INTEL, {0x62, 0xf6, 0xff, 0x48, 0x95, 0x00}},
        {S8_INITIALIZER("bsrmovh zmm1, bsr0\n"), ASSEMBLY_SYNTAX_INTEL, {0x62, 0xf6, 0x7f, 0x48, 0x95, 0xc1}},
        {S8_INITIALIZER("bsrmovh qword ptr [rax], bsr0\n"), ASSEMBLY_SYNTAX_INTEL, {0x62, 0xf6, 0x7f, 0x48, 0x95, 0x00}},
        {S8_INITIALIZER("bsrmovl bsr0, zmm1\n"), ASSEMBLY_SYNTAX_INTEL, {0x62, 0xf6, 0xfe, 0x48, 0x95, 0xc1}},
        {S8_INITIALIZER("bsrmovl bsr0, qword ptr [rax]\n"), ASSEMBLY_SYNTAX_INTEL, {0x62, 0xf6, 0xfe, 0x48, 0x95, 0x00}},
        {S8_INITIALIZER("bsrmovl zmm1, bsr0\n"), ASSEMBLY_SYNTAX_INTEL, {0x62, 0xf6, 0x7e, 0x48, 0x95, 0xc1}},
        {S8_INITIALIZER("bsrmovl qword ptr [rax], bsr0\n"), ASSEMBLY_SYNTAX_INTEL, {0x62, 0xf6, 0x7e, 0x48, 0x95, 0x00}},
        {S8_INITIALIZER("bsrmovf %zmm2, %zmm1\n"), ASSEMBLY_SYNTAX_ATT, {0x62, 0xf6, 0xf4, 0x48, 0x95, 0xc2}},
        {S8_INITIALIZER("bsrmovf (%rax), %zmm1\n"), ASSEMBLY_SYNTAX_ATT, {0x62, 0xf6, 0xf4, 0x48, 0x95, 0x00}},
        {S8_INITIALIZER("bsrmovh %zmm1, %bsr0\n"), ASSEMBLY_SYNTAX_ATT, {0x62, 0xf6, 0xff, 0x48, 0x95, 0xc1}},
        {S8_INITIALIZER("bsrmovh (%rax), %bsr0\n"), ASSEMBLY_SYNTAX_ATT, {0x62, 0xf6, 0xff, 0x48, 0x95, 0x00}},
        {S8_INITIALIZER("bsrmovh %bsr0, %zmm1\n"), ASSEMBLY_SYNTAX_ATT, {0x62, 0xf6, 0x7f, 0x48, 0x95, 0xc1}},
        {S8_INITIALIZER("bsrmovh %bsr0, (%rax)\n"), ASSEMBLY_SYNTAX_ATT, {0x62, 0xf6, 0x7f, 0x48, 0x95, 0x00}},
        {S8_INITIALIZER("bsrmovl %zmm1, %bsr0\n"), ASSEMBLY_SYNTAX_ATT, {0x62, 0xf6, 0xfe, 0x48, 0x95, 0xc1}},
        {S8_INITIALIZER("bsrmovl (%rax), %bsr0\n"), ASSEMBLY_SYNTAX_ATT, {0x62, 0xf6, 0xfe, 0x48, 0x95, 0x00}},
        {S8_INITIALIZER("bsrmovl %bsr0, %zmm1\n"), ASSEMBLY_SYNTAX_ATT, {0x62, 0xf6, 0x7e, 0x48, 0x95, 0xc1}},
        {S8_INITIALIZER("bsrmovl %bsr0, (%rax)\n"), ASSEMBLY_SYNTAX_ATT, {0x62, 0xf6, 0x7e, 0x48, 0x95, 0x00}},
    };
    for (u32 case_index = 0; case_index < BUSTER_ARRAY_LENGTH(ace_front_door_cases); case_index += 1)
    {
        AceAssemblyCase const test_case = ace_front_door_cases[case_index];
        AssemblyEncodeResult encoded = assembly_encode(arguments->arena, test_case.source,
                                                        (AssemblyEncodeOptions){.target = ace_target, .syntax = test_case.syntax});
        BUSTER_TEST(arguments, encoded.diagnostic_count == 0 &&
                                   assembly_test_bytes_equal(encoded.bytes, test_case.expected,
                                                             BUSTER_ARRAY_LENGTH(test_case.expected)));
    }
    AssemblyEncodeResult ace_wrong_direction = assembly_encode(
        arguments->arena, S8("bsrmovh zmm1, zmm2\n"),
        (AssemblyEncodeOptions){.target = ace_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    AssemblyEncodeResult ace_wrong_class = assembly_encode(
        arguments->arena, S8("bsrmovh bsr0, ymm1\n"),
        (AssemblyEncodeOptions){.target = ace_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    AssemblyEncodeResult ace_wrong_width = assembly_encode(
        arguments->arena, S8("bsrmovh bsr0, byte ptr [rax]\n"),
        (AssemblyEncodeOptions){.target = ace_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, ace_wrong_direction.diagnostic_count == 1 &&
                               ace_wrong_direction.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    BUSTER_TEST(arguments, ace_wrong_class.diagnostic_count == 1 &&
                               ace_wrong_class.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    BUSTER_TEST(arguments, ace_wrong_width.diagnostic_count == 1 &&
                               ace_wrong_width.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    Target amx_tile_only_target = ace_target;
    amx_tile_only_target.cpu_features = target_cpu_features_from_array((TargetCpuFeature const[]){TARGET_CPU_FEATURE_X86_SSE2,
                                                                                                  TARGET_CPU_FEATURE_X86_AMX_TILE},
                                                                       2);
    AssemblyEncodeResult ace_without_feature = assembly_encode(
        arguments->arena, S8("bsrmovf zmm0, zmm1\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    AssemblyEncodeResult ace_init_without_feature = assembly_encode(
        arguments->arena, S8("bsrinit\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    AssemblyEncodeResult ace_with_amx_tile = assembly_encode(
        arguments->arena, S8("bsrmovf zmm0, zmm1\n"),
        (AssemblyEncodeOptions){.target = amx_tile_only_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, ace_without_feature.diagnostic_count == 1 &&
                               ace_without_feature.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
    BUSTER_TEST(arguments, ace_init_without_feature.diagnostic_count == 1 &&
                               ace_init_without_feature.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
    BUSTER_TEST(arguments, ace_with_amx_tile.diagnostic_count == 1 &&
                               ace_with_amx_tile.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
    AssemblyEncodeResult ace_bsr0_without_feature = assembly_encode(
        arguments->arena, S8("bsrmovh bsr0, zmm1\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, ace_bsr0_without_feature.diagnostic_count == 1 &&
                               ace_bsr0_without_feature.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
    Target scalar_feature_target = x86_target;
    scalar_feature_target.cpu_model = CPU_MODEL_BASELINE;
    scalar_feature_target.cpu_features_explicit = true;
    scalar_feature_target.cpu_features = target_cpu_features_from_array((TargetCpuFeature const[]){
        TARGET_CPU_FEATURE_X86_SSE2, TARGET_CPU_FEATURE_X86_AVX, TARGET_CPU_FEATURE_X86_F16C,
        TARGET_CPU_FEATURE_X86_FMA, TARGET_CPU_FEATURE_X86_SSE4_2, TARGET_CPU_FEATURE_X86_BMI2,
        TARGET_CPU_FEATURE_X86_RDRAND}, 7);
    AssemblyEncodeResult f16c_without_feature = assembly_encode(
        arguments->arena, S8("vcvtph2ps xmm0, xmm1\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    AssemblyEncodeResult f16c_with_feature = assembly_encode(
        arguments->arena, S8("vcvtph2ps xmm0, xmm1\n"),
        (AssemblyEncodeOptions){.target = scalar_feature_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    AssemblyEncodeResult fma_without_feature = assembly_encode(
        arguments->arena, S8("vfmadd132ps xmm0, xmm1, xmm2\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    AssemblyEncodeResult fma_with_feature = assembly_encode(
        arguments->arena, S8("vfmadd132ps xmm0, xmm1, xmm2\n"),
        (AssemblyEncodeOptions){.target = scalar_feature_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    AssemblyEncodeResult bmi2_without_feature = assembly_encode(
        arguments->arena, S8("bzhi rax, rbx, rcx\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    AssemblyEncodeResult bmi2_with_feature = assembly_encode(
        arguments->arena, S8("bzhi rax, rbx, rcx\n"),
        (AssemblyEncodeOptions){.target = scalar_feature_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    AssemblyEncodeResult sse42_without_feature = assembly_encode(
        arguments->arena, S8("crc32 eax, ecx\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    AssemblyEncodeResult sse42_with_feature = assembly_encode(
        arguments->arena, S8("crc32 eax, ecx\n"),
        (AssemblyEncodeOptions){.target = scalar_feature_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, f16c_without_feature.diagnostic_count == 1 &&
                               f16c_without_feature.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE &&
                               f16c_with_feature.diagnostic_count == 0);
    BUSTER_TEST(arguments, fma_without_feature.diagnostic_count == 1 &&
                               fma_without_feature.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE &&
                               fma_with_feature.diagnostic_count == 0);
    BUSTER_TEST(arguments, bmi2_without_feature.diagnostic_count == 1 &&
                               bmi2_without_feature.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE &&
                               bmi2_with_feature.diagnostic_count == 0);
    BUSTER_TEST(arguments, sse42_without_feature.diagnostic_count == 1 &&
                               sse42_without_feature.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE &&
                               sse42_with_feature.diagnostic_count == 0);
    Target sse4a_target = x86_target;
    sse4a_target.cpu_model = CPU_MODEL_AMD_AMD_FAMILY_10;
    sse4a_target.cpu_features_explicit = true;
    sse4a_target.cpu_features = target_cpu_features_from_array((TargetCpuFeature const[]){TARGET_CPU_FEATURE_X86_SSE2,
                                                                                          TARGET_CPU_FEATURE_X86_SSE3,
                                                                                          TARGET_CPU_FEATURE_X86_SSE4A},
                                                               3);
    Target virtualization_target = x86_target;
    virtualization_target.cpu_model = CPU_MODEL_BASELINE;
    virtualization_target.cpu_features_explicit = true;
    virtualization_target.cpu_features = target_cpu_features_from_array((TargetCpuFeature const[]){
        TARGET_CPU_FEATURE_X86_SSE2, TARGET_CPU_FEATURE_X86_SVM, TARGET_CPU_FEATURE_X86_VMX}, 3);
    BUSTER_TEST(arguments, target_cpu_feature_from_string(CPU_ARCH_X86_64, S8("svm")) == TARGET_CPU_FEATURE_X86_SVM);
    BUSTER_TEST(arguments, target_cpu_feature_from_string(CPU_ARCH_X86_64, S8("vmx")) == TARGET_CPU_FEATURE_X86_VMX);
    BUSTER_TEST(arguments, target_cpu_features_are_valid(virtualization_target));
    BUSTER_STRING_TEST(arguments, target_cpu_features_to_string(arguments->arena, virtualization_target), S8("sse2,svm,vmx"));
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

    // Front-door routing for the legacy prefix-control rows: RET/LEAVE use
    // the newly normalized DF64/IMMUNE66_LOOP64 forms, while LOOP-family
    // branches retain their 8-bit displacement and ignore redundant 66.
    AssemblyEncodeResult residual_controls = assembly_encode(
        arguments->arena, S8("ret\nleave\nloop loop_target\nloopne loop_target\nloope loop_target\nloop_target:\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_residual_controls[] = {0xc3, 0xc9, 0xe2, 0x04, 0xe0, 0x02, 0xe1, 0x00};
    BUSTER_TEST(arguments, residual_controls.diagnostic_count == 0 &&
                               assembly_test_bytes_equal(residual_controls.bytes, expected_residual_controls,
                                                         sizeof(expected_residual_controls)));

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

    // GNU/AT&T treats a bare non-immediate expression as an absolute memory
    // operand.  Keep the explicit '$' spelling as an immediate and preserve
    // direct versus indirect branch targets despite their shared bare form.
    AssemblyEncodeResult x86_att_absolute = assembly_encode(
        arguments->arena,
        S8("movq 0x12345678, %rax\n"
           "movq %rax, 0x12345678\n"
           "movb external, %al\n"
           "movb %al, external\n"
           "movq 0x123456789, %rax\n"
           "call target\n"
           "call *target\n"
           "target:\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    u8 expected_x86_att_absolute[] = {
        0x48, 0x8b, 0x04, 0x25, 0x78, 0x56, 0x34, 0x12,
        0x48, 0x89, 0x04, 0x25, 0x78, 0x56, 0x34, 0x12,
        0x8a, 0x04, 0x25, 0x00, 0x00, 0x00, 0x00,
        0x88, 0x04, 0x25, 0x00, 0x00, 0x00, 0x00,
        0x48, 0xa1, 0x89, 0x67, 0x45, 0x23, 0x01, 0x00, 0x00, 0x00,
        0xe8, 0x07, 0x00, 0x00, 0x00,
        0xff, 0x14, 0x25, 0x34, 0x00, 0x00, 0x00,
    };
    BUSTER_TEST(arguments, x86_att_absolute.diagnostic_count == 0 &&
                               assembly_test_bytes_equal(x86_att_absolute.bytes, expected_x86_att_absolute,
                                                         BUSTER_ARRAY_LENGTH(expected_x86_att_absolute)));
    BUSTER_TEST(arguments, x86_att_absolute.relocation_count == 2 &&
                               x86_att_absolute.relocations[0].offset == 19 &&
                               x86_att_absolute.relocations[0].kind == ASSEMBLY_RELOCATION_X86_32 &&
                               x86_att_absolute.relocations[1].offset == 26 &&
                               x86_att_absolute.relocations[1].kind == ASSEMBLY_RELOCATION_X86_32 &&
                               string_equal(x86_att_absolute.symbols[x86_att_absolute.relocations[0].symbol].name, S8("external")) &&
                               string_equal(x86_att_absolute.symbols[x86_att_absolute.relocations[1].symbol].name, S8("external")));
    AssemblyEncodeResult x86_att_immediate = assembly_encode(
        arguments->arena, S8("movq $0x10, %rax\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    u8 expected_x86_att_immediate[] = {0x48, 0xb8, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    BUSTER_TEST(arguments, x86_att_immediate.diagnostic_count == 0 &&
                               assembly_test_bytes_equal(x86_att_immediate.bytes, expected_x86_att_immediate,
                                                         BUSTER_ARRAY_LENGTH(expected_x86_att_immediate)));
    // CET indirect-branch tracking has a typed `notrack` source prefix.  It
    // is accepted in both dialects for register and memory CALL/JMP forms,
    // while ordinary handwritten call/jmp syntax remains unprefixed.
    String8 x86_notrack_intel_source =
        S8("notrack call rax\n"
           "notrack jmp rax\n"
           "notrack call qword ptr [rax]\n"
           "notrack jmp qword ptr [rax]\n");
    u8 expected_x86_notrack[] = {
        0x3e, 0xff, 0xd0,
        0x3e, 0xff, 0xe0,
        0x3e, 0xff, 0x10,
        0x3e, 0xff, 0x20,
    };
    AssemblyEncodeResult x86_notrack_intel = assembly_encode(
        arguments->arena, x86_notrack_intel_source,
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, x86_notrack_intel.diagnostic_count == 0 &&
                               x86_notrack_intel.bytes.length == sizeof(expected_x86_notrack) &&
                               memcmp(x86_notrack_intel.bytes.pointer, expected_x86_notrack, sizeof(expected_x86_notrack)) == 0);
    String8 x86_notrack_att_source =
        S8("notrack callq *%rax\n"
           "notrack jmpq *%rax\n"
           "notrack callq *(%rax)\n"
           "notrack jmpq *(%rax)\n");
    AssemblyEncodeResult x86_notrack_att = assembly_encode(
        arguments->arena, x86_notrack_att_source,
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, x86_notrack_att.diagnostic_count == 0 &&
                               x86_notrack_att.bytes.length == sizeof(expected_x86_notrack) &&
                               memcmp(x86_notrack_att.bytes.pointer, expected_x86_notrack, sizeof(expected_x86_notrack)) == 0);
    AssemblyEncodeResult x86_notrack_invalid = assembly_encode(
        arguments->arena, S8("notrack call external\nnotrack jmp external\nnotrack notrack call rax\nrep notrack call rax\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, x86_notrack_invalid.diagnostic_count == 4 && x86_notrack_invalid.bytes.length == 0);
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

    // MOV moffs is the one legacy absolute-memory encoding whose accumulator
    // is implicit in the opcode.  Keep the accumulator written in source so
    // the assembly front door binds AL/RAX and direction to A0/A2 exactly;
    // the 64-bit address is deliberately outside ModRM's signed-32 range.
    String8 x86_moffs_source =
        S8("mov al, byte ptr es:[0x155667788]\n"
           "mov byte ptr es:[0x155667788], al\n");
    AssemblyEncodeResult x86_moffs = assembly_encode(
        arguments->arena, x86_moffs_source, (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_x86_moffs[] = {
        0x26, 0xa0, 0x88, 0x77, 0x66, 0x55, 0x01, 0x00, 0x00, 0x00,
        0x26, 0xa2, 0x88, 0x77, 0x66, 0x55, 0x01, 0x00, 0x00, 0x00,
    };
    BUSTER_TEST(arguments, x86_moffs.diagnostic_count == 0 &&
                               x86_moffs.bytes.length == sizeof(expected_x86_moffs) &&
                               memcmp(x86_moffs.bytes.pointer, expected_x86_moffs, sizeof(expected_x86_moffs)) == 0);
    AssemblyEncodeResult x86_moffs_wrong_accumulator = assembly_encode(
        arguments->arena, S8("mov bl, byte ptr es:[0x155667788]\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, x86_moffs_wrong_accumulator.diagnostic_count != 0 && x86_moffs_wrong_accumulator.bytes.length == 0);

    // MASKMOV's architectural destination is the implicit [DI] location;
    // only the two visible vector registers appear in source.  Intel and
    // AT&T spellings select the same REG/RM bytes after AT&T's operand
    // reversal, while address-size 32 carries the ordinary 67 override.
    AssemblyEncodeResult x86_maskmov_intel = assembly_encode(
        arguments->arena,
        S8("maskmovq mm0, mm1\nmaskmovdqu xmm0, xmm1\naddr32 maskmovq mm0, mm1\n"
           "fs maskmovq mm0, mm1\ngs addr32 maskmovdqu xmm0, xmm1\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_x86_maskmov_intel[] = {
        0x0f, 0xf7, 0xc1,
        0x66, 0x0f, 0xf7, 0xc1,
        0x67, 0x0f, 0xf7, 0xc1,
        0x64, 0x0f, 0xf7, 0xc1,
        0x65, 0x67, 0x66, 0x0f, 0xf7, 0xc1,
    };
    BUSTER_TEST(arguments, x86_maskmov_intel.diagnostic_count == 0 &&
                               assembly_test_bytes_equal(x86_maskmov_intel.bytes, expected_x86_maskmov_intel,
                                                         BUSTER_ARRAY_LENGTH(expected_x86_maskmov_intel)));
    AssemblyEncodeResult x86_maskmov_att = assembly_encode(
        arguments->arena, S8("maskmovq %mm1, %mm0\nmaskmovdqu %xmm1, %xmm0\nfs maskmovq %mm1, %mm0\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    u8 expected_x86_maskmov_att[] = {0x0f, 0xf7, 0xc1, 0x66, 0x0f, 0xf7, 0xc1, 0x64, 0x0f, 0xf7, 0xc1};
    BUSTER_TEST(arguments, x86_maskmov_att.diagnostic_count == 0 &&
                               assembly_test_bytes_equal(x86_maskmov_att.bytes, expected_x86_maskmov_att,
                                                         BUSTER_ARRAY_LENGTH(expected_x86_maskmov_att)));
    AssemblyEncodeResult x86_maskmov_wrong_class = assembly_encode(
        arguments->arena, S8("maskmovq xmm0, xmm1\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    AssemblyEncodeResult x86_maskmov_wrong_count = assembly_encode(
        arguments->arena, S8("maskmovq mm0\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    AssemblyEncodeResult x86_maskmov_wrong_direction = assembly_encode(
        // The implicit [DI] store cannot be written as an explicit memory
        // destination in either operand direction.
        arguments->arena, S8("maskmovq [rdi], mm1\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, x86_maskmov_wrong_class.diagnostic_count != 0 && x86_maskmov_wrong_class.bytes.length == 0 &&
                               x86_maskmov_wrong_count.diagnostic_count != 0 && x86_maskmov_wrong_count.bytes.length == 0 &&
                               x86_maskmov_wrong_direction.diagnostic_count != 0 && x86_maskmov_wrong_direction.bytes.length == 0);
    AssemblyEncodeResult x86_maskmov_fs = assembly_encode(
        arguments->arena, S8("fs:maskmovq mm0, mm1\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    AssemblyEncodeResult x86_maskmov_gs_att = assembly_encode(
        arguments->arena, S8("%gs:maskmovq %mm1, %mm0\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, x86_maskmov_fs.diagnostic_count != 0 && x86_maskmov_fs.bytes.length == 0);
    BUSTER_TEST(arguments, x86_maskmov_gs_att.diagnostic_count != 0 && x86_maskmov_gs_att.bytes.length == 0);

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
           "sbbq %external, %rax\n"
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
    x86_without_sse2.cpu_features = target_cpu_features_empty();
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
    u8 expected_x86_address32_mmx[] = {0x67, 0x0f, 0x6f, 0x00};
    AssemblyEncodeResult x86_address32_mmx = assembly_encode(
        arguments->arena, S8("movq mm0, qword ptr [eax]\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, x86_address32_mmx.diagnostic_count == 0 && x86_address32_mmx.bytes.length == sizeof(expected_x86_address32_mmx) &&
                               memcmp(x86_address32_mmx.bytes.pointer, expected_x86_address32_mmx, sizeof(expected_x86_address32_mmx)) == 0);
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
    x86_sse3_target.cpu_features = target_cpu_features_from_array((TargetCpuFeature const[]){
        TARGET_CPU_FEATURE_X86_SSE2, TARGET_CPU_FEATURE_X86_SSE3}, 2);
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
    u8 expected_x86_address32_fisttp[] = {0x67, 0xdb, 0x08};
    AssemblyEncodeResult x86_address32_fisttp = assembly_encode(
        arguments->arena, S8("fisttp dword ptr [eax]\n"),
        (AssemblyEncodeOptions){.target = x86_sse3_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, x86_address32_fisttp.diagnostic_count == 0 &&
                               x86_address32_fisttp.bytes.length == sizeof(expected_x86_address32_fisttp) &&
                               memcmp(x86_address32_fisttp.bytes.pointer, expected_x86_address32_fisttp,
                                      sizeof(expected_x86_address32_fisttp)) == 0);
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

    // CS/DS are the architectural not-taken/taken branch-hint prefixes.  A
    // literal targets exercise short-displacement sizing (including the
    // prefix byte) and force a 32-bit displacement for a distant target in
    // both source syntaxes.
    AssemblyEncodeResult x86_intel_branch_hints = assembly_encode(
        arguments->arena, S8("cs jz 0\n"
                             "ds jnz 0\n"
                             "cs jz 1000\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_x86_intel_branch_hints[] = {
        0x2e, 0x74, 0xfd,
        0x3e, 0x75, 0xfa,
        0x2e, 0x0f, 0x84, 0xdb, 0x03, 0x00, 0x00,
    };
    BUSTER_TEST(arguments, x86_intel_branch_hints.diagnostic_count == 0 &&
                               assembly_test_bytes_equal(x86_intel_branch_hints.bytes, expected_x86_intel_branch_hints,
                                                         sizeof(expected_x86_intel_branch_hints)));
    AssemblyEncodeResult x86_att_branch_hints = assembly_encode(
        arguments->arena, S8("cs jz $0\n"
                             "ds jnz $0\n"
                             "cs jz $1000\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, x86_att_branch_hints.diagnostic_count == 0 &&
                               assembly_test_bytes_equal(x86_att_branch_hints.bytes, expected_x86_intel_branch_hints,
                                                         sizeof(expected_x86_intel_branch_hints)));
    AssemblyEncodeResult x86_invalid_branch_hints = assembly_encode(
        arguments->arena, S8("cs mov rax, rbx\n"
                             "cs ds jz 0\n"
                             "cs cs jz 0\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, x86_invalid_branch_hints.diagnostic_count == 3);
    Target x86_avx_target = x86_target;
    x86_avx_target.cpu_features_explicit = true;
    x86_avx_target.cpu_features = target_cpu_features_from_array((TargetCpuFeature const[]){
        TARGET_CPU_FEATURE_X86_SSE2, TARGET_CPU_FEATURE_X86_AVX}, 2);
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
    x86_avx2_target.cpu_features = target_cpu_features_add(x86_avx2_target.cpu_features, TARGET_CPU_FEATURE_X86_AVX2);
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

    // LLVM byte oracles for the VV1 width-control rows.  The 32/64-bit
    // conversion pairs exercise NOREXW/REXW, while the extract pairs cover
    // the same metadata rule in a different opcode family.
    String8 x86_vv1_width_intel_source =
        S8("vcvtsd2si rax, qword ptr [rbx]\n"
           "vcvtsd2si eax, qword ptr [rbx]\n"
           "vcvtsi2sd xmm0, xmm1, rax\n"
           "vcvtsi2sd xmm0, xmm1, eax\n"
           "vpextrq qword ptr [rbx], xmm0, 1\n"
           "vpextrd dword ptr [rbx], xmm0, 1\n");
    u8 expected_x86_vv1_width[] = {
        0xc4, 0xe1, 0xfb, 0x2d, 0x03,
        0xc5, 0xfb, 0x2d, 0x03,
        0xc4, 0xe1, 0xf3, 0x2a, 0xc0,
        0xc5, 0xf3, 0x2a, 0xc0,
        0xc4, 0xe3, 0xf9, 0x16, 0x03, 0x01,
        0xc4, 0xe3, 0x79, 0x16, 0x03, 0x01,
    };
    AssemblyEncodeResult x86_vv1_width_intel = assembly_encode(
        arguments->arena, x86_vv1_width_intel_source,
        (AssemblyEncodeOptions){.target = x86_avx2_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, x86_vv1_width_intel.diagnostic_count == 0 &&
                               assembly_test_bytes_equal(x86_vv1_width_intel.bytes, expected_x86_vv1_width,
                                                         BUSTER_ARRAY_LENGTH(expected_x86_vv1_width)));
    Target x86_bit_atomic_target = x86_target;
    x86_bit_atomic_target.cpu_model = CPU_MODEL_BASELINE;
    x86_bit_atomic_target.cpu_features_explicit = true;
    x86_bit_atomic_target.cpu_features = target_cpu_features_from_array((TargetCpuFeature const[]){
        TARGET_CPU_FEATURE_X86_SSE2, TARGET_CPU_FEATURE_X86_POPCNT, TARGET_CPU_FEATURE_X86_LZCNT,
        TARGET_CPU_FEATURE_X86_BMI1, TARGET_CPU_FEATURE_X86_CX16}, 5);
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
    u8 expected_x86_address32_cmpxchg16b[] = {0x67, 0x48, 0x0f, 0xc7, 0x08};
    AssemblyEncodeResult x86_address32_cmpxchg16b = assembly_encode(
        arguments->arena, S8("cmpxchg16b xmmword ptr [eax]\n"),
        (AssemblyEncodeOptions){.target = x86_bit_atomic_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, x86_address32_cmpxchg16b.diagnostic_count == 0 &&
                               x86_address32_cmpxchg16b.bytes.length == sizeof(expected_x86_address32_cmpxchg16b) &&
                               memcmp(x86_address32_cmpxchg16b.bytes.pointer, expected_x86_address32_cmpxchg16b,
                                      sizeof(expected_x86_address32_cmpxchg16b)) == 0);
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
        assembly_encode(arguments->arena, S8("movq %rbx, 3(,%rax,2,4)\naddq 3(,%rax,2,4), %rax\ncallq %r11\n"),
                        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, invalid_att_forms.diagnostic_count == 3);
    AssemblyEncodeResult invalid_att_absolute = assembly_encode(
        arguments->arena,
        S8("movq , %rax\n"
           "movq broken(,%rax\n"
           "movq broken(,%rax,2,4), %rax\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, invalid_att_absolute.diagnostic_count == 3 && invalid_att_absolute.bytes.length == 0);
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
                               aarch64.relocations[0].kind == ASSEMBLY_RELOCATION_AARCH64_CALL26);
    AssemblyEncodeResult aarch64_jump = assembly_encode(arguments->arena, S8("b external\n"),
                                                         (AssemblyEncodeOptions){
                                                             .target = aarch64_target,
                                                         });
    u8 expected_aarch64_jump[] = {0, 0, 0, 0x14};
    BUSTER_TEST(arguments, aarch64_jump.diagnostic_count == 0 &&
                               aarch64_jump.bytes.length == sizeof(expected_aarch64_jump) &&
                               memcmp(aarch64_jump.bytes.pointer, expected_aarch64_jump, sizeof(expected_aarch64_jump)) == 0);
    BUSTER_TEST(arguments, aarch64_jump.relocation_count == 1 && aarch64_jump.relocations[0].offset == 0 &&
                               aarch64_jump.relocations[0].kind == ASSEMBLY_RELOCATION_AARCH64_JUMP26);

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

    Target aarch64_m1_target = {
        .cpu_arch = CPU_ARCH_AARCH64,
        .cpu_model = CPU_MODEL_A64_APPLE_M1,
        .os = OPERATING_SYSTEM_MACOS,
    };
    AssemblyEncodeResult aarch64_fixed = assembly_encode(
        arguments->arena,
        S8("NOP\n"
           "aUtIaSp\n"
           "RETAA\n"
           "AXFLAG\n"
           "PSSBB\n"
           "TSB    CSYNC\n"),
        (AssemblyEncodeOptions){.target = aarch64_m1_target});
    static u8 const expected_aarch64_fixed[] = {
        0x1f, 0x20, 0x03, 0xd5,
        0xbf, 0x23, 0x03, 0xd5,
        0xff, 0x0b, 0x5f, 0xd6,
        0x5f, 0x40, 0x00, 0xd5,
        0x9f, 0x34, 0x03, 0xd5,
        0x5f, 0x22, 0x03, 0xd5,
    };
    BUSTER_TEST(arguments, aarch64_fixed.diagnostic_count == 0 &&
                               aarch64_fixed.bytes.length == sizeof(expected_aarch64_fixed) &&
                               memcmp(aarch64_fixed.bytes.pointer, expected_aarch64_fixed, sizeof(expected_aarch64_fixed)) == 0);
    AssemblyEncodeResult aarch64_fixed_bad_operand = assembly_encode(
        arguments->arena, S8("NOP x0\n"), (AssemblyEncodeOptions){.target = aarch64_m1_target});
    BUSTER_TEST(arguments, aarch64_fixed_bad_operand.diagnostic_count == 1 &&
                               aarch64_fixed_bad_operand.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    AssemblyEncodeResult aarch64_fixed_bad_token = assembly_encode(
        arguments->arena, S8("AUTIASP x0\nTSB CSYNC extra\n"), (AssemblyEncodeOptions){.target = aarch64_m1_target});
    BUSTER_TEST(arguments, aarch64_fixed_bad_token.diagnostic_count == 2 &&
                               aarch64_fixed_bad_token.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNKNOWN_INSTRUCTION &&
                               aarch64_fixed_bad_token.diagnostics[1].kind == ASSEMBLY_DIAGNOSTIC_UNKNOWN_INSTRUCTION);
    AssemblyEncodeResult aarch64_fixed_generic = assembly_encode(
        arguments->arena, S8("AUTIASP\nRETAA\n"), (AssemblyEncodeOptions){.target = aarch64_target});
    BUSTER_TEST(arguments, aarch64_fixed_generic.diagnostic_count == 2 &&
                               aarch64_fixed_generic.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNKNOWN_INSTRUCTION &&
                               aarch64_fixed_generic.diagnostics[1].kind == ASSEMBLY_DIAGNOSTIC_UNKNOWN_INSTRUCTION);

    String8 split_operands[6] = {0};
    u32 split_operand_count = 0;
    bool split_lists = assembly_test_split_operands(
        S8("{v0.4s, v1.4s}, [x2, x3], (x4, x5), :lo12:symbol"),
        split_operands, BUSTER_ARRAY_LENGTH(split_operands), &split_operand_count);
    BUSTER_TEST(arguments, split_lists && split_operand_count == 4);
    if (split_lists && split_operand_count == 4)
    {
        BUSTER_STRING_TEST(arguments, split_operands[0], S8("{v0.4s, v1.4s}"));
        BUSTER_STRING_TEST(arguments, split_operands[1], S8("[x2, x3]"));
        BUSTER_STRING_TEST(arguments, split_operands[2], S8("(x4, x5)"));
        BUSTER_STRING_TEST(arguments, split_operands[3], S8(":lo12:symbol"));
    }
    BUSTER_TEST(arguments, !assembly_test_split_operands(
                               S8("{v0.4s, v1.4s], x0"), split_operands,
                               BUSTER_ARRAY_LENGTH(split_operands), &split_operand_count));
    BUSTER_TEST(arguments, !assembly_test_split_operands(
                               S8("{v0.4s, v1.4s, x0"), split_operands,
                               BUSTER_ARRAY_LENGTH(split_operands), &split_operand_count));
    BUSTER_TEST(arguments, !assembly_test_split_operands(
                               S8("([x0, x1)]"), split_operands,
                               BUSTER_ARRAY_LENGTH(split_operands), &split_operand_count));
    BUSTER_TEST(arguments, !assembly_test_split_operands(
                               S8("{[x0, x1}]"), split_operands,
                               BUSTER_ARRAY_LENGTH(split_operands), &split_operand_count));
    bool split_nested = assembly_test_split_operands(
        S8("({[x0, x1]}), x2"), split_operands,
        BUSTER_ARRAY_LENGTH(split_operands), &split_operand_count);
    BUSTER_TEST(arguments, split_nested && split_operand_count == 2);
    if (split_nested && split_operand_count == 2)
    {
        BUSTER_STRING_TEST(arguments, split_operands[0], S8("({[x0, x1]})"));
        BUSTER_STRING_TEST(arguments, split_operands[1], S8("x2"));
    }

    bool split_six = assembly_test_split_operands(
        S8("x0, x1, x2, x3, x4, x5"), split_operands,
        BUSTER_ARRAY_LENGTH(split_operands), &split_operand_count);
    BUSTER_TEST(arguments, split_six && split_operand_count == BUSTER_ARRAY_LENGTH(split_operands));
    BUSTER_TEST(arguments, !assembly_test_split_operands(
                               S8("x0, x1, x2, x3, x4, x5"), split_operands,
                               BUSTER_ARRAY_LENGTH(split_operands) - 1, &split_operand_count));

    AssemblyEncodeResult aarch64_same_line_label = assembly_encode(
        arguments->arena, S8("leading_label : b leading_label\n"),
        (AssemblyEncodeOptions){.target = aarch64_target});
    BUSTER_TEST(arguments, aarch64_same_line_label.diagnostic_count == 0 &&
                               aarch64_same_line_label.symbol_count == 1 &&
                               aarch64_same_line_label.symbols[0].defined &&
                               aarch64_same_line_label.symbols[0].offset == 0 &&
                               string_equal(aarch64_same_line_label.symbols[0].name, S8("leading_label")) &&
                               aarch64_same_line_label.bytes.length == sizeof(expected_aarch64_jump) &&
                               memcmp(aarch64_same_line_label.bytes.pointer, expected_aarch64_jump,
                                      sizeof(expected_aarch64_jump)) == 0);
    AssemblyEncodeResult aarch64_separated_label_without_space = assembly_encode(
        arguments->arena, S8("leading_label_without_space :nop\n"),
        (AssemblyEncodeOptions){.target = aarch64_target});
    BUSTER_TEST(arguments, aarch64_separated_label_without_space.diagnostic_count == 0 &&
                               aarch64_separated_label_without_space.symbol_count == 1 &&
                               aarch64_separated_label_without_space.symbols[0].defined &&
                               aarch64_separated_label_without_space.symbols[0].offset == 0 &&
                               string_equal(aarch64_separated_label_without_space.symbols[0].name,
                                            S8("leading_label_without_space")) &&
                               aarch64_separated_label_without_space.bytes.length == 4);
    AssemblyEncodeResult aarch64_modifier_operand = assembly_encode(
        arguments->arena, S8("b :lo12:target\n"),
        (AssemblyEncodeOptions){.target = aarch64_target});
    BUSTER_TEST(arguments, aarch64_modifier_operand.diagnostic_count == 1 &&
                               aarch64_modifier_operand.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS &&
                               aarch64_modifier_operand.symbol_count == 0 && aarch64_modifier_operand.bytes.length == 0);
    AssemblyEncodeResult aarch64_trailing_modifier_operand = assembly_encode(
        arguments->arena, S8("b target, :lo12:other\n"),
        (AssemblyEncodeOptions){.target = aarch64_target});
    BUSTER_TEST(arguments, aarch64_trailing_modifier_operand.diagnostic_count == 1 &&
                               aarch64_trailing_modifier_operand.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS &&
                               aarch64_trailing_modifier_operand.symbol_count == 1 &&
                               string_equal(aarch64_trailing_modifier_operand.symbols[0].name, S8("target")) &&
                               aarch64_trailing_modifier_operand.bytes.length == 0);
    AssemblyEncodeResult invalid_leading_label = assembly_encode(
        arguments->arena, S8("bad-label: nop\n"),
        (AssemblyEncodeOptions){.target = aarch64_target});
    BUSTER_TEST(arguments, invalid_leading_label.diagnostic_count == 1 &&
                               invalid_leading_label.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_STATEMENT &&
                               invalid_leading_label.symbol_count == 0 && invalid_leading_label.bytes.length == 4);

    AssemblyEncodeResult aarch64_six_operands = assembly_encode(
        arguments->arena, S8("b one, two, three, four, five, six\n"),
        (AssemblyEncodeOptions){.target = aarch64_target});
    BUSTER_TEST(arguments, aarch64_six_operands.diagnostic_count == 1 &&
                               aarch64_six_operands.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS &&
                               aarch64_six_operands.bytes.length == 0);
    AssemblyEncodeResult unchanged_x86_diagnostic = assembly_encode(
        arguments->arena, S8("mov rax, xmm0\n"),
        (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    AssemblyEncodeResult unchanged_aarch64_diagnostic = assembly_encode(
        arguments->arena, S8("ret x0\n"),
        (AssemblyEncodeOptions){.target = aarch64_target});
    BUSTER_TEST(arguments, unchanged_x86_diagnostic.diagnostic_count == 1 &&
                               unchanged_x86_diagnostic.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS &&
                               unchanged_x86_diagnostic.bytes.length == 0);
    BUSTER_TEST(arguments, unchanged_aarch64_diagnostic.diagnostic_count == 1 &&
                               unchanged_aarch64_diagnostic.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS &&
                               unchanged_aarch64_diagnostic.bytes.length == 0);

    AssemblyEncodeResult invalid_syntax = assembly_encode(arguments->arena, S8("nop"),
                                                           (AssemblyEncodeOptions){
                                                               .target = aarch64_target,
                                                               .syntax = ASSEMBLY_SYNTAX_ATT,
                                                           });
    BUSTER_TEST(arguments, invalid_syntax.diagnostic_count == 1 &&
                               invalid_syntax.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_SYNTAX);
    Target advanced_target = x86_target;
    advanced_target.cpu_features_explicit = true;
    advanced_target.cpu_features = target_cpu_features_from_array((TargetCpuFeature const[]){
        TARGET_CPU_FEATURE_X86_SSE2, TARGET_CPU_FEATURE_X86_AVX,
        TARGET_CPU_FEATURE_X86_AVX512F, TARGET_CPU_FEATURE_X86_AVX512VL,
        TARGET_CPU_FEATURE_X86_AVX512BW, TARGET_CPU_FEATURE_X86_AVX512DQ,
        TARGET_CPU_FEATURE_X86_APX, TARGET_CPU_FEATURE_X86_AMX_TILE,
        TARGET_CPU_FEATURE_X86_AMX_BF16, TARGET_CPU_FEATURE_X86_AMX_INT8}, 10);
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

    AssemblyEncodeResult advanced_evex_masked_forms = assembly_encode(
        arguments->arena,
        S8("vcmpps k1 {k2}, zmm2, zmm3, 7\n"
           "vcmpps k1 {k2}, zmm2, zmmword ptr [rax+64], 7\n"
           "vmovdqa64 zmmword ptr [rax] {k1}, zmm2\n"
           "vmovdqa64 zmmword ptr [rax+r8*4+64] {k3}, zmm2\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_advanced_evex_masked_forms[] = {
        0x62, 0xf1, 0x6c, 0x4a, 0xc2, 0xcb, 0x07,
        0x62, 0xf1, 0x6c, 0x4a, 0xc2, 0x48, 0x01, 0x07,
        0x62, 0xf1, 0xfd, 0x49, 0x7f, 0x10,
        0x62, 0xb1, 0xfd, 0x4b, 0x7f, 0x54, 0x80, 0x01,
    };
    BUSTER_TEST(arguments, advanced_evex_masked_forms.diagnostic_count == 0 &&
                               advanced_evex_masked_forms.bytes.length == sizeof(expected_advanced_evex_masked_forms) &&
                               memcmp(advanced_evex_masked_forms.bytes.pointer, expected_advanced_evex_masked_forms,
                                      sizeof(expected_advanced_evex_masked_forms)) == 0);

    AssemblyEncodeResult advanced_evex_masked_forms_att = assembly_encode(
        arguments->arena,
        S8("vcmpps $7, %zmm3, %zmm2, %k1 {%k2}\n"
           "vcmpps $7, 64(%rax), %zmm2, %k1 {%k2}\n"
           "vmovdqa64 %zmm2, (%rax) {%k1}\n"
           "vmovdqa64 %zmm2, 64(%rax,%r8,4) {%k3}\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, advanced_evex_masked_forms_att.diagnostic_count == 0 &&
                               advanced_evex_masked_forms_att.bytes.length == sizeof(expected_advanced_evex_masked_forms) &&
                               memcmp(advanced_evex_masked_forms_att.bytes.pointer, expected_advanced_evex_masked_forms,
                                      sizeof(expected_advanced_evex_masked_forms)) == 0);

    AssemblyEncodeResult advanced_evex_integer_compare_masks = assembly_encode(
        arguments->arena,
        S8("vpcmpeqb k1, zmm2, zmm3\n"
           "vpcmpgtq k1, zmm2, zmm3\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_advanced_evex_integer_compare_masks[] = {
        0x62, 0xf1, 0x6d, 0x48, 0x74, 0xcb,
        0x62, 0xf2, 0xed, 0x48, 0x37, 0xcb,
    };
    BUSTER_TEST(arguments, advanced_evex_integer_compare_masks.diagnostic_count == 0 &&
                               advanced_evex_integer_compare_masks.bytes.length == sizeof(expected_advanced_evex_integer_compare_masks) &&
                               memcmp(advanced_evex_integer_compare_masks.bytes.pointer,
                                      expected_advanced_evex_integer_compare_masks,
                                      sizeof(expected_advanced_evex_integer_compare_masks)) == 0);

    AssemblyEncodeResult advanced_evex_integer_compare_masks_att = assembly_encode(
        arguments->arena,
        S8("vpcmpeqb %zmm3, %zmm2, %k1\n"
           "vpcmpgtq %zmm3, %zmm2, %k1\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, advanced_evex_integer_compare_masks_att.diagnostic_count == 0 &&
                               advanced_evex_integer_compare_masks_att.bytes.length == sizeof(expected_advanced_evex_integer_compare_masks) &&
                               memcmp(advanced_evex_integer_compare_masks_att.bytes.pointer,
                                      expected_advanced_evex_integer_compare_masks,
                                      sizeof(expected_advanced_evex_integer_compare_masks)) == 0);

    AssemblyEncodeResult advanced_evex_low_mask_compare = assembly_encode(
        arguments->arena,
        S8("vpcmpeqd k1, xmm2, xmm3\n"
           "vpcmpgtq k1, ymm2, ymm3\n"
           "vpcmpw k1, zmm2, zmm3, 7\n"
           "vxorps zmm0, zmm1, dword ptr [rax]{1to16}\n"
           "vxorpd zmm0, zmm1, qword ptr [rax]{1to8}\n"
           "vpaddd zmm0, zmm1, dword ptr [rax]{1to16}\n"
           "vpcmpgtq k1, zmm2, qword ptr [rax]{1to8}\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_advanced_evex_low_mask_compare[] = {
        0x62, 0xf1, 0x6d, 0x08, 0x76, 0xcb,
        0x62, 0xf2, 0xed, 0x28, 0x37, 0xcb,
        0x62, 0xf3, 0xed, 0x48, 0x3f, 0xcb, 0x07,
        0x62, 0xf1, 0x74, 0x58, 0x57, 0x00,
        0x62, 0xf1, 0xf5, 0x58, 0x57, 0x00,
        0x62, 0xf1, 0x75, 0x58, 0xfe, 0x00,
        0x62, 0xf2, 0xed, 0x58, 0x37, 0x08,
    };
    BUSTER_TEST(arguments, advanced_evex_low_mask_compare.diagnostic_count == 0 &&
                               advanced_evex_low_mask_compare.bytes.length == sizeof(expected_advanced_evex_low_mask_compare) &&
                               memcmp(advanced_evex_low_mask_compare.bytes.pointer, expected_advanced_evex_low_mask_compare,
                                      sizeof(expected_advanced_evex_low_mask_compare)) == 0);

    AssemblyEncodeResult advanced_evex_low_mask_compare_att = assembly_encode(
        arguments->arena,
        S8("vpcmpeqd %xmm3, %xmm2, %k1\n"
           "vpcmpgtq %ymm3, %ymm2, %k1\n"
           "vpcmpw $7, %zmm3, %zmm2, %k1\n"
           "vxorps (%rax){1to16}, %zmm1, %zmm0\n"
           "vxorpd (%rax){1to8}, %zmm1, %zmm0\n"
           "vpaddd (%rax){1to16}, %zmm1, %zmm0\n"
           "vpcmpgtq (%rax){1to8}, %zmm2, %k1\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, advanced_evex_low_mask_compare_att.diagnostic_count == 0 &&
                               advanced_evex_low_mask_compare_att.bytes.length == sizeof(expected_advanced_evex_low_mask_compare) &&
                               memcmp(advanced_evex_low_mask_compare_att.bytes.pointer, expected_advanced_evex_low_mask_compare,
                                      sizeof(expected_advanced_evex_low_mask_compare)) == 0);

    AssemblyEncodeResult advanced_evex_canonical_decorators = assembly_encode(
        arguments->arena,
        S8("vcmpps k1, zmm2, zmm3, {sae}, 7\n"
           "vcmppd k1, zmm2, zmm3, {sae}, 7\n"
           "vrndscaleps zmm0, zmm1, {sae}, 4\n"
           "vrndscalepd zmm0, zmm1, {sae}, 4\n"
           "vaddps zmm0, zmm1, zmm2, {rn-sae}\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_advanced_evex_canonical_decorators[] = {
        0x62, 0xf1, 0x6c, 0x18, 0xc2, 0xcb, 0x07,
        0x62, 0xf1, 0xed, 0x18, 0xc2, 0xcb, 0x07,
        0x62, 0xf3, 0x7d, 0x18, 0x08, 0xc1, 0x04,
        0x62, 0xf3, 0xfd, 0x18, 0x09, 0xc1, 0x04,
        0x62, 0xf1, 0x74, 0x18, 0x58, 0xc2,
    };
    BUSTER_TEST(arguments, advanced_evex_canonical_decorators.diagnostic_count == 0 &&
                               advanced_evex_canonical_decorators.bytes.length == sizeof(expected_advanced_evex_canonical_decorators) &&
                               memcmp(advanced_evex_canonical_decorators.bytes.pointer, expected_advanced_evex_canonical_decorators,
                                      sizeof(expected_advanced_evex_canonical_decorators)) == 0);

    AssemblyEncodeResult advanced_evex_canonical_decorators_att = assembly_encode(
        arguments->arena,
        S8("vcmpps $7, {sae}, %zmm3, %zmm2, %k1\n"
           "vcmppd $7, {sae}, %zmm3, %zmm2, %k1\n"
           "vrndscaleps $4, {sae}, %zmm1, %zmm0\n"
           "vrndscalepd $4, {sae}, %zmm1, %zmm0\n"
           "vaddps {rn-sae}, %zmm2, %zmm1, %zmm0\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, advanced_evex_canonical_decorators_att.diagnostic_count == 0 &&
                               advanced_evex_canonical_decorators_att.bytes.length == sizeof(expected_advanced_evex_canonical_decorators) &&
                               memcmp(advanced_evex_canonical_decorators_att.bytes.pointer, expected_advanced_evex_canonical_decorators,
                                      sizeof(expected_advanced_evex_canonical_decorators)) == 0);

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
           "{nf} add r16d, r17d\n"
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

    AssemblyEncodeResult advanced_apx_ndd_memory_immediate = assembly_encode(
        arguments->arena,
        S8("add r16d, r17d, dword ptr [r18]\n"
           "add r16d, dword ptr [r18], 5\n"
           "add r16d, r17d, 5\n"
           "{nf} add r16d, 5\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_advanced_apx_ndd_memory_immediate[] = {
        0x62, 0xec, 0x7c, 0x10, 0x03, 0x0a,
        0x62, 0xfc, 0x7c, 0x10, 0x83, 0x02, 0x05,
        0x62, 0xfc, 0x7c, 0x10, 0x83, 0xc1, 0x05,
        0x62, 0xfc, 0x7c, 0x0c, 0x83, 0xc0, 0x05,
    };
    BUSTER_TEST(arguments, advanced_apx_ndd_memory_immediate.diagnostic_count == 0 &&
                               advanced_apx_ndd_memory_immediate.bytes.length == sizeof(expected_advanced_apx_ndd_memory_immediate) &&
                               memcmp(advanced_apx_ndd_memory_immediate.bytes.pointer, expected_advanced_apx_ndd_memory_immediate,
                                      sizeof(expected_advanced_apx_ndd_memory_immediate)) == 0);

    AssemblyEncodeResult advanced_apx_ndd_egpr_sib = assembly_encode(
        arguments->arena,
        S8("add r16d, r17d, dword ptr [r24+r25*4+64]\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_advanced_apx_ndd_egpr_sib[] = {0x62, 0x8c, 0x78, 0x10, 0x03, 0x4c, 0x88, 0x40};
    BUSTER_TEST(arguments, advanced_apx_ndd_egpr_sib.diagnostic_count == 0 &&
                               advanced_apx_ndd_egpr_sib.bytes.length == sizeof(expected_advanced_apx_ndd_egpr_sib) &&
                               memcmp(advanced_apx_ndd_egpr_sib.bytes.pointer, expected_advanced_apx_ndd_egpr_sib,
                                      sizeof(expected_advanced_apx_ndd_egpr_sib)) == 0);

    AssemblyEncodeResult advanced_apx_ndd_immediate_sib = assembly_encode(
        arguments->arena,
        S8("add r16d, dword ptr [r24+r25*4+64], 5\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_advanced_apx_ndd_immediate_sib[] = {
        0x62, 0x9c, 0x78, 0x10, 0x83, 0x44, 0x88, 0x40, 0x05,
    };
    BUSTER_TEST(arguments, advanced_apx_ndd_immediate_sib.diagnostic_count == 0 &&
                               advanced_apx_ndd_immediate_sib.bytes.length == sizeof(expected_advanced_apx_ndd_immediate_sib) &&
                               memcmp(advanced_apx_ndd_immediate_sib.bytes.pointer, expected_advanced_apx_ndd_immediate_sib,
                                      sizeof(expected_advanced_apx_ndd_immediate_sib)) == 0);

    AssemblyEncodeResult advanced_apx_ndd_immediate_sib_att = assembly_encode(
        arguments->arena,
        S8("addl $5, 64(%r24,%r25,4), %r16d\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, advanced_apx_ndd_immediate_sib_att.diagnostic_count == 0 &&
                               advanced_apx_ndd_immediate_sib_att.bytes.length == sizeof(expected_advanced_apx_ndd_immediate_sib) &&
                               memcmp(advanced_apx_ndd_immediate_sib_att.bytes.pointer, expected_advanced_apx_ndd_immediate_sib,
                                      sizeof(expected_advanced_apx_ndd_immediate_sib)) == 0);

    AssemblyEncodeResult advanced_apx_ndd_relocation = assembly_encode(
        arguments->arena,
        S8("add r16d, r17d, dword ptr [rip+apx_ndd_external]\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_advanced_apx_ndd_relocation[] = {0x62, 0xe4, 0x7c, 0x10, 0x03, 0x0d, 0x00, 0x00, 0x00, 0x00};
    BUSTER_TEST(arguments, advanced_apx_ndd_relocation.diagnostic_count == 0);
    BUSTER_TEST(arguments, advanced_apx_ndd_relocation.bytes.length == sizeof(expected_advanced_apx_ndd_relocation));
    BUSTER_TEST(arguments, memcmp(advanced_apx_ndd_relocation.bytes.pointer, expected_advanced_apx_ndd_relocation,
                                  sizeof(expected_advanced_apx_ndd_relocation)) == 0);
    BUSTER_TEST(arguments, advanced_apx_ndd_relocation.relocation_count == 1);
    BUSTER_TEST(arguments, advanced_apx_ndd_relocation.relocations[0].offset == 6);
    BUSTER_TEST(arguments, advanced_apx_ndd_relocation.relocations[0].addend == -4);
    BUSTER_TEST(arguments, advanced_apx_ndd_relocation.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC32);

    AssemblyEncodeResult advanced_apx_ndd_immediate_relocation = assembly_encode(
        arguments->arena,
        S8("add r16d, dword ptr [rip+apx_ndd_immediate_external], 5\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_advanced_apx_ndd_immediate_relocation[] = {
        0x62, 0xf4, 0x7c, 0x10, 0x83, 0x05, 0x00, 0x00, 0x00, 0x00, 0x05,
    };
    BUSTER_TEST(arguments, advanced_apx_ndd_immediate_relocation.diagnostic_count == 0 &&
                               advanced_apx_ndd_immediate_relocation.bytes.length == sizeof(expected_advanced_apx_ndd_immediate_relocation) &&
                               memcmp(advanced_apx_ndd_immediate_relocation.bytes.pointer,
                                      expected_advanced_apx_ndd_immediate_relocation,
                                      sizeof(expected_advanced_apx_ndd_immediate_relocation)) == 0 &&
                               advanced_apx_ndd_immediate_relocation.relocation_count == 1 &&
                               advanced_apx_ndd_immediate_relocation.relocations[0].offset == 6 &&
                               advanced_apx_ndd_immediate_relocation.relocations[0].addend == -5 &&
                               advanced_apx_ndd_immediate_relocation.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC32);

    AssemblyEncodeResult advanced_apx_ndd_immediate_relocation_att = assembly_encode(
        arguments->arena,
        S8("addl $5, apx_ndd_immediate_att_external(%rip), %r16d\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, advanced_apx_ndd_immediate_relocation_att.diagnostic_count == 0 &&
                               advanced_apx_ndd_immediate_relocation_att.bytes.length ==
                                   sizeof(expected_advanced_apx_ndd_immediate_relocation) &&
                               memcmp(advanced_apx_ndd_immediate_relocation_att.bytes.pointer,
                                      expected_advanced_apx_ndd_immediate_relocation,
                                      sizeof(expected_advanced_apx_ndd_immediate_relocation)) == 0 &&
                               advanced_apx_ndd_immediate_relocation_att.relocation_count == 1 &&
                               advanced_apx_ndd_immediate_relocation_att.relocations[0].offset == 6 &&
                               advanced_apx_ndd_immediate_relocation_att.relocations[0].addend == -5 &&
                               advanced_apx_ndd_immediate_relocation_att.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC32);

    AssemblyEncodeResult advanced_apx_nf_memory_immediate_relocation = assembly_encode(
        arguments->arena,
        S8("{nf} add dword ptr [rip+apx_nf_external], 5\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_advanced_apx_nf_memory_immediate_relocation[] = {
        0x62, 0xf4, 0x7c, 0x0c, 0x83, 0x05, 0x00, 0x00, 0x00, 0x00, 0x05,
    };
    BUSTER_TEST(arguments, advanced_apx_nf_memory_immediate_relocation.diagnostic_count == 0 &&
                               advanced_apx_nf_memory_immediate_relocation.bytes.length ==
                                   sizeof(expected_advanced_apx_nf_memory_immediate_relocation) &&
                               memcmp(advanced_apx_nf_memory_immediate_relocation.bytes.pointer,
                                      expected_advanced_apx_nf_memory_immediate_relocation,
                                      sizeof(expected_advanced_apx_nf_memory_immediate_relocation)) == 0 &&
                               advanced_apx_nf_memory_immediate_relocation.relocation_count == 1 &&
                               advanced_apx_nf_memory_immediate_relocation.relocations[0].offset == 6 &&
                               advanced_apx_nf_memory_immediate_relocation.relocations[0].addend == -5 &&
                               advanced_apx_nf_memory_immediate_relocation.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC32);

    AssemblyEncodeResult advanced_apx_ndd_memory_immediate_att = assembly_encode(
        arguments->arena,
        S8("addl (%r18), %r17d, %r16d\n"
           "addl $5, (%r18), %r16d\n"
           "addl $5, %r17d, %r16d\n"
           "{nf} addl $5, %r16d\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, advanced_apx_ndd_memory_immediate_att.diagnostic_count == 0 &&
                               advanced_apx_ndd_memory_immediate_att.bytes.length == sizeof(expected_advanced_apx_ndd_memory_immediate) &&
                               memcmp(advanced_apx_ndd_memory_immediate_att.bytes.pointer, expected_advanced_apx_ndd_memory_immediate,
                                      sizeof(expected_advanced_apx_ndd_memory_immediate)) == 0);

    AssemblyEncodeResult invalid_apx_att_immediate_order = assembly_encode(
        arguments->arena,
        S8("addl %r17d, $5, %r16d\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, invalid_apx_att_immediate_order.diagnostic_count == 1 &&
                               invalid_apx_att_immediate_order.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);

    AssemblyEncodeResult advanced_apx_ndd_carry = assembly_encode(
        arguments->arena,
        S8("adc r16d, r17d, r18d\n"
           "sbb r16d, r17d, 5\n"
           "adc r16d, r17d, dword ptr [r18]\n"
           "sbb r16d, dword ptr [r18], r17d\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_advanced_apx_ndd_carry[] = {
        0x62, 0xec, 0x7c, 0x10, 0x11, 0xd1,
        0x62, 0xfc, 0x7c, 0x10, 0x83, 0xd9, 0x05,
        0x62, 0xec, 0x7c, 0x10, 0x13, 0x0a,
        0x62, 0xec, 0x7c, 0x10, 0x19, 0x0a,
    };
    BUSTER_TEST(arguments, advanced_apx_ndd_carry.diagnostic_count == 0 &&
                               advanced_apx_ndd_carry.bytes.length == sizeof(expected_advanced_apx_ndd_carry) &&
                               memcmp(advanced_apx_ndd_carry.bytes.pointer, expected_advanced_apx_ndd_carry,
                                      sizeof(expected_advanced_apx_ndd_carry)) == 0);

    AssemblyEncodeResult advanced_apx_ndd_carry_att = assembly_encode(
        arguments->arena,
        S8("adcl %r18d, %r17d, %r16d\n"
           "sbbl $5, %r17d, %r16d\n"
           "adcl (%r18), %r17d, %r16d\n"
           "sbbl %r17d, (%r18), %r16d\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, advanced_apx_ndd_carry_att.diagnostic_count == 0 &&
                               advanced_apx_ndd_carry_att.bytes.length == sizeof(expected_advanced_apx_ndd_carry) &&
                               memcmp(advanced_apx_ndd_carry_att.bytes.pointer, expected_advanced_apx_ndd_carry,
                                      sizeof(expected_advanced_apx_ndd_carry)) == 0);

    AssemblyEncodeResult advanced_apx_ndd_nf = assembly_encode(
        arguments->arena,
        S8("{nf} add r16d, r17d, r18d\n"
           "{nf} sub r24, r25, qword ptr [r26+r27*8+64]\n"
           "{nf} xor r16b, r17b, 255\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_advanced_apx_ndd_nf[] = {
        0x62, 0xec, 0x7c, 0x14, 0x01, 0xd1,
        0x62, 0x0c, 0xb8, 0x14, 0x2b, 0x4c, 0xda, 0x40,
        0x62, 0xfc, 0x7c, 0x14, 0x80, 0xf1, 0xff,
    };
    BUSTER_TEST(arguments, advanced_apx_ndd_nf.diagnostic_count == 0 &&
                               advanced_apx_ndd_nf.bytes.length == sizeof(expected_advanced_apx_ndd_nf) &&
                               memcmp(advanced_apx_ndd_nf.bytes.pointer, expected_advanced_apx_ndd_nf,
                                      sizeof(expected_advanced_apx_ndd_nf)) == 0);

    AssemblyEncodeResult advanced_apx_ndd_nf_att = assembly_encode(
        arguments->arena,
        S8("{nf} addl %r18d, %r17d, %r16d\n"
           "{nf} subq 64(%r26,%r27,8), %r25, %r24\n"
           "{nf} xorb $255, %r17b, %r16b\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, advanced_apx_ndd_nf_att.diagnostic_count == 0 &&
                               advanced_apx_ndd_nf_att.bytes.length == sizeof(expected_advanced_apx_ndd_nf) &&
                               memcmp(advanced_apx_ndd_nf_att.bytes.pointer, expected_advanced_apx_ndd_nf,
                                      sizeof(expected_advanced_apx_ndd_nf)) == 0);

    AssemblyEncodeResult invalid_apx_nf_carry = assembly_encode(
        arguments->arena,
        S8("{nf} adc r16d, r17d\n"
           "{nf} sbb r16d, r17d\n"
           "{nf} adc r16d, r17d, r18d\n"
           "{nf} sbb r16d, r17d, r18d\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_apx_nf_carry.diagnostic_count == 4);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_apx_nf_carry.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, invalid_apx_nf_carry.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }

    AssemblyEncodeResult invalid_apx_nf_carry_att = assembly_encode(
        arguments->arena,
        S8("{nf} adcl %r17d, %r16d\n"
           "{nf} sbbl %r17d, %r16d\n"
           "{nf} adcl %r18d, %r17d, %r16d\n"
           "{nf} sbbl %r18d, %r17d, %r16d\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, invalid_apx_nf_carry_att.diagnostic_count == 4);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_apx_nf_carry_att.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, invalid_apx_nf_carry_att.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }

    AssemblyEncodeResult advanced_apx_nf_memory_immediate_relocation_att = assembly_encode(
        arguments->arena,
        S8("{nf} addl $5, apx_nf_att_external(%rip)\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, advanced_apx_nf_memory_immediate_relocation_att.diagnostic_count == 0 &&
                               advanced_apx_nf_memory_immediate_relocation_att.bytes.length ==
                                   sizeof(expected_advanced_apx_nf_memory_immediate_relocation) &&
                               memcmp(advanced_apx_nf_memory_immediate_relocation_att.bytes.pointer,
                                      expected_advanced_apx_nf_memory_immediate_relocation,
                                      sizeof(expected_advanced_apx_nf_memory_immediate_relocation)) == 0 &&
                               advanced_apx_nf_memory_immediate_relocation_att.relocation_count == 1 &&
                               advanced_apx_nf_memory_immediate_relocation_att.relocations[0].offset == 6 &&
                               advanced_apx_nf_memory_immediate_relocation_att.relocations[0].addend == -5 &&
                               advanced_apx_nf_memory_immediate_relocation_att.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC32);

    AssemblyEncodeResult advanced_apx_nf_shift_relocation = assembly_encode(
        arguments->arena,
        S8("{nf} shl dword ptr [rip+apx_nf_shift_external], 5\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_advanced_apx_nf_shift_relocation[] = {
        0x62, 0xf4, 0x7c, 0x0c, 0xc1, 0x25, 0x00, 0x00, 0x00, 0x00, 0x05,
    };
    BUSTER_TEST(arguments, advanced_apx_nf_shift_relocation.diagnostic_count == 0 &&
                               advanced_apx_nf_shift_relocation.bytes.length == sizeof(expected_advanced_apx_nf_shift_relocation) &&
                               memcmp(advanced_apx_nf_shift_relocation.bytes.pointer, expected_advanced_apx_nf_shift_relocation,
                                      sizeof(expected_advanced_apx_nf_shift_relocation)) == 0 &&
                               advanced_apx_nf_shift_relocation.relocation_count == 1 &&
                               advanced_apx_nf_shift_relocation.relocations[0].offset == 6 &&
                               advanced_apx_nf_shift_relocation.relocations[0].addend == -5 &&
                               advanced_apx_nf_shift_relocation.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC32);

    AssemblyEncodeResult advanced_apx_nf_shift_relocation_att = assembly_encode(
        arguments->arena,
        S8("{nf} shll $5, apx_nf_shift_att_external(%rip)\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, advanced_apx_nf_shift_relocation_att.diagnostic_count == 0 &&
                               advanced_apx_nf_shift_relocation_att.bytes.length == sizeof(expected_advanced_apx_nf_shift_relocation) &&
                               memcmp(advanced_apx_nf_shift_relocation_att.bytes.pointer, expected_advanced_apx_nf_shift_relocation,
                                      sizeof(expected_advanced_apx_nf_shift_relocation)) == 0 &&
                               advanced_apx_nf_shift_relocation_att.relocation_count == 1 &&
                               advanced_apx_nf_shift_relocation_att.relocations[0].offset == 6 &&
                               advanced_apx_nf_shift_relocation_att.relocations[0].addend == -5 &&
                               advanced_apx_nf_shift_relocation_att.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC32);

    AssemblyEncodeResult invalid_apx_ndd_memory_immediate = assembly_encode(
        arguments->arena,
        S8("add r16d, dword ptr [r17], dword ptr [r18]\n"
           "add r16d, 4294967296\n"
           "{nf} add dword ptr [r16], 4294967296\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_apx_ndd_memory_immediate.diagnostic_count == 3);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_apx_ndd_memory_immediate.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, invalid_apx_ndd_memory_immediate.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }

    AssemblyEncodeResult advanced_vrndscale_signed_immediate = assembly_encode(
        arguments->arena,
        S8("vrndscaleps zmm0, zmm1, -1\n"
           "vrndscaleps zmm0, zmm1, -128\n"
           "vrndscaleps zmm0, zmm1, 255\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_advanced_vrndscale_signed_immediate[] = {
        0x62, 0xf3, 0x7d, 0x48, 0x08, 0xc1, 0xff,
        0x62, 0xf3, 0x7d, 0x48, 0x08, 0xc1, 0x80,
        0x62, 0xf3, 0x7d, 0x48, 0x08, 0xc1, 0xff,
    };
    BUSTER_TEST(arguments, advanced_vrndscale_signed_immediate.diagnostic_count == 0 &&
                               advanced_vrndscale_signed_immediate.bytes.length == sizeof(expected_advanced_vrndscale_signed_immediate) &&
                               memcmp(advanced_vrndscale_signed_immediate.bytes.pointer, expected_advanced_vrndscale_signed_immediate,
                                      sizeof(expected_advanced_vrndscale_signed_immediate)) == 0);

    AssemblyEncodeResult advanced_vrndscale_signed_immediate_att = assembly_encode(
        arguments->arena,
        S8("vrndscaleps $-1, %zmm1, %zmm0\n"
           "vrndscaleps $-128, %zmm1, %zmm0\n"
           "vrndscaleps $255, %zmm1, %zmm0\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, advanced_vrndscale_signed_immediate_att.diagnostic_count == 0 &&
                               advanced_vrndscale_signed_immediate_att.bytes.length == sizeof(expected_advanced_vrndscale_signed_immediate) &&
                               memcmp(advanced_vrndscale_signed_immediate_att.bytes.pointer, expected_advanced_vrndscale_signed_immediate,
                                      sizeof(expected_advanced_vrndscale_signed_immediate)) == 0);

    AssemblyEncodeResult invalid_vrndscale_signed_immediate = assembly_encode(
        arguments->arena,
        S8("vrndscaleps zmm0, zmm1, -129\n"
           "vrndscaleps zmm0, zmm1, 256\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_vrndscale_signed_immediate.diagnostic_count == 2);
    AssemblyEncodeResult invalid_vrndscale_signed_immediate_att = assembly_encode(
        arguments->arena,
        S8("vrndscaleps $-129, %zmm1, %zmm0\n"
           "vrndscaleps $256, %zmm1, %zmm0\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, invalid_vrndscale_signed_immediate_att.diagnostic_count == 2);

    AssemblyEncodeResult invalid_nf_aliases = assembly_encode(
        arguments->arena,
        S8("{nf} add{nf} r16d, r17d\n"
           "{nf} addnf r16d, r17d\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_nf_aliases.diagnostic_count == 2);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_nf_aliases.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, invalid_nf_aliases.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_UNKNOWN_INSTRUCTION);
    }
    AssemblyEncodeResult invalid_nf_aliases_att = assembly_encode(
        arguments->arena,
        S8("{nf} addl{nf} %r17d, %r16d\n"
           "{nf} addnfl %r17d, %r16d\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, invalid_nf_aliases_att.diagnostic_count == 2);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_nf_aliases_att.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, invalid_nf_aliases_att.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_UNKNOWN_INSTRUCTION);
    }

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

    AssemblyEncodeResult advanced_apx_rex2_families = assembly_encode(
        arguments->arena,
        S8("lea r16, [r17+r18*4+64]\n"
           "call r16\n"
           "jmp qword ptr [r19]\n"
           "movaps xmm0, [r16]\n"
           "movdqa xmm1, [r17]\n"
           "movdqu xmm0, [r16]\n"
           "addss xmm2, dword ptr [r18]\n"
           "addsd xmm3, qword ptr [r19]\n"
           "adc r16d, r17d\n"
           "sbb r18d, dword ptr [r19]\n"
           "imul r20d, r21d\n"
           "shl r22d, 3\n"
           "shl r23d, cl\n"
           "mov r16d, 5\n"
           "mov qword ptr [r16], 5\n"
           "add r16d, 5\n"
           "imul r16d, r17d, 5\n"
           "imul r16d, dword ptr [r18], 5\n"
           "imul r24\n"
           "imul qword ptr [r25]\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_apx_rex2_families[] = {
        0xd5, 0x78, 0x8d, 0x44, 0x91, 0x40,
        0xd5, 0x10, 0xff, 0xd0,
        0xd5, 0x10, 0xff, 0x23,
        0xd5, 0x90, 0x28, 0x00,
        0x66, 0xd5, 0x90, 0x6f, 0x09,
        0xf3, 0xd5, 0x90, 0x6f, 0x00,
        0xf3, 0xd5, 0x90, 0x58, 0x12,
        0xf2, 0xd5, 0x90, 0x58, 0x1b,
        0xd5, 0x50, 0x11, 0xc8,
        0xd5, 0x50, 0x1b, 0x13,
        0xd5, 0xd0, 0xaf, 0xe5,
        0xd5, 0x10, 0xc1, 0xe6, 0x03,
        0xd5, 0x10, 0xd3, 0xe7,
        0xd5, 0x10, 0xb8, 0x05, 0x00, 0x00, 0x00,
        0xd5, 0x18, 0xc7, 0x00, 0x05, 0x00, 0x00, 0x00,
        0xd5, 0x10, 0x83, 0xc0, 0x05,
        0xd5, 0x50, 0x6b, 0xc1, 0x05,
        0xd5, 0x50, 0x6b, 0x02, 0x05,
        0xd5, 0x19, 0xf7, 0xe8,
        0xd5, 0x19, 0xf7, 0x29,
    };
    BUSTER_TEST(arguments, advanced_apx_rex2_families.diagnostic_count == 0 &&
                               advanced_apx_rex2_families.bytes.length == sizeof(expected_apx_rex2_families) &&
                               memcmp(advanced_apx_rex2_families.bytes.pointer, expected_apx_rex2_families,
                                      sizeof(expected_apx_rex2_families)) == 0);

    AssemblyEncodeResult advanced_apx_rex2_families_att = assembly_encode(
        arguments->arena,
        S8("leaq 64(%r17,%r18,4), %r16\n"
           "call *%r16\n"
           "jmp *(%r19)\n"
           "movaps (%r16), %xmm0\n"
           "movdqa (%r17), %xmm1\n"
           "movdqu (%r16), %xmm0\n"
           "addss (%r18), %xmm2\n"
           "addsd (%r19), %xmm3\n"
           "adcl %r17d, %r16d\n"
           "sbbl (%r19), %r18d\n"
           "imull %r21d, %r20d\n"
           "shll $3, %r22d\n"
           "shll %cl, %r23d\n"
           "movl $5, %r16d\n"
           "movq $5, (%r16)\n"
           "addl $5, %r16d\n"
           "imull $5, %r17d, %r16d\n"
           "imull $5, (%r18), %r16d\n"
           "imulq %r24\n"
           "imulq (%r25)\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, advanced_apx_rex2_families_att.diagnostic_count == 0 &&
                               advanced_apx_rex2_families_att.bytes.length == sizeof(expected_apx_rex2_families) &&
                               memcmp(advanced_apx_rex2_families_att.bytes.pointer, expected_apx_rex2_families,
                                      sizeof(expected_apx_rex2_families)) == 0);

    AssemblyEncodeResult advanced_apx_rex2_unary = assembly_encode(
        arguments->arena,
        S8("inc r16b\n"
           "dec byte ptr [r17]\n"
           "neg r16b\n"
           "not byte ptr [r17]\n"
           "inc r16d\n"
           "dec dword ptr [r17]\n"
           "neg r16d\n"
           "not dword ptr [r17]\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_apx_rex2_unary[] = {
        0xd5, 0x10, 0xfe, 0xc0,
        0xd5, 0x10, 0xfe, 0x09,
        0xd5, 0x10, 0xf6, 0xd8,
        0xd5, 0x10, 0xf6, 0x11,
        0xd5, 0x10, 0xff, 0xc0,
        0xd5, 0x10, 0xff, 0x09,
        0xd5, 0x10, 0xf7, 0xd8,
        0xd5, 0x10, 0xf7, 0x11,
    };
    BUSTER_TEST(arguments, advanced_apx_rex2_unary.diagnostic_count == 0 &&
                               advanced_apx_rex2_unary.bytes.length == sizeof(expected_apx_rex2_unary) &&
                               memcmp(advanced_apx_rex2_unary.bytes.pointer, expected_apx_rex2_unary,
                                      sizeof(expected_apx_rex2_unary)) == 0);

    AssemblyEncodeResult advanced_apx_rex2_unary_att = assembly_encode(
        arguments->arena,
        S8("incb %r16b\n"
           "decb (%r17)\n"
           "negb %r16b\n"
           "notb (%r17)\n"
           "incl %r16d\n"
           "decl (%r17)\n"
           "negl %r16d\n"
           "notl (%r17)\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, advanced_apx_rex2_unary_att.diagnostic_count == 0 &&
                               advanced_apx_rex2_unary_att.bytes.length == sizeof(expected_apx_rex2_unary) &&
                               memcmp(advanced_apx_rex2_unary_att.bytes.pointer, expected_apx_rex2_unary,
                                      sizeof(expected_apx_rex2_unary)) == 0);

    AssemblyEncodeResult advanced_apx_rex2_byte_imul = assembly_encode(
        arguments->arena, S8("imul r16b\nimul byte ptr [r17]\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_apx_rex2_byte_imul[] = {
        0xd5, 0x10, 0xf6, 0xe8,
        0xd5, 0x10, 0xf6, 0x29,
    };
    BUSTER_TEST(arguments, advanced_apx_rex2_byte_imul.diagnostic_count == 0 &&
                               advanced_apx_rex2_byte_imul.bytes.length == sizeof(expected_apx_rex2_byte_imul) &&
                               memcmp(advanced_apx_rex2_byte_imul.bytes.pointer, expected_apx_rex2_byte_imul,
                                      sizeof(expected_apx_rex2_byte_imul)) == 0);

    AssemblyEncodeResult advanced_apx_rex2_byte_imul_att = assembly_encode(
        arguments->arena, S8("imulb %r16b\nimulb (%r17)\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, advanced_apx_rex2_byte_imul_att.diagnostic_count == 0 &&
                               advanced_apx_rex2_byte_imul_att.bytes.length == sizeof(expected_apx_rex2_byte_imul) &&
                               memcmp(advanced_apx_rex2_byte_imul_att.bytes.pointer, expected_apx_rex2_byte_imul,
                                      sizeof(expected_apx_rex2_byte_imul)) == 0);

    AssemblyEncodeResult invalid_apx_rex2_byte_imul = assembly_encode(
        arguments->arena,
        S8("imul r16b, r17b\n"
           "imul r16b, r17b, 5\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_apx_rex2_byte_imul.diagnostic_count == 2);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_apx_rex2_byte_imul.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, invalid_apx_rex2_byte_imul.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }

    AssemblyEncodeResult invalid_apx_rex2_byte_imul_att = assembly_encode(
        arguments->arena,
        S8("imulb %r17b, %r16b\n"
           "imulb $5, %r17b, %r16b\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, invalid_apx_rex2_byte_imul_att.diagnostic_count == 2);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_apx_rex2_byte_imul_att.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments,
                    invalid_apx_rex2_byte_imul_att.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }

    AssemblyEncodeResult advanced_apx_rex2_mov_immediate = assembly_encode(
        arguments->arena,
        S8("mov r16b, 5\n"
           "mov r16w, 5\n"
           "mov r16d, 5\n"
           "mov r16, 5\n"
           "mov r16, 0x1122334455667788\n"
           "mov byte ptr [r16], 5\n"
           "mov word ptr [r16], 5\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_apx_rex2_mov_immediate[] = {
        0xd5, 0x10, 0xb0, 0x05,
        0x66, 0xd5, 0x10, 0xb8, 0x05, 0x00,
        0xd5, 0x10, 0xb8, 0x05, 0x00, 0x00, 0x00,
        0xd5, 0x18, 0xc7, 0xc0, 0x05, 0x00, 0x00, 0x00,
        0xd5, 0x18, 0xb8, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11,
        0xd5, 0x10, 0xc6, 0x00, 0x05,
        0x66, 0xd5, 0x10, 0xc7, 0x00, 0x05, 0x00,
    };
    BUSTER_TEST(arguments, advanced_apx_rex2_mov_immediate.diagnostic_count == 0 &&
                               advanced_apx_rex2_mov_immediate.bytes.length == sizeof(expected_apx_rex2_mov_immediate) &&
                               memcmp(advanced_apx_rex2_mov_immediate.bytes.pointer, expected_apx_rex2_mov_immediate,
                                      sizeof(expected_apx_rex2_mov_immediate)) == 0);

    AssemblyEncodeResult advanced_apx_rex2_mov_immediate_att = assembly_encode(
        arguments->arena,
        S8("movb $5, %r16b\n"
           "movw $5, %r16w\n"
           "movl $5, %r16d\n"
           "movq $5, %r16\n"
           "movq $0x1122334455667788, %r16\n"
           "movb $5, (%r16)\n"
           "movw $5, (%r16)\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, advanced_apx_rex2_mov_immediate_att.diagnostic_count == 0 &&
                               advanced_apx_rex2_mov_immediate_att.bytes.length == sizeof(expected_apx_rex2_mov_immediate) &&
                               memcmp(advanced_apx_rex2_mov_immediate_att.bytes.pointer, expected_apx_rex2_mov_immediate,
                                      sizeof(expected_apx_rex2_mov_immediate)) == 0);

    AssemblyEncodeResult advanced_apx_evex_ndd_families = assembly_encode(
        arguments->arena,
        S8("imul r16d, r17d, r18d\n"
           "{nf} imul r16d, r17d, r18d\n"
           "shl r16d, r17d, 3\n"
           "{nf} shl r16d, r17d, 3\n"
           "shl r16d, r17d, cl\n"
           "{nf} inc r16d\n"
           "{nf} dec dword ptr [r17]\n"
           "{nf} neg r18d\n"
           "{nf} imul r19d, r20d\n"
           "{nf} shl r21d, 3\n"
           "{nf} shr r22d, cl\n"
           "{nf} inc r16b\n"
           "{nf} dec byte ptr [r17]\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_apx_evex_ndd_families[] = {
        0x62, 0xec, 0x7c, 0x10, 0xaf, 0xca,
        0x62, 0xec, 0x7c, 0x14, 0xaf, 0xca,
        0x62, 0xfc, 0x7c, 0x10, 0xc1, 0xe1, 0x03,
        0x62, 0xfc, 0x7c, 0x14, 0xc1, 0xe1, 0x03,
        0x62, 0xfc, 0x7c, 0x10, 0xd3, 0xe1,
        0x62, 0xfc, 0x7c, 0x0c, 0xff, 0xc0,
        0x62, 0xfc, 0x7c, 0x0c, 0xff, 0x09,
        0x62, 0xfc, 0x7c, 0x0c, 0xf7, 0xda,
        0x62, 0xec, 0x7c, 0x0c, 0xaf, 0xdc,
        0x62, 0xfc, 0x7c, 0x0c, 0xc1, 0xe5, 0x03,
        0x62, 0xfc, 0x7c, 0x0c, 0xd3, 0xee,
        0x62, 0xfc, 0x7c, 0x0c, 0xfe, 0xc0,
        0x62, 0xfc, 0x7c, 0x0c, 0xfe, 0x09,
    };
    BUSTER_TEST(arguments, advanced_apx_evex_ndd_families.diagnostic_count == 0 &&
                               advanced_apx_evex_ndd_families.bytes.length == sizeof(expected_apx_evex_ndd_families) &&
                               memcmp(advanced_apx_evex_ndd_families.bytes.pointer, expected_apx_evex_ndd_families,
                                      sizeof(expected_apx_evex_ndd_families)) == 0);

    AssemblyEncodeResult advanced_apx_evex_ndd_families_att = assembly_encode(
        arguments->arena,
        S8("imull %r18d, %r17d, %r16d\n"
           "{nf} imull %r18d, %r17d, %r16d\n"
           "shll $3, %r17d, %r16d\n"
           "{nf} shll $3, %r17d, %r16d\n"
           "shll %cl, %r17d, %r16d\n"
           "{nf} incl %r16d\n"
           "{nf} decl (%r17)\n"
           "{nf} negl %r18d\n"
           "{nf} imull %r20d, %r19d\n"
           "{nf} shll $3, %r21d\n"
           "{nf} shrl %cl, %r22d\n"
           "{nf} incb %r16b\n"
           "{nf} decb (%r17)\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, advanced_apx_evex_ndd_families_att.diagnostic_count == 0 &&
                               advanced_apx_evex_ndd_families_att.bytes.length == sizeof(expected_apx_evex_ndd_families) &&
                               memcmp(advanced_apx_evex_ndd_families_att.bytes.pointer, expected_apx_evex_ndd_families,
                                      sizeof(expected_apx_evex_ndd_families)) == 0);

    AssemblyEncodeResult advanced_apx_nf_immediate_imul = assembly_encode(
        arguments->arena,
        S8("{nf} imul r16d, r17d, 5\n"
           "{nf} imul r16d, dword ptr [r18], 5\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_advanced_apx_nf_immediate_imul[] = {
        0x62, 0xec, 0x7c, 0x0c, 0x6b, 0xc1, 0x05,
        0x62, 0xec, 0x7c, 0x0c, 0x6b, 0x02, 0x05,
    };
    BUSTER_TEST(arguments, advanced_apx_nf_immediate_imul.diagnostic_count == 0 &&
                               advanced_apx_nf_immediate_imul.bytes.length == sizeof(expected_advanced_apx_nf_immediate_imul) &&
                               memcmp(advanced_apx_nf_immediate_imul.bytes.pointer, expected_advanced_apx_nf_immediate_imul,
                                      sizeof(expected_advanced_apx_nf_immediate_imul)) == 0);

    AssemblyEncodeResult advanced_apx_nf_immediate_imul_att = assembly_encode(
        arguments->arena,
        S8("{nf} imull $5, %r17d, %r16d\n"
           "{nf} imull $5, (%r18), %r16d\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, advanced_apx_nf_immediate_imul_att.diagnostic_count == 0 &&
                               advanced_apx_nf_immediate_imul_att.bytes.length == sizeof(expected_advanced_apx_nf_immediate_imul) &&
                               memcmp(advanced_apx_nf_immediate_imul_att.bytes.pointer, expected_advanced_apx_nf_immediate_imul,
                                      sizeof(expected_advanced_apx_nf_immediate_imul)) == 0);

    AssemblyEncodeResult advanced_apx_legacy_lock = assembly_encode(
        arguments->arena, S8("lock add qword ptr [r16], r17\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_advanced_apx_legacy_lock[] = {0xf0, 0xd5, 0x58, 0x01, 0x08};
    BUSTER_TEST(arguments, advanced_apx_legacy_lock.diagnostic_count == 0 &&
                               advanced_apx_legacy_lock.bytes.length == sizeof(expected_advanced_apx_legacy_lock) &&
                               memcmp(advanced_apx_legacy_lock.bytes.pointer, expected_advanced_apx_legacy_lock,
                                      sizeof(expected_advanced_apx_legacy_lock)) == 0);

    AssemblyEncodeResult advanced_apx_legacy_lock_att = assembly_encode(
        arguments->arena, S8("lock addq %r17, (%r16)\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, advanced_apx_legacy_lock_att.diagnostic_count == 0 &&
                               advanced_apx_legacy_lock_att.bytes.length == sizeof(expected_advanced_apx_legacy_lock) &&
                               memcmp(advanced_apx_legacy_lock_att.bytes.pointer, expected_advanced_apx_legacy_lock,
                                      sizeof(expected_advanced_apx_legacy_lock)) == 0);

    AssemblyEncodeResult advanced_apx_legacy_lock_immediate = assembly_encode(
        arguments->arena, S8("lock add dword ptr [r17], 5\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_advanced_apx_legacy_lock_immediate[] = {0xf0, 0xd5, 0x10, 0x83, 0x01, 0x05};
    BUSTER_TEST(arguments, advanced_apx_legacy_lock_immediate.diagnostic_count == 0 &&
                               advanced_apx_legacy_lock_immediate.bytes.length == sizeof(expected_advanced_apx_legacy_lock_immediate) &&
                               memcmp(advanced_apx_legacy_lock_immediate.bytes.pointer, expected_advanced_apx_legacy_lock_immediate,
                                      sizeof(expected_advanced_apx_legacy_lock_immediate)) == 0);

    AssemblyEncodeResult advanced_apx_legacy_lock_immediate_att = assembly_encode(
        arguments->arena, S8("lock addl $5, (%r17)\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, advanced_apx_legacy_lock_immediate_att.diagnostic_count == 0 &&
                               advanced_apx_legacy_lock_immediate_att.bytes.length == sizeof(expected_advanced_apx_legacy_lock_immediate) &&
                               memcmp(advanced_apx_legacy_lock_immediate_att.bytes.pointer, expected_advanced_apx_legacy_lock_immediate,
                                      sizeof(expected_advanced_apx_legacy_lock_immediate)) == 0);

    AssemblyEncodeResult invalid_apx_nf_lock = assembly_encode(
        arguments->arena, S8("lock {nf} add qword ptr [r16], r17\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_apx_nf_lock.diagnostic_count == 1 &&
                               invalid_apx_nf_lock.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);

    AssemblyEncodeResult invalid_apx_nf_lock_att = assembly_encode(
        arguments->arena, S8("lock {nf} addq %r17, (%r16)\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, invalid_apx_nf_lock_att.diagnostic_count == 1 &&
                               invalid_apx_nf_lock_att.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);

    AssemblyEncodeResult invalid_apx_legacy_lock = assembly_encode(
        arguments->arena,
        S8("lock mov qword ptr [r16], r17\n"
           "lock add r16, r17\n"
           "lock {nf} add r16, r17\n"
           "lock vaddps zmm0, zmm1, zmm2\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_apx_legacy_lock.diagnostic_count == 4);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_apx_legacy_lock.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, invalid_apx_legacy_lock.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }

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

    AssemblyEncodeResult invalid_amx_rip = assembly_encode(
        arguments->arena,
        S8("tileloadd tmm0, [rip+tile_external]\n"
           "tileloaddt1 tmm0, [rip+tile_external_t1]\n"
           "tilestored [rip+tile_external_store], tmm0\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_amx_rip.diagnostic_count == 3);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_amx_rip.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, invalid_amx_rip.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }

    AssemblyEncodeResult invalid_amx_rip_att = assembly_encode(
        arguments->arena,
        S8("tileloadd tile_att_external(%rip), %tmm0\n"
           "tileloaddt1 tile_att_external_t1(%rip), %tmm0\n"
           "tilestored %tmm0, tile_att_external_store(%rip)\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, invalid_amx_rip_att.diagnostic_count == 3);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_amx_rip_att.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, invalid_amx_rip_att.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }

    AssemblyEncodeResult invalid_amx_repeated_tiles = assembly_encode(
        arguments->arena,
        S8("tdpbf16ps tmm0, tmm0, tmm2\n"
           "tdpbssd tmm0, tmm1, tmm0\n"
           "tdpbsud tmm1, tmm1, tmm2\n"
           "tdpbusd tmm0, tmm1, tmm1\n"
           "tdpbuud tmm2, tmm1, tmm2\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_amx_repeated_tiles.diagnostic_count == 5);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_amx_repeated_tiles.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments,
                    invalid_amx_repeated_tiles.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }

    Target avx10_1_target = x86_target;
    avx10_1_target.cpu_features_explicit = true;
    avx10_1_target.cpu_features = target_cpu_features_from_array((TargetCpuFeature const[]){
        TARGET_CPU_FEATURE_X86_SSE2, TARGET_CPU_FEATURE_X86_AVX, TARGET_CPU_FEATURE_X86_AVX10_1}, 3);
    AssemblyEncodeResult avx10_1 = assembly_encode(arguments->arena, S8("vmovdqa32 ymm0, ymm1\nvmovdqa32 zmm0, zmm1\n"),
                                                     (AssemblyEncodeOptions){.target = avx10_1_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_avx10_1[] = {0x62, 0xf1, 0x7d, 0x28, 0x6f, 0xc1};
    BUSTER_TEST(arguments, avx10_1.diagnostic_count == 1 && avx10_1.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE &&
                               avx10_1.bytes.length == sizeof(expected_avx10_1) &&
                               memcmp(avx10_1.bytes.pointer, expected_avx10_1, sizeof(expected_avx10_1)) == 0);
    avx10_1_target.cpu_features = target_cpu_features_add(avx10_1_target.cpu_features, TARGET_CPU_FEATURE_X86_AVX10_512);
    AssemblyEncodeResult avx10_512 = assembly_encode(arguments->arena, S8("vmovdqa32 zmm0, zmm1\n"),
                                                      (AssemblyEncodeOptions){.target = avx10_1_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_avx10_512[] = {0x62, 0xf1, 0x7d, 0x48, 0x6f, 0xc1};
    BUSTER_TEST(arguments, avx10_512.diagnostic_count == 0 && avx10_512.bytes.length == sizeof(expected_avx10_512) &&
                               memcmp(avx10_512.bytes.pointer, expected_avx10_512, sizeof(expected_avx10_512)) == 0);

    Target avx10_aux_target = x86_target;
    avx10_aux_target.cpu_features_explicit = true;
    avx10_aux_target.cpu_features = target_cpu_features_from_array((TargetCpuFeature const[]){
        TARGET_CPU_FEATURE_X86_SSE2, TARGET_CPU_FEATURE_X86_AVX, TARGET_CPU_FEATURE_X86_AVX10_2,
        TARGET_CPU_FEATURE_X86_AVX10_V1_AUX, TARGET_CPU_FEATURE_X86_AVX10_512}, 5);
    String8 avx10_aux_intel_source =
        S8("vcvtbf42hf8 xmm0, qword ptr [rax]\n"
           "vcvtbf42hf8 ymm0, xmmword ptr [rax]\n"
           "vcvtbf42hf8 zmm0, ymmword ptr [rax]\n"
           "vcvtbf42hf8 xmm0 {k1}, qword ptr [rax]\n"
           "vcvtbf42hf8 zmm0 {k1}, ymmword ptr [rax]\n"
           "vcvtbf42hf8 xmm0, qword ptr [rax + 4]\n");
    u8 expected_avx10_aux_intel[] = {
        0x62, 0xf5, 0x7c, 0x08, 0x37, 0x00,
        0x62, 0xf5, 0x7c, 0x28, 0x37, 0x00,
        0x62, 0xf5, 0x7c, 0x48, 0x37, 0x00,
        0x62, 0xf5, 0x7c, 0x09, 0x37, 0x00,
        0x62, 0xf5, 0x7c, 0x49, 0x37, 0x00,
        0x62, 0xf5, 0x7c, 0x08, 0x37, 0x80, 0x04, 0x00, 0x00, 0x00,
    };
    AssemblyEncodeResult avx10_aux_intel = assembly_encode(
        arguments->arena, avx10_aux_intel_source,
        (AssemblyEncodeOptions){.target = avx10_aux_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, avx10_aux_intel.diagnostic_count == 0 && avx10_aux_intel.bytes.length == sizeof(expected_avx10_aux_intel) &&
                               memcmp(avx10_aux_intel.bytes.pointer, expected_avx10_aux_intel,
                                      sizeof(expected_avx10_aux_intel)) == 0);

    String8 avx10_aux_att_source =
        S8("vcvtbf42hf8 (%rax), %xmm0\n"
           "vcvtbf42hf8 (%rax), %ymm0\n"
           "vcvtbf42hf8 (%rax), %zmm0\n"
           "vcvtbf42hf8 (%rax), %xmm0 {%k1}\n"
           "vcvtbf42hf8 (%rax), %zmm0 {%k1}\n"
           "vcvtbf42hf8 4(%rax), %xmm0\n");
    AssemblyEncodeResult avx10_aux_att = assembly_encode(
        arguments->arena, avx10_aux_att_source,
        (AssemblyEncodeOptions){.target = avx10_aux_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, avx10_aux_att.diagnostic_count == 0 && avx10_aux_att.bytes.length == sizeof(expected_avx10_aux_intel) &&
                               memcmp(avx10_aux_att.bytes.pointer, expected_avx10_aux_intel,
                                      sizeof(expected_avx10_aux_intel)) == 0);

    AssemblyEncodeResult mem128_intel = assembly_encode(
        arguments->arena,
        S8("vpslld ymm0 {k1}, ymm1, xmmword ptr [rax + 16]\n"
           "vpsrld zmm0 {k1}, zmm1, xmmword ptr [rax + 32]\n"
           "vpslld ymm0 {k1}, ymm1, xmmword ptr [rax + 17]\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_mem128[] = {
        0x62, 0xf1, 0x75, 0x29, 0xf2, 0x40, 0x01,
        0x62, 0xf1, 0x75, 0x49, 0xd2, 0x40, 0x02,
        0x62, 0xf1, 0x75, 0x29, 0xf2, 0x80, 0x11, 0x00, 0x00, 0x00,
    };
    BUSTER_TEST(arguments, mem128_intel.diagnostic_count == 0 && mem128_intel.bytes.length == sizeof(expected_mem128) &&
                               memcmp(mem128_intel.bytes.pointer, expected_mem128, sizeof(expected_mem128)) == 0);

    AssemblyEncodeResult mem128_att = assembly_encode(
        arguments->arena,
        S8("vpslld 16(%rax), %ymm1, %ymm0 {%k1}\n"
           "vpsrld 32(%rax), %zmm1, %zmm0 {%k1}\n"
           "vpslld 17(%rax), %ymm1, %ymm0 {%k1}\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, mem128_att.diagnostic_count == 0 && mem128_att.bytes.length == sizeof(expected_mem128) &&
                               memcmp(mem128_att.bytes.pointer, expected_mem128, sizeof(expected_mem128)) == 0);

    AssemblyEncodeResult invalid_avx10_aux_widths = assembly_encode(
        arguments->arena,
        S8("vcvtbf42hf8 xmm0, xmmword ptr [rax]\n"
           "vcvtbf42hf8 ymm0, ymmword ptr [rax]\n"
           "vcvtbf42hf8 zmm0, zmmword ptr [rax]\n"),
        (AssemblyEncodeOptions){.target = avx10_aux_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_avx10_aux_widths.diagnostic_count == 3 && invalid_avx10_aux_widths.bytes.length == 0);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_avx10_aux_widths.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, invalid_avx10_aux_widths.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }

    Target fixed_round_len_target = advanced_target;
    fixed_round_len_target.cpu_features = target_cpu_features_add(fixed_round_len_target.cpu_features, TARGET_CPU_FEATURE_X86_AVX512FP16);
    AssemblyEncodeResult fixed_round_len512_intel = assembly_encode(
        arguments->arena,
        S8("vaddph zmm0, zmm1, zmm2\n"
           "vaddph {rn-sae}, zmm0, zmm1, zmm2\n"),
        (AssemblyEncodeOptions){.target = fixed_round_len_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_fixed_round_len512[] = {
        0x62, 0xf5, 0x74, 0x48, 0x58, 0xc2,
        0x62, 0xf5, 0x74, 0x18, 0x58, 0xc2,
    };
    BUSTER_TEST(arguments, fixed_round_len512_intel.diagnostic_count == 0 &&
                               fixed_round_len512_intel.bytes.length == sizeof(expected_fixed_round_len512) &&
                               memcmp(fixed_round_len512_intel.bytes.pointer, expected_fixed_round_len512,
                                      sizeof(expected_fixed_round_len512)) == 0);
    AssemblyEncodeResult fixed_round_len512_att = assembly_encode(
        arguments->arena,
        S8("vaddph %zmm2, %zmm1, %zmm0\n"
           "vaddph {rn-sae}, %zmm2, %zmm1, %zmm0\n"),
        (AssemblyEncodeOptions){.target = fixed_round_len_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, fixed_round_len512_att.diagnostic_count == 0 &&
                               fixed_round_len512_att.bytes.length == sizeof(expected_fixed_round_len512) &&
                               memcmp(fixed_round_len512_att.bytes.pointer, expected_fixed_round_len512,
                                      sizeof(expected_fixed_round_len512)) == 0);
    AssemblyEncodeResult invalid_fixed_round_len = assembly_encode(
        arguments->arena, S8("vaddph {rn-sae}, ymm0, ymm1, ymm2\n"),
        (AssemblyEncodeOptions){.target = fixed_round_len_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_fixed_round_len.diagnostic_count == 1 &&
                               invalid_fixed_round_len.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS &&
                               invalid_fixed_round_len.bytes.length == 0);

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
           "vcmpps k1 {z}, zmm2, zmm3, 7\n"
           "vcmpps k1, zmm2 {k2}, zmm3, 7\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_advanced_decorators.diagnostic_count == 8);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_advanced_decorators.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, invalid_advanced_decorators.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }

    AssemblyEncodeResult invalid_advanced_broadcasts = assembly_encode(
        arguments->arena,
        S8("vpaddb zmm0, zmm1, byte ptr [rax]{1to64}\n"
           "vpmullw zmm0, zmm1, word ptr [rax]{1to32}\n"
           "vpcmpeqb k1, zmm2, byte ptr [rax]{1to64}\n"
           "vpcmpw k1, zmm2, word ptr [rax]{1to32}, 7\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_advanced_broadcasts.diagnostic_count == 4);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_advanced_broadcasts.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, invalid_advanced_broadcasts.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }
    AssemblyEncodeResult invalid_advanced_broadcasts_att = assembly_encode(
        arguments->arena,
        S8("vpaddb (%rax){1to64}, %zmm1, %zmm0\n"
           "vpmullw (%rax){1to32}, %zmm1, %zmm0\n"
           "vpcmpeqb (%rax){1to64}, %zmm2, %k1\n"
           "vpcmpw $7, (%rax){1to32}, %zmm2, %k1\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, invalid_advanced_broadcasts_att.diagnostic_count == 4);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_advanced_broadcasts_att.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, invalid_advanced_broadcasts_att.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }

    // XED's EMX_BROADCAST_* pseudo operands are implicit instruction
    // semantics, so ordinary source syntax has no {1toN} decorator.  Keep
    // exact-byte checks for one AVX, one AVX2/NE-convert, and one masked EVEX
    // form; the EVEX byte also proves that EMX did not set EVEX.b.
    AssemblyEncodeResult emx_avx = assembly_encode(
        arguments->arena, S8("vbroadcastss ymm0, dword ptr [rax]\n"),
        (AssemblyEncodeOptions){.target = x86_avx_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_emx_avx[] = {0xc4, 0xe2, 0x7d, 0x18, 0x00};
    BUSTER_TEST(arguments, emx_avx.diagnostic_count == 0 && emx_avx.bytes.length == sizeof(expected_emx_avx) &&
                               memcmp(emx_avx.bytes.pointer, expected_emx_avx, sizeof(expected_emx_avx)) == 0);

    AssemblyEncodeResult emx_avx2 = assembly_encode(
        arguments->arena, S8("vpbroadcastb ymm0, byte ptr [rax]\n"),
        (AssemblyEncodeOptions){.target = x86_avx2_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_emx_avx2[] = {0xc4, 0xe2, 0x7d, 0x78, 0x00};
    BUSTER_TEST(arguments, emx_avx2.diagnostic_count == 0 && emx_avx2.bytes.length == sizeof(expected_emx_avx2) &&
                               memcmp(emx_avx2.bytes.pointer, expected_emx_avx2, sizeof(expected_emx_avx2)) == 0);

    Target x86_avx_ne_target = x86_avx2_target;
    x86_avx_ne_target.cpu_features = target_cpu_features_add(x86_avx_ne_target.cpu_features,
                                                               TARGET_CPU_FEATURE_X86_AVX_NE_CONVERT);
    AssemblyEncodeResult emx_avx_ne = assembly_encode(
        arguments->arena, S8("vbcstnebf162ps ymm0, word ptr [rax]\n"),
        (AssemblyEncodeOptions){.target = x86_avx_ne_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_emx_avx_ne[] = {0xc4, 0xe2, 0x7e, 0xb1, 0x00};
    BUSTER_TEST(arguments, emx_avx_ne.diagnostic_count == 0 && emx_avx_ne.bytes.length == sizeof(expected_emx_avx_ne) &&
                               memcmp(emx_avx_ne.bytes.pointer, expected_emx_avx_ne, sizeof(expected_emx_avx_ne)) == 0);

    AssemblyEncodeResult emx_evex = assembly_encode(
        arguments->arena, S8("vbroadcastss ymm0 {k1}, dword ptr [rax]\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_emx_evex[] = {0x62, 0xf2, 0x7d, 0x29, 0x18, 0x00};
    BUSTER_TEST(arguments, emx_evex.diagnostic_count == 0 && emx_evex.bytes.length == sizeof(expected_emx_evex) &&
                               memcmp(emx_evex.bytes.pointer, expected_emx_evex, sizeof(expected_emx_evex)) == 0);

    AssemblyEncodeResult invalid_advanced_rounding = assembly_encode(
        arguments->arena,
        S8("vaddps {rn-sae}, xmm0, xmm1, xmm2\n"
           "vaddps {rn-sae}, ymm0, ymm1, ymm2\n"
           "vcmpps k1, xmm2, xmm3, {sae}, 7\n"
           "vcmpps k1, ymm2, ymm3, {sae}, 7\n"
           "vrndscaleps xmm0, xmm1, {sae}, 4\n"
           "vrndscaleps ymm0, ymm1, {sae}, 4\n"
           "vcmpps k1, zmm2, zmm3, {rn-sae}, 7\n"
           "vrndscaleps zmm0, zmm1, {rn-sae}, 4\n"
           "vrndscalepd zmm0, zmm1, {rd-sae}, 4\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_advanced_rounding.diagnostic_count == 9);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_advanced_rounding.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, invalid_advanced_rounding.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }
    AssemblyEncodeResult invalid_advanced_rounding_att = assembly_encode(
        arguments->arena,
        S8("vaddps {rn-sae}, %xmm2, %xmm1, %xmm0\n"
           "vaddps {rn-sae}, %ymm2, %ymm1, %ymm0\n"
           "vcmpps $7, {sae}, %xmm3, %xmm2, %k1\n"
           "vcmpps $7, {sae}, %ymm3, %ymm2, %k1\n"
           "vrndscaleps $4, {sae}, %xmm1, %xmm0\n"
           "vrndscaleps $4, {sae}, %ymm1, %ymm0\n"
           "vcmpps $7, {rn-sae}, %zmm3, %zmm2, %k1\n"
           "vrndscaleps $4, {rn-sae}, %zmm1, %zmm0\n"
           "vrndscalepd $4, {rd-sae}, %zmm1, %zmm0\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, invalid_advanced_rounding_att.diagnostic_count == 9);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_advanced_rounding_att.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, invalid_advanced_rounding_att.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }

    Target scalar_evex_target = advanced_target;
    scalar_evex_target.cpu_features = target_cpu_features_remove(scalar_evex_target.cpu_features, TARGET_CPU_FEATURE_X86_AVX512VL);
    AssemblyEncodeResult scalar_evex = assembly_encode(
        arguments->arena,
        S8("vaddss xmm1 {k1}, xmm2, xmm3\n"
           "vaddsd xmm1 {k1}, xmm2, xmm3\n"),
        (AssemblyEncodeOptions){.target = scalar_evex_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_scalar_evex[] = {
        0x62, 0xf1, 0x6e, 0x09, 0x58, 0xcb,
        0x62, 0xf1, 0xef, 0x09, 0x58, 0xcb,
    };
    BUSTER_TEST(arguments, scalar_evex.diagnostic_count == 0 && scalar_evex.bytes.length == sizeof(expected_scalar_evex) &&
                               memcmp(scalar_evex.bytes.pointer, expected_scalar_evex, sizeof(expected_scalar_evex)) == 0);

    Target missing_vxor_dq_target = advanced_target;
    missing_vxor_dq_target.cpu_features = target_cpu_features_remove(missing_vxor_dq_target.cpu_features, TARGET_CPU_FEATURE_X86_AVX512DQ);
    AssemblyEncodeResult invalid_vxor_dq = assembly_encode(
        arguments->arena,
        S8("vxorps zmm0, zmm1, zmm2\n"
           "vxorpd zmm0, zmm1, zmm2\n"),
        (AssemblyEncodeOptions){.target = missing_vxor_dq_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_vxor_dq.diagnostic_count == 2);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_vxor_dq.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, invalid_vxor_dq.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
    }
    AssemblyEncodeResult invalid_vxor_dq_att = assembly_encode(
        arguments->arena,
        S8("vxorps %zmm2, %zmm1, %zmm0\n"
           "vxorpd %zmm2, %zmm1, %zmm0\n"),
        (AssemblyEncodeOptions){.target = missing_vxor_dq_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, invalid_vxor_dq_att.diagnostic_count == 2);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_vxor_dq_att.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, invalid_vxor_dq_att.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
    }

    Target avx10_vxor_target = x86_target;
    avx10_vxor_target.cpu_features_explicit = true;
    avx10_vxor_target.cpu_features = target_cpu_features_from_array((TargetCpuFeature const[]){
        TARGET_CPU_FEATURE_X86_SSE2, TARGET_CPU_FEATURE_X86_AVX,
        TARGET_CPU_FEATURE_X86_AVX10_1, TARGET_CPU_FEATURE_X86_AVX10_512}, 4);
    AssemblyEncodeResult avx10_vxor = assembly_encode(
        arguments->arena, S8("vxorps zmm0, zmm1, dword ptr [rax]{1to16}\n"),
        (AssemblyEncodeOptions){.target = avx10_vxor_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_avx10_vxor[] = {0x62, 0xf1, 0x74, 0x58, 0x57, 0x00};
    BUSTER_TEST(arguments, avx10_vxor.diagnostic_count == 0 && avx10_vxor.bytes.length == sizeof(expected_avx10_vxor) &&
                               memcmp(avx10_vxor.bytes.pointer, expected_avx10_vxor, sizeof(expected_avx10_vxor)) == 0);

    AssemblyEncodeResult invalid_pop2_same_register = assembly_encode(
        arguments->arena, S8("pop2 r16, r16\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_pop2_same_register.diagnostic_count == 1 &&
                               invalid_pop2_same_register.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    AssemblyEncodeResult invalid_pop2_same_register_att = assembly_encode(
        arguments->arena, S8("pop2q %r16, %r16\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, invalid_pop2_same_register_att.diagnostic_count == 1 &&
                               invalid_pop2_same_register_att.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);

    AssemblyEncodeResult invalid_operand_nf = assembly_encode(
        arguments->arena,
        S8("add r16d, r17d {nf}\n"
           "add r16d {nf}, r17d\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_operand_nf.diagnostic_count == 2);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_operand_nf.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, invalid_operand_nf.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }
    AssemblyEncodeResult invalid_operand_nf_att = assembly_encode(
        arguments->arena,
        S8("addl %r17d, %r16d {nf}\n"
           "addl %r17d {nf}, %r16d\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, invalid_operand_nf_att.diagnostic_count == 2);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_operand_nf_att.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, invalid_operand_nf_att.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }

    AssemblyEncodeResult numbered_register_boundaries = assembly_encode(
        arguments->arena,
        S8("mov r16, r17\n"
           "vaddps xmm16, xmm17, xmm18\n"
           "vaddps zmm31, zmm30, zmm29\n"
           "kmovw k7, k1\n"
           "tilezero tmm7\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, numbered_register_boundaries.diagnostic_count == 0);
    AssemblyEncodeResult invalid_numbered_registers = assembly_encode(
        arguments->arena,
        S8("mov r016, r17\n"
           "vaddps xmm00, xmm1, xmm2\n"
           "vaddps zmm000, zmm1, zmm2\n"
           "kmovw k00, k1\n"
           "tilezero tmm00\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_numbered_registers.diagnostic_count == 5);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_numbered_registers.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, invalid_numbered_registers.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }
    AssemblyEncodeResult invalid_numbered_registers_att = assembly_encode(
        arguments->arena,
        S8("movq %r016, %r17\n"
           "vaddps %xmm00, %xmm1, %xmm2\n"
           "vaddps %zmm000, %zmm1, %zmm2\n"
           "kmovw %k00, %k1\n"
           "tilezero %tmm00\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, invalid_numbered_registers_att.diagnostic_count == 5);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_numbered_registers_att.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, invalid_numbered_registers_att.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
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

    AssemblyEncodeResult invalid_vex_only_evex = assembly_encode(
        arguments->arena,
        S8("vmovdqa zmm0, zmm1\n"
           "vmovdqa xmm16, xmm17\n"
           "vpand zmm0, zmm1, zmm2\n"
           "vpand xmm16, xmm17, xmm18\n"
           "vpcmpeqb zmm0, zmm1, zmm2\n"
           "vpcmpeqb xmm16, xmm17, xmm18\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_vex_only_evex.diagnostic_count == 6);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_vex_only_evex.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, invalid_vex_only_evex.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }

    AssemblyEncodeResult invalid_evex_masks = assembly_encode(
        arguments->arena,
        S8("vcmpps k1 {k0}, zmm2, zmm3, 7\n"
           "vcmpps k1 {z}, zmm2, zmm3, 7\n"
           "vmovdqa64 zmmword ptr [rax] {z}, zmm2\n"),
        (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_evex_masks.diagnostic_count == 3);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_evex_masks.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, invalid_evex_masks.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }

    Target missing_kmov_width_target = advanced_target;
    missing_kmov_width_target.cpu_features = target_cpu_features_remove(missing_kmov_width_target.cpu_features, TARGET_CPU_FEATURE_X86_AVX512BW);
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
    missing_kadd_width_target.cpu_features = target_cpu_features_remove(missing_kadd_width_target.cpu_features, TARGET_CPU_FEATURE_X86_AVX512DQ);
    AssemblyEncodeResult invalid_kadd_width_features = assembly_encode(
        arguments->arena, S8("kaddw k1, k2, k3\n"),
        (AssemblyEncodeOptions){.target = missing_kadd_width_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_kadd_width_features.diagnostic_count == 1 &&
                               invalid_kadd_width_features.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);

    Target amx_missing_target = advanced_target;
    amx_missing_target.cpu_features = target_cpu_features_remove(amx_missing_target.cpu_features, TARGET_CPU_FEATURE_X86_AMX_TILE);
    amx_missing_target.cpu_features = target_cpu_features_remove(amx_missing_target.cpu_features, TARGET_CPU_FEATURE_X86_AMX_BF16);
    amx_missing_target.cpu_features = target_cpu_features_remove(amx_missing_target.cpu_features, TARGET_CPU_FEATURE_X86_AMX_INT8);
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

    Target amd_target = x86_target;
    amd_target.cpu_features_explicit = true;
    amd_target.cpu_features = target_cpu_features_from_array((TargetCpuFeature const[]){
        TARGET_CPU_FEATURE_X86_SSE2, TARGET_CPU_FEATURE_X86_AVX, TARGET_CPU_FEATURE_X86_XOP,
        TARGET_CPU_FEATURE_X86_FMA4, TARGET_CPU_FEATURE_X86_TBM, TARGET_CPU_FEATURE_X86_LWP,
        TARGET_CPU_FEATURE_X86_3DNOW, TARGET_CPU_FEATURE_X86_3DNOWA}, 8);
    String8 amd_intel_source =
        S8("vfrczps xmm1, xmm2\n"
           "vfrczps xmm8, xmm9\n"
           "vfrczps xmm1, xmmword ptr [rax + 127]\n"
           "vpshab xmm1, xmm2, xmm3\n"
           "vpshab xmm1, xmm2, xmmword ptr [rax]\n"
           "vprotb xmm1, xmm2, 0x55\n"
           "vprotb xmm1, xmm2, xmm3\n"
           "vprotb xmm1, xmm2, xmmword ptr [rax]\n"
           "vpcmov xmm1, xmm2, xmm3, xmm4\n"
           "vpcmov xmm1, xmm2, xmm3, xmmword ptr [rax]\n"
           "vpcmov xmm1, xmm2, xmmword ptr [rax], xmm4\n"
           "vpperm xmm1, xmm2, xmm3, xmmword ptr [rax + 127]\n"
           "vfmaddps xmm1, xmm2, xmm3, xmm4\n"
           "vfmaddps xmm1, xmm2, xmm3, xmmword ptr [rax]\n"
           "vfmaddps xmm1, xmm2, xmmword ptr [rax], xmm4\n"
           "vfmaddss xmm1, xmm2, xmm3, xmm4\n"
           "bextr r8, r9, 0x11223344\n"
           "bextr eax, ecx, 0x11223344\n"
           "blcfill r8, r9\n"
           "blcfill r8, qword ptr [rax + 127]\n"
           "llwpcb r8\n"
           "slwpcb r9\n"
           "lwpins r8, ecx, 0x11223344\n"
           "lwpins r8, dword ptr [rax + 127], 0x11223344\n"
           "femms\n"
           "pi2fw mm0, mm1\n"
           "pfadd mm0, qword ptr [rax + 127]\n");
    u8 expected_amd_intel[] = {
        0x8f, 0xe9, 0x78, 0x80, 0xca,
        0x8f, 0x49, 0x78, 0x80, 0xc1,
        0x8f, 0xe9, 0x78, 0x80, 0x48, 0x7f,
        0x8f, 0xe9, 0x60, 0x98, 0xca,
        0x8f, 0xe9, 0xe8, 0x98, 0x08,
        0x8f, 0xe8, 0x78, 0xc0, 0xca, 0x55,
        0x8f, 0xe9, 0x60, 0x90, 0xca,
        0x8f, 0xe9, 0xe8, 0x90, 0x08,
        0x8f, 0xe8, 0x68, 0xa2, 0xcb, 0x40,
        0x8f, 0xe8, 0xe8, 0xa2, 0x08, 0x30,
        0x8f, 0xe8, 0x68, 0xa2, 0x08, 0x40,
        0x8f, 0xe8, 0xe8, 0xa3, 0x48, 0x7f, 0x30,
        0xc4, 0xe3, 0xe9, 0x68, 0xcc, 0x30,
        0xc4, 0xe3, 0xe9, 0x68, 0x08, 0x30,
        0xc4, 0xe3, 0x69, 0x68, 0x08, 0x40,
        0xc4, 0xe3, 0xe9, 0x6a, 0xcc, 0x30,
        0x8f, 0x4a, 0xf8, 0x10, 0xc1, 0x44, 0x33, 0x22, 0x11,
        0x8f, 0xea, 0x78, 0x10, 0xc1, 0x44, 0x33, 0x22, 0x11,
        0x8f, 0xc9, 0xb8, 0x01, 0xc9,
        0x8f, 0xe9, 0xb8, 0x01, 0x48, 0x7f,
        0x8f, 0xc9, 0xf8, 0x12, 0xc0,
        0x8f, 0xc9, 0xf8, 0x12, 0xc9,
        0x8f, 0xea, 0xb8, 0x12, 0xc1, 0x44, 0x33, 0x22, 0x11,
        0x8f, 0xea, 0xb8, 0x12, 0x40, 0x7f, 0x44, 0x33, 0x22, 0x11,
        0x0f, 0x0e,
        0x0f, 0x0f, 0xc1, 0x0c,
        0x0f, 0x0f, 0x40, 0x7f, 0x9e,
    };
    AssemblyEncodeResult amd_intel = assembly_encode(arguments->arena, amd_intel_source,
                                                      (AssemblyEncodeOptions){.target = amd_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, amd_intel.diagnostic_count == 0 && amd_intel.bytes.length == sizeof(expected_amd_intel) &&
                               memcmp(amd_intel.bytes.pointer, expected_amd_intel, sizeof(expected_amd_intel)) == 0);

    String8 amd_att_source =
        S8("vfrczps %xmm2, %xmm1\n"
           "vpshab %xmm3, %xmm2, %xmm1\n"
           "vprotb $0x55, %xmm2, %xmm1\n"
           "vpcmov %xmm4, %xmm3, %xmm2, %xmm1\n"
           "vpcmov (%rax), %xmm3, %xmm2, %xmm1\n"
           "vfmaddps %xmm4, %xmm3, %xmm2, %xmm1\n"
           "bextrq $0x11223344, %r9, %r8\n"
           "pfadd 127(%rax), %mm0\n");
    u8 expected_amd_att[] = {
        0x8f, 0xe9, 0x78, 0x80, 0xca,
        0x8f, 0xe9, 0x60, 0x98, 0xca,
        0x8f, 0xe8, 0x78, 0xc0, 0xca, 0x55,
        0x8f, 0xe8, 0x68, 0xa2, 0xcb, 0x40,
        0x8f, 0xe8, 0xe8, 0xa2, 0x08, 0x30,
        0xc4, 0xe3, 0xe9, 0x68, 0xcc, 0x30,
        0x8f, 0x4a, 0xf8, 0x10, 0xc1, 0x44, 0x33, 0x22, 0x11,
        0x0f, 0x0f, 0x40, 0x7f, 0x9e,
    };
    AssemblyEncodeResult amd_att = assembly_encode(arguments->arena, amd_att_source,
                                                    (AssemblyEncodeOptions){.target = amd_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, amd_att.diagnostic_count == 0 && amd_att.bytes.length == sizeof(expected_amd_att) &&
                               memcmp(amd_att.bytes.pointer, expected_amd_att, sizeof(expected_amd_att)) == 0);

    AssemblyEncodeResult amd_att_shapes = assembly_encode(
        arguments->arena,
        S8("vpcomb $3, %xmm2, %xmm1, %xmm0\n"
           "vpcomub $3, %xmm2, %xmm1, %xmm0\n"
           "vpshlb (%rax), %xmm1, %xmm0\n"
           "vprotb %xmm2, %xmm1, %xmm0\n"
           "vpcmov (%rax), %xmm2, %xmm1, %xmm0\n"
           "vpperm %xmm3, %xmm2, %xmm1, %xmm0\n"
           "vpermil2ps $3, %xmm3, %xmm2, %xmm1, %xmm0\n"
           "vfmaddps (%rax), %xmm2, %xmm1, %xmm0\n"
           "vfmaddps %xmm3, (%rax), %xmm1, %xmm0\n"
           "lwpval $3, %edx, %r9\n"),
        (AssemblyEncodeOptions){.target = amd_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    u8 expected_amd_att_shapes[] = {
        0x8f, 0xe8, 0x70, 0xcc, 0xc2, 0x03,
        0x8f, 0xe8, 0x70, 0xec, 0xc2, 0x03,
        0x8f, 0xe9, 0xf0, 0x94, 0x00,
        0x8f, 0xe9, 0x68, 0x90, 0xc1,
        0x8f, 0xe8, 0xf0, 0xa2, 0x00, 0x20,
        0x8f, 0xe8, 0x70, 0xa3, 0xc2, 0x30,
        0xc4, 0xe3, 0x71, 0x48, 0xc2, 0x33,
        0xc4, 0xe3, 0xf1, 0x68, 0x00, 0x20,
        0xc4, 0xe3, 0x71, 0x68, 0x00, 0x30,
        0x8f, 0xea, 0xb0, 0x12, 0xca, 0x03, 0x00, 0x00, 0x00,
    };
    BUSTER_TEST(arguments, amd_att_shapes.diagnostic_count == 0 && amd_att_shapes.bytes.length == sizeof(expected_amd_att_shapes) &&
                               memcmp(amd_att_shapes.bytes.pointer, expected_amd_att_shapes, sizeof(expected_amd_att_shapes)) == 0);

    String8 amd_xop_inventory_source =
        S8("vfrczps xmm0, xmm1\n"
           "vfrczpd xmm0, xmm1\n"
           "vfrczss xmm0, xmm1\n"
           "vfrczsd xmm0, xmm1\n"
           "vphaddbw xmm0, xmm1\n"
           "vphaddbd xmm0, xmm1\n"
           "vphaddbq xmm0, xmm1\n"
           "vphaddwd xmm0, xmm1\n"
           "vphaddwq xmm0, xmm1\n"
           "vphaddubw xmm0, xmm1\n"
           "vphaddubd xmm0, xmm1\n"
           "vphaddubq xmm0, xmm1\n"
           "vphadduwd xmm0, xmm1\n"
           "vphadduwq xmm0, xmm1\n"
           "vphsubbw xmm0, xmm1\n"
           "vphsubwd xmm0, xmm1\n"
           "vphsubdq xmm0, xmm1\n"
           "vphadddq xmm0, xmm1\n"
           "vphaddudq xmm0, xmm1\n"
           "vprotb xmm0, xmm1, 3\n"
           "vprotw xmm0, xmm1, 3\n"
           "vprotd xmm0, xmm1, 3\n"
           "vprotq xmm0, xmm1, 3\n"
           "vpcomb xmm0, xmm1, xmm2, 3\n"
           "vpcomw xmm0, xmm1, xmm2, 3\n"
           "vpcomd xmm0, xmm1, xmm2, 3\n"
           "vpcomq xmm0, xmm1, xmm2, 3\n"
           "vpcomub xmm0, xmm1, xmm2, 3\n"
           "vpcomuw xmm0, xmm1, xmm2, 3\n"
           "vpcomud xmm0, xmm1, xmm2, 3\n"
           "vpcomuq xmm0, xmm1, xmm2, 3\n"
           "vprotb xmm0, xmm1, xmm2\n"
           "vprotw xmm0, xmm1, xmm2\n"
           "vprotd xmm0, xmm1, xmm2\n"
           "vprotq xmm0, xmm1, xmm2\n"
           "vpshlb xmm0, xmm1, xmm2\n"
           "vpshlw xmm0, xmm1, xmm2\n"
           "vpshld xmm0, xmm1, xmm2\n"
           "vpshlq xmm0, xmm1, xmm2\n"
           "vpshab xmm0, xmm1, xmm2\n"
           "vpshaw xmm0, xmm1, xmm2\n"
           "vpshad xmm0, xmm1, xmm2\n"
           "vpshaq xmm0, xmm1, xmm2\n"
           "vpmacssww xmm0, xmm1, xmm2, xmm3\n"
           "vpmacsswd xmm0, xmm1, xmm2, xmm3\n"
           "vpmacssdql xmm0, xmm1, xmm2, xmm3\n"
           "vpmacsww xmm0, xmm1, xmm2, xmm3\n"
           "vpmacswd xmm0, xmm1, xmm2, xmm3\n"
           "vpmacsdql xmm0, xmm1, xmm2, xmm3\n"
           "vpcmov xmm0, xmm1, xmm2, xmm3\n"
           "vpperm xmm0, xmm1, xmm2, xmm3\n"
           "vpmadcsswd xmm0, xmm1, xmm2, xmm3\n"
           "vpmadcswd xmm0, xmm1, xmm2, xmm3\n"
           "vpmacssdd xmm0, xmm1, xmm2, xmm3\n"
           "vpmacssdqh xmm0, xmm1, xmm2, xmm3\n"
           "vpmacsdd xmm0, xmm1, xmm2, xmm3\n"
           "vpmacsdqh xmm0, xmm1, xmm2, xmm3\n"
           "vpermil2ps xmm0, xmm1, xmm2, xmm3, 3\n"
           "vpermil2pd xmm0, xmm1, xmm2, xmm3, 3\n"
           "llwpcb r8\n"
           "slwpcb r9\n"
           "lwpins r8, ecx, 3\n"
           "lwpval r9, edx, 3\n");
    u8 expected_amd_xop_inventory[] = {
        0x8f, 0xe9, 0x78, 0x80, 0xc1,
        0x8f, 0xe9, 0x78, 0x81, 0xc1,
        0x8f, 0xe9, 0x78, 0x82, 0xc1,
        0x8f, 0xe9, 0x78, 0x83, 0xc1,
        0x8f, 0xe9, 0x78, 0xc1, 0xc1,
        0x8f, 0xe9, 0x78, 0xc2, 0xc1,
        0x8f, 0xe9, 0x78, 0xc3, 0xc1,
        0x8f, 0xe9, 0x78, 0xc6, 0xc1,
        0x8f, 0xe9, 0x78, 0xc7, 0xc1,
        0x8f, 0xe9, 0x78, 0xd1, 0xc1,
        0x8f, 0xe9, 0x78, 0xd2, 0xc1,
        0x8f, 0xe9, 0x78, 0xd3, 0xc1,
        0x8f, 0xe9, 0x78, 0xd6, 0xc1,
        0x8f, 0xe9, 0x78, 0xd7, 0xc1,
        0x8f, 0xe9, 0x78, 0xe1, 0xc1,
        0x8f, 0xe9, 0x78, 0xe2, 0xc1,
        0x8f, 0xe9, 0x78, 0xe3, 0xc1,
        0x8f, 0xe9, 0x78, 0xcb, 0xc1,
        0x8f, 0xe9, 0x78, 0xdb, 0xc1,
        0x8f, 0xe8, 0x78, 0xc0, 0xc1, 0x03,
        0x8f, 0xe8, 0x78, 0xc1, 0xc1, 0x03,
        0x8f, 0xe8, 0x78, 0xc2, 0xc1, 0x03,
        0x8f, 0xe8, 0x78, 0xc3, 0xc1, 0x03,
        0x8f, 0xe8, 0x70, 0xcc, 0xc2, 0x03,
        0x8f, 0xe8, 0x70, 0xcd, 0xc2, 0x03,
        0x8f, 0xe8, 0x70, 0xce, 0xc2, 0x03,
        0x8f, 0xe8, 0x70, 0xcf, 0xc2, 0x03,
        0x8f, 0xe8, 0x70, 0xec, 0xc2, 0x03,
        0x8f, 0xe8, 0x70, 0xed, 0xc2, 0x03,
        0x8f, 0xe8, 0x70, 0xee, 0xc2, 0x03,
        0x8f, 0xe8, 0x70, 0xef, 0xc2, 0x03,
        0x8f, 0xe9, 0x68, 0x90, 0xc1,
        0x8f, 0xe9, 0x68, 0x91, 0xc1,
        0x8f, 0xe9, 0x68, 0x92, 0xc1,
        0x8f, 0xe9, 0x68, 0x93, 0xc1,
        0x8f, 0xe9, 0x68, 0x94, 0xc1,
        0x8f, 0xe9, 0x68, 0x95, 0xc1,
        0x8f, 0xe9, 0x68, 0x96, 0xc1,
        0x8f, 0xe9, 0x68, 0x97, 0xc1,
        0x8f, 0xe9, 0x68, 0x98, 0xc1,
        0x8f, 0xe9, 0x68, 0x99, 0xc1,
        0x8f, 0xe9, 0x68, 0x9a, 0xc1,
        0x8f, 0xe9, 0x68, 0x9b, 0xc1,
        0x8f, 0xe8, 0x70, 0x85, 0xc2, 0x30,
        0x8f, 0xe8, 0x70, 0x86, 0xc2, 0x30,
        0x8f, 0xe8, 0x70, 0x87, 0xc2, 0x30,
        0x8f, 0xe8, 0x70, 0x95, 0xc2, 0x30,
        0x8f, 0xe8, 0x70, 0x96, 0xc2, 0x30,
        0x8f, 0xe8, 0x70, 0x97, 0xc2, 0x30,
        0x8f, 0xe8, 0x70, 0xa2, 0xc2, 0x30,
        0x8f, 0xe8, 0x70, 0xa3, 0xc2, 0x30,
        0x8f, 0xe8, 0x70, 0xa6, 0xc2, 0x30,
        0x8f, 0xe8, 0x70, 0xb6, 0xc2, 0x30,
        0x8f, 0xe8, 0x70, 0x8e, 0xc2, 0x30,
        0x8f, 0xe8, 0x70, 0x8f, 0xc2, 0x30,
        0x8f, 0xe8, 0x70, 0x9e, 0xc2, 0x30,
        0x8f, 0xe8, 0x70, 0x9f, 0xc2, 0x30,
        0xc4, 0xe3, 0x71, 0x48, 0xc2, 0x33,
        0xc4, 0xe3, 0x71, 0x49, 0xc2, 0x33,
        0x8f, 0xc9, 0xf8, 0x12, 0xc0,
        0x8f, 0xc9, 0xf8, 0x12, 0xc9,
        0x8f, 0xea, 0xb8, 0x12, 0xc1, 0x03, 0x00, 0x00, 0x00,
        0x8f, 0xea, 0xb0, 0x12, 0xca, 0x03, 0x00, 0x00, 0x00,
    };
    AssemblyEncodeResult amd_xop_inventory = assembly_encode(arguments->arena, amd_xop_inventory_source,
                                                              (AssemblyEncodeOptions){.target = amd_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, amd_xop_inventory.diagnostic_count == 0 && amd_xop_inventory.bytes.length == sizeof(expected_amd_xop_inventory) &&
                               memcmp(amd_xop_inventory.bytes.pointer, expected_amd_xop_inventory, sizeof(expected_amd_xop_inventory)) == 0);

    String8 amd_fma4_inventory_source =
        S8("vfmaddsubps xmm0, xmm1, xmm2, xmm3\n"
           "vfmaddsubpd xmm0, xmm1, xmm2, xmm3\n"
           "vfmsubaddps xmm0, xmm1, xmm2, xmm3\n"
           "vfmsubaddpd xmm0, xmm1, xmm2, xmm3\n"
           "vfmaddps xmm0, xmm1, xmm2, xmm3\n"
           "vfmaddpd xmm0, xmm1, xmm2, xmm3\n"
           "vfmaddss xmm0, xmm1, xmm2, xmm3\n"
           "vfmaddsd xmm0, xmm1, xmm2, xmm3\n"
           "vfmsubps xmm0, xmm1, xmm2, xmm3\n"
           "vfmsubpd xmm0, xmm1, xmm2, xmm3\n"
           "vfmsubss xmm0, xmm1, xmm2, xmm3\n"
           "vfmsubsd xmm0, xmm1, xmm2, xmm3\n"
           "vfnmaddps xmm0, xmm1, xmm2, xmm3\n"
           "vfnmaddpd xmm0, xmm1, xmm2, xmm3\n"
           "vfnmaddss xmm0, xmm1, xmm2, xmm3\n"
           "vfnmaddsd xmm0, xmm1, xmm2, xmm3\n"
           "vfnmsubps xmm0, xmm1, xmm2, xmm3\n"
           "vfnmsubpd xmm0, xmm1, xmm2, xmm3\n"
           "vfnmsubss xmm0, xmm1, xmm2, xmm3\n"
           "vfnmsubsd xmm0, xmm1, xmm2, xmm3\n");
    u8 expected_amd_fma4_inventory[] = {
        0xc4, 0xe3, 0xf1, 0x5c, 0xc3, 0x20,
        0xc4, 0xe3, 0xf1, 0x5d, 0xc3, 0x20,
        0xc4, 0xe3, 0xf1, 0x5e, 0xc3, 0x20,
        0xc4, 0xe3, 0xf1, 0x5f, 0xc3, 0x20,
        0xc4, 0xe3, 0xf1, 0x68, 0xc3, 0x20,
        0xc4, 0xe3, 0xf1, 0x69, 0xc3, 0x20,
        0xc4, 0xe3, 0xf1, 0x6a, 0xc3, 0x20,
        0xc4, 0xe3, 0xf1, 0x6b, 0xc3, 0x20,
        0xc4, 0xe3, 0xf1, 0x6c, 0xc3, 0x20,
        0xc4, 0xe3, 0xf1, 0x6d, 0xc3, 0x20,
        0xc4, 0xe3, 0xf1, 0x6e, 0xc3, 0x20,
        0xc4, 0xe3, 0xf1, 0x6f, 0xc3, 0x20,
        0xc4, 0xe3, 0xf1, 0x78, 0xc3, 0x20,
        0xc4, 0xe3, 0xf1, 0x79, 0xc3, 0x20,
        0xc4, 0xe3, 0xf1, 0x7a, 0xc3, 0x20,
        0xc4, 0xe3, 0xf1, 0x7b, 0xc3, 0x20,
        0xc4, 0xe3, 0xf1, 0x7c, 0xc3, 0x20,
        0xc4, 0xe3, 0xf1, 0x7d, 0xc3, 0x20,
        0xc4, 0xe3, 0xf1, 0x7e, 0xc3, 0x20,
        0xc4, 0xe3, 0xf1, 0x7f, 0xc3, 0x20,
    };
    AssemblyEncodeResult amd_fma4_inventory = assembly_encode(arguments->arena, amd_fma4_inventory_source,
                                                               (AssemblyEncodeOptions){.target = amd_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, amd_fma4_inventory.diagnostic_count == 0 && amd_fma4_inventory.bytes.length == sizeof(expected_amd_fma4_inventory) &&
                               memcmp(amd_fma4_inventory.bytes.pointer, expected_amd_fma4_inventory, sizeof(expected_amd_fma4_inventory)) == 0);

    String8 amd_tbm_inventory_source =
        S8("bextr r8, r9, 3\n"
           "blcfill r8, r9\n"
           "blci r8, r9\n"
           "blcic r8, r9\n"
           "blcmsk r8, r9\n"
           "blcs r8, r9\n"
           "blsfill r8, r9\n"
           "blsic r8, r9\n"
           "t1mskc r8, r9\n"
           "tzmsk r8, r9\n");
    u8 expected_amd_tbm_inventory[] = {
        0x8f, 0x4a, 0xf8, 0x10, 0xc1, 0x03, 0x00, 0x00, 0x00,
        0x8f, 0xc9, 0xb8, 0x01, 0xc9,
        0x8f, 0xc9, 0xb8, 0x02, 0xf1,
        0x8f, 0xc9, 0xb8, 0x01, 0xe9,
        0x8f, 0xc9, 0xb8, 0x02, 0xc9,
        0x8f, 0xc9, 0xb8, 0x01, 0xd9,
        0x8f, 0xc9, 0xb8, 0x01, 0xd1,
        0x8f, 0xc9, 0xb8, 0x01, 0xf1,
        0x8f, 0xc9, 0xb8, 0x01, 0xf9,
        0x8f, 0xc9, 0xb8, 0x01, 0xe1,
    };
    AssemblyEncodeResult amd_tbm_inventory = assembly_encode(arguments->arena, amd_tbm_inventory_source,
                                                              (AssemblyEncodeOptions){.target = amd_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, amd_tbm_inventory.diagnostic_count == 0 && amd_tbm_inventory.bytes.length == sizeof(expected_amd_tbm_inventory) &&
                               memcmp(amd_tbm_inventory.bytes.pointer, expected_amd_tbm_inventory, sizeof(expected_amd_tbm_inventory)) == 0);

    String8 amd_3dnow_inventory_source =
        S8("femms\n"
           "pi2fw mm0, mm1\n"
           "pi2fd mm0, mm1\n"
           "pf2iw mm0, mm1\n"
           "pf2id mm0, mm1\n"
           "pfnacc mm0, mm1\n"
           "pfpnacc mm0, mm1\n"
           "pfcmpge mm0, mm1\n"
           "pfmin mm0, mm1\n"
           "pfrcp mm0, mm1\n"
           "pfrsqrt mm0, mm1\n"
           "pfsub mm0, mm1\n"
           "pfadd mm0, mm1\n"
           "pfcmpgt mm0, mm1\n"
           "pfmax mm0, mm1\n"
           "pfrcpit1 mm0, mm1\n"
           "pfrsqit1 mm0, mm1\n"
           "pfsubr mm0, mm1\n"
           "pfacc mm0, mm1\n"
           "pfcmpeq mm0, mm1\n"
           "pfmul mm0, mm1\n"
           "pfrcpit2 mm0, mm1\n"
           "pmulhrw mm0, mm1\n"
           "pswapd mm0, mm1\n"
           "pavgusb mm0, mm1\n");
    u8 expected_amd_3dnow_inventory[] = {
        0x0f, 0x0e,
        0x0f, 0x0f, 0xc1, 0x0c,
        0x0f, 0x0f, 0xc1, 0x0d,
        0x0f, 0x0f, 0xc1, 0x1c,
        0x0f, 0x0f, 0xc1, 0x1d,
        0x0f, 0x0f, 0xc1, 0x8a,
        0x0f, 0x0f, 0xc1, 0x8e,
        0x0f, 0x0f, 0xc1, 0x90,
        0x0f, 0x0f, 0xc1, 0x94,
        0x0f, 0x0f, 0xc1, 0x96,
        0x0f, 0x0f, 0xc1, 0x97,
        0x0f, 0x0f, 0xc1, 0x9a,
        0x0f, 0x0f, 0xc1, 0x9e,
        0x0f, 0x0f, 0xc1, 0xa0,
        0x0f, 0x0f, 0xc1, 0xa4,
        0x0f, 0x0f, 0xc1, 0xa6,
        0x0f, 0x0f, 0xc1, 0xa7,
        0x0f, 0x0f, 0xc1, 0xaa,
        0x0f, 0x0f, 0xc1, 0xae,
        0x0f, 0x0f, 0xc1, 0xb0,
        0x0f, 0x0f, 0xc1, 0xb4,
        0x0f, 0x0f, 0xc1, 0xb6,
        0x0f, 0x0f, 0xc1, 0xb7,
        0x0f, 0x0f, 0xc1, 0xbb,
        0x0f, 0x0f, 0xc1, 0xbf,
    };
    AssemblyEncodeResult amd_3dnow_inventory = assembly_encode(arguments->arena, amd_3dnow_inventory_source,
                                                                (AssemblyEncodeOptions){.target = amd_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, amd_3dnow_inventory.diagnostic_count == 0 && amd_3dnow_inventory.bytes.length == sizeof(expected_amd_3dnow_inventory) &&
                               memcmp(amd_3dnow_inventory.bytes.pointer, expected_amd_3dnow_inventory, sizeof(expected_amd_3dnow_inventory)) == 0);

    String8 amd_memory_source =
        S8("vfrczps ymm8, ymm9\n"
           "vfrczpd ymm8, ymm9\n"
           "vpcmov ymm8, ymm9, ymm10, ymm11\n"
           "vpcmov ymm8, ymm9, ymm10, ymmword ptr [rax]\n"
           "vpcmov ymm8, ymm9, ymmword ptr [rax], ymm11\n"
           "vfrczps xmm8, xmmword ptr [r12 + r9*4 + 0x12345678]\n"
           "vprotb xmm8, xmm9, xmmword ptr [r12 + r9*8 - 128]\n"
           "vpshlq xmm8, xmm9, xmmword ptr [r12 + r9*8 + 127]\n"
           "vpcmov xmm8, xmm9, xmm10, xmmword ptr [r12 + r9*4 + 0x12345678]\n"
           "vpperm xmm8, xmm9, xmm10, xmmword ptr [r12 + r9*8 + 127]\n"
           "vpperm xmm8, xmm9, xmmword ptr [r12 + r9*8 + 127], xmm10\n"
           "vpmacssww xmm8, xmm9, xmmword ptr [r12 + r9*2 + 127], xmm10\n"
           "vpermil2ps ymm8, ymm9, ymmword ptr [r12 + r9*4 - 128], ymm10, 7\n"
           "vfmaddpd ymm8, ymm9, ymmword ptr [r12 + r9*8 + 0x12345678], ymm10\n"
           "vfmaddsubps ymm8, ymm9, ymm10, ymmword ptr [r12 + r9*8 - 128]\n"
           "bextr r8, qword ptr [r12 + r9*8 + 0x12345678], 0x11223344\n"
           "blci r8, qword ptr [r12 + r9*8 + 127]\n"
           "lwpins r8, dword ptr [r12 + r9*4 + 0x12345678], 0x11223344\n"
           "pfadd mm0, qword ptr [r12 + r9*8 + 127]\n"
           "vfrczps xmm0, xmmword ptr [0x12345678]\n"
           "pfadd mm0, qword ptr [0x12345678]\n"
           "blcfill r8, qword ptr [0x12345678]\n");
    u8 expected_amd_memory[] = {
        0x8f, 0x49, 0x7c, 0x80, 0xc1,
        0x8f, 0x49, 0x7c, 0x81, 0xc1,
        0x8f, 0x48, 0x34, 0xa2, 0xc2, 0xb0,
        0x8f, 0x68, 0xb4, 0xa2, 0x00, 0xa0,
        0x8f, 0x68, 0x34, 0xa2, 0x00, 0xb0,
        0x8f, 0x09, 0x78, 0x80, 0x84, 0x8c, 0x78, 0x56, 0x34, 0x12,
        0x8f, 0x09, 0xb0, 0x90, 0x44, 0xcc, 0x80,
        0x8f, 0x09, 0xb0, 0x97, 0x44, 0xcc, 0x7f,
        0x8f, 0x08, 0xb0, 0xa2, 0x84, 0x8c, 0x78, 0x56, 0x34, 0x12, 0xa0,
        0x8f, 0x08, 0xb0, 0xa3, 0x44, 0xcc, 0x7f, 0xa0,
        0x8f, 0x08, 0x30, 0xa3, 0x44, 0xcc, 0x7f, 0xa0,
        0x8f, 0x08, 0x30, 0x85, 0x44, 0x4c, 0x7f, 0xa0,
        0xc4, 0x03, 0x35, 0x48, 0x44, 0x8c, 0x80, 0xa7,
        0xc4, 0x03, 0x35, 0x69, 0x84, 0xcc, 0x78, 0x56, 0x34, 0x12, 0xa0,
        0xc4, 0x03, 0xb5, 0x5c, 0x44, 0xcc, 0x80, 0xa0,
        0x8f, 0x0a, 0xf8, 0x10, 0x84, 0xcc, 0x78, 0x56, 0x34, 0x12, 0x44, 0x33, 0x22, 0x11,
        0x8f, 0x89, 0xb8, 0x02, 0x74, 0xcc, 0x7f,
        0x8f, 0x8a, 0xb8, 0x12, 0x84, 0x8c, 0x78, 0x56, 0x34, 0x12, 0x44, 0x33, 0x22, 0x11,
        0x43, 0x0f, 0x0f, 0x44, 0xcc, 0x7f, 0x9e,
        0x8f, 0xe9, 0x78, 0x80, 0x04, 0x25, 0x78, 0x56, 0x34, 0x12,
        0x0f, 0x0f, 0x04, 0x25, 0x78, 0x56, 0x34, 0x12, 0x9e,
        0x8f, 0xe9, 0xb8, 0x01, 0x0c, 0x25, 0x78, 0x56, 0x34, 0x12,
    };
    AssemblyEncodeResult amd_memory = assembly_encode(arguments->arena, amd_memory_source,
                                                       (AssemblyEncodeOptions){.target = amd_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, amd_memory.diagnostic_count == 0 && amd_memory.bytes.length == sizeof(expected_amd_memory) &&
                               memcmp(amd_memory.bytes.pointer, expected_amd_memory, sizeof(expected_amd_memory)) == 0);

    String8 amd_memory_att_source =
        S8("vfrczps %ymm9, %ymm8\n"
           "vfrczpd %ymm9, %ymm8\n"
           "vpcmov %ymm11, %ymm10, %ymm9, %ymm8\n"
           "vpcmov (%rax), %ymm10, %ymm9, %ymm8\n"
           "vpcmov %ymm11, (%rax), %ymm9, %ymm8\n"
           "vfrczps 305419896(%r12,%r9,4), %xmm8\n"
           "vprotb -128(%r12,%r9,8), %xmm9, %xmm8\n"
           "vpshlq 127(%r12,%r9,8), %xmm9, %xmm8\n"
           "vpcmov 305419896(%r12,%r9,4), %xmm10, %xmm9, %xmm8\n"
           "vpperm 127(%r12,%r9,8), %xmm10, %xmm9, %xmm8\n"
           "vpperm %xmm10, 127(%r12,%r9,8), %xmm9, %xmm8\n"
           "vpmacssww %xmm10, 127(%r12,%r9,2), %xmm9, %xmm8\n"
           "vpermil2ps $7, %ymm10, -128(%r12,%r9,4), %ymm9, %ymm8\n"
           "vfmaddpd %ymm10, 305419896(%r12,%r9,8), %ymm9, %ymm8\n"
           "vfmaddsubps -128(%r12,%r9,8), %ymm10, %ymm9, %ymm8\n"
           "bextrq $287454020, 305419896(%r12,%r9,8), %r8\n"
           "blciq 127(%r12,%r9,8), %r8\n"
           "lwpins $287454020, 305419896(%r12,%r9,4), %r8\n"
           "pfadd 127(%r12,%r9,8), %mm0\n");
    u8 expected_amd_memory_att[] = {
        0x8f, 0x49, 0x7c, 0x80, 0xc1,
        0x8f, 0x49, 0x7c, 0x81, 0xc1,
        0x8f, 0x48, 0x34, 0xa2, 0xc2, 0xb0,
        0x8f, 0x68, 0xb4, 0xa2, 0x00, 0xa0,
        0x8f, 0x68, 0x34, 0xa2, 0x00, 0xb0,
        0x8f, 0x09, 0x78, 0x80, 0x84, 0x8c, 0x78, 0x56, 0x34, 0x12,
        0x8f, 0x09, 0xb0, 0x90, 0x44, 0xcc, 0x80,
        0x8f, 0x09, 0xb0, 0x97, 0x44, 0xcc, 0x7f,
        0x8f, 0x08, 0xb0, 0xa2, 0x84, 0x8c, 0x78, 0x56, 0x34, 0x12, 0xa0,
        0x8f, 0x08, 0xb0, 0xa3, 0x44, 0xcc, 0x7f, 0xa0,
        0x8f, 0x08, 0x30, 0xa3, 0x44, 0xcc, 0x7f, 0xa0,
        0x8f, 0x08, 0x30, 0x85, 0x44, 0x4c, 0x7f, 0xa0,
        0xc4, 0x03, 0x35, 0x48, 0x44, 0x8c, 0x80, 0xa7,
        0xc4, 0x03, 0x35, 0x69, 0x84, 0xcc, 0x78, 0x56, 0x34, 0x12, 0xa0,
        0xc4, 0x03, 0xb5, 0x5c, 0x44, 0xcc, 0x80, 0xa0,
        0x8f, 0x0a, 0xf8, 0x10, 0x84, 0xcc, 0x78, 0x56, 0x34, 0x12, 0x44, 0x33, 0x22, 0x11,
        0x8f, 0x89, 0xb8, 0x02, 0x74, 0xcc, 0x7f,
        0x8f, 0x8a, 0xb8, 0x12, 0x84, 0x8c, 0x78, 0x56, 0x34, 0x12, 0x44, 0x33, 0x22, 0x11,
        0x43, 0x0f, 0x0f, 0x44, 0xcc, 0x7f, 0x9e,
    };
    AssemblyEncodeResult amd_memory_att = assembly_encode(arguments->arena, amd_memory_att_source,
                                                           (AssemblyEncodeOptions){.target = amd_target, .syntax = ASSEMBLY_SYNTAX_ATT});
    BUSTER_TEST(arguments, amd_memory_att.diagnostic_count == 0 && amd_memory_att.bytes.length == sizeof(expected_amd_memory_att) &&
                               memcmp(amd_memory_att.bytes.pointer, expected_amd_memory_att, sizeof(expected_amd_memory_att)) == 0);

    AssemblyEncodeResult amd_rip_relocations = assembly_encode(
        arguments->arena,
        S8("vfrczps xmm0, xmmword ptr [rip + amd_external]\n"
           "vpcmov xmm0, xmm1, xmm2, xmmword ptr [rip + amd_external]\n"
           "vfmaddps xmm0, xmm1, xmm2, xmmword ptr [rip + amd_external]\n"
           "bextr r8, qword ptr [rip + amd_external], 0x11223344\n"
           "pfadd mm0, qword ptr [rip + amd_external]\n"),
        (AssemblyEncodeOptions){.target = amd_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_amd_rip_relocations[] = {
        0x8f, 0xe9, 0x78, 0x80, 0x05, 0x00, 0x00, 0x00, 0x00,
        0x8f, 0xe8, 0xf0, 0xa2, 0x05, 0x00, 0x00, 0x00, 0x00, 0x20,
        0xc4, 0xe3, 0xf1, 0x68, 0x05, 0x00, 0x00, 0x00, 0x00, 0x20,
        0x8f, 0x6a, 0xf8, 0x10, 0x05, 0x00, 0x00, 0x00, 0x00, 0x44, 0x33, 0x22, 0x11,
        0x0f, 0x0f, 0x05, 0x00, 0x00, 0x00, 0x00, 0x9e,
    };
    BUSTER_TEST(arguments, amd_rip_relocations.diagnostic_count == 0 && amd_rip_relocations.bytes.length == sizeof(expected_amd_rip_relocations) &&
                               memcmp(amd_rip_relocations.bytes.pointer, expected_amd_rip_relocations, sizeof(expected_amd_rip_relocations)) == 0);
    BUSTER_TEST(arguments, amd_rip_relocations.symbol_count == 1 && !amd_rip_relocations.symbols[0].defined &&
                               string_equal(amd_rip_relocations.symbols[0].name, S8("amd_external")) && amd_rip_relocations.relocation_count == 5);
    u64 amd_rip_relocation_offsets[] = {5, 14, 24, 34, 45};
    s64 amd_rip_relocation_addends[] = {-4, -5, -5, -8, -5};
    for (u32 relocation_index = 0; relocation_index < 5; relocation_index += 1)
    {
        BUSTER_TEST(arguments, amd_rip_relocations.relocations[relocation_index].offset == amd_rip_relocation_offsets[relocation_index] &&
                                   amd_rip_relocations.relocations[relocation_index].symbol == 0 &&
                                   amd_rip_relocations.relocations[relocation_index].addend == amd_rip_relocation_addends[relocation_index] &&
                                   amd_rip_relocations.relocations[relocation_index].kind == ASSEMBLY_RELOCATION_X86_PC32);
    }

    Target amd_no_xop = amd_target;
    amd_no_xop.cpu_features = target_cpu_features_remove(amd_no_xop.cpu_features, TARGET_CPU_FEATURE_X86_XOP);
    AssemblyEncodeResult unsupported_amd_xop = assembly_encode(arguments->arena, S8("vfrczps xmm0, xmm1\n"),
                                                                 (AssemblyEncodeOptions){.target = amd_no_xop, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, unsupported_amd_xop.diagnostic_count == 1 &&
                               unsupported_amd_xop.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE &&
                               string_equal(unsupported_amd_xop.diagnostics[0].message, S8("instruction requires the xop target feature")));
    Target amd_no_fma4 = amd_target;
    amd_no_fma4.cpu_features = target_cpu_features_remove(amd_no_fma4.cpu_features, TARGET_CPU_FEATURE_X86_FMA4);
    AssemblyEncodeResult unsupported_amd_fma4 = assembly_encode(arguments->arena, S8("vfmaddps xmm0, xmm1, xmm2, xmm3\n"),
                                                                  (AssemblyEncodeOptions){.target = amd_no_fma4, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, unsupported_amd_fma4.diagnostic_count == 1 &&
                               unsupported_amd_fma4.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE &&
                               string_equal(unsupported_amd_fma4.diagnostics[0].message, S8("instruction requires the fma4 target feature")));
    Target amd_no_3dnow = amd_target;
    amd_no_3dnow.cpu_features = target_cpu_features_remove(amd_no_3dnow.cpu_features, TARGET_CPU_FEATURE_X86_3DNOW);
    amd_no_3dnow.cpu_features = target_cpu_features_remove(amd_no_3dnow.cpu_features, TARGET_CPU_FEATURE_X86_3DNOWA);
    AssemblyEncodeResult unsupported_amd_3dnow = assembly_encode(arguments->arena, S8("pfadd mm0, mm1\n"),
                                                                   (AssemblyEncodeOptions){.target = amd_no_3dnow, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, unsupported_amd_3dnow.diagnostic_count == 1 &&
                               unsupported_amd_3dnow.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE &&
                               string_equal(unsupported_amd_3dnow.diagnostics[0].message, S8("instruction requires the 3dnow target feature")));
    Target amd_base_3dnow_only = amd_target;
    amd_base_3dnow_only.cpu_features = target_cpu_features_remove(amd_base_3dnow_only.cpu_features, TARGET_CPU_FEATURE_X86_3DNOWA);
    AssemblyEncodeResult unsupported_amd_3dnowa = assembly_encode(arguments->arena, S8("pi2fw mm0, mm1\n"),
                                                                    (AssemblyEncodeOptions){.target = amd_base_3dnow_only, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, unsupported_amd_3dnowa.diagnostic_count == 1 &&
                               unsupported_amd_3dnowa.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE &&
                               string_equal(unsupported_amd_3dnowa.diagnostics[0].message, S8("instruction requires the 3dnowa target feature")));
    AssemblyEncodeResult enhanced_amd_3dnow = assembly_encode(
        arguments->arena,
        S8("pi2fw mm0, mm1\n"
           "pf2iw mm0, mm1\n"
           "pfnacc mm0, mm1\n"
           "pfpnacc mm0, mm1\n"
           "pswapd mm0, mm1\n"),
        (AssemblyEncodeOptions){.target = amd_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    u8 expected_enhanced_amd_3dnow[] = {
        0x0f, 0x0f, 0xc1, 0x0c,
        0x0f, 0x0f, 0xc1, 0x1c,
        0x0f, 0x0f, 0xc1, 0x8a,
        0x0f, 0x0f, 0xc1, 0x8e,
        0x0f, 0x0f, 0xc1, 0xbb,
    };
    BUSTER_TEST(arguments, enhanced_amd_3dnow.diagnostic_count == 0 && enhanced_amd_3dnow.bytes.length == sizeof(expected_enhanced_amd_3dnow) &&
                               memcmp(enhanced_amd_3dnow.bytes.pointer, expected_enhanced_amd_3dnow, sizeof(expected_enhanced_amd_3dnow)) == 0);
    AssemblyEncodeResult unsupported_amd_3dnowa_all = assembly_encode(
        arguments->arena,
        S8("pi2fw mm0, mm1\n"
           "pf2iw mm0, mm1\n"
           "pfnacc mm0, mm1\n"
           "pfpnacc mm0, mm1\n"
           "pswapd mm0, mm1\n"),
        (AssemblyEncodeOptions){.target = amd_base_3dnow_only, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, unsupported_amd_3dnowa_all.diagnostic_count == 5);
    for (u32 diagnostic_index = 0; diagnostic_index < unsupported_amd_3dnowa_all.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, unsupported_amd_3dnowa_all.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE &&
                                   string_equal(unsupported_amd_3dnowa_all.diagnostics[diagnostic_index].message,
                                                S8("instruction requires the 3dnowa target feature")));
    }
    Target amd_no_tbm = amd_target;
    amd_no_tbm.cpu_features = target_cpu_features_remove(amd_no_tbm.cpu_features, TARGET_CPU_FEATURE_X86_TBM);
    AssemblyEncodeResult unsupported_amd_tbm = assembly_encode(arguments->arena, S8("bextr rax, rcx, 0x1\n"),
                                                                 (AssemblyEncodeOptions){.target = amd_no_tbm, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, unsupported_amd_tbm.diagnostic_count == 1 &&
                               unsupported_amd_tbm.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
    Target amd_no_lwp = amd_target;
    amd_no_lwp.cpu_features = target_cpu_features_remove(amd_no_lwp.cpu_features, TARGET_CPU_FEATURE_X86_LWP);
    AssemblyEncodeResult unsupported_amd_lwp = assembly_encode(arguments->arena, S8("llwpcb r8\n"),
                                                                (AssemblyEncodeOptions){.target = amd_no_lwp, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, unsupported_amd_lwp.diagnostic_count == 1 &&
                               unsupported_amd_lwp.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE &&
                               string_equal(unsupported_amd_lwp.diagnostics[0].message, S8("instruction requires the lwp target feature")));

    AssemblyEncodeResult invalid_amd_forms = assembly_encode(
        arguments->arena,
        S8("vfrczps ymm0, xmm1\n"
           "vfmaddss ymm0, ymm1, ymm2, ymm3\n"
           "vpcmov xmm0, xmm1, xmmword ptr [rax], xmmword ptr [rbx]\n"
           "vpmacssww xmm0, xmm1, xmm2, xmmword ptr [rax]\n"
           "vprotb xmm0, xmm1, 0x100\n"
           "vpermil2ps xmm0, xmm1, xmm2, xmm3, 0x10\n"
           "bextr rax, ecx, 0x1\n"
           "lwpins eax, ecx, 0x1\n"
           "pfadd xmm0, xmm1\n"),
        (AssemblyEncodeOptions){.target = amd_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_amd_forms.diagnostic_count == 9);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_amd_forms.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, invalid_amd_forms.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }
    AssemblyEncodeResult invalid_amd_bounds = assembly_encode(
        arguments->arena,
        S8("vpcomb xmm0, xmm1, xmm2, 0x100\n"
           "vpcomb xmm0, xmm1, 0x1\n"
           "vpcomw ymm0, ymm1, ymm2, 0\n"
           "vprotb xmm0, xmm1, xmm2, 0\n"
           "vpshlb xmm0, xmmword ptr [rax], xmmword ptr [rbx]\n"
           "vpcmov xmm0, xmm1, xmmword ptr [rax], xmmword ptr [rbx]\n"
           "vpmacssww xmm0, xmm1, xmm2, xmmword ptr [rax]\n"
           "vpermil2ps xmm0, xmm1, xmm2, xmm3, 16\n"
           "vpermil2pd xmm0, xmm1, xmm2, xmm3, 16\n"
           "vfmaddps xmm0, xmm1, xmmword ptr [rax], xmmword ptr [rbx]\n"
           "blci eax, rcx\n"
           "lwpval r8, r9, 1\n"
           "pi2fw xmm0, xmm1\n"),
        (AssemblyEncodeOptions){.target = amd_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
    BUSTER_TEST(arguments, invalid_amd_bounds.diagnostic_count == 13);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_amd_bounds.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, invalid_amd_bounds.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
    }

    {
        u8 expected_ud0[] = {0x0f, 0xff};
        AssemblyEncodeResult metadata_legacy = assembly_encode(arguments->arena, S8("ud0\n"),
                                                                (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, metadata_legacy.diagnostic_count == 0 && metadata_legacy.relocation_count == 0 &&
                                   metadata_legacy.bytes.length == sizeof(expected_ud0) &&
                                   memcmp(metadata_legacy.bytes.pointer, expected_ud0, sizeof(expected_ud0)) == 0);

        u8 expected_rex[] = {0x41, 0x0f, 0x01, 0xe0};
        AssemblyEncodeResult metadata_rex_intel = assembly_encode(arguments->arena, S8("smsw r8d\n"),
                                                                   (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult metadata_rex_att = assembly_encode(arguments->arena, S8("smswl %r8d\n"),
                                                                 (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, metadata_rex_intel.diagnostic_count == 0 && metadata_rex_intel.relocation_count == 0 &&
                                   metadata_rex_intel.bytes.length == sizeof(expected_rex) &&
                                   memcmp(metadata_rex_intel.bytes.pointer, expected_rex, sizeof(expected_rex)) == 0);
        BUSTER_TEST(arguments, metadata_rex_att.diagnostic_count == 0 && metadata_rex_att.relocation_count == 0 &&
                                   metadata_rex_att.bytes.length == sizeof(expected_rex) &&
                                   memcmp(metadata_rex_att.bytes.pointer, expected_rex, sizeof(expected_rex)) == 0);

        u8 expected_hlt[] = {0xf4};
        AssemblyEncodeResult metadata_system = assembly_encode(arguments->arena, S8("hlt\n"),
                                                                (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, metadata_system.diagnostic_count == 0 && metadata_system.relocation_count == 0 &&
                                   metadata_system.bytes.length == sizeof(expected_hlt) &&
                                   memcmp(metadata_system.bytes.pointer, expected_hlt, sizeof(expected_hlt)) == 0);

        u8 expected_smsw[] = {0x0f, 0x01, 0xe0};
        AssemblyEncodeResult metadata_control = assembly_encode(arguments->arena, S8("smsw eax\n"),
                                                                 (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, metadata_control.diagnostic_count == 0 && metadata_control.relocation_count == 0 &&
                                   metadata_control.bytes.length == sizeof(expected_smsw) &&
                                   memcmp(metadata_control.bytes.pointer, expected_smsw, sizeof(expected_smsw)) == 0);

        u8 expected_rex2[] = {0xd5, 0x18, 0x50};
        AssemblyEncodeResult metadata_rex2_intel = assembly_encode(arguments->arena, S8("pushp r16\n"),
                                                                    (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult metadata_rex2_att = assembly_encode(arguments->arena, S8("pushpq %r16\n"),
                                                                  (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, metadata_rex2_intel.diagnostic_count == 0 && metadata_rex2_intel.bytes.length == sizeof(expected_rex2) &&
                                   memcmp(metadata_rex2_intel.bytes.pointer, expected_rex2, sizeof(expected_rex2)) == 0);
        BUSTER_TEST(arguments, metadata_rex2_att.diagnostic_count == 0 && metadata_rex2_att.bytes.length == sizeof(expected_rex2) &&
                                   memcmp(metadata_rex2_att.bytes.pointer, expected_rex2, sizeof(expected_rex2)) == 0);

        u8 expected_apx[] = {0x62, 0xfc, 0x7c, 0x10, 0x83, 0xc1, 0x7b};
        AssemblyEncodeResult metadata_apx = assembly_encode(arguments->arena, S8("addl $123, %r17d, %r16d\n"),
                                                             (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, metadata_apx.diagnostic_count == 0 && metadata_apx.relocation_count == 0 &&
                                   metadata_apx.bytes.length == sizeof(expected_apx) &&
                                   memcmp(metadata_apx.bytes.pointer, expected_apx, sizeof(expected_apx)) == 0);
        AssemblyEncodeResult metadata_apx_missing = assembly_encode(arguments->arena, S8("addl $123, %r17d, %r16d\n"),
                                                                     (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, metadata_apx_missing.diagnostic_count == 1 &&
                                   metadata_apx_missing.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);

        Target apx_scc_target = advanced_target;
        apx_scc_target.cpu_features = target_cpu_features_add(apx_scc_target.cpu_features, TARGET_CPU_FEATURE_X86_APX_NCI_NDD_NF);
        u8 expected_apx_scc_byte[] = {0x62, 0x74, 0x14, 0x02, 0x38, 0xf2};
        AssemblyEncodeResult metadata_apx_scc_intel = assembly_encode(
            arguments->arena, S8("ccmpb 2, dl, r14b\n"),
            (AssemblyEncodeOptions){.target = apx_scc_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult metadata_apx_scc_att = assembly_encode(
            arguments->arena, S8("ccmpb $2, %r14b, %dl\n"),
            (AssemblyEncodeOptions){.target = apx_scc_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        u8 expected_apx_scc_dfv15[] = {0x62, 0x74, 0x7c, 0x02, 0x38, 0xf2};
        AssemblyEncodeResult metadata_apx_scc_dfv15_intel = assembly_encode(
            arguments->arena, S8("ccmpb 15, dl, r14b\n"),
            (AssemblyEncodeOptions){.target = apx_scc_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult metadata_apx_scc_dfv15_att = assembly_encode(
            arguments->arena, S8("ccmpb $15, %r14b, %dl\n"),
            (AssemblyEncodeOptions){.target = apx_scc_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, metadata_apx_scc_intel.diagnostic_count == 0 && metadata_apx_scc_intel.relocation_count == 0 &&
                                   metadata_apx_scc_intel.bytes.length == sizeof(expected_apx_scc_byte) &&
                                   memcmp(metadata_apx_scc_intel.bytes.pointer, expected_apx_scc_byte, sizeof(expected_apx_scc_byte)) == 0);
        BUSTER_TEST(arguments, metadata_apx_scc_att.diagnostic_count == 0 && metadata_apx_scc_att.relocation_count == 0 &&
                                   metadata_apx_scc_att.bytes.length == sizeof(expected_apx_scc_byte) &&
                                   memcmp(metadata_apx_scc_att.bytes.pointer, expected_apx_scc_byte, sizeof(expected_apx_scc_byte)) == 0);
        BUSTER_TEST(arguments, metadata_apx_scc_dfv15_intel.diagnostic_count == 0 &&
                                   metadata_apx_scc_dfv15_intel.relocation_count == 0 &&
                                   metadata_apx_scc_dfv15_intel.bytes.length == sizeof(expected_apx_scc_dfv15) &&
                                   memcmp(metadata_apx_scc_dfv15_intel.bytes.pointer, expected_apx_scc_dfv15,
                                          sizeof(expected_apx_scc_dfv15)) == 0);
        BUSTER_TEST(arguments, metadata_apx_scc_dfv15_att.diagnostic_count == 0 &&
                                   metadata_apx_scc_dfv15_att.relocation_count == 0 &&
                                   metadata_apx_scc_dfv15_att.bytes.length == sizeof(expected_apx_scc_dfv15) &&
                                   memcmp(metadata_apx_scc_dfv15_att.bytes.pointer, expected_apx_scc_dfv15,
                                          sizeof(expected_apx_scc_dfv15)) == 0);

        u8 expected_apx_scc_rip_dfv2[] = {0x62, 0x74, 0x14, 0x02, 0x38, 0x35, 0x00, 0x00, 0x00, 0x00};
        u8 expected_apx_scc_rip_dfv0[] = {0x62, 0x74, 0x04, 0x02, 0x38, 0x35, 0x00, 0x00, 0x00, 0x00};
        AssemblyEncodeResult metadata_apx_scc_rip_intel = assembly_encode(
            arguments->arena, S8("ccmpb 2, byte ptr [rip + ccmp_external], r14b\n"),
            (AssemblyEncodeOptions){.target = apx_scc_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult metadata_apx_scc_rip_att = assembly_encode(
            arguments->arena, S8("ccmpbb $0, %r14b, ccmp_external(%rip)\n"),
            (AssemblyEncodeOptions){.target = apx_scc_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, metadata_apx_scc_rip_intel.diagnostic_count == 0 &&
                                   metadata_apx_scc_rip_intel.bytes.length == sizeof(expected_apx_scc_rip_dfv2) &&
                                   memcmp(metadata_apx_scc_rip_intel.bytes.pointer, expected_apx_scc_rip_dfv2,
                                          sizeof(expected_apx_scc_rip_dfv2)) == 0 &&
                                   metadata_apx_scc_rip_intel.symbol_count == 1 && !metadata_apx_scc_rip_intel.symbols[0].defined &&
                                   string_equal(metadata_apx_scc_rip_intel.symbols[0].name, S8("ccmp_external")) &&
                                   metadata_apx_scc_rip_intel.relocation_count == 1 &&
                                   metadata_apx_scc_rip_intel.relocations[0].offset == 6 &&
                                   metadata_apx_scc_rip_intel.relocations[0].symbol == 0 &&
                                   metadata_apx_scc_rip_intel.relocations[0].addend == -4 &&
                                   metadata_apx_scc_rip_intel.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC32);
        BUSTER_TEST(arguments, metadata_apx_scc_rip_att.diagnostic_count == 0 &&
                                   metadata_apx_scc_rip_att.bytes.length == sizeof(expected_apx_scc_rip_dfv0) &&
                                   memcmp(metadata_apx_scc_rip_att.bytes.pointer, expected_apx_scc_rip_dfv0,
                                          sizeof(expected_apx_scc_rip_dfv0)) == 0 &&
                                   metadata_apx_scc_rip_att.relocation_count == 1 &&
                                   metadata_apx_scc_rip_att.relocations[0].offset == 6 &&
                                   metadata_apx_scc_rip_att.relocations[0].symbol == 0 &&
                                   metadata_apx_scc_rip_att.relocations[0].addend == -4 &&
                                   metadata_apx_scc_rip_att.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC32);

        u8 expected_apx_scc_memory_immediate[] = {0x62, 0xf4, 0x14, 0x02, 0x80, 0x38, 0x07};
        AssemblyEncodeResult metadata_apx_scc_memory_immediate_intel = assembly_encode(
            arguments->arena, S8("ccmpb 2, byte ptr [rax], 7\n"),
            (AssemblyEncodeOptions){.target = apx_scc_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult metadata_apx_scc_memory_immediate_att = assembly_encode(
            arguments->arena, S8("ccmpbb $2, $7, (%rax)\n"),
            (AssemblyEncodeOptions){.target = apx_scc_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, metadata_apx_scc_memory_immediate_intel.diagnostic_count == 0 &&
                                   metadata_apx_scc_memory_immediate_intel.relocation_count == 0 &&
                                   metadata_apx_scc_memory_immediate_intel.bytes.length == sizeof(expected_apx_scc_memory_immediate) &&
                                   memcmp(metadata_apx_scc_memory_immediate_intel.bytes.pointer, expected_apx_scc_memory_immediate,
                                          sizeof(expected_apx_scc_memory_immediate)) == 0);
        BUSTER_TEST(arguments, metadata_apx_scc_memory_immediate_att.diagnostic_count == 0 &&
                                   metadata_apx_scc_memory_immediate_att.relocation_count == 0 &&
                                   metadata_apx_scc_memory_immediate_att.bytes.length == sizeof(expected_apx_scc_memory_immediate) &&
                                   memcmp(metadata_apx_scc_memory_immediate_att.bytes.pointer, expected_apx_scc_memory_immediate,
                                          sizeof(expected_apx_scc_memory_immediate)) == 0);

        u8 expected_apx_scc_egpr_memory[] = {0x62, 0x7c, 0x10, 0x02, 0x38, 0x74, 0x51, 0x08};
        AssemblyEncodeResult metadata_apx_scc_egpr_memory = assembly_encode(
            arguments->arena, S8("ccmpb 2, byte ptr [r17+r18*2+8], r14b\n"),
            (AssemblyEncodeOptions){.target = apx_scc_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, metadata_apx_scc_egpr_memory.diagnostic_count == 0 &&
                                   metadata_apx_scc_egpr_memory.relocation_count == 0 &&
                                   metadata_apx_scc_egpr_memory.bytes.length == sizeof(expected_apx_scc_egpr_memory) &&
                                   memcmp(metadata_apx_scc_egpr_memory.bytes.pointer, expected_apx_scc_egpr_memory,
                                          sizeof(expected_apx_scc_egpr_memory)) == 0);

        u8 expected_apx_ctestz[] = {0x62, 0x74, 0x84, 0x04, 0x85, 0xf2};
        AssemblyEncodeResult metadata_apx_ctestz_intel = assembly_encode(
            arguments->arena, S8("ctestz 0, rdx, r14\n"),
            (AssemblyEncodeOptions){.target = apx_scc_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult metadata_apx_ctestz_att = assembly_encode(
            arguments->arena, S8("ctestz $0, %r14, %rdx\n"),
            (AssemblyEncodeOptions){.target = apx_scc_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, metadata_apx_ctestz_intel.diagnostic_count == 0 && metadata_apx_ctestz_intel.relocation_count == 0 &&
                                   metadata_apx_ctestz_intel.bytes.length == sizeof(expected_apx_ctestz) &&
                                   memcmp(metadata_apx_ctestz_intel.bytes.pointer, expected_apx_ctestz, sizeof(expected_apx_ctestz)) == 0);
        BUSTER_TEST(arguments, metadata_apx_ctestz_att.diagnostic_count == 0 && metadata_apx_ctestz_att.relocation_count == 0 &&
                                   metadata_apx_ctestz_att.bytes.length == sizeof(expected_apx_ctestz) &&
                                   memcmp(metadata_apx_ctestz_att.bytes.pointer, expected_apx_ctestz, sizeof(expected_apx_ctestz)) == 0);

        AssemblyEncodeResult metadata_apx_scc_missing_dfv = assembly_encode(
            arguments->arena, S8("ccmpb dl, r14b\n"),
            (AssemblyEncodeOptions){.target = apx_scc_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult metadata_apx_scc_negative_dfv = assembly_encode(
            arguments->arena, S8("ccmpb -1, dl, r14b\n"),
            (AssemblyEncodeOptions){.target = apx_scc_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult metadata_apx_scc_large_dfv = assembly_encode(
            arguments->arena, S8("ccmpb 16, dl, r14b\n"),
            (AssemblyEncodeOptions){.target = apx_scc_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult metadata_apx_scc_symbol_dfv = assembly_encode(
            arguments->arena, S8("ccmpb ccmp_dfv, dl, r14b\n"),
            (AssemblyEncodeOptions){.target = apx_scc_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult metadata_apx_scc_duplicate_dfv = assembly_encode(
            arguments->arena, S8("ccmpb 2, 3, dl, r14b\n"),
            (AssemblyEncodeOptions){.target = apx_scc_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult metadata_apx_scc_malformed_dfv = assembly_encode(
            arguments->arena, S8("ccmpb 0xffffffffffffffff, dl, r14b\n"),
            (AssemblyEncodeOptions){.target = apx_scc_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, metadata_apx_scc_missing_dfv.diagnostic_count == 1 &&
                                   metadata_apx_scc_missing_dfv.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS &&
                                   metadata_apx_scc_missing_dfv.bytes.length == 0 && metadata_apx_scc_missing_dfv.relocation_count == 0 &&
                                   metadata_apx_scc_missing_dfv.symbol_count == 0);
        BUSTER_TEST(arguments, metadata_apx_scc_negative_dfv.diagnostic_count == 1 &&
                                   metadata_apx_scc_negative_dfv.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS &&
                                   metadata_apx_scc_negative_dfv.bytes.length == 0 && metadata_apx_scc_negative_dfv.relocation_count == 0 &&
                                   metadata_apx_scc_negative_dfv.symbol_count == 0);
        BUSTER_TEST(arguments, metadata_apx_scc_large_dfv.diagnostic_count == 1 &&
                                   metadata_apx_scc_large_dfv.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS &&
                                   metadata_apx_scc_large_dfv.bytes.length == 0 && metadata_apx_scc_large_dfv.relocation_count == 0 &&
                                   metadata_apx_scc_large_dfv.symbol_count == 0);
        BUSTER_TEST(arguments, metadata_apx_scc_symbol_dfv.diagnostic_count == 1 &&
                                   metadata_apx_scc_symbol_dfv.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS &&
                                   metadata_apx_scc_symbol_dfv.bytes.length == 0 && metadata_apx_scc_symbol_dfv.relocation_count == 0 &&
                                   metadata_apx_scc_symbol_dfv.symbol_count == 0);
        BUSTER_TEST(arguments, metadata_apx_scc_duplicate_dfv.diagnostic_count == 1 &&
                                   metadata_apx_scc_duplicate_dfv.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS &&
                                   metadata_apx_scc_duplicate_dfv.bytes.length == 0 && metadata_apx_scc_duplicate_dfv.relocation_count == 0 &&
                                   metadata_apx_scc_duplicate_dfv.symbol_count == 0);
        BUSTER_TEST(arguments, metadata_apx_scc_malformed_dfv.diagnostic_count == 1 &&
                                   metadata_apx_scc_malformed_dfv.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS &&
                                   metadata_apx_scc_malformed_dfv.bytes.length == 0 && metadata_apx_scc_malformed_dfv.relocation_count == 0 &&
                                   metadata_apx_scc_malformed_dfv.symbol_count == 0);

        u8 expected_evex_r4_scalar[] = {0x62, 0xf1, 0x7f, 0x18, 0x2d, 0xc1};
        AssemblyEncodeResult metadata_evex_r4_intel = assembly_encode(
            arguments->arena, S8("vcvtsd2si {rn-sae}, eax, xmm1\n"),
            (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult metadata_evex_r4_att = assembly_encode(
            arguments->arena, S8("vcvtsd2si {rn-sae}, %xmm1, %eax\n"),
            (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, metadata_evex_r4_intel.diagnostic_count == 0 && metadata_evex_r4_intel.relocation_count == 0 &&
                                   metadata_evex_r4_intel.bytes.length == sizeof(expected_evex_r4_scalar) &&
                                   memcmp(metadata_evex_r4_intel.bytes.pointer, expected_evex_r4_scalar,
                                          sizeof(expected_evex_r4_scalar)) == 0);
        BUSTER_TEST(arguments, metadata_evex_r4_att.diagnostic_count == 0 && metadata_evex_r4_att.relocation_count == 0 &&
                                   metadata_evex_r4_att.bytes.length == sizeof(expected_evex_r4_scalar) &&
                                   memcmp(metadata_evex_r4_att.bytes.pointer, expected_evex_r4_scalar,
                                          sizeof(expected_evex_r4_scalar)) == 0);

        u8 expected_evex_r4_egpr_scalar[] = {0x62, 0xe1, 0x7f, 0x18, 0x2d, 0xc1};
        AssemblyEncodeResult metadata_evex_r4_egpr_intel = assembly_encode(
            arguments->arena, S8("vcvtsd2si {rn-sae}, r16d, xmm1\n"),
            (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult metadata_evex_r4_egpr_att = assembly_encode(
            arguments->arena, S8("vcvtsd2si {rn-sae}, %xmm1, %r16d\n"),
            (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, metadata_evex_r4_egpr_intel.diagnostic_count == 0 &&
                                   metadata_evex_r4_egpr_intel.relocation_count == 0 &&
                                   metadata_evex_r4_egpr_intel.bytes.length == sizeof(expected_evex_r4_egpr_scalar) &&
                                   memcmp(metadata_evex_r4_egpr_intel.bytes.pointer, expected_evex_r4_egpr_scalar,
                                          sizeof(expected_evex_r4_egpr_scalar)) == 0);
        BUSTER_TEST(arguments, metadata_evex_r4_egpr_att.diagnostic_count == 0 &&
                                   metadata_evex_r4_egpr_att.relocation_count == 0 &&
                                   metadata_evex_r4_egpr_att.bytes.length == sizeof(expected_evex_r4_egpr_scalar) &&
                                   memcmp(metadata_evex_r4_egpr_att.bytes.pointer, expected_evex_r4_egpr_scalar,
                                          sizeof(expected_evex_r4_egpr_scalar)) == 0);

        u8 expected_unsigned_immediate[] = {0x48, 0xb8, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
        AssemblyEncodeResult metadata_unsigned_immediate_intel = assembly_encode(
            arguments->arena, S8("mov rax, 0xffffffffffffffff\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult metadata_unsigned_immediate_att = assembly_encode(
            arguments->arena, S8("movq $0xffffffffffffffff, %rax\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, metadata_unsigned_immediate_intel.diagnostic_count == 0 &&
                                   metadata_unsigned_immediate_intel.bytes.length == sizeof(expected_unsigned_immediate) &&
                                   memcmp(metadata_unsigned_immediate_intel.bytes.pointer, expected_unsigned_immediate,
                                          sizeof(expected_unsigned_immediate)) == 0);
        BUSTER_TEST(arguments, metadata_unsigned_immediate_att.diagnostic_count == 0 &&
                                   metadata_unsigned_immediate_att.bytes.length == sizeof(expected_unsigned_immediate) &&
                                   memcmp(metadata_unsigned_immediate_att.bytes.pointer, expected_unsigned_immediate,
                                          sizeof(expected_unsigned_immediate)) == 0);

        {
            Target metadata_target = sse4a_target;
            u8 expected_extrq[] = {0x66, 0x0f, 0x78, 0xc0, 0x01, 0x02};
            u8 expected_extrq_att[] = {0x66, 0x0f, 0x78, 0xc0, 0x02, 0x01};
            u8 expected_insertq[] = {0xf2, 0x0f, 0x78, 0xc1, 0x01, 0x02};
            u8 expected_insertq_att[] = {0xf2, 0x0f, 0x78, 0xc8, 0x02, 0x01};
            Target intel_no_sse4a_target = x86_target;
            intel_no_sse4a_target.cpu_model = CPU_MODEL_INTEL_HASWELL;
            intel_no_sse4a_target.cpu_features_explicit = true;
            intel_no_sse4a_target.cpu_features = target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_INTEL_HASWELL);
            AssemblyEncodeResult metadata_sse4a_missing =
                assembly_encode(arguments->arena, S8("extrq xmm0, 1, 2\ninsertq xmm0, xmm1, 1, 2\n"),
                                (AssemblyEncodeOptions){.target = intel_no_sse4a_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
            BUSTER_TEST(arguments, metadata_sse4a_missing.diagnostic_count == 2 && metadata_sse4a_missing.bytes.length == 0 &&
                                       metadata_sse4a_missing.relocation_count == 0 && metadata_sse4a_missing.symbol_count == 0);
            for (u32 diagnostic_index = 0; diagnostic_index < metadata_sse4a_missing.diagnostic_count; diagnostic_index += 1)
            {
                BUSTER_TEST(arguments, metadata_sse4a_missing.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
            }
            AssemblyEncodeResult metadata_extrq_intel = assembly_encode(arguments->arena, S8("extrq xmm0, 1, 2\n"),
                                                                        (AssemblyEncodeOptions){.target = metadata_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
            AssemblyEncodeResult metadata_insertq_intel = assembly_encode(arguments->arena, S8("insertq xmm0, xmm1, 1, 2\n"),
                                                                          (AssemblyEncodeOptions){.target = metadata_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
            AssemblyEncodeResult metadata_extrq_att = assembly_encode(arguments->arena, S8("extrq $1, $2, %xmm0\n"),
                                                                      (AssemblyEncodeOptions){.target = metadata_target, .syntax = ASSEMBLY_SYNTAX_ATT});
            AssemblyEncodeResult metadata_insertq_att = assembly_encode(arguments->arena, S8("insertq $1, $2, %xmm0, %xmm1\n"),
                                                                        (AssemblyEncodeOptions){.target = metadata_target, .syntax = ASSEMBLY_SYNTAX_ATT});
            BUSTER_TEST(arguments, metadata_extrq_intel.diagnostic_count == 0 && metadata_extrq_intel.bytes.length == sizeof(expected_extrq) &&
                                       memcmp(metadata_extrq_intel.bytes.pointer, expected_extrq, sizeof(expected_extrq)) == 0);
            BUSTER_TEST(arguments, metadata_insertq_intel.diagnostic_count == 0 && metadata_insertq_intel.bytes.length == sizeof(expected_insertq) &&
                                       memcmp(metadata_insertq_intel.bytes.pointer, expected_insertq, sizeof(expected_insertq)) == 0);
            BUSTER_TEST(arguments, metadata_extrq_att.diagnostic_count == 0 && metadata_extrq_att.bytes.length == sizeof(expected_extrq_att) &&
                                       memcmp(metadata_extrq_att.bytes.pointer, expected_extrq_att, sizeof(expected_extrq_att)) == 0);
            BUSTER_TEST(arguments, metadata_insertq_att.diagnostic_count == 0 && metadata_insertq_att.bytes.length == sizeof(expected_insertq_att) &&
                                       memcmp(metadata_insertq_att.bytes.pointer, expected_insertq_att, sizeof(expected_insertq_att)) == 0);

            u8 expected_extrq_boundaries[] = {
                0x66, 0x0f, 0x78, 0xc0, 0x00, 0x00, 0x66, 0x0f, 0x78, 0xc0, 0xff, 0xff,
            };
            AssemblyEncodeResult metadata_extrq_boundaries =
                assembly_encode(arguments->arena, S8("extrq xmm0, 0, 0\nextrq xmm0, 255, 255\n"),
                                (AssemblyEncodeOptions){.target = metadata_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
            BUSTER_TEST(arguments,
                        metadata_extrq_boundaries.diagnostic_count == 0 &&
                            assembly_test_bytes_equal(metadata_extrq_boundaries.bytes, expected_extrq_boundaries, sizeof(expected_extrq_boundaries)) &&
                            metadata_extrq_boundaries.relocation_count == 0 && metadata_extrq_boundaries.symbol_count == 0);

            u8 expected_insertq_boundaries[] = {
                0xf2, 0x0f, 0x78, 0xc1, 0x00, 0x00, 0xf2, 0x0f, 0x78, 0xc1, 0xff, 0xff,
            };
            AssemblyEncodeResult metadata_insertq_boundaries =
                assembly_encode(arguments->arena, S8("insertq xmm0, xmm1, 0, 0\ninsertq xmm0, xmm1, 255, 255\n"),
                                (AssemblyEncodeOptions){.target = metadata_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
            BUSTER_TEST(arguments,
                        metadata_insertq_boundaries.diagnostic_count == 0 &&
                            assembly_test_bytes_equal(metadata_insertq_boundaries.bytes, expected_insertq_boundaries, sizeof(expected_insertq_boundaries)) &&
                            metadata_insertq_boundaries.relocation_count == 0 && metadata_insertq_boundaries.symbol_count == 0);

            AssemblyEncodeResult metadata_extrq_invalid = assembly_encode(arguments->arena, S8("extrq xmm0, -1, 0\nextrq xmm0, 0, 256\n"),
                                                                          (AssemblyEncodeOptions){.target = metadata_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
            AssemblyEncodeResult metadata_insertq_invalid =
                assembly_encode(arguments->arena, S8("insertq xmm0, xmm1, -1, 0\ninsertq xmm0, xmm1, 0, 256\n"),
                                (AssemblyEncodeOptions){.target = metadata_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
            BUSTER_TEST(arguments, metadata_extrq_invalid.diagnostic_count == 2 && metadata_extrq_invalid.bytes.length == 0 &&
                                       metadata_extrq_invalid.relocation_count == 0 && metadata_extrq_invalid.symbol_count == 0);
            BUSTER_TEST(arguments, metadata_insertq_invalid.diagnostic_count == 2 && metadata_insertq_invalid.bytes.length == 0 &&
                                       metadata_insertq_invalid.relocation_count == 0 && metadata_insertq_invalid.symbol_count == 0);
            for (u32 diagnostic_index = 0; diagnostic_index < 2; diagnostic_index += 1)
            {
                BUSTER_TEST(arguments, metadata_extrq_invalid.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_INVALID_EXPRESSION);
                BUSTER_TEST(arguments, metadata_insertq_invalid.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_INVALID_EXPRESSION);
            }

            AssemblyEncodeResult metadata_extrq_shape = assembly_encode(arguments->arena, S8("extrq xmm0, 1\nextrq eax, 1, 2\n"),
                                                                        (AssemblyEncodeOptions){.target = metadata_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
            AssemblyEncodeResult metadata_insertq_shape = assembly_encode(arguments->arena, S8("insertq xmm0, 1, 2\ninsertq rax, xmm1, 1, 2\n"),
                                                                          (AssemblyEncodeOptions){.target = metadata_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
            BUSTER_TEST(arguments, metadata_extrq_shape.diagnostic_count == 2 && metadata_extrq_shape.bytes.length == 0 &&
                                       metadata_extrq_shape.relocation_count == 0 && metadata_extrq_shape.symbol_count == 0);
            BUSTER_TEST(arguments, metadata_insertq_shape.diagnostic_count == 2 && metadata_insertq_shape.bytes.length == 0 &&
                                       metadata_insertq_shape.relocation_count == 0 && metadata_insertq_shape.symbol_count == 0);
            for (u32 diagnostic_index = 0; diagnostic_index < 2; diagnostic_index += 1)
            {
                BUSTER_TEST(arguments, metadata_extrq_shape.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
                BUSTER_TEST(arguments, metadata_insertq_shape.diagnostics[diagnostic_index].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);
            }

            u8 expected_extrq_symbols[] = {0x66, 0x0f, 0x78, 0xc0, 0x00, 0x00};
            u8 expected_insertq_symbols[] = {0xf2, 0x0f, 0x78, 0xc1, 0x00, 0x00};
            u8 expected_extrq_symbols_att[] = {0x66, 0x0f, 0x78, 0xc0, 0x00, 0x00};
            u8 expected_insertq_symbols_att[] = {0xf2, 0x0f, 0x78, 0xc8, 0x00, 0x00};
            AssemblyEncodeResult metadata_extrq_symbols = assembly_encode(arguments->arena, S8("extrq xmm0, extrq_lo, extrq_hi\n"),
                                                                          (AssemblyEncodeOptions){.target = metadata_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
            AssemblyEncodeResult metadata_insertq_symbols =
                assembly_encode(arguments->arena, S8("insertq xmm0, xmm1, insertq_lo, insertq_hi\n"),
                                (AssemblyEncodeOptions){.target = metadata_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
            BUSTER_TEST(arguments, metadata_extrq_symbols.diagnostic_count == 0 &&
                                       assembly_test_bytes_equal(metadata_extrq_symbols.bytes, expected_extrq_symbols, sizeof(expected_extrq_symbols)) &&
                                       metadata_extrq_symbols.symbol_count == 2 && metadata_extrq_symbols.relocation_count == 2);
            BUSTER_TEST(arguments, metadata_insertq_symbols.diagnostic_count == 0 &&
                                       assembly_test_bytes_equal(metadata_insertq_symbols.bytes, expected_insertq_symbols, sizeof(expected_insertq_symbols)) &&
                                       metadata_insertq_symbols.symbol_count == 2 && metadata_insertq_symbols.relocation_count == 2);
            BUSTER_TEST(arguments, metadata_extrq_symbols.relocations[0].offset == 4 && metadata_extrq_symbols.relocations[1].offset == 5 &&
                                       metadata_extrq_symbols.relocations[0].kind == ASSEMBLY_RELOCATION_X86_ABSOLUTE8 &&
                                       metadata_extrq_symbols.relocations[1].kind == ASSEMBLY_RELOCATION_X86_ABSOLUTE8 &&
                                       string_equal(metadata_extrq_symbols.symbols[metadata_extrq_symbols.relocations[0].symbol].name, S8("extrq_lo")) &&
                                       string_equal(metadata_extrq_symbols.symbols[metadata_extrq_symbols.relocations[1].symbol].name, S8("extrq_hi")));
            BUSTER_TEST(arguments, metadata_insertq_symbols.relocations[0].offset == 4 && metadata_insertq_symbols.relocations[1].offset == 5 &&
                                       metadata_insertq_symbols.relocations[0].kind == ASSEMBLY_RELOCATION_X86_ABSOLUTE8 &&
                                       metadata_insertq_symbols.relocations[1].kind == ASSEMBLY_RELOCATION_X86_ABSOLUTE8 &&
                                       string_equal(metadata_insertq_symbols.symbols[metadata_insertq_symbols.relocations[0].symbol].name, S8("insertq_lo")) &&
                                       string_equal(metadata_insertq_symbols.symbols[metadata_insertq_symbols.relocations[1].symbol].name, S8("insertq_hi")));

            AssemblyEncodeResult metadata_extrq_symbols_att =
                assembly_encode(arguments->arena, S8("extrq $extrq_att_lo, $extrq_att_hi, %xmm0\n"),
                                (AssemblyEncodeOptions){.target = metadata_target, .syntax = ASSEMBLY_SYNTAX_ATT});
            AssemblyEncodeResult metadata_insertq_symbols_att =
                assembly_encode(arguments->arena, S8("insertq $insertq_att_lo, $insertq_att_hi, %xmm0, %xmm1\n"),
                                (AssemblyEncodeOptions){.target = metadata_target, .syntax = ASSEMBLY_SYNTAX_ATT});
            BUSTER_TEST(arguments,
                        metadata_extrq_symbols_att.diagnostic_count == 0 &&
                            assembly_test_bytes_equal(metadata_extrq_symbols_att.bytes, expected_extrq_symbols_att, sizeof(expected_extrq_symbols_att)) &&
                            metadata_extrq_symbols_att.symbol_count == 2 && metadata_extrq_symbols_att.relocation_count == 2);
            BUSTER_TEST(arguments,
                        metadata_insertq_symbols_att.diagnostic_count == 0 &&
                            assembly_test_bytes_equal(metadata_insertq_symbols_att.bytes, expected_insertq_symbols_att, sizeof(expected_insertq_symbols_att)) &&
                            metadata_insertq_symbols_att.symbol_count == 2 && metadata_insertq_symbols_att.relocation_count == 2);
            BUSTER_TEST(arguments,
                        metadata_extrq_symbols_att.relocations[0].offset == 4 && metadata_extrq_symbols_att.relocations[1].offset == 5 &&
                            metadata_extrq_symbols_att.relocations[0].kind == ASSEMBLY_RELOCATION_X86_ABSOLUTE8 &&
                            metadata_extrq_symbols_att.relocations[1].kind == ASSEMBLY_RELOCATION_X86_ABSOLUTE8 &&
                            string_equal(metadata_extrq_symbols_att.symbols[metadata_extrq_symbols_att.relocations[0].symbol].name, S8("extrq_att_hi")) &&
                            string_equal(metadata_extrq_symbols_att.symbols[metadata_extrq_symbols_att.relocations[1].symbol].name, S8("extrq_att_lo")));
            BUSTER_TEST(arguments,
                        metadata_insertq_symbols_att.relocations[0].offset == 4 && metadata_insertq_symbols_att.relocations[1].offset == 5 &&
                            metadata_insertq_symbols_att.relocations[0].kind == ASSEMBLY_RELOCATION_X86_ABSOLUTE8 &&
                            metadata_insertq_symbols_att.relocations[1].kind == ASSEMBLY_RELOCATION_X86_ABSOLUTE8 &&
                            string_equal(metadata_insertq_symbols_att.symbols[metadata_insertq_symbols_att.relocations[0].symbol].name, S8("insertq_att_hi")) &&
                            string_equal(metadata_insertq_symbols_att.symbols[metadata_insertq_symbols_att.relocations[1].symbol].name, S8("insertq_att_lo")));
        }

        u8 expected_movq_exact[] = {0x0f, 0x6f, 0xc1};
        AssemblyEncodeResult metadata_movq_exact_intel =
            assembly_encode(arguments->arena, S8("movq mm0, mm1\n"), (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult metadata_movq_exact_att = assembly_encode(
            arguments->arena, S8("movq %mm1, %mm0\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, metadata_movq_exact_intel.diagnostic_count == 0 &&
                                   metadata_movq_exact_intel.bytes.length == sizeof(expected_movq_exact) &&
                                   memcmp(metadata_movq_exact_intel.bytes.pointer, expected_movq_exact, sizeof(expected_movq_exact)) == 0);
        BUSTER_TEST(arguments, metadata_movq_exact_att.diagnostic_count == 0 &&
                                   metadata_movq_exact_att.bytes.length == sizeof(expected_movq_exact) &&
                                   memcmp(metadata_movq_exact_att.bytes.pointer, expected_movq_exact, sizeof(expected_movq_exact)) == 0);

        AssemblyEncodeResult metadata_invalid_suffix = assembly_encode(
            arguments->arena, S8("hltq\nsmswq %r8d\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, metadata_invalid_suffix.diagnostic_count == 2 && metadata_invalid_suffix.bytes.length == 0 &&
                                   metadata_invalid_suffix.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNKNOWN_INSTRUCTION &&
                                   metadata_invalid_suffix.diagnostics[1].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS);

        u8 expected_relative_literal[] = {0x90, 0xeb, 0xfd};
        AssemblyEncodeResult metadata_relative_literal_intel = assembly_encode(
            arguments->arena, S8("nop\njmp_near 0\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult metadata_relative_literal_att = assembly_encode(
            arguments->arena, S8("nop\njmp_near $0\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, metadata_relative_literal_intel.diagnostic_count == 0 &&
                                   metadata_relative_literal_intel.bytes.length == sizeof(expected_relative_literal) &&
                                   memcmp(metadata_relative_literal_intel.bytes.pointer, expected_relative_literal,
                                          sizeof(expected_relative_literal)) == 0);
        BUSTER_TEST(arguments, metadata_relative_literal_att.diagnostic_count == 0 &&
                                   metadata_relative_literal_att.bytes.length == sizeof(expected_relative_literal) &&
                                   memcmp(metadata_relative_literal_att.bytes.pointer, expected_relative_literal,
                                          sizeof(expected_relative_literal)) == 0);

        u8 expected_int_symbol[] = {0xcd, 0x00};
        AssemblyEncodeResult metadata_int_external = assembly_encode(
            arguments->arena, S8("int external\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, metadata_int_external.diagnostic_count == 0 && metadata_int_external.symbol_count == 1 &&
                                   !metadata_int_external.symbols[0].defined && metadata_int_external.relocation_count == 1 &&
                                   metadata_int_external.relocations[0].offset == 1 && metadata_int_external.relocations[0].symbol == 0 &&
                                   metadata_int_external.relocations[0].addend == 0 &&
                                   metadata_int_external.relocations[0].kind == ASSEMBLY_RELOCATION_X86_ABSOLUTE8 &&
                                   metadata_int_external.bytes.length == sizeof(expected_int_symbol) &&
                                   memcmp(metadata_int_external.bytes.pointer, expected_int_symbol, sizeof(expected_int_symbol)) == 0);
        AssemblyEncodeResult metadata_int_local = assembly_encode(
            arguments->arena, S8("local:\nint local\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, metadata_int_local.diagnostic_count == 0 && metadata_int_local.symbol_count == 1 &&
                                   metadata_int_local.symbols[0].defined && metadata_int_local.symbols[0].offset == 0 &&
                                   metadata_int_local.relocation_count == 0 && metadata_int_local.bytes.length == sizeof(expected_int_symbol) &&
                                   memcmp(metadata_int_local.bytes.pointer, expected_int_symbol, sizeof(expected_int_symbol)) == 0);

        u8 expected_vex_address32[] = {0x67, 0xc5, 0xf0, 0x58, 0x03};
        AssemblyEncodeResult metadata_vex_intel = assembly_encode(
            arguments->arena, S8("vaddps xmm0, xmm1, xmmword ptr [ebx]\n"),
            (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult metadata_vex_att = assembly_encode(
            arguments->arena, S8("vaddps (%ebx), %xmm1, %xmm0\n"),
            (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, metadata_vex_intel.diagnostic_count == 0 && metadata_vex_intel.bytes.length == sizeof(expected_vex_address32) &&
                                   memcmp(metadata_vex_intel.bytes.pointer, expected_vex_address32, sizeof(expected_vex_address32)) == 0);
        BUSTER_TEST(arguments, metadata_vex_att.diagnostic_count == 0 && metadata_vex_att.bytes.length == sizeof(expected_vex_address32) &&
                                   memcmp(metadata_vex_att.bytes.pointer, expected_vex_address32, sizeof(expected_vex_address32)) == 0);

        u8 expected_vex_ymm_address32[] = {0x67, 0xc5, 0xf4, 0x58, 0x03};
        AssemblyEncodeResult metadata_vex_ymm_address32 = assembly_encode(
            arguments->arena, S8("vaddps ymm0, ymm1, ymmword ptr [ebx]\n"),
            (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, metadata_vex_ymm_address32.diagnostic_count == 0 &&
                                   metadata_vex_ymm_address32.bytes.length == sizeof(expected_vex_ymm_address32) &&
                                   memcmp(metadata_vex_ymm_address32.bytes.pointer, expected_vex_ymm_address32,
                                          sizeof(expected_vex_ymm_address32)) == 0);

        u8 expected_evex_zmm_memory[] = {0x62, 0xf9, 0x74, 0x48, 0x58, 0x00};
        AssemblyEncodeResult metadata_evex_zmm_qualified = assembly_encode(
            arguments->arena, S8("vaddps zmm0, zmm1, zmmword ptr [r16]\n"),
            (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, metadata_evex_zmm_qualified.diagnostic_count == 0 &&
                                   metadata_evex_zmm_qualified.bytes.length == sizeof(expected_evex_zmm_memory) &&
                                   memcmp(metadata_evex_zmm_qualified.bytes.pointer, expected_evex_zmm_memory,
                                          sizeof(expected_evex_zmm_memory)) == 0);
        AssemblyEncodeResult metadata_evex_mismatched_qualifier = assembly_encode(
            arguments->arena, S8("vaddps zmm0, zmm1, xmmword ptr [r16]\n"),
            (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, metadata_evex_mismatched_qualifier.diagnostic_count == 1 &&
                                   metadata_evex_mismatched_qualifier.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS &&
                                   metadata_evex_mismatched_qualifier.bytes.length == 0 &&
                                   metadata_evex_mismatched_qualifier.symbol_count == 0 &&
                                   metadata_evex_mismatched_qualifier.relocation_count == 0);

        AssemblyEncodeResult metadata_vex_relocation_intel = assembly_encode(
            arguments->arena, S8("vaddps xmm0, xmm1, xmmword ptr [ebx + external + 8]\n"),
            (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult metadata_vex_relocation_att = assembly_encode(
            arguments->arena, S8("vaddps external+8(%ebx), %xmm1, %xmm0\n"),
            (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        u8 expected_vex_relocation[] = {0x67, 0xc5, 0xf0, 0x58, 0x83, 0x00, 0x00, 0x00, 0x00};
        BUSTER_TEST(arguments, metadata_vex_relocation_intel.diagnostic_count == 0 && metadata_vex_relocation_intel.symbol_count == 1 &&
                                   metadata_vex_relocation_intel.bytes.length == sizeof(expected_vex_relocation) &&
                                   memcmp(metadata_vex_relocation_intel.bytes.pointer, expected_vex_relocation,
                                          sizeof(expected_vex_relocation)) == 0 && metadata_vex_relocation_intel.relocation_count == 1 &&
                                   !metadata_vex_relocation_intel.symbols[0].defined &&
                                   string_equal(metadata_vex_relocation_intel.symbols[0].name, S8("external")) &&
                                   metadata_vex_relocation_intel.relocations[0].offset == 5 &&
                                   metadata_vex_relocation_intel.relocations[0].symbol == 0 &&
                                   metadata_vex_relocation_intel.relocations[0].addend == 8 &&
                                   metadata_vex_relocation_intel.relocations[0].kind == ASSEMBLY_RELOCATION_X86_ABSOLUTE32_SIGN_EXTENDED);
        BUSTER_TEST(arguments, metadata_vex_relocation_att.diagnostic_count == 0 && metadata_vex_relocation_att.symbol_count == 1 &&
                                   metadata_vex_relocation_att.bytes.length == sizeof(expected_vex_relocation) &&
                                   memcmp(metadata_vex_relocation_att.bytes.pointer, expected_vex_relocation,
                                          sizeof(expected_vex_relocation)) == 0 && metadata_vex_relocation_att.relocation_count == 1 &&
                                   !metadata_vex_relocation_att.symbols[0].defined &&
                                   string_equal(metadata_vex_relocation_att.symbols[0].name, S8("external")) &&
                                   metadata_vex_relocation_att.relocations[0].offset == 5 &&
                                   metadata_vex_relocation_att.relocations[0].symbol == 0 &&
                                   metadata_vex_relocation_att.relocations[0].addend == 8 &&
                                   metadata_vex_relocation_att.relocations[0].kind == ASSEMBLY_RELOCATION_X86_ABSOLUTE32_SIGN_EXTENDED);

        u8 expected_vpternlogd_two_relocations[] = {
            0x62, 0xf3, 0x75, 0x48, 0x25, 0x04, 0x25, 0x00, 0x00, 0x00, 0x00, 0x00,
        };
        AssemblyEncodeResult metadata_vpternlogd_no_newline = assembly_encode(
            arguments->arena, S8("vpternlogd zmm0, zmm1, zmmword ptr [mem_symbol], imm_symbol"),
            (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult metadata_vpternlogd_newline = assembly_encode(
            arguments->arena, S8("vpternlogd zmm0, zmm1, zmmword ptr [mem_symbol], imm_symbol\n"),
            (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, metadata_vpternlogd_no_newline.diagnostic_count == 0 &&
                                   metadata_vpternlogd_no_newline.bytes.length == sizeof(expected_vpternlogd_two_relocations) &&
                                   memcmp(metadata_vpternlogd_no_newline.bytes.pointer, expected_vpternlogd_two_relocations,
                                          sizeof(expected_vpternlogd_two_relocations)) == 0 &&
                                   metadata_vpternlogd_no_newline.symbol_count == 2 &&
                                   string_equal(metadata_vpternlogd_no_newline.symbols[0].name, S8("mem_symbol")) &&
                                   !metadata_vpternlogd_no_newline.symbols[0].defined &&
                                   string_equal(metadata_vpternlogd_no_newline.symbols[1].name, S8("imm_symbol")) &&
                                   !metadata_vpternlogd_no_newline.symbols[1].defined &&
                                   metadata_vpternlogd_no_newline.relocation_count == 2 &&
                                   metadata_vpternlogd_no_newline.relocations[0].offset == 7 &&
                                   metadata_vpternlogd_no_newline.relocations[0].symbol == 0 &&
                                   metadata_vpternlogd_no_newline.relocations[0].addend == 0 &&
                                   metadata_vpternlogd_no_newline.relocations[0].kind == ASSEMBLY_RELOCATION_X86_ABSOLUTE32_SIGN_EXTENDED &&
                                   metadata_vpternlogd_no_newline.relocations[1].offset == 11 &&
                                   metadata_vpternlogd_no_newline.relocations[1].symbol == 1 &&
                                   metadata_vpternlogd_no_newline.relocations[1].addend == 0 &&
                                   metadata_vpternlogd_no_newline.relocations[1].kind == ASSEMBLY_RELOCATION_X86_ABSOLUTE8);
        BUSTER_TEST(arguments, metadata_vpternlogd_newline.diagnostic_count == 0 &&
                                   metadata_vpternlogd_newline.bytes.length == metadata_vpternlogd_no_newline.bytes.length &&
                                   memcmp(metadata_vpternlogd_newline.bytes.pointer, metadata_vpternlogd_no_newline.bytes.pointer,
                                          metadata_vpternlogd_no_newline.bytes.length) == 0 &&
                                   metadata_vpternlogd_newline.symbol_count == metadata_vpternlogd_no_newline.symbol_count &&
                                   metadata_vpternlogd_newline.relocation_count == metadata_vpternlogd_no_newline.relocation_count &&
                                   metadata_vpternlogd_newline.relocations[0].offset == metadata_vpternlogd_no_newline.relocations[0].offset &&
                                   metadata_vpternlogd_newline.relocations[0].symbol == metadata_vpternlogd_no_newline.relocations[0].symbol &&
                                   metadata_vpternlogd_newline.relocations[0].addend == metadata_vpternlogd_no_newline.relocations[0].addend &&
                                   metadata_vpternlogd_newline.relocations[0].kind == metadata_vpternlogd_no_newline.relocations[0].kind &&
                                   metadata_vpternlogd_newline.relocations[1].offset == metadata_vpternlogd_no_newline.relocations[1].offset &&
                                   metadata_vpternlogd_newline.relocations[1].symbol == metadata_vpternlogd_no_newline.relocations[1].symbol &&
                                   metadata_vpternlogd_newline.relocations[1].addend == metadata_vpternlogd_no_newline.relocations[1].addend &&
                                   metadata_vpternlogd_newline.relocations[1].kind == metadata_vpternlogd_no_newline.relocations[1].kind);
        AssemblyEncodeResult metadata_vex_local = assembly_encode(
            arguments->arena, S8("local:\n"
                                 "vaddps xmm0, xmm1, xmmword ptr [ebx + local + 8]\n"),
            (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        u8 expected_vex_local[] = {0x67, 0xc5, 0xf0, 0x58, 0x83, 0x08, 0x00, 0x00, 0x00};
        BUSTER_TEST(arguments, metadata_vex_local.diagnostic_count == 0 && metadata_vex_local.symbol_count == 1 &&
                                   metadata_vex_local.symbols[0].defined && metadata_vex_local.symbols[0].offset == 0 &&
                                   string_equal(metadata_vex_local.symbols[0].name, S8("local")) &&
                                   metadata_vex_local.relocation_count == 0 && metadata_vex_local.bytes.length == sizeof(expected_vex_local) &&
                                   memcmp(metadata_vex_local.bytes.pointer, expected_vex_local, sizeof(expected_vex_local)) == 0);

        u8 expected_xop_address32[] = {0x67, 0x8f, 0xe8, 0x70, 0xa3, 0x03, 0x20};
        AssemblyEncodeResult metadata_xop_intel = assembly_encode(
            arguments->arena, S8("vpperm xmm0, xmm1, xmmword ptr [ebx], xmm2\n"),
            (AssemblyEncodeOptions){.target = amd_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult metadata_xop_att = assembly_encode(
            arguments->arena, S8("vpperm %xmm2, (%ebx), %xmm1, %xmm0\n"),
            (AssemblyEncodeOptions){.target = amd_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, metadata_xop_intel.diagnostic_count == 0 && metadata_xop_intel.bytes.length == sizeof(expected_xop_address32) &&
                                   memcmp(metadata_xop_intel.bytes.pointer, expected_xop_address32, sizeof(expected_xop_address32)) == 0);
        BUSTER_TEST(arguments, metadata_xop_att.diagnostic_count == 0 && metadata_xop_att.bytes.length == sizeof(expected_xop_address32) &&
                                   memcmp(metadata_xop_att.bytes.pointer, expected_xop_address32, sizeof(expected_xop_address32)) == 0);

        Target gather_target = advanced_target;
        gather_target.cpu_features = target_cpu_features_add(gather_target.cpu_features, TARGET_CPU_FEATURE_X86_AVX2);
        AssemblyEncodeResult metadata_vsib_intel = assembly_encode(
            arguments->arena, S8("vgatherdps xmm0, dword ptr [rax + xmm1*4], xmm2\n"),
            (AssemblyEncodeOptions){.target = gather_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult metadata_vsib_att = assembly_encode(
            arguments->arena, S8("vgatherdps %xmm2, (%rax,%xmm1,4), %xmm0\n"),
            (AssemblyEncodeOptions){.target = gather_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        // The final XMM operand is VEX.vvvv (the distinct destination); it
        // must not be dropped merely because the form also carries VSIB.
        u8 expected_vsib[] = {0xc4, 0xe2, 0x69, 0x92, 0x04, 0x88};
        BUSTER_TEST(arguments, metadata_vsib_intel.diagnostic_count == 0 && metadata_vsib_intel.bytes.length == sizeof(expected_vsib) &&
                                   memcmp(metadata_vsib_intel.bytes.pointer, expected_vsib, sizeof(expected_vsib)) == 0);
        BUSTER_TEST(arguments, metadata_vsib_att.diagnostic_count == 0 && metadata_vsib_att.bytes.length == sizeof(expected_vsib) &&
                                   memcmp(metadata_vsib_att.bytes.pointer, expected_vsib, sizeof(expected_vsib)) == 0);
        AssemblyEncodeResult metadata_vsib_invalid = assembly_encode(
            arguments->arena, S8("vgatherdps xmm0, dword ptr [rax + rcx*4], xmm2\n"),
            (AssemblyEncodeOptions){.target = gather_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, metadata_vsib_invalid.diagnostic_count == 1 &&
                                   metadata_vsib_invalid.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS &&
                                   string_equal(metadata_vsib_invalid.diagnostics[0].message, S8("metadata instruction form is not encodable")) &&
                                   metadata_vsib_invalid.bytes.length == 0 && metadata_vsib_invalid.symbol_count == 0 &&
                                   metadata_vsib_invalid.relocation_count == 0);
        Target gather_no_avx2 = gather_target;
        gather_no_avx2.cpu_features = target_cpu_features_remove(gather_no_avx2.cpu_features, TARGET_CPU_FEATURE_X86_AVX2);
        AssemblyEncodeResult metadata_vsib_missing = assembly_encode(
            arguments->arena, S8("vgatherdps xmm0, dword ptr [rax + xmm1*4], xmm2\n"),
            (AssemblyEncodeOptions){.target = gather_no_avx2, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, metadata_vsib_missing.diagnostic_count == 1 &&
                                   metadata_vsib_missing.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE &&
                                   metadata_vsib_missing.bytes.length == 0 && metadata_vsib_missing.symbol_count == 0 &&
                                   metadata_vsib_missing.relocation_count == 0);

        u8 expected_evex[] = {0x62, 0xf2, 0x6d, 0x49, 0x65, 0xc3};
        AssemblyEncodeResult metadata_evex_intel = assembly_encode(
            arguments->arena, S8("vblendmps zmm0 {k1}, zmm2, zmm3\n"),
            (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult metadata_evex_att = assembly_encode(
            arguments->arena, S8("vblendmps %zmm3, %zmm2, %zmm0 {%k1}\n"),
            (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, metadata_evex_intel.diagnostic_count == 0 && metadata_evex_intel.bytes.length == sizeof(expected_evex) &&
                                   memcmp(metadata_evex_intel.bytes.pointer, expected_evex, sizeof(expected_evex)) == 0);
        BUSTER_TEST(arguments, metadata_evex_att.diagnostic_count == 0 && metadata_evex_att.bytes.length == sizeof(expected_evex) &&
                                   memcmp(metadata_evex_att.bytes.pointer, expected_evex, sizeof(expected_evex)) == 0);

        u8 expected_amx[] = {0xc4, 0xe2, 0x78, 0x49, 0xc0};
        AssemblyEncodeResult metadata_amx_intel = assembly_encode(arguments->arena, S8("tilerelease\n"),
                                                                   (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult metadata_amx_att = assembly_encode(arguments->arena, S8("tilerelease\n"),
                                                                 (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, metadata_amx_intel.diagnostic_count == 0 && metadata_amx_intel.bytes.length == sizeof(expected_amx) &&
                                   memcmp(metadata_amx_intel.bytes.pointer, expected_amx, sizeof(expected_amx)) == 0);
        BUSTER_TEST(arguments, metadata_amx_att.diagnostic_count == 0 && metadata_amx_att.bytes.length == sizeof(expected_amx) &&
                                   memcmp(metadata_amx_att.bytes.pointer, expected_amx, sizeof(expected_amx)) == 0);

        u8 expected_segment[] = {0x64, 0x8b, 0x03};
        AssemblyEncodeResult metadata_segment_intel = assembly_encode(
            arguments->arena, S8("mov eax, dword ptr fs:[rbx]\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult metadata_segment_att = assembly_encode(
            arguments->arena, S8("movl %fs:(%rbx), %eax\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, metadata_segment_intel.diagnostic_count == 0 && metadata_segment_intel.bytes.length == sizeof(expected_segment) &&
                                   memcmp(metadata_segment_intel.bytes.pointer, expected_segment, sizeof(expected_segment)) == 0);
        BUSTER_TEST(arguments, metadata_segment_att.diagnostic_count == 0 && metadata_segment_att.bytes.length == sizeof(expected_segment) &&
                                   memcmp(metadata_segment_att.bytes.pointer, expected_segment, sizeof(expected_segment)) == 0);
        u8 expected_segment_displacement[] = {0x64, 0x8b, 0x43, 0x08};
        AssemblyEncodeResult metadata_segment_displacement_att = assembly_encode(
            arguments->arena, S8("movl %fs:8(%rbx), %eax\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, metadata_segment_displacement_att.diagnostic_count == 0 &&
                                   metadata_segment_displacement_att.bytes.length == sizeof(expected_segment_displacement) &&
                                   memcmp(metadata_segment_displacement_att.bytes.pointer, expected_segment_displacement,
                                          sizeof(expected_segment_displacement)) == 0);

        u8 expected_segment_register[] = {0x66, 0x8c, 0xe0};
        AssemblyEncodeResult metadata_segment_register_intel = assembly_encode(
            arguments->arena, S8("mov ax, fs\n"), (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult metadata_segment_register_att = assembly_encode(
            arguments->arena, S8("movw %fs, %ax\n"), (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, metadata_segment_register_intel.diagnostic_count == 0 &&
                                   metadata_segment_register_intel.bytes.length == sizeof(expected_segment_register) &&
                                   memcmp(metadata_segment_register_intel.bytes.pointer, expected_segment_register,
                                          sizeof(expected_segment_register)) == 0);
        BUSTER_TEST(arguments, metadata_segment_register_att.diagnostic_count == 0 &&
                                   metadata_segment_register_att.bytes.length == sizeof(expected_segment_register) &&
                                   memcmp(metadata_segment_register_att.bytes.pointer, expected_segment_register,
                                          sizeof(expected_segment_register)) == 0);

        u8 expected_segment_register_store[] = {0x66, 0x8e, 0xe0};
        AssemblyEncodeResult metadata_segment_register_store_intel = assembly_encode(
            arguments->arena, S8("mov fs, ax\n"), (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult metadata_segment_register_store_att = assembly_encode(
            arguments->arena, S8("movw %ax, %fs\n"), (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, metadata_segment_register_store_intel.diagnostic_count == 0 &&
                                   metadata_segment_register_store_intel.bytes.length == sizeof(expected_segment_register_store) &&
                                   memcmp(metadata_segment_register_store_intel.bytes.pointer, expected_segment_register_store,
                                          sizeof(expected_segment_register_store)) == 0);
        BUSTER_TEST(arguments, metadata_segment_register_store_att.diagnostic_count == 0 &&
                                   metadata_segment_register_store_att.bytes.length == sizeof(expected_segment_register_store) &&
                                   memcmp(metadata_segment_register_store_att.bytes.pointer, expected_segment_register_store,
                                          sizeof(expected_segment_register_store)) == 0);

        u8 expected_address32[] = {0x67, 0x8b, 0x03};
        AssemblyEncodeResult metadata_address32_intel = assembly_encode(
            arguments->arena, S8("mov eax, dword ptr [ebx]\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult metadata_address32_att = assembly_encode(
            arguments->arena, S8("movl (%ebx), %eax\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, metadata_address32_intel.diagnostic_count == 0 && metadata_address32_intel.bytes.length == sizeof(expected_address32) &&
                                   memcmp(metadata_address32_intel.bytes.pointer, expected_address32, sizeof(expected_address32)) == 0);
        BUSTER_TEST(arguments, metadata_address32_att.diagnostic_count == 0 && metadata_address32_att.bytes.length == sizeof(expected_address32) &&
                                   memcmp(metadata_address32_att.bytes.pointer, expected_address32, sizeof(expected_address32)) == 0);

        u8 expected_indirect[] = {0xff, 0xd0, 0xff, 0x50, 0x08, 0xff, 0xe0, 0xff, 0x60, 0x08};
        AssemblyEncodeResult metadata_indirect_att = assembly_encode(
            arguments->arena, S8("call *%rax\ncall *8(%rax)\njmp *%rax\njmp *8(%rax)\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, metadata_indirect_att.diagnostic_count == 0 && metadata_indirect_att.relocation_count == 0 &&
                                   metadata_indirect_att.bytes.length == sizeof(expected_indirect) &&
                                   memcmp(metadata_indirect_att.bytes.pointer, expected_indirect, sizeof(expected_indirect)) == 0);

        AssemblyEncodeResult metadata_bnd = assembly_encode(arguments->arena, S8("bndcl bnd0, rax\n"),
                                                             (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, metadata_bnd.diagnostic_count == 1 && metadata_bnd.bytes.length == 0 &&
                                   metadata_bnd.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE &&
                                   string_equal(metadata_bnd.diagnostics[0].message, S8("instruction requires an enabled target feature")));
        AssemblyEncodeResult metadata_debug = assembly_encode(arguments->arena, S8("mov rax, dr0\n"),
                                                               (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        u8 expected_metadata_debug[] = {0x0f, 0x21, 0xc0};
        BUSTER_TEST(arguments, metadata_debug.diagnostic_count == 0 && metadata_debug.bytes.length == sizeof(expected_metadata_debug) &&
                                   memcmp(metadata_debug.bytes.pointer, expected_metadata_debug, sizeof(expected_metadata_debug)) == 0 &&
                                   metadata_debug.relocation_count == 0 && metadata_debug.symbol_count == 0);

        Target public_invlpgb_target = x86_target;
        public_invlpgb_target.cpu_model = CPU_MODEL_BASELINE;
        public_invlpgb_target.cpu_features_explicit = true;
        public_invlpgb_target.cpu_features = target_cpu_features_from_array((TargetCpuFeature const[]){
            TARGET_CPU_FEATURE_X86_SSE2, TARGET_CPU_FEATURE_X86_INVLPGB}, 2);
        String8 public_invlpgb_intel_source = S8("invlpgb\naddr32 invlpgb\n");
        AssemblyEncodeResult public_invlpgb_intel = assembly_encode(
            arguments->arena, public_invlpgb_intel_source,
            (AssemblyEncodeOptions){.target = public_invlpgb_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult public_invlpgb_att = assembly_encode(
            arguments->arena, public_invlpgb_intel_source,
            (AssemblyEncodeOptions){.target = public_invlpgb_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        u8 expected_public_invlpgb[] = {0x0f, 0x01, 0xfe, 0x67, 0x0f, 0x01, 0xfe};
        BUSTER_TEST(arguments, public_invlpgb_intel.diagnostic_count == 0 &&
                                   public_invlpgb_intel.bytes.length == sizeof(expected_public_invlpgb) &&
                                   memcmp(public_invlpgb_intel.bytes.pointer, expected_public_invlpgb,
                                          sizeof(expected_public_invlpgb)) == 0 && public_invlpgb_intel.relocation_count == 0 &&
                                   public_invlpgb_intel.symbol_count == 0);
        BUSTER_TEST(arguments, public_invlpgb_att.diagnostic_count == 0 &&
                                   public_invlpgb_att.bytes.length == sizeof(expected_public_invlpgb) &&
                                   memcmp(public_invlpgb_att.bytes.pointer, expected_public_invlpgb,
                                          sizeof(expected_public_invlpgb)) == 0 && public_invlpgb_att.relocation_count == 0 &&
                                   public_invlpgb_att.symbol_count == 0);
        Target missing_public_invlpgb = public_invlpgb_target;
        missing_public_invlpgb.cpu_features = target_cpu_features_remove(missing_public_invlpgb.cpu_features,
                                                                          TARGET_CPU_FEATURE_X86_INVLPGB);
        AssemblyEncodeResult missing_public_invlpgb_result = assembly_encode(
            arguments->arena, S8("invlpgb\n"),
            (AssemblyEncodeOptions){.target = missing_public_invlpgb, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, missing_public_invlpgb_result.diagnostic_count == 1);
        BUSTER_TEST(arguments, missing_public_invlpgb_result.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
        BUSTER_TEST(arguments, missing_public_invlpgb_result.bytes.length == 0 && missing_public_invlpgb_result.relocation_count == 0 &&
                                   missing_public_invlpgb_result.symbol_count == 0);
        AssemblyEncodeResult duplicate_public_address_prefix = assembly_encode(
            arguments->arena, S8("addr32 addr32 invlpgb\n"),
            (AssemblyEncodeOptions){.target = public_invlpgb_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, duplicate_public_address_prefix.diagnostic_count == 1 &&
                                   duplicate_public_address_prefix.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS &&
                                   duplicate_public_address_prefix.bytes.length == 0 && duplicate_public_address_prefix.relocation_count == 0 &&
                                   duplicate_public_address_prefix.symbol_count == 0);

        AssemblyEncodeResult public_addr32_wbinvd = assembly_encode(
            arguments->arena, S8("addr32 wbinvd\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        u8 expected_public_addr32_wbinvd[] = {0x67, 0x0f, 0x09};
        BUSTER_TEST(arguments, public_addr32_wbinvd.diagnostic_count == 0 &&
                                   public_addr32_wbinvd.bytes.length == sizeof(expected_public_addr32_wbinvd) &&
                                   memcmp(public_addr32_wbinvd.bytes.pointer, expected_public_addr32_wbinvd,
                                          sizeof(expected_public_addr32_wbinvd)) == 0 &&
                                   public_addr32_wbinvd.relocation_count == 0 && public_addr32_wbinvd.symbol_count == 0);
        AssemblyEncodeResult public_addr64_wbinvd = assembly_encode(
            arguments->arena, S8("addr64 wbinvd\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, public_addr64_wbinvd.diagnostic_count == 1 &&
                                   public_addr64_wbinvd.bytes.length == 0 && public_addr64_wbinvd.relocation_count == 0 &&
                                   public_addr64_wbinvd.symbol_count == 0);
        AssemblyEncodeResult duplicate_public_wbinvd_address_prefix = assembly_encode(
            arguments->arena, S8("addr32 addr32 wbinvd\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, duplicate_public_wbinvd_address_prefix.diagnostic_count == 1 &&
                                   duplicate_public_wbinvd_address_prefix.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS &&
                                   duplicate_public_wbinvd_address_prefix.bytes.length == 0 &&
                                   duplicate_public_wbinvd_address_prefix.relocation_count == 0 &&
                                   duplicate_public_wbinvd_address_prefix.symbol_count == 0);

        Target public_monitor_target = x86_target;
        public_monitor_target.cpu_model = CPU_MODEL_BASELINE;
        public_monitor_target.cpu_features_explicit = true;
        public_monitor_target.cpu_features = target_cpu_features_from_array((TargetCpuFeature const[]){
            TARGET_CPU_FEATURE_X86_SSE2, TARGET_CPU_FEATURE_X86_MONITOR}, 2);
        AssemblyEncodeResult public_monitor_intel = assembly_encode(
            arguments->arena, S8("monitor\naddr32 monitor\n"),
            (AssemblyEncodeOptions){.target = public_monitor_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult public_monitor_att = assembly_encode(
            arguments->arena, S8("monitor\naddr32 monitor\n"),
            (AssemblyEncodeOptions){.target = public_monitor_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        u8 expected_public_monitor[] = {0x0f, 0x01, 0xc8, 0x67, 0x0f, 0x01, 0xc8};
        BUSTER_TEST(arguments, public_monitor_intel.diagnostic_count == 0 && public_monitor_intel.bytes.length == sizeof(expected_public_monitor) &&
                                   memcmp(public_monitor_intel.bytes.pointer, expected_public_monitor, sizeof(expected_public_monitor)) == 0);
        BUSTER_TEST(arguments, public_monitor_att.diagnostic_count == 0 && public_monitor_att.bytes.length == sizeof(expected_public_monitor) &&
                                   memcmp(public_monitor_att.bytes.pointer, expected_public_monitor, sizeof(expected_public_monitor)) == 0);
        Target missing_public_monitor = public_monitor_target;
        missing_public_monitor.cpu_features = target_cpu_features_remove(missing_public_monitor.cpu_features,
                                                                         TARGET_CPU_FEATURE_X86_MONITOR);
        AssemblyEncodeResult missing_public_monitor_result = assembly_encode(
            arguments->arena, S8("monitor\n"),
            (AssemblyEncodeOptions){.target = missing_public_monitor, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, missing_public_monitor_result.diagnostic_count == 1 &&
                                   missing_public_monitor_result.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE &&
                                   missing_public_monitor_result.bytes.length == 0 && missing_public_monitor_result.relocation_count == 0 &&
                                   missing_public_monitor_result.symbol_count == 0);

        AssemblyEncodeResult public_wbinvd = assembly_encode(
            arguments->arena, S8("wbinvd\nrepne wbinvd\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult public_wbinvd_att = assembly_encode(
            arguments->arena, S8("wbinvd\nrepne wbinvd\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        u8 expected_public_wbinvd[] = {0x0f, 0x09, 0xf2, 0x0f, 0x09};
        BUSTER_TEST(arguments, public_wbinvd.diagnostic_count == 0 && public_wbinvd.bytes.length == sizeof(expected_public_wbinvd) &&
                                   memcmp(public_wbinvd.bytes.pointer, expected_public_wbinvd, sizeof(expected_public_wbinvd)) == 0);
        BUSTER_TEST(arguments, public_wbinvd_att.diagnostic_count == 0 && public_wbinvd_att.bytes.length == sizeof(expected_public_wbinvd) &&
                                   memcmp(public_wbinvd_att.bytes.pointer, expected_public_wbinvd, sizeof(expected_public_wbinvd)) == 0);
        AssemblyEncodeResult public_rep_wbinvd = assembly_encode(
            arguments->arena, S8("rep wbinvd\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, public_rep_wbinvd.diagnostic_count == 1 &&
                                   public_rep_wbinvd.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS &&
                                   public_rep_wbinvd.bytes.length == 0 && public_rep_wbinvd.relocation_count == 0 &&
                                   public_rep_wbinvd.symbol_count == 0);
        Target public_wbnoinvd_target = x86_target;
        public_wbnoinvd_target.cpu_model = CPU_MODEL_BASELINE;
        public_wbnoinvd_target.cpu_features_explicit = true;
        public_wbnoinvd_target.cpu_features = target_cpu_features_from_array((TargetCpuFeature const[]){
            TARGET_CPU_FEATURE_X86_SSE2, TARGET_CPU_FEATURE_X86_WBNOINVD}, 2);
        AssemblyEncodeResult public_wbnoinvd = assembly_encode(
            arguments->arena, S8("wbnoinvd\n"),
            (AssemblyEncodeOptions){.target = public_wbnoinvd_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        u8 expected_public_wbnoinvd[] = {0xf3, 0x0f, 0x09};
        BUSTER_TEST(arguments, public_wbnoinvd.diagnostic_count == 0 && public_wbnoinvd.bytes.length == sizeof(expected_public_wbnoinvd) &&
                                   memcmp(public_wbnoinvd.bytes.pointer, expected_public_wbnoinvd, sizeof(expected_public_wbnoinvd)) == 0);
        Target missing_public_wbnoinvd = public_wbnoinvd_target;
        missing_public_wbnoinvd.cpu_features = target_cpu_features_remove(missing_public_wbnoinvd.cpu_features,
                                                                            TARGET_CPU_FEATURE_X86_WBNOINVD);
        AssemblyEncodeResult missing_public_wbnoinvd_result = assembly_encode(
            arguments->arena, S8("wbnoinvd\n"),
            (AssemblyEncodeOptions){.target = missing_public_wbnoinvd, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, missing_public_wbnoinvd_result.diagnostic_count == 1 &&
                                   missing_public_wbnoinvd_result.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE &&
                                   missing_public_wbnoinvd_result.bytes.length == 0 &&
                                   missing_public_wbnoinvd_result.relocation_count == 0 &&
                                   missing_public_wbnoinvd_result.symbol_count == 0);

        u8 expected_public_control_debug[] = {
            0x44, 0x0f, 0x22, 0xf8,
            0x44, 0x0f, 0x20, 0xf8,
            0x44, 0x0f, 0x23, 0xf8,
            0x44, 0x0f, 0x21, 0xf8,
        };
        AssemblyEncodeResult public_control_debug_intel = assembly_encode(
            arguments->arena, S8("mov cr15, rax\nmov rax, cr15\nmov dr15, rax\nmov rax, dr15\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult public_control_debug_att = assembly_encode(
            arguments->arena, S8("movq %rax, %cr15\nmovq %cr15, %rax\nmovq %rax, %dr15\nmovq %dr15, %rax\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, public_control_debug_intel.diagnostic_count == 0 &&
                                   public_control_debug_intel.bytes.length == sizeof(expected_public_control_debug) &&
                                   memcmp(public_control_debug_intel.bytes.pointer, expected_public_control_debug,
                                          sizeof(expected_public_control_debug)) == 0);
        BUSTER_TEST(arguments, public_control_debug_att.diagnostic_count == 0 &&
                                   public_control_debug_att.bytes.length == sizeof(expected_public_control_debug) &&
                                   memcmp(public_control_debug_att.bytes.pointer, expected_public_control_debug,
                                          sizeof(expected_public_control_debug)) == 0);
        AssemblyEncodeResult invalid_public_control_debug = assembly_encode(
            arguments->arena, S8("mov cr16, rax\nmov rax, cr16\nmov dr16, rax\nmov rax, dr16\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, invalid_public_control_debug.diagnostic_count == 4 &&
                                   invalid_public_control_debug.bytes.length == 0 &&
                                   invalid_public_control_debug.relocation_count == 0 &&
                                   invalid_public_control_debug.symbol_count == 0);
        AssemblyEncodeResult invalid_public_control_debug_egpr = assembly_encode(
            arguments->arena, S8("mov cr15, r16\nmov r16, cr15\nmov dr15, r16\nmov r16, dr15\n"),
            (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, invalid_public_control_debug_egpr.diagnostic_count == 4);
        BUSTER_TEST(arguments, invalid_public_control_debug_egpr.bytes.length == 0);
        BUSTER_TEST(arguments, invalid_public_control_debug_egpr.relocation_count == 0);
        BUSTER_TEST(arguments, invalid_public_control_debug_egpr.symbol_count == 0);

        Target public_vmx_target = virtualization_target;
        public_vmx_target.cpu_features = target_cpu_features_remove(public_vmx_target.cpu_features, TARGET_CPU_FEATURE_X86_SVM);
        AssemblyEncodeResult public_vmcall = assembly_encode(
            arguments->arena, S8("vmcall\n"),
            (AssemblyEncodeOptions){.target = public_vmx_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        u8 expected_public_vmcall[] = {0x0f, 0x01, 0xc1};
        BUSTER_TEST(arguments, public_vmcall.diagnostic_count == 0 && public_vmcall.bytes.length == sizeof(expected_public_vmcall) &&
                                   memcmp(public_vmcall.bytes.pointer, expected_public_vmcall, sizeof(expected_public_vmcall)) == 0);
        AssemblyEncodeResult public_vmcall_att = assembly_encode(
            arguments->arena, S8("vmcall\n"),
            (AssemblyEncodeOptions){.target = public_vmx_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, public_vmcall_att.diagnostic_count == 0 && public_vmcall_att.bytes.length == sizeof(expected_public_vmcall) &&
                                   memcmp(public_vmcall_att.bytes.pointer, expected_public_vmcall, sizeof(expected_public_vmcall)) == 0);
        Target missing_public_vmx = public_vmx_target;
        missing_public_vmx.cpu_features = target_cpu_features_remove(missing_public_vmx.cpu_features, TARGET_CPU_FEATURE_X86_VMX);
        AssemblyEncodeResult missing_public_vmx_result = assembly_encode(
            arguments->arena, S8("vmcall\n"),
            (AssemblyEncodeOptions){.target = missing_public_vmx, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, missing_public_vmx_result.diagnostic_count == 1 &&
                                   missing_public_vmx_result.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE &&
                                   missing_public_vmx_result.bytes.length == 0 && missing_public_vmx_result.relocation_count == 0 &&
                                   missing_public_vmx_result.symbol_count == 0);

        Target public_svm_target = virtualization_target;
        public_svm_target.cpu_features = target_cpu_features_remove(public_svm_target.cpu_features, TARGET_CPU_FEATURE_X86_VMX);
        AssemblyEncodeResult public_vmmcall = assembly_encode(
            arguments->arena, S8("vmmcall\n"),
            (AssemblyEncodeOptions){.target = public_svm_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        u8 expected_public_vmmcall[] = {0x0f, 0x01, 0xd9};
        BUSTER_TEST(arguments, public_vmmcall.diagnostic_count == 0 && public_vmmcall.bytes.length == sizeof(expected_public_vmmcall) &&
                                   memcmp(public_vmmcall.bytes.pointer, expected_public_vmmcall, sizeof(expected_public_vmmcall)) == 0);
        Target missing_public_svm = public_svm_target;
        missing_public_svm.cpu_features = target_cpu_features_remove(missing_public_svm.cpu_features, TARGET_CPU_FEATURE_X86_SVM);
        AssemblyEncodeResult missing_public_svm_result = assembly_encode(
            arguments->arena, S8("vmmcall\n"),
            (AssemblyEncodeOptions){.target = missing_public_svm, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, missing_public_svm_result.diagnostic_count == 1 &&
                                   missing_public_svm_result.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE &&
                                   missing_public_svm_result.bytes.length == 0 && missing_public_svm_result.relocation_count == 0 &&
                                   missing_public_svm_result.symbol_count == 0);
        AssemblyEncodeResult public_invlpga = assembly_encode(
            arguments->arena, S8("invlpga\n"),
            (AssemblyEncodeOptions){.target = public_svm_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        u8 expected_public_invlpga[] = {0x0f, 0x01, 0xdf};
        BUSTER_TEST(arguments, public_invlpga.diagnostic_count == 0 && public_invlpga.bytes.length == sizeof(expected_public_invlpga) &&
                                   memcmp(public_invlpga.bytes.pointer, expected_public_invlpga, sizeof(expected_public_invlpga)) == 0);
        AssemblyEncodeResult public_svm_att = assembly_encode(
            arguments->arena, S8("vmmcall\ninvlpga\n"),
            (AssemblyEncodeOptions){.target = public_svm_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, public_svm_att.diagnostic_count == 0 &&
                                   public_svm_att.bytes.length == sizeof(expected_public_vmmcall) + sizeof(expected_public_invlpga) &&
                                   memcmp(public_svm_att.bytes.pointer, expected_public_vmmcall, sizeof(expected_public_vmmcall)) == 0 &&
                                   memcmp(public_svm_att.bytes.pointer + sizeof(expected_public_vmmcall), expected_public_invlpga,
                                          sizeof(expected_public_invlpga)) == 0);

        AssemblyEncodeResult public_vm_data_intel = assembly_encode(
            arguments->arena,
            S8("vmread qword ptr [rax], rcx\n"
               "vmread rdx, rcx\n"
               "vmwrite rcx, qword ptr [rax]\n"
               "vmwrite rcx, rdx\n"),
            (AssemblyEncodeOptions){.target = public_vmx_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        u8 expected_public_vm_data[] = {0x0f, 0x78, 0x08, 0x0f, 0x78, 0xca, 0x0f, 0x79, 0x08, 0x0f, 0x79, 0xca};
        BUSTER_TEST(arguments, public_vm_data_intel.diagnostic_count == 0 &&
                                   public_vm_data_intel.bytes.length == sizeof(expected_public_vm_data) &&
                                   memcmp(public_vm_data_intel.bytes.pointer, expected_public_vm_data,
                                          sizeof(expected_public_vm_data)) == 0);
        AssemblyEncodeResult public_vm_data_att = assembly_encode(
            arguments->arena,
            S8("vmreadq %rcx, (%rax)\n"
               "vmreadq %rcx, %rdx\n"
               "vmwriteq (%rax), %rcx\n"
               "vmwriteq %rdx, %rcx\n"),
            (AssemblyEncodeOptions){.target = public_vmx_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, public_vm_data_att.diagnostic_count == 0 &&
                                   public_vm_data_att.bytes.length == sizeof(expected_public_vm_data) &&
                                   memcmp(public_vm_data_att.bytes.pointer, expected_public_vm_data,
                                          sizeof(expected_public_vm_data)) == 0);
        AssemblyEncodeResult missing_public_vm_data = assembly_encode(
            arguments->arena, S8("vmread qword ptr [rax], rax\nvmwrite rax, qword ptr [rax]\n"),
            (AssemblyEncodeOptions){.target = missing_public_vmx, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, missing_public_vm_data.diagnostic_count == 2 &&
                                   missing_public_vm_data.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE &&
                                   missing_public_vm_data.diagnostics[1].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE &&
                                   missing_public_vm_data.bytes.length == 0 && missing_public_vm_data.relocation_count == 0 &&
                                   missing_public_vm_data.symbol_count == 0);

        Target public_vmx_advanced_target = advanced_target;
        public_vmx_advanced_target.cpu_features = target_cpu_features_union(
            public_vmx_advanced_target.cpu_features,
            target_cpu_features_from_array((TargetCpuFeature const[]){
                TARGET_CPU_FEATURE_X86_APX_NCI_NDD_NF, TARGET_CPU_FEATURE_X86_VMX}, 2));
        Target public_vmx_invpcid_target = public_vmx_target;
        public_vmx_invpcid_target.cpu_features = target_cpu_features_union(
            public_vmx_invpcid_target.cpu_features,
            target_cpu_features_singleton(TARGET_CPU_FEATURE_X86_INVPCID));
        AssemblyEncodeResult public_vmx_memory_intel = assembly_encode(
            arguments->arena,
            S8("invpcid rcx, xmmword ptr [rax]\n"
               "invept rcx, xmmword ptr [rax]\n"
               "invvpid rcx, xmmword ptr [rax]\n"),
            (AssemblyEncodeOptions){.target = public_vmx_invpcid_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        u8 expected_public_vmx_memory[] = {
            0x66, 0x0f, 0x38, 0x82, 0x08,
            0x66, 0x0f, 0x38, 0x80, 0x08,
            0x66, 0x0f, 0x38, 0x81, 0x08,
        };
        BUSTER_TEST(arguments, public_vmx_memory_intel.diagnostic_count == 0 &&
                                   public_vmx_memory_intel.bytes.length == sizeof(expected_public_vmx_memory) &&
                                   memcmp(public_vmx_memory_intel.bytes.pointer, expected_public_vmx_memory,
                                          sizeof(expected_public_vmx_memory)) == 0);
        AssemblyEncodeResult public_vmx_memory_att = assembly_encode(
            arguments->arena,
            S8("invpcid (%rax), %rcx\n"
               "invept (%rax), %rcx\n"
               "invvpid (%rax), %rcx\n"),
            (AssemblyEncodeOptions){.target = public_vmx_invpcid_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, public_vmx_memory_att.diagnostic_count == 0 &&
                                   public_vmx_memory_att.bytes.length == sizeof(expected_public_vmx_memory) &&
                                   memcmp(public_vmx_memory_att.bytes.pointer, expected_public_vmx_memory,
                                          sizeof(expected_public_vmx_memory)) == 0);
        AssemblyEncodeResult missing_public_invpcid = assembly_encode(
            arguments->arena, S8("invpcid rax, xmmword ptr [rax]\n"),
            (AssemblyEncodeOptions){.target = public_vmx_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, missing_public_invpcid.diagnostic_count == 1 &&
                                   missing_public_invpcid.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE &&
                                   missing_public_invpcid.bytes.length == 0 && missing_public_invpcid.relocation_count == 0 &&
                                   missing_public_invpcid.symbol_count == 0);
        Target missing_public_vmx_memory_target = public_vmx_invpcid_target;
        missing_public_vmx_memory_target.cpu_features = target_cpu_features_remove(missing_public_vmx_memory_target.cpu_features,
                                                                                     TARGET_CPU_FEATURE_X86_VMX);
        AssemblyEncodeResult missing_public_vmx_memory = assembly_encode(
            arguments->arena, S8("invept rax, xmmword ptr [rax]\ninvvpid rax, xmmword ptr [rax]\n"),
            (AssemblyEncodeOptions){.target = missing_public_vmx_memory_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, missing_public_vmx_memory.diagnostic_count == 2 &&
                                   missing_public_vmx_memory.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE &&
                                   missing_public_vmx_memory.diagnostics[1].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE &&
                                   missing_public_vmx_memory.bytes.length == 0 && missing_public_vmx_memory.relocation_count == 0 &&
                                   missing_public_vmx_memory.symbol_count == 0);

        AssemblyEncodeResult public_apx_vmx = assembly_encode(
            arguments->arena,
            S8("invept r16, xmmword ptr [rax]\ninvvpid r16, xmmword ptr [rax]\n"),
            (AssemblyEncodeOptions){.target = public_vmx_advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        u8 expected_public_apx_vmx[] = {0x62, 0xe4, 0x7e, 0x08, 0xf0, 0x00, 0x62, 0xe4, 0x7e, 0x08, 0xf1, 0x00};
        BUSTER_TEST(arguments, public_apx_vmx.diagnostic_count == 0 && public_apx_vmx.bytes.length == sizeof(expected_public_apx_vmx) &&
                                   memcmp(public_apx_vmx.bytes.pointer, expected_public_apx_vmx,
                                          sizeof(expected_public_apx_vmx)) == 0);
        Target public_apx_system_target = public_vmx_advanced_target;
        public_apx_system_target.cpu_features = target_cpu_features_union(
            public_apx_system_target.cpu_features,
            target_cpu_features_from_array((TargetCpuFeature const[]){
                TARGET_CPU_FEATURE_X86_ENQCMD, TARGET_CPU_FEATURE_X86_INVPCID, TARGET_CPU_FEATURE_X86_MOVDIR64B,
                TARGET_CPU_FEATURE_X86_MSR_IMM}, 4));
        AssemblyEncodeResult public_apx_system = assembly_encode(
            arguments->arena,
            S8("enqcmds r16, zmmword ptr [rax]\n"
               "movdir64b r16, zmmword ptr [rax]\n"
               "invpcid r16, xmmword ptr [rax]\n"
               "rdmsr r16, 0x1234\n"
               "wrmsrns 0x1234, r16\n"),
            (AssemblyEncodeOptions){.target = public_apx_system_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        u8 expected_public_apx_system[] = {
            0x62, 0xe4, 0x7e, 0x08, 0xf8, 0x00,
            0x62, 0xe4, 0x7d, 0x08, 0xf8, 0x00,
            0x62, 0xe4, 0x7e, 0x08, 0xf2, 0x00,
            0x62, 0xff, 0x7f, 0x08, 0xf6, 0xc0, 0x34, 0x12, 0x00, 0x00,
            0x62, 0xff, 0x7e, 0x08, 0xf6, 0xc0, 0x34, 0x12, 0x00, 0x00,
        };
        BUSTER_TEST(arguments, public_apx_system.diagnostic_count == 0 &&
                                   public_apx_system.bytes.length == sizeof(expected_public_apx_system) &&
                                   memcmp(public_apx_system.bytes.pointer, expected_public_apx_system,
                                          sizeof(expected_public_apx_system)) == 0);
        Target public_apx_only_target = public_vmx_advanced_target;
        public_apx_only_target.cpu_features = target_cpu_features_remove(public_apx_only_target.cpu_features,
                                                                           TARGET_CPU_FEATURE_X86_VMX);
        AssemblyEncodeResult missing_public_apx_vmx_apx_only = assembly_encode(
            arguments->arena, S8("invept r16, xmmword ptr [rax]\n"),
            (AssemblyEncodeOptions){.target = public_apx_only_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, missing_public_apx_vmx_apx_only.diagnostic_count == 1 &&
                                   missing_public_apx_vmx_apx_only.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE &&
                                   missing_public_apx_vmx_apx_only.bytes.length == 0 &&
                                   missing_public_apx_vmx_apx_only.relocation_count == 0 &&
                                   missing_public_apx_vmx_apx_only.symbol_count == 0);
        Target public_vmx_only_target = public_vmx_target;
        AssemblyEncodeResult missing_public_apx_vmx_vmx_only = assembly_encode(
            arguments->arena, S8("invept r16, xmmword ptr [rax]\n"),
            (AssemblyEncodeOptions){.target = public_vmx_only_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, missing_public_apx_vmx_vmx_only.diagnostic_count == 1 &&
                                   missing_public_apx_vmx_vmx_only.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE &&
                                   missing_public_apx_vmx_vmx_only.bytes.length == 0 &&
                                   missing_public_apx_vmx_vmx_only.relocation_count == 0 &&
                                   missing_public_apx_vmx_vmx_only.symbol_count == 0);

        AssemblyEncodeResult public_msr_intel = assembly_encode(
            arguments->arena, S8("wrmsr\nrdmsr\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult public_msr_att = assembly_encode(
            arguments->arena, S8("wrmsr\nrdmsr\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        u8 expected_public_msr[] = {0x0f, 0x30, 0x0f, 0x32};
        BUSTER_TEST(arguments, public_msr_intel.diagnostic_count == 0 && public_msr_intel.bytes.length == sizeof(expected_public_msr) &&
                                   memcmp(public_msr_intel.bytes.pointer, expected_public_msr, sizeof(expected_public_msr)) == 0);
        BUSTER_TEST(arguments, public_msr_att.diagnostic_count == 0 && public_msr_att.bytes.length == sizeof(expected_public_msr) &&
                                   memcmp(public_msr_att.bytes.pointer, expected_public_msr, sizeof(expected_public_msr)) == 0);

        AssemblyEncodeResult metadata_rollback = assembly_encode(
            arguments->arena,
            S8("vpperm xmm0, xmm1, xmmword ptr [rip + leaked], xmm2, 7\n"
               "nop\n"),
            (AssemblyEncodeOptions){.target = amd_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, metadata_rollback.diagnostic_count == 1 &&
                                   metadata_rollback.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS &&
                                   metadata_rollback.bytes.length == 1 && metadata_rollback.bytes.pointer[0] == 0x90 &&
                                   metadata_rollback.symbol_count == 0 && metadata_rollback.relocation_count == 0);

        // Public APX ONE syntax omits the source count.  Keep it beside the
        // explicit-immediate spelling so the metadata fallback cannot confuse
        // an implicit ONE() operand with an ordinary immediate.
        u8 expected_apx_rol_one[] = {0xd5, 0x10, 0xd0, 0xc0};
        u8 expected_apx_rol_immediate[] = {0xd5, 0x10, 0xc0, 0xc0, 0x01};
        AssemblyEncodeResult apx_rol_one_intel = assembly_encode(
            arguments->arena, S8("rol r16b\n"),
            (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult apx_rol_one_att = assembly_encode(
            arguments->arena, S8("rolb %r16b\n"),
            (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        AssemblyEncodeResult apx_rol_immediate_intel = assembly_encode(
            arguments->arena, S8("rol r16b, 1\n"),
            (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult apx_rol_immediate_att = assembly_encode(
            arguments->arena, S8("rolb $1, %r16b\n"),
            (AssemblyEncodeOptions){.target = advanced_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, apx_rol_one_intel.diagnostic_count == 0 &&
                                   apx_rol_one_intel.bytes.length == sizeof(expected_apx_rol_one) &&
                                   memcmp(apx_rol_one_intel.bytes.pointer, expected_apx_rol_one, sizeof(expected_apx_rol_one)) == 0);
        BUSTER_TEST(arguments, apx_rol_one_att.diagnostic_count == 0 &&
                                   apx_rol_one_att.bytes.length == sizeof(expected_apx_rol_one) &&
                                   memcmp(apx_rol_one_att.bytes.pointer, expected_apx_rol_one, sizeof(expected_apx_rol_one)) == 0);
        BUSTER_TEST(arguments, apx_rol_immediate_intel.diagnostic_count == 0 &&
                                   apx_rol_immediate_intel.bytes.length == sizeof(expected_apx_rol_immediate) &&
                                   memcmp(apx_rol_immediate_intel.bytes.pointer, expected_apx_rol_immediate,
                                          sizeof(expected_apx_rol_immediate)) == 0);
        BUSTER_TEST(arguments, apx_rol_immediate_att.diagnostic_count == 0 &&
                                   apx_rol_immediate_att.bytes.length == sizeof(expected_apx_rol_immediate) &&
                                   memcmp(apx_rol_immediate_att.bytes.pointer, expected_apx_rol_immediate,
                                          sizeof(expected_apx_rol_immediate)) == 0);

        Target selector_target = advanced_target;
        selector_target.cpu_features = target_cpu_features_union(selector_target.cpu_features,
                                                                  target_cpu_features_from_array((TargetCpuFeature const[]){
                                                                      TARGET_CPU_FEATURE_X86_IBT, TARGET_CPU_FEATURE_X86_CLDEMOTE,
                                                                      TARGET_CPU_FEATURE_X86_PREFETCHI, TARGET_CPU_FEATURE_X86_MOVRS,
                                                                      TARGET_CPU_FEATURE_X86_SHSTK}, 5));
        u8 expected_selector[] = {
            0xf3, 0x0f, 0x1e, 0xfb,
            0xf3, 0x0f, 0x1e, 0xfa,
            0x0f, 0x1c, 0x00,
            0x0f, 0x18, 0x20,
        };
        AssemblyEncodeResult selector_intel = assembly_encode(
            arguments->arena,
            S8("endbr32\nendbr64\ncldemote byte ptr [rax]\nprefetchrst2 byte ptr [rax]\n"),
            (AssemblyEncodeOptions){.target = selector_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult selector_att = assembly_encode(
            arguments->arena,
            S8("endbr32\nendbr64\ncldemote (%rax)\nprefetchrst2 (%rax)\n"),
            (AssemblyEncodeOptions){.target = selector_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, selector_intel.diagnostic_count == 0 && selector_intel.bytes.length == sizeof(expected_selector) &&
                                   memcmp(selector_intel.bytes.pointer, expected_selector, sizeof(expected_selector)) == 0 &&
                                   selector_intel.relocation_count == 0 && selector_intel.symbol_count == 0);
        BUSTER_TEST(arguments, selector_att.diagnostic_count == 0 && selector_att.bytes.length == sizeof(expected_selector) &&
                                   memcmp(selector_att.bytes.pointer, expected_selector, sizeof(expected_selector)) == 0 &&
                                   selector_att.relocation_count == 0 && selector_att.symbol_count == 0);
        u8 expected_prefetchit[] = {
            0x0f, 0x18, 0x3d, 0x00, 0x00, 0x00, 0x00,
            0x0f, 0x18, 0x35, 0x00, 0x00, 0x00, 0x00,
        };
        AssemblyEncodeResult prefetchit_intel = assembly_encode(
            arguments->arena,
            S8("prefetchit0 byte ptr [rip + prefetchit0_external]\nprefetchit1 byte ptr [rip + prefetchit1_external]\n"),
            (AssemblyEncodeOptions){.target = selector_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult prefetchit_att = assembly_encode(
            arguments->arena,
            S8("prefetchit0 prefetchit0_external(%rip)\nprefetchit1 prefetchit1_external(%rip)\n"),
            (AssemblyEncodeOptions){.target = selector_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, prefetchit_intel.diagnostic_count == 0 && prefetchit_intel.bytes.length == sizeof(expected_prefetchit) &&
                                   memcmp(prefetchit_intel.bytes.pointer, expected_prefetchit, sizeof(expected_prefetchit)) == 0 &&
                                   prefetchit_intel.relocation_count == 2 && prefetchit_intel.symbol_count == 2 &&
                                   prefetchit_intel.relocations[0].offset == 3 && prefetchit_intel.relocations[0].addend == -4 &&
                                   prefetchit_intel.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC32 &&
                                   string_equal(prefetchit_intel.symbols[prefetchit_intel.relocations[0].symbol].name, S8("prefetchit0_external")) &&
                                   prefetchit_intel.relocations[1].offset == 10 && prefetchit_intel.relocations[1].addend == -4 &&
                                   prefetchit_intel.relocations[1].kind == ASSEMBLY_RELOCATION_X86_PC32 &&
                                   string_equal(prefetchit_intel.symbols[prefetchit_intel.relocations[1].symbol].name, S8("prefetchit1_external")));
        BUSTER_TEST(arguments, prefetchit_att.diagnostic_count == 0 && prefetchit_att.bytes.length == sizeof(expected_prefetchit) &&
                                   memcmp(prefetchit_att.bytes.pointer, expected_prefetchit, sizeof(expected_prefetchit)) == 0 &&
                                   prefetchit_att.relocation_count == 2 && prefetchit_att.symbol_count == 2 &&
                                   prefetchit_att.relocations[0].offset == 3 && prefetchit_att.relocations[0].addend == -4 &&
                                   prefetchit_att.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC32 &&
                                   string_equal(prefetchit_att.symbols[prefetchit_att.relocations[0].symbol].name, S8("prefetchit0_external")) &&
                                   prefetchit_att.relocations[1].offset == 10 && prefetchit_att.relocations[1].addend == -4 &&
                                   prefetchit_att.relocations[1].kind == ASSEMBLY_RELOCATION_X86_PC32 &&
                                   string_equal(prefetchit_att.symbols[prefetchit_att.relocations[1].symbol].name, S8("prefetchit1_external")));
        AssemblyEncodeResult invalid_prefetchit_intel = assembly_encode(
            arguments->arena, S8("prefetchit0 byte ptr [rax]\nprefetchit1 byte ptr [rax]\n"),
            (AssemblyEncodeOptions){.target = selector_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult invalid_prefetchit_att = assembly_encode(
            arguments->arena, S8("prefetchit0 (%rax)\nprefetchit1 (%rax)\n"),
            (AssemblyEncodeOptions){.target = selector_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, invalid_prefetchit_intel.diagnostic_count == 2 &&
                                   invalid_prefetchit_intel.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS &&
                                   invalid_prefetchit_intel.diagnostics[1].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS &&
                                   invalid_prefetchit_intel.bytes.length == 0 && invalid_prefetchit_intel.relocation_count == 0 &&
                                   invalid_prefetchit_intel.symbol_count == 0);
        BUSTER_TEST(arguments, invalid_prefetchit_att.diagnostic_count == 2 &&
                                   invalid_prefetchit_att.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS &&
                                   invalid_prefetchit_att.diagnostics[1].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS &&
                                   invalid_prefetchit_att.bytes.length == 0 && invalid_prefetchit_att.relocation_count == 0 &&
                                   invalid_prefetchit_att.symbol_count == 0);
        String8 cet_ordinary_intel_source = S8(
            "clrssbsy [rax]\n"
            "endbr32\n"
            "endbr64\n"
            "incsspd eax\n"
            "incsspq rax\n"
            "rdsspd eax\n"
            "rdsspq rax\n"
            "rstorssp [rax]\n"
            "saveprevssp\n"
            "setssbsy\n"
            "wrssd dword ptr [rax], ebx\n"
            "wrssq qword ptr [rax], rbx\n"
            "wrussd dword ptr [rax], ebx\n"
            "wrussq qword ptr [rax], rbx\n");
        String8 cet_ordinary_att_source = S8(
            "clrssbsy (%rax)\n"
            "endbr32\n"
            "endbr64\n"
            "incsspd %eax\n"
            "incsspq %rax\n"
            "rdsspd %eax\n"
            "rdsspq %rax\n"
            "rstorssp (%rax)\n"
            "saveprevssp\n"
            "setssbsy\n"
            "wrssd %ebx, (%rax)\n"
            "wrssq %rbx, (%rax)\n"
            "wrussd %ebx, (%rax)\n"
            "wrussq %rbx, (%rax)\n");
        u8 expected_cet_ordinary[] = {
            0xf3, 0x0f, 0xae, 0x30,
            0xf3, 0x0f, 0x1e, 0xfb,
            0xf3, 0x0f, 0x1e, 0xfa,
            0xf3, 0x0f, 0xae, 0xe8,
            0xf3, 0x48, 0x0f, 0xae, 0xe8,
            0xf3, 0x0f, 0x1e, 0xc8,
            0xf3, 0x48, 0x0f, 0x1e, 0xc8,
            0xf3, 0x0f, 0x01, 0x28,
            0xf3, 0x0f, 0x01, 0xea,
            0xf3, 0x0f, 0x01, 0xe8,
            0x0f, 0x38, 0xf6, 0x18,
            0x48, 0x0f, 0x38, 0xf6, 0x18,
            0x66, 0x0f, 0x38, 0xf5, 0x18,
            0x66, 0x48, 0x0f, 0x38, 0xf5, 0x18,
        };
        Target ordinary_cet_target = selector_target;
        ordinary_cet_target.cpu_features = target_cpu_features_remove(ordinary_cet_target.cpu_features, TARGET_CPU_FEATURE_X86_APX);
        ordinary_cet_target.cpu_features = target_cpu_features_remove(ordinary_cet_target.cpu_features, TARGET_CPU_FEATURE_X86_APX_NCI_NDD_NF);
        AssemblyEncodeResult cet_ordinary_intel = assembly_encode(
            arguments->arena, cet_ordinary_intel_source,
            (AssemblyEncodeOptions){.target = ordinary_cet_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult cet_ordinary_att = assembly_encode(
            arguments->arena, cet_ordinary_att_source,
            (AssemblyEncodeOptions){.target = ordinary_cet_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, cet_ordinary_intel.diagnostic_count == 0 && cet_ordinary_intel.bytes.length == sizeof(expected_cet_ordinary) &&
                                   memcmp(cet_ordinary_intel.bytes.pointer, expected_cet_ordinary, sizeof(expected_cet_ordinary)) == 0 &&
                                   cet_ordinary_intel.relocation_count == 0 && cet_ordinary_intel.symbol_count == 0);
        BUSTER_TEST(arguments, cet_ordinary_att.diagnostic_count == 0 && cet_ordinary_att.bytes.length == sizeof(expected_cet_ordinary) &&
                                   memcmp(cet_ordinary_att.bytes.pointer, expected_cet_ordinary, sizeof(expected_cet_ordinary)) == 0 &&
                                   cet_ordinary_att.relocation_count == 0 && cet_ordinary_att.symbol_count == 0);
        u8 expected_shadow_stack[] = {
            0x48, 0x0f, 0x38, 0xf6, 0x18,
            0xf3, 0x48, 0x0f, 0x1e, 0xc9,
        };
        AssemblyEncodeResult shadow_stack_intel = assembly_encode(
            arguments->arena, S8("wrssq qword ptr [rax], rbx\nrdsspq rcx\n"),
            (AssemblyEncodeOptions){.target = selector_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult shadow_stack_att = assembly_encode(
            arguments->arena, S8("wrssq %rbx, (%rax)\nrdsspq %rcx\n"),
            (AssemblyEncodeOptions){.target = selector_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, shadow_stack_intel.diagnostic_count == 0 &&
                                   shadow_stack_intel.bytes.length == sizeof(expected_shadow_stack) &&
                                   memcmp(shadow_stack_intel.bytes.pointer, expected_shadow_stack, sizeof(expected_shadow_stack)) == 0 &&
                                   shadow_stack_intel.relocation_count == 0 && shadow_stack_intel.symbol_count == 0);
        BUSTER_TEST(arguments, shadow_stack_att.diagnostic_count == 0 && shadow_stack_att.bytes.length == sizeof(expected_shadow_stack) &&
                                   memcmp(shadow_stack_att.bytes.pointer, expected_shadow_stack, sizeof(expected_shadow_stack)) == 0 &&
                                   shadow_stack_att.relocation_count == 0 && shadow_stack_att.symbol_count == 0);

        Target apx_cet_target = selector_target;
        apx_cet_target.cpu_features = target_cpu_features_add(apx_cet_target.cpu_features, TARGET_CPU_FEATURE_X86_APX);
        u8 expected_apx_cet[] = {0x62, 0xec, 0xfc, 0x08, 0x66, 0x08};
        AssemblyEncodeResult apx_cet_intel = assembly_encode(
            arguments->arena, S8("wrssq qword ptr [r16], r17\n"),
            (AssemblyEncodeOptions){.target = apx_cet_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult apx_cet_att = assembly_encode(
            arguments->arena, S8("wrssq %r17, (%r16)\n"),
            (AssemblyEncodeOptions){.target = apx_cet_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, apx_cet_intel.diagnostic_count == 0 && apx_cet_intel.bytes.length == sizeof(expected_apx_cet) &&
                                   memcmp(apx_cet_intel.bytes.pointer, expected_apx_cet, sizeof(expected_apx_cet)) == 0 &&
                                   apx_cet_intel.relocation_count == 0 && apx_cet_intel.symbol_count == 0);
        BUSTER_TEST(arguments, apx_cet_att.diagnostic_count == 0 && apx_cet_att.bytes.length == sizeof(expected_apx_cet) &&
                                   memcmp(apx_cet_att.bytes.pointer, expected_apx_cet, sizeof(expected_apx_cet)) == 0 &&
                                   apx_cet_att.relocation_count == 0 && apx_cet_att.symbol_count == 0);

        Target missing_apx_cet_apx = apx_cet_target;
        missing_apx_cet_apx.cpu_features = target_cpu_features_remove(missing_apx_cet_apx.cpu_features, TARGET_CPU_FEATURE_X86_APX);
        Target missing_apx_cet_shstk = apx_cet_target;
        missing_apx_cet_shstk.cpu_features = target_cpu_features_remove(missing_apx_cet_shstk.cpu_features, TARGET_CPU_FEATURE_X86_SHSTK);
        AssemblyEncodeResult missing_apx_cet_apx_intel = assembly_encode(
            arguments->arena, S8("wrssq qword ptr [r16], r17\n"),
            (AssemblyEncodeOptions){.target = missing_apx_cet_apx, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult missing_apx_cet_shstk_intel = assembly_encode(
            arguments->arena, S8("wrssq qword ptr [r16], r17\n"),
            (AssemblyEncodeOptions){.target = missing_apx_cet_shstk, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult missing_apx_cet_apx_att = assembly_encode(
            arguments->arena, S8("wrssq %r17, (%r16)\n"),
            (AssemblyEncodeOptions){.target = missing_apx_cet_apx, .syntax = ASSEMBLY_SYNTAX_ATT});
        AssemblyEncodeResult missing_apx_cet_shstk_att = assembly_encode(
            arguments->arena, S8("wrssq %r17, (%r16)\n"),
            (AssemblyEncodeOptions){.target = missing_apx_cet_shstk, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, missing_apx_cet_apx_intel.diagnostic_count == 1 &&
                                   missing_apx_cet_apx_intel.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE &&
                                   missing_apx_cet_apx_intel.bytes.length == 0 && missing_apx_cet_apx_intel.relocation_count == 0 &&
                                   missing_apx_cet_apx_intel.symbol_count == 0);
        BUSTER_TEST(arguments, missing_apx_cet_shstk_intel.diagnostic_count == 1 &&
                                   missing_apx_cet_shstk_intel.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE &&
                                   missing_apx_cet_shstk_intel.bytes.length == 0 && missing_apx_cet_shstk_intel.relocation_count == 0 &&
                                   missing_apx_cet_shstk_intel.symbol_count == 0);
        BUSTER_TEST(arguments, missing_apx_cet_apx_att.diagnostic_count == 1 &&
                                   missing_apx_cet_apx_att.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE &&
                                   missing_apx_cet_apx_att.bytes.length == 0 && missing_apx_cet_apx_att.relocation_count == 0 &&
                                   missing_apx_cet_apx_att.symbol_count == 0);
        BUSTER_TEST(arguments, missing_apx_cet_shstk_att.diagnostic_count == 1 &&
                                   missing_apx_cet_shstk_att.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE &&
                                   missing_apx_cet_shstk_att.bytes.length == 0 && missing_apx_cet_shstk_att.relocation_count == 0 &&
                                   missing_apx_cet_shstk_att.symbol_count == 0);

        Target missing_shstk_target = selector_target;
        missing_shstk_target.cpu_features = target_cpu_features_remove(missing_shstk_target.cpu_features, TARGET_CPU_FEATURE_X86_SHSTK);
        AssemblyEncodeResult missing_shstk = assembly_encode(
            arguments->arena, S8("wrssq qword ptr [rax], rbx\nrdsspq rcx\n"),
            (AssemblyEncodeOptions){.target = missing_shstk_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult missing_shstk_att = assembly_encode(
            arguments->arena, S8("wrssq %rbx, (%rax)\nrdsspq %rcx\n"),
            (AssemblyEncodeOptions){.target = missing_shstk_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, missing_shstk.diagnostic_count == 2 && missing_shstk.bytes.length == 0 &&
                                   missing_shstk.relocation_count == 0 && missing_shstk.symbol_count == 0 &&
                                   missing_shstk.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE &&
                                   missing_shstk.diagnostics[1].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
        BUSTER_TEST(arguments, missing_shstk_att.diagnostic_count == 2 && missing_shstk_att.bytes.length == 0 &&
                                   missing_shstk_att.relocation_count == 0 && missing_shstk_att.symbol_count == 0 &&
                                   missing_shstk_att.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE &&
                                   missing_shstk_att.diagnostics[1].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);

        Target missing_ibt_target = selector_target;
        missing_ibt_target.cpu_features = target_cpu_features_remove(missing_ibt_target.cpu_features, TARGET_CPU_FEATURE_X86_IBT);
        AssemblyEncodeResult missing_ibt = assembly_encode(
            arguments->arena, S8("endbr32\nendbr64\n"),
            (AssemblyEncodeOptions){.target = missing_ibt_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult missing_ibt_att = assembly_encode(
            arguments->arena, S8("endbr32\nendbr64\n"),
            (AssemblyEncodeOptions){.target = missing_ibt_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, missing_ibt.diagnostic_count == 2 && missing_ibt.bytes.length == 0 &&
                                   missing_ibt.relocation_count == 0 && missing_ibt.symbol_count == 0 &&
                                   missing_ibt.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE &&
                                   missing_ibt.diagnostics[1].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
        BUSTER_TEST(arguments, missing_ibt_att.diagnostic_count == 2 && missing_ibt_att.bytes.length == 0 &&
                                   missing_ibt_att.relocation_count == 0 && missing_ibt_att.symbol_count == 0 &&
                                   missing_ibt_att.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE &&
                                   missing_ibt_att.diagnostics[1].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);

        String8 selector_intel_sources[] = {
            S8("endbr64\n"),
            S8("cldemote byte ptr [rax]\n"),
            S8("prefetchit0 byte ptr [rip + missing_prefetchit0]\nprefetchit1 byte ptr [rip + missing_prefetchit1]\n"),
            S8("prefetchrst2 byte ptr [rax]\n"),
        };
        String8 selector_att_sources[] = {
            S8("endbr64\n"),
            S8("cldemote (%rax)\n"),
            S8("prefetchit0 missing_prefetchit0(%rip)\nprefetchit1 missing_prefetchit1(%rip)\n"),
            S8("prefetchrst2 (%rax)\n"),
        };
        TargetCpuFeature selector_features[] = {
            TARGET_CPU_FEATURE_X86_IBT,
            TARGET_CPU_FEATURE_X86_CLDEMOTE,
            TARGET_CPU_FEATURE_X86_PREFETCHI,
            TARGET_CPU_FEATURE_X86_MOVRS,
        };
        for (u32 selector_index = 0; selector_index < BUSTER_ARRAY_LENGTH(selector_features); selector_index += 1)
        {
            Target missing_selector_target = selector_target;
            missing_selector_target.cpu_features = target_cpu_features_remove(missing_selector_target.cpu_features, selector_features[selector_index]);
            AssemblyEncodeResult missing_selector_intel = assembly_encode(
                arguments->arena, selector_intel_sources[selector_index],
                (AssemblyEncodeOptions){.target = missing_selector_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
            AssemblyEncodeResult missing_selector_att = assembly_encode(
                arguments->arena, selector_att_sources[selector_index],
                (AssemblyEncodeOptions){.target = missing_selector_target, .syntax = ASSEMBLY_SYNTAX_ATT});
            u32 expected_selector_diagnostics = selector_index == 2 ? 2 : 1;
            BUSTER_TEST(arguments, missing_selector_intel.diagnostic_count == expected_selector_diagnostics &&
                                       missing_selector_intel.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE &&
                                       (expected_selector_diagnostics == 1 ||
                                        missing_selector_intel.diagnostics[1].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE) &&
                                       missing_selector_intel.bytes.length == 0 && missing_selector_intel.relocation_count == 0 &&
                                       missing_selector_intel.symbol_count == 0);
            BUSTER_TEST(arguments, missing_selector_att.diagnostic_count == expected_selector_diagnostics &&
                                       missing_selector_att.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE &&
                                       (expected_selector_diagnostics == 1 ||
                                        missing_selector_att.diagnostics[1].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE) &&
                                       missing_selector_att.bytes.length == 0 && missing_selector_att.relocation_count == 0 &&
                                       missing_selector_att.symbol_count == 0);
        }
    }

    {
        u8 const expected_classic_intel[] = {
            0xa4,
            0x66, 0xa5,
            0xa5,
            0x48, 0xa5,
            0xa6,
            0x66, 0xa7,
            0xa7,
            0x48, 0xa7,
            0xaa,
            0x66, 0xab,
            0xab,
            0x48, 0xab,
            0xac,
            0x66, 0xad,
            0xad,
            0x48, 0xad,
            0xae,
            0x66, 0xaf,
            0xaf,
            0x48, 0xaf,
            0x6c,
            0x66, 0x6d,
            0x6d,
            0x6e,
            0x66, 0x6f,
            0x6f,
        };
        AssemblyEncodeResult classic_intel = assembly_encode(
            arguments->arena,
            S8("movsb\nmovsw\nmovsd\nmovsq\n"
               "cmpsb\ncmpsw\ncmpsd\ncmpsq\n"
               "stosb\nstosw\nstosd\nstosq\n"
               "lodsb\nlodsw\nlodsd\nlodsq\n"
               "scasb\nscasw\nscasd\nscasq\n"
               "insb\ninsw\ninsd\noutsb\noutsw\noutsd\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, classic_intel.diagnostic_count == 0 &&
                                   assembly_test_bytes_equal(classic_intel.bytes, expected_classic_intel,
                                                             BUSTER_ARRAY_LENGTH(expected_classic_intel)) &&
                                   classic_intel.relocation_count == 0 && classic_intel.symbol_count == 0);

        AssemblyEncodeResult classic_att = assembly_encode(
            arguments->arena,
            S8("movsb\nmovsw\nmovsl\nmovsq\n"
               "cmpsb\ncmpsw\ncmpsl\ncmpsq\n"
               "stosb\nstosw\nstosl\nstosq\n"
               "lodsb\nlodsw\nlodsl\nlodsq\n"
               "scasb\nscasw\nscasl\nscasq\n"
               "insb\ninsw\ninsl\noutsb\noutsw\noutsl\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, classic_att.diagnostic_count == 0 &&
                                   assembly_test_bytes_equal(classic_att.bytes, expected_classic_intel,
                                                             BUSTER_ARRAY_LENGTH(expected_classic_intel)) &&
                                   classic_att.relocation_count == 0 && classic_att.symbol_count == 0);

        typedef struct ClassicRepeatEncodingCase ClassicRepeatEncodingCase;
        struct ClassicRepeatEncodingCase
        {
            String8 intel;
            String8 att;
            u8 bytes[3];
            u8 byte_count;
        };
        static ClassicRepeatEncodingCase const classic_repeat_matrix[] = {
            {S8_INITIALIZER("rep movsb"), S8_INITIALIZER("rep movsb"), {0xf3, 0xa4}, 2},
            {S8_INITIALIZER("rep movsw"), S8_INITIALIZER("rep movsw"), {0xf3, 0x66, 0xa5}, 3},
            {S8_INITIALIZER("rep movsd"), S8_INITIALIZER("rep movsl"), {0xf3, 0xa5}, 2},
            {S8_INITIALIZER("rep movsq"), S8_INITIALIZER("rep movsq"), {0xf3, 0x48, 0xa5}, 3},
            {S8_INITIALIZER("rep cmpsb"), S8_INITIALIZER("rep cmpsb"), {0xf3, 0xa6}, 2},
            {S8_INITIALIZER("repz cmpsw"), S8_INITIALIZER("repz cmpsw"), {0xf3, 0x66, 0xa7}, 3},
            {S8_INITIALIZER("repe cmpsd"), S8_INITIALIZER("repe cmpsl"), {0xf3, 0xa7}, 2},
            {S8_INITIALIZER("repz cmpsq"), S8_INITIALIZER("repz cmpsq"), {0xf3, 0x48, 0xa7}, 3},
            {S8_INITIALIZER("repne cmpsb"), S8_INITIALIZER("repne cmpsb"), {0xf2, 0xa6}, 2},
            {S8_INITIALIZER("repnz cmpsw"), S8_INITIALIZER("repnz cmpsw"), {0xf2, 0x66, 0xa7}, 3},
            {S8_INITIALIZER("repne cmpsd"), S8_INITIALIZER("repne cmpsl"), {0xf2, 0xa7}, 2},
            {S8_INITIALIZER("repnz cmpsq"), S8_INITIALIZER("repnz cmpsq"), {0xf2, 0x48, 0xa7}, 3},
            {S8_INITIALIZER("rep stosb"), S8_INITIALIZER("rep stosb"), {0xf3, 0xaa}, 2},
            {S8_INITIALIZER("rep stosw"), S8_INITIALIZER("rep stosw"), {0xf3, 0x66, 0xab}, 3},
            {S8_INITIALIZER("rep stosd"), S8_INITIALIZER("rep stosl"), {0xf3, 0xab}, 2},
            {S8_INITIALIZER("rep stosq"), S8_INITIALIZER("rep stosq"), {0xf3, 0x48, 0xab}, 3},
            {S8_INITIALIZER("rep lodsb"), S8_INITIALIZER("rep lodsb"), {0xf3, 0xac}, 2},
            {S8_INITIALIZER("rep lodsw"), S8_INITIALIZER("rep lodsw"), {0xf3, 0x66, 0xad}, 3},
            {S8_INITIALIZER("rep lodsd"), S8_INITIALIZER("rep lodsl"), {0xf3, 0xad}, 2},
            {S8_INITIALIZER("rep lodsq"), S8_INITIALIZER("rep lodsq"), {0xf3, 0x48, 0xad}, 3},
            {S8_INITIALIZER("rep insb"), S8_INITIALIZER("rep insb"), {0xf3, 0x6c}, 2},
            {S8_INITIALIZER("rep insw"), S8_INITIALIZER("rep insw"), {0xf3, 0x66, 0x6d}, 3},
            {S8_INITIALIZER("rep insd"), S8_INITIALIZER("rep insl"), {0xf3, 0x6d}, 2},
            {S8_INITIALIZER("rep outsb"), S8_INITIALIZER("rep outsb"), {0xf3, 0x6e}, 2},
            {S8_INITIALIZER("rep outsw"), S8_INITIALIZER("rep outsw"), {0xf3, 0x66, 0x6f}, 3},
            {S8_INITIALIZER("rep outsd"), S8_INITIALIZER("rep outsl"), {0xf3, 0x6f}, 2},
            {S8_INITIALIZER("rep scasb"), S8_INITIALIZER("rep scasb"), {0xf3, 0xae}, 2},
            {S8_INITIALIZER("repz scasw"), S8_INITIALIZER("repz scasw"), {0xf3, 0x66, 0xaf}, 3},
            {S8_INITIALIZER("repe scasd"), S8_INITIALIZER("repe scasl"), {0xf3, 0xaf}, 2},
            {S8_INITIALIZER("repz scasq"), S8_INITIALIZER("repz scasq"), {0xf3, 0x48, 0xaf}, 3},
            {S8_INITIALIZER("repne scasb"), S8_INITIALIZER("repne scasb"), {0xf2, 0xae}, 2},
            {S8_INITIALIZER("repnz scasw"), S8_INITIALIZER("repnz scasw"), {0xf2, 0x66, 0xaf}, 3},
            {S8_INITIALIZER("repne scasd"), S8_INITIALIZER("repne scasl"), {0xf2, 0xaf}, 2},
            {S8_INITIALIZER("repnz scasq"), S8_INITIALIZER("repnz scasq"), {0xf2, 0x48, 0xaf}, 3},
            {S8_INITIALIZER("repne movsb"), S8_INITIALIZER("repne movsb"), {0xf2, 0xa4}, 2},
            {S8_INITIALIZER("repnz movsw"), S8_INITIALIZER("repnz movsw"), {0xf2, 0x66, 0xa5}, 3},
            {S8_INITIALIZER("repne movsd"), S8_INITIALIZER("repne movsl"), {0xf2, 0xa5}, 2},
            {S8_INITIALIZER("repnz movsq"), S8_INITIALIZER("repnz movsq"), {0xf2, 0x48, 0xa5}, 3},
            {S8_INITIALIZER("repnz stosb"), S8_INITIALIZER("repnz stosb"), {0xf2, 0xaa}, 2},
            {S8_INITIALIZER("repne stosw"), S8_INITIALIZER("repne stosw"), {0xf2, 0x66, 0xab}, 3},
            {S8_INITIALIZER("repnz stosd"), S8_INITIALIZER("repnz stosl"), {0xf2, 0xab}, 2},
            {S8_INITIALIZER("repne stosq"), S8_INITIALIZER("repne stosq"), {0xf2, 0x48, 0xab}, 3},
            {S8_INITIALIZER("repne lodsb"), S8_INITIALIZER("repne lodsb"), {0xf2, 0xac}, 2},
            {S8_INITIALIZER("repnz lodsw"), S8_INITIALIZER("repnz lodsw"), {0xf2, 0x66, 0xad}, 3},
            {S8_INITIALIZER("repne lodsd"), S8_INITIALIZER("repne lodsl"), {0xf2, 0xad}, 2},
            {S8_INITIALIZER("repnz lodsq"), S8_INITIALIZER("repnz lodsq"), {0xf2, 0x48, 0xad}, 3},
            {S8_INITIALIZER("repnz insb"), S8_INITIALIZER("repnz insb"), {0xf2, 0x6c}, 2},
            {S8_INITIALIZER("repne insw"), S8_INITIALIZER("repne insw"), {0xf2, 0x66, 0x6d}, 3},
            {S8_INITIALIZER("repnz insd"), S8_INITIALIZER("repnz insl"), {0xf2, 0x6d}, 2},
            {S8_INITIALIZER("repne outsb"), S8_INITIALIZER("repne outsb"), {0xf2, 0x6e}, 2},
            {S8_INITIALIZER("repnz outsw"), S8_INITIALIZER("repnz outsw"), {0xf2, 0x66, 0x6f}, 3},
            {S8_INITIALIZER("repne outsd"), S8_INITIALIZER("repne outsl"), {0xf2, 0x6f}, 2},
        };
        for (u32 repeat_index = 0; repeat_index < BUSTER_ARRAY_LENGTH(classic_repeat_matrix); repeat_index += 1)
        {
            ClassicRepeatEncodingCase repeat_case = classic_repeat_matrix[repeat_index];
            AssemblyEncodeResult repeat_intel = assembly_encode(
                arguments->arena, repeat_case.intel,
                (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
            AssemblyEncodeResult repeat_att = assembly_encode(
                arguments->arena, repeat_case.att,
                (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
            BUSTER_TEST(arguments, repeat_intel.diagnostic_count == 0 &&
                                       assembly_test_bytes_equal(repeat_intel.bytes, repeat_case.bytes, repeat_case.byte_count) &&
                                       repeat_intel.relocation_count == 0 && repeat_intel.symbol_count == 0 &&
                                       repeat_att.diagnostic_count == 0 &&
                                       assembly_test_bytes_equal(repeat_att.bytes, repeat_case.bytes, repeat_case.byte_count) &&
                                       repeat_att.relocation_count == 0 && repeat_att.symbol_count == 0);
        }

        typedef struct ClassicAddressSizeEncodingCase ClassicAddressSizeEncodingCase;
        struct ClassicAddressSizeEncodingCase
        {
            String8 intel;
            String8 att;
            u8 bytes[3];
            u8 byte_count;
        };
        static ClassicAddressSizeEncodingCase const classic_address_size_matrix[] = {
            {S8_INITIALIZER("addr32 movsb"), S8_INITIALIZER("addr32 movsb"), {0x67, 0xa4}, 2},
            {S8_INITIALIZER("addr32 cmpsb"), S8_INITIALIZER("addr32 cmpsb"), {0x67, 0xa6}, 2},
            {S8_INITIALIZER("addr32 stosb"), S8_INITIALIZER("addr32 stosb"), {0x67, 0xaa}, 2},
            {S8_INITIALIZER("addr32 lodsb"), S8_INITIALIZER("addr32 lodsb"), {0x67, 0xac}, 2},
            {S8_INITIALIZER("addr32 scasb"), S8_INITIALIZER("addr32 scasb"), {0x67, 0xae}, 2},
            {S8_INITIALIZER("addr32 insb"), S8_INITIALIZER("addr32 insb"), {0x67, 0x6c}, 2},
            {S8_INITIALIZER("addr32 outsb"), S8_INITIALIZER("addr32 outsb"), {0x67, 0x6e}, 2},
            {S8_INITIALIZER("addr32 rep movsb"), S8_INITIALIZER("addr32 rep movsb"), {0x67, 0xf3, 0xa4}, 3},
            {S8_INITIALIZER("addr32 repe cmpsb"), S8_INITIALIZER("addr32 repz cmpsb"), {0x67, 0xf3, 0xa6}, 3},
            {S8_INITIALIZER("addr32 rep stosb"), S8_INITIALIZER("addr32 rep stosb"), {0x67, 0xf3, 0xaa}, 3},
            {S8_INITIALIZER("addr32 rep lodsb"), S8_INITIALIZER("addr32 rep lodsb"), {0x67, 0xf3, 0xac}, 3},
            {S8_INITIALIZER("addr32 rep insb"), S8_INITIALIZER("addr32 rep insb"), {0x67, 0xf3, 0x6c}, 3},
            {S8_INITIALIZER("addr32 rep outsb"), S8_INITIALIZER("addr32 rep outsb"), {0x67, 0xf3, 0x6e}, 3},
            {S8_INITIALIZER("addr32 repne scasb"), S8_INITIALIZER("addr32 repnz scasb"), {0x67, 0xf2, 0xae}, 3},
        };
        for (u32 address_size_index = 0; address_size_index < BUSTER_ARRAY_LENGTH(classic_address_size_matrix);
             address_size_index += 1)
        {
            ClassicAddressSizeEncodingCase address_size_case = classic_address_size_matrix[address_size_index];
            AssemblyEncodeResult address_size_intel = assembly_encode(
                arguments->arena, address_size_case.intel,
                (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
            AssemblyEncodeResult address_size_att = assembly_encode(
                arguments->arena, address_size_case.att,
                (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
            BUSTER_TEST(arguments, address_size_intel.diagnostic_count == 0 &&
                                       assembly_test_bytes_equal(address_size_intel.bytes, address_size_case.bytes,
                                                                 address_size_case.byte_count) &&
                                       address_size_intel.relocation_count == 0 && address_size_intel.symbol_count == 0 &&
                                       address_size_att.diagnostic_count == 0 &&
                                       assembly_test_bytes_equal(address_size_att.bytes, address_size_case.bytes,
                                                                 address_size_case.byte_count) &&
                                       address_size_att.relocation_count == 0 && address_size_att.symbol_count == 0);
        }

        typedef struct ClassicSegmentPrefixEncodingCase ClassicSegmentPrefixEncodingCase;
        struct ClassicSegmentPrefixEncodingCase
        {
            String8 intel;
            String8 att;
            u8 bytes[4];
            u8 byte_count;
        };
        static ClassicSegmentPrefixEncodingCase const classic_segment_prefix_matrix[] = {
            {S8_INITIALIZER("es movsb"), S8_INITIALIZER("es movsb"), {0x26, 0xa4}, 2},
            {S8_INITIALIZER("cs movsb"), S8_INITIALIZER("cs movsb"), {0x2e, 0xa4}, 2},
            {S8_INITIALIZER("ss movsb"), S8_INITIALIZER("ss movsb"), {0x36, 0xa4}, 2},
            {S8_INITIALIZER("ds movsb"), S8_INITIALIZER("ds movsb"), {0x3e, 0xa4}, 2},
            {S8_INITIALIZER("fs movsb"), S8_INITIALIZER("fs movsb"), {0x64, 0xa4}, 2},
            {S8_INITIALIZER("gs movsb"), S8_INITIALIZER("gs movsb"), {0x65, 0xa4}, 2},
            {S8_INITIALIZER("fs rep movsw"), S8_INITIALIZER("fs rep movsw"), {0x64, 0xf3, 0x66, 0xa5}, 4},
            {S8_INITIALIZER("addr32 fs rep movsb"), S8_INITIALIZER("addr32 fs rep movsb"), {0x64, 0x67, 0xf3, 0xa4}, 4},
            {S8_INITIALIZER("fs addr32 rep movsb"), S8_INITIALIZER("fs addr32 rep movsb"), {0x64, 0x67, 0xf3, 0xa4}, 4},
            {S8_INITIALIZER("gs outsb"), S8_INITIALIZER("gs outsb"), {0x65, 0x6e}, 2},
            {S8_INITIALIZER("cs cmpsb"), S8_INITIALIZER("cs cmpsb"), {0x2e, 0xa6}, 2},
            {S8_INITIALIZER("ss lodsb"), S8_INITIALIZER("ss lodsb"), {0x36, 0xac}, 2},
            {S8_INITIALIZER("ds xlat"), S8_INITIALIZER("ds xlat"), {0x3e, 0xd7}, 2},
            {S8_INITIALIZER("fs xlatb"), S8_INITIALIZER("fs xlatb"), {0x64, 0xd7}, 2},
        };
        for (u32 segment_index = 0; segment_index < BUSTER_ARRAY_LENGTH(classic_segment_prefix_matrix); segment_index += 1)
        {
            ClassicSegmentPrefixEncodingCase segment_case = classic_segment_prefix_matrix[segment_index];
            AssemblyEncodeResult segment_intel = assembly_encode(
                arguments->arena, segment_case.intel,
                (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
            AssemblyEncodeResult segment_att = assembly_encode(
                arguments->arena, segment_case.att,
                (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
            BUSTER_TEST(arguments, segment_intel.diagnostic_count == 0 &&
                                       assembly_test_bytes_equal(segment_intel.bytes, segment_case.bytes, segment_case.byte_count) &&
                                       segment_intel.relocation_count == 0 && segment_intel.symbol_count == 0 &&
                                       segment_att.diagnostic_count == 0 &&
                                       assembly_test_bytes_equal(segment_att.bytes, segment_case.bytes, segment_case.byte_count) &&
                                       segment_att.relocation_count == 0 && segment_att.symbol_count == 0);
        }

        String8 const invalid_segment_prefixes[] = {
            S8("fs insb"),
            S8("fs stosb"),
            S8("fs scasb"),
            S8("%fs movsb"),
            S8("fs: movsb"),
            S8("fs fs movsb"),
            S8("fs gs movsb"),
            S8("fs lock movsb"),
            S8("fs jz 0"),
        };
        for (u32 invalid_index = 0; invalid_index < BUSTER_ARRAY_LENGTH(invalid_segment_prefixes); invalid_index += 1)
        {
            AssemblyEncodeResult invalid_intel = assembly_encode(
                arguments->arena, invalid_segment_prefixes[invalid_index],
                (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
            AssemblyEncodeResult invalid_att = assembly_encode(
                arguments->arena, invalid_segment_prefixes[invalid_index],
                (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
            BUSTER_TEST(arguments, invalid_intel.diagnostic_count > 0 && invalid_intel.bytes.length == 0 &&
                                       invalid_intel.relocation_count == 0 && invalid_intel.symbol_count == 0 &&
                                       invalid_att.diagnostic_count > 0 && invalid_att.bytes.length == 0 &&
                                       invalid_att.relocation_count == 0 && invalid_att.symbol_count == 0);
        }
        AssemblyEncodeResult invalid_visible_segment = assembly_encode(
            arguments->arena, S8("fs mov rax, fs:[rbx]\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, invalid_visible_segment.diagnostic_count > 0 && invalid_visible_segment.bytes.length == 0 &&
                                   invalid_visible_segment.relocation_count == 0 && invalid_visible_segment.symbol_count == 0);
        AssemblyEncodeResult invalid_moffs_segment = assembly_encode(
            arguments->arena, S8("fs mov al, byte ptr fs:[0x1234]\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, invalid_moffs_segment.diagnostic_count > 0 && invalid_moffs_segment.bytes.length == 0 &&
                                   invalid_moffs_segment.relocation_count == 0 && invalid_moffs_segment.symbol_count == 0);

        AssemblyEncodeResult segment_label_intel = assembly_encode(
            arguments->arena, S8("fs:\nmovsb\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult segment_label_att = assembly_encode(
            arguments->arena, S8("fs:\nmovsb\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, segment_label_intel.diagnostic_count == 0 &&
                                   assembly_test_bytes_equal(segment_label_intel.bytes, (u8 const[]){0xa4}, 1) &&
                                   segment_label_intel.relocation_count == 0 && segment_label_intel.symbol_count == 1 &&
                                   segment_label_att.diagnostic_count == 0 &&
                                   assembly_test_bytes_equal(segment_label_att.bytes, (u8 const[]){0xa4}, 1) &&
                                   segment_label_att.relocation_count == 0 && segment_label_att.symbol_count == 1);

        AssemblyEncodeResult segment_memory_intel = assembly_encode(
            arguments->arena, S8("mov eax, dword ptr fs:[rbx]\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult segment_memory_att = assembly_encode(
            arguments->arena, S8("movl %fs:(%rbx), %eax\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        u8 const expected_segment_memory[] = {0x64, 0x8b, 0x03};
        BUSTER_TEST(arguments, segment_memory_intel.diagnostic_count == 0 &&
                                   assembly_test_bytes_equal(segment_memory_intel.bytes, expected_segment_memory,
                                                             BUSTER_ARRAY_LENGTH(expected_segment_memory)) &&
                                   segment_memory_intel.relocation_count == 0 && segment_memory_intel.symbol_count == 0 &&
                                   segment_memory_att.diagnostic_count == 0 &&
                                   assembly_test_bytes_equal(segment_memory_att.bytes, expected_segment_memory,
                                                             BUSTER_ARRAY_LENGTH(expected_segment_memory)) &&
                                   segment_memory_att.relocation_count == 0 && segment_memory_att.symbol_count == 0);

        u8 const expected_classic_repeat[] = {
            0xf3, 0xa4,
            0xf3, 0xa6,
            0xf3, 0x66, 0xa7,
            0xf2, 0xae,
            0xf2, 0xaf,
            0xf3, 0x66, 0xab,
            0xf3, 0xad,
            0xf3, 0x6d,
            0xf3, 0x6f,
        };
        AssemblyEncodeResult classic_repeat = assembly_encode(
            arguments->arena,
            S8("rep movsb\nrepe cmpsb\nrepz cmpsw\nrepne scasb\nrepnz scasd\n"
               "rep stosw\nrep lodsd\nrep insd\nrep outsd\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, classic_repeat.diagnostic_count == 0 &&
                                   assembly_test_bytes_equal(classic_repeat.bytes, expected_classic_repeat,
                                                             BUSTER_ARRAY_LENGTH(expected_classic_repeat)) &&
                                   classic_repeat.relocation_count == 0 && classic_repeat.symbol_count == 0);

        u8 const expected_loop[] = {0xe2, 0x00, 0xe1, 0x00, 0xe0, 0x00};
        AssemblyEncodeResult loop_forward = assembly_encode(
            arguments->arena, S8("loop forward\nforward:\nloope backward\nbackward:\nloopne done\ndone:\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, loop_forward.diagnostic_count == 0 &&
                                   assembly_test_bytes_equal(loop_forward.bytes, expected_loop, BUSTER_ARRAY_LENGTH(expected_loop)) &&
                                   loop_forward.relocation_count == 0 && loop_forward.symbol_count == 3);

        u8 const expected_loop_aliases[] = {0xe1, 0x00, 0xe0, 0x00};
        AssemblyEncodeResult loop_aliases = assembly_encode(
            arguments->arena, S8("loopz target\ntarget:\nloopnz done\ndone:\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, loop_aliases.diagnostic_count == 0 &&
                                   assembly_test_bytes_equal(loop_aliases.bytes, expected_loop_aliases,
                                                             BUSTER_ARRAY_LENGTH(expected_loop_aliases)) &&
                                   loop_aliases.relocation_count == 0 && loop_aliases.symbol_count == 2);

        AssemblyEncodeResult loop_att = assembly_encode(
            arguments->arena, S8("loop forward\nforward:\nloopz backward\nbackward:\nloopnz done\ndone:\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, loop_att.diagnostic_count == 0 &&
                                   assembly_test_bytes_equal(loop_att.bytes, expected_loop, BUSTER_ARRAY_LENGTH(expected_loop)) &&
                                   loop_att.relocation_count == 0 && loop_att.symbol_count == 3);

        u8 const expected_backward_loop[] = {0x90, 0xe2, 0xfd};
        AssemblyEncodeResult loop_backward = assembly_encode(
            arguments->arena, S8("target:\nnop\nloop target\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, loop_backward.diagnostic_count == 0 &&
                                   assembly_test_bytes_equal(loop_backward.bytes, expected_backward_loop,
                                                             BUSTER_ARRAY_LENGTH(expected_backward_loop)) &&
                                   loop_backward.relocation_count == 0 && loop_backward.symbol_count == 1);

        u8 const expected_jcxz[] = {0x67, 0xe3, 0x00, 0xe3, 0x00};
        AssemblyEncodeResult jcxz_long_mode = assembly_encode(
            arguments->arena, S8("jecxz target\ntarget:\njrcxz done\ndone:\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, jcxz_long_mode.diagnostic_count == 0 &&
                                   assembly_test_bytes_equal(jcxz_long_mode.bytes, expected_jcxz,
                                                             BUSTER_ARRAY_LENGTH(expected_jcxz)) &&
                                   jcxz_long_mode.relocation_count == 0 && jcxz_long_mode.symbol_count == 2);

        AssemblyEncodeResult jcxz_att = assembly_encode(
            arguments->arena, S8("jecxz target\ntarget:\njrcxz done\ndone:\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, jcxz_att.diagnostic_count == 0 &&
                                   assembly_test_bytes_equal(jcxz_att.bytes, expected_jcxz, BUSTER_ARRAY_LENGTH(expected_jcxz)) &&
                                   jcxz_att.relocation_count == 0 && jcxz_att.symbol_count == 2);

        AssemblyEncodeResult explicit_addr32_jecxz = assembly_encode(
            arguments->arena, S8("addr32 jecxz external\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        u8 const expected_explicit_addr32_jecxz[] = {0x67, 0xe3, 0x00};
        BUSTER_TEST(arguments, explicit_addr32_jecxz.diagnostic_count == 0 &&
                                   assembly_test_bytes_equal(explicit_addr32_jecxz.bytes, expected_explicit_addr32_jecxz,
                                                             BUSTER_ARRAY_LENGTH(expected_explicit_addr32_jecxz)) &&
                                   explicit_addr32_jecxz.relocation_count == 1 && explicit_addr32_jecxz.symbol_count == 1 &&
                                   explicit_addr32_jecxz.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC8 &&
                                   explicit_addr32_jecxz.relocations[0].offset == 2);

        AssemblyEncodeResult explicit_addr32_loop = assembly_encode(
            arguments->arena, S8("addr32 loop external\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        u8 const expected_explicit_addr32_loop[] = {0x67, 0xe2, 0x00};
        BUSTER_TEST(arguments, explicit_addr32_loop.diagnostic_count == 0 &&
                                   assembly_test_bytes_equal(explicit_addr32_loop.bytes, expected_explicit_addr32_loop,
                                                             BUSTER_ARRAY_LENGTH(expected_explicit_addr32_loop)) &&
                                   explicit_addr32_loop.relocation_count == 1 && explicit_addr32_loop.symbol_count == 1 &&
                                   explicit_addr32_loop.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC8 &&
                                   explicit_addr32_loop.relocations[0].offset == 2);

        AssemblyEncodeResult loop_repeat_external = assembly_encode(
            arguments->arena,
            S8("repne loopne external_loopne\n"
               "rep loope external_loope\n"
               "addr32 repne loopne external_addr32_loopne\n"
               "addr32 rep loope external_addr32_loope\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        u8 const expected_loop_repeat_external[] = {
            0xf2, 0xe0, 0x00,
            0xf3, 0xe1, 0x00,
            0x67, 0xf2, 0xe0, 0x00,
            0x67, 0xf3, 0xe1, 0x00,
        };
        BUSTER_TEST(arguments, loop_repeat_external.diagnostic_count == 0 &&
                                   assembly_test_bytes_equal(loop_repeat_external.bytes, expected_loop_repeat_external,
                                                             BUSTER_ARRAY_LENGTH(expected_loop_repeat_external)) &&
                                   loop_repeat_external.relocation_count == 4 && loop_repeat_external.symbol_count == 4 &&
                                   loop_repeat_external.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC8 &&
                                   loop_repeat_external.relocations[1].kind == ASSEMBLY_RELOCATION_X86_PC8 &&
                                   loop_repeat_external.relocations[2].kind == ASSEMBLY_RELOCATION_X86_PC8 &&
                                   loop_repeat_external.relocations[3].kind == ASSEMBLY_RELOCATION_X86_PC8 &&
                                   loop_repeat_external.relocations[0].offset == 2 &&
                                   loop_repeat_external.relocations[1].offset == 5 &&
                                   loop_repeat_external.relocations[2].offset == 9 &&
                                   loop_repeat_external.relocations[3].offset == 13);

        AssemblyEncodeResult loop_repeat_aliases = assembly_encode(
            arguments->arena, S8("repe loope loop_target\nloop_target:\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        u8 const expected_loop_repeat_aliases[] = {0xf3, 0xe1, 0x00};
        BUSTER_TEST(arguments, loop_repeat_aliases.diagnostic_count == 0 &&
                                   assembly_test_bytes_equal(loop_repeat_aliases.bytes, expected_loop_repeat_aliases,
                                                             BUSTER_ARRAY_LENGTH(expected_loop_repeat_aliases)) &&
                                   loop_repeat_aliases.relocation_count == 0 && loop_repeat_aliases.symbol_count == 1);

        AssemblyEncodeResult invalid_loop_controls = assembly_encode(
            arguments->arena, S8("addr16 loop external_addr16\nrep repne loopne external_duplicate\nrepne loop external_loop\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, invalid_loop_controls.diagnostic_count == 3 && invalid_loop_controls.bytes.length == 0 &&
                                   invalid_loop_controls.relocation_count == 0 && invalid_loop_controls.symbol_count == 0);

        AssemblyEncodeResult invalid_addr32_jrcxz = assembly_encode(
            arguments->arena, S8("addr32 jrcxz external\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, invalid_addr32_jrcxz.diagnostic_count > 0 && invalid_addr32_jrcxz.bytes.length == 0 &&
                                   invalid_addr32_jrcxz.relocation_count == 0 && invalid_addr32_jrcxz.symbol_count == 0);

        u8 const expected_enter[] = {0xc8, 0x34, 0x12, 0x56};
        AssemblyEncodeResult enter_intel = assembly_encode(
            arguments->arena, S8("enter 0x1234, 0x56\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, enter_intel.diagnostic_count == 0 &&
                                   assembly_test_bytes_equal(enter_intel.bytes, expected_enter, BUSTER_ARRAY_LENGTH(expected_enter)));
        AssemblyEncodeResult enter_att = assembly_encode(
            arguments->arena, S8("enter $0x1234, $0x56\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, enter_att.diagnostic_count == 0 &&
                                   assembly_test_bytes_equal(enter_att.bytes, expected_enter, BUSTER_ARRAY_LENGTH(expected_enter)));

        AssemblyEncodeResult xlat = assembly_encode(
            arguments->arena, S8("xlat\nxlatb\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        u8 const expected_xlat[] = {0xd7, 0xd7};
        BUSTER_TEST(arguments, xlat.diagnostic_count == 0 &&
                                   assembly_test_bytes_equal(xlat.bytes, expected_xlat, BUSTER_ARRAY_LENGTH(expected_xlat)) &&
                                   xlat.relocation_count == 0 && xlat.symbol_count == 0);

        AssemblyEncodeResult xlat_att = assembly_encode(
            arguments->arena, S8("xlat\nxlatb\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, xlat_att.diagnostic_count == 0 &&
                                   assembly_test_bytes_equal(xlat_att.bytes, expected_xlat, BUSTER_ARRAY_LENGTH(expected_xlat)) &&
                                   xlat_att.relocation_count == 0 && xlat_att.symbol_count == 0);

        String8 const invalid_classic_sources[] = {
            S8("enter 65536, 0\n"),
            S8("enter 0, 256\n"),
            S8("movsb rax\n"),
            S8("lock movsb\n"),
            S8("lock lock add dword ptr [rax], ecx\n"),
            S8("jcxz 0\n"),
            S8("rep rep movsb\n"),
            S8("repe repne cmpsb\n"),
            S8("repne repne scasb\n"),
            S8("repne add rax, rbx\n"),
        };
        for (u32 invalid_index = 0; invalid_index < BUSTER_ARRAY_LENGTH(invalid_classic_sources); invalid_index += 1)
        {
            AssemblyEncodeResult invalid_classic = assembly_encode(
                arguments->arena, invalid_classic_sources[invalid_index],
                (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
            BUSTER_TEST(arguments, invalid_classic.diagnostic_count > 0 && invalid_classic.bytes.length == 0 &&
                                       invalid_classic.relocation_count == 0 && invalid_classic.symbol_count == 0);
        }

        AssemblyEncodeResult invalid_duplicate_lock_att = assembly_encode(
            arguments->arena, S8("lock lock addl %ecx, (%rax)\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, invalid_duplicate_lock_att.diagnostic_count > 0 && invalid_duplicate_lock_att.bytes.length == 0 &&
                                   invalid_duplicate_lock_att.relocation_count == 0 && invalid_duplicate_lock_att.symbol_count == 0);

        AssemblyEncodeResult invalid_duplicate_rep_att = assembly_encode(
            arguments->arena, S8("rep rep movsb\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, invalid_duplicate_rep_att.diagnostic_count > 0 && invalid_duplicate_rep_att.bytes.length == 0 &&
                                   invalid_duplicate_rep_att.relocation_count == 0 && invalid_duplicate_rep_att.symbol_count == 0);

        typedef struct ClassicPc8BoundaryCase ClassicPc8BoundaryCase;
        struct ClassicPc8BoundaryCase
        {
            String8 source;
            bool succeeds;
            u8 displacement;
        };
        static ClassicPc8BoundaryCase const classic_pc8_boundary_cases[] = {
            {S8_INITIALIZER("loop 129\n"), true, 0x7f},
            {S8_INITIALIZER("loop 130\n"), false, 0},
            {S8_INITIALIZER("loop -126\n"), true, 0x80},
            {S8_INITIALIZER("loop -127\n"), false, 0},
        };
        for (u32 boundary_index = 0; boundary_index < BUSTER_ARRAY_LENGTH(classic_pc8_boundary_cases); boundary_index += 1)
        {
            ClassicPc8BoundaryCase boundary = classic_pc8_boundary_cases[boundary_index];
            AssemblyEncodeResult boundary_result = assembly_encode(
                arguments->arena, boundary.source,
                (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
            if (boundary.succeeds)
            {
                u8 expected[] = {0xe2, boundary.displacement};
                BUSTER_TEST(arguments, boundary_result.diagnostic_count == 0 &&
                                           assembly_test_bytes_equal(boundary_result.bytes, expected, 2) &&
                                           boundary_result.relocation_count == 0 && boundary_result.symbol_count == 0);
            }
            else
            {
                BUSTER_TEST(arguments, boundary_result.diagnostic_count == 1 &&
                                           boundary_result.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_BRANCH_OUT_OF_RANGE &&
                                           boundary_result.bytes.length == 0 && boundary_result.relocation_count == 0 &&
                                           boundary_result.symbol_count == 0);
            }
        }

        AssemblyEncodeResult valid_then_invalid_classic = assembly_encode(
            arguments->arena, S8("nop\njcxz 0\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        u8 const expected_valid_then_invalid_classic[] = {0x90};
        BUSTER_TEST(arguments, valid_then_invalid_classic.diagnostic_count == 1 &&
                                   assembly_test_bytes_equal(valid_then_invalid_classic.bytes, expected_valid_then_invalid_classic, 1) &&
                                   valid_then_invalid_classic.relocation_count == 0 && valid_then_invalid_classic.symbol_count == 0);

        AssemblyEncodeResult unresolved_pc8 = assembly_encode(
            arguments->arena, S8("loop external_loop\n"),
            (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        u8 const expected_unresolved_pc8[] = {0xe2, 0x00};
        BUSTER_TEST(arguments, unresolved_pc8.diagnostic_count == 0 &&
                                   assembly_test_bytes_equal(unresolved_pc8.bytes, expected_unresolved_pc8,
                                                             BUSTER_ARRAY_LENGTH(expected_unresolved_pc8)) &&
                                   unresolved_pc8.symbol_count == 1 && unresolved_pc8.relocation_count == 1 &&
                                   unresolved_pc8.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC8 &&
                                   unresolved_pc8.relocations[0].offset == 1 && unresolved_pc8.relocations[0].addend == -1 &&
                                   unresolved_pc8.relocations[0].symbol == 0);

        typedef struct ClassicUnresolvedPc8Case ClassicUnresolvedPc8Case;
        struct ClassicUnresolvedPc8Case
        {
            String8 mnemonic;
            u8 expected_prefix;
            u8 expected_opcode;
            u32 expected_offset;
        };
        static ClassicUnresolvedPc8Case const classic_unresolved_pc8_cases[] = {
            {S8_INITIALIZER("jecxz"), 0x67, 0xe3, 2},
            {S8_INITIALIZER("jrcxz"), 0x00, 0xe3, 1},
        };
        for (u32 unresolved_index = 0; unresolved_index < BUSTER_ARRAY_LENGTH(classic_unresolved_pc8_cases); unresolved_index += 1)
        {
            ClassicUnresolvedPc8Case unresolved_case = classic_unresolved_pc8_cases[unresolved_index];
            AssemblyEncodeResult unresolved_intel = assembly_encode(
                arguments->arena, string_format(arguments->arena, S8("{S8} external\n"), unresolved_case.mnemonic),
                (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
            AssemblyEncodeResult unresolved_att = assembly_encode(
                arguments->arena, string_format(arguments->arena, S8("{S8} external\n"), unresolved_case.mnemonic),
                (AssemblyEncodeOptions){.target = x86_target, .syntax = ASSEMBLY_SYNTAX_ATT});
            u8 expected_unresolved[3] = {0};
            if (unresolved_case.expected_prefix)
            {
                expected_unresolved[0] = unresolved_case.expected_prefix;
                expected_unresolved[1] = unresolved_case.expected_opcode;
            }
            else
            {
                expected_unresolved[0] = unresolved_case.expected_opcode;
            }
            u32 expected_byte_count = unresolved_case.expected_prefix ? 3 : 2;
            BUSTER_TEST(arguments, unresolved_intel.diagnostic_count == 0 &&
                                       assembly_test_bytes_equal(unresolved_intel.bytes, expected_unresolved, expected_byte_count) &&
                                       unresolved_intel.symbol_count == 1 && unresolved_intel.relocation_count == 1 &&
                                       unresolved_intel.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC8 &&
                                       unresolved_intel.relocations[0].offset == unresolved_case.expected_offset &&
                                       unresolved_intel.relocations[0].addend == -1 && unresolved_intel.relocations[0].symbol == 0 &&
                                       unresolved_att.diagnostic_count == 0 &&
                                       assembly_test_bytes_equal(unresolved_att.bytes, expected_unresolved, expected_byte_count) &&
                                       unresolved_att.symbol_count == 1 && unresolved_att.relocation_count == 1 &&
                                       unresolved_att.relocations[0].kind == ASSEMBLY_RELOCATION_X86_PC8 &&
                                       unresolved_att.relocations[0].offset == unresolved_case.expected_offset &&
                                       unresolved_att.relocations[0].addend == -1 && unresolved_att.relocations[0].symbol == 0);
        }
    }

    return result;
}
#endif
