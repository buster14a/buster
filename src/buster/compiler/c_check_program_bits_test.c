// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #148's bit builtins (c_check_builtin_bits.c, c_builtin_bits_lower.c) in test-checker: source fixtures of their result
// types, folds and refusals, each checked by both paths against clang -fsyntax-only, then programs run natively against
// clang -O0's build of the same source on both paths: every generic builtin of the standard unsigned types and bool of
// boundary values, with clzg's and ctzg's results for zero, the fixed ones, _BitInt, enumeration and bit-field operands,
// rotations by counts of eight types, the carry chains of every width, folds beside operands evaluated once, in order,
// and 128-bit and wide _BitInt operands, results and rotation counts. Include after c_check_musttail_test.c.
#ifndef BUSTER_C_CHECK_PROGRAM_BITS_TEST_INCLUDED
#define BUSTER_C_CHECK_PROGRAM_BITS_TEST_INCLUDED

static __attribute__((noinline)) void check_test_bits_sources(CheckTest* test, const char* clang)
{
    static const CheckSourceFixture fixtures[] =
    {
        { .name = "bits: result types", .source =
          "#define T(e, t) _Static_assert(_Generic((e), t: 1, default: 0), #e);\n"
          "enum U { UA = 1 };\n"
          "struct B { unsigned x : 3; int z : 5; };\n"
          "void f(unsigned char c, unsigned short s, _Bool b, enum U e, unsigned _BitInt(37) w, _BitInt(48) v, const unsigned k, struct B r)\n"
          "{\n"
          "T(__builtin_bitreverse8(c), unsigned char) T(__builtin_bitreverse64(1), unsigned long) T(__builtin_clrsbl(1), int) T(__builtin_ctzs(s), int)\n"
          "T(__builtin_popcountg(b), int) T(__builtin_popcountg(e), int) T(__builtin_clzg(w, (short)1), int) T(__builtin_ctzg(r.x, r.z), int)\n"
          "T(__builtin_bitreverseg(v), _BitInt(48)) T(__builtin_bitreverseg(k), unsigned int) T(__builtin_bswapg(e), enum U) T(__builtin_bitreverseg(r.z), int)\n"
          "T(__builtin_stdc_leading_zeros(c), unsigned int) T(__builtin_stdc_has_single_bit(w), _Bool) T(__builtin_stdc_bit_floor(s), unsigned short)\n"
          "T(__builtin_stdc_bit_ceil(w), unsigned _BitInt(37)) T(__builtin_stdc_rotate_left(b, -1), _Bool) T(__builtin_stdc_rotate_right(e, v), enum U)\n"
          "T(__builtin_addcb(c, c, c, &c), unsigned char) T(__builtin_subcll(1, 2, 0, (unsigned long long *)0), unsigned long long)\n"
          "}\n" },
        { .name = "bits: folds in constant contexts", .source =
          "_Static_assert(__builtin_popcountg(7u) == 3 && __builtin_clzg(1u, 5) == 31 && __builtin_clzg(0u, 5) == 5, \"\");\n"
          "_Static_assert(__builtin_ctzg((unsigned char)0, -1) == -1 && __builtin_stdc_bit_ceil(0x80000001u) == 0 && __builtin_stdc_bit_ceil(0u) == 1, \"\");\n"
          "_Static_assert(__builtin_stdc_rotate_left((unsigned char)1, -1) == 0x80 && __builtin_stdc_rotate_right(1u, 33) == 0x80000000u, \"\");\n"
          "_Static_assert(__builtin_bitreverse8(1) == 0x80 && __builtin_clrsb(-1) == 31 && __builtin_clrsbll(0) == 63, \"\");\n"
          "_Static_assert(__builtin_bswapg((unsigned short)0x1234) == 0x3412 && __builtin_bitreverseg((signed char)1) == -128, \"\");\n"
          "_Static_assert(__builtin_stdc_first_leading_zero((unsigned char)0xff) == 0 && __builtin_stdc_leading_ones((unsigned char)0xf0) == 4, \"\");\n"
          "_Static_assert(__builtin_popcountg((unsigned __int128)-1) == 128 && __builtin_clzg((unsigned _BitInt(100))1) == 99, \"\");\n"
          "int a[__builtin_clzs(1)];\n"
          "enum { E1 = __builtin_ctzs(8), E2 = __builtin_stdc_bit_width(5u) };\n"
          "static int s = __builtin_stdc_count_zeros((unsigned short)0x0f0f) + __builtin_ctzg(0u, 9);\n" },
        { .name = "bits: folds through const objects (#990)", .source =
          "static const unsigned ku=7u;\n"
          "static const unsigned _BitInt(37) kw=37;\n"
          "static const unsigned _BitInt(16) kw16=0x1234;\n"
          "static const unsigned short ks=0x1234;\n"
          "static const unsigned char kc=0xf0;\n"
          "_Static_assert(__builtin_popcountg(ku)==3 && __builtin_clzg(ku,99)==29 && __builtin_ctzg(ku,99)==0, \"\");\n"
          "_Static_assert(__builtin_bswapg(ks)==0x3412 && __builtin_bitreverseg((unsigned char)ku)==0xe0, \"\");\n"
          "_Static_assert(__builtin_stdc_count_ones(ku)==3 && __builtin_stdc_bit_width(ku)==3 && __builtin_stdc_bit_ceil(ku)==8, \"\");\n"
          "_Static_assert(__builtin_popcountg(kw)==3 && __builtin_bswapg(kw16)==0x3412 && __builtin_stdc_rotate_left(kc,4)==0x0f, \"\");\n"
          "int ga[__builtin_popcountg(ku)];\n"
          "enum { GE=__builtin_stdc_bit_floor(ku) };\n"
          "struct GF { unsigned field:__builtin_stdc_bit_width(ku); };\n"
          "unsigned f(unsigned n) { switch (n) { case __builtin_stdc_count_ones(ku): return 1; } return 0; }\n" },
        { .name = "bits: fold of a volatile object is no constant", .source =
          "static const volatile unsigned kv=7u;\n"
          "int gv[__builtin_popcountg(kv)];\n", .rejected = true },
        { .name = "bits: fold of a mutable object is no constant", .source =
          "static unsigned kn=7u;\n"
          "_Static_assert(__builtin_clzg(kn,0)==29, \"\");\n", .rejected = true },
        { .name = "bits: clzg of zero without a fallback is no constant", .source = "int z =\n__builtin_clzg(0u);\n", .rejected = true },
        { .name = "bits: popcountg of an int", .source = "int f(int i)\n{\nreturn __builtin_popcountg(i);\n}\n", .rejected = true },
        { .name = "bits: popcountg of plain char where it is signed", .source = "int f(char c)\n{\nreturn __builtin_popcountg(c);\n}\n", .rejected = true },
        { .name = "bits: popcountg of plain char where it is unsigned", .source = "int f(char c)\n{\nreturn __builtin_popcountg(c);\n}\n", .char_unsigned = true },
        { .name = "bits: a stdc builtin of a bool", .source = "unsigned f(_Bool b)\n{\nreturn __builtin_stdc_count_ones(b);\n}\n", .rejected = true },
        { .name = "bits: a stdc builtin of an enumeration", .source = "enum U { UA = 1 };\nunsigned f(enum U e)\n{\nreturn __builtin_stdc_leading_zeros(e);\n}\n",
          .rejected = true },
        { .name = "bits: clzg's fallback of a long", .source = "int f(unsigned u, long l)\n{\nreturn __builtin_clzg(u, l);\n}\n", .rejected = true },
        { .name = "bits: ctzg's fallback of an unsigned int", .source = "int f(unsigned u, unsigned v)\n{\nreturn __builtin_ctzg(u, v);\n}\n", .rejected = true },
        { .name = "bits: clzg's fallback of a 31-bit unsigned bit-field", .source = "struct B { unsigned x : 31; };\nint f(unsigned u, struct B b)\n{\nreturn __builtin_clzg(u, b.x);\n}\n" },
        { .name = "bits: clzg's fallback of a 32-bit unsigned bit-field", .source = "struct B { unsigned x : 32; };\nint f(unsigned u, struct B b)\n{\nreturn __builtin_clzg(u, b.x);\n}\n",
          .rejected = true },
        { .name = "bits: clzg's fallback of an enumeration of int", .source = "enum S { SA = -1 };\nint f(unsigned u, enum S e)\n{\nreturn __builtin_clzg(u, e);\n}\n" },
        { .name = "bits: a rotation of an int", .source = "unsigned f(int u)\n{\nreturn __builtin_stdc_rotate_left(u, 1);\n}\n", .rejected = true },
        { .name = "bits: a rotation by a float", .source = "unsigned f(unsigned u, float c)\n{\nreturn __builtin_stdc_rotate_right(u, c);\n}\n", .rejected = true },
        { .name = "bits: bswapg of a 24-bit _BitInt", .source = "_BitInt(24) f(_BitInt(24) x)\n{\nreturn __builtin_bswapg(x);\n}\n", .rejected = true },
        { .name = "bits: bitreverseg of a float", .source = "float f(float x)\n{\nreturn __builtin_bitreverseg(x);\n}\n", .rejected = true },
        { .name = "bits: a carry of the other signedness", .source = "unsigned f(unsigned u, int *q)\n{\nreturn __builtin_addc(u, u, 0, q);\n}\n" },
        { .name = "bits: a carry of a const pointee", .source = "unsigned f(unsigned u, const unsigned *p)\n{\nreturn __builtin_subc(u, u, 1, p);\n}\n" },
        { .name = "bits: a carry of another width", .source = "unsigned f(unsigned u, unsigned long *p)\n{\nreturn __builtin_addc(u, u, 0, p);\n}\n", .rejected = true },
        { .name = "bits: a carry of an int", .source = "unsigned f(unsigned u, int x)\n{\nreturn __builtin_addc(u, u, 0, x);\n}\n", .rejected = true },
        { .name = "bits: a carry chain of three arguments", .source = "unsigned f(unsigned u)\n{\nreturn __builtin_addcl(u, u, 0);\n}\n", .rejected = true },
        { .name = "bits: bitreverse32 of a pointer", .source = "unsigned f(int *p)\n{\nreturn __builtin_bitreverse32(p);\n}\n", .rejected = true },
        { .name = "bits: bitreverse32 of a double", .source = "unsigned f(double d)\n{\nreturn __builtin_bitreverse32(d);\n}\n" },
        { .name = "bits: clzg of three arguments", .source = "int f(unsigned u)\n{\nreturn __builtin_clzg(u, 1, 2);\n}\n", .rejected = true },
    };
    enum { FILE_COUNT = BUSTER_ARRAY_LENGTH(fixtures) };
    char directory[] = "/tmp/buster-checker-bits-XXXXXX";
    bool ready = mkdtemp(directory) != 0;
    char paths[FILE_COUNT][160];
    for (uint32_t i = 0; ready && i < FILE_COUNT; i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        ready = parse_test_write(paths[i], fixtures[i].source, strlen(fixtures[i].source));
    }
    // Plain char's signedness is an option: clang checks the fixtures of each in a run of its own.
    char* pointers[2][FILE_COUNT];
    uint32_t indices[2][FILE_COUNT];
    uint32_t counts[2] = { 0, 0 };
    for (uint32_t i = 0; ready && i < FILE_COUNT; i += 1)
    {
        const uint32_t group = fixtures[i].char_unsigned ? 1 : 0;
        pointers[group][counts[group]] = paths[i];
        indices[group][counts[group]] = i;
        counts[group] += 1;
    }
    const char* signed_arguments[] = { "-std=gnu17" };
    const char* unsigned_arguments[] = { "-std=gnu17", "-funsigned-char" };
    for (uint32_t group = 0; ready && group < 2; group += 1)
    {
        ParseTestClang outcomes[FILE_COUNT];
        const bool ran = counts[group] == 0 ||
                         (group == 0 ? parse_test_clang(clang, signed_arguments, BUSTER_ARRAY_LENGTH(signed_arguments), pointers[group], counts[group], outcomes) :
                                       parse_test_clang(clang, unsigned_arguments, BUSTER_ARRAY_LENGTH(unsigned_arguments), pointers[group], counts[group], outcomes));
        check_test_expect(test, ran, "the bit builtins' Clang oracle ran");
        for (uint32_t k = 0; ran && k < counts[group]; k += 1)
        {
            const CheckSourceFixture* fixture = fixtures + indices[group][k];
            char description[192];
            snprintf(description, sizeof(description), "%s: expectation differs from Clang", fixture->name);
            check_test_expect(test, outcomes[k].rejected == (fixture->rejected && (!fixture->unsupported || fixture->undecided)), description);
            check_test_source(test, fixture, paths[indices[group][k]], &outcomes[k]);
        }
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

static __attribute__((noinline)) void check_test_bits_programs(CheckTest* test, const char* clang)
{
    check_test_program_source(test,
        "static const unsigned long long values[]={0,1,2,3,0x7f,0x80,0xff,0x100,0x7fff,0x8000,0xffff,0x12345678,0x7fffffff,0x80000000,0xffffffff,"
        "0x100000000ull,0x123456789abcdef0ull,0x7fffffffffffffffull,0x8000000000000000ull,0xffffffffffffffffull,0x5555555555555555ull,37};"
        "static unsigned long long h;static void mix(unsigned long long v){h=h*31+v;}"
        "int tested(int n){h=(unsigned)n;for(unsigned i=0;i<sizeof values/sizeof values[0];i++){"
        "unsigned long long v=values[i]^((unsigned long long)(n+3)<<(i%60));unsigned char c=v;unsigned short s=v;unsigned u=v;unsigned long l=v;_Bool b=v&1;"
        "mix(__builtin_popcountg(c));mix(__builtin_popcountg(s));mix(__builtin_popcountg(u));mix(__builtin_popcountg(l));mix(__builtin_popcountg(b));"
        "mix(__builtin_clzg(c,-1));mix(__builtin_ctzg(s,99));mix(__builtin_clzg(u,n));mix(__builtin_ctzg(l,-7));mix(__builtin_clzg(b,5));mix(__builtin_ctzg(b,6));"
        "if(u){mix(__builtin_clzg(u));mix(__builtin_ctzg(u));}if(s){mix(__builtin_clzs(s));mix(__builtin_ctzs(s));}"
        "mix(__builtin_bitreverse8(c));mix(__builtin_bitreverse16(s));mix(__builtin_bitreverse32(u));mix(__builtin_bitreverse64(l));"
        "mix(__builtin_clrsb((int)u));mix(__builtin_clrsbl((long)l));mix(__builtin_clrsbll((long long)l));"
        "mix((unsigned char)__builtin_bitreverseg((signed char)c));mix((unsigned short)__builtin_bitreverseg((short)s));mix(__builtin_bitreverseg(u));"
        "mix(__builtin_bitreverseg((long)l));mix(__builtin_bitreverseg(b));"
        "mix(__builtin_bswapg(c));mix(__builtin_bswapg(s));mix((unsigned)__builtin_bswapg((int)u));mix(__builtin_bswapg(l));mix(__builtin_bswapg(b));"
        "mix(__builtin_stdc_leading_zeros(c));mix(__builtin_stdc_leading_ones(s));mix(__builtin_stdc_trailing_zeros(u));mix(__builtin_stdc_trailing_ones(l));"
        "mix(__builtin_stdc_first_leading_zero(c));mix(__builtin_stdc_first_leading_one(s));mix(__builtin_stdc_first_trailing_zero(u));"
        "mix(__builtin_stdc_first_trailing_one(l));mix(__builtin_stdc_first_leading_zero(s));mix(__builtin_stdc_first_trailing_one(c));"
        "mix(__builtin_stdc_count_zeros(s));mix(__builtin_stdc_count_ones(l));mix(__builtin_stdc_count_zeros(c));mix(__builtin_stdc_has_single_bit(u));"
        "mix(__builtin_stdc_has_single_bit(c));mix(__builtin_stdc_bit_width(l));mix(__builtin_stdc_bit_width(s));"
        "mix(__builtin_stdc_bit_floor(s));mix(__builtin_stdc_bit_floor(l));mix(__builtin_stdc_bit_ceil(u));mix(__builtin_stdc_bit_ceil(l));"
        "mix(__builtin_stdc_bit_ceil(c));mix(__builtin_stdc_bit_floor(c));}"
        "return (int)(h^(h>>32));}",
        "bits-every-builtin-of-boundary-values", clang);
    check_test_program_source(test,
        "enum U{UA=1,UB=0x80000000u};enum S{SA=-1,SB=5};struct B{unsigned x:3;int z:5;unsigned long long w:40;};"
        "static const unsigned long long values[]={0,1,2,5,0x1f,0x10,0x7fffffffffull,0x1000000000ull,0x123456789aull,0xffffffffffull,0x8000000000ull};"
        "static unsigned long long h;static void mix(unsigned long long v){h=h*31+v;}"
        "int tested(int n){h=(unsigned)n;for(unsigned i=0;i<sizeof values/sizeof values[0];i++){"
        "unsigned long long v=values[i]+(unsigned long long)n*0x10001;unsigned _BitInt(37) w=(unsigned _BitInt(37))v;_BitInt(48) s=(_BitInt(48))v;"
        "unsigned _BitInt(5) f=(unsigned _BitInt(5))v;_BitInt(7) g=(_BitInt(7))v;enum U e=(enum U)(unsigned)v;_Bool b=v&1;"
        "struct B r={(unsigned)v,(int)v,v};"
        "mix(__builtin_popcountg(w));mix(__builtin_popcountg(f));mix(__builtin_popcountg(e));mix(__builtin_clzg(w,-2));mix(__builtin_ctzg(f,-3));"
        "mix(__builtin_ctzg(e,(short)-4));mix((unsigned long long)__builtin_bitreverseg(w));mix((unsigned long long)(long long)__builtin_bitreverseg(s));"
        "mix((unsigned)__builtin_bitreverseg(f));mix((unsigned)(int)__builtin_bitreverseg(g));mix((unsigned long long)(long long)__builtin_bswapg(s));"
        "mix((unsigned)__builtin_bswapg(e));mix(__builtin_stdc_leading_zeros(w));mix(__builtin_stdc_trailing_ones(f));"
        "mix(__builtin_stdc_first_leading_zero(w));mix((unsigned long long)__builtin_stdc_bit_floor(w));mix((unsigned long long)__builtin_stdc_bit_ceil(w));"
        "mix((unsigned)__builtin_stdc_bit_ceil(f));mix(__builtin_stdc_has_single_bit(f));mix(__builtin_stdc_count_zeros(w));"
        "mix(__builtin_popcountg(r.x));mix((unsigned)__builtin_bitreverseg(r.z));mix(__builtin_stdc_bit_width(r.w));mix(__builtin_clzg(r.w,r.z));}"
        "return (int)(h^(h>>32));}",
        "bits-bitint-enumeration-bool-and-bit-field-operands", clang);
    check_test_program_source(test,
        "enum S{SA=-1,SB=5};"
        "static const unsigned long long values[]={0,1,0x80,0x8001,0x12345678,0x80000001,0x123456789abcdef0ull,0xfedcba9876543210ull,0x1f,0x1ffffffffull};"
        "static unsigned long long h;static void mix(unsigned long long v){h=h*31+v;}"
        "int tested(int n){h=(unsigned)n;for(unsigned i=0;i<sizeof values/sizeof values[0];i++){"
        "unsigned long long v=values[i];unsigned char c=v;unsigned short s=v;unsigned u=v;unsigned long l=v;_Bool b=v&1;"
        "unsigned _BitInt(37) w=(unsigned _BitInt(37))v;unsigned _BitInt(5) f=(unsigned _BitInt(5))v;"
        "for(int k=-70;k<=70;k+=7){int m=k+n;"
        "mix(__builtin_stdc_rotate_left(c,m));mix(__builtin_stdc_rotate_right(s,(signed char)m));mix(__builtin_stdc_rotate_left(u,(long long)m));"
        "mix(__builtin_stdc_rotate_right(l,(unsigned)m));mix(__builtin_stdc_rotate_left(l,(short)m));mix(__builtin_stdc_rotate_right(b,m));"
        "mix((unsigned long long)__builtin_stdc_rotate_left(w,m));mix((unsigned)__builtin_stdc_rotate_right(f,(_BitInt(5))m));"
        "mix((unsigned long long)__builtin_stdc_rotate_right(w,(unsigned _BitInt(9))m));mix(__builtin_stdc_rotate_left(u,(enum S)m));"
        "mix(__builtin_stdc_rotate_left(c,(_Bool)m));mix(__builtin_stdc_rotate_right(u,(unsigned char)m));}}"
        "return (int)(h^(h>>32));}",
        "bits-rotations-by-counts-of-every-type", clang);
    check_test_program_source(test,
        "static const unsigned long long values[]={0,1,2,0x7f,0x80,0xff,0xffff,0x7fffffff,0xffffffff,0x8000000000000000ull,0xffffffffffffffffull,37};"
        "static unsigned long long h;static void mix(unsigned long long v){h=h*31+v;}"
        "int tested(int n){h=(unsigned)n;unsigned k=(unsigned)(n+3)%4;"
        "for(unsigned i=0;i<sizeof values/sizeof values[0];i++)for(unsigned j=0;j<sizeof values/sizeof values[0];j++){"
        "unsigned long long x=values[i],y=values[j];unsigned char cb=9;unsigned short cs=9;unsigned ci=9;unsigned long cl=9;unsigned long long cll=9;"
        "mix(__builtin_addcb(x,y,k,&cb));mix(cb);mix(__builtin_subcb(x,y,k,&cb));mix(cb);"
        "mix(__builtin_addcs(x,y,k,&cs));mix(cs);mix(__builtin_subcs(x,y,k,&cs));mix(cs);"
        "mix(__builtin_addc(x,y,k,&ci));mix(ci);mix(__builtin_subc(x,y,k,&ci));mix(ci);"
        "mix(__builtin_addcl(x,y,k*0x7fffffffffffffffull,&cl));mix(cl);mix(__builtin_subcl(x,y,k,&cl));mix(cl);"
        "mix(__builtin_addcll(x,y,k,&cll));mix(cll);mix(__builtin_subcll(x,y,y,&cll));mix(cll);}"
        "return (int)(h^(h>>32));}",
        "bits-carry-chains-of-every-width", clang);
    check_test_program_source(test,
        "static int order[8];static int counter;static unsigned next(unsigned v,int slot){order[slot]=++counter;return v;}"
        "static const int folded=__builtin_popcountg(0xffu)+__builtin_stdc_bit_ceil(5u)+__builtin_clzg(0u,3);"
        "enum{E=__builtin_ctzs(8)+__builtin_stdc_bit_width(5u)};int sized[__builtin_clzs(1)];"
        "int tested(int n){counter=0;unsigned carry=0;"
        "unsigned r=__builtin_addc(next(n,0),next(2,1),next(1,2),(order[3]=++counter,&carry));"
        "int z=__builtin_clzg(next(n&1,4),(int)next(9,5));unsigned w=__builtin_stdc_rotate_left(next(n,6),(int)next(3,7));"
        "return (int)(r*1000+carry*100)+z*7+(int)w+order[0]+order[1]*2+order[2]*4+order[3]*8+order[4]*16+order[5]*32+order[6]*64+order[7]*128+"
        "folded+E+(int)sizeof sized+(int)sizeof(__builtin_clzg(0u))+__builtin_constant_p(__builtin_bitreverse8(1))*1000;}",
        "bits-folds-and-operands-evaluated-once-in-order", clang);
    check_test_program_source(test,
        "static unsigned long long h;static void mix(unsigned long long v){h=h*31+v;}"
        "static void mix128(unsigned __int128 v){mix((unsigned long long)v);mix((unsigned long long)(v>>64));}"
        "static const unsigned long long seeds[]={0,1,3,0x8000000000000000ull,0xffffffffffffffffull,0x123456789abcdef0ull,0x5555555555555555ull,37};"
        "int tested(int n){h=(unsigned)n;for(unsigned i=0;i<sizeof seeds/sizeof seeds[0];i++)for(unsigned j=0;j<sizeof seeds/sizeof seeds[0];j++){"
        "unsigned __int128 u=(((unsigned __int128)seeds[i]<<64)|seeds[j])+(unsigned)(n+3);__int128 s=(__int128)u;"
        "unsigned _BitInt(100) a=(unsigned _BitInt(100))u;_BitInt(100) b=(_BitInt(100))u;unsigned _BitInt(80) c=(unsigned _BitInt(80))u;"
        "_BitInt(112) d=(_BitInt(112))u;unsigned _BitInt(65) e=(unsigned _BitInt(65))u;unsigned _BitInt(37) f=(unsigned _BitInt(37))u;"
        "mix(__builtin_popcountg(u));mix(__builtin_popcountg(a));mix(__builtin_popcountg(e));"
        "mix(__builtin_clzg(u,-1));mix(__builtin_ctzg(a,-2));mix(__builtin_clzg(c,-3));mix(__builtin_ctzg(e,-4));if(u){mix(__builtin_clzg(u));mix(__builtin_ctzg(u));}"
        "mix128(__builtin_bitreverseg(u));mix128((unsigned __int128)__builtin_bitreverseg(s));mix128((unsigned __int128)__builtin_bitreverseg(a));"
        "mix128((unsigned __int128)(__int128)__builtin_bitreverseg(b));mix128((unsigned __int128)__builtin_bitreverseg(e));"
        "mix128(__builtin_bswapg(u));mix128((unsigned __int128)__builtin_bswapg(c));mix128((unsigned __int128)(__int128)__builtin_bswapg(d));"
        "mix128((unsigned __int128)__builtin_bswapg(s));"
        "mix(__builtin_stdc_leading_zeros(u));mix(__builtin_stdc_leading_ones(a));mix(__builtin_stdc_trailing_zeros(c));mix(__builtin_stdc_trailing_ones(u));"
        "mix(__builtin_stdc_first_leading_zero(a));mix(__builtin_stdc_first_leading_one(u));mix(__builtin_stdc_first_trailing_zero(e));"
        "mix(__builtin_stdc_first_trailing_one(c));mix(__builtin_stdc_count_zeros(a));mix(__builtin_stdc_count_ones(u));mix(__builtin_stdc_has_single_bit(u));"
        "mix(__builtin_stdc_has_single_bit(e));mix(__builtin_stdc_bit_width(a));mix128(__builtin_stdc_bit_floor(u));"
        "mix128((unsigned __int128)__builtin_stdc_bit_floor(a));mix128(__builtin_stdc_bit_ceil(u));mix128((unsigned __int128)__builtin_stdc_bit_ceil(c));"
        "mix128((unsigned __int128)__builtin_stdc_bit_ceil(e));"
        "for(int k=-260;k<=260;k+=37){__int128 big=((__int128)k<<70)+k;"
        "mix128(__builtin_stdc_rotate_left(u,k));mix128((unsigned __int128)__builtin_stdc_rotate_right(a,(long long)k));"
        "mix128((unsigned __int128)__builtin_stdc_rotate_left(c,big));mix128((unsigned __int128)__builtin_stdc_rotate_right(e,(unsigned __int128)big));"
        "mix128(__builtin_stdc_rotate_left(u,(_BitInt(70))big));mix128(__builtin_stdc_rotate_right(u,(unsigned _BitInt(99))big));"
        "mix(__builtin_stdc_rotate_left((unsigned)u,big));mix(__builtin_stdc_rotate_right((unsigned char)u,(_BitInt(99))big));"
        "mix((unsigned long long)__builtin_stdc_rotate_left(f,big));mix((unsigned long long)__builtin_stdc_rotate_right(f,(unsigned __int128)big));"
        "mix(__builtin_stdc_rotate_left((unsigned short)u,(unsigned _BitInt(65))big));}}"
        "return (int)(h^(h>>32));}",
        "bits-wide-operands-results-and-counts", clang);
    // clang's code evaluates bitreverseg's argument twice, and bswapg's but of 1 or 8 bits (EmitScalarExpr, then
    // emitBuiltinWithOneOverloadedType again), repeating its effects; this compiler evaluates it once, as a call does:
    // its program agrees with clang's build of a model that evaluates each argument first, and differs from clang's build
    // of the program itself.
    static const char evaluated[] =
        "static int calls;static unsigned next(unsigned v){calls++;return v;}static unsigned short half(unsigned short v){calls++;return v;}"
        "int tested(int n){calls=0;unsigned a=__builtin_bitreverseg(next((unsigned)n));unsigned short b=__builtin_bswapg(half((unsigned short)(n*3)));"
        "_Bool c=__builtin_bitreverseg((_Bool)(next((unsigned)n)&1));unsigned char d=__builtin_bswapg((unsigned char)next((unsigned)n));"
        "return (int)(a>>16)+b+c+d+calls*100000;}";
    static const char model[] =
        "static int calls;static unsigned next(unsigned v){calls++;return v;}static unsigned short half(unsigned short v){calls++;return v;}"
        "int tested(int n){calls=0;unsigned x=next((unsigned)n);unsigned a=__builtin_bitreverseg(x);unsigned short y=half((unsigned short)(n*3));"
        "unsigned short b=__builtin_bswapg(y);_Bool z=(_Bool)(next((unsigned)n)&1);_Bool c=__builtin_bitreverseg(z);"
        "unsigned char w=(unsigned char)next((unsigned)n);unsigned char d=__builtin_bswapg(w);return (int)(a>>16)+b+c+d+calls*100000;}";
    check_test_program_against(test, evaluated, model, false, "bits-reversal-and-swap-arguments-evaluated-once", clang);
    check_test_program_against(test, evaluated, evaluated, true, "bits-clang-evaluates-reversal-and-swap-arguments-twice", clang);
}

static __attribute__((noinline)) void check_test_bits(CheckTest* test, const char* clang)
{
    check_test_bits_sources(test, clang);
    check_test_bits_programs(test, clang);
}

#endif
