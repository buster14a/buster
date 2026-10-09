#include <buster/tests/compiler/assembly/aarch64_base_assembly_test.h>

#if BUSTER_INCLUDE_TESTS

#include <buster/lib/compiler/assembly/aarch64_base_assembly.h>
#include <buster/lib/compiler/assembly/assembly.h>
#include <buster/lib/compiler/assembly/assembly_unit.h>
#include <buster/lib/string.h>

// Base A64 statements the table-driven AArch64 owners refuse (#2673):
// frame pairs, unscaled and register-offset transfers, MOV/shift/select
// aliases, scalar multiply and floating point, element moves, barriers,
// exception generation, MRS/MSR, common AdvSIMD forms and LSE.
// Expected words are llvm-mc 18.1.3 `-show-encoding` output. The corpus-wide
// census over compiler-emitted fixtures is recorded on #2688; its native
// reimplementation is tracked by #2695.

typedef struct Aarch64BaseAssemblyCase Aarch64BaseAssemblyCase;
struct Aarch64BaseAssemblyCase
{
    String8 source;
    u32 word;
};

BUSTER_GLOBAL_LOCAL bool aarch64_base_assembly_test_word(ByteSlice bytes, u64 offset, u32 word)
{
    bool equal = bytes.length >= offset + 4;
    for (u32 index = 0; equal && index < 4; index += 1)
    {
        equal = bytes.pointer[offset + index] == (u8)(word >> (index * 8u));
    }
    return equal;
}

// Assemble one statement standalone and inside a unit followed by RET, so
// both entry points and the instruction's layout size are checked.
BUSTER_GLOBAL_LOCAL void aarch64_base_assembly_test_case(UnitTestArguments* arguments, UnitTestResult* test_result, Target target,
                                                         Aarch64BaseAssemblyCase test_case)
{
    UnitTestResult result = *test_result;
    AssemblyEncodeResult instruction = assembly_encode(arguments->arena, test_case.source, (AssemblyEncodeOptions){.target = target});
    BUSTER_TEST_RAW(arguments, !instruction.diagnostic_count && !instruction.relocation_count &&
        instruction.bytes.length == 4 && aarch64_base_assembly_test_word(instruction.bytes, 0, test_case.word), test_case.source);
    String8 source = string_format(arguments->arena, S8(".text\n{S8}\nret\n"), test_case.source);
    AssemblyUnitResult unit = assembly_unit_encode(arguments->arena, source, (AssemblyEncodeOptions){.target = target});
    BUSTER_TEST_RAW(arguments, !unit.diagnostic_count && unit.section_count == 1 && unit.sections[0].data.length == 8 &&
        aarch64_base_assembly_test_word(unit.sections[0].data, 0, test_case.word) &&
        aarch64_base_assembly_test_word(unit.sections[0].data, 4, UINT32_C(0xd65f03c0)),
        source);
    *test_result = result;
}

BUSTER_GLOBAL_LOCAL void aarch64_base_assembly_test_complex_simd_lane(UnitTestArguments* arguments,
                                                                      UnitTestResult* test_result, Target apple)
{
    UnitTestResult result = *test_result;
    static Aarch64BaseAssemblyCase const fmla_seed_cases[] = {
        {S8_INITIALIZER("fmla v0.4s, v1.4s, v2.s[0]"), UINT32_C(0x4f821020)},
        {S8_INITIALIZER("fmla v0.4s, v20.4s, v21.s[0]"), UINT32_C(0x4f951280)},
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(fmla_seed_cases); index += 1)
    {
        aarch64_base_assembly_test_case(arguments, &result, apple, fmla_seed_cases[index]);
    }

    Target fp16_target = apple;
    fp16_target.cpu_features_explicit = true;
    fp16_target.cpu_features = target_cpu_features_from_array(
        (TargetCpuFeature const[]){
            TARGET_CPU_FEATURE_AARCH64_FP_ARMV8,
            TARGET_CPU_FEATURE_AARCH64_NEON,
            TARGET_CPU_FEATURE_AARCH64_FULLFP16,
        },
        3);

    /* The lane carrier in the half form is limited to V0..V15 and H lanes
     * are 0..7. Run malformed half forms with every required feature enabled,
     * so the source parser must diagnose the operand shape/range. */
    static String8 const invalid_half_sources[] = {
        S8_INITIALIZER("fmla v0.8h, v1.8h, v15.h[8]"),
        S8_INITIALIZER("fmla v0.8h, v1.8h, v16.h[0]"),
        S8_INITIALIZER("fmla v0.8h, v1.8h, v2.s[0]"),
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(invalid_half_sources); index += 1)
    {
        AssemblyEncodeResult invalid = assembly_encode(arguments->arena, invalid_half_sources[index],
            (AssemblyEncodeOptions){.target = fp16_target});
        BUSTER_TEST_RAW(arguments, invalid.diagnostic_count == 1 && !invalid.bytes.length &&
            invalid.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS, invalid_half_sources[index]);
    }

    /* The fixed-H scalar form must not accept another scalar prefix, and its
     * literal .H lane arrangement must match the source register. */
    static String8 const invalid_scalar_lane_sources[] = {
        S8_INITIALIZER("fmla s0, s1, v2.h[0]"),
        S8_INITIALIZER("fmla d0, d1, v2.h[0]"),
        S8_INITIALIZER("fmla h0, h1, v2.s[0]"),
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(invalid_scalar_lane_sources); index += 1)
    {
        AssemblyEncodeResult invalid = assembly_encode(arguments->arena, invalid_scalar_lane_sources[index],
            (AssemblyEncodeOptions){.target = fp16_target});
        BUSTER_TEST_RAW(arguments, invalid.diagnostic_count == 1 && !invalid.bytes.length &&
            invalid.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS, invalid_scalar_lane_sources[index]);
    }

    static Aarch64BaseAssemblyCase const additional_lane_seeds[] = {
        {S8_INITIALIZER("fmla v0.8h, v1.8h, v15.h[7]"), UINT32_C(0x4f3f1820)},
        {S8_INITIALIZER("fmla d0, d1, v2.d[0]"), UINT32_C(0x5fc21020)},
        {S8_INITIALIZER("fmla v0.2d, v1.2d, v2.d[0]"), UINT32_C(0x4fc21020)},
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(additional_lane_seeds); index += 1)
    {
        aarch64_base_assembly_test_case(arguments, &result, fp16_target, additional_lane_seeds[index]);
    }

    /* The ROTATE operand must retain its VM type through source parsing and
     * typed encoding. This source uses a nonzero legal quarter-turn. */
    Target fcma_target = apple;
    fcma_target.cpu_features_explicit = true;
    fcma_target.cpu_features = target_cpu_features_from_array(
        (TargetCpuFeature const[]){
            TARGET_CPU_FEATURE_AARCH64_FP_ARMV8,
            TARGET_CPU_FEATURE_AARCH64_NEON,
            TARGET_CPU_FEATURE_AARCH64_COMPLXNUM,
        },
        3);
    Aarch64BaseAssemblyCase fcmla_rotation_case = {
        S8_INITIALIZER("fcmla v0.4s, v1.4s, v2.s[0], #90"), UINT32_C(0x6f823020)};
    aarch64_base_assembly_test_case(arguments, &result, fcma_target, fcmla_rotation_case);

    /* Keep each feature gate isolated while satisfying the other generated
     * row requirements. */
    Target no_neon = fp16_target;
    no_neon.cpu_features = target_cpu_features_remove(no_neon.cpu_features, TARGET_CPU_FEATURE_AARCH64_NEON);
    String8 neon_source = S8("fmla v0.4s, v1.4s, v2.s[0]");
    AssemblyEncodeResult neon_refusal = assembly_encode(arguments->arena, neon_source,
        (AssemblyEncodeOptions){.target = no_neon});
    BUSTER_TEST_RAW(arguments, neon_refusal.diagnostic_count == 1 && !neon_refusal.bytes.length &&
        neon_refusal.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE, neon_source);

    Target no_fullfp16 = fp16_target;
    no_fullfp16.cpu_features = target_cpu_features_remove(no_fullfp16.cpu_features, TARGET_CPU_FEATURE_AARCH64_FULLFP16);
    String8 fullfp16_source = S8("fmla v0.8h, v1.8h, v15.h[7]");
    AssemblyEncodeResult fullfp16_refusal = assembly_encode(arguments->arena, fullfp16_source,
        (AssemblyEncodeOptions){.target = no_fullfp16});
    BUSTER_TEST_RAW(arguments, fullfp16_refusal.diagnostic_count == 1 && !fullfp16_refusal.bytes.length &&
        fullfp16_refusal.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE, fullfp16_source);

    *test_result = result;
}


