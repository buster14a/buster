// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #136's AVX512-FP16 in the lowering: c_check_program_x86.c's rules of its builtins, and c_check_program_vectors.c's
// and c_check_program_vector_builtins.c's rows of half-precision elements, against clang -O0's code of the same source:
// a function of a znver5 unit whose target attribute adds avx512fp16, as clang's headers' intrinsics are, with each
// family's builtins called directly at their prototypes' types, and the vectors' operators, elementwise builtins and
// conversions, every result folded into the program's hash. Its halves are random bits where one instruction computes
// a result (NaNs among them, which both codes' instructions give alike), and modest values where the forms clang
// chooses could choose among NaNs (the FMAs, the complex products, two operands' arithmetic): finite, of exponents
// from -2 to 2 (modest), or the 16-bit integers whose halves are exact (small). The programs run where the host has
// AVX512-FP16 and its saved state (x86_target_native's extensions) besides the x86 programs' features; elsewhere, the
// 9700X's Zen 5 among them, they compile and link. Then what the lowering refuses without the extension. Include after
// c_check_program_vector_builtins_test.c.

// The vector types, which the refusals take alone, and the programs' helpers after them.
static const char check_test_fp16_types[] =
    "typedef _Float16 v8hf __attribute__((vector_size(16)));typedef _Float16 v16hf __attribute__((vector_size(32)));"
    "typedef _Float16 v32hf __attribute__((vector_size(64)));typedef short v8hi __attribute__((vector_size(16)));"
    "typedef short v16hi __attribute__((vector_size(32)));typedef short v32hi __attribute__((vector_size(64)));"
    "typedef unsigned short v8hu __attribute__((vector_size(16)));typedef unsigned short v16hu __attribute__((vector_size(32)));"
    "typedef unsigned short v32hu __attribute__((vector_size(64)));typedef int v4si __attribute__((vector_size(16)));"
    "typedef int v8si __attribute__((vector_size(32)));typedef int v16si __attribute__((vector_size(64)));"
    "typedef unsigned v4su __attribute__((vector_size(16)));typedef unsigned v8su __attribute__((vector_size(32)));"
    "typedef unsigned v16su __attribute__((vector_size(64)));typedef long long v2di __attribute__((vector_size(16)));"
    "typedef long long v4di __attribute__((vector_size(32)));typedef long long v8di __attribute__((vector_size(64)));"
    "typedef unsigned long long v2du __attribute__((vector_size(16)));typedef unsigned long long v4du __attribute__((vector_size(32)));"
    "typedef unsigned long long v8du __attribute__((vector_size(64)));typedef float v4sf __attribute__((vector_size(16)));"
    "typedef float v8sf __attribute__((vector_size(32)));typedef float v16sf __attribute__((vector_size(64)));"
    "typedef double v2df __attribute__((vector_size(16)));typedef double v4df __attribute__((vector_size(32)));"
    "typedef double v8df __attribute__((vector_size(64)));\n";
static const char check_test_fp16_helpers[] =
    "static unsigned fold(unsigned h,const void*p,unsigned long n){const unsigned char*b=p;for(unsigned long i=0;i<n;i++)h=(h^b[i])*16777619u;return h;}\n"
    "#define FOLD(v) h=fold(h,&(v),sizeof(v))\n"
    "static void fill(void*p,unsigned long n,unsigned s){unsigned char*b=p;for(unsigned long i=0;i<n;i++){s=s*1103515245u+12345u;b[i]=(unsigned char)(s>>16);}}\n"
    "static unsigned long long mask(int n){return (unsigned long long)((unsigned)n*2654435761u)<<32|((unsigned)n*40503u+5u);}\n"
    "static void modest(void*p,unsigned long n,unsigned s){unsigned short*b=p;for(unsigned long i=0;i<n;i++){s=s*1103515245u+12345u;"
    "b[i]=(unsigned short)((s>>31)<<15|(13u+(s>>16)%5u)<<10|((s>>4)&0x3ffu));}}\n"
    "static void small(void*p,unsigned long n,unsigned s,int positive){short*b=p;for(unsigned long i=0;i<n;i++){s=s*1103515245u+12345u;"
    "b[i]=(short)(positive?16+(int)((s>>16)%985u):(int)((s>>16)%2001u)-1000);}}\n";

