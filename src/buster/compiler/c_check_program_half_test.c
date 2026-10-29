// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #136's half-precision scalars in the lowering (c_check_program_half.c, for #129's --lower-all of main.c), against
// clang -O0's code of the same source: _Float16's, __fp16's and __bf16's values in arithmetic chains, compares and truth,
// compound assignments, increments, conditionals and statement values; conversions to and from every integer and
// floating type, between _Float16 and the others, of constants and as bit casts; calls passing and returning them alone,
// in structs and through va_arg, with members, arrays, globals and unions of them; unary chains over signaling NaNs and
// subnormals; and floating-point flags under FENV_ACCESS. Each program runs for five targets, each where the host has
// the features clang's code of it may take and their saved state, and is compiled and linked elsewhere: x86-64 (libgcc's
// conversions), x86-64-v3 (F16C's), x86-64-v4 (AVX-512 without BF16), znver5 (AVX512-BF16's rounding) and
// sapphirerapids (AVX512-FP16's arithmetic, which rounds each operation). Then what the lowering refuses by name. What
// clang 23 compiles wrong the programs leave out (docs/c-checker.md, "Half-precision values"): conversions between
// __fp16 and __bf16, __bf16 under FENV_ACCESS, and __bf16 parameters' subnormals and signaling NaNs. They leave out too
// what the lowering doesn't follow: a unary + alone of a signaling NaN in a function with FENV_ACCESS. Include after
// c_check_program_fp16_test.c.

// The programs' helpers: the hash, half-precision values from their bits, and bits of special values (zeros,
// infinities, a quiet and a signaling NaN, subnormals, ones, the largest finite one, the smallest normal one, and
// others), as binary16's.
static const char check_test_half_helpers[] =
    "static unsigned fold(unsigned h,const void*p,unsigned long n){const unsigned char*b=p;for(unsigned long i=0;i<n;i++)h=(h^b[i])*16777619u;return h;}\n"
    "#define FOLD(v) h=fold(h,&(v),sizeof(v))\n"
    "#define FOLDLD(v) do{long double t_=(v);unsigned char b_[10];__builtin_memcpy(b_,&t_,10);FOLD(b_);}while(0)\n"
    "static const unsigned short specials[]={0x0000,0x8000,0x7c00,0xfc00,0x7e00,0x7d01,0x0001,0x8001,0x3c00,0xbc00,0x7bff,0x0400,0x03ff,0x3555,0x4248,0xc8a0};\n"
    "static _Float16 hb(unsigned short b){_Float16 x;__builtin_memcpy(&x,&b,2);return x;}\n"
    "static __bf16 bb(unsigned short b){__bf16 x;__builtin_memcpy(&x,&b,2);return x;}\n";

