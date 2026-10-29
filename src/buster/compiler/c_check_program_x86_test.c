// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #136's x86 builtins in the lowering (c_check_program_x86.c), against clang -O0's code of the same source for znver5:
// each family's builtins called directly, at their prototypes' types, on inputs from the call's argument, with every
// result folded into the program's hash. The programs run where the host has the features their rows take (AVX-512F,
// BW, CD, DQ, VL, VBMI, VBMI2, BITALG and VNNI, GFNI, VAES, AVX2 and BMI2, by x86_target_native's CPUID and XCR0), and
// those of whose code clang takes more where it has that too (the dot products' AVX-VNNI); elsewhere they compile and
// link, as clang's builds do, and don't run. Include after c_check_program_vector_test.c.

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

// An x86 builtin program: its name, its body after check_test_x86_types, and the features clang's code of it takes past
// those every program's rows take, which the host must have too to run it.
typedef struct CheckTestX86Program
{
    const char* name;
    const char* body;
    uint64_t features;
} CheckTestX86Program;

static void check_test_program_x86_sources(CheckTest* test, const char* clang, const X86Target* target, bool run_native)
{
    static const CheckTestX86Program programs[] = {
        {
            // The floating selects, which move bits: each width of ps, pd and pbf, and the scalar ones, whose first element
            // is a's or w's by the mask's first bit and whose others are a's. (ph's rows are pbf's; their builtins need
            // AVX512-FP16, which znver5 hasn't.)
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
    };
    const X86Target native = x86_target_native();
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(programs); i += 1)
    {
        const bool run = run_native && (native.features & programs[i].features) == programs[i].features;
        if (run_native && !run)
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
// (check_lower_x86_operand_class).
static void check_test_program_x86_rows(CheckTest* test)
{
    static const uint64_t immediates[] = { 0, 1, 2, 3, 4, 5, 7, 8, 15, 16, 31, 63, 0x1b, 0x80, 0xff };
    uint32_t rows = 0;
    uint32_t invalid = 0;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(check_x86_lower_rules); i += 1)
    {
        const CheckX86LowerRule* rule = check_x86_lower_rules + i;
        for (uint32_t k = 0; k < BUSTER_ARRAY_LENGTH(immediates); k += 1)
        {
            Tier0Operation row;
            if (check_x86_lower_row(UINT64_MAX, rule->name, rule->name_length, immediates[k], &row) == NULL)
            {
                for (uint32_t j = 0; j < row.operand_count; j += 1)
                {
                    row.operand_classes[j] = row.operand_classes[j] == BUSTER_TYPED_CLASS_VECTOR ? row.operand_classes[j] :
                                                                                                    check_lower_x86_operand_class(row.operand_classes[j]);
                }
                rows += 1;
                if (!tier0_operation_valid(&row))
                {
                    invalid += 1;
                    if (invalid <= 8)
                    {
                        printf("  x86 rows: %s at immediate %llu isn't a row Tier 0 takes\n", rule->name, (unsigned long long)immediates[k]);
                    }
                }
            }
        }
    }
    char description[128];
    snprintf(description, sizeof(description), "every x86 rule's row is one Tier 0 takes (%u rows, %u not)", rows, invalid);
    check_test_expect(test, rows != 0 && invalid == 0, description);
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
                                  BUSTER_X86_FEATURE(GFNI) | BUSTER_X86_FEATURE(VAES) | BUSTER_X86_FEATURE(AVX2) | BUSTER_X86_FEATURE(BMI2);
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
    }
}