static void check_test_program_fp16_sources(CheckTest* test, const char* clang, const X86Target* target, bool run_native)
{
    static const char* const programs[][2] = {
        {
            // Its rows under a mask of one vector: the exponents, reciprocals and square roots' reciprocals of random
            // bits at each width, and the conversions to and from floats, doubles and 16-, 32- and 64-bit integers by
            // each rounding control, of random bits (NaNs and values past the integers' ranges among them, which the
            // instructions give alike).
            "x86-fp16-unary",
            "__attribute__((target(\"avx512fp16\"))) int tested(int n){unsigned h=2166136261u;unsigned long long m=mask(n);v8hf a8,w8;v16hf a16,w16;v32hf a32,"
            "w32;fill(&a8,16,n);fill(&w8,16,n+1);fill(&a16,32,n+2);fill(&w16,32,n+3);fill(&a32,64,n+4);fill(&w32,64,n+5);"
            "v8hf g0=__builtin_ia32_getexpph128_mask(a8,w8,(unsigned char)m);v16hf g1=__builtin_ia32_getexpph256_mask(a16,w16,(unsigned short)(m>>3));"
            "v32hf g2=__builtin_ia32_getexpph512_mask(a32,w32,(unsigned)(m>>5),4),g3=__builtin_ia32_getexpph512_mask(a32,w32,(unsigned)(m>>7),8);FOLD(g0);"
            "FOLD(g1);FOLD(g2);FOLD(g3);v8hf c0=__builtin_ia32_rcpph128_mask(a8,w8,(unsigned char)(m>>9));v16hf c1=__builtin_ia32_rcpph256_mask(a16,w16,"
            "(unsigned short)(m>>11));v32hf c2=__builtin_ia32_rcpph512_mask(a32,w32,(unsigned)(m>>13));FOLD(c0);FOLD(c1);FOLD(c2);"
            "v8hf q0=__builtin_ia32_rsqrtph128_mask(a8,w8,(unsigned char)(m>>15));v16hf q1=__builtin_ia32_rsqrtph256_mask(a16,w16,(unsigned short)(m>>17));"
            "v32hf q2=__builtin_ia32_rsqrtph512_mask(a32,w32,(unsigned)(m>>19));FOLD(q0);FOLD(q1);FOLD(q2);v4sf f4;v8sf f8;v16sf f16;fill(&f4,16,n+6);fill(&f8,"
            "32,n+7);fill(&f16,64,n+8);v4sf x0=__builtin_ia32_vcvtph2psx128_mask(a8,f4,(unsigned char)(m>>21));v8sf x1=__builtin_ia32_vcvtph2psx256_mask(a8,f8,"
            "(unsigned char)(m>>23));v16sf x2=__builtin_ia32_vcvtph2psx512_mask(a16,f16,(unsigned short)(m>>25),4),x3=__builtin_ia32_vcvtph2psx512_mask(a16,"
            "f16,(unsigned short)(m>>27),8);FOLD(x0);FOLD(x1);FOLD(x2);FOLD(x3);v2df d2;v4df d4;v8df d8;fill(&d2,16,n+9);fill(&d4,32,n+10);fill(&d8,64,n+11);"
            "v2df y0=__builtin_ia32_vcvtph2pd128_mask(a8,d2,(unsigned char)(m>>29));v4df y1=__builtin_ia32_vcvtph2pd256_mask(a8,d4,(unsigned char)(m>>31));"
            "v8df y2=__builtin_ia32_vcvtph2pd512_mask(a8,d8,(unsigned char)(m>>33),12);FOLD(y0);FOLD(y1);FOLD(y2);v4si i4;v8si i8;v16si i16;v4su u4;v8su u8;"
            "v16su u16;fill(&i4,16,n+12);fill(&i8,32,n+13);fill(&i16,64,n+14);fill(&u4,16,n+15);fill(&u8,32,n+16);fill(&u16,64,n+17);"
            "v4si vcvtph2dq0=__builtin_ia32_vcvtph2dq128_mask(a8,i4,(unsigned char)(m>>2));v8si vcvtph2dq1=__builtin_ia32_vcvtph2dq256_mask(a8,i8,"
            "(unsigned char)(m>>6));v16si vcvtph2dq2=__builtin_ia32_vcvtph2dq512_mask(a16,i16,(unsigned short)(m>>10),4),"
            "vcvtph2dq3=__builtin_ia32_vcvtph2dq512_mask(a16,i16,(unsigned short)(m>>14),9);FOLD(vcvtph2dq0);FOLD(vcvtph2dq1);FOLD(vcvtph2dq2);"
            "FOLD(vcvtph2dq3);v4su vcvtph2udq0=__builtin_ia32_vcvtph2udq128_mask(a8,u4,(unsigned char)(m>>2));"
            "v8su vcvtph2udq1=__builtin_ia32_vcvtph2udq256_mask(a8,u8,(unsigned char)(m>>6));v16su vcvtph2udq2=__builtin_ia32_vcvtph2udq512_mask(a16,u16,"
            "(unsigned short)(m>>10),10),vcvtph2udq3=__builtin_ia32_vcvtph2udq512_mask(a16,u16,(unsigned short)(m>>14),11);FOLD(vcvtph2udq0);FOLD(vcvtph2udq1);"
            "FOLD(vcvtph2udq2);FOLD(vcvtph2udq3);v4si vcvttph2dq0=__builtin_ia32_vcvttph2dq128_mask(a8,i4,(unsigned char)(m>>2));"
            "v8si vcvttph2dq1=__builtin_ia32_vcvttph2dq256_mask(a8,i8,(unsigned char)(m>>6));v16si vcvttph2dq2=__builtin_ia32_vcvttph2dq512_mask(a16,i16,"
            "(unsigned short)(m>>10),4),vcvttph2dq3=__builtin_ia32_vcvttph2dq512_mask(a16,i16,(unsigned short)(m>>14),8);FOLD(vcvttph2dq0);FOLD(vcvttph2dq1);"
            "FOLD(vcvttph2dq2);FOLD(vcvttph2dq3);v4su vcvttph2udq0=__builtin_ia32_vcvttph2udq128_mask(a8,u4,(unsigned char)(m>>2));"
            "v8su vcvttph2udq1=__builtin_ia32_vcvttph2udq256_mask(a8,u8,(unsigned char)(m>>6));v16su vcvttph2udq2=__builtin_ia32_vcvttph2udq512_mask(a16,u16,"
            "(unsigned short)(m>>10),12),vcvttph2udq3=__builtin_ia32_vcvttph2udq512_mask(a16,u16,(unsigned short)(m>>14),4);FOLD(vcvttph2udq0);"
            "FOLD(vcvttph2udq1);FOLD(vcvttph2udq2);FOLD(vcvttph2udq3);v2di l2;v4di l4;v8di l8;v2du k2;v4du k4;v8du k8;fill(&l2,16,n+18);fill(&l4,32,n+19);"
            "fill(&l8,64,n+20);fill(&k2,16,n+21);fill(&k4,32,n+22);fill(&k8,64,n+23);v2di vcvtph2qq0=__builtin_ia32_vcvtph2qq128_mask(a8,l2,"
            "(unsigned char)(m>>3));v4di vcvtph2qq1=__builtin_ia32_vcvtph2qq256_mask(a8,l4,(unsigned char)(m>>7));"
            "v8di vcvtph2qq2=__builtin_ia32_vcvtph2qq512_mask(a8,l8,(unsigned char)(m>>11),4),vcvtph2qq3=__builtin_ia32_vcvtph2qq512_mask(a8,l8,"
            "(unsigned char)(m>>15),8);FOLD(vcvtph2qq0);FOLD(vcvtph2qq1);FOLD(vcvtph2qq2);FOLD(vcvtph2qq3);"
            "v2du vcvtph2uqq0=__builtin_ia32_vcvtph2uqq128_mask(a8,k2,(unsigned char)(m>>3));v4du vcvtph2uqq1=__builtin_ia32_vcvtph2uqq256_mask(a8,k4,"
            "(unsigned char)(m>>7));v8du vcvtph2uqq2=__builtin_ia32_vcvtph2uqq512_mask(a8,k8,(unsigned char)(m>>11),9),"
            "vcvtph2uqq3=__builtin_ia32_vcvtph2uqq512_mask(a8,k8,(unsigned char)(m>>15),11);FOLD(vcvtph2uqq0);FOLD(vcvtph2uqq1);FOLD(vcvtph2uqq2);"
            "FOLD(vcvtph2uqq3);v2di vcvttph2qq0=__builtin_ia32_vcvttph2qq128_mask(a8,l2,(unsigned char)(m>>3));"
            "v4di vcvttph2qq1=__builtin_ia32_vcvttph2qq256_mask(a8,l4,(unsigned char)(m>>7));v8di vcvttph2qq2=__builtin_ia32_vcvttph2qq512_mask(a8,l8,"
            "(unsigned char)(m>>11),8),vcvttph2qq3=__builtin_ia32_vcvttph2qq512_mask(a8,l8,(unsigned char)(m>>15),4);FOLD(vcvttph2qq0);FOLD(vcvttph2qq1);"
            "FOLD(vcvttph2qq2);FOLD(vcvttph2qq3);v2du vcvttph2uqq0=__builtin_ia32_vcvttph2uqq128_mask(a8,k2,(unsigned char)(m>>3));"
            "v4du vcvttph2uqq1=__builtin_ia32_vcvttph2uqq256_mask(a8,k4,(unsigned char)(m>>7));v8du vcvttph2uqq2=__builtin_ia32_vcvttph2uqq512_mask(a8,k8,"
            "(unsigned char)(m>>11),4),vcvttph2uqq3=__builtin_ia32_vcvttph2uqq512_mask(a8,k8,(unsigned char)(m>>15),12);FOLD(vcvttph2uqq0);FOLD(vcvttph2uqq1);"
            "FOLD(vcvttph2uqq2);FOLD(vcvttph2uqq3);v8hi s8;v16hi s16;v32hi s32;v8hu o8;v16hu o16;v32hu o32;fill(&s8,16,n+24);fill(&s16,32,n+25);fill(&s32,64,"
            "n+26);fill(&o8,16,n+27);fill(&o16,32,n+28);fill(&o32,64,n+29);v8hi vcvtph2w0=__builtin_ia32_vcvtph2w128_mask(a8,s8,(unsigned char)(m>>4));"
            "v16hi vcvtph2w1=__builtin_ia32_vcvtph2w256_mask(a16,s16,(unsigned short)(m>>8));v32hi vcvtph2w2=__builtin_ia32_vcvtph2w512_mask(a32,s32,"
            "(unsigned)(m>>12),4),vcvtph2w3=__builtin_ia32_vcvtph2w512_mask(a32,s32,(unsigned)(m>>16),10);FOLD(vcvtph2w0);FOLD(vcvtph2w1);FOLD(vcvtph2w2);"
            "FOLD(vcvtph2w3);v8hu vcvtph2uw0=__builtin_ia32_vcvtph2uw128_mask(a8,o8,(unsigned char)(m>>4));"
            "v16hu vcvtph2uw1=__builtin_ia32_vcvtph2uw256_mask(a16,o16,(unsigned short)(m>>8));v32hu vcvtph2uw2=__builtin_ia32_vcvtph2uw512_mask(a32,o32,"
            "(unsigned)(m>>12),8),vcvtph2uw3=__builtin_ia32_vcvtph2uw512_mask(a32,o32,(unsigned)(m>>16),11);FOLD(vcvtph2uw0);FOLD(vcvtph2uw1);FOLD(vcvtph2uw2);"
            "FOLD(vcvtph2uw3);v8hi vcvttph2w0=__builtin_ia32_vcvttph2w128_mask(a8,s8,(unsigned char)(m>>4));"
            "v16hi vcvttph2w1=__builtin_ia32_vcvttph2w256_mask(a16,s16,(unsigned short)(m>>8));v32hi vcvttph2w2=__builtin_ia32_vcvttph2w512_mask(a32,s32,"
            "(unsigned)(m>>12),8),vcvttph2w3=__builtin_ia32_vcvttph2w512_mask(a32,s32,(unsigned)(m>>16),4);FOLD(vcvttph2w0);FOLD(vcvttph2w1);FOLD(vcvttph2w2);"
            "FOLD(vcvttph2w3);v8hu vcvttph2uw0=__builtin_ia32_vcvttph2uw128_mask(a8,o8,(unsigned char)(m>>4));"
            "v16hu vcvttph2uw1=__builtin_ia32_vcvttph2uw256_mask(a16,o16,(unsigned short)(m>>8));v32hu vcvttph2uw2=__builtin_ia32_vcvttph2uw512_mask(a32,o32,"
            "(unsigned)(m>>12),12),vcvttph2uw3=__builtin_ia32_vcvttph2uw512_mask(a32,o32,(unsigned)(m>>16),4);FOLD(vcvttph2uw0);FOLD(vcvttph2uw1);"
            "FOLD(vcvttph2uw2);FOLD(vcvttph2uw3);v8hf p0=__builtin_ia32_vcvtps2phx128_mask(f4,w8,(unsigned char)(m>>20)),"
            "p1=__builtin_ia32_vcvtps2phx256_mask(f8,w8,(unsigned char)(m>>24));v16hf p2=__builtin_ia32_vcvtps2phx512_mask(f16,w16,(unsigned short)(m>>28),4),"
            "p3=__builtin_ia32_vcvtps2phx512_mask(f16,w16,(unsigned short)(m>>30),9);FOLD(p0);FOLD(p1);FOLD(p2);FOLD(p3);"
            "v8hf e0=__builtin_ia32_vcvtpd2ph128_mask(d2,w8,(unsigned char)(m>>32)),e1=__builtin_ia32_vcvtpd2ph256_mask(d4,w8,(unsigned char)(m>>34)),"
            "e2=__builtin_ia32_vcvtpd2ph512_mask(d8,w8,(unsigned char)(m>>36),4),e3=__builtin_ia32_vcvtpd2ph512_mask(d8,w8,(unsigned char)(m>>38),10);FOLD(e0);"
            "FOLD(e1);FOLD(e2);FOLD(e3);v8hf vcvtqq2ph0=__builtin_ia32_vcvtqq2ph128_mask(l2,w8,(unsigned char)(m>>40)),"
            "vcvtqq2ph1=__builtin_ia32_vcvtqq2ph256_mask(l4,w8,(unsigned char)(m>>42)),vcvtqq2ph2=__builtin_ia32_vcvtqq2ph512_mask(l8,w8,"
            "(unsigned char)(m>>44),4),vcvtqq2ph3=__builtin_ia32_vcvtqq2ph512_mask(l8,w8,(unsigned char)(m>>46),11);FOLD(vcvtqq2ph0);FOLD(vcvtqq2ph1);"
            "FOLD(vcvtqq2ph2);FOLD(vcvtqq2ph3);v8hf vcvtuqq2ph0=__builtin_ia32_vcvtuqq2ph128_mask(k2,w8,(unsigned char)(m>>40)),"
            "vcvtuqq2ph1=__builtin_ia32_vcvtuqq2ph256_mask(k4,w8,(unsigned char)(m>>42)),vcvtuqq2ph2=__builtin_ia32_vcvtuqq2ph512_mask(k8,w8,"
            "(unsigned char)(m>>44),8),vcvtuqq2ph3=__builtin_ia32_vcvtuqq2ph512_mask(k8,w8,(unsigned char)(m>>46),9);FOLD(vcvtuqq2ph0);FOLD(vcvtuqq2ph1);"
            "FOLD(vcvtuqq2ph2);FOLD(vcvtuqq2ph3);v8hf t0=__builtin_ia32_vcvtdq2ph128_mask(i4,w8,(unsigned char)(m>>48)),"
            "t1=__builtin_ia32_vcvtudq2ph128_mask(u4,w8,(unsigned char)(m>>50));v16hf t2=__builtin_ia32_vcvtdq2ph512_mask(i16,w16,(unsigned short)(m>>52),4),"
            "t3=__builtin_ia32_vcvtdq2ph512_mask(i16,w16,(unsigned short)(m>>9),10),t4=__builtin_ia32_vcvtudq2ph512_mask(u16,w16,(unsigned short)(m>>13),11);"
            "FOLD(t0);FOLD(t1);FOLD(t2);FOLD(t3);FOLD(t4);v32hf z0=__builtin_ia32_vcvtw2ph512_mask(s32,w32,(unsigned)(m>>17),4),"
            "z1=__builtin_ia32_vcvtw2ph512_mask(s32,w32,(unsigned)(m>>21),9),z2=__builtin_ia32_vcvtuw2ph512_mask(o32,w32,(unsigned)(m>>25),8);FOLD(z0);"
            "FOLD(z1);FOLD(z2);return (int)h;}",
        },
        {
            // Two vectors' operations merged into w under a mask, of modest values: the scale at each width, and the
            // scalars' (the reciprocals, exponents, maximums and minimums, scales and square roots, and the conversions
            // between the first elements of halves, floats and doubles), by each rounding control or the exceptions'
            // suppression, the other elements a's.
            "x86-fp16-merges",
            "__attribute__((target(\"avx512fp16\"))) int tested(int n){unsigned h=2166136261u;unsigned long long m=mask(n);v8hf a8,b8,w8;v16hf a16,b16,w16;"
            "v32hf a32,b32,w32;modest(&a8,8,n);modest(&b8,8,n+1);fill(&w8,16,n+2);modest(&a16,16,n+3);modest(&b16,16,n+4);fill(&w16,32,n+5);modest(&a32,32,"
            "n+6);modest(&b32,32,n+7);fill(&w32,64,n+8);v8hf s0=__builtin_ia32_scalefph128_mask(a8,b8,w8,(unsigned char)m);"
            "v16hf s1=__builtin_ia32_scalefph256_mask(a16,b16,w16,(unsigned short)(m>>4));v32hf s2=__builtin_ia32_scalefph512_mask(a32,b32,w32,"
            "(unsigned)(m>>8),4),s3=__builtin_ia32_scalefph512_mask(a32,b32,w32,(unsigned)(m>>12),11);FOLD(s0);FOLD(s1);FOLD(s2);FOLD(s3);"
            "v8hf r0=__builtin_ia32_rcpsh_mask(a8,b8,w8,(unsigned char)(m>>16)),r1=__builtin_ia32_rsqrtsh_mask(a8,b8,w8,(unsigned char)(m>>17));FOLD(r0);"
            "FOLD(r1);v8hf g0=__builtin_ia32_getexpsh128_round_mask(a8,b8,w8,(unsigned char)(m>>18),4),g1=__builtin_ia32_getexpsh128_round_mask(a8,b8,w8,"
            "(unsigned char)(m>>19),8);v8hf x0=__builtin_ia32_maxsh_round_mask(a8,b8,w8,(unsigned char)(m>>20),4),x1=__builtin_ia32_maxsh_round_mask(a8,b8,w8,"
            "(unsigned char)(m>>21),8);v8hf y0=__builtin_ia32_minsh_round_mask(a8,b8,w8,(unsigned char)(m>>22),4),y1=__builtin_ia32_minsh_round_mask(a8,b8,w8,"
            "(unsigned char)(m>>23),12);FOLD(g0);FOLD(g1);FOLD(x0);FOLD(x1);FOLD(y0);FOLD(y1);v8hf c0=__builtin_ia32_scalefsh_round_mask(a8,b8,w8,"
            "(unsigned char)(m>>24),4),c1=__builtin_ia32_scalefsh_round_mask(a8,b8,w8,(unsigned char)(m>>25),9);v8hf q0=__builtin_ia32_sqrtsh_round_mask(a8,b8,"
            "w8,(unsigned char)(m>>26),4),q1=__builtin_ia32_sqrtsh_round_mask(a8,a8,w8,(unsigned char)(m>>27),10);FOLD(c0);FOLD(c1);FOLD(q0);FOLD(q1);v2df d,e;"
            "v4sf f,g;fill(&d,16,n+9);fill(&e,16,n+10);fill(&f,16,n+11);fill(&g,16,n+12);v8hf t0=__builtin_ia32_vcvtsd2sh_round_mask(a8,d,w8,"
            "(unsigned char)(m>>28),4),t1=__builtin_ia32_vcvtsd2sh_round_mask(a8,d,w8,(unsigned char)(m>>29),11);v2df t2=__builtin_ia32_vcvtsh2sd_round_mask(e,"
            "b8,d,(unsigned char)(m>>30),4),t3=__builtin_ia32_vcvtsh2sd_round_mask(e,b8,d,(unsigned char)(m>>31),8);"
            "v4sf t4=__builtin_ia32_vcvtsh2ss_round_mask(f,b8,g,(unsigned char)(m>>32),4),t5=__builtin_ia32_vcvtsh2ss_round_mask(f,b8,g,(unsigned char)(m>>33),"
            "12);v8hf t6=__builtin_ia32_vcvtss2sh_round_mask(a8,f,w8,(unsigned char)(m>>34),4),t7=__builtin_ia32_vcvtss2sh_round_mask(a8,g,w8,"
            "(unsigned char)(m>>35),8);FOLD(t0);FOLD(t1);FOLD(t2);FOLD(t3);FOLD(t4);FOLD(t5);FOLD(t6);FOLD(t7);return (int)h;}",
        },
        {
            // The complex products, plain and conjugate, added to c or not, of modest values: at each width under a
            // mask, merged into c, zeroed, and at 64 bytes into a or c as clang's code merges them, and the scalars'
            // first complex numbers, by each rounding control.
            "x86-fp16-complex",
            "__attribute__((target(\"avx512fp16\"))) int tested(int n){unsigned h=2166136261u;unsigned long long m=mask(n);v4sf a4,b4,c4;v8sf a8,b8,c8;"
            "v16sf a16,b16,c16;modest(&a4,8,n);modest(&b4,8,n+1);modest(&c4,8,n+2);modest(&a8,16,n+3);modest(&b8,16,n+4);modest(&c8,16,n+5);modest(&a16,32,"
            "n+6);modest(&b16,32,n+7);modest(&c16,32,n+8);v4sf vfmaddcph0=__builtin_ia32_vfmaddcph128_mask(a4,b4,c4,(unsigned char)m),"
            "vfmaddcph1=__builtin_ia32_vfmaddcph128_maskz(a4,b4,c4,(unsigned char)(m>>4));v8sf vfmaddcph2=__builtin_ia32_vfmaddcph256_mask(a8,b8,c8,"
            "(unsigned char)(m>>8)),vfmaddcph3=__builtin_ia32_vfmaddcph256_maskz(a8,b8,c8,(unsigned char)(m>>12));"
            "v16sf vfmaddcph4=__builtin_ia32_vfmaddcph512_mask(a16,b16,c16,(unsigned short)(m>>16),4),vfmaddcph5=__builtin_ia32_vfmaddcph512_mask(a16,b16,c16,"
            "(unsigned short)(m>>20),9),vfmaddcph6=__builtin_ia32_vfmaddcph512_mask3(a16,b16,c16,(unsigned short)(m>>24),4),"
            "vfmaddcph7=__builtin_ia32_vfmaddcph512_mask3(a16,b16,c16,(unsigned short)(m>>28),10),vfmaddcph8=__builtin_ia32_vfmaddcph512_maskz(a16,b16,c16,"
            "(unsigned short)(m>>32),4),vfmaddcph9=__builtin_ia32_vfmaddcph512_maskz(a16,b16,c16,(unsigned short)(m>>36),11);FOLD(vfmaddcph0);FOLD(vfmaddcph1);"
            "FOLD(vfmaddcph2);FOLD(vfmaddcph3);FOLD(vfmaddcph4);FOLD(vfmaddcph5);FOLD(vfmaddcph6);FOLD(vfmaddcph7);FOLD(vfmaddcph8);FOLD(vfmaddcph9);"
            "v4sf vfcmaddcph0=__builtin_ia32_vfcmaddcph128_mask(a4,b4,c4,(unsigned char)m),vfcmaddcph1=__builtin_ia32_vfcmaddcph128_maskz(a4,b4,c4,"
            "(unsigned char)(m>>4));v8sf vfcmaddcph2=__builtin_ia32_vfcmaddcph256_mask(a8,b8,c8,(unsigned char)(m>>8)),"
            "vfcmaddcph3=__builtin_ia32_vfcmaddcph256_maskz(a8,b8,c8,(unsigned char)(m>>12));v16sf vfcmaddcph4=__builtin_ia32_vfcmaddcph512_mask(a16,b16,c16,"
            "(unsigned short)(m>>16),4),vfcmaddcph5=__builtin_ia32_vfcmaddcph512_mask(a16,b16,c16,(unsigned short)(m>>20),9),"
            "vfcmaddcph6=__builtin_ia32_vfcmaddcph512_mask3(a16,b16,c16,(unsigned short)(m>>24),4),vfcmaddcph7=__builtin_ia32_vfcmaddcph512_mask3(a16,b16,c16,"
            "(unsigned short)(m>>28),10),vfcmaddcph8=__builtin_ia32_vfcmaddcph512_maskz(a16,b16,c16,(unsigned short)(m>>32),4),"
            "vfcmaddcph9=__builtin_ia32_vfcmaddcph512_maskz(a16,b16,c16,(unsigned short)(m>>36),11);FOLD(vfcmaddcph0);FOLD(vfcmaddcph1);FOLD(vfcmaddcph2);"
            "FOLD(vfcmaddcph3);FOLD(vfcmaddcph4);FOLD(vfcmaddcph5);FOLD(vfcmaddcph6);FOLD(vfcmaddcph7);FOLD(vfcmaddcph8);FOLD(vfcmaddcph9);"
            "v4sf vfmaddcsh0=__builtin_ia32_vfmaddcsh_mask(a4,b4,c4,(unsigned char)(m>>40),4),vfmaddcsh1=__builtin_ia32_vfmaddcsh_mask(a4,b4,c4,"
            "(unsigned char)(m>>41),8),vfmaddcsh2=__builtin_ia32_vfmaddcsh_maskz(a4,b4,c4,(unsigned char)(m>>42),4),"
            "vfmaddcsh3=__builtin_ia32_vfmaddcsh_maskz(a4,b4,c4,(unsigned char)(m>>43),9),vfmaddcsh4=__builtin_ia32_vfmaddcsh_round_mask(a4,b4,c4,"
            "(unsigned char)(m>>44),4),vfmaddcsh5=__builtin_ia32_vfmaddcsh_round_mask(a4,b4,c4,(unsigned char)(m>>45),10),"
            "vfmaddcsh6=__builtin_ia32_vfmaddcsh_round_mask3(a4,b4,c4,(unsigned char)(m>>46),4),vfmaddcsh7=__builtin_ia32_vfmaddcsh_round_mask3(a4,b4,c4,"
            "(unsigned char)(m>>47),11);FOLD(vfmaddcsh0);FOLD(vfmaddcsh1);FOLD(vfmaddcsh2);FOLD(vfmaddcsh3);FOLD(vfmaddcsh4);FOLD(vfmaddcsh5);FOLD(vfmaddcsh6);"
            "FOLD(vfmaddcsh7);v4sf vfcmaddcsh0=__builtin_ia32_vfcmaddcsh_mask(a4,b4,c4,(unsigned char)(m>>40),4),vfcmaddcsh1=__builtin_ia32_vfcmaddcsh_mask(a4,"
            "b4,c4,(unsigned char)(m>>41),8),vfcmaddcsh2=__builtin_ia32_vfcmaddcsh_maskz(a4,b4,c4,(unsigned char)(m>>42),4),"
            "vfcmaddcsh3=__builtin_ia32_vfcmaddcsh_maskz(a4,b4,c4,(unsigned char)(m>>43),9),vfcmaddcsh4=__builtin_ia32_vfcmaddcsh_round_mask(a4,b4,c4,"
            "(unsigned char)(m>>44),4),vfcmaddcsh5=__builtin_ia32_vfcmaddcsh_round_mask(a4,b4,c4,(unsigned char)(m>>45),10),"
            "vfcmaddcsh6=__builtin_ia32_vfcmaddcsh_round_mask3(a4,b4,c4,(unsigned char)(m>>46),4),vfcmaddcsh7=__builtin_ia32_vfcmaddcsh_round_mask3(a4,b4,c4,"
            "(unsigned char)(m>>47),11);FOLD(vfcmaddcsh0);FOLD(vfcmaddcsh1);FOLD(vfcmaddcsh2);FOLD(vfcmaddcsh3);FOLD(vfcmaddcsh4);FOLD(vfcmaddcsh5);"
            "FOLD(vfcmaddcsh6);FOLD(vfcmaddcsh7);v4sf vfmulcph0=__builtin_ia32_vfmulcph128_mask(a4,b4,c4,(unsigned char)(m>>48));"
            "v8sf vfmulcph1=__builtin_ia32_vfmulcph256_mask(a8,b8,c8,(unsigned char)(m>>50));v16sf vfmulcph2=__builtin_ia32_vfmulcph512_mask(a16,b16,c16,"
            "(unsigned short)(m>>52),4),vfmulcph3=__builtin_ia32_vfmulcph512_mask(a16,b16,c16,(unsigned short)(m>>1),8);FOLD(vfmulcph0);FOLD(vfmulcph1);"
            "FOLD(vfmulcph2);FOLD(vfmulcph3);v4sf vfcmulcph0=__builtin_ia32_vfcmulcph128_mask(a4,b4,c4,(unsigned char)(m>>48));"
            "v8sf vfcmulcph1=__builtin_ia32_vfcmulcph256_mask(a8,b8,c8,(unsigned char)(m>>50));v16sf vfcmulcph2=__builtin_ia32_vfcmulcph512_mask(a16,b16,c16,"
            "(unsigned short)(m>>52),4),vfcmulcph3=__builtin_ia32_vfcmulcph512_mask(a16,b16,c16,(unsigned short)(m>>1),8);FOLD(vfcmulcph0);FOLD(vfcmulcph1);"
            "FOLD(vfcmulcph2);FOLD(vfcmulcph3);v4sf vfmulcsh0=__builtin_ia32_vfmulcsh_mask(a4,b4,c4,(unsigned char)(m>>5),4),"
            "vfmulcsh1=__builtin_ia32_vfmulcsh_mask(a4,b4,c4,(unsigned char)(m>>6),11);FOLD(vfmulcsh0);FOLD(vfmulcsh1);"
            "v4sf vfcmulcsh0=__builtin_ia32_vfcmulcsh_mask(a4,b4,c4,(unsigned char)(m>>5),4),vfcmulcsh1=__builtin_ia32_vfcmulcsh_mask(a4,b4,c4,"
            "(unsigned char)(m>>6),11);FOLD(vfcmulcsh0);FOLD(vfcmulcsh1);return (int)h;}",
        },
        {
            // The FMAs under a mask at 64 bytes and of the scalars, merged into a, zeroed and into c, and the
            // alternating ones without one, of modest values; the maximums and minimums; the selects, the scalar one's
            // and each width's, and the scalar's masked loads and stores; the scalar conversions to integers by each
            // rounding control and truncated, of random bits and modest values; and the scalar compare by predicates of
            // each kind, NaNs among its operands.
            "x86-fp16-fmas-and-scalars",
            "__attribute__((target(\"avx512fp16\"))) int tested(int n){unsigned h=2166136261u;unsigned long long m=mask(n);v8hf a8,b8,c8,w8;v16hf a16,b16,c16;"
            "v32hf a32,b32,c32;modest(&a8,8,n);modest(&b8,8,n+1);modest(&c8,8,n+2);fill(&w8,16,n+3);modest(&a16,16,n+4);modest(&b16,16,n+5);modest(&c16,16,"
            "n+6);modest(&a32,32,n+7);modest(&b32,32,n+8);modest(&c32,32,n+9);v32hf f0=__builtin_ia32_vfmaddph512_mask(a32,b32,c32,(unsigned)m,4),"
            "f1=__builtin_ia32_vfmaddph512_mask(a32,b32,c32,(unsigned)(m>>2),9),f2=__builtin_ia32_vfmaddph512_maskz(a32,b32,c32,(unsigned)(m>>4),4),"
            "f3=__builtin_ia32_vfmaddph512_mask3(a32,b32,c32,(unsigned)(m>>6),10),f4=__builtin_ia32_vfmsubph512_mask3(a32,b32,c32,(unsigned)(m>>8),4),"
            "f5=__builtin_ia32_vfmaddsubph512_mask(a32,b32,c32,(unsigned)(m>>10),11),f6=__builtin_ia32_vfmaddsubph512_maskz(a32,b32,c32,(unsigned)(m>>12),4),"
            "f7=__builtin_ia32_vfmaddsubph512_mask3(a32,b32,c32,(unsigned)(m>>14),8),f8=__builtin_ia32_vfmsubaddph512_mask3(a32,b32,c32,(unsigned)(m>>16),4);"
            "FOLD(f0);FOLD(f1);FOLD(f2);FOLD(f3);FOLD(f4);FOLD(f5);FOLD(f6);FOLD(f7);FOLD(f8);v8hf k0=__builtin_ia32_vfmaddsh3_mask(a8,b8,c8,"
            "(unsigned char)(m>>18),4),k1=__builtin_ia32_vfmaddsh3_mask(a8,b8,c8,(unsigned char)(m>>19),10),k2=__builtin_ia32_vfmaddsh3_maskz(a8,b8,c8,"
            "(unsigned char)(m>>20),4),k3=__builtin_ia32_vfmaddsh3_mask3(a8,b8,c8,(unsigned char)(m>>21),9),k4=__builtin_ia32_vfmsubsh3_mask3(a8,b8,c8,"
            "(unsigned char)(m>>22),4),k5=__builtin_ia32_vfmaddsubph(a8,b8,c8);v16hf k6=__builtin_ia32_vfmaddsubph256(a16,b16,c16);FOLD(k0);FOLD(k1);FOLD(k2);"
            "FOLD(k3);FOLD(k4);FOLD(k5);FOLD(k6);v8hf x0=__builtin_ia32_maxph128(a8,b8),x1=__builtin_ia32_minph128(a8,b8);v16hf x2=__builtin_ia32_maxph256(a16,"
            "b16),x3=__builtin_ia32_minph256(a16,b16);v32hf x4=__builtin_ia32_maxph512(a32,b32,4),x5=__builtin_ia32_maxph512(a32,b32,8),"
            "x6=__builtin_ia32_minph512(a32,b32,4),x7=__builtin_ia32_minph512(a32,b32,12);FOLD(x0);FOLD(x1);FOLD(x2);FOLD(x3);FOLD(x4);FOLD(x5);FOLD(x6);"
            "FOLD(x7);v8hf e0=__builtin_ia32_selectsh_128((unsigned char)(m>>23),a8,w8),e1=__builtin_ia32_selectsh_128((unsigned char)(m>>24),a8,w8);FOLD(e0);"
            "FOLD(e1);v8hf e2=__builtin_ia32_selectph_128((unsigned char)(m>>27),a8,w8);v16hf e3=__builtin_ia32_selectph_256((unsigned short)(m>>29),a16,b16);"
            "v32hf e4=__builtin_ia32_selectph_512((unsigned)(m>>31),a32,b32);FOLD(e2);FOLD(e3);FOLD(e4);v8hf l0=__builtin_ia32_loadsh128_mask(&a8,w8,"
            "(unsigned char)(m>>25)),l1=__builtin_ia32_loadsh128_mask(&b8,w8,1);FOLD(l0);FOLD(l1);v8hf o=w8;__builtin_ia32_storesh128_mask(&o,a8,"
            "(unsigned char)(m>>26));FOLD(o);__builtin_ia32_storesh128_mask(&o,b8,0);FOLD(o);v8hf r;fill(&r,16,n+10);int i0=__builtin_ia32_vcvtsh2si32(r,4),"
            "i1=__builtin_ia32_vcvtsh2si32(a8,9),i2=__builtin_ia32_vcvttsh2si32(r,4),i3=__builtin_ia32_vcvttsh2si32(a8,8);"
            "long long j0=__builtin_ia32_vcvtsh2si64(r,4),j1=__builtin_ia32_vcvtsh2si64(b8,10),j2=__builtin_ia32_vcvttsh2si64(r,4),"
            "j3=__builtin_ia32_vcvttsh2si64(b8,12);unsigned u0=__builtin_ia32_vcvtsh2usi32(r,4),u1=__builtin_ia32_vcvtsh2usi32(c8,11),"
            "u2=__builtin_ia32_vcvttsh2usi32(r,4),u3=__builtin_ia32_vcvttsh2usi32(c8,8);unsigned long long v0=__builtin_ia32_vcvtsh2usi64(r,4),"
            "v1=__builtin_ia32_vcvtsh2usi64(c8,8),v2=__builtin_ia32_vcvttsh2usi64(r,4),v3=__builtin_ia32_vcvttsh2usi64(c8,12);FOLD(i0);FOLD(i1);FOLD(i2);"
            "FOLD(i3);FOLD(j0);FOLD(j1);FOLD(j2);FOLD(j3);FOLD(u0);FOLD(u1);FOLD(u2);FOLD(u3);FOLD(v0);FOLD(v1);FOLD(v2);FOLD(v3);v8hf z;fill(&z,16,n+11);"
            "int p[12]={__builtin_ia32_vcomish(a8,b8,0,4),__builtin_ia32_vcomish(a8,b8,1,4),__builtin_ia32_vcomish(a8,b8,2,8),__builtin_ia32_vcomish(a8,b8,4,"
            "4),__builtin_ia32_vcomish(b8,a8,13,4),__builtin_ia32_vcomish(b8,a8,14,8),__builtin_ia32_vcomish(z,a8,3,4),__builtin_ia32_vcomish(z,r,7,4),"
            "__builtin_ia32_vcomish(a8,a8,16,4),__builtin_ia32_vcomish(z,z,24,8),__builtin_ia32_vcomish(r,b8,30,4),__builtin_ia32_vcomish(a8,c8,31,8)};FOLD(p);"
            "return (int)h;}",
        },
        {
            // Half-precision vectors' operators at each width (arithmetic of modest values, negation of random bits,
            // NaNs among them, and compares), the square roots of random bits and the fused multiply-adds of modest
            // values, and the conversions to and from 16-, 32- and 64-bit integers and doubles, of integers whose
            // halves are exact and of halves within the integers' ranges, as clang's headers write the intrinsics.
            "x86-fp16-vectors",
            "__attribute__((target(\"avx512fp16\"))) int tested(int n){unsigned h=2166136261u;v8hf a8,b8,c8,r8;v16hf a16,b16,c16,r16;v32hf a32,b32,c32,r32;"
            "modest(&a8,8,n);modest(&b8,8,n+1);modest(&c8,8,n+2);modest(&a16,16,n+3);modest(&b16,16,n+4);modest(&c16,16,n+5);modest(&a32,32,n+6);modest(&b32,"
            "32,n+7);modest(&c32,32,n+8);fill(&r8,16,n+9);fill(&r16,32,n+10);fill(&r32,64,n+11);v8hf s8=a8+b8,d8=a8-b8,p8=a8*b8,q8=a8/b8,g8=-r8;FOLD(s8);"
            "FOLD(d8);FOLD(p8);FOLD(q8);FOLD(g8);v16hf s16=a16+b16,d16=a16-b16,p16=a16*b16,q16=a16/b16,g16=-r16;FOLD(s16);FOLD(d16);FOLD(p16);FOLD(q16);"
            "FOLD(g16);v32hf s32=a32+b32,d32=a32-b32,p32=a32*b32,q32=a32/b32,g32=-r32;FOLD(s32);FOLD(d32);FOLD(p32);FOLD(q32);FOLD(g32);v8hi l8=a8<b8,"
            "le8=a8<=b8,gt8=r8>b8,ge8=a8>=r8,eq8=r8==r8,ne8=a8!=b8;FOLD(l8);FOLD(le8);FOLD(gt8);FOLD(ge8);FOLD(eq8);FOLD(ne8);v16hi l16=a16<b16,le16=a16<=b16,"
            "gt16=r16>b16,ge16=a16>=r16,eq16=r16==r16,ne16=a16!=b16;FOLD(l16);FOLD(le16);FOLD(gt16);FOLD(ge16);FOLD(eq16);FOLD(ne16);v32hi l32=a32<b32,"
            "le32=a32<=b32,gt32=r32>b32,ge32=a32>=r32,eq32=r32==r32,ne32=a32!=b32;FOLD(l32);FOLD(le32);FOLD(gt32);FOLD(ge32);FOLD(eq32);FOLD(ne32);"
            "v8hf t8=__builtin_elementwise_sqrt(r8),f8=__builtin_elementwise_fma(a8,b8,c8),h8=__builtin_elementwise_fma(a8,b8,-c8);FOLD(t8);FOLD(f8);FOLD(h8);"
            "v16hf t16=__builtin_elementwise_sqrt(r16),f16=__builtin_elementwise_fma(a16,b16,c16),h16=__builtin_elementwise_fma(a16,b16,-c16);FOLD(t16);"
            "FOLD(f16);FOLD(h16);v32hf t32=__builtin_elementwise_sqrt(r32),f32=__builtin_elementwise_fma(a32,b32,c32),h32=__builtin_elementwise_fma(a32,b32,"
            "-c32);FOLD(t32);FOLD(f32);FOLD(h32);v8hi i8;v16hi i16;v32hi i32;v8hu u8;v16hu u16;v32hu u32;small(&i8,8,n,0);small(&i16,16,n+1,0);small(&i32,32,"
            "n+2,0);small(&u8,8,n+3,1);small(&u16,16,n+4,1);small(&u32,32,n+5,1);v8hf w0=__builtin_convertvector(i8,v8hf),w1=__builtin_convertvector(u8,v8hf);"
            "v16hf w2=__builtin_convertvector(i16,v16hf),w3=__builtin_convertvector(u16,v16hf);v32hf w4=__builtin_convertvector(i32,v32hf),"
            "w5=__builtin_convertvector(u32,v32hf);FOLD(w0);FOLD(w1);FOLD(w2);FOLD(w3);FOLD(w4);FOLD(w5);v8hi z0=__builtin_convertvector(w0+a8,v8hi);"
            "v8hu z1=__builtin_convertvector(w1+b8,v8hu);v16hi z2=__builtin_convertvector(w2-a16,v16hi);v16hu z3=__builtin_convertvector(w3+c16,v16hu);"
            "v32hi z4=__builtin_convertvector(w4,v32hi);v32hu z5=__builtin_convertvector(w5+c32,v32hu);FOLD(z0);FOLD(z1);FOLD(z2);FOLD(z3);FOLD(z4);FOLD(z5);"
            "v8si j8;v16si j16;v8su k8;v16su k16;fill(&j8,32,n+12);fill(&j16,64,n+13);fill(&k8,32,n+14);fill(&k16,64,n+15);v8hf y0=__builtin_convertvector(j8,"
            "v8hf),y1=__builtin_convertvector(k8,v8hf);v16hf y2=__builtin_convertvector(j16,v16hf),y3=__builtin_convertvector(k16,v16hf);FOLD(y0);FOLD(y1);"
            "FOLD(y2);FOLD(y3);v8si x0=__builtin_convertvector(a8*b8,v8si);v8su x1=__builtin_convertvector(w1,v8su);v16si x2=__builtin_convertvector(a16+c16,"
            "v16si);v16su x3=__builtin_convertvector(w3,v16su);FOLD(x0);FOLD(x1);FOLD(x2);FOLD(x3);v8di ls8;v8du lu8;v8df df8;fill(&ls8,64,n+16);fill(&lu8,64,"
            "n+17);fill(&df8,64,n+18);v8hf v0=__builtin_convertvector(ls8,v8hf),v1=__builtin_convertvector(lu8,v8hf),v2=__builtin_convertvector(df8,v8hf);"
            "FOLD(v0);FOLD(v1);FOLD(v2);v8di m0=__builtin_convertvector(a8-c8,v8di);v8du m1=__builtin_convertvector(w1,v8du);"
            "v8df m2=__builtin_convertvector(r8,v8df);FOLD(m0);FOLD(m1);FOLD(m2);return (int)h;}",
        },
        {
            // The reductions at each width, each in a function of its own, as the headers' intrinsics are: of random bits
            // (NaNs among them, which both codes' instructions give alike, as they reduce in the same order) and of modest
            // values, the sums and products with starts of the headers' identities and of others (a NaN and an infinity
            // among them), the start the last operation's first operand.
            "x86-fp16-reductions",
            "#define RED(name,T) __attribute__((target(\"avx512fp16\"),noinline)) static _Float16 name(_Float16 s,T a)"
            "{return __builtin_ia32_reduce_##name(s,a);}\n"
            "#define EXT(name,T) __attribute__((target(\"avx512fp16\"),noinline)) static _Float16 name(T a){return __builtin_ia32_reduce_##name(a);}\n"
            "RED(fadd_ph128,v8hf)RED(fadd_ph256,v16hf)RED(fadd_ph512,v32hf)RED(fmul_ph128,v8hf)RED(fmul_ph256,v16hf)RED(fmul_ph512,v32hf)"
            "EXT(fmax_ph128,v8hf)EXT(fmax_ph256,v16hf)EXT(fmax_ph512,v32hf)EXT(fmin_ph128,v8hf)EXT(fmin_ph256,v16hf)EXT(fmin_ph512,v32hf)\n"
            "__attribute__((target(\"avx512fp16\"))) int tested(int n){unsigned h=2166136261u;v8hf a8,m8;v16hf a16,m16;v32hf a32,m32;"
            "fill(&a8,16,n);fill(&a16,32,n+1);fill(&a32,64,n+2);modest(&m8,8,n+3);modest(&m16,16,n+4);modest(&m32,32,n+5);"
            "unsigned short starts[]={0x8000,0x3c00,0x3e00,0x7e01,0xfc00};for(int i=0;i<5;i++){union{unsigned short u;_Float16 f;}v={starts[i]};"
            "_Float16 s=v.f;_Float16 r[]={fadd_ph128(s,a8),fadd_ph256(s,a16),fadd_ph512(s,a32),fmul_ph128(s,a8),fmul_ph256(s,a16),fmul_ph512(s,a32),"
            "fadd_ph128(s,m8),fadd_ph256(s,m16),fadd_ph512(s,m32),fmul_ph128(s,m8),fmul_ph256(s,m16),fmul_ph512(s,m32)};FOLD(r);}"
            "_Float16 x[]={fmax_ph128(a8),fmax_ph256(a16),fmax_ph512(a32),fmin_ph128(a8),fmin_ph256(a16),fmin_ph512(a32),"
            "fmax_ph128(m8),fmax_ph256(m16),fmax_ph512(m32),fmin_ph128(m8),fmin_ph256(m16),fmin_ph512(m32)};FOLD(x);return (int)h;}",
        },
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(programs); i += 1)
    {
        const size_t length = strlen(check_test_fp16_types) + strlen(check_test_fp16_helpers) + strlen(programs[i][1]) + 1;
        char* source = malloc(length);
        check_test_expect(test, source != 0, "AVX512-FP16 program source");
        if (source != 0)
        {
            snprintf(source, length, "%s%s%s", check_test_fp16_types, check_test_fp16_helpers, programs[i][1]);
            check_test_program_source_native_limits(test, source, programs[i][0], clang, false, -1, 0, target, run_native);
            free(source);
        }
    }
}

