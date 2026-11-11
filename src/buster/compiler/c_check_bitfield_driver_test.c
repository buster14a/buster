// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original sources for clang's AnalyzeBitFieldAssignment (#1447): a constant a bit-field's assignment or initialization
// truncates, -Wbitfield-constant-conversion and -Wsingle-bit-bitfield-constant-conversion, with their groups under
// -Wconstant-conversion and -Wconversion. The shared warning harness compares the pinned Clang's complete diagnostics,
// warning policy and object preservation through both kernels and requested lanes.

static const char check_driver_bitfield_groups_source[] =
    "struct S { int a : 1; int b : 3; } s;\nchar c = 300;\nvoid f(void) { s.a = 1; s.b = 9; }\n";

static const CheckDriverDefaultWarningFixture check_driver_bitfield_fixtures[] =
{
    {
        .name = "assignments truncate constants by their bit-fields' widths and signedness",
        .source = "struct S { int a : 1; signed char c : 2; unsigned u : 3; int b : 3; enum E { X, Y = 9 } e : 2; _Bool z : 1; } s;\n"
                  "void g(void) {\n    s.a = 1;\n    s.c = 200;\n    s.u = 9u;\n    s.b = 'a';\n    s.b = (9);\n    s.b = 3;\n    s.b = 4;\n"
                  "    s.b = -4;\n    s.u = -1;\n    s.z = 2;\n    s.e = Y;\n    s.e = 9;\n    s.b = 9L;\n    s.b = 9.0;\n    s.b += 9;\n    s.b++;\n}\n",
        .warnings = 9,
    },
    {
        .name = "a negated or complemented constant keeps its significant bits",
        .source = "struct S { int b : 3; unsigned u : 3; } s;\n"
                  "void g(void) {\n    s.b = ~8;\n    s.b = -(9);\n    s.b = -4;\n    s.b = -5;\n    s.u = -9;\n    s.u = ~0u;\n    s.u = -1;\n"
                  "    s.b = 0 - 5;\n}\n",
        .warnings = 5,
    },
    {
        .name = "a computed negative constant truncates in an unsigned bit-field, and fitting flags stay quiet",
        .source = "struct S { unsigned u : 3; int b : 3; unsigned v : 1; };\nstruct S g = { 0 - 2, 0 - 4, 1 };\n"
                  "void f(struct S *p) { struct S t = { .u = 0 - 2, .b = -4, .v = 1 }; p->u = 0 - 2; p->b = 0 - 5; p->v = 1; (void)t; }\n",
        .warnings = 4,
    },
    {
        // _BitInt constants past 128 bits (#1447, c_check_wide.c): their values change in narrower types, signed, unsigned
        // and wider than 128 bits, in decimal; a sign flip into a signed target is reported at once, as clang's.
        .name = "constants past 128 bits change value in narrower types",
        .source = "char c = ((_BitInt(200))1 << 150) | 257;\nint s = 340282366920938463463374607431768211457wb;\n"
                  "_BitInt(150) n = (_BitInt(300))1 << 200;\nunsigned _BitInt(129) m = ((unsigned _BitInt(300))1 << 250) + 7;\n"
                  "signed char k = ((_BitInt(200))1 << 7) + 0;\n_BitInt(129) p = ((_BitInt(130))1 << 128) + 1;\nunsigned char u = -(_BitInt(200))1;\n"
                  "short t = -((_BitInt(200))40000);\nunsigned long v = -((_BitInt(300))1 << 64);\n"
                  "int f(void) { int a = (_BitInt(200))1 << 150; unsigned _BitInt(200) b = (_BitInt(256))-1; long d = (_BitInt(200))1 << 63; return a + (int)b + (int)d; }\n",
        .warnings = 11,
    },
    {
        // A declarator after a specifier's parentheses is evaluated (#1447): `_BitInt(100) n = e;` (and `typeof(int) y = e;`) keeps
        // its conversion's warnings, which an operand of typeof's own drops.
        .name = "a declarator after typeof's or _BitInt's parentheses keeps its warnings",
        .source = "_BitInt(100) n = (_BitInt(120))1 << 110;\n__typeof__((char)300) z = 0;\nvoid f(void) { _BitInt(100) m = (_BitInt(120))1 << 110; (void)m; }\n",
        .warnings = 2,
    },
    {
        .name = "an assignment's truncation leaves out its constant conversion and follows clang's walk",
        .source = "struct S { signed char c : 2; unsigned char d : 4; int b : 3; unsigned u : 3; } s;\nint f(int);\n"
                  "void g(int x) {\n    s.c = 200;\n    s.d = 300;\n    s.b = 9, s.b = 10;\n    f(s.b = 12);\n    s.b = s.u = 9;\n    s.b = 1 ? 9 : 2;\n"
                  "    s.b = 1 << 32;\n    x ? (s.b = 9) : (s.u = 9);\n    0 && (s.b = 10);\n    return;\n    s.b = 9;\n}\n",
        .warnings = 13,
    },
    {
        .name = "typedefs, enumerations, members through pointers and wide bit-fields",
        .source = "typedef unsigned U;\ntypedef int I;\nenum E { A = 9, B = -9 };\n"
                  "struct S { int b : 3; unsigned u : 3; long long l : 40; enum E e : 3; unsigned char c : 4; I t : 2; };\n"
                  "void f(struct S *p) {\n    p->b = (I)9;\n    p->u = (U)9;\n    p->l = 0x10000000000LL;\n    p->l = -0x8000000001LL;\n    p->e = A;\n"
                  "    p->e = B;\n    p->c = 300;\n    p->t = -3;\n    p->b = 'z';\n    p->b = 1u << 31;\n}\n",
        .warnings = 10,
    },
    {
        .name = "initializers truncate in member order before their conversions",
        .source = "struct S { int a : 1; signed char c : 2; unsigned u : 3; int b : 3; int w; char z; };\n"
                  "struct S g1 = { 1, 200, 9, 1 + 8, 1.5, 300 };\nstruct S g2 = { .b = -5, .u = ~0u, .c = (char)200 };\n"
                  "int f(int x) {\n    struct S t = { 1, 200, 9, 1 + 8, 0, 1.5 };\n    struct S v[2] = { [1].b = 9, [0] = { .u = -9 } };\n"
                  "    struct S y = { x, 9, 9, x / 0, 300 };\n    return t.a + v[0].b + y.w;\n}\n",
        .warnings = 18,
    },
    {
        .name = "an initializer's truncation stands at its value's grouping parenthesis",
        .source = "struct S { int b : 3; int c : 3; };\nstruct S g1 = { (9) };\nstruct S g2 = { ((9)), (1 << 4) };\n"
                  "struct S g3 = { .b = (9), .c = 9 };\nstruct T { struct S s; } g4 = { { (9) } };\nstruct S g5[2] = { [0 ... 1] = { (9) } };\n"
                  "int f(void) { struct S l = (struct S){ (9), -(9) }; return l.b; }\nstruct S g6 = { -(9), (int)9 };\n",
        .warnings = 12,
    },
    {
        .name = "initializers keep their ordinary constant conversions",
        .source = "struct S { unsigned char c : 4; signed char d : 2; signed char e : 8; int i : 3; unsigned char f : 8; };\n"
                  "struct S g = { 300, 200, 200, 9, 256 };\nvoid f(void) {\n    struct S t = { 300, 200, 200, 9, 256 };\n    (void)t;\n}\n",
        .warnings = 14,
    },
    {
        .name = "unions, nested records, compound literals, unreachable and unevaluated initializers",
        .source = "struct S { int b : 3; unsigned u : 3; };\nunion W { int b : 3; unsigned u : 2; };\nstruct N { struct S s; int k : 2; };\n"
                  "union W gw = { 9 };\nstruct N gn = { { .u = 8 }, 4 };\nvoid f(void) {\n    (void)sizeof((struct S){ .b = 9 });\n"
                  "    (void)(struct S){ .b = 9, .u = 9 };\n    if (0) { struct S q = { 9 }; (void)q; }\n    union W w = { .u = 4 };\n    (void)w;\n"
                  "    return;\n    struct S r = { 10 };\n    (void)r;\n}\n",
        .warnings = 9,
    },
    {
        .name = "bit-field truncation's default groups",
        .source = check_driver_bitfield_groups_source,
        .warnings = 3,
    },
    {
        .name = "-Wno-conversion silences constant and bit-field truncation warnings",
        .source = check_driver_bitfield_groups_source,
        .options = { "-Wno-conversion" },
    },
    {
        .name = "-Wno-non-gcc silences constant and bit-field truncation warnings",
        .source = check_driver_bitfield_groups_source,
        .options = { "-Wno-non-gcc" },
    },
    {
        .name = "-Wno-bitfield-constant-conversion keeps the constant conversion",
        .source = check_driver_bitfield_groups_source,
        .options = { "-Wno-bitfield-constant-conversion" },
        .warnings = 1,
    },
    {
        .name = "-Wno-single-bit-bitfield-constant-conversion keeps the others",
        .source = check_driver_bitfield_groups_source,
        .options = { "-Wno-single-bit-bitfield-constant-conversion" },
        .warnings = 2,
    },
    {
        .name = "-Werror=bitfield-constant-conversion promotes both bit-field warnings",
        .source = check_driver_bitfield_groups_source,
        .options = { "-Werror=bitfield-constant-conversion" },
        .warnings = 1,
        .errors = 2,
    },
    {
        .name = "-Werror=conversion promotes all three",
        .source = check_driver_bitfield_groups_source,
        .options = { "-Werror=conversion" },
        .errors = 3,
    },
    {
        .name = "-Wconversion enables all three after -Wno-everything",
        .source = check_driver_bitfield_groups_source,
        .options = { "-Wno-everything", "-Wconversion" },
        .warnings = 3,
    },
    {
        .name = "-Wsingle-bit-bitfield-constant-conversion re-enables its own after -Wno-constant-conversion",
        .source = check_driver_bitfield_groups_source,
        .options = { "-Wno-constant-conversion", "-Wsingle-bit-bitfield-constant-conversion" },
        .warnings = 1,
    },
    {
        .name = "source policy reaches bit-field truncation",
        .source = "struct S { int a : 1; int b : 3; } s;\nvoid f(void) {\n#pragma clang diagnostic push\n"
                  "#pragma clang diagnostic ignored \"-Wbitfield-constant-conversion\"\n    s.a = 1;\n    s.b = 9;\n#pragma clang diagnostic pop\n"
                  "#pragma clang diagnostic error \"-Wsingle-bit-bitfield-constant-conversion\"\n    s.a = 1;\n    s.b = 9;\n}\n",
        .warnings = 1,
        .errors = 1,
    },
};

static void check_test_driver_bitfields(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_fixtures(test, clang, executable, directory, check_driver_bitfield_fixtures,
                                      BUSTER_ARRAY_LENGTH(check_driver_bitfield_fixtures));
}
