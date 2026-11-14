// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #148's alignment builtins and __builtin_expect_with_probability (c_check_builtin_misc.c, c_builtin_misc_lower.c) in
// test-checker: source fixtures of their result types, folds and refusals, each checked by both paths against clang
// -fsyntax-only, then programs run natively against clang -O0's build of the same source on both paths: align_up,
// align_down and is_aligned of every integer width, _BitInt (past 128 bits too, #1447), 128-bit and pointer sources, by
// alignments of other widths (one that truncates to 0 in a char among them), folded and at run time, of addresses in
// static data and frames; expect's
// and expect_with_probability's arguments evaluated once, in order, or not at all where the first argument folds, as
// clang's code does; and the generic builtins of c_check_program_generic_builtins_test.c, which runs with them. Include after
// c_check_program_bits_test.c and c_check_program_generic_builtins_test.c.
#ifndef BUSTER_C_CHECK_PROGRAM_ALIGN_TEST_INCLUDED
#define BUSTER_C_CHECK_PROGRAM_ALIGN_TEST_INCLUDED

static __attribute__((noinline)) void check_test_align_sources(CheckTest* test, const char* clang)
{
    // Its source fixtures are runtime data (#2053): tests/checker/align, which check_test_suite reads and runs.
    check_test_suite(test, clang, "checker/align", "the alignment builtins' Clang oracle ran");
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
    // _BitInts past 128 bits (#1447) as sources, limb by limb (check_lower_align_limbs), signed and unsigned, of 129, 200 and
    // 256 bits, by alignments of every width up to 128 bits (a wider one is refused by name); the results' bytes hashed.
    check_test_program_source(test,
        "typedef unsigned _BitInt(200) U;typedef _BitInt(200) S;typedef unsigned _BitInt(256) W;typedef _BitInt(129) T;typedef unsigned __int128 Q;\n"
        "static unsigned long long h;static void mix(unsigned long long v){h=h*31+v;}\n"
        "static void raw(const void*p,unsigned n){unsigned long long w[8];__builtin_memcpy(w,p,n);for(unsigned i=0;i<n/8;i++)mix(w[i]);}\n"
        "int tested(int n){h=(unsigned)n;unsigned long long s=0x9e3779b97f4a7c15ull*(unsigned)(n+1);\n"
        "for(unsigned i=0;i<300;i++){s^=s<<13;s^=s>>7;s^=s<<17;\n"
        "W w=((W)s<<(s%200))^s^(i%5==0?~(W)0:0);U x=(U)w;S y=(S)w;T t=(T)w;\n"
        "Q a=(Q)1<<(s%128);int ai=1<<(i%31);long al=1L<<(i%63);unsigned char ac=(unsigned char)(1u<<(i%8));\n"
        "U r=__builtin_align_up(x,ai);raw(&r,32);r=__builtin_align_down(x,al);raw(&r,32);mix(__builtin_is_aligned(x,a));\n"
        "S q=__builtin_align_up(y,a);raw(&q,32);q=__builtin_align_down(y,ac);raw(&q,32);mix(__builtin_is_aligned(y,ai));\n"
        "W v=__builtin_align_up(w,a);raw(&v,32);v=__builtin_align_down(w,al);raw(&v,32);mix(__builtin_is_aligned(w,ac));\n"
        "T z=__builtin_align_up(t,a);raw(&z,24);z=__builtin_align_down(t,ai);raw(&z,24);mix(__builtin_is_aligned(t,al));\n"
        "r=__builtin_align_up(x,(Q)1<<127);raw(&r,32);v=__builtin_align_up(w,16);raw(&v,32);\n"
        "}return (int)(h^(h>>32));}",
        "align-bit-int-sources-past-128-bits", clang);
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
    check_test_generic_builtins(test, clang);
}

#endif
