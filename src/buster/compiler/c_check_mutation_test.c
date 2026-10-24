// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// test-checker's semantic mutation checks (c_check_mutation.c). check_test_mutation_engine checks the sites a unit
// of its own gives, counted by hand from its parse, the edits some of them make, and the minimizer's helpers. The
// fixtures are differences test-checker-corpus --semantic-mutations found, minimized (checker_mutation_N.c) and
// then fixed in the checker: each is checked as the corpus check checks a file, against clang (which must still give
// the result recorded with it) and with the scalar and vector paths alike. Include after c_check_corpus.c.
#ifndef BUSTER_C_CHECK_MUTATION_TEST_INCLUDED
#define BUSTER_C_CHECK_MUTATION_TEST_INCLUDED

typedef struct CheckMutationFixture CheckMutationFixture;
struct CheckMutationFixture
{
    // The corpus and the mutation it came from.
    const char* name;
    const char* source;
    // The language mode its corpus's configuration gave: 1999 or 2017, and whether GNU's.
    uint32_t standard;
    bool gnu;
    // Clang's verdict and first error's line, which the checker must give.
    bool rejected;
    uint32_t line;
};

static const CheckMutationFixture check_mutation_fixtures[] =
{
    {
        .name = "accepted control: a pointer compared with a null pointer constant",
        .source = "int f(int *p) { return p == 0 || p > (int *)0; }\n",
        .standard = 2017,
        .gnu = true,
        .rejected = false,
        .line = 0,
    },
    {
        .name = "musl 1.2.5 src/string/wcpcpy.c, a parameter made const: returning it discards the qualifier, which only warns",
        .source =
            "typedef int wchar_t;\n"
            "wchar_t *wcpcpy(wchar_t *d, const wchar_t *s)\n"
            "{\n"
            "    return d == 0 ? 0 : (s);\n"
            "}\n",
        .standard = 1999,
        .gnu = false,
        .rejected = false,
        .line = 0,
    },
    {
        .name = "musl 1.2.5 src/mman/shm_open.c, a parameter made const: passing it to a char * parameter only warns",
        .source =
            "char *__shm_mapname(char *, char *);\n"
            "int shm_open(const char *name)\n"
            "{\n"
            "    char buf[10];\n"
            "    return (name = __shm_mapname(buf, name)) != 0;\n"
            "}\n",
        .standard = 1999,
        .gnu = false,
        .rejected = false,
        .line = 0,
    },
    {
        .name = "musl 1.2.5 src/stdio/__uflow.c, a local made unsigned: unsigned char * for char * differs only in sign, which only warns",
        .source =
            "typedef unsigned long size_t;\n"
            "typedef struct _IO_FILE FILE;\n"
            "struct _IO_FILE { size_t (*read)(FILE *, char *, size_t); };\n"
            "int __uflow(FILE *f)\n"
            "{\n"
            "    unsigned char c;\n"
            "    return f->read(f, &c, 1) == 1 ? c : -1;\n"
            "}\n",
        .standard = 1999,
        .gnu = false,
        .rejected = false,
        .line = 0,
    },
    {
        .name = "musl 1.2.5 src/thread/thrd_sleep.c, two arguments swapped: a const struct pointer for a plain one only warns",
        .source =
            "struct timespec;\n"
            "int __clock_nanosleep(int, int, struct timespec *, struct timespec *);\n"
            "int thrd_sleep(const struct timespec *req, struct timespec *rem)\n"
            "{\n"
            "    return __clock_nanosleep(0, 0, rem, req);\n"
            "}\n",
        .standard = 1999,
        .gnu = false,
        .rejected = false,
        .line = 0,
    },
    {
        .name = "Lua 5.4.8 src/lua.c's pmain, a declaration made const: char ** and const char ** convert with a nested-qualifier warning",
        .source =
            "int collectargs(char **argv, int *first);\n"
            "int pmain(void *L)\n"
            "{\n"
            "    const char **argv = (char **)L;\n"
            "    int script;\n"
            "    return collectargs(argv, &script);\n"
            "}\n",
        .standard = 1999,
        .gnu = true,
        .rejected = false,
        .line = 0,
    },
    {
        .name = "glibc's bits/stdio.h under Lua 5.4.8, a call blanked: a conditional of a pointer and an integer has the pointer's type",
        .source =
            "typedef struct _IO_FILE FILE;\n"
            "struct _IO_FILE { char *_IO_write_ptr; char *_IO_write_end; };\n"
            "extern FILE *stdout;\n"
            "int putchar_unlocked(int c)\n"
            "{\n"
            "    return (stdout->_IO_write_ptr >= stdout->_IO_write_end ? ((char)c, stdout) : (char)(*stdout->_IO_write_ptr++ = c)) != 0;\n"
            "}\n",
        .standard = 1999,
        .gnu = true,
        .rejected = false,
        .line = 0,
    },
    {
        .name = "glibc's bits/stdio.h under Lua 5.4.8, a call blanked: a conditional of pointers to other types is void *",
        .source =
            "typedef struct _IO_FILE FILE;\n"
            "struct _IO_FILE { char *_IO_write_ptr; };\n"
            "int putc_unlocked(int c, FILE *stream)\n"
            "{\n"
            "    char *p = c ? stream : stream->_IO_write_ptr;\n"
            "    return p != 0;\n"
            "}\n",
        .standard = 1999,
        .gnu = true,
        .rejected = false,
        .line = 0,
    },
    {
        .name = "SQLite 3.53.2, a declaration made a pointer: a pointer compared with an integer, ordered and not, only warns",
        .source =
            "int f(int *p, int n)\n"
            "{\n"
            "    return p <= n || p == n || n > p || p > 0;\n"
            "}\n",
        .standard = 2017,
        .gnu = true,
        .rejected = false,
        .line = 0,
    },
    {
        .name = "comparisons of pointers to distinct types, and of void * with a function pointer, only warn in ISO modes too",
        .source =
            "int h(void);\n"
            "int f(int *p, char *q, void *v)\n"
            "{\n"
            "    return p == q || p < q || v == h || v < (void *)h || h < h;\n"
            "}\n",
        .standard = 1999,
        .gnu = false,
        .rejected = false,
        .line = 0,
    },
    {
        .name = "pointers to integers of another width, or to pointers to other types, remain incompatible",
        .source =
            "void f(long *p, char **q)\n"
            "{\n"
            "    long long *r = p;\n"
            "    unsigned char **s = q;\n"
            "    (void)r;\n"
            "    (void)s;\n"
            "}\n",
        .standard = 2017,
        .gnu = true,
        .rejected = true,
        .line = 3,
    },
    {
        .name = "musl 1.2.5 compat/time32/futimens_time32.c, two arguments swapped: the error is at the argument, not the call's )",
        .source =
            "struct timespec32 { long tv_sec, tv_nsec; };\n"
            "struct timespec { long tv_sec, tv_nsec; };\n"
            "int futimens(int, const struct timespec[2]);\n"
            "int __futimens_time32(int fd, const struct timespec32 *times32)\n"
            "{\n"
            "    return futimens(times32 ? (&(struct timespec){\n"
            "        .tv_sec = times32[0].tv_sec, .tv_nsec = times32[0].tv_nsec }) : 0,\n"
            "        fd);\n"
            "}\n",
        .standard = 1999,
        .gnu = false,
        .rejected = true,
        .line = 6,
    },
    {
        .name = "musl 1.2.5 compat/time32/settimeofday_time32.c, a parameter's type changed: the error is at the argument's first line",
        .source =
            "struct timeval { long tv_sec, tv_usec; };\n"
            "struct timezone;\n"
            "int settimeofday(const struct timeval *, const struct timezone *);\n"
            "int __settimeofday_time32(const struct timeval *tv32, void *tz)\n"
            "{\n"
            "    return settimeofday(0, tv32 ? (&(struct timeval){\n"
            "        .tv_sec = tv32->tv_sec,\n"
            "        .tv_usec = tv32->tv_usec }) : 0) + (tz != 0);\n"
            "}\n",
        .standard = 1999,
        .gnu = false,
        .rejected = true,
        .line = 6,
    },
    {
        .name = "too many arguments are reported at the first extra argument",
        .source =
            "void h(int, int);\n"
            "void f(void)\n"
            "{\n"
            "    h(1, 2,\n"
            "      3);\n"
            "}\n",
        .standard = 2017,
        .gnu = true,
        .rejected = true,
        .line = 5,
    },
    {
        .name = "musl 1.2.5 include/alltypes.h's pthread_attr_t, a type made void: sizeof(void) is 1 and the array of none GNU's, under -std=c99",
        .source = "typedef struct { union { int __i[14]; void *__p[sizeof(void) / 12 * sizeof(int)]; } __u; } pthread_attr_t;\n",
        .standard = 1999,
        .gnu = false,
        .rejected = false,
        .line = 0,
    },
    {
        .name = "musl 1.2.5 include/sys/select.h's fd_set, a constant changed: an array of no elements is GNU's extension under -std=c99 too",
        .source =
            "typedef struct {\n"
            "    unsigned long fds_bits[sizeof(long) / 1024 / 8];\n"
            "} fd_set;\n",
        .standard = 1999,
        .gnu = false,
        .rejected = false,
        .line = 0,
    },
    {
        .name = "an array bound, an enumerator, a case label and a static assertion that fold without being ICEs, under -std=c99",
        .source =
            "enum { E = (int)(1.5 * 2) };\n"
            "int a[(int)(1.5 + 1.0)];\n"
            "_Static_assert((int)(0.5 + 0.5) == 1, \"folded\");\n"
            "int f(int x)\n"
            "{\n"
            "    switch (x) { case (int)(1.0 + 1): return 1; }\n"
            "    return 0;\n"
            "}\n",
        .standard = 1999,
        .gnu = false,
        .rejected = false,
        .line = 0,
    },
    {
        .name = "GNU's other extensions clang has under -std=c99: ?:, ranges, flexible initializers, void arithmetic and conditionals",
        .source =
            "struct F { int n; int d[]; };\n"
            "static struct F flexible = { 1, { 2, 3 } };\n"
            "int ranges[10] = { [1 ... 3] = 5 };\n"
            "__extension__ typedef long long wide;\n"
            "struct S { int a; };\n"
            "void g(void);\n"
            "void h(void) { return g(); }\n"
            "long f(void *p, void *q, int c, struct S s)\n"
            "{\n"
            "    c ? g() : c;\n"
            "    s = (struct S)s;\n"
            "    return (p + 1 - q) + (c ?: 2) + (long)sizeof(g) + (long)__alignof__(void) + s.a;\n"
            "}\n",
        .standard = 1999,
        .gnu = false,
        .rejected = false,
        .line = 0,
    },
    {
        .name = "glibc's bits/stdio.h under Lua 5.4.8 src/lobject.c, two arguments swapped: a system header's -Wint-conversion is suppressed",
        .source =
            "# 1 \"lobject.c\"\n"
            "# 1 \"/usr/include/x86_64-linux-gnu/bits/stdio.h\" 1 3\n"
            "typedef struct _IO_FILE FILE;\n"
            "extern FILE *stdout;\n"
            "extern int __overflow(FILE *, int);\n"
            "static __inline int putchar_unlocked(int __c)\n"
            "{\n"
            "    return __overflow(__c, stdout);\n"
            "}\n"
            "# 2 \"lobject.c\" 2\n"
            "int main(void) { return putchar_unlocked('a'); }\n",
        .standard = 1999,
        .gnu = true,
        .rejected = false,
        .line = 0,
    },
    {
        .name = "glibc's bits/stdio.h under Lua 5.4.8 src/ltablib.c, two arguments swapped: a system header's incompatible pointers convert",
        .source =
            "# 1 \"ltablib.c\"\n"
            "# 1 \"/usr/include/x86_64-linux-gnu/bits/stdio.h\" 1 3\n"
            "typedef __builtin_va_list __gnuc_va_list;\n"
            "typedef struct _IO_FILE FILE;\n"
            "extern FILE *stdout;\n"
            "extern int vfprintf(FILE *__restrict __s, const char *__restrict __format, __gnuc_va_list __arg);\n"
            "static __inline int vprintf(const char *__restrict __fmt, __gnuc_va_list __arg)\n"
            "{\n"
            "    return vfprintf(stdout, __arg, __fmt);\n"
            "}\n"
            "static __inline void none(int __c) { return __c; }\n"
            "# 2 \"ltablib.c\" 2\n"
            "int main(void) { return 0; }\n",
        .standard = 1999,
        .gnu = true,
        .rejected = false,
        .line = 0,
    },
    {
        .name = "the same conversions outside a system header remain errors",
        .source =
            "# 1 \"lobject.c\"\n"
            "# 1 \"/usr/include/x86_64-linux-gnu/bits/stdio.h\" 1\n"
            "typedef struct _IO_FILE FILE;\n"
            "extern FILE *stdout;\n"
            "extern int __overflow(FILE *, int);\n"
            "static __inline int putchar_unlocked(int __c)\n"
            "{\n"
            "    return __overflow(__c, stdout);\n"
            "}\n"
            "# 2 \"lobject.c\" 2\n"
            "int main(void) { return putchar_unlocked('a'); }\n",
        .standard = 1999,
        .gnu = true,
        .rejected = true,
        .line = 6,
    },
    {
        .name = "glibc's stdlib.h under Lua 5.4.8 src/lua.c, arguments swapped: a struct for a pointer stays an error in a system header",
        .source =
            "# 1 \"lua.c\"\n"
            "# 1 \"/usr/include/stdlib.h\" 1 3\n"
            "typedef struct { int quot; int rem; } div_t;\n"
            "extern long strtol(const char *__restrict __nptr, char **__restrict __endptr, int __base);\n"
            "static __inline int atoi_div(const char *__nptr, div_t __d)\n"
            "{\n"
            "    return (int) strtol(10, (char **) ((void*)0), __nptr) + (int) strtol(__d, 0, 10);\n"
            "}\n"
            "# 2 \"lua.c\" 2\n"
            "int main(void) { return 0; }\n",
        .standard = 1999,
        .gnu = true,
        .rejected = true,
        .line = 5,
    },
    {
        .name = "musl 1.2.5 src/math/log_data.h, a subtraction's operands swapped: the negative bound is the first error, not the attribute's items",
        .source =
            "__attribute__((__visibility__(\"hidden\"))) extern const struct log_data {\n"
            "    double ln2hi;\n"
            "    double poly1[1 - 12];\n"
            "} __log_data;\n",
        .standard = 1999,
        .gnu = false,
        .rejected = true,
        .line = 3,
    },
    {
        .name = "Lua 5.4.8 src/liolib.c, const added to opencheck's void result: a qualified void is void, for its body and returns",
        .source =
            "typedef struct lua_State lua_State;\n"
            "static const void opencheck(lua_State *L, const char *fname, const char *mode)\n"
            "{\n"
            "    if (L == 0)\n"
            "        return;\n"
            "    (void)fname;\n"
            "    (void)mode;\n"
            "}\n"
            "int io_open(lua_State *L) { opencheck(L, \"f\", \"r\"); return 0; }\n",
        .standard = 1999,
        .gnu = true,
        .rejected = false,
        .line = 0,
    },
    {
        .name = "a return without a value in a system header's int function: -Wreturn-mismatch, which clang suppresses there",
        .source =
            "# 1 \"user.c\"\n"
            "# 1 \"/usr/include/header.h\" 1 3\n"
            "static int f(int x) { if (x) return; return 1; }\n"
            "# 2 \"user.c\" 2\n"
            "int main(void) { return f(0); }\n",
        .standard = 2017,
        .gnu = true,
        .rejected = false,
        .line = 0,
    },
    {
        .name = "SQLite 3.53.2's <string.h>, a pointer removed from strlcpy's source: restrict on a non-pointer is reported at the qualifier",
        .source =
            "# 1 \"user.c\"\n"
            "# 1 \"/usr/include/string.h\" 1 3\n"
            "extern unsigned long strlcpy(char *__restrict __dest,\n"
            "    const char __restrict __src, unsigned long __n);\n"
            "# 2 \"user.c\" 2\n"
            "int main(void) { return 0; }\n",
        .standard = 2017,
        .gnu = true,
        .rejected = true,
        .line = 2,
    },
    {
        .name = "the same rule for a restrict pointer to a function, which clang reports at the pointer's *",
        .source =
            "void g(int a, void (*\n"
            "    restrict f)(void));\n",
        .standard = 2017,
        .gnu = true,
        .rejected = true,
        .line = 1,
    },
    {
        .name = "musl 1.2.5 src/math/log2_data.h, 11 - 1 swapped in poly1's bound: a visibility attribute before a struct definition that fails is no call",
        .source =
            "# 1 \"src/math/log2_data.c\"\n"
            "# 13 \"src/math/log2_data.h\" 1\n"
            "extern __attribute__((__visibility__(\"hidden\"))) const struct log2_data {\n"
            "double invln2hi;\n"
            "double poly1[ 1 - 11 ];\n"
            "} __log2_data;\n"
            "# 12 \"src/math/log2_data.c\" 2\n",
        .standard = 1999,
        .gnu = false,
        .rejected = true,
        .line = 15,
    },
    {
        .name = "too few arguments are reported at the call's )",
        .source =
            "void h(int, int);\n"
            "void f(void)\n"
            "{\n"
            "    h(1\n"
            "     );\n"
            "}\n",
        .standard = 2017,
        .gnu = true,
        .rejected = true,
        .line = 5,
    },
};

