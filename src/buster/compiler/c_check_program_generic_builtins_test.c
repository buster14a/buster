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
        // A const array's string (#1917's review): a known one's, an automatic array's, a const record's member array's and a
        // const pointer's pointee's too, with a `$` is clang's error; through a cast to void * clang's evaluator loses the
        // array's designator, its error too, and through casts between pointers to char it reads it (#1843).
        { .name = "generic: verbose_trap of a const array's dollar", .source =
          "static const char m[] = \"a$c\";\nvoid f(void)\n{\n__builtin_verbose_trap(\"x\", m);\n}\n", .rejected = true },
        { .name = "generic: verbose_trap of a const array", .source =
          "static const char m[] = \"abc\";\nvoid f(void)\n{\n__builtin_verbose_trap(\"x\", m + 1);\n}\n" },
        { .name = "generic: verbose_trap of an automatic const array", .source =
          "void f(void)\n{\nconst char m[] = \"abc\";\n__builtin_verbose_trap(\"x\", m);\n}\n" },
        { .name = "generic: verbose_trap of an automatic const array's dollar", .source =
          "void f(void)\n{\nconst char m[] = \"a$c\";\n__builtin_verbose_trap(\"x\", m);\n}\n", .rejected = true },
        { .name = "generic: verbose_trap of a member array", .source =
          "struct S { char a[4]; };\nstatic const struct S s = { \"abc\" };\nvoid f(void)\n{\n__builtin_verbose_trap(\"x\", s.a);\n}\n" },
        { .name = "generic: verbose_trap of a member array's dollar", .source =
          "struct S { char a[4]; };\nstatic const struct S s = { \"ab$\" };\nvoid f(void)\n{\n__builtin_verbose_trap(\"x\", s.a);\n}\n", .rejected = true },
        { .name = "generic: verbose_trap of a const pointer's value", .source =
          "static const char m[] = \"abc\";\nstatic const char* const p = m + 1;\nvoid f(void)\n{\n__builtin_verbose_trap(\"x\", p);\n}\n" },
        { .name = "generic: verbose_trap of a const array through void *", .source =
          "static const char m[] = \"abc\";\nvoid f(void)\n{\n__builtin_verbose_trap(\"x\", (const char*)(const void*)m);\n}\n", .rejected = true },
        { .name = "generic: verbose_trap of a const array through char pointers", .source =
          "static const char m[] = \"abc\";\nvoid f(void)\n{\n__builtin_verbose_trap(\"x\", (const char*)(char*)m + 1);\n}\n" },
        { .name = "generic: verbose_trap of a literal through void *", .source =
          "void f(void)\n{\n__builtin_verbose_trap(\"x\", (const char*)(const void*)\"abc\" + 1);\n}\n" },
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
        // __builtin_function_start's operand evaluates to a function at offset 0, as clang's getAsBuiltinConstantDeclRef reads
        // it: a named function's address and its casts are, a conditional's, a null pointer's and an offset one aren't
        // (#1126's review).
        { .name = "generic: function_start of a function's address", .source =
          "void g(void);\nvoid* a = __builtin_function_start(&g);\nvoid* b = __builtin_function_start((&g));\n"
          "void* c = __builtin_function_start((void (*)(int))g);\nvoid* d = __builtin_function_start((void*)&g);\n"
          "void* f(void)\n{\nreturn __builtin_function_start(*&g) == __builtin_function_start((char*)&g) ? a : b;\n}\n" },
        { .name = "generic: function_start of a conditional", .source =
          "void g(void);\nvoid h(void);\nvoid* f(int y)\n{\nreturn __builtin_function_start(y ? g : h);\n}\n", .rejected = true },
        { .name = "generic: function_start of a null pointer", .source = "void* a = __builtin_function_start(0);\n", .rejected = true },
        { .name = "generic: function_start of an offset", .source =
          "void g(void);\nvoid* a = __builtin_function_start((char*)g + 1);\n", .rejected = true },
        { .name = "generic: function_start of an offset address", .source =
          "void g(void);\nvoid* f(void)\n{\nreturn __builtin_function_start((void*)&g + 1);\n}\n", .rejected = true },
        { .name = "generic: function_start of folded addresses", .source =
          "void g(void);\nvoid k(void);\nvoid* a = __builtin_function_start(1 ? g : g);\nvoid* b = __builtin_function_start(g + 0);\n"
          "void* c = __builtin_function_start(0 ? g : k);\nvoid* d = __builtin_function_start((g, k));\nvoid* e = __builtin_function_start(&g + 1 - 1);\n"
          "void* f(void)\n{\nreturn __builtin_function_start(1 ? (char*)g + 0 : 0);\n}\n" },
        { .name = "generic: function_start of an object's address", .source =
          "int v;\nvoid* a = __builtin_function_start((void (*)(void))&v);\n", .rejected = true },
        { .name = "generic: function_start of a folded object's address", .source =
          "int v;\nvoid g(void);\nvoid* a = __builtin_function_start(1 ? (void (*)(void))&v : g);\n", .rejected = true },
        { .name = "generic: function_start of a folded string", .source =
          "void g(void);\nvoid* a = __builtin_function_start(0 ? g : (void (*)(void))\"abc\");\n", .rejected = true },
        { .name = "generic: function_start of a cast pointer", .source =
          "void* f(void (*q)(void))\n{\nreturn __builtin_function_start((void*)q);\n}\n", .rejected = true },
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
        // #1126's review: a comma expression's operand, its last operand's type (both orders of two widths), and a type
        // before a comma, which clang's parser refuses.
        { .name = "generic: vectorelements of a comma expression", .source =
          "typedef int A __attribute__((ext_vector_type(2)));typedef int B __attribute__((ext_vector_type(4)));\n"
          "_Static_assert(__builtin_vectorelements((A){0}, (B){0}) == 4 && __builtin_vectorelements((B){0}, (A){0}) == 2, \"\");\n"
          "unsigned long f(A a, B b)\n{\nreturn __builtin_vectorelements(a, b) * 10 + __builtin_vectorelements(b, a, a);\n}\n" },
        { .name = "generic: vectorelements of a type and an expression", .source =
          "typedef int A __attribute__((ext_vector_type(2)));\nunsigned long f(A a)\n{\nreturn __builtin_vectorelements(int, a);\n}\n", .rejected = true },
        // #1126's review: omp_required_simd_align's operand is alignof's, an array's base element type, which must be a
        // complete object type.
        { .name = "generic: omp_required_simd_align of void", .source = "int f(void)\n{\nreturn __builtin_omp_required_simd_align(void);\n}\n", .rejected = true },
        { .name = "generic: omp_required_simd_align of an incomplete struct", .source =
          "struct S;\nint f(void)\n{\nreturn __builtin_omp_required_simd_align(struct S);\n}\n", .rejected = true },
        { .name = "generic: omp_required_simd_align of a function type", .source = "int f(void)\n{\nreturn __builtin_omp_required_simd_align(int(void));\n}\n",
          .rejected = true },
        { .name = "generic: omp_required_simd_align of an incomplete struct's array", .source =
          "struct S;\nint f(void)\n{\nreturn __builtin_omp_required_simd_align(struct S[]);\n}\n", .rejected = true },
        { .name = "generic: omp_required_simd_align of complete types", .source =
          "struct S;struct T{int x;};\nint f(void)\n{\nreturn __builtin_omp_required_simd_align(int[]) + __builtin_omp_required_simd_align(int[3]) +\n"
          "__builtin_omp_required_simd_align(struct S*) + __builtin_omp_required_simd_align(double[2][3]) + __builtin_omp_required_simd_align(struct T) +\n"
          "__builtin_omp_required_simd_align(int(*)(void));\n}\n" },
        // clang's type traits of C (#148): an int constant in every context, of each kind of type, and an incomplete
        // record's or enumeration's error, the destructible ones' of an array's base element too.
        { .name = "generic: type traits' values and contexts", .source =
          "struct S;struct T{int x;};union U{int y;};enum E{EA};\n"
          "#define D(t, v) _Static_assert(__is_destructible(t) == v && __is_nothrow_destructible(t) == v, #t);\n"
          "#define C(t, v) _Static_assert(__is_bitwise_cloneable(t) == v, #t);\n"
          "D(int, 1) D(struct T, 1) D(union U, 1) D(enum E, 1) D(int[3], 1) D(int*, 1) D(struct S*, 1) D(_Atomic long double, 1) D(const volatile int, 1)\n"
          "D(void, 0) D(const void, 0) D(int[], 0) D(void(void), 0) D(int(*)(void), 1) D(double[2][3], 1)\n"
          "C(int, 1) C(struct T, 1) C(int[], 1) C(void(void), 1) C(void, 0) C(struct T[][2], 1) C(_Atomic int, 1) C(__int128, 1)\n"
          "_Static_assert(_Generic(__is_destructible(int), int: 1, default: 0) && _Generic(__is_bitwise_cloneable(int), int: 1, default: 0), \"\");\n"
          "enum { K = __is_destructible(int) + __is_bitwise_cloneable(void) };\nint a[__is_nothrow_destructible(int) + 1];\n"
          "static int s = __is_bitwise_cloneable(struct T);\n"
          "int f(int n)\n{\nswitch (n) { case __is_destructible(int): return __is_destructible(int[n]) + __is_bitwise_cloneable(int[n]); }\nreturn K;\n}\n" },
        { .name = "generic: is_destructible of an incomplete struct", .source = "struct S;\nint f(void)\n{\nreturn __is_destructible(struct S);\n}\n", .rejected = true },
        { .name = "generic: is_nothrow_destructible of an incomplete union", .source = "union S;\nint x =\n__is_nothrow_destructible(union S);\n",
          .rejected = true },
        { .name = "generic: is_bitwise_cloneable of an incomplete struct", .source = "struct S;\nint f(void)\n{\nreturn __is_bitwise_cloneable(struct S);\n}\n",
          .rejected = true },
        { .name = "generic: is_bitwise_cloneable of an incomplete enum", .source = "enum S;\nint f(void)\n{\nreturn __is_bitwise_cloneable(enum S);\n}\n",
          .rejected = true },
        { .name = "generic: ptrauth_type_discriminator of a type", .source = "unsigned long f(void)\n{\nreturn __builtin_ptrauth_type_discriminator(int);\n}\n",
          .rejected = true },
        { .name = "generic: ptrauth_type_discriminator in a constant", .source = "struct Z;\nenum { E =\n__builtin_ptrauth_type_discriminator(struct Z) };\n", .rejected = true },
        { .name = "generic: is_destructible of an expression", .source = "int f(int n)\n{\nreturn __is_destructible(n);\n}\n", .rejected = true },
        // __builtin_setjmp and __builtin_longjmp (#148): a buffer of five pointers, longjmp's value a constant 1 wherever the
        // call is, and buffers of other types, which clang refuses.
        { .name = "generic: setjmp and longjmp", .source =
          "void *b[5];\nvoid f(void)\n{\n__builtin_longjmp(b, 1);\n}\nint g(void)\n{\nreturn __builtin_setjmp(b);\n}\n"
          "_Static_assert(_Generic(__builtin_setjmp(b), int: 1, default: 0), \"\");\n" },
        { .name = "generic: longjmp of 2", .source = "void *b[5];\nvoid f(int n)\n{\n__builtin_longjmp(b, 2);\n}\n", .rejected = true },
        // A builtin's name in parentheses is its call's callee, which clang's call looks through (#1242's review): in a
        // constant expression and in code.
        { .name = "generic: a builtin's name in parentheses", .source =
          "_Static_assert((__builtin_popcount)(7) == 3, \"\");\n_Static_assert(((__builtin_clz))(1) == 31, \"\");\n"
          "enum { E = (__builtin_ctz)(8) };\nint f(unsigned x)\n{\nreturn (__builtin_popcount)(x) + ((__builtin_ffs))((int)x) + E;\n}\n" },
        { .name = "generic: longjmp of a variable", .source = "void *b[5];\nvoid f(int n)\n{\n__builtin_longjmp(b, n);\n}\n", .rejected = true },
        { .name = "generic: longjmp of 0 where it can't run", .source = "void *b[5];\nvoid f(void)\n{\nif (0) __builtin_longjmp(b, 0);\n}\n",
          .rejected = true },
        { .name = "generic: setjmp of an array of longs", .source = "long b[8];\nint f(void)\n{\nreturn __builtin_setjmp(b);\n}\n", .rejected = true },
        { .name = "generic: setjmp of an array of char pointers", .source = "char *b[5];\nint f(void)\n{\nreturn __builtin_setjmp(b);\n}\n", .rejected = true },
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
        // BPF's preserve_access_index (#148): its operand's value of its type, which clang's Sema takes of any expression, an
        // rvalue and never a constant; and HLSL's resource pointer, void(...) in C, of any arguments but void ones.
        { .name = "generic: preserve_access_index", .source =
          "struct S { int a[4]; short b; };\nvoid g(void);\nint* f(struct S* s, int x)\n{\n__builtin_preserve_access_index(g());\n"
          "_Static_assert(_Generic(__builtin_preserve_access_index(s->b), short: 1, default: 0), \"\");\n"
          "return __builtin_preserve_access_index(s->a) + *__builtin_preserve_access_index(&x);\n}\n" },
        { .name = "generic: preserve_access_index of none", .source = "int f(void)\n{\nreturn __builtin_preserve_access_index();\n}\n", .rejected = true },
        { .name = "generic: preserve_access_index of two", .source = "int f(int x)\n{\nreturn __builtin_preserve_access_index(x, x);\n}\n", .rejected = true },
        { .name = "generic: preserve_access_index's address", .source = "int* f(int x)\n{\nreturn &__builtin_preserve_access_index(x);\n}\n", .rejected = true },
        { .name = "generic: preserve_access_index assigned", .source = "void f(int x)\n{\n__builtin_preserve_access_index(x) = 3;\n}\n", .rejected = true },
        { .name = "generic: preserve_access_index in a constant", .source = "enum { E = __builtin_preserve_access_index(3) };\n", .rejected = true },
        { .name = "generic: hlsl resource pointer", .source =
          "struct S { int a; };\nvoid f(struct S s, int x)\n{\n__builtin_hlsl_resource_getpointer_typed();\n"
          "__builtin_hlsl_resource_getpointer_typed(s, 1.0f, \"x\", x++, f);\n(void)sizeof(__builtin_hlsl_resource_getpointer_typed(1));\n}\n" },
        // clear_padding (#148): a pointer as written to a complete object type that isn't const, volatile and _Atomic ones
        // and a pointer to a variable length array among them.
        { .name = "generic: clear_padding", .source =
          "struct S { char c; int i; };\nvoid f(int n, struct S* p, volatile struct S* v, _Atomic(struct S)* a, int (*q)[n], int (*r)[3])\n{\n"
          "__builtin_clear_padding(p);\n__builtin_clear_padding(v);\n__builtin_clear_padding(a);\n__builtin_clear_padding(q);\n__builtin_clear_padding(r);\n}\n" },
        { .name = "generic: clear_padding of void", .source = "void f(void* p)\n{\n__builtin_clear_padding(p);\n}\n", .rejected = true },
        { .name = "generic: clear_padding of an incomplete struct", .source = "struct S;\nvoid f(struct S* p)\n{\n__builtin_clear_padding(p);\n}\n", .rejected = true },
        { .name = "generic: clear_padding of an int", .source = "void f(int x)\n{\n__builtin_clear_padding(x);\n}\n", .rejected = true },
        { .name = "generic: clear_padding of const", .source = "void f(const int* p)\n{\n__builtin_clear_padding(p);\n}\n", .rejected = true },
        { .name = "generic: clear_padding of a function pointer", .source = "void f(int (*p)(void))\n{\n__builtin_clear_padding(p);\n}\n", .rejected = true },
        { .name = "generic: clear_padding of an array", .source = "int a[3];\nvoid f(void)\n{\n__builtin_clear_padding(a);\n}\n", .rejected = true },
        { .name = "generic: clear_padding of an incomplete array", .source = "void f(int (*q)[])\n{\n__builtin_clear_padding(q);\n}\n", .rejected = true },
        { .name = "generic: clear_padding of two", .source = "void f(int* p)\n{\n__builtin_clear_padding(p, p);\n}\n", .rejected = true },
        { .name = "generic: clear_padding's value", .source = "int f(int* p)\n{\nreturn __builtin_clear_padding(p);\n}\n", .rejected = true },
        // eh_return (#148): void(size_t, void*), which doesn't return. CFString's argument is an ordinary literal past its
        // parentheses and casts, NSString's any const char *, each a const __NSConstantString *, whose record is made
        // where an operand's evaluation reaches it first.
        { .name = "generic: eh_return and constant strings", .source =
          "enum { E = sizeof(*__builtin___NSStringMakeConstantString(\"x\")) };\nconst __NSConstantString* f(const char* s, int x)\n{\n"
          "if (x) __builtin_eh_return(x, &x);\n(void)__builtin___CFStringMakeConstantString(\"x\");\n"
          "(void)__builtin___CFStringMakeConstantString(((const char*)(\"a\" \"b\")));\n(void)sizeof(__builtin___CFStringMakeConstantString(\"\"));\n"
          "long n = __builtin___CFStringMakeConstantString(\"abc\")->length;\n__typeof__(__builtin___CFStringMakeConstantString(\"x\")) p = 0;\n"
          "(void)n;\n(void)p;\nreturn __builtin___NSStringMakeConstantString(s);\n}\n" },
        // A constant CFString of a literal is an address constant (#148): a static initializer of one, cast to long or offset,
        // and NSString's, at file scope and in a function.
        { .name = "generic: constant strings in static initializers", .source =
          "static const void* p = __builtin___CFStringMakeConstantString(\"x\");\nstatic long l = (long)__builtin___CFStringMakeConstantString(\"y\");\n"
          "static const char* q = (const char*)__builtin___CFStringMakeConstantString(\"w\") + 1;\n"
          "const void* f(void)\n{\nstatic const void* s = __builtin___NSStringMakeConstantString(\"v\");\n(void)p;\n(void)l;\n(void)q;\nreturn s;\n}\n" },
        // clang's evaluator takes any NSString call as an address constant, of no literal too (#1785), whose static data -c
        // refuses by name (c_checker_driver_test.c's code generation refusals).
        { .name = "generic: NSString of no literal in static initializers", .source =
          "static const char m[] = \"x\";\nconst char* g(void);\nstatic const void* p = __builtin___NSStringMakeConstantString(m);\n"
          "static long l = (long)__builtin___NSStringMakeConstantString(m + 1) + 2;\nstatic const void* c = __builtin___NSStringMakeConstantString(g());\n"
          "const void* f(const char* s)\n{\nstatic const void* t = __builtin___NSStringMakeConstantString(s);\n(void)p;\n(void)l;\n(void)c;\nreturn t;\n}\n" },
        { .name = "generic: NSStrings compared in a static initializer", .source =
          "static const char m[] = \"x\";\nint d = __builtin___NSStringMakeConstantString(m) == __builtin___NSStringMakeConstantString(m);\n", .rejected = true },
        { .name = "generic: eh_return of one", .source = "void f(void)\n{\n__builtin_eh_return(1);\n}\n", .rejected = true },
        { .name = "generic: eh_return of a pointer offset", .source = "void f(void)\n{\n__builtin_eh_return((void*)0, 0);\n}\n", .rejected = true },
        { .name = "generic: eh_return's value", .source = "int f(void)\n{\nreturn __builtin_eh_return(1, 0);\n}\n", .rejected = true },
        { .name = "generic: CFString of a pointer", .source = "const void* f(const char* s)\n{\nreturn __builtin___CFStringMakeConstantString(s);\n}\n", .rejected = true },
        { .name = "generic: CFString of a wide literal", .source = "const void* f(void)\n{\nreturn __builtin___CFStringMakeConstantString(L\"x\");\n}\n", .rejected = true },
        { .name = "generic: CFString of a UTF-8 literal", .source = "const void* f(void)\n{\nreturn __builtin___CFStringMakeConstantString(\"a\" u8\"x\");\n}\n", .rejected = true },
        { .name = "generic: CFString of __func__", .source = "const void* f(void)\n{\nreturn __builtin___CFStringMakeConstantString(__func__);\n}\n", .rejected = true },
        { .name = "generic: CFString of none", .source = "const void* f(void)\n{\nreturn __builtin___CFStringMakeConstantString();\n}\n", .rejected = true },
        { .name = "generic: CFString as an int", .source = "int f(void)\n{\nreturn __builtin___CFStringMakeConstantString(\"x\");\n}\n", .rejected = true },
        { .name = "generic: NSString of an int", .source = "const void* f(int x)\n{\nreturn __builtin___NSStringMakeConstantString(x);\n}\n", .rejected = true },
        // The Microsoft and z/OS lists (#148): each ms builtin's list an lvalue of exactly char * (clang's char *&), and
        // va_arg's where it is one; ms_va_start in an ms_abi variadic function alone, every other start out of one;
        // z/OS's lists char ** parameters.
        { .name = "generic: Microsoft and z/OS lists", .source =
          "int __attribute__((ms_abi)) f(int n, ...)\n{\n__builtin_ms_va_list a, b;\n__builtin_ms_va_start(a, n);\n__builtin_ms_va_copy(b, a);\n"
          "int x = __builtin_va_arg(b, int);\n__builtin_ms_va_end(b);\n__builtin_ms_va_end(a);\n__builtin_va_list s;\n__builtin_va_copy(s, s);\nreturn x;\n}\n"
          "int g(char* p, char** q, int n, ...)\n{\nchar* c[1];\n__builtin_ms_va_copy(c[0], *q);\n__builtin_ms_va_end(p);\nchar* z;\n"
          "__builtin_zos_va_start(&z, n);\n__builtin_zos_va_copy(q, &z);\n__builtin_zos_va_end(q);\nreturn __builtin_va_arg(p, int) + __builtin_va_arg(*q, int);\n}\n" },
        { .name = "generic: ms_va_start out of ms_abi", .source = "void f(int n, ...)\n{\nchar* a;\n__builtin_ms_va_start(a, n);\n}\n", .rejected = true },
        { .name = "generic: va_start in ms_abi", .source = "void __attribute__((ms_abi)) f(int n, ...)\n{\n__builtin_va_list a;\n__builtin_va_start(a, n);\n}\n", .rejected = true },
        { .name = "generic: zos_va_start in ms_abi", .source = "void __attribute__((ms_abi)) f(int n, ...)\n{\nchar* a;\n__builtin_zos_va_start(&a, n);\n}\n", .rejected = true },
        { .name = "generic: ms_va_start with fixed args", .source = "void __attribute__((ms_abi)) f(int n)\n{\nchar* a;\n__builtin_ms_va_start(a, n);\n}\n", .rejected = true },
        { .name = "generic: ms_va_end of a const list", .source = "void f(char* const a)\n{\n__builtin_ms_va_end(a);\n}\n", .rejected = true },
        { .name = "generic: ms_va_end of an unsigned char pointer", .source = "void f(unsigned char* a)\n{\n__builtin_ms_va_end(a);\n}\n", .rejected = true },
        { .name = "generic: ms_va_copy of a temporary", .source = "void f(char* a)\n{\n__builtin_ms_va_copy(a, a + 1);\n}\n", .rejected = true },
        { .name = "generic: ms_va_copy of a const char pointer", .source = "void f(char* a, const char* b)\n{\n__builtin_ms_va_copy(a, b);\n}\n", .rejected = true },
        { .name = "generic: va_arg of a const char pointer", .source = "int f(const char* a)\n{\nreturn __builtin_va_arg(a, int);\n}\n", .rejected = true },
        { .name = "generic: va_arg of a char pointer temporary", .source = "int f(char* a)\n{\nreturn __builtin_va_arg(a + 0, int);\n}\n", .rejected = true },
        { .name = "generic: zos_va_start of a void list", .source = "void f(int n, ...)\n{\nvoid* a;\n__builtin_zos_va_start(&a, n);\n}\n", .rejected = true },
        { .name = "generic: zos_va_start with fixed args", .source = "void f(int n)\n{\nchar* a;\n__builtin_zos_va_start(&a, n);\n}\n", .rejected = true },
        { .name = "generic: zos_va_copy of one", .source = "void f(char** a)\n{\n__builtin_zos_va_copy(a);\n}\n", .rejected = true },
        { .name = "generic: zos_va_end's value", .source = "int f(char** a)\n{\nreturn __builtin_zos_va_end(a);\n}\n", .rejected = true },
        // infer_alloc_token (#148): a size_t of arguments that never run, an integer constant expression of the type a
        // sizeof among them gives (through groupings and + - * / << >>, or a variable's initializer), LLVM's SipHash of
        // its name without tag keywords, past half the tokens for a type holding a pointer (uintptr_t's typedef chain
        // too, a member's included); no constant where no type is inferred.
        { .name = "generic: infer_alloc_token", .source =
          "typedef unsigned long uintptr_t;\ntypedef uintptr_t handle;\nstruct S { int a; char* p; };\nstruct W { handle h; };\n"
          "struct Q { int a[3]; long b; };\ntypedef struct { short s; } TS;\n"
          "_Static_assert(__builtin_infer_alloc_token(sizeof(int)) == 2689373973731826898u, \"\");\n"
          "_Static_assert(__builtin_infer_alloc_token(sizeof(struct S)) == 9965185003513084084u, \"\");\n"
          "_Static_assert(__builtin_infer_alloc_token(sizeof(char*) * 4) == 11473864704255292954u, \"\");\n"
          "_Static_assert(__builtin_infer_alloc_token(3 + sizeof(TS)) == 2947234827507279461u, \"\");\n"
          "_Static_assert(__builtin_infer_alloc_token(sizeof(struct W)) > 9223372036854775807u, \"\");\n"
          "_Static_assert(__builtin_infer_alloc_token(sizeof(uintptr_t)) > 9223372036854775807u, \"\");\n"
          "_Static_assert(__builtin_infer_alloc_token(sizeof(struct Q)) == 6602487442970685230u, \"\");\n"
          "_Static_assert(sizeof(__builtin_infer_alloc_token(1)) == 8, \"\");\nenum { E = __builtin_infer_alloc_token(sizeof(int[4])) != 0 };\n"
          "unsigned long f(int i)\n{\nunsigned long s = sizeof(struct Q) * 2;\nstatic unsigned long t = __builtin_infer_alloc_token(sizeof(_Bool));\n"
          "return __builtin_infer_alloc_token(i++, sizeof(char)) + __builtin_infer_alloc_token(s) + __builtin_infer_alloc_token(i) + t;\n}\n" },
        // call_with_static_chain (#148): a call as written of no builtin (a catalog one, a header's leaf or a library
        // builtin's external declaration), the call's value, and a pointer chain.
        { .name = "generic: call_with_static_chain", .source =
          "struct S { int a[4]; };\nint f(int);\nstruct S h(void);\nstatic int abs(int x) { return x; }\nvoid v(void);\n"
          "int g(void* p, char* q, int (*k)(int, ...))\n{\nstruct S s = __builtin_call_with_static_chain(h(), q);\n"
          "__builtin_call_with_static_chain(v(), p);\n(void)sizeof(__builtin_call_with_static_chain(f(1), p));\n"
          "return __builtin_call_with_static_chain(f(1), p) + __builtin_call_with_static_chain((*k)(1, 2.0), p++) + s.a[0] +\n"
          "__builtin_call_with_static_chain(abs(2), p);\n}\n" },
        { .name = "generic: call_with_static_chain of a name", .source = "int f(int);\nint g(void* p)\n{\nreturn __builtin_call_with_static_chain(f, p);\n}\n", .rejected = true },
        { .name = "generic: call_with_static_chain of a grouping", .source = "int f(int);\nint g(void* p)\n{\nreturn __builtin_call_with_static_chain((f(1)), p);\n}\n", .rejected = true },
        { .name = "generic: call_with_static_chain of a builtin", .source = "int g(void* p)\n{\nreturn __builtin_call_with_static_chain(__builtin_abs(1), p);\n}\n", .rejected = true },
        { .name = "generic: call_with_static_chain of a library builtin", .source = "unsigned long strlen(const char*);\nunsigned long g(void* p)\n{\nreturn __builtin_call_with_static_chain(strlen(\"x\"), p);\n}\n", .rejected = true },
        { .name = "generic: call_with_static_chain of an int chain", .source = "int f(int);\nint g(int p)\n{\nreturn __builtin_call_with_static_chain(f(1), p);\n}\n", .rejected = true },
        { .name = "generic: call_with_static_chain of a null constant", .source = "int f(int);\nint g(void)\n{\nreturn __builtin_call_with_static_chain(f(1), 0);\n}\n", .rejected = true },
        { .name = "generic: call_with_static_chain of one", .source = "int f(int);\nint g(void)\n{\nreturn __builtin_call_with_static_chain(f(1));\n}\n", .rejected = true },
        // The os_log builtins (#148): a buffer that converts to void *, a format that is an ordinary or UTF-8 literal past its
        // parentheses and casts, data of less than 256 bytes each; the buffer's size a size_t integer constant expression of
        // the layout, whatever its data's types; __builtin_os_log_format's format checks refusing a struct or a complex
        // value a specifier takes and a mask of other than 1 to 8 bytes, one after a %P without its length too (the checks go
        // on past it, as the layout doesn't); escaped percent signs, and specifiers past the format's NUL, of no items (#1329);
        // and a %P without its length whose positional width has no argument, the pointer's item alone (#1335's review).
        { .name = "generic: os_log", .source =
          "struct S { int a; };\n_Static_assert(__builtin_os_log_format_buffer_size(\"%d %s\", 1, \"x\") == 18, \"\");\n"
          "enum { E = __builtin_os_log_format_buffer_size(\"%{mask.ab}d %m\", 1) };\n_Static_assert(E == 20, \"\");\n"
          "_Static_assert(__builtin_os_log_format_buffer_size(\"%Lf %P\", 1.0L, 1) == 26, \"\");\n"
          "_Static_assert(__builtin_os_log_format_buffer_size(\"%2$s %1$d\", 1, \"x\") == 18, \"\");\n"
          "_Static_assert(__builtin_os_log_format_buffer_size(\"%.*s %{public, mask.abcdefghij}d\", 3, \"x\", 1, 2) == 34, \"\");\n"
          "_Static_assert(__builtin_os_log_format_buffer_size(u8\"%d\", (struct S){ 1 }) == 8, \"\");\n"
          "_Static_assert(__builtin_os_log_format_buffer_size(\"%f %f %f\", (_Float16)1, (__bf16)1, (__fp16)1) == 20, \"\");\n"
          "_Static_assert(__builtin_os_log_format_buffer_size(\"%Lf %d %d\", 1.0L, (__int128)1, (_BitInt(72))1) == 56, \"\");\n"
          "void* f(void* b, char* c, int i, struct S s)\n{\nunsigned long n = __builtin_os_log_format_buffer_size(\"%d\", i++);\n"
          "(void)__builtin_os_log_format(c, (\"%d\"), i);\n(void)__builtin_os_log_format(b, (const char*)\"%s %f\", \"x\", 1.5f);\n"
          "(void)__builtin_os_log_format(b, \"%d\", 1, s);\n(void)__builtin_os_log_format(b, \"%P %{mask.ab}d\", n, 1);\n"
          "return __builtin_os_log_format(b, \"%{private}s %%\", \"x\");\n}\n"
          "_Static_assert(__builtin_os_log_format_buffer_size(\"%%%%%% %d\\0%d%s\", 1) == 8, \"\");\n"
          "_Static_assert(__builtin_os_log_format_buffer_size(\"\\0%d%d%d%d\") == 2 && __builtin_os_log_format_buffer_size(\"%% %5% %{public}% %%%%\") == 2, \"\");\n"
          "_Static_assert(__builtin_os_log_format_buffer_size(\"%1$*2$P\", (void*)0) == 12, \"\");\n" },
        { .name = "generic: os_log of an int buffer", .source = "void* f(int b)\n{\nreturn __builtin_os_log_format(b, \"%d\", 1);\n}\n", .rejected = true },
        { .name = "generic: os_log of a pointer format", .source = "void* f(void* b, const char* s)\n{\nreturn __builtin_os_log_format(b, s);\n}\n", .rejected = true },
        { .name = "generic: os_log of a wide format", .source = "unsigned long f(void)\n{\nreturn __builtin_os_log_format_buffer_size(L\"x\");\n}\n", .rejected = true },
        { .name = "generic: os_log of a struct", .source = "struct S { int a; };\nvoid* f(void* b, struct S s)\n{\nreturn __builtin_os_log_format(b, \"%d\", s);\n}\n", .rejected = true },
        { .name = "generic: os_log of a complex width", .source = "void* f(void* b, _Complex double c)\n{\nreturn __builtin_os_log_format(b, \"%*d\", c, 1);\n}\n", .rejected = true },
        { .name = "generic: os_log of a long mask", .source = "void* f(void* b)\n{\nreturn __builtin_os_log_format(b, \"%{mask.abcdefghi}d\", 1);\n}\n", .rejected = true },
        { .name = "generic: os_log of a long mask after an unlengthened pointer", .source = "void* f(void* b)\n{\nreturn __builtin_os_log_format(b, \"%P %{mask.abcdefghi}d\", 4, 1);\n}\n", .rejected = true },
        { .name = "generic: os_log of an empty mask", .source = "void* f(void* b)\n{\nreturn __builtin_os_log_format(b, \"%{mask.}d\", 1);\n}\n", .rejected = true },
        { .name = "generic: os_log of a big argument", .source = "struct T { char c[300]; };\nunsigned long f(struct T t)\n{\nreturn __builtin_os_log_format_buffer_size(\"%d\", t);\n}\n", .rejected = true },
        { .name = "generic: os_log of no buffer", .source = "void* f(void)\n{\nreturn __builtin_os_log_format(\"%d\");\n}\n", .rejected = true },
        { .name = "generic: os_log of no format", .source = "unsigned long f(void)\n{\nreturn __builtin_os_log_format_buffer_size();\n}\n", .rejected = true },
        // Up to 255 data arguments past the operands' 32-bit mask, as clang's BuiltinOSLogFormat takes them, and no more (#1785).
        { .name = "generic: os_log of 255 data arguments", .source =
          "#define A8 1, 2, 3, 4, 5, 6, 7, 8\n#define A64 A8, A8, A8, A8, A8, A8, A8, A8\n#define A255 A64, A64, A64, A8, A8, A8, A8, A8, A8, A8, 1, 2, 3, 4, 5, 6, 7\n"
          "#define F8 \"%d %d %d %d %d %d %d %d \"\n#define F64 F8 F8 F8 F8 F8 F8 F8 F8\n"
          "_Static_assert(__builtin_os_log_format_buffer_size(F64 F64 F64 F8 F8 F8 F8 F8 F8 F8 \"%d %d %d %d %d %d %d\", A255) == 1532, \"\");\n"
          "_Static_assert(__builtin_os_log_format_buffer_size(\"%255$lld %40$d\", A255) == 14, \"\");\n"
          "void* f(void* b, int n)\n{\nreturn __builtin_os_log_format(b, F8 F8 F8 F8 \"%d %d %s\", A8, A8, A8, A8, n, n + 1, \"x\");\n}\n"
          "void* g(void* b)\n{\nreturn __builtin_os_log_format(b, \"%d\", A255);\n}\n" },
        { .name = "generic: os_log of 256 data arguments", .source =
          "#define A8 1, 2, 3, 4, 5, 6, 7, 8\n#define A64 A8, A8, A8, A8, A8, A8, A8, A8\nunsigned long f(void)\n{\n"
          "return __builtin_os_log_format_buffer_size(\"%d\", A64, A64, A64, A64);\n}\n", .rejected = true },
        { .name = "generic: os_log_format of 256 data arguments", .source =
          "#define A8 1, 2, 3, 4, 5, 6, 7, 8\n#define A64 A8, A8, A8, A8, A8, A8, A8, A8\nvoid* f(void* b)\n{\n"
          "return __builtin_os_log_format(b, \"%d\", A64, A64, A64, A64);\n}\n", .rejected = true },
        // dump_struct (#148): a pointer to a complete struct or union and a function or a function pointer that takes clang's
        // generated calls (variadic past the rest of the arguments and the format, or without a prototype).
        { .name = "generic: dump_struct", .source =
          "int printf(const char*, ...);\nint pf(void* ctx, const char* f, ...);\nint kr();\nstruct S { int a; unsigned b : 2; struct { char c; } in; int arr[2]; };\n"
          "union U { int a; };\nvoid f(struct S* s, const union U* u, int (*p)(const char*, ...))\n{\n__builtin_dump_struct(s, printf);\n"
          "__builtin_dump_struct(u, p);\n__builtin_dump_struct((s), (printf));\n__builtin_dump_struct(s, pf, (void*)0);\n__builtin_dump_struct(s, pf, s);\n"
          "__builtin_dump_struct(s, kr, 1, 2.0);\n}\n" },
        { .name = "generic: dump_struct of a struct", .source = "int printf(const char*, ...);\nstruct S { int a; };\nvoid f(struct S s)\n{\n__builtin_dump_struct(s, printf);\n}\n", .rejected = true },
        { .name = "generic: dump_struct of an int pointer", .source = "int printf(const char*, ...);\nvoid f(int* p)\n{\n__builtin_dump_struct(p, printf);\n}\n", .rejected = true },
        { .name = "generic: dump_struct of an incomplete struct", .source = "int printf(const char*, ...);\nstruct I;\nvoid f(struct I* p)\n{\n__builtin_dump_struct(p, printf);\n}\n", .rejected = true },
        { .name = "generic: dump_struct of no callable", .source = "struct S { int a; };\nvoid f(struct S* s)\n{\n__builtin_dump_struct(s, 1);\n}\n", .rejected = true },
        { .name = "generic: dump_struct of a fixed printer", .source = "int bad(const char*);\nstruct S { int a; };\nvoid f(struct S* s)\n{\n__builtin_dump_struct(s, bad);\n}\n", .rejected = true },
        { .name = "generic: dump_struct of a missing context", .source = "int pf(void* ctx, const char* f, ...);\nstruct S { int a; };\nvoid f(struct S* s)\n{\n__builtin_dump_struct(s, pf);\n}\n", .rejected = true },
        { .name = "generic: dump_struct of a wrong context", .source = "int pf(int* ctx, const char* f, ...);\nstruct S { int a; };\nvoid f(struct S* s, long* l)\n{\n__builtin_dump_struct(s, pf, l);\n}\n", .rejected = true },
        { .name = "generic: dump_struct of an int format", .source = "int pf(void* ctx, int f, ...);\nstruct S { int a; };\nvoid f(struct S* s)\n{\n__builtin_dump_struct(s, pf, s);\n}\n", .rejected = true },
        { .name = "generic: dump_struct of a _BitInt bit-field", .source = "int printf(const char*, ...);\nstruct S { _BitInt(12) b : 9; };\nvoid f(struct S* s)\n{\n__builtin_dump_struct(s, printf);\n}\n", .rejected = true },
        { .name = "generic: dump_struct of a nested __int128 bit-field", .source = "int printf(const char*, ...);\nstruct S { struct { __int128 w : 70; } in; };\nvoid f(struct S* s)\n{\n__builtin_dump_struct(s, printf);\n}\n", .rejected = true },
        // The bit-field after more sibling records than the walk's 64 levels of nesting (#1405's review).
        { .name = "generic: dump_struct of a bit-field after 65 sibling records", .source = "int printf(const char*, ...);\nstruct S { struct { int x; } f0; struct { int x; } f1; struct { int x; } f2; struct { int x; } f3; struct { int x; } f4; struct { int x; } f5; struct { int x; } f6; struct { int x; } f7; struct { int x; } f8; struct { int x; } f9; struct { int x; } f10; struct { int x; } f11; struct { int x; } f12; struct { int x; } f13; struct { int x; } f14; struct { int x; } f15; struct { int x; } f16; struct { int x; } f17; struct { int x; } f18; struct { int x; } f19; struct { int x; } f20; struct { int x; } f21; struct { int x; } f22; struct { int x; } f23; struct { int x; } f24; struct { int x; } f25; struct { int x; } f26; struct { int x; } f27; struct { int x; } f28; struct { int x; } f29; struct { int x; } f30; struct { int x; } f31; struct { int x; } f32; struct { int x; } f33; struct { int x; } f34; struct { int x; } f35; struct { int x; } f36; struct { int x; } f37; struct { int x; } f38; struct { int x; } f39; struct { int x; } f40; struct { int x; } f41; struct { int x; } f42; struct { int x; } f43; struct { int x; } f44; struct { int x; } f45; struct { int x; } f46; struct { int x; } f47; struct { int x; } f48; struct { int x; } f49; struct { int x; } f50; struct { int x; } f51; struct { int x; } f52; struct { int x; } f53; struct { int x; } f54; struct { int x; } f55; struct { int x; } f56; struct { int x; } f57; struct { int x; } f58; struct { int x; } f59; struct { int x; } f60; struct { int x; } f61; struct { int x; } f62; struct { int x; } f63; struct { int x; } f64; "
          "struct { _BitInt(12) bad : 9; } last; };\nvoid f(struct S* s)\n{\n__builtin_dump_struct(s, printf);\n}\n", .rejected = true },
        { .name = "generic: dump_struct's value", .source = "int printf(const char*, ...);\nstruct S { int a; };\nint f(struct S* s)\n{\nreturn __builtin_dump_struct(s, printf);\n}\n", .rejected = true },
        // clang's counted_by family (#148): on flexible array members and pointers, wherever written, its count a member
        // declared before it (an enclosing record's through an anonymous one); __builtin_counted_by_ref a pointer to it,
        // or a void * of a member of none, in sizeof, typeof and _Generic too, and its value stored through.
        { .name = "generic: counted_by", .source =
          "struct S { long n; int fam[] __attribute__((counted_by(n))); };\n"
          "struct P { int k; char *p __attribute__((sized_by(k))); int * __attribute__((counted_by(k))) q; void *v __attribute__((counted_by_or_null(k))); };\n"
          "struct N { int k; struct { int x; char fam[] __attribute__((counted_by(k))); }; };\n"
          "struct Q { int n; int fam[]; };\n"
          "_Static_assert(_Generic(__builtin_counted_by_ref(((struct S *)0)->fam), long *: 1, default: 0), \"\");\n"
          "int f(struct S *s, struct P *p, struct N *m, struct Q *q)\n{\n"
          "*__builtin_counted_by_ref(s->fam) = 3;\n*__builtin_counted_by_ref(p->q) = 4;\n*__builtin_counted_by_ref(m->fam) = 5;\n"
          "typeof(__builtin_counted_by_ref(p->p)) v = (void *)__builtin_counted_by_ref(q->fam);\n(void)v;\n"
          "return (int)sizeof(*__builtin_counted_by_ref((s)->fam)) + (int)__builtin_dynamic_object_size(s->fam, 0);\n}\n" },
        { .name = "generic: counted_by of a later member", .source = "struct S { int fam[] __attribute__((counted_by(n))); int n; };\n", .rejected = true },
        { .name = "generic: counted_by of a bool", .source = "struct S { _Bool n; int fam[] __attribute__((counted_by(n))); };\n", .rejected = true },
        { .name = "generic: counted_by on an int", .source = "struct S { int n; int x __attribute__((counted_by(n))); };\n", .rejected = true },
        { .name = "generic: counted_by in a union", .source = "union U { int n; int *p __attribute__((counted_by(n))); };\n", .rejected = true },
        { .name = "generic: sized_by on an array", .source = "struct S { int n; char fam[] __attribute__((sized_by(n))); };\n", .rejected = true },
        { .name = "generic: counted_by_ref returned", .source = "struct S { int n; int fam[] __attribute__((counted_by(n))); };\nint *f(struct S *s)\n{\nreturn __builtin_counted_by_ref(s->fam);\n}\n", .rejected = true },
        { .name = "generic: counted_by_ref of no member", .source = "void f(int *p)\n{\n(void)__builtin_counted_by_ref(p);\n}\n", .rejected = true },
        // __builtin_available (#148): a _Bool of no constant expression, its specs names and numbers of nothing of the
        // unit's, a typedef's name among them; a spec's error and the list's, at file scope and in a function.
        { .name = "generic: available", .source =
          "typedef int macos;\nint s = sizeof(__builtin_available(*));\ntypeof(__builtin_available(ios 10, *)) t;\n"
          "_Static_assert(_Generic(__builtin_available(*), _Bool: 1, default: 0), \"\");\nint f(int x)\n{\n"
          "if (__builtin_available(macos 10.12, iOS 10_3, watchOS 3.1.2, *)) x += 1;\nif (!__builtin_available(*)) x += 2;\n"
          "_Bool b = __builtin_available(android 21, *);\nint a[__builtin_available(*) + 1];\n"
          "return x + b + (int)sizeof a + __builtin_available(anyAppleOS 26.1, *);\n}\n" },
        { .name = "generic: available of no version", .source = "int f(void)\n{\nreturn __builtin_available(macos x, *);\n}\n", .rejected = true },
        { .name = "generic: available of an unknown platform", .source = "int f(void)\n{\nreturn __builtin_available(linuxos 1, *);\n}\n", .rejected = true },
        { .name = "generic: available of a zero version", .source = "int f(void)\n{\nreturn __builtin_available(macos 0.0, *);\n}\n", .rejected = true },
        { .name = "generic: available without a star", .source = "int f(void)\n{\nreturn __builtin_available(macos 1);\n}\n", .rejected = true },
        { .name = "generic: available of a repeated platform", .source = "int f(void)\n{\nreturn __builtin_available(macos 1, macosx 2, *);\n}\n", .rejected = true },
        { .name = "generic: available of an old anyAppleOS", .source = "int f(void)\n{\nreturn __builtin_available(anyAppleOS 25, *);\n}\n", .rejected = true },
        { .name = "generic: available as a constant", .source = "enum { E = __builtin_available(*) };\n", .rejected = true },
        { .name = "generic: available's static", .source = "static int v = __builtin_available(*);\n", .rejected = true },
        { .name = "generic: infer_alloc_token of none", .source = "unsigned long f(void)\n{\nreturn __builtin_infer_alloc_token();\n}\n", .rejected = true },
        { .name = "generic: infer_alloc_token of void", .source = "unsigned long f(void)\n{\nreturn __builtin_infer_alloc_token((void)0);\n}\n", .rejected = true },
        { .name = "generic: infer_alloc_token of no type", .source = "_Static_assert(__builtin_infer_alloc_token(1) == 0, \"\");\n", .rejected = true },
        { .name = "generic: infer_alloc_token of a cast", .source = "_Static_assert(__builtin_infer_alloc_token((unsigned long)sizeof(int)) != 1, \"\");\n", .rejected = true },
        { .name = "generic: infer_alloc_token's static", .source = "void f(void)\n{\nstatic unsigned long t = __builtin_infer_alloc_token(3);\n}\n", .rejected = true },
        // __builtin_invoke(f, a...) is the call f(a...) in C (#148): of a function, a pointer to one and a member of one,
        // with a prototype's conversions and a variadic one's promotions; its errors are the call's.
        { .name = "generic: invoke", .source =
          "int g(int);\nint v(int, ...);\nint (*p)(int) = g;\nstruct S { int (*fn)(int); };\nlong f(struct S s)\n{\n"
          "_Static_assert(_Generic(__builtin_invoke(g, 1), int: 1, default: 0), \"\");\n"
          "return __builtin_invoke(g, 1.5) + __builtin_invoke(p, 2) + __builtin_invoke(*p, 3) + __builtin_invoke(s.fn, 4) + __builtin_invoke(&g, 5) + __builtin_invoke(v, 1, 2.0f, \"x\");\n}\n" },
        { .name = "generic: invoke of none", .source = "int f(void)\n{\nreturn __builtin_invoke();\n}\n", .rejected = true },
        { .name = "generic: invoke of too many", .source = "int g(int);\nint f(void)\n{\nreturn __builtin_invoke(g, 1, 2);\n}\n", .rejected = true },
        { .name = "generic: invoke of too few", .source = "int g(int);\nint f(void)\n{\nreturn __builtin_invoke(g);\n}\n", .rejected = true },
        { .name = "generic: invoke of an int", .source = "int f(int x)\n{\nreturn __builtin_invoke(x, 1);\n}\n", .rejected = true },
        { .name = "generic: invoke's address", .source = "void* h = __builtin_invoke;\n", .rejected = true },
        // Any builtin's name that no call takes is clang's "builtin functions must be directly called".
        { .name = "generic: a builtin's address", .source = "int (*p)(unsigned) = &__builtin_popcount;\n", .rejected = true },
        { .name = "generic: a builtin's value", .source = "void f(void)\n{\n(void)__builtin_trap;\n}\n", .rejected = true },
        { .name = "generic: invoke in a constant", .source = "int g(int);\nenum { E = __builtin_invoke(g, 1) };\n", .rejected = true },
        { .name = "generic: hlsl resource pointer of void", .source = "void g(void);\nvoid f(void)\n{\n__builtin_hlsl_resource_getpointer_typed(g());\n}\n", .rejected = true },
        { .name = "generic: hlsl resource pointer's value", .source = "int* f(void)\n{\nreturn __builtin_hlsl_resource_getpointer_typed();\n}\n", .rejected = true },
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
        // More arguments than the operands' 32-bit mask has bits: refused before the mask is made (#1126's review).
        { .name = "generic: operator_new of 33 arguments", .source = "void* f(void)\n{\nreturn __builtin_operator_new(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);\n}\n", .rejected = true },
        { .name = "generic: operator_delete of 33 arguments", .source = "void f(void)\n{\n__builtin_operator_delete(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);\n}\n", .rejected = true },
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
        { .name = "generic: cpu_is of an unknown name", .source = "int f(void)\n{\nreturn __builtin_cpu_is(\"bogus\");\n}\n", .rejected = true },
        { .name = "generic: cpu_is of a vendor's two", .source = "int f(void)\n{\nreturn __builtin_cpu_is(\"intel\", 1);\n}\n", .rejected = true },
        { .name = "generic: cpu_is of a wide string", .source = "int f(void)\n{\nreturn __builtin_cpu_is(L\"intel\");\n}\n", .rejected = true },
        { .name = "generic: cpu_supports of a variable", .source = "int f(const char* p)\n{\nreturn __builtin_cpu_supports(p);\n}\n", .rejected = true },
        { .name = "generic: cpu_supports in static data", .source = "static int x = __builtin_cpu_supports(\"avx\");\n", .rejected = true },
        { .name = "generic: cpu_supports in a static assertion", .source = "_Static_assert(__builtin_cpu_supports(\"avx\") || 1, \"\");\n", .rejected = true },
        { .name = "generic: cpu_supports of an unknown feature", .source = "int f(void)\n{\nreturn __builtin_cpu_supports(\"not-a-feature\") + __builtin_cpu_supports((\"avx\"));\n}\n" },
        // The literal-only arguments refuse an explicit cast, which annotation's string takes, and accept parentheses (#1126's review).
        { .name = "generic: allow_sanitize_check of a cast literal", .source =
          "int f(void)\n{\nreturn __builtin_allow_sanitize_check((const char*)\"address\");\n}\n", .rejected = true },
        { .name = "generic: allow_runtime_check of a cast literal", .source =
          "int f(void)\n{\nreturn __builtin_allow_runtime_check(((const char*)\"feature\"));\n}\n", .rejected = true },
        { .name = "generic: cpu_supports of a cast literal", .source =
          "int f(void)\n{\nreturn __builtin_cpu_supports((const char*)\"sse2\");\n}\n", .rejected = true },
        { .name = "generic: cpu_supports of a void pointer literal", .source =
          "int f(void)\n{\nreturn __builtin_cpu_supports((void*)\"sse2\");\n}\n", .rejected = true },
        { .name = "generic: cpu_is of a cast literal", .source = "int f(void)\n{\nreturn __builtin_cpu_is((char*)\"amd\");\n}\n", .rejected = true },
        { .name = "generic: cpu_is of a cast parenthesized literal", .source =
          "int f(void)\n{\nreturn __builtin_cpu_is((const char*)(\"amd\"));\n}\n", .rejected = true },
        { .name = "generic: literal-only arguments in parentheses", .source =
          "long f(void)\n{\nreturn __builtin_cpu_is((\"amd\")) + __builtin_cpu_supports(((\"sse2\"))) + __builtin_allow_sanitize_check((\"address\")) +\n"
          "__builtin_allow_runtime_check((\"x\")) + __builtin_annotation(3L, (const char*)\"note\") + __builtin_annotation(4L, (\"note\"));\n}\n" },
        { .name = "generic: cpu_init of an argument", .source = "void f(void)\n{\n__builtin_cpu_init(1);\n}\n", .rejected = true },
        { .name = "generic: cpu_init's value", .source = "int f(void)\n{\nreturn __builtin_cpu_init();\n}\n", .rejected = true },
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
        // A refusal by name (`unsupported`) of what clang takes, or (`undecided`) of what it rejects, as test-checker's
        // source fixtures read them.
        check_test_expect(test, outcomes[i].rejected == (fixtures[i].rejected && (!fixtures[i].unsupported || fixtures[i].undecided)), description);
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
    // __builtin_unwind_init's function saves the callee-saved registers rbx and r12 to r15 in its prologue, as clang's
    // does (#1126's review), where a function that doesn't call it, and uses none of them, saves none: the leading pushes
    // of each function's code, read by the program itself (past an endbr64, and a frame pointer's move).
    check_test_program_source(test,
        "static void saver(void){__builtin_unwind_init();}"
        "static int plain(int x){return x+1;}"
        "static int kept(int x){int a=x*3,b=x*5;__builtin_unwind_init();return a+b;}"
        "static unsigned pushed(const unsigned char* code){unsigned mask=0;unsigned i=0;int going=1;"
        "if(code[0]==0xf3&&code[1]==0x0f&&code[2]==0x1e&&code[3]==0xfa)i=4;"
        "while(going&&i<64){if(code[i]>=0x50&&code[i]<=0x57){mask|=1u<<(code[i]-0x50);i+=1;}"
        "else if(code[i]==0x41&&code[i+1]>=0x50&&code[i+1]<=0x57){mask|=1u<<(code[i+1]-0x50+8);i+=2;}"
        "else if(code[i]==0x48&&code[i+1]==0x89&&code[i+2]==0xe5){i+=3;}else going=0;}return mask;}"
        "int tested(int n){const unsigned callee=1u<<3|1u<<12|1u<<13|1u<<14|1u<<15;saver();"
        "return (int)((pushed((const unsigned char*)saver)&callee)*4+(pushed((const unsigned char*)kept)&callee)*2+"
        "((pushed((const unsigned char*)plain)&callee)!=0))+plain(n)*7+kept(n);}",
        "generic-unwind-init-saves", clang);
    // __builtin_counted_by_ref (#148): stores through the count's address, of an `->`'s and a `.`'s access, of a count in an
    // anonymous struct's enclosing record, and a null void * of a flexible array member of no count; and
    // __builtin_dynamic_object_size of clang's counted_by forms, at counts below zero and above: a flexible array member,
    // its element's address (and through a char *), another member's (an array's element too), a counted pointer and a
    // sized_by one, a count in the enclosing record, mode 3's 0, and __builtin_object_size, which ignores the count.
    check_test_program_source(test,
        "typedef unsigned long size_t;void* malloc(size_t);"
        "static unsigned long long h;static void mix(unsigned long long v){h=h*31+v;}"
        "struct S{char tag;long n;int fam[] __attribute__((counted_by(n)));};"
        "struct U{unsigned char n;short arr[3];char c;long fam[] __attribute__((counted_by(n)));};"
        "struct P{int x;unsigned short m;double* p __attribute__((counted_by(m)));char* q __attribute__((sized_by(x)));};"
        "struct N{int k;struct{int x;char fam[] __attribute__((counted_by(k)));};};"
        "struct Q{int n;int fam[];};"
        "__attribute__((noinline)) static unsigned long f0(struct S* s){return __builtin_dynamic_object_size(s->fam,0);}"
        "__attribute__((noinline)) static unsigned long f1(struct S* s,int i){return __builtin_dynamic_object_size(&s->fam[i],1);}"
        "__attribute__((noinline)) static unsigned long f2(struct S* s){return __builtin_dynamic_object_size(&s->tag,0);}"
        "__attribute__((noinline)) static unsigned long f3(struct U* u,int i){return __builtin_dynamic_object_size(&u->arr[i],2);}"
        "__attribute__((noinline)) static unsigned long f4(struct P* p,int i){return __builtin_dynamic_object_size(&p->p[i],0)+1000*__builtin_dynamic_object_size(p->q,1);}"
        "__attribute__((noinline)) static unsigned long f5(struct N* n){return __builtin_dynamic_object_size(n->fam,0);}"
        "__attribute__((noinline)) static unsigned long f6(struct S* s){return __builtin_dynamic_object_size(s->fam,3);}"
        "__attribute__((noinline)) static unsigned long f7(struct Q* q){return __builtin_dynamic_object_size(q->fam,0);}"
        "__attribute__((noinline)) static unsigned long f8(struct S* s,int i){return __builtin_dynamic_object_size(&((char*)s->fam)[i],0);}"
        "__attribute__((noinline)) static unsigned long f9(struct U* u){return __builtin_dynamic_object_size(u->fam,0);}"
        "__attribute__((noinline)) static unsigned long f10(struct S* s){return __builtin_object_size(s->fam,0);}"
        "int tested(int n){h=(unsigned)n;struct S* s=malloc(sizeof(struct S)+64);struct U* u=malloc(sizeof(struct U)+64);"
        "struct P p={5,3,0,0};struct N* m=malloc(sizeof(struct N)+8);struct Q* q=malloc(sizeof(struct Q)+8);"
        "for(long c=-2;c<=6+n;c+=4){*__builtin_counted_by_ref(s->fam)=c;u->n=(unsigned char)c;*__builtin_counted_by_ref(m->fam)=(int)c;"
        "*__builtin_counted_by_ref(p.p)=(unsigned short)(c+3);"
        "mix(f0(s));mix(f1(s,2));mix(f1(s,-1));mix(f1(s,9));mix(f2(s));mix(f3(u,1));mix(f4(&p,1));mix(f4(&p,5));mix(f5(m));mix(f6(s));"
        "mix(f7(q));mix(f8(s,3));mix(f9(u));mix(f10(s));mix((unsigned long)s->n);mix(m->k);mix(p.m);}"
        "mix((void*)__builtin_counted_by_ref(q->fam)==0);"
        "return (int)(h^(h>>32));}",
        "generic-counted-by", clang);
    // __builtin_available (#148): 1 for every platform's spec on Linux, in conditions, negated, as a value, through its
    // type's queries, which never run it, and in a loop's condition.
    check_test_program_source(test,
        "int tested(int n){int r=0;if(__builtin_available(macos 10.12,ios 10,*))r+=1;if(!__builtin_available(*))r+=2;"
        "_Bool b=__builtin_available(android 21,*);r+=b*4;r+=(int)sizeof(__builtin_available(*))*8;"
        "r+=_Generic(__builtin_available(*),_Bool:16,default:0);typeof(__builtin_available(*)) t=7;r+=t*32;"
        "for(int i=0;i<n&&__builtin_available(*);i++)r+=64;return r+(n>3?__builtin_available(swift 5,*):0)*1024;}",
        "generic-available", clang);
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
    // __builtin_function_start of a function's address and its casts, in static data and at run time, the function's start
    // (#1126's review).
    check_test_program_source(test,
        "static int callee(int x){return x*5+1;}"
        "static void* const starts[]={__builtin_function_start(&callee),__builtin_function_start((&callee)),"
        "__builtin_function_start((int(*)(void))callee),__builtin_function_start((void*)&callee),__builtin_function_start(*&callee),"
        "__builtin_function_start(1?callee:callee),__builtin_function_start(callee+0)};"
        "int tested(int n){unsigned r=(unsigned)n;for(int i=0;i<7;i++)r=r*31+(starts[i]==(void*)callee);"
        "r=r*31+(__builtin_function_start(&callee)==(void*)&callee);r=r*31+(__builtin_function_start((void(*)(void))&callee)==(void*)callee);"
        "r=r*31+(__builtin_function_start((&callee))==starts[n&3]);int (*f)(int)=(int(*)(int))__builtin_function_start(&callee);"
        "r=r*31+(unsigned)f(n);return (int)r;}",
        "generic-function-start-of-addresses", clang);
    // __builtin_FILE_NAME's and __FILE_NAME__'s component of presumed names, as clang's llvm::sys::path::filename takes it
    // (#1126's review): a root's, a trailing separator's ".", a network root name's, an empty name's and a plain path's.
    check_test_program_source(test,
        "static unsigned long long h;static void mix(unsigned long long v){h=h*31+v;}\n"
        "static void mixs(const char* s){while(*s)mix((unsigned char)*s++);mix(0);}\n"
        "int tested(int n){h=(unsigned)n;\n"
        "#line 20 \"/\"\nmixs(__builtin_FILE_NAME());mixs(__FILE_NAME__);\n"
        "#line 30 \"/foo/\"\nmixs(__builtin_FILE_NAME());mixs(__FILE_NAME__);\n"
        "#line 40 \"a/\"\nmixs(__builtin_FILE_NAME());mixs(__FILE_NAME__);\n"
        "#line 50 \"//\"\nmixs(__builtin_FILE_NAME());mixs(__FILE_NAME__);\n"
        "#line 60 \"//net\"\nmixs(__builtin_FILE_NAME());mixs(__FILE_NAME__);\n"
        "#line 70 \"\"\nmixs(__builtin_FILE_NAME());mixs(__FILE_NAME__);\n"
        "#line 80 \"../\"\nmixs(__builtin_FILE_NAME());mixs(__FILE_NAME__);\n"
        "#line 90 \"/dir/name.c\"\nmixs(__builtin_FILE_NAME());mixs(__FILE_NAME__);mixs(__builtin_FILE());\n"
        "return (int)(h^(h>>32));}\n",
        "generic-file-name-components", clang);
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
    // __builtin_setjmp and __builtin_longjmp (#148): a longjmp through eight frames of their own, after which a local the
    // function didn't change keeps its value and a volatile one has its last, four times over, and the callee-saved
    // registers the harness keeps across tested come back as they were; and the buffer's first word, the frame address.
    check_test_program_source(test,
        "static void* buf[5];static int depth;static unsigned long long h;static void mix(unsigned long long v){h=h*31+v;}\n"
        "static void deep(int n){volatile int pad[16];pad[n&15]=n;depth+=pad[n&15]+1;if(n>0)deep(n-1);else __builtin_longjmp(buf,1);}\n"
        "static int jumps(int n){int kept=n*7+1;volatile int changed=0;if(__builtin_setjmp(buf)==0){changed=5;deep(n&7);return -1;}return kept+changed*100;}\n"
        "static int frames(void){void* f[5];int r=__builtin_setjmp(f);return r==0&&f[0]==__builtin_frame_address(0);}\n"
        "int tested(int n){h=(unsigned)n;depth=0;for(int i=0;i<4;i++)mix((unsigned)jumps(n+i));mix((unsigned)depth);mix((unsigned)frames());return (int)(h^(h>>32));}\n",
        "generic-setjmp-longjmp", clang);
    // A value an expression holds across __builtin_setjmp's call, which a longjmp's resumption finds, though calls after it
    // saved values of their own below their arguments (#1242's review): an int's, a double's beside an int's, and nested
    // operands of a call, each resumed once; and a control that holds none.
    check_test_program_source(test,
        "static void* buf[5];static volatile int sink;static volatile double dsink;static unsigned long long h;static void mix(unsigned long long v){h=h*31+v;}\n"
        "__attribute__((noinline)) static int side(int x){return x+5;}__attribute__((noinline)) static double dside(double x){return x+0.5;}\n"
        "__attribute__((noinline)) static void jump(void){__builtin_longjmp(buf,1);}\n"
        "__attribute__((noinline)) static int run(int n){int result=n+100+__builtin_setjmp(buf);if(result==n+100){sink=12345+side(8);jump();}return result;}\n"
        "__attribute__((noinline)) static double drun(int k){double result=(double)k*1.5+__builtin_setjmp(buf);if(result==k*1.5){dsink=7.25+dside(1.0)+side(1);jump();}return result;}\n"
        "__attribute__((noinline)) static int nested(int a){int r=a+(a*3+(side(a)+__builtin_setjmp(buf)));if(r==a+a*3+side(a)){sink=1+(2+(3+side(4)));jump();}return r;}\n"
        "__attribute__((noinline)) static int control(void){if(__builtin_setjmp(buf)==0){sink=99+side(1);jump();}return sink;}\n"
        "int tested(int n){h=(unsigned)n;mix((unsigned)run(n));mix((unsigned)sink);mix((unsigned long long)(drun(n&7)*4));mix((unsigned long long)(dsink*4));\n"
        "mix((unsigned)nested(n&15));mix((unsigned)control());return (int)(h^(h>>32));}\n",
        "generic-setjmp-held-values", clang);
    // A long double's, an __int128's and vectors' values held across __builtin_setjmp's call in their depths' slots, which
    // calls before the longjmp reuse (#1843): the call snapshots them and puts them back. A 256-bit int vector and a
    // 512-bit float vector, and the three classes nested in one expression.
    check_test_program_source(test,
        "typedef int V __attribute__((vector_size(32)));typedef float W __attribute__((vector_size(64)));\n"
        "static void* buf[5];static volatile long double lsink;static volatile __int128 wsink;static V vsink;static W wvsink;\n"
        "static unsigned long long h;static void mix(unsigned long long v){h=h*31+v;}\n"
        "__attribute__((noinline)) static void jump(void){__builtin_longjmp(buf,1);}__attribute__((noinline)) static int side(int x){return x+5;}\n"
        "__attribute__((noinline)) static long double lside(long double x){return x+0.25L;}__attribute__((noinline)) static __int128 wside(__int128 x){return x*3;}\n"
        "__attribute__((noinline)) static V vside(V x){return x+1;}\n"
        "__attribute__((noinline)) static long double lrun(int k){long double r=(long double)k*1.5L+__builtin_setjmp(buf);if(r==k*1.5L){lsink=7.25L+lside(9.5L)*(lside(2.0L)+side(1));jump();}return r;}\n"
        "__attribute__((noinline)) static __int128 wrun(int k){__int128 r=((__int128)k<<70)+__builtin_setjmp(buf);if(r==((__int128)k<<70)){wsink=((__int128)99<<80)+wside((__int128)5<<66)*(wside(3)+side(1));jump();}return r;}\n"
        "__attribute__((noinline)) static V vrun(int k){V b={k,k+1,k+2,k+3,k+4,k+5,k+6,k+7};V r=b*3+__builtin_setjmp(buf);if(r[0]==k*3){vsink=(V){9,9,9,9,9,9,9,9}+vside(b*7)*(vside(b)+side(2));jump();}return r;}\n"
        "__attribute__((noinline)) static W wvrun(int k){W b={k,1,2,3,4,5,6,7,8,9,10,11,12,13,14,k};W r=b*2.5f+(float)__builtin_setjmp(buf);if(r[0]==k*2.5f){wvsink=(W){1}+b*(b+(float)side(3))*(b+(float)side(4));jump();}return r;}\n"
        "__attribute__((noinline)) static long double mixed(int k){long double a=k;__int128 w=k;int r=(int)(a*2+(w*3+(side(k)+__builtin_setjmp(buf))));if(r==k*5+k+5){lsink=lside(1.0L)+(long double)(wside(7)+side(1));jump();}return r+a;}\n"
        "int tested(int n){h=(unsigned)n;int k=(n&15)+1;mix((unsigned long long)(lrun(k)*4));mix((unsigned long long)(lsink*16));__int128 w=wrun(k);mix((unsigned long long)(w>>64));mix((unsigned long long)w);\n"
        "mix((unsigned long long)(wsink>>64));V v=vrun(k);for(int i=0;i<8;i++)mix((unsigned)v[i]);mix((unsigned)vsink[3]);W q=wvrun(k);for(int i=0;i<16;i++)mix((unsigned long long)(q[i]*2));\n"
        "mix((unsigned long long)wvsink[5]);mix((unsigned long long)(mixed(k)*2));return (int)(h^(h>>32));}\n",
        "generic-setjmp-held-wide", clang);
    // Builtins called through their names in parentheses (#1242's review), whose arguments run once.
    check_test_program_source(test,
        "static int calls;static unsigned next(unsigned v){calls++;return v;}\n"
        "int tested(int n){unsigned u=(unsigned)n|1u;return (int)((__builtin_popcount)(next(u))+((__builtin_clz))(u)*100+(__builtin_bswap32)(u)%1000*10000)+calls*7;}\n",
        "generic-builtin-names-in-parentheses", clang);
    // __builtin_clear_padding (#148): the bytes of objects filled with ones, then cleared, against clang's build: a struct's
    // holes and tail, bit-fields' unused bits and an unnamed one, a union's bytes no member holds, an x87 long double's last
    // six, a _BitInt's high bits, an atomic struct's, an ext vector's tail, an anonymous member's, arrays of structs in a
    // loop (and of arrays), a volatile object, and a variable length array, which clang's code leaves as it is; an element
    // past 4 KiB, and elements whose stores are many, a table's loop's (#1842).
    check_test_program_source(test,
        "void *memset(void *, int, unsigned long);static unsigned long long h;static void mix(unsigned long long v){h=h*31+v;}\n"
        "struct A{char c;int i;short s;};struct B{int x:3;int:4;unsigned y:9;char z;long double ld;};union U{char c;int i;struct{char a;short b;}s;};\n"
        "struct K{char k[3];};struct C{struct A a[2];_Complex long double z;_Atomic(struct K) at;_BitInt(37) bi;};typedef int V __attribute__((ext_vector_type(3)));\n"
        "struct D{V v;char tail;};struct E{char c;struct{char d;long e;};};struct F{char c;int a[2][2];struct{char p:1;unsigned long q:50;}bits[2];};\n"
        "static void bytes(const void* p,unsigned long n){for(unsigned long i=0;i<n;i++)mix(((const unsigned char*)p)[i]);}\n"
        "#define T(decl) do{decl;memset((void*)&v,0xff,sizeof v);__builtin_clear_padding(&v);bytes((const void*)&v,sizeof v);}while(0)\n"
        "static struct A big[300];struct G{char c[5000];int i;char d;};struct H{char c;struct A a[64];short t:3;};struct I{struct H h[3];long double x[40];};\n"
        "int tested(int n){h=(unsigned)n;T(struct A v);T(struct B v);T(union U v);T(struct C v);T(struct D v);T(struct E v);T(long double v);T(struct F v);\n"
        "T(struct A v[3]);T(struct A v[2][3]);T(struct G v);T(struct H v);T(struct I v);T(struct I v[2]);T(volatile struct A v);T(_Complex long double v);memset(big,0xff,sizeof big);__builtin_clear_padding(&big);bytes(big,sizeof big);\n"
        "int k=(n&3)+1;int vla[k];memset(vla,0xff,sizeof vla);__builtin_clear_padding(&vla);bytes(vla,sizeof vla);return (int)(h^(h>>32));}\n",
        "generic-clear-padding", clang);
    // __builtin_clear_padding of arrays whose types carry sugar (#1842's 1b), which clang's walk keeps as they are: a
    // typedef's array, typeof's, a pointer declared with parentheses (`(*p)[2]`, a parameter's too, through `*pp`), a
    // typedef of such a pointer, an array of a typedef's array, a subscript into one, and a member of a typedef's array, whose
    // record's other padding is cleared; an array of a typedef's struct is cleared.
    check_test_program_source(test,
        "void *memset(void *, int, unsigned long);static unsigned long long h;static void mix(unsigned long long v){h=h*31+v;}\n"
        "struct S{char c;int i;};typedef struct S A[2];typedef struct S TS;typedef struct S (*PA)[2];typedef A *PTA;struct M{A a;char z;int y;};\n"
        "struct Q{struct M2{A a;char z;}m;int k;};\n"
        "static void bytes(const void* p,unsigned long n){for(unsigned long i=0;i<n;i++)mix(((const unsigned char*)p)[i]);}\n"
        "#define T(decl,arg) do{decl;memset((void*)&v,0xff,sizeof v);__builtin_clear_padding(arg);bytes((const void*)&v,sizeof v);}while(0)\n"
        "static void param(struct S (*p)[2],struct S b[2][2],A *q){__builtin_clear_padding(p);__builtin_clear_padding(b);__builtin_clear_padding(q);}\n"
        "int tested(int n){h=(unsigned)n;T(struct S v[2],&v);T(A v,&v);T(TS v[2],&v);T(struct S v[2];struct S (*p)[2]=&v,p);T(struct S v[2];__typeof__(v) *q=&v,q);\n"
        "T(struct S v[2];__typeof__(struct S[2]) *q=&v,q);T(struct M v,&v);T(struct Q v,&v);T(A v[2],&v);T(A v[3],&v[1]);T(struct S v[3][2],&v[1]);\n"
        "T(struct S v[2];PA p=&v,p);T(A v;PTA p=&v,p);T(struct S v[2];struct S (*p)[2]=&v;struct S (**pp)[2]=&p,*pp);T(struct S v[2];struct S ((*p))[2]=&v,p);\n"
        "struct S b[2];struct S d[2][2];A a;memset(b,0xff,sizeof b);memset(d,0xff,sizeof d);memset(a,0xff,sizeof a);param(&b,d,&a);bytes(b,sizeof b);bytes(d,sizeof d);bytes(a,sizeof a);\n"
        "return (int)(h^(h>>32));}\n",
        "generic-clear-padding-sugar", clang);
    // __builtin_clear_padding of a type nested 90 levels (#1842), past the walk's first room, whose stack grows: before, past
    // 64 levels it was refused, and its frame written past the stack.
    check_test_program_source(test,
        "void *memset(void *, int, unsigned long);static unsigned long long h;static void mix(unsigned long long v){h=h*31+v;}\n"
        "struct L0{char c;int i;};struct L1{char c;struct L0 in;short s;};struct L2{char c;struct L1 in;short s;};\n"
        "struct L3{char c;struct L2 in;short s;};struct L4{char c;struct L3 in;short s;};struct L5{char c;struct L4 in;short s;};\n"
        "struct L6{char c;struct L5 in;short s;};struct L7{char c;struct L6 in;short s;};struct L8{char c;struct L7 in;short s;};\n"
        "struct L9{char c;struct L8 in;short s;};struct L10{char c;struct L9 in;short s;};struct L11{char c;struct L10 in;short s;};\n"
        "struct L12{char c;struct L11 in;short s;};struct L13{char c;struct L12 in;short s;};struct L14{char c;struct L13 in;short s;};\n"
        "struct L15{char c;struct L14 in;short s;};struct L16{char c;struct L15 in;short s;};struct L17{char c;struct L16 in;short s;};\n"
        "struct L18{char c;struct L17 in;short s;};struct L19{char c;struct L18 in;short s;};struct L20{char c;struct L19 in;short s;};\n"
        "struct L21{char c;struct L20 in;short s;};struct L22{char c;struct L21 in;short s;};struct L23{char c;struct L22 in;short s;};\n"
        "struct L24{char c;struct L23 in;short s;};struct L25{char c;struct L24 in;short s;};struct L26{char c;struct L25 in;short s;};\n"
        "struct L27{char c;struct L26 in;short s;};struct L28{char c;struct L27 in;short s;};struct L29{char c;struct L28 in;short s;};\n"
        "struct L30{struct L29 in[2];char t;};struct L31{char c;struct L30 in;short s;};struct L32{char c;struct L31 in;short s;};\n"
        "struct L33{char c;struct L32 in;short s;};struct L34{char c;struct L33 in;short s;};struct L35{char c;struct L34 in;short s;};\n"
        "struct L36{char c;struct L35 in;short s;};struct L37{char c;struct L36 in;short s;};struct L38{char c;struct L37 in;short s;};\n"
        "struct L39{char c;struct L38 in;short s;};struct L40{char c;struct L39 in;short s;};struct L41{char c;struct L40 in;short s;};\n"
        "struct L42{char c;struct L41 in;short s;};struct L43{char c;struct L42 in;short s;};struct L44{char c;struct L43 in;short s;};\n"
        "struct L45{char c;struct L44 in;short s;};struct L46{char c;struct L45 in;short s;};struct L47{char c;struct L46 in;short s;};\n"
        "struct L48{char c;struct L47 in;short s;};struct L49{char c;struct L48 in;short s;};struct L50{char c;struct L49 in;short s;};\n"
        "struct L51{char c;struct L50 in;short s;};struct L52{char c;struct L51 in;short s;};struct L53{char c;struct L52 in;short s;};\n"
        "struct L54{char c;struct L53 in;short s;};struct L55{char c;struct L54 in;short s;};struct L56{char c;struct L55 in;short s;};\n"
        "struct L57{char c;struct L56 in;short s;};struct L58{char c;struct L57 in;short s;};struct L59{char c;struct L58 in;short s;};\n"
        "struct L60{struct L59 in[2];char t;};struct L61{char c;struct L60 in;short s;};struct L62{char c;struct L61 in;short s;};\n"
        "struct L63{char c;struct L62 in;short s;};struct L64{char c;struct L63 in;short s;};struct L65{char c;struct L64 in;short s;};\n"
        "struct L66{char c;struct L65 in;short s;};struct L67{char c;struct L66 in;short s;};struct L68{char c;struct L67 in;short s;};\n"
        "struct L69{char c;struct L68 in;short s;};struct L70{char c;struct L69 in;short s;};struct L71{char c;struct L70 in;short s;};\n"
        "struct L72{char c;struct L71 in;short s;};struct L73{char c;struct L72 in;short s;};struct L74{char c;struct L73 in;short s;};\n"
        "struct L75{char c;struct L74 in;short s;};struct L76{char c;struct L75 in;short s;};struct L77{char c;struct L76 in;short s;};\n"
        "struct L78{char c;struct L77 in;short s;};struct L79{char c;struct L78 in;short s;};struct L80{char c;struct L79 in;short s;};\n"
        "struct L81{char c;struct L80 in;short s;};struct L82{char c;struct L81 in;short s;};struct L83{char c;struct L82 in;short s;};\n"
        "struct L84{char c;struct L83 in;short s;};struct L85{char c;struct L84 in;short s;};struct L86{char c;struct L85 in;short s;};\n"
        "struct L87{char c;struct L86 in;short s;};struct L88{char c;struct L87 in;short s;};struct L89{char c;struct L88 in;short s;};\n"
        "static struct L89 deep;int tested(int n){h=(unsigned)n;memset(&deep,0xff,sizeof deep);__builtin_clear_padding(&deep);\n"
        "for(unsigned long i=0;i<sizeof deep;i++)mix(((const unsigned char*)&deep)[i]);return (int)(h^(h>>32));}\n",
        "generic-clear-padding-deep", clang);
    // va_arg of a Microsoft list (#148, clang's EmitMSVAArg) against clang's build: 8-byte slots built by hand, read as an
    // int, a float and a double, structs of 3, 4, 8 and 16 bytes (the 3- and 16-byte ones through their slots' pointers),
    // a long double, a _Complex double and an __int128 through theirs, then __builtin_ms_va_copy into a list through a
    // pointer, __builtin_ms_va_end, and va_arg through that pointer and of the copy, whose place is the last value.
    check_test_program_source(test,
        "void *memcpy(void *, const void *, unsigned long); static unsigned long long h; static void mix(unsigned long long v) { h = h * 31 + v; }\n"
        "struct S3 { char c[3]; }; struct S4 { short a, b; }; struct S8 { int a, b; }; struct S16 { long a, b; };\n"
        "static unsigned long long slots[16]; static struct S3 s3 = { { 1, 2, 3 } }; static struct S16 s16 = { 11, 12 };\n"
        "static long double ld = 2.5L; static _Complex double cd = 1.0 + 2.0i; static __int128 wide = ((__int128)7 << 64) | 9;\n"
        "static double take(char** list) { return __builtin_va_arg(*list, double); }\n"
        "static void copy_end(__builtin_ms_va_list* to, __builtin_ms_va_list from) { __builtin_ms_va_copy(*to, from); __builtin_ms_va_end(from); }\n"
        "int tested(int n) { h = (unsigned)n; struct S4 s4 = { 5, 6 }; struct S8 s8 = { 7, 8 }; float f = 1.5f; double d = 3.25;\n"
        "unsigned long long k = 0; slots[k++] = (unsigned)n; memcpy(&slots[k++], &f, sizeof f); memcpy(&slots[k++], &d, sizeof d);\n"
        "slots[k++] = (unsigned long long)&s3; memcpy(&slots[k++], &s4, sizeof s4); memcpy(&slots[k++], &s8, sizeof s8);\n"
        "slots[k++] = (unsigned long long)&s16; slots[k++] = (unsigned long long)&ld; slots[k++] = (unsigned long long)&cd;\n"
        "slots[k++] = (unsigned long long)&wide; slots[k++] = (unsigned long long)(long long)-3; memcpy(&slots[k++], &d, sizeof d);\n"
        "slots[k++] = 0x1234; char* ap = (char*)slots; __builtin_ms_va_list copy; mix((unsigned)__builtin_va_arg(ap, int));\n"
        "mix((unsigned long long)(__builtin_va_arg(ap, float) * 4)); mix((unsigned long long)(__builtin_va_arg(ap, double) * 4));\n"
        "struct S3 r3 = __builtin_va_arg(ap, struct S3); mix(r3.c[0] + r3.c[1] * 10 + r3.c[2] * 100); struct S4 r4 = __builtin_va_arg(ap, struct S4);\n"
        "mix(r4.a * 10 + r4.b); struct S8 r8 = __builtin_va_arg(ap, struct S8); mix(r8.a * 10 + r8.b);\n"
        "struct S16 r16 = __builtin_va_arg(ap, struct S16); mix(r16.a * 100 + r16.b);\n"
        "mix((unsigned long long)(__builtin_va_arg(ap, long double) * 8)); _Complex double rc = __builtin_va_arg(ap, _Complex double);\n"
        "mix((unsigned long long)(__real__ rc * 10 + __imag__ rc)); __int128 rw = __builtin_va_arg(ap, __int128);\n"
        "mix((unsigned long long)(rw >> 64) * 100 + (unsigned long long)rw); mix((unsigned long long)__builtin_va_arg(ap, long long));\n"
        "copy_end(&copy, ap); mix((unsigned long long)(take(&copy) * 4)); mix((unsigned)__builtin_va_arg(copy, short));\n"
        "mix((unsigned long long)(copy - (char*)slots)); return (int)(h ^ (h >> 32)); }\n",
        "generic-microsoft-va-arg", clang);
    // __builtin_infer_alloc_token (#148) against clang's build: the tokens of scalars, pointers, arrays, function pointers,
    // records with and without pointers (through members, nested records and uintptr_t's typedefs), typedef-named and
    // nested records, an atomic and a qualified type, by sizeof's type or its expression's, through arithmetic and a
    // variable's initializer, and the fallback 0 of no type; arguments never run. (An unnamed record's name holds its file's
    // path, which differs between this build's source and clang's.)
    check_test_program_source(test,
        "typedef unsigned long uintptr_t; typedef uintptr_t handle; static unsigned long long h; static void mix(unsigned long long v) { h = h * 31 + v; }\n"
        "struct I; struct P { struct I* p; }; struct Q { int a[3]; long b; }; typedef struct { char c; } T; enum E { E0 }; union U { int a; float b; };\n"
        "struct R { struct R* next; }; struct W { handle u; }; struct O { struct N { int x; } n; struct { char c; } a; }; struct V { struct Q q[2]; struct W w; };\n"
        "int tested(int n) { int i = n; unsigned long s = sizeof(struct Q) * 2; h = (unsigned)n; mix(__builtin_infer_alloc_token(i++));\n"
        "mix(__builtin_infer_alloc_token(sizeof(struct I*))); mix(__builtin_infer_alloc_token(sizeof(struct P))); mix(__builtin_infer_alloc_token(s));\n"
        "mix(__builtin_infer_alloc_token(3 + sizeof(T))); mix(__builtin_infer_alloc_token(sizeof(enum E))); mix(__builtin_infer_alloc_token(sizeof(union U)));\n"
        "mix(__builtin_infer_alloc_token(sizeof(struct R))); mix(__builtin_infer_alloc_token(sizeof(uintptr_t))); mix(__builtin_infer_alloc_token(sizeof(struct W)));\n"
        "mix(__builtin_infer_alloc_token(sizeof(int[4]))); mix(__builtin_infer_alloc_token(sizeof(int (*)(int)))); mix(__builtin_infer_alloc_token(sizeof(const char*)));\n"
        "mix(__builtin_infer_alloc_token(sizeof i)); mix(__builtin_infer_alloc_token(4, sizeof(double))); mix(__builtin_infer_alloc_token(sizeof(_Atomic(int))));\n"
        "mix(__builtin_infer_alloc_token(sizeof(volatile int))); mix(__builtin_infer_alloc_token(sizeof(struct N)));\n"
        "mix(__builtin_infer_alloc_token(n * sizeof(struct V) << 1)); mix(__builtin_infer_alloc_token((sizeof(struct V) / 2) - 1)); mix((unsigned)i);\n"
        "return (int)(h ^ (h >> 32)); }\n",
        "generic-infer-alloc-token", clang);
    // __builtin_os_log_format's buffers against clang's build (#148, the bytes of clang's helper's stores): privacy
    // annotations (sensitive over a later public), scalars of each promoted size, %m and %%, a string's count from its
    // precision's argument and from a constant, %.8P, masks, a field width and a precision of arguments, a double, a
    // pointer, and an array (its address, compared in place, since the two builds place it apart); buffer sizes of
    // positions, of a %P without its length and of missing data; and escaped percent signs, with specifiers past the
    // format's NUL that take nothing (#1329).
    check_test_program_source(test,
        "static unsigned long long h; static void mix(unsigned long long v) { h = h * 31 + v; }\n"
        "static unsigned char buffer[512]; static char arr[4] = { 1, 2, 3, 4 };\n"
        "static void bytes(unsigned long n) { for (unsigned long i = 0; i < n; i++) mix(buffer[i]); } int tested(int n) { h = (unsigned)n;\n"
        "const char* s = (const char*)(unsigned long)(0x1000 + n);\n"
        "char c = (char)(n + 3); short w = (short)(n * 7); long long q = -n; float f = 1.5f * n; double d = 0.25 * n;\n"
        "void* r = __builtin_os_log_format(buffer, \"%d %s %{public}s\", n, s, s);\n"
        "mix(r == (void*)buffer); bytes(__builtin_os_log_format_buffer_size(\"%d %s %{public}s\", n, s, s));\n"
        "__builtin_os_log_format(buffer, \"%{private}c %hd %lld %f %{sensitive, public}lu\", c, w, q, f, (unsigned long)n);\n"
        "bytes(__builtin_os_log_format_buffer_size(\"%{private}c %hd %lld %f %{sensitive, public}lu\", c, w, q, f, (unsigned long)n));\n"
        "__builtin_os_log_format(buffer, \"%m %% %.*s %.8P %{mask.abc}d %*.*d\", 3, s, s, n, 4, 2, n);\n"
        "bytes(__builtin_os_log_format_buffer_size(\"%m %% %.*s %.8P %{mask.abc}d %*.*d\", 3, s, s, n, 4, 2, n));\n"
        "__builtin_os_log_format(buffer, \"%{public,mask.xy}.16s %e %p %s\", s, d, (void*)s, arr);\n"
        "unsigned long size = __builtin_os_log_format_buffer_size(\"%{public,mask.xy}.16s %e %p %s\", s, d, (void*)s, arr);\n"
        "unsigned long v = 0; for (int i = 0; i < 8; i++) v |= (unsigned long)buffer[size - 8 + i] << (8 * i);\n"
        "mix(v == (unsigned long)arr); bytes(size - 8);\n"
        "mix(__builtin_os_log_format_buffer_size(\"plain\") + __builtin_os_log_format_buffer_size(\"%2$d %1$s\", s, n) * 100 +\n"
        "__builtin_os_log_format_buffer_size(\"%P %d\", n, n) * 10000 + __builtin_os_log_format_buffer_size(\"%d %d\", n) * 1000000);\n"
        "__builtin_os_log_format(buffer, \"%%%% %{public}d %%\\0%s %d\", n); bytes(__builtin_os_log_format_buffer_size(\"%%%% %{public}d %%\\0%s %d\", n));\n"
        "return (int)(h ^ (h >> 32)); }\n",
        "generic-os-log", clang);
    // __builtin_os_log_format's data in its items' order (#148, as clang's emitBuiltinOSLogFormat evaluates it): positions
    // that take the data out of order (their calls' order checked), data no item takes (its call never made), data taken
    // again where no side effect ran since (a field width's and a precision's argument too); and data of 16 bytes or of
    // 2, unpromoted: a long double's 10 bytes then zeros, an __int128, a 16-byte vector, _Float16 and __bf16 (an __fp16 as
    // a double), and signed _BitInts narrower than their classes, zero-extended from their widths, as clang stores them.
    check_test_program_source(test,
        "static unsigned long long h; static void mix(unsigned long long v) { h = h * 31 + v; }\n"
        "static unsigned char buffer[512]; static int order[8]; static int made;\n"
        "static int next(int v) { order[made++ & 7] = v; return v; }\n"
        "static void bytes(unsigned long n) { for (unsigned long i = 0; i < n; i++) mix(buffer[i]); }\n"
        "typedef float V4 __attribute__((vector_size(16))); typedef short V2 __attribute__((vector_size(4)));\n"
        "int tested(int n) { h = (unsigned)n;\n"
        "__builtin_os_log_format(buffer, \"%3$d %1$d %2$d\", next(n), next(n + 1), next(n + 2));\n"
        "bytes(__builtin_os_log_format_buffer_size(\"%3$d %1$d %2$d\", 1, 2, 3)); for (int i = 0; i < made; i++) mix(order[i]); made = 0;\n"
        "__builtin_os_log_format(buffer, \"%d\", n, next(5)); bytes(__builtin_os_log_format_buffer_size(\"%d\", 1, 2)); mix(made);\n"
        "__builtin_os_log_format(buffer, \"%1$d %1$d %2$d\", n * 3, next(n)); bytes(__builtin_os_log_format_buffer_size(\"%1$d %1$d %2$d\", 1, 2));\n"
        "__builtin_os_log_format(buffer, \"%2$*1$d %2$.*1$d\", n, n + 7); bytes(__builtin_os_log_format_buffer_size(\"%2$*1$d %2$.*1$d\", 1, 1));\n"
        "long double l = -1.5L * n; __int128 w = -(__int128)n << 70; V4 v = { 1.0f, 2.0f, -1.0f, (float)n }; V2 s = { (short)-n, 7 };\n"
        "__builtin_os_log_format(buffer, \"%Lf %d %d %d\", l, w, v, s); bytes(__builtin_os_log_format_buffer_size(\"%Lf %d %d %d\", l, w, v, s));\n"
        "_Float16 f = (_Float16)(n * 0.5); __bf16 b = (__bf16)(n * 0.25f); __fp16 p = (__fp16)n;\n"
        "__builtin_os_log_format(buffer, \"%f %f %f\", f, b, p); bytes(__builtin_os_log_format_buffer_size(\"%f %f %f\", f, b, p));\n"
        "_BitInt(24) a = -n; _BitInt(7) c = -n; _BitInt(72) d = -n; _BitInt(40) e = -n; unsigned _BitInt(24) u = (unsigned _BitInt(24))-n;\n"
        "__builtin_os_log_format(buffer, \"%d %d %d %d %d\", a, c, d, e, u); bytes(__builtin_os_log_format_buffer_size(\"%d %d %d %d %d\", a, c, d, e, u));\n"
        "return (int)(h ^ (h >> 32)); }\n",
        "generic-os-log-order", clang);
    // __builtin_os_log_format's data evaluated once an item (#148, as clang's emitBuiltinOSLogFormat evaluates each item's
    // expression): data taken again after a side effect ran, and an item's call made again; and a __float128's 16 bytes
    // and a 2-byte vector's, read as they are evaluated.
    check_test_program_source(test,
        "static unsigned long long h; static void mix(unsigned long long v) { h = h * 31 + v; }\n"
        "static unsigned char buffer[512]; static int made;\n"
        "static int next(int v) { made++; return v + made; }\n"
        "static void bytes(unsigned long n) { for (unsigned long i = 0; i < n; i++) mix(buffer[i]); }\n"
        "typedef char V2 __attribute__((vector_size(2)));\n"
        "int tested(int n) { h = (unsigned)n; int g = n;\n"
        "__builtin_os_log_format(buffer, \"%1$d %2$d %1$d\", g, next(n)); bytes(__builtin_os_log_format_buffer_size(\"%1$d %2$d %1$d\", 1, 2));\n"
        "__builtin_os_log_format(buffer, \"%1$d %2$d %1$d\", made, next(n)); bytes(__builtin_os_log_format_buffer_size(\"%1$d %2$d %1$d\", 1, 2));\n"
        "__builtin_os_log_format(buffer, \"%1$d %1$d %1$d\", next(n)); bytes(__builtin_os_log_format_buffer_size(\"%1$d %1$d %1$d\", 1)); mix(made);\n"
        "__float128 q = (__float128)n / 3; V2 v = { (char)n, (char)-n };\n"
        "__builtin_os_log_format(buffer, \"%f %d %2$d %1$f\", q, v); bytes(__builtin_os_log_format_buffer_size(\"%f %d %2$d %1$f\", q, v));\n"
        "return (int)(h ^ (h >> 32)); }\n",
        "generic-os-log-reevaluated", clang);
    // __builtin_os_log_format past the operands' 32-bit mask (#1785, clang's BuiltinOSLogFormat takes up to 255 data
    // arguments): 40 data arguments, those past the 32nd of other types and one a call of a static function nothing else
    // names (so the program reaches it through the full mask), positions past the 32nd, and 255 data arguments, the most
    // clang takes, each with its buffer's size.
    check_test_program_source(test,
        "static unsigned long long h; static void mix(unsigned long long v) { h = h * 31 + v; }\n"
        "static unsigned char buffer[2048]; static int made;\n"
        "static int late(int v) { made++; return v * 5 + made; }\n"
        "static void bytes(unsigned long n) { for (unsigned long i = 0; i < n; i++) mix(buffer[i]); }\n"
        "#define F8 \"%d %d %d %d %d %d %d %d \"\n#define A8(x) x, x + 1, x + 2, x + 3, x + 4, x + 5, x + 6, x + 7\n"
        "#define F32 F8 F8 F8 F8\n#define A32(x) A8(x), A8(x + 8), A8(x + 16), A8(x + 24)\n"
        "#define F255 F32 F32 F32 F32 F32 F32 F32 F8 F8 F8 \"%d %d %d %d %d %d %d\"\n"
        "int tested(int n) { h = (unsigned)n; long long q = -n; double d = 0.5 * n;\n"
        "__builtin_os_log_format(buffer, F32 \"%d %lld %f %{private}d %hhd %hd %d %d\", A32(n), late(n), q, d, n * 3, (char)n, (short)-n, made, n);\n"
        "bytes(__builtin_os_log_format_buffer_size(F32 \"%d %lld %f %{private}d %hhd %hd %d %d\", A32(n), late(n), q, d, n * 3, (char)n, (short)-n, made, n));\n"
        "__builtin_os_log_format(buffer, \"%40$d %33$d %1$d %35$f\", A32(n), late(n), 0, d, 0, 0, 0, 0, n - 9);\n"
        "bytes(__builtin_os_log_format_buffer_size(\"%40$d %33$d %1$d %35$f\", A32(n), late(n), 0, d, 0, 0, 0, 0, n - 9)); mix(made);\n"
        "__builtin_os_log_format(buffer, F255, A32(n), A32(n + 1), A32(n + 2), A32(n + 3), A32(n + 4), A32(n + 5), A32(n + 6),\n"
        "A8(n), A8(-n), A8(n * 2), n, n + 1, n + 2, n + 3, n + 4, n + 5, late(n));\n"
        "unsigned long size = __builtin_os_log_format_buffer_size(F255, A32(0), A32(1), A32(2), A32(3), A32(4), A32(5), A32(6),\n"
        "A8(0), A8(1), A8(2), 0, 1, 2, 3, 4, 5, 6); mix(size); bytes(size); mix(made);\n"
        "return (int)(h ^ (h >> 32)); }\n",
        "generic-os-log-many", clang);
    // __builtin_dump_struct (#148): clang's calls of the printer, a line each, hashed by a printer that reads each format's
    // operands as its conversions say (a pointer as its offset from the record): the record's name, each field's type
    // name as its declaration spells it (typedefs kept, an unnamed record's "struct (unnamed)"), its name, a bit-field's
    // width, and its value promoted (bool, the character types, the integers, size_t's %zu, an enum's underlying type,
    // the floating types, strings of at most 32 characters, pointers) or its address (an array, an __int128, a complex);
    // nested records' fields deeper, anonymous members' flattened, unnamed bit-fields skipped; the printer and the rest
    // of the arguments evaluated again for each call (a call through a pointer, the rest's side effects), the record
    // once; a union's dump, a nested record's and a typedef'd unnamed record's.
    check_test_program_source(test,
        "static unsigned long long h; static void mix(unsigned long long v) { h = h * 31 + v; }\n"
        "static const char* base;\n"
        "static int pr(int tag, const char* f, ...) { __builtin_va_list ap; __builtin_va_start(ap, f); mix((unsigned)tag);\n"
        "for (const char* p = f; *p; p++) { mix((unsigned char)*p); if (*p != '%') continue; p++;\n"
        "while (*p == '.' || (*p >= '0' && *p <= '9')) { mix((unsigned char)*p); p++; }\n"
        "int l = 0, L = 0; while (*p == 'h' || *p == 'l' || *p == 'z' || *p == 'j' || *p == 't' || *p == 'L') { l += *p == 'l' || *p == 'z' || *p == 'j' || *p == 't'; L |= *p == 'L'; mix((unsigned char)*p); p++; }\n"
        "mix((unsigned char)*p);\n"
        "if (*p == 's') { const char* s = __builtin_va_arg(ap, const char*); for (int i = 0; s && s[i] && i < 32; i++) mix((unsigned char)s[i]); }\n"
        "else if (*p == 'p') { const char* q = __builtin_va_arg(ap, const char*); mix(q ? (unsigned long long)(q - base) : 7777); }\n"
        "else if (*p == 'f' && L) { long double d = __builtin_va_arg(ap, long double); mix((unsigned long long)(long long)(d * 1000)); }\n"
        "else if (*p == 'f') { double d = __builtin_va_arg(ap, double); mix((unsigned long long)(long long)(d * 1000)); }\n"
        "else if (l) mix((unsigned long long)__builtin_va_arg(ap, long)); else mix((unsigned long long)__builtin_va_arg(ap, int)); }\n"
        "__builtin_va_end(ap); return 0; }\n"
        "typedef unsigned long size_t; typedef signed char i8; typedef struct { int q; short r; } Anon; enum Color { RED, GREEN }; enum Neg { MINUS = -1 };\n"
        "struct In { int x; long y; };\n"
        "struct S { _Bool b; char c; i8 sc; unsigned char uc; short s; unsigned short us; int i; unsigned u; long l; unsigned long long ull;\n"
        "float f; double d; long double ld; const char* str; char* mstr; void* vp; int* np; int arr[2]; size_t z; enum Color col; enum Neg ng;\n"
        "struct In in; Anon an; int bf : 3; unsigned ubf : 5; int : 4; _Bool bb : 1; long long lbf : 40; enum Color cbf : 2;\n"
        "struct { int ia; union { int ib; float fc; }; }; struct { int named_in; } named; union U { int ux; char uy; } un; __int128 w; _Complex double cd; };\n"
        "static int calls; static int next(void) { return calls++; }\n"
        "int tested(int n) { h = (unsigned)n; calls = 0; struct S s = { 0 }; base = (const char*)&s;\n"
        "s.b = n & 1; s.c = (char)-n; s.sc = (i8)(n - 100); s.uc = (unsigned char)(200 + n); s.s = (short)(-300 * n); s.us = (unsigned short)(65000 + n);\n"
        "s.i = -7 * n; s.u = 4000000000u + (unsigned)n; s.l = -8L * n; s.ull = 11ULL << n; s.f = 1.5f * n; s.d = -2.25 * n; s.ld = 3.125L * n;\n"
        "s.str = \"hello\"; s.mstr = \"a string longer than thirty-two characters for sure\"; s.vp = &s.i; s.z = (size_t)n * 3; s.col = GREEN; s.ng = MINUS;\n"
        "s.in.x = n; s.in.y = -n; s.an.q = 3; s.an.r = (short)n; s.bf = -2; s.ubf = (unsigned)n & 31; s.bb = 1; s.lbf = -12345678901LL * n; s.cbf = GREEN;\n"
        "s.ia = 13; s.ib = n; s.named.named_in = 15; s.un.ux = 0x41 + n;\n"
        "__builtin_dump_struct(&s, pr, n);\n"
        "int (*volatile through)(int, const char*, ...) = pr; __builtin_dump_struct(&s.in, through, next());\n"
        "Anon a = { n, 6 }; __builtin_dump_struct(&a, pr, next() * 10);\n"
        "union U u = { n }; __builtin_dump_struct(&u, pr, 3);\n"
        "__builtin_dump_struct(&s.named, pr, 4); mix((unsigned)calls);\n"
        "return (int)(h ^ (h >> 32)); }\n",
        "generic-dump-struct", clang);
    // __builtin_invoke (#148): calls through it, of a function, a pointer, a member and a variadic function, each argument
    // evaluated once, as the call's.
    check_test_program_source(test,
        "static int calls;static int next(int v){calls++;return v;}static int twice(int x){return 2*x;}static int sum(int n,...){return n;}\n"
        "struct S{int (*fn)(int);};\n"
        "int tested(int n){int (*p)(int)=twice;struct S s={twice};return __builtin_invoke(twice,next(n))+__builtin_invoke(p,3)*10+__builtin_invoke(*p,1)*100+\n"
        "__builtin_invoke(s.fn,next(2))*1000+__builtin_invoke(sum,7,1.5,\"x\")*10000+calls;}\n",
        "generic-invoke", clang);
    // clang's type traits of C (#148), their values in code: of complete, incomplete and variably modified types.
    check_test_program_source(test,
        "struct T{int x;};\n"
        "int tested(int n){int k=n>0?n:1;return __is_destructible(int)+__is_destructible(void)*2+__is_destructible(int[])*4+__is_destructible(int[k])*8+\n"
        "__is_nothrow_destructible(struct T)*16+__is_bitwise_cloneable(int[])*32+__is_bitwise_cloneable(void(void))*64+__is_bitwise_cloneable(void)*128+\n"
        "__is_bitwise_cloneable(int[k][2])*256+n*1000;}\n",
        "generic-type-traits", clang);
    // __builtin_vectorelements of a comma expression (#1126's review), its last operand's type, none of its operands
    // evaluated (a call among them).
    check_test_program_source(test,
        "typedef int A __attribute__((ext_vector_type(2)));typedef int B __attribute__((ext_vector_type(4)));typedef double C __attribute__((vector_size(32)));\n"
        "static int calls;static A side(void){calls++;A a={0};return a;}\n"
        "int tested(int n){A a={n};B b={n};C c={0};unsigned long r=__builtin_vectorelements(a,b)*1000+__builtin_vectorelements(b,a)*100+\n"
        "__builtin_vectorelements(side(),b,c)*10+__builtin_vectorelements(B);return (int)r+calls*100000+n;}\n",
        "generic-vectorelements-of-comma-expressions", clang);
    // A place through macros (#1126's review): clang's SourceLocExpr reads getExpansionRange(...).getEnd(), the outermost
    // expansion's end: a function-like invocation's closing parenthesis (on a later line too), an object-like macro's
    // name, a nested one's outer invocation, an argument's invocation (of a builtin and of another macro), in constant
    // expressions and at run time, before and after a #line. (The first #line keeps the lines the oracle's added macro
    // would shift.)
    check_test_program_source(test,
        "#line 1 \"macro-places.c\"\n"
        "#define COLUMN() __builtin_COLUMN()\n"
        "#define LINE() __builtin_LINE()\n"
        "#define WRAPPED() COLUMN()\n"
        "#define OBJECT __builtin_COLUMN()\n"
        "#define WRAPPEDARG(x) COLUMN()\n"
        "#define ID(x) x\n"
        "#define WHERE() (__builtin_LINE() * 1000 + __builtin_COLUMN())\n"
        "static unsigned long long h;static void mix(unsigned long long v){h=h*31+v;}\n"
        "enum{S=COLUMN()*1000+WRAPPED(),T=OBJECT*1000+WRAPPEDARG(1),U=ID(LINE()\n)};\n"
        "int tested(int n){h=(unsigned)n;mix(S);mix(T);mix(U);mix(COLUMN());mix(WRAPPED());mix(OBJECT);mix(WRAPPEDARG(42));\n"
        "mix(ID(__builtin_COLUMN()));mix(ID(COLUMN()));mix(LINE(\n));mix(ID(\n  __builtin_LINE()));mix(WHERE(\n   ));\n"
        "#line 300 \"moved.c\"\n"
        "mix(WRAPPED());mix(LINE());mix(ID(WRAPPED()));mix(WHERE());mix(OBJECT);\n"
        "return (int)(h^(h>>32));}\n",
        "generic-source-places-through-macros", clang);
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