// What the lowering refuses by name in a znver5 function without the extension: its builtins, and half-precision
// vectors' arithmetic, compares, elementwise builtins and conversions; and in one with it, the chains of their arithmetic
// that clang computes in float (check_lower_vector_half_chain), beside the links a store, a cast or a compare breaks.
static void check_test_program_fp16_refusals(CheckTest* test, const X86Target* target)
{
    static const struct
    {
        const char* source;
        const char* reason;
        uint32_t initializations;
    } fixtures[] = {
        { "void tested(v8hf* p,v8hf* q){*p=__builtin_ia32_maxph128(*p,*q);}", "x86 builtins whose instructions the target hasn't" },
        { "void tested(v8hf* p,v8hf* q){*p=*p*(*q);}", "vector operations on half-precision elements without AVX512-FP16" },
        { "int tested(v8hf* p,v8hf* q){v8hi c=*p<*q;return c[0];}", "vector operations on half-precision elements without AVX512-FP16", 1 },
        { "void tested(v8hf* p,v8hf* q){*p=__builtin_elementwise_fma(*p,*q,*q);}", "elementwise vector builtins of half-precision elements without AVX512-FP16" },
        { "void tested(v8hi* p,v8hf* q){*q=__builtin_convertvector(*p,v8hf);}",
          "vector conversions between half-precision elements and types other than float without AVX512-FP16" },
        { "void tested(v8hf* p,v8df* q){*q=__builtin_convertvector(*p,v8df);}",
          "vector conversions between half-precision elements and types other than float without AVX512-FP16" },
        { "__attribute__((target(\"avx512fp16\"))) void tested(v8hf* p,v8hf* q){*p=*p*(*q)+*q;}",
          "chained arithmetic of half-precision vectors, which clang computes in float without AVX512-FP16" },
        { "__attribute__((target(\"avx512fp16\"))) void tested(v32hf* p,v32hf* q){*p=-(*p/(*q));}",
          "chained arithmetic of half-precision vectors, which clang computes in float without AVX512-FP16" },
        { "__attribute__((target(\"avx512fp16\"))) void tested(v16hf* p,v16hf* q){*p-=*q*(*q);}",
          "chained arithmetic of half-precision vectors, which clang computes in float without AVX512-FP16" },
        { "__attribute__((target(\"avx512fp16\"))) void tested(v8hf* p,v8hf* q){*p=*q-(+(*p+*q));}",
          "chained arithmetic of half-precision vectors, which clang computes in float without AVX512-FP16" },
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(fixtures); i += 1)
    {
        const size_t length = strlen(check_test_fp16_types) + strlen(fixtures[i].source) + 1;
        char* source = malloc(length);
        check_test_expect(test, source != 0, "an AVX512-FP16 refusal's source");
        if (source != 0)
        {
            snprintf(source, length, "%s%s", check_test_fp16_types, fixtures[i].source);
            check_test_program_boundary_target(test, source, "fp16-refusal", true, false, 0, fixtures[i].initializations, fixtures[i].reason, target);
            free(source);
        }
    }
    // What lowers: a negation, which needs no AVX512-FP16, as clang's fneg doesn't (its float conversions are F16C's);
    // and the links of a chain that a store, a cast, a compare and a builtin's argument break.
    static const char* const accepted[] = {
        "void tested(v8hf* p){*p=-*p;}",
        "__attribute__((target(\"avx512fp16\"))) void tested(v8hf* p,v8hf* q){v8hf t=*p*(*q);*p=t+*q;}",
        "__attribute__((target(\"avx512fp16\"))) void tested(v8hf* p,v8hf* q){*p=(v8hf)(*p*(*q))+*q;}",
        "__attribute__((target(\"avx512fp16\"))) void tested(v8hi* r,v8hf* p,v8hf* q){*r=*p*(*q)<*q;}",
        "__attribute__((target(\"avx512fp16\"))) void tested(v8hf* p,v8hf* q){*p=__builtin_elementwise_sqrt(*p*(*q));}",
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(accepted); i += 1)
    {
        const size_t length = strlen(check_test_fp16_types) + strlen(accepted[i]) + 1;
        char* source = malloc(length);
        check_test_expect(test, source != 0, "an AVX512-FP16 control's source");
        if (source != 0)
        {
            snprintf(source, length, "%s%s", check_test_fp16_types, accepted[i]);
            check_test_program_boundary_target(test, source, "fp16-lowered", true, true, 0, i == 1 ? 1 : 0, NULL, target);
            free(source);
        }
    }
}

