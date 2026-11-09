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
    static const CheckSourceFixture fixtures[] =
    {
        { .name = "float constants: folds in constant expressions",
          .source = "_Static_assert(__builtin_issignaling(__builtin_nans(\"\")) && __builtin_issignaling(__builtin_nansf(\"1\")) &&\n"
                    "__builtin_issignaling(__builtin_nansl(\"0x10\")), \"\");\n"
                    "_Static_assert(__builtin_isnan(__builtin_nanf16(\"\")) && __builtin_issignaling(__builtin_nansf16(\"\")) &&\n"
                    "!__builtin_issignaling(__builtin_nanf16(\"3\")) && __builtin_isinf(__builtin_inff16()) && __builtin_isinf(__builtin_huge_valf16()), \"\");\n"
                    "_Static_assert(__builtin_isnan(__builtin_nanf128(\"9\")) && __builtin_issignaling(__builtin_nansf128(\"\")) &&\n"
                    "__builtin_isinf(__builtin_inff128()) && __builtin_isinf(-__builtin_huge_valf128()) && __builtin_signbit(-__builtin_nansf128(\"\")), \"\");\n"
                    "_Static_assert(!__builtin_issignaling((double)__builtin_nansf(\"\")) && !__builtin_issignaling(-(float)__builtin_nans(\"\")) &&\n"
                    "__builtin_issignaling(-__builtin_nans(\"2\")) && __builtin_fpclassify(0, 1, 2, 3, 4, __builtin_nansf16(\"\")) == 0, \"\");\n"
                    "int a[__builtin_isinf(__builtin_inff16()) + __builtin_isfpclass(__builtin_nansf128(\"\"), 1) + 1];\n" },
        { .name = "float constants: result types",
          .source = "_Static_assert(_Generic(__builtin_nans(\"\"), double: 1, default: 0) && _Generic(__builtin_nansf(\"\"), float: 1, default: 0) &&\n"
                    "_Generic(__builtin_nansl(\"\"), long double: 1, default: 0), \"\");\n"
                    "_Static_assert(_Generic(__builtin_nanf16(\"\"), _Float16: 1, default: 0) && _Generic(__builtin_nansf16(\"\"), _Float16: 1, default: 0) &&\n"
                    "_Generic(__builtin_inff16(), _Float16: 1, default: 0) && _Generic(__builtin_huge_valf16(), _Float16: 1, default: 0), \"\");\n"
                    "_Static_assert(_Generic(__builtin_nanf128(\"\"), __float128: 1, default: 0) && _Generic(__builtin_nansf128(\"\"), __float128: 1, default: 0) &&\n"
                    "_Generic(__builtin_inff128(), __float128: 1, default: 0) && _Generic(__builtin_huge_valf128(), __float128: 1, default: 0), \"\");\n" },
        { .name = "float constants: static initializers",
          .source = "static const __float128 q[] = { __builtin_inff128(), -__builtin_nanf128(\"5\"), __builtin_nansf128(\"\") };\n"
                    "static _Float16 h = __builtin_nansf16(\"1\");\nstruct S { double d; __float128 q; _Float16 h; } s = { __builtin_nans(\"\"), "
                    "__builtin_huge_valf128(), __builtin_inff16() };\nconst __float128* p = q;\n" },
        { .name = "float constants: exact __float128 initializers",
          .source = "static __float128 a = 0;\nstatic __float128 b = 1.5;\nstatic __float128 c = 1.5q;\n"
                    "static __float128 d = 0.1;\nstatic __float128 e = 1.5L;\nstatic __float128 f = 18446744073709551615ULL;\n"
                    "static __float128 g = (__int128)1 << 100;\n"
                    "int tested(void){return (int)(a+b+c+d+e+(f!=0)+(g!=0));}\n" },
        { .name = "float constants: an integer's payload", .source = "double f(void)\n{\nreturn __builtin_nans(1);\n}\n", .rejected = true },
        { .name = "float constants: nansf16 without its payload", .source = "_Float16 f(void)\n{\nreturn __builtin_nansf16();\n}\n",
          .rejected = true },
        { .name = "float constants: inff128 of an argument", .source = "__float128 f(void)\n{\nreturn __builtin_inff128(1);\n}\n", .rejected = true },
        { .name = "float constants: nanf128 of two payloads", .source = "__float128 f(void)\n{\nreturn __builtin_nanf128(\"\", \"\");\n}\n",
          .rejected = true },
    };
    enum { FILE_COUNT = BUSTER_ARRAY_LENGTH(fixtures) };
    char directory[] = "/tmp/buster-checker-float-constants-XXXXXX";
    const bool made = mkdtemp(directory) != 0;
    bool ready = made;
    char paths[FILE_COUNT][160];
    char* pointers[FILE_COUNT];
    ParseTestClang outcomes[FILE_COUNT];
    for (uint32_t i = 0; ready && i < FILE_COUNT; i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        pointers[i] = paths[i];
        ready = parse_test_write(paths[i], fixtures[i].source, strlen(fixtures[i].source));
    }
    const char* arguments[] = { "-std=gnu17" };
    ready = ready && parse_test_clang(clang, arguments, BUSTER_ARRAY_LENGTH(arguments), pointers, FILE_COUNT, outcomes);
    check_test_expect(test, ready, "the constant floating builtins' Clang oracle ran");
    for (uint32_t i = 0; ready && i < FILE_COUNT; i += 1)
    {
        char description[192];
        snprintf(description, sizeof(description), "%s: expectation differs from Clang", fixtures[i].name);
        check_test_expect(test, outcomes[i].rejected == fixtures[i].rejected, description);
        check_test_source(test, &fixtures[i], paths[i], &outcomes[i]);
    }
    for (uint32_t i = 0; made && i < FILE_COUNT; i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        unlink(paths[i]);
    }
    if (made)
    {
        rmdir(directory);
    }
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
    // __float128 constants of their binary128 bits (#1250): literals rounded once, conversions to and from every type, a
    // NaN's whole payload, comparisons, classification, and fabsf128, copysignf128, fmaxf128 and fminf128, in static data and
    // in code; and NaNs narrowed then widened, whose payload the narrow format truncates.
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
      // A _Complex __float128 constant's parts stay exact long doubles until complex constants keep bits: a part that isn't
      // one is computed at run time, and an exact one's part folds with its bits.
      "static int z[]={(_Complex __float128)1.5q==(_Complex __float128)1.25q,(_Complex __float128)1.5q!=0};"
      "int cz(void){return (_Complex __float128)0x1.0000000000000000000000001p0q==(_Complex __float128)0x1.00000000000000000000000008p0q;}"
      "__float128 cr(int n){return n?__real__(_Complex __float128)0.1q:__real__(_Complex __float128)1.5q+__imag__ 1.5qi;}"
      "int tested(int n){unsigned x=2166136261u;x=hash(x,q,sizeof q);x=hash(x,d,sizeof d);x=hash(x,f,sizeof f);x=hash(x,h,sizeof h);"
      "x=hash(x,b,sizeof b);x=hash(x,u,sizeof u);x=hash(x,k,sizeof k);for(unsigned i=0;i<sizeof l/sizeof*l;i++)x=hash(x,&l[i],10);"
      "__float128 r=g(n);x=hash(x,&r,16);__float128 c=0.7q;x=hash(x,&c,16);double e=0.1q;x=hash(x,&e,8);"
      "x=hash(x,z,sizeof z);x^=(unsigned)cz();__float128 w=cr(n&1);x=hash(x,&w,16);"
      "return (int)(x^(unsigned)n);}" },
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
