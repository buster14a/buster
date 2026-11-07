// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// x86's interrupt attribute (#147) by the driver against clang, on original fixtures: each unit's whole output (its
// diagnostics, their places, their notes, and those clang gives no place, which check_test_interrupt_lines keeps too) and
// status must be clang's by the vector path, the scalar one and deferred bodies on two lanes. They are its handler's errors
// (a result that isn't void at its type's start, a count of parameters, a first parameter that is no pointer and a second
// that is no unsigned 64-bit integer, at theirs, and those of no place: a function pointer's, a typedef's, a function
// declared by a typedef's name, one returning a pointer to an array or a function), its subjects' warning, its arguments
// (expressions it never counts), its places in C23's syntax, redeclarations (inheriting it, and its two warnings after a
// definition, the implicit used's of no place), its implicit used (-Wunused-function), K&R definitions, and calls: the
// direct call of an interrupt service routine (through parentheses, selections, __extension__ and an address-of, and in
// sizeof), and -Wexcessive-regsave in interrupt service routines and no_caller_saved_registers functions, with its notes
// (a builtin's at its first use), its order among nested calls, and its options and pragmas. Then a definition's -c,
// which the lowering refuses by name until the backend compiles its frame (check_program_interrupt).

// Keeps `text`'s diagnostic lines, as check_test_diagnostic_lines does, and those of no place ("error: ...").
static bool check_test_interrupt_lines(const char* text, char* lines, size_t capacity)
{
    size_t length = 0;
    bool fits = capacity != 0;
    if (fits)
    {
        lines[0] = 0;
    }
    for (const char* line = text; fits && line != NULL && *line != 0;)
    {
        const char* end = strchr(line, '\n');
        const size_t size = end != NULL ? (size_t)(end - line) : strlen(line);
        const bool unplaced = (size >= 7 && memcmp(line, "error: ", 7) == 0) || (size >= 9 && memcmp(line, "warning: ", 9) == 0) ||
                              (size >= 6 && memcmp(line, "note: ", 6) == 0);
        const bool kept = unplaced || memmem(line, size, ": error: ", 9) != NULL || memmem(line, size, ": warning: ", 11) != NULL ||
                          memmem(line, size, ": note: ", 8) != NULL;
        fits = !kept || size + 1 < capacity - length;
        if (kept && fits)
        {
            memcpy(lines + length, line, size);
            length += size;
            lines[length++] = '\n';
            lines[length] = 0;
        }
        line = end != NULL ? end + 1 : NULL;
    }
    return fits;
}

typedef struct CheckInterruptFixture CheckInterruptFixture;
struct CheckInterruptFixture
{
    const char* source;
    // The standard (GNU C23 when null) and up to two more options.
    const char* standard;
    const char* options[2];
};