static const char* const check_test_half_programs[][2] = {
    {
        // Excess precision's chains (each rounds once, where it ends) and native arithmetic's (each operation rounds):
        // a product past 65504 that a quotient brings back, mixed int, float and double operands, compares, truth,
        // compound assignments, increments past 2048 (where 1 rounds away), ?:, the comma and statement values.
        "half-arithmetic",
        "int tested(int n){unsigned h=2166136261u;"
        "_Float16 a=(_Float16)n/(_Float16)4,b=(_Float16)(n*n)-(_Float16)7,c=hb(specials[(unsigned)(n+3)%16u]),d=(_Float16)1000*(_Float16)n;"
        "_Float16 r[]={a+b,a-b,a*b,a/(b+(_Float16)0.5),a*b+d,d*d/d,(d*d-d*d)+a,-(a+b),+a,-c,+c,-(-c),-+-c,a+b*d-c,c*a,(a+b)*(a-b)/(d+(_Float16)1)};FOLD(r);"
        "float f=a*2.5f;double g=b*2.5;_Float16 m=a*n+b;_Float16 k=a+(_Float16)3.25*(_Float16)n;FOLD(f);FOLD(g);FOLD(m);FOLD(k);"
        "int cmp=(a<b)+2*(a<=c)+4*(a==c)+8*(c!=c)+16*(a>b)+32*(a>=d)+64*(c<1.0f)+128*(a==n)+256*(b>2.5);FOLD(cmp);"
        "int t=!c+2*(c?1:0)+4*(a&&b)+8*(c||a);if(c)t+=16;while(b>(_Float16)100)b=b-(_Float16)100;FOLD(t);FOLD(b);"
        "_Float16 u=a;u+=b*d;u-=c*(_Float16)0;u*=(_Float16)3;u/=b+(_Float16)9;u+=1;u-=0.5;u*=2.5f;FOLD(u);"
        "_Float16 i=d;i++;++i;i--;--i;_Float16 j=(_Float16)2048;j++;_Float16 l=j--;FOLD(i);FOLD(j);FOLD(l);"
        "_Float16 s=n>3?a+b:a-b;_Float16 q=(a,b+c);_Float16 e=({_Float16 z=a*b;z+d;});FOLD(s);FOLD(q);FOLD(e);"
        "_Float16 big=(_Float16)60000;_Float16 o=big*(_Float16)(n+2)/(_Float16)(n+4);_Float16 p=(big+big)-big;FOLD(o);FOLD(p);"
        "return (int)h;}",
    },
    {
        // To and from floats, doubles and long doubles (subnormals, ties, overflow), every integer type in range
        // (__int128's through libgcc's own), bool, constants (an integer one to __bf16 rounded once, as clang folds it,
        // but a conditional's at run time, through float), bit casts, and between _Float16 and __bf16.
        "half-conversions",
        "int tested(int n){unsigned h=2166136261u;"
        "_Float16 c=hb(specials[(unsigned)(n+3)%16u]);__bf16 cb=bb(specials[(unsigned)(n+7)%16u]);"
        "float f=c;double d=c;long double l=c;float fb=cb;double db=cb;FOLD(f);FOLD(d);FOLDLD(l);FOLD(fb);FOLD(db);"
        "const float fs[]={0.1f,-3.75f,65504.0f,65520.0f,1e-8f,6.1e-5f,(float)n/3.0f,-0.0f,3.0e38f,1.0f/3.0f};"
        "for(int i=0;i<10;i++){_Float16 x=fs[i];__bf16 y=fs[i];FOLD(x);FOLD(y);}"
        "const double ds[]={0.1,1e-10,65519.99,(double)n*1.1,-1e300,1.0/3.0,5.960464477539063e-8,2.9802322387695312e-8};"
        "for(int i=0;i<8;i++){_Float16 x=ds[i];__bf16 y=ds[i];FOLD(x);FOLD(y);}"
        "const long double ls[]={0.1L,(long double)n/7.0L,65519.5L,-1e4000L,1.0L/3.0L};"
        "for(int i=0;i<5;i++){_Float16 x=ls[i];__bf16 y=ls[i];FOLD(x);FOLD(y);}"
        "signed char sc=(signed char)(n*5);short s=(short)(n*1000);int i=n*-3001;long long ll=(long long)n*4000;unsigned u=(unsigned)(n+3)*3000u;"
        "unsigned long long ul=(unsigned long long)(n+3)*3333u;__int128 w=(__int128)n*2000;unsigned __int128 uw=(unsigned __int128)(n+3)*2047u;_Bool bo=n&1;"
        "_Float16 from[]={sc,s,i,ll,u,ul,w,uw,bo,(char)n,(unsigned char)(n*13),(unsigned short)(n*3000)};FOLD(from);"
        "__bf16 bfrom[]={sc,s,i,ll,u,ul,w,uw,bo,(char)n,(unsigned char)(n*13),(unsigned short)(n*3000),16842753};FOLD(bfrom);"
        "_Float16 v=(_Float16)n*(_Float16)100.75;__bf16 vb=(__bf16)n*(__bf16)100.75;"
        "int ti=v;short ts=v;long long tl=v;unsigned tu=(_Float16)(n+3)*(_Float16)30;unsigned long long tul=(_Float16)(n+3)*(_Float16)300;"
        "signed char tc=(_Float16)n*(_Float16)5;unsigned char tuc=(_Float16)(n+3)*(_Float16)10;_Bool tb=c;__int128 tw=v;"
        "unsigned __int128 tuw=(_Float16)(n+3)*(_Float16)1000;int bi=vb;long long bl=vb;unsigned bu=(__bf16)(n+3)*(__bf16)30;_Bool bt=cb;__int128 bw=vb;"
        "FOLD(ti);FOLD(ts);FOLD(tl);FOLD(tu);FOLD(tul);FOLD(tc);FOLD(tuc);FOLD(tb);FOLD(tw);FOLD(tuw);FOLD(bi);FOLD(bl);FOLD(bu);FOLD(bt);FOLD(bw);"
        "_Float16 k[]={0.1f16,1e-7,65519,65520,-0.0,1.0/3.0,(_Float16)1e5,2049,4097.0,-0.0f16,6e-8,(_Float16)(1.0/0.0)};FOLD(k);"
        "__bf16 kb[]={0.1,1e-7,3.4e38,16842753,-0.0,(__bf16)1e39,257,1.0/3.0};FOLD(kb);"
        "__bf16 bj=n>100?1:16842753;__bf16 ba;ba=16842753;int cz=(__bf16)(n+16842750)==16842753;FOLD(bj);FOLD(ba);FOLD(cz);"
        "unsigned short bits=__builtin_bit_cast(unsigned short,c);_Float16 back=__builtin_bit_cast(_Float16,(unsigned short)(bits^0x8000));"
        "short sb=__builtin_bit_cast(short,cb);__bf16 bback=__builtin_bit_cast(__bf16,(short)(sb+1));FOLD(bits);FOLD(back);FOLD(sb);FOLD(bback);"
        "__bf16 hb2=c;_Float16 bh=cb;float viafp=(_Float16)cb;FOLD(hb2);FOLD(bh);FOLD(viafp);"
        "return (int)h;}",
    },
    {
        // Parameters and results in XMM registers' low 16 bits, past the eight registers on the stack, beside integers
        // and other floating values, through a pointer to a function; structs of them (SSE, and INTEGER where a short
        // shares their eightbyte); va_arg's; and arrays, globals with initializers, pointers, members and a union.
        "half-calls-and-memory",
        "struct pair{_Float16 a,b;};struct mixed{_Float16 a;short s;_Float16 b;};struct four{_Float16 a,b,c,d;float f;};struct bpair{__bf16 a,b;};"
        "static _Float16 garr[4]={0.1,0.2,0.3,0.4};static __bf16 gb=3.25;static struct pair gp={1.0,2.0};static _Float16 g1=1.5;"
        "static __attribute__((noinline)) _Float16 add3(_Float16 a,_Float16 b,_Float16 c){return a+b*c;}"
        "static __attribute__((noinline)) _Float16 many(_Float16 a0,_Float16 a1,_Float16 a2,_Float16 a3,_Float16 a4,_Float16 a5,_Float16 a6,_Float16 a7,"
        "_Float16 a8,_Float16 a9,_Float16 a10,_Float16 a11){return a0-a1+a2-a3+a4-a5+a6-a7+a8*(_Float16)2+a9*(_Float16)3+a10*(_Float16)4+a11*(_Float16)5;}"
        "static __attribute__((noinline)) double mixed_args(int i,_Float16 a,double d,__bf16 b,float f,long l,_Float16 c){return i+a*2+d+b*3+f+l+c;}"
        "static __attribute__((noinline)) struct pair mkpair(_Float16 a,_Float16 b){struct pair p={a,b+a};return p;}"
        "static __attribute__((noinline)) _Float16 sumpair(struct pair p,struct mixed m,struct four q){return p.a+p.b+m.a+m.b+q.a+q.d+(_Float16)m.s+(_Float16)q.f;}"
        "static __attribute__((noinline)) struct bpair mkb(__bf16 a){struct bpair p={a,a*(__bf16)2};return p;}"
        "static __attribute__((noinline)) double vsum(int count,...){__builtin_va_list ap;__builtin_va_start(ap,count);double s=0;"
        "for(int i=0;i<count;i++){_Float16 x=__builtin_va_arg(ap,_Float16);s=s*3+x;}__bf16 y=__builtin_va_arg(ap,__bf16);double z=__builtin_va_arg(ap,double);"
        "__builtin_va_end(ap);return s+y*0.5+z;}"
        "static __attribute__((noinline)) _Float16 through(_Float16(*f)(_Float16,_Float16,_Float16),_Float16 x){return f(x,x,(_Float16)2);}"
        "static __attribute__((noinline)) __bf16 bret(__bf16 a,__bf16 b){return a-b;}"
        "int tested(int n){unsigned h=2166136261u;"
        "_Float16 a=(_Float16)n/(_Float16)8,b=(_Float16)(n+1)*(_Float16)0.75,c=hb(specials[(unsigned)(n+3)%16u]);"
        "_Float16 r1=add3(a,b,c);FOLD(r1);_Float16 r2=many(a,b,a,b,a,b,a,b,a,b,(_Float16)n,c);FOLD(r2);"
        "double r3=mixed_args(n,a,0.25,(__bf16)n,1.5f,7,b);FOLD(r3);struct pair p=mkpair(a,b);FOLD(p);struct mixed m={a,(short)n,b};struct four q={a,b,c,a,2.5f};"
        "_Float16 r4=sumpair(p,m,q);FOLD(r4);struct bpair bp=mkb((__bf16)n);FOLD(bp);double r5=vsum(3,a,b,(_Float16)n,(__bf16)n,1.25);FOLD(r5);"
        "_Float16 r6=through(add3,a);FOLD(r6);__bf16 r7=bret((__bf16)n,gb);FOLD(r7);"
        "garr[n&3]+=a;garr[(n+1)&3]=garr[n&3]*b;_Float16*pt=garr;pt[2]-=(_Float16)1;*pt++=c;*pt+=a;FOLD(garr);"
        "gb=gb*(__bf16)n;gp.a=gp.b+a;gp.b=-gp.a;g1+=g1;FOLD(gb);FOLD(gp);FOLD(g1);"
        "struct mixed arr[3]={{a,1,b},{b,2,a},{c,3,c}};arr[n%3>=0?n%3:0].b=arr[1].a*(_Float16)3;FOLD(arr);"
        "union{_Float16 hv;unsigned short bits;__bf16 bv;}un;un.hv=a;un.bits^=1;FOLD(un);un.bv=(__bf16)a;FOLD(un);"
        "return (int)h;}",
    },
    {
        // Under FENV_ACCESS: a chain's flags (overflow and underflow in its rounding alone, inexact, division by zero),
        // a negation's round trip (which quiets a signaling NaN and raises FE_INVALID), a conversion's, a compare's and a
        // compound assignment's, and an integer constant's conversion, at run time. A unary + alone, whose round trip
        // clang's code doesn't fold here, is the lowering's operand (docs/c-checker.md), and so isn't one of them.
        "half-floating-flags",
        "\n#pragma STDC FENV_ACCESS ON\n"
        "int clear_fp(void);int fp_state(void);"
        "int tested(int n){unsigned h=2166136261u;volatile _Float16 big=(_Float16)60000,tiny=(_Float16)6.1e-5,one=(_Float16)1;"
        "_Float16 c=hb(specials[(unsigned)(n+3)%16u]);"
        "clear_fp();_Float16 r1=big*(_Float16)(n+4)/(_Float16)(n+5);int f1=fp_state();FOLD(r1);FOLD(f1);"
        "clear_fp();_Float16 r2=big*(_Float16)(n+4);int f2=fp_state();FOLD(r2);FOLD(f2);"
        "clear_fp();_Float16 r3=tiny*tiny;int f3=fp_state();FOLD(r3);FOLD(f3);"
        "clear_fp();_Float16 r4=one/(_Float16)3;int f4=fp_state();FOLD(r4);FOLD(f4);"
        "clear_fp();_Float16 r5=-c;int f5=fp_state();FOLD(r5);FOLD(f5);"
        "clear_fp();float r7=c;int f7=fp_state();FOLD(r7);FOLD(f7);"
        "clear_fp();_Float16 r8=(float)n/3.0f;int f8=fp_state();FOLD(r8);FOLD(f8);"
        "clear_fp();_Float16 r9=one/(_Float16)(n&1?0:2);int f9=fp_state();FOLD(r9);FOLD(f9);"
        "clear_fp();_Float16 r11=(_Float16)((double)n*1e-9);int f11=fp_state();FOLD(r11);FOLD(f11);"
        "clear_fp();int r12=c==c;int f12=fp_state();FOLD(r12);FOLD(f12);"
        "clear_fp();_Float16 r13=c;r13+=(_Float16)1;int f13=fp_state();FOLD(r13);FOLD(f13);"
        "clear_fp();_Float16 r15=60001;int f15=fp_state();FOLD(r15);FOLD(f15);"
        "return (int)h;}",
    },
    {
        // __fp16, which computes in float (its arithmetic's type) and rounds where it is stored: locals, a global array,
        // a pointer to them, compares, compound assignments and conversions to integers and to _Float16.
        "fp16-storage",
        "static __fp16 gs[3]={0.5,1.5,-2.25};"
        "static __attribute__((noinline)) float sf(__fp16* p,int i){return p[i]*2.0f+p[i+1];}"
        "int tested(int n){unsigned h=2166136261u;"
        "__fp16 a=(float)n/3.0f,b=n*1000,c;unsigned short bits=specials[(unsigned)(n+3)%16u];__builtin_memcpy(&c,&bits,2);"
        "float x=a+b;double y=a*c;__fp16 z=a*b;__fp16 w=z/(__fp16)7;int cmp=(a<b)+2*(c==c)+4*(a>=c);FOLD(x);FOLD(y);FOLD(z);FOLD(w);FOLD(cmp);"
        "__fp16 u=a;u+=b;u*=2;u-=(_Float16)1.5;FOLD(u);_Float16 hh=a;__fp16 back=hh+(_Float16)1;FOLD(hh);FOLD(back);"
        "gs[n&1]+=a;float s=sf(gs,0);FOLD(gs);FOLD(s);int i=b;long long l=a*100;FOLD(i);FOLD(l);__fp16 k=65519.0;__fp16 k2=-0.0;FOLD(k);FOLD(k2);"
        "return (int)h;}",
    },
    {
        // __bf16's arithmetic, which every target here computes in float (excess precision, without AVX10.2), and
        // rounds by VCVTNEPS2BF16 (flushing subnormals) or libgcc's __truncsfbf2.
        "bf16-arithmetic",
        "int tested(int n){unsigned h=2166136261u;"
        "__bf16 a=(__bf16)n/(__bf16)4,b=(__bf16)(n*n)-(__bf16)7,c=bb(specials[(unsigned)(n+3)%16u]),d=(__bf16)1000*(__bf16)n;"
        "__bf16 r[]={a+b,a-b,a*b,a/(b+(__bf16)0.5),a*b+d,d*d/d,(d*d-d*d)+a,-(a+b),+a,-c,+c,-(-c),a+b*d-c,c*a,(a+b)*(a-b)/(d+(__bf16)1)};FOLD(r);"
        "float f=a*2.5f;double g=b*2.5;__bf16 m=a*n+b;FOLD(f);FOLD(g);FOLD(m);"
        "int cmp=(a<b)+2*(a<=c)+4*(a==c)+8*(c!=c)+16*(a>b)+32*(a>=d)+64*(c<1.0f)+128*(a==n);FOLD(cmp);"
        "int t=!c+2*(c?1:0)+4*(a&&b)+8*(c||a);if(c)t+=16;FOLD(t);"
        "__bf16 u=a;u+=b*d;u-=c*(__bf16)0;u*=(__bf16)3;u/=b+(__bf16)9;u+=1;u-=0.5;FOLD(u);"
        "__bf16 i=d;i++;++i;i--;--i;__bf16 j=(__bf16)256;j++;__bf16 l=j--;FOLD(i);FOLD(j);FOLD(l);"
        "__bf16 s=n>3?a+b:a-b;__bf16 e=({__bf16 z=a*b;z+d;});FOLD(s);FOLD(e);"
        "__bf16 big=(__bf16)3e38;__bf16 o=big*(__bf16)(n+2)/(__bf16)(n+4);FOLD(o);"
        "_Float16 hm=a+(__bf16)1;__bf16 bm=(_Float16)n*(_Float16)0.5;FOLD(hm);FOLD(bm);"
        "return (int)h;}",
    },
    {
        // Unary chains over each format's special values: binary16's - and + are sign flips (signaling NaNs keep their
        // bits); bfloat16's odd count of - rounds a float's negation, and an even one is the value; and binary16's
        // chains of - in a function whose block has FENV_ACCESS, which clang's code makes strict whole, round trips
        // through float.
        "half-unary-chains",
        "static const unsigned short bspecials[]={0x0000,0x8000,0x7f80,0xff80,0x7fc0,0x7f81,0x0001,0x8001,0x3f80,0xbf80,0x7f7f,0x0080,0x007f,0x3eab,0x4049,0xc2c8};"
        "static __attribute__((noinline)) void strict_chains(_Float16 d,_Float16*out){out[0]=-(+d);out[1]=-d;out[2]=-(-d);{\n#pragma STDC FENV_ACCESS ON\n}}"
        "int tested(int n){unsigned h=2166136261u;"
        "__bf16 c=bb(bspecials[(unsigned)(n+3)%16u]);_Float16 d=hb(specials[(unsigned)(n+3)%16u]);"
        "__bf16 r[]={-c,+c,-(-c),-(+c),-(-(-c)),+(-c),c};_Float16 s[]={-d,+d,-(-d),-(+d),-(-(-d)),+(-d),d};FOLD(r);FOLD(s);"
        "_Float16 t[3];strict_chains(d,t);FOLD(t);"
        "return (int)h;}",
    },
};