UnitTestResult aarch64_base_assembly_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Target baseline = {.cpu_arch = CPU_ARCH_AARCH64, .cpu_model = CPU_MODEL_BASELINE, .os = OPERATING_SYSTEM_LINUX};
    Target apple = {.cpu_arch = CPU_ARCH_AARCH64, .cpu_model = CPU_MODEL_A64_APPLE_M1, .os = OPERATING_SYSTEM_MACOS};
    aarch64_base_assembly_test_complex_simd_lane(arguments, &result, apple);
    static Aarch64BaseAssemblyCase const cases[] = {
        {S8_INITIALIZER("stp x29, x30, [sp, #-16]!"), UINT32_C(0xa9bf7bfd)},
        {S8_INITIALIZER("stp x29, x30, [sp, #16]"), UINT32_C(0xa9017bfd)},
        {S8_INITIALIZER("ldp x29, x30, [sp], #16"), UINT32_C(0xa8c17bfd)},
        {S8_INITIALIZER("ldp x0, x1, [x2]"), UINT32_C(0xa9400440)},
        {S8_INITIALIZER("ldur w8, [x29, #-4]"), UINT32_C(0xb85fc3a8)},
        {S8_INITIALIZER("stur w0, [x29, #-4]"), UINT32_C(0xb81fc3a0)},
        {S8_INITIALIZER("mov x29, sp"), UINT32_C(0x910003fd)},
        {S8_INITIALIZER("mov x0, #-1"), UINT32_C(0x92800000)},
        {S8_INITIALIZER("mov w0, #65536"), UINT32_C(0x52a00020)},
        {S8_INITIALIZER("fmov d0, x0"), UINT32_C(0x9e670000)},
        {S8_INITIALIZER("fmov s0, #1.0"), UINT32_C(0x1e2e1000)},
        {S8_INITIALIZER("lsl w8, w0, #2"), UINT32_C(0x531e7408)},
        {S8_INITIALIZER("lsr x0, x1, #3"), UINT32_C(0xd343fc20)},
        {S8_INITIALIZER("cset w0, eq"), UINT32_C(0x1a9f17e0)},
        {S8_INITIALIZER("mvn w0, w1"), UINT32_C(0x2a2103e0)},
        {S8_INITIALIZER("ldr x0, [x1, x2, lsl #3]"), UINT32_C(0xf8627820)},
        {S8_INITIALIZER("ldrsw x8, [sp, #4]"), UINT32_C(0xb98007e8)},
        {S8_INITIALIZER("mul w0, w1, w2"), UINT32_C(0x1b027c20)},
        {S8_INITIALIZER("fdiv d0, d1, d2"), UINT32_C(0x1e621820)},
        {S8_INITIALIZER("scvtf d0, w0"), UINT32_C(0x1e620000)},
        {S8_INITIALIZER("fcvtzs w0, d0"), UINT32_C(0x1e780000)},
        {S8_INITIALIZER("ldr x0, [x1, #-8]"), UINT32_C(0xf85f8020)},
        {S8_INITIALIZER("ldrb w0, [x1, x2, lsl #0]"), UINT32_C(0x38627820)},
        {S8_INITIALIZER("ldrb w0, [x1, x2]"), UINT32_C(0x38626820)},
        {S8_INITIALIZER("ldrh w0, [x1, w2, sxtw #1]"), UINT32_C(0x7862d820)},
        {S8_INITIALIZER("str q0, [sp, #-32]!"), UINT32_C(0x3c9e0fe0)},
        {S8_INITIALIZER("ldp q0, q1, [x0, #32]"), UINT32_C(0xad410400)},
        {S8_INITIALIZER("fmov d0, #0.0"), UINT32_C(0x9e6703e0)},
        {S8_INITIALIZER("fmov s1, #-2.5"), UINT32_C(0x1e309001)},
        {S8_INITIALIZER("fcmp d0, #0.0"), UINT32_C(0x1e602008)},
        {S8_INITIALIZER("fcsel s0, s1, s2, ne"), UINT32_C(0x1e221c20)},
        {S8_INITIALIZER("fmadd d0, d1, d2, d3"), UINT32_C(0x1f420c20)},
        {S8_INITIALIZER("fcvt s0, d1"), UINT32_C(0x1e624020)},
        {S8_INITIALIZER("ubfx x0, x1, #4, #8"), UINT32_C(0xd3442c20)},
        {S8_INITIALIZER("bfi w0, w1, #3, #5"), UINT32_C(0x331d1020)},
        {S8_INITIALIZER("bfc w3, #0, #32"), UINT32_C(0x33007fe3)},
        {S8_INITIALIZER("bfc wzr, #31, #1"), UINT32_C(0x330103ff)},
        {S8_INITIALIZER("bfc x0, #5, #9"), UINT32_C(0xb37b23e0)},
        {S8_INITIALIZER("bfc xzr, #63, #1"), UINT32_C(0xb34103ff)},
        {S8_INITIALIZER("sbfiz x0, x1, #2, #30"), UINT32_C(0x937e7420)},
        {S8_INITIALIZER("sxtb x0, w1"), UINT32_C(0x93401c20)},
        {S8_INITIALIZER("uxth w0, w1"), UINT32_C(0x53003c20)},
        {S8_INITIALIZER("ror x0, x1, #7"), UINT32_C(0x93c11c20)},
        {S8_INITIALIZER("asr w0, w1, w2"), UINT32_C(0x1ac22820)},
        {S8_INITIALIZER("cinc x0, x1, lt"), UINT32_C(0x9a81a420)},
        {S8_INITIALIZER("csetm w0, hi"), UINT32_C(0x5a9f93e0)},
        {S8_INITIALIZER("ccmp x0, #3, #4, ne"), UINT32_C(0xfa431804)},
        {S8_INITIALIZER("madd x0, x1, x2, x3"), UINT32_C(0x9b020c20)},
        {S8_INITIALIZER("smull x0, w1, w2"), UINT32_C(0x9b227c20)},
        {S8_INITIALIZER("umulh x0, x1, x2"), UINT32_C(0x9bc27c20)},
        {S8_INITIALIZER("add sp, sp, x1"), UINT32_C(0x8b2163ff)},
        {S8_INITIALIZER("add x0, sp, x1, lsl #2"), UINT32_C(0x8b216be0)},
        {S8_INITIALIZER("add x0, x1, w2, sxtw #2"), UINT32_C(0x8b22c820)},
        {S8_INITIALIZER("add x0, x1, #4096"), UINT32_C(0x91400420)},
        {S8_INITIALIZER("add x0, x1, #-16"), UINT32_C(0xd1004020)},
        {S8_INITIALIZER("cmp w0, #-1"), UINT32_C(0x3100041f)},
        {S8_INITIALIZER("tst w0, #0xff"), UINT32_C(0x72001c1f)},
        {S8_INITIALIZER("and x0, x1, #0x5555555555555555"), UINT32_C(0x9200f020)},
        {S8_INITIALIZER("mov x0, #0x5555555555555555"), UINT32_C(0xb200f3e0)},
        {S8_INITIALIZER("neg x0, x1, lsl #3"), UINT32_C(0xcb010fe0)},
        {S8_INITIALIZER("umov w0, v1.b[3]"), UINT32_C(0x0e073c20)},
        {S8_INITIALIZER("smov x0, v1.h[2]"), UINT32_C(0x4e0a2c20)},
        {S8_INITIALIZER("mov w0, v1.s[1]"), UINT32_C(0x0e0c3c20)},
        {S8_INITIALIZER("mov v0.d[1], x1"), UINT32_C(0x4e181c20)},
        {S8_INITIALIZER("mov v0.s[1], v1.s[0]"), UINT32_C(0x6e0c0420)},
        {S8_INITIALIZER("dup v0.4s, w1"), UINT32_C(0x4e040c20)},
        {S8_INITIALIZER("mov s0, v1.s[2]"), UINT32_C(0x5e140420)},
        {S8_INITIALIZER("fmov x0, v1.d[1]"), UINT32_C(0x9eae0020)},
        {S8_INITIALIZER("rev x0, x1"), UINT32_C(0xdac00c20)},
        {S8_INITIALIZER("clz w0, w1"), UINT32_C(0x5ac01020)},
        {S8_INITIALIZER("prfm pldl1keep, [x0, #8]"), UINT32_C(0xf9800400)},
        {S8_INITIALIZER("ldaxr w0, [x1]"), UINT32_C(0x885ffc20)},
        {S8_INITIALIZER("stlxr w2, x0, [x1]"), UINT32_C(0xc802fc20)},
        {S8_INITIALIZER("ldar x0, [sp]"), UINT32_C(0xc8dfffe0)},
        {S8_INITIALIZER("stlrb w0, [x1]"), UINT32_C(0x089ffc20)},
        {S8_INITIALIZER("movk x0, #0x1234, lsl #32"), UINT32_C(0xf2c24680)},
        {S8_INITIALIZER("adc x0, x1, x2"), UINT32_C(0x9a020020)},
        {S8_INITIALIZER("ngc w0, w1"), UINT32_C(0x5a0103e0)},
        {S8_INITIALIZER("extr x0, x1, x2, #13"), UINT32_C(0x93c23420)},
        {S8_INITIALIZER("ldnp d0, d1, [x0, #-16]"), UINT32_C(0x6c7f0400)},
        {S8_INITIALIZER("ldpsw x0, x1, [x2, #8]!"), UINT32_C(0x69c10440)},
        {S8_INITIALIZER("scvtf s0, x0, #16"), UINT32_C(0x9e02c000)},
        {S8_INITIALIZER("fcvtzu w0, s0, #3"), UINT32_C(0x1e19f400)},
        {S8_INITIALIZER("fcvtzs d0, d1"), UINT32_C(0x5ee1b820)},
        {S8_INITIALIZER("ldr h0, [x0, #2]"), UINT32_C(0x7d400400)},
        {S8_INITIALIZER("str b0, [x0, x1]"), UINT32_C(0x3c216800)},
        {S8_INITIALIZER("stp x29, x30, [sp, #-0x10]!"), UINT32_C(0xa9bf7bfd)},
        {S8_INITIALIZER("ldp x29, x30, [sp], #0x10"), UINT32_C(0xa8c17bfd)},
        {S8_INITIALIZER("stp w0, w1, [x2, #-256]"), UINT32_C(0x29200440)},
        {S8_INITIALIZER("ldp s0, s1, [sp, #252]!"), UINT32_C(0x2ddf87e0)},
        {S8_INITIALIZER("stnp q0, q1, [x0, #1008]"), UINT32_C(0xac1f8400)},
        {S8_INITIALIZER("str x30, [sp, #-16]!"), UINT32_C(0xf81f0ffe)},
        {S8_INITIALIZER("ldr x30, [sp], #16"), UINT32_C(0xf84107fe)},
        {S8_INITIALIZER("ldurb w0, [x1, #255]"), UINT32_C(0x384ff020)},
        {S8_INITIALIZER("ldursh x0, [x1, #-256]"), UINT32_C(0x78900020)},
        {S8_INITIALIZER("sturh w0, [sp, #1]"), UINT32_C(0x780013e0)},
        {S8_INITIALIZER("ldrsb w0, [x1, #4095]"), UINT32_C(0x39fffc20)},
        {S8_INITIALIZER("ldr q0, [x1, x2, lsl #4]"), UINT32_C(0x3ce27820)},
        {S8_INITIALIZER("ldr w0, [x1, w2, uxtw]"), UINT32_C(0xb8624820)},
        {S8_INITIALIZER("ldr x0, [x1, w2, sxtw #3]"), UINT32_C(0xf862d820)},
        {S8_INITIALIZER("ldrsh x0, [x1, x2, sxtx #1]"), UINT32_C(0x78a2f820)},
        {S8_INITIALIZER("prfm pstl3strm, [x1, x2]"), UINT32_C(0xf8a26835)},
        {S8_INITIALIZER("mov sp, x0"), UINT32_C(0x9100001f)},
        {S8_INITIALIZER("mov wsp, w1"), UINT32_C(0x1100003f)},
        {S8_INITIALIZER("mov w0, #-1"), UINT32_C(0x12800000)},
        {S8_INITIALIZER("mov x0, #0xffff0000"), UINT32_C(0xd2bfffe0)},
        {S8_INITIALIZER("mov x0, #0x123400000000"), UINT32_C(0xd2c24680)},
        {S8_INITIALIZER("mov w0, #0x7fffffff"), UINT32_C(0x12b00000)},
        {S8_INITIALIZER("mov x0, #-65537"), UINT32_C(0x92a00020)},
        {S8_INITIALIZER("mov w0, wzr"), UINT32_C(0x2a1f03e0)},
        {S8_INITIALIZER("bic x0, x1, x2, ror #7"), UINT32_C(0x8ae21c20)},
        {S8_INITIALIZER("orn w0, w1, #0xff"), UINT32_C(0x32185c20)},
        {S8_INITIALIZER("eon x0, x1, x2, lsr #1"), UINT32_C(0xca620420)},
        {S8_INITIALIZER("ands w0, w1, #0x80000001"), UINT32_C(0x72010420)},
        {S8_INITIALIZER("bics xzr, x1, x2"), UINT32_C(0xea22003f)},
        {S8_INITIALIZER("tst x0, x1, asr #3"), UINT32_C(0xea810c1f)},
        {S8_INITIALIZER("mvn x0, x1, lsl #2"), UINT32_C(0xaa210be0)},
        {S8_INITIALIZER("negs w0, w1"), UINT32_C(0x6b0103e0)},
        {S8_INITIALIZER("cmn sp, #8"), UINT32_C(0xb10023ff)},
        {S8_INITIALIZER("cmp x0, w1, uxtb #1"), UINT32_C(0xeb21041f)},
        {S8_INITIALIZER("subs x0, sp, x1"), UINT32_C(0xeb2163e0)},
        {S8_INITIALIZER("add w0, w1, #1, lsl #12"), UINT32_C(0x11400420)},
        {S8_INITIALIZER("adds xzr, x0, #4095"), UINT32_C(0xb13ffc1f)},
        {S8_INITIALIZER("asr x0, x1, #63"), UINT32_C(0x937ffc20)},
        {S8_INITIALIZER("lsr w0, w1, w2"), UINT32_C(0x1ac22420)},
        {S8_INITIALIZER("ror w0, w1, w2"), UINT32_C(0x1ac22c20)},
        {S8_INITIALIZER("lsl x0, x1, #0"), UINT32_C(0xd340fc20)},
        {S8_INITIALIZER("sxth w0, w1"), UINT32_C(0x13003c20)},
        {S8_INITIALIZER("sxtw x0, w1"), UINT32_C(0x93407c20)},
        {S8_INITIALIZER("uxtb w0, w1"), UINT32_C(0x53001c20)},
        {S8_INITIALIZER("sbfx w0, w1, #31, #1"), UINT32_C(0x131f7c20)},
        {S8_INITIALIZER("bfxil x0, x1, #0, #64"), UINT32_C(0xb340fc20)},
        {S8_INITIALIZER("ubfiz w0, w1, #31, #1"), UINT32_C(0x53010020)},
        {S8_INITIALIZER("sbfm x0, x1, #3, #5"), UINT32_C(0x93431420)},
        {S8_INITIALIZER("ubfm w0, w1, #31, #0"), UINT32_C(0x531f0020)},
        {S8_INITIALIZER("csinc w0, w1, w2, al"), UINT32_C(0x1a82e420)},
        {S8_INITIALIZER("csneg x0, x1, x2, nv"), UINT32_C(0xda82f420)},
        {S8_INITIALIZER("cneg w0, w1, mi"), UINT32_C(0x5a815420)},
        {S8_INITIALIZER("cinv x0, x1, hs"), UINT32_C(0xda813020)},
        {S8_INITIALIZER("ccmn w0, w1, #15, lo"), UINT32_C(0x3a41300f)},
        {S8_INITIALIZER("udiv x0, x1, x2"), UINT32_C(0x9ac20820)},
        {S8_INITIALIZER("sdiv w0, w1, w2"), UINT32_C(0x1ac20c20)},
        {S8_INITIALIZER("rorv x0, x1, x2"), UINT32_C(0x9ac22c20)},
        {S8_INITIALIZER("rbit w0, w1"), UINT32_C(0x5ac00020)},
        {S8_INITIALIZER("rev16 x0, x1"), UINT32_C(0xdac00420)},
        {S8_INITIALIZER("rev32 x0, x1"), UINT32_C(0xdac00820)},
        {S8_INITIALIZER("rev64 x0, x1"), UINT32_C(0xdac00c20)},
        {S8_INITIALIZER("cls x0, x1"), UINT32_C(0xdac01420)},
        {S8_INITIALIZER("msub w0, w1, w2, w3"), UINT32_C(0x1b028c20)},
        {S8_INITIALIZER("mneg x0, x1, x2"), UINT32_C(0x9b02fc20)},
        {S8_INITIALIZER("umaddl x0, w1, w2, x3"), UINT32_C(0x9ba20c20)},
        {S8_INITIALIZER("smsubl x0, w1, w2, x3"), UINT32_C(0x9b228c20)},
        {S8_INITIALIZER("umnegl x0, w1, w2"), UINT32_C(0x9ba2fc20)},
        {S8_INITIALIZER("smulh x0, x1, x2"), UINT32_C(0x9b427c20)},
        {S8_INITIALIZER("sbcs w0, w1, w2"), UINT32_C(0x7a020020)},
        {S8_INITIALIZER("ngcs x0, x1"), UINT32_C(0xfa0103e0)},
        {S8_INITIALIZER("movn w0, #0xffff, lsl #16"), UINT32_C(0x12bfffe0)},
        {S8_INITIALIZER("movz x0, #1, lsl #48"), UINT32_C(0xd2e00020)},
        {S8_INITIALIZER("fmov s0, w1"), UINT32_C(0x1e270020)},
        {S8_INITIALIZER("fmov w0, s1"), UINT32_C(0x1e260020)},
        {S8_INITIALIZER("fmov d0, d1"), UINT32_C(0x1e604020)},
        {S8_INITIALIZER("fmov d0, #31.0"), UINT32_C(0x1e67f000)},
        {S8_INITIALIZER("fmov s0, #0.125"), UINT32_C(0x1e281000)},
        {S8_INITIALIZER("fmov d0, #-1.9375"), UINT32_C(0x1e7ff000)},
        {S8_INITIALIZER("fmov d0, #1.00000000e+00"), UINT32_C(0x1e6e1000)},
        {S8_INITIALIZER("fabs d0, d1"), UINT32_C(0x1e60c020)},
        {S8_INITIALIZER("fneg s0, s1"), UINT32_C(0x1e214020)},
        {S8_INITIALIZER("fsqrt d0, d1"), UINT32_C(0x1e61c020)},
        {S8_INITIALIZER("frintm s0, s1"), UINT32_C(0x1e254020)},
        {S8_INITIALIZER("frinti d0, d1"), UINT32_C(0x1e67c020)},
        {S8_INITIALIZER("fcvt d0, s1"), UINT32_C(0x1e22c020)},
        {S8_INITIALIZER("fmax d0, d1, d2"), UINT32_C(0x1e624820)},
        {S8_INITIALIZER("fminnm s0, s1, s2"), UINT32_C(0x1e227820)},
        {S8_INITIALIZER("fnmul d0, d1, d2"), UINT32_C(0x1e628820)},
        {S8_INITIALIZER("fnmsub s0, s1, s2, s3"), UINT32_C(0x1f228c20)},
        {S8_INITIALIZER("fcmpe s0, s1"), UINT32_C(0x1e212010)},
        {S8_INITIALIZER("fcmpe d0, #0.0"), UINT32_C(0x1e602018)},
        {S8_INITIALIZER("fccmpe d0, d1, #8, gt"), UINT32_C(0x1e61c418)},
        {S8_INITIALIZER("ucvtf s0, x1"), UINT32_C(0x9e230020)},
        {S8_INITIALIZER("fcvtzu x0, d1"), UINT32_C(0x9e790020)},
        {S8_INITIALIZER("fcvtms w0, s1"), UINT32_C(0x1e300020)},
        {S8_INITIALIZER("fcvtau x0, s1"), UINT32_C(0x9e250020)},
        {S8_INITIALIZER("ucvtf d0, d1"), UINT32_C(0x7e61d820)},
        {S8_INITIALIZER("fcvtzs s0, s1"), UINT32_C(0x5ea1b820)},
        {S8_INITIALIZER("ins v0.h[3], w1"), UINT32_C(0x4e0e1c20)},
        {S8_INITIALIZER("ins v0.b[15], v1.b[0]"), UINT32_C(0x6e1f0420)},
        {S8_INITIALIZER("dup v0.2d, x1"), UINT32_C(0x4e080c20)},
        {S8_INITIALIZER("dup v0.8h, v1.h[7]"), UINT32_C(0x4e1e0420)},
        {S8_INITIALIZER("dup b0, v1.b[9]"), UINT32_C(0x5e130420)},
        {S8_INITIALIZER("umov x0, v1.d[1]"), UINT32_C(0x4e183c20)},
        {S8_INITIALIZER("smov w0, v1.b[15]"), UINT32_C(0x0e1f2c20)},
        {S8_INITIALIZER("mov v0.16b, v1.16b"), UINT32_C(0x4ea11c20)},
        {S8_INITIALIZER("mov v0.8b, v1.8b"), UINT32_C(0x0ea11c20)},
        {S8_INITIALIZER("stxrb w0, w1, [x2]"), UINT32_C(0x08007c41)},
        {S8_INITIALIZER("ldxrh w0, [x1, #0]"), UINT32_C(0x485f7c20)},
        {S8_INITIALIZER("ldxr x0, [x1]"), UINT32_C(0xc85f7c20)},
        {S8_INITIALIZER("stlr w0, [sp]"), UINT32_C(0x889fffe0)},
        {S8_INITIALIZER("ldarh w0, [x1]"), UINT32_C(0x48dffc20)},
        {S8_INITIALIZER("dmb ish"), UINT32_C(0xd5033bbf)},
        {S8_INITIALIZER("dmb ishld"), UINT32_C(0xd50339bf)},
        {S8_INITIALIZER("dsb sy"), UINT32_C(0xd5033f9f)},
        {S8_INITIALIZER("dsb #3"), UINT32_C(0xd503339f)},
        {S8_INITIALIZER("isb"), UINT32_C(0xd5033fdf)},
        {S8_INITIALIZER("isb sy"), UINT32_C(0xd5033fdf)},
        {S8_INITIALIZER("clrex"), UINT32_C(0xd5033f5f)},
        {S8_INITIALIZER("clrex #5"), UINT32_C(0xd503355f)},
        {S8_INITIALIZER("brk #0"), UINT32_C(0xd4200000)},
        {S8_INITIALIZER("brk #0x3e8"), UINT32_C(0xd4207d00)},
        {S8_INITIALIZER("svc #0x80"), UINT32_C(0xd4001001)},
        {S8_INITIALIZER("hlt #1"), UINT32_C(0xd4400020)},
        {S8_INITIALIZER("udf #12"), UINT32_C(0x0000000c)},
        {S8_INITIALIZER("mrs x9, TPIDR_EL0"), UINT32_C(0xd53bd049)},
        {S8_INITIALIZER("mrs x0, nzcv"), UINT32_C(0xd53b4200)},
        {S8_INITIALIZER("msr fpcr, x1"), UINT32_C(0xd51b4401)},
        {S8_INITIALIZER("msr tpidr_el0, x3"), UINT32_C(0xd51bd043)},
        {S8_INITIALIZER("mrs x0, cntvct_el0"), UINT32_C(0xd53be040)},
        {S8_INITIALIZER("movi d0, #0000000000000000"), UINT32_C(0x2f00e400)},
        {S8_INITIALIZER("movi v0.2d, #0xff00ff00ff00ff00"), UINT32_C(0x6f05e540)},
        {S8_INITIALIZER("movi v1.16b, #255"), UINT32_C(0x4f07e7e1)},
        {S8_INITIALIZER("movi v2.8b, #0x7f"), UINT32_C(0x0f03e7e2)},
        {S8_INITIALIZER("movi v3.4s, #1"), UINT32_C(0x4f000423)},
        {S8_INITIALIZER("movi v3.4s, #1, lsl #8"), UINT32_C(0x4f002423)},
        {S8_INITIALIZER("movi v3.2s, #12, msl #16"), UINT32_C(0x0f00d583)},
        {S8_INITIALIZER("movi v4.8h, #3, lsl #8"), UINT32_C(0x4f00a464)},
        {S8_INITIALIZER("mvni v5.4s, #7, lsl #24"), UINT32_C(0x6f0064e5)},
        {S8_INITIALIZER("mvni v5.4h, #9"), UINT32_C(0x2f008525)},
        {S8_INITIALIZER("bic v6.4h, #255, lsl #8"), UINT32_C(0x2f07b7e6)},
        {S8_INITIALIZER("orr v7.4s, #1, lsl #16"), UINT32_C(0x4f005427)},
        {S8_INITIALIZER("fmov v0.2s, #1.00000000"), UINT32_C(0x0f03f600)},
        {S8_INITIALIZER("fmov v0.4s, #2.50000000"), UINT32_C(0x4f00f480)},
        {S8_INITIALIZER("fmov v0.2d, #-3.0"), UINT32_C(0x6f04f500)},
        {S8_INITIALIZER("mvn v0.16b, v1.16b"), UINT32_C(0x6e205820)},
        {S8_INITIALIZER("not v0.8b, v1.8b"), UINT32_C(0x2e205820)},
        {S8_INITIALIZER("ext v0.16b, v1.16b, v2.16b, #15"), UINT32_C(0x6e027820)},
        {S8_INITIALIZER("ext v0.8b, v1.8b, v2.8b, #3"), UINT32_C(0x2e021820)},
        {S8_INITIALIZER("xtn v0.8b, v1.8h"), UINT32_C(0x0e212820)},
        {S8_INITIALIZER("xtn v0.2s, v1.2d"), UINT32_C(0x0ea12820)},
        {S8_INITIALIZER("xtn2 v0.16b, v1.8h"), UINT32_C(0x4e212820)},
        {S8_INITIALIZER("shl v0.4s, v1.4s, #31"), UINT32_C(0x4f3f5420)},
        {S8_INITIALIZER("shl v0.2d, v1.2d, #63"), UINT32_C(0x4f7f5420)},
        {S8_INITIALIZER("shl d0, d1, #3"), UINT32_C(0x5f435420)},
        {S8_INITIALIZER("sshr v0.8h, v1.8h, #16"), UINT32_C(0x4f100420)},
        {S8_INITIALIZER("sshr d0, d1, #64"), UINT32_C(0x5f400420)},
        {S8_INITIALIZER("ushr v0.16b, v1.16b, #1"), UINT32_C(0x6f0f0420)},
        {S8_INITIALIZER("ssra v0.8b, v1.8b, #8"), UINT32_C(0x0f081420)},
        {S8_INITIALIZER("usra v0.2d, v1.2d, #32"), UINT32_C(0x6f601420)},
        {S8_INITIALIZER("ushll v0.4s, v1.4h, #0"), UINT32_C(0x2f10a420)},
        {S8_INITIALIZER("ushll v0.8h, v1.8b, #7"), UINT32_C(0x2f0fa420)},
        {S8_INITIALIZER("sshll v0.2d, v1.2s, #31"), UINT32_C(0x0f3fa420)},
        {S8_INITIALIZER("ushll2 v0.4s, v1.8h, #3"), UINT32_C(0x6f13a420)},
        {S8_INITIALIZER("sshll2 v0.8h, v1.16b, #0"), UINT32_C(0x4f08a420)},
        {S8_INITIALIZER("uxtl v0.8h, v1.8b"), UINT32_C(0x2f08a420)},
        {S8_INITIALIZER("sxtl2 v0.2d, v1.4s"), UINT32_C(0x4f20a420)},
        {S8_INITIALIZER("smlal v0.4s, v1.4h, v2.4h"), UINT32_C(0x0e628020)},
        {S8_INITIALIZER("umull2 v0.2d, v1.4s, v2.4s"), UINT32_C(0x6ea2c020)},
        {S8_INITIALIZER("smull v1.4s, v3.4h, v1.4h"), UINT32_C(0x0e61c061)},
        {S8_INITIALIZER("uaddl v0.2d, v1.2s, v2.2s"), UINT32_C(0x2ea20020)},
        {S8_INITIALIZER("uaddl2 v0.8h, v1.16b, v2.16b"), UINT32_C(0x6e220020)},
        {S8_INITIALIZER("ssubl v0.4s, v1.4h, v2.4h"), UINT32_C(0x0e622020)},
        {S8_INITIALIZER("cmeq v6.4s, v6.4s, #0"), UINT32_C(0x4ea098c6)},
        {S8_INITIALIZER("cmlt v2.8b, v3.8b, #0"), UINT32_C(0x0e20a862)},
        {S8_INITIALIZER("cmgt v2.2d, v3.2d, #0"), UINT32_C(0x4ee08862)},
        {S8_INITIALIZER("cmge v2.8h, v3.8h, #0"), UINT32_C(0x6e608862)},
        {S8_INITIALIZER("cmle v2.4h, v3.4h, #0"), UINT32_C(0x2e609862)},
        {S8_INITIALIZER("ld1 { v0.b }[15], [x8]"), UINT32_C(0x4d401d00)},
        {S8_INITIALIZER("ld1 { v0.h }[5], [sp]"), UINT32_C(0x4d404be0)},
        {S8_INITIALIZER("ld1 { v0.s }[3], [x1]"), UINT32_C(0x4d409020)},
        {S8_INITIALIZER("ld1 { v0.d }[1], [x1]"), UINT32_C(0x4d408420)},
        {S8_INITIALIZER("st1 { v0.s }[2], [x8]"), UINT32_C(0x4d008100)},
        {S8_INITIALIZER("st1 { v0.b }[0], [x8]"), UINT32_C(0x0d000100)},
        {S8_INITIALIZER("ld1 { v1.s }[1], [x2], #4"), UINT32_C(0x0ddf9041)},
        {S8_INITIALIZER("st1 { v2.h }[7], [x3], x4"), UINT32_C(0x4d845862)},
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(cases); index += 1)
    {
        aarch64_base_assembly_test_case(arguments, &result, baseline, cases[index]);
        aarch64_base_assembly_test_case(arguments, &result, apple, cases[index]);
    }

    // LSE and FP16 encode on Apple M1 and report the missing feature on the
    // Armv8.0 baseline.
    static Aarch64BaseAssemblyCase const feature_cases[] = {
        {S8_INITIALIZER("ldadd w0, w1, [x2]"), UINT32_C(0xb8200041)},
        {S8_INITIALIZER("ldaddal x0, x1, [sp]"), UINT32_C(0xf8e003e1)},
        {S8_INITIALIZER("staddlb w0, [x1]"), UINT32_C(0x3860003f)},
        {S8_INITIALIZER("swpal w0, w1, [x2]"), UINT32_C(0xb8e08041)},
        {S8_INITIALIZER("casal x0, x1, [x2]"), UINT32_C(0xc8e0fc41)},
        {S8_INITIALIZER("casb w0, w1, [x2]"), UINT32_C(0x08a07c41)},
        {S8_INITIALIZER("ldumaxh w3, w4, [x5]"), UINT32_C(0x782360a4)},
        {S8_INITIALIZER("stset x0, [x1]"), UINT32_C(0xf820303f)},
        {S8_INITIALIZER("ldclral w0, wzr, [x1]"), UINT32_C(0xb8e0103f)},
        {S8_INITIALIZER("fadd h0, h1, h2"), UINT32_C(0x1ee22820)},
        {S8_INITIALIZER("fmov h0, w1"), UINT32_C(0x1ee70020)},
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(feature_cases); index += 1)
    {
        aarch64_base_assembly_test_case(arguments, &result, apple, feature_cases[index]);
        AssemblyEncodeResult refused = assembly_encode(arguments->arena, feature_cases[index].source, (AssemblyEncodeOptions){.target = baseline});
        BUSTER_TEST_RAW(arguments, refused.diagnostic_count == 1 && !refused.bytes.length &&
            refused.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE, feature_cases[index].source);
    }

    // CASP, LDAPR, LDTR, and STTR exercise the pinned typed memory rows via
    // the public single-instruction and unit entry points. The first 35 words
    // are independent LLVM MC `-show-encoding` witnesses from
    // llvm-project@ca7933e47d3a3451d81e72ac174dcb5aa28b59d1:
    // armv8.1a-lse.s, armv8.3a-rcpc.s, and arm64-memory.s. Additional bare
    // offsets and signed imm9 endpoints are paired against hosted Clang 21's
    // independent source oracle fixture for this bounded LDTR/STTR scope.
    Target lse_rcpc = {
        .cpu_arch = CPU_ARCH_AARCH64,
        .cpu_model = CPU_MODEL_A64_GENERIC,
        .os = OPERATING_SYSTEM_LINUX,
        .cpu_features_explicit = true,
        .cpu_features = target_cpu_features_add(
            target_cpu_features_singleton(TARGET_CPU_FEATURE_AARCH64_LSE), TARGET_CPU_FEATURE_AARCH64_RCPC),
    };
    static Aarch64BaseAssemblyCase const typed_memory_cases[] = {
        {S8_INITIALIZER("casp w0, w1, w2, w3, [x5]"), UINT32_C(0x08207ca2)},
        {S8_INITIALIZER("casp w4, w5, w6, w7, [sp]"), UINT32_C(0x08247fe6)},
        {S8_INITIALIZER("casp x0, x1, x2, x3, [x2]"), UINT32_C(0x48207c42)},
        {S8_INITIALIZER("casp x4, x5, x6, x7, [sp]"), UINT32_C(0x48247fe6)},
        {S8_INITIALIZER("caspa w0, w1, w2, w3, [x5]"), UINT32_C(0x08607ca2)},
        {S8_INITIALIZER("caspa w4, w5, w6, w7, [sp]"), UINT32_C(0x08647fe6)},
        {S8_INITIALIZER("caspa x0, x1, x2, x3, [x2]"), UINT32_C(0x48607c42)},
        {S8_INITIALIZER("caspa x4, x5, x6, x7, [sp]"), UINT32_C(0x48647fe6)},
        {S8_INITIALIZER("caspl w0, w1, w2, w3, [x5]"), UINT32_C(0x0820fca2)},
        {S8_INITIALIZER("caspl w4, w5, w6, w7, [sp]"), UINT32_C(0x0824ffe6)},
        {S8_INITIALIZER("caspl x0, x1, x2, x3, [x2]"), UINT32_C(0x4820fc42)},
        {S8_INITIALIZER("caspl x4, x5, x6, x7, [sp]"), UINT32_C(0x4824ffe6)},
        {S8_INITIALIZER("caspal w0, w1, w2, w3, [x5]"), UINT32_C(0x0860fca2)},
        {S8_INITIALIZER("caspal w4, w5, w6, w7, [sp]"), UINT32_C(0x0864ffe6)},
        {S8_INITIALIZER("caspal x0, x1, x2, x3, [x2]"), UINT32_C(0x4860fc42)},
        {S8_INITIALIZER("caspal x4, x5, x6, x7, [sp]"), UINT32_C(0x4864ffe6)},
        {S8_INITIALIZER("ldaprb w0, [x0]"), UINT32_C(0x38bfc000)},
        {S8_INITIALIZER("ldaprb w0, [x0, #0]"), UINT32_C(0x38bfc000)},
        {S8_INITIALIZER("ldaprh w0, [x17]"), UINT32_C(0x78bfc220)},
        {S8_INITIALIZER("ldapr w18, [x0]"), UINT32_C(0xb8bfc012)},
        {S8_INITIALIZER("ldapr x15, [x0]"), UINT32_C(0xf8bfc00f)},
        {S8_INITIALIZER("ldtr w3, [x4, #16]"), UINT32_C(0xb8410883)},
        {S8_INITIALIZER("ldtr x3, [x4, #16]"), UINT32_C(0xf8410883)},
        {S8_INITIALIZER("ldtrb w3, [x4, #16]"), UINT32_C(0x38410883)},
        {S8_INITIALIZER("ldtrsb w9, [x3]"), UINT32_C(0x38c00869)},
        {S8_INITIALIZER("ldtrsb x2, [sp, #128]"), UINT32_C(0x38880be2)},
        {S8_INITIALIZER("ldtrh w3, [x4, #16]"), UINT32_C(0x78410883)},
        {S8_INITIALIZER("ldtrsh w3, [sp, #32]"), UINT32_C(0x78c20be3)},
        {S8_INITIALIZER("ldtrsh x5, [x9, #24]"), UINT32_C(0x78818925)},
        {S8_INITIALIZER("ldtrsw x9, [sp, #-128]"), UINT32_C(0xb8980be9)},
        {S8_INITIALIZER("sttr w5, [x4, #20]"), UINT32_C(0xb8014885)},
        {S8_INITIALIZER("sttr x4, [x3]"), UINT32_C(0xf8000864)},
        {S8_INITIALIZER("sttrb w4, [x3]"), UINT32_C(0x38000864)},
        {S8_INITIALIZER("sttrh w2, [sp, #32]"), UINT32_C(0x78020be2)},
        // Bare signed offsets are accepted for this bounded LDTR/STTR source cohort.
        // Paired hash/bare spellings intentionally assert the same typed-row word.
        {S8_INITIALIZER("ldtr w3, [x4, 16]"), UINT32_C(0xb8410883)},
        {S8_INITIALIZER("sttr w5, [x4, 20]"), UINT32_C(0xb8014885)},
        {S8_INITIALIZER("ldtr w0, [x1, #-256]"), UINT32_C(0xb8500820)},
        {S8_INITIALIZER("ldtr w0, [x1, -256]"), UINT32_C(0xb8500820)},
        {S8_INITIALIZER("ldtr w0, [x1, #255]"), UINT32_C(0xb84ff820)},
        {S8_INITIALIZER("ldtr w0, [x1, 255]"), UINT32_C(0xb84ff820)},
        {S8_INITIALIZER("sttr x0, [x1, #-256]"), UINT32_C(0xf8100820)},
        {S8_INITIALIZER("sttr x0, [x1, -256]"), UINT32_C(0xf8100820)},
        {S8_INITIALIZER("sttr x0, [x1, #255]"), UINT32_C(0xf80ff820)},
        {S8_INITIALIZER("sttr x0, [x1, 255]"), UINT32_C(0xf80ff820)},
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(typed_memory_cases); index += 1)
    {
        aarch64_base_assembly_test_case(arguments, &result, lse_rcpc, typed_memory_cases[index]);
        aarch64_base_assembly_test_case(arguments, &result, apple, typed_memory_cases[index]);
        if (index >= 21)
        {
            aarch64_base_assembly_test_case(arguments, &result, baseline, typed_memory_cases[index]);
        }
    }
    static String8 const typed_memory_feature_refused[] = {
        S8_INITIALIZER("casp w0, w1, w2, w3, [x5]"),
        S8_INITIALIZER("ldapr x0, [x1]"),
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(typed_memory_feature_refused); index += 1)
    {
        AssemblyEncodeResult refused_memory = assembly_encode(arguments->arena, typed_memory_feature_refused[index],
            (AssemblyEncodeOptions){.target = baseline});
        BUSTER_TEST_RAW(arguments, refused_memory.diagnostic_count == 1 && !refused_memory.bytes.length &&
            refused_memory.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE, typed_memory_feature_refused[index]);
    }
    static String8 const typed_memory_invalid[] = {
        S8_INITIALIZER("casp w0, w1, w2, [x5]"),
        S8_INITIALIZER("casp w0, w1, w2, w3, [x5, #0]"),
        S8_INITIALIZER("casp w0, w1, w1, w2, [x5]"),
        S8_INITIALIZER("casp w0, w2, w4, w5, [x5]"),
        S8_INITIALIZER("casp w1, w2, w4, w5, [x5]"),
        S8_INITIALIZER("casp w0, w1, w5, w6, [x5]"),
        S8_INITIALIZER("casp x1, x2, x4, x5, [x5]"),
        S8_INITIALIZER("casp x0, x1, x5, x6, [x5]"),
        S8_INITIALIZER("casp w0, w1, x2, x3, [x5]"),
        S8_INITIALIZER("casp w0, w1, w2, w3, [w5]"),
        S8_INITIALIZER("casp w0, w1, w2, w3, [x5, #1]"),
        S8_INITIALIZER("ldaprb x0, [x1]"),
        S8_INITIALIZER("ldaprb w0, [x1, #1]"),
        S8_INITIALIZER("ldaprh x0, [x1]"),
        S8_INITIALIZER("ldtrb x0, [x1]"),
        S8_INITIALIZER("ldtr w0, [x1, #-257]"),
        S8_INITIALIZER("ldtr w0, [x1, #256]"),
        S8_INITIALIZER("ldtr w0, [x1, -257]"),
        S8_INITIALIZER("ldtr w0, [x1, 256]"),
        S8_INITIALIZER("sttr x0, [x1, #-257]"),
        S8_INITIALIZER("sttr x0, [x1, #256]"),
        S8_INITIALIZER("sttr x0, [x1, -257]"),
        S8_INITIALIZER("sttr x0, [x1, 256]"),
        S8_INITIALIZER("ldtr w0, [x1, x2]"),
        S8_INITIALIZER("ldtr w0, [x1, #8]!"),
        S8_INITIALIZER("ldtr w0, [w1]"),
        S8_INITIALIZER("sttrb x0, [x1]"),
        S8_INITIALIZER("sttrh w0, [x1, #256]"),
        S8_INITIALIZER("sttr x0, [x1, x2]"),
        S8_INITIALIZER("ldtr w0, [x1,]"),
        S8_INITIALIZER("casp w0, w1, w2, w3, [x5,]"),
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(typed_memory_invalid); index += 1)
    {
        AssemblyEncodeResult refused_memory = assembly_encode(arguments->arena, typed_memory_invalid[index],
            (AssemblyEncodeOptions){.target = lse_rcpc});
        BUSTER_TEST_RAW(arguments, refused_memory.diagnostic_count == 1 && !refused_memory.bytes.length &&
            refused_memory.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS, typed_memory_invalid[index]);
    }

    // Malformed or unencodable operands keep a single operand diagnostic.
    static String8 const refused[] = {
        S8_INITIALIZER("stp x0, x1, [x0, #8]!"),
        S8_INITIALIZER("ldp x0, x0, [x1]"),
        S8_INITIALIZER("ldp x0, x1, [x2, #4]"),
        S8_INITIALIZER("ldp x0, x1, [x2, #512]"),
        S8_INITIALIZER("ldnp x0, x1, [x2, #16]!"),
        S8_INITIALIZER("ldr x0, [x0], #8"),
        S8_INITIALIZER("ldur x0, [x1, #256]"),
        S8_INITIALIZER("ldur x0, [x1], #8"),
        S8_INITIALIZER("ldr x0, [x1, x2, lsl #2]"),
        S8_INITIALIZER("ldrb w0, [x1, x2, lsl #1]"),
        S8_INITIALIZER("ldrsw w0, [x1]"),
        S8_INITIALIZER("lsl w0, w1, #32"),
        S8_INITIALIZER("ubfx w0, w1, #16, #17"),
        S8_INITIALIZER("bfc wsp, #0, #1"),
        S8_INITIALIZER("bfc sp, #0, #1"),
        S8_INITIALIZER("bfc w0, #0, #0"),
        S8_INITIALIZER("bfc w0, #32, #1"),
        S8_INITIALIZER("bfc w0, #31, #2"),
        S8_INITIALIZER("bfc x0, #64, #1"),
        S8_INITIALIZER("bfc x0, #63, #2"),
        S8_INITIALIZER("bfc x0, #0, #65"),
        S8_INITIALIZER("cset w0, al"),
        S8_INITIALIZER("mov x0, sym"),
        S8_INITIALIZER("mov w0, #0x123456789"),
        S8_INITIALIZER("add x0, xzr, #1"),
        S8_INITIALIZER("orr w0, w1, #0"),
        S8_INITIALIZER("fmov d0, #0.1"),
        S8_INITIALIZER("fmov s0, d1"),
        S8_INITIALIZER("mul x0, x1, w2"),
        S8_INITIALIZER("smull w0, w1, w2"),
        S8_INITIALIZER("uxtb x0, w1"),
        S8_INITIALIZER("umov w0, v1.d[0]"),
        S8_INITIALIZER("stlxr w0, x0, [x1]"),
        S8_INITIALIZER("ldr x0, [x1, #010]"),
        // Arrangements and lane indices take only architectural unsigned
        // decimal spellings: no sign, radix, leading zero or wrapping count.
        S8_INITIALIZER("mov v0.16b, v1.-16b"),
        S8_INITIALIZER("mov v0.16b, v1.+16b"),
        S8_INITIALIZER("mov v0.16b, v1.0x10b"),
        S8_INITIALIZER("mov v0.16b, v1.016b"),
        S8_INITIALIZER("mov v0.16b, v1.2305843009213693968b"),
        S8_INITIALIZER("mvn v0.16b, v1.32b"),
        S8_INITIALIZER("umov w0, v1.s[-1]"),
        S8_INITIALIZER("umov w0, v1.s[4]"),
        S8_INITIALIZER("umov w0, v1.s[0x1]"),
        S8_INITIALIZER("ld1 { v0.s }[-1], [x1]"),
        S8_INITIALIZER("ld1 { v0.s }[0x1], [x1]"),
        S8_INITIALIZER("ld1 { v0.-4s }, [x1]"),
        S8_INITIALIZER("ext v0.16b, v1.16b, v2.-16b, #1"),
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(refused); index += 1)
    {
        AssemblyEncodeResult instruction = assembly_encode(arguments->arena, refused[index], (AssemblyEncodeOptions){.target = baseline});
        BUSTER_TEST_RAW(arguments, instruction.diagnostic_count == 1 && !instruction.bytes.length &&
            instruction.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS, refused[index]);
    }

    // LDR (literal) to a unit label folds like the short branches, for GPR and
    // FP/SIMD destinations; undefined or cross-section targets are refused
    // because the object model retains no LD_PREL_LO19 relocation.
    {
        static u8 const literal_expected[] = {
            0x80, 0x00, 0x00, 0x58, 0x61, 0x00, 0x00, 0x5c, 0x42, 0x00, 0x00, 0x98, 0xc0, 0x03, 0x5f, 0xd6,
            0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        };
        static u8 const literal_vector_expected[] = {
            0x60, 0x00, 0x00, 0x9c, 0x43, 0x00, 0x00, 0x1c, 0xc0, 0x03, 0x5f, 0xd6, 0x01, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        };
        AssemblyUnitResult literal = assembly_unit_encode(arguments->arena,
            S8(".text\nldr x0, .Lpool\nldr d1, .Lpool\nldrsw x2, .Lpool\nret\n.Lpool:\n.quad 5\n"), (AssemblyEncodeOptions){.target = baseline});
        BUSTER_TEST(arguments, !literal.diagnostic_count && !literal.relocation_count && literal.section_count == 1 &&
                                   literal.sections[0].data.length == sizeof(literal_expected) &&
                                   memcmp(literal.sections[0].data.pointer, literal_expected, sizeof(literal_expected)) == 0);
        AssemblyUnitResult literal_vector = assembly_unit_encode(arguments->arena,
            S8(".text\nldr q0, 1f\nldr s3, 1f\nret\n1: .quad 1, 2\n"), (AssemblyEncodeOptions){.target = baseline});
        BUSTER_TEST(arguments, !literal_vector.diagnostic_count && !literal_vector.relocation_count && literal_vector.section_count == 1 &&
                                   literal_vector.sections[0].data.length == sizeof(literal_vector_expected) &&
                                   memcmp(literal_vector.sections[0].data.pointer, literal_vector_expected, sizeof(literal_vector_expected)) == 0);
        static String8 const literal_refused[] = {
            S8_INITIALIZER(".text\nldr x0, missing\n"),
            S8_INITIALIZER(".text\nldr x0, other\n.data\nother: .quad 1\n"),
        };
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(literal_refused); index += 1)
        {
            AssemblyUnitResult refused_literal = assembly_unit_encode(arguments->arena, literal_refused[index], (AssemblyEncodeOptions){.target = baseline});
            BUSTER_TEST_RAW(arguments, refused_literal.diagnostic_count == 1, literal_refused[index]);
        }
    }

    // The encoder's own boundary: spellings outside its vocabulary are
    // unknown, and an explicit feature subtraction is reported by name.
    u32 word = 0;
    BUSTER_TEST(arguments, a64_base_assemble(baseline, S8("ldxp"), S8("x0, x1, [x2]"), &word) == A64_BASE_ASSEMBLY_UNKNOWN_MNEMONIC);
    static String8 const malformed_arrangements[] = {
        S8_INITIALIZER("v0.16b, v1.-16b"),
        S8_INITIALIZER("v0.16b, v1.2305843009213693968b"),
        S8_INITIALIZER("v0.-16b, v1.16b"),
        S8_INITIALIZER("w0, v1.s[-1]"),
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(malformed_arrangements); index += 1)
    {
        BUSTER_TEST_RAW(arguments, a64_base_assemble(baseline, S8("mov"), malformed_arrangements[index], &word) == A64_BASE_ASSEMBLY_INVALID_OPERANDS,
                        malformed_arrangements[index]);
    }
    BUSTER_TEST(arguments, a64_base_assemble(baseline, S8("STP"), S8("X29, X30, [SP, #-16]!"), &word) == A64_BASE_ASSEMBLY_OK &&
                               word == UINT32_C(0xa9bf7bfd));
    Target no_fp = baseline;
    no_fp.cpu_features_explicit = true;
    no_fp.cpu_features = target_cpu_features_empty();
    BUSTER_TEST(arguments, a64_base_assemble(no_fp, S8("fadd"), S8("d0, d1, d2"), &word) == A64_BASE_ASSEMBLY_REQUIRES_FP);
    BUSTER_TEST(arguments, a64_base_assemble(no_fp, S8("ldr"), S8("d0, [x1]"), &word) == A64_BASE_ASSEMBLY_REQUIRES_FP);
    BUSTER_TEST(arguments, a64_base_assemble(no_fp, S8("ldp"), S8("x0, x1, [x2]"), &word) == A64_BASE_ASSEMBLY_OK);
    Target no_neon = baseline;
    no_neon.cpu_features_explicit = true;
    no_neon.cpu_features = target_cpu_features_singleton(TARGET_CPU_FEATURE_AARCH64_FP_ARMV8);
    BUSTER_TEST(arguments, a64_base_assemble(no_neon, S8("umov"), S8("w0, v1.b[3]"), &word) == A64_BASE_ASSEMBLY_REQUIRES_NEON);
    BUSTER_TEST(arguments, a64_base_assemble(no_neon, S8("fmov"), S8("d0, x1"), &word) == A64_BASE_ASSEMBLY_OK);
    return result;
}

#endif
