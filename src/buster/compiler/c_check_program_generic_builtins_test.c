// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #148's generic builtins of an instruction or two and of clang's own Sema checks in test-checker, beside the alignment
// builtins (check_test_align): source fixtures of __builtin_classify_type's classes, __builtin_eh_return_data_regno's
// registers, __builtin_launder's and __builtin_dwarf_cfa's types and the leaves' prototypes (c_builtin_x86_leaves.inc),
// with their refusals, each checked by both paths against clang -fsyntax-only; then programs run natively against clang
// -O0's build of the same source on both paths: the folds, launder's pointer, the CFA against the caller's stack address,
// and the leaves (debugtrap's trap handled, the counters, the thread and stack pointers, the no-ops' arguments evaluated
// once in order, memcpy_inline's and memset_inline's bytes, and the alloca variants, the checks, the annotation and the
// addresses). Include before c_check_program_align_test.c.
#ifndef BUSTER_C_CHECK_PROGRAM_GENERIC_BUILTINS_TEST_INCLUDED
#define BUSTER_C_CHECK_PROGRAM_GENERIC_BUILTINS_TEST_INCLUDED

static __attribute__((noinline)) void check_test_generic_builtin_sources(CheckTest* test, const char* clang)
{
    // Its source fixtures are runtime data (#2053): tests/checker/generic-builtins, which check_test_suite reads and runs.
    check_test_suite(test, clang, "checker/generic-builtins", "the generic builtins' Clang oracle ran");
}

