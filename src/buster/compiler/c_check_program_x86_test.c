// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #136's x86 builtins in the lowering (c_check_program_x86.c), against clang -O0's code of the same source for znver5:
// each family's builtins called directly, at their prototypes' types, on inputs from the call's argument, with every
// result folded into the program's hash. The programs run where the host has the features their rows take (AVX-512F,
// BW, CD, DQ, VL, VBMI, VBMI2, BITALG, VNNI, IFMA and BF16, GFNI, VAES, AES, FMA, AVX2 and BMI2, by x86_target_native's
// CPUID and XCR0), and those of whose code clang takes more where it has that too (the dot products' AVX-VNNI, SSE4A,
// SHA, the direct stores); elsewhere they compile and link, as clang's builds do, and don't run, and so do the
// operating system's instructions' everywhere. Include after c_check_program_vector_test.c.

static const char check_test_x86_types[] =
    "typedef char v16qi __attribute__((vector_size(16)));typedef char v32qi __attribute__((vector_size(32)));"
    "typedef char v64qi __attribute__((vector_size(64)));typedef unsigned char v64qu __attribute__((vector_size(64)));"
    "typedef unsigned char v16qu __attribute__((vector_size(16)));typedef unsigned char v32qu __attribute__((vector_size(32)));"
    "typedef short v8hi __attribute__((vector_size(16)));typedef short v16hi __attribute__((vector_size(32)));"
    "typedef short v32hi __attribute__((vector_size(64)));typedef int v4si __attribute__((vector_size(16)));"
    "typedef int v8si __attribute__((vector_size(32)));typedef int v16si __attribute__((vector_size(64)));"
    "typedef long long v2di __attribute__((vector_size(16)));typedef long long v4di __attribute__((vector_size(32)));"
    "typedef long long v8di __attribute__((vector_size(64)));typedef float v4sf __attribute__((vector_size(16)));"
    "typedef float v8sf __attribute__((vector_size(32)));typedef double v2df __attribute__((vector_size(16)));"
    "typedef double v4df __attribute__((vector_size(32)));typedef float v16sf __attribute__((vector_size(64)));"
    "typedef double v8df __attribute__((vector_size(64)));typedef __bf16 v8bf __attribute__((vector_size(16)));typedef __bf16 v16bf __attribute__((vector_size(32)));"
    "typedef __bf16 v32bf __attribute__((vector_size(64)));\n"
    "static unsigned fold(unsigned h,const void*p,unsigned long n){const unsigned char*b=p;for(unsigned long i=0;i<n;i++)h=(h^b[i])*16777619u;return h;}\n"
    "#define FOLD(v) h=fold(h,&(v),sizeof(v))\n"
    "static void fill(void*p,unsigned long n,unsigned s){unsigned char*b=p;for(unsigned long i=0;i<n;i++){s=s*1103515245u+12345u;b[i]=(unsigned char)(s>>16);}}\n"
    "static unsigned long long mask(int n){return (unsigned long long)((unsigned)n*2654435761u)<<32|((unsigned)n*40503u+5u);}\n";

// An x86 builtin program: its name, its body after check_test_x86_types, the features clang's code of it takes past
// those every program's rows take, which the host must have too to run it, and whether it only compiles and links (its
// builtins' instructions are the operating system's to allow, or values no two runs share).
typedef struct CheckTestX86Program
{
    const char* name;
    const char* body;
    uint64_t features;
    bool link_only;
} CheckTestX86Program;

// A program of the x86 builtins after the types and helpers, against clang's build of the same for the target, run where
// `run` says.
static void check_test_program_x86_source(CheckTest* test, const char* clang, const X86Target* target, bool run, const char* name, const char* program)
{
    const size_t length = strlen(check_test_x86_types) + strlen(program) + 1;
    char* source = malloc(length);
    check_test_expect(test, source != 0, "x86 builtin program source");
    if (source != 0)
    {
        snprintf(source, length, "%s%s", check_test_x86_types, program);
        check_test_program_source_native_limits(test, source, name, clang, false, -1, 0, target, run);
        free(source);
    }
}

