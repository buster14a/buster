// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Swift's typedef and asynchronous error attributes (#1447): swift_newtype, swift_wrapper and swift_async_error, by the
// driver against clang, on original fixtures. Each unit's whole output, by both paths, in GNU C17 and C23: swift_newtype's
// parser's errors and its unsupported argument's warning (check_input_swift_newtype_parse), its subjects and its count
// without parentheses; swift_async_error's conventions, counts and index (check_input_attribute_swift_async_error), with
// swift_async(none) before and after it, and its C23 spelling. (A first argument of no identifier, on which clang 23
// crashes, is refused by name and has no fixture.)

static void check_test_driver_swift_attributes(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char* const units[] = {
        "typedef int T1 __attribute__((swift_newtype(struct)));\n",
        "typedef int T2 __attribute__((swift_newtype(enum)));\n",
        "typedef int T3 __attribute__((swift_wrapper(struct)));\n",
        "typedef int T4 __attribute__((swift_newtype(union)));\n",
        "typedef int T5 __attribute__((swift_newtype));\n",
        "typedef int T6 __attribute__((swift_newtype()));\n",
        "typedef int T7 __attribute__((swift_newtype(1)));\n",
        "typedef int T8 __attribute__((swift_newtype(struct, enum)));\n",
        "int v __attribute__((swift_newtype(struct)));\n",
        "typedef int T9 __attribute__((swift_newtype(struct))) __attribute__((swift_newtype(enum)));\n",
        "[[clang::swift_newtype(struct)]] typedef int T10;\n",
        "typedef int T11 __attribute__((swift_newtype(struct enum)));\n",
        "typedef int T1 __attribute__((swift_newtype(\"x\")));\n",
        "typedef int T2 __attribute__((swift_newtype(foo)));\n",
        "typedef int T3 __attribute__((__swift_newtype__(union)));\n",
        "typedef int T4 __attribute__((swift_newtype(union, x)));\n",
        "typedef int T5 __attribute__((swift_newtype((x))));\n",
        "int v __attribute__((swift_newtype));\n",
        "int w __attribute__((swift_newtype(foo)));\n",
        "typedef int T7 __attribute__((swift_newtype(1+2)));\n",
        "typedef int T8 __attribute__((swift_wrapper(__foo__)));\n",
        "typedef int T8 __attribute__((swift_wrapper(int)));\n",
        "struct __attribute__((swift_newtype(struct))) S;\n",
        "struct __attribute__((swift_newtype(enum))) S { int a; };\n",
        "void f(void) __attribute__((swift_wrapper(enum)));\n",
        "typedef struct S { int a; } T __attribute__((swift_newtype(struct)));\n",
        "typedef int T __attribute__((swift_newtype(struct)));\n" "typedef int T __attribute__((swift_newtype(enum)));\n" "T t;\n",
        "void g(void) { typedef int L __attribute__((swift_newtype(enum))); L l = 0; (void)l; }\n",
        "typedef int T __attribute__((swift_newtype(struct), swift_newtype(x)));\n",
        "void f1(void) __attribute__((swift_async_error(none)));\n",
        "void f2(void) __attribute__((swift_async_error(nonnull_error)));\n",
        "void f3(void) __attribute__((swift_async_error(zero_argument, 1)));\n",
        "void f4(void) __attribute__((swift_async_error(nonzero_argument, 1)));\n",
        "void f5(void) __attribute__((swift_async_error(bogus)));\n",
        "void f6(void) __attribute__((swift_async_error(none, 1)));\n",
        "void f7(void) __attribute__((swift_async_error(zero_argument)));\n",
        "void f8(void) __attribute__((swift_async_error(zero_argument, -1)));\n",
        "void f9(void) __attribute__((swift_async_error(zero_argument, f9)));\n",
        "void f10(void) __attribute__((swift_async(none))) __attribute__((swift_async_error(zero_argument, 1)));\n",
        "void f11(void) __attribute__((swift_async_error(zero_argument, 1))) __attribute__((swift_async(none)));\n",
        "void f12(void) __attribute__((swift_async(none))) __attribute__((swift_async_error(none)));\n",
        "int v __attribute__((swift_async_error(none)));\n",
        "void f14(void) __attribute__((swift_async_error));\n",
        "void f15(void) __attribute__((swift_async_error(zero_argument, 1, 2)));\n",
        "void f16(void) __attribute__((swift_async_error(zero_argument, 1.0)));\n",
        "void f17(void) __attribute__((swift_async_error(zero_argument, 4294967296)));\n",
        "void (*fp)(void) __attribute__((swift_async_error(none)));\n",
        "void f18(void) __attribute__((swift_async(none))) __attribute__((swift_async_error(nonnull_error)));\n",
        "void f19(void) __attribute__((swift_async_error(__none__)));\n",
        "[[clang::swift_async_error(none)]] void f20(void);\n",
        "void f21(void) [[clang::swift_async_error(nonnull_error)]];\n",
        "[[clang::swift_async_error(bogus)]] void f22(void);\n",
        "void f23(void) __attribute__((swift_async_error(nonzero_argument, 2 + 3)));\n",
        "enum { K = 4 };\n" "void f24(void) __attribute__((swift_async_error(nonzero_argument, K)));\n",
        "void f25(void) __attribute__((swift_async_error(none), swift_async(none)));\n" "void f25(void) { }\n",
        "void f26(void) __attribute__((swift_async_error(nonzero_argument, 1), swift_async(swift_private, 1)));\n",
        "void f27(int x) __attribute__((swift_async_error(zero_argument, x)));\n",
        "void f28(void) __attribute__((swift_async_error(zero_argument, 1))) __attribute__((swift_async(bogus)));\n",
    };
    static const char* const standards[] = { "-std=gnu17", "-std=gnu23" };
    enum { LINES = 16384 };
    char* expected_lines = malloc(LINES);
    char* actual_lines = malloc(LINES);
    char source[PATH_MAX];
    snprintf(source, sizeof(source), "%s/swift-attributes.c", directory);
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
        snprintf(description, sizeof(description), "swift attributes %u: clang's lines read", unit);
        check_test_expect(test, made && lines, description);
        for (uint32_t scalar = 0; lines && scalar < 2; scalar += 1)
        {
            arguments[0] = (char*)executable;
            arguments[5] = scalar != 0 ? "--scalar" : NULL;
            char* actual = NULL;
            const int status = check_test_driver_spawn(arguments, &actual);
            const bool same = actual != NULL && check_test_diagnostic_lines(actual, actual_lines, LINES) && strcmp(expected_lines, actual_lines) == 0;
            snprintf(description, sizeof(description), "swift attributes %u (%s %s), %s: status %d (clang %d), lines\n%s(clang\n%s)", unit, standard, text,
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
