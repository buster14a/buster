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

// Operands of an enumeration type that are no named variable: members, calls, casts, subscripts, dereferences and
// comma operators' (CheckStream.enum_operands).
static const char check_driver_enum_forms_source[] =
    "enum E1 { A1, B1 };\n"
    "enum E2 { A2, B2 };\n"
    "struct S { enum E1 a; enum E2 b; };\n"
    "enum E1 f1(void);\n"
    "enum E2 f2(void);\n"
    "int g(struct S s, struct S* p, enum E1* q, enum E2 r[2], int i)\n"
    "{\n"
    "    int x = s.a + s.b;\n"
    "    x += p->a == p->b;\n"
    "    x += f1() | f2();\n"
    "    x += (enum E1)i * (enum E2)i;\n"
    "    x += *q - r[1];\n"
    "    x += (i, *q) < (i, r[0]);\n"
    "    return x + (i ? s.a : s.b);\n"
    "}\n";

// The constant reader's operators (CHECK_WARNING_EVENT_ENUM_MIX): array bounds of declarators, type names and members,
// a bit-field's width, enumerators' values, and a _Static_assert's and an initializer's, which the graph holds too.
static const char check_driver_enum_bounds_source[] =
    "enum E1 { A1, B1 };\n"
    "enum E2 { A2, B2 };\n"
    "int arr[A1 + A2];\n"
    "int s1 = sizeof(char[A1 | A2]);\n"
    "typedef int T[A1 * A2 + 1];\n"
    "enum C { X = A1 + A2, Y = B1 == B2, Z = (A1 < A2) };\n"
    "int f(void) { int local[A1 - A2 + 2]; return sizeof(int[A1 ^ A2 + 1]) + sizeof local; }\n"
    "_Static_assert(A1 + A2 == 0, \"\");\n"
    "struct S { int m[A1 + B2 + 1]; int bf : A1 + B2 + 1; };\n"
    "int c1 = A1 ? A1 : A2;\n"
    "enum D { P = (int)(1.0 + A1) };\n";

static const char check_driver_enum_constants_source[] =
    "typedef enum E1 { A1, B1 } TE1;\n"
    "typedef enum E2 { A2, B2 } TE2;\n"
    "enum { U1, U2 };\n"
    "enum C { X, Y = X + A1, Z = Y | A2, W = (TE1)1 + (TE2)1 };\n"
    "int a1[(A1 < A2) + 1];\n"
    "int a2[1 + (B1 ? A1 : A2)];\n"
    "enum F { Q = (int)(A1 + 2.0f) };\n"
    "int a4[U1 + A1 + 1];\n"
    "int a5[sizeof(typeof(A1 + A2))];\n"
    "_Alignas(4 << (A1 & A2)) int a6;\n"
    "int g(int i) {\n"
    "    switch (i) { case A1 + A2: return 1; }\n"
    "    return sizeof(char[(enum E1)1 * (TE2)1]);\n"
    "}\n"
    "struct S { unsigned bf : (A1 != B2) + 1; };\n";

