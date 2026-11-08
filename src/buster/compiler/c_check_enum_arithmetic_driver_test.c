// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original sources for clang's checkEnumArithmeticConversions (#1447): the usual arithmetic conversions of enumerations
// of different types, or of an enumeration and a floating type — -Wenum-compare, -Wenum-enum-conversion,
// -Wenum-compare-conditional, -Wenum-float-conversion and -Wanon-enum-enum-conversion, with their groups. The shared
// warning harness compares the pinned Clang's complete diagnostics through both kernels and requested lanes.

static const char check_driver_enum_mix_source[] =
    "enum E { A, B, A2 = A + 1 }; enum F { C, D, D2 = A | C };\n"
                  "typedef enum { T1, T2 } Tn; enum { U1, U2 }; enum { V1 };\n"
                  "#define MIX(a, b) ((a) + (b))\n"
                  "int g(int);\n"
                  "int f(enum E e, enum F f2, Tn t, double d, float _Complex z, unsigned u, long double ld) {\n"
                  "    int r = 0;\n"
                  "    r += (e + f2) < (A | C);\n"
                  "    r += MIX(e, f2);\n"
                  "    r += sizeof(e * f2);\n"
                  "    r += e < u;\n"
                  "    r += t + e;\n"
                  "    r += U1 + V1;\n"
                  "    r += U1 + e;\n"
                  "    r += e + d;\n"
                  "    r += d * e;\n"
                  "    r += z + e;\n"
                  "    r += e == d;\n"
                  "    d += e;\n"
                  "    d -= A;\n"
                  "    r += (e ? d : B) > 0;\n"
                  "    r += (int)e + f2;\n"
                  "    r += (enum E)f2 + e;\n"
                  "    r += g(e - f2);\n"
                  "    r += e % (f2 ^ A) / C;\n"
                  "    if (0) r += e & f2;\n"
                  "    return r;\n"
                  "    r += e - f2;\n"
                  "}\n"
                  "int h(enum E e, enum F f2) { return e <= f2 && e != C; }\n";

static const CheckDriverDefaultWarningFixture check_driver_enum_arithmetic_fixtures[] =
{
    {
        .name = "comparisons of different enumerations warn by default, at their operators",
        .source = check_driver_enum_mix_source,
        .warnings = 3,
    },
    {
        .name = "-Wenum-conversion's arithmetic, bitwise, conditional and floating mixes",
        .source = check_driver_enum_mix_source,
        .options = { "-Wenum-conversion" },
        .warnings = 19,
        .notes = 1,
    },
    {
        .name = "-Wanon-enum-enum-conversion and -Wenum-float-conversion alone",
        .source = check_driver_enum_mix_source,
        .options = { "-Wanon-enum-enum-conversion", "-Wenum-float-conversion" },
        .warnings = 12,
    },
    {
        .name = "-Werror=enum-compare with -Wenum-enum-conversion",
        .source = check_driver_enum_mix_source,
        .options = { "-Werror=enum-compare", "-Wenum-enum-conversion" },
        .warnings = 10,
        .errors = 2,
        .notes = 1,
    },
    {
        .name = "-Wconversion holds -Wenum-conversion",
        .source = "enum E { A, B }; enum F { C, D }; enum { X, Y }; typedef enum E TE;\n"
                  "int f(enum E e, enum F g, TE t, float fl, int i) {\n"
                  "    int r = 0;\n"
                  "    r += e + g;\n"
                  "    r += A + C;\n"
                  "    r += e | g;\n"
                  "    r += e < g;\n"
                  "    r += i ? e : g;\n"
                  "    r += e + X;\n"
                  "    r += e + fl > 0;\n"
                  "    r += t + g;\n"
                  "    r += e + t;\n"
                  "    e += g;\n"
                  "    r += (e, g);\n"
                  "    r += A & D;\n"
                  "    r += e * C;\n"
                  "    r += e == g;\n"
                  "    r += e << g;\n"
                  "    return r;\n"
                  "}\n",
        .options = { "-Wconversion" },
        .warnings = 11,
    },
    {
        .name = "pragmas map the enumeration warnings at their operators",
        .source = "enum E { A, B }; enum F { C, D };\n"
                  "int f(enum E e, enum F g) {\n"
                  "    int r = e < g;\n"
                  "#pragma clang diagnostic push\n"
                  "#pragma clang diagnostic ignored \"-Wenum-compare\"\n"
                  "    r += e == g;\n"
                  "#pragma clang diagnostic warning \"-Wenum-enum-conversion\"\n"
                  "    r += e + g;\n"
                  "#pragma clang diagnostic pop\n"
                  "    r += e != C;\n"
                  "    r += e + g;\n"
                  "    return r;\n"
                  "}\n",
        .warnings = 3,
    },
};

static void check_test_driver_enum_arithmetic(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_fixtures(test, clang, executable, directory, check_driver_enum_arithmetic_fixtures,
                                      BUSTER_ARRAY_LENGTH(check_driver_enum_arithmetic_fixtures));
}