// The features clang's -O0 code of the programs may take: a program runs where the host has the target's among them
// and its extensions, with their saved state (x86_target_native).
#define BUSTER_CHECK_TEST_HALF_FEATURES                                                                                     \
    (BUSTER_X86_FEATURE(SSE3) | BUSTER_X86_FEATURE(SSSE3) | BUSTER_X86_FEATURE(SSE41) | BUSTER_X86_FEATURE(SSE42) |         \
     BUSTER_X86_FEATURE(POPCNT) | BUSTER_X86_FEATURE(AVX) | BUSTER_X86_FEATURE(AVX2) | BUSTER_X86_FEATURE(BMI) |            \
     BUSTER_X86_FEATURE(BMI2) | BUSTER_X86_FEATURE(F16C) | BUSTER_X86_FEATURE(FMA) | BUSTER_X86_FEATURE(LZCNT) |            \
     BUSTER_X86_FEATURE(MOVBE) | BUSTER_X86_FEATURE(AVX512F) | BUSTER_X86_FEATURE(AVX512BW) | BUSTER_X86_FEATURE(AVX512CD) | \
     BUSTER_X86_FEATURE(AVX512DQ) | BUSTER_X86_FEATURE(AVX512VL) | BUSTER_X86_FEATURE(AVX512BF16))