static const CheckDriverDefaultWarningFixture check_driver_enum_arithmetic_fixtures[] =
{
    {
        // #1829: exercise vector chunks and a scalar tail while preserving same-enum and integer fallbacks.
        .name = "enumerator coercion across 9 completed definitions",
        .source = "enum E0 { V0 };\n"
                  "enum E1 { V1 };\n"
                  "enum E2 { V2 };\n"
                  "enum E3 { V3 };\n"
                  "enum E4 { V4 };\n"
                  "enum E5 { V5 };\n"
                  "enum E6 { V6 };\n"
                  "enum E7 { V7 };\n"
                  "enum E8 { V8 };\n"
                  "int f(enum E4 mid, int x) { return (V0 == V8) + (V8 == V0) + (mid == V4) + (x == V8); }\n",
        .warnings = 2,
    },
    {
        .name = "enumerator coercion across 17 completed definitions",
        .source = "enum E0 { V0 };\n"
                  "enum E1 { V1 };\n"
                  "enum E2 { V2 };\n"
                  "enum E3 { V3 };\n"
                  "enum E4 { V4 };\n"
                  "enum E5 { V5 };\n"
                  "enum E6 { V6 };\n"
                  "enum E7 { V7 };\n"
                  "enum E8 { V8 };\n"
                  "enum E9 { V9 };\n"
                  "enum E10 { V10 };\n"
                  "enum E11 { V11 };\n"
                  "enum E12 { V12 };\n"
                  "enum E13 { V13 };\n"
                  "enum E14 { V14 };\n"
                  "enum E15 { V15 };\n"
                  "enum E16 { V16 };\n"
                  "int f(enum E8 mid, int x) { return (V0 == V16) + (V16 == V0) + (V7 == V8) + (mid == V8) + (x == V16); }\n",
        .warnings = 3,
    },
    {
        // The nearest opening definition is an already-closed inner enum for OuterLast and PeerLast.
        // This exercises check_common_enum_coerced's linear fallback, not its binary-search fast path.
        .name = "enumerator coercion with nested definitions and closed inner scopes",
        .source = "enum Outer { OuterFirst = sizeof(enum Inner { InnerOnly }), OuterLast };\n"
                  "enum Peer { PeerFirst = sizeof(enum PeerInner { PeerInnerOnly }), PeerLast };\n"
                  "int f(enum Outer a, enum Peer b) { return (OuterLast == PeerLast) + (a == OuterLast) + (b == PeerLast); }\n",
        .warnings = 1,
    },
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
        // Review on #1809: a comparison's operands are rvalues, and a GNU `?:`'s omitted operand is its promoted condition.
        .name = "qualified comparisons and GNU omitted conditional operands",
        .source = "enum E { A, B }; enum F { C, D }; typedef enum E TN; typedef const enum E CNST; typedef enum { X } AN;\n"
                  "int f(const enum E ce, volatile enum E ve, enum F f2, const double cd, const TN ct, CNST cn, volatile float vf, enum E e, double d, const AN an) {\n"
                  "    int r = 0;\n"
                  "    r += ce < f2; r += f2 == ve; r += ce < cd; r += vf > e; r += ct != f2; r += cn <= f2; r += an == f2;\n"
                  "    r += ce + f2; r += ct | f2; r += cd * ce;\n"
                  "    r += e ?: f2; r += e ?: 1.5; r += e ?: d; r += cd ?: e; r += d ?: e; r += A ?: C;\n"
                  "    return r;\n"
                  "}\n",
        .warnings = 5,
    },
    {
        .name = "qualified comparisons and GNU omitted conditional operands under -Wenum-conversion",
        .source = "enum E { A, B }; enum F { C, D }; typedef enum E TN; typedef const enum E CNST; typedef enum { X } AN;\n"
                  "int f(const enum E ce, volatile enum E ve, enum F f2, const double cd, const TN ct, CNST cn, volatile float vf, enum E e, double d, const AN an) {\n"
                  "    int r = 0;\n"
                  "    r += ce < f2; r += f2 == ve; r += ce < cd; r += vf > e; r += ct != f2; r += cn <= f2; r += an == f2;\n"
                  "    r += ce + f2; r += ct | f2; r += cd * ce;\n"
                  "    r += e ?: f2; r += e ?: 1.5; r += e ?: d; r += cd ?: e; r += d ?: e; r += A ?: C;\n"
                  "    return r;\n"
                  "}\n",
        .options = { "-Wenum-conversion", "-Wanon-enum-enum-conversion" },
        .warnings = 12,
    },
    {
        .name = "enumeration mixes of forms, by default",
        .source = check_driver_enum_forms_source,
        .warnings = 4,
    },
    {
        .name = "enumeration mixes of forms under -Wenum-conversion",
        .source = check_driver_enum_forms_source,
        .options = { "-Wenum-conversion", "-Wanon-enum-enum-conversion" },
        .warnings = 9,
    },
    {
        .name = "enumeration mixes of forms under -w",
        .source = check_driver_enum_forms_source,
        .options = { "-w" },
    },
    {
        .name = "enumeration mixes of bounds, by default",
        .source = check_driver_enum_bounds_source,
        .warnings = 2,
    },
    {
        .name = "enumeration mixes of bounds under -Wenum-conversion",
        .source = check_driver_enum_bounds_source,
        .options = { "-Wenum-conversion", "-Wanon-enum-enum-conversion" },
        .warnings = 12,
    },
    {
        .name = "enumeration mixes of bounds under -w",
        .source = check_driver_enum_bounds_source,
        .options = { "-w" },
    },
    {
        .name = "enumeration mixes of constants, by default",
        .source = check_driver_enum_constants_source,
        .warnings = 2,
    },
    {
        .name = "enumeration mixes of constants under -Wenum-conversion",
        .source = check_driver_enum_constants_source,
        .options = { "-Wenum-conversion", "-Wanon-enum-enum-conversion" },
        .warnings = 10,
    },
    {
        .name = "enumeration mixes of constants under -w",
        .source = check_driver_enum_constants_source,
        .options = { "-w" },
    },
    {
        .name = "-Werror=enum-compare in array bounds and bit-field widths",
        .source = check_driver_enum_constants_source,
        .options = { "-Werror=enum-compare" },
        .errors = 2,
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