// The unit the engine's checks mutate, and the sites its parse gives, counted by hand: nine declarations with
// declarators (the members a and b, g and its x, f and its p and q, k and the loop's i), two of which start with `*`;
// the members; struct s, g and f at file scope; k and i as locals; x, p and q as parameters (g's `...` stays); the
// operators +, < and = (the loop's i = 0 is a declarator's initializer); and the call of g.
static const char check_mutation_test_unit[] =
    "struct s { int a; char *b; };\n"
    "int g(int x, ...);\n"
    "int f(int p, struct s *q)\n"
    "{\n"
    "    unsigned k = p + 1;\n"
    "    for (int i = 0; i < 2; i++) { k = g(p, q->b); }\n"
    "    return k;\n"
    "}\n";

static const uint32_t check_mutation_test_sites[CHECK_MUTATION_KIND_COUNT] =
{
    [CHECK_MUTATION_POINTER_ADD] = 9,
    [CHECK_MUTATION_POINTER_REMOVE] = 2,
    [CHECK_MUTATION_SIGNEDNESS] = 9,
    [CHECK_MUTATION_WIDTH] = 9,
    [CHECK_MUTATION_CONST] = 9,
    [CHECK_MUTATION_STRUCT] = 9,
    [CHECK_MUTATION_REMOVE_LOCAL] = 2,
    [CHECK_MUTATION_REMOVE_PARAMETER] = 3,
    [CHECK_MUTATION_REMOVE_FILE_SCOPE] = 3,
    [CHECK_MUTATION_REMOVE_MEMBER] = 2,
    [CHECK_MUTATION_SWAP_OPERANDS] = 3,
    [CHECK_MUTATION_SWAP_ARGUMENTS] = 1,
};

