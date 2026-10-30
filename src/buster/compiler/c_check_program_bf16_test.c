// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #136's bf16 vectors in the lowering: c_check_program_vectors.c's rows of their operators
// (check_vector_lower_brain_rows) and c_check_program_vector_builtins.c's of their square roots and fused multiply-adds,
// against clang -O0's code of the same source. In a znver5 unit, whose target hasn't AVX10.2, clang's bfloat16 excess
// precision computes their arithmetic in float and rounds it back by AVX512-BF16's VCVTNEPS2BF16: this program's +, -,
// *, /, negations, splats and compound assignments of random bits (NaNs and subnormals among them, which both codes'
// instructions give alike) at 4, 8, 16, 32 and 64 bytes run where the host has AVX512-BF16 besides the x86 programs'
// features. AVX10.2's square roots and fused multiply-adds, whose negated operands fold into their forms, compile and
// link in a function whose target attribute has the extension, which no host of the tests runs (test-x86-intrinsics
// compares the headers' intrinsics of them with clang's code), and so does a unit whose target has it (x86-64-v4's and
// the extension, against clang's code for diamondrapids), whose arithmetic is AVX10.2's own. Then what the lowering
// refuses by name. Include after c_check_program_fp16_test.c, whose helpers it takes.

static const char check_test_bf16_types[] =
    "typedef __bf16 v2bf __attribute__((vector_size(4)));typedef __bf16 v4bf __attribute__((vector_size(8)));"
    "typedef __bf16 v8bf __attribute__((vector_size(16)));typedef __bf16 v16bf __attribute__((vector_size(32)));"
    "typedef __bf16 v32bf __attribute__((vector_size(64)));typedef short v8hi __attribute__((vector_size(16)));\n";

// The operators at every width, each a statement's one operator (a chain is refused): the five of two vectors and a
// negation, a splat of an element (a scalar's operand on either side) and of a constant, and compound assignments.
static const char check_test_bf16_arithmetic[] =
    "int tested(int n){unsigned h=2166136261u;v2bf a2,b2;v4bf a4,b4;v8bf a8,b8;v16bf a16,b16;v32bf a32,b32;fill(&a2,4,n);fill(&b2,4,n+1);"
    "fill(&a4,8,n+2);fill(&b4,8,n+3);fill(&a8,16,n+4);fill(&b8,16,n+5);fill(&a16,32,n+6);fill(&b16,32,n+7);fill(&a32,64,n+8);fill(&b32,64,n+9);"
    "v2bf r2[]={a2+b2,a2-b2,a2*b2,a2/b2,-a2};FOLD(r2);v4bf r4[]={a4+b4,a4-b4,a4*b4,a4/b4,-a4};FOLD(r4);"
    "v8bf r8[]={a8+b8,a8-b8,a8*b8,a8/b8,-a8};FOLD(r8);v16bf r16[]={a16+b16,a16-b16,a16*b16,a16/b16,-a16};FOLD(r16);"
    "v32bf r32[]={a32+b32,a32-b32,a32*b32,a32/b32,-a32};FOLD(r32);__bf16 s=b8[3];v8bf p8=a8+s,q8=s*a8;FOLD(p8);FOLD(q8);"
    "v32bf p32=a32-b32[5],q32=a32/(__bf16)2.0f;FOLD(p32);FOLD(q32);v2bf u2=a2;u2/=b2;FOLD(u2);v16bf u16=a16;u16+=b16;FOLD(u16);u16/=a16;FOLD(u16);"
    "v32bf u32=a32;u32*=b32;FOLD(u32);u32-=a32;FOLD(u32);return (int)h;}";

