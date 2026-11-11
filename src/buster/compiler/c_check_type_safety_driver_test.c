// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// clang's type tags (#1447, c_check_type_safety_input.c): argument_with_type_tag, pointer_with_type_tag and
// type_tag_for_datatype, by the driver against clang, on original fixtures. First -Wtype-safety's warnings at calls
// (check_common_type_safety), through the shared warning harness at each policy, both paths and lanes: tags named by
// variables and by registered values, through casts, & and *, commas and constant conditions; mismatched, matching,
// qualified, aliased, character, layout-compatible and null pointer tags; inherited and replaced rules; registrations
// before and after calls; and a pragma. Then each declaration unit's whole output, by both paths, in GNU C17 and C23: the
// attributes' parser's and handlers' errors, their subjects and counts, and their C23 spellings.

static const char check_driver_type_safety_calls_source[] =
    "typedef struct ompi_datatype_t *MPI_Datatype;\n"
    "struct ompi_predefined_datatype_t { int a; };\n"
    "typedef int MPI_Int;\n"
    "typedef struct { int x; } Pair;\n"
    "typedef struct { int y; } Same;\n"
    "typedef struct { long y; } Other;\n"
    "typedef union { int i; float f; } U1;\n"
    "typedef union { float g; int j; } U2;\n"
    "typedef enum { E0 } E1;\n"
    "typedef enum { F0 } E2;\n"
    "extern struct ompi_predefined_datatype_t mpi_int __attribute__((type_tag_for_datatype(mpi, int)));\n"
    "extern struct ompi_predefined_datatype_t mpi_double __attribute__((type_tag_for_datatype(mpi, double)));\n"
    "extern struct ompi_predefined_datatype_t mpi_alias __attribute__((type_tag_for_datatype(mpi, MPI_Int)));\n"
    "extern struct ompi_predefined_datatype_t mpi_null __attribute__((type_tag_for_datatype(mpi, void, must_be_null)));\n"
    "extern struct ompi_predefined_datatype_t mpi_pair __attribute__((type_tag_for_datatype(mpi, Pair, layout_compatible)));\n"
    "extern struct ompi_predefined_datatype_t mpi_union __attribute__((type_tag_for_datatype(mpi, U1, layout_compatible)));\n"
    "extern struct ompi_predefined_datatype_t mpi_enum __attribute__((type_tag_for_datatype(mpi, E1, layout_compatible)));\n"
    "extern struct ompi_predefined_datatype_t mpi_char __attribute__((type_tag_for_datatype(mpi, char)));\n"
    "extern struct ompi_predefined_datatype_t mpi_const __attribute__((type_tag_for_datatype(mpi, const int)));\n"
    "extern struct ompi_predefined_datatype_t other_int __attribute__((type_tag_for_datatype(other, int)));\n"
    "#define MPI_INT ((MPI_Datatype)&mpi_int)\n"
    "#define MPI_DOUBLE ((MPI_Datatype)&mpi_double)\n"
    "int MPI_Send(const void *buf, int count, MPI_Datatype datatype) __attribute__((pointer_with_type_tag(mpi, 1, 3)));\n"
    "int MPI_Recv(void *buf, int count, MPI_Datatype datatype);\n"
    "int MPI_Recv(void *buf, int count, MPI_Datatype datatype) __attribute__((pointer_with_type_tag(mpi, 1, 3)));\n"
    "int MPI_Recv(void *buf, int count, MPI_Datatype datatype);\n"
    "void put(int value, MPI_Datatype datatype) __attribute__((argument_with_type_tag(mpi, 1, 2)));\n"
    "void (*sender)(void *buf, MPI_Datatype datatype) __attribute__((pointer_with_type_tag(mpi, 1, 2)));\n"
    "static const int magic_long __attribute__((type_tag_for_datatype(magic, long))) = 42;\n"
    "enum { MAGIC_DOUBLE = 7 };\n"
    "static const int magic_double __attribute__((type_tag_for_datatype(magic, double))) = MAGIC_DOUBLE;\n"
    "void take(void *buffer, int tag) __attribute__((pointer_with_type_tag(magic, 1, 2)));\n"
    "void use(int c) {\n"
    "    int i = 0; double d = 0; float f = 0; long l = 0; char ch = 0; signed char sc = 0; unsigned char uc = 0;\n"
    "    const int ci = 0; MPI_Int alias = 0; int array[4] = { 0 };\n"
    "    Pair pair; Same same; Other other; U1 u1; U2 u2; E1 e1; E2 e2;\n"
    "    MPI_Send(&i, 1, MPI_INT);\n"
    "    MPI_Send(&d, 1, MPI_INT);\n"
    "    MPI_Send(&f, 1, MPI_DOUBLE);\n"
    "    MPI_Send(&l, 1, (MPI_Datatype)&mpi_double);\n"
    "    MPI_Send(array, 4, MPI_INT);\n"
    "    MPI_Send(&array, 4, MPI_INT);\n"
    "    MPI_Send(&ci, 1, MPI_INT);\n"
    "    MPI_Send(&ci, 1, (MPI_Datatype)&mpi_const);\n"
    "    MPI_Send(&i, 1, (MPI_Datatype)&mpi_const);\n"
    "    MPI_Send(&alias, 1, (MPI_Datatype)&mpi_alias);\n"
    "    MPI_Send(&d, 1, (MPI_Datatype)&mpi_alias);\n"
    "    MPI_Send((void *)&d, 1, MPI_INT);\n"
    "    MPI_Send(0, 1, MPI_INT);\n"
    "    MPI_Send(&i, 1, (MPI_Datatype)&mpi_null);\n"
    "    MPI_Send(0, 1, (MPI_Datatype)&mpi_null);\n"
    "    MPI_Send(&ch, 1, (MPI_Datatype)&mpi_char);\n"
    "    MPI_Send(&sc, 1, (MPI_Datatype)&mpi_char);\n"
    "    MPI_Send(&uc, 1, (MPI_Datatype)&mpi_char);\n"
    "    MPI_Send(&same, 1, (MPI_Datatype)&mpi_pair);\n"
    "    MPI_Send(&other, 1, (MPI_Datatype)&mpi_pair);\n"
    "    MPI_Send(&pair, 1, (MPI_Datatype)&mpi_pair);\n"
    "    MPI_Send(&u2, 1, (MPI_Datatype)&mpi_union);\n"
    "    MPI_Send(&u1, 1, (MPI_Datatype)&mpi_union);\n"
    "    MPI_Send(&e2, 1, (MPI_Datatype)&mpi_enum);\n"
    "    MPI_Send(&i, 1, (MPI_Datatype)&other_int);\n"
    "    MPI_Send(&d, 1, c ? MPI_INT : MPI_DOUBLE);\n"
    "    MPI_Send(&d, 1, 1 ? MPI_INT : MPI_DOUBLE);\n"
    "    MPI_Send(&d, 1, (0, MPI_INT));\n"
    "    MPI_Send((&d), 1, MPI_INT);\n"
    "    MPI_Send(c ? &d : &d, 1, MPI_INT);\n"
    "    MPI_Send(array + 1, 1, MPI_DOUBLE);\n"
    "    MPI_Send(&pair.x, 1, MPI_DOUBLE);\n"
    "    MPI_Send(&d, 1, MPI_INT), MPI_Send(&f, 1, MPI_INT);\n"
    "    MPI_Recv(&d, 1, MPI_INT);\n"
    "    put(i, MPI_INT);\n"
    "    put(d, MPI_INT);\n"
    "    put(l, (MPI_Datatype)&mpi_double);\n"
    "    sender(&d, MPI_INT);\n"
    "    (*sender)(&d, MPI_INT);\n"
    "    take(&l, 42);\n"
    "    take(&d, 42);\n"
    "    take(&l, MAGIC_DOUBLE);\n"
    "    take(&l, 7);\n"
    "    take(&d, 43);\n"
    "}\n";