// In a unit whose target has AVX512-FP16, clang computes half-precision vectors' arithmetic in half precision, chains and
// all, and negates by the sign bits alone (signaling NaNs as they are): this program's chains, compound assignments and
// negations of random bits, against clang -O0's code for sapphirerapids, in a unit of x86-64-v4 and the extension.
static void check_test_program_fp16_unit(CheckTest* test, const char* clang, bool run_native)
{
    static const char program[] =
        "int tested(int n){unsigned h=2166136261u;v8hf a8,b8,c8,r8;v16hf a16,b16,c16;v32hf a32,b32,c32,r32;modest(&a8,8,n);modest(&b8,8,n+1);"
        "modest(&c8,8,n+2);modest(&a16,16,n+3);modest(&b16,16,n+4);modest(&c16,16,n+5);modest(&a32,32,n+6);modest(&b32,32,n+7);modest(&c32,32,n+8);"
        "fill(&r8,16,n+9);fill(&r32,64,n+10);v8hf m8=a8*b8+c8,e8=-(a8*b8),g8=-r8,q8=(a8+b8)*(c8-a8)/b8;FOLD(m8);FOLD(e8);FOLD(g8);FOLD(q8);"
        "v16hf m16=a16*b16-c16,e16=-(a16-b16)*c16;FOLD(m16);FOLD(e16);v32hf m32=a32/b32+c32*a32,e32=-(a32/b32),g32=-r32;FOLD(m32);FOLD(e32);FOLD(g32);"
        "v8hf u8=a8;u8+=b8*c8;v32hf u32=a32;u32-=b32*c32;u32*=a32+c32;FOLD(u8);FOLD(u32);return (int)h;}";
    X86Target unit = { 0 };
    const bool parsed = x86_target_parse("x86-64-v4", strlen("x86-64-v4"), &unit);
    check_test_expect(test, parsed, "the AVX512-FP16 unit's target, x86-64-v4");
    const size_t length = strlen(check_test_fp16_types) + strlen(check_test_fp16_helpers) + strlen(program) + 1;
    char* source = parsed ? malloc(length) : 0;
    check_test_expect(test, !parsed || source != 0, "the AVX512-FP16 unit's program source");
    if (source != 0)
    {
        unit.extensions |= BUSTER_X86_EXTENSION(AVX512FP16);
        snprintf(source, length, "%s%s%s", check_test_fp16_types, check_test_fp16_helpers, program);
        check_test_program_build_release();
        check_test_program_march = "-march=sapphirerapids";
        check_test_program_source_native_limits(test, source, "x86-fp16-unit", clang, false, -1, 0, &unit, run_native);
        check_test_program_build_release();
        check_test_program_march = "-march=x86-64";
        free(source);
    }
}

