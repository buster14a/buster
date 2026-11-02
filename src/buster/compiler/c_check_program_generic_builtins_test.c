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
        { .name = "generic: vectorelements of an integer", .source = "unsigned long f(int n)\n{\nreturn __builtin_vectorelements(n);\n}\n", .rejected = true },
        { .name = "generic: vectorelements of an integer type", .source = "unsigned long f(void)\n{\nreturn __builtin_vectorelements(int);\n}\n", .rejected = true },
        { .name = "generic: LINE of an argument", .source = "unsigned f(void)\n{\nreturn __builtin_LINE(1);\n}\n", .rejected = true },
        { .name = "generic: complex of an int", .source = "_Complex double f(void)\n{\nreturn __builtin_complex(1, 2.0);\n}\n", .rejected = true },
        { .name = "generic: complex of an int second", .source = "_Complex double f(double x)\n{\nreturn __builtin_complex(x, 2);\n}\n", .rejected = true },
        { .name = "generic: complex of two types", .source = "_Complex double f(void)\n{\nreturn __builtin_complex(1.0f, 2.0);\n}\n", .rejected = true },
        { .name = "generic: complex of a complex", .source =
          "_Complex double f(_Complex double z)\n{\nreturn __builtin_complex(z, z);\n}\n", .rejected = true },
        { .name = "generic: complex of an atomic", .source =
          "_Atomic double a;\n_Complex double f(void)\n{\nreturn __builtin_complex(a, 1.0);\n}\n", .rejected = true },
        { .name = "generic: complex of one", .source = "_Complex double f(void)\n{\nreturn __builtin_complex(1.0);\n}\n", .rejected = true },
        { .name = "generic: complex's address", .source = "void* p = &__builtin_complex(1.0, 2.0);\n", .rejected = true },
        { .name = "generic: complex of variables in static data", .source =
          "double x;\n_Complex double z = __builtin_complex(x, 2.0);\n", .rejected = true },
        { .name = "generic: complex of a const object in static data", .source =
          "static const double c = 1.0;\n_Complex double z = __builtin_complex(c, 2.0);\n" },
        { .name = "generic: complex in a static assertion", .source = "_Static_assert(__builtin_complex(1.0, 2.0) == 1.0 + 2.0i, \"\");\n" },
        { .name = "generic: fence of an int", .source = "int f(int x)\n{\nreturn __arithmetic_fence(x);\n}\n", .rejected = true },
        { .name = "generic: fence of a pointer", .source = "double* f(double* x)\n{\nreturn __arithmetic_fence(x);\n}\n", .rejected = true },
        { .name = "generic: fence of an atomic", .source = "_Atomic double a;\ndouble f(void)\n{\nreturn __arithmetic_fence(a);\n}\n", .rejected = true },
        { .name = "generic: fence of a complex integer", .source = "_Complex int f(_Complex int z)\n{\nreturn __arithmetic_fence(z);\n}\n", .rejected = true },
        { .name = "generic: fence of an integer vector", .source = "typedef int V __attribute__((vector_size(16)));\nV f(V v)\n{\nreturn __arithmetic_fence(v);\n}\n", .rejected = true },
        { .name = "generic: fence of two", .source = "double f(double x)\n{\nreturn __arithmetic_fence(x, x);\n}\n", .rejected = true },
        { .name = "generic: fence in constant contexts", .source = "static const double c = 2.0;\nstatic double e = __arithmetic_fence(c);\n_Static_assert(__arithmetic_fence(1.0) == 1.0, \"\");\n" },
        { .name = "generic: fence of float vectors and complex values", .source = "typedef float V __attribute__((vector_size(16)));\nV f(V v, _Complex float z)\n{\nreturn __arithmetic_fence(v) + __real__ __arithmetic_fence(z);\n}\n" },
        { .name = "generic: xray of an integer for its string", .source = "void f(void)\n{\n__xray_customevent(1, 2);\n}\n", .rejected = true },
        { .name = "generic: xray typed event of two", .source = "void f(void)\n{\n__xray_typedevent(1, \"x\");\n}\n", .rejected = true },
        { .name = "generic: warn_memset_zero_len of one", .source = "void f(void)\n{\n__warn_memset_zero_len(1);\n}\n", .rejected = true },
        { .name = "generic: relocate of const", .source = "void f(const int* a, const int* b)\n{\n__builtin_trivially_relocate(a, b, 1);\n}\n", .rejected = true },
        { .name = "generic: relocate of a struct", .source = "struct S { int x; };\nvoid f(struct S* a, struct S* b)\n{\n__builtin_trivially_relocate(a, b, 1);\n}\n", .rejected = true },
        { .name = "generic: relocate of an atomic", .source = "void f(_Atomic int* a, _Atomic int* b)\n{\n__builtin_trivially_relocate(a, b, 1);\n}\n", .rejected = true },
        { .name = "generic: relocate of an array", .source = "int x[4], y[4];\nvoid f(void)\n{\n__builtin_trivially_relocate(x, y, 1);\n}\n", .rejected = true },
        { .name = "generic: relocate of two types", .source = "void f(int* a, long* b)\n{\n__builtin_trivially_relocate(a, b, 1);\n}\n", .rejected = true },
        { .name = "generic: relocate of a pointer and an array", .source = "void f(int* a)\n{\nint c[2];\n__builtin_trivially_relocate(a, c, 1);\n}\n", .rejected = true },
        { .name = "generic: relocate of void", .source = "void f(void* a, void* b)\n{\n__builtin_trivially_relocate(a, b, 1);\n}\n", .rejected = true },
        { .name = "generic: relocate of a pointer size", .source = "void f(int* a, int* b)\n{\n__builtin_trivially_relocate(a, b, (void*)0);\n}\n", .rejected = true },
        { .name = "generic: relocate of enums, arrays and vectors", .source = "enum E { X };\ntypedef int V __attribute__((vector_size(16)));\nvoid f(enum E* a, enum E* b, int (*c)[3], int (*d)[3], V* v, V* w)\n{\n__builtin_trivially_relocate(a, b, 1.5);\n__builtin_trivially_relocate(c, d, 1);\n__builtin_trivially_relocate(v, w, 1);\n}\n" },
        { .name = "generic: operator_new in C", .source = "void* f(void)\n{\nreturn __builtin_operator_new(4);\n}\n", .rejected = true },
        { .name = "generic: operator_delete in C", .source = "void f(void* p)\n{\n__builtin_operator_delete(p);\n}\n", .rejected = true },
        { .name = "generic: ptrauth_auth", .source = "void* f(void* p)\n{\nreturn __builtin_ptrauth_auth(p, 0, 0);\n}\n", .rejected = true },
        { .name = "generic: ptrauth_auth_and_resign", .source = "void* f(void* p)\n{\nreturn __builtin_ptrauth_auth_and_resign(p, 0, 0, 0, 0);\n}\n", .rejected = true },
        { .name = "generic: ptrauth_auth_load_relative_and_sign", .source = "void* f(void* p)\n{\nreturn __builtin_ptrauth_auth_load_relative_and_sign(p, 0, 0, 0, 0, 0);\n}\n", .rejected = true },
        { .name = "generic: ptrauth_auth_with_pc_and_resign", .source = "void* f(void* p)\n{\nreturn __builtin_ptrauth_auth_with_pc_and_resign(p, 0, 0, 0, 0, 0);\n}\n", .rejected = true },
        { .name = "generic: ptrauth_blend_discriminator", .source = "long f(void* p)\n{\nreturn __builtin_ptrauth_blend_discriminator(p, 0);\n}\n", .rejected = true },
        { .name = "generic: ptrauth_sign_constant", .source = "void g(void);\nvoid* f(void)\n{\nreturn __builtin_ptrauth_sign_constant(&g, 0, 0);\n}\n", .rejected = true },
        { .name = "generic: ptrauth_sign_generic_data", .source = "long f(void* p)\n{\nreturn __builtin_ptrauth_sign_generic_data(p, 0);\n}\n", .rejected = true },
        { .name = "generic: ptrauth_sign_unauthenticated", .source = "void* f(void* p)\n{\nreturn __builtin_ptrauth_sign_unauthenticated(p, 0, 0);\n}\n", .rejected = true },
        { .name = "generic: ptrauth_string_discriminator", .source = "static unsigned long x = __builtin_ptrauth_string_discriminator(\"abc\");\n", .rejected = true },
        { .name = "generic: ptrauth_strip", .source = "void* f(void* p)\n{\nreturn __builtin_ptrauth_strip(p, 0);\n}\n", .rejected = true },
        { .name = "generic: complex of qualified and aligned operands", .source =
          "typedef double D __attribute__((aligned(16)));\nvolatile D v;\nconst double c;\n_Complex double f(void)\n{\nreturn __builtin_complex(v, c);\n}\n" },
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
    // The builtins of a place (#148): __builtin_LINE's and __builtin_COLUMN's, through a macro, in an enumerator, an
    // _Static_assert and a static initializer; __builtin_FILE's, __builtin_FILE_NAME's and __builtin_FUNCTION's strings, at
    // file scope ("" for the function) and in one, after a #line that names the file; and the queries of a type,
    // __builtin_vectorelements' (of a vector, an extended vector type and an expression) and
    // __builtin_omp_required_simd_align's. (The oracle's tested is a macro's oracle_tested, so a function's name is a
    // helper's.)
    check_test_program_source(test,
        "#line 7 \"dir/places.c\"\n"
        "static unsigned long long h;static void mix(unsigned long long v){h=h*31+v;}\n"
        "static void mixs(const char* s){while(*s)mix((unsigned char)*s++);mix(0);}static const char* who(void){return __builtin_FUNCTION();}\n"
        "#define HERE __builtin_LINE()\n"
        "typedef int V __attribute__((vector_size(16)));typedef float W __attribute__((ext_vector_type(3)));\n"
        "static const char* pf=__builtin_FILE();static const char* pn=__builtin_FILE_NAME();static const char* pu=__builtin_FUNCTION();\n"
        "static unsigned line=__builtin_LINE();enum{E=__builtin_LINE(),C=__builtin_COLUMN()};_Static_assert(__builtin_LINE()==12,\"\");\n"
        "int tested(int n){h=(unsigned)n;mixs(pf);mixs(pn);mixs(pu);mix(line);mix(E);mix(C);mix(HERE);mix(__builtin_COLUMN());\n"
        "mixs(who());mixs(__builtin_FILE());V v={n};mix(__builtin_vectorelements(v));mix(__builtin_vectorelements(W));\n"
        "mix(__builtin_omp_required_simd_align(double));mix(sizeof(__builtin_FILE()));mix(_Generic(__builtin_LINE(),unsigned:1,default:2));\n"
        "mix(_Generic(__builtin_FILE(),char*:3,default:4));mix(_Generic(__builtin_vectorelements(v),unsigned long:5,default:6));return (int)(h^(h>>32));}\n",
        "generic-source-places-and-type-queries", clang);
    // __builtin_complex (#148): its folds in static data (signed zeros, an infinity, a NaN, a const object's value, long
    // double's and _Float16's) and in an enumerator, and its values at run time, of each real floating type, from
    // qualified, aligned and volatile operands, evaluated once each, in order, and taken apart, multiplied and summed.
    check_test_program_source(test,
        "static unsigned long long h;static void mix(unsigned long long v){h=h*31+v;}"
        "static void mixd(double d){unsigned long long b;__builtin_memcpy(&b,&d,8);mix(b);}static const double c=1.5;"
        "static _Complex double zd=__builtin_complex(1.0,-0.0);static _Complex float zf=__builtin_complex(__builtin_inff(),2.5f);"
        "static _Complex long double zl=__builtin_complex(3.0L,4.0L);static _Complex double zc=__builtin_complex(c,2.0);"
        "static _Complex _Float16 zh=__builtin_complex((_Float16)1.5,(_Float16)-2);"
        "static _Complex double zn=__builtin_complex(__builtin_nan(\"\"),-__builtin_inf());enum{E=(int)__imag__ __builtin_complex(3.0,7.0)};"
        "static int order;static double at(double v,int t){order=order*10+t;return v;}typedef double D __attribute__((aligned(16)));"
        "int tested(int n){h=(unsigned)n;mixd(__real__ zd);mixd(__imag__ zd);mixd(__real__ zf);mixd(__imag__ zf);"
        "mixd((double)__real__ zl);mixd((double)__imag__ zl);mixd(__real__ zc);mixd(__imag__ zc);mixd((double)__real__ zh);mixd((double)__imag__ zh);"
        "mix(__real__ zn!=__real__ zn);mixd(__imag__ zn);mix(E);double x=n*0.5,y=-n;_Complex double a=__builtin_complex(x,y);mixd(__real__ a);mixd(__imag__ a);"
        "_Complex double b=__builtin_complex(at(x,1),at(y,2))*__builtin_complex(y,x);mixd(__real__ b);mixd(__imag__ b);mix((unsigned)order);"
        "float fx=(float)n;volatile float vf=2.0f;_Complex float f=__builtin_complex(fx,vf);mixd(__real__ f);mixd(__imag__ f);"
        "long double lx=n*3.0L;_Complex long double l=__builtin_complex(lx,-lx);mixd((double)__real__ l);mixd((double)__imag__ l);"
        "D dd=n+0.25;const double cd=-n;_Complex double g=__builtin_complex(dd,cd);mixd(__real__ g);mixd(__imag__ g);"
        "_Float16 hx=(_Float16)n;_Complex _Float16 hh=__builtin_complex(hx,(_Float16)0.5);mixd((double)__real__ hh);mixd((double)__imag__ hh);"
        "__bf16 bx=(__bf16)n;mixd((double)(float)__real__ __builtin_complex(bx,bx));mixd((double)(float)__imag__ __builtin_complex(bx,(__bf16)3));"
        "__float128 qx=n;_Complex __float128 qq=__builtin_complex(qx,qx*2);mixd((double)__real__ qq);mixd((double)__imag__ qq);"
        "mix(sizeof(__builtin_complex(1.0f,2.0f)));mix(_Generic(__builtin_complex(1.0,2.0),_Complex double:1,default:2));"
        "_Complex double s[2]={__builtin_complex(x,1.0),__builtin_complex(2.0,y)};mixd(__real__ s[1]);mixd(__imag__ s[0]);"
        "mixd(__imag__ (__builtin_complex(x,y)+1.0));return (int)(h^(h>>32));}",
        "generic-complex", clang);
    // __arithmetic_fence's values of each floating type, a complex one and a vector, and in static data; the xray events
    // and __warn_memset_zero_len, whose arguments never run; and
    // trivially_relocate's moves, overlapping ones among them, of ints, doubles and arrays of ints, with its destination.
    check_test_program_source(test,
        "static unsigned long long h;static void mix(unsigned long long v){h=h*31+v;}"
        "static void mixd(double d){unsigned long long b;__builtin_memcpy(&b,&d,8);mix(b);}"
        "static int counter;static int bump(void){return ++counter;}"
        "static const double c=2.5;static double fenced=__arithmetic_fence(c)+__arithmetic_fence(0.5);"
        "typedef float V __attribute__((vector_size(16)));"
        "int tested(int n){h=(unsigned)n;mixd(fenced);double x=n*0.75;float y=(float)n/3;long double l=n*1.25L;_Float16 hf=(_Float16)n;"
        "mixd(__arithmetic_fence(x+1.0));mixd(__arithmetic_fence(y));mixd((double)__arithmetic_fence(l));mixd((double)__arithmetic_fence(hf));"
        "_Complex double z=__arithmetic_fence(x+2.0i);mixd(__real__ z);mixd(__imag__ z);V v={y,1,2,3};V w=__arithmetic_fence(v);mixd(w[0]);mixd(w[3]);"
        "mix(_Generic(__arithmetic_fence(y),float:1,default:2));"
        "counter=0;__xray_customevent((const char*)(long)bump(),(unsigned long)bump());__xray_typedevent(bump(),\"x\",bump());__warn_memset_zero_len();"
        "mix((unsigned)counter);"
        "int a[8];for(int i=0;i<8;i++)a[i]=i*n+1;int* pa=a;int* r=__builtin_trivially_relocate(pa+2,pa,(unsigned long)(n&3)+3);mix(r==a+2);for(int i=0;i<8;i++)mix((unsigned)a[i]);"
        "double d[4]={1,2,3,4},e[4];double* pe=e;double* pd=d;double* q=__builtin_trivially_relocate(pe,pd,4.0);mix(q==e);for(int i=0;i<4;i++)mixd(e[i]);"
        "int m[2][3]={{1,2,3},{4,5,n}},k[2][3];int (*pk)[3]=k;int (*pm)[3]=m;__builtin_trivially_relocate(pk,pm,2);for(int i=0;i<2;i++)for(int j=0;j<3;j++)mix((unsigned)k[i][j]);"
        "long s[3]={n,2,3};long* t=s;__builtin_trivially_relocate(t+1,t,bump());mix((unsigned long)s[2]);mix((unsigned)counter);"
        "return (int)(h^(h>>32));}"
        , "generic-fence-events-and-relocate", clang);
}

static __attribute__((noinline)) void check_test_generic_builtins(CheckTest* test, const char* clang)
{
    check_test_generic_builtin_sources(test, clang);
    check_test_generic_builtin_programs(test, clang);
}

#endif