static void check_test_driver_interrupts(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const CheckInterruptFixture units[] = {
        // The result's type, at its start: past specifiers, qualifiers and attributes, a typedef's name, _Complex's
        // type's, a grouped name's; and of none where the result is written around the name.
        { "__attribute__((interrupt)) int f(void *p);\n", NULL, { NULL } },
        { "int f(void *p) __attribute__((interrupt));\n", NULL, { NULL } },
        { "static const unsigned long f(void *p) __attribute__((interrupt));\n", NULL, { NULL } },
        { "__attribute__((interrupt)) _Complex double f(void *p);\n", NULL, { NULL } },
        { "typedef int T; __attribute__((interrupt)) const T f(void *p);\n", NULL, { NULL } },
        { "__attribute__((interrupt)) int (f)(void *p);\n", NULL, { NULL } },
        { "int (__attribute__((interrupt)) f)(void *p);\n", NULL, { NULL } },
        { "__attribute__((interrupt)) int *f(void *p) { return 0; }\n", NULL, { NULL } },
        { "__attribute__((interrupt)) int (*f(void *p))[3];\n", NULL, { NULL } },
        { "__attribute__((interrupt)) void (*f(void *p))(int);\n", NULL, { NULL } },
        { "__attribute__((interrupt)) const void f(void *p);\n", NULL, { NULL } },
        // The parameters' count, at the declaration's start (C23 attributes before it aren't).
        { "__attribute__((interrupt)) void f(void) {}\n", NULL, { NULL } },
        { "[[gnu::interrupt]] static void f(void);\n", NULL, { NULL } },
        { "__attribute__((interrupt)) void f(void *p, unsigned long e, int z);\n", NULL, { NULL } },
        { "__attribute__((interrupt)) void f(...);\n", NULL, { NULL } },
        { "__attribute__((interrupt)) void f(void *p, ...);\n", NULL, { NULL } },
        { "__attribute__((interrupt)) void f(void *p, unsigned long e, ...);\n", NULL, { NULL } },
        // The first parameter, a pointer (adjusted from an array or a function), at its declaration's start.
        { "__attribute__((interrupt)) void f(int p);\n", NULL, { NULL } },
        { "__attribute__((interrupt)) void f(__attribute__((unused)) int p);\n", NULL, { NULL } },
        { "__attribute__((interrupt)) void f([[maybe_unused]] const int p);\n", NULL, { NULL } },
        { "__attribute__((interrupt)) void f(int, unsigned long);\n", NULL, { NULL } },
        { "__attribute__((interrupt)) void f(int a[], unsigned long e);\n", NULL, { NULL } },
        { "__attribute__((interrupt)) void f(void g(void));\n", NULL, { NULL } },
        { "__attribute__((interrupt)) void f(const void *p);\n", NULL, { NULL } },
        // The second, an unsigned integer of 64 bits.
        { "__attribute__((interrupt)) void f(void *p, unsigned int e);\n", NULL, { NULL } },
        { "__attribute__((interrupt)) void f(void *p, long e);\n", NULL, { NULL } },
        { "__attribute__((interrupt)) void f(void *p, _Atomic unsigned long e);\n", NULL, { NULL } },
        { "__attribute__((interrupt)) void f(void *p, register unsigned short);\n", NULL, { NULL } },
        { "__attribute__((interrupt)) void f(void *p, _BitInt(64) e);\n", NULL, { NULL } },
        { "__attribute__((interrupt)) void f(void *p, unsigned long *e);\n", NULL, { NULL } },
        { "__attribute__((interrupt)) void f(void *p, bool e);\n", NULL, { NULL } },
        { "__attribute__((interrupt)) void f(void *p, unsigned long e);\n", NULL, { NULL } },
        { "__attribute__((interrupt)) void f(void *p, unsigned long long e);\n", NULL, { NULL } },
        { "__attribute__((interrupt)) void f(void *p, unsigned _BitInt(64) e);\n", NULL, { NULL } },
        { "enum E : unsigned long { A }; __attribute__((interrupt)) void f(void *p, enum E e);\n", NULL, { NULL } },
        { "typedef unsigned long u64; __attribute__((interrupt)) void f(void *const p, const u64 e);\n", NULL, { NULL } },
        // Each declarator of shared specifiers; functions declared by a typedef's name, their parameters at it.
        { "__attribute__((interrupt)) void f(void *p), g(int);\n", NULL, { NULL } },
        { "typedef void F(int); __attribute__((interrupt)) F f;\n", NULL, { NULL } },
        { "typedef void F(void *, int); __attribute__((interrupt)) F f;\n", NULL, { NULL } },
        { "typedef int F(void *); __attribute__((interrupt)) F f;\n", NULL, { NULL } },
        { "typedef void F(void); __attribute__((interrupt)) F f;\n", NULL, { NULL } },
        // Its subjects: functions and pointers to functions with prototypes; on a pointer, a typedef, a member or a
        // parameter, the errors but the count's have no place.
        { "__attribute__((interrupt)) int v;\n", NULL, { NULL } },
        { "int x __attribute__((interrupt));\n", NULL, { NULL } },
        { "struct __attribute__((interrupt)) S { int a; };\n", NULL, { NULL } },
        { "void g(int x __attribute__((interrupt)));\n", NULL, { NULL } },
        { "enum E { A __attribute__((interrupt)) };\n", NULL, { NULL } },
        { "__attribute__((interrupt)) void (*fp)(void *);\n", NULL, { NULL } },
        { "__attribute__((interrupt)) int (*fp)(void *);\n", NULL, { NULL } },
        { "__attribute__((interrupt)) void (*fp)(void);\n", NULL, { NULL } },
        { "__attribute__((interrupt)) void (*fp)(int);\n", NULL, { NULL } },
        { "void (*fp)(void *, int) __attribute__((interrupt));\n", NULL, { NULL } },
        { "typedef void T(void *) __attribute__((interrupt));\n", NULL, { NULL } },
        { "typedef int T(void *) __attribute__((interrupt));\n", NULL, { NULL } },
        { "struct S { int (*f)(void *) __attribute__((interrupt)); };\n", NULL, { NULL } },
        { "void g(void (*f)(int) __attribute__((interrupt)));\n", NULL, { NULL } },
        { "void g(void) { __attribute__((interrupt)) int (*fp)(void *) = 0; (void)fp; }\n", NULL, { NULL } },
        // Its arguments: expressions to clang's parser, nothing to its handler.
        { "__attribute__((interrupt(\"x\"))) void f(void *p);\n", NULL, { NULL } },
        { "__attribute__((interrupt(1, 2))) void f(void *p);\n", NULL, { NULL } },
        { "[[gnu::interrupt(1, 2)]] void f(void *p);\n", NULL, { NULL } },
        { "__attribute__((interrupt())) void f(void *p);\n", NULL, { NULL } },
        { "__attribute__((interrupt(x))) void f(void *p);\n", NULL, { NULL } },
        { "__attribute__((interrupt(1 +))) void f(void *p);\n", NULL, { NULL } },
        { "__attribute__((interrupt(1, 2))) int v;\n", NULL, { NULL } },
        { "__attribute__((__interrupt__)) void f(void *p);\n", NULL, { NULL } },
        { "[[__gnu__::__interrupt__]] void f(void *p);\n", NULL, { NULL } },
        { "[[clang::interrupt]] void f(void *p);\n", NULL, { NULL } },
        // C23's places.
        { "void f(void *p) [[gnu::interrupt]];\n", NULL, { NULL } },
        { "int [[gnu::interrupt]] f(void *p);\n", NULL, { NULL } },
        // Redeclarations, which inherit it, and one after the definition: its warning and its implicit used's, of no
        // place, unless the definition has a used.
        { "__attribute__((interrupt)) void f(void *p); void f(void *p) {}\n", NULL, { NULL } },
        { "void f(void *p); __attribute__((interrupt)) void f(void *p) {}\n", NULL, { NULL } },
        { "void f(void *p) {} __attribute__((interrupt)) void f(void *p);\n", NULL, { NULL } },
        { "__attribute__((used)) void f(void *p) {} __attribute__((interrupt)) void f(void *p);\n", NULL, { NULL } },
        { "__attribute__((interrupt)) void f(void *p) {} __attribute__((used)) void f(void *p);\n", NULL, { NULL } },
        { "__attribute__((interrupt)) void f(void *p) {} __attribute__((interrupt)) void f(void *p);\n", NULL, { NULL } },
        { "__attribute__((interrupt)) __attribute__((interrupt)) void f(void *p);\n", NULL, { NULL } },
        // Its implicit used, and its type, which it leaves as it is.
        { "__attribute__((interrupt)) static void f(void *p) {}\n", NULL, { "-Wall", NULL } },
        { "static void f(void *p) {} __attribute__((interrupt)) static void f(void *p);\n", NULL, { "-Wall", NULL } },
        { "__attribute__((interrupt)) static void f(void *p); static void f(void *p) {}\n", NULL, { "-Wall", NULL } },
        { "static __attribute__((interrupt)) void (*fp)(void *);\n", NULL, { "-Wall", NULL } },
        { "static __attribute__((interrupt)) int v;\n", NULL, { "-Wall", NULL } },
        { "__attribute__((interrupt)) void f(void *p); void (*q)(void *) = f; _Static_assert(_Generic(&f, void (*)(void *): 1));\n", NULL, { "-Wall", NULL } },
        // K&R: a declaration has no prototype; a definition's declarations give its parameters' types and places.
        { "__attribute__((interrupt)) void f();\n", "-std=gnu17", { NULL } },
        { "void f() __attribute__((interrupt));\n", "-std=gnu17", { NULL } },
        { "__attribute__((interrupt)) void f(p) void *p; {} void g(void) { f(0); }\n", "-std=gnu17", { NULL } },
        // Direct calls, through parentheses, selections, __extension__ and an address-of, in sizeof and at file scope;
        // not through a pointer or a cast.
        { "__attribute__((interrupt)) void f(void *p); void g(void) { f(0); }\n", NULL, { NULL } },
        { "__attribute__((interrupt)) void f(void *p); void g(void) { (f)(0); }\n", NULL, { NULL } },
        { "__attribute__((interrupt)) void f(void *p); void g(void) { ( f )(0); }\n", NULL, { NULL } },
        { "__attribute__((interrupt)) void f(void *p); void g(void) { (&f)(0); }\n", NULL, { NULL } },
        { "__attribute__((interrupt)) void f(void *p); void g(void) { (&(f))(0); }\n", NULL, { NULL } },
        { "__attribute__((interrupt)) void f(void *p); void g(void) { (*f)(0); }\n", NULL, { NULL } },
        { "__attribute__((interrupt)) void f(void *p); void g(void) { (_Generic(0, int: f))(0); }\n", NULL, { NULL } },
        { "__attribute__((interrupt)) void f(void *p); void g(void) { _Generic(0, int: f)(0); }\n", NULL, { NULL } },
        { "__attribute__((interrupt)) void f(void *p); void g(void) { __builtin_choose_expr(1, f, f)(0); }\n", NULL, { NULL } },
        { "__attribute__((interrupt)) void f(void *p); void g(void) { (__extension__ f)(0); }\n", NULL, { NULL } },
        { "__attribute__((interrupt)) void f(void *p); void g(void) { ((void (*)(void *))f)(0); }\n", NULL, { NULL } },
        { "__attribute__((interrupt)) void f(void *p); void g(int c) { (c ? f : f)(0); }\n", NULL, { NULL } },
        { "__attribute__((interrupt)) void f(void *p); void g(void) { void (*q)(void *) = &f; q(0); }\n", NULL, { NULL } },
        { "__attribute__((interrupt)) void f(void *p); void (*q)(void *) = f; void g(void) { q(0); }\n", NULL, { NULL } },
        { "__attribute__((interrupt)) void f(void *p); void g(void) { f(0); f(0); }\n", NULL, { NULL } },
        { "__attribute__((interrupt)) void f(void *p); void g(void) { (void)sizeof(f(0)); }\n", NULL, { NULL } },
        { "__attribute__((interrupt)) void f(void *p); void g(void) { _Generic(f(0), default: 0); }\n", NULL, { NULL } },
        { "__attribute__((interrupt)) void f(void *p); void g(void) { ({ f(0); }); }\n", NULL, { NULL } },
        { "__attribute__((interrupt)) void f(void *p); void g(void) { { { f(0); } } }\n", NULL, { NULL } },
        { "__attribute__((interrupt)) void f(void *p); int g(void) { return 0 ? (f(0), 1) : 2; }\n", NULL, { NULL } },
        { "__attribute__((interrupt)) void f(void *p); int x = sizeof(f(0));\n", NULL, { NULL } },
        { "__attribute__((interrupt)) void f(void *p) { f(p); }\n", NULL, { NULL } },
        { "__attribute__((interrupt)) void f(void *p, ...); void g(void) { f(0, 1, 2); }\n", NULL, { NULL } },
        { "typedef void F(void *); __attribute__((interrupt)) F f; void g(void) { f(0); }\n", NULL, { NULL } },
        { "void g(void) { __attribute__((interrupt)) void f(void *); f(0); }\n", NULL, { NULL } },
        { "__attribute__((interrupt)) void f(void *p); void f(void *p); void g(void) { f(0); }\n", NULL, { NULL } },
        { "void f(void *p); void g(void) { f(0); } __attribute__((interrupt)) void f(void *p);\n", NULL, { NULL } },
        { "__attribute__((interrupt)) void f(void *p); void g(void *p) { __builtin_call_with_static_chain(f(0), p); }\n", NULL, { NULL } },
        // A call around one's error is clang's invalid expression, of no -Wexcessive-regsave warning of its own.
        { "__attribute__((interrupt)) void h(void *p) { (void)__builtin_call_with_static_chain(h(p), p); }\n", NULL, { NULL } },
        { "__attribute__((interrupt)) void f(void *p); int g(int); __attribute__((interrupt)) void h(void *p) { g((f(p), 1)); g(2); }\n", NULL, { NULL } },
        // -Wexcessive-regsave in an interrupt service routine, a callee's note at its declaration (the one its name finds).
        { "void g(void); __attribute__((interrupt)) void h(void *p) { g(); }\n", NULL, { NULL } },
        { "void g(void); __attribute__((interrupt)) void h(void *p) { g(); g(); }\n", NULL, { NULL } },
        { "void g(void); void g(void); __attribute__((interrupt)) void h(void *p) { g(); }\n", NULL, { NULL } },
        { "void g(void) {} void g(void); __attribute__((interrupt)) void h(void *p) { g(); }\n", NULL, { NULL } },
        { "static void g(void) {} __attribute__((interrupt)) void h(void *p) { g(); }\n", NULL, { NULL } },
        { "void g(void); __attribute__((interrupt)) void h(void *p) { void g(void); g(); }\n", NULL, { NULL } },
        { "void g(void); __attribute__((interrupt)) void h(void *p) { { void g(void); } g(); }\n", NULL, { NULL } },
        { "int g(int); __attribute__((interrupt)) void h(void *p) { g(g(1)); }\n", NULL, { NULL } },
        { "int g(int); int k(int); __attribute__((interrupt)) void h(void *p) { k(g(1)); }\n", NULL, { NULL } },
        { "int g(void); __attribute__((interrupt)) void h(void *p) { int x = g(); (void)x; }\n", NULL, { NULL } },
        { "void g(void); __attribute__((interrupt)) void h(void *p) { (void)sizeof(g()); }\n", NULL, { NULL } },
        { "void g(void); __attribute__((interrupt)) void h(void *p) { ({ g(); }); }\n", NULL, { NULL } },
        { "int g(void); __attribute__((interrupt)) void h(void *p) { static int z = sizeof(g()); (void)z; }\n", NULL, { NULL } },
        { "void g(void); __attribute__((interrupt)) void h(void *p) { (g)(); (&g)(); (*g)(); }\n", NULL, { NULL } },
        { "void g(void); __attribute__((interrupt)) void h(void *p) { _Generic(0, int: g)(); __builtin_choose_expr(1, g, g)(); }\n", NULL, { NULL } },
        { "void g(void); __attribute__((interrupt)) void h(void *p) { ((void (*)(void))g)(); }\n", NULL, { NULL } },
        { "struct S { void (*f)(void); }; __attribute__((interrupt)) void h(void *p) { struct S s = {0}; s.f(); }\n", NULL, { NULL } },
        { "void (*a[2])(void); __attribute__((interrupt)) void h(void *p) { a[0](); }\n", NULL, { NULL } },
        { "void (*gf(void))(void); __attribute__((interrupt)) void h(void *p) { gf()(); }\n", NULL, { NULL } },
        { "__attribute__((interrupt)) void h(void *p) { void (*q)(void) = 0; q(); }\n", NULL, { NULL } },
        { "__attribute__((interrupt)) void h(void *p) { __attribute__((no_caller_saved_registers)) void (*q)(void) = 0; q(); }\n", NULL, { NULL } },
        { "__attribute__((interrupt)) void h(void *p); void g(void); void h(void *p) { g(); }\n", NULL, { NULL } },
        { "void g(void); void h(void *p); __attribute__((interrupt)) void h(void *p) { g(); }\n", NULL, { NULL } },
        { "void g(void); void h(void *p) { g(); } __attribute__((interrupt)) void h(void *p);\n", NULL, { NULL } },
        { "void g(void); __attribute__((interrupt)) void h(void *p) { g(); } __attribute__((interrupt)) void k(void *p) { g(); h(p); }\n", NULL, { NULL } },
        // A no_caller_saved_registers callee: its declaration's attribute, which an earlier one gives, not a typedef's
        // function type.
        { "__attribute__((no_caller_saved_registers)) void g(void); __attribute__((interrupt)) void h(void *p) { g(); }\n", NULL, { NULL } },
        { "void g(void) __attribute__((no_caller_saved_registers)); __attribute__((interrupt)) void h(void *p) { g(); }\n", NULL, { NULL } },
        { "__attribute__((no_caller_saved_registers)) void g(void); void g(void); __attribute__((interrupt)) void h(void *p) { g(); }\n", NULL, { NULL } },
        { "[[gnu::no_caller_saved_registers]] void g(void); __attribute__((interrupt)) void h(void *p) { g(); }\n", NULL, { NULL } },
        { "void g(void) [[gnu::no_caller_saved_registers]]; __attribute__((interrupt)) void h(void *p) { g(); }\n", NULL, { NULL } },
        { "__attribute__((no_caller_saved_registers)) void g(void), k(void); __attribute__((interrupt)) void h(void *p) { k(); }\n", NULL, { NULL } },
        { "typedef void T(void) __attribute__((no_caller_saved_registers)); T g; __attribute__((interrupt)) void h(void *p) { g(); }\n", NULL, { NULL } },
        { "__attribute__((interrupt)) void h(void *p) { __attribute__((no_caller_saved_registers)) void g(void); g(); }\n", NULL, { NULL } },
        // A no_caller_saved_registers caller: its declaration's attribute, its own message; interrupt's where both are.
        { "void g(void); __attribute__((no_caller_saved_registers)) void h(void *p) { g(); }\n", NULL, { NULL } },
        { "__attribute__((no_caller_saved_registers)) void g(void); __attribute__((no_caller_saved_registers)) void h(void *p) { g(); }\n", NULL, { NULL } },
        { "void g(void); __attribute__((no_caller_saved_registers)) void h(void); void h(void) { g(); }\n", NULL, { NULL } },
        { "typedef void T(void) __attribute__((no_caller_saved_registers)); T h; void g(void); void h(void) { g(); }\n", NULL, { NULL } },
        { "void g(void); __attribute__((interrupt, no_caller_saved_registers)) void h(void *p) { g(); }\n", NULL, { NULL } },
        { "void g(void); __attribute__((no_caller_saved_registers, interrupt)) void h(void *p) { g(); }\n", NULL, { NULL } },
        { "__attribute__((no_caller_saved_registers)) int g(int x) { return x; } __attribute__((no_caller_saved_registers)) int h(int x) { return g(x) + g(x + 1); }\n",
          NULL, { NULL } },
        { "void g(void); __attribute__((no_caller_saved_registers)) void h(void) { __builtin_trap(); g(); }\n", NULL, { NULL } },
        // Builtins: clang declares one where its name is first looked up, which its note names.
        { "__attribute__((interrupt)) void h(void *p) { __builtin_memset(p, 0, 4); }\n", NULL, { NULL } },
        { "__attribute__((interrupt)) void h(void *p) { __builtin_memset(p, 0, 4); __builtin_memset(p, 0, 4); }\n", NULL, { NULL } },
        { "void g(void *p) { __builtin_memset(p, 0, 4); } __attribute__((interrupt)) void h(void *p) { __builtin_memset(p, 0, 4); }\n", NULL, { NULL } },
        { "__attribute__((interrupt)) void h(void *p) { (__builtin_memset)(p, 0, 4); }\n", NULL, { NULL } },
        { "__attribute__((interrupt)) void h(void *p) { __builtin_trap(); }\n", NULL, { NULL } },
        { "__attribute__((interrupt)) void h(void *p) { (void)__builtin_expect(1, 1); (void)__builtin_constant_p(1); }\n", NULL, { NULL } },
        { "__attribute__((interrupt)) void h(void *p) { int r; (void)__builtin_add_overflow(1, 2, &r); (void)__builtin_isnan(1.0); }\n", NULL, { NULL } },
        { "__attribute__((interrupt)) void h(void *p) { int x = 0; (void)__atomic_load_n(&x, 0); (void)__builtin_ia32_rdtsc(); }\n", NULL, { NULL } },
        { "__attribute__((interrupt)) void h(void *p, ...) { __builtin_va_list ap; __builtin_va_start(ap, p); (void)__builtin_va_arg(ap, int); __builtin_va_end(ap); }\n",
          NULL, { NULL } },
        { "typedef int v4 __attribute__((vector_size(16))); typedef float f4 __attribute__((vector_size(16))); __attribute__((interrupt)) void h(void *p) "
          "{ v4 a = {0}; (void)__builtin_shufflevector(a, a, 0, 1, 2, 3); (void)__builtin_convertvector(a, f4); }\n", NULL, { NULL } },
        { "struct S { int a; }; __attribute__((interrupt)) void h(void *p) { (void)__builtin_offsetof(struct S, a); (void)__builtin_types_compatible_p(int, int); }\n",
          NULL, { NULL } },
        { "void g(void); __attribute__((interrupt)) void h(void *p) { __builtin_call_with_static_chain(g(), p); }\n", NULL, { NULL } },
        { "void *memset(void *, int, unsigned long); __attribute__((interrupt)) void h(void *p) { memset(p, 0, 4); }\n", NULL, { NULL } },
        // Its options and pragmas.
        { "void g(void); __attribute__((interrupt)) void h(void *p) { g(); }\n", NULL, { "-Wno-excessive-regsave", NULL } },
        { "void g(void); __attribute__((interrupt)) void h(void *p) { g(); }\n", NULL, { "-Werror=excessive-regsave", NULL } },
        { "void g(void); __attribute__((interrupt)) void h(void *p) { g(); }\n", NULL, { "-w", NULL } },
        { "void g(void); __attribute__((interrupt)) void h(void *p) { g(); }\n", NULL, { "-Wall", "-Wextra" } },
        { "void g(void); __attribute__((interrupt)) void h(void *p) { g(); }\n", NULL, { "-Werror", NULL } },
        { "void g(void); __attribute__((interrupt)) void h(void *p) { g(); }\n", NULL, { "-Werror", "-Wno-error=excessive-regsave" } },
        { "void g(void); __attribute__((interrupt)) void h(void *p) { g(); }\n", NULL, { "-Wall", "-Wno-excessive-regsave" } },
        { "#pragma clang diagnostic ignored \"-Wexcessive-regsave\"\nvoid g(void); __attribute__((interrupt)) void h(void *p) { g(); }\n", NULL, { NULL } },
        { "#pragma clang diagnostic error \"-Wexcessive-regsave\"\nvoid g(void); __attribute__((no_caller_saved_registers)) void h(void) { g(); }\n", NULL, { NULL } },
    };
    enum { LINES = 16384 };
    char* expected_lines = malloc(LINES);
    char* actual_lines = malloc(LINES);
    char source[PATH_MAX];
    snprintf(source, sizeof(source), "%s/interrupt.c", directory);
    for (uint32_t unit = 0; expected_lines != NULL && actual_lines != NULL && unit < BUSTER_ARRAY_LENGTH(units); unit += 1)
    {
        const CheckInterruptFixture* fixture = units + unit;
        const bool made = parse_test_write(source, fixture->source, strlen(fixture->source));
        char* arguments[16] = { (char*)clang, (char*)(fixture->standard != NULL ? fixture->standard : "-std=gnu23"), "-fsyntax-only", "-fno-caret-diagnostics", source };
        uint32_t count = 5;
        for (uint32_t o = 0; o < BUSTER_ARRAY_LENGTH(fixture->options) && fixture->options[o] != NULL; o += 1)
        {
            arguments[count++] = (char*)fixture->options[o];
        }
        arguments[count] = NULL;
        char* expected = NULL;
        const int oracle = made ? check_test_driver_spawn(arguments, &expected) : -1;
        const bool lines = expected != NULL && check_test_interrupt_lines(expected, expected_lines, LINES);
        char description[4096];
        snprintf(description, sizeof(description), "interrupt %u: clang's lines read", unit);
        check_test_expect(test, made && lines, description);
        for (uint32_t mode = 0; lines && mode < 3; mode += 1)
        {
            arguments[0] = (char*)executable;
            uint32_t at = count;
            if (mode == 1)
            {
                arguments[at++] = "--scalar";
            }
            else if (mode == 2)
            {
                arguments[at++] = "--deferred-bodies";
                arguments[at++] = "--lanes";
                arguments[at++] = "2";
            }
            arguments[at] = NULL;
            char* actual = NULL;
            const int status = check_test_driver_spawn(arguments, &actual);
            const bool same = actual != NULL && check_test_interrupt_lines(actual, actual_lines, LINES) && strcmp(expected_lines, actual_lines) == 0;
            snprintf(description, sizeof(description), "interrupt %u (%s), mode %u: status %d (clang %d), lines\n%s(clang\n%s)", unit, fixture->source, mode,
                     status, oracle, actual_lines, expected_lines);
            check_test_expect(test, same && (status != 0) == (oracle != 0), description);
            free(actual);
        }
        free(expected);
    }
    free(expected_lines);
    free(actual_lines);
    // A definition's code: its frame isn't the lowering's yet (check_program_interrupt), where clang compiles it.
    static const char* const definitions[] = {
        "__attribute__((interrupt)) void f(void *p) { (void)p; }\n",
        "__attribute__((interrupt)) static void f(void *p, unsigned long e) { (void)p; (void)e; }\n",
        "__attribute__((interrupt)) void f(void *p); void f(void *p) { (void)p; }\n",
        "__attribute__((no_caller_saved_registers)) void g(void); __attribute__((interrupt)) void f(void *p) { (void)p; g(); }\n",
    };
    char object[PATH_MAX];
    snprintf(object, sizeof(object), "%s/interrupt.o", directory);
    for (uint32_t d = 0; d < BUSTER_ARRAY_LENGTH(definitions); d += 1)
    {
        const bool made = parse_test_write(source, definitions[d], strlen(definitions[d]));
        char* text = NULL;
        char* reference[] = { (char*)clang, "-std=gnu17", "-O0", "-c", source, "-o", object, NULL };
        const int oracle = made ? check_test_driver_spawn(reference, &text) : -1;
        free(text);
        for (uint32_t p = 0; made && p < 2; p += 1)
        {
            text = NULL;
            char* arguments[] = { (char*)executable, "-std=gnu17", "-c", source, "-o", object, p == 1 ? "--scalar" : NULL, NULL };
            const int status = check_test_driver_spawn(arguments, &text);
            char description[1024];
            snprintf(description, sizeof(description), "interrupt definition %u by the %s path is refused by name where clang compiles it (status %d, clang %d: %s)", d,
                     p == 1 ? "scalar" : "vector", status, oracle, text != NULL ? text : "");
            check_test_expect(test, oracle == 0 && status == 1 && text != NULL && strstr(text, "unsupported: interrupt service routines") != NULL, description);
            free(text);
        }
    }
    unlink(source);
    unlink(object);
}
