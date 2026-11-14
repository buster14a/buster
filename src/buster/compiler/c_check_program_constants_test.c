// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// clang's constant floating builtins past huge_val, inf and nan of float, double and long double (#148): the signaling
// NaNs nans, nansf and nansl, and the _Float16 and __float128 forms of nan, nans, inf and huge_val. Source fixtures
// checked by both paths against clang, and programs run natively against clang -O0's build: each builtin's bits in static
// data, in a function's code and in an aggregate's initializer, with payloads, signs and the conversions that quiet a
// signaling NaN, and the NaN payloads of nan, nanf and nanl in static data, which main wrote as the canonical NaN.
// Included by c_checker_test.c.

static __attribute__((noinline)) void check_test_float_constant_sources(CheckTest* test, const char* clang)
{
    // Its source fixtures are runtime data (#2053): tests/checker/float-constants, which check_test_suite reads and runs.
    check_test_suite(test, clang, "checker/float-constants", "the constant floating builtins' Clang oracle ran");
}

// An FNV-1a hash of objects' bytes, and the values whose bytes the programs hash: each builtin's in static data, in an
// aggregate's initializer and in code, with payloads and signs, and conversions of them (which quiet a signaling NaN, as
// clang's APFloat converts one), for the n given.
#define BUSTER_CHECK_TEST_FLOAT_CONSTANTS_HASH \
    "static unsigned hash(unsigned h,const void*p,unsigned n){const unsigned char*b=p;for(unsigned i=0;i<n;i++)h=(h^b[i])*16777619u;return h;}"