static __attribute__((noinline)) void check_test_generic_builtin_programs(CheckTest* test, const char* clang)
{
    // The folds at run time and in static data, launder's pointers, and the CFA of a callee, which is the caller's stack
    // address at the call.
    check_test_program_suite(test, "checker/programs/generic-folds-launder-and-the-cfa", clang);
    const bool masks_native = (x86_target_native().features & BUSTER_X86_FEATURE(AVX512F)) != 0;
    if (!masks_native)
    {
        printf("test-checker: generic-setjmp-held-mask: AVX-512F unavailable; checking compile/link without native execution\n");
    }
    check_test_program_source_native(test,
        "static void* buf[5];static volatile unsigned short msink;static unsigned long long h;static void mix(unsigned long long v){h=h*31+v;}\n"
        "__attribute__((noinline)) static void jump(void){__builtin_longjmp(buf,1);}__attribute__((noinline)) static int side(int x){return x+5;}\n"
        "__attribute__((noinline,target(\"avx512f\"))) static int mrun(int k){int r=(int)__builtin_ia32_kandhi((unsigned short)(k*0x1111+1),(unsigned short)(0xff00|__builtin_setjmp(buf)*0xff));\n"
        "if(r==((k*0x1111+1)&0xff00)){msink=__builtin_ia32_kandhi((unsigned short)side(3),(unsigned short)(0xf0f0|side(9)));jump();}return r;}\n"
        "int tested(int n){h=(unsigned)n;int k=(n&7)+1;mix((unsigned)mrun(k));mix(msink);return (int)(h^(h>>32));}\n",
        "generic-setjmp-held-mask", clang, masks_native);
    // Builtins called through their names in parentheses (#1242's review), whose arguments run once.
    check_test_program_suite(test, "checker/programs/generic-builtin-names-in-parentheses", clang);
    // __builtin_clear_padding past what the plan once refused (#1842): an element of 2.2 million runs (an array of 1.1
    // million records inside one), arrays inside the arrays of a record's member, a union's array, whose members' bits
    // overlap, and an element past 4 GiB (each mmap'd, the last's ends alone touched). Clang's walk takes every element of
    // every array, which takes it too long, so its build is of the expected bytes, cleared by the predicates the check reads.
    check_test_program_against(test,
        "#define CLEAR(p,pad) __builtin_clear_padding(p)\n"
        "void *memset(void *, int, unsigned long);void *mmap(void *, unsigned long, int, int, int, long);int munmap(void *, unsigned long);\n"
        "struct P{char c;int i;};struct W{char h;struct P a[1100000];char z;};struct Q{char c;struct{short s;struct P p[3];}in[400000];};\n"
        "union V{struct P a[100000];char b[5];};struct Big{char c;long x[536870912];char t;};\n"
        "static int pad_p(unsigned long k){return k%8>=1&&k%8<=3;}\n"
        "static int pad_w(unsigned long i){unsigned long a=__builtin_offsetof(struct W,a);\n"
        "return i>=a&&i<a+sizeof(struct P)*1100000?pad_p(i-a):i!=0&&i!=__builtin_offsetof(struct W,z);}\n"
        "static int pad_q(unsigned long i){unsigned long a=__builtin_offsetof(struct Q,in);unsigned long e=(i-a)%28;return i<a?i!=0:e<2?0:e<4?1:pad_p(e-4);}\n"
        "static int pad_u(unsigned long i){return i>=5&&pad_p(i);}\n"
        "static int pad_big(unsigned long i){return (i>=1&&i<8)||i>sizeof(struct Big)-8;}\n"
        "static int bad;static void check(const unsigned char* p,unsigned long first,unsigned long end,int (*pad)(unsigned long))\n"
        "{for(unsigned long i=first;i<end;i++)bad+=p[i]!=(pad(i)?0:0xff);}\n"
        "static void* map(unsigned long n){return mmap(0,n,3,0x4022,-1,0);}\n"
        "int tested(int n){bad=0;struct W* w=map(sizeof *w);struct Q* q=map(sizeof *q);union V* u=map(sizeof *u);struct Big* g=map(sizeof *g);\n"
        "if(w==(void*)-1||q==(void*)-1||u==(void*)-1||g==(void*)-1)return -1;unsigned char* gb=(unsigned char*)g;\n"
        "memset(w,0xff,sizeof *w);memset(q,0xff,sizeof *q);memset(u,0xff,sizeof *u);memset(gb,0xff,4096);memset(gb+sizeof *g-4096,0xff,4096);\n"
        "CLEAR(w,pad_w);CLEAR(q,pad_q);CLEAR(u,pad_u);CLEAR(g,pad_big);\n"
        "check((const unsigned char*)w,0,sizeof *w,pad_w);check((const unsigned char*)q,0,sizeof *q,pad_q);check((const unsigned char*)u,0,sizeof *u,pad_u);\n"
        "check(gb,0,4096,pad_big);check(gb,sizeof *g-4096,sizeof *g,pad_big);\n"
        "munmap(w,sizeof *w);munmap(q,sizeof *q);munmap(u,sizeof *u);munmap(g,sizeof *g);return bad*1000+(n&7);}\n",
        "#define CLEAR(p,pad) do{unsigned char* b_=(unsigned char*)(p);unsigned long n_=sizeof *(p);\\\n"
        "for(unsigned long i_=0;i_<n_;i_=n_>(1ul<<32)&&i_+1==4096?n_-4096:i_+1)if(pad(i_))b_[i_]=0;}while(0)\n"
        "void *memset(void *, int, unsigned long);void *mmap(void *, unsigned long, int, int, int, long);int munmap(void *, unsigned long);\n"
        "struct P{char c;int i;};struct W{char h;struct P a[1100000];char z;};struct Q{char c;struct{short s;struct P p[3];}in[400000];};\n"
        "union V{struct P a[100000];char b[5];};struct Big{char c;long x[536870912];char t;};\n"
        "static int pad_p(unsigned long k){return k%8>=1&&k%8<=3;}\n"
        "static int pad_w(unsigned long i){unsigned long a=__builtin_offsetof(struct W,a);\n"
        "return i>=a&&i<a+sizeof(struct P)*1100000?pad_p(i-a):i!=0&&i!=__builtin_offsetof(struct W,z);}\n"
        "static int pad_q(unsigned long i){unsigned long a=__builtin_offsetof(struct Q,in);unsigned long e=(i-a)%28;return i<a?i!=0:e<2?0:e<4?1:pad_p(e-4);}\n"
        "static int pad_u(unsigned long i){return i>=5&&pad_p(i);}\n"
        "static int pad_big(unsigned long i){return (i>=1&&i<8)||i>sizeof(struct Big)-8;}\n"
        "static int bad;static void check(const unsigned char* p,unsigned long first,unsigned long end,int (*pad)(unsigned long))\n"
        "{for(unsigned long i=first;i<end;i++)bad+=p[i]!=(pad(i)?0:0xff);}\n"
        "static void* map(unsigned long n){return mmap(0,n,3,0x4022,-1,0);}\n"
        "int tested(int n){bad=0;struct W* w=map(sizeof *w);struct Q* q=map(sizeof *q);union V* u=map(sizeof *u);struct Big* g=map(sizeof *g);\n"
        "if(w==(void*)-1||q==(void*)-1||u==(void*)-1||g==(void*)-1)return -1;unsigned char* gb=(unsigned char*)g;\n"
        "memset(w,0xff,sizeof *w);memset(q,0xff,sizeof *q);memset(u,0xff,sizeof *u);memset(gb,0xff,4096);memset(gb+sizeof *g-4096,0xff,4096);\n"
        "CLEAR(w,pad_w);CLEAR(q,pad_q);CLEAR(u,pad_u);CLEAR(g,pad_big);\n"
        "check((const unsigned char*)w,0,sizeof *w,pad_w);check((const unsigned char*)q,0,sizeof *q,pad_q);check((const unsigned char*)u,0,sizeof *u,pad_u);\n"
        "check(gb,0,4096,pad_big);check(gb,sizeof *g-4096,sizeof *g,pad_big);\n"
        "munmap(w,sizeof *w);munmap(q,sizeof *q);munmap(u,sizeof *u);munmap(g,sizeof *g);return bad*1000+(n&7);}\n",
        false, "generic-clear-padding-repeats", clang);
    // va_arg of a Microsoft list (#148, clang's EmitMSVAArg) against clang's build: 8-byte slots built by hand, read as an
    // int, a float and a double, structs of 3, 4, 8 and 16 bytes (the 3- and 16-byte ones through their slots' pointers),
    // a long double, a _Complex double and an __int128 through theirs, then __builtin_ms_va_copy into a list through a
    // pointer, __builtin_ms_va_end, and va_arg through that pointer and of the copy, whose place is the last value.
    check_test_program_suite(test, "checker/programs/generic-microsoft-va-arg", clang);
}

