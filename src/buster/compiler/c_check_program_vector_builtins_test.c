// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #136's generic vector builtins in the lowering (c_check_program_vector_builtins.c), against clang -O0's code of the
// same source for znver5: shuffles, conversions, the elementwise functions and the reductions, called directly on
// inputs from the call's argument, with every defined result folded into the program's hash (a shuffle's undefined
// elements aren't). The programs run where the host has the features their rows take (AVX-512F, BW, CD, DQ, VL, VBMI,
// VBMI2, BITALG and VPOPCNTDQ, by x86_target_native's CPUID and XCR0); elsewhere they compile and link, as clang's
// builds do, and don't run. Include after c_check_program_x86_test.c, whose helpers' style it follows.

static const char check_test_vector_builtin_types[] =
    "typedef signed char v16qs __attribute__((vector_size(16)));typedef unsigned char v16qu __attribute__((vector_size(16)));"
    "typedef signed char v32qs __attribute__((vector_size(32)));typedef unsigned char v32qu __attribute__((vector_size(32)));"
    "typedef signed char v64qs __attribute__((vector_size(64)));typedef unsigned char v64qu __attribute__((vector_size(64)));"
    "typedef short v8hs __attribute__((vector_size(16)));typedef unsigned short v8hu __attribute__((vector_size(16)));"
    "typedef short v16hs __attribute__((vector_size(32)));typedef unsigned short v16hu __attribute__((vector_size(32)));"
    "typedef short v32hs __attribute__((vector_size(64)));typedef unsigned short v32hu __attribute__((vector_size(64)));"
    "typedef int v4ss __attribute__((vector_size(16)));typedef unsigned v4su __attribute__((vector_size(16)));"
    "typedef int v8ss __attribute__((vector_size(32)));typedef unsigned v8su __attribute__((vector_size(32)));"
    "typedef int v16ss __attribute__((vector_size(64)));typedef unsigned v16su __attribute__((vector_size(64)));"
    "typedef long long v2ds __attribute__((vector_size(16)));typedef unsigned long long v2du __attribute__((vector_size(16)));"
    "typedef long long v4ds __attribute__((vector_size(32)));typedef unsigned long long v4du __attribute__((vector_size(32)));"
    "typedef long long v8ds __attribute__((vector_size(64)));typedef unsigned long long v8du __attribute__((vector_size(64)));"
    "typedef float v4sf __attribute__((vector_size(16)));typedef double v4df __attribute__((vector_size(32)));"
    "typedef char v16qc __attribute__((vector_size(16)));typedef char v32qc __attribute__((vector_size(32)));"
    "typedef signed char v2qs __attribute__((vector_size(2)));typedef signed char v4qs __attribute__((vector_size(4)));"
    "typedef signed char v8qs __attribute__((vector_size(8)));typedef short v2hs __attribute__((vector_size(4)));"
    "typedef short v4hs __attribute__((vector_size(8)));typedef int v2ss __attribute__((vector_size(8)));\n"
    "static unsigned fold(unsigned h,const void*p,unsigned long n){const unsigned char*b=p;for(unsigned long i=0;i<n;i++)h=(h^b[i])*16777619u;return h;}\n"
    "#define FOLD(v) h=fold(h,&(v),sizeof(v))\n"
    "static void fill(void*p,unsigned long n,unsigned s){unsigned char*b=p;for(unsigned long i=0;i<n;i++){s=s*1103515245u+12345u;b[i]=(unsigned char)(s>>16);}}\n";