// The edit of the unit's site of `kind` whose first token is spelled `first` (the first such site), as the bytes it
// replaces and its replacement.
static bool check_test_mutation_edit(CheckMutationUnit* unit, uint8_t kind, const char* first, CheckMutationEdit* edit, char* replaced, size_t size)
{
    const CheckMutationSite* site = NULL;
    for (uint32_t i = 0; site == NULL && i < unit->site_count; i += 1)
    {
        const CheckMutationSite* candidate = unit->sites + i;
        site = candidate->kind == kind && check_mutation_spelled(unit, candidate->first, first) ? candidate : NULL;
    }
    uint64_t state = 133;
    if (site != NULL)
    {
        check_mutation_edit(edit, unit, site, &state);
        snprintf(replaced, size, "%.*s", (int)(edit->end - edit->begin), unit->text + edit->begin);
    }
    return site != NULL && edit->valid;
}

static bool check_test_mutation_replacement(const CheckMutationEdit* edit, const char* expected)
{
    return edit->replacement_length == strlen(expected) && memcmp(edit->replacement, expected, edit->replacement_length) == 0;
}

static void check_test_mutation_engine(CheckTest* test, const char* directory)
{
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    const PPOptions options =
    {
        .language =
        {
            .standard = 2017,
            .gnu = true,
        },
        .no_standard_includes = true,
        .quiet = true,
        .semantic_pragmas = true,
    };
    PPOptions plain = options;
    plain.undefine = true;
    char source[4300];
    char base[4300];
    snprintf(source, sizeof(source), "%s/mutation_unit.c", directory);
    snprintf(base, sizeof(base), "%s/mutation_base.c", directory);
    CheckMutationUnit unit = { 0 };
    const bool written = check_corpus_write(source, check_mutation_test_unit, sizeof(check_mutation_test_unit) - 1);
    const bool prepared = written && check_mutation_prepare(&unit, source, &options, &plain, base, &identifiers, &pp, &parser);
    check_test_expect(test, prepared, "the mutation engine's unit prints, reads back and parses as its base");
    uint32_t counts[CHECK_MUTATION_KIND_COUNT] = { 0 };
    bool main = true;
    for (uint32_t site = 0; prepared && site < unit.site_count; site += 1)
    {
        counts[unit.sites[site].kind] += 1;
        main &= unit.sites[site].main;
    }
    for (uint32_t kind = 0; prepared && kind < CHECK_MUTATION_KIND_COUNT; kind += 1)
    {
        char description[128];
        snprintf(description, sizeof(description), "the unit's %s sites are the ones its parse gives", check_mutation_names[kind]);
        check_test_expect(test, counts[kind] == check_mutation_test_sites[kind], description);
        check_test_expect(test, unit.site_offsets[kind * 2 + 2] - unit.site_offsets[kind * 2] == counts[kind], "sites are grouped by kind");
    }
    check_test_expect(test, !prepared || main, "every site of a unit without includes is in the unit's own file");
    CheckMutationEdit edit = { 0 };
    char replaced[256];
    check_test_expect(test, prepared && check_test_mutation_edit(&unit, CHECK_MUTATION_SWAP_ARGUMENTS, "(", &edit, replaced, sizeof(replaced)) &&
                            strcmp(replaced, "p, q->b") == 0 && check_test_mutation_replacement(&edit, " q->b, p "),
                      "an argument swap exchanges a call's two arguments");
    check_test_mutation_edit(&unit, CHECK_MUTATION_SWAP_OPERANDS, "p", &edit, replaced, sizeof(replaced));
    check_test_expect(test, prepared && edit.valid && strcmp(replaced, "p + 1") == 0 && check_test_mutation_replacement(&edit, " 1 + p "),
                      "an operand swap exchanges an operator's operands");
    check_test_mutation_edit(&unit, CHECK_MUTATION_REMOVE_PARAMETER, "int", &edit, replaced, sizeof(replaced));
    check_test_expect(test, prepared && edit.valid && strcmp(replaced, "int x,") == 0 && check_test_mutation_replacement(&edit, " "),
                      "a parameter goes with the comma after it");
    check_test_mutation_edit(&unit, CHECK_MUTATION_REMOVE_PARAMETER, ",", &edit, replaced, sizeof(replaced));
    check_test_expect(test, prepared && edit.valid && strcmp(replaced, ", struct s *q") == 0 && check_test_mutation_replacement(&edit, " "),
                      "a last parameter goes with the comma before it");
    check_test_mutation_edit(&unit, CHECK_MUTATION_REMOVE_LOCAL, "unsigned", &edit, replaced, sizeof(replaced));
    check_test_expect(test, prepared && edit.valid && strcmp(replaced, "unsigned k = p + 1;") == 0 && check_test_mutation_replacement(&edit, " "),
                      "a local declaration goes whole");
    check_test_mutation_edit(&unit, CHECK_MUTATION_POINTER_ADD, "unsigned", &edit, replaced, sizeof(replaced));
    check_test_expect(test, prepared && edit.valid && replaced[0] == 0 && unit.text[edit.begin] == 'k' && check_test_mutation_replacement(&edit, " * "),
                      "a pointer goes before the declarator");
    check_test_mutation_edit(&unit, CHECK_MUTATION_POINTER_REMOVE, "char", &edit, replaced, sizeof(replaced));
    check_test_expect(test, prepared && edit.valid && strcmp(replaced, "*") == 0 && check_test_mutation_replacement(&edit, " "),
                      "a pointer's `*` goes");
    check_test_mutation_edit(&unit, CHECK_MUTATION_SIGNEDNESS, "unsigned", &edit, replaced, sizeof(replaced));
    check_test_expect(test, prepared && edit.valid && strcmp(replaced, "unsigned") == 0 && check_test_mutation_replacement(&edit, " signed"),
                      "unsigned becomes signed");
    check_test_mutation_edit(&unit, CHECK_MUTATION_SIGNEDNESS, "char", &edit, replaced, sizeof(replaced));
    check_test_expect(test, prepared && edit.valid && replaced[0] == 0 && check_test_mutation_replacement(&edit, " unsigned "),
                      "a type without a sign gets unsigned");
    check_test_mutation_edit(&unit, CHECK_MUTATION_CONST, "int", &edit, replaced, sizeof(replaced));
    check_test_expect(test, prepared && edit.valid && replaced[0] == 0 && unit.text[edit.begin] == 'i' && check_test_mutation_replacement(&edit, " const "),
                      "const goes before the type");
    check_test_mutation_edit(&unit, CHECK_MUTATION_WIDTH, "int", &edit, replaced, sizeof(replaced));
    const bool width = edit.valid && strcmp(replaced, "int") == 0 && (check_test_mutation_replacement(&edit, " char") || check_test_mutation_replacement(&edit, " short") ||
                                                                      check_test_mutation_replacement(&edit, " long") || check_test_mutation_replacement(&edit, " long long"));
    check_test_expect(test, prepared && width, "a width change gives another integer type");
    check_test_mutation_edit(&unit, CHECK_MUTATION_STRUCT, "unsigned", &edit, replaced, sizeof(replaced));
    const bool tagged = edit.valid && strcmp(replaced, "unsigned") == 0 &&
                        (check_test_mutation_replacement(&edit, " struct s") || check_test_mutation_replacement(&edit, " struct " BUSTER_CHECK_MUTATION_TAG));
    check_test_expect(test, prepared && tagged, "a struct replacement names a struct defined before the declaration, or the incomplete one");
    check_test_mutation_edit(&unit, CHECK_MUTATION_WIDTH, "struct", &edit, replaced, sizeof(replaced));
    check_test_expect(test, !edit.valid, "a struct type has no width to change");
    // The edit made: the base with its bytes replaced.
    check_test_mutation_edit(&unit, CHECK_MUTATION_SWAP_ARGUMENTS, "(", &edit, replaced, sizeof(replaced));
    size_t length = 0;
    char* mutant = prepared ? check_mutation_text(&unit, &edit, &length) : NULL;
    check_test_expect(test, mutant != NULL && length == unit.length + 2 && strstr(mutant, "k = g( q->b, p );") != NULL && strstr(mutant, "p, q->b") == NULL,
                      "the mutant is the base with the edit made");
    free(mutant);
    free(edit.replacement);
    check_mutation_release(&unit);
    parse_release(&parser);
    pp_release(&pp);
    intern_release(&identifiers);
    const int removed_source = unlink(source);
    const int removed_base = unlink(base);
    check_test_expect(test, removed_source == 0 && (removed_base == 0 || !prepared), "the engine's files are removed");
    // The minimizer's helpers.
    char blanked[] = "ab\ncd";
    const bool changed = check_mutation_blank(blanked, 1, 4);
    check_test_expect(test, changed && strcmp(blanked, "a \n d") == 0 && !check_mutation_blank(blanked, 1, 4), "blanking keeps newlines and says whether it changed anything");
    static const char spaced[] = "a  b \n\n  c\n";
    size_t compact_length = 0;
    char* lines = check_mutation_compact(spaced, sizeof(spaced) - 1, 2, &compact_length);
    check_test_expect(test, lines != NULL && strcmp(lines, "a  b \n  c\n") == 0 && compact_length == 10, "compaction drops lines of spaces alone");
    free(lines);
    char* spaces = check_mutation_compact(spaced, sizeof(spaced) - 1, 3, &compact_length);
    check_test_expect(test, spaces != NULL && strcmp(spaces, "a b\n\n c\n") == 0 && compact_length == 8, "compaction collapses runs of spaces and drops trailing ones");
    free(spaces);
    static const char directives[] = "int x;\n# 1 \"f.c\" 3\nint y;\n";
    uint32_t segments[64];
    const uint32_t tokens = check_mutation_token_segments(directives, sizeof(directives) - 1, false, segments);
    check_test_expect(test, tokens == 6 && segments[6] == 19 && segments[7] == 22, "the token phase passes over directive lines");
    const uint32_t marker_lines = check_mutation_token_segments(directives, sizeof(directives) - 1, true, segments);
    check_test_expect(test, marker_lines == 1 && segments[0] == 7 && segments[1] == 18, "the markers' phase takes a directive's whole line");
    check_test_expect(test, check_mutation_line_segments("a\nb\nc", 5, segments) == 3 && segments[4] == 4 && segments[5] == 5, "a text that doesn't parse is minimized by lines");
    CheckMutationOutcome wanted =
    {
        .valid = true,
        .parsed = true,
        .clang_rejected = true,
        .accepted = false,
        .equal = true,
        .clang_line = 7,
        .line = 5,
        .phase = "expression",
        .clang_file = "x.c",
        .clang_message = "an error",
        .file = "x.c",
        .message = "another error",
    };
    CheckMutationOutcome moved = wanted;
    moved.clang_line = 9;
    moved.line = 6;
    CheckMutationOutcome met = wanted;
    met.line = 7;
    CheckMutationOutcome unparsed = wanted;
    unparsed.parsed = false;
    check_test_expect(test, check_mutation_same(&wanted, &wanted, true) && !check_mutation_same(&wanted, &moved, true) &&
                            check_mutation_same(&wanted, &moved, false) && !check_mutation_same(&wanted, &met, false) && !check_mutation_same(&wanted, &unparsed, false),
                      "a minimizer's candidate keeps its difference: lines while exact, their order after, and the parse");
    check_test_expect(test, check_mutation_different(&wanted) && !check_mutation_different(&met), "a first error's line difference is a difference");
}

