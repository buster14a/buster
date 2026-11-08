// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original sources for clang's -Wimplicit-enum-enum-cast (#1447, the end of CheckImplicitConversion): an implicit
// conversion from one named enumeration to another, an enumerator taken as its enumeration, on by default under
// -Wenum-conversion. The shared warning harness compares the pinned Clang's complete diagnostics through both kernels and
// requested lanes.

static const char check_driver_enum_cast_contexts_source[] =
    "enum E { A, B }; enum F { C, D }; typedef enum E TE; enum { U1 }; typedef enum { T1 } Tn;\n"
                  "struct S { enum E e; enum F f : 2; };\n"
                  "enum E g(enum E);\n"
                  "enum E h(enum F f) {\n"
                  "    enum E e = C;\n"
                  "    enum E e2 = (enum F)0;\n"
                  "    enum F f2 = f;\n"
                  "    e = f;\n"
                  "    e = (f);\n"
                  "    e = 0 ? f : f;\n"
                  "    e = (e, f);\n"
                  "    TE t = f;\n"
                  "    struct S s = { f, e };\n"
                  "    s.e = f;\n"
                  "    g(f);\n"
                  "    e = U1;\n"
                  "    enum E e3 = (enum { V1 })0;\n"
                  "    Tn tn = f;\n"
                  "    e += f;\n"
                  "    e = (enum E)f;\n"
                  "    e = (int)f;\n"
                  "    _Bool bb = f;\n"
                  "    int i = f;\n"
                  "    (void)e2; (void)f2; (void)t; (void)e3; (void)tn; (void)bb; (void)i;\n"
                  "    if (0) e = f;\n"
                  "    return f;\n"
                  "}\n";

static const char check_driver_enum_cast_forms_source[] =
    "enum E { A, B }; enum F { C, D }; typedef enum E TE; typedef enum F TF; typedef enum { T1 } Tn;\n"
                  "enum E ge = C;\n"
                  "TE gt = D;\n"
                  "enum E garr[2] = { C, A };\n"
                  "struct S { enum E e; enum F f; } gs = { C, A };\n"
                  "void take(enum E, enum F);\n"
                  "TE back(enum F f) { return f; }\n"
                  "Tn anon(enum F f) { return f; }\n"
                  "int f(enum E e, TF g, int c) {\n"
                  "    e = c ? (c ? g : C) : D;\n"
                  "    e = (0, 1, g);\n"
                  "    take(g, e);\n"
                  "    take(C, A);\n"
                  "    struct S s = { g, e };\n"
                  "    struct S *p = &(struct S){ g, e };\n"
                  "    enum E arr[] = { g, C, e };\n"
                  "    TE t = g;\n"
                  "    t = C;\n"
                  "    e = (enum F)c;\n"
                  "    e = (enum E)g;\n"
                  "    e += g;\n"
                  "    c = g;\n"
                  "    (void)s; (void)p; (void)arr; (void)t;\n"
                  "#pragma clang diagnostic push\n"
                  "#pragma clang diagnostic ignored \"-Wimplicit-enum-enum-cast\"\n"
                  "    e = g;\n"
                  "#pragma clang diagnostic pop\n"
                  "    if (0) e = g;\n"
                  "    return e = g;\n"
                  "}\n";

static const CheckDriverDefaultWarningFixture check_driver_enum_cast_fixtures[] =
{
    {
        .name = "initializers, assignments, arms, commas, arguments and returns convert between enumerations",
        .source = check_driver_enum_cast_contexts_source,
        .warnings = 16,
    },
    {
        .name = "file-scope, brace, compound literal and unreachable conversions between enumerations, and pragmas",
        .source = check_driver_enum_cast_forms_source,
        .warnings = 28,
    },
    {
        .name = "-Wno-implicit-enum-enum-cast",
        .source = check_driver_enum_cast_forms_source,
        .options = { "-Wno-implicit-enum-enum-cast" },
        .warnings = 2,
    },
    {
        .name = "-Wenum-conversion holds -Wimplicit-enum-enum-cast",
        .source = check_driver_enum_cast_forms_source,
        .options = { "-Wenum-conversion" },
        .warnings = 28,
    },
    {
        .name = "-Werror=implicit-enum-enum-cast",
        .source = check_driver_enum_cast_forms_source,
        .options = { "-Werror=implicit-enum-enum-cast" },
        .errors = 20,
    },
};

static void check_test_driver_enum_casts(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_fixtures(test, clang, executable, directory, check_driver_enum_cast_fixtures,
                                      BUSTER_ARRAY_LENGTH(check_driver_enum_cast_fixtures));
}