// What the lowering refuses by name: __bf16's arithmetic where the unit's target has AVX10.2's (which rounds each
// operation in bfloat16, and Tier 0 doesn't emit), complex _Float16, and a conversion to __bf16 where AVX512-BF16 has
// no VL to round one float with.
static void check_test_program_half_refusals(CheckTest* test)
{
    X86Target avx10 = { 0 };
    X86Target without_vl = { 0 };
    const bool parsed = x86_target_parse("x86-64-v4", strlen("x86-64-v4"), &avx10) && x86_target_parse("x86-64-v4", strlen("x86-64-v4"), &without_vl);
    check_test_expect(test, parsed, "the half-precision refusals' targets");
    if (parsed)
    {
        avx10.extensions |= BUSTER_X86_EXTENSION(AVX10_2);
        without_vl.features = (without_vl.features & ~BUSTER_X86_FEATURE(AVX512VL)) | BUSTER_X86_FEATURE(AVX512BF16);
        check_test_program_boundary_target(test, "int tested(int n){__bf16 a=(__bf16)n,b=a+(__bf16)1;return (int)b;}", "bf16-native-arithmetic", true, false,
                                           0, 2, "__bf16 arithmetic where the unit's target has AVX10.2", &avx10);
        check_test_program_boundary_target(test, "int tested(int n){_Complex _Float16 z=(_Float16)n;return (int)__real__ z;}", "complex-float16", true, false, 0, 1,
                                           "complex types other than _Complex float, double and long double", &avx10);
        check_test_program_boundary_target(test, "int tested(int n){__bf16 b=(float)n;return b==1;}", "bf16-without-vl", true, false, 0, 1,
                                           "conversions to __bf16 with AVX512-BF16 but not AVX-512VL", &without_vl);
    }
}

