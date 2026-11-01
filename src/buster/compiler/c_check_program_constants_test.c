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
        { .name = "float constants: an integer's payload", .source = "double f(void)\n{\nreturn __builtin_nans(1);\n}\n", .rejected = true },
        { .name = "float constants: nansf16 without its payload", .source = "_Float16 f(void)\n{\nreturn __builtin_nansf16();\n}\n",
          .rejected = true },
        { .name = "float constants: inff128 of an argument", .source = "__float128 f(void)\n{\nreturn __builtin_inff128(1);\n}\n", .rejected = true },
        { .name = "float constants: nanf128 of two payloads", .source = "__float128 f(void)\n{\nreturn __builtin_nanf128(\"\", \"\");\n}\n",
          .rejected = true },
    };
    enum { FILE_COUNT = BUSTER_ARRAY_LENGTH(fixtures) };
    char directory[] = "/tmp/buster-checker-float-constants-XXXXXX";
    bool ready = mkdtemp(directory) != 0;
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
    for (uint32_t i = 0; i < FILE_COUNT; i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        unlink(paths[i]);
    }
    if (directory[0] != 0)
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