static void check_test_program_vector_builtin_sources(CheckTest* test, const char* clang, const X86Target* target, bool run_native)
{
    static const char* const programs[][2] = {
        {
            "vector-shuffles",
            "int tested(int n){unsigned h=2166136261u;v16qs a1,b1;v32hs a2,b2;v16ss a4,b4;v8ds a8,b8;v4ss x4,y4;v2ds x2;v4sf f4,g4;v4df e4,e5;"
            "fill(&a1,16,n);fill(&b1,16,n+1);fill(&a2,64,n+2);fill(&b2,64,n+3);fill(&a4,64,n+4);fill(&b4,64,n+5);fill(&a8,64,n+6);fill(&b8,64,n+7);"
            "fill(&x4,16,n+8);fill(&y4,16,n+9);fill(&x2,16,n+10);fill(&f4,16,n+11);fill(&g4,16,n+12);fill(&e4,32,n+13);fill(&e5,32,n+14);"
            "v16qs s1=__builtin_shufflevector(a1,b1,0,16+0,1,16+1,2,16+2,3,16+3,4,16+4,5,16+5,6,16+6,7,16+7);FOLD(s1);"
            "v32hs s2=__builtin_shufflevector(a2,b2,4,32+4,5,32+5,6,32+6,7,32+7,12,32+12,13,32+13,14,32+14,15,32+15,20,32+20,21,32+21,22,32+22,"
            "23,32+23,28,32+28,29,32+29,30,32+30,31,32+31);FOLD(s2);"
            "v16ss s3=__builtin_shufflevector(a4,b4,2,18,3,19,2+4,18+4,3+4,19+4,2+8,18+8,3+8,19+8,2+12,18+12,3+12,19+12);FOLD(s3);"
            "v8ds s4=__builtin_shufflevector(a8,b8,0,8,2,10,4,12,6,14);FOLD(s4);"
            "v8du z={0},prev={(unsigned long long)n*977u};v8du s5=__builtin_shufflevector((v8du)a8,z,8,0,1,2,3,4,5,6);"
            "v8du s6=__builtin_shufflevector((v8du)a8,prev,1,2,3,4,5,6,7,8);FOLD(s5);FOLD(s6);"
            "v2ds c1=__builtin_shufflevector(a8,a8,0,1);v4ds c2=__builtin_shufflevector(a8,a8,0,1,2,3);v2ds c3=__builtin_shufflevector(a8,b8,6,15);"
            "FOLD(c1);FOLD(c2);FOLD(c3);v4ds c4=__builtin_shufflevector(x2,x2,0,1,-1,-1);long long c4l[2]={c4[0],c4[1]};FOLD(c4l);"
            "v4ds c5=__builtin_shufflevector(x2,(v2ds){0,0},0,1,2,3);v4ds c6=__builtin_shufflevector(x2,x2,0,1,0,1);FOLD(c5);FOLD(c6);"
            "v16ss c7=__builtin_shufflevector(x4,x4,0,1,2,3,0,1,2,3,0,1,2,3,0,1,2,3);FOLD(c7);"
            "v4ds c8=__builtin_shufflevector(x2,__builtin_nondeterministic_value(x2),0,1,2,3);long long c8l[2]={c8[0],c8[1]};FOLD(c8l);"
            "int k=n;v4ss c9=__builtin_shufflevector(x4,(k++,y4),3,2,1,0);FOLD(c9);FOLD(k);"
            "v4sf c10=__builtin_shufflevector(f4,g4,0,1,4,5);v4df c11=__builtin_shufflevector(e4,e5,1,5,2,6);FOLD(c10);FOLD(c11);"
            "v8ss c12=__builtin_shufflevector(a4,a4,8,9,10,11,12,13,14,15);FOLD(c12);"
            "v16qs c13=__builtin_shufflevector(a1,a1,15u,(char)14,(long long)13,12,11,10,9,8,7,6,5,4,3,2,1,0);FOLD(c13);"
            "v32qs c14=__builtin_shufflevector(a1,b1,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31);FOLD(c14);"
            "return (int)h;}",
        },
        {
            "vector-conversions",
            "int tested(int n){unsigned h=2166136261u;v16qs b;v16qu bu;v16hs w;v16hu wu;v8hs w8;v8hu w8u;v8ss d;v8su du;v16ss dd;v8ds q;v4ds q4;v32qu b32;"
            "fill(&b,16,n);fill(&bu,16,n+1);fill(&w,32,n+2);fill(&wu,32,n+3);fill(&w8,16,n+4);fill(&w8u,16,n+5);fill(&d,32,n+6);fill(&du,32,n+7);"
            "fill(&dd,64,n+8);fill(&q,64,n+9);fill(&q4,32,n+10);fill(&b32,32,n+11);"
            "v16hs r1=__builtin_convertvector(b,v16hs);v16hu r2=__builtin_convertvector(bu,v16hu);FOLD(r1);FOLD(r2);"
            "v16ss r3=__builtin_convertvector(b,v16ss);v16ss r4=__builtin_convertvector(bu,v16ss);FOLD(r3);FOLD(r4);"
            "v8ds r5=__builtin_convertvector(d,v8ds);v8ds r6=__builtin_convertvector(du,v8ds);FOLD(r5);FOLD(r6);"
            "v16ss r7=__builtin_convertvector(w,v16ss);v16su r8=__builtin_convertvector(wu,v16su);FOLD(r7);FOLD(r8);"
            "v8ds r9=__builtin_convertvector(w8,v8ds);v8du r10=__builtin_convertvector(w8u,v8du);FOLD(r9);FOLD(r10);"
            "v32hs r11=__builtin_convertvector(b32,v32hs);FOLD(r11);"
            "v4ss p1=__builtin_convertvector(__builtin_shufflevector(bu,bu,0,1,2,3),v4ss);FOLD(p1);"
            "v8ss p2=__builtin_convertvector(__builtin_shufflevector(bu,bu,0,1,2,3,4,5,6,7),v8ss);FOLD(p2);"
            "v8ds p3=__builtin_convertvector(__builtin_shufflevector(bu,bu,0,1,2,3,4,5,6,7),v8ds);FOLD(p3);"
            "v8ds p4=__builtin_convertvector(__builtin_shufflevector(b,b,0,1,2,3,4,5,6,7),v8ds);FOLD(p4);"
            "v4ds p5=__builtin_convertvector(__builtin_shufflevector(w8,w8,0,1,2,3),v4ds);FOLD(p5);"
            "v16qs t1=__builtin_convertvector(w,v16qs);v16qs t2=__builtin_convertvector(dd,v16qs);v16hs t3=__builtin_convertvector(dd,v16hs);"
            "FOLD(t1);FOLD(t2);FOLD(t3);v8ss t4=__builtin_convertvector(q,v8ss);v8hs t5=__builtin_convertvector(q,v8hs);"
            "v8hs t6=__builtin_convertvector(d,v8hs);v4ss t7=__builtin_convertvector(q4,v4ss);FOLD(t4);FOLD(t5);FOLD(t6);FOLD(t7);"
            "v16su s1=__builtin_convertvector(dd,v16su);FOLD(s1);"
            "return (int)h;}",
        },
        {
            "vector-elementwise",
            "int tested(int n){unsigned h=2166136261u;v64qs a,b;v64qu au,bu;v32hs w,x;v32hu wu,xu,cw;v16ss d,e;v16su du,eu,c;v8ds q,r;v8du qu,ru,cq;"
            "v16qs a16,b16;v4ss d4,e4;v4su u4,c4;fill(&a,64,n);fill(&b,64,n+1);fill(&au,64,n+2);fill(&bu,64,n+3);fill(&w,64,n+4);fill(&x,64,n+5);"
            "fill(&wu,64,n+6);fill(&xu,64,n+7);fill(&cw,64,n+8);fill(&d,64,n+9);fill(&e,64,n+10);fill(&du,64,n+11);fill(&eu,64,n+12);fill(&c,64,n+13);"
            "fill(&q,64,n+14);fill(&r,64,n+15);fill(&qu,64,n+16);fill(&ru,64,n+17);fill(&cq,64,n+18);fill(&a16,16,n+19);fill(&b16,16,n+20);"
            "fill(&d4,16,n+21);fill(&e4,16,n+22);fill(&u4,16,n+23);fill(&c4,16,n+24);"
            "v64qs m1=__builtin_elementwise_max(a,b),m2=__builtin_elementwise_min(a,b);v64qu m3=__builtin_elementwise_max(au,bu),m4=__builtin_elementwise_min(au,bu);"
            "FOLD(m1);FOLD(m2);FOLD(m3);FOLD(m4);v32hs m5=__builtin_elementwise_max(w,x);v32hu m6=__builtin_elementwise_min(wu,xu);"
            "v16ss m7=__builtin_elementwise_max(d,e);v16su m8=__builtin_elementwise_max(du,eu);v8ds m9=__builtin_elementwise_max(q,r);"
            "v8du m10=__builtin_elementwise_max(qu,ru),m11=__builtin_elementwise_min(qu,ru);v4ss m12=__builtin_elementwise_min(d4,e4);"
            "FOLD(m5);FOLD(m6);FOLD(m7);FOLD(m8);FOLD(m9);FOLD(m10);FOLD(m11);FOLD(m12);"
            "v64qs s1=__builtin_elementwise_add_sat(a,b),s2=__builtin_elementwise_sub_sat(a,b);"
            "v64qu s3=__builtin_elementwise_sub_sat(au,bu),s4=__builtin_elementwise_add_sat(au,bu);v32hs s5=__builtin_elementwise_add_sat(w,x);"
            "v32hu s6=__builtin_elementwise_sub_sat(wu,xu);v16qs s7=__builtin_elementwise_sub_sat(a16,b16);"
            "FOLD(s1);FOLD(s2);FOLD(s3);FOLD(s4);FOLD(s5);FOLD(s6);FOLD(s7);"
            "v64qs ab1=__builtin_elementwise_abs(a);v32hs ab2=__builtin_elementwise_abs(w);v16ss ab3=__builtin_elementwise_abs(d);"
            "v8ds ab4=__builtin_elementwise_abs(q);v4ss ab5=__builtin_elementwise_abs(d4);FOLD(ab1);FOLD(ab2);FOLD(ab3);FOLD(ab4);FOLD(ab5);"
            "v64qu p1=__builtin_elementwise_popcount(au);v32hu p2=__builtin_elementwise_popcount(wu);v16su p3=__builtin_elementwise_popcount(du);"
            "v8du p4=__builtin_elementwise_popcount(qu);v4su p5=__builtin_elementwise_popcount(u4);FOLD(p1);FOLD(p2);FOLD(p3);FOLD(p4);FOLD(p5);"
            "v16ss dz=d;dz[0]=0;dz[5]=0;v8ds qz=q;qz[3]=0;v4ss d4z=d4;d4z[1]=0;"
            "v16ss l1=__builtin_elementwise_clzg(dz,(v16ss){0}+32),l2=__builtin_elementwise_clzg(dz,e),l3=__builtin_elementwise_clzg(d|1);"
            "v8ds l4=__builtin_elementwise_clzg(qz,(v8ds){0}+64),l5=__builtin_elementwise_clzg(q|1);v4ss l6=__builtin_elementwise_clzg(d4z,e4);"
            "FOLD(l1);FOLD(l2);FOLD(l3);FOLD(l4);FOLD(l5);FOLD(l6);"
            "v16su f1=__builtin_elementwise_fshl(du,eu,c),f2=__builtin_elementwise_fshr(du,eu,c),f3=__builtin_elementwise_fshr(du,du,c);"
            "v8du f4=__builtin_elementwise_fshl(qu,ru,cq),f5=__builtin_elementwise_fshr(qu,ru,cq);v32hu f6=__builtin_elementwise_fshr(wu,xu,cw);"
            "v4su f7=__builtin_elementwise_fshl(u4,u4,c4);FOLD(f1);FOLD(f2);FOLD(f3);FOLD(f4);FOLD(f5);FOLD(f6);FOLD(f7);"
            "return (int)h;}",
        },
        {
            "vector-reductions",
            "int tested(int n){unsigned h=2166136261u;v8ds q;v8du qu;v16ss d;v16su du;v32hs w;v32hu wu;v64qs b;v64qu bu;v8ss d8;v4ss d4;v2ds q2;v16qs b16;"
            "fill(&q,64,n);fill(&qu,64,n+1);fill(&d,64,n+2);fill(&du,64,n+3);fill(&w,64,n+4);fill(&wu,64,n+5);fill(&b,64,n+6);fill(&bu,64,n+7);"
            "fill(&d8,32,n+8);fill(&d4,16,n+9);fill(&q2,16,n+10);fill(&b16,16,n+11);"
            "long long a[]={__builtin_reduce_add(q),__builtin_reduce_and(q),__builtin_reduce_or(q),__builtin_reduce_mul(q),__builtin_reduce_max(q),"
            "__builtin_reduce_min(q)};unsigned long long au[]={__builtin_reduce_max(qu),__builtin_reduce_min(qu)};FOLD(a);FOLD(au);"
            "int i[]={__builtin_reduce_add(d),__builtin_reduce_or(d),__builtin_reduce_and(d),__builtin_reduce_mul(d),__builtin_reduce_max(d),"
            "__builtin_reduce_min(d)};unsigned iu[]={__builtin_reduce_max(du),__builtin_reduce_min(du)};FOLD(i);FOLD(iu);"
            "short s[]={__builtin_reduce_add(w),__builtin_reduce_max(w),__builtin_reduce_mul(w)};unsigned short su=__builtin_reduce_min(wu);FOLD(s);FOLD(su);"
            "signed char c[]={__builtin_reduce_add(b),__builtin_reduce_min(b)};unsigned char cu[]={__builtin_reduce_max(bu),__builtin_reduce_or(bu)};"
            "FOLD(c);FOLD(cu);int g[]={__builtin_reduce_add(d8),__builtin_reduce_max(d4)};long long g3=__builtin_reduce_add(q2);"
            "signed char g4=__builtin_reduce_add(b16);FOLD(g);FOLD(g3);FOLD(g4);"
            "return (int)h;}",
        },
        {
            // The intrinsics' narrowing conversions as the headers write them (_mm_cvtepi32_epi8 and the rest: a shuffle of a
            // conversion into a vector of fewer than 16 bytes with a constant one), then constants of other bytes at other
            // places, a result of 32 bytes and a second vector of __builtin_nondeterministic_value's; and the masked
            // truncating stores (_mm512_mask_cvtepi32_storeu_epi8 and the rest).
            "vector-narrowing",
            "int tested(int n){unsigned h=2166136261u;v4ss d4;v8ss d8;v2ds q2;v4ds q4;v8hs w8;v16ss d16;v32hs w32;v8ds q8;"
            "fill(&d4,16,n);fill(&d8,32,n+1);fill(&q2,16,n+2);fill(&q4,32,n+3);fill(&w8,16,n+4);fill(&d16,64,n+5);fill(&w32,64,n+6);fill(&q8,64,n+7);"
            "v16qs r1=__builtin_shufflevector(__builtin_convertvector(d4,v4qs),(v4qs){0,0,0,0},0,1,2,3,4,5,6,7,7,7,7,7,7,7,7,7);FOLD(r1);"
            "v16qs r2=__builtin_shufflevector(__builtin_convertvector(d8,v8qs),(v8qs){0,0,0,0,0,0,0,0},0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15);FOLD(r2);"
            "v8hs r3=__builtin_shufflevector(__builtin_convertvector(d4,v4hs),(v4hs){0,0,0,0},0,1,2,3,4,5,6,7);FOLD(r3);"
            "v16qs r4=__builtin_shufflevector(__builtin_convertvector(q2,v2qs),(v2qs){0,0},0,1,2,3,3,3,3,3,3,3,3,3,3,3,3,3);FOLD(r4);"
            "v16qs r5=__builtin_shufflevector(__builtin_convertvector(q4,v4qs),(v4qs){0,0,0,0},0,1,2,3,4,5,6,7,7,7,7,7,7,7,7,7);FOLD(r5);"
            "v4ss r6=__builtin_shufflevector(__builtin_convertvector(q2,v2ss),(v2ss){0,0},0,1,2,3);FOLD(r6);"
            "v8hs r7=__builtin_shufflevector(__builtin_convertvector(q2,v2hs),(v2hs){0,0},0,1,2,3,3,3,3,3);FOLD(r7);"
            "v8hs r8=__builtin_shufflevector(__builtin_convertvector(q4,v4hs),(v4hs){0,0,0,0},0,1,2,3,4,5,6,7);FOLD(r8);"
            "v16qs r9=__builtin_shufflevector(__builtin_convertvector(w8,v8qs),(v8qs){0,0,0,0,0,0,0,0},0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15);FOLD(r9);"
            "v16qs r10=__builtin_shufflevector(__builtin_convertvector(d4,v4qs),(v4qs){1,-2,3,(signed char)200},6,0,4,1,7,5,2,3,-1,4,4,0,1,2,3,5);"
            "r10[8]=0;FOLD(r10);"
            "v32qs r11=__builtin_shufflevector(__builtin_convertvector(d8,v8qs),(v8qs){9,8,7,6,5,4,3,2},0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,"
            "0,1,2,3,4,5,6,7,15,14,13,12,11,10,9,8);FOLD(r11);"
            "v8hs r12=__builtin_shufflevector(__builtin_convertvector(q4,v4hs),(v4hs){-1,2,-3,4},4,0,5,1,6,2,7,3);FOLD(r12);"
            "v16qs r13=__builtin_shufflevector(__builtin_convertvector(d4,v4qs),__builtin_nondeterministic_value((v4qs){0}),0,1,2,3,4,5,6,7,7,7,7,7,7,7,7,7);"
            "signed char r13b[4]={r13[0],r13[1],r13[2],r13[3]};FOLD(r13b);"
            "char o1[16]={0},o6[32]={0};short o2[16]={0},o5[8]={0};signed char o3[16]={0};int o4[8]={0};"
            "__builtin_ia32_pmovdb512mem_mask((v16qc*)o1,d16,(unsigned short)(n*40503u));__builtin_ia32_pmovdw512mem_mask((v16hs*)o2,d16,(unsigned short)(n*7919u));"
            "__builtin_ia32_pmovqb512mem_mask((v16qc*)o3,q8,(unsigned char)(n*77u));__builtin_ia32_pmovqd512mem_mask((v8ss*)o4,q8,(unsigned char)(n*13u));"
            "__builtin_ia32_pmovqw512mem_mask((v8hs*)o5,q8,(unsigned char)(n*91u));__builtin_ia32_pmovwb512mem_mask((v32qc*)o6,w32,(unsigned)n*2654435761u);"
            "FOLD(o1);FOLD(o2);FOLD(o3);FOLD(o4);FOLD(o5);FOLD(o6);"
            "return (int)h;}",
        },
        {
            // The leaves (c_builtin_x86_helpers.c): rdtsc's counts in order, pause, xgetbv's XCR0, the trailing zeros' counts
            // of 0 and of others at each width, at run time; and the hints, whose operands are a value and nothing.
            "x86-leaves",
            "int tested(int n){unsigned h=2166136261u;unsigned zero=(unsigned)n>>31;"
            "unsigned long long t0=__rdtsc(),t1=__rdtsc(),t2=__rdtsc();int ordered=t1>=t0&&t2>=t1;FOLD(ordered);__builtin_ia32_pause();"
            "unsigned long long x0=__builtin_ia32_xgetbv(zero);FOLD(x0);"
            "unsigned z[]={__builtin_ia32_tzcnt_u32(zero),__builtin_ia32_tzcnt_u32(1u<<(n&31)),__builtin_ia32_tzcnt_u32((unsigned)n*2654435761u),"
            "__builtin_ia32_tzcnt_u32(zero|0x80000000u)};FOLD(z);"
            "unsigned long long y[]={__builtin_ia32_tzcnt_u64(zero),__builtin_ia32_tzcnt_u64(1ull<<(n*7&63)),__builtin_ia32_tzcnt_u64(zero|1ull<<63),"
            "__builtin_ia32_tzcnt_u64((unsigned long long)n<<40)};FOLD(y);"
            "unsigned short s[]={__builtin_ia32_tzcnt_u16((unsigned short)zero),__builtin_ia32_tzcnt_u16((unsigned short)(n<<9)),"
            "__builtin_ia32_tzcnt_u16((unsigned short)(zero|0x8000u)),__builtin_ia32_tzcnt_u16((unsigned short)(n<<16))};FOLD(s);"
            "long u=__builtin_unpredictable(n*3L);int p=__builtin_unpredictable(n>0);FOLD(u);FOLD(p);__builtin_assume(n>0);"
            "return (int)h;}",
        },
        {
            "vector-nondeterministic",
            "int tested(int n){unsigned h=2166136261u;v8ds a;fill(&a,64,n);v8ds z=__builtin_nondeterministic_value(a);for(int i=0;i<8;i++)z[i]=(long long)i*n;FOLD(z);"
            "int k=__builtin_nondeterministic_value(n);k=n+1;FOLD(k);"
            "return (int)h;}",
        },
    };
    // monitorx and mwaitx's leaves, with values live across them (mwaitx's saves rbx), run where the host has MWAITX
    // (AMD's): mwaitx waits until a write to the monitored line or for its timer, enabled by the extensions' bit 1, of at
    // most n thousand TSC ticks.
    static const char mwaitx[] =
        "int tested(int n){unsigned h=2166136261u;int w=n;long a=n*3L,b=n*5L,c=n*7L,d=n*11L,e=n*13L,f=n*17L;"
        "__builtin_ia32_monitorx(&w,0,0);__builtin_ia32_mwaitx(2,0,(unsigned)n*1000u);long r[]={a,b,c,d,e,f};FOLD(r);FOLD(w);"
        "return (int)h;}";
    const bool has_mwaitx = (x86_target_native().features & BUSTER_X86_FEATURE(MWAITX)) != 0;
    for (uint32_t i = 0; i <= BUSTER_ARRAY_LENGTH(programs); i += 1)
    {
        const char* program = i < BUSTER_ARRAY_LENGTH(programs) ? programs[i][1] : mwaitx;
        const size_t length = strlen(check_test_vector_builtin_types) + strlen(program) + 1;
        char* source = malloc(length);
        check_test_expect(test, source != 0, "vector builtin program source");
        if (source != 0)
        {
            snprintf(source, length, "%s%s", check_test_vector_builtin_types, program);
            check_test_program_source_native_limits(test, source, i < BUSTER_ARRAY_LENGTH(programs) ? programs[i][0] : "x86-mwaitx", clang, false, -1, 0,
                                                    target, i < BUSTER_ARRAY_LENGTH(programs) ? run_native : run_native && has_mwaitx);
            free(source);
        }
    }
    if (run_native && !has_mwaitx)
    {
        printf("  x86-mwaitx: native execution skipped (the host hasn't MWAITX); the program compiles and links\n");
    }
}