static const char check_driver_type_safety_order_source[] =
    "typedef int Datatype;\n"
    "void put(void *buffer, Datatype type) __attribute__((pointer_with_type_tag(tag, 1, 2)));\n"
    "void early(long *l) { put(l, 5); }\n"
    "static const Datatype tag_int __attribute__((type_tag_for_datatype(tag, int))) = 5;\n"
    "void middle(long *l) { put(l, 5); }\n"
    "static const Datatype tag_long __attribute__((type_tag_for_datatype(tag, long))) = 5;\n"
    "void late(long *l, int *i) {\n"
    "    put(l, 5);\n"
    "    put(i, 5);\n"
    "#pragma clang diagnostic push\n"
    "#pragma clang diagnostic ignored \"-Wtype-safety\"\n"
    "    put(i, 5);\n"
    "#pragma clang diagnostic pop\n"
    "    put(i, 0x5);\n"
    "    put(i, 5u);\n"
    "    put(i, (5));\n"
    "    put(i, '\\5');\n"
    "    put(i, +5);\n"
    "    put(i, (Datatype)5);\n"
    "    {\n"
    "        static const Datatype local __attribute__((type_tag_for_datatype(tag, int))) = 6;\n"
    "    }\n"
    "    put(l, 6);\n"
    "}\n"
    "struct ompi { int a; };\n"
    "extern struct ompi tag_short __attribute__((type_tag_for_datatype(tag, short)));\n"
    "extern struct ompi tag_short;\n"
    "void put2(void *buffer, struct ompi *type) __attribute__((pointer_with_type_tag(tag, 1, 2)));\n"
    "void put2(void *buffer, struct ompi *type);\n"
    "void put2(void *buffer, struct ompi *type) __attribute__((argument_with_type_tag(tag, 1, 2)));\n"
    "void again(int *i) { put2(i, &tag_short); }\n";