static void check_test_mutation_fixture(CheckTest* test, CheckCorpusTally* tally, const CheckMutationFixture* fixture, uint32_t index, const char* directory,
                                        const char* clang)
{
    const PPOptions options =
    {
        .language =
        {
            .standard = fixture->standard,
            .gnu = fixture->gnu,
        },
        .no_standard_includes = true,
        .undefine = true,
        .quiet = true,
        .clang_features = true,
        .semantic_pragmas = true,
    };
    char standard[32];
    snprintf(standard, sizeof(standard), "-std=%s%u", fixture->gnu ? "gnu" : "c", fixture->standard % 100);
    const char* arguments[] =
    {
        "--no-default-config",
        "--target=x86_64-linux-gnu",
        standard,
        "-nostdinc",
        "-x",
        "cpp-output",
    };
    char path[4300];
    snprintf(path, sizeof(path), "%s/mutation_fixture_%u.c", directory, index);
    CheckCorpusOracle oracle;
    bool ready = check_corpus_write(path, fixture->source, strlen(fixture->source)) &&
                 check_corpus_clang_file(clang, arguments, BUSTER_ARRAY_LENGTH(arguments), path, &oracle);
    char description[512];
    snprintf(description, sizeof(description), "%s: clang %s at line %u as recorded", fixture->name, fixture->rejected ? "rejects" : "accepts", fixture->line);
    check_test_expect(test, ready && oracle.rejected == fixture->rejected && (!fixture->rejected || oracle.line == fixture->line), description);
    if (ready)
    {
        const bool different = check_corpus_file(tally, path, fixture->name, "mutation-fixture", &options, &oracle, NULL, 64);
        snprintf(description, sizeof(description), "%s: the checker gives clang's result, on both paths alike", fixture->name);
        check_test_expect(test, !different, description);
        ready = unlink(path) == 0;
    }
    check_test_expect(test, ready, "the fixture's file is removed");
}

static void check_test_mutations(CheckTest* test, const char* clang)
{
    char directory[4096];
    const char* temporary = getenv("TMPDIR");
    snprintf(directory, sizeof(directory), "%s/buster-check-mutation-XXXXXX", temporary != NULL ? temporary : "/tmp");
    const bool made = mkdtemp(directory) != NULL;
    check_test_expect(test, made, "the mutation checks' directory");
    if (made)
    {
        check_test_mutation_engine(test, directory);
        CheckCorpusTally tally = { 0 };
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(check_mutation_fixtures); i += 1)
        {
            check_test_mutation_fixture(test, &tally, check_mutation_fixtures + i, i, directory, clang);
        }
        check_test_expect(test, rmdir(directory) == 0, "the mutation checks' directory is removed");
    }
}

#endif
