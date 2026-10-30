// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #136's generic vector builtins in the lowering (c_check_program_vector_builtins.c), against clang -O0's code of the
// same source for znver5: shuffles, conversions, the elementwise functions, the reductions, the nontemporal loads and
// stores and the bit casts, called directly on inputs from the call's argument and in the forms clang's headers write the
// floating intrinsics in, with every defined result folded into the program's hash (a shuffle's undefined elements
// aren't). The programs run where the host has the features their rows take (AVX-512F, BW, CD, DQ, VL, VBMI,
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

// The floating programs' own types, and their inputs of modest values: n floats (d 0) or doubles (d 1) between about
// -338 and 338, or with `positive` between 0 and 676, whose products and truncations are defined and NaN-free.
#define BUSTER_CHECK_TEST_FLOATING_TYPES \
    "typedef float v8sf __attribute__((vector_size(32)));typedef float v16sf __attribute__((vector_size(64)));" \
    "typedef double v2df __attribute__((vector_size(16)));typedef double v8df __attribute__((vector_size(64)));" \
    "static void modest(void*p,unsigned long n,unsigned s,int d,int positive){for(unsigned long i=0;i<n;i++){s=s*1103515245u+12345u;" \
    "double v=((double)(int)((s>>8)&0xffff)-(positive?0.0:32768.0))/97.0;if(d)((double*)p)[i]=v;else((float*)p)[i]=(float)v;}}\n"