static const struct
{
    const char* name;
    const char* source;
} check_test_float_constant_programs[] =
{
    { "float-constants-in-static-data",
      BUSTER_CHECK_TEST_FLOAT_CONSTANTS_HASH
      "static double d[]={__builtin_nans(\"\"),__builtin_nans(\"5\"),-__builtin_nans(\"3\"),(double)__builtin_nansf(\"\"),"
      "(double)__builtin_nansf(\"7\"),(double)__builtin_nansl(\"0x1000\"),(double)__builtin_nanf128(\"0x800\"),"
      "(double)__builtin_nansf128(\"\"),(double)__builtin_inff128(),(double)__builtin_nanf16(\"3\"),(double)__builtin_nansf16(\"\"),"
      "__builtin_nan(\"0x7ffffffffffff\"),__builtin_nan(\"5\"),-__builtin_nan(\"9\")};"
      "static float f[]={__builtin_nansf(\"\"),__builtin_nansf(\"0x3fffff\"),(float)__builtin_nans(\"\"),(float)__builtin_nan(\"0x8000000000\"),"
      "__builtin_nanf(\"3\"),-__builtin_nansf(\"1\")};"
      "static long double l[]={__builtin_nansl(\"\"),__builtin_nansl(\"9\"),__builtin_nanl(\"0x3fffffffffffffff\"),(long double)__builtin_nans(\"1\"),"
      "__builtin_nanl(\"7\"),-__builtin_nansl(\"5\")};"
      "static _Float16 h[]={__builtin_nansf16(\"\"),__builtin_nanf16(\"0x1ff\"),__builtin_inff16(),__builtin_huge_valf16(),"
      "(_Float16)__builtin_nans(\"\"),-__builtin_nansf16(\"\"),__builtin_nanf16(\"1\")};"
      "static __float128 q[]={__builtin_nansf128(\"\"),__builtin_nanf128(\"9\"),__builtin_inff128(),__builtin_huge_valf128(),"
      "__builtin_nansf128(\"0x1fffffffffffffff\"),-__builtin_inff128(),-__builtin_nansf128(\"3\"),+__builtin_nanf128(\"\")};"
      "static struct{double d;__float128 q;_Float16 h;float f;}s={__builtin_nans(\"4\"),__builtin_nanf128(\"2\"),__builtin_nansf16(\"5\"),"
      "__builtin_nansf(\"6\")};"
      "int tested(int n){unsigned x=2166136261u;x=hash(x,d,sizeof d);x=hash(x,f,sizeof f);x=hash(x,h,sizeof h);x=hash(x,q,sizeof q);"
      "for(unsigned i=0;i<sizeof l/sizeof*l;i++)x=hash(x,&l[i],10);x=hash(x,&s.d,8);x=hash(x,&s.q,16);x=hash(x,&s.h,2);x=hash(x,&s.f,4);"
      "return (int)(x^(unsigned)n);}" },
    { "float-constants-quad-initializers",
      BUSTER_CHECK_TEST_FLOAT_CONSTANTS_HASH
      "static __float128 q[]={0,1.5,-1.5,1.5q,1.5Q,0.1,0.1f,1.5L,18446744073709551615ULL,-9223372036854775807LL,(__int128)1<<100};"
      "int tested(int n){unsigned x=2166136261u;x=hash(x,q,sizeof q);return (int)(x^(unsigned)n);}" },
    // __float128 constants of their binary128 bits (#1250): literals rounded once, conversions to and from every type,
    // a NaN's whole payload, comparisons, classification, and fabsf128, copysignf128, fmaxf128 and fminf128, in static
    // data and in code; and NaNs narrowed then widened, whose payload the narrow format truncates.
    { "float-constants-quad-bits",
      BUSTER_CHECK_TEST_FLOAT_CONSTANTS_HASH
      "static __float128 q[]={0.1q,0x1.0000000000000000000000000001p0q,1e4000q,0x1p-16494q,1.18973149535723176508575932662800702e4932q,-0.0q,"
      "0.3333333333333333333333333333333333333333q,1e-4940q,0.1L,0.1,0.1f,(__int128)0x7fffffffffffffff*3,(unsigned __int128)-1,"
      "(_Float16)0.1,(__int128)0x7fffffffffffffff*0x7fffffffffffffff,__builtin_nanf128(\"0x123456789abcdef0123\"),"
      "-__builtin_nansf128(\"0x123456789abcdef0123\"),__builtin_nansf128(\"1\"),__builtin_nanf128(\"0xffffffffffffffffffffffffffffffffffff\"),"
      "(__float128)__builtin_nans(\"0x5\"),(__float128)__builtin_nanl(\"0x123\"),(__float128)__builtin_nanf(\"0x7\"),"
      "(__float128)(_Float16)__builtin_nanf(\"0x7\"),(__float128)(double)__builtin_nanl(\"0x7\"),__builtin_fabsf128(-0.1q),"
      "__builtin_copysignf128(0.1q,-__builtin_nanf128(\"\")),__builtin_fmaxf128(0.1q,0.2q),__builtin_fminf128(-0.0q,0.0q),"
      "__builtin_fmaxf128(__builtin_nanf128(\"\"),0.3q),-0.1q,+0.7q};"
      "static double d[]={0.1q,0x1.00000000000008000000000001p0q,1e-320q,(double)__builtin_nanf128(\"0x123456789abcdef0123\"),"
      "(double)(_Float16)__builtin_nanf(\"0x7\"),(double)(float)__builtin_nan(\"0x7\")};"
      "static float f[]={0.1q,(float)__builtin_nansf128(\"1\"),1e300q};"
      "static long double l[]={0.1q,0x1.00000000000000008p0q,0x1.00000000000000018p0q,(long double)__builtin_nansf128(\"0x123456789abcdef0123\"),"
      "(long double)__builtin_nanf128(\"0x1ffffffffffff\"),(long double)(float)__builtin_nan(\"0x7\")};"
      "static _Float16 h[]={0.1q,(_Float16)__builtin_nanf128(\"0x123456789abcdef0123\")};"
      "static __bf16 b[]={0.1q};"
      "static unsigned __int128 u[]={1e30q,-1.7e38q,2.75q,-9.2e18q,(_Bool)0.1q,(_Bool)0x1p-16494q,(_Bool)-0.0q,1e40q,-1e40q};"
      "static int k[]={0.1q<0.2q,0.1q==0.1,0.1q!=0.1L,__builtin_nanf128(\"\")!=__builtin_nanf128(\"\"),0x1p-16494q>0,0.1q?3:4,!0x1p-16494q,"
      "__builtin_isless(0.1q,0.10000000000000000000000000000000001q),0.3q>=0.3q,(__float128)0.1f==0.1f,__builtin_isunordered(0.1q,__builtin_nanf128(\"\")),"
      "__builtin_fpclassify(0,1,2,3,4,0x1p-16494q),__builtin_isnormal(0x1p-16382q),__builtin_issignaling(__builtin_nansf128(\"0x123456789abcdef0123\")),"
      "__builtin_signbit(-0.0q),__builtin_isfpclass(0x1p-16494q,0x0080),__builtin_isinf(1e5000q),(int)0.1q};"
      "__float128 g(int n){switch(n&3){case 0:return 0.1q;case 1:return -0x1p-16494q;case 2:return __builtin_nansf128(\"0x123456789abcdef0123\");"
      "default:return (__float128)(unsigned __int128)-1;}}"
      // A _Complex __float128 constant's parts are its bits: two that a long double can't tell apart compare unequal, and
      // a part folds with its bits.
      "static int z[]={(_Complex __float128)1.5q==(_Complex __float128)1.25q,(_Complex __float128)1.5q!=0};"
      "int cz(void){return (_Complex __float128)0x1.0000000000000000000000001p0q==(_Complex __float128)0x1.00000000000000000000000008p0q;}"
      "__float128 cr(int n){return n?__real__(_Complex __float128)0.1q:__real__(_Complex __float128)1.5q+__imag__ 1.5qi;}"
      "int tested(int n){unsigned x=2166136261u;x=hash(x,q,sizeof q);x=hash(x,d,sizeof d);x=hash(x,f,sizeof f);x=hash(x,h,sizeof h);"
      "x=hash(x,b,sizeof b);x=hash(x,u,sizeof u);x=hash(x,k,sizeof k);for(unsigned i=0;i<sizeof l/sizeof*l;i++)x=hash(x,&l[i],10);"
      "__float128 r=g(n);x=hash(x,&r,16);__float128 c=0.7q;x=hash(x,&c,16);double e=0.1q;x=hash(x,&e,8);"
      "x=hash(x,z,sizeof z);x^=(unsigned)cz();__float128 w=cr(n&1);x=hash(x,&w,16);"
      "return (int)(x^(unsigned)n);}" },
    // __float128 narrowed where rounding to odd at 64 bits must still round once (#1250, review on #1889): below, at
    // and above the midpoints of zero and the least subnormal, of the greatest subnormal and the least normal, and of
    // the greatest finite value and the infinity, of double, float, _Float16 and long double, with their negatives; and
    // integer conversions at each type's range boundary, the out-of-range ones saturated as clang folds them.
    { "float-constants-quad-boundaries",
      BUSTER_CHECK_TEST_FLOAT_CONSTANTS_HASH
      "static double bd[]={0x1p-1075q,0x1.0000000000000000000000000001p-1075q,0x1.ffffffffffffffffffffffffffffp-1076q,0"
      "x1.8p-1074q,0x1.4p-1074q,0x1.cp-1074q,0x1.ffffffffffffep-1023q,0x1.fffffffffffff8p-1023q,0x1.fffffffffffff7fffff"
      "fffffffffp-1023q,0x1.fffffffffffff80000000000001p-1023q,0x1.fffffffffffffp-1023q,-0x1p-1075q,-0x1.00000000000000"
      "00000000000001p-1075q,-0x1.ffffffffffffffffffffffffffffp-1076q,-0x1.fffffffffffff8p-1023q,-0x1.fffffffffffff7fff"
      "fffffffffffp-1023q,0x1.fffffffffffff8p1023q,0x1.fffffffffffff7ffffffffffffffp1023q,0x1.00000000000008p0q,0x1.000"
      "00000000018p0q,0x1.000000000000080000000000001p0q};static float bf[]={0x1p-150q,0x1.0000000000000000000000000001"
      "p-150q,0x1.ffffffffffffffffffffffffffffp-151q,0x1.fffffcp-127q,0x1.fffffdp-127q,0x1.fffffbffffffffffffffffffffff"
      "p-127q,-0x1p-150q,-0x1.0000000000000000000000000001p-150q,-0x1.fffffcp-127q,0x1.ffffffp127q,0x1.fffffeffffffffff"
      "ffffffffffffp127q,0x1.000001p0q,0x1.000003p0q};static _Float16 bh[]={0x1p-25q,0x1.0000000000000000000000000001p-"
      "25q,0x1.ffffffffffffffffffffffffffffp-26q,0x1.ff8p-15q,-0x1p-25q,0x1.ffcp15q,0x1.ffbffffffffffffffffffffffffp15q"
      "};static long double bl[]={0x1p-16446q,0x1.0000000000000000000000000001p-16446q,0x1.ffffffffffffffffffffffffffff"
      "p-16447q,0x1.fffffffffffffffep-16383q,0x1.ffffffffffffffffp-16383q,-0x1p-16446q,0x1.00000000000000008p0q,0x1.000"
      "00000000000018p0q,0x1.ffffffffffffffffp16383q};static __int128 bi[]={(__int128)-0x1p127q,(__int128)0x1p127q,(__i"
      "nt128)0x1.ffffffffffffffffffffffffffffp126q,(__int128)-0x1.ffffffffffffffffffffffffffffp126q,(__int128)-0x1.0000"
      "000000000000000000000001p127q};static unsigned __int128 bu[]={(unsigned __int128)0x1.fffffffffffffffffffffffffff"
      "fp127q,(unsigned __int128)0x1p128q,(unsigned __int128)-0.99q,(unsigned __int128)-1.0q};static long long bll[]={("
      "long long)-0x1p63q,(long long)0x1p63q,(long long)-0x1.0000000000000000000000000001p63q,(long long)-0x1.000000000"
      "00000020000000000p63q,(long long)0x1.fffffffffffffffep62q};static unsigned bui[]={(unsigned)0x1.fffffffep31q,(un"
      "signed)0x1p32q,(unsigned)4294967295.999q};int tested(int n){unsigned x=2166136261u;x=hash(x,bd,sizeof bd);x=hash"
      "(x,bf,sizeof bf);x=hash(x,bh,sizeof bh);for(unsigned i=0;i<sizeof bl/sizeof*bl;i++)x=hash(x,&bl[i],10);x=hash(x,"
      "bi,sizeof bi);x=hash(x,bu,sizeof bu);x=hash(x,bll,sizeof bll);x=hash(x,bui,sizeof bui);return (int)(x^(unsigned)"
      "n);}" },
    // __float128 arithmetic folded in binary128, rounded once (#1250): ties, cancellation, signed zeros, overflow to an
    // infinity, gradual underflow, APFloat's NaNs (an invalid operation's positive canonical NaN, else the first NaN
    // operand quieted), mixed operands converted first, in static data, in code and in an integer constant expression.
    { "float-constants-quad-arithmetic",
      BUSTER_CHECK_TEST_FLOAT_CONSTANTS_HASH
      "static __float128 q[]={0.1q+0.2q,0.3q-0.1q,0.1q*3,1.0q/3,2.0q/3,1.0q+0x1p-113q,1.0q+0x1.0000000000000000000000000001p-113q,"
      "1.0q+0x1p-112q+0x1p-113q,(1.0q+0x1p-112q)-1.0q,1e4932q*10,-1e4932q*10,0x1p-16494q/2,0x1.8p-16494q/2,0x1p-16382q*0x1p-100q,"
      "0.0q/0.0q,1.0q/0.0q,-1.0q/0.0q,1.0q/-0.0q,__builtin_inff128()-__builtin_inff128(),0.0q*__builtin_inff128(),"
      "__builtin_inff128()/__builtin_inff128(),1.0q-(-__builtin_nanf128(\"5\")),-__builtin_nanf128(\"1\")+__builtin_nanf128(\"2\"),"
      "__builtin_nansf128(\"4\")*-__builtin_nanf128(\"2\"),__builtin_nanf128(\"2\")/__builtin_nansf128(\"4\"),0.1q-0.1q,-0.1q+0.1q,"
      "-0.0q-0.0q,-0.0q+-0.0q,(__float128)0.1*0.1,0.1q*0.1+0.1f,7/2.0q,((__int128)1<<100)*1.0q,1e100q*1e100q/3e-50q,"
      "1.18973149535723176508575932662800702e4932q+1e4900q,0x1.ffffffffffffffffffffffffffffp0q*0x1.ffffffffffffffffffffffffffffp0q,"
      "0x1.0000000000000000000000000001p0q*0x1.0000000000000000000000000001p0q,1.0q/0x1.0000000000000000000000000001p0q};"
      "static double d[]={0.1q+0.2q,(0.1q+0.2q)*1e300q};static int k[]={(int)(2.5q*2),1.0q/3*3==1.0q,0.1q+0.2q!=0.3q,0.5q+0.25q==0.75q};"
      "int a[(int)(2.5q*2)+1];"
      "__float128 g(int n){switch(n&3){case 0:return 0.1q+0.2q;case 1:return 1.0q/3;case 2:return 0.0q/0.0q;default:return -0x1p-16494q/4;}}"
      "int tested(int n){unsigned x=2166136261u;x=hash(x,q,sizeof q);x=hash(x,d,sizeof d);x=hash(x,k,sizeof k);"
      "__float128 r=g(n);x=hash(x,&r,16);__float128 c=1e100q*1e100q/3e-50q;x=hash(x,&c,16);"
      "return (int)(x^(unsigned)n^(unsigned)sizeof a);}" },
    // Under FENV_ACCESS a __float128 operation is constant where it is exact, as clang's evaluator decides it (#1250).
    { "float-constants-quad-fenv",
      "#pragma STDC FENV_ACCESS ON\n"
      "int tested(int n){return n^(__builtin_constant_p(0.5q+0.25q)*1+__builtin_constant_p(0.1q+0.2q)*2+__builtin_constant_p(3.0q*0.5q)*4+"
      "__builtin_constant_p(1.0q/3)*8+__builtin_constant_p(1.0q/4)*16+"
      "__builtin_constant_p(0x1.0000000000000000000000000001p0q*0x1.0000000000000000000000000001p0q)*32+"
      "__builtin_constant_p(0x1.8p0q*0x1.8p0q)*64+__builtin_constant_p(1e4932q*10)*128);}" },
    // _Complex __float128 constants of their parts' bits (#1250): __builtin_complex, conversions from and to real and complex
    // types, imaginary q literals, + - * / with real and complex operands, Annex G's recoveries as clang's evaluator makes them in
    // static data and as clang -O0's code computes them (its complex / and two-NaN * by __divtc3 and __multc3), == and truth.
    { "float-constants-complex-quad",
      BUSTER_CHECK_TEST_FLOAT_CONSTANTS_HASH
      "static _Complex __float128 s[]={__builtin_complex(1.0q,2.0q),1.0q,0.1q,0.1qi,__builtin_complex(0.1q,-0.0q)+__builtin"
      "_complex(0.2q,0.3q),__builtin_complex(0.1q,0.2q)-0.1q,0.1q-__builtin_complex(0.1q,0.2q),-__builtin_complex(0.1q,__bu"
      "iltin_nansf128(\"0x123\")),~__builtin_complex(0.1q,0.2q),(_Complex double)__builtin_complex(0.1q,0.2q),(_Complex dou"
      "ble)0.1+0.1qi,3,__builtin_complex(0.1q,0.2q)*__builtin_complex(0.3q,0.4q),__builtin_complex(0.1q,0.2q)/__builtin_com"
      "plex(0.3q,0.4q),__builtin_complex(0.1q,0.2q)*3,__builtin_complex(0.1q,0.2q)/3,3*__builtin_complex(0.1q,0.2q),3/__bui"
      "ltin_complex(0.1q,0.2q),__builtin_complex(1e4000q,1e-4000q)/__builtin_complex(3e4000q,7e3999q),__builtin_complex(1.0"
      "q,1.0q)/__builtin_complex(0x1p-16490q,0x1p-16494q),__builtin_complex(-0.0q,0.0q)*__builtin_complex(0.0q,-0.0q),__bui"
      "ltin_complex(1.0q,2.0q)/__builtin_complex(4.0q,0.0q),__builtin_complex(__builtin_inff128(),0.0q)*__builtin_complex(1"
      ".0q,0.0q),__builtin_complex(1.0q,2.0q)/__builtin_complex(0.0q,0.0q),__builtin_complex(__builtin_nanf128(\"1\"),1.0q)"
      "*__builtin_complex(2.0q,3.0q),__builtin_complex(-__builtin_inff128(),1.0q)*__builtin_complex(__builtin_nanf128(\"5\""
      "),0x1p-16494q),__builtin_complex(1e4932q,1e4932q)/__builtin_complex(0x1p16383q,0x1p16383q),__builtin_complex(1.0q,1."
      "0q)/__builtin_complex(__builtin_inff128(),2.0q),__builtin_complex(-__builtin_inff128(),1e4932q)*-0.0q};static int k["
      "]={__builtin_complex(0.1q,0.2q)==__builtin_complex(0.1q,0.2q),__builtin_complex(0.1q,0.2q)!=0.1q,(_Bool)__builtin_co"
      "mplex(0.0q,0x1p-16494q),(_Bool)__builtin_complex(-0.0q,-0.0q),__builtin_complex(0.0q,__builtin_nanf128(\"\"))==__bui"
      "ltin_complex(0.0q,__builtin_nanf128(\"\")),__builtin_complex(-0.0q,0.0q)==0,(int)__imag__ __builtin_complex(1.5q,3.5"
      "q)};static __float128 r[]={__real__ __builtin_complex(0.1q,0.2q),__imag__ (__builtin_complex(0.1q,0.2q)+1),__imag__ "
      "0.1qi,__real__ 0.1qi};_Complex __float128 g(int n){switch(n&15){case 0:return __builtin_complex(0.1q,0.2q)*__builtin"
      "_complex(0.3q,0.4q);case 1:return __builtin_complex(__builtin_inff128(),0.0q)*__builtin_complex(1.0q,0.0q);case 2:re"
      "turn __builtin_complex(1.0q,2.0q)/__builtin_complex(0.0q,0.0q);case 3:return __builtin_complex(__builtin_nanf128(\"1"
      "\"),1.0q)/__builtin_complex(2.0q,3.0q);case 4:return 0.1qi;case 5:return 3/__builtin_complex(0.1q,0.2q);case 6:retur"
      "n __builtin_complex(1e4932q,1e4932q)/__builtin_complex(0x1p16383q,0x1p16383q);case 7:return __builtin_complex(-__bui"
      "ltin_inff128(),1.0q)*__builtin_complex(__builtin_nanf128(\"5\"),0x1p-16494q);default:return __builtin_complex(-__bui"
      "ltin_inff128(),1e4932q)*-0.0q;}}int tested(int n){unsigned x=2166136261u;x=hash(x,s,sizeof s);x=hash(x,k,sizeof k);x"
      "=hash(x,r,sizeof r);_Complex __float128 v=g(n);x=hash(x,&v,32);_Complex __float128 w=__builtin_complex(0.1q,0.2q);x="
      "hash(x,&w,32);__float128 t=__imag__ w;x=hash(x,&t,16);return (int)(x^(unsigned)n);}" },
    // A narrow NaN converted at run time. Runtime conversion must still
    // quiet signaling inputs and put their payloads in the target format's high fraction bits.
    { "float-constants-narrow-nan-to-quad-runtime",
      BUSTER_CHECK_TEST_FLOAT_CONSTANTS_HASH
      "__float128 f(int n){switch(n&3){case 0:return (__float128)__builtin_nans(\"\");"
      "case 1:return (__float128)__builtin_nan(\"1\");case 2:return (__float128)-__builtin_nansf(\"\");"
      "default:return (__float128)__builtin_nanl(\"0x1234\");}}"
      "int tested(int n){__float128 q=f(n);return (int)hash(2166136261u,&q,sizeof q);}" },
    { "float-constants-in-code",
      BUSTER_CHECK_TEST_FLOAT_CONSTANTS_HASH
      "int tested(int n){unsigned x=2166136261u;int k=n&3;"
      "double d=k==0?__builtin_nans(\"6\"):k==1?__builtin_nans(\"\"):k==2?-__builtin_nans(\"0x7ffffffffffff\"):__builtin_nan(\"5\");"
      "float f=k==0?__builtin_nansf(\"\"):k==1?__builtin_nansf(\"9\"):k==2?(float)__builtin_nans(\"6\"):-__builtin_nansf(\"2\");"
      "long double l=k==0?__builtin_nansl(\"\"):k==1?__builtin_nansl(\"0x1234\"):k==2?__builtin_nanl(\"7\"):-__builtin_nansl(\"3\");"
      "_Float16 h=k==0?__builtin_nansf16(\"\"):k==1?__builtin_nanf16(\"1\"):k==2?__builtin_inff16():-__builtin_huge_valf16();"
      "__float128 q=k==0?__builtin_nansf128(\"4\"):k==1?__builtin_nanf128(\"\"):k==2?__builtin_inff128():-__builtin_nansf128(\"\");"
      "x=hash(x,&d,8);x=hash(x,&f,4);x=hash(x,&l,10);x=hash(x,&h,2);x=hash(x,&q,16);"
      "int c=__builtin_issignaling(__builtin_nans(\"\"))+2*__builtin_isnan(__builtin_nanf16(\"\"))+4*__builtin_isinf(__builtin_inff128())+"
      "8*__builtin_issignaling(__builtin_nansf128(\"1\"))+16*__builtin_issignaling((double)__builtin_nansf(\"\"));"
      "return (int)(x^(unsigned)c);}" },
    // Folded invalid operations and NaN operands of float, double and long double (#1473), in static data and in code:
    // clang's APFloat gives an invalid operation the positive canonical NaN and keeps the first NaN operand, quieted,
    // where the host's SSE and x87 give their negative NaN and may take either operand.
    { "float-constants-invalid-arithmetic",
      BUSTER_CHECK_TEST_FLOAT_CONSTANTS_HASH
      "static double d[]={__builtin_inf()+-__builtin_inf(),__builtin_inf()-__builtin_inf(),0.0*__builtin_inf(),0.0/0.0,"
      "-__builtin_inf()/__builtin_inf(),-(__builtin_inf()-__builtin_inf()),1.0-(-__builtin_nan(\"5\")),-__builtin_nan(\"1\")+__builtin_nan(\"2\"),"
      "__builtin_nans(\"4\")*-__builtin_nan(\"2\"),__builtin_nan(\"2\")/__builtin_nans(\"4\")};"
      "static float f[]={__builtin_inff()+-__builtin_inff(),0.0f*-__builtin_inff(),0.0f/0.0f,2.0f*-__builtin_nanf(\"7\"),"
      "__builtin_nanf(\"3\")-__builtin_nansf(\"1\")};"
      "static long double l[]={__builtin_infl()+-__builtin_infl(),0.0L*__builtin_infl(),0.0L/0.0L,1.0L-(-__builtin_nanl(\"9\")),"
      "-__builtin_nanl(\"1\")+__builtin_nanl(\"2\")};"
      "int tested(int n){unsigned x=2166136261u;"
      "double e=((double)__builtin_inff()/-1.0)+(0.5- -(double)__builtin_inff());"
      "double c[]={__builtin_inf()+-__builtin_inf(),0.0/0.0,1.0+-__builtin_nan(\"6\"),__builtin_nan(\"1\")*-__builtin_nan(\"2\")};"
      "float g=__builtin_inff()*0.0f;long double m=__builtin_infl()-__builtin_infl();"
      "x=hash(x,d,sizeof d);x=hash(x,f,sizeof f);for(unsigned i=0;i<sizeof l/sizeof*l;i++)x=hash(x,&l[i],10);"
      "x=hash(x,&e,8);x=hash(x,c,sizeof c);x=hash(x,&g,4);x=hash(x,&m,10);"
      "int s=__builtin_signbit(e)+2*__builtin_signbit(g)+4*__builtin_signbit(m);"
      "return (int)(x^(unsigned)n^(unsigned)s);}" },
};

static __attribute__((noinline)) void check_test_float_constant_runs(CheckTest* test, const char* clang)
{
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(check_test_float_constant_programs); i += 1)
    {
        check_test_program_source(test, check_test_float_constant_programs[i].source, check_test_float_constant_programs[i].name, clang);
    }
}

static __attribute__((noinline)) void check_test_float_constants(CheckTest* test, const char* clang)
{
    check_test_float_constant_sources(test, clang);
    check_test_float_constant_runs(test, clang);
}