static const CheckDriverDefaultWarningFixture check_driver_type_safety_fixtures[] =
{
    {
        .name = "-Wtype-safety's calls",
        .source = check_driver_type_safety_calls_source,
        .warnings = 25,
    },
    {
        .name = "-Wtype-safety's calls with an unsigned plain char",
        .source = check_driver_type_safety_calls_source,
        .options = { "-funsigned-char" },
        .warnings = 25,
    },
    {
        .name = "-Wtype-safety's calls in C23",
        .source = check_driver_type_safety_calls_source,
        .c23 = true,
        .warnings = 25,
    },
    {
        .name = "-Wno-type-safety",
        .source = check_driver_type_safety_calls_source,
        .options = { "-Wno-type-safety" },
        .warnings = 1,
    },
    {
        .name = "-Werror=type-safety",
        .source = check_driver_type_safety_calls_source,
        .options = { "-Werror=type-safety" },
        // clang's error limit stops it at its twentieth error, before the other warning.
        .errors = 20,
    },
    {
        .name = "registered values in source order, a pragma and redeclarations' rules",
        .source = check_driver_type_safety_order_source,
        .warnings = 8,
    },
};

static void check_test_driver_type_safety(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_fixtures(test, clang, executable, directory, check_driver_type_safety_fixtures,
                                      BUSTER_ARRAY_LENGTH(check_driver_type_safety_fixtures));
    static const char* const units[] = {
        "void f(int *p, int t) __attribute__((pointer_with_type_tag(mpi, 1, 2)));\n",
        "void f(int p, int t) __attribute__((pointer_with_type_tag(mpi, 1, 2)));\n",
        "void f(int *p, int t) __attribute__((pointer_with_type_tag(mpi, 3, 2)));\n",
        "void f(int *p, int t) __attribute__((pointer_with_type_tag(mpi, 1, 3)));\n",
        "void f(int *p, int t) __attribute__((pointer_with_type_tag(1, 1, 2)));\n",
        "void f(int *p, int t) __attribute__((pointer_with_type_tag(mpi, 1)));\n",
        "void f(int *p, int t) __attribute__((pointer_with_type_tag(mpi, 0, 2)));\n",
        "void f(int *p, int t) __attribute__((pointer_with_type_tag(mpi, x, 2)));\n",
        "void f(int *p, int t) __attribute__((pointer_with_type_tag(mpi, 1.0, 2)));\n",
        "void f(int *p, int t) __attribute__((pointer_with_type_tag(mpi, 1, 2, 3)));\n",
        "void f(int *p, int t) __attribute__((pointer_with_type_tag));\n",
        "void f(int *p, int t) __attribute__((pointer_with_type_tag(mpi, -1, 2)));\n",
        "void f(int *p, int t) __attribute__((pointer_with_type_tag(mpi, 4294967297, 2)));\n",
        "void f(int *p, int t) __attribute__((argument_with_type_tag(mpi, 1, 2)));\n",
        "void f(int p, int t) __attribute__((argument_with_type_tag(mpi, 1, 2)));\n",
        "int v __attribute__((argument_with_type_tag(mpi, 1, 2)));\n",
        "void (*fp)(int *p, int t) __attribute__((pointer_with_type_tag(mpi, 1, 2)));\n",
        "void f() __attribute__((pointer_with_type_tag(mpi, 1, 2)));\n",
        "typedef void T(int *p, int t) __attribute__((pointer_with_type_tag(mpi, 1, 2)));\n",
        "void f(int *p, int t) __attribute__((argument_with_type_tag(mpi, 1, 2), pointer_with_type_tag(other, 1, 2)));\n",
        "void f(int *p, int t) __attribute__((__pointer_with_type_tag__(mpi, 1, 2)));\n",
        "struct S { int a; } __attribute__((pointer_with_type_tag(mpi, 1, 2)));\n",
        "void f(int *p __attribute__((pointer_with_type_tag(mpi, 1, 2))), int t);\n",
        "void f(int *p, int t) __attribute__((pointer_with_type_tag(mpi, 2 - 1, 1 + 1)));\n",
        "int t __attribute__((type_tag_for_datatype(mpi, int)));\n",
        "int t __attribute__((type_tag_for_datatype(mpi, int, layout_compatible)));\n",
        "int t __attribute__((type_tag_for_datatype(mpi, int, must_be_null)));\n",
        "int t __attribute__((type_tag_for_datatype(mpi, int, layout_compatible, must_be_null, layout_compatible)));\n",
        "int t __attribute__((type_tag_for_datatype(mpi, int, foo)));\n",
        "int t __attribute__((type_tag_for_datatype(1, int)));\n",
        "int t __attribute__((type_tag_for_datatype(mpi int)));\n",
        "int t __attribute__((type_tag_for_datatype(mpi, 1)));\n",
        "int t __attribute__((type_tag_for_datatype(mpi, int, 1)));\n",
        "int t __attribute__((type_tag_for_datatype));\n",
        "int t __attribute__((type_tag_for_datatype()));\n",
        "int t __attribute__((type_tag_for_datatype(mpi)));\n",
        "int t __attribute__((type_tag_for_datatype(mpi,)));\n",
        "int t __attribute__((type_tag_for_datatype(mpi, foo)));\n",
        "int t __attribute__((type_tag_for_datatype(mpi, int,)));\n",
        "int t __attribute__((type_tag_for_datatype(mpi, int *)));\n",
        "int t __attribute__((type_tag_for_datatype(mpi, struct S)));\n",
        "typedef long L;\n" "int t __attribute__((type_tag_for_datatype(mpi, L)));\n",
        "int t __attribute__((type_tag_for_datatype(mpi, __layout_compatible__)));\n",
        "int t __attribute__((type_tag_for_datatype(mpi, int, __layout_compatible__)));\n",
        "void f(void) __attribute__((type_tag_for_datatype(mpi, int)));\n",
        "typedef int T __attribute__((type_tag_for_datatype(mpi, int)));\n",
        "struct S { int m __attribute__((type_tag_for_datatype(mpi, int))); };\n",
        "struct __attribute__((type_tag_for_datatype(mpi, int))) S { int m; };\n",
        "void f(void) { int l __attribute__((type_tag_for_datatype(mpi, int))); }\n",
        "void f(void) { static int l __attribute__((type_tag_for_datatype(mpi, int))) = 3; }\n",
        "static const int m __attribute__((type_tag_for_datatype(mpi, int))) = 3;\n",
        "int k;\n" "const int m __attribute__((type_tag_for_datatype(mpi, int))) = k;\n",
        "const double m __attribute__((type_tag_for_datatype(mpi, int))) = 1.5;\n",
        "const int *m __attribute__((type_tag_for_datatype(mpi, int))) = 0;\n",
        "static const int m __attribute__((type_tag_for_datatype(mpi, int), type_tag_for_datatype(mpi, long))) = 3;\n",
        "extern const int m __attribute__((type_tag_for_datatype(mpi, int)));\n" "const int m = 4;\n",
        "int t __attribute__((__type_tag_for_datatype__(mpi, int)));\n",
        "[[clang::type_tag_for_datatype(mpi, int)]] int t;\n",
        "int t [[clang::type_tag_for_datatype(mpi, int, must_be_null)]];\n",
        "[[clang::pointer_with_type_tag(mpi, 1, 2)]] void f(int *p, int t);\n",
        "void f(int *p, int t) [[clang::argument_with_type_tag(mpi, 1, 2)]];\n",
        "void f(int *p, int t, ...) __attribute__((pointer_with_type_tag(mpi, 3, 2)));\n",
        "void f(int k) { const int m __attribute__((type_tag_for_datatype(mpi, int))) = k; }\n",
        "void f(int k) { int m __attribute__((type_tag_for_datatype(mpi, int))) = k + 1, n = 2; }\n",
        "void f(int k) { int m __attribute__((type_tag_for_datatype(mpi, int))) = 1, n __attribute__((type_tag_for_datatype(mpi, int))) = k; }\n",
        "int m __attribute__((type_tag_for_datatype(mpi, int))) = 1;\n",
        "enum E { A = 3 } e __attribute__((type_tag_for_datatype(mpi, int))) = A;\n",
        "_Bool b __attribute__((type_tag_for_datatype(mpi, int))) = 1;\n",
        "long long x __attribute__((type_tag_for_datatype(mpi, int))) = -1;\n",
        "int y __attribute__((type_tag_for_datatype(mpi, int))) = { 1 };\n",
    };
    static const char* const standards[] = { "-std=gnu17", "-std=gnu23" };
    enum { LINES = 16384 };
    char* expected_lines = malloc(LINES);
    char* actual_lines = malloc(LINES);
    char source[PATH_MAX];
    snprintf(source, sizeof(source), "%s/type-tags.c", directory);
    const uint32_t total = BUSTER_ARRAY_LENGTH(units) * BUSTER_ARRAY_LENGTH(standards);
    for (uint32_t unit = 0; expected_lines != NULL && actual_lines != NULL && unit < total; unit += 1)
    {
        const char* text = units[unit % BUSTER_ARRAY_LENGTH(units)];
        char* standard = (char*)standards[unit / BUSTER_ARRAY_LENGTH(units)];
        const bool made = parse_test_write(source, text, strlen(text));
        char* arguments[] = { (char*)clang, standard, "-fsyntax-only", "-fno-caret-diagnostics", source, NULL, NULL };
        char* expected = NULL;
        const int oracle = made ? check_test_driver_spawn(arguments, &expected) : -1;
        const bool lines = expected != NULL && check_test_diagnostic_lines(expected, expected_lines, LINES);
        char description[4096];
        snprintf(description, sizeof(description), "type tags %u: clang's lines read", unit);
        check_test_expect(test, made && lines, description);
        for (uint32_t scalar = 0; lines && scalar < 2; scalar += 1)
        {
            arguments[0] = (char*)executable;
            arguments[5] = scalar != 0 ? "--scalar" : NULL;
            char* actual = NULL;
            const int status = check_test_driver_spawn(arguments, &actual);
            const bool same = actual != NULL && check_test_diagnostic_lines(actual, actual_lines, LINES) && strcmp(expected_lines, actual_lines) == 0;
            snprintf(description, sizeof(description), "type tags %u (%s %s), %s: status %d (clang %d), lines\n%s(clang\n%s)", unit, standard, text,
                     scalar != 0 ? "scalar" : "vector", status, oracle, actual_lines, expected_lines);
            check_test_expect(test, same && (status != 0) == (oracle != 0), description);
            free(actual);
        }
        free(expected);
    }
    free(expected_lines);
    free(actual_lines);
    unlink(source);
}
