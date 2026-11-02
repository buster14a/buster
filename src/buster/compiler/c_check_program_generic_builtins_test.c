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
        { .name = "generic: the unwinder's, the rounding mode's, the pointer's and the trap's leaves", .source =
          "#define T(e, t) _Static_assert(_Generic((e), t: 1, default: 0), #e);\n"
          "void f(void* p, int v)\n{\nT(__builtin_extend_pointer(p), unsigned long long) __builtin_unwind_init(); __builtin_init_dwarf_reg_size_table(p);\n"
          "__builtin_set_flt_rounds(v); __builtin_set_flt_rounds(3.0);\nif (v) __builtin_verbose_trap(\"category\", \"message\");\n"
          "if (v > 1) __builtin_verbose_trap(__func__, ((\"x\" \"y\")));\nif (v > 2) __builtin_verbose_trap((const char*)\"a\", u8\"b\");\n}\n" },
        { .name = "generic: verbose_trap of a variable", .source =
          "void f(const char* p)\n{\n__builtin_verbose_trap(p, \"x\");\n}\n", .rejected = true },
        { .name = "generic: verbose_trap of a null pointer", .source =
          "void f(void)\n{\n__builtin_verbose_trap(\"x\", (const char*)0);\n}\n", .rejected = true },
        { .name = "generic: verbose_trap of a dollar", .source =
          "void f(void)\n{\n__builtin_verbose_trap(\"a$b\", \"x\");\n}\n", .rejected = true },
        { .name = "generic: verbose_trap of a dollar under a cast", .source =
          "void f(void)\n{\n__builtin_verbose_trap(\"a\", (const char*)(\"x$\"));\n}\n", .rejected = true },
        { .name = "generic: verbose_trap of one argument", .source = "void f(void)\n{\n__builtin_verbose_trap(\"a\");\n}\n", .rejected = true },
        { .name = "generic: set_flt_rounds of nothing", .source = "void f(void)\n{\n__builtin_set_flt_rounds();\n}\n", .rejected = true },
        { .name = "generic: extend_pointer of an integer", .source =
          "unsigned long long f(long v)\n{\nreturn __builtin_extend_pointer(v);\n}\n", .rejected = true },
        { .name = "generic: the alloca variants', checks' and addresses' types", .source =
          "#define T(e, t) _Static_assert(_Generic((e), t: 1, default: 0), #e);\nstruct S { int a; } s; int g; void fn(void); int arr[3];\nint* pg = __builtin_addressof(g); void* ps = __builtin_function_start(fn); int (*pa)[3] = __builtin_addressof(arr);\nstruct S* pss = __builtin_addressof(s); int* pm = __builtin_addressof(s.a); void (*pf)(void) = __builtin_addressof(fn);\nvoid f(unsigned long n, int x, long y)\n{\nT(__builtin_alloca_uninitialized(n), void*) T(__builtin_alloca_with_align(n, 64), void*) T(__builtin_alloca_with_align_uninitialized(n, 8 * 4096), void*)\nT(__builtin_allow_runtime_check(\"x\"), _Bool) T(__builtin_allow_sanitize_check(\"address\"), _Bool) T(__builtin_annotation(y, \"a\"), long)\nT(__builtin_annotation((char)x, \"a\"), char) T(__builtin_addressof(x), int*) T(__builtin_function_start(f), void*)\nint* p = __builtin_addressof(x); (void)p; (void)__builtin_allow_sanitize_check(\"kernel-hwaddress\");\n}\n" },
        { .name = "generic: alloca_with_align of 24 bits", .source =
          "void* f(unsigned long n)\n{\nreturn __builtin_alloca_with_align(n, 24);\n}\n", .rejected = true },
        { .name = "generic: alloca_with_align of 4 bits", .source =
          "void* f(unsigned long n)\n{\nreturn __builtin_alloca_with_align(n, 4);\n}\n", .rejected = true },
        { .name = "generic: alloca_with_align of 0 bits", .source =
          "void* f(unsigned long n)\n{\nreturn __builtin_alloca_with_align(n, 0);\n}\n", .rejected = true },
        { .name = "generic: alloca_with_align of a variable", .source =
          "void* f(unsigned long n, unsigned long a)\n{\nreturn __builtin_alloca_with_align(n, a);\n}\n", .rejected = true },
        { .name = "generic: alloca_with_align past INT32_MAX", .source =
          "void* f(unsigned long n)\n{\nreturn __builtin_alloca_with_align(n, 1UL << 32);\n}\n", .rejected = true },
        { .name = "generic: alloca_with_align of one", .source =
          "void* f(unsigned long n)\n{\nreturn __builtin_alloca_with_align(n);\n}\n", .rejected = true },
        { .name = "generic: allow_runtime_check of a variable", .source =
          "int f(const char* p)\n{\nreturn __builtin_allow_runtime_check(p);\n}\n", .rejected = true },
        { .name = "generic: allow_runtime_check in a constant", .source =
          "int x = __builtin_allow_runtime_check(\"a\");\n", .rejected = true },
        { .name = "generic: allow_sanitize_check of undefined", .source =
          "int f(void)\n{\nreturn __builtin_allow_sanitize_check(\"undefined\");\n}\n", .rejected = true },
        { .name = "generic: annotation of a double", .source =
          "int f(double x)\n{\nreturn __builtin_annotation(x, \"a\");\n}\n", .rejected = true },
        { .name = "generic: annotation of a variable string", .source =
          "int f(int x, const char* p)\n{\nreturn __builtin_annotation(x, p);\n}\n", .rejected = true },
        { .name = "generic: annotation of a wide string", .source =
          "int f(int x)\n{\nreturn __builtin_annotation(x, L\"a\");\n}\n", .rejected = true },
        { .name = "generic: annotation in a constant", .source =
          "_Static_assert(__builtin_annotation(3, \"a\") == 3, \"\");\n", .rejected = true },
        { .name = "generic: function_start of an object", .source =
          "int g;\nvoid* f(void)\n{\nreturn __builtin_function_start(g);\n}\n", .rejected = true },
        { .name = "generic: function_start of a pointer", .source =
          "void* f(void (*q)(void))\n{\nreturn __builtin_function_start(q);\n}\n", .rejected = true },
        { .name = "generic: addressof of an rvalue", .source =
          "int* f(void)\n{\nreturn __builtin_addressof(3);\n}\n", .rejected = true },
        { .name = "generic: addressof of a bit-field", .source =
          "struct S { int b : 3; } s;\nint* f(void)\n{\nreturn __builtin_addressof(s.b);\n}\n", .rejected = true },
        { .name = "generic: addressof of a register", .source =
          "int f(int x)\n{\nregister int r = x;\nreturn *__builtin_addressof(r);\n}\n", .rejected = true },
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
    // The unwinder's table of DWARF registers' sizes, over a buffer's bytes; each rounding mode in the x87's control word
    // and MXCSR, and a division rounded by it; the pointer's value; the values unwind_init leaves; and verbose_trap's
    // SIGILL, in a child process.
    check_test_program_source(test,
        "int fork(void);int waitpid(int,int*,int);void _exit(int);"
        "static unsigned long long h;static void mix(unsigned long long v){h=h*31+v;}"
        "static unsigned cw(void){unsigned short w;__asm__ volatile(\"fnstcw %0\":\"=m\"(w));return w;}"
        "int tested(int n){h=(unsigned)n;unsigned char t[24];for(int i=0;i<24;i++)t[i]=(unsigned char)(n+i);"
        "__builtin_init_dwarf_reg_size_table(t);for(int i=0;i<24;i++)mix(t[i]);"
        "for(int v=0;v<4;v++){__builtin_set_flt_rounds((v+n)&3);mix(cw()&0xc00);mix(__builtin_ia32_stmxcsr()&0x6000);"
        "volatile double x=1.0,y=3.0;mix((unsigned long long)(x/y*1e17));}__builtin_set_flt_rounds(1);"
        "int x=n;unsigned long long e=__builtin_extend_pointer(&x);mix(e==(unsigned long long)&x);"
        "int a=n*7,b=n+3;__builtin_unwind_init();mix((unsigned)(a+b));"
        "int pid=fork();if(pid==0){__builtin_verbose_trap(\"category\",\"the message\");_exit(9);}"
        "int status=0;waitpid(pid,&status,0);mix((unsigned)status&0x7f);"
        "return (int)(h^(h>>32));}",
        "generic-rounding-unwinder-and-trap", clang);
    // The alloca variants' bytes and alignments, in a loop whose allocations each stay apart; the checks' values; the
    // annotation's integer, once evaluated; and addressof's and function_start's addresses, of a local, a member, an
    // array, a function, and in static data.
    check_test_program_source(test,
        "static unsigned long long h;static void mix(unsigned long long v){h=h*31+v;}struct S{int a,b;};static int g;"
        "static int callee(int x){return x*5+1;}static int* pg=__builtin_addressof(g);static void* ps=__builtin_function_start(callee);"
        "static int counter;static int bump(void){return ++counter;}"
        "int tested(int n){h=(unsigned)n;unsigned char* last=0;for(int i=0;i<4;i++){"
        "unsigned char* a=__builtin_alloca_uninitialized(n+i);unsigned char* b=__builtin_alloca_with_align(i+3,64);"
        "unsigned char* c=__builtin_alloca_with_align_uninitialized(5,8*4096);unsigned char* d=__builtin_alloca_with_align(7,8);"
        "for(int k=0;k<n+i;k++)a[k]=(unsigned char)(k+i);for(int k=0;k<i+3;k++)b[k]=(unsigned char)(k*3);for(int k=0;k<5;k++)c[k]=(unsigned char)k;"
        "for(int k=0;k<7;k++)d[k]=(unsigned char)(k^i);mix(((unsigned long)b&7)==0);mix(((unsigned long)c&4095)==0);mix(((unsigned long)d&7)==0);"
        "mix(last==0||(last!=a&&last!=b&&last!=c&&last!=d));last=a;unsigned s=0;for(int k=0;k<n+i;k++)s+=a[k];for(int k=0;k<i+3;k++)s+=b[k];"
        "for(int k=0;k<5;k++)s+=c[k];for(int k=0;k<7;k++)s+=d[k];mix(s);}"
        "mix(__builtin_allow_runtime_check(\"x\"));mix(__builtin_allow_sanitize_check(\"address\"));mix(__builtin_allow_sanitize_check(\"thread\"));"
        "counter=0;long y=__builtin_annotation((long)bump()*n,\"note\");mix((unsigned long long)y);mix((unsigned)counter);"
        "int x=n;int* px=__builtin_addressof(x);*px+=3;mix((unsigned)x);struct S st={n,2*n};int* pb=__builtin_addressof(st.b);mix((unsigned)*pb);"
        "int arr[4]={1,2,3,n};int (*pa)[4]=__builtin_addressof(arr);mix((unsigned)(*pa)[3]);mix(pg==&g);"
        "int (*f)(int)=__builtin_addressof(callee);mix((unsigned)f(n));mix(ps==(void*)callee);mix(__builtin_function_start(callee)==(void*)&callee);"
        "return (int)(h^(h>>32));}",
        "generic-alloca-checks-and-addresses", clang);
}

static __attribute__((noinline)) void check_test_generic_builtins(CheckTest* test, const char* clang)
{
    check_test_generic_builtin_sources(test, clang);
    check_test_generic_builtin_programs(test, clang);
}

#endif