// AVX10.2's square roots and fused multiply-adds, each negation of an operand folded into the form (a negation of both
// factors into none), and one in a comma's operand, which isn't.
static const char check_test_bf16_elementwise[] =
    "#define EW(t,a,b,c) {t r[]={__builtin_elementwise_sqrt(a),__builtin_elementwise_fma(a,b,c),__builtin_elementwise_fma(a,b,-c),"
    "__builtin_elementwise_fma(-a,b,c),__builtin_elementwise_fma(a,-b,-c),__builtin_elementwise_fma(-a,-b,c),__builtin_elementwise_fma(a,b,(n,-c))};"
    "FOLD(r);}\n"
    "__attribute__((target(\"avx10.2\"))) int tested(int n){unsigned h=2166136261u;v8bf a8,b8,c8;v16bf a16,b16,c16;v32bf a32,b32,c32;"
    "fill(&a8,16,n);fill(&b8,16,n+1);fill(&c8,16,n+2);fill(&a16,32,n+3);fill(&b16,32,n+4);fill(&c16,32,n+5);fill(&a32,64,n+6);fill(&b32,64,n+7);"
    "fill(&c32,64,n+8);EW(v8bf,a8,b8,c8)EW(v16bf,a16,b16,c16)EW(v32bf,a32,b32,c32)return (int)h;}";

static void check_test_program_bf16_source(CheckTest* test, const char* program, const char* name, const char* clang, const X86Target* target, bool run_native)
{
    const size_t length = strlen(check_test_bf16_types) + strlen(check_test_fp16_helpers) + strlen(program) + 1;
    char* source = malloc(length);
    check_test_expect(test, source != 0, "a bf16 vector program's source");
    if (source != 0)
    {
        snprintf(source, length, "%s%s%s", check_test_bf16_types, check_test_fp16_helpers, program);
        check_test_program_source_native_limits(test, source, name, clang, false, -1, 0, target, run_native);
        free(source);
    }
}

// What the lowering refuses by name in a znver5 function: bf16 vectors' compares, the chains of their arithmetic that
// clang computes in float (check_lower_vector_half_chain), their elementwise builtins without AVX10.2, and the others
// with it; their arithmetic in an x86-64-v4 function, without AVX512-BF16; and beside them what lowers: a negation, a
// chain's links that a store breaks, the halves of a 64-byte vector's quotient, and AVX10.2's folded negations.
static void check_test_program_bf16_refusals(CheckTest* test, const X86Target* target)
{
    static const char chained[] = "chained arithmetic of bf16 vectors, which clang computes in float without AVX10.2";
    static const char elementwise[] = "elementwise vector builtins of bf16 elements without AVX10.2";
    static const struct
    {
        const char* source;
        const char* reason;
        uint32_t initializations;
    } fixtures[] = {
        { "int tested(v8bf* p,v8bf* q){v8hi c=*p<*q;return c[0];}", "vector compares of bf16 elements", 1 },
        { "void tested(v8bf* p,v8bf* q){*p=*p*(*q)+*q;}", chained },
        { "void tested(v32bf* p,v32bf* q){*p=-(*p/(*q));}", chained },
        { "void tested(v16bf* p,v16bf* q){*p-=*q*(*q);}", chained },
        { "void tested(v8bf* p,v8bf* q){*p=*q-(+(*p+*q));}", chained },
        { "void tested(v8bf* p){*p=__builtin_elementwise_sqrt(*p);}", elementwise },
        { "void tested(v8bf* p,v8bf* q){*p=__builtin_elementwise_fma(*p,-*q,*q);}", elementwise },
        { "__attribute__((target(\"avx10.2\"))) void tested(v8bf* p){*p=__builtin_elementwise_abs(*p);}", "elementwise vector builtins of bf16 elements" },
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(fixtures); i += 1)
    {
        const size_t length = strlen(check_test_bf16_types) + strlen(fixtures[i].source) + 1;
        char* source = malloc(length);
        check_test_expect(test, source != 0, "a bf16 vector refusal's source");
        if (source != 0)
        {
            snprintf(source, length, "%s%s", check_test_bf16_types, fixtures[i].source);
            check_test_program_boundary_target(test, source, "bf16-refusal", true, false, 0, fixtures[i].initializations, fixtures[i].reason, target);
            free(source);
        }
    }
    X86Target without = { 0 };
    const bool parsed = x86_target_parse("x86-64-v4", strlen("x86-64-v4"), &without);
    check_test_expect(test, parsed, "the bf16 refusals' target without AVX512-BF16, x86-64-v4");
    char source[512];
    snprintf(source, sizeof(source), "%s%s", check_test_bf16_types, "void tested(v8bf* p,v8bf* q){*p=*p+*q;}");
    if (parsed)
    {
        check_test_program_boundary_target(test, source, "bf16-refusal", true, false, 0, 0, "vector arithmetic of bf16 elements without AVX512-BF16", &without);
    }
    static const char* const accepted[] = {
        "void tested(v8bf* p){*p=-*p;}",
        "void tested(v8bf* p,v8bf* q){v8bf t=*p*(*q);*p=t+*q;}",
        "void tested(v32bf* p,v32bf* q){*p=*p/(*q);}",
        "__attribute__((target(\"avx10.2\"))) void tested(v32bf* p,v32bf* q){*p=__builtin_elementwise_fma(-*p,-*q,*q);}",
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(accepted); i += 1)
    {
        snprintf(source, sizeof(source), "%s%s", check_test_bf16_types, accepted[i]);
        check_test_program_boundary_target(test, source, "bf16-lowered", true, true, 0, i == 1 ? 1 : 0, NULL, target);
    }
}