static __attribute__((noinline)) void check_test_program_halves(CheckTest* test, const char* clang)
{
    static const char* const marches[] = { "-march=x86-64", "-march=x86-64-v3", "-march=x86-64-v4", "-march=znver5", "-march=sapphirerapids" };
    const X86Target native = x86_target_native();
    for (uint32_t t = 0; t < BUSTER_ARRAY_LENGTH(marches); t += 1)
    {
        // The unit's target as the driver resolves its -march (predefines_target), AVX512-FP16's extension among it.
        X86Target target = { 0 };
        X86Architecture tune = X86_ARCH_BASELINE;
        const PredefinesOptions options = { .march = marches[t] + strlen("-march=") };
        const bool resolved = predefines_target(&options, &target, &tune);
        check_test_expect(test, resolved, "a half-precision program's target");
        if (resolved)
        {
            const uint64_t features = target.features & BUSTER_CHECK_TEST_HALF_FEATURES;
            const bool run_native = (native.features & features) == features && (native.extensions & target.extensions) == target.extensions;
            if (!run_native)
            {
                printf("  half-precision values for %s: native execution skipped (the host lacks the target's features or their saved state); the programs "
                       "compile and link\n",
                       marches[t] + strlen("-march="));
            }
            check_test_program_build_release();
            check_test_program_march = marches[t];
            for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(check_test_half_programs); i += 1)
            {
                const size_t length = strlen(check_test_half_helpers) + strlen(check_test_half_programs[i][1]) + 1;
                char* source = malloc(length);
                check_test_expect(test, source != 0, "a half-precision program's source");
                if (source != 0)
                {
                    snprintf(source, length, "%s%s", check_test_half_helpers, check_test_half_programs[i][1]);
                    check_test_program_source_native_limits(test, source, check_test_half_programs[i][0], clang, false, -1, 0, &target, run_native);
                    free(source);
                }
            }
            check_test_program_build_release();
            check_test_program_march = "-march=x86-64";
        }
    }
    check_test_program_half_refusals(test);
}