// What the lowering refuses by name: each fixture's builtin, with its reason, and on x86-64-v4 the rows whose extensions
// it hasn't.
static void check_test_program_vector_builtin_refusals(CheckTest* test)
{
    static const char types[] =
        "typedef signed char v16qs __attribute__((vector_size(16)));typedef unsigned char v16qu __attribute__((vector_size(16)));"
        "typedef short v8hs __attribute__((vector_size(16)));typedef int v4ss __attribute__((vector_size(16)));"
        "typedef unsigned v4su __attribute__((vector_size(16)));typedef float v4sf __attribute__((vector_size(16)));"
        "typedef signed char v4qs __attribute__((vector_size(4)));";
    // Each fixture's target, source, reason and initialized declarations.
    static const struct
    {
        const char* target;
        const char* source;
        const char* reason;
        uint32_t initializations;
        uint32_t references;
    } fixtures[] = {
        { "x86-64", "unsigned long tested(int n){return __builtin_ia32_xgetbv((unsigned)n);}", "xgetbv without XSAVE", 0 },
        { "x86-64", "int x;int tested(int n){__builtin_ia32_monitorx(&x,0,0);return n;}", "monitorx or mwaitx without MWAITX", 0, 1 },
        { "x86-64", "int tested(int n){__builtin_ia32_mwaitx(0,0,0);return n;}", "monitorx or mwaitx without MWAITX", 0 },
        { "znver5", "int calls;int tested(int n){v4ss a={n};v16qs c=__builtin_shufflevector(__builtin_convertvector(a,v4qs),"
          "(v4qs){(calls++,7),8,9,10},0,1,2,3,4,5,6,7,0,1,2,3,4,5,6,7);return calls+c[n&15];}",
          "narrow vector shuffle operands with nonconstant values or side effects", 2, 1 },
        { "znver5", "int tested(int n){v4ss a={n};v16qs c=__builtin_shufflevector(__builtin_convertvector(a,v4qs),"
          "(v4qs){(n++,7),8,9,10},0,1,2,3,0,1,2,3,0,1,2,3,0,1,2,3);return n+c[0];}",
          "narrow vector shuffle operands with nonconstant values or side effects", 2 },
        { "znver5", "int tested(int n){v4ss a={n};v16qs c=__builtin_shufflevector(__builtin_convertvector(a,v4qs),"
          "(volatile v4qs){7,8,9,10},0,1,2,3,4,5,6,7,0,1,2,3,4,5,6,7);return c[n&15];}",
          "narrow vector shuffle operands with nonconstant values or side effects", 2 },
        { "znver5", "int tested(int n){v4ss a={n};v4sf c=__builtin_convertvector(a,v4sf);return (int)c[0];}", "vector conversions of floating elements", 2 },
        { "znver5", "int tested(int n){v4ss a={n},i={3,2,1,0};v4ss c=__builtin_shufflevector(a,i);return c[0];}", "vector shuffles by a vector of indices", 3 },
        { "znver5", "int tested(int n){v8hs a={(short)n};v8hs c=__builtin_elementwise_clzg(a);return c[0];}",
          "vector leading zero counts of 8- and 16-bit elements", 2 },
        { "znver5", "int tested(int n){v4ss a={n},b={1};v4ss c=__builtin_elementwise_sub_sat(a,b);return c[0];}",
          "saturating vector arithmetic of 32- and 64-bit elements", 3 },
        { "znver5", "int tested(int n){v16qs a={(signed char)n};signed char c=__builtin_reduce_mul(a);return c;}", "vector multiplication of 8-bit elements", 2 },
        { "znver5", "int tested(int n){v4sf a={(float)n},b={1};v4sf c=__builtin_elementwise_max(a,b);return (int)c[0];}",
          "elementwise vector builtins of floating elements", 3 },
        { "znver5", "int tested(int n){v4su a={(unsigned)n};v4su c=__builtin_elementwise_bitreverse(a);return (int)c[0];}", "vector bit reversal", 2 },
        { "znver5", "int tested(int n){v4sf a={(float)n};float c=__builtin_reduce_max(a);return (int)c;}", "vector reductions of floating elements", 2 },
        { "znver5", "int tested(int n){v16qu a={(unsigned char)n},b={1};v16qu c=__builtin_elementwise_fshl(a,b,b);return c[0];}",
          "vector funnel shifts of 8-bit elements", 3 },
        { "znver5", "int tested(int n){return ((v4qs){(signed char)n,1,2,3})[1];}", "vector types of other than 16, 32 or 64 bytes", 0 },
        { "znver5", "int tested(int n){v4ss a={n};v16qs c=__builtin_shufflevector(__builtin_convertvector(a,v4qs),(v4qs){(signed char)n},0,1,2,3,4,5,6,7,7,7,7,7,7,7,7,7);"
          "return c[0];}", "narrow vector shuffle operands with nonconstant values or side effects", 2 },
        { "x86-64-v4", "int tested(int n){v16qs a={(signed char)n},b={1};v16qs c=__builtin_shufflevector(a,b,1,0,3,2,5,4,7,6,9,8,11,10,13,12,15,14);return c[0];}",
          "vector shuffles of 8-bit elements without AVX-512 VBMI", 3 },
        { "x86-64-v4", "int tested(int n){v16qu a={(unsigned char)n};v16qu c=__builtin_elementwise_popcount(a);return c[0];}",
          "vector population counts of 8- and 16-bit elements without AVX-512 BITALG", 2 },
        { "x86-64-v4", "int tested(int n){v4su a={(unsigned)n};v4su c=__builtin_elementwise_popcount(a);return (int)c[0];}",
          "vector population counts of 32- and 64-bit elements without AVX-512 VPOPCNTDQ", 2 },
        { "x86-64-v4", "int tested(int n){v4su a={(unsigned)n},b={1};v4su c=__builtin_elementwise_fshr(a,b,b);return (int)c[0];}",
          "vector funnel shifts without AVX-512 VBMI2", 3 },
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(fixtures); i += 1)
    {
        X86Target target = { 0 };
        const bool parsed = x86_target_parse(fixtures[i].target, strlen(fixtures[i].target), &target);
        const size_t length = strlen(types) + strlen(fixtures[i].source) + 1;
        char* source = malloc(length);
        check_test_expect(test, parsed && source != 0, "a vector builtin refusal's target and source");
        if (parsed && source != 0)
        {
            snprintf(source, length, "%s%s", types, fixtures[i].source);
            check_test_program_boundary_target(test, source, "vector-builtin-refusal", true, false, fixtures[i].references, fixtures[i].initializations, fixtures[i].reason, &target);
        }
        free(source);
    }
    // The target check applies to the unavailable instruction, rather than all helper calls. These are compile-only
    // controls: even an eligible output target need not be executable on the testing host.
    X86Target baseline = { 0 };
    X86Target zen = { 0 };
    const bool targets = x86_target_parse("x86-64", 6, &baseline) && x86_target_parse("znver5", 6, &zen);
    check_test_expect(test, targets, "leaf target eligibility controls");
    if (targets)
    {
        check_test_program_boundary_target(test, "unsigned long tested(int n){return __builtin_ia32_xgetbv((unsigned)n);}",
                                           "eligible-xgetbv", true, true, 0, 0, NULL, &zen);
        check_test_program_boundary_target(test, "int x;int tested(int n){__builtin_ia32_monitorx(&x,0,0);return n;}",
                                           "eligible-monitorx", true, true, 1, 0, NULL, &zen);
        check_test_program_boundary_target(test, "int tested(int n){__builtin_ia32_mwaitx(0,0,0);return n;}",
                                           "eligible-mwaitx", true, true, 0, 0, NULL, &zen);
        check_test_program_boundary_target(test, "unsigned long tested(int n){__builtin_ia32_pause();return __rdtsc()+__builtin_ia32_tzcnt_u64((unsigned long)n);}",
                                           "baseline-leaves", true, true, 0, 0, NULL, &baseline);
    }
}