static void check_test_program_x86_sources(CheckTest* test, const char* clang, const X86Target* target, bool run_native)
{
    static const CheckTestX86Program programs[] = {
        {
            // The floating selects, which move bits: each width of ps, pd and pbf, and the scalar ones, whose first element
            // is a's or w's by the mask's first bit and whose others are a's. (ph's rows are pbf's; their builtins need
            // AVX512-FP16, which znver5 hasn't: c_check_program_fp16_test.c calls them.)
            "x86-float-selects",
            "int tested(int n){unsigned h=2166136261u;v4sf a4,w4;v8sf a8,w8;v16sf a16,w16;v2df b2,x2;v4df b4,x4;v8df b8,x8;"
            "v8bf d8,z8;v16bf d16,z16;v32bf d32,z32;"
            "fill(&a4,16,n);fill(&w4,16,n+1);fill(&a8,32,n+2);fill(&w8,32,n+3);fill(&a16,64,n+4);fill(&w16,64,n+5);fill(&b2,16,n+6);"
            "fill(&x2,16,n+7);fill(&b4,32,n+8);fill(&x4,32,n+9);fill(&b8,64,n+10);fill(&x8,64,n+11);"
            "fill(&d8,16,n+18);fill(&z8,16,n+19);"
            "fill(&d16,32,n+20);fill(&z16,32,n+21);fill(&d32,64,n+22);fill(&z32,64,n+23);unsigned long long m=mask(n);"
            "v4sf s1=__builtin_ia32_selectps_128((unsigned char)m,a4,w4);v8sf s2=__builtin_ia32_selectps_256((unsigned char)(m>>8),a8,w8);"
            "v16sf s3=__builtin_ia32_selectps_512((unsigned short)(m>>16),a16,w16);FOLD(s1);FOLD(s2);FOLD(s3);"
            "v2df s4=__builtin_ia32_selectpd_128((unsigned char)(m>>5),b2,x2);v4df s5=__builtin_ia32_selectpd_256((unsigned char)(m>>9),b4,x4);"
            "v8df s6=__builtin_ia32_selectpd_512((unsigned char)(m>>13),b8,x8);FOLD(s4);FOLD(s5);FOLD(s6);"
            "v8bf s10=__builtin_ia32_selectpbf_128((unsigned char)(m>>2),d8,z8);v16bf s11=__builtin_ia32_selectpbf_256((unsigned short)(m>>6),d16,z16);"
            "v32bf s12=__builtin_ia32_selectpbf_512((unsigned)(m>>10),d32,z32);FOLD(s10);FOLD(s11);FOLD(s12);"
            "v4sf s13=__builtin_ia32_selectss_128((unsigned char)m,a4,w4),s14=__builtin_ia32_selectss_128((unsigned char)(m^1),a4,w4);"
            "v2df s15=__builtin_ia32_selectsd_128((unsigned char)(m>>1),b2,x2),s16=__builtin_ia32_selectsd_128((unsigned char)((m>>1)^1),b2,x2);"
            "FOLD(s13);FOLD(s14);FOLD(s15);FOLD(s16);"
            "return (int)h;}",
        },
        {
            "x86-opmasks",
            "int tested(int n){unsigned h=2166136261u;unsigned short a=(unsigned short)(n*40503u+7),b=(unsigned short)(n*7919u+3);"
            "unsigned c=(unsigned)n*2654435761u+1,d=c^0x5555aaaau;unsigned long long e=(unsigned long long)c<<32|d,f=e*0x9e3779b97f4a7c15ull+(unsigned)n;"
            "unsigned short r[]={__builtin_ia32_kandhi(a,b),__builtin_ia32_kaddhi(a,b),__builtin_ia32_kunpckhi(a,b),__builtin_ia32_kshiftlihi(a,3),"
            "__builtin_ia32_kshiftlihi(b,16)};FOLD(r);unsigned s=__builtin_ia32_kunpcksi(c,d);FOLD(s);"
            "unsigned long long q[]={__builtin_ia32_kunpckdi(e,f),__builtin_ia32_kordi(e,f),__builtin_ia32_kxordi(e,f),__builtin_ia32_kshiftlidi(e,5),"
            "__builtin_ia32_kshiftlidi(f,63),__builtin_ia32_kmovq(f)};FOLD(q);"
            "int z[]={__builtin_ia32_ktestzdi(e,f),__builtin_ia32_ktestzdi(e,~e),__builtin_ia32_ktestzdi(0,f)};FOLD(z);"
            "return (int)h;}",
        },
        {
            "x86-compress-expand",
            "int tested(int n){unsigned h=2166136261u;v64qi qa,qw;v32hi ha,hw;v16si da,dw;v8di ea,ew;v8hi xa,xw;v16qi ba,bw;v8si ya,yw;"
            "fill(&qa,64,n);fill(&qw,64,n+1);fill(&ha,64,n+2);fill(&hw,64,n+3);fill(&da,64,n+4);fill(&dw,64,n+5);fill(&ea,64,n+6);fill(&ew,64,n+7);"
            "fill(&xa,16,n+8);fill(&xw,16,n+9);fill(&ba,16,n+10);fill(&bw,16,n+11);fill(&ya,32,n+12);fill(&yw,32,n+13);unsigned long long m=mask(n);"
            "v64qi q[]={__builtin_ia32_compressqi512_mask(qa,qw,m),__builtin_ia32_expandqi512_mask(qa,qw,m)};FOLD(q);"
            "v32hi hh[]={__builtin_ia32_compresshi512_mask(ha,hw,(unsigned)m),__builtin_ia32_expandhi512_mask(ha,hw,(unsigned)(m>>9))};FOLD(hh);"
            "v16si d[]={__builtin_ia32_compresssi512_mask(da,dw,(unsigned short)m),__builtin_ia32_expandsi512_mask(da,dw,(unsigned short)(m>>7))};FOLD(d);"
            "v8di e[]={__builtin_ia32_compressdi512_mask(ea,ew,(unsigned char)m),__builtin_ia32_expanddi512_mask(ea,ew,(unsigned char)(m>>3))};FOLD(e);"
            "v8hi x=__builtin_ia32_compresshi128_mask(xa,xw,(unsigned char)(m>>11));FOLD(x);"
            "v16qi bq=__builtin_ia32_compressqi128_mask(ba,bw,(unsigned short)(m>>13));FOLD(bq);"
            "v8si y=__builtin_ia32_compresssi256_mask(ya,yw,(unsigned char)(m>>17));FOLD(y);"
            "v16si out[2];fill(out,sizeof(out),n+14);__builtin_ia32_compressstoresi512_mask(out,da,(unsigned short)(m>>21));FOLD(out);"
            "v64qi bytes[2];fill(bytes,sizeof(bytes),n+15);__builtin_ia32_compressstoreqi512_mask(bytes,qa,m>>1);FOLD(bytes);"
            "return (int)h;}",
        },
        {
            "x86-mask-vectors",
            "int tested(int n){unsigned h=2166136261u;v64qi a;v16qi b;v32hi w;fill(&a,64,n);fill(&b,16,n+1);fill(&w,64,n+2);unsigned long long m=mask(n);"
            "unsigned long long k=__builtin_ia32_cvtb2mask512(a);unsigned short k2=__builtin_ia32_cvtb2mask128(b);unsigned k3=__builtin_ia32_cvtw2mask512(w);"
            "FOLD(k);FOLD(k2);FOLD(k3);v64qi v=__builtin_ia32_cvtmask2b512(m);v16si vd=__builtin_ia32_cvtmask2d512((unsigned short)(m>>5));FOLD(v);FOLD(vd);"
            "return (int)h;}",
        },
        {
            "x86-convert",
            "int tested(int n){unsigned h=2166136261u;union{unsigned u;float f;}q={0x7fc00000u};v2df d={(double)n*1.75-3.5,0};"
            "v4sf f={(float)n*-2.25f+1.0f,0,0,0};v2df big={1e30*(n+1),0};v4sf nan={q.f,0,0,0};"
            "int a[]={__builtin_ia32_cvttsd2si(d),__builtin_ia32_cvttss2si(f),__builtin_ia32_cvttsd2si(big),__builtin_ia32_cvttss2si(nan)};FOLD(a);"
            "long long b[]={__builtin_ia32_cvttsd2si64(d),__builtin_ia32_cvttss2si64(f),__builtin_ia32_cvttsd2si64(big)};FOLD(b);"
            "return (int)h;}",
        },
        {
            "x86-lanes",
            "int tested(int n){unsigned h=2166136261u;v16si a,b;v8di c,d;v4si x;v4di y;fill(&a,64,n);fill(&b,64,n+1);fill(&c,64,n+2);fill(&d,64,n+3);"
            "fill(&x,16,n+4);fill(&y,32,n+5);unsigned char m=(unsigned char)(n*37+1);"
            "v4si e[]={__builtin_ia32_extracti32x4_mask(a,2,x,m),__builtin_ia32_extracti32x4_mask(b,3,x,0xff)};FOLD(e);"
            "v4di e2=__builtin_ia32_extracti64x4_mask(c,1,y,m);FOLD(e2);"
            "v16si i1=__builtin_ia32_inserti32x4(a,x,3);v8di i2=__builtin_ia32_inserti64x4(c,y,1);v4di i3=__builtin_ia32_insert128i256(y,(v2di)x,1);"
            "FOLD(i1);FOLD(i2);FOLD(i3);v16si al=__builtin_ia32_alignd512(a,b,5);v8di aq=__builtin_ia32_alignq512(c,d,3);FOLD(al);FOLD(aq);"
            "v16si s1=__builtin_ia32_shuf_i32x4(a,b,0x4e);v8di s2=__builtin_ia32_shuf_i64x2(c,d,0xb1);FOLD(s1);FOLD(s2);"
            "return (int)h;}",
        },
        {
            "x86-immediates",
            "int tested(int n){unsigned h=2166136261u;v16si a;v16qi b;v64qi c,d;fill(&a,64,n);fill(&b,16,n+1);fill(&c,64,n+2);fill(&d,64,n+3);"
            "v16si p=__builtin_ia32_pshufd512(a,0x1b);v16si r=__builtin_ia32_prold512(a,7);FOLD(p);FOLD(r);"
            "v16qi s[]={__builtin_ia32_pslldqi128_byteshift(b,3),__builtin_ia32_pslldqi128_byteshift(b,15)};FOLD(s);"
            "v64qi g=__builtin_ia32_vgf2p8affineqb_v64qi(c,d,0x5a);FOLD(g);"
            "return (int)h;}",
        },
        {
            "x86-ternary",
            "int tested(int n){unsigned h=2166136261u;v16si a,b,c;v8di d,e,f;v64qu u;v64qi s;v32hi w1,w2,w3;v64qi q1,q2,q3;v32qi y1,y2,y3;"
            "fill(&a,64,n);fill(&b,64,n+1);fill(&c,64,n+2);fill(&d,64,n+3);fill(&e,64,n+4);fill(&f,64,n+5);fill(&u,64,n+6);fill(&s,64,n+7);"
            "fill(&w1,64,n+8);fill(&w2,64,n+9);fill(&w3,64,n+10);fill(&q1,64,n+11);fill(&q2,64,n+12);fill(&q3,64,n+13);fill(&y1,32,n+14);"
            "fill(&y2,32,n+15);fill(&y3,32,n+16);unsigned long long m=mask(n);"
            "v16si t1=__builtin_ia32_pternlogd512_mask(a,b,c,0x96,(unsigned short)m);v8di t2=__builtin_ia32_pternlogq512_mask(d,e,f,0xe8,(unsigned char)m);"
            "FOLD(t1);FOLD(t2);v16si dp=__builtin_ia32_vpdpbusd512(a,u,s);FOLD(dp);"
            "v16si p1=__builtin_ia32_vpermi2vard512(a,b,c);v8di p2=__builtin_ia32_vpermi2varq512(d,e,f);v32hi p3=__builtin_ia32_vpermi2varhi512(w1,w2,w3);"
            "v64qi p4=__builtin_ia32_vpermi2varqi512(q1,q2,q3);v32qi p5=__builtin_ia32_vpermi2varqi256(y1,y2,y3);"
            "FOLD(p1);FOLD(p2);FOLD(p3);FOLD(p4);FOLD(p5);"
            "return (int)h;}",
        },
        {
            "x86-truncations",
            "int tested(int n){unsigned h=2166136261u;v16si a;v8di b;v32hi c;v16qi w1;v16hi w2;v8si w3;v8hi w4;v32qi w5;fill(&a,64,n);fill(&b,64,n+1);"
            "fill(&c,64,n+2);fill(&w1,16,n+3);fill(&w2,32,n+4);fill(&w3,32,n+5);fill(&w4,16,n+6);fill(&w5,32,n+7);unsigned long long m=mask(n);"
            "v16qi r1=__builtin_ia32_pmovdb512_mask(a,w1,(unsigned short)m);v16hi r2=__builtin_ia32_pmovdw512_mask(a,w2,(unsigned short)(m>>3));"
            "v16qi r3=__builtin_ia32_pmovqb512_mask(b,w1,(unsigned char)m);v8si r4=__builtin_ia32_pmovqd512_mask(b,w3,(unsigned char)(m>>5));"
            "v8hi r5=__builtin_ia32_pmovqw512_mask(b,w4,(unsigned char)(m>>9));v32qi r6=__builtin_ia32_pmovuswb512_mask(c,w5,(unsigned)m);"
            "v32qi r7=__builtin_ia32_pmovwb512_mask(c,w5,(unsigned)(m>>2));FOLD(r1);FOLD(r2);FOLD(r3);FOLD(r4);FOLD(r5);FOLD(r6);FOLD(r7);"
            "return (int)h;}",
        },
        {
            // VPMOV's, VPMOVS's and VPMOVUS's truncations under a mask at each width, of each pair of elements, the bytes
            // random so that many saturate.
            "x86-truncations-saturating",
            "int tested(int n){unsigned h=2166136261u;v4si s4si,w12,w28,w29,w46,w47;v8si s8si,w13,w30,w48;v16si s16si;v2di s2di;v4di s4di;v8di s8di;"
            "v8hi s8hi,w3,w4,w9,w10,w11,w19,w20,w25,w26,w27,w37,w38,w43,w44,w45;v16hi s16hi,w5,w21,w39;v32hi s32hi;"
            "v16qi w0,w1,w2,w6,w7,w8,w14,w16,w17,w18,w22,w23,w24,w31,w32,w34,w35,w36,w40,w41,w42,w49,w50;v32qi w15,w33,w51;fill(&s4si,16,n+0);"
            "fill(&s8si,32,n+1);fill(&s16si,64,n+2);fill(&s2di,16,n+3);fill(&s4di,32,n+4);fill(&s8di,64,n+5);fill(&s8hi,16,n+6);fill(&s16hi,32,n+7);"
            "fill(&s32hi,64,n+8);fill(&w0,16,n+9);fill(&w1,16,n+10);fill(&w2,16,n+11);fill(&w3,16,n+12);fill(&w4,16,n+13);fill(&w5,32,n+14);"
            "fill(&w6,16,n+15);fill(&w7,16,n+16);fill(&w8,16,n+17);fill(&w9,16,n+18);fill(&w10,16,n+19);fill(&w11,16,n+20);fill(&w12,16,n+21);"
            "fill(&w13,32,n+22);fill(&w14,16,n+23);fill(&w15,32,n+24);fill(&w16,16,n+25);fill(&w17,16,n+26);fill(&w18,16,n+27);fill(&w19,16,n+28);"
            "fill(&w20,16,n+29);fill(&w21,32,n+30);fill(&w22,16,n+31);fill(&w23,16,n+32);fill(&w24,16,n+33);fill(&w25,16,n+34);fill(&w26,16,n+35);"
            "fill(&w27,16,n+36);fill(&w28,16,n+37);fill(&w29,16,n+38);fill(&w30,32,n+39);fill(&w31,16,n+40);fill(&w32,16,n+41);fill(&w33,32,n+42);"
            "fill(&w34,16,n+43);fill(&w35,16,n+44);fill(&w36,16,n+45);fill(&w37,16,n+46);fill(&w38,16,n+47);fill(&w39,32,n+48);fill(&w40,16,n+49);"
            "fill(&w41,16,n+50);fill(&w42,16,n+51);fill(&w43,16,n+52);fill(&w44,16,n+53);fill(&w45,16,n+54);fill(&w46,16,n+55);fill(&w47,16,n+56);"
            "fill(&w48,32,n+57);fill(&w49,16,n+58);fill(&w50,16,n+59);fill(&w51,32,n+60);unsigned long long m=mask(n);"
            "v16qi r0=__builtin_ia32_pmovdb128_mask(s4si,w0,(unsigned char)m);FOLD(r0);"
            "v16qi r1=__builtin_ia32_pmovdb256_mask(s8si,w1,(unsigned char)(m>>3));FOLD(r1);"
            "v16qi r2=__builtin_ia32_pmovdb512_mask(s16si,w2,(unsigned short)(m>>24));FOLD(r2);"
            "v8hi r3=__builtin_ia32_pmovdw128_mask(s4si,w3,(unsigned char)(m>>49));FOLD(r3);"
            "v8hi r4=__builtin_ia32_pmovdw256_mask(s8si,w4,(unsigned char)(m>>41));FOLD(r4);"
            "v16hi r5=__builtin_ia32_pmovdw512_mask(s16si,w5,(unsigned short)(m>>46));FOLD(r5);"
            "v16qi r6=__builtin_ia32_pmovqb128_mask(s2di,w6,(unsigned char)(m>>20));FOLD(r6);"
            "v16qi r7=__builtin_ia32_pmovqb256_mask(s4di,w7,(unsigned char)(m>>21));FOLD(r7);"
            "v16qi r8=__builtin_ia32_pmovqb512_mask(s8di,w8,(unsigned char)(m>>28));FOLD(r8);"
            "v8hi r9=__builtin_ia32_pmovqw128_mask(s2di,w9,(unsigned char)(m>>16));FOLD(r9);"
            "v8hi r10=__builtin_ia32_pmovqw256_mask(s4di,w10,(unsigned char)(m>>54));FOLD(r10);"
            "v8hi r11=__builtin_ia32_pmovqw512_mask(s8di,w11,(unsigned char)(m>>15));FOLD(r11);"
            "v4si r12=__builtin_ia32_pmovqd128_mask(s2di,w12,(unsigned char)(m>>47));FOLD(r12);"
            "v8si r13=__builtin_ia32_pmovqd512_mask(s8di,w13,(unsigned char)(m>>27));FOLD(r13);"
            "v16qi r14=__builtin_ia32_pmovwb128_mask(s8hi,w14,(unsigned char)(m>>9));FOLD(r14);"
            "v32qi r15=__builtin_ia32_pmovwb512_mask(s32hi,w15,(unsigned)(m>>5));FOLD(r15);"
            "v16qi r16=__builtin_ia32_pmovsdb128_mask(s4si,w16,(unsigned char)(m>>38));FOLD(r16);"
            "v16qi r17=__builtin_ia32_pmovsdb256_mask(s8si,w17,(unsigned char)(m>>25));FOLD(r17);"
            "v16qi r18=__builtin_ia32_pmovsdb512_mask(s16si,w18,(unsigned short)(m>>56));FOLD(r18);"
            "v8hi r19=__builtin_ia32_pmovsdw128_mask(s4si,w19,(unsigned char)(m>>29));FOLD(r19);"
            "v8hi r20=__builtin_ia32_pmovsdw256_mask(s8si,w20,(unsigned char)(m>>23));FOLD(r20);"
            "v16hi r21=__builtin_ia32_pmovsdw512_mask(s16si,w21,(unsigned short)(m>>42));FOLD(r21);"
            "v16qi r22=__builtin_ia32_pmovsqb128_mask(s2di,w22,(unsigned char)(m>>53));FOLD(r22);"
            "v16qi r23=__builtin_ia32_pmovsqb256_mask(s4di,w23,(unsigned char)(m>>8));FOLD(r23);"
            "v16qi r24=__builtin_ia32_pmovsqb512_mask(s8di,w24,(unsigned char)(m>>59));FOLD(r24);"
            "v8hi r25=__builtin_ia32_pmovsqw128_mask(s2di,w25,(unsigned char)(m>>50));FOLD(r25);"
            "v8hi r26=__builtin_ia32_pmovsqw256_mask(s4di,w26,(unsigned char)(m>>48));FOLD(r26);"
            "v8hi r27=__builtin_ia32_pmovsqw512_mask(s8di,w27,(unsigned char)(m>>34));FOLD(r27);"
            "v4si r28=__builtin_ia32_pmovsqd128_mask(s2di,w28,(unsigned char)(m>>58));FOLD(r28);"
            "v4si r29=__builtin_ia32_pmovsqd256_mask(s4di,w29,(unsigned char)(m>>43));FOLD(r29);"
            "v8si r30=__builtin_ia32_pmovsqd512_mask(s8di,w30,(unsigned char)(m>>60));FOLD(r30);"
            "v16qi r31=__builtin_ia32_pmovswb128_mask(s8hi,w31,(unsigned char)(m>>57));FOLD(r31);"
            "v16qi r32=__builtin_ia32_pmovswb256_mask(s16hi,w32,(unsigned short)(m>>36));FOLD(r32);"
            "v32qi r33=__builtin_ia32_pmovswb512_mask(s32hi,w33,(unsigned)(m>>11));FOLD(r33);"
            "v16qi r34=__builtin_ia32_pmovusdb128_mask(s4si,w34,(unsigned char)(m>>19));FOLD(r34);"
            "v16qi r35=__builtin_ia32_pmovusdb256_mask(s8si,w35,(unsigned char)(m>>14));FOLD(r35);"
            "v16qi r36=__builtin_ia32_pmovusdb512_mask(s16si,w36,(unsigned short)(m>>40));FOLD(r36);"
            "v8hi r37=__builtin_ia32_pmovusdw128_mask(s4si,w37,(unsigned char)(m>>39));FOLD(r37);"
            "v8hi r38=__builtin_ia32_pmovusdw256_mask(s8si,w38,(unsigned char)(m>>32));FOLD(r38);"
            "v16hi r39=__builtin_ia32_pmovusdw512_mask(s16si,w39,(unsigned short)(m>>44));FOLD(r39);"
            "v16qi r40=__builtin_ia32_pmovusqb128_mask(s2di,w40,(unsigned char)(m>>6));FOLD(r40);"
            "v16qi r41=__builtin_ia32_pmovusqb256_mask(s4di,w41,(unsigned char)(m>>45));FOLD(r41);"
            "v16qi r42=__builtin_ia32_pmovusqb512_mask(s8di,w42,(unsigned char)(m>>13));FOLD(r42);"
            "v8hi r43=__builtin_ia32_pmovusqw128_mask(s2di,w43,(unsigned char)(m>>33));FOLD(r43);"
            "v8hi r44=__builtin_ia32_pmovusqw256_mask(s4di,w44,(unsigned char)(m>>51));FOLD(r44);"
            "v8hi r45=__builtin_ia32_pmovusqw512_mask(s8di,w45,(unsigned char)(m>>55));FOLD(r45);"
            "v4si r46=__builtin_ia32_pmovusqd128_mask(s2di,w46,(unsigned char)(m>>22));FOLD(r46);"
            "v4si r47=__builtin_ia32_pmovusqd256_mask(s4di,w47,(unsigned char)(m>>35));FOLD(r47);"
            "v8si r48=__builtin_ia32_pmovusqd512_mask(s8di,w48,(unsigned char)(m>>4));FOLD(r48);"
            "v16qi r49=__builtin_ia32_pmovuswb128_mask(s8hi,w49,(unsigned char)(m>>31));FOLD(r49);"
            "v16qi r50=__builtin_ia32_pmovuswb256_mask(s16hi,w50,(unsigned short)(m>>37));FOLD(r50);"
            "v32qi r51=__builtin_ia32_pmovuswb512_mask(s32hi,w51,(unsigned)(m>>18));FOLD(r51);return (int)h;}",
        },
        {
            // The same truncations into memory under a mask, over bytes that the elements left unwritten keep.
            "x86-truncating-stores",
            "int tested(int n){unsigned h=2166136261u;v4si s4si,o12,o13,o30,o31,o48,o49;v8si s8si,o14,o32,o50;v16si s16si;v2di s2di;v4di s4di;v8di s8di;"
            "v8hi s8hi,o3,o4,o9,o10,o11,o21,o22,o27,o28,o29,o39,o40,o45,o46,o47;v16hi s16hi,o5,o23,o41;v32hi s32hi;"
            "v16qi o0,o1,o2,o6,o7,o8,o15,o16,o18,o19,o20,o24,o25,o26,o33,o34,o36,o37,o38,o42,o43,o44,o51,o52;v32qi o17,o35,o53;fill(&s4si,16,n+0);"
            "fill(&s8si,32,n+1);fill(&s16si,64,n+2);fill(&s2di,16,n+3);fill(&s4di,32,n+4);fill(&s8di,64,n+5);fill(&s8hi,16,n+6);fill(&s16hi,32,n+7);"
            "fill(&s32hi,64,n+8);fill(&o0,16,n+9);fill(&o1,16,n+10);fill(&o2,16,n+11);fill(&o3,16,n+12);fill(&o4,16,n+13);fill(&o5,32,n+14);"
            "fill(&o6,16,n+15);fill(&o7,16,n+16);fill(&o8,16,n+17);fill(&o9,16,n+18);fill(&o10,16,n+19);fill(&o11,16,n+20);fill(&o12,16,n+21);"
            "fill(&o13,16,n+22);fill(&o14,32,n+23);fill(&o15,16,n+24);fill(&o16,16,n+25);fill(&o17,32,n+26);fill(&o18,16,n+27);fill(&o19,16,n+28);"
            "fill(&o20,16,n+29);fill(&o21,16,n+30);fill(&o22,16,n+31);fill(&o23,32,n+32);fill(&o24,16,n+33);fill(&o25,16,n+34);fill(&o26,16,n+35);"
            "fill(&o27,16,n+36);fill(&o28,16,n+37);fill(&o29,16,n+38);fill(&o30,16,n+39);fill(&o31,16,n+40);fill(&o32,32,n+41);fill(&o33,16,n+42);"
            "fill(&o34,16,n+43);fill(&o35,32,n+44);fill(&o36,16,n+45);fill(&o37,16,n+46);fill(&o38,16,n+47);fill(&o39,16,n+48);fill(&o40,16,n+49);"
            "fill(&o41,32,n+50);fill(&o42,16,n+51);fill(&o43,16,n+52);fill(&o44,16,n+53);fill(&o45,16,n+54);fill(&o46,16,n+55);fill(&o47,16,n+56);"
            "fill(&o48,16,n+57);fill(&o49,16,n+58);fill(&o50,32,n+59);fill(&o51,16,n+60);fill(&o52,16,n+61);fill(&o53,32,n+62);"
            "unsigned long long m=mask(n);__builtin_ia32_pmovdb128mem_mask(&o0,s4si,(unsigned char)m);FOLD(o0);"
            "__builtin_ia32_pmovdb256mem_mask(&o1,s8si,(unsigned char)(m>>3));FOLD(o1);"
            "__builtin_ia32_pmovdb512mem_mask(&o2,s16si,(unsigned short)(m>>24));FOLD(o2);"
            "__builtin_ia32_pmovdw128mem_mask(&o3,s4si,(unsigned char)(m>>49));FOLD(o3);"
            "__builtin_ia32_pmovdw256mem_mask(&o4,s8si,(unsigned char)(m>>41));FOLD(o4);"
            "__builtin_ia32_pmovdw512mem_mask(&o5,s16si,(unsigned short)(m>>46));FOLD(o5);"
            "__builtin_ia32_pmovqb128mem_mask(&o6,s2di,(unsigned char)(m>>20));FOLD(o6);"
            "__builtin_ia32_pmovqb256mem_mask(&o7,s4di,(unsigned char)(m>>21));FOLD(o7);"
            "__builtin_ia32_pmovqb512mem_mask(&o8,s8di,(unsigned char)(m>>28));FOLD(o8);"
            "__builtin_ia32_pmovqw128mem_mask(&o9,s2di,(unsigned char)(m>>16));FOLD(o9);"
            "__builtin_ia32_pmovqw256mem_mask(&o10,s4di,(unsigned char)(m>>54));FOLD(o10);"
            "__builtin_ia32_pmovqw512mem_mask(&o11,s8di,(unsigned char)(m>>15));FOLD(o11);"
            "__builtin_ia32_pmovqd128mem_mask(&o12,s2di,(unsigned char)(m>>47));FOLD(o12);"
            "__builtin_ia32_pmovqd256mem_mask(&o13,s4di,(unsigned char)(m>>27));FOLD(o13);"
            "__builtin_ia32_pmovqd512mem_mask(&o14,s8di,(unsigned char)(m>>9));FOLD(o14);"
            "__builtin_ia32_pmovwb128mem_mask(&o15,s8hi,(unsigned char)(m>>5));FOLD(o15);"
            "__builtin_ia32_pmovwb256mem_mask(&o16,s16hi,(unsigned short)(m>>38));FOLD(o16);"
            "__builtin_ia32_pmovwb512mem_mask(&o17,s32hi,(unsigned)(m>>25));FOLD(o17);"
            "__builtin_ia32_pmovsdb128mem_mask(&o18,s4si,(unsigned char)(m>>56));FOLD(o18);"
            "__builtin_ia32_pmovsdb256mem_mask(&o19,s8si,(unsigned char)(m>>29));FOLD(o19);"
            "__builtin_ia32_pmovsdb512mem_mask(&o20,s16si,(unsigned short)(m>>23));FOLD(o20);"
            "__builtin_ia32_pmovsdw128mem_mask(&o21,s4si,(unsigned char)(m>>42));FOLD(o21);"
            "__builtin_ia32_pmovsdw256mem_mask(&o22,s8si,(unsigned char)(m>>53));FOLD(o22);"
            "__builtin_ia32_pmovsdw512mem_mask(&o23,s16si,(unsigned short)(m>>8));FOLD(o23);"
            "__builtin_ia32_pmovsqb128mem_mask(&o24,s2di,(unsigned char)(m>>59));FOLD(o24);"
            "__builtin_ia32_pmovsqb256mem_mask(&o25,s4di,(unsigned char)(m>>50));FOLD(o25);"
            "__builtin_ia32_pmovsqb512mem_mask(&o26,s8di,(unsigned char)(m>>48));FOLD(o26);"
            "__builtin_ia32_pmovsqw128mem_mask(&o27,s2di,(unsigned char)(m>>34));FOLD(o27);"
            "__builtin_ia32_pmovsqw256mem_mask(&o28,s4di,(unsigned char)(m>>58));FOLD(o28);"
            "__builtin_ia32_pmovsqw512mem_mask(&o29,s8di,(unsigned char)(m>>43));FOLD(o29);"
            "__builtin_ia32_pmovsqd128mem_mask(&o30,s2di,(unsigned char)(m>>60));FOLD(o30);"
            "__builtin_ia32_pmovsqd256mem_mask(&o31,s4di,(unsigned char)(m>>57));FOLD(o31);"
            "__builtin_ia32_pmovsqd512mem_mask(&o32,s8di,(unsigned char)(m>>36));FOLD(o32);"
            "__builtin_ia32_pmovswb128mem_mask(&o33,s8hi,(unsigned char)(m>>11));FOLD(o33);"
            "__builtin_ia32_pmovswb256mem_mask(&o34,s16hi,(unsigned short)(m>>19));FOLD(o34);"
            "__builtin_ia32_pmovswb512mem_mask(&o35,s32hi,(unsigned)(m>>14));FOLD(o35);"
            "__builtin_ia32_pmovusdb128mem_mask(&o36,s4si,(unsigned char)(m>>40));FOLD(o36);"
            "__builtin_ia32_pmovusdb256mem_mask(&o37,s8si,(unsigned char)(m>>39));FOLD(o37);"
            "__builtin_ia32_pmovusdb512mem_mask(&o38,s16si,(unsigned short)(m>>32));FOLD(o38);"
            "__builtin_ia32_pmovusdw128mem_mask(&o39,s4si,(unsigned char)(m>>44));FOLD(o39);"
            "__builtin_ia32_pmovusdw256mem_mask(&o40,s8si,(unsigned char)(m>>6));FOLD(o40);"
            "__builtin_ia32_pmovusdw512mem_mask(&o41,s16si,(unsigned short)(m>>45));FOLD(o41);"
            "__builtin_ia32_pmovusqb128mem_mask(&o42,s2di,(unsigned char)(m>>13));FOLD(o42);"
            "__builtin_ia32_pmovusqb256mem_mask(&o43,s4di,(unsigned char)(m>>33));FOLD(o43);"
            "__builtin_ia32_pmovusqb512mem_mask(&o44,s8di,(unsigned char)(m>>51));FOLD(o44);"
            "__builtin_ia32_pmovusqw128mem_mask(&o45,s2di,(unsigned char)(m>>55));FOLD(o45);"
            "__builtin_ia32_pmovusqw256mem_mask(&o46,s4di,(unsigned char)(m>>22));FOLD(o46);"
            "__builtin_ia32_pmovusqw512mem_mask(&o47,s8di,(unsigned char)(m>>35));FOLD(o47);"
            "__builtin_ia32_pmovusqd128mem_mask(&o48,s2di,(unsigned char)(m>>4));FOLD(o48);"
            "__builtin_ia32_pmovusqd256mem_mask(&o49,s4di,(unsigned char)(m>>31));FOLD(o49);"
            "__builtin_ia32_pmovusqd512mem_mask(&o50,s8di,(unsigned char)(m>>37));FOLD(o50);"
            "__builtin_ia32_pmovuswb128mem_mask(&o51,s8hi,(unsigned char)(m>>18));FOLD(o51);"
            "__builtin_ia32_pmovuswb256mem_mask(&o52,s16hi,(unsigned short)(m>>7));FOLD(o52);"
            "__builtin_ia32_pmovuswb512mem_mask(&o53,s32hi,(unsigned)(m>>52));FOLD(o53);return (int)h;}",
        },
        {
            // The masked loads and stores of floating elements, aligned ones and scalars, and the expanding loads, at each
            // width: the loads merge into w under the mask, and the stores keep the bytes past it.
            "x86-loads-stores",
            "int tested(int n){unsigned h=2166136261u;v4sf a0,w0,a6,w6,a18,o18,a24,o24,a36,o36,a38,o38,a52,w52;v8sf a1,w1,a7,w7,a19,o19,a25,o25,a53,w53;"
            "v16sf a2,w2,a8,w8,a20,o20,a26,o26,a54,w54;v2df a3,w3,a9,w9,a21,o21,a27,o27,a37,o37,a39,o39,a55,w55;"
            "v4df a4,w4,a10,w10,a22,o22,a28,o28,a56,w56;v8df a5,w5,a11,w11,a23,o23,a29,o29,a57,w57;v4si a12,w12,a30,o30,a46,w46;"
            "v8si a13,w13,a31,o31,a47,w47;v16si a14,w14,a32,o32,a48,w48;v2di a15,w15,a33,o33,a49,w49;v4di a16,w16,a34,o34,a50,w50;"
            "v8di a17,w17,a35,o35,a51,w51;v16qi a40,w40;v32qi a41,w41;v64qi a42,w42;v8hi a43,w43;v16hi a44,w44;v32hi a45,w45;fill(&a0,16,n+0);"
            "fill(&w0,16,n+1);fill(&a1,32,n+2);fill(&w1,32,n+3);fill(&a2,64,n+4);fill(&w2,64,n+5);fill(&a3,16,n+6);fill(&w3,16,n+7);fill(&a4,32,n+8);"
            "fill(&w4,32,n+9);fill(&a5,64,n+10);fill(&w5,64,n+11);fill(&a6,16,n+12);fill(&w6,16,n+13);fill(&a7,32,n+14);fill(&w7,32,n+15);"
            "fill(&a8,64,n+16);fill(&w8,64,n+17);fill(&a9,16,n+18);fill(&w9,16,n+19);fill(&a10,32,n+20);fill(&w10,32,n+21);fill(&a11,64,n+22);"
            "fill(&w11,64,n+23);fill(&a12,16,n+24);fill(&w12,16,n+25);fill(&a13,32,n+26);fill(&w13,32,n+27);fill(&a14,64,n+28);fill(&w14,64,n+29);"
            "fill(&a15,16,n+30);fill(&w15,16,n+31);fill(&a16,32,n+32);fill(&w16,32,n+33);fill(&a17,64,n+34);fill(&w17,64,n+35);fill(&a18,16,n+36);"
            "fill(&o18,16,n+37);fill(&a19,32,n+38);fill(&o19,32,n+39);fill(&a20,64,n+40);fill(&o20,64,n+41);fill(&a21,16,n+42);fill(&o21,16,n+43);"
            "fill(&a22,32,n+44);fill(&o22,32,n+45);fill(&a23,64,n+46);fill(&o23,64,n+47);fill(&a24,16,n+48);fill(&o24,16,n+49);fill(&a25,32,n+50);"
            "fill(&o25,32,n+51);fill(&a26,64,n+52);fill(&o26,64,n+53);fill(&a27,16,n+54);fill(&o27,16,n+55);fill(&a28,32,n+56);fill(&o28,32,n+57);"
            "fill(&a29,64,n+58);fill(&o29,64,n+59);fill(&a30,16,n+60);fill(&o30,16,n+61);fill(&a31,32,n+62);fill(&o31,32,n+63);fill(&a32,64,n+64);"
            "fill(&o32,64,n+65);fill(&a33,16,n+66);fill(&o33,16,n+67);fill(&a34,32,n+68);fill(&o34,32,n+69);fill(&a35,64,n+70);fill(&o35,64,n+71);"
            "fill(&a36,16,n+72);fill(&o36,16,n+73);fill(&a37,16,n+74);fill(&o37,16,n+75);fill(&a38,16,n+76);fill(&o38,16,n+77);fill(&a39,16,n+78);"
            "fill(&o39,16,n+79);fill(&a40,16,n+80);fill(&w40,16,n+81);fill(&a41,32,n+82);fill(&w41,32,n+83);fill(&a42,64,n+84);fill(&w42,64,n+85);"
            "fill(&a43,16,n+86);fill(&w43,16,n+87);fill(&a44,32,n+88);fill(&w44,32,n+89);fill(&a45,64,n+90);fill(&w45,64,n+91);fill(&a46,16,n+92);"
            "fill(&w46,16,n+93);fill(&a47,32,n+94);fill(&w47,32,n+95);fill(&a48,64,n+96);fill(&w48,64,n+97);fill(&a49,16,n+98);fill(&w49,16,n+99);"
            "fill(&a50,32,n+100);fill(&w50,32,n+101);fill(&a51,64,n+102);fill(&w51,64,n+103);fill(&a52,16,n+104);fill(&w52,16,n+105);fill(&a53,32,n+106);"
            "fill(&w53,32,n+107);fill(&a54,64,n+108);fill(&w54,64,n+109);fill(&a55,16,n+110);fill(&w55,16,n+111);fill(&a56,32,n+112);fill(&w56,32,n+113);"
            "fill(&a57,64,n+114);fill(&w57,64,n+115);unsigned long long m=mask(n);v4sf r0=__builtin_ia32_loadups128_mask(&a0,w0,(unsigned char)m);"
            "FOLD(r0);v8sf r1=__builtin_ia32_loadups256_mask(&a1,w1,(unsigned char)(m>>3));FOLD(r1);"
            "v16sf r2=__builtin_ia32_loadups512_mask((const float*)&a2,w2,(unsigned short)(m>>24));FOLD(r2);"
            "v2df r3=__builtin_ia32_loadupd128_mask(&a3,w3,(unsigned char)(m>>49));FOLD(r3);"
            "v4df r4=__builtin_ia32_loadupd256_mask(&a4,w4,(unsigned char)(m>>41));FOLD(r4);"
            "v8df r5=__builtin_ia32_loadupd512_mask((const double*)&a5,w5,(unsigned char)(m>>46));FOLD(r5);"
            "v4sf r6=__builtin_ia32_loadaps128_mask(&a6,w6,(unsigned char)(m>>20));FOLD(r6);"
            "v8sf r7=__builtin_ia32_loadaps256_mask(&a7,w7,(unsigned char)(m>>21));FOLD(r7);"
            "v16sf r8=__builtin_ia32_loadaps512_mask(&a8,w8,(unsigned short)(m>>28));FOLD(r8);"
            "v2df r9=__builtin_ia32_loadapd128_mask(&a9,w9,(unsigned char)(m>>16));FOLD(r9);"
            "v4df r10=__builtin_ia32_loadapd256_mask(&a10,w10,(unsigned char)(m>>54));FOLD(r10);"
            "v8df r11=__builtin_ia32_loadapd512_mask(&a11,w11,(unsigned char)(m>>15));FOLD(r11);"
            "v4si r12=__builtin_ia32_movdqa32load128_mask(&a12,w12,(unsigned char)(m>>47));FOLD(r12);"
            "v8si r13=__builtin_ia32_movdqa32load256_mask(&a13,w13,(unsigned char)(m>>27));FOLD(r13);"
            "v16si r14=__builtin_ia32_movdqa32load512_mask(&a14,w14,(unsigned short)(m>>9));FOLD(r14);"
            "v2di r15=__builtin_ia32_movdqa64load128_mask(&a15,w15,(unsigned char)(m>>5));FOLD(r15);"
            "v4di r16=__builtin_ia32_movdqa64load256_mask(&a16,w16,(unsigned char)(m>>38));FOLD(r16);"
            "v8di r17=__builtin_ia32_movdqa64load512_mask(&a17,w17,(unsigned char)(m>>25));FOLD(r17);"
            "__builtin_ia32_storeups128_mask(&o18,a18,(unsigned char)(m>>56));FOLD(o18);__builtin_ia32_storeups256_mask(&o19,a19,(unsigned char)(m>>29));"
            "FOLD(o19);__builtin_ia32_storeups512_mask((float*)&o20,a20,(unsigned short)(m>>23));FOLD(o20);"
            "__builtin_ia32_storeupd128_mask(&o21,a21,(unsigned char)(m>>42));FOLD(o21);__builtin_ia32_storeupd256_mask(&o22,a22,(unsigned char)(m>>53));"
            "FOLD(o22);__builtin_ia32_storeupd512_mask((double*)&o23,a23,(unsigned char)(m>>8));FOLD(o23);"
            "__builtin_ia32_storeaps128_mask(&o24,a24,(unsigned char)(m>>59));FOLD(o24);__builtin_ia32_storeaps256_mask(&o25,a25,(unsigned char)(m>>50));"
            "FOLD(o25);__builtin_ia32_storeaps512_mask(&o26,a26,(unsigned short)(m>>48));FOLD(o26);"
            "__builtin_ia32_storeapd128_mask(&o27,a27,(unsigned char)(m>>34));FOLD(o27);__builtin_ia32_storeapd256_mask(&o28,a28,(unsigned char)(m>>58));"
            "FOLD(o28);__builtin_ia32_storeapd512_mask(&o29,a29,(unsigned char)(m>>43));FOLD(o29);"
            "__builtin_ia32_movdqa32store128_mask(&o30,a30,(unsigned char)(m>>60));FOLD(o30);"
            "__builtin_ia32_movdqa32store256_mask(&o31,a31,(unsigned char)(m>>57));FOLD(o31);"
            "__builtin_ia32_movdqa32store512_mask(&o32,a32,(unsigned short)(m>>36));FOLD(o32);"
            "__builtin_ia32_movdqa64store128_mask(&o33,a33,(unsigned char)(m>>11));FOLD(o33);"
            "__builtin_ia32_movdqa64store256_mask(&o34,a34,(unsigned char)(m>>19));FOLD(o34);"
            "__builtin_ia32_movdqa64store512_mask(&o35,a35,(unsigned char)(m>>14));FOLD(o35);"
            "v4sf r36=__builtin_ia32_loadss128_mask(&a36,o36,(unsigned char)(m>>40));FOLD(r36);"
            "v2df r37=__builtin_ia32_loadsd128_mask(&a37,o37,(unsigned char)(m>>39));FOLD(r37);"
            "__builtin_ia32_storess128_mask(&o38,a38,(unsigned char)(m>>32));FOLD(o38);__builtin_ia32_storesd128_mask(&o39,a39,(unsigned char)(m>>44));"
            "FOLD(o39);v16qi r40=__builtin_ia32_expandloadqi128_mask(&a40,w40,(unsigned short)(m>>6));FOLD(r40);"
            "v32qi r41=__builtin_ia32_expandloadqi256_mask(&a41,w41,(unsigned)(m>>45));FOLD(r41);"
            "v64qi r42=__builtin_ia32_expandloadqi512_mask(&a42,w42,(unsigned long long)(m>>13));FOLD(r42);"
            "v8hi r43=__builtin_ia32_expandloadhi128_mask(&a43,w43,(unsigned char)(m>>33));FOLD(r43);"
            "v16hi r44=__builtin_ia32_expandloadhi256_mask(&a44,w44,(unsigned short)(m>>51));FOLD(r44);"
            "v32hi r45=__builtin_ia32_expandloadhi512_mask(&a45,w45,(unsigned)(m>>55));FOLD(r45);"
            "v4si r46=__builtin_ia32_expandloadsi128_mask(&a46,w46,(unsigned char)(m>>22));FOLD(r46);"
            "v8si r47=__builtin_ia32_expandloadsi256_mask(&a47,w47,(unsigned char)(m>>35));FOLD(r47);"
            "v16si r48=__builtin_ia32_expandloadsi512_mask(&a48,w48,(unsigned short)(m>>4));FOLD(r48);"
            "v4si r49=__builtin_ia32_expandloaddi128_mask(&a49,w49,(unsigned char)(m>>31));FOLD(r49);"
            "v4di r50=__builtin_ia32_expandloaddi256_mask(&a50,w50,(unsigned char)(m>>37));FOLD(r50);"
            "v8di r51=__builtin_ia32_expandloaddi512_mask(&a51,w51,(unsigned char)(m>>18));FOLD(r51);"
            "v4sf r52=__builtin_ia32_expandloadsf128_mask(&a52,w52,(unsigned char)(m>>7));FOLD(r52);"
            "v8sf r53=__builtin_ia32_expandloadsf256_mask(&a53,w53,(unsigned char)(m>>52));FOLD(r53);"
            "v16sf r54=__builtin_ia32_expandloadsf512_mask(&a54,w54,(unsigned short)(m>>1));FOLD(r54);"
            "v2df r55=__builtin_ia32_expandloaddf128_mask(&a55,w55,(unsigned char)(m>>10));FOLD(r55);"
            "v4df r56=__builtin_ia32_expandloaddf256_mask(&a56,w56,(unsigned char)(m>>12));FOLD(r56);"
            "v8df r57=__builtin_ia32_expandloaddf512_mask(&a57,w57,(unsigned char)(m>>26));FOLD(r57);return (int)h;}",
        },
        {
            // The floating compresses and expands under a mask, and the compressing stores, at each width.
            "x86-float-compress",
            "int tested(int n){unsigned h=2166136261u;v4sf a0,w0,o0;v8sf a1,w1,o1;v16sf a2,w2,o2;v2df a3,w3,o3;v4df a4,w4,o4;v8df a5,w5,o5;"
            "fill(&a0,16,n+0);fill(&w0,16,n+1);fill(&o0,16,n+2);fill(&a1,32,n+3);fill(&w1,32,n+4);fill(&o1,32,n+5);fill(&a2,64,n+6);fill(&w2,64,n+7);"
            "fill(&o2,64,n+8);fill(&a3,16,n+9);fill(&w3,16,n+10);fill(&o3,16,n+11);fill(&a4,32,n+12);fill(&w4,32,n+13);fill(&o4,32,n+14);"
            "fill(&a5,64,n+15);fill(&w5,64,n+16);fill(&o5,64,n+17);unsigned long long m=mask(n);"
            "v4sf c0=__builtin_ia32_compresssf128_mask(a0,w0,(unsigned char)m),x0=__builtin_ia32_expandsf128_mask(a0,w0,(unsigned char)(m>>3));FOLD(c0);"
            "FOLD(x0);__builtin_ia32_compressstoresf128_mask(&o0,a0,(unsigned char)(m>>24));FOLD(o0);"
            "v8sf c1=__builtin_ia32_compresssf256_mask(a1,w1,(unsigned char)(m>>49)),x1=__builtin_ia32_expandsf256_mask(a1,w1,(unsigned char)(m>>41));"
            "FOLD(c1);FOLD(x1);__builtin_ia32_compressstoresf256_mask(&o1,a1,(unsigned char)(m>>46));FOLD(o1);"
            "v16sf c2=__builtin_ia32_compresssf512_mask(a2,w2,(unsigned short)(m>>20)),x2=__builtin_ia32_expandsf512_mask(a2,w2,(unsigned short)(m>>21));"
            "FOLD(c2);FOLD(x2);__builtin_ia32_compressstoresf512_mask(&o2,a2,(unsigned short)(m>>28));FOLD(o2);"
            "v2df c3=__builtin_ia32_compressdf128_mask(a3,w3,(unsigned char)(m>>16)),x3=__builtin_ia32_expanddf128_mask(a3,w3,(unsigned char)(m>>54));"
            "FOLD(c3);FOLD(x3);__builtin_ia32_compressstoredf128_mask(&o3,a3,(unsigned char)(m>>15));FOLD(o3);"
            "v4df c4=__builtin_ia32_compressdf256_mask(a4,w4,(unsigned char)(m>>47)),x4=__builtin_ia32_expanddf256_mask(a4,w4,(unsigned char)(m>>27));"
            "FOLD(c4);FOLD(x4);__builtin_ia32_compressstoredf256_mask(&o4,a4,(unsigned char)(m>>9));FOLD(o4);"
            "v8df c5=__builtin_ia32_compressdf512_mask(a5,w5,(unsigned char)(m>>5)),x5=__builtin_ia32_expanddf512_mask(a5,w5,(unsigned char)(m>>38));"
            "FOLD(c5);FOLD(x5);__builtin_ia32_compressstoredf512_mask(&o5,a5,(unsigned char)(m>>25));FOLD(o5);return (int)h;}",
        },
        {
            // AVX-512VNNI's dot products of bytes and of words, with and without saturation, at each width.
            "x86-dot-products",
            "int tested(int n){unsigned h=2166136261u;v4si s0,s3,s6,s9;v16qu a0,a3;v16qi b0,b3;v8si s1,s4,s7,s10;v32qu a1,a4;v32qi b1,b4;"
            "v16si s2,s5,s8,s11;v64qu a2,a5;v64qi b2,b5;v8hi a6,b6,a9,b9;v16hi a7,b7,a10,b10;v32hi a8,b8,a11,b11;fill(&s0,16,n+0);fill(&a0,16,n+1);"
            "fill(&b0,16,n+2);fill(&s1,32,n+3);fill(&a1,32,n+4);fill(&b1,32,n+5);fill(&s2,64,n+6);fill(&a2,64,n+7);fill(&b2,64,n+8);fill(&s3,16,n+9);"
            "fill(&a3,16,n+10);fill(&b3,16,n+11);fill(&s4,32,n+12);fill(&a4,32,n+13);fill(&b4,32,n+14);fill(&s5,64,n+15);fill(&a5,64,n+16);"
            "fill(&b5,64,n+17);fill(&s6,16,n+18);fill(&a6,16,n+19);fill(&b6,16,n+20);fill(&s7,32,n+21);fill(&a7,32,n+22);fill(&b7,32,n+23);"
            "fill(&s8,64,n+24);fill(&a8,64,n+25);fill(&b8,64,n+26);fill(&s9,16,n+27);fill(&a9,16,n+28);fill(&b9,16,n+29);fill(&s10,32,n+30);"
            "fill(&a10,32,n+31);fill(&b10,32,n+32);fill(&s11,64,n+33);fill(&a11,64,n+34);fill(&b11,64,n+35);unsigned long long m=mask(n);"
            "v4si r0=__builtin_ia32_vpdpbusd128(s0,a0,b0);FOLD(r0);v8si r1=__builtin_ia32_vpdpbusd256(s1,a1,b1);FOLD(r1);"
            "v16si r2=__builtin_ia32_vpdpbusd512(s2,a2,b2);FOLD(r2);v4si r3=__builtin_ia32_vpdpbusds128(s3,a3,b3);FOLD(r3);"
            "v8si r4=__builtin_ia32_vpdpbusds256(s4,a4,b4);FOLD(r4);v16si r5=__builtin_ia32_vpdpbusds512(s5,a5,b5);FOLD(r5);"
            "v4si r6=__builtin_ia32_vpdpwssd128(s6,a6,b6);FOLD(r6);v8si r7=__builtin_ia32_vpdpwssd256(s7,a7,b7);FOLD(r7);"
            "v16si r8=__builtin_ia32_vpdpwssd512(s8,a8,b8);FOLD(r8);v4si r9=__builtin_ia32_vpdpwssds128(s9,a9,b9);FOLD(r9);"
            "v8si r10=__builtin_ia32_vpdpwssds256(s10,a10,b10);FOLD(r10);v16si r11=__builtin_ia32_vpdpwssds512(s11,a11,b11);FOLD(r11);return (int)h;}",
            // clang's code for znver5 writes the 128- and 256-bit ones in AVX-VNNI's VEX forms, which a host of AVX512-VNNI
            // alone (Zen 4) hasn't; ours are their EVEX forms.
            BUSTER_X86_FEATURE(AVXVNNI),
        },
        {
            // Batch 2's binary rules: each builtin at its prototype's types, by both paths against clang -O0's code for
            // znver5.
            "x86-binary-1",
            "typedef long long x2Oi __attribute__((vector_size(16)));typedef long long x4Oi __attribute__((vector_size(32)));"
            "typedef int x4i __attribute__((vector_size(16)));typedef long long x8Oi __attribute__((vector_size(64)));"
            "typedef unsigned short x8Us __attribute__((vector_size(16)));typedef int x8i __attribute__((vector_size(32)));"
            "typedef short x8s __attribute__((vector_size(16)));typedef unsigned char x16Uc __attribute__((vector_size(16)));"
            "typedef unsigned short x16Us __attribute__((vector_size(32)));typedef char x16c __attribute__((vector_size(16)));"
            "typedef int x16i __attribute__((vector_size(64)));typedef short x16s __attribute__((vector_size(32)));"
            "typedef unsigned char x32Uc __attribute__((vector_size(32)));typedef unsigned short x32Us __attribute__((vector_size(64)));"
            "typedef char x32c __attribute__((vector_size(32)));typedef short x32s __attribute__((vector_size(64)));"
            "typedef unsigned char x64Uc __attribute__((vector_size(64)));typedef char x64c __attribute__((vector_size(64)));"
            "int tested(int n){unsigned h=2166136261u;x4i a0_0;fill(&a0_0,16,n+63);x4i a0_1;fill(&a0_1,16,n+92);x8i a1_0;fill(&a1_0,32,n+24);x8i a1_1;"
            "fill(&a1_1,32,n+53);x16i a2_0;fill(&a2_0,64,n+82);x16i a2_1;fill(&a2_1,64,n+15);x8s a3_0;fill(&a3_0,16,n+45);x8s a3_1;fill(&a3_1,16,n+74);"
            "x16s a4_0;fill(&a4_0,32,n+6);x16s a4_1;fill(&a4_1,32,n+35);x32s a5_0;fill(&a5_0,64,n+65);x32s a5_1;fill(&a5_1,64,n+95);x8s a6_0;"
            "fill(&a6_0,16,n+28);x8s a6_1;fill(&a6_1,16,n+57);x16s a7_0;fill(&a7_0,32,n+86);x16s a7_1;fill(&a7_1,32,n+19);x32s a8_0;fill(&a8_0,64,n+49);"
            "x32s a8_1;fill(&a8_1,64,n+79);x16Uc a9_0;fill(&a9_0,16,n+12);x16Uc a9_1;fill(&a9_1,16,n+43);x32Uc a10_0;fill(&a10_0,32,n+74);x32Uc a10_1;"
            "fill(&a10_1,32,n+10);x64Uc a11_0;fill(&a11_0,64,n+43);x64Uc a11_1;fill(&a11_1,64,n+76);x8Us a12_0;fill(&a12_0,16,n+12);x8Us a12_1;"
            "fill(&a12_1,16,n+44);x16Us a13_0;fill(&a13_0,32,n+76);x16Us a13_1;fill(&a13_1,32,n+12);x32Us a14_0;fill(&a14_0,64,n+45);x32Us a14_1;"
            "fill(&a14_1,64,n+78);x4i a15_0;fill(&a15_0,16,n+14);x4i a15_1;fill(&a15_1,16,n+45);x8i a16_0;fill(&a16_0,32,n+76);x8i a16_1;"
            "fill(&a16_1,32,n+10);x16i a17_0;fill(&a17_0,64,n+41);x16i a17_1;fill(&a17_1,64,n+73);x8s a18_0;fill(&a18_0,16,n+8);x8s a18_1;"
            "fill(&a18_1,16,n+38);x16s a19_0;fill(&a19_0,32,n+69);x16s a19_1;fill(&a19_1,32,n+4);x32s a20_0;fill(&a20_0,64,n+35);x32s a20_1;"
            "fill(&a20_1,64,n+67);x8Us a21_0;fill(&a21_0,16,n+2);x8Us a21_1;fill(&a21_1,16,n+33);x16Us a22_0;fill(&a22_0,32,n+65);x16Us a22_1;"
            "fill(&a22_1,32,n+1);x32Us a23_0;fill(&a23_0,64,n+33);x32Us a23_1;fill(&a23_1,64,n+66);x8s a24_0;fill(&a24_0,16,n+2);x8s a24_1;"
            "fill(&a24_1,16,n+32);x16s a25_0;fill(&a25_0,32,n+63);x16s a25_1;fill(&a25_1,32,n+95);x32s a26_0;fill(&a26_0,64,n+30);x32s a26_1;"
            "fill(&a26_1,64,n+62);x4i a27_0;fill(&a27_0,16,n+94);x4i a27_1;fill(&a27_1,16,n+28);x8i a28_0;fill(&a28_0,32,n+59);x8i a28_1;"
            "fill(&a28_1,32,n+90);x16Uc a29_0;fill(&a29_0,16,n+24);x16Uc a29_1;fill(&a29_1,16,n+57);x32Uc a30_0;fill(&a30_0,32,n+90);x32Uc a30_1;"
            "fill(&a30_1,32,n+26);x16c a31_0;fill(&a31_0,16,n+59);x16c a31_1;fill(&a31_1,16,n+91);x32c a32_0;fill(&a32_0,32,n+26);x32c a32_1;"
            "fill(&a32_1,32,n+58);x2Oi a33_0;fill(&a33_0,16,n+90);x2Oi a33_1;fill(&a33_1,16,n+25);for(int i=0;i<2;i++)a33_1[i]&=127;x4Oi a34_0;"
            "fill(&a34_0,32,n+91);x4Oi a34_1;fill(&a34_1,32,n+26);for(int i=0;i<4;i++)a34_1[i]&=127;x16s a35_0;fill(&a35_0,32,n+92);x16s a35_1;"
            "fill(&a35_1,32,n+27);for(int i=0;i<16;i++)a35_1[i]&=31;x32s a36_0;fill(&a36_0,64,n+93);x32s a36_1;fill(&a36_1,64,n+28);for(int i=0;i<32;"
            "i++)a36_1[i]&=31;x16s a37_0;fill(&a37_0,32,n+94);x16s a37_1;fill(&a37_1,32,n+29);for(int i=0;i<16;i++)a37_1[i]&=31;x32s a38_0;"
            "fill(&a38_0,64,n+95);x32s a38_1;fill(&a38_1,64,n+30);for(int i=0;i<32;i++)a38_1[i]&=31;x16s a39_0;fill(&a39_0,32,n+96);x16s a39_1;"
            "fill(&a39_1,32,n+31);for(int i=0;i<16;i++)a39_1[i]&=31;unsigned long long m=mask(n);{x8s r=__builtin_ia32_packssdw128(a0_0,a0_1);FOLD(r);}"
            "{x16s r=__builtin_ia32_packssdw256(a1_0,a1_1);FOLD(r);}{x32s r=__builtin_ia32_packssdw512(a2_0,a2_1);FOLD(r);}"
            "{x16c r=__builtin_ia32_packsswb128(a3_0,a3_1);FOLD(r);}{x32c r=__builtin_ia32_packsswb256(a4_0,a4_1);FOLD(r);}"
            "{x64c r=__builtin_ia32_packsswb512(a5_0,a5_1);FOLD(r);}{x16c r=__builtin_ia32_packuswb128(a6_0,a6_1);FOLD(r);}"
            "{x32c r=__builtin_ia32_packuswb256(a7_0,a7_1);FOLD(r);}{x64c r=__builtin_ia32_packuswb512(a8_0,a8_1);FOLD(r);}"
            "{x16Uc r=__builtin_ia32_pavgb128(a9_0,a9_1);FOLD(r);}{x32Uc r=__builtin_ia32_pavgb256(a10_0,a10_1);FOLD(r);}"
            "{x64Uc r=__builtin_ia32_pavgb512(a11_0,a11_1);FOLD(r);}{x8Us r=__builtin_ia32_pavgw128(a12_0,a12_1);FOLD(r);}"
            "{x16Us r=__builtin_ia32_pavgw256(a13_0,a13_1);FOLD(r);}{x32Us r=__builtin_ia32_pavgw512(a14_0,a14_1);FOLD(r);}"
            "{x2Oi r=__builtin_ia32_pmuldq128(a15_0,a15_1);FOLD(r);}{x4Oi r=__builtin_ia32_pmuldq256(a16_0,a16_1);FOLD(r);}"
            "{x8Oi r=__builtin_ia32_pmuldq512(a17_0,a17_1);FOLD(r);}{x8s r=__builtin_ia32_pmulhrsw128(a18_0,a18_1);FOLD(r);}"
            "{x16s r=__builtin_ia32_pmulhrsw256(a19_0,a19_1);FOLD(r);}{x32s r=__builtin_ia32_pmulhrsw512(a20_0,a20_1);FOLD(r);}"
            "{x8Us r=__builtin_ia32_pmulhuw128(a21_0,a21_1);FOLD(r);}{x16Us r=__builtin_ia32_pmulhuw256(a22_0,a22_1);FOLD(r);}"
            "{x32Us r=__builtin_ia32_pmulhuw512(a23_0,a23_1);FOLD(r);}{x8s r=__builtin_ia32_pmulhw128(a24_0,a24_1);FOLD(r);}"
            "{x16s r=__builtin_ia32_pmulhw256(a25_0,a25_1);FOLD(r);}{x32s r=__builtin_ia32_pmulhw512(a26_0,a26_1);FOLD(r);}"
            "{x2Oi r=__builtin_ia32_pmuludq128(a27_0,a27_1);FOLD(r);}{x4Oi r=__builtin_ia32_pmuludq256(a28_0,a28_1);FOLD(r);}"
            "{x2Oi r=__builtin_ia32_psadbw128(a29_0,a29_1);FOLD(r);}{x4Oi r=__builtin_ia32_psadbw256(a30_0,a30_1);FOLD(r);}"
            "{x16c r=__builtin_ia32_vpmultishiftqb128(a31_0,a31_1);FOLD(r);}{x32c r=__builtin_ia32_vpmultishiftqb256(a32_0,a32_1);FOLD(r);}"
            "{x2Oi r=__builtin_ia32_psravq128(a33_0,a33_1);FOLD(r);}{x4Oi r=__builtin_ia32_psravq256(a34_0,a34_1);FOLD(r);}"
            "{x16s r=__builtin_ia32_psllv16hi(a35_0,a35_1);FOLD(r);}{x32s r=__builtin_ia32_psllv32hi(a36_0,a36_1);FOLD(r);}"
            "{x16s r=__builtin_ia32_psrav16hi(a37_0,a37_1);FOLD(r);}{x32s r=__builtin_ia32_psrav32hi(a38_0,a38_1);FOLD(r);}"
            "{x16s r=__builtin_ia32_psrlv16hi(a39_0,a39_1);FOLD(r);}return (int)h;}",
        },
        {
            // Batch 2's binary rules: each builtin at its prototype's types, by both paths against clang -O0's code for
            // znver5.
            "x86-binary-2",
            "typedef long long x2Oi __attribute__((vector_size(16)));typedef double x2d __attribute__((vector_size(16)));"
            "typedef long long x4Oi __attribute__((vector_size(32)));typedef double x4d __attribute__((vector_size(32)));"
            "typedef float x4f __attribute__((vector_size(16)));typedef int x4i __attribute__((vector_size(16)));"
            "typedef long long x8Oi __attribute__((vector_size(64)));typedef double x8d __attribute__((vector_size(64)));"
            "typedef float x8f __attribute__((vector_size(32)));typedef int x8i __attribute__((vector_size(32)));"
            "typedef short x8s __attribute__((vector_size(16)));typedef __bf16 x8y __attribute__((vector_size(16)));"
            "typedef char x16c __attribute__((vector_size(16)));typedef float x16f __attribute__((vector_size(64)));"
            "typedef int x16i __attribute__((vector_size(64)));typedef __bf16 x16y __attribute__((vector_size(32)));"
            "typedef char x32c __attribute__((vector_size(32)));typedef short x32s __attribute__((vector_size(64)));"
            "typedef __bf16 x32y __attribute__((vector_size(64)));typedef char x64c __attribute__((vector_size(64)));"
            "int tested(int n){unsigned h=2166136261u;x32s a0_0;fill(&a0_0,64,n+14);x32s a0_1;fill(&a0_1,64,n+44);for(int i=0;i<32;i++)a0_1[i]&=31;"
            "x4Oi a1_0;fill(&a1_0,32,n+10);x4Oi a1_1;fill(&a1_1,32,n+40);for(int i=0;i<4;i++)a1_1[i]&=127;x8i a2_0;fill(&a2_0,32,n+6);x8i a2_1;"
            "fill(&a2_1,32,n+34);for(int i=0;i<8;i++)a2_1[i]&=63;x8i a3_0;fill(&a3_0,32,n+95);x8i a3_1;fill(&a3_1,32,n+27);for(int i=0;i<8;"
            "i++)a3_1[i]&=63;x4Oi a4_0;fill(&a4_0,32,n+88);x4Oi a4_1;fill(&a4_1,32,n+21);for(int i=0;i<4;i++)a4_1[i]&=127;x8i a5_0;fill(&a5_0,32,n+84);"
            "x8i a5_1;fill(&a5_1,32,n+16);for(int i=0;i<8;i++)a5_1[i]&=63;x32c a6_0;fill(&a6_0,32,n+77);x32c a6_1;fill(&a6_1,32,n+10);x64c a7_0;"
            "fill(&a7_0,64,n+40);x64c a7_1;fill(&a7_1,64,n+70);x8s a8_0;fill(&a8_0,16,n+3);x8s a8_1;fill(&a8_1,16,n+31);for(int i=0;i<8;i++)a8_1[i]&=31;"
            "x8s a9_0;fill(&a9_0,16,n+92);x8s a9_1;fill(&a9_1,16,n+24);for(int i=0;i<8;i++)a9_1[i]&=31;x8s a10_0;fill(&a10_0,16,n+85);x8s a10_1;"
            "fill(&a10_1,16,n+19);for(int i=0;i<8;i++)a10_1[i]&=31;x2Oi a11_0;fill(&a11_0,16,n+83);x2Oi a11_1;fill(&a11_1,16,n+18);for(int i=0;i<2;"
            "i++)a11_1[i]&=127;x4i a12_0;fill(&a12_0,16,n+84);x4i a12_1;fill(&a12_1,16,n+18);for(int i=0;i<4;i++)a12_1[i]&=63;x4i a13_0;"
            "fill(&a13_0,16,n+82);x4i a13_1;fill(&a13_1,16,n+16);for(int i=0;i<4;i++)a13_1[i]&=63;x2Oi a14_0;fill(&a14_0,16,n+80);x2Oi a14_1;"
            "fill(&a14_1,16,n+15);for(int i=0;i<2;i++)a14_1[i]&=127;x4i a15_0;fill(&a15_0,16,n+81);x4i a15_1;fill(&a15_1,16,n+15);for(int i=0;i<4;"
            "i++)a15_1[i]&=63;x16c a16_0;fill(&a16_0,16,n+79);x16c a16_1;fill(&a16_1,16,n+14);x2Oi a17_0;fill(&a17_0,16,n+46);x2Oi a17_1;"
            "fill(&a17_1,16,n+78);x4Oi a18_0;fill(&a18_0,32,n+13);x4Oi a18_1;fill(&a18_1,32,n+45);x8Oi a19_0;fill(&a19_0,64,n+77);x8Oi a19_1;"
            "fill(&a19_1,64,n+12);x2Oi a20_0;fill(&a20_0,16,n+44);x2Oi a20_1;fill(&a20_1,16,n+76);x4Oi a21_0;fill(&a21_0,32,n+11);x4Oi a21_1;"
            "fill(&a21_1,32,n+43);x8Oi a22_0;fill(&a22_0,64,n+75);x8Oi a22_1;fill(&a22_1,64,n+10);x4Oi a23_0;fill(&a23_0,32,n+42);x4Oi a23_1;"
            "fill(&a23_1,32,n+74);x4f a24_0;fill(&a24_0,16,n+9);a24_0[0]=(float)n*1.25f;a24_0[3]=-(float)n*3.5f+0.375f;x4f a24_1;fill(&a24_1,16,n+94);"
            "a24_1[0]=(float)n*1.25f;a24_1[3]=-(float)n*3.5f+0.375f;x8f a25_0;fill(&a25_0,32,n+83);a25_0[0]=(float)n*1.25f;"
            "a25_0[7]=-(float)n*3.5f+0.375f;x8f a25_1;fill(&a25_1,32,n+72);a25_1[0]=(float)n*1.25f;a25_1[7]=-(float)n*3.5f+0.375f;x16f a26_0;"
            "fill(&a26_0,64,n+61);a26_0[0]=(float)n*1.25f;a26_0[15]=-(float)n*3.5f+0.375f;x16f a26_1;fill(&a26_1,64,n+52);a26_1[0]=(float)n*1.25f;"
            "a26_1[15]=-(float)n*3.5f+0.375f;x4d a27_0;fill(&a27_0,32,n+43);a27_0[0]=(double)n*1.25;a27_0[3]=-(double)n*3.5+0.375;x4Oi a27_1;"
            "fill(&a27_1,32,n+31);x8d a28_0;fill(&a28_0,64,n+63);a28_0[0]=(double)n*1.25;a28_0[7]=-(double)n*3.5+0.375;x8Oi a28_1;fill(&a28_1,64,n+51);"
            "x8f a29_0;fill(&a29_0,32,n+83);a29_0[0]=(float)n*1.25f;a29_0[7]=-(float)n*3.5f+0.375f;x8i a29_1;fill(&a29_1,32,n+72);x16f a30_0;"
            "fill(&a30_0,64,n+6);a30_0[0]=(float)n*1.25f;a30_0[15]=-(float)n*3.5f+0.375f;x16i a30_1;fill(&a30_1,64,n+93);x4Oi a31_0;fill(&a31_0,32,n+28);"
            "x4Oi a31_1;fill(&a31_1,32,n+60);x2d a32_0;fill(&a32_0,16,n+92);a32_0[0]=(double)n*1.25;a32_0[1]=-(double)n*3.5+0.375;x2Oi a32_1;"
            "fill(&a32_1,16,n+80);x4d a33_0;fill(&a33_0,32,n+15);a33_0[0]=(double)n*1.25;a33_0[3]=-(double)n*3.5+0.375;x4Oi a33_1;fill(&a33_1,32,n+3);"
            "x8d a34_0;fill(&a34_0,64,n+34);a34_0[0]=(double)n*1.25;a34_0[7]=-(double)n*3.5+0.375;x8Oi a34_1;fill(&a34_1,64,n+22);x4f a35_0;"
            "fill(&a35_0,16,n+54);a35_0[0]=(float)n*1.25f;a35_0[3]=-(float)n*3.5f+0.375f;x4i a35_1;fill(&a35_1,16,n+43);x8f a36_0;fill(&a36_0,32,n+74);"
            "a36_0[0]=(float)n*1.25f;a36_0[7]=-(float)n*3.5f+0.375f;x8i a36_1;fill(&a36_1,32,n+63);x16f a37_0;fill(&a37_0,64,n+94);"
            "a37_0[0]=(float)n*1.25f;a37_0[15]=-(float)n*3.5f+0.375f;x16i a37_1;fill(&a37_1,64,n+85);unsigned long long m=mask(n);"
            "{x32s r=__builtin_ia32_psrlv32hi(a0_0,a0_1);FOLD(r);}{x4Oi r=__builtin_ia32_psllv4di(a1_0,a1_1);FOLD(r);}"
            "{x8i r=__builtin_ia32_psllv8si(a2_0,a2_1);FOLD(r);}{x8i r=__builtin_ia32_psrav8si(a3_0,a3_1);FOLD(r);}"
            "{x4Oi r=__builtin_ia32_psrlv4di(a4_0,a4_1);FOLD(r);}{x8i r=__builtin_ia32_psrlv8si(a5_0,a5_1);FOLD(r);}"
            "{x32c r=__builtin_ia32_vgf2p8mulb_v32qi(a6_0,a6_1);FOLD(r);}{x64c r=__builtin_ia32_vgf2p8mulb_v64qi(a7_0,a7_1);FOLD(r);}"
            "{x8s r=__builtin_ia32_psllv8hi(a8_0,a8_1);FOLD(r);}{x8s r=__builtin_ia32_psrav8hi(a9_0,a9_1);FOLD(r);}"
            "{x8s r=__builtin_ia32_psrlv8hi(a10_0,a10_1);FOLD(r);}{x2Oi r=__builtin_ia32_psllv2di(a11_0,a11_1);FOLD(r);}"
            "{x4i r=__builtin_ia32_psllv4si(a12_0,a12_1);FOLD(r);}{x4i r=__builtin_ia32_psrav4si(a13_0,a13_1);FOLD(r);}"
            "{x2Oi r=__builtin_ia32_psrlv2di(a14_0,a14_1);FOLD(r);}{x4i r=__builtin_ia32_psrlv4si(a15_0,a15_1);FOLD(r);}"
            "{x16c r=__builtin_ia32_vgf2p8mulb_v16qi(a16_0,a16_1);FOLD(r);}{x2Oi r=__builtin_ia32_aesdec128(a17_0,a17_1);FOLD(r);}"
            "{x4Oi r=__builtin_ia32_aesdec256(a18_0,a18_1);FOLD(r);}{x8Oi r=__builtin_ia32_aesdec512(a19_0,a19_1);FOLD(r);}"
            "{x2Oi r=__builtin_ia32_aesdeclast128(a20_0,a20_1);FOLD(r);}{x4Oi r=__builtin_ia32_aesdeclast256(a21_0,a21_1);FOLD(r);}"
            "{x8Oi r=__builtin_ia32_aesdeclast512(a22_0,a22_1);FOLD(r);}{x4Oi r=__builtin_ia32_aesenclast256(a23_0,a23_1);FOLD(r);}"
            "{x8y r=__builtin_ia32_cvtne2ps2bf16_128(a24_0,a24_1);FOLD(r);}{x16y r=__builtin_ia32_cvtne2ps2bf16_256(a25_0,a25_1);FOLD(r);}"
            "{x32y r=__builtin_ia32_cvtne2ps2bf16_512(a26_0,a26_1);FOLD(r);}{x4d r=__builtin_ia32_permvardf256(a27_0,a27_1);FOLD(r);}"
            "{x8d r=__builtin_ia32_permvardf512(a28_0,a28_1);FOLD(r);}{x8f r=__builtin_ia32_permvarsf256(a29_0,a29_1);FOLD(r);}"
            "{x16f r=__builtin_ia32_permvarsf512(a30_0,a30_1);FOLD(r);}{x4Oi r=__builtin_ia32_permvardi256(a31_0,a31_1);FOLD(r);}"
            "{x2d r=__builtin_ia32_vpermilvarpd(a32_0,a32_1);FOLD(r);}{x4d r=__builtin_ia32_vpermilvarpd256(a33_0,a33_1);FOLD(r);}"
            "{x8d r=__builtin_ia32_vpermilvarpd512(a34_0,a34_1);FOLD(r);}{x4f r=__builtin_ia32_vpermilvarps(a35_0,a35_1);FOLD(r);}"
            "{x8f r=__builtin_ia32_vpermilvarps256(a36_0,a36_1);FOLD(r);}{x16f r=__builtin_ia32_vpermilvarps512(a37_0,a37_1);FOLD(r);}return (int)h;}",
        },
        {
            // Batch 2's sse rules: each builtin at its prototype's types, by both paths against clang -O0's code for znver5.
            "x86-sse-1",
            "typedef double x2d __attribute__((vector_size(16)));typedef double x4d __attribute__((vector_size(32)));"
            "typedef float x4f __attribute__((vector_size(16)));typedef int x4i __attribute__((vector_size(16)));"
            "typedef float x8f __attribute__((vector_size(32)));typedef int x8i __attribute__((vector_size(32)));"
            "typedef short x8s __attribute__((vector_size(16)));typedef char x16c __attribute__((vector_size(16)));"
            "typedef short x16s __attribute__((vector_size(32)));typedef char x32c __attribute__((vector_size(32)));"
            "int tested(int n){unsigned h=2166136261u;x2d a0_0;fill(&a0_0,16,n+65);a0_0[0]=(double)n*1.25;a0_0[1]=-(double)n*3.5+0.375;x2d a0_1;"
            "fill(&a0_1,16,n+49);a0_1[0]=(double)n*1.25;a0_1[1]=-(double)n*3.5+0.375;x4d a1_0;fill(&a1_0,32,n+33);a1_0[0]=(double)n*1.25;"
            "a1_0[3]=-(double)n*3.5+0.375;x4d a1_1;fill(&a1_1,32,n+17);a1_1[0]=(double)n*1.25;a1_1[3]=-(double)n*3.5+0.375;x4f a2_0;fill(&a2_0,16,n+1);"
            "a2_0[0]=(float)n*1.25f;a2_0[3]=-(float)n*3.5f+0.375f;x4f a2_1;fill(&a2_1,16,n+82);a2_1[0]=(float)n*1.25f;a2_1[3]=-(float)n*3.5f+0.375f;"
            "x8f a3_0;fill(&a3_0,32,n+67);a3_0[0]=(float)n*1.25f;a3_0[7]=-(float)n*3.5f+0.375f;x8f a3_1;fill(&a3_1,32,n+52);a3_1[0]=(float)n*1.25f;"
            "a3_1[7]=-(float)n*3.5f+0.375f;x2d a4_0;fill(&a4_0,16,n+37);a4_0[0]=(double)n*1.25;a4_0[1]=-(double)n*3.5+0.375;x2d a4_1;fill(&a4_1,16,n+21);"
            "a4_1[0]=(double)n*1.25;a4_1[1]=-(double)n*3.5+0.375;x4d a5_0;fill(&a5_0,32,n+5);a5_0[0]=(double)n*1.25;a5_0[3]=-(double)n*3.5+0.375;"
            "x4d a5_1;fill(&a5_1,32,n+85);a5_1[0]=(double)n*1.25;a5_1[3]=-(double)n*3.5+0.375;x4f a6_0;fill(&a6_0,16,n+69);a6_0[0]=(float)n*1.25f;"
            "a6_0[3]=-(float)n*3.5f+0.375f;x4f a6_1;fill(&a6_1,16,n+54);a6_1[0]=(float)n*1.25f;a6_1[3]=-(float)n*3.5f+0.375f;x8f a7_0;"
            "fill(&a7_0,32,n+39);a7_0[0]=(float)n*1.25f;a7_0[7]=-(float)n*3.5f+0.375f;x8f a7_1;fill(&a7_1,32,n+24);a7_1[0]=(float)n*1.25f;"
            "a7_1[7]=-(float)n*3.5f+0.375f;x2d a8_0;fill(&a8_0,16,n+9);a8_0[0]=(double)n*1.25;a8_0[1]=-(double)n*3.5+0.375;x2d a8_1;fill(&a8_1,16,n+89);"
            "a8_1[0]=(double)n*1.25;a8_1[1]=-(double)n*3.5+0.375;x4d a9_0;fill(&a9_0,32,n+73);a9_0[0]=(double)n*1.25;a9_0[3]=-(double)n*3.5+0.375;"
            "x4d a9_1;fill(&a9_1,32,n+57);a9_1[0]=(double)n*1.25;a9_1[3]=-(double)n*3.5+0.375;x4f a10_0;fill(&a10_0,16,n+41);a10_0[0]=(float)n*1.25f;"
            "a10_0[3]=-(float)n*3.5f+0.375f;x4f a10_1;fill(&a10_1,16,n+30);a10_1[0]=(float)n*1.25f;a10_1[3]=-(float)n*3.5f+0.375f;x8f a11_0;"
            "fill(&a11_0,32,n+19);a11_0[0]=(float)n*1.25f;a11_0[7]=-(float)n*3.5f+0.375f;x8f a11_1;fill(&a11_1,32,n+8);a11_1[0]=(float)n*1.25f;"
            "a11_1[7]=-(float)n*3.5f+0.375f;x2d a12_0;fill(&a12_0,16,n+93);a12_0[0]=(double)n*1.25;a12_0[1]=-(double)n*3.5+0.375;x2d a12_1;"
            "fill(&a12_1,16,n+81);a12_1[0]=(double)n*1.25;a12_1[1]=-(double)n*3.5+0.375;x4d a13_0;fill(&a13_0,32,n+69);a13_0[0]=(double)n*1.25;"
            "a13_0[3]=-(double)n*3.5+0.375;x4d a13_1;fill(&a13_1,32,n+57);a13_1[0]=(double)n*1.25;a13_1[3]=-(double)n*3.5+0.375;x4f a14_0;"
            "fill(&a14_0,16,n+45);a14_0[0]=(float)n*1.25f;a14_0[3]=-(float)n*3.5f+0.375f;x4f a14_1;fill(&a14_1,16,n+34);a14_1[0]=(float)n*1.25f;"
            "a14_1[3]=-(float)n*3.5f+0.375f;x8f a15_0;fill(&a15_0,32,n+23);a15_0[0]=(float)n*1.25f;a15_0[7]=-(float)n*3.5f+0.375f;x8f a15_1;"
            "fill(&a15_1,32,n+12);a15_1[0]=(float)n*1.25f;a15_1[7]=-(float)n*3.5f+0.375f;x2d a16_0;fill(&a16_0,16,n+1);a16_0[0]=(double)n*1.25;"
            "a16_0[1]=-(double)n*3.5+0.375;x2d a16_1;fill(&a16_1,16,n+85);a16_1[0]=(double)n*1.25;a16_1[1]=-(double)n*3.5+0.375;x4d a17_0;"
            "fill(&a17_0,32,n+73);a17_0[0]=(double)n*1.25;a17_0[3]=-(double)n*3.5+0.375;x4d a17_1;fill(&a17_1,32,n+61);a17_1[0]=(double)n*1.25;"
            "a17_1[3]=-(double)n*3.5+0.375;x4f a18_0;fill(&a18_0,16,n+49);a18_0[0]=(float)n*1.25f;a18_0[3]=-(float)n*3.5f+0.375f;x4f a18_1;"
            "fill(&a18_1,16,n+38);a18_1[0]=(float)n*1.25f;a18_1[3]=-(float)n*3.5f+0.375f;x8f a19_0;fill(&a19_0,32,n+27);a19_0[0]=(float)n*1.25f;"
            "a19_0[7]=-(float)n*3.5f+0.375f;x8f a19_1;fill(&a19_1,32,n+16);a19_1[0]=(float)n*1.25f;a19_1[7]=-(float)n*3.5f+0.375f;x4i a20_0;"
            "fill(&a20_0,16,n+5);x4i a20_1;fill(&a20_1,16,n+35);x8i a21_0;fill(&a21_0,32,n+66);x8i a21_1;fill(&a21_1,32,n+0);x8s a22_0;"
            "fill(&a22_0,16,n+30);x8s a22_1;fill(&a22_1,16,n+61);x16s a23_0;fill(&a23_0,32,n+92);x16s a23_1;fill(&a23_1,32,n+27);x8s a24_0;"
            "fill(&a24_0,16,n+59);x8s a24_1;fill(&a24_1,16,n+90);x16s a25_0;fill(&a25_0,32,n+24);x16s a25_1;fill(&a25_1,32,n+56);x4i a26_0;"
            "fill(&a26_0,16,n+88);x4i a26_1;fill(&a26_1,16,n+22);x8i a27_0;fill(&a27_0,32,n+53);x8i a27_1;fill(&a27_1,32,n+84);x8s a28_0;"
            "fill(&a28_0,16,n+18);x8s a28_1;fill(&a28_1,16,n+49);x16s a29_0;fill(&a29_0,32,n+80);x16s a29_1;fill(&a29_1,32,n+15);x8s a30_0;"
            "fill(&a30_0,16,n+47);x8s a30_1;fill(&a30_1,16,n+78);x16s a31_0;fill(&a31_0,32,n+12);x16s a31_1;fill(&a31_1,32,n+44);x16c a32_0;"
            "fill(&a32_0,16,n+76);x16c a32_1;fill(&a32_1,16,n+11);x32c a33_0;fill(&a33_0,32,n+43);x32c a33_1;fill(&a33_1,32,n+75);x8s a34_0;"
            "fill(&a34_0,16,n+10);x8s a34_1;fill(&a34_1,16,n+41);x16s a35_0;fill(&a35_0,32,n+72);x16s a35_1;fill(&a35_1,32,n+7);x4i a36_0;"
            "fill(&a36_0,16,n+38);x4i a36_1;fill(&a36_1,16,n+69);x8i a37_0;fill(&a37_0,32,n+3);x8i a37_1;fill(&a37_1,32,n+33);x2d a38_0;"
            "fill(&a38_0,16,n+64);a38_0[0]=(double)n*1.25;a38_0[1]=-(double)n*3.5+0.375;x2d a38_1;fill(&a38_1,16,n+52);a38_1[0]=(double)n*1.25;"
            "a38_1[1]=-(double)n*3.5+0.375;x4f a39_0;fill(&a39_0,16,n+40);a39_0[0]=(float)n*1.25f;a39_0[3]=-(float)n*3.5f+0.375f;x4f a39_1;"
            "fill(&a39_1,16,n+29);a39_1[0]=(float)n*1.25f;a39_1[3]=-(float)n*3.5f+0.375f;unsigned long long m=mask(n);"
            "{x2d r=__builtin_ia32_addsubpd(a0_0,a0_1);FOLD(r);}{x4d r=__builtin_ia32_addsubpd256(a1_0,a1_1);FOLD(r);}"
            "{x4f r=__builtin_ia32_addsubps(a2_0,a2_1);FOLD(r);}{x8f r=__builtin_ia32_addsubps256(a3_0,a3_1);FOLD(r);}"
            "{x2d r=__builtin_ia32_haddpd(a4_0,a4_1);FOLD(r);}{x4d r=__builtin_ia32_haddpd256(a5_0,a5_1);FOLD(r);}"
            "{x4f r=__builtin_ia32_haddps(a6_0,a6_1);FOLD(r);}{x8f r=__builtin_ia32_haddps256(a7_0,a7_1);FOLD(r);}"
            "{x2d r=__builtin_ia32_hsubpd(a8_0,a8_1);FOLD(r);}{x4d r=__builtin_ia32_hsubpd256(a9_0,a9_1);FOLD(r);}"
            "{x4f r=__builtin_ia32_hsubps(a10_0,a10_1);FOLD(r);}{x8f r=__builtin_ia32_hsubps256(a11_0,a11_1);FOLD(r);}"
            "{x2d r=__builtin_ia32_maxpd(a12_0,a12_1);FOLD(r);}{x4d r=__builtin_ia32_maxpd256(a13_0,a13_1);FOLD(r);}"
            "{x4f r=__builtin_ia32_maxps(a14_0,a14_1);FOLD(r);}{x8f r=__builtin_ia32_maxps256(a15_0,a15_1);FOLD(r);}"
            "{x2d r=__builtin_ia32_minpd(a16_0,a16_1);FOLD(r);}{x4d r=__builtin_ia32_minpd256(a17_0,a17_1);FOLD(r);}"
            "{x4f r=__builtin_ia32_minps(a18_0,a18_1);FOLD(r);}{x8f r=__builtin_ia32_minps256(a19_0,a19_1);FOLD(r);}"
            "{x4i r=__builtin_ia32_phaddd128(a20_0,a20_1);FOLD(r);}{x8i r=__builtin_ia32_phaddd256(a21_0,a21_1);FOLD(r);}"
            "{x8s r=__builtin_ia32_phaddw128(a22_0,a22_1);FOLD(r);}{x16s r=__builtin_ia32_phaddw256(a23_0,a23_1);FOLD(r);}"
            "{x8s r=__builtin_ia32_phaddsw128(a24_0,a24_1);FOLD(r);}{x16s r=__builtin_ia32_phaddsw256(a25_0,a25_1);FOLD(r);}"
            "{x4i r=__builtin_ia32_phsubd128(a26_0,a26_1);FOLD(r);}{x8i r=__builtin_ia32_phsubd256(a27_0,a27_1);FOLD(r);}"
            "{x8s r=__builtin_ia32_phsubw128(a28_0,a28_1);FOLD(r);}{x16s r=__builtin_ia32_phsubw256(a29_0,a29_1);FOLD(r);}"
            "{x8s r=__builtin_ia32_phsubsw128(a30_0,a30_1);FOLD(r);}{x16s r=__builtin_ia32_phsubsw256(a31_0,a31_1);FOLD(r);}"
            "{x16c r=__builtin_ia32_psignb128(a32_0,a32_1);FOLD(r);}{x32c r=__builtin_ia32_psignb256(a33_0,a33_1);FOLD(r);}"
            "{x8s r=__builtin_ia32_psignw128(a34_0,a34_1);FOLD(r);}{x16s r=__builtin_ia32_psignw256(a35_0,a35_1);FOLD(r);}"
            "{x4i r=__builtin_ia32_psignd128(a36_0,a36_1);FOLD(r);}{x8i r=__builtin_ia32_psignd256(a37_0,a37_1);FOLD(r);}"
            "{x2d r=__builtin_ia32_maxsd(a38_0,a38_1);FOLD(r);}{x4f r=__builtin_ia32_maxss(a39_0,a39_1);FOLD(r);}return (int)h;}",
        },
        {
            // Batch 2's sse rules: each builtin at its prototype's types, by both paths against clang -O0's code for znver5.
            "x86-sse-2",
            "typedef double x2d __attribute__((vector_size(16)));typedef float x4f __attribute__((vector_size(16)));"
            "int tested(int n){unsigned h=2166136261u;x2d a0_0;fill(&a0_0,16,n+47);a0_0[0]=(double)n*1.25;a0_0[1]=-(double)n*3.5+0.375;x2d a0_1;"
            "fill(&a0_1,16,n+31);a0_1[0]=(double)n*1.25;a0_1[1]=-(double)n*3.5+0.375;x4f a1_0;fill(&a1_0,16,n+15);a1_0[0]=(float)n*1.25f;"
            "a1_0[3]=-(float)n*3.5f+0.375f;x4f a1_1;fill(&a1_1,16,n+0);a1_1[0]=(float)n*1.25f;a1_1[3]=-(float)n*3.5f+0.375f;x4f a2_0;fill(&a2_0,16,n+81);"
            "a2_0[0]=(float)n*1.25f;a2_0[3]=-(float)n*3.5f+0.375f;x2d a2_1;fill(&a2_1,16,n+66);a2_1[0]=(double)n*1.25;a2_1[1]=-(double)n*3.5+0.375;"
            "unsigned long long m=mask(n);{x2d r=__builtin_ia32_minsd(a0_0,a0_1);FOLD(r);}{x4f r=__builtin_ia32_minss(a1_0,a1_1);FOLD(r);}"
            "{x4f r=__builtin_ia32_cvtsd2ss(a2_0,a2_1);FOLD(r);}return (int)h;}",
        },
        {
            // Batch 2's shift rules: each builtin at its prototype's types, by both paths against clang -O0's code for znver5.
            "x86-shift",
            "typedef long long x2Oi __attribute__((vector_size(16)));typedef long long x4Oi __attribute__((vector_size(32)));"
            "typedef int x4i __attribute__((vector_size(16)));typedef long long x8Oi __attribute__((vector_size(64)));"
            "typedef int x8i __attribute__((vector_size(32)));typedef short x8s __attribute__((vector_size(16)));"
            "typedef int x16i __attribute__((vector_size(64)));typedef short x16s __attribute__((vector_size(32)));"
            "typedef short x32s __attribute__((vector_size(64)));int tested(int n){unsigned h=2166136261u;x8s a0_0;fill(&a0_0,16,n+27);x8s a0_1;"
            "fill(&a0_1,16,n+56);a0_1[0]=(short)((unsigned)n%40);a0_1[1]=0;a0_1[2]=0;a0_1[3]=0;x16s a1_0;fill(&a1_0,32,n+50);x8s a1_1;"
            "fill(&a1_1,16,n+80);a1_1[0]=(short)((unsigned)n%40);a1_1[1]=0;a1_1[2]=0;a1_1[3]=0;x32s a2_0;fill(&a2_0,64,n+74);x8s a2_1;fill(&a2_1,16,n+7);"
            "a2_1[0]=(short)((unsigned)n%40);a2_1[1]=0;a2_1[2]=0;a2_1[3]=0;x4i a3_0;fill(&a3_0,16,n+0);x4i a3_1;fill(&a3_1,16,n+28);"
            "a3_1[0]=(int)((unsigned)n%40);a3_1[1]=0;x8i a4_0;fill(&a4_0,32,n+0);x4i a4_1;fill(&a4_1,16,n+28);a4_1[0]=(int)((unsigned)n%40);a4_1[1]=0;"
            "x16i a5_0;fill(&a5_0,64,n+0);x4i a5_1;fill(&a5_1,16,n+29);a5_1[0]=(int)((unsigned)n%40);a5_1[1]=0;x2Oi a6_0;fill(&a6_0,16,n+1);x2Oi a6_1;"
            "fill(&a6_1,16,n+30);a6_1[0]=(long long)((unsigned)n%40);x4Oi a7_0;fill(&a7_0,32,n+96);x2Oi a7_1;fill(&a7_1,16,n+29);"
            "a7_1[0]=(long long)((unsigned)n%40);x8Oi a8_0;fill(&a8_0,64,n+95);x2Oi a8_1;fill(&a8_1,16,n+28);a8_1[0]=(long long)((unsigned)n%40);"
            "x8s a9_0;fill(&a9_0,16,n+94);x8s a9_1;fill(&a9_1,16,n+26);a9_1[0]=(short)((unsigned)n%40);a9_1[1]=0;a9_1[2]=0;a9_1[3]=0;x16s a10_0;"
            "fill(&a10_0,32,n+20);x8s a10_1;fill(&a10_1,16,n+52);a10_1[0]=(short)((unsigned)n%40);a10_1[1]=0;a10_1[2]=0;a10_1[3]=0;x32s a11_0;"
            "fill(&a11_0,64,n+52);x8s a11_1;fill(&a11_1,16,n+84);a11_1[0]=(short)((unsigned)n%40);a11_1[1]=0;a11_1[2]=0;a11_1[3]=0;x4i a12_0;"
            "fill(&a12_0,16,n+84);x4i a12_1;fill(&a12_1,16,n+18);a12_1[0]=(int)((unsigned)n%40);a12_1[1]=0;x8i a13_0;fill(&a13_0,32,n+91);x4i a13_1;"
            "fill(&a13_1,16,n+25);a13_1[0]=(int)((unsigned)n%40);a13_1[1]=0;x16i a14_0;fill(&a14_0,64,n+1);x4i a14_1;fill(&a14_1,16,n+32);"
            "a14_1[0]=(int)((unsigned)n%40);a14_1[1]=0;x2Oi a15_0;fill(&a15_0,16,n+8);x2Oi a15_1;fill(&a15_1,16,n+39);"
            "a15_1[0]=(long long)((unsigned)n%40);x4Oi a16_0;fill(&a16_0,32,n+11);x2Oi a16_1;fill(&a16_1,16,n+43);a16_1[0]=(long long)((unsigned)n%40);"
            "x8Oi a17_0;fill(&a17_0,64,n+15);x2Oi a17_1;fill(&a17_1,16,n+47);a17_1[0]=(long long)((unsigned)n%40);x8s a18_0;fill(&a18_0,16,n+19);"
            "x8s a18_1;fill(&a18_1,16,n+50);a18_1[0]=(short)((unsigned)n%40);a18_1[1]=0;a18_1[2]=0;a18_1[3]=0;x16s a19_0;fill(&a19_0,32,n+50);x8s a19_1;"
            "fill(&a19_1,16,n+82);a19_1[0]=(short)((unsigned)n%40);a19_1[1]=0;a19_1[2]=0;a19_1[3]=0;x32s a20_0;fill(&a20_0,64,n+82);x8s a20_1;"
            "fill(&a20_1,16,n+17);a20_1[0]=(short)((unsigned)n%40);a20_1[1]=0;a20_1[2]=0;a20_1[3]=0;x4i a21_0;fill(&a21_0,16,n+17);x4i a21_1;"
            "fill(&a21_1,16,n+48);a21_1[0]=(int)((unsigned)n%40);a21_1[1]=0;x8i a22_0;fill(&a22_0,32,n+24);x4i a22_1;fill(&a22_1,16,n+55);"
            "a22_1[0]=(int)((unsigned)n%40);a22_1[1]=0;x16i a23_0;fill(&a23_0,64,n+31);x4i a23_1;fill(&a23_1,16,n+63);a23_1[0]=(int)((unsigned)n%40);"
            "a23_1[1]=0;x2Oi a24_0;fill(&a24_0,16,n+39);x2Oi a24_1;fill(&a24_1,16,n+71);a24_1[0]=(long long)((unsigned)n%40);x4Oi a25_0;"
            "fill(&a25_0,32,n+43);x2Oi a25_1;fill(&a25_1,16,n+75);a25_1[0]=(long long)((unsigned)n%40);x8Oi a26_0;fill(&a26_0,64,n+47);x2Oi a26_1;"
            "fill(&a26_1,16,n+79);a26_1[0]=(long long)((unsigned)n%40);unsigned long long m=mask(n);{x8s r=__builtin_ia32_psllw128(a0_0,a0_1);FOLD(r);}"
            "{x16s r=__builtin_ia32_psllw256(a1_0,a1_1);FOLD(r);}{x32s r=__builtin_ia32_psllw512(a2_0,a2_1);FOLD(r);}"
            "{x4i r=__builtin_ia32_pslld128(a3_0,a3_1);FOLD(r);}{x8i r=__builtin_ia32_pslld256(a4_0,a4_1);FOLD(r);}"
            "{x16i r=__builtin_ia32_pslld512(a5_0,a5_1);FOLD(r);}{x2Oi r=__builtin_ia32_psllq128(a6_0,a6_1);FOLD(r);}"
            "{x4Oi r=__builtin_ia32_psllq256(a7_0,a7_1);FOLD(r);}{x8Oi r=__builtin_ia32_psllq512(a8_0,a8_1);FOLD(r);}"
            "{x8s r=__builtin_ia32_psraw128(a9_0,a9_1);FOLD(r);}{x16s r=__builtin_ia32_psraw256(a10_0,a10_1);FOLD(r);}"
            "{x32s r=__builtin_ia32_psraw512(a11_0,a11_1);FOLD(r);}{x4i r=__builtin_ia32_psrad128(a12_0,a12_1);FOLD(r);}"
            "{x8i r=__builtin_ia32_psrad256(a13_0,a13_1);FOLD(r);}{x16i r=__builtin_ia32_psrad512(a14_0,a14_1);FOLD(r);}"
            "{x2Oi r=__builtin_ia32_psraq128(a15_0,a15_1);FOLD(r);}{x4Oi r=__builtin_ia32_psraq256(a16_0,a16_1);FOLD(r);}"
            "{x8Oi r=__builtin_ia32_psraq512(a17_0,a17_1);FOLD(r);}{x8s r=__builtin_ia32_psrlw128(a18_0,a18_1);FOLD(r);}"
            "{x16s r=__builtin_ia32_psrlw256(a19_0,a19_1);FOLD(r);}{x32s r=__builtin_ia32_psrlw512(a20_0,a20_1);FOLD(r);}"
            "{x4i r=__builtin_ia32_psrld128(a21_0,a21_1);FOLD(r);}{x8i r=__builtin_ia32_psrld256(a22_0,a22_1);FOLD(r);}"
            "{x16i r=__builtin_ia32_psrld512(a23_0,a23_1);FOLD(r);}{x2Oi r=__builtin_ia32_psrlq128(a24_0,a24_1);FOLD(r);}"
            "{x4Oi r=__builtin_ia32_psrlq256(a25_0,a25_1);FOLD(r);}{x8Oi r=__builtin_ia32_psrlq512(a26_0,a26_1);FOLD(r);}return (int)h;}",
        },
        {
            // Batch 2's unary rules: each builtin at its prototype's types, by both paths against clang -O0's code for znver5.
            "x86-unary",
            "typedef long long x2Oi __attribute__((vector_size(16)));typedef double x2d __attribute__((vector_size(16)));"
            "typedef long long x4Oi __attribute__((vector_size(32)));typedef double x4d __attribute__((vector_size(32)));"
            "typedef float x4f __attribute__((vector_size(16)));typedef int x4i __attribute__((vector_size(16)));"
            "typedef float x8f __attribute__((vector_size(32)));typedef int x8i __attribute__((vector_size(32)));"
            "typedef short x8s __attribute__((vector_size(16)));typedef __bf16 x8y __attribute__((vector_size(16)));"
            "int tested(int n){unsigned h=2166136261u;x2d a0_0;fill(&a0_0,16,n+75);a0_0[0]=(double)n*1.25;a0_0[1]=-(double)n*3.5+0.375;x4d a1_0;"
            "fill(&a1_0,32,n+59);a1_0[0]=(double)n*1.25;a1_0[3]=-(double)n*3.5+0.375;x2d a2_0;fill(&a2_0,16,n+43);a2_0[0]=(double)n*1.25;"
            "a2_0[1]=-(double)n*3.5+0.375;x4d a3_0;fill(&a3_0,32,n+27);a3_0[0]=(double)n*1.25;a3_0[3]=-(double)n*3.5+0.375;x2d a4_0;fill(&a4_0,16,n+11);"
            "a4_0[0]=(double)n*1.25;a4_0[1]=-(double)n*3.5+0.375;x4d a5_0;fill(&a5_0,32,n+92);a5_0[0]=(double)n*1.25;a5_0[3]=-(double)n*3.5+0.375;"
            "x4f a6_0;fill(&a6_0,16,n+76);a6_0[0]=(float)n*1.25f;a6_0[3]=-(float)n*3.5f+0.375f;x8f a7_0;fill(&a7_0,32,n+61);a7_0[0]=(float)n*1.25f;"
            "a7_0[7]=-(float)n*3.5f+0.375f;x4f a8_0;fill(&a8_0,16,n+46);a8_0[0]=(float)n*1.25f;a8_0[3]=-(float)n*3.5f+0.375f;x8f a9_0;"
            "fill(&a9_0,32,n+31);a9_0[0]=(float)n*1.25f;a9_0[7]=-(float)n*3.5f+0.375f;x4f a10_0;fill(&a10_0,16,n+16);a10_0[0]=(float)n*1.25f;"
            "a10_0[3]=-(float)n*3.5f+0.375f;x8f a11_0;fill(&a11_0,32,n+5);a11_0[0]=(float)n*1.25f;a11_0[7]=-(float)n*3.5f+0.375f;x4f a12_0;"
            "fill(&a12_0,16,n+90);a12_0[0]=(float)n*1.25f;a12_0[3]=-(float)n*3.5f+0.375f;x8f a13_0;fill(&a13_0,32,n+79);a13_0[0]=(float)n*1.25f;"
            "a13_0[7]=-(float)n*3.5f+0.375f;x2Oi a14_0;fill(&a14_0,16,n+68);x8s a15_0;fill(&a15_0,16,n+3);x2Oi a16_0;fill(&a16_0,16,n+33);x4Oi a17_0;"
            "fill(&a17_0,32,n+65);x4i a18_0;fill(&a18_0,16,n+0);x8i a19_0;fill(&a19_0,32,n+30);x4f a20_0;fill(&a20_0,16,n+61);a20_0[0]=(float)n*1.25f;"
            "a20_0[3]=-(float)n*3.5f+0.375f;x8f a21_0;fill(&a21_0,32,n+50);a21_0[0]=(float)n*1.25f;a21_0[7]=-(float)n*3.5f+0.375f;x4f a22_0;"
            "fill(&a22_0,16,n+39);a22_0[0]=(float)n*1.25f;a22_0[3]=-(float)n*3.5f+0.375f;x4f a23_0;fill(&a23_0,16,n+28);a23_0[0]=(float)n*1.25f;"
            "a23_0[3]=-(float)n*3.5f+0.375f;unsigned long long m=mask(n);{x4i r=__builtin_ia32_cvtpd2dq(a0_0);FOLD(r);}"
            "{x4i r=__builtin_ia32_cvtpd2dq256(a1_0);FOLD(r);}{x4f r=__builtin_ia32_cvtpd2ps(a2_0);FOLD(r);}{x4f r=__builtin_ia32_cvtpd2ps256(a3_0);"
            "FOLD(r);}{x4i r=__builtin_ia32_cvttpd2dq(a4_0);FOLD(r);}{x4i r=__builtin_ia32_cvttpd2dq256(a5_0);FOLD(r);}"
            "{x4i r=__builtin_ia32_cvtps2dq(a6_0);FOLD(r);}{x8i r=__builtin_ia32_cvtps2dq256(a7_0);FOLD(r);}{x4i r=__builtin_ia32_cvttps2dq(a8_0);"
            "FOLD(r);}{x8i r=__builtin_ia32_cvttps2dq256(a9_0);FOLD(r);}{x4f r=__builtin_ia32_rcpps(a10_0);FOLD(r);}"
            "{x8f r=__builtin_ia32_rcpps256(a11_0);FOLD(r);}{x4f r=__builtin_ia32_rsqrtps(a12_0);FOLD(r);}{x8f r=__builtin_ia32_rsqrtps256(a13_0);"
            "FOLD(r);}{x2Oi r=__builtin_ia32_aesimc128(a14_0);FOLD(r);}{x8s r=__builtin_ia32_phminposuw128(a15_0);FOLD(r);}"
            "{x2Oi r=__builtin_ia32_vpconflictdi_128(a16_0);FOLD(r);}{x4Oi r=__builtin_ia32_vpconflictdi_256(a17_0);FOLD(r);}"
            "{x4i r=__builtin_ia32_vpconflictsi_128(a18_0);FOLD(r);}{x8i r=__builtin_ia32_vpconflictsi_256(a19_0);FOLD(r);}"
            "{x8y r=__builtin_ia32_vcvtneps2bf16128(a20_0);FOLD(r);}{x8y r=__builtin_ia32_vcvtneps2bf16256(a21_0);FOLD(r);}"
            "{x4f r=__builtin_ia32_rcpss(a22_0);FOLD(r);}{x4f r=__builtin_ia32_rsqrtss(a23_0);FOLD(r);}return (int)h;}",
        },
        {
            // Batch 2's ternary rules: each builtin at its prototype's types, by both paths against clang -O0's code for
            // znver5.
            "x86-ternary",
            "typedef long long x2Oi __attribute__((vector_size(16)));typedef double x2d __attribute__((vector_size(16)));"
            "typedef long long x4Oi __attribute__((vector_size(32)));typedef double x4d __attribute__((vector_size(32)));"
            "typedef float x4f __attribute__((vector_size(16)));typedef int x4i __attribute__((vector_size(16)));"
            "typedef long long x8Oi __attribute__((vector_size(64)));typedef double x8d __attribute__((vector_size(64)));"
            "typedef float x8f __attribute__((vector_size(32)));typedef int x8i __attribute__((vector_size(32)));"
            "typedef short x8s __attribute__((vector_size(16)));typedef __bf16 x8y __attribute__((vector_size(16)));"
            "typedef float x16f __attribute__((vector_size(64)));typedef int x16i __attribute__((vector_size(64)));"
            "typedef short x16s __attribute__((vector_size(32)));typedef __bf16 x16y __attribute__((vector_size(32)));"
            "typedef __bf16 x32y __attribute__((vector_size(64)));int tested(int n){unsigned h=2166136261u;x2d a0_0;fill(&a0_0,16,n+55);"
            "a0_0[0]=(double)n*1.25;a0_0[1]=-(double)n*3.5+0.375;x2d a0_1;fill(&a0_1,16,n+39);a0_1[0]=(double)n*1.25;a0_1[1]=-(double)n*3.5+0.375;"
            "x2d a0_2;fill(&a0_2,16,n+23);a0_2[0]=(double)n*1.25;a0_2[1]=-(double)n*3.5+0.375;x4d a1_0;fill(&a1_0,32,n+7);a1_0[0]=(double)n*1.25;"
            "a1_0[3]=-(double)n*3.5+0.375;x4d a1_1;fill(&a1_1,32,n+87);a1_1[0]=(double)n*1.25;a1_1[3]=-(double)n*3.5+0.375;x4d a1_2;fill(&a1_2,32,n+71);"
            "a1_2[0]=(double)n*1.25;a1_2[3]=-(double)n*3.5+0.375;x4f a2_0;fill(&a2_0,16,n+55);a2_0[0]=(float)n*1.25f;a2_0[3]=-(float)n*3.5f+0.375f;"
            "x4f a2_1;fill(&a2_1,16,n+40);a2_1[0]=(float)n*1.25f;a2_1[3]=-(float)n*3.5f+0.375f;x4f a2_2;fill(&a2_2,16,n+25);a2_2[0]=(float)n*1.25f;"
            "a2_2[3]=-(float)n*3.5f+0.375f;x8f a3_0;fill(&a3_0,32,n+10);a3_0[0]=(float)n*1.25f;a3_0[7]=-(float)n*3.5f+0.375f;x8f a3_1;"
            "fill(&a3_1,32,n+92);a3_1[0]=(float)n*1.25f;a3_1[7]=-(float)n*3.5f+0.375f;x8f a3_2;fill(&a3_2,32,n+77);a3_2[0]=(float)n*1.25f;"
            "a3_2[7]=-(float)n*3.5f+0.375f;x4i a4_0;fill(&a4_0,16,n+62);x4i a4_1;fill(&a4_1,16,n+91);x4i a4_2;fill(&a4_2,16,n+23);x8i a5_0;"
            "fill(&a5_0,32,n+52);x8i a5_1;fill(&a5_1,32,n+81);x8i a5_2;fill(&a5_2,32,n+13);x2Oi a6_0;fill(&a6_0,16,n+42);x2Oi a6_1;fill(&a6_1,16,n+72);"
            "x2Oi a6_2;fill(&a6_2,16,n+5);x4Oi a7_0;fill(&a7_0,32,n+34);x4Oi a7_1;fill(&a7_1,32,n+64);x4Oi a7_2;fill(&a7_2,32,n+94);x8s a8_0;"
            "fill(&a8_0,16,n+27);x8s a8_1;fill(&a8_1,16,n+56);x8s a8_2;fill(&a8_2,16,n+85);x16s a9_0;fill(&a9_0,32,n+17);x16s a9_1;fill(&a9_1,32,n+47);"
            "x16s a9_2;fill(&a9_2,32,n+77);x2d a10_0;fill(&a10_0,16,n+10);a10_0[0]=(double)n*1.25;a10_0[1]=-(double)n*3.5+0.375;x2Oi a10_1;"
            "fill(&a10_1,16,n+95);x2d a10_2;fill(&a10_2,16,n+30);a10_2[0]=(double)n*1.25;a10_2[1]=-(double)n*3.5+0.375;x4d a11_0;fill(&a11_0,32,n+18);"
            "a11_0[0]=(double)n*1.25;a11_0[3]=-(double)n*3.5+0.375;x4Oi a11_1;fill(&a11_1,32,n+6);x4d a11_2;fill(&a11_2,32,n+37);a11_2[0]=(double)n*1.25;"
            "a11_2[3]=-(double)n*3.5+0.375;x8d a12_0;fill(&a12_0,64,n+25);a12_0[0]=(double)n*1.25;a12_0[7]=-(double)n*3.5+0.375;x8Oi a12_1;"
            "fill(&a12_1,64,n+13);x8d a12_2;fill(&a12_2,64,n+45);a12_2[0]=(double)n*1.25;a12_2[7]=-(double)n*3.5+0.375;x4f a13_0;fill(&a13_0,16,n+33);"
            "a13_0[0]=(float)n*1.25f;a13_0[3]=-(float)n*3.5f+0.375f;x4i a13_1;fill(&a13_1,16,n+22);x4f a13_2;fill(&a13_2,16,n+53);"
            "a13_2[0]=(float)n*1.25f;a13_2[3]=-(float)n*3.5f+0.375f;x8f a14_0;fill(&a14_0,32,n+42);a14_0[0]=(float)n*1.25f;"
            "a14_0[7]=-(float)n*3.5f+0.375f;x8i a14_1;fill(&a14_1,32,n+31);x8f a14_2;fill(&a14_2,32,n+62);a14_2[0]=(float)n*1.25f;"
            "a14_2[7]=-(float)n*3.5f+0.375f;x16f a15_0;fill(&a15_0,64,n+51);a15_0[0]=(float)n*1.25f;a15_0[15]=-(float)n*3.5f+0.375f;x16i a15_1;"
            "fill(&a15_1,64,n+42);x16f a15_2;fill(&a15_2,64,n+74);a15_2[0]=(float)n*1.25f;a15_2[15]=-(float)n*3.5f+0.375f;x2Oi a16_0;"
            "fill(&a16_0,16,n+65);x2Oi a16_1;fill(&a16_1,16,n+0);x2Oi a16_2;fill(&a16_2,16,n+31);x4Oi a17_0;fill(&a17_0,32,n+63);x4Oi a17_1;"
            "fill(&a17_1,32,n+95);x4Oi a17_2;fill(&a17_2,32,n+30);x8Oi a18_0;fill(&a18_0,64,n+62);x8Oi a18_1;fill(&a18_1,64,n+94);x8Oi a18_2;"
            "fill(&a18_2,64,n+29);x2Oi a19_0;fill(&a19_0,16,n+61);x2Oi a19_1;fill(&a19_1,16,n+93);x2Oi a19_2;fill(&a19_2,16,n+28);x4Oi a20_0;"
            "fill(&a20_0,32,n+60);x4Oi a20_1;fill(&a20_1,32,n+92);x4Oi a20_2;fill(&a20_2,32,n+27);x8Oi a21_0;fill(&a21_0,64,n+59);x8Oi a21_1;"
            "fill(&a21_1,64,n+91);x8Oi a21_2;fill(&a21_2,64,n+26);x4f a22_0;fill(&a22_0,16,n+58);a22_0[0]=(float)n*1.25f;a22_0[3]=-(float)n*3.5f+0.375f;"
            "x8y a22_1;fill(&a22_1,16,n+47);x8y a22_2;fill(&a22_2,16,n+78);x8f a23_0;fill(&a23_0,32,n+12);a23_0[0]=(float)n*1.25f;"
            "a23_0[7]=-(float)n*3.5f+0.375f;x16y a23_1;fill(&a23_1,32,n+1);x16y a23_2;fill(&a23_2,32,n+32);x16f a24_0;fill(&a24_0,64,n+64);"
            "a24_0[0]=(float)n*1.25f;a24_0[15]=-(float)n*3.5f+0.375f;x32y a24_1;fill(&a24_1,64,n+55);x32y a24_2;fill(&a24_2,64,n+87);x2d a25_0;"
            "fill(&a25_0,16,n+22);a25_0[0]=(double)n*1.25;a25_0[1]=-(double)n*3.5+0.375;x2d a25_1;fill(&a25_1,16,n+10);a25_1[0]=(double)n*1.25;"
            "a25_1[1]=-(double)n*3.5+0.375;x2d a25_2;fill(&a25_2,16,n+95);a25_2[0]=(double)n*1.25;a25_2[1]=-(double)n*3.5+0.375;x4d a26_0;"
            "fill(&a26_0,32,n+83);a26_0[0]=(double)n*1.25;a26_0[3]=-(double)n*3.5+0.375;x4d a26_1;fill(&a26_1,32,n+71);a26_1[0]=(double)n*1.25;"
            "a26_1[3]=-(double)n*3.5+0.375;x4d a26_2;fill(&a26_2,32,n+59);a26_2[0]=(double)n*1.25;a26_2[3]=-(double)n*3.5+0.375;x4f a27_0;"
            "fill(&a27_0,16,n+47);a27_0[0]=(float)n*1.25f;a27_0[3]=-(float)n*3.5f+0.375f;x4f a27_1;fill(&a27_1,16,n+36);a27_1[0]=(float)n*1.25f;"
            "a27_1[3]=-(float)n*3.5f+0.375f;x4f a27_2;fill(&a27_2,16,n+25);a27_2[0]=(float)n*1.25f;a27_2[3]=-(float)n*3.5f+0.375f;x8f a28_0;"
            "fill(&a28_0,32,n+14);a28_0[0]=(float)n*1.25f;a28_0[7]=-(float)n*3.5f+0.375f;x8f a28_1;fill(&a28_1,32,n+3);a28_1[0]=(float)n*1.25f;"
            "a28_1[7]=-(float)n*3.5f+0.375f;x8f a28_2;fill(&a28_2,32,n+88);a28_2[0]=(float)n*1.25f;a28_2[7]=-(float)n*3.5f+0.375f;"
            "unsigned long long m=mask(n);{x2d r=__builtin_ia32_vfmaddsubpd(a0_0,a0_1,a0_2);FOLD(r);}"
            "{x4d r=__builtin_ia32_vfmaddsubpd256(a1_0,a1_1,a1_2);FOLD(r);}{x4f r=__builtin_ia32_vfmaddsubps(a2_0,a2_1,a2_2);FOLD(r);}"
            "{x8f r=__builtin_ia32_vfmaddsubps256(a3_0,a3_1,a3_2);FOLD(r);}{x4i r=__builtin_ia32_vpermi2vard128(a4_0,a4_1,a4_2);FOLD(r);}"
            "{x8i r=__builtin_ia32_vpermi2vard256(a5_0,a5_1,a5_2);FOLD(r);}{x2Oi r=__builtin_ia32_vpermi2varq128(a6_0,a6_1,a6_2);FOLD(r);}"
            "{x4Oi r=__builtin_ia32_vpermi2varq256(a7_0,a7_1,a7_2);FOLD(r);}{x8s r=__builtin_ia32_vpermi2varhi128(a8_0,a8_1,a8_2);FOLD(r);}"
            "{x16s r=__builtin_ia32_vpermi2varhi256(a9_0,a9_1,a9_2);FOLD(r);}{x2d r=__builtin_ia32_vpermi2varpd128(a10_0,a10_1,a10_2);FOLD(r);}"
            "{x4d r=__builtin_ia32_vpermi2varpd256(a11_0,a11_1,a11_2);FOLD(r);}{x8d r=__builtin_ia32_vpermi2varpd512(a12_0,a12_1,a12_2);FOLD(r);}"
            "{x4f r=__builtin_ia32_vpermi2varps128(a13_0,a13_1,a13_2);FOLD(r);}{x8f r=__builtin_ia32_vpermi2varps256(a14_0,a14_1,a14_2);FOLD(r);}"
            "{x16f r=__builtin_ia32_vpermi2varps512(a15_0,a15_1,a15_2);FOLD(r);}{x2Oi r=__builtin_ia32_vpmadd52huq128(a16_0,a16_1,a16_2);FOLD(r);}"
            "{x4Oi r=__builtin_ia32_vpmadd52huq256(a17_0,a17_1,a17_2);FOLD(r);}{x8Oi r=__builtin_ia32_vpmadd52huq512(a18_0,a18_1,a18_2);FOLD(r);}"
            "{x2Oi r=__builtin_ia32_vpmadd52luq128(a19_0,a19_1,a19_2);FOLD(r);}{x4Oi r=__builtin_ia32_vpmadd52luq256(a20_0,a20_1,a20_2);FOLD(r);}"
            "{x8Oi r=__builtin_ia32_vpmadd52luq512(a21_0,a21_1,a21_2);FOLD(r);}{x4f r=__builtin_ia32_dpbf16ps_128(a22_0,a22_1,a22_2);FOLD(r);}"
            "{x8f r=__builtin_ia32_dpbf16ps_256(a23_0,a23_1,a23_2);FOLD(r);}{x16f r=__builtin_ia32_dpbf16ps_512(a24_0,a24_1,a24_2);FOLD(r);}"
            "{x2d r=__builtin_ia32_blendvpd(a25_0,a25_1,a25_2);FOLD(r);}{x4d r=__builtin_ia32_blendvpd256(a26_0,a26_1,a26_2);FOLD(r);}"
            "{x4f r=__builtin_ia32_blendvps(a27_0,a27_1,a27_2);FOLD(r);}{x8f r=__builtin_ia32_blendvps256(a28_0,a28_1,a28_2);FOLD(r);}return (int)h;}",
        },
        {
            // Batch 2's mask-unary rules: each builtin at its prototype's types, by both paths against clang -O0's code for
            // znver5.
            "x86-mask-unary-1",
            "typedef long long x2Oi __attribute__((vector_size(16)));typedef double x2d __attribute__((vector_size(16)));"
            "typedef long long x4Oi __attribute__((vector_size(32)));typedef double x4d __attribute__((vector_size(32)));"
            "typedef float x4f __attribute__((vector_size(16)));typedef int x4i __attribute__((vector_size(16)));"
            "typedef double x8d __attribute__((vector_size(64)));typedef float x8f __attribute__((vector_size(32)));"
            "typedef int x8i __attribute__((vector_size(32)));typedef float x16f __attribute__((vector_size(64)));"
            "int tested(int n){unsigned h=2166136261u;x2d a0_0;fill(&a0_0,16,n+76);a0_0[0]=(double)n*1.25;a0_0[1]=-(double)n*3.5+0.375;x4i a0_1;"
            "fill(&a0_1,16,n+60);x2d a1_0;fill(&a1_0,16,n+89);a1_0[0]=(double)n*1.25;a1_0[1]=-(double)n*3.5+0.375;x4f a1_1;fill(&a1_1,16,n+73);"
            "a1_1[0]=(float)n*1.25f;a1_1[3]=-(float)n*3.5f+0.375f;x2d a2_0;fill(&a2_0,16,n+58);a2_0[0]=(double)n*1.25;a2_0[1]=-(double)n*3.5+0.375;"
            "x2Oi a2_1;fill(&a2_1,16,n+42);x4d a3_0;fill(&a3_0,32,n+72);a3_0[0]=(double)n*1.25;a3_0[3]=-(double)n*3.5+0.375;x4Oi a3_1;"
            "fill(&a3_1,32,n+56);x2d a4_0;fill(&a4_0,16,n+86);a4_0[0]=(double)n*1.25;a4_0[1]=-(double)n*3.5+0.375;x2Oi a4_1;fill(&a4_1,16,n+70);x4d a5_0;"
            "fill(&a5_0,32,n+3);a5_0[0]=(double)n*1.25;a5_0[3]=-(double)n*3.5+0.375;x4Oi a5_1;fill(&a5_1,32,n+83);x2d a6_0;fill(&a6_0,16,n+16);"
            "a6_0[0]=(double)n*1.25;a6_0[1]=-(double)n*3.5+0.375;x2Oi a6_1;fill(&a6_1,16,n+0);x4d a7_0;fill(&a7_0,32,n+29);a7_0[0]=(double)n*1.25;"
            "a7_0[3]=-(double)n*3.5+0.375;x4Oi a7_1;fill(&a7_1,32,n+13);x2d a8_0;fill(&a8_0,16,n+43);a8_0[0]=(double)n*1.25;a8_0[1]=-(double)n*3.5+0.375;"
            "x2Oi a8_1;fill(&a8_1,16,n+27);x4d a9_0;fill(&a9_0,32,n+57);a9_0[0]=(double)n*1.25;a9_0[3]=-(double)n*3.5+0.375;x4Oi a9_1;"
            "fill(&a9_1,32,n+41);x2d a10_0;fill(&a10_0,16,n+71);a10_0[0]=(double)n*1.25;a10_0[1]=-(double)n*3.5+0.375;x4i a10_1;fill(&a10_1,16,n+59);"
            "x4d a11_0;fill(&a11_0,32,n+90);a11_0[0]=(double)n*1.25;a11_0[3]=-(double)n*3.5+0.375;x4i a11_1;fill(&a11_1,16,n+78);x2d a12_0;"
            "fill(&a12_0,16,n+12);a12_0[0]=(double)n*1.25;a12_0[1]=-(double)n*3.5+0.375;x4i a12_1;fill(&a12_1,16,n+0);x4d a13_0;fill(&a13_0,32,n+30);"
            "a13_0[0]=(double)n*1.25;a13_0[3]=-(double)n*3.5+0.375;x4i a13_1;fill(&a13_1,16,n+18);x2d a14_0;fill(&a14_0,16,n+49);a14_0[0]=(double)n*1.25;"
            "a14_0[1]=-(double)n*3.5+0.375;x4i a14_1;fill(&a14_1,16,n+37);x4f a15_0;fill(&a15_0,16,n+68);a15_0[0]=(float)n*1.25f;"
            "a15_0[3]=-(float)n*3.5f+0.375f;x4i a15_1;fill(&a15_1,16,n+57);x8f a16_0;fill(&a16_0,32,n+88);a16_0[0]=(float)n*1.25f;"
            "a16_0[7]=-(float)n*3.5f+0.375f;x8i a16_1;fill(&a16_1,32,n+77);x4f a17_0;fill(&a17_0,16,n+11);a17_0[0]=(float)n*1.25f;"
            "a17_0[3]=-(float)n*3.5f+0.375f;x4i a17_1;fill(&a17_1,16,n+0);x8f a18_0;fill(&a18_0,32,n+30);a18_0[0]=(float)n*1.25f;"
            "a18_0[7]=-(float)n*3.5f+0.375f;x8i a18_1;fill(&a18_1,32,n+19);x4f a19_0;fill(&a19_0,16,n+50);a19_0[0]=(float)n*1.25f;"
            "a19_0[3]=-(float)n*3.5f+0.375f;x2Oi a19_1;fill(&a19_1,16,n+39);x4f a20_0;fill(&a20_0,16,n+71);a20_0[0]=(float)n*1.25f;"
            "a20_0[3]=-(float)n*3.5f+0.375f;x4Oi a20_1;fill(&a20_1,32,n+60);x4f a21_0;fill(&a21_0,16,n+92);a21_0[0]=(float)n*1.25f;"
            "a21_0[3]=-(float)n*3.5f+0.375f;x2Oi a21_1;fill(&a21_1,16,n+81);x4f a22_0;fill(&a22_0,16,n+16);a22_0[0]=(float)n*1.25f;"
            "a22_0[3]=-(float)n*3.5f+0.375f;x4Oi a22_1;fill(&a22_1,32,n+5);x4f a23_0;fill(&a23_0,16,n+36);a23_0[0]=(float)n*1.25f;"
            "a23_0[3]=-(float)n*3.5f+0.375f;x2Oi a23_1;fill(&a23_1,16,n+25);x4f a24_0;fill(&a24_0,16,n+57);a24_0[0]=(float)n*1.25f;"
            "a24_0[3]=-(float)n*3.5f+0.375f;x4Oi a24_1;fill(&a24_1,32,n+46);x4f a25_0;fill(&a25_0,16,n+78);a25_0[0]=(float)n*1.25f;"
            "a25_0[3]=-(float)n*3.5f+0.375f;x2Oi a25_1;fill(&a25_1,16,n+67);x4f a26_0;fill(&a26_0,16,n+2);a26_0[0]=(float)n*1.25f;"
            "a26_0[3]=-(float)n*3.5f+0.375f;x4Oi a26_1;fill(&a26_1,32,n+87);x2Oi a27_0;fill(&a27_0,16,n+22);x4f a27_1;fill(&a27_1,16,n+54);"
            "a27_1[0]=(float)n*1.25f;a27_1[3]=-(float)n*3.5f+0.375f;x2Oi a28_0;fill(&a28_0,16,n+43);x4f a28_1;fill(&a28_1,16,n+75);"
            "a28_1[0]=(float)n*1.25f;a28_1[3]=-(float)n*3.5f+0.375f;x2d a29_0;fill(&a29_0,16,n+64);a29_0[0]=(double)n*1.25;a29_0[1]=-(double)n*3.5+0.375;"
            "x2d a29_1;fill(&a29_1,16,n+52);a29_1[0]=(double)n*1.25;a29_1[1]=-(double)n*3.5+0.375;x4d a30_0;fill(&a30_0,32,n+40);a30_0[0]=(double)n*1.25;"
            "a30_0[3]=-(double)n*3.5+0.375;x4d a30_1;fill(&a30_1,32,n+28);a30_1[0]=(double)n*1.25;a30_1[3]=-(double)n*3.5+0.375;x4f a31_0;"
            "fill(&a31_0,16,n+16);a31_0[0]=(float)n*1.25f;a31_0[3]=-(float)n*3.5f+0.375f;x4f a31_1;fill(&a31_1,16,n+5);a31_1[0]=(float)n*1.25f;"
            "a31_1[3]=-(float)n*3.5f+0.375f;x8f a32_0;fill(&a32_0,32,n+90);a32_0[0]=(float)n*1.25f;a32_0[7]=-(float)n*3.5f+0.375f;x8f a32_1;"
            "fill(&a32_1,32,n+79);a32_1[0]=(float)n*1.25f;a32_1[7]=-(float)n*3.5f+0.375f;x2d a33_0;fill(&a33_0,16,n+68);a33_0[0]=(double)n*1.25;"
            "a33_0[1]=-(double)n*3.5+0.375;x2d a33_1;fill(&a33_1,16,n+56);a33_1[0]=(double)n*1.25;a33_1[1]=-(double)n*3.5+0.375;x4d a34_0;"
            "fill(&a34_0,32,n+44);a34_0[0]=(double)n*1.25;a34_0[3]=-(double)n*3.5+0.375;x4d a34_1;fill(&a34_1,32,n+32);a34_1[0]=(double)n*1.25;"
            "a34_1[3]=-(double)n*3.5+0.375;x8d a35_0;fill(&a35_0,64,n+20);a35_0[0]=(double)n*1.25;a35_0[7]=-(double)n*3.5+0.375;x8d a35_1;"
            "fill(&a35_1,64,n+8);a35_1[0]=(double)n*1.25;a35_1[7]=-(double)n*3.5+0.375;x4f a36_0;fill(&a36_0,16,n+92);a36_0[0]=(float)n*1.25f;"
            "a36_0[3]=-(float)n*3.5f+0.375f;x4f a36_1;fill(&a36_1,16,n+81);a36_1[0]=(float)n*1.25f;a36_1[3]=-(float)n*3.5f+0.375f;x8f a37_0;"
            "fill(&a37_0,32,n+70);a37_0[0]=(float)n*1.25f;a37_0[7]=-(float)n*3.5f+0.375f;x8f a37_1;fill(&a37_1,32,n+59);a37_1[0]=(float)n*1.25f;"
            "a37_1[7]=-(float)n*3.5f+0.375f;x16f a38_0;fill(&a38_0,64,n+48);a38_0[0]=(float)n*1.25f;a38_0[15]=-(float)n*3.5f+0.375f;x16f a38_1;"
            "fill(&a38_1,64,n+39);a38_1[0]=(float)n*1.25f;a38_1[15]=-(float)n*3.5f+0.375f;x2d a39_0;fill(&a39_0,16,n+30);a39_0[0]=(double)n*1.25;"
            "a39_0[1]=-(double)n*3.5+0.375;x2d a39_1;fill(&a39_1,16,n+18);a39_1[0]=(double)n*1.25;a39_1[1]=-(double)n*3.5+0.375;"
            "unsigned long long m=mask(n);{x4i r=__builtin_ia32_cvtpd2dq128_mask(a0_0,a0_1,(unsigned char)(m>>26));FOLD(r);}"
            "{x4f r=__builtin_ia32_cvtpd2ps_mask(a1_0,a1_1,(unsigned char)(m>>33));FOLD(r);}"
            "{x2Oi r=__builtin_ia32_cvtpd2qq128_mask(a2_0,a2_1,(unsigned char)(m>>40));FOLD(r);}"
            "{x4Oi r=__builtin_ia32_cvtpd2qq256_mask(a3_0,a3_1,(unsigned char)(m>>47));FOLD(r);}"
            "{x2Oi r=__builtin_ia32_cvtpd2uqq128_mask(a4_0,a4_1,(unsigned char)(m>>54));FOLD(r);}"
            "{x4Oi r=__builtin_ia32_cvtpd2uqq256_mask(a5_0,a5_1,(unsigned char)(m>>0));FOLD(r);}"
            "{x2Oi r=__builtin_ia32_cvttpd2qq128_mask(a6_0,a6_1,(unsigned char)(m>>7));FOLD(r);}"
            "{x4Oi r=__builtin_ia32_cvttpd2qq256_mask(a7_0,a7_1,(unsigned char)(m>>14));FOLD(r);}"
            "{x2Oi r=__builtin_ia32_cvttpd2uqq128_mask(a8_0,a8_1,(unsigned char)(m>>21));FOLD(r);}"
            "{x4Oi r=__builtin_ia32_cvttpd2uqq256_mask(a9_0,a9_1,(unsigned char)(m>>28));FOLD(r);}"
            "{x4i r=__builtin_ia32_cvtpd2udq128_mask(a10_0,a10_1,(unsigned char)(m>>35));FOLD(r);}"
            "{x4i r=__builtin_ia32_cvtpd2udq256_mask(a11_0,a11_1,(unsigned char)(m>>42));FOLD(r);}"
            "{x4i r=__builtin_ia32_cvttpd2udq128_mask(a12_0,a12_1,(unsigned char)(m>>49));FOLD(r);}"
            "{x4i r=__builtin_ia32_cvttpd2udq256_mask(a13_0,a13_1,(unsigned char)(m>>56));FOLD(r);}"
            "{x4i r=__builtin_ia32_cvttpd2dq128_mask(a14_0,a14_1,(unsigned char)(m>>2));FOLD(r);}"
            "{x4i r=__builtin_ia32_cvtps2udq128_mask(a15_0,a15_1,(unsigned char)(m>>9));FOLD(r);}"
            "{x8i r=__builtin_ia32_cvtps2udq256_mask(a16_0,a16_1,(unsigned char)(m>>16));FOLD(r);}"
            "{x4i r=__builtin_ia32_cvttps2udq128_mask(a17_0,a17_1,(unsigned char)(m>>23));FOLD(r);}"
            "{x8i r=__builtin_ia32_cvttps2udq256_mask(a18_0,a18_1,(unsigned char)(m>>30));FOLD(r);}"
            "{x2Oi r=__builtin_ia32_cvtps2qq128_mask(a19_0,a19_1,(unsigned char)(m>>37));FOLD(r);}"
            "{x4Oi r=__builtin_ia32_cvtps2qq256_mask(a20_0,a20_1,(unsigned char)(m>>44));FOLD(r);}"
            "{x2Oi r=__builtin_ia32_cvtps2uqq128_mask(a21_0,a21_1,(unsigned char)(m>>51));FOLD(r);}"
            "{x4Oi r=__builtin_ia32_cvtps2uqq256_mask(a22_0,a22_1,(unsigned char)(m>>58));FOLD(r);}"
            "{x2Oi r=__builtin_ia32_cvttps2qq128_mask(a23_0,a23_1,(unsigned char)(m>>4));FOLD(r);}"
            "{x4Oi r=__builtin_ia32_cvttps2qq256_mask(a24_0,a24_1,(unsigned char)(m>>11));FOLD(r);}"
            "{x2Oi r=__builtin_ia32_cvttps2uqq128_mask(a25_0,a25_1,(unsigned char)(m>>18));FOLD(r);}"
            "{x4Oi r=__builtin_ia32_cvttps2uqq256_mask(a26_0,a26_1,(unsigned char)(m>>25));FOLD(r);}"
            "{x4f r=__builtin_ia32_cvtqq2ps128_mask(a27_0,a27_1,(unsigned char)(m>>32));FOLD(r);}"
            "{x4f r=__builtin_ia32_cvtuqq2ps128_mask(a28_0,a28_1,(unsigned char)(m>>39));FOLD(r);}"
            "{x2d r=__builtin_ia32_getexppd128_mask(a29_0,a29_1,(unsigned char)(m>>46));FOLD(r);}"
            "{x4d r=__builtin_ia32_getexppd256_mask(a30_0,a30_1,(unsigned char)(m>>53));FOLD(r);}"
            "{x4f r=__builtin_ia32_getexpps128_mask(a31_0,a31_1,(unsigned char)(m>>60));FOLD(r);}"
            "{x8f r=__builtin_ia32_getexpps256_mask(a32_0,a32_1,(unsigned char)(m>>6));FOLD(r);}"
            "{x2d r=__builtin_ia32_rcp14pd128_mask(a33_0,a33_1,(unsigned char)(m>>13));FOLD(r);}"
            "{x4d r=__builtin_ia32_rcp14pd256_mask(a34_0,a34_1,(unsigned char)(m>>20));FOLD(r);}"
            "{x8d r=__builtin_ia32_rcp14pd512_mask(a35_0,a35_1,(unsigned char)(m>>27));FOLD(r);}"
            "{x4f r=__builtin_ia32_rcp14ps128_mask(a36_0,a36_1,(unsigned char)(m>>34));FOLD(r);}"
            "{x8f r=__builtin_ia32_rcp14ps256_mask(a37_0,a37_1,(unsigned char)(m>>41));FOLD(r);}"
            "{x16f r=__builtin_ia32_rcp14ps512_mask(a38_0,a38_1,(unsigned short)(m>>48));FOLD(r);}"
            "{x2d r=__builtin_ia32_rsqrt14pd128_mask(a39_0,a39_1,(unsigned char)(m>>55));FOLD(r);}return (int)h;}",
        },
        {
            // Batch 2's mask-unary rules: each builtin at its prototype's types, by both paths against clang -O0's code for
            // znver5.
            "x86-mask-unary-2",
            "typedef double x4d __attribute__((vector_size(32)));typedef float x4f __attribute__((vector_size(16)));"
            "typedef long long x8Oi __attribute__((vector_size(64)));typedef double x8d __attribute__((vector_size(64)));"
            "typedef float x8f __attribute__((vector_size(32)));typedef int x8i __attribute__((vector_size(32)));"
            "typedef short x8s __attribute__((vector_size(16)));typedef __bf16 x8y __attribute__((vector_size(16)));"
            "typedef float x16f __attribute__((vector_size(64)));typedef int x16i __attribute__((vector_size(64)));"
            "typedef short x16s __attribute__((vector_size(32)));typedef __bf16 x16y __attribute__((vector_size(32)));"
            "int tested(int n){unsigned h=2166136261u;x4d a0_0;fill(&a0_0,32,n+80);a0_0[0]=(double)n*1.25;a0_0[3]=-(double)n*3.5+0.375;x4d a0_1;"
            "fill(&a0_1,32,n+64);a0_1[0]=(double)n*1.25;a0_1[3]=-(double)n*3.5+0.375;x8d a1_0;fill(&a1_0,64,n+48);a1_0[0]=(double)n*1.25;"
            "a1_0[7]=-(double)n*3.5+0.375;x8d a1_1;fill(&a1_1,64,n+32);a1_1[0]=(double)n*1.25;a1_1[7]=-(double)n*3.5+0.375;x4f a2_0;fill(&a2_0,16,n+16);"
            "a2_0[0]=(float)n*1.25f;a2_0[3]=-(float)n*3.5f+0.375f;x4f a2_1;fill(&a2_1,16,n+1);a2_1[0]=(float)n*1.25f;a2_1[3]=-(float)n*3.5f+0.375f;"
            "x8f a3_0;fill(&a3_0,32,n+82);a3_0[0]=(float)n*1.25f;a3_0[7]=-(float)n*3.5f+0.375f;x8f a3_1;fill(&a3_1,32,n+67);a3_1[0]=(float)n*1.25f;"
            "a3_1[7]=-(float)n*3.5f+0.375f;x16f a4_0;fill(&a4_0,64,n+52);a4_0[0]=(float)n*1.25f;a4_0[15]=-(float)n*3.5f+0.375f;x16f a4_1;"
            "fill(&a4_1,64,n+39);a4_1[0]=(float)n*1.25f;a4_1[15]=-(float)n*3.5f+0.375f;x8s a5_0;fill(&a5_0,16,n+26);x4f a5_1;fill(&a5_1,16,n+55);"
            "a5_1[0]=(float)n*1.25f;a5_1[3]=-(float)n*3.5f+0.375f;x8s a6_0;fill(&a6_0,16,n+40);x8f a6_1;fill(&a6_1,32,n+69);a6_1[0]=(float)n*1.25f;"
            "a6_1[7]=-(float)n*3.5f+0.375f;x16s a7_0;fill(&a7_0,32,n+54);x16f a7_1;fill(&a7_1,64,n+84);a7_1[0]=(float)n*1.25f;"
            "a7_1[15]=-(float)n*3.5f+0.375f;x4f a8_0;fill(&a8_0,16,n+71);a8_0[0]=(float)n*1.25f;a8_0[3]=-(float)n*3.5f+0.375f;x8y a8_1;"
            "fill(&a8_1,16,n+56);x8f a9_0;fill(&a9_0,32,n+85);a9_0[0]=(float)n*1.25f;a9_0[7]=-(float)n*3.5f+0.375f;x8y a9_1;fill(&a9_1,16,n+70);"
            "x16f a10_0;fill(&a10_0,64,n+2);a10_0[0]=(float)n*1.25f;a10_0[15]=-(float)n*3.5f+0.375f;x16y a10_1;fill(&a10_1,32,n+89);x8d a11_0;"
            "fill(&a11_0,64,n+24);a11_0[0]=(double)n*1.25;a11_0[7]=-(double)n*3.5+0.375;x8i a11_1;fill(&a11_1,32,n+12);x8d a12_0;fill(&a12_0,64,n+43);"
            "a12_0[0]=(double)n*1.25;a12_0[7]=-(double)n*3.5+0.375;x8f a12_1;fill(&a12_1,32,n+31);a12_1[0]=(float)n*1.25f;a12_1[7]=-(float)n*3.5f+0.375f;"
            "x8d a13_0;fill(&a13_0,64,n+20);a13_0[0]=(double)n*1.25;a13_0[7]=-(double)n*3.5+0.375;x8i a13_1;fill(&a13_1,32,n+8);x8Oi a14_0;"
            "fill(&a14_0,64,n+38);x8f a14_1;fill(&a14_1,32,n+70);a14_1[0]=(float)n*1.25f;a14_1[7]=-(float)n*3.5f+0.375f;x8d a15_0;fill(&a15_0,64,n+59);"
            "a15_0[0]=(double)n*1.25;a15_0[7]=-(double)n*3.5+0.375;x8i a15_1;fill(&a15_1,32,n+47);x8d a16_0;fill(&a16_0,64,n+78);a16_0[0]=(double)n*1.25;"
            "a16_0[7]=-(double)n*3.5+0.375;x8i a16_1;fill(&a16_1,32,n+66);x8Oi a17_0;fill(&a17_0,64,n+0);x8f a17_1;fill(&a17_1,32,n+31);"
            "a17_1[0]=(float)n*1.25f;a17_1[7]=-(float)n*3.5f+0.375f;x8d a18_0;fill(&a18_0,64,n+20);a18_0[0]=(double)n*1.25;a18_0[7]=-(double)n*3.5+0.375;"
            "x8Oi a18_1;fill(&a18_1,64,n+8);x8d a19_0;fill(&a19_0,64,n+39);a19_0[0]=(double)n*1.25;a19_0[7]=-(double)n*3.5+0.375;x8Oi a19_1;"
            "fill(&a19_1,64,n+27);x8d a20_0;fill(&a20_0,64,n+59);a20_0[0]=(double)n*1.25;a20_0[7]=-(double)n*3.5+0.375;x8Oi a20_1;fill(&a20_1,64,n+47);"
            "x8d a21_0;fill(&a21_0,64,n+79);a21_0[0]=(double)n*1.25;a21_0[7]=-(double)n*3.5+0.375;x8Oi a21_1;fill(&a21_1,64,n+67);x8d a22_0;"
            "fill(&a22_0,64,n+2);a22_0[0]=(double)n*1.25;a22_0[7]=-(double)n*3.5+0.375;x8d a22_1;fill(&a22_1,64,n+86);a22_1[0]=(double)n*1.25;"
            "a22_1[7]=-(double)n*3.5+0.375;x16f a23_0;fill(&a23_0,64,n+74);a23_0[0]=(float)n*1.25f;a23_0[15]=-(float)n*3.5f+0.375f;x16i a23_1;"
            "fill(&a23_1,64,n+65);x16f a24_0;fill(&a24_0,64,n+0);a24_0[0]=(float)n*1.25f;a24_0[15]=-(float)n*3.5f+0.375f;x16i a24_1;fill(&a24_1,64,n+87);"
            "x16f a25_0;fill(&a25_0,64,n+22);a25_0[0]=(float)n*1.25f;a25_0[15]=-(float)n*3.5f+0.375f;x16i a25_1;fill(&a25_1,64,n+13);x16f a26_0;"
            "fill(&a26_0,64,n+45);a26_0[0]=(float)n*1.25f;a26_0[15]=-(float)n*3.5f+0.375f;x16i a26_1;fill(&a26_1,64,n+36);x16f a27_0;"
            "fill(&a27_0,64,n+68);a27_0[0]=(float)n*1.25f;a27_0[15]=-(float)n*3.5f+0.375f;x16f a27_1;fill(&a27_1,64,n+59);a27_1[0]=(float)n*1.25f;"
            "a27_1[15]=-(float)n*3.5f+0.375f;x8f a28_0;fill(&a28_0,32,n+50);a28_0[0]=(float)n*1.25f;a28_0[7]=-(float)n*3.5f+0.375f;x8Oi a28_1;"
            "fill(&a28_1,64,n+39);x8f a29_0;fill(&a29_0,32,n+71);a29_0[0]=(float)n*1.25f;a29_0[7]=-(float)n*3.5f+0.375f;x8Oi a29_1;fill(&a29_1,64,n+60);"
            "x8f a30_0;fill(&a30_0,32,n+92);a30_0[0]=(float)n*1.25f;a30_0[7]=-(float)n*3.5f+0.375f;x8Oi a30_1;fill(&a30_1,64,n+81);x8f a31_0;"
            "fill(&a31_0,32,n+16);a31_0[0]=(float)n*1.25f;a31_0[7]=-(float)n*3.5f+0.375f;x8Oi a31_1;fill(&a31_1,64,n+5);unsigned long long m=mask(n);"
            "{x4d r=__builtin_ia32_rsqrt14pd256_mask(a0_0,a0_1,(unsigned char)(m>>26));FOLD(r);}"
            "{x8d r=__builtin_ia32_rsqrt14pd512_mask(a1_0,a1_1,(unsigned char)(m>>33));FOLD(r);}"
            "{x4f r=__builtin_ia32_rsqrt14ps128_mask(a2_0,a2_1,(unsigned char)(m>>40));FOLD(r);}"
            "{x8f r=__builtin_ia32_rsqrt14ps256_mask(a3_0,a3_1,(unsigned char)(m>>47));FOLD(r);}"
            "{x16f r=__builtin_ia32_rsqrt14ps512_mask(a4_0,a4_1,(unsigned short)(m>>54));FOLD(r);}"
            "{x4f r=__builtin_ia32_vcvtph2ps_mask(a5_0,a5_1,(unsigned char)(m>>0));FOLD(r);}"
            "{x8f r=__builtin_ia32_vcvtph2ps256_mask(a6_0,a6_1,(unsigned char)(m>>7));FOLD(r);}"
            "{x16f r=__builtin_ia32_vcvtph2ps512_mask(a7_0,a7_1,(unsigned short)(m>>14),4);FOLD(r);}"
            "{x16f r=__builtin_ia32_vcvtph2ps512_mask(a7_0,a7_1,(unsigned short)(m>>14),8);FOLD(r);}"
            "{x16f r=__builtin_ia32_vcvtph2ps512_mask(a7_0,a7_1,(unsigned short)(m>>14),12);FOLD(r);}"
            "{x8y r=__builtin_ia32_cvtneps2bf16_128_mask(a8_0,a8_1,(unsigned char)(m>>21));FOLD(r);}"
            "{x8y r=__builtin_ia32_cvtneps2bf16_256_mask(a9_0,a9_1,(unsigned char)(m>>28));FOLD(r);}"
            "{x16y r=__builtin_ia32_cvtneps2bf16_512_mask(a10_0,a10_1,(unsigned short)(m>>35));FOLD(r);}"
            "{x8i r=__builtin_ia32_cvtpd2dq512_mask(a11_0,a11_1,(unsigned char)(m>>42),4);FOLD(r);}"
            "{x8i r=__builtin_ia32_cvtpd2dq512_mask(a11_0,a11_1,(unsigned char)(m>>42),8);FOLD(r);}"
            "{x8i r=__builtin_ia32_cvtpd2dq512_mask(a11_0,a11_1,(unsigned char)(m>>42),9);FOLD(r);}"
            "{x8i r=__builtin_ia32_cvtpd2dq512_mask(a11_0,a11_1,(unsigned char)(m>>42),10);FOLD(r);}"
            "{x8i r=__builtin_ia32_cvtpd2dq512_mask(a11_0,a11_1,(unsigned char)(m>>42),11);FOLD(r);}"
            "{x8f r=__builtin_ia32_cvtpd2ps512_mask(a12_0,a12_1,(unsigned char)(m>>49),4);FOLD(r);}"
            "{x8f r=__builtin_ia32_cvtpd2ps512_mask(a12_0,a12_1,(unsigned char)(m>>49),8);FOLD(r);}"
            "{x8f r=__builtin_ia32_cvtpd2ps512_mask(a12_0,a12_1,(unsigned char)(m>>49),9);FOLD(r);}"
            "{x8f r=__builtin_ia32_cvtpd2ps512_mask(a12_0,a12_1,(unsigned char)(m>>49),10);FOLD(r);}"
            "{x8f r=__builtin_ia32_cvtpd2ps512_mask(a12_0,a12_1,(unsigned char)(m>>49),11);FOLD(r);}"
            "{x8i r=__builtin_ia32_cvtpd2udq512_mask(a13_0,a13_1,(unsigned char)(m>>56),4);FOLD(r);}"
            "{x8i r=__builtin_ia32_cvtpd2udq512_mask(a13_0,a13_1,(unsigned char)(m>>56),8);FOLD(r);}"
            "{x8i r=__builtin_ia32_cvtpd2udq512_mask(a13_0,a13_1,(unsigned char)(m>>56),9);FOLD(r);}"
            "{x8i r=__builtin_ia32_cvtpd2udq512_mask(a13_0,a13_1,(unsigned char)(m>>56),10);FOLD(r);}"
            "{x8i r=__builtin_ia32_cvtpd2udq512_mask(a13_0,a13_1,(unsigned char)(m>>56),11);FOLD(r);}"
            "{x8f r=__builtin_ia32_cvtqq2ps512_mask(a14_0,a14_1,(unsigned char)(m>>2),4);FOLD(r);}"
            "{x8f r=__builtin_ia32_cvtqq2ps512_mask(a14_0,a14_1,(unsigned char)(m>>2),8);FOLD(r);}"
            "{x8f r=__builtin_ia32_cvtqq2ps512_mask(a14_0,a14_1,(unsigned char)(m>>2),9);FOLD(r);}"
            "{x8f r=__builtin_ia32_cvtqq2ps512_mask(a14_0,a14_1,(unsigned char)(m>>2),10);FOLD(r);}"
            "{x8f r=__builtin_ia32_cvtqq2ps512_mask(a14_0,a14_1,(unsigned char)(m>>2),11);FOLD(r);}"
            "{x8i r=__builtin_ia32_cvttpd2dq512_mask(a15_0,a15_1,(unsigned char)(m>>9),4);FOLD(r);}"
            "{x8i r=__builtin_ia32_cvttpd2dq512_mask(a15_0,a15_1,(unsigned char)(m>>9),8);FOLD(r);}"
            "{x8i r=__builtin_ia32_cvttpd2dq512_mask(a15_0,a15_1,(unsigned char)(m>>9),12);FOLD(r);}"
            "{x8i r=__builtin_ia32_cvttpd2udq512_mask(a16_0,a16_1,(unsigned char)(m>>16),4);FOLD(r);}"
            "{x8i r=__builtin_ia32_cvttpd2udq512_mask(a16_0,a16_1,(unsigned char)(m>>16),8);FOLD(r);}"
            "{x8i r=__builtin_ia32_cvttpd2udq512_mask(a16_0,a16_1,(unsigned char)(m>>16),12);FOLD(r);}"
            "{x8f r=__builtin_ia32_cvtuqq2ps512_mask(a17_0,a17_1,(unsigned char)(m>>23),4);FOLD(r);}"
            "{x8f r=__builtin_ia32_cvtuqq2ps512_mask(a17_0,a17_1,(unsigned char)(m>>23),8);FOLD(r);}"
            "{x8f r=__builtin_ia32_cvtuqq2ps512_mask(a17_0,a17_1,(unsigned char)(m>>23),9);FOLD(r);}"
            "{x8f r=__builtin_ia32_cvtuqq2ps512_mask(a17_0,a17_1,(unsigned char)(m>>23),10);FOLD(r);}"
            "{x8f r=__builtin_ia32_cvtuqq2ps512_mask(a17_0,a17_1,(unsigned char)(m>>23),11);FOLD(r);}"
            "{x8Oi r=__builtin_ia32_cvtpd2qq512_mask(a18_0,a18_1,(unsigned char)(m>>30),4);FOLD(r);}"
            "{x8Oi r=__builtin_ia32_cvtpd2qq512_mask(a18_0,a18_1,(unsigned char)(m>>30),8);FOLD(r);}"
            "{x8Oi r=__builtin_ia32_cvtpd2qq512_mask(a18_0,a18_1,(unsigned char)(m>>30),9);FOLD(r);}"
            "{x8Oi r=__builtin_ia32_cvtpd2qq512_mask(a18_0,a18_1,(unsigned char)(m>>30),10);FOLD(r);}"
            "{x8Oi r=__builtin_ia32_cvtpd2qq512_mask(a18_0,a18_1,(unsigned char)(m>>30),11);FOLD(r);}"
            "{x8Oi r=__builtin_ia32_cvtpd2uqq512_mask(a19_0,a19_1,(unsigned char)(m>>37),4);FOLD(r);}"
            "{x8Oi r=__builtin_ia32_cvtpd2uqq512_mask(a19_0,a19_1,(unsigned char)(m>>37),8);FOLD(r);}"
            "{x8Oi r=__builtin_ia32_cvtpd2uqq512_mask(a19_0,a19_1,(unsigned char)(m>>37),9);FOLD(r);}"
            "{x8Oi r=__builtin_ia32_cvtpd2uqq512_mask(a19_0,a19_1,(unsigned char)(m>>37),10);FOLD(r);}"
            "{x8Oi r=__builtin_ia32_cvtpd2uqq512_mask(a19_0,a19_1,(unsigned char)(m>>37),11);FOLD(r);}"
            "{x8Oi r=__builtin_ia32_cvttpd2qq512_mask(a20_0,a20_1,(unsigned char)(m>>44),4);FOLD(r);}"
            "{x8Oi r=__builtin_ia32_cvttpd2qq512_mask(a20_0,a20_1,(unsigned char)(m>>44),8);FOLD(r);}"
            "{x8Oi r=__builtin_ia32_cvttpd2qq512_mask(a20_0,a20_1,(unsigned char)(m>>44),12);FOLD(r);}"
            "{x8Oi r=__builtin_ia32_cvttpd2uqq512_mask(a21_0,a21_1,(unsigned char)(m>>51),4);FOLD(r);}"
            "{x8Oi r=__builtin_ia32_cvttpd2uqq512_mask(a21_0,a21_1,(unsigned char)(m>>51),8);FOLD(r);}"
            "{x8Oi r=__builtin_ia32_cvttpd2uqq512_mask(a21_0,a21_1,(unsigned char)(m>>51),12);FOLD(r);}"
            "{x8d r=__builtin_ia32_getexppd512_mask(a22_0,a22_1,(unsigned char)(m>>58),4);FOLD(r);}"
            "{x8d r=__builtin_ia32_getexppd512_mask(a22_0,a22_1,(unsigned char)(m>>58),8);FOLD(r);}"
            "{x8d r=__builtin_ia32_getexppd512_mask(a22_0,a22_1,(unsigned char)(m>>58),12);FOLD(r);}"
            "{x16i r=__builtin_ia32_cvtps2dq512_mask(a23_0,a23_1,(unsigned short)(m>>4),4);FOLD(r);}"
            "{x16i r=__builtin_ia32_cvtps2dq512_mask(a23_0,a23_1,(unsigned short)(m>>4),8);FOLD(r);}"
            "{x16i r=__builtin_ia32_cvtps2dq512_mask(a23_0,a23_1,(unsigned short)(m>>4),9);FOLD(r);}"
            "{x16i r=__builtin_ia32_cvtps2dq512_mask(a23_0,a23_1,(unsigned short)(m>>4),10);FOLD(r);}"
            "{x16i r=__builtin_ia32_cvtps2dq512_mask(a23_0,a23_1,(unsigned short)(m>>4),11);FOLD(r);}"
            "{x16i r=__builtin_ia32_cvtps2udq512_mask(a24_0,a24_1,(unsigned short)(m>>11),4);FOLD(r);}"
            "{x16i r=__builtin_ia32_cvtps2udq512_mask(a24_0,a24_1,(unsigned short)(m>>11),8);FOLD(r);}"
            "{x16i r=__builtin_ia32_cvtps2udq512_mask(a24_0,a24_1,(unsigned short)(m>>11),9);FOLD(r);}"
            "{x16i r=__builtin_ia32_cvtps2udq512_mask(a24_0,a24_1,(unsigned short)(m>>11),10);FOLD(r);}"
            "{x16i r=__builtin_ia32_cvtps2udq512_mask(a24_0,a24_1,(unsigned short)(m>>11),11);FOLD(r);}"
            "{x16i r=__builtin_ia32_cvttps2dq512_mask(a25_0,a25_1,(unsigned short)(m>>18),4);FOLD(r);}"
            "{x16i r=__builtin_ia32_cvttps2dq512_mask(a25_0,a25_1,(unsigned short)(m>>18),8);FOLD(r);}"
            "{x16i r=__builtin_ia32_cvttps2dq512_mask(a25_0,a25_1,(unsigned short)(m>>18),12);FOLD(r);}"
            "{x16i r=__builtin_ia32_cvttps2udq512_mask(a26_0,a26_1,(unsigned short)(m>>25),4);FOLD(r);}"
            "{x16i r=__builtin_ia32_cvttps2udq512_mask(a26_0,a26_1,(unsigned short)(m>>25),8);FOLD(r);}"
            "{x16i r=__builtin_ia32_cvttps2udq512_mask(a26_0,a26_1,(unsigned short)(m>>25),12);FOLD(r);}"
            "{x16f r=__builtin_ia32_getexpps512_mask(a27_0,a27_1,(unsigned short)(m>>32),4);FOLD(r);}"
            "{x16f r=__builtin_ia32_getexpps512_mask(a27_0,a27_1,(unsigned short)(m>>32),8);FOLD(r);}"
            "{x16f r=__builtin_ia32_getexpps512_mask(a27_0,a27_1,(unsigned short)(m>>32),12);FOLD(r);}"
            "{x8Oi r=__builtin_ia32_cvtps2qq512_mask(a28_0,a28_1,(unsigned char)(m>>39),4);FOLD(r);}"
            "{x8Oi r=__builtin_ia32_cvtps2qq512_mask(a28_0,a28_1,(unsigned char)(m>>39),8);FOLD(r);}"
            "{x8Oi r=__builtin_ia32_cvtps2qq512_mask(a28_0,a28_1,(unsigned char)(m>>39),9);FOLD(r);}"
            "{x8Oi r=__builtin_ia32_cvtps2qq512_mask(a28_0,a28_1,(unsigned char)(m>>39),10);FOLD(r);}"
            "{x8Oi r=__builtin_ia32_cvtps2qq512_mask(a28_0,a28_1,(unsigned char)(m>>39),11);FOLD(r);}"
            "{x8Oi r=__builtin_ia32_cvtps2uqq512_mask(a29_0,a29_1,(unsigned char)(m>>46),4);FOLD(r);}"
            "{x8Oi r=__builtin_ia32_cvtps2uqq512_mask(a29_0,a29_1,(unsigned char)(m>>46),8);FOLD(r);}"
            "{x8Oi r=__builtin_ia32_cvtps2uqq512_mask(a29_0,a29_1,(unsigned char)(m>>46),9);FOLD(r);}"
            "{x8Oi r=__builtin_ia32_cvtps2uqq512_mask(a29_0,a29_1,(unsigned char)(m>>46),10);FOLD(r);}"
            "{x8Oi r=__builtin_ia32_cvtps2uqq512_mask(a29_0,a29_1,(unsigned char)(m>>46),11);FOLD(r);}"
            "{x8Oi r=__builtin_ia32_cvttps2qq512_mask(a30_0,a30_1,(unsigned char)(m>>53),4);FOLD(r);}"
            "{x8Oi r=__builtin_ia32_cvttps2qq512_mask(a30_0,a30_1,(unsigned char)(m>>53),8);FOLD(r);}"
            "{x8Oi r=__builtin_ia32_cvttps2qq512_mask(a30_0,a30_1,(unsigned char)(m>>53),12);FOLD(r);}"
            "{x8Oi r=__builtin_ia32_cvttps2uqq512_mask(a31_0,a31_1,(unsigned char)(m>>60),4);FOLD(r);}"
            "{x8Oi r=__builtin_ia32_cvttps2uqq512_mask(a31_0,a31_1,(unsigned char)(m>>60),8);FOLD(r);}"
            "{x8Oi r=__builtin_ia32_cvttps2uqq512_mask(a31_0,a31_1,(unsigned char)(m>>60),12);FOLD(r);}return (int)h;}",
        },
        {
            // Batch 2's mask-merge rules: each builtin at its prototype's types, by both paths against clang -O0's code for
            // znver5.
            "x86-mask-merge",
            "typedef double x2d __attribute__((vector_size(16)));typedef double x4d __attribute__((vector_size(32)));"
            "typedef float x4f __attribute__((vector_size(16)));typedef double x8d __attribute__((vector_size(64)));"
            "typedef float x8f __attribute__((vector_size(32)));typedef float x16f __attribute__((vector_size(64)));"
            "int tested(int n){unsigned h=2166136261u;x2d a0_0;fill(&a0_0,16,n+60);a0_0[0]=(double)n*1.25;a0_0[1]=-(double)n*3.5+0.375;x2d a0_1;"
            "fill(&a0_1,16,n+44);a0_1[0]=(double)n*1.25;a0_1[1]=-(double)n*3.5+0.375;x2d a0_2;fill(&a0_2,16,n+28);a0_2[0]=(double)n*1.25;"
            "a0_2[1]=-(double)n*3.5+0.375;x4d a1_0;fill(&a1_0,32,n+12);a1_0[0]=(double)n*1.25;a1_0[3]=-(double)n*3.5+0.375;x4d a1_1;fill(&a1_1,32,n+93);"
            "a1_1[0]=(double)n*1.25;a1_1[3]=-(double)n*3.5+0.375;x4d a1_2;fill(&a1_2,32,n+77);a1_2[0]=(double)n*1.25;a1_2[3]=-(double)n*3.5+0.375;"
            "x8d a2_0;fill(&a2_0,64,n+61);a2_0[0]=(double)n*1.25;a2_0[7]=-(double)n*3.5+0.375;x8d a2_1;fill(&a2_1,64,n+45);a2_1[0]=(double)n*1.25;"
            "a2_1[7]=-(double)n*3.5+0.375;x8d a2_2;fill(&a2_2,64,n+29);a2_2[0]=(double)n*1.25;a2_2[7]=-(double)n*3.5+0.375;x4f a3_0;fill(&a3_0,16,n+13);"
            "a3_0[0]=(float)n*1.25f;a3_0[3]=-(float)n*3.5f+0.375f;x4f a3_1;fill(&a3_1,16,n+95);a3_1[0]=(float)n*1.25f;a3_1[3]=-(float)n*3.5f+0.375f;"
            "x4f a3_2;fill(&a3_2,16,n+80);a3_2[0]=(float)n*1.25f;a3_2[3]=-(float)n*3.5f+0.375f;x8f a4_0;fill(&a4_0,32,n+65);a4_0[0]=(float)n*1.25f;"
            "a4_0[7]=-(float)n*3.5f+0.375f;x8f a4_1;fill(&a4_1,32,n+50);a4_1[0]=(float)n*1.25f;a4_1[7]=-(float)n*3.5f+0.375f;x8f a4_2;"
            "fill(&a4_2,32,n+35);a4_2[0]=(float)n*1.25f;a4_2[7]=-(float)n*3.5f+0.375f;x16f a5_0;fill(&a5_0,64,n+20);a5_0[0]=(float)n*1.25f;"
            "a5_0[15]=-(float)n*3.5f+0.375f;x16f a5_1;fill(&a5_1,64,n+7);a5_1[0]=(float)n*1.25f;a5_1[15]=-(float)n*3.5f+0.375f;x16f a5_2;"
            "fill(&a5_2,64,n+90);a5_2[0]=(float)n*1.25f;a5_2[15]=-(float)n*3.5f+0.375f;x2d a6_0;fill(&a6_0,16,n+77);a6_0[0]=(double)n*1.25;"
            "a6_0[1]=-(double)n*3.5+0.375;x2d a6_1;fill(&a6_1,16,n+61);a6_1[0]=(double)n*1.25;a6_1[1]=-(double)n*3.5+0.375;x2d a6_2;fill(&a6_2,16,n+45);"
            "a6_2[0]=(double)n*1.25;a6_2[1]=-(double)n*3.5+0.375;x4f a7_0;fill(&a7_0,16,n+29);a7_0[0]=(float)n*1.25f;a7_0[3]=-(float)n*3.5f+0.375f;"
            "x4f a7_1;fill(&a7_1,16,n+14);a7_1[0]=(float)n*1.25f;a7_1[3]=-(float)n*3.5f+0.375f;x4f a7_2;fill(&a7_2,16,n+96);a7_2[0]=(float)n*1.25f;"
            "a7_2[3]=-(float)n*3.5f+0.375f;x2d a8_0;fill(&a8_0,16,n+81);a8_0[0]=(double)n*1.25;a8_0[1]=-(double)n*3.5+0.375;x2d a8_1;fill(&a8_1,16,n+65);"
            "a8_1[0]=(double)n*1.25;a8_1[1]=-(double)n*3.5+0.375;x2d a8_2;fill(&a8_2,16,n+49);a8_2[0]=(double)n*1.25;a8_2[1]=-(double)n*3.5+0.375;"
            "x4f a9_0;fill(&a9_0,16,n+33);a9_0[0]=(float)n*1.25f;a9_0[3]=-(float)n*3.5f+0.375f;x4f a9_1;fill(&a9_1,16,n+18);a9_1[0]=(float)n*1.25f;"
            "a9_1[3]=-(float)n*3.5f+0.375f;x4f a9_2;fill(&a9_2,16,n+3);a9_2[0]=(float)n*1.25f;a9_2[3]=-(float)n*3.5f+0.375f;x2d a10_0;"
            "fill(&a10_0,16,n+84);a10_0[0]=(double)n*1.25;a10_0[1]=-(double)n*3.5+0.375;x2d a10_1;fill(&a10_1,16,n+72);a10_1[0]=(double)n*1.25;"
            "a10_1[1]=-(double)n*3.5+0.375;x2d a10_2;fill(&a10_2,16,n+60);a10_2[0]=(double)n*1.25;a10_2[1]=-(double)n*3.5+0.375;x4f a11_0;"
            "fill(&a11_0,16,n+48);a11_0[0]=(float)n*1.25f;a11_0[3]=-(float)n*3.5f+0.375f;x4f a11_1;fill(&a11_1,16,n+37);a11_1[0]=(float)n*1.25f;"
            "a11_1[3]=-(float)n*3.5f+0.375f;x4f a11_2;fill(&a11_2,16,n+26);a11_2[0]=(float)n*1.25f;a11_2[3]=-(float)n*3.5f+0.375f;x2d a12_0;"
            "fill(&a12_0,16,n+15);a12_0[0]=(double)n*1.25;a12_0[1]=-(double)n*3.5+0.375;x2d a12_1;fill(&a12_1,16,n+3);a12_1[0]=(double)n*1.25;"
            "a12_1[1]=-(double)n*3.5+0.375;x2d a12_2;fill(&a12_2,16,n+87);a12_2[0]=(double)n*1.25;a12_2[1]=-(double)n*3.5+0.375;x4f a13_0;"
            "fill(&a13_0,16,n+75);a13_0[0]=(float)n*1.25f;a13_0[3]=-(float)n*3.5f+0.375f;x4f a13_1;fill(&a13_1,16,n+64);a13_1[0]=(float)n*1.25f;"
            "a13_1[3]=-(float)n*3.5f+0.375f;x4f a13_2;fill(&a13_2,16,n+53);a13_2[0]=(float)n*1.25f;a13_2[3]=-(float)n*3.5f+0.375f;x2d a14_0;"
            "fill(&a14_0,16,n+42);a14_0[0]=(double)n*1.25;a14_0[1]=-(double)n*3.5+0.375;x2d a14_1;fill(&a14_1,16,n+30);a14_1[0]=(double)n*1.25;"
            "a14_1[1]=-(double)n*3.5+0.375;x2d a14_2;fill(&a14_2,16,n+18);a14_2[0]=(double)n*1.25;a14_2[1]=-(double)n*3.5+0.375;x4f a15_0;"
            "fill(&a15_0,16,n+6);a15_0[0]=(float)n*1.25f;a15_0[3]=-(float)n*3.5f+0.375f;x4f a15_1;fill(&a15_1,16,n+91);a15_1[0]=(float)n*1.25f;"
            "a15_1[3]=-(float)n*3.5f+0.375f;x4f a15_2;fill(&a15_2,16,n+80);a15_2[0]=(float)n*1.25f;a15_2[3]=-(float)n*3.5f+0.375f;x2d a16_0;"
            "fill(&a16_0,16,n+69);a16_0[0]=(double)n*1.25;a16_0[1]=-(double)n*3.5+0.375;x2d a16_1;fill(&a16_1,16,n+57);a16_1[0]=(double)n*1.25;"
            "a16_1[1]=-(double)n*3.5+0.375;x2d a16_2;fill(&a16_2,16,n+45);a16_2[0]=(double)n*1.25;a16_2[1]=-(double)n*3.5+0.375;x4f a17_0;"
            "fill(&a17_0,16,n+33);a17_0[0]=(float)n*1.25f;a17_0[3]=-(float)n*3.5f+0.375f;x4f a17_1;fill(&a17_1,16,n+22);a17_1[0]=(float)n*1.25f;"
            "a17_1[3]=-(float)n*3.5f+0.375f;x4f a17_2;fill(&a17_2,16,n+11);a17_2[0]=(float)n*1.25f;a17_2[3]=-(float)n*3.5f+0.375f;x2d a18_0;"
            "fill(&a18_0,16,n+0);a18_0[0]=(double)n*1.25;a18_0[1]=-(double)n*3.5+0.375;x2d a18_1;fill(&a18_1,16,n+84);a18_1[0]=(double)n*1.25;"
            "a18_1[1]=-(double)n*3.5+0.375;x2d a18_2;fill(&a18_2,16,n+72);a18_2[0]=(double)n*1.25;a18_2[1]=-(double)n*3.5+0.375;x4f a19_0;"
            "fill(&a19_0,16,n+60);a19_0[0]=(float)n*1.25f;a19_0[3]=-(float)n*3.5f+0.375f;x4f a19_1;fill(&a19_1,16,n+49);a19_1[0]=(float)n*1.25f;"
            "a19_1[3]=-(float)n*3.5f+0.375f;x4f a19_2;fill(&a19_2,16,n+38);a19_2[0]=(float)n*1.25f;a19_2[3]=-(float)n*3.5f+0.375f;x4f a20_0;"
            "fill(&a20_0,16,n+27);a20_0[0]=(float)n*1.25f;a20_0[3]=-(float)n*3.5f+0.375f;x2d a20_1;fill(&a20_1,16,n+16);a20_1[0]=(double)n*1.25;"
            "a20_1[1]=-(double)n*3.5+0.375;x4f a20_2;fill(&a20_2,16,n+4);a20_2[0]=(float)n*1.25f;a20_2[3]=-(float)n*3.5f+0.375f;x2d a21_0;"
            "fill(&a21_0,16,n+89);a21_0[0]=(double)n*1.25;a21_0[1]=-(double)n*3.5+0.375;x4f a21_1;fill(&a21_1,16,n+77);a21_1[0]=(float)n*1.25f;"
            "a21_1[3]=-(float)n*3.5f+0.375f;x2d a21_2;fill(&a21_2,16,n+66);a21_2[0]=(double)n*1.25;a21_2[1]=-(double)n*3.5+0.375;"
            "unsigned long long m=mask(n);{x2d r=__builtin_ia32_scalefpd128_mask(a0_0,a0_1,a0_2,(unsigned char)(m>>39));FOLD(r);}"
            "{x4d r=__builtin_ia32_scalefpd256_mask(a1_0,a1_1,a1_2,(unsigned char)(m>>46));FOLD(r);}"
            "{x8d r=__builtin_ia32_scalefpd512_mask(a2_0,a2_1,a2_2,(unsigned char)(m>>53),4);FOLD(r);}"
            "{x8d r=__builtin_ia32_scalefpd512_mask(a2_0,a2_1,a2_2,(unsigned char)(m>>53),8);FOLD(r);}"
            "{x8d r=__builtin_ia32_scalefpd512_mask(a2_0,a2_1,a2_2,(unsigned char)(m>>53),9);FOLD(r);}"
            "{x8d r=__builtin_ia32_scalefpd512_mask(a2_0,a2_1,a2_2,(unsigned char)(m>>53),10);FOLD(r);}"
            "{x8d r=__builtin_ia32_scalefpd512_mask(a2_0,a2_1,a2_2,(unsigned char)(m>>53),11);FOLD(r);}"
            "{x4f r=__builtin_ia32_scalefps128_mask(a3_0,a3_1,a3_2,(unsigned char)(m>>60));FOLD(r);}"
            "{x8f r=__builtin_ia32_scalefps256_mask(a4_0,a4_1,a4_2,(unsigned char)(m>>6));FOLD(r);}"
            "{x16f r=__builtin_ia32_scalefps512_mask(a5_0,a5_1,a5_2,(unsigned short)(m>>13),4);FOLD(r);}"
            "{x16f r=__builtin_ia32_scalefps512_mask(a5_0,a5_1,a5_2,(unsigned short)(m>>13),8);FOLD(r);}"
            "{x16f r=__builtin_ia32_scalefps512_mask(a5_0,a5_1,a5_2,(unsigned short)(m>>13),9);FOLD(r);}"
            "{x16f r=__builtin_ia32_scalefps512_mask(a5_0,a5_1,a5_2,(unsigned short)(m>>13),10);FOLD(r);}"
            "{x16f r=__builtin_ia32_scalefps512_mask(a5_0,a5_1,a5_2,(unsigned short)(m>>13),11);FOLD(r);}"
            "{x2d r=__builtin_ia32_rcp14sd_mask(a6_0,a6_1,a6_2,(unsigned char)(m>>20));FOLD(r);}"
            "{x4f r=__builtin_ia32_rcp14ss_mask(a7_0,a7_1,a7_2,(unsigned char)(m>>27));FOLD(r);}"
            "{x2d r=__builtin_ia32_rsqrt14sd_mask(a8_0,a8_1,a8_2,(unsigned char)(m>>34));FOLD(r);}"
            "{x4f r=__builtin_ia32_rsqrt14ss_mask(a9_0,a9_1,a9_2,(unsigned char)(m>>41));FOLD(r);}"
            "{x2d r=__builtin_ia32_maxsd_round_mask(a10_0,a10_1,a10_2,(unsigned char)(m>>48),4);FOLD(r);}"
            "{x2d r=__builtin_ia32_maxsd_round_mask(a10_0,a10_1,a10_2,(unsigned char)(m>>48),8);FOLD(r);}"
            "{x2d r=__builtin_ia32_maxsd_round_mask(a10_0,a10_1,a10_2,(unsigned char)(m>>48),12);FOLD(r);}"
            "{x4f r=__builtin_ia32_maxss_round_mask(a11_0,a11_1,a11_2,(unsigned char)(m>>55),4);FOLD(r);}"
            "{x4f r=__builtin_ia32_maxss_round_mask(a11_0,a11_1,a11_2,(unsigned char)(m>>55),8);FOLD(r);}"
            "{x4f r=__builtin_ia32_maxss_round_mask(a11_0,a11_1,a11_2,(unsigned char)(m>>55),12);FOLD(r);}"
            "{x2d r=__builtin_ia32_minsd_round_mask(a12_0,a12_1,a12_2,(unsigned char)(m>>1),4);FOLD(r);}"
            "{x2d r=__builtin_ia32_minsd_round_mask(a12_0,a12_1,a12_2,(unsigned char)(m>>1),8);FOLD(r);}"
            "{x2d r=__builtin_ia32_minsd_round_mask(a12_0,a12_1,a12_2,(unsigned char)(m>>1),12);FOLD(r);}"
            "{x4f r=__builtin_ia32_minss_round_mask(a13_0,a13_1,a13_2,(unsigned char)(m>>8),4);FOLD(r);}"
            "{x4f r=__builtin_ia32_minss_round_mask(a13_0,a13_1,a13_2,(unsigned char)(m>>8),8);FOLD(r);}"
            "{x4f r=__builtin_ia32_minss_round_mask(a13_0,a13_1,a13_2,(unsigned char)(m>>8),12);FOLD(r);}"
            "{x2d r=__builtin_ia32_getexpsd128_round_mask(a14_0,a14_1,a14_2,(unsigned char)(m>>15),4);FOLD(r);}"
            "{x2d r=__builtin_ia32_getexpsd128_round_mask(a14_0,a14_1,a14_2,(unsigned char)(m>>15),8);FOLD(r);}"
            "{x2d r=__builtin_ia32_getexpsd128_round_mask(a14_0,a14_1,a14_2,(unsigned char)(m>>15),12);FOLD(r);}"
            "{x4f r=__builtin_ia32_getexpss128_round_mask(a15_0,a15_1,a15_2,(unsigned char)(m>>22),4);FOLD(r);}"
            "{x4f r=__builtin_ia32_getexpss128_round_mask(a15_0,a15_1,a15_2,(unsigned char)(m>>22),8);FOLD(r);}"
            "{x4f r=__builtin_ia32_getexpss128_round_mask(a15_0,a15_1,a15_2,(unsigned char)(m>>22),12);FOLD(r);}"
            "{x2d r=__builtin_ia32_scalefsd_round_mask(a16_0,a16_1,a16_2,(unsigned char)(m>>29),4);FOLD(r);}"
            "{x2d r=__builtin_ia32_scalefsd_round_mask(a16_0,a16_1,a16_2,(unsigned char)(m>>29),8);FOLD(r);}"
            "{x2d r=__builtin_ia32_scalefsd_round_mask(a16_0,a16_1,a16_2,(unsigned char)(m>>29),9);FOLD(r);}"
            "{x2d r=__builtin_ia32_scalefsd_round_mask(a16_0,a16_1,a16_2,(unsigned char)(m>>29),10);FOLD(r);}"
            "{x2d r=__builtin_ia32_scalefsd_round_mask(a16_0,a16_1,a16_2,(unsigned char)(m>>29),11);FOLD(r);}"
            "{x4f r=__builtin_ia32_scalefss_round_mask(a17_0,a17_1,a17_2,(unsigned char)(m>>36),4);FOLD(r);}"
            "{x4f r=__builtin_ia32_scalefss_round_mask(a17_0,a17_1,a17_2,(unsigned char)(m>>36),8);FOLD(r);}"
            "{x4f r=__builtin_ia32_scalefss_round_mask(a17_0,a17_1,a17_2,(unsigned char)(m>>36),9);FOLD(r);}"
            "{x4f r=__builtin_ia32_scalefss_round_mask(a17_0,a17_1,a17_2,(unsigned char)(m>>36),10);FOLD(r);}"
            "{x4f r=__builtin_ia32_scalefss_round_mask(a17_0,a17_1,a17_2,(unsigned char)(m>>36),11);FOLD(r);}"
            "{x2d r=__builtin_ia32_sqrtsd_round_mask(a18_0,a18_1,a18_2,(unsigned char)(m>>43),4);FOLD(r);}"
            "{x2d r=__builtin_ia32_sqrtsd_round_mask(a18_0,a18_1,a18_2,(unsigned char)(m>>43),8);FOLD(r);}"
            "{x2d r=__builtin_ia32_sqrtsd_round_mask(a18_0,a18_1,a18_2,(unsigned char)(m>>43),9);FOLD(r);}"
            "{x2d r=__builtin_ia32_sqrtsd_round_mask(a18_0,a18_1,a18_2,(unsigned char)(m>>43),10);FOLD(r);}"
            "{x2d r=__builtin_ia32_sqrtsd_round_mask(a18_0,a18_1,a18_2,(unsigned char)(m>>43),11);FOLD(r);}"
            "{x4f r=__builtin_ia32_sqrtss_round_mask(a19_0,a19_1,a19_2,(unsigned char)(m>>50),4);FOLD(r);}"
            "{x4f r=__builtin_ia32_sqrtss_round_mask(a19_0,a19_1,a19_2,(unsigned char)(m>>50),8);FOLD(r);}"
            "{x4f r=__builtin_ia32_sqrtss_round_mask(a19_0,a19_1,a19_2,(unsigned char)(m>>50),9);FOLD(r);}"
            "{x4f r=__builtin_ia32_sqrtss_round_mask(a19_0,a19_1,a19_2,(unsigned char)(m>>50),10);FOLD(r);}"
            "{x4f r=__builtin_ia32_sqrtss_round_mask(a19_0,a19_1,a19_2,(unsigned char)(m>>50),11);FOLD(r);}"
            "{x4f r=__builtin_ia32_cvtsd2ss_round_mask(a20_0,a20_1,a20_2,(unsigned char)(m>>57),4);FOLD(r);}"
            "{x4f r=__builtin_ia32_cvtsd2ss_round_mask(a20_0,a20_1,a20_2,(unsigned char)(m>>57),8);FOLD(r);}"
            "{x4f r=__builtin_ia32_cvtsd2ss_round_mask(a20_0,a20_1,a20_2,(unsigned char)(m>>57),9);FOLD(r);}"
            "{x4f r=__builtin_ia32_cvtsd2ss_round_mask(a20_0,a20_1,a20_2,(unsigned char)(m>>57),10);FOLD(r);}"
            "{x4f r=__builtin_ia32_cvtsd2ss_round_mask(a20_0,a20_1,a20_2,(unsigned char)(m>>57),11);FOLD(r);}"
            "{x2d r=__builtin_ia32_cvtss2sd_round_mask(a21_0,a21_1,a21_2,(unsigned char)(m>>3),4);FOLD(r);}"
            "{x2d r=__builtin_ia32_cvtss2sd_round_mask(a21_0,a21_1,a21_2,(unsigned char)(m>>3),8);FOLD(r);}"
            "{x2d r=__builtin_ia32_cvtss2sd_round_mask(a21_0,a21_1,a21_2,(unsigned char)(m>>3),12);FOLD(r);}return (int)h;}",
        },
        {
            // Batch 2's round rules: each builtin at its prototype's types, by both paths against clang -O0's code for znver5.
            "x86-round",
            "typedef double x8d __attribute__((vector_size(64)));typedef float x16f __attribute__((vector_size(64)));"
            "int tested(int n){unsigned h=2166136261u;x8d a0_0;fill(&a0_0,64,n+48);a0_0[0]=(double)n*1.25;a0_0[7]=-(double)n*3.5+0.375;x8d a0_1;"
            "fill(&a0_1,64,n+32);a0_1[0]=(double)n*1.25;a0_1[7]=-(double)n*3.5+0.375;x16f a1_0;fill(&a1_0,64,n+16);a1_0[0]=(float)n*1.25f;"
            "a1_0[15]=-(float)n*3.5f+0.375f;x16f a1_1;fill(&a1_1,64,n+3);a1_1[0]=(float)n*1.25f;a1_1[15]=-(float)n*3.5f+0.375f;x8d a2_0;"
            "fill(&a2_0,64,n+86);a2_0[0]=(double)n*1.25;a2_0[7]=-(double)n*3.5+0.375;x8d a2_1;fill(&a2_1,64,n+70);a2_1[0]=(double)n*1.25;"
            "a2_1[7]=-(double)n*3.5+0.375;x16f a3_0;fill(&a3_0,64,n+54);a3_0[0]=(float)n*1.25f;a3_0[15]=-(float)n*3.5f+0.375f;x16f a3_1;"
            "fill(&a3_1,64,n+41);a3_1[0]=(float)n*1.25f;a3_1[15]=-(float)n*3.5f+0.375f;unsigned long long m=mask(n);"
            "{x8d r=__builtin_ia32_maxpd512(a0_0,a0_1,4);FOLD(r);}{x8d r=__builtin_ia32_maxpd512(a0_0,a0_1,8);FOLD(r);}"
            "{x8d r=__builtin_ia32_maxpd512(a0_0,a0_1,12);FOLD(r);}{x16f r=__builtin_ia32_maxps512(a1_0,a1_1,4);FOLD(r);}"
            "{x16f r=__builtin_ia32_maxps512(a1_0,a1_1,8);FOLD(r);}{x16f r=__builtin_ia32_maxps512(a1_0,a1_1,12);FOLD(r);}"
            "{x8d r=__builtin_ia32_minpd512(a2_0,a2_1,4);FOLD(r);}{x8d r=__builtin_ia32_minpd512(a2_0,a2_1,8);FOLD(r);}"
            "{x8d r=__builtin_ia32_minpd512(a2_0,a2_1,12);FOLD(r);}{x16f r=__builtin_ia32_minps512(a3_0,a3_1,4);FOLD(r);}"
            "{x16f r=__builtin_ia32_minps512(a3_0,a3_1,8);FOLD(r);}{x16f r=__builtin_ia32_minps512(a3_0,a3_1,12);FOLD(r);}return (int)h;}",
        },
        {
            // Batch 2's predicates rules: each builtin at its prototype's types, by both paths against clang -O0's code for
            // znver5.
            "x86-predicates",
            "typedef double x2d __attribute__((vector_size(16)));typedef float x4f __attribute__((vector_size(16)));"
            "int tested(int n){unsigned h=2166136261u;x4f a0_0;fill(&a0_0,16,n+47);a0_0[0]=(float)n*1.25f;a0_0[3]=-(float)n*3.5f+0.375f;x4f a0_1;"
            "fill(&a0_1,16,n+32);a0_1[0]=(float)n*1.25f;a0_1[3]=-(float)n*3.5f+0.375f;x4f a1_0;fill(&a1_0,16,n+17);a1_0[0]=(float)n*1.25f;"
            "a1_0[3]=-(float)n*3.5f+0.375f;x4f a1_1;fill(&a1_1,16,n+2);a1_1[0]=(float)n*1.25f;a1_1[3]=-(float)n*3.5f+0.375f;x4f a2_0;fill(&a2_0,16,n+83);"
            "a2_0[0]=(float)n*1.25f;a2_0[3]=-(float)n*3.5f+0.375f;x4f a2_1;fill(&a2_1,16,n+68);a2_1[0]=(float)n*1.25f;a2_1[3]=-(float)n*3.5f+0.375f;"
            "x4f a3_0;fill(&a3_0,16,n+53);a3_0[0]=(float)n*1.25f;a3_0[3]=-(float)n*3.5f+0.375f;x4f a3_1;fill(&a3_1,16,n+38);a3_1[0]=(float)n*1.25f;"
            "a3_1[3]=-(float)n*3.5f+0.375f;x4f a4_0;fill(&a4_0,16,n+23);a4_0[0]=(float)n*1.25f;a4_0[3]=-(float)n*3.5f+0.375f;x4f a4_1;fill(&a4_1,16,n+8);"
            "a4_1[0]=(float)n*1.25f;a4_1[3]=-(float)n*3.5f+0.375f;x4f a5_0;fill(&a5_0,16,n+89);a5_0[0]=(float)n*1.25f;a5_0[3]=-(float)n*3.5f+0.375f;"
            "x4f a5_1;fill(&a5_1,16,n+74);a5_1[0]=(float)n*1.25f;a5_1[3]=-(float)n*3.5f+0.375f;x4f a6_0;fill(&a6_0,16,n+59);a6_0[0]=(float)n*1.25f;"
            "a6_0[3]=-(float)n*3.5f+0.375f;x4f a6_1;fill(&a6_1,16,n+44);a6_1[0]=(float)n*1.25f;a6_1[3]=-(float)n*3.5f+0.375f;x4f a7_0;"
            "fill(&a7_0,16,n+29);a7_0[0]=(float)n*1.25f;a7_0[3]=-(float)n*3.5f+0.375f;x4f a7_1;fill(&a7_1,16,n+14);a7_1[0]=(float)n*1.25f;"
            "a7_1[3]=-(float)n*3.5f+0.375f;x2d a8_0;fill(&a8_0,16,n+96);a8_0[0]=(double)n*1.25;a8_0[1]=-(double)n*3.5+0.375;x2d a8_1;fill(&a8_1,16,n+80);"
            "a8_1[0]=(double)n*1.25;a8_1[1]=-(double)n*3.5+0.375;x2d a9_0;fill(&a9_0,16,n+64);a9_0[0]=(double)n*1.25;a9_0[1]=-(double)n*3.5+0.375;"
            "x2d a9_1;fill(&a9_1,16,n+48);a9_1[0]=(double)n*1.25;a9_1[1]=-(double)n*3.5+0.375;x2d a10_0;fill(&a10_0,16,n+32);a10_0[0]=(double)n*1.25;"
            "a10_0[1]=-(double)n*3.5+0.375;x2d a10_1;fill(&a10_1,16,n+20);a10_1[0]=(double)n*1.25;a10_1[1]=-(double)n*3.5+0.375;x2d a11_0;"
            "fill(&a11_0,16,n+8);a11_0[0]=(double)n*1.25;a11_0[1]=-(double)n*3.5+0.375;x2d a11_1;fill(&a11_1,16,n+92);a11_1[0]=(double)n*1.25;"
            "a11_1[1]=-(double)n*3.5+0.375;x2d a12_0;fill(&a12_0,16,n+80);a12_0[0]=(double)n*1.25;a12_0[1]=-(double)n*3.5+0.375;x2d a12_1;"
            "fill(&a12_1,16,n+68);a12_1[0]=(double)n*1.25;a12_1[1]=-(double)n*3.5+0.375;x2d a13_0;fill(&a13_0,16,n+56);a13_0[0]=(double)n*1.25;"
            "a13_0[1]=-(double)n*3.5+0.375;x2d a13_1;fill(&a13_1,16,n+44);a13_1[0]=(double)n*1.25;a13_1[1]=-(double)n*3.5+0.375;x2d a14_0;"
            "fill(&a14_0,16,n+32);a14_0[0]=(double)n*1.25;a14_0[1]=-(double)n*3.5+0.375;x2d a14_1;fill(&a14_1,16,n+20);a14_1[0]=(double)n*1.25;"
            "a14_1[1]=-(double)n*3.5+0.375;x2d a15_0;fill(&a15_0,16,n+8);a15_0[0]=(double)n*1.25;a15_0[1]=-(double)n*3.5+0.375;x2d a15_1;"
            "fill(&a15_1,16,n+92);a15_1[0]=(double)n*1.25;a15_1[1]=-(double)n*3.5+0.375;x4f a16_0;fill(&a16_0,16,n+80);a16_0[0]=(float)n*1.25f;"
            "a16_0[3]=-(float)n*3.5f+0.375f;x4f a16_1;fill(&a16_1,16,n+69);a16_1[0]=(float)n*1.25f;a16_1[3]=-(float)n*3.5f+0.375f;x4f a17_0;"
            "fill(&a17_0,16,n+58);a17_0[0]=(float)n*1.25f;a17_0[3]=-(float)n*3.5f+0.375f;x4f a17_1;fill(&a17_1,16,n+47);a17_1[0]=(float)n*1.25f;"
            "a17_1[3]=-(float)n*3.5f+0.375f;x4f a18_0;fill(&a18_0,16,n+36);a18_0[0]=(float)n*1.25f;a18_0[3]=-(float)n*3.5f+0.375f;x4f a18_1;"
            "fill(&a18_1,16,n+25);a18_1[0]=(float)n*1.25f;a18_1[3]=-(float)n*3.5f+0.375f;x4f a19_0;fill(&a19_0,16,n+14);a19_0[0]=(float)n*1.25f;"
            "a19_0[3]=-(float)n*3.5f+0.375f;x4f a19_1;fill(&a19_1,16,n+3);a19_1[0]=(float)n*1.25f;a19_1[3]=-(float)n*3.5f+0.375f;x4f a20_0;"
            "fill(&a20_0,16,n+88);a20_0[0]=(float)n*1.25f;a20_0[3]=-(float)n*3.5f+0.375f;x4f a20_1;fill(&a20_1,16,n+77);a20_1[0]=(float)n*1.25f;"
            "a20_1[3]=-(float)n*3.5f+0.375f;x4f a21_0;fill(&a21_0,16,n+66);a21_0[0]=(float)n*1.25f;a21_0[3]=-(float)n*3.5f+0.375f;x4f a21_1;"
            "fill(&a21_1,16,n+55);a21_1[0]=(float)n*1.25f;a21_1[3]=-(float)n*3.5f+0.375f;x4f a22_0;fill(&a22_0,16,n+44);a22_0[0]=(float)n*1.25f;"
            "a22_0[3]=-(float)n*3.5f+0.375f;x4f a22_1;fill(&a22_1,16,n+33);a22_1[0]=(float)n*1.25f;a22_1[3]=-(float)n*3.5f+0.375f;x4f a23_0;"
            "fill(&a23_0,16,n+22);a23_0[0]=(float)n*1.25f;a23_0[3]=-(float)n*3.5f+0.375f;x4f a23_1;fill(&a23_1,16,n+11);a23_1[0]=(float)n*1.25f;"
            "a23_1[3]=-(float)n*3.5f+0.375f;x2d a24_0;fill(&a24_0,16,n+0);a24_0[0]=(double)n*1.25;a24_0[1]=-(double)n*3.5+0.375;x2d a24_1;"
            "fill(&a24_1,16,n+84);a24_1[0]=(double)n*1.25;a24_1[1]=-(double)n*3.5+0.375;x2d a25_0;fill(&a25_0,16,n+72);a25_0[0]=(double)n*1.25;"
            "a25_0[1]=-(double)n*3.5+0.375;x2d a25_1;fill(&a25_1,16,n+60);a25_1[0]=(double)n*1.25;a25_1[1]=-(double)n*3.5+0.375;x2d a26_0;"
            "fill(&a26_0,16,n+48);a26_0[0]=(double)n*1.25;a26_0[1]=-(double)n*3.5+0.375;x2d a26_1;fill(&a26_1,16,n+36);a26_1[0]=(double)n*1.25;"
            "a26_1[1]=-(double)n*3.5+0.375;x2d a27_0;fill(&a27_0,16,n+24);a27_0[0]=(double)n*1.25;a27_0[1]=-(double)n*3.5+0.375;x2d a27_1;"
            "fill(&a27_1,16,n+12);a27_1[0]=(double)n*1.25;a27_1[1]=-(double)n*3.5+0.375;x2d a28_0;fill(&a28_0,16,n+0);a28_0[0]=(double)n*1.25;"
            "a28_0[1]=-(double)n*3.5+0.375;x2d a28_1;fill(&a28_1,16,n+84);a28_1[0]=(double)n*1.25;a28_1[1]=-(double)n*3.5+0.375;x2d a29_0;"
            "fill(&a29_0,16,n+72);a29_0[0]=(double)n*1.25;a29_0[1]=-(double)n*3.5+0.375;x2d a29_1;fill(&a29_1,16,n+60);a29_1[0]=(double)n*1.25;"
            "a29_1[1]=-(double)n*3.5+0.375;x2d a30_0;fill(&a30_0,16,n+48);a30_0[0]=(double)n*1.25;a30_0[1]=-(double)n*3.5+0.375;x2d a30_1;"
            "fill(&a30_1,16,n+36);a30_1[0]=(double)n*1.25;a30_1[1]=-(double)n*3.5+0.375;x2d a31_0;fill(&a31_0,16,n+24);a31_0[0]=(double)n*1.25;"
            "a31_0[1]=-(double)n*3.5+0.375;x2d a31_1;fill(&a31_1,16,n+12);a31_1[0]=(double)n*1.25;a31_1[1]=-(double)n*3.5+0.375;"
            "unsigned long long m=mask(n);{x4f r=__builtin_ia32_cmpeqps(a0_0,a0_1);FOLD(r);}{x4f r=__builtin_ia32_cmpltps(a1_0,a1_1);FOLD(r);}"
            "{x4f r=__builtin_ia32_cmpleps(a2_0,a2_1);FOLD(r);}{x4f r=__builtin_ia32_cmpunordps(a3_0,a3_1);FOLD(r);}"
            "{x4f r=__builtin_ia32_cmpneqps(a4_0,a4_1);FOLD(r);}{x4f r=__builtin_ia32_cmpnltps(a5_0,a5_1);FOLD(r);}"
            "{x4f r=__builtin_ia32_cmpnleps(a6_0,a6_1);FOLD(r);}{x4f r=__builtin_ia32_cmpordps(a7_0,a7_1);FOLD(r);}"
            "{x2d r=__builtin_ia32_cmpeqpd(a8_0,a8_1);FOLD(r);}{x2d r=__builtin_ia32_cmpltpd(a9_0,a9_1);FOLD(r);}"
            "{x2d r=__builtin_ia32_cmplepd(a10_0,a10_1);FOLD(r);}{x2d r=__builtin_ia32_cmpunordpd(a11_0,a11_1);FOLD(r);}"
            "{x2d r=__builtin_ia32_cmpneqpd(a12_0,a12_1);FOLD(r);}{x2d r=__builtin_ia32_cmpnltpd(a13_0,a13_1);FOLD(r);}"
            "{x2d r=__builtin_ia32_cmpnlepd(a14_0,a14_1);FOLD(r);}{x2d r=__builtin_ia32_cmpordpd(a15_0,a15_1);FOLD(r);}"
            "{x4f r=__builtin_ia32_cmpeqss(a16_0,a16_1);FOLD(r);}{x4f r=__builtin_ia32_cmpltss(a17_0,a17_1);FOLD(r);}"
            "{x4f r=__builtin_ia32_cmpless(a18_0,a18_1);FOLD(r);}{x4f r=__builtin_ia32_cmpunordss(a19_0,a19_1);FOLD(r);}"
            "{x4f r=__builtin_ia32_cmpneqss(a20_0,a20_1);FOLD(r);}{x4f r=__builtin_ia32_cmpnltss(a21_0,a21_1);FOLD(r);}"
            "{x4f r=__builtin_ia32_cmpnless(a22_0,a22_1);FOLD(r);}{x4f r=__builtin_ia32_cmpordss(a23_0,a23_1);FOLD(r);}"
            "{x2d r=__builtin_ia32_cmpeqsd(a24_0,a24_1);FOLD(r);}{x2d r=__builtin_ia32_cmpltsd(a25_0,a25_1);FOLD(r);}"
            "{x2d r=__builtin_ia32_cmplesd(a26_0,a26_1);FOLD(r);}{x2d r=__builtin_ia32_cmpunordsd(a27_0,a27_1);FOLD(r);}"
            "{x2d r=__builtin_ia32_cmpneqsd(a28_0,a28_1);FOLD(r);}{x2d r=__builtin_ia32_cmpnltsd(a29_0,a29_1);FOLD(r);}"
            "{x2d r=__builtin_ia32_cmpnlesd(a30_0,a30_1);FOLD(r);}{x2d r=__builtin_ia32_cmpordsd(a31_0,a31_1);FOLD(r);}return (int)h;}",
        },
        {
            "x86-gathers-scatters",
            "int tested(int n){unsigned h=2166136261u;int table[64];long long wide[64];for(int i=0;i<64;i++){table[i]=i*7+n;wide[i]=(long long)i*i-n;}"
            "v16si idx;for(int i=0;i<16;i++)idx[i]=(i*5+n)&63;v8si idx8;for(int i=0;i<8;i++)idx8[i]=(i*3+n)&31;"
            "v8di idx64;for(int i=0;i<8;i++)idx64[i]=(i*11+n)&63;v16si src;fill(&src,64,n);v8di src2;fill(&src2,64,n+1);unsigned long long m=mask(n);"
            "v16si g1=__builtin_ia32_gathersiv16si(src,table,idx,(unsigned short)m,4);v8di g2=__builtin_ia32_gathersiv8di(src2,wide,idx8,(unsigned char)m,8);"
            "v8di g3=__builtin_ia32_gatherdiv8di(src2,wide,idx64,(unsigned char)(m>>3),8);v16si g4=__builtin_ia32_gathersiv16si(src,(char*)table+4,idx,0xffff,2);"
            "v16si g5=__builtin_ia32_gathersiv16si(src,table,idx,(unsigned short)m,4294967300ULL);"
            "FOLD(g1);FOLD(g2);FOLD(g3);FOLD(g4);FOLD(g5);int dst[128];long long dst2[64];fill(dst,sizeof(dst),n+2);fill(dst2,sizeof(dst2),n+3);"
            "__builtin_ia32_scatterdiv16si(dst,(unsigned char)m,idx64,idx8,4);__builtin_ia32_scatterdiv8di(dst2,(unsigned char)(m>>1),idx64,src2,8);"
            "__builtin_ia32_scattersiv16si(dst,(unsigned short)(m>>4),idx,src,4);FOLD(dst);FOLD(dst2);"
            "return (int)h;}",
        },
        {
            "x86-permutes-shifts-misc",
            "int tested(int n){unsigned h=2166136261u;v32qi a,b,c;v16qi d,e,f;v64qi x,y;v16si p,q;v8di r,s;v32hi w,z;v8si t,u;v8sf ff;v4sf fs;v2df fd;v4df fd4;"
            "v4di vq;fill(&a,32,n);fill(&b,32,n+1);fill(&c,32,n+2);fill(&d,16,n+3);fill(&e,16,n+4);fill(&f,16,n+5);fill(&x,64,n+6);fill(&y,64,n+7);"
            "fill(&p,64,n+8);fill(&q,64,n+9);fill(&r,64,n+10);fill(&s,64,n+11);fill(&w,64,n+12);fill(&z,64,n+13);fill(&t,32,n+14);fill(&u,32,n+15);"
            "fill(&vq,32,n+16);for(int i=0;i<8;i++)ff[i]=(float)(i*3-n);for(int i=0;i<4;i++)fs[i]=(float)(n-i);fd[0]=-1.5*n;fd[1]=2.0;"
            "for(int i=0;i<4;i++)fd4[i]=(double)(i-2)*n;unsigned long long m=mask(n);"
            "v32qi bl=__builtin_ia32_pblendvb256(a,b,c);v16qi bl2=__builtin_ia32_pblendvb128(d,e,f);FOLD(bl);FOLD(bl2);"
            "unsigned long long sb=__builtin_ia32_vpshufbitqmb512_mask(x,y,m);unsigned short sb2=__builtin_ia32_vpshufbitqmb128_mask(d,e,(unsigned short)m);"
            "FOLD(sb);FOLD(sb2);v16si cf=__builtin_ia32_vpconflictsi_512(p&7);v8di cq=__builtin_ia32_vpconflictdi_512(r&3);FOLD(cf);FOLD(cq);"
            "v8di pv=__builtin_ia32_permvardi512(r,s);v16si pv2=__builtin_ia32_permvarsi512(p,q);v8si pv3=__builtin_ia32_permvarsi256(t,u);"
            "v32hi pv4=__builtin_ia32_permvarhi512(w,z);FOLD(pv);FOLD(pv2);FOLD(pv3);FOLD(pv4);"
            "v16si sh[]={__builtin_ia32_psllv16si(p,q&63),__builtin_ia32_psrlv16si(p,q&63),__builtin_ia32_psrav16si(p,q&63)};FOLD(sh);"
            "v8di shq[]={__builtin_ia32_psllv8di(r,s&127),__builtin_ia32_psrlv8di(r,s&127),__builtin_ia32_psrav8di(r,s&127)};FOLD(shq);"
            "v8di mu=__builtin_ia32_pmuludq512(p,q);v8di sa=__builtin_ia32_psadbw512((v64qu)x,(v64qu)y);v64qi ms=__builtin_ia32_vpmultishiftqb512(x,y);"
            "FOLD(mu);FOLD(sa);FOLD(ms);v8di ae=__builtin_ia32_aesenc512(r,s);v8di al=__builtin_ia32_aesenclast512(r,s);v4di ae2=__builtin_ia32_aesenc256(vq,vq);"
            "FOLD(ae);FOLD(al);FOLD(ae2);v8di sr=__builtin_ia32_psraqi512(r,n&63);v2di sr2=__builtin_ia32_psraqi128((v2di){n,-n},70);FOLD(sr);FOLD(sr2);"
            "int mm[]={__builtin_ia32_movmskps(fs),__builtin_ia32_movmskps256(ff),__builtin_ia32_movmskpd(fd),__builtin_ia32_movmskpd256(fd4),"
            "__builtin_ia32_pmovmskb256(a)};FOLD(mm);long long ex[]={__builtin_ia32_vec_ext_v4di(vq,0),__builtin_ia32_vec_ext_v4di(vq,3)};FOLD(ex);"
            "return (int)h;}",
        },
        {
            "x86-compares-selects-moves",
            "int tested(int n){unsigned h=2166136261u;v64qi a,b;v16si c,d;v8di e,f;v32hi g,i;v16qi j,k;fill(&a,64,n);fill(&b,64,n+1);fill(&c,64,n+2);"
            "fill(&d,64,n+3);fill(&e,64,n+4);fill(&f,64,n+5);fill(&g,64,n+6);fill(&i,64,n+7);fill(&j,16,n+8);fill(&k,16,n+9);unsigned long long m=mask(n);"
            "unsigned long long k1=__builtin_ia32_cmpb512_mask(a,b,1,m),k2=__builtin_ia32_ucmpb512_mask(a,b,6,~m);"
            "unsigned short k3=__builtin_ia32_cmpd512_mask(c,d,2,(unsigned short)m),k4=__builtin_ia32_ucmpd512_mask(c,d,5,0xffff);"
            "unsigned char k5=__builtin_ia32_cmpq512_mask(e,f,4,(unsigned char)m);unsigned k6=__builtin_ia32_cmpw512_mask(g,i,0,(unsigned)m);"
            "unsigned short k7=__builtin_ia32_cmpb128_mask(j,k,1,0xffff);FOLD(k1);FOLD(k2);FOLD(k3);FOLD(k4);FOLD(k5);FOLD(k6);FOLD(k7);"
            "v64qi s1=__builtin_ia32_selectb_512(m,a,b);v16si s2=__builtin_ia32_selectd_512((unsigned short)m,c,d);"
            "v8di s3=__builtin_ia32_selectq_512((unsigned char)m,e,f);v32hi s4=__builtin_ia32_selectw_512((unsigned)m,g,i);"
            "v16qi s5=__builtin_ia32_selectb_128((unsigned short)m,j,k);FOLD(s1);FOLD(s2);FOLD(s3);FOLD(s4);FOLD(s5);"
            "char buf[200];fill(buf,200,n);v64qi l1=__builtin_ia32_loaddquqi512_mask((const v64qi*)(buf+(n&7)),a,m);"
            "v16si l2=__builtin_ia32_loaddqusi512_mask((const int*)(buf+3),c,(unsigned short)m);FOLD(l1);FOLD(l2);"
            "__builtin_ia32_storedquqi512_mask((v64qi*)(buf+5),b,m>>3);__builtin_ia32_storedqusi512_mask((int*)(buf+64),d,(unsigned short)(m>>9));FOLD(buf);"
            "return (int)h;}",
        },
        {
            "x86-elements-bits",
            "int tested(int n){unsigned h=2166136261u;v64qi a,b;v16qi x,y;v32hi w,w2;v16si d;v8di q;v2di l,l2;v4si v,v2;v32qi c,c2;fill(&a,64,n);"
            "fill(&b,64,n+1);fill(&x,16,n+2);fill(&y,16,n+3);fill(&w,64,n+4);fill(&w2,64,n+5);fill(&d,64,n+6);fill(&q,64,n+7);fill(&l,16,n+8);"
            "fill(&l2,16,n+9);fill(&v,16,n+10);fill(&v2,16,n+11);fill(&c,32,n+12);fill(&c2,32,n+13);"
            "v64qi sh=__builtin_ia32_pshufb512(a,b);v16qi sh2=__builtin_ia32_pshufb128(x,y);v32qi sh3=__builtin_ia32_pshufb256(c,c2);FOLD(sh);FOLD(sh2);FOLD(sh3);"
            "v16si ma=__builtin_ia32_pmaddwd512(w,w2);v32hi mb=__builtin_ia32_pmaddubsw512(a,b);v8hi pk=__builtin_ia32_packusdw128(v,v2);FOLD(ma);FOLD(mb);FOLD(pk);"
            "v64qi pq=__builtin_ia32_permvarqi512(a,b);v16qi pq2=__builtin_ia32_permvarqi128(x,y);FOLD(pq);FOLD(pq2);"
            "v2di ae=__builtin_ia32_aesenc128(l,l2);v2di ael=__builtin_ia32_aesenclast128(l,l2);FOLD(ae);FOLD(ael);"
            "v32hi s1=__builtin_ia32_psllwi512(w,n&15);v16si s2=__builtin_ia32_pslldi512(d,n&31);v8di s3=__builtin_ia32_psrlqi512(q,(n&63)+1);"
            "v16si s4=__builtin_ia32_psradi512(d,40);FOLD(s1);FOLD(s2);FOLD(s3);FOLD(s4);"
            "int e[]={__builtin_ia32_vec_ext_v16qi(x,5),__builtin_ia32_vec_ext_v4si(v,3),__builtin_ia32_pmovmskb128(x)};FOLD(e);"
            "v4si vs=__builtin_ia32_vec_set_v4si(v,n,2);FOLD(vs);unsigned u=(unsigned)n*2654435761u;unsigned long long uu=mask(n);"
            "unsigned bits[]={__builtin_ia32_pdep_si(u,0x0f0f0f0fu),__builtin_ia32_pext_si(u,0x00ff00f0u),__builtin_ia32_bzhi_si(u,(unsigned)n&31)};FOLD(bits);"
            "unsigned long long wide[]={__builtin_ia32_pdep_di(uu,0x00ff00ff00ff00ffull),__builtin_ia32_pext_di(uu,0xf0f0f0f0f0f0f0f0ull),"
            "__builtin_ia32_bzhi_di(uu,(unsigned long long)(n&63)+1)};FOLD(wide);"
            "v8di z=(v8di)__builtin_ia32_undef512();for(int i=0;i<8;i++)z[i]=i;FOLD(z);"
            "return (int)h;}",
        },
        {
            // The compares and tests into an int, from the flags: the SSE compares of each kind on equal, ordered and
            // unordered operands, the vectors' tests on complements, equals and others, and the masks' on complements, zeros,
            // equals and others.
            "x86-compares-and-tests",
            "int tested(int n){unsigned h=2166136261u;union{unsigned u;float f;}qf={0x7fc00000u};union{unsigned long long u;double d;}"
            "qd={0x7ff8000000000000ull};v4sf a,b;v2df c,d;fill(&a,16,n);fill(&b,16,n+1);fill(&c,16,n+2);fill(&d,16,n+3);a[0]=(float)(n%3);"
            "b[0]=(float)(n%5)*0.5f;c[0]=(double)(n%4);d[0]=(double)(n%3);if(n%7==0){b[0]=qf.f;d[0]=qd.d;}if(n%11==1){a[0]=qf.f;c[0]=qd.d;}"
            "int r[24]={__builtin_ia32_comieq(a,b),__builtin_ia32_comilt(a,b),__builtin_ia32_comile(a,b),__builtin_ia32_comigt(a,b),"
            "__builtin_ia32_comige(a,b),__builtin_ia32_comineq(a,b),__builtin_ia32_comisdeq(c,d),__builtin_ia32_comisdlt(c,d),"
            "__builtin_ia32_comisdle(c,d),__builtin_ia32_comisdgt(c,d),__builtin_ia32_comisdge(c,d),__builtin_ia32_comisdneq(c,d),"
            "__builtin_ia32_ucomieq(a,b),__builtin_ia32_ucomilt(a,b),__builtin_ia32_ucomile(a,b),__builtin_ia32_ucomigt(a,b),__builtin_ia32_ucomige(a,b),"
            "__builtin_ia32_ucomineq(a,b),__builtin_ia32_ucomisdeq(c,d),__builtin_ia32_ucomisdlt(c,d),__builtin_ia32_ucomisdle(c,d),"
            "__builtin_ia32_ucomisdgt(c,d),__builtin_ia32_ucomisdge(c,d),__builtin_ia32_ucomisdneq(c,d)};FOLD(r);v2di x,y;v4di X,Y;fill(&x,16,n+4);"
            "fill(&X,32,n+5);if(n%3==0){y=~x;Y=~X;}else if(n%3==1){y=x;Y=X;}else{fill(&y,16,n+6);fill(&Y,32,n+7);}"
            "int t[6]={__builtin_ia32_ptestz128(x,y),__builtin_ia32_ptestc128(x,y),__builtin_ia32_ptestnzc128(x,y),__builtin_ia32_ptestz256(X,Y),"
            "__builtin_ia32_ptestc256(X,Y),__builtin_ia32_ptestnzc256(X,Y)};FOLD(t);v4sf p=(v4sf)x,q=(v4sf)y;v8sf P=(v8sf)X,Q=(v8sf)Y;v2df u=(v2df)x,"
            "w=(v2df)y;v4df U=(v4df)X,W=(v4df)Y;int v[12]={__builtin_ia32_vtestzps(p,q),__builtin_ia32_vtestcps(p,q),__builtin_ia32_vtestnzcps(p,q),"
            "__builtin_ia32_vtestzps256(P,Q),__builtin_ia32_vtestcps256(P,Q),__builtin_ia32_vtestnzcps256(P,Q),__builtin_ia32_vtestzpd(u,w),"
            "__builtin_ia32_vtestcpd(u,w),__builtin_ia32_vtestnzcpd(u,w),__builtin_ia32_vtestzpd256(U,W),__builtin_ia32_vtestcpd256(U,W),"
            "__builtin_ia32_vtestnzcpd256(U,W)};FOLD(v);unsigned long long m=mask(n);unsigned char a8=(unsigned char)m,b8=(unsigned char)(m>>8);"
            "unsigned short a16=(unsigned short)(m>>16),b16=(unsigned short)(m>>32);unsigned a32=(unsigned)(m>>5),b32=(unsigned)(m>>27);"
            "unsigned long long a64=m,b64=m*0x9e3779b97f4a7c15ull;if(n%4==0){b8=(unsigned char)~a8;b16=(unsigned short)~a16;b32=~a32;b64=~a64;}"
            "if(n%4==1){a8=0;b8=0;a16=0;b16=0;a32=0;b32=0;a64=0;b64=0;}if(n%5==2){b8=a8;b16=a16;b32=a32;b64=a64;}"
            "int k[15]={__builtin_ia32_kortestzqi(a8,b8),__builtin_ia32_kortestzhi(a16,b16),__builtin_ia32_kortestzsi(a32,b32),"
            "__builtin_ia32_kortestzdi(a64,b64),__builtin_ia32_kortestcqi(a8,b8),__builtin_ia32_kortestchi(a16,b16),__builtin_ia32_kortestcsi(a32,b32),"
            "__builtin_ia32_kortestcdi(a64,b64),__builtin_ia32_ktestzqi(a8,b8),__builtin_ia32_ktestzhi(a16,b16),__builtin_ia32_ktestzsi(a32,b32),"
            "__builtin_ia32_ktestcqi(a8,b8),__builtin_ia32_ktestchi(a16,b16),__builtin_ia32_ktestcsi(a32,b32),__builtin_ia32_ktestcdi(a64,b64)};FOLD(k);"
            "return (int)h;}",
        },
        {
            // The masks' operations of each width: KNOT's of one, and KADD's, KAND's, KANDN's, KOR's, KXNOR's and KXOR's of
            // two.
            "x86-mask-operations",
            "int tested(int n){unsigned h=2166136261u;unsigned long long m=mask(n),w=m*0x9e3779b97f4a7c15ull+(unsigned)n;"
            "unsigned char a8=(unsigned char)m,b8=(unsigned char)w;unsigned short a16=(unsigned short)(m>>9),b16=(unsigned short)(w>>13);"
            "unsigned a32=(unsigned)(m>>3),b32=(unsigned)(w>>7);unsigned long long a64=m,b64=w;unsigned char r8[7]={__builtin_ia32_knotqi(a8),"
            "__builtin_ia32_kaddqi(a8,b8),__builtin_ia32_kandqi(a8,b8),__builtin_ia32_kandnqi(a8,b8),__builtin_ia32_korqi(a8,b8),"
            "__builtin_ia32_kxnorqi(a8,b8),__builtin_ia32_kxorqi(a8,b8)};FOLD(r8);unsigned short r16[5]={__builtin_ia32_knothi(a16),"
            "__builtin_ia32_kandnhi(a16,b16),__builtin_ia32_korhi(a16,b16),__builtin_ia32_kxnorhi(a16,b16),__builtin_ia32_kxorhi(a16,b16)};FOLD(r16);"
            "unsigned r32[7]={__builtin_ia32_knotsi(a32),__builtin_ia32_kaddsi(a32,b32),__builtin_ia32_kandsi(a32,b32),__builtin_ia32_kandnsi(a32,b32),"
            "__builtin_ia32_korsi(a32,b32),__builtin_ia32_kxnorsi(a32,b32),__builtin_ia32_kxorsi(a32,b32)};FOLD(r32);"
            "unsigned long long r64[5]={__builtin_ia32_knotdi(a64),__builtin_ia32_kadddi(a64,b64),__builtin_ia32_kanddi(a64,b64),"
            "__builtin_ia32_kandndi(a64,b64),__builtin_ia32_kxnordi(a64,b64)};FOLD(r64);return (int)h;}",
        },
        {
            // The integers' builtins: the CRCs chained, the leading zeros' leaves (of 0 too), the bit fields, and the sums and
            // differences with carries in and out.
            "x86-integers",
            "int tested(int n){unsigned h=2166136261u;unsigned long long m=mask(n);unsigned c=0x12345678u^(unsigned)n;"
            "c=__builtin_ia32_crc32qi(c,(unsigned char)(n*7));c=__builtin_ia32_crc32hi(c,(unsigned short)(n*911));"
            "c=__builtin_ia32_crc32si(c,(unsigned)m);FOLD(c);unsigned long long cd=__builtin_ia32_crc32di(0x9e3779b97f4a7c15ull^(unsigned long long)n,m);"
            "FOLD(cd);unsigned z32[2]={__builtin_ia32_lzcnt_u32((unsigned)m>>(n&31)),__builtin_ia32_lzcnt_u32(0u)};FOLD(z32);"
            "unsigned long long z64[2]={__builtin_ia32_lzcnt_u64(m>>(n&63)),__builtin_ia32_lzcnt_u64(0ull)};FOLD(z64);"
            "unsigned b32[2]={__builtin_ia32_bextr_u32((unsigned)m,(unsigned)((n&31)|((n*3&31)<<8))),__builtin_ia32_bextr_u32(~(unsigned)m,0x1004u)};"
            "FOLD(b32);unsigned long long b64[2]={__builtin_ia32_bextr_u64(m,(unsigned long long)((n&63)|((n*5&63)<<8))),"
            "__builtin_ia32_bextr_u64(~m,0x2010ull)};FOLD(b64);unsigned a=(n%2)?0xffffffffu:(unsigned)m,b=(unsigned)(m>>32)|((n%3)?0u:1u);"
            "unsigned long long A=(n%2)?~0ull:m,B=m*3+(unsigned)n;unsigned s32[2];unsigned long long s64[2];"
            "unsigned char k[4]={__builtin_ia32_addcarryx_u32((unsigned char)(n&3),a,b,&s32[0]),"
            "__builtin_ia32_subborrow_u32((unsigned char)(n&5),b,a,&s32[1]),__builtin_ia32_addcarryx_u64((unsigned char)(n&6),A,B,&s64[0]),"
            "__builtin_ia32_subborrow_u64((unsigned char)(n%4==0),B,A,&s64[1])};FOLD(k);FOLD(s32);FOLD(s64);return (int)h;}",
            BUSTER_X86_FEATURE(CRC32) | BUSTER_X86_FEATURE(BMI),
        },
        {
            // The random numbers' steps, which store a value of their width at their argument: the values around it, which
            // they must leave, and their results, each 0 or 1.
            "x86-random",
            "int tested(int n){unsigned h=2166136261u;unsigned short s[3]={0x1111,0x2222,0x3333};unsigned i[3]={0x44444444u,0x55555555u,0x66666666u};"
            "unsigned long long l[3]={0x7777777777777777ull,0x8888888888888888ull,0x9999999999999999ull};unsigned ok=0;"
            "ok+=__builtin_ia32_rdrand16_step(&s[1])<2u;ok+=__builtin_ia32_rdrand32_step(&i[1])<2u;ok+=__builtin_ia32_rdrand64_step(&l[1])<2u;FOLD(s[0]);"
            "FOLD(s[2]);FOLD(i[0]);FOLD(i[2]);FOLD(l[0]);FOLD(l[2]);ok+=__builtin_ia32_rdseed16_step(&s[1])<2u;"
            "ok+=__builtin_ia32_rdseed32_step(&i[1])<2u;ok+=__builtin_ia32_rdseed64_step(&l[1])<2u;FOLD(s[0]);FOLD(s[2]);FOLD(i[0]);FOLD(i[2]);"
            "FOLD(l[0]);FOLD(l[2]);FOLD(ok);return (int)h+n;}",
            BUSTER_X86_FEATURE(RDRND) | BUSTER_X86_FEATURE(RDSEED),
        },
        {
            // The vectors' loads and stores of their own: under each width's mask of signs, the unaligned loads at every
            // offset, and the byte stores under a vector's signs.
            "x86-vector-memory",
            "int tested(int n){unsigned h=2166136261u;int bi[8];long long bq[4];float bf[8];double bd[4];char bc[48];for(int i=0;i<8;i++){bi[i]=i*31+n;"
            "bf[i]=(float)(i*3-n);}for(int i=0;i<4;i++){bq[i]=(long long)i*1000003-n;bd[i]=(double)i*0.25+n;}for(int i=0;i<48;i++)bc[i]=(char)(i*7+n);"
            "v4si m4;v8si m8;v2di q2;v4di q4;fill(&m4,16,n);fill(&m8,32,n+1);fill(&q2,16,n+2);fill(&q4,32,n+3);"
            "v4si l0=__builtin_ia32_maskloadd((const v4si*)bi,m4);FOLD(l0);v8si l1=__builtin_ia32_maskloadd256((const v8si*)bi,m8);FOLD(l1);"
            "v2di l2=__builtin_ia32_maskloadq((const v2di*)bq,q2);FOLD(l2);v4di l3=__builtin_ia32_maskloadq256((const v4di*)bq,q4);FOLD(l3);"
            "v4sf l4=__builtin_ia32_maskloadps((const v4sf*)bf,m4);FOLD(l4);v8sf l5=__builtin_ia32_maskloadps256((const v8sf*)bf,m8);FOLD(l5);"
            "v2df l6=__builtin_ia32_maskloadpd((const v2df*)bd,q2);FOLD(l6);v4df l7=__builtin_ia32_maskloadpd256((const v4df*)bd,q4);FOLD(l7);v4si s0;"
            "v8si s1;v2di s2;v4di s3;fill(&s0,16,n+4);fill(&s1,32,n+5);fill(&s2,16,n+6);fill(&s3,32,n+7);__builtin_ia32_maskstored((v4si*)bi,m4,s0);"
            "__builtin_ia32_maskstored256((v8si*)bi,m8,s1);FOLD(bi);__builtin_ia32_maskstoreq((v2di*)bq,q2,s2);"
            "__builtin_ia32_maskstoreq256((v4di*)bq,q4,s3);FOLD(bq);__builtin_ia32_maskstoreps((v4sf*)bf,m4,(v4sf)s0);"
            "__builtin_ia32_maskstoreps256((v8sf*)bf,m8,(v8sf)s1);FOLD(bf);__builtin_ia32_maskstorepd((v2df*)bd,q2,(v2df)s2);"
            "__builtin_ia32_maskstorepd256((v4df*)bd,q4,(v4df)s3);FOLD(bd);v16qi u0=__builtin_ia32_lddqu(bc+(n&7));FOLD(u0);"
            "v32qi u1=__builtin_ia32_lddqu256(bc+(n&7));FOLD(u1);v16qi d,k;fill(&d,16,n+8);fill(&k,16,n+9);__builtin_ia32_maskmovdqu(d,k,bc+(n&15));"
            "FOLD(bc);return (int)h;}",
        },
        {
            // SSE4A's bit fields (each field's length and index below 32, so that their sum is within the quadword) and non-
            // temporal scalar stores, and CLZERO's line: AMD's.
            "x86-sse4a-clzero",
            "int tested(int n){unsigned h=2166136261u;v2di a,b,c;fill(&a,16,n);fill(&b,16,n+1);fill(&c,16,n+2);"
            "b[0]=(long long)(((n*3)&31)|(((n*5)&31)<<8));c[1]=(long long)(((n*7)&31)|(((n*11)&31)<<8));v2di e=__builtin_ia32_extrq(a,(v16qi)b);FOLD(e);"
            "v2di i=__builtin_ia32_insertq(a,c);FOLD(i);double d[3]={1.5,2.5,3.5};float f[3]={4.5f,5.5f,6.5f};v2df x;v4sf y;fill(&x,16,n+3);"
            "fill(&y,16,n+4);__builtin_ia32_movntsd(&d[1],x);__builtin_ia32_movntss(&f[1],y);FOLD(d);FOLD(f);struct{_Alignas(64) unsigned char b[128];}z;"
            "for(int k=0;k<128;k++)z.b[k]=(unsigned char)(k+n);__builtin_ia32_clzero(z.b+64+(n&31));FOLD(z);return (int)h;}",
            BUSTER_X86_FEATURE(SSE4A) | BUSTER_X86_FEATURE(CLZERO),
        },
        {
            // The FMAs under a mask: merged into a, a zeroed and merged into c, of the scalars and of 64 bytes' alternating
            // sums, at each rounding control.
            "x86-masked-fmas",
            "int tested(int n){unsigned h=2166136261u;unsigned long long m=mask(n);v2df v2df_a,v2df_b,v2df_c;fill(&v2df_a,16,n);fill(&v2df_b,16,n+1);"
            "fill(&v2df_c,16,n+2);if(n%2==0)for(int i=0;i<2;i++){v2df_a[i]=(n-i)*0.375;v2df_b[i]=(i+1)*-1.25;v2df_c[i]=(n*i)*0.0625;}v4sf v4sf_a,v4sf_b,"
            "v4sf_c;fill(&v4sf_a,16,n);fill(&v4sf_b,16,n+1);fill(&v4sf_c,16,n+2);if(n%2==0)for(int i=0;i<4;i++){v4sf_a[i]=(n-i)*0.375f;"
            "v4sf_b[i]=(i+1)*-1.25f;v4sf_c[i]=(n*i)*0.0625f;}v8df v8df_a,v8df_b,v8df_c;fill(&v8df_a,64,n);fill(&v8df_b,64,n+1);fill(&v8df_c,64,n+2);"
            "if(n%2==0)for(int i=0;i<8;i++){v8df_a[i]=(n-i)*0.375;v8df_b[i]=(i+1)*-1.25;v8df_c[i]=(n*i)*0.0625;}v16sf v16sf_a,v16sf_b,v16sf_c;"
            "fill(&v16sf_a,64,n);fill(&v16sf_b,64,n+1);fill(&v16sf_c,64,n+2);if(n%2==0)for(int i=0;i<16;i++){v16sf_a[i]=(n-i)*0.375f;"
            "v16sf_b[i]=(i+1)*-1.25f;v16sf_c[i]=(n*i)*0.0625f;}{v2df r=__builtin_ia32_vfmaddsd3_mask(v2df_a,v2df_b,v2df_c,(unsigned char)(m>>0),4);"
            "FOLD(r);}{v2df r=__builtin_ia32_vfmaddsd3_mask(v2df_a,v2df_b,v2df_c,(unsigned char)(m>>7),8);FOLD(r);}"
            "{v2df r=__builtin_ia32_vfmaddsd3_mask(v2df_a,v2df_b,v2df_c,(unsigned char)(m>>14),9);FOLD(r);}"
            "{v2df r=__builtin_ia32_vfmaddsd3_mask(v2df_a,v2df_b,v2df_c,(unsigned char)(m>>21),10);FOLD(r);}"
            "{v2df r=__builtin_ia32_vfmaddsd3_mask(v2df_a,v2df_b,v2df_c,(unsigned char)(m>>28),11);FOLD(r);}"
            "{v2df r=__builtin_ia32_vfmaddsd3_maskz(v2df_a,v2df_b,v2df_c,(unsigned char)(m>>35),4);FOLD(r);}"
            "{v2df r=__builtin_ia32_vfmaddsd3_maskz(v2df_a,v2df_b,v2df_c,(unsigned char)(m>>42),8);FOLD(r);}"
            "{v2df r=__builtin_ia32_vfmaddsd3_maskz(v2df_a,v2df_b,v2df_c,(unsigned char)(m>>1),9);FOLD(r);}"
            "{v2df r=__builtin_ia32_vfmaddsd3_maskz(v2df_a,v2df_b,v2df_c,(unsigned char)(m>>8),10);FOLD(r);}"
            "{v2df r=__builtin_ia32_vfmaddsd3_maskz(v2df_a,v2df_b,v2df_c,(unsigned char)(m>>15),11);FOLD(r);}"
            "{v2df r=__builtin_ia32_vfmaddsd3_mask3(v2df_a,v2df_b,v2df_c,(unsigned char)(m>>22),4);FOLD(r);}"
            "{v2df r=__builtin_ia32_vfmaddsd3_mask3(v2df_a,v2df_b,v2df_c,(unsigned char)(m>>29),8);FOLD(r);}"
            "{v2df r=__builtin_ia32_vfmaddsd3_mask3(v2df_a,v2df_b,v2df_c,(unsigned char)(m>>36),9);FOLD(r);}"
            "{v2df r=__builtin_ia32_vfmaddsd3_mask3(v2df_a,v2df_b,v2df_c,(unsigned char)(m>>43),10);FOLD(r);}"
            "{v2df r=__builtin_ia32_vfmaddsd3_mask3(v2df_a,v2df_b,v2df_c,(unsigned char)(m>>2),11);FOLD(r);}"
            "{v4sf r=__builtin_ia32_vfmaddss3_mask(v4sf_a,v4sf_b,v4sf_c,(unsigned char)(m>>9),4);FOLD(r);}"
            "{v4sf r=__builtin_ia32_vfmaddss3_mask(v4sf_a,v4sf_b,v4sf_c,(unsigned char)(m>>16),8);FOLD(r);}"
            "{v4sf r=__builtin_ia32_vfmaddss3_mask(v4sf_a,v4sf_b,v4sf_c,(unsigned char)(m>>23),9);FOLD(r);}"
            "{v4sf r=__builtin_ia32_vfmaddss3_mask(v4sf_a,v4sf_b,v4sf_c,(unsigned char)(m>>30),10);FOLD(r);}"
            "{v4sf r=__builtin_ia32_vfmaddss3_mask(v4sf_a,v4sf_b,v4sf_c,(unsigned char)(m>>37),11);FOLD(r);}"
            "{v4sf r=__builtin_ia32_vfmaddss3_maskz(v4sf_a,v4sf_b,v4sf_c,(unsigned char)(m>>44),4);FOLD(r);}"
            "{v4sf r=__builtin_ia32_vfmaddss3_maskz(v4sf_a,v4sf_b,v4sf_c,(unsigned char)(m>>3),8);FOLD(r);}"
            "{v4sf r=__builtin_ia32_vfmaddss3_maskz(v4sf_a,v4sf_b,v4sf_c,(unsigned char)(m>>10),9);FOLD(r);}"
            "{v4sf r=__builtin_ia32_vfmaddss3_maskz(v4sf_a,v4sf_b,v4sf_c,(unsigned char)(m>>17),10);FOLD(r);}"
            "{v4sf r=__builtin_ia32_vfmaddss3_maskz(v4sf_a,v4sf_b,v4sf_c,(unsigned char)(m>>24),11);FOLD(r);}"
            "{v4sf r=__builtin_ia32_vfmaddss3_mask3(v4sf_a,v4sf_b,v4sf_c,(unsigned char)(m>>31),4);FOLD(r);}"
            "{v4sf r=__builtin_ia32_vfmaddss3_mask3(v4sf_a,v4sf_b,v4sf_c,(unsigned char)(m>>38),8);FOLD(r);}"
            "{v4sf r=__builtin_ia32_vfmaddss3_mask3(v4sf_a,v4sf_b,v4sf_c,(unsigned char)(m>>45),9);FOLD(r);}"
            "{v4sf r=__builtin_ia32_vfmaddss3_mask3(v4sf_a,v4sf_b,v4sf_c,(unsigned char)(m>>4),10);FOLD(r);}"
            "{v4sf r=__builtin_ia32_vfmaddss3_mask3(v4sf_a,v4sf_b,v4sf_c,(unsigned char)(m>>11),11);FOLD(r);}"
            "{v2df r=__builtin_ia32_vfmsubsd3_mask3(v2df_a,v2df_b,v2df_c,(unsigned char)(m>>18),4);FOLD(r);}"
            "{v2df r=__builtin_ia32_vfmsubsd3_mask3(v2df_a,v2df_b,v2df_c,(unsigned char)(m>>25),8);FOLD(r);}"
            "{v2df r=__builtin_ia32_vfmsubsd3_mask3(v2df_a,v2df_b,v2df_c,(unsigned char)(m>>32),9);FOLD(r);}"
            "{v2df r=__builtin_ia32_vfmsubsd3_mask3(v2df_a,v2df_b,v2df_c,(unsigned char)(m>>39),10);FOLD(r);}"
            "{v2df r=__builtin_ia32_vfmsubsd3_mask3(v2df_a,v2df_b,v2df_c,(unsigned char)(m>>46),11);FOLD(r);}"
            "{v4sf r=__builtin_ia32_vfmsubss3_mask3(v4sf_a,v4sf_b,v4sf_c,(unsigned char)(m>>5),4);FOLD(r);}"
            "{v4sf r=__builtin_ia32_vfmsubss3_mask3(v4sf_a,v4sf_b,v4sf_c,(unsigned char)(m>>12),8);FOLD(r);}"
            "{v4sf r=__builtin_ia32_vfmsubss3_mask3(v4sf_a,v4sf_b,v4sf_c,(unsigned char)(m>>19),9);FOLD(r);}"
            "{v4sf r=__builtin_ia32_vfmsubss3_mask3(v4sf_a,v4sf_b,v4sf_c,(unsigned char)(m>>26),10);FOLD(r);}"
            "{v4sf r=__builtin_ia32_vfmsubss3_mask3(v4sf_a,v4sf_b,v4sf_c,(unsigned char)(m>>33),11);FOLD(r);}"
            "{v8df r=__builtin_ia32_vfmaddsubpd512_mask(v8df_a,v8df_b,v8df_c,(unsigned char)(m>>40),4);FOLD(r);}"
            "{v8df r=__builtin_ia32_vfmaddsubpd512_mask(v8df_a,v8df_b,v8df_c,(unsigned char)(m>>47),8);FOLD(r);}"
            "{v8df r=__builtin_ia32_vfmaddsubpd512_mask(v8df_a,v8df_b,v8df_c,(unsigned char)(m>>6),9);FOLD(r);}"
            "{v8df r=__builtin_ia32_vfmaddsubpd512_mask(v8df_a,v8df_b,v8df_c,(unsigned char)(m>>13),10);FOLD(r);}"
            "{v8df r=__builtin_ia32_vfmaddsubpd512_mask(v8df_a,v8df_b,v8df_c,(unsigned char)(m>>20),11);FOLD(r);}"
            "{v8df r=__builtin_ia32_vfmaddsubpd512_maskz(v8df_a,v8df_b,v8df_c,(unsigned char)(m>>27),4);FOLD(r);}"
            "{v8df r=__builtin_ia32_vfmaddsubpd512_maskz(v8df_a,v8df_b,v8df_c,(unsigned char)(m>>34),8);FOLD(r);}"
            "{v8df r=__builtin_ia32_vfmaddsubpd512_maskz(v8df_a,v8df_b,v8df_c,(unsigned char)(m>>41),9);FOLD(r);}"
            "{v8df r=__builtin_ia32_vfmaddsubpd512_maskz(v8df_a,v8df_b,v8df_c,(unsigned char)(m>>0),10);FOLD(r);}"
            "{v8df r=__builtin_ia32_vfmaddsubpd512_maskz(v8df_a,v8df_b,v8df_c,(unsigned char)(m>>7),11);FOLD(r);}"
            "{v8df r=__builtin_ia32_vfmaddsubpd512_mask3(v8df_a,v8df_b,v8df_c,(unsigned char)(m>>14),4);FOLD(r);}"
            "{v8df r=__builtin_ia32_vfmaddsubpd512_mask3(v8df_a,v8df_b,v8df_c,(unsigned char)(m>>21),8);FOLD(r);}"
            "{v8df r=__builtin_ia32_vfmaddsubpd512_mask3(v8df_a,v8df_b,v8df_c,(unsigned char)(m>>28),9);FOLD(r);}"
            "{v8df r=__builtin_ia32_vfmaddsubpd512_mask3(v8df_a,v8df_b,v8df_c,(unsigned char)(m>>35),10);FOLD(r);}"
            "{v8df r=__builtin_ia32_vfmaddsubpd512_mask3(v8df_a,v8df_b,v8df_c,(unsigned char)(m>>42),11);FOLD(r);}"
            "{v16sf r=__builtin_ia32_vfmaddsubps512_mask(v16sf_a,v16sf_b,v16sf_c,(unsigned short)(m>>1),4);FOLD(r);}"
            "{v16sf r=__builtin_ia32_vfmaddsubps512_mask(v16sf_a,v16sf_b,v16sf_c,(unsigned short)(m>>8),8);FOLD(r);}"
            "{v16sf r=__builtin_ia32_vfmaddsubps512_mask(v16sf_a,v16sf_b,v16sf_c,(unsigned short)(m>>15),9);FOLD(r);}"
            "{v16sf r=__builtin_ia32_vfmaddsubps512_mask(v16sf_a,v16sf_b,v16sf_c,(unsigned short)(m>>22),10);FOLD(r);}"
            "{v16sf r=__builtin_ia32_vfmaddsubps512_mask(v16sf_a,v16sf_b,v16sf_c,(unsigned short)(m>>29),11);FOLD(r);}"
            "{v16sf r=__builtin_ia32_vfmaddsubps512_maskz(v16sf_a,v16sf_b,v16sf_c,(unsigned short)(m>>36),4);FOLD(r);}"
            "{v16sf r=__builtin_ia32_vfmaddsubps512_maskz(v16sf_a,v16sf_b,v16sf_c,(unsigned short)(m>>43),8);FOLD(r);}"
            "{v16sf r=__builtin_ia32_vfmaddsubps512_maskz(v16sf_a,v16sf_b,v16sf_c,(unsigned short)(m>>2),9);FOLD(r);}"
            "{v16sf r=__builtin_ia32_vfmaddsubps512_maskz(v16sf_a,v16sf_b,v16sf_c,(unsigned short)(m>>9),10);FOLD(r);}"
            "{v16sf r=__builtin_ia32_vfmaddsubps512_maskz(v16sf_a,v16sf_b,v16sf_c,(unsigned short)(m>>16),11);FOLD(r);}"
            "{v16sf r=__builtin_ia32_vfmaddsubps512_mask3(v16sf_a,v16sf_b,v16sf_c,(unsigned short)(m>>23),4);FOLD(r);}"
            "{v16sf r=__builtin_ia32_vfmaddsubps512_mask3(v16sf_a,v16sf_b,v16sf_c,(unsigned short)(m>>30),8);FOLD(r);}"
            "{v16sf r=__builtin_ia32_vfmaddsubps512_mask3(v16sf_a,v16sf_b,v16sf_c,(unsigned short)(m>>37),9);FOLD(r);}"
            "{v16sf r=__builtin_ia32_vfmaddsubps512_mask3(v16sf_a,v16sf_b,v16sf_c,(unsigned short)(m>>44),10);FOLD(r);}"
            "{v16sf r=__builtin_ia32_vfmaddsubps512_mask3(v16sf_a,v16sf_b,v16sf_c,(unsigned short)(m>>3),11);FOLD(r);}"
            "{v8df r=__builtin_ia32_vfmsubaddpd512_mask3(v8df_a,v8df_b,v8df_c,(unsigned char)(m>>10),4);FOLD(r);}"
            "{v8df r=__builtin_ia32_vfmsubaddpd512_mask3(v8df_a,v8df_b,v8df_c,(unsigned char)(m>>17),8);FOLD(r);}"
            "{v8df r=__builtin_ia32_vfmsubaddpd512_mask3(v8df_a,v8df_b,v8df_c,(unsigned char)(m>>24),9);FOLD(r);}"
            "{v8df r=__builtin_ia32_vfmsubaddpd512_mask3(v8df_a,v8df_b,v8df_c,(unsigned char)(m>>31),10);FOLD(r);}"
            "{v8df r=__builtin_ia32_vfmsubaddpd512_mask3(v8df_a,v8df_b,v8df_c,(unsigned char)(m>>38),11);FOLD(r);}"
            "{v16sf r=__builtin_ia32_vfmsubaddps512_mask3(v16sf_a,v16sf_b,v16sf_c,(unsigned short)(m>>45),4);FOLD(r);}"
            "{v16sf r=__builtin_ia32_vfmsubaddps512_mask3(v16sf_a,v16sf_b,v16sf_c,(unsigned short)(m>>4),8);FOLD(r);}"
            "{v16sf r=__builtin_ia32_vfmsubaddps512_mask3(v16sf_a,v16sf_b,v16sf_c,(unsigned short)(m>>11),9);FOLD(r);}"
            "{v16sf r=__builtin_ia32_vfmsubaddps512_mask3(v16sf_a,v16sf_b,v16sf_c,(unsigned short)(m>>18),10);FOLD(r);}"
            "{v16sf r=__builtin_ia32_vfmsubaddps512_mask3(v16sf_a,v16sf_b,v16sf_c,(unsigned short)(m>>25),11);FOLD(r);}return (int)h;}",
        },
        {
            // The floating reductions of 64 bytes, of random bits (NaNs and infinities among them) and of modest values, whose
            // order of operations clang's code has.
            "x86-reductions",
            "int tested(int n){unsigned h=2166136261u;v8df a;v16sf b;fill(&a,64,n);fill(&b,64,n+1);if(n%2==0){for(int i=0;i<8;i++)a[i]=(double)(i*n)-2.5;"
            "for(int i=0;i<16;i++)b[i]=(float)(i-n)*0.75f;}double r[4]={__builtin_ia32_reduce_fadd_pd512(1.5,a),__builtin_ia32_reduce_fmul_pd512(-0.5,a),"
            "__builtin_ia32_reduce_fmax_pd512(a),__builtin_ia32_reduce_fmin_pd512(a)};FOLD(r);float s[4]={__builtin_ia32_reduce_fadd_ps512(0.25f,b),"
            "__builtin_ia32_reduce_fmul_ps512(2.0f,b),__builtin_ia32_reduce_fmax_ps512(b),__builtin_ia32_reduce_fmin_ps512(b)};FOLD(s);return (int)h;}",
        },
        {
            // The scalar conversions to integers: by MXCSR's rounding, by each rounding control, and truncating with the
            // exceptions' suppression and without, of random bits and of modest values.
            "x86-scalar-conversions",
            "int tested(int n){unsigned h=2166136261u;v2df a;v4sf b;fill(&a,16,n);fill(&b,16,n+1);if(n%3!=0){a[0]=(double)n*1.75-3.5;"
            "b[0]=(float)n*-2.25f+0.5f;}int c32[2]={__builtin_ia32_cvtsd2si(a),__builtin_ia32_cvtss2si(b)};"
            "long long c64[2]={__builtin_ia32_cvtsd2si64(a),__builtin_ia32_cvtss2si64(b)};FOLD(c32);FOLD(c64);"
            "{unsigned r[5]={__builtin_ia32_vcvtsd2usi32(a,4),__builtin_ia32_vcvtsd2usi32(a,8),__builtin_ia32_vcvtsd2usi32(a,9),"
            "__builtin_ia32_vcvtsd2usi32(a,10),__builtin_ia32_vcvtsd2usi32(a,11)};FOLD(r);}{unsigned r[5]={__builtin_ia32_vcvtss2usi32(b,4),"
            "__builtin_ia32_vcvtss2usi32(b,8),__builtin_ia32_vcvtss2usi32(b,9),__builtin_ia32_vcvtss2usi32(b,10),__builtin_ia32_vcvtss2usi32(b,11)};"
            "FOLD(r);}{unsigned long long r[5]={__builtin_ia32_vcvtsd2usi64(a,4),__builtin_ia32_vcvtsd2usi64(a,8),__builtin_ia32_vcvtsd2usi64(a,9),"
            "__builtin_ia32_vcvtsd2usi64(a,10),__builtin_ia32_vcvtsd2usi64(a,11)};FOLD(r);}{unsigned long long r[5]={__builtin_ia32_vcvtss2usi64(b,4),"
            "__builtin_ia32_vcvtss2usi64(b,8),__builtin_ia32_vcvtss2usi64(b,9),__builtin_ia32_vcvtss2usi64(b,10),__builtin_ia32_vcvtss2usi64(b,11)};"
            "FOLD(r);}{int r[2]={__builtin_ia32_vcvttsd2si32(a,4),__builtin_ia32_vcvttsd2si32(a,8)};FOLD(r);}{int r[2]={__builtin_ia32_vcvttss2si32(b,4),"
            "__builtin_ia32_vcvttss2si32(b,8)};FOLD(r);}{long long r[2]={__builtin_ia32_vcvttsd2si64(a,4),__builtin_ia32_vcvttsd2si64(a,8)};FOLD(r);}"
            "{long long r[2]={__builtin_ia32_vcvttss2si64(b,4),__builtin_ia32_vcvttss2si64(b,8)};FOLD(r);}"
            "{unsigned r[2]={__builtin_ia32_vcvttsd2usi32(a,4),__builtin_ia32_vcvttsd2usi32(a,8)};FOLD(r);}"
            "{unsigned r[2]={__builtin_ia32_vcvttss2usi32(b,4),__builtin_ia32_vcvttss2usi32(b,8)};FOLD(r);}"
            "{unsigned long long r[2]={__builtin_ia32_vcvttsd2usi64(a,4),__builtin_ia32_vcvttsd2usi64(a,8)};FOLD(r);}"
            "{unsigned long long r[2]={__builtin_ia32_vcvttss2usi64(b,4),__builtin_ia32_vcvttss2usi64(b,8)};FOLD(r);}return (int)h;}",
        },
        {
            // AVX's lanes of 32 bytes, the rounding to a scale under a mask with the exceptions' suppression and without,
            // bf16's scalar select by each value of the mask's first bit, and SHA's messages and rounds of SHA-1.
            "x86-lanes-scales-sha",
            "int tested(int n){unsigned h=2166136261u;v4df a;v8sf b;v8si c;fill(&a,32,n);fill(&b,32,n+1);fill(&c,32,n+2);"
            "v2df e0=__builtin_ia32_vextractf128_pd256(a,0),e1=__builtin_ia32_vextractf128_pd256(a,1);FOLD(e0);FOLD(e1);"
            "v4sf e2=__builtin_ia32_vextractf128_ps256(b,0),e3=__builtin_ia32_vextractf128_ps256(b,1);FOLD(e2);FOLD(e3);"
            "v4si e4=__builtin_ia32_vextractf128_si256(c,0),e5=__builtin_ia32_vextractf128_si256(c,1);FOLD(e4);FOLD(e5);v8df x,w;v16sf y,z;"
            "fill(&w,64,n+4);fill(&z,64,n+6);for(int i=0;i<8;i++)x[i]=(double)(i*n)*0.37-1.0;for(int i=0;i<16;i++)y[i]=(float)(i+n)*-0.61f;"
            "unsigned long long m=mask(n);v8df r0=__builtin_ia32_rndscalepd_mask(x,0x01,w,(unsigned char)m,4),"
            "r1=__builtin_ia32_rndscalepd_mask(x,0x32,w,(unsigned char)(m>>8),8),r2=__builtin_ia32_rndscalepd_mask(x,0x0b,w,(unsigned char)(m>>16),4);"
            "FOLD(r0);FOLD(r1);FOLD(r2);v16sf r3=__builtin_ia32_rndscaleps_mask(y,0x02,z,(unsigned short)m,4),"
            "r4=__builtin_ia32_rndscaleps_mask(y,0x43,z,(unsigned short)(m>>16),8);FOLD(r3);FOLD(r4);v8bf p,q;fill(&p,16,n+7);fill(&q,16,n+8);"
            "v8bf s0=__builtin_ia32_selectsbf_128((unsigned char)m,p,q),s1=__builtin_ia32_selectsbf_128((unsigned char)(m>>1),p,q);FOLD(s0);FOLD(s1);"
            "v4si k0,k1;fill(&k0,16,n+9);fill(&k1,16,n+10);v4si g[5]={__builtin_ia32_sha1msg1(k0,k1),__builtin_ia32_sha1msg2(k0,k1),"
            "__builtin_ia32_sha1nexte(k0,k1),__builtin_ia32_sha256msg1(k0,k1),__builtin_ia32_sha256msg2(k0,k1)};FOLD(g);return (int)h;}",
            BUSTER_X86_FEATURE(SHA),
        },
        {
            // SHA-256's rounds, a vector leaf's calls: chained as a hash's rounds are, with floating and integer values live
            // across them, one in another's arguments, and two in one initializer.
            "x86-sha256-rounds",
            "int tested(int n){unsigned h=2166136261u;v4si a,b,k,t;fill(&a,16,n);fill(&b,16,n+1);fill(&k,16,n+2);fill(&t,16,n+3);"
            "double d=n*0.75-2.0;float f=(float)n*-1.25f;unsigned long long z=mask(n);v4si r0=__builtin_ia32_sha256rnds2(a,b,k);FOLD(r0);"
            "for(int i=0;i<4;i++){v4si s=__builtin_ia32_sha256rnds2(b,a,k);a=b;b=s;k=k+t;d=d*1.5+f;f=f-(float)d;z^=z<<7;}"
            "FOLD(a);FOLD(b);FOLD(k);FOLD(d);FOLD(f);FOLD(z);v4si u=__builtin_ia32_sha256rnds2(a+t,b^t,__builtin_ia32_sha256rnds2(k,t,a))+t;FOLD(u);"
            "v4si v[2]={__builtin_ia32_sha256rnds2(t,k,b),__builtin_ia32_sha256rnds2(k,t,b)};FOLD(v);return (int)h;}",
            BUSTER_X86_FEATURE(SHA),
        },
        {
            // The leaves of the rotates of each width, of the x87 and SSE state's saves and restores (whose control words, the
            // same in both, it folds), of the time stamp, the flags and the vector registers' clearing; and the cache lines'
            // and non-temporal stores' rows.
            "x86-leaves-and-hints",
            "int tested(int n){unsigned h=2166136261u;unsigned long long m=mask(n);"
            "unsigned char r8[2]={__builtin_rotateleft8((unsigned char)m,(unsigned char)n),"
            "__builtin_rotateright8((unsigned char)(m>>8),(unsigned char)(n*3))};FOLD(r8);"
            "unsigned short r16[2]={__builtin_rotateleft16((unsigned short)m,(unsigned short)(n*5)),"
            "__builtin_rotateright16((unsigned short)(m>>16),(unsigned short)n)};FOLD(r16);"
            "unsigned r32[2]={__builtin_rotateleft32((unsigned)m,(unsigned)n*7u),__builtin_rotateright32((unsigned)(m>>32),(unsigned)n)};FOLD(r32);"
            "unsigned long long r64[2]={__builtin_rotateleft64(m,(unsigned long long)n*9u),__builtin_rotateright64(~m,(unsigned long long)n)};FOLD(r64);"
            "struct{_Alignas(64) unsigned char b[1024];}s;for(int i=0;i<1024;i++)s.b[i]=0;__builtin_ia32_xsave(s.b,3);"
            "unsigned control=s.b[0]|s.b[1]<<8|(unsigned)s.b[24]<<16|(unsigned)s.b[25]<<24;FOLD(control);__builtin_ia32_xrstor(s.b,3);"
            "__builtin_ia32_xsave64(s.b,3);control=s.b[0]|s.b[1]<<8|(unsigned)s.b[24]<<16|(unsigned)s.b[25]<<24;FOLD(control);"
            "__builtin_ia32_xrstor64(s.b,3);__builtin_ia32_xsavec(s.b,3);__builtin_ia32_xsavec64(s.b,3);__builtin_ia32_xsaveopt(s.b,3);"
            "__builtin_ia32_xsaveopt64(s.b,3);struct{_Alignas(16) unsigned char b[512];}f;for(int i=0;i<512;i++)f.b[i]=0;__builtin_ia32_fxsave(f.b);"
            "control=f.b[0]|f.b[1]<<8|(unsigned)f.b[24]<<16|(unsigned)f.b[25]<<24;FOLD(control);__builtin_ia32_fxrstor(f.b);__builtin_ia32_fxsave64(f.b);"
            "control=f.b[0]|f.b[1]<<8|(unsigned)f.b[24]<<16|(unsigned)f.b[25]<<24;FOLD(control);__builtin_ia32_fxrstor64(f.b);unsigned aux=0;"
            "unsigned long long t=__builtin_ia32_rdtscp(&aux);unsigned ticks=(t|1u)!=0;FOLD(ticks);"
            "unsigned interrupts=(__builtin_ia32_readeflags_u64()&0x200u)!=0;FOLD(interrupts);__builtin_ia32_vzeroupper();__builtin_ia32_vzeroall();"
            "int line[16];for(int i=0;i<16;i++)line[i]=i+n;__builtin_ia32_clflushopt(line);__builtin_ia32_clwb(line+8);int nt;long long nt64;"
            "__builtin_ia32_movnti(&nt,n*3);__builtin_ia32_movnti64(&nt64,(long long)m);FOLD(nt);FOLD(nt64);FOLD(line);__builtin_ia32_emms();"
            "return (int)h;}",
            BUSTER_X86_FEATURE(XSAVE) | BUSTER_X86_FEATURE(XSAVEC) | BUSTER_X86_FEATURE(XSAVEOPT) | BUSTER_X86_FEATURE(CLFLUSHOPT) | BUSTER_X86_FEATURE(CLWB),
        },
        {
            // The direct stores of 4, 8 and 64 bytes and the instructions' prefetch hints, which Zen 5 has and Zen 4 hasn't.
            "x86-direct-stores",
            "int tested(int n){unsigned h=2166136261u;unsigned d32=0;unsigned long d64=0;__builtin_ia32_directstore_u32(&d32,(unsigned)n*2654435761u);"
            "__builtin_ia32_directstore_u64(&d64,(unsigned long)mask(n));FOLD(d32);FOLD(d64);struct{_Alignas(64) unsigned char b[64];}src,dst;"
            "for(int i=0;i<64;i++){src.b[i]=(unsigned char)(i*3+n);dst.b[i]=0;}__builtin_ia32_movdir64b(dst.b,src.b);FOLD(dst);"
            "__builtin_ia32_prefetchi(&h,3);__builtin_ia32_prefetchi(&h,2);return (int)h;}",
            BUSTER_X86_FEATURE(MOVDIRI) | BUSTER_X86_FEATURE(MOVDIR64B) | BUSTER_X86_FEATURE(PREFETCHI),
        },
        {
            // The builtins of the operating system's instructions, which compile and link and don't run: the FS and GS bases,
            // the processor's id, the shadow stack's, the TLB's and the caches' invalidations, the supervisor state's saves
            // and restores, the protection keys, the performance counters, the monitor's waits and the flags' write.
            "x86-system",
            "int tested(int n){unsigned h=2166136261u;unsigned long long m=mask(n);unsigned b32=__builtin_ia32_rdfsbase32();"
            "unsigned long long b64=__builtin_ia32_rdgsbase64();unsigned g32=__builtin_ia32_rdgsbase32();"
            "unsigned long long f64=__builtin_ia32_rdfsbase64();FOLD(b32);FOLD(b64);FOLD(g32);FOLD(f64);__builtin_ia32_wrfsbase32(b32);"
            "__builtin_ia32_wrfsbase64(f64);__builtin_ia32_wrgsbase32(g32);__builtin_ia32_wrgsbase64(b64);unsigned p=__builtin_ia32_rdpid();FOLD(p);"
            "unsigned s32=__builtin_ia32_rdsspd((unsigned)n);unsigned long long s64=__builtin_ia32_rdsspq(m);FOLD(s32);FOLD(s64);"
            "__builtin_ia32_incsspd((unsigned)n);__builtin_ia32_incsspq(m);unsigned long long cell[4]={0,0,0,0};__builtin_ia32_wrssd((unsigned)n,cell);"
            "__builtin_ia32_wrssq(m,cell+1);__builtin_ia32_wrussd((unsigned)n,cell+2);__builtin_ia32_wrussq(m,cell+3);__builtin_ia32_rstorssp(cell);"
            "__builtin_ia32_clrssbsy(cell);__builtin_ia32_saveprevssp();__builtin_ia32_setssbsy();"
            "unsigned long long descriptor[2]={(unsigned long long)n,m};__builtin_ia32_invpcid((unsigned)n&3u,descriptor);__builtin_ia32_wbinvd();"
            "__builtin_ia32_wbnoinvd();struct{_Alignas(64) unsigned char b[1024];}x;__builtin_ia32_xsaves(x.b,m);__builtin_ia32_xrstors(x.b,m);"
            "__builtin_ia32_xsaves64(x.b,m);__builtin_ia32_xrstors64(x.b,m);unsigned k=__builtin_ia32_rdpkru();__builtin_ia32_wrpkru(k);FOLD(k);"
            "unsigned long long c=__builtin_ia32_rdpmc(n)+__builtin_ia32_rdpru(n);FOLD(c);__builtin_ia32_monitor(cell,0u,0u);__builtin_ia32_mwait(0u,0u);"
            "__builtin_ia32_writeeflags_u64(__builtin_ia32_readeflags_u64());return (int)h;}",
            0,
            true,
        },
        {
            // VP2INTERSECT's pairs of masks at each width, of vectors that share some elements: each mask stored at its
            // pointer, the bytes around it left.
            "x86-intersections",
            "int tested(int n){unsigned h=2166136261u;v4si a4,b4;v8si a8,b8;v16si a16,b16;v2di c2,d2;v4di c4,d4;v8di c8,d8;fill(&a4,16,n);"
            "fill(&b4,16,n+1);fill(&a8,32,n+2);fill(&b8,32,n+3);fill(&a16,64,n+4);fill(&b16,64,n+5);fill(&c2,16,n+6);fill(&d2,16,n+7);fill(&c4,32,n+8);"
            "fill(&d4,32,n+9);fill(&c8,64,n+10);fill(&d8,64,n+11);for(int i=0;i<16;i++){a16[i]&=7;b16[i]=(b16[i]&7)+(n&1);}for(int i=0;i<8;i++){a8[i]&=3;"
            "b8[i]&=3;c8[i]&=7;d8[i]=(d8[i]&7)^(n&2);}for(int i=0;i<4;i++){a4[i]&=3;b4[i]&=3;c4[i]&=3;d4[i]&=3;}for(int i=0;i<2;i++){c2[i]&=1;d2[i]&=1;}"
            "unsigned char m[14]={0x5a,0x5a,0x5a,0x5a,0x5a,0x5a,0x5a,0x5a,0x5a,0x5a,0x5a,0x5a,0x5a,0x5a};unsigned short w[4]={0x5a5a,0x5a5a,0x5a5a,"
            "0x5a5a};__builtin_ia32_vp2intersect_d_128(a4,b4,&m[1],&m[2]);__builtin_ia32_vp2intersect_d_256(a8,b8,&m[4],&m[5]);"
            "__builtin_ia32_vp2intersect_d_512(a16,b16,&w[1],&w[2]);__builtin_ia32_vp2intersect_q_128(c2,d2,&m[7],&m[8]);"
            "__builtin_ia32_vp2intersect_q_256(c4,d4,&m[10],&m[11]);__builtin_ia32_vp2intersect_q_512(c8,d8,&m[12],&m[13]);FOLD(m);FOLD(w);return (int)h;"
            "}",
            BUSTER_X86_FEATURE(AVX512VP2INTERSECT),
        },
        {
            // #148's SSE builtins: shuffles, the byte shift and the align past their vectors' bytes (zeros, or a's
            // bytes alone), the word's extraction and insertion, packs, multiplications, and the floating ones on
            // random bits (NaNs with payloads, denormals) and on the pairs where MAXPS and MINPS give their second
            // operand (a NaN in either, zeros of either sign).
            "x86-sse",
            "int tested(int n){unsigned h=2166136261u;v4si a,b;v8hi w,w2;v16qi c,d;v4sf f,g;fill(&a,16,n);fill(&b,16,n+1);fill(&w,16,n+2);"
            "fill(&w2,16,n+3);fill(&c,16,n+4);fill(&d,16,n+5);fill(&f,16,n+6);fill(&g,16,n+7);"
            "v4si s[]={__builtin_ia32_pshufd(a,0x1b),__builtin_ia32_pshufd(b,0),__builtin_ia32_pshufd(a,255)};FOLD(s);"
            "v8hi sw[]={__builtin_ia32_pshufhw(w,0x4e),__builtin_ia32_pshuflw(w,0xb1),__builtin_ia32_pshufhw(w2,255),__builtin_ia32_pshuflw(w2,0)};"
            "FOLD(sw);v4sf sp[]={__builtin_ia32_shufps(f,g,0x4e),__builtin_ia32_shufps(g,f,0xe4),__builtin_ia32_shufps(f,f,255)};FOLD(sp);"
            "v16qi sr[]={__builtin_ia32_psrldqi128_byteshift(c,3),__builtin_ia32_psrldqi128_byteshift(c,15),__builtin_ia32_psrldqi128_byteshift(c,16),"
            "__builtin_ia32_psrldqi128_byteshift(c,255)};FOLD(sr);"
            "v16qi al[]={__builtin_ia32_palignr128(c,d,0),__builtin_ia32_palignr128(c,d,5),__builtin_ia32_palignr128(c,d,16),"
            "__builtin_ia32_palignr128(c,d,20),__builtin_ia32_palignr128(c,d,32),__builtin_ia32_palignr128(c,d,255)};FOLD(al);"
            "short e[]={__builtin_ia32_vec_ext_v8hi(w,0),__builtin_ia32_vec_ext_v8hi(w,7)};int ei=__builtin_ia32_vec_ext_v8hi(w2,5);FOLD(e);FOLD(ei);"
            "v8hi st[]={__builtin_ia32_vec_set_v8hi(w,(short)(n*977),3),__builtin_ia32_vec_set_v8hi(w2,-1,7)};FOLD(st);"
            "v8hi pk=__builtin_ia32_packssdw128(a,b);v16qi pu=__builtin_ia32_packuswb128(w,w2);v8hi mh=__builtin_ia32_pmulhw128(w,w2);"
            "FOLD(pk);FOLD(pu);FOLD(mh);v2di md=__builtin_ia32_pmuldq128(a,b);v2di mu=__builtin_ia32_pmuludq128(a,b);FOLD(md);FOLD(mu);"
            "union{unsigned u[4];v4sf v;}z={{0x80000000u,0,0x7fc00000u,0x3f800000u}},z2={{0,0x80000000u,0x3f800000u,0xffc00001u}};"
            "v4sf mx[]={__builtin_ia32_maxps(f,g),__builtin_ia32_minps(f,g),__builtin_ia32_maxps(z.v,z2.v),__builtin_ia32_maxps(z2.v,z.v),"
            "__builtin_ia32_minps(z.v,z2.v),__builtin_ia32_minps(z2.v,z.v)};FOLD(mx);"
            "v4sf half={(float)n+0.5f,-2.5f,(float)n*1e9f,-0.5f};v4si cv[]={__builtin_ia32_cvtps2dq(f),__builtin_ia32_cvtps2dq(half),"
            "__builtin_ia32_cvtps2dq(z.v),__builtin_ia32_cvtps2dq(z2.v)};FOLD(cv);"
            "return (int)h;}",
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
        const size_t length = strlen(check_test_x86_types) + strlen(programs[i].body) + 1;
        char* source = malloc(length);
        check_test_expect(test, source != 0, "x86 builtin program source");
        if (source != 0)
        {
            snprintf(source, length, "%s%s", check_test_x86_types, programs[i].body);
            check_test_program_source_native_limits(test, source, programs[i].name, clang, false, -1, 0, target, run);
            free(source);
        }
    }
    // The SSE builtins through the headers' intrinsics, as the corpora call them: a program that includes them, which its
    // preprocessing finds where the oracle's compile does.
    check_test_program_includes = true;
    check_test_program_x86_source(test, clang, target, run_native, "x86-sse-intrinsics",
                                  "#include <smmintrin.h>\n"
                                  "int tested(int n){unsigned h=2166136261u;__m128i a,b;__m128 f,g;fill(&a,16,n);fill(&b,16,n+1);fill(&f,16,n+2);"
                                  "fill(&g,16,n+3);__m128i r[]={_mm_shuffle_epi32(a,_MM_SHUFFLE(3,1,2,0)),_mm_shufflehi_epi16(a,0x4e),"
                                  "_mm_shufflelo_epi16(b,0xb1),_mm_srli_si128(a,5),_mm_alignr_epi8(a,b,7),_mm_packs_epi32(a,b),_mm_packus_epi16(a,b),"
                                  "_mm_mulhi_epi16(a,b),_mm_mul_epi32(a,b),_mm_mul_epu32(a,b),_mm_insert_epi16(a,n*31,6),_mm_cvtps_epi32(f)};FOLD(r);"
                                  "__m128 m[]={_mm_shuffle_ps(f,g,_MM_SHUFFLE(0,1,2,3)),_mm_max_ps(f,g),_mm_min_ps(f,g)};FOLD(m);"
                                  "int e[]={_mm_extract_epi16(a,5),_mm_extract_epi16(b,0)};FOLD(e);"
                                  "return (int)h;}");
    check_test_program_includes = false;
}

// What the lowering refuses by name: a builtin the table doesn't name, and one whose instructions the target hasn't.
static void check_test_program_x86_refusals(CheckTest* test)
{
    X86Target baseline = { 0 };
    const bool baseline_parsed = x86_target_parse("x86-64", strlen("x86-64"), &baseline);
    check_test_expect(test, baseline_parsed, "the integer-only opmask refusals' baseline target");
    static const char* const masks[] = {
        "int tested(int n){return __builtin_ia32_kandhi((unsigned short)n,3);}",
        "int tested(int n){return __builtin_ia32_kmovw((unsigned short)n);}",
        "int tested(int n){return __builtin_ia32_kshiftlihi((unsigned short)n,3);}",
    };
    for (uint32_t i = 0; baseline_parsed && i < BUSTER_ARRAY_LENGTH(masks); i += 1)
    {
        check_test_program_boundary_target(test, masks[i], "integer-only-opmask-without-avx512", true, false, 0, 0,
                                           "x86 builtins whose instructions the target hasn't", &baseline);
    }
    // #148's SSE builtins pass the checker at the default target, as clang's do there, and the lowering refuses their
    // vectors.
    if (baseline_parsed)
    {
        check_test_program_boundary_target(test, "typedef int v4si __attribute__((vector_size(16)));int tested(int n){v4si a={n,1,2,3};"
                                                 "v4si b=__builtin_ia32_pshufd(a,0x1b);return b[0];}",
                                           "sse-builtin-without-avx512", true, false, 0, 2, "vector types without AVX-512F and AVX-512VL", &baseline);
    }
    X86Target v4 = { 0 };
    const bool parsed = x86_target_parse("x86-64-v4", strlen("x86-64-v4"), &v4);
    check_test_expect(test, parsed, "the x86 builtins' refusals' target, x86-64-v4");
    if (parsed)
    {
        check_test_program_boundary_target(test, "typedef char v64qi __attribute__((vector_size(64)));int tested(int n){v64qi a={(char)n},b={1};"
                                                 "v64qi c=__builtin_ia32_vpmultishiftqb512(a,b);return c[0];}",
                                           "x86-builtin-without-its-features", true, false, 0, 3, "x86 builtins whose instructions the target hasn't", &v4);
    }
}

// Every rule's row, with every feature and at immediates around its fields' edges, is one Tier 0 places and emits
// (tier0_operation_valid: each field's role of its register's kind, and the record placed one x86_validate takes), the
// rows main.c's builtins don't reach among them, its integer operands widened as the lowering widens its arguments'
// (check_lower_x86_operand_class); a rule with a rounding control at each value clang takes for it, and one past them.
static void check_test_program_x86_rows(CheckTest* test)
{
    static const uint64_t immediates[] = { 0, 1, 2, 3, 4, 5, 7, 8, 15, 16, 31, 63, 0x1b, 0x80, 0xff };
    static const uint64_t rounds[] = { 4, 8, 9, 10, 11, 12, 15 };
    const X86Target every = { .features = UINT64_MAX, .extensions = UINT64_MAX };
    uint32_t rows = 0;
    uint32_t invalid = 0;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(check_x86_lower_rules); i += 1)
    {
        const CheckX86LowerRule* rule = check_x86_lower_rules + i;
        for (uint32_t k = 0; k < BUSTER_ARRAY_LENGTH(immediates) * BUSTER_ARRAY_LENGTH(rounds); k += 1)
        {
            const uint64_t immediate = immediates[k % BUSTER_ARRAY_LENGTH(immediates)];
            const uint64_t round = rounds[k / BUSTER_ARRAY_LENGTH(immediates)];
            Tier0Operation row;
            if ((round == 4 || rule->rounding != CHECK_X86_ROUND_NONE) && check_x86_lower_row_of(&every, rule, immediate, round, &row) == NULL)
            {
                for (uint32_t j = 0; j < row.operand_count; j += 1)
                {
                    row.operand_classes[j] = row.operand_classes[j] == BUSTER_TYPED_CLASS_VECTOR ? row.operand_classes[j] :
                                                                                                    check_lower_x86_operand_class(row.operand_classes[j]);
                }
                // A reduction's second row (check_x86_lower_reduce_row) too.
                Tier0Operation tail;
                const bool reduce = rule->family == CHECK_X86_LOWER_REDUCE;
                if (reduce)
                {
                    check_x86_lower_reduce_row(rule, &tail);
                }
                rows += reduce ? 2 : 1;
                if (!tier0_operation_valid(&row) || (reduce && !tier0_operation_valid(&tail)))
                {
                    invalid += 1;
                    if (invalid <= 8)
                    {
                        printf("  x86 rows: %s at immediate %llu and rounding %llu isn't a row Tier 0 takes\n", rule->name, (unsigned long long)immediate,
                               (unsigned long long)round);
                    }
                }
            }
        }
    }
    char description[128];
    snprintf(description, sizeof(description), "every x86 rule's row is one Tier 0 takes (%u rows, %u not)", rows, invalid);
    check_test_expect(test, rows != 0 && invalid == 0, description);
}

