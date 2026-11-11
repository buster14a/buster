// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original -Wstring-plus-int sources (#2190, c_check_common_warnings.c's check_common_string_plus_int). Clang supplies the
// external diagnostic oracle. Include after c_check_warning_driver_test.c, whose fixture runner compares complete
// no-caret diagnostics through both kernels and lane requests, under the default, -Werror and -w policies, with
// -fsyntax-only and -c.

static const CheckDriverDefaultWarningFixture check_driver_string_plus_int_fixtures[] =
{
    {
        // A string literal and an operand of integer, boolean or enumeration type, in either order, at the `+`, the operand's
        // type as written (a typedef's, __size_t's sugar); not through parentheses, not a subtraction, not a pointer.
        .name = "integers added to string literals",
        .source = "typedef unsigned long size_t;\n"
                  "typedef int myint;\n"
                  "enum X { X1 };\n"
                  "const char* a(void) { return \"abc\" + 1; }\n"
                  "const char* b(int i) { return i + \"abc\"; }\n"
                  "const char* c(char ch, _Bool f) { (void)(\"ab\" + f); return \"abc\" + ch; }\n"
                  "const char* d(void) { return (\"abc\") + -1; }\n"
                  "const char* e(void) { return \"abc\" + sizeof(int) * 3; }\n"
                  "const char* g(size_t n, myint m, enum X x, _BitInt(7) w) { (void)(\"ab\" + m); (void)(\"ab\" + x); (void)(\"ab\" + w); return \"ab\" + n; }\n"
                  "const char* h(void) { return \"a\" \"b\" \"c\" + 2; }\n"
                  "const char* k(int i) { const char* r = \"abc\"; r += i; (void)r; return \"abc\" - i; }\n"
                  "const char* m(int* p) { return (const char*)p + 1; }\n"
                  "const char* n(int i) { return L\"abc\" + i ? \"x\" : \"y\"; }\n",
        .warnings = 11,
        .notes = 11,
    },
    {
        // At file scope, in a macro, and before an operand's own warning.
        .name = "integers added to string literals in other places",
        .source = "const char* g = \"abc\" + 1;\n"
                  "const char* h(int i) { return \"abc\" + (1 << 40) + i; }\n"
                  "#define P(x) (x + 1)\n"
                  "const char* k(void) { return P(\"abc\"); }\n"
                  "const char* m(void) { return 1 + \"x\" \"y\"; }\n"
                  "const char* n(int i) { return \"abc\" + (i); }\n",
        .warnings = 6,
        .notes = 7,
    },
};

static void check_test_driver_string_plus_int(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_fixtures(test, clang, executable, directory, check_driver_string_plus_int_fixtures,
                                      BUSTER_ARRAY_LENGTH(check_driver_string_plus_int_fixtures));
}