static __attribute__((noinline)) void check_test_program_vector_builtins(CheckTest* test, const char* clang)
{
    X86Target target = { 0 };
    const bool parsed = x86_target_parse("znver5", strlen("znver5"), &target);
    check_test_expect(test, parsed, "the vector builtin programs' target, znver5");
    if (parsed)
    {
        // The features the programs' rows take, which the host must have and save to run them.
        const uint64_t required = BUSTER_X86_FEATURE(AVX512F) | BUSTER_X86_FEATURE(AVX512BW) | BUSTER_X86_FEATURE(AVX512CD) |
                                  BUSTER_X86_FEATURE(AVX512DQ) | BUSTER_X86_FEATURE(AVX512VL) | BUSTER_X86_FEATURE(AVX512VBMI) |
                                  BUSTER_X86_FEATURE(AVX512VBMI2) | BUSTER_X86_FEATURE(AVX512BITALG) | BUSTER_X86_FEATURE(AVX512VPOPCNTDQ);
        const X86Target native = x86_target_native();
        const bool run_native = (native.features & required) == required;
        if (!run_native)
        {
            printf("  vector builtins: native execution skipped (the host lacks their features or their saved state); the programs compile and link\n");
        }
        check_test_program_build_release();
        check_test_program_march = "-march=znver5";
        check_test_program_vector_builtin_sources(test, clang, &target, run_native);
        check_test_program_build_release();
        check_test_program_march = "-march=x86-64";
        check_test_program_vector_builtin_refusals(test);
    }
}