// A function's target attribute (#180, check_program_targets): in a baseline unit, functions of BMI, BMI2 and CRC32
// builtins, each by its own attribute, an inherited one, an arch=, a list that turns a feature on and off, a trimmed one
// and "default", against clang -O0's code; then what doesn't apply, refused as without the attribute (one after the
// definition, one clang ignores, one that turns the feature off), a function of several versions (target attributes
// of two strings, target_clones), and vectors where the unit's target or the function's has no AVX-512F and VL.
static void check_test_program_x86_target_attributes(CheckTest* test, const char* clang)
{
    X86Target baseline = { 0 };
    X86Target v3 = { 0 };
    X86Target zen = { 0 };
    const bool parsed = x86_target_parse("x86-64", strlen("x86-64"), &baseline) && x86_target_parse("x86-64-v3", strlen("x86-64-v3"), &v3) &&
                        x86_target_parse("znver5", strlen("znver5"), &zen);
    check_test_expect(test, parsed, "the target attributes' units' targets");
    const X86Target native = x86_target_native();
    const uint64_t needed = BUSTER_X86_FEATURE(BMI) | BUSTER_X86_FEATURE(BMI2) | BUSTER_X86_FEATURE(SSE42) | BUSTER_X86_FEATURE(CRC32);
    if (parsed)
    {
        check_test_program_build_release();
        check_test_program_march = "-march=x86-64";
        check_test_program_source_native_limits(test,
                                                "__attribute__((target(\"bmi2\"))) static unsigned deposit(unsigned x, unsigned m);"
                                                "static unsigned deposit(unsigned x, unsigned m){return __builtin_ia32_pdep_si(x,m);}"
                                                "__attribute__((target(\"arch=x86-64-v3\"))) static unsigned long long extract(unsigned long long x, unsigned long long m)"
                                                "{return __builtin_ia32_pext_di(x,m);}"
                                                "__attribute__((target(\"bmi\"))) static unsigned field(unsigned x, unsigned c){return __builtin_ia32_bextr_u32(x,c);}"
                                                "__attribute__((target(\" crc32 \"))) static unsigned check(unsigned c, unsigned char b){return __builtin_ia32_crc32qi(c,b);}"
                                                "__attribute__((target(\"avx2,no-avx2,bmi2\"))) static unsigned low(unsigned x, unsigned n){return __builtin_ia32_bzhi_si(x,n);}"
                                                "static unsigned plain(unsigned x);__attribute__((target(\"default\"))) static unsigned plain(unsigned x){return x*3u;}"
                                                "int tested(int n){unsigned h=2166136261u;for(int i=0;i<8;i++){unsigned x=(unsigned)n*2654435761u+(unsigned)i*40503u;"
                                                "h=(h^deposit(x,0x0f0f0f0fu))*16777619u;"
                                                "h=(h^(unsigned)extract((unsigned long long)x<<17|x,0x00ff00ff00ff00ffull))*16777619u;"
                                                "h=(h^field(x,0x0804u+(unsigned)i))*16777619u;h=(h^check(h,(unsigned char)x))*16777619u;"
                                                "h=(h^low(x,(unsigned)i*4u))*16777619u;h=(h^plain(x))*16777619u;}return (int)h;}",
                                                "x86-target-attributes", clang, false, -1, 0, &baseline, (native.features & needed) == needed);
        check_test_program_build_release();
        static const struct
        {
            const char* name;
            const char* source;
            const char* reason;
        } refusals[] = {
            { "target-attribute-after-definition",
              "int tested(int n){return (int)__builtin_ia32_bzhi_si((unsigned)n,3u);}__attribute__((target(\"bmi2\"))) int tested(int n);",
              "x86 builtins whose instructions the target hasn't" },
            { "target-attribute-ignored", "__attribute__((target(\"bmi2,bogus\"))) int tested(int n){return (int)__builtin_ia32_bzhi_si((unsigned)n,3u);}",
              "x86 builtins whose instructions the target hasn't" },
            { "target-attribute-versions", "__attribute__((target(\"bmi\"))) int tested(int n);__attribute__((target(\"bmi2\"))) int tested(int n){return n;}",
              "functions of several target versions (target_clones, or target attributes of other strings)" },
            { "target-attribute-later-version", "__attribute__((target(\"bmi2\"))) int tested(int n){return n;}__attribute__((target(\"bmi\"))) int tested(int n);",
              "functions of several target versions (target_clones, or target attributes of other strings)" },
            { "target-clones", "__attribute__((target_clones(\"bmi2\",\"default\"))) int tested(int n){return n;}",
              "functions of several target versions (target_clones, or target attributes of other strings)" },
        };
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(refusals); i += 1)
        {
            check_test_program_boundary_target(test, refusals[i].source, refusals[i].name, true, false, 0, 0, refusals[i].reason, &baseline);
        }
        check_test_program_march = "-march=x86-64-v3";
        check_test_program_boundary_target(test, "__attribute__((target(\"no-bmi2\"))) int tested(int n){return (int)__builtin_ia32_bzhi_si((unsigned)n,3u);}",
                                           "target-attribute-off", true, false, 0, 0, "x86 builtins whose instructions the target hasn't", &v3);
        check_test_program_march = "-march=znver5";
        check_test_program_boundary_target(test,
                                           "typedef int v4si __attribute__((vector_size(16)));"
                                           "__attribute__((target(\"no-avx512vl\"))) int tested(int n){v4si a={n},b={2};a=b;return a[0];}",
                                           "target-attribute-without-avx512vl", true, false, 0, 2,
                                           "vector types in a function whose target attribute turns off AVX-512F or AVX-512VL", &zen);
        check_test_program_march = "-march=x86-64";
        check_test_program_boundary_target(test,
                                           "typedef int v4si __attribute__((vector_size(16)));"
                                           "__attribute__((target(\"avx512f,avx512vl\"))) int tested(int n){v4si a={n},b={2};a=b;return a[0];}",
                                           "target-attribute-vectors-in-a-baseline-unit", true, false, 0, 2, "vector types without AVX-512F and AVX-512VL",
                                           &baseline);
    }
}