static void check_test_program_vector_builtin_sources(CheckTest* test, const char* clang, const X86Target* target, bool run_native)
{
    static const char* const programs[][2] = {
        {
            "vector-byte-product-reductions",
            "typedef unsigned char u4 __attribute__((vector_size(4)));typedef unsigned char u8 __attribute__((vector_size(8)));"
            "int tested(int n){unsigned h=2166136261u;u4 a4;u8 a8;v16qu a16;v32qu a32;v64qu a64;"
            "fill(&a4,4,n);fill(&a8,8,n+1);fill(&a16,16,n+2);fill(&a32,32,n+3);fill(&a64,64,n+4);"
            "for(int round=0;round<3;round++){"
            "for(int i=0;i<64;i++){unsigned char v=(unsigned char)(n+i*2+1);if(round==0)v|=1;if(round==1&&i==17)v=0;"
            "a64[i]=v;if(i<32)a32[i]=v;if(i<16)a16[i]=v;if(i<8)a8[i]=v;if(i<4)a4[i]=v;}"
            "unsigned char r4=__builtin_reduce_mul(a4),r8=__builtin_reduce_mul(a8),r16=__builtin_reduce_mul(a16),"
            "r32=__builtin_reduce_mul(a32),r64=__builtin_reduce_mul(a64);"
            "unsigned p=1;for(int i=0;i<64;i++){p=(p*a64[i])&255u;if(i==3&&r4!=p)return 101;if(i==7&&r8!=p)return 102;"
            "if(i==15&&r16!=p)return 103;if(i==31&&r32!=p)return 104;}if(r64!=p)return 105;"
            "signed char s16=__builtin_reduce_mul((v16qs)a16),s32=__builtin_reduce_mul((v32qs)a32),s64=__builtin_reduce_mul((v64qs)a64);"
            "if((unsigned char)s16!=r16||(unsigned char)s32!=r32||(unsigned char)s64!=r64)return 106;"
            "FOLD(r4);FOLD(r8);FOLD(r16);FOLD(r32);FOLD(r64);}return (int)h;}",
        },
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
            // Builtins of vectors of 4 and 8 bytes (MMX's __m64, and the narrow vectors of the intrinsics'
            // conversions), whose bytes past them are zero in their registers: shuffles of them, into them and out of
            // them; a shuffle of a conversion into one with a second vector of side effects, a volatile one's or a
            // nonconstant one's, which a 2-byte vector's narrow row refuses; conversions between their integers, and
            // between 8- or 16-bit integers and floats or doubles through 32-bit ones, of values in the result's range
            // (fractions truncated); the elementwise functions; the reductions, which read their bytes alone (a zero
            // lane past them would change a minimum, an and or a product); nontemporal stores of integers and loads
            // (clang's code stores one of floats with SSE4A's MOVNTSD, which x86-sse4a-clzero runs where the host has
            // it); and bit casts between them and integers, floats and doubles, at run time and folded.
            "vector-narrow-builtins",
            BUSTER_CHECK_TEST_FLOATING_TYPES
            "typedef unsigned char v4qu __attribute__((vector_size(4)));typedef unsigned char v8qu __attribute__((vector_size(8)));"
            "typedef unsigned short v4hu __attribute__((vector_size(8)));typedef float v2sf __attribute__((vector_size(8)));"
            "typedef unsigned v2su __attribute__((vector_size(8)));\n"
            "int tested(int n){unsigned h=2166136261u;v4qs b4;v8qs b8;v4qu c4;v8qu c8;v2hs w2;v4hs w4;v4hu u4;v2ss d2;v2su e2;v16qs b16;v8hs w8;v4ss d4;"
            "fill(&b4,4,n);fill(&b8,8,n+1);fill(&c4,4,n+2);fill(&c8,8,n+3);fill(&w2,4,n+4);fill(&w4,8,n+5);fill(&u4,8,n+6);fill(&d2,8,n+7);fill(&e2,8,n+8);"
            "fill(&b16,16,n+9);fill(&w8,16,n+10);fill(&d4,16,n+11);"
            "v16qs s1=__builtin_shufflevector(b8,b8,0,8,1,9,2,10,3,11,4,12,5,13,6,14,7,15);v8qs s2=__builtin_shufflevector(b8,(v8qs){0},0,8,1,9,2,10,3,11);"
            "v4hs s3=__builtin_shufflevector(w4,w4,3,2,1,0);v8qs s4=__builtin_shufflevector(b4,b4,0,1,2,3,4,5,6,7);v4qs s5=__builtin_shufflevector(b16,b16,15,0,7,8);"
            "v2ss s6=__builtin_shufflevector(d4,d4,3,1);v4ss s7=__builtin_shufflevector(d2,(v2ss){0},0,1,2,3);v8hs s8=__builtin_shufflevector(w4,w4,0,1,2,3,4,5,6,7);"
            "FOLD(s1);FOLD(s2);FOLD(s3);FOLD(s4);FOLD(s5);FOLD(s6);FOLD(s7);FOLD(s8);"
            "int calls=0;v16qs s9=__builtin_shufflevector(__builtin_convertvector(d4,v4qs),(v4qs){(calls++,7),8,9,10},0,1,2,3,4,5,6,7,0,1,2,3,4,5,6,7);"
            "volatile v4qs vol={1,2,3,(signed char)n};v16qs s10=__builtin_shufflevector(__builtin_convertvector(d4,v4qs),vol,0,1,2,3,4,5,6,7,7,6,5,4,3,2,1,0);"
            "v16qs s11=__builtin_shufflevector(__builtin_convertvector(d4,v4qs),(v4qs){(signed char)n},0,1,2,3,4,5,6,7,7,7,7,7,7,7,7,7);"
            "FOLD(s9);FOLD(s10);FOLD(s11);FOLD(calls);"
            "v4ss i1=__builtin_convertvector(b4,v4ss);v8hs i2=__builtin_convertvector(b8,v8hs);v4hu i3=__builtin_convertvector(c4,v4hu);"
            "v4qs i4=__builtin_convertvector(d4,v4qs);v4qu i5=__builtin_convertvector(w4,v4qu);v2ds i6=__builtin_convertvector(d2,v2ds);"
            "v2ss i7=__builtin_convertvector((v2ds){n,-n},v2ss);v8qs i8=__builtin_convertvector(w8,v8qs);v2hs i9=__builtin_convertvector(d2,v2hs);"
            "v4hs i10=__builtin_convertvector(b4,v4hs);FOLD(i1);FOLD(i2);FOLD(i3);FOLD(i4);FOLD(i5);FOLD(i6);FOLD(i7);FOLD(i8);FOLD(i9);FOLD(i10);"
            "v4sf f1=__builtin_convertvector(b4,v4sf);v8sf f2=__builtin_convertvector(b8,v8sf);v16sf f3=__builtin_convertvector(b16,v16sf);"
            "v4sf f4=__builtin_convertvector(c4,v4sf);v8sf f5=__builtin_convertvector(w8,v8sf);v4sf f6=__builtin_convertvector(u4,v4sf);"
            "v2df f7=__builtin_convertvector(w2,v2df);v4df f8=__builtin_convertvector(b4,v4df);v8df f9=__builtin_convertvector(w8,v8df);"
            "v2sf f10=__builtin_convertvector(w2,v2sf);v8df f11=__builtin_convertvector(c8,v8df);v4df f12=__builtin_convertvector(u4,v4df);"
            "FOLD(f1);FOLD(f2);FOLD(f3);FOLD(f4);FOLD(f5);FOLD(f6);FOLD(f7);FOLD(f8);FOLD(f9);FOLD(f10);FOLD(f11);FOLD(f12);"
            "v4qs g1=__builtin_convertvector(f1*0.5f,v4qs);v8qs g2=__builtin_convertvector(f2*0.75f,v8qs);v16qs g3=__builtin_convertvector(f3-0.5f,v16qs);"
            "v4qu g4=__builtin_convertvector(f4+0.5f,v4qu);v8hs g5=__builtin_convertvector(f5*0.5f,v8hs);v4hu g6=__builtin_convertvector(f6+0.25f,v4hu);"
            "v2hs g7=__builtin_convertvector(f7*0.5,v2hs);v4qs g8=__builtin_convertvector(f8*-0.5,v4qs);v8hs g9=__builtin_convertvector(f9*0.25,v8hs);"
            "v2hs g10=__builtin_convertvector(f10*0.5f,v2hs);v8qu g11=__builtin_convertvector(f11+0.75,v8qu);v4hu g12=__builtin_convertvector(f12,v4hu);"
            "FOLD(g1);FOLD(g2);FOLD(g3);FOLD(g4);FOLD(g5);FOLD(g6);FOLD(g7);FOLD(g8);FOLD(g9);FOLD(g10);FOLD(g11);FOLD(g12);"
            "v8qs m1=__builtin_elementwise_max(b8,(v8qs){1,-1,2,-2,3,-3,4,-4});v4qu m2=__builtin_elementwise_add_sat(c4,c4);v4hs m3=__builtin_elementwise_abs(w4);"
            "v2ss m4=__builtin_elementwise_min(d2,(v2ss){n,-n});v2su m5=__builtin_elementwise_popcount(e2);"
            "v8qu m6=__builtin_elementwise_sub_sat(c8,(v8qu){100,100,100,100,7,7,7,7});v2ss m7=__builtin_elementwise_clzg(d2|1);"
            "v2sf m8=__builtin_elementwise_sqrt(f10);v2sf m9=__builtin_elementwise_fma(f10,f10,(v2sf){1.5f,-2.0f});v4qs m10=__builtin_elementwise_abs(b4);"
            "v2hs m11=__builtin_elementwise_sub_sat(w2,(v2hs){(short)n,-1});"
            "FOLD(m1);FOLD(m2);FOLD(m3);FOLD(m4);FOLD(m5);FOLD(m6);FOLD(m7);FOLD(m8);FOLD(m9);FOLD(m10);FOLD(m11);"
            "int r1[]={__builtin_reduce_add(d2),__builtin_reduce_and(d2),__builtin_reduce_max(d2),__builtin_reduce_mul(d2),__builtin_reduce_min(d2|1)};"
            "signed char r2[]={__builtin_reduce_add(b8),__builtin_reduce_min(b8),__builtin_reduce_and(b4),__builtin_reduce_max(b4),__builtin_reduce_or(b4)};"
            "short r3[]={__builtin_reduce_add(w4),__builtin_reduce_min(w2),__builtin_reduce_mul(w4),__builtin_reduce_and(w2)};"
            "unsigned char r4[]={__builtin_reduce_min(c8),__builtin_reduce_and(c4),__builtin_reduce_max(c4)};unsigned short r5=__builtin_reduce_min(u4);"
            "FOLD(r1);FOLD(r2);FOLD(r3);FOLD(r4);FOLD(r5);"
            "v8qs nt8[2];v4qs nt4[2];v2sf ntf[2];v2ss nti[2];fill(nt8,16,n+12);fill(nt4,8,n+13);fill(ntf,16,n+14);fill(nti,16,n+15);"
            "__builtin_nontemporal_store(b8,&nt8[1]);__builtin_nontemporal_store(b4,&nt4[0]);__builtin_nontemporal_store(d2,nti);"
            "v8qs l1=__builtin_nontemporal_load(&nt8[0]);v2sf l2=__builtin_nontemporal_load(&ntf[1]);v4qs l3=__builtin_nontemporal_load(&nt4[1]);"
            "FOLD(nt8);FOLD(nt4);FOLD(nti);FOLD(l1);FOLD(l2);FOLD(l3);"
            "long long bc1=__builtin_bit_cast(long long,b8);v4qs bc2=__builtin_bit_cast(v4qs,n*7);v2sf bc3=__builtin_bit_cast(v2sf,d2);"
            "v8qs bc4=__builtin_bit_cast(v8qs,f10);double bc5=__builtin_bit_cast(double,b8);v4hs bc6=__builtin_bit_cast(v4hs,(double)n*1.25);"
            "float bc7=__builtin_bit_cast(float,b4);v4qs bc8=__builtin_bit_cast(v4qs,(float)n*0.5f);unsigned bc9=__builtin_bit_cast(unsigned,c4);"
            "v4qs bc10=__builtin_bit_cast(v4qs,0x01020304);v2ss bc11=__builtin_bit_cast(v2ss,0x0102030405060708ll);"
            "long long bc12=__builtin_bit_cast(long long,(v8qs){1,2,3,4,5,6,7,8});"
            "v4ss bc13=__builtin_bit_cast(v4ss,(v16qs){1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,(signed char)200});"
            "FOLD(bc1);FOLD(bc2);FOLD(bc3);FOLD(bc4);FOLD(bc5);FOLD(bc6);FOLD(bc7);FOLD(bc8);FOLD(bc9);FOLD(bc10);FOLD(bc11);FOLD(bc12);FOLD(bc13);"
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
            // The square roots and fused multiply-adds of floats and doubles at each width: the roots of random bits (NaNs
            // among them, which both codes' instructions propagate alike), the products of modest values (whose NaNs the
            // forms would choose among); and of scalars, as the intrinsics write them (an element's root or product
            // stored back), with values live across.
            "vector-floating-math",
            BUSTER_CHECK_TEST_FLOATING_TYPES
            "int tested(int n){unsigned h=2166136261u;v4sf r4;v8sf r8;v16sf r16;v2df q2;v4df q4;v8df q8;"
            "fill(&r4,16,n);fill(&r8,32,n+1);fill(&r16,64,n+2);fill(&q2,16,n+3);fill(&q4,32,n+4);fill(&q8,64,n+5);"
            "v4sf s1=__builtin_elementwise_sqrt(r4);v8sf s2=__builtin_elementwise_sqrt(r8);v16sf s3=__builtin_elementwise_sqrt(r16);"
            "v2df s4=__builtin_elementwise_sqrt(q2);v4df s5=__builtin_elementwise_sqrt(q4);v8df s6=__builtin_elementwise_sqrt(q8);"
            "FOLD(s1);FOLD(s2);FOLD(s3);FOLD(s4);FOLD(s5);FOLD(s6);"
            "v4sf a4,b4,c4;v8sf a8,b8,c8;v16sf a16,b16,c16;v2df d2,e2,f2;v4df d4,e4,f4;v8df d8,e8,f8;"
            "modest(&a4,4,n,0,0);modest(&b4,4,n+1,0,0);modest(&c4,4,n+2,0,0);modest(&a8,8,n+3,0,0);modest(&b8,8,n+4,0,0);modest(&c8,8,n+5,0,0);"
            "modest(&a16,16,n+6,0,0);modest(&b16,16,n+7,0,0);modest(&c16,16,n+8,0,0);modest(&d2,2,n+9,1,0);modest(&e2,2,n+10,1,0);modest(&f2,2,n+11,1,0);"
            "modest(&d4,4,n+12,1,0);modest(&e4,4,n+13,1,0);modest(&f4,4,n+14,1,0);modest(&d8,8,n+15,1,0);modest(&e8,8,n+16,1,0);modest(&f8,8,n+17,1,0);"
            "v4sf p1=__builtin_elementwise_fma(a4,b4,c4),p2=__builtin_elementwise_fma(a4,b4,-c4);v8sf p3=__builtin_elementwise_fma(a8,b8,c8);"
            "v16sf p4=__builtin_elementwise_fma(a16,b16,c16);v2df p5=__builtin_elementwise_fma(d2,e2,f2);v4df p6=__builtin_elementwise_fma(-d4,e4,f4);"
            "v8df p7=__builtin_elementwise_fma(d8,e8,f8);FOLD(p1);FOLD(p2);FOLD(p3);FOLD(p4);FOLD(p5);FOLD(p6);FOLD(p7);"
            "float x=a4[1];double y=d2[1];float g=(float)n*0.5f;double k=(double)n*-0.25;"
            "float t1=__builtin_elementwise_sqrt(x*x+g);double t2=__builtin_elementwise_sqrt(y*y+k*k);"
            "float t3=__builtin_elementwise_fma(x,b4[2],c4[3]);double t4=__builtin_elementwise_fma(y,e2[0],-f2[1]);"
            "a4[0]=__builtin_elementwise_sqrt(r4[0]);d2[0]=__builtin_elementwise_fma(d2[0],e2[0],f2[0]);"
            "FOLD(t1);FOLD(t2);FOLD(t3);FOLD(t4);FOLD(a4);FOLD(d2);FOLD(g);FOLD(k);"
            "return (int)h;}",
        },
        {
            // __builtin_convertvector with floating elements, each pair at each width it has: from 32- and 64-bit integers
            // of random bits, signed and unsigned, and from a shuffle's first elements as _mm_cvtepi32_pd writes it; to
            // them, truncated, from modest values in their range; between floats and doubles, from half-precision and bf16
            // elements to floats and from floats to half-precision ones, of random bits; and within a format, bf16's at each
            // width too, of operands with effects.
            "vector-floating-conversions",
            BUSTER_CHECK_TEST_FLOATING_TYPES
            "typedef __fp16 v8hp __attribute__((vector_size(16)));typedef __fp16 v16hp __attribute__((vector_size(32)));"
            "typedef _Float16 v8hf __attribute__((vector_size(16)));typedef __bf16 v8bf __attribute__((vector_size(16)));"
            "typedef __bf16 v16bf __attribute__((vector_size(32)));typedef __bf16 v32bf __attribute__((vector_size(64)));\n"
            "int tested(int n){unsigned h=2166136261u;v4ss i4;v8ss i8;v16ss i16;v4su u4;v8su u8;v16su u16;v2ds q2;v4ds q4;v8ds q8;v2du w2;v4du w4;v8du w8;"
            "fill(&i4,16,n);fill(&i8,32,n+1);fill(&i16,64,n+2);fill(&u4,16,n+3);fill(&u8,32,n+4);fill(&u16,64,n+5);"
            "fill(&q2,16,n+6);fill(&q4,32,n+7);fill(&q8,64,n+8);fill(&w2,16,n+9);fill(&w4,32,n+10);fill(&w8,64,n+11);"
            "v4sf a1=__builtin_convertvector(i4,v4sf);v8sf a2=__builtin_convertvector(i8,v8sf);v16sf a3=__builtin_convertvector(i16,v16sf);"
            "v4sf a4=__builtin_convertvector(u4,v4sf);v8sf a5=__builtin_convertvector(u8,v8sf);v16sf a6=__builtin_convertvector(u16,v16sf);"
            "FOLD(a1);FOLD(a2);FOLD(a3);FOLD(a4);FOLD(a5);FOLD(a6);"
            "v4df b1=__builtin_convertvector(i4,v4df);v8df b2=__builtin_convertvector(i8,v8df);v2df b3=__builtin_convertvector(__builtin_shufflevector(i4,i4,0,1),v2df);"
            "v4df b4=__builtin_convertvector(u4,v4df);v8df b5=__builtin_convertvector(u8,v8df);v2df b6=__builtin_convertvector(__builtin_shufflevector(u4,u4,0,1),v2df);"
            "FOLD(b1);FOLD(b2);FOLD(b3);FOLD(b4);FOLD(b5);FOLD(b6);"
            "v4sf c1=__builtin_convertvector(q4,v4sf);v8sf c2=__builtin_convertvector(q8,v8sf);v4sf c3=__builtin_convertvector(w4,v4sf);"
            "v8sf c4=__builtin_convertvector(w8,v8sf);v2df c5=__builtin_convertvector(q2,v2df);v4df c6=__builtin_convertvector(q4,v4df);"
            "v8df c7=__builtin_convertvector(q8,v8df);v2df c8=__builtin_convertvector(w2,v2df);v4df c9=__builtin_convertvector(w4,v4df);"
            "v8df c10=__builtin_convertvector(w8,v8df);FOLD(c1);FOLD(c2);FOLD(c3);FOLD(c4);FOLD(c5);FOLD(c6);FOLD(c7);FOLD(c8);FOLD(c9);FOLD(c10);"
            "v4sf m4,o4;v8sf m8,o8;v16sf m16,o16;v2df n2,p2;v4df n4,p4;v8df n8,p8;modest(&m4,4,n,0,0);modest(&m8,8,n+1,0,0);modest(&m16,16,n+2,0,0);"
            "modest(&o4,4,n+3,0,1);modest(&o8,8,n+4,0,1);modest(&o16,16,n+5,0,1);modest(&n2,2,n+6,1,0);modest(&n4,4,n+7,1,0);modest(&n8,8,n+8,1,0);"
            "modest(&p2,2,n+9,1,1);modest(&p4,4,n+10,1,1);modest(&p8,8,n+11,1,1);"
            "v4ss d1=__builtin_convertvector(m4,v4ss);v8ss d2=__builtin_convertvector(m8,v8ss);v16ss d3=__builtin_convertvector(m16,v16ss);"
            "v4su d4=__builtin_convertvector(o4,v4su);v8su d5=__builtin_convertvector(o8,v8su);v16su d6=__builtin_convertvector(o16,v16su);"
            "v4ds d7=__builtin_convertvector(m4,v4ds);v8ds d8=__builtin_convertvector(m8,v8ds);v2ds d9=__builtin_convertvector(__builtin_shufflevector(m4,m4,0,1),v2ds);"
            "v4du d10=__builtin_convertvector(o4,v4du);v8du d11=__builtin_convertvector(o8,v8du);"
            "FOLD(d1);FOLD(d2);FOLD(d3);FOLD(d4);FOLD(d5);FOLD(d6);FOLD(d7);FOLD(d8);FOLD(d9);FOLD(d10);FOLD(d11);"
            "v4ss e1=__builtin_convertvector(n4,v4ss);v8ss e2=__builtin_convertvector(n8,v8ss);v4su e3=__builtin_convertvector(p4,v4su);"
            "v8su e4=__builtin_convertvector(p8,v8su);v2ds e5=__builtin_convertvector(n2,v2ds);v4ds e6=__builtin_convertvector(n4,v4ds);"
            "v8ds e7=__builtin_convertvector(n8,v8ds);v2du e8=__builtin_convertvector(p2,v2du);v4du e9=__builtin_convertvector(p4,v4du);"
            "v8du e10=__builtin_convertvector(p8,v8du);FOLD(e1);FOLD(e2);FOLD(e3);FOLD(e4);FOLD(e5);FOLD(e6);FOLD(e7);FOLD(e8);FOLD(e9);FOLD(e10);"
            "v4sf r4;v8sf r8;v16sf r16;v4df s4;v8df s8;fill(&r4,16,n+12);fill(&r8,32,n+13);fill(&r16,64,n+14);fill(&s4,32,n+15);fill(&s8,64,n+16);"
            "v4df f1=__builtin_convertvector(r4,v4df);v8df f2=__builtin_convertvector(r8,v8df);v2df f3=__builtin_convertvector(__builtin_shufflevector(r4,r4,0,1),v2df);"
            "v4sf f4=__builtin_convertvector(s4,v4sf);v8sf f5=__builtin_convertvector(s8,v8sf);v4sf f6=__builtin_convertvector(r4,v4sf);"
            "FOLD(f1);FOLD(f2);FOLD(f3);FOLD(f4);FOLD(f5);FOLD(f6);"
            "v8hp h8;v16hp h16;v8hf k8;v8bf g8;v16bf g16;fill(&h8,16,n+17);fill(&h16,32,n+18);fill(&k8,16,n+19);fill(&g8,16,n+20);fill(&g16,32,n+21);"
            "v8sf j1=__builtin_convertvector(h8,v8sf);v16sf j2=__builtin_convertvector(h16,v16sf);"
            "v4sf j3=__builtin_convertvector(__builtin_shufflevector(h8,h8,0,1,2,3),v4sf);v8sf j4=__builtin_convertvector(k8,v8sf);"
            "v8hp j5=__builtin_convertvector(r8,v8hp);v16hp j6=__builtin_convertvector(r16,v16hp);v8sf j7=__builtin_convertvector(g8,v8sf);"
            "v16sf j8=__builtin_convertvector(g16,v16sf);v4sf j9=__builtin_convertvector(__builtin_shufflevector(g8,g8,0,1,2,3),v4sf);"
            "v8hp j10=__builtin_convertvector(k8,v8hp);FOLD(j1);FOLD(j2);FOLD(j3);FOLD(j4);FOLD(j5);FOLD(j6);FOLD(j7);FOLD(j8);FOLD(j9);FOLD(j10);"
            "v32bf g32;fill(&g32,64,n+22);int calls=0;v8bf l1=__builtin_convertvector((calls++,g8),v8bf);"
            "v16bf l2=__builtin_convertvector((calls+=2,g16),v16bf);v32bf l3=__builtin_convertvector((calls+=4,g32),v32bf);FOLD(l1);FOLD(l2);FOLD(l3);FOLD(calls);"
            "return (int)h;}",
        },
        {
            // Nontemporal stores of each width and element kind into aligned vectors beside ones they leave as they were,
            // and nontemporal loads.
            "vector-nontemporal",
            BUSTER_CHECK_TEST_FLOATING_TYPES
            "int tested(int n){unsigned h=2166136261u;v4sf f4;v8sf f8;v16sf f16;v2df d2;v4df d4;v8df d8;v2ds q2;v4ds q4;v8ds q8;v16qs b16;"
            "fill(&f4,16,n);fill(&f8,32,n+1);fill(&f16,64,n+2);fill(&d2,16,n+3);fill(&d4,32,n+4);fill(&d8,64,n+5);fill(&q2,16,n+6);fill(&q4,32,n+7);"
            "fill(&q8,64,n+8);fill(&b16,16,n+9);v4sf o1[2];v8sf o2[2];v16sf o3[2];v2df o4[2];v4df o5[2];v8df o6[2];v2ds o7[2];v4ds o8[2];v8ds o9[2];v16qs o10[2];"
            "fill(o1,32,n+10);fill(o2,64,n+11);fill(o3,128,n+12);fill(o4,32,n+13);fill(o5,64,n+14);fill(o6,128,n+15);fill(o7,32,n+16);fill(o8,64,n+17);"
            "fill(o9,128,n+18);fill(o10,32,n+19);"
            "__builtin_nontemporal_store(f4,&o1[1]);__builtin_nontemporal_store(f8,&o2[1]);__builtin_nontemporal_store(f16,&o3[0]);"
            "__builtin_nontemporal_store(d2,&o4[1]);__builtin_nontemporal_store(d4,&o5[0]);__builtin_nontemporal_store(d8,&o6[1]);"
            "__builtin_nontemporal_store(q2,&o7[0]);__builtin_nontemporal_store(q4,&o8[1]);__builtin_nontemporal_store(q8,&o9[1]);__builtin_nontemporal_store(b16,o10);"
            "FOLD(o1);FOLD(o2);FOLD(o3);FOLD(o4);FOLD(o5);FOLD(o6);FOLD(o7);FOLD(o8);FOLD(o9);FOLD(o10);"
            "v2ds l1=__builtin_nontemporal_load(&q2);v4ds l2=__builtin_nontemporal_load(&q4);v8ds l3=__builtin_nontemporal_load(&q8);"
            "v4sf l4=__builtin_nontemporal_load(&f4);v8df l5=__builtin_nontemporal_load(&o6[1]);FOLD(l1);FOLD(l2);FOLD(l3);FOLD(l4);FOLD(l5);"
            "return (int)h;}",
        },
        {
            // __builtin_bit_cast between floats and unsigned ints and doubles and unsigned longs both ways, of run-time
            // values, of a volatile one, and of constants (which the checker folds); between integers and a pointer's
            // bits; and between vectors of other elements.
            "bit-casts",
            "int tested(int n){unsigned h=2166136261u;float f=(float)n*1.5f-2.25f;double d=(double)n*-3.75+1e10;unsigned u=(unsigned)n*2654435761u;"
            "unsigned long long q=(unsigned long long)n*0x9e3779b97f4a7c15ull;"
            "unsigned b1=__builtin_bit_cast(unsigned,f);float b2=__builtin_bit_cast(float,u);unsigned long long b3=__builtin_bit_cast(unsigned long long,d);"
            "double b4=__builtin_bit_cast(double,q);int b5=__builtin_bit_cast(int,u);long b6=__builtin_bit_cast(long,(void*)&h)-(long)&h;"
            "v4sf vf;fill(&vf,16,n);v4ss b7=__builtin_bit_cast(v4ss,vf);v16qs b8=__builtin_bit_cast(v16qs,b7);"
            "float b9=__builtin_bit_cast(float,0x3f800000u);double b10=__builtin_bit_cast(double,0x400921fb54442d18ull);"
            "volatile float vol=f;unsigned b11=__builtin_bit_cast(unsigned,vol);float b12=__builtin_bit_cast(float,b1+1);"
            "FOLD(b1);FOLD(b2);FOLD(b3);FOLD(b4);FOLD(b5);FOLD(b6);FOLD(b7);FOLD(b8);FOLD(b9);FOLD(b10);FOLD(b11);FOLD(b12);"
            "return (int)h;}",
        },
        {
            // The intrinsics this batch's builtins make, written as clang 23's headers write them (the harness reads no
            // system header): square roots and fused multiply-adds of first elements stored back and in a compound
            // literal, a bf16 conversion's low half by a shuffle, a half-precision vector's cast then conversion, the
            // conversions through a shuffle, the streams and the casts.
            "floating-intrinsic-forms",
            BUSTER_CHECK_TEST_FLOATING_TYPES
            "typedef __fp16 v8hp __attribute__((vector_size(16),aligned(16)));typedef __bf16 v8bf __attribute__((vector_size(16)));"
            "static v4sf fnmsub_ss(v4sf a,v4sf b,v4sf c){a[0]=__builtin_elementwise_fma(a[0],-b[0],-c[0]);return a;}"
            "static v2df sqrt_sd(v2df a,v2df b){return __extension__(v2df){__builtin_elementwise_sqrt(b[0]),a[1]};}"
            "static v2df fmsub_pd(v2df a,v2df b,v2df c){return (v2df)__builtin_elementwise_fma((v2df)a,(v2df)b,-(v2df)c);}"
            "static v8sf cvtph_ps(v2ds a){return (v8sf)__builtin_convertvector((v8hp)a,v8sf);}"
            "static v4sf cvtpbh_ps(v8bf a){return (v4sf)__builtin_shufflevector((v8sf)__builtin_convertvector(a,v8sf),(v8sf)__builtin_convertvector(a,v8sf),0,1,2,3);}"
            "static v2df cvtps_pd(v4sf a){return (v2df)__builtin_convertvector(__builtin_shufflevector((v4sf)a,(v4sf)a,0,1),v2df);}"
            "static v4df cvtepu32_pd(v2ds a){return (v4df)__builtin_convertvector((v4su)a,v4df);}"
            "static void stream_ps(void*p,v4sf a){typedef v4sf aligned_v4sf __attribute__((aligned(16)));__builtin_nontemporal_store((aligned_v4sf)a,(aligned_v4sf*)p);}"
            "static v2ds stream_load_si128(const void*p){return (v2ds)__builtin_nontemporal_load((const v2ds*)p);}"
            "static unsigned castf32_u32(float a){return __builtin_bit_cast(unsigned int,a);}"
            "static double castu64_f64(unsigned long long a){return __builtin_bit_cast(double,a);}\n"
            "int tested(int n){unsigned h=2166136261u;v4sf a,b,c;v2df d,e;v2ds k __attribute__((aligned(64)));v8bf g;"
            "modest(&a,4,n,0,0);modest(&b,4,n+1,0,0);modest(&c,4,n+2,0,0);modest(&d,2,n+3,1,0);modest(&e,2,n+4,1,1);fill(&k,16,n+5);fill(&g,16,n+6);"
            "v4sf r1=fnmsub_ss(a,b,c);v2df r2=sqrt_sd(d,e),r3=fmsub_pd(d,e,d);v8sf r4=cvtph_ps(k);v4sf r5=cvtpbh_ps(g);v2df r6=cvtps_pd(a);"
            "v4df r7=cvtepu32_pd(k);FOLD(r1);FOLD(r2);FOLD(r3);FOLD(r4);FOLD(r5);FOLD(r6);FOLD(r7);"
            "float st[4] __attribute__((aligned(16)));stream_ps(st,a);v2ds ld=stream_load_si128(&k);FOLD(st);FOLD(ld);"
            "unsigned cf=castf32_u32(a[3]);double cd=castu64_f64(0x4000000000000000ull+(unsigned)n);FOLD(cf);FOLD(cd);"
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
    // xbegin's leaf, run where the host has RTM (CPUID 7's EBX bit 11): a transaction that commits (xend), within 100
    // tries, and one that a store through a null pointer aborts (RTM suppresses the fault), whose status isn't -1.
    static const char rtm[] =
        "__attribute__((target(\"rtm\"))) int tested(int n){unsigned h=2166136261u;int committed=0,aborted=0;"
        "for(int i=0;i<100&&!committed;i++){if(__builtin_ia32_xbegin()==~0u){committed=1;__builtin_ia32_xend();}}"
        "for(int i=0;i<100&&!aborted;i++){if(__builtin_ia32_xbegin()==~0u){*(volatile int*)0=n;__builtin_ia32_xend();}else aborted=1;}"
        "FOLD(committed);FOLD(aborted);FOLD(n);return (int)h;}";
    const bool has_mwaitx = (x86_target_native().features & BUSTER_X86_FEATURE(MWAITX)) != 0;
    uint32_t leaf7[4] = { 0 };
    if (__get_cpuid_max(0, 0) >= 7)
    {
        __cpuid_count(7, 0, leaf7[0], leaf7[1], leaf7[2], leaf7[3]);
    }
    const bool has_rtm = (leaf7[1] >> 11 & 1) != 0;
    const uint32_t count = BUSTER_ARRAY_LENGTH(programs);
    for (uint32_t i = 0; i < count + 2; i += 1)
    {
        const char* program = i < count ? programs[i][1] : i == count ? mwaitx : rtm;
        const size_t length = strlen(check_test_vector_builtin_types) + strlen(program) + 1;
        char* source = malloc(length);
        check_test_expect(test, source != 0, "vector builtin program source");
        if (source != 0)
        {
            snprintf(source, length, "%s%s", check_test_vector_builtin_types, program);
            check_test_program_source_native_limits(test, source, i < count ? programs[i][0] : i == count ? "x86-mwaitx" : "x86-rtm", clang, false, -1, 0, target,
                                                    i < count ? run_native : i == count ? run_native && has_mwaitx : run_native && has_rtm);
            free(source);
        }
    }
    if (run_native && !has_mwaitx)
    {
        printf("  x86-mwaitx: native execution skipped (the host hasn't MWAITX); the program compiles and links\n");
    }
    if (run_native && !has_rtm)
    {
        printf("  x86-rtm: native execution skipped (the host hasn't RTM); the program compiles and links\n");
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
        "typedef signed char v2qs __attribute__((vector_size(2)));typedef float v8sf __attribute__((vector_size(32)));"
        "typedef long long v2ds __attribute__((vector_size(16)));typedef signed char v8qs __attribute__((vector_size(8)));"
        "typedef double v8df __attribute__((vector_size(64)));typedef __bf16 v8bf __attribute__((vector_size(16)));"
        "typedef __fp16 v8hp __attribute__((vector_size(16)));typedef _Float16 v8hf __attribute__((vector_size(16)));"
        "typedef float v4sfu __attribute__((vector_size(16),aligned(4)));typedef long long v4ds __attribute__((vector_size(32)));";
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
        // The leaves of the extensions' instructions, without them: RTM's, WAITPKG's and Key Locker's, and its wide
        // forms' with KL but not WIDEKL.
        { "x86-64", "int tested(int n){return __builtin_ia32_xbegin()+n;}", "x86 builtins whose instructions the target hasn't", 0 },
        { "x86-64", "int tested(int n){return __builtin_ia32_umwait(0u,0u,(unsigned)n);}", "x86 builtins whose instructions the target hasn't", 0 },
        { "x86-64", "int tested(int n){return __builtin_ia32_tpause(0u,0u,(unsigned)n);}", "x86 builtins whose instructions the target hasn't", 0 },
        { "x86-64", "int tested(int n){v2ds o,i={n};char h[48]={0};return __builtin_ia32_aesenc128kl_u8(&o,i,h);}",
          "x86 builtins whose instructions the target hasn't", 2 },
        { "x86-64", "__attribute__((target(\"kl\"))) int tested(int n){v2ds o[8],i[8]={{n}};char h[48]={0};return __builtin_ia32_aesencwide128kl_u8(o,i,h);}",
          "x86 builtins whose instructions the target hasn't", 2 },
        { "x86-64", "unsigned tested(int n){v2ds k={n};char h[48];return __builtin_ia32_encodekey128_u32((unsigned)n,k,h);}",
          "x86 builtins whose instructions the target hasn't", 1 },
        { "x86-64", "int tested(int n){v2ds a={n},b={n},c={n};__builtin_ia32_loadiwkey(a,b,c,(unsigned)n);return n;}",
          "x86 builtins whose instructions the target hasn't", 3 },
        { "znver5", "int calls;int tested(int n){v2ds a={n};v16qs c=__builtin_shufflevector(__builtin_convertvector(a,v2qs),"
          "(v2qs){(calls++,7),8},0,1,2,3,0,1,2,3,0,1,2,3,0,1,2,3);return calls+c[n&15];}",
          "narrow vector shuffle operands with nonconstant values or side effects", 2, 1 },
        { "znver5", "int tested(int n){v2ds a={n};v16qs c=__builtin_shufflevector(__builtin_convertvector(a,v2qs),"
          "(v2qs){(n++,7),8},0,1,2,3,0,1,2,3,0,1,2,3,0,1,2,3);return n+c[0];}",
          "narrow vector shuffle operands with nonconstant values or side effects", 2 },
        { "znver5", "int tested(int n){v2ds a={n};v16qs c=__builtin_shufflevector(__builtin_convertvector(a,v2qs),"
          "(volatile v2qs){7,8},0,1,2,3,0,1,2,3,0,1,2,3,0,1,2,3);return c[n&15];}",
          "narrow vector shuffle operands with nonconstant values or side effects", 2 },
        { "znver5", "void tested(v8qs* p,v8hf* q){*q=__builtin_convertvector(*p,v8hf);}",
          "vector conversions between half-precision elements and 8-bit integers, or bf16 elements and 8- or 16-bit integers", 0 },
        { "znver5", "void tested(v8hs* p,v8bf* q){*q=__builtin_convertvector(*p,v8bf);}",
          "vector conversions between half-precision elements and 8-bit integers, or bf16 elements and 8- or 16-bit integers", 0 },
        { "znver5", "void tested(v8sf* p,v8bf* q){*q=__builtin_convertvector(*p,v8bf);}", "vector conversions to bf16 elements, or of bf16 ones to types other than float", 0 },
        { "znver5", "void tested(v8hp* p,v8df* q){*q=__builtin_convertvector(*p,v8df);}",
          "vector conversions between half-precision elements and types other than float without AVX512-FP16", 0 },
        { "znver5", "void tested(v8bf* p,v4ss* q){*q=__builtin_convertvector(__builtin_shufflevector(*p,*p,0,1,2,3),v4ss);}",
          "vector conversions to bf16 elements, or of bf16 ones to types other than float", 0 },
        { "znver5", "void tested(v8hf* p){*p=__builtin_elementwise_sqrt(*p);}", "elementwise vector builtins of half-precision elements without AVX512-FP16", 0 },
        { "znver5", "void tested(v8bf* p){*p=__builtin_elementwise_sqrt(*p);}", "elementwise vector builtins of bf16 elements without AVX10.2", 0 },
        { "znver5", "void tested(v4sfu* p){v4sf v={1};__builtin_nontemporal_store(v,p);}", "nontemporal vector builtins at underaligned addresses", 1 },
        { "znver5", "void tested(int* p,int v){__builtin_nontemporal_store(v,p);}", "nontemporal builtins of scalars", 0 },
        { "znver5", "int tested(int x){return __builtin_elementwise_abs(x);}", "elementwise builtins of scalars", 0 },
        { "x86-64", "float tested(float x){return __builtin_elementwise_sqrt(x);}", "elementwise builtins of scalars without AVX-512F", 0 },
        { "znver5", "struct s{float a;};unsigned tested(struct s v){return __builtin_bit_cast(unsigned,v);}",
          "bit-cast builtins of types other than integers, pointers, floats, doubles, half-precision types and vectors", 0 },
        { "znver5", "int tested(int n){v4ss a={n},i={3,2,1,0};v4ss c=__builtin_shufflevector(a,i);return c[0];}", "vector shuffles by a vector of indices", 3 },
        { "znver5", "int tested(int n){v8hs a={(short)n};v8hs c=__builtin_elementwise_clzg(a);return c[0];}",
          "vector leading zero counts of 8- and 16-bit elements", 2 },
        { "znver5", "int tested(int n){v4ss a={n},b={1};v4ss c=__builtin_elementwise_sub_sat(a,b);return c[0];}",
          "saturating vector arithmetic of 32- and 64-bit elements", 3 },
        { "znver5", "int tested(int n){v4sf a={(float)n},b={1};v4sf c=__builtin_elementwise_max(a,b);return (int)c[0];}",
          "elementwise vector builtins of floating elements", 3 },
        { "znver5", "int tested(int n){v4su a={(unsigned)n};v4su c=__builtin_elementwise_bitreverse(a);return (int)c[0];}", "vector bit reversal", 2 },
        { "znver5", "int tested(int n){v16qu a={(unsigned char)n};v16qu c=__builtin_elementwise_bitreverse(a);return c[0];}",
          "vector bit reversal of bytes without AVX512-BMM", 2 },
        { "znver5", "int tested(int n){v4sf a={(float)n};float c=__builtin_reduce_max(a);return (int)c;}", "vector reductions of floating elements", 2 },
        { "znver5", "int tested(int n){v16qu a={(unsigned char)n},b={1};v16qu c=__builtin_elementwise_fshl(a,b,b);return c[0];}",
          "vector funnel shifts of 8-bit elements", 3 },
        { "znver5", "int tested(int n){return ((v2qs){(signed char)n,1})[1];}", "vector types of other than 4, 8, 16, 32 or 64 bytes", 0 },
        { "znver5", "int tested(int n){v2ds a={n};v16qs c=__builtin_shufflevector(__builtin_convertvector(a,v2qs),(v2qs){(signed char)n},0,1,2,3,3,3,3,3,3,3,3,3,3,3,3,3);"
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
        // The extensions' leaves, in functions whose target attribute has the extensions: Key Locker's on Zen 5, whose
        // AVX-512F and VL its vectors take (the attribute's target is the command line's, check_test_program_march, and
        // its features).
        check_test_program_boundary_target(test, "__attribute__((target(\"rtm\"))) int tested(int n){return __builtin_ia32_xbegin()+n;}", "eligible-xbegin", true, true, 0, 0,
                                           NULL, &baseline);
        check_test_program_boundary_target(test,
                                           "__attribute__((target(\"waitpkg\"))) int tested(int n){return __builtin_ia32_umwait(0u,0u,(unsigned)n)+"
                                           "__builtin_ia32_tpause(1u,(unsigned)n,0u);}",
                                           "eligible-waits", true, true, 0, 0, NULL, &baseline);
        const char* march = check_test_program_march;
        check_test_program_march = "-march=znver5";
        check_test_program_boundary_target(test,
                                           "typedef long long v2ds __attribute__((vector_size(16)));__attribute__((target(\"kl,widekl\"))) int tested(int n){"
                                           "v2ds o[8],i[8]={{n}},k={n};char h[64]={0};int r=__builtin_ia32_aesenc128kl_u8(o,k,h)+__builtin_ia32_aesdec256kl_u8(o,k,h)+"
                                           "__builtin_ia32_aesencwide128kl_u8(o,i,h)+__builtin_ia32_aesdecwide256kl_u8(o,i,h);__builtin_ia32_loadiwkey(k,k,k,0u);"
                                           "return r+(int)__builtin_ia32_encodekey128_u32(0u,k,h)+(int)__builtin_ia32_encodekey256_u32(0u,k,k,h);}",
                                           "eligible-key-locker", true, true, 0, 4, NULL, &zen);
        check_test_program_march = march;
    }
    // The conversions of 64-bit integers take AVX-512DQ, which an AVX-512F and VL target may lack.
    X86Target without_dq = { 0 };
    const bool v4 = x86_target_parse("x86-64-v4", 9, &without_dq);
    check_test_expect(test, v4, "the AVX-512DQ refusal's target");
    if (v4)
    {
        const size_t length = strlen(types) + 200;
        char* source = malloc(length);
        check_test_expect(test, source != 0, "the AVX-512DQ refusal's source");
        if (source != 0)
        {
            without_dq.features &= ~BUSTER_X86_FEATURE(AVX512DQ);
            snprintf(source, length, "%s%s", types, "void tested(v4ds* p,v4sf* q){*q=__builtin_convertvector(*p,v4sf);}");
            check_test_program_boundary_target(test, source, "vector-builtin-refusal", true, false, 0, 0,
                                               "vector conversions between 64-bit integers and floating elements without AVX-512DQ", &without_dq);
        }
        free(source);
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
