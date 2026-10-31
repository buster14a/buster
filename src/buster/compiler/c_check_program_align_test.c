// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #148's alignment builtins and __builtin_expect_with_probability (c_check_builtin_misc.c, c_builtin_misc_lower.c) in
// test-checker: source fixtures of their result types, folds and refusals, each checked by both paths against clang
// -fsyntax-only, then programs run natively against clang -O0's build of the same source on both paths: align_up,
// align_down and is_aligned of every integer width, _BitInt, 128-bit and pointer sources, by alignments of other widths
// (one that truncates to 0 in a char among them), folded and at run time, of addresses in static data and frames; expect's
// and expect_with_probability's arguments evaluated once, in order, or not at all where the first argument folds, as
// clang's code does. Include after c_check_program_bits_test.c.
#ifndef BUSTER_C_CHECK_PROGRAM_ALIGN_TEST_INCLUDED
#define BUSTER_C_CHECK_PROGRAM_ALIGN_TEST_INCLUDED

static __attribute__((noinline)) void check_test_align_sources(CheckTest* test, const char* clang)
{
    static const CheckSourceFixture fixtures[] =
    {
        { .name = "align: result types", .source =
          "#define T(e, t) _Static_assert(_Generic((e), t: 1, default: 0), #e);\n"
          "struct B { unsigned x : 3; };\n"
          "void f(char c, unsigned short s, long l, unsigned __int128 q, _BitInt(37) w, int* p, const char* k, int a[4], struct B r, const int ci)\n"
          "{\n"
          "T(__builtin_align_up(c, 4), char) T(__builtin_align_down(s, 2), unsigned short) T(__builtin_align_up(l, (char)8), long)\n"
          "T(__builtin_align_down(q, (unsigned __int128)16), unsigned __int128) T(__builtin_align_up(w, 4), _BitInt(37))\n"
          "T(__builtin_align_up(p, 64), int*) T(__builtin_align_down(k, 2), const char*) T(__builtin_align_up(a, 8), int*)\n"
          "T(__builtin_align_up(r.x, 2), unsigned int) T(__builtin_is_aligned(p, 8), _Bool) T(__builtin_is_aligned(c, 2), _Bool)\n"
          "T(__builtin_expect_with_probability(c, 1, 0.5), long) T(__builtin_align_up(ci, 4) + 0, int)\n"
          "}\n" },
        { .name = "align: a const source's result is const", .source =
          "int f(const int ci)\n{\n__typeof__(__builtin_align_up(ci, 4)) z = 1;\nz = 2;\nreturn z;\n}\n", .rejected = true },
        { .name = "align: folds in constant contexts", .source =
          "static char buf[64];\nstatic _Alignas(32) char aligned[64];\nstruct S { char a; int b; char c[8]; } s;\n"
          "_Static_assert(__builtin_align_up(5, 4) == 8 && __builtin_align_down(7, 4) == 4 && __builtin_is_aligned(16, 8), \"\");\n"
          "_Static_assert(__builtin_align_up((signed char)127, 2) == -128 && __builtin_align_up(0xffffffffu, 16) == 0, \"\");\n"
          "_Static_assert(__builtin_align_up(-5, 4) == -4 && __builtin_align_down(-5, 4) == -8, \"\");\n"
          "_Static_assert(__builtin_align_up((unsigned __int128)1 << 100 | 1, (unsigned __int128)1 << 64) >> 64 == ((unsigned __int128)1 << 36) + 1, \"\");\n"
          "_Static_assert(__builtin_is_aligned(&buf[16], 16) && !__builtin_is_aligned(&buf[3], 8) && __builtin_is_aligned(&aligned[32], 32), \"\");\n"
          "_Static_assert(!__builtin_is_aligned(&aligned[16], 32) && __builtin_is_aligned(&s.b, 4) && !__builtin_is_aligned(&s.c[1], 2), \"\");\n"
          "_Static_assert(__builtin_is_aligned(\"abc\", 1) && __builtin_is_aligned((char*)&s + 4, 4), \"\");\n"
          "_Static_assert(__builtin_expect_with_probability(3, 1, 0.25) == 3, \"\");\n"
          "static char* up = __builtin_align_up(&buf[3], 8);\nstatic char* down = __builtin_align_down(&buf[13], 8);\n"
          "static char* same = __builtin_align_up(&aligned[32], 32);\nstatic int flag = __builtin_is_aligned(&buf[16], 16);\n"
          "int sizes[__builtin_align_up(3, 4)];\nenum { E = __builtin_align_down(13, 8), F = __builtin_is_aligned(&buf[16], 16) };\n"
          "int g(int x) { switch (x) { case __builtin_align_up(1, 8): return 1; case __builtin_is_aligned(&buf[16], 16): return 2; } return 0; }\n" },
        { .name = "align: an address no declaration aligns enough", .source =
          "static char small[8];\nstatic char* p =\n__builtin_align_up(&small[1], 8);\n", .rejected = true },
        { .name = "align: is_aligned of a pointer variable", .source = "char* q;\n_Static_assert(\n__builtin_is_aligned(q, 1), \"\");\n", .rejected = true },
        { .name = "align: a bool source", .source = "int f(_Bool b)\n{\nreturn __builtin_align_up(b, 4);\n}\n", .rejected = true },
        { .name = "align: an enumeration source", .source = "enum E { A };\nint f(enum E e)\n{\nreturn __builtin_align_up(e, 4);\n}\n", .rejected = true },
        { .name = "align: a float source", .source = "int f(float x)\n{\nreturn __builtin_align_down(x, 4);\n}\n", .rejected = true },
        { .name = "align: a struct source", .source = "struct S { int x; };\nint f(struct S s)\n{\nreturn __builtin_is_aligned(s, 4);\n}\n", .rejected = true },
        { .name = "align: a function pointer source", .source = "void g(void);\nint f(void)\n{\nreturn __builtin_is_aligned(&g, 4);\n}\n", .rejected = true },
        { .name = "align: a function source", .source = "void g(void);\nint f(void)\n{\nreturn __builtin_is_aligned(g, 4);\n}\n", .rejected = true },
        { .name = "align: an _Atomic source", .source = "int f(_Atomic int a)\n{\nreturn __builtin_align_up(a, 4);\n}\n", .rejected = true },
        { .name = "align: a bool alignment", .source = "int f(int x, _Bool a)\n{\nreturn __builtin_align_up(x, a);\n}\n", .rejected = true },
        { .name = "align: an enumeration alignment", .source = "enum E { A = 4 };\nint f(int x)\n{\nreturn __builtin_align_up(x, A);\n}\n" },
        { .name = "align: an enumeration variable's alignment", .source = "enum E { A = 4 };\nint f(int x, enum E e)\n{\nreturn __builtin_align_up(x, e);\n}\n",
          .rejected = true },
        { .name = "align: a double alignment", .source = "int f(int x)\n{\nreturn __builtin_align_up(x, 4.0);\n}\n", .rejected = true },
        { .name = "align: a pointer alignment", .source = "int f(int x, int* p)\n{\nreturn __builtin_align_up(x, p);\n}\n", .rejected = true },
        { .name = "align: an alignment of 0", .source = "int f(int x)\n{\nreturn __builtin_align_up(x, 0);\n}\n", .rejected = true },
        { .name = "align: a negative alignment", .source = "int f(int x)\n{\nreturn __builtin_align_down(x, -4);\n}\n", .rejected = true },
        { .name = "align: an alignment that isn't a power of 2", .source = "int f(int x)\n{\nreturn __builtin_is_aligned(x, 3);\n}\n", .rejected = true },
        { .name = "align: an alignment past a char's", .source = "char f(char x)\n{\nreturn __builtin_align_up(x, 256);\n}\n", .rejected = true },
        { .name = "align: an alignment past an int's", .source = "int f(int x)\n{\nreturn __builtin_align_up(x, 4294967296L);\n}\n", .rejected = true },
        { .name = "align: the largest alignment of an int and of a pointer", .source =
          "int f(int x, char* p)\n{\nreturn __builtin_align_up(x, 2147483648L) + (int)(long)__builtin_align_down(p, 0x8000000000000000ul);\n}\n" },
        { .name = "align: an alignment past its side effects", .source = "int g(void);\nint f(int x)\n{\nreturn __builtin_align_up(x, (g(), 3));\n}\n",
          .rejected = true },
        { .name = "align: an unknown alignment", .source = "int f(int x, int a)\n{\nreturn __builtin_align_down(x, a) + __builtin_is_aligned(x, a);\n}\n" },
        { .name = "align: a const object's alignment", .source = "int f(int x)\n{\nconst int a = 3;\nreturn __builtin_align_up(x, a);\n}\n", .rejected = true },
        { .name = "align: a const object's alignment of 0", .source = "int f(int x)\n{\nconst int z = 0;\nreturn __builtin_align_down(x, z);\n}\n",
          .rejected = true },
        { .name = "align: a file-scope const object's alignment", .source = "static const int s = 6;\nint f(int x)\n{\nreturn __builtin_is_aligned(x, s);\n}\n",
          .rejected = true },
        { .name = "align: a const object's alignment past an int's", .source =
          "static const long big = 1L << 40;\nint f(int x)\n{\nreturn __builtin_align_down(x, big);\n}\n", .rejected = true },
        { .name = "align: a const object inside an alignment", .source = "int f(int x)\n{\nconst int c = 3;\nreturn __builtin_align_up(x, c + 0);\n}\n",
          .rejected = true },
        { .name = "align: a const double's alignment", .source = "static const double d = 3.0;\nint f(int x)\n{\nreturn __builtin_align_up(x, (int)d);\n}\n",
          .rejected = true },
        { .name = "align: alignments the evaluator doesn't read", .source =
          "int f(int x, int y)\n{\nconst volatile int v = 3;\nconst int w = y;\nreturn __builtin_align_up(x, v) + __builtin_align_down(x, w);\n}\n" },
        { .name = "align: const objects folded in constant contexts", .source =
          "static const int x = 5;\nstatic const int a = 8;\n"
          "_Static_assert(__builtin_align_up(x, 8) == 8 && __builtin_align_down(13, a) == 8 && __builtin_is_aligned(16, a), \"\");\n"
          "int sizes[__builtin_align_up(x, 8)];\n"
          "int f(void)\n{\nconst int y = 6;\nswitch (0) { case __builtin_align_up(y, 4) - 8: return 1; }\nreturn 0;\n}\n" },
        { .name = "align: one argument", .source = "int f(int x)\n{\nreturn __builtin_align_up(x);\n}\n", .rejected = true },
        { .name = "align: three arguments", .source = "int f(int x)\n{\nreturn __builtin_align_up(x, 4, 4);\n}\n", .rejected = true },
        { .name = "probability: accepted forms", .source =
          "const double half = 0.5;\nconst int one = 1;\n"
          "long f(long x)\n{\nreturn __builtin_expect_with_probability(x, 1, 0.5) + __builtin_expect_with_probability(x, 0, 1) +\n"
          "__builtin_expect_with_probability(x, 1, half) + __builtin_expect_with_probability(x, 1, -0.0) + __builtin_expect_with_probability(x, 1, 0.25f) +\n"
          "__builtin_expect_with_probability(x, 1, sizeof(int) / 8.0) + __builtin_expect_with_probability(x, 1, one ? 0.5 : 0.25) +\n"
          "__builtin_expect_with_probability(x, 1, (0, 0.5)) + __builtin_expect_with_probability(x, 1, one);\n}\n" },
        { .name = "probability: const objects in its expression", .source =
          "static const double half = 0.5;\nlong f(long x)\n{\nconst int n = 4;\nreturn __builtin_expect_with_probability(x, 1, half / 2 + 1.0 / n);\n}\n" },
        { .name = "probability: a const object's past 1", .source =
          "static const double two = 2.0;\nlong f(long x)\n{\nreturn __builtin_expect_with_probability(x, 1, two / 1.5);\n}\n", .rejected = true },
        { .name = "probability: a variable", .source = "long f(long x, double p)\n{\nreturn __builtin_expect_with_probability(x, 1, p);\n}\n", .rejected = true },
        { .name = "probability: past 1", .source = "long f(long x)\n{\nreturn __builtin_expect_with_probability(x, 1, 1.5);\n}\n", .rejected = true },
        { .name = "probability: below 0", .source = "long f(long x)\n{\nreturn __builtin_expect_with_probability(x, 1, -0.1);\n}\n", .rejected = true },
        { .name = "probability: a NaN", .source = "long f(long x)\n{\nreturn __builtin_expect_with_probability(x, 1, __builtin_nan(\"\"));\n}\n", .rejected = true },
        { .name = "probability: an infinity", .source = "long f(long x)\n{\nreturn __builtin_expect_with_probability(x, 1, __builtin_inf());\n}\n", .rejected = true },
        { .name = "probability: a call's", .source = "double g(void);\nlong f(long x)\n{\nreturn __builtin_expect_with_probability(x, 1, (g(), 0.5));\n}\n",
          .rejected = true },
        { .name = "probability: a pointer", .source = "long f(long x)\n{\nreturn __builtin_expect_with_probability(x, 1, (void*)0);\n}\n", .rejected = true },
        { .name = "probability: two arguments", .source = "long f(long x)\n{\nreturn __builtin_expect_with_probability(x, 1);\n}\n", .rejected = true },
        { .name = "expect: a folded first argument's effects in constant contexts", .source =
          "long g(void);\nextern volatile long v;\n_Static_assert(__builtin_expect(1, g()) && __builtin_expect_with_probability(2, v, 0.5) == 2, \"\");\n"
          "static long s = __builtin_expect_with_probability(4, g(), 0.25);\nint arr[__builtin_expect_with_probability(2, g(), 1.0)];\n" },
    };
    enum { FILE_COUNT = BUSTER_ARRAY_LENGTH(fixtures) };
    char directory[] = "/tmp/buster-checker-align-XXXXXX";
    bool ready = mkdtemp(directory) != 0;
    char paths[FILE_COUNT][160];
    char* pointers[FILE_COUNT];
    for (uint32_t i = 0; ready && i < FILE_COUNT; i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        pointers[i] = paths[i];
        ready = parse_test_write(paths[i], fixtures[i].source, strlen(fixtures[i].source));
    }
    const char* arguments[] = { "-std=gnu17" };
    ParseTestClang outcomes[FILE_COUNT];
    const bool ran = ready && parse_test_clang(clang, arguments, BUSTER_ARRAY_LENGTH(arguments), pointers, FILE_COUNT, outcomes);
    check_test_expect(test, ran, "the alignment builtins' Clang oracle ran");
    for (uint32_t i = 0; ran && i < FILE_COUNT; i += 1)
    {
        char description[192];
        snprintf(description, sizeof(description), "%s: expectation differs from Clang", fixtures[i].name);
        check_test_expect(test, outcomes[i].rejected == fixtures[i].rejected, description);
        check_test_source(test, fixtures + i, paths[i], outcomes + i);
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

static __attribute__((noinline)) void check_test_align_programs(CheckTest* test, const char* clang)
{
    // Every integer width at run time, by alignments of every width, powers of 2 from n; a char by an int's 256, which
    // truncates to 0 in its width; _BitInt and 128-bit sources; folds beside them.
    check_test_program_source(test,
        "static unsigned long long h;static void mix(unsigned long long v){h=h*31+v;}static int id(int x){return x;}"
        "static const unsigned long long values[]={0,1,2,3,5,7,8,9,15,16,17,0x7f,0x80,0xff,0x7fff,0xffff,0x12345678,0x7fffffff,0x80000000,0xffffffff,"
        "0x123456789abcdef0ull,0x7fffffffffffffffull,0x8000000000000000ull,0xffffffffffffffffull};"
        "int tested(int n){h=(unsigned)n;unsigned k=(unsigned)(n<0?-n:n)%7;for(unsigned i=0;i<sizeof values/sizeof values[0];i++){"
        "unsigned long long v=values[i]^((unsigned long long)n<<(i%40));signed char sc=v;unsigned char uc=v;char c=v;short s=v;unsigned short us=v;"
        "int si=v;unsigned u=v;long l=v;unsigned long ul=v;_BitInt(7) b7=(_BitInt(7))(v&63);unsigned _BitInt(37) b37=v;"
        "unsigned char a1=(unsigned char)(1u<<k);int a4=1<<k;long a8=1L<<(k*5);unsigned __int128 a16=(unsigned __int128)1<<(k*9);"
        "mix(__builtin_align_up(sc,a1));mix(__builtin_align_down(uc,a4));mix(__builtin_is_aligned(c,a8));mix(__builtin_align_up(s,a16));"
        "mix(__builtin_align_down(us,a1));mix(__builtin_align_up(si,a4));mix(__builtin_is_aligned(u,a8));mix(__builtin_align_up(l,a16));"
        "mix(__builtin_align_down(ul,a1));mix(__builtin_is_aligned(l,a16));mix(__builtin_align_up(b7,a1));mix(__builtin_align_down(b37,a8));"
        "mix(__builtin_is_aligned(b37,a4));mix(__builtin_align_up(c,id(256)));mix(__builtin_is_aligned(uc,id(256)));"
        "mix(__builtin_align_up(sc,(unsigned char)128));mix(__builtin_align_up(si,4));mix(__builtin_align_down(l,1L<<40));}"
        "mix(__builtin_align_up(-5,4));mix(__builtin_align_down(-5,4));mix(__builtin_is_aligned(48,16));"
        "return (int)(h^(h>>32));}",
        "align-integers-of-every-width-by-every-alignment", clang);
    check_test_program_source(test,
        "static unsigned long long h;static void mix(unsigned long long v){h=h*31+v;}"
        "static void mix128(unsigned __int128 v){mix((unsigned long long)v);mix((unsigned long long)(v>>64));}"
        "int tested(int n){h=(unsigned)n;unsigned k=(unsigned)(n<0?-n:n)%9;"
        "for(int i=0;i<12;i++){unsigned __int128 v=((unsigned __int128)0x0123456789abcdefull<<64|0xfedcba9876543210ull)*(unsigned)(n+i*7)+(unsigned)i;"
        "__int128 sv=(__int128)v;_BitInt(100) b=(_BitInt(100))(v>>28);unsigned _BitInt(65) w=(unsigned _BitInt(65))v;"
        "unsigned __int128 a=(unsigned __int128)1<<(k*14+i);long a8=1L<<(k*7);unsigned char a1=(unsigned char)(1u<<(k%8));"
        "mix128(__builtin_align_up(v,a));mix128(__builtin_align_down(v,a8));mix(__builtin_is_aligned(v,a));mix(__builtin_is_aligned(sv,a1));"
        "mix128((unsigned __int128)__builtin_align_up(sv,a8));mix128((unsigned __int128)__builtin_align_down(sv,a));"
        "mix128((unsigned __int128)__builtin_align_up(b,a1));mix128((unsigned __int128)__builtin_align_down(b,a));mix(__builtin_is_aligned(b,a8));"
        "mix128(__builtin_align_up(w,a));mix128(__builtin_align_down(w,(unsigned __int128)1<<64));mix(__builtin_is_aligned(w,a1));"
        "mix((unsigned long long)__builtin_align_up(i,a));}"
        "return (int)(h^(h>>32));}",
        "align-128-bit-and-wide-bit-int-sources-and-alignments", clang);
    // Addresses in static data and frames, folded and at run time: offsets from n, and the arrays' own alignment (#978).
    check_test_program_source(test,
        "static char buf[64];static _Alignas(32) char aligned[96];char pad;static char*up=__builtin_align_up(&buf[3],8);"
        "static char*down=__builtin_align_down(&buf[13],8);static char*same=__builtin_align_up(&aligned[32],32);static int flag=__builtin_is_aligned(&buf[16],16);"
        "static char*keep(char*p){return p;}"
        "int tested(int n){char local[80];char pad2;keep(&pad2);keep(&pad);unsigned i=(unsigned)(n<0?-n:n)%40;long r=0;"
        "r+=__builtin_align_up(&buf[i],16)-buf;r=r*7+(__builtin_align_down(&buf[i+3],8)-buf);r=r*7+__builtin_is_aligned(&buf[i],8);"
        "r=r*7+(__builtin_align_up(&aligned[i],32)-aligned);r=r*7+__builtin_is_aligned(&aligned[i],32);"
        "r=r*7+(__builtin_align_up(&local[i],16)-local);r=r*7+(__builtin_align_down(local+i+15,16)-local);r=r*7+__builtin_is_aligned(&local[i],4);"
        "r=r*7+(up-buf)+(down-buf)*3+(same-aligned)*5+flag*11;r=r*7+__builtin_is_aligned(&buf[16],16)+__builtin_is_aligned(&local[16],16);"
        "int*ip=(int*)&local[8];r=r*7+(int)((char*)__builtin_align_up(ip+n%3,16)-local);"
        "return (int)r;}",
        "align-addresses-in-static-data-and-frames", clang);
    // expect's and expect_with_probability's arguments: each once, in order, where the first argument is unknown; neither
    // where it folds, a const local's constant initializer among them, the expected value's call dropped, as clang's code
    // drops it.
    check_test_program_source(test,
        "static int calls,order;static long next(int tag){calls++;order=order*10+tag;return tag;}"
        "int tested(int n){calls=0;order=0;long r=0;"
        "r+=__builtin_expect(next(1),next(2));"
        "r+=__builtin_expect_with_probability(next(3),next(4),0.5);"
        "r+=__builtin_expect(7,next(5));"
        "r+=__builtin_expect_with_probability(8,next(6),1.0);"
        "const long k=9;r+=__builtin_expect(k,next(7));"
                "r+=__builtin_expect_with_probability(n,next(8),0.25f);"
        "if(__builtin_expect(n>3,0))r+=100;if(__builtin_expect_with_probability(n<0,1,0.9))r+=1000;"
        "return (int)r*1000+calls*100+order%97;}",
        "expect-arguments-evaluated-once-or-not-at-all", clang);
}

static __attribute__((noinline)) void check_test_align(CheckTest* test, const char* clang)
{
    check_test_align_sources(test, clang);
    check_test_align_programs(test, clang);
}

#endif