static __attribute__((noinline)) void check_test_program_fp16(CheckTest* test, const char* clang)
{
    X86Target target = { 0 };
    const bool parsed = x86_target_parse("znver5", strlen("znver5"), &target);
    check_test_expect(test, parsed, "the AVX512-FP16 programs' target, znver5");
    if (parsed)
    {
        // The x86 programs' features (check_test_program_x86's), which clang's code of the other functions may take,
        // and the extension.
        const uint64_t required = BUSTER_X86_FEATURE(AVX512F) | BUSTER_X86_FEATURE(AVX512BW) | BUSTER_X86_FEATURE(AVX512CD) |
                                  BUSTER_X86_FEATURE(AVX512DQ) | BUSTER_X86_FEATURE(AVX512VL) | BUSTER_X86_FEATURE(AVX512VBMI) |
                                  BUSTER_X86_FEATURE(AVX512VBMI2) | BUSTER_X86_FEATURE(AVX512BITALG) | BUSTER_X86_FEATURE(AVX512VNNI) |
                                  BUSTER_X86_FEATURE(GFNI) | BUSTER_X86_FEATURE(VAES) | BUSTER_X86_FEATURE(AVX2) | BUSTER_X86_FEATURE(BMI2) |
                                  BUSTER_X86_FEATURE(AVX512IFMA) | BUSTER_X86_FEATURE(AVX512BF16) | BUSTER_X86_FEATURE(AES) | BUSTER_X86_FEATURE(FMA);
        const X86Target native = x86_target_native();
        const bool run_native = (native.features & required) == required && (native.extensions & BUSTER_X86_EXTENSION(AVX512FP16)) != 0;
        if (!run_native)
        {
            printf("  AVX512-FP16: native execution skipped (the host lacks it, its saved state or the x86 programs' features); the programs compile and link\n");
        }
        check_test_program_build_release();
        check_test_program_march = "-march=znver5";
        check_test_program_fp16_sources(test, clang, &target, run_native);
        // The headers' complex-half splats reinterpret the four-byte parameter as float before broadcasting it.
        // Compile the actual intrinsics and compare each lane's representation, including signed zeros and NaNs.
        check_test_program_includes = true;
        check_test_program_source_native_limits(test,
            "#include <immintrin.h>\n"
            "__attribute__((target(\"avx512fp16\"))) int tested(int n){"
            "static const unsigned bits[]={0u,0x80000000u,0x00008000u,0x80008000u,0x7d017e55u,0xfd01fe55u,0x00010001u,0x3c00bc00u};"
            "unsigned b=bits[(unsigned)(n+3)&7u];_Float16 _Complex z;__builtin_memcpy(&z,&b,4);"
            "__m128h a=_mm_set1_pch(z);__m256h c=_mm256_set1_pch(z);__m512h d=_mm512_set1_pch(z);"
            "unsigned x[4],y[8],w[16];__builtin_memcpy(x,&a,16);__builtin_memcpy(y,&c,32);__builtin_memcpy(w,&d,64);"
            "for(int i=0;i<4;i++)if(x[i]!=b)return 1;for(int i=0;i<8;i++)if(y[i]!=b)return 2;for(int i=0;i<16;i++)if(w[i]!=b)return 3;return 0;}",
            "x86-fp16-complex-half-splats", clang, false, -1, 0, &target, run_native);
        check_test_program_includes = false;
        check_test_program_build_release();
        check_test_program_fp16_refusals(test, &target);
        check_test_program_march = "-march=x86-64";
        check_test_program_fp16_unit(test, clang, run_native);
    }
}