// __builtin_cpu_init with libgcc's model, of this host, against clang's build (the table's every name is
// test-checker-driver's, c_check_library_driver_test.c's check_test_cpu_model_runs).
static __attribute__((noinline)) void check_test_generic_cpu_programs(CheckTest* test, const char* clang)
{
    check_test_program_source(test,
        "static unsigned long long h;static void mix(unsigned long long v){h=h*31+v;}"
        "int tested(int n){h=(unsigned)n;__builtin_cpu_init();mix(__builtin_cpu_supports(\"sse2\"));mix(__builtin_cpu_supports(\"avx2\"));"
        "mix(__builtin_cpu_supports(\"avx512f\"));mix(__builtin_cpu_supports(\"x86-64-v3\"));mix(__builtin_cpu_supports(\"x86-64-v4\"));"
        "mix(__builtin_cpu_is(\"intel\"));mix(__builtin_cpu_is(\"amd\"));mix(__builtin_cpu_is(\"znver5\"));mix(__builtin_cpu_is(\"amdfam1ah\"));"
        "return (int)(h^(h>>32));}",
        "generic-cpu-init", clang);
}

static __attribute__((noinline)) void check_test_generic_builtins(CheckTest* test, const char* clang)
{
    check_test_generic_builtin_sources(test, clang);
    check_test_generic_builtin_programs(test, clang);
    check_test_generic_cpu_programs(test, clang);
}

#endif