static __attribute__((noinline)) void check_test_program_bf16(CheckTest* test, const char* clang)
{
    X86Target target = { 0 };
    X86Target unit = { 0 };
    const bool parsed = x86_target_parse("znver5", strlen("znver5"), &target) && x86_target_parse("x86-64-v4", strlen("x86-64-v4"), &unit);
    check_test_expect(test, parsed, "the bf16 vector programs' targets, znver5 and x86-64-v4");
    if (parsed)
    {
        // The unit whose target has AVX10.2: x86-64-v4's with the extension and AVX512-BF16, as clang's diamondrapids has.
        unit.extensions |= BUSTER_X86_EXTENSION(AVX10_2);
        unit.features |= BUSTER_X86_FEATURE(AVX512BF16);
        // The x86 programs' features (check_test_program_fp16's), AVX512-BF16 among them.
        const uint64_t required = BUSTER_X86_FEATURE(AVX512F) | BUSTER_X86_FEATURE(AVX512BW) | BUSTER_X86_FEATURE(AVX512CD) |
                                  BUSTER_X86_FEATURE(AVX512DQ) | BUSTER_X86_FEATURE(AVX512VL) | BUSTER_X86_FEATURE(AVX512VBMI) |
                                  BUSTER_X86_FEATURE(AVX512VBMI2) | BUSTER_X86_FEATURE(AVX512BITALG) | BUSTER_X86_FEATURE(AVX512VNNI) |
                                  BUSTER_X86_FEATURE(GFNI) | BUSTER_X86_FEATURE(VAES) | BUSTER_X86_FEATURE(AVX2) | BUSTER_X86_FEATURE(BMI2) |
                                  BUSTER_X86_FEATURE(AVX512IFMA) | BUSTER_X86_FEATURE(AVX512BF16) | BUSTER_X86_FEATURE(AES) | BUSTER_X86_FEATURE(FMA);
        const X86Target native = x86_target_native();
        const bool run_native = (native.features & required) == required;
        if (!run_native)
        {
            printf("  bf16 vectors: native execution skipped (the host lacks AVX512-BF16 or the x86 programs' features); the programs compile and link\n");
        }
        check_test_program_build_release();
        check_test_program_march = "-march=znver5";
        check_test_program_bf16_source(test, check_test_bf16_arithmetic, "x86-bf16-arithmetic", clang, &target, run_native);
        check_test_program_bf16_source(test, check_test_bf16_elementwise, "x86-bf16-avx10", clang, &target, false);
        check_test_program_build_release();
        check_test_program_bf16_refusals(test, &target);
        check_test_program_march = "-march=diamondrapids";
        check_test_program_bf16_source(test, check_test_bf16_arithmetic, "x86-bf16-unit", clang, &unit, false);
        check_test_program_build_release();
        check_test_program_march = "-march=x86-64";
    }
}
