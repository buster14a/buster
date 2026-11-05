// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #1450: aggregate register parts that exceed the red zone's fourteen eightbytes. Included by
// c_check_tier1_driver_test.c after its two-unit runner. Four and five repeated 32-byte vectors cross the old boundary;
// eight distinct vectors plus six integer aggregate parts exercise the full register staging area; the ninth vector
// adds outgoing stack storage. A dynamic caller checks the stable frame base and an indirect caller the callee's move.
// Both target policies, both host paths and both rematerialization choices must select every function under --verify.
// The dynamic caller alone remains outside the emitted-code certificate's scope.

static const char check_test_tier1_staging_header[] =
    "#include <stdio.h>\n"
    "typedef int V8 __attribute__((vector_size(32)));\n"
    "typedef struct Word { unsigned long value; } Word;\n"
    "typedef unsigned long (*Five)(V8, V8, V8, V8, V8);\n"
    "unsigned long b_four(V8 a, V8 b, V8 c, V8 d);\n"
    "unsigned long b_five(V8 a, V8 b, V8 c, V8 d, V8 e);\n"
    "unsigned long b_eight(V8 a, V8 b, V8 c, V8 d, V8 e, V8 f, V8 g, V8 h);\n"
    "unsigned long b_nine(V8 a, V8 b, V8 c, V8 d, V8 e, V8 f, V8 g, V8 h, V8 i);\n"
    "unsigned long b_mixed(Word u, V8 a, Word v, V8 b, Word w, V8 c, Word x, V8 d, Word y, V8 e, Word z, V8 f, V8 g, V8 h);\n"
    "unsigned long a_four(V8* p, int k);\n"
    "unsigned long a_five(V8* p, int k);\n"
    "unsigned long a_eight(V8* p, int k);\n"
    "unsigned long a_nine(V8* p, int k);\n"
    "unsigned long a_mixed(V8* p, int k);\n"
    "unsigned long a_dynamic(V8* p, int n);\n"
    "unsigned long a_indirect(V8* p, Five take);\n";

static const char check_test_tier1_staging_source[] =
    "#include \"tier1-staging.h\"\n"
    "unsigned long a_four(V8* p, int k) { V8 x = p[k]; return b_four(x, x, x, x); }\n"
    "unsigned long a_five(V8* p, int k) { V8 x = p[k]; return b_five(x, x, x, x, x); }\n"
    "unsigned long a_eight(V8* p, int k) { return b_eight(p[0], p[1] + p[k], p[2], p[3] - p[k], p[4], p[5], p[6], p[7]); }\n"
    "unsigned long a_nine(V8* p, int k) { return b_nine(p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8] + p[k]); }\n"
    "unsigned long a_mixed(V8* p, int k)\n"
    "{\n"
    "    Word u = { (unsigned)k + 1 }, v = { (unsigned)k + 2 }, w = { (unsigned)k + 3 };\n"
    "    Word x = { (unsigned)k + 4 }, y = { (unsigned)k + 5 }, z = { (unsigned)k + 6 };\n"
    "    return b_mixed(u, p[0], v, p[1], w, p[2], x, p[3], y, p[4], z, p[5], p[6], p[7]);\n"
    "}\n"
    "unsigned long a_dynamic(V8* p, int n)\n"
    "{\n"
    "    volatile unsigned char* q = __builtin_alloca(n);\n"
    "    q[0] = (unsigned char)n;\n"
    "    return b_nine(p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8]) + q[0];\n"
    "}\n"
    "unsigned long a_indirect(V8* p, Five take) { return take(p[4], p[3], p[2], p[1], p[0]); }\n"
    "int main(void)\n"
    "{\n"
    "    V8 p[9];\n"
    "    for (int i = 0; i < 9; i += 1) for (int j = 0; j < 8; j += 1) p[i][j] = i * 100 + j * 7 - 50;\n"
    "    for (int k = 0; k < 3; k += 1)\n"
    "    {\n"
    "        printf(\"%lu %lu %lu %lu %lu %lu %lu\\n\", a_four(p, k), a_five(p, k), a_eight(p, k), a_nine(p, k), a_mixed(p, k), a_dynamic(p, 17 + k * 13), a_indirect(p, b_five));\n"
    "    }\n"
    "    return 0;\n"
    "}\n";

static const char check_test_tier1_staging_other_source[] =
    "#include \"tier1-staging.h\"\n"
    "static unsigned long fold(unsigned long h, const int* v, int n) { for (int i = 0; i < n; i += 1) h = h * 31 + (unsigned)v[i]; return h; }\n"
    "unsigned long b_four(V8 a, V8 b, V8 c, V8 d) { V8 v[4] = { a, b, c, d }; return fold(1, (int*)v, 32); }\n"
    "unsigned long b_five(V8 a, V8 b, V8 c, V8 d, V8 e) { V8 v[5] = { a, b, c, d, e }; return fold(2, (int*)v, 40); }\n"
    "unsigned long b_eight(V8 a, V8 b, V8 c, V8 d, V8 e, V8 f, V8 g, V8 h) { V8 v[8] = { a, b, c, d, e, f, g, h }; return fold(3, (int*)v, 64); }\n"
    "unsigned long b_nine(V8 a, V8 b, V8 c, V8 d, V8 e, V8 f, V8 g, V8 h, V8 i) { V8 v[9] = { a, b, c, d, e, f, g, h, i }; return fold(5, (int*)v, 72); }\n"
    "unsigned long b_mixed(Word u, V8 a, Word v, V8 b, Word w, V8 c, Word x, V8 d, Word y, V8 e, Word z, V8 f, V8 g, V8 h)\n"
    "{\n"
    "    V8 p[8] = { a, b, c, d, e, f, g, h };\n"
    "    return fold(u.value + 3 * v.value + 5 * w.value + 7 * x.value + 11 * y.value + 13 * z.value, (int*)p, 64);\n"
    "}\n";

static void check_test_driver_tier1_staging(CheckTest* test, const char* clang, const char* executable, const char* directory, const char* option, bool host)
{
    check_test_driver_tier1_units(test, clang, executable, directory, "tier1-staging", "overflow vector call staging", check_test_tier1_staging_header,
                                  check_test_tier1_staging_source, check_test_tier1_staging_other_source, option, host, 1);
}
