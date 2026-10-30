// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #148's rest of clang's x86 builtins (c_builtin_supplementary_scope.inc) in the lowering, as c_check_program_x86_test.c
// tests the others: each family's builtins called directly at their prototypes' types against clang -O0's code of the same
// source for znver5, at their immediates' edges, every result folded into the program's hash, run where the host has the
// features clang's code takes and compiled and linked elsewhere. Where a program passes an immediate outside its range,
// clang's -Wargument-outside-range is turned off by its pragma, as code that clang's code generation still takes: its
// code masks the immediate as the instruction's field reads it (an index's low bits, a predicate's), and so does ours.
// Include after c_check_program_x86_test.c, whose types, helpers and harness it takes.

static void check_test_program_x86_rest_sources(CheckTest* test, const char* clang, const X86Target* target, bool run_native)
{
    static const CheckTestX86Program programs[] = {
        {
            // The unsigned compares of quadwords and words at 16 and 32 bytes, by every predicate under masks of none, one
            // lane, others and all, the second operands sharing some elements with the first; then a predicate past 7,
            // which clang's code and the instruction take modulo 8.
            "x86-rest-unsigned-compares",
            "#pragma clang diagnostic ignored \"-Wargument-outside-range\"\n"
            "#define UC(P) {unsigned char k[]={__builtin_ia32_ucmpq128_mask(a,b,P,0xff),__builtin_ia32_ucmpq128_mask(a,b,P,(unsigned char)m),"
            "__builtin_ia32_ucmpq256_mask(c,d,P,0xff),__builtin_ia32_ucmpq256_mask(c,d,P,1),__builtin_ia32_ucmpw128_mask(e,f,P,(unsigned char)(m>>8)),"
            "__builtin_ia32_ucmpw128_mask(e,f,P,0)};unsigned short w[]={__builtin_ia32_ucmpw256_mask(g,i,P,0xffff),"
            "__builtin_ia32_ucmpw256_mask(g,i,P,(unsigned short)(m>>16))};FOLD(k);FOLD(w);}\n"
            "int tested(int n){unsigned h=2166136261u;v2di a,b;v4di c,d;v8hi e,f;v16hi g,i;fill(&a,16,n);fill(&b,16,n+1);fill(&c,32,n+2);"
            "fill(&d,32,n+3);fill(&e,16,n+4);fill(&f,16,n+5);fill(&g,32,n+6);fill(&i,32,n+7);b[1]=a[1];d[2]=c[2];for(int k=0;k<8;k+=3)f[k]=e[k];"
            "for(int k=0;k<16;k+=5)i[k]=g[k];unsigned long long m=mask(n);UC(0)UC(1)UC(2)UC(3)UC(4)UC(5)UC(6)UC(7)UC(9)UC(15)return (int)h;}",
        },
        {
            // A quadword's extract and the insertions of a byte and a quadword at every index, each index's edges, and
            // past them, where clang's code takes the index's low bits.
            "x86-rest-elements",
            "#pragma clang diagnostic ignored \"-Wargument-outside-range\"\n"
            "int tested(int n){unsigned h=2166136261u;v2di a;v16qi b;fill(&a,16,n);fill(&b,16,n+1);"
            "long long e[]={__builtin_ia32_vec_ext_v2di(a,0),__builtin_ia32_vec_ext_v2di(a,1),__builtin_ia32_vec_ext_v2di(a,2),"
            "__builtin_ia32_vec_ext_v2di(a,-1)};FOLD(e);char c=(char)(n*37+5);"
            "v16qi s[]={__builtin_ia32_vec_set_v16qi(b,c,0),__builtin_ia32_vec_set_v16qi(b,c,1),__builtin_ia32_vec_set_v16qi(b,c,7),"
            "__builtin_ia32_vec_set_v16qi(b,c,8),__builtin_ia32_vec_set_v16qi(b,(char)-1,14),__builtin_ia32_vec_set_v16qi(b,c,15),"
            "__builtin_ia32_vec_set_v16qi(b,c,16),__builtin_ia32_vec_set_v16qi(b,c,-1)};FOLD(s);long long q=(long long)n*0x123456789ll-7;"
            "v2di t[]={__builtin_ia32_vec_set_v2di(a,q,0),__builtin_ia32_vec_set_v2di(a,q,1),__builtin_ia32_vec_set_v2di(a,-1,1),"
            "__builtin_ia32_vec_set_v2di(a,q,2),__builtin_ia32_vec_set_v2di(a,q,3)};FOLD(t);return (int)h;}",
        },
        {
            // The rotates of doublewords and quadwords left and right at each width, by counts around the elements' bits
            // and the immediate's whole byte, and past it.
            "x86-rest-rotates",
            "#pragma clang diagnostic ignored \"-Wargument-outside-range\"\n"
            "#define ROT(name,T,v) {T r[]={__builtin_ia32_##name(v,0),__builtin_ia32_##name(v,1),__builtin_ia32_##name(v,7),__builtin_ia32_##name(v,31),"
            "__builtin_ia32_##name(v,32),__builtin_ia32_##name(v,33),__builtin_ia32_##name(v,63),__builtin_ia32_##name(v,64),__builtin_ia32_##name(v,65),"
            "__builtin_ia32_##name(v,200),__builtin_ia32_##name(v,255),__builtin_ia32_##name(v,256),__builtin_ia32_##name(v,-1)};FOLD(r);}\n"
            "int tested(int n){unsigned h=2166136261u;v4si a;v8si b;v16si c;v2di d;v4di e;v8di f;fill(&a,16,n);fill(&b,32,n+1);fill(&c,64,n+2);"
            "fill(&d,16,n+3);fill(&e,32,n+4);fill(&f,64,n+5);ROT(prold128,v4si,a)ROT(prold256,v8si,b)ROT(prold512,v16si,c)ROT(prord128,v4si,a)"
            "ROT(prord256,v8si,b)ROT(prord512,v16si,c)ROT(prolq128,v2di,d)ROT(prolq256,v4di,e)ROT(prolq512,v8di,f)ROT(prorq128,v2di,d)"
            "ROT(prorq256,v4di,e)ROT(prorq512,v8di,f)return (int)h;}",
        },
    };
    const X86Target native = x86_target_native();
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(programs); i += 1)
    {
        const bool run = run_native && !programs[i].link_only && (native.features & programs[i].features) == programs[i].features;
        if (run_native && !programs[i].link_only && !run)
        {
            printf("  %s: native execution skipped (the host lacks features clang's code of it takes); the program compiles and links\n",
                   programs[i].name);
        }
        check_test_program_x86_source(test, clang, target, run, programs[i].name, programs[i].body);
    }
}

