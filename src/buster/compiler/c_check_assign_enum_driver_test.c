// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original -Wassign-enum sources (#2264, c_check_common_warnings.c's check_common_assign_enum). Clang supplies the
// external diagnostic oracle. Include after c_check_warning_driver_test.c, whose fixture runner compares complete
// no-caret diagnostics through both kernels and lane requests, under the default, -Werror and -w policies, with
// -fsyntax-only and -c.

static const CheckDriverDefaultWarningFixture check_driver_assign_enum_fixtures[] =
{
    {
        // Assignments, compound assignments, initializers, returns, prototype arguments, brace
        // initializers and a typedef's spelling; a flag_enum's bits; enumerators, a variable and a cast don't warn.
        .name = "integer constants out of an enumeration",
        .source = "enum A { A1, A2 } a;\n"
                  "enum B { B1 = 1, B2 = 4, B3 = 8 } b;\n"
                  "enum __attribute__((flag_enum)) F { F1 = 1, F2 = 4 } fl;\n"
                  "typedef enum A TA;\n"
                  "void g(enum A);\n"
                  "enum A h(void) { return 7; }\n"
                  "void f(int x)\n"
                  "{\n"
                  "    a = 5; a = 1; a = A2; a = x; a = -1; a = 1 ? 3 : 0;\n"
                  "    enum A c = 7; TA d = 9; b = 2; b = 4; b = 3; fl = 5; fl = 2;\n"
                  "    g(4); a += 9; a = (enum A)9; a = 'x';\n"
                  "    enum A arr[2] = { 1, 6 };\n"
                  "    struct { enum A m; } s = { 9 };\n"
                  "    (void)c; (void)d; (void)arr; (void)s;\n"
                  "}\n"
                  "enum A k = 9;\n",
        .options = { "-Wassign-enum" },
        .warnings = 15,
    },
    {
        // enum_extensibility(open) and (closed); an anonymous typedef's; a value in the enumeration's width and signedness
        // (negative, past int, a fixed underlying type's truncation); a trailing flag_enum's masks; bit-fields; unreachable
        // and unevaluated code; designators, a compound literal; _Bool, character, conditional and comma sources.
        .name = "integer constants out of an enumeration's edges",
        .source = "enum __attribute__((enum_extensibility(open))) O { O1 } o;\n"
                  "enum __attribute__((enum_extensibility(closed))) C { C1 } cl;\n"
                  "typedef enum { U1, U2 } T;\n"
                  "enum N { NM = -1, N0, NB = 0x80000000u } n;\n"
                  "enum M { M1 = 1 } __attribute__((flag_enum)) m;\n"
                  "enum : unsigned char { X0 = 0, X1 = 1 } ux;\n"
                  "struct R { enum N f : 4; enum N g; } r;\n"
                  "void take(T, ...);\n"
                  "const enum N cn = 7;\n"
                  "void f(int x)\n"
                  "{\n"
                  "    o = 3; cl = 3; T t = 9; n = -1; n = 0x80000000u; n = 3; m = 1; m = ~1; m = 2;\n"
                  "    ux = 256; ux = 257; r.f = 5; r.g = 0; r.g = 2;\n"
                  "    take(5, 6);\n"
                  "    if (0) n = 9;\n"
                  "    unsigned long z = sizeof(n = 9);\n"
                  "    enum N arr[] = { [3] = 9, 1, 0 };\n"
                  "    struct R rr = { .g = 9 };\n"
                  "    T *pt = &(T){ 7 };\n"
                  "    n = (_Bool)1; n = (char)'a'; n = 0 ? 1 : 2; n = (1, 3);\n"
                  "    (void)t; (void)z; (void)arr; (void)rr; (void)pt;\n"
                  "}\n",
        .options = { "-Wassign-enum" },
        .c23 = true,
        .warnings = 21,
    },
    {
        // A braced scalar, a nested definition's enumerators, file-scope aggregates, a macro's argument, a pragma's
        // suppression and a return's conditional.
        .name = "integer constants out of an enumeration at file scope",
        .source = "enum A { A1, A2 = sizeof(enum B { B1, B2 = 5 }) } a;\n"
                  "enum A x = { 9 };\n"
                  "enum B y = 3;\n"
                  "struct S { enum A m[2]; enum B n; } s = { { 1, 7 }, 4 };\n"
                  "#define SET(v) a = v\n"
                  "#pragma clang diagnostic push\n"
                  "#pragma clang diagnostic ignored \"-Wassign-enum\"\n"
                  "enum A ig = 9;\n"
                  "#pragma clang diagnostic pop\n"
                  "enum A f(void) { SET(3); return 0 ? 2 : 3; }\n",
        .options = { "-Wassign-enum" },
        .warnings = 7,
    },
    {
        // Off by default; a pragma enables and ignores it.
        .name = "assign-enum pragmas",
        .source = "#pragma clang diagnostic warning \"-Wassign-enum\"\n"
                  "enum A { A1, A2 } a;\n"
                  "void f(void) { a = 5; }\n"
                  "#pragma clang diagnostic ignored \"-Wassign-enum\"\n"
                  "void g(void) { a = 5; }\n",
        .warnings = 1,
    },
};

static void check_test_driver_assign_enum(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_fixtures(test, clang, executable, directory, check_driver_assign_enum_fixtures,
                                      BUSTER_ARRAY_LENGTH(check_driver_assign_enum_fixtures));
}
