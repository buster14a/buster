// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// clang's floating builtins past the first classifications (#148): isnormal, issubnormal, iszero, issignaling and
// isfpclass, signbitf and signbitl, and canonicalize, canonicalizef and canonicalizel. Source fixtures checked by both
// paths against clang, and programs run natively against clang -O0's build: isfpclass with every mask on every class of
// float, double and x87 encoding, under FENV_ACCESS and not, the other classifiers, the signbits' conversions,
// canonicalize's results, folds and evaluation, and isfpclass of vectors of floats and doubles at x86-64-v4. Included by
// c_checker_test.c.

// The rules, as clang's Sema has them: a real floating argument unconverted (isfpclass's a vector of them too), isfpclass's
// mask an integer constant expression in [0, 1023], and the signbits' and canonicalize's prototypes.
static __attribute__((noinline)) void check_test_classify_sources(CheckTest* test, const char* clang)
{
    static const CheckSourceFixture fixtures[] =
    {
        { .name = "classify: folds in constant expressions",
          .source = "_Static_assert(__builtin_isnormal(1.0) && !__builtin_isnormal(0x1p-1030) && __builtin_issubnormal(0x1p-1030), \"\");\n"
                    "_Static_assert(__builtin_iszero(-0.0f) && !__builtin_issignaling(__builtin_nanf(\"\")) && __builtin_isfpclass(-1.0, 8), \"\");\n"
                    "_Static_assert(__builtin_isnormal(0x1p-16382L) && __builtin_issubnormal(0x1p-16383L) && __builtin_isfpclass(-0.0L, 32), \"\");\n"
                    "_Static_assert(__builtin_signbitf(-0.0) && !__builtin_signbitl(1) && __builtin_signbitl(-__builtin_infl()), \"\");\n"
                    "int a[__builtin_isfpclass(-2.0, 8) + 1];\nint f(double x)\n{\nswitch (1)\n{\ncase __builtin_isnormal(1.0):\nreturn 1;\n}\n"
                    "return __builtin_issignaling(x);\n}\n" },
        { .name = "classify: result types",
          .source = "_Static_assert(_Generic(__builtin_isfpclass(1.0f, 3), int: 1, default: 0) && _Generic(__builtin_signbitl(1), int: 1, default: 0), \"\");\n"
                    "_Static_assert(_Generic(__builtin_canonicalizef(1), float: 1, default: 0), \"\");\n"
                    "_Static_assert(_Generic(__builtin_canonicalize(1.0f), double: 1, default: 0), \"\");\n"
                    "_Static_assert(_Generic(__builtin_canonicalizel(1.0), long double: 1, default: 0), \"\");\n" },
        { .name = "classify: masks of other integer types",
          .source = "int f(double x)\n{\nreturn __builtin_isfpclass(x, 1023u) + __builtin_isfpclass(x, (char)3) + __builtin_isfpclass(x, (int)3.7) +\n"
                    "__builtin_isfpclass(x, sizeof(int)) + __builtin_isfpclass(x, (_Bool)0) + __builtin_isfpclass(x, (unsigned char)127);\n}\n" },
        { .name = "classify: isfpclass of a vector",
          .source = "typedef float v4 __attribute__((vector_size(16)));\ntypedef int i4 __attribute__((vector_size(16)));\n"
                    "typedef double v2 __attribute__((ext_vector_type(2)));\ni4 f(v4 x)\n{\nreturn __builtin_isfpclass(x, 3);\n}\n"
                    "_Static_assert(_Generic(__builtin_isfpclass((v4){ 0 }, 3), i4: 1, default: 0), \"\");\n" },
        { .name = "classify: a mask that isn't constant", .source = "int f(double x, int m)\n{\nreturn __builtin_isfpclass(x, m);\n}\n", .rejected = true },
        { .name = "classify: a mask past 1023", .source = "int f(double x)\n{\nreturn __builtin_isfpclass(x, 1024);\n}\n", .rejected = true },
        { .name = "classify: a negative mask", .source = "int f(double x)\n{\nreturn __builtin_isfpclass(x, -1);\n}\n", .rejected = true },
        // clang sign-extends the mask from its type's width, whatever its signedness.
        { .name = "classify: a bool mask of 1", .source = "int f(double x)\n{\nreturn __builtin_isfpclass(x, (_Bool)1);\n}\n", .rejected = true },
        { .name = "classify: an unsigned char mask of 200", .source = "int f(double x)\n{\nreturn __builtin_isfpclass(x, (unsigned char)200);\n}\n",
          .rejected = true },
        { .name = "classify: a floating mask", .source = "int f(double x)\n{\nreturn __builtin_isfpclass(x, 1.0);\n}\n", .rejected = true },
        { .name = "classify: a comma's mask", .source = "int f(double x)\n{\nreturn __builtin_isfpclass(x, (1, 2));\n}\n", .rejected = true },
        { .name = "classify: isfpclass of an int", .source = "int f(int x)\n{\nreturn __builtin_isfpclass(x, 3);\n}\n", .rejected = true },
        { .name = "classify: isfpclass of a complex", .source = "int f(_Complex double x)\n{\nreturn __builtin_isfpclass(x, 3);\n}\n", .rejected = true },
        { .name = "classify: isfpclass without its mask", .source = "int f(double x)\n{\nreturn __builtin_isfpclass(x);\n}\n", .rejected = true },
        { .name = "classify: isnormal of an int", .source = "int f(int x)\n{\nreturn __builtin_isnormal(x);\n}\n", .rejected = true },
        { .name = "classify: iszero of two", .source = "int f(double x)\n{\nreturn __builtin_iszero(x, x);\n}\n", .rejected = true },
        { .name = "classify: issubnormal of a struct", .source = "struct S { double d; };\nint f(struct S s)\n{\nreturn __builtin_issubnormal(s);\n}\n",
          .rejected = true },
        { .name = "classify: signbitf of a pointer", .source = "int f(int *p)\n{\nreturn __builtin_signbitf(p);\n}\n", .rejected = true },
        { .name = "classify: canonicalize of a pointer", .source = "double f(int *p)\n{\nreturn __builtin_canonicalize(p);\n}\n", .rejected = true },
        { .name = "classify: canonicalize in a static initializer", .source = "static const double d = __builtin_canonicalize(1.0);\n", .rejected = true },
    };
    enum { FILE_COUNT = BUSTER_ARRAY_LENGTH(fixtures) };
    char directory[] = "/tmp/buster-checker-classify-XXXXXX";
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
    check_test_expect(test, ready, "classification Clang oracle could not run");
    for (uint32_t i = 0; ready && i < FILE_COUNT; i += 1)
    {
        char description[192];
        snprintf(description, sizeof(description), "%s: expectation differs from Clang", fixtures[i].name);
        check_test_expect(test, outcomes[i].rejected == (fixtures[i].rejected && (!fixtures[i].unsupported || fixtures[i].undecided)), description);
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

// isfpclass of x with every mask (each a constant, as it must be), into an FNV-1a hash h of the answers.
#define BUSTER_CHECK_TEST_CLASSIFY_MASKS \
    "#define C1(m) h=(h^(unsigned)__builtin_isfpclass(x,(m)))*16777619u;\n" \
    "#define C4(m) C1(m) C1((m)+1) C1((m)+2) C1((m)+3)\n" \
    "#define C16(m) C4(m) C4((m)+4) C4((m)+8) C4((m)+12)\n" \
    "#define C64(m) C16(m) C16((m)+16) C16((m)+32) C16((m)+48)\n" \
    "#define C256(m) C64(m) C64((m)+64) C64((m)+128) C64((m)+192)\n" \
    "#define C1024 C256(0) C256(256) C256(512) C256(768)\n"

// The x87's encodings, two of each category: zero, denormal, pseudo-denormal, normal, unnormal (a pseudo-zero among
// them), infinity, pseudo-infinity, signaling and quiet NaNs, pseudo-NaN; tested(n) takes one by n, with each sign.
#define BUSTER_CHECK_TEST_CLASSIFY_X87 \
    "typedef union{struct{unsigned long m;unsigned short e;}b;long double x;}L;" \
    "static const unsigned long ms[20]={0,0,1,0x7fffffffffffffffUL,0x8000000000000000UL,0xffffffffffffffffUL,0x8000000000000000UL," \
    "0xc000000000000001UL,0,0x4000000000000000UL,0x8000000000000000UL,0x8000000000000000UL,0,0,0x8000000000000001UL,0xbfffffffffffffffUL," \
    "0xc000000000000000UL,0xffffffffffffffffUL,0x4000000000000000UL,1};" \
    "static const unsigned short es[20]={0,0,0,0,0,0,1,0x3fff,1,0x7ffe,0x7fff,0x7fff,0x7fff,0x7fff,0x7fff,0x7fff,0x7fff,0x7fff,0x7fff,0x7fff};" \
    "long double make(int n,int s){L l={0};unsigned k=(unsigned)n%20;l.b.m=ms[k];l.b.e=(unsigned short)(es[k]|(s<<15));return l.x;}"

// A float's and a double's bits, two of each class: zero, denormal, normal, infinity, signaling and quiet NaNs.
#define BUSTER_CHECK_TEST_CLASSIFY_IEEE \
    "static const unsigned fs[12]={0,0,1,0x7fffff,0x800000,0x3f800000,0x7f800000,0x7f800000,0x7f800001,0x7fbfffff,0x7fc00000,0x7fffffff};" \
    "static const unsigned long ds[12]={0,0,1,0xfffffffffffffUL,0x10000000000000UL,0x3ff0000000000001UL,0x7ff0000000000000UL," \
    "0x7ff0000000000000UL,0x7ff0000000000001UL,0x7ff7ffffffffffffUL,0x7ff8000000000000UL,0x7fffffffffffffffUL};" \
    "float makef(int n,int s){union{unsigned u;float f;}v;v.u=fs[(unsigned)n%12]|((unsigned)s<<31);return v.f;}" \
    "double maked(int n,int s){union{unsigned long u;double d;}v;v.u=ds[(unsigned)n%12]|((unsigned long)s<<63);return v.d;}"

// The programs, each natively against clang -O0's build of the same source, on both paths.
static const struct
{
    const char* name;
    const char* source;
} check_test_classify_programs[] =
{
    { "classify-isfpclass-every-mask-of-x87-encodings",
      BUSTER_CHECK_TEST_CLASSIFY_MASKS BUSTER_CHECK_TEST_CLASSIFY_X87
      "int tested(int n){unsigned h=2166136261u;for(int s=0;s<2;s++){long double x=make(n,s);C1024}return (int)h;}" },
    { "classify-isfpclass-every-mask-of-x87-encodings-under-fenv-access",
      "#pragma STDC FENV_ACCESS ON\n" BUSTER_CHECK_TEST_CLASSIFY_MASKS BUSTER_CHECK_TEST_CLASSIFY_X87
      "int tested(int n){unsigned h=2166136261u;for(int s=0;s<2;s++){long double x=make(n,s);C1024}return (int)h;}" },
    { "classify-isfpclass-every-mask-of-floats",
      BUSTER_CHECK_TEST_CLASSIFY_MASKS BUSTER_CHECK_TEST_CLASSIFY_IEEE
      "int tested(int n){unsigned h=2166136261u;for(int s=0;s<2;s++){float x=makef(n,s);C1024}return (int)h;}" },
    { "classify-isfpclass-every-mask-of-doubles",
      BUSTER_CHECK_TEST_CLASSIFY_MASKS BUSTER_CHECK_TEST_CLASSIFY_IEEE
      "int tested(int n){unsigned h=2166136261u;for(int s=0;s<2;s++){double x=maked(n,s);C1024}return (int)h;}" },
    { "classify-classifiers-signbits-and-canonicalize-of-x87-encodings",
      BUSTER_CHECK_TEST_CLASSIFY_X87
      "int tested(int n){int r=0;for(int s=0;s<2;s++){long double x=make(n,s);L c;c.x=__builtin_canonicalizel(x);"
      "r=r*7+__builtin_isnormal(x)+2*__builtin_issubnormal(x)+4*__builtin_iszero(x)+8*__builtin_issignaling(x)+16*__builtin_signbitl(x)+"
      "(int)(c.b.m^(c.b.m>>29)^(c.b.m>>47))+c.b.e*31;}return r;}" },
    { "classify-classifiers-signbits-and-canonicalize-of-floats-and-doubles",
      BUSTER_CHECK_TEST_CLASSIFY_IEEE
      "int tested(int n){int r=0;for(int s=0;s<2;s++){float f=makef(n,s);double d=maked(n,s);"
      "union{float f;unsigned u;}cf={__builtin_canonicalizef(f)};union{double d;unsigned long u;}cd={__builtin_canonicalize(d)};"
      "union{float f;unsigned u;}nf={__builtin_canonicalizef(d)};"
      "r=r*5+__builtin_isnormal(f)+2*__builtin_issubnormal(f)+4*__builtin_iszero(f)+8*__builtin_issignaling(f)+16*__builtin_isnormal(d)+"
      "32*__builtin_issubnormal(d)+64*__builtin_iszero(d)+128*__builtin_issignaling(d)+256*__builtin_signbitf(f)+512*__builtin_signbitf(d)+"
      "1024*__builtin_signbitl(f)+2048*__builtin_signbitl(d)+(int)(cf.u^nf.u*3)+(int)(cd.u^(cd.u>>32));}return r;}" },
    { "classify-folds-and-operands-evaluated-once",
      "static const int t[]={__builtin_isnormal(1.0),__builtin_issubnormal(0x1p-1074),__builtin_iszero(-0.0f),"
      "__builtin_issignaling(__builtin_nanl(\"\")),__builtin_isfpclass(-1.0L,8),__builtin_isfpclass(0x1p-130f,0x80),__builtin_signbitf(-2),"
      "__builtin_signbitl(-0.0),__builtin_isfpclass(__builtin_inf(),0x200)};"
      "_Static_assert(__builtin_isnormal(0x1p-16382L)&&__builtin_issubnormal(0x1p-16383L)&&!__builtin_isnormal(0x1p-1023),\"\");"
      "int seen;double f(double v){seen+=1;return v;}long double g(long double v){seen+=100;return v;}"
      "int tested(int n){seen=0;int r=__builtin_isnormal(f(n))+2*__builtin_isfpclass(g(-n),0x38)+4*__builtin_signbitf(f(-n))+"
      "8*__builtin_signbitl(g(n))+16*(__builtin_canonicalize(f(n))==n)+32*(__builtin_canonicalizel(g(n))==n)+64*__builtin_iszero(f(n));"
      "return r+seen*128+t[(unsigned)n%9]*(n+7);}" },
};

// isfpclass of vectors of 4, 8, 16, 32 and 64 bytes of floats and doubles, GNU's and clang's extended ones, each lane 1
// or 0 (clang's zero extension of llvm.is.fpclass's lanes), with masks LLVM tests apart, into an FNV-1a hash.
#define BUSTER_CHECK_TEST_CLASSIFY_VECTORS \
    "typedef float f2 __attribute__((vector_size(8)));typedef int i2 __attribute__((vector_size(8)));" \
    "typedef float f4 __attribute__((vector_size(16)));typedef int i4 __attribute__((vector_size(16)));" \
    "typedef float f8 __attribute__((vector_size(32)));typedef int i8 __attribute__((vector_size(32)));" \
    "typedef float f16 __attribute__((vector_size(64)));typedef int i16 __attribute__((vector_size(64)));" \
    "typedef double d2 __attribute__((vector_size(16)));typedef long long l2 __attribute__((vector_size(16)));" \
    "typedef double d4 __attribute__((vector_size(32)));typedef long long l4 __attribute__((vector_size(32)));" \
    "typedef double d8 __attribute__((vector_size(64)));typedef long long l8 __attribute__((vector_size(64)));" \
    "typedef float e4 __attribute__((ext_vector_type(4)));typedef int k4 __attribute__((ext_vector_type(4)));" \
    "typedef float e1 __attribute__((ext_vector_type(1)));typedef int k1 __attribute__((ext_vector_type(1)));" \
    "unsigned mix(unsigned h,int v){return (h^(unsigned)v)*16777619u;}"

static __attribute__((noinline)) void check_test_classify_runs(CheckTest* test, const char* clang)
{
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(check_test_classify_programs); i += 1)
    {
        check_test_program_source(test, check_test_classify_programs[i].source, check_test_classify_programs[i].name, clang);
    }
    // isfpclass of vectors, which vectors' lowering takes at x86-64-v4 (clang's builds with its -march too).
    X86Target target = { 0 };
    const bool parsed = x86_target_parse("x86-64-v4", strlen("x86-64-v4"), &target);
    check_test_expect(test, parsed, "the vector classifications' target, x86-64-v4");
    if (parsed)
    {
        const X86Target native = x86_target_native();
        const bool run_native = (native.features & target.features) == target.features;
        check_test_program_build_release();
        check_test_program_march = "-march=x86-64-v4";
        check_test_program_source_native_limits(test,
            BUSTER_CHECK_TEST_CLASSIFY_VECTORS BUSTER_CHECK_TEST_CLASSIFY_IEEE
            "int tested(int n){unsigned h=2166136261u;f16 f;d8 d;for(int i=0;i<16;i++)f[i]=makef(n+i,(n+i)&1);"
            "for(int i=0;i<8;i++)d[i]=maked(n*3+i,i&1);"
            "i16 a=__builtin_isfpclass(f,0x3),b=__builtin_isfpclass(f,0x1f8),c=__builtin_isfpclass(f,0x2a5);"
            "l8 p=__builtin_isfpclass(d,0x90),q=__builtin_isfpclass(d,0x204),r=__builtin_isfpclass(d,0x3ff);"
            "f2 g2={f[0],f[1]};i2 a2=__builtin_isfpclass(g2,0x60);f4 g4={f[2],f[3],f[4],f[5]};i4 a4=__builtin_isfpclass(g4,0x108);"
            "f8 g8;for(int i=0;i<8;i++)g8[i]=f[i+8];i8 a8=__builtin_isfpclass(g8,0x0f3);"
            "d2 h2={d[0],d[1]};l2 p2=__builtin_isfpclass(h2,0x1);d4 h4={d[2],d[3],d[4],d[5]};l4 p4=__builtin_isfpclass(h4,0x3fc);"
            "e4 x4={f[6],f[7],f[8],f[9]};k4 y4=__builtin_isfpclass(x4,0x207);e1 x1={f[10]};k1 y1=__builtin_isfpclass(x1,0x80);"
            "for(int i=0;i<16;i++)h=mix(h,a[i]+2*b[i]+4*c[i]);for(int i=0;i<8;i++)h=mix(h,(int)(p[i]+2*q[i]+4*r[i])+8*a8[i]);"
            "for(int i=0;i<2;i++)h=mix(h,a2[i]+2*(int)p2[i]);for(int i=0;i<4;i++)h=mix(h,a4[i]+2*(int)p4[i]+4*y4[i]);"
            "return (int)mix(h,y1[0]);}",
            "classify-isfpclass-of-vectors", clang, false, -1, 0, &target, run_native);
        check_test_program_build_release();
        check_test_program_march = "-march=x86-64";
    }
}

static __attribute__((noinline)) void check_test_classify(CheckTest* test, const char* clang)
{
    check_test_classify_sources(test, clang);
    check_test_classify_runs(test, clang);
}