// The rest's programs at znver5's target, where the host runs what its features allow (check_test_program_x86's rule).
static __attribute__((noinline)) void check_test_program_x86_rest(CheckTest* test, const char* clang)
{
    X86Target target = { 0 };
    const bool parsed = x86_target_parse("znver5", strlen("znver5"), &target);
    check_test_expect(test, parsed, "the rest of the x86 builtins' programs' target, znver5");
    if (parsed)
    {
        const uint64_t required = BUSTER_X86_FEATURE(AVX512F) | BUSTER_X86_FEATURE(AVX512BW) | BUSTER_X86_FEATURE(AVX512DQ) |
                                  BUSTER_X86_FEATURE(AVX512VL) | BUSTER_X86_FEATURE(AVX2) | BUSTER_X86_FEATURE(BMI2);
        const X86Target native = x86_target_native();
        const bool run_native = (native.features & required) == required;
        if (!run_native)
        {
            printf("  the rest of the x86 builtins: native execution skipped (the host lacks their features or their saved state); the programs compile and "
                   "link\n");
        }
        check_test_program_build_release();
        check_test_program_march = "-march=znver5";
        check_test_program_x86_rest_sources(test, clang, &target, run_native);
        check_test_program_build_release();
        check_test_program_march = "-march=x86-64";
    }
}
