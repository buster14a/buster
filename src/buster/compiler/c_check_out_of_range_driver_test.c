// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original -Wtautological-constant-out-of-range-compare sources (#2218, c_check_common_warnings.c's
// check_common_out_of_range). Clang supplies the external diagnostic oracle. Include after c_check_warning_driver_test.c,
// whose fixture runner compares complete no-caret diagnostics through both kernels and lane requests, under the default,
// -Werror and -w policies, with -fsyntax-only and -c.

static const CheckDriverDefaultWarningFixture check_driver_out_of_range_fixtures[] =
{
    {
        // Constants out of an operand type's range on either side, its typedef as written, an enumerator's name, a _Bool's
        // and a known boolean's range, an unsigned comparison's wrapped gap, and those in range or at a limit, which say
        // nothing here.
        .name = "constants out of an operand's range",
        .source = "typedef signed char S;\n"
                  "enum E { A = 1000, B = 2 };\n"
                  "int f(signed char x) { return x < 1000; }\n"
                  "int h(unsigned char c, S s, _Bool b, unsigned u, short sh) {\n"
                  "  int r = 0;\n"
                  "  r += c == -1; r += c != 256; r += 255 >= c; r += c > 255; r += c >= 255;\n"
                  "  r += s == 200; r += s < A; r += b == 2; r += (c < u) == 2; r += 3 < (c != 1);\n"
                  "  r += u < -1; r += u == -1L; r += sh == 40000; r += s == 4000000000u; r += sh < 0x10000u;\n"
                  "  r += c < 0; r += c >= 0; r += s == B; r += c < 10;\n"
                  "  return r;\n"
                  "}\n"
                  "int m(signed char x) { if (0) return x < 1000; return sizeof(x < 1000); }\n"
                  "int n(signed char x) { switch (x) { case 1000 > 5: return x == 300; } return 0; }\n",
        .warnings = 12,
        .compare_warning_text = true,
    },
    {
        // A _BitInt constant past 128 bits (check_wide_text's digits), and file scope, where the comparison completes after
        // the initializer's error.
        .name = "constants out of range past 128 bits and at file scope",
        .source = "int g(signed char x) { return x < ((_BitInt(200))1 << 150); }\n"
                  "int k(long long v) { return v == 0x10000000000000000wb; }\n"
                  "signed char gc;\n"
                  "int gl = gc < 1000;\n",
        .warnings = 3,
        .errors = 1,
        .c23 = true,
        .syntax_only = true,
    },
    {
        // -Wsign-compare (-Wextra) gives nothing of an always true or false comparison, and both follow clang's walk of a
        // full expression: a comparison before its operands, an assignment's, a conditional's and a comparison's operands
        // left to right, any other node's right to left, a call's last argument first; GNU's ?: reads its condition once.
        .name = "constants out of range under -Wextra",
        .source = "int f(signed char s, unsigned u) { return (s == 4000000000u) + (u < -1) + (s < u); }\n"
                  "int g(unsigned char c, signed char s, unsigned u, int i, int h(int, int)) {\n"
                  "  int r = (c == -1) + (c != 256) + ((c < 300) == (c > -5));\n"
                  "  r += (c < 300) ?: 1; r += h(c < 300, c > -5); r = c > 300 ? c < 400 : c < 500;\n"
                  "  r += (i < u) + (s < u);\n"
                  "  return r;\n"
                  "}\n",
        .options = { "-Wextra" },
        .warnings = 15,
        .compare_warning_text = true,
    },
    {
        // Forty comparisons across the candidate pass's 16-node blocks (check_common_range_candidates): constants out of
        // and in a narrow operand's range on either side, both sides constant, neither, and a known boolean's.
        .name = "constants out of range across vector blocks",
        .source = "int f(unsigned char c, signed char s, int i, short h)\n"
                  "{\n"
                  "    int r = 0;\n"
                  "    r += c == 300; r += -1 < c; r += s > 2; r += i < 3; r += h == 200;\n"
                  "    r += c < i; r += 1 < 129; r += c != 'x'; r += s <= -70000; r += (c == 1) == 9;\n"
                  "    r += c == 300; r += -1 < c; r += s > 2; r += i < 3; r += h == 200;\n"
                  "    r += c < i; r += 1 < 129; r += c != 'x'; r += s <= -70000; r += (c == 1) == 9;\n"
                  "    r += c == 300; r += -1 < c; r += s > 2; r += i < 3; r += h == 200;\n"
                  "    r += c < i; r += 1 < 129; r += c != 'x'; r += s <= -70000; r += (c == 1) == 9;\n"
                  "    r += c == 300; r += -1 < c; r += s > 2; r += i < 3; r += h == 200;\n"
                  "    r += c < i; r += 1 < 129; r += c != 'x'; r += s <= -70000; r += (c == 1) == 9;\n"
                  "    return r;\n"
                  "}\n",
        .warnings = 16,
    },
    {
        // The vector test's type rule: an int or a long class operand against a constant its type covers is no candidate,
        // a narrower one's constant is (an int's against a long constant), and a boolean operator's always is.
        .name = "constants out of range of builtin types",
        .source = "int f(int i, unsigned long ul, long l, unsigned u, _Bool b)\n"
                  "{\n"
                  "    int r = 0;\n"
                  "    r += i == 4000000000; r += ul == -1; r += l < 5; r += u > 4294967296; r += (i < l) == 3;\n"
                  "    r += i < 5L; r += !i == 2; r += (i || l) > 2; r += b == 3; r += (unsigned char)i < 256;\n"
                  "    r += 2 < i; r += i == 1; r += l != 1L; r += u == 7u; r += -1 == ul; r += (long)i > 4294967296L;\n"
                  "    return r;\n"
                  "}\n",
        .warnings = 8,
        .notes = 2,
    },
};

static void check_test_driver_out_of_range(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_fixtures(test, clang, executable, directory, check_driver_out_of_range_fixtures,
                                      BUSTER_ARRAY_LENGTH(check_driver_out_of_range_fixtures));
}