static __attribute__((noinline)) void check_test_program_x86(CheckTest* test, const char* clang)
{
    check_test_program_x86_rows(test);
    X86Target target = { 0 };
    const bool parsed = x86_target_parse("znver5", strlen("znver5"), &target);
    check_test_expect(test, parsed, "the x86 builtin programs' target, znver5");
    if (parsed)
    {
        // The features the programs' rows take, which the host must have and save to run them.
        const uint64_t required = BUSTER_X86_FEATURE(AVX512F) | BUSTER_X86_FEATURE(AVX512BW) | BUSTER_X86_FEATURE(AVX512CD) |
                                  BUSTER_X86_FEATURE(AVX512DQ) | BUSTER_X86_FEATURE(AVX512VL) | BUSTER_X86_FEATURE(AVX512VBMI) |
                                  BUSTER_X86_FEATURE(AVX512VBMI2) | BUSTER_X86_FEATURE(AVX512BITALG) | BUSTER_X86_FEATURE(AVX512VNNI) |
                                  BUSTER_X86_FEATURE(GFNI) | BUSTER_X86_FEATURE(VAES) | BUSTER_X86_FEATURE(AVX2) | BUSTER_X86_FEATURE(BMI2) |
                                  BUSTER_X86_FEATURE(AVX512IFMA) | BUSTER_X86_FEATURE(AVX512BF16) | BUSTER_X86_FEATURE(AES) | BUSTER_X86_FEATURE(FMA);
        const X86Target native = x86_target_native();
        const bool run_native = (native.features & required) == required;
        if (!run_native)
        {
            printf("  x86 builtins: native execution skipped (the host lacks their features or their saved state); the programs compile and link\n");
        }
        check_test_program_build_release();
        check_test_program_march = "-march=znver5";
        check_test_program_x86_sources(test, clang, &target, run_native);
        check_test_program_build_release();
        check_test_program_march = "-march=x86-64";
        check_test_program_x86_refusals(test);
        X86Target bmi2 = { 0 };
        const bool bmi2_parsed = x86_target_parse("x86-64-v3", strlen("x86-64-v3"), &bmi2);
        check_test_expect(test, bmi2_parsed, "the integer-only BMI2 target without AVX-512");
        if (bmi2_parsed)
        {
            check_test_program_march = "-march=x86-64-v3";
            check_test_program_source_native_limits(test, "int tested(int n){return (int)__builtin_ia32_pdep_si((unsigned)n,0x55555555u);}",
                                                    "integer-only-BMI2-without-avx512", clang, false, -1, 0, &bmi2,
                                                    (native.features & bmi2.features) == bmi2.features);
            check_test_program_build_release();
            check_test_program_march = "-march=x86-64";
        }
        check_test_program_x86_target_attributes(test, clang);
    }
}