// __builtin_cpu_init with libgcc's model, of this host, against clang's build (the table's every name is
// test-checker-driver's, c_check_library_driver_test.c's check_test_cpu_model_runs).
static __attribute__((noinline)) void check_test_generic_cpu_programs(CheckTest* test, const char* clang)
{
    check_test_program_suite(test, "checker/programs/generic-cpu-programs", clang);
}

// __builtin_clear_padding's repeats (#1842, c_check_program_padding.c): unrolled (check_lower_padding_unroll), a table of
// a plain entry and a repeated one, three instances of 8 bytes in each of four combinations of two outer pairs, comes out
// as 13 plain entries sorted by offset, the table loop's offsets; with 100 instances (past the inline cost) and with no
// repeats, it stays as it is; an unrolled run of zeros joins the next. In place (check_lower_padding_cost), each shared
// repeat costs one nest of loops, and a repeat past the nest's levels is left to the table.
static __attribute__((noinline)) void check_test_generic_clear_padding_unroll(CheckTest* test)
{
    _Alignas(64) static uint8_t buffer[4096];
    Arena scratch = { .base = buffer, .length = 0, .capacity = sizeof(buffer) };
    const uint64_t r = BUSTER_CBUILTIN_PADDING_REPEATED;
    uint64_t table[] = { 1, 0, 1, 0, 0, 0, 40, 3 << 8, 4 | r, 2 << 8, 3, 8, 4, 16, 2, 0, 2, 100, 2, 1000 };
    static const uint64_t offsets[] = { 4, 12, 20, 40, 104, 112, 120, 1004, 1012, 1020, 1104, 1112, 1120 };
    uint64_t* result = NULL;
    uint32_t count = 0;
    const bool unrolled = check_lower_padding_unroll(table, 2, &scratch, &result, &count);
    bool exact = unrolled && count == sizeof(offsets) / sizeof(offsets[0]) && memcmp(result, table, BUSTER_CBUILTIN_PADDING_HEADER * sizeof(*table)) == 0;
    for (uint32_t i = 0; exact && i < count; i += 1)
    {
        const uint64_t* entry = result + BUSTER_CBUILTIN_PADDING_HEADER + i * BUSTER_CBUILTIN_PADDING_PLAIN;
        exact = entry[0] == offsets[i] && entry[1] == (offsets[i] == 40 ? 3u << 8 : 2u << 8);
    }
    check_test_expect(test, exact, "clear_padding: a table's few repeats unroll into plain entries, sorted, at the loop's offsets");
    table[10] = 100;
    uint64_t* untouched = NULL;
    const bool large = check_lower_padding_unroll(table, 2, &scratch, &untouched, &count);
    const bool plain = check_lower_padding_unroll(table, 1, &scratch, &untouched, &count);
    check_test_expect(test, !large && !plain && untouched == NULL, "clear_padding: many repeats, or none, keep the table");
    // An inner array's last run of zeros and its parent's after it, joined: 2 instances of 2 bytes at 6, by 8, then 4 at 16.
    uint64_t joined[] = { 1, 0, 1, 0, 0, 0, 6 | r, 2 << 8, 2, 8, 1, 0, 0, 0, 16, 4 << 8 };
    const bool unrolled_joined = check_lower_padding_unroll(joined, 2, &scratch, &result, &count);
    check_test_expect(test, unrolled_joined && count == 2 && result[6] == 6 && result[7] == 2 << 8 && result[8] == 14 && result[9] == 6 << 8,
                      "clear_padding: an unrolled repeat's last run of zeros joins the run after it");
    // In place, a run of 3 bytes costs 12 typed entries, of 2 bytes 6, and a repeat's nest of loops
    // BUSTER_CBUILTIN_PADDING_LOOP a level: the table's plain entry and its repeat of three levels; two entries sharing a
    // repeat of one level, one nest; not sharing it, two; a repeat past the nest's levels, the table's alone.
    const uint64_t levels = check_lower_padding_cost(table, 2);
    uint64_t pair_of[] = { 1, 0, 1, 0, 0, 0, 1 | r, 3 << 8, 64, 12, 1, 0, 0, 0, 10 | r, 2 << 8, 64, 12, 1, 0, 0, 0 };
    const uint64_t shared = check_lower_padding_cost(pair_of, 2);
    pair_of[16] = 32;
    const uint64_t apart = check_lower_padding_cost(pair_of, 2);
    table[14] = BUSTER_CBUILTIN_PADDING_LEVELS;
    const uint64_t deep = check_lower_padding_cost(table, 2);
    check_test_expect(test, levels == 12 + 6 + 3 * BUSTER_CBUILTIN_PADDING_LOOP && shared == 12 + 6 + BUSTER_CBUILTIN_PADDING_LOOP &&
                           apart == 12 + 6 + 2 * BUSTER_CBUILTIN_PADDING_LOOP && deep == UINT64_MAX,
                      "clear_padding: in place, a shared repeat's loops cost one nest, and a repeat past its levels can't be");
}

static __attribute__((noinline)) void check_test_generic_builtins(CheckTest* test, const char* clang)
{
    check_test_generic_clear_padding_unroll(test);
    check_test_generic_builtin_sources(test, clang);
    check_test_generic_builtin_programs(test, clang);
    check_test_generic_cpu_programs(test, clang);
}

#endif
