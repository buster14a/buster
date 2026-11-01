// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #148's generic builtins of an instruction or two and of clang's own Sema checks in test-checker, beside the alignment
// builtins (check_test_align): source fixtures of __builtin_classify_type's classes, __builtin_eh_return_data_regno's
// registers, __builtin_launder's and __builtin_dwarf_cfa's types and the leaves' prototypes (c_builtin_x86_leaves.inc),
// with their refusals, each checked by both paths against clang -fsyntax-only; then programs run natively against clang
// -O0's build of the same source on both paths: the folds, launder's pointer, the CFA against the caller's stack address,
// and the leaves (debugtrap's trap handled, the counters, the thread and stack pointers, the no-ops' arguments evaluated
// once in order, and memcpy_inline's and memset_inline's bytes). Include before c_check_program_align_test.c.
#ifndef BUSTER_C_CHECK_PROGRAM_GENERIC_BUILTINS_TEST_INCLUDED
#define BUSTER_C_CHECK_PROGRAM_GENERIC_BUILTINS_TEST_INCLUDED

static __attribute__((noinline)) void check_test_generic_builtin_sources(CheckTest* test, const char* clang)
{
    static const CheckSourceFixture fixtures[] =
    {
        { .name = "generic: classify_type's classes", .source =
          "struct S { int a; }; union U { int a; }; enum E { X }; struct I; extern struct I inc;\n"
          "int arr[3]; void fn(void); int g; _Atomic int ai; _Alignas(16) const volatile char cv;\n"
          "#define C(e, v) _Static_assert(__builtin_classify_type(e) == (v), #e);\n"
          "C(0, 1) C((char)0, 1) C((_Bool)0, 4) C(X, 1) C((enum E)0, 1) C((int*)0, 5) C(1.0, 8) C(1.0f, 8) C(1.0L, 8) C((__float128)1, 8)\n"
          "C((_Float16)1, 8) C((_Complex double)0, 9) C((_Complex int)0, 9) C(fn, 5) C(&fn, 5) C((struct S){0}, 12) C((union U){0}, 13)\n"
          "C(arr, 5) C(\"ab\", 5) C((void)0, 0) C((_BitInt(7))0, 18) C((__attribute__((vector_size(16))) int){0}, 19) C((__int128)0, 1)\n"
          "C(inc, 12) C(g++, 1) C(ai, 1) C(cv, 1) C(0UL, 1)\n"
          "static int sizes[__builtin_classify_type(1.0)];\nenum { K = __builtin_classify_type((struct S){0}) };\n" },
        { .name = "generic: eh_return_data_regno's registers", .source =
          "_Static_assert(__builtin_eh_return_data_regno(0) == 0 && __builtin_eh_return_data_regno(1) == 1, \"\");\n"
          "_Static_assert(__builtin_eh_return_data_regno(2) == -1 && __builtin_eh_return_data_regno(-1) == -1, \"\");\n"
          "_Static_assert(__builtin_eh_return_data_regno(1L << 32) == 0 && __builtin_eh_return_data_regno('\\1') == 1, \"\");\n"
          "int r = __builtin_eh_return_data_regno(sizeof(char));\n" },
        { .name = "generic: launder's and dwarf_cfa's types", .source =
          "#define T(e, t) _Static_assert(_Generic((e), t: 1, default: 0), #e);\n"
          "struct S { int a; }; int g; struct S* sp; const int* cp; int a[4];\n"
          "int* laundered = __builtin_launder(&g);\n"
          "void f(void)\n{\nT(__builtin_launder(&g), int*) T(__builtin_launder(sp), struct S*) T(__builtin_launder(cp), const int*)\n"
          "T(__builtin_launder(a), int*) T(__builtin_dwarf_cfa(), void*)\n}\n" },
        { .name = "generic: the leaves' prototypes", .source =
          "#define T(e, t) _Static_assert(_Generic((e), t: 1, default: 0), #e);\n"
          "void f(char* x, void* p, const void* q, int v)\n{\n"
          "T(__builtin_readcyclecounter(), unsigned long long) T(__builtin_readsteadycounter(), unsigned long long)\n"
          "T(__builtin_thread_pointer(), void*) T(__builtin_stack_address(), void*) T(__builtin_dwarf_sp_column(), unsigned int)\n"
          "__builtin_debugtrap(); __builtin___clear_cache(x, x + 4); __builtin_assume_separate_storage(p, q);\n"
          "__builtin_assume_dereferenceable(q, 8); __builtin_memcpy_inline(p, q, 13); __builtin_memset_inline(p, v, sizeof(int) * 2);\n}\n" },
        { .name = "generic: eh_return_data_regno of a variable", .source = "int f(int n)\n{\nreturn __builtin_eh_return_data_regno(n);\n}\n", .rejected = true },
        { .name = "generic: eh_return_data_regno of a double", .source = "int f(void)\n{\nreturn __builtin_eh_return_data_regno(1.0);\n}\n", .rejected = true },
        { .name = "generic: classify_type of nothing", .source = "int f(void)\n{\nreturn __builtin_classify_type();\n}\n", .rejected = true },
        { .name = "generic: classify_type of two", .source = "int f(void)\n{\nreturn __builtin_classify_type(1, 2);\n}\n", .rejected = true },
        { .name = "generic: launder of a void pointer", .source = "void* f(void* p)\n{\nreturn __builtin_launder(p);\n}\n", .rejected = true },
        { .name = "generic: launder of a function pointer", .source =
          "typedef void F(void);\nF* f(F* p)\n{\nreturn __builtin_launder(p);\n}\n", .rejected = true },
        { .name = "generic: launder of an integer", .source = "long f(long p)\n{\nreturn (long)__builtin_launder(p);\n}\n", .rejected = true },
        { .name = "generic: launder of an incomplete type", .source = "struct I;\nstruct I* f(struct I* p)\n{\nreturn __builtin_launder(p);\n}\n",
          .rejected = true },
        { .name = "generic: launder of two", .source = "int* f(int* p)\n{\nreturn __builtin_launder(p, p);\n}\n", .rejected = true },
        { .name = "generic: dwarf_cfa of a level", .source = "void* f(void)\n{\nreturn __builtin_dwarf_cfa(1);\n}\n", .rejected = true },
        { .name = "generic: memcpy_inline of a variable size", .source =
          "void f(void* p, const void* q, unsigned long n)\n{\n__builtin_memcpy_inline(p, q, n);\n}\n", .rejected = true },
        { .name = "generic: memset_inline of a variable size", .source =
          "void f(void* p, unsigned long n)\n{\n__builtin_memset_inline(p, 0, n);\n}\n", .rejected = true },
        { .name = "generic: assume_dereferenceable of a variable size", .source =
          "void f(const void* p, unsigned long n)\n{\n__builtin_assume_dereferenceable(p, n);\n}\n" },
        { .name = "generic: debugtrap of an argument", .source = "void f(void)\n{\n__builtin_debugtrap(1);\n}\n", .rejected = true },
        { .name = "generic: clear_cache of one", .source = "void f(char* x)\n{\n__builtin___clear_cache(x);\n}\n", .rejected = true },
    };
    enum { FILE_COUNT = BUSTER_ARRAY_LENGTH(fixtures) };
    char directory[] = "/tmp/buster-checker-generic-XXXXXX";
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
    check_test_expect(test, ran, "the generic builtins' Clang oracle ran");
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

static __attribute__((noinline)) void check_test_generic_builtin_programs(CheckTest* test, const char* clang)
{
    // The folds at run time and in static data, launder's pointers, and the CFA of a callee, which is the caller's stack
    // address at the call.
    check_test_program_source(test,
        "static unsigned long long h;static void mix(unsigned long long v){h=h*31+v;}struct S{int a;};union U{int a;};int g;"
        "static int classes[]={__builtin_classify_type(0),__builtin_classify_type((_Bool)0),__builtin_classify_type((int*)0),"
        "__builtin_classify_type(1.0),__builtin_classify_type((_Complex float)0),__builtin_classify_type((struct S){0}),"
        "__builtin_classify_type((union U){0}),__builtin_classify_type((void)0),__builtin_classify_type((_BitInt(9))0),"
        "__builtin_eh_return_data_regno(0),__builtin_eh_return_data_regno(1),__builtin_eh_return_data_regno(7)};"
        "static int* laundered=__builtin_launder(&g);"
        "static __attribute__((noinline)) char* cfa(void){return __builtin_dwarf_cfa();}"
        "int tested(int n){h=(unsigned)n;for(unsigned i=0;i<sizeof classes/sizeof classes[0];i++)mix((unsigned)classes[i]);"
        "int k=n;mix((unsigned)__builtin_classify_type(k++));mix((unsigned)k);int x=n*3;int* p=__builtin_launder(&x);const int* q=__builtin_launder((const int*)p);"
        "mix((unsigned)(*p+*q));mix(laundered==&g);char* s=__builtin_stack_address();char* c=cfa();mix((unsigned long long)(c-s));"
        "return (int)(h^(h>>32));}",
        "generic-folds-launder-and-the-cfa", clang);
    // The leaves: debugtrap's trap, which a SIGTRAP handler counts and returns past; the cycle counter, which doesn't go
    // back, and the steady one's 0; the thread pointer, which glibc's TCB holds at its own address; the stack address,
    // below a local; rsp's DWARF register; the no-ops' arguments, evaluated once, in order; and the inline copy's and fill's
    // bytes.
    check_test_program_source(test,
        "typedef void (*H)(int);H signal(int,H);static volatile int trapped;static void on_trap(int s){trapped+=s;}"
        "static unsigned long long h;static void mix(unsigned long long v){h=h*31+v;}static int order;static char* at(char* p,int t){order=order*10+t;return p;}"
        "int tested(int n){h=(unsigned)n;signal(5,on_trap);__builtin_debugtrap();__builtin_debugtrap();mix((unsigned)trapped);"
        "unsigned long long a=__builtin_readcyclecounter(),b=__builtin_readcyclecounter();mix(b>=a);mix(__builtin_readsteadycounter());"
        "void* tp=__builtin_thread_pointer();mix(tp==*(void**)tp);char local;long d=&local-(char*)__builtin_stack_address();mix(d>=0&&d<4096);"
        "mix(__builtin_dwarf_sp_column());char s[32]=\"abcdefghijklmnopqrstuvwxyz\",t[32]={0};"
        "__builtin___clear_cache(at(s,1),at(s+4,2));__builtin_assume_separate_storage(at(s,3),at(t,4));__builtin_assume_dereferenceable(at(s+1,5),8);"
        "mix((unsigned)order);__builtin_memcpy_inline(t+(n&3),s+1,13);__builtin_memset_inline(s+2,'A'+(n&7),5);__builtin_memcpy_inline(t+20,s,0);"
        "for(int i=0;i<32;i++){mix((unsigned char)s[i]);mix((unsigned char)t[i]);}"
        "return (int)(h^(h>>32));}",
        "generic-leaves", clang);
}

static __attribute__((noinline)) void check_test_generic_builtins(CheckTest* test, const char* clang)
{
    check_test_generic_builtin_sources(test, clang);
    check_test_generic_builtin_programs(test, clang);
}

#endif
