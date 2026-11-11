// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Swift's names and error conventions (#1447): swift_name, swift_async_name and swift_error, by the driver against clang,
// on original fixtures. Each unit's whole output, by both paths, in GNU C17 and C23: swift_name's and swift_async_name's
// -Wswift-name-attribute warnings (check_input_attribute_swift_name, check_input_swift_function_name) on functions,
// prototypes or not, with their parameters' counts, getters, setters, subscripts, contexts and inits, and on variables,
// tags, members, typedefs, enumerators and labels; swift_error's conventions, error parameters (the first struct bridged to
// NSError, check_input_swift_cf_error) and result types (check_input_attribute_swift_error). (A first argument of no
// identifier of swift_error, on which clang 23 crashes, is refused by name and has no fixture. Nor does a function of no
// prototype under swift_error run in C17: clang's error-parameter walk casts its type to a prototype's
// (getFunctionOrMethodNumParams) and reads a parameter count that type never set, so its answer is whatever memory held:
// none of the parameters as a fresh clang process, a crash in the clang server that reuses its memory. In C23, where ()
// is a prototype, it runs.)

static void check_test_driver_swift_names(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char* const units[] = {
        "void f1(int a) __attribute__((swift_name(\"foo(_:)\")));\n",
        "void f2(int a) __attribute__((swift_name(\"foo\")));\n",
        "void f3(int a) __attribute__((swift_name(\"foo()\")));\n",
        "void f4(int a, int b) __attribute__((swift_name(\"foo(a:)\")));\n",
        "void f5(void) __attribute__((swift_name(\"foo(a:)\")));\n",
        "void f6() __attribute__((swift_name(\"foo()\")));\n",
        "int v __attribute__((swift_name(\"Bar\")));\n",
        "int v2 __attribute__((swift_name(\"A.B\")));\n",
        "int v3 __attribute__((swift_name(\"1A\")));\n",
        "int v4 __attribute__((swift_name(\"A.1\")));\n",
        "struct __attribute__((swift_name(\"S2\"))) S { int m __attribute__((swift_name(\"mm\"))); };\n",
        "typedef int T __attribute__((swift_name(\"TT\")));\n",
        "enum E { A __attribute__((swift_name(\"AA\"))) };\n",
        "void f7(int a) __attribute__((swift_name(1)));\n",
        "void f8(int a) __attribute__((swift_name(\"getter:foo()\")));\n",
        "void f9(int a) __attribute__((swift_name(\"setter:foo()\")));\n",
        "void f10(int a) __attribute__((swift_name(\"X.subscript(_:)\")));\n",
        "void f11(int a) __attribute__((swift_name(\"_(_:)\")));\n",
        "void f12(int a) __attribute__((swift_name(\"foo(a)\")));\n",
        "void f13(int a) __attribute__((swift_name(\"foo(1:)\")));\n",
        "void f14(int *a) __attribute__((swift_name(\"foo()\")));\n",
        "void f15(int a) __attribute__((swift_name(\"init(a:b:)\")));\n",
        "void f16(void) __attribute__((swift_name(\"init(a:)\")));\n",
        "void f17(int a, int b) __attribute__((swift_name(\"X.foo(self:self:)\")));\n",
        "void f18(int a) __attribute__((swift_name(\"getter:X.foo(self:)\")));\n",
        "void f19(int a, int b) __attribute__((swift_name(\"getter:X.foo(self:a:)\")));\n",
        "void f20(int a) __attribute__((swift_async_name(\"foo()\")));\n",
        "void f21(void) __attribute__((swift_async_name(\"foo()\")));\n",
        "int v5 __attribute__((swift_async_name(\"foo\")));\n",
        "void (*fp)(int) __attribute__((swift_name(\"foo(_:)\")));\n",
        "void f22(int a) __attribute__((swift_name(\"foo(_:)\"))) __attribute__((swift_name(\"bar(_:)\")));\n",
        "void f23(int a) __attribute__((swift_name(\"\")));\n",
        "int v6 __attribute__((swift_name(\"\")));\n",
        "void f24(int a) __attribute__((swift_name(\".foo(_:)\")));\n",
        "void f25(int a) __attribute__((swift_name(\"X.subscript(self:)\")));\n",
        "void f26(int a, int b) __attribute__((swift_name(\"setter:X.subscript(self:_:)\")));\n",
        "void f27(int a, int b, int c) __attribute__((swift_name(\"setter:X.subscript(self:newValue:newValue:)\")));\n",
        "void f28(int a, int b) __attribute__((swift_name(\"getter:X.subscript(self:newValue:)\")));\n",
        "void f29(int a) __attribute__((swift_name(\"setter:X.subscript(self:)\")));\n",
        "void f30(int a) __attribute__((swift_name(\"setter:subscript()\")));\n",
        "void f31(int a) __attribute__((swift_name(\"getter:subscript()\")));\n",
        "void g(int a) { lbl: __attribute__((swift_name(\"x\"))); }\n",
        "void f(int a, const int *b) __attribute__((swift_name(\"foo(_:)\")));\n",
        "void f(int a, int *b, int *c) __attribute__((swift_name(\"foo(_:)\")));\n",
        "void f(int a) __attribute__((swift_name(\"A.foo(self:)\")));\n",
        "void f(int a) __attribute__((__swift_name__(\"foo(_:_:)\")));\n",
        "void f(int a) __attribute__((swift_name(\"foo\" \"(_:)\")));\n",
        "void f(int a, int b) __attribute__((swift_async_name(\"foo(_:)\")));\n",
        "void f(int a, int b) __attribute__((swift_async_name(\"foo(_:_:)\")));\n",
        "void f(int a) __attribute__((swift_name(\"getter:\")));\n",
        "int x __attribute__((swift_name(\"A.\")));\n",
        "int x __attribute__((swift_name(\".A\")));\n",
        "void f(int a) __attribute__((swift_name(\"foo(:)\")));\n",
        "void f(int a) __attribute__((swift_name)) ;\n",
        "void f(int a) __attribute__((swift_name(\"a\", \"b\")));\n",
        "void f(int a) __attribute__((swift_error(none), swift_name(\"foo(_:)\")));\n",
        "typedef struct __attribute__((objc_bridge(NSError))) __CFError *CFErrorRef;\n" "typedef struct __attribute__((objc_bridge_mutable(NSError))) __Other *OtherRef;\n" "typedef struct __attribute__((objc_bridge(NSFoo))) __Foo *FooRef;\n" "void f1(CFErrorRef *e) __attribute__((swift_error(nonnull_error)));\n",
        "typedef struct __attribute__((objc_bridge(NSError))) __CFError *CFErrorRef;\n" "typedef struct __attribute__((objc_bridge_mutable(NSError))) __Other *OtherRef;\n" "typedef struct __attribute__((objc_bridge(NSFoo))) __Foo *FooRef;\n" "void f2(int *e) __attribute__((swift_error(nonnull_error)));\n",
        "typedef struct __attribute__((objc_bridge(NSError))) __CFError *CFErrorRef;\n" "typedef struct __attribute__((objc_bridge_mutable(NSError))) __Other *OtherRef;\n" "typedef struct __attribute__((objc_bridge(NSFoo))) __Foo *FooRef;\n" "void *f3(CFErrorRef *e) __attribute__((swift_error(null_result)));\n",
        "typedef struct __attribute__((objc_bridge(NSError))) __CFError *CFErrorRef;\n" "typedef struct __attribute__((objc_bridge_mutable(NSError))) __Other *OtherRef;\n" "typedef struct __attribute__((objc_bridge(NSFoo))) __Foo *FooRef;\n" "int f4(CFErrorRef *e) __attribute__((swift_error(null_result)));\n",
        "typedef struct __attribute__((objc_bridge(NSError))) __CFError *CFErrorRef;\n" "typedef struct __attribute__((objc_bridge_mutable(NSError))) __Other *OtherRef;\n" "typedef struct __attribute__((objc_bridge(NSFoo))) __Foo *FooRef;\n" "int f5(CFErrorRef *e) __attribute__((swift_error(zero_result)));\n",
        "typedef struct __attribute__((objc_bridge(NSError))) __CFError *CFErrorRef;\n" "typedef struct __attribute__((objc_bridge_mutable(NSError))) __Other *OtherRef;\n" "typedef struct __attribute__((objc_bridge(NSFoo))) __Foo *FooRef;\n" "void *f6(CFErrorRef *e) __attribute__((swift_error(nonzero_result)));\n",
        "typedef struct __attribute__((objc_bridge(NSError))) __CFError *CFErrorRef;\n" "typedef struct __attribute__((objc_bridge_mutable(NSError))) __Other *OtherRef;\n" "typedef struct __attribute__((objc_bridge(NSFoo))) __Foo *FooRef;\n" "void f7(void) __attribute__((swift_error(none)));\n",
        "typedef struct __attribute__((objc_bridge(NSError))) __CFError *CFErrorRef;\n" "typedef struct __attribute__((objc_bridge_mutable(NSError))) __Other *OtherRef;\n" "typedef struct __attribute__((objc_bridge(NSFoo))) __Foo *FooRef;\n" "void f8(void) __attribute__((swift_error(bogus)));\n",
        "typedef struct __attribute__((objc_bridge(NSError))) __CFError *CFErrorRef;\n" "typedef struct __attribute__((objc_bridge_mutable(NSError))) __Other *OtherRef;\n" "typedef struct __attribute__((objc_bridge(NSFoo))) __Foo *FooRef;\n" "void f9(OtherRef *e) __attribute__((swift_error(nonnull_error)));\n",
        "typedef struct __attribute__((objc_bridge(NSError))) __CFError *CFErrorRef;\n" "typedef struct __attribute__((objc_bridge_mutable(NSError))) __Other *OtherRef;\n" "typedef struct __attribute__((objc_bridge(NSFoo))) __Foo *FooRef;\n" "void f10(FooRef *e) __attribute__((swift_error(nonnull_error)));\n",
        "typedef struct __attribute__((objc_bridge(NSError))) __CFError *CFErrorRef;\n" "typedef struct __attribute__((objc_bridge_mutable(NSError))) __Other *OtherRef;\n" "typedef struct __attribute__((objc_bridge(NSFoo))) __Foo *FooRef;\n" "int v __attribute__((swift_error(none)));\n",
        "typedef struct __attribute__((objc_bridge(NSError))) __CFError *CFErrorRef;\n" "typedef struct __attribute__((objc_bridge_mutable(NSError))) __Other *OtherRef;\n" "typedef struct __attribute__((objc_bridge(NSFoo))) __Foo *FooRef;\n" "void f11(CFErrorRef e) __attribute__((swift_error(nonnull_error)));\n",
        "typedef struct __attribute__((objc_bridge(NSError))) __CFError *CFErrorRef;\n" "typedef struct __attribute__((objc_bridge_mutable(NSError))) __Other *OtherRef;\n" "typedef struct __attribute__((objc_bridge(NSFoo))) __Foo *FooRef;\n" "_Bool f12(CFErrorRef *e) __attribute__((swift_error(zero_result)));\n",
        "typedef struct __attribute__((objc_bridge(NSError))) __CFError *CFErrorRef;\n" "typedef struct __attribute__((objc_bridge_mutable(NSError))) __Other *OtherRef;\n" "typedef struct __attribute__((objc_bridge(NSFoo))) __Foo *FooRef;\n" "enum E { A } f13(CFErrorRef *e) __attribute__((swift_error(zero_result)));\n",
        "typedef struct __attribute__((objc_bridge(NSError))) __CFError *CFErrorRef;\n" "typedef struct __attribute__((objc_bridge_mutable(NSError))) __Other *OtherRef;\n" "typedef struct __attribute__((objc_bridge(NSFoo))) __Foo *FooRef;\n" "void f14() __attribute__((swift_error(nonnull_error)));\n",
        "typedef struct __attribute__((objc_bridge(NSError))) __CFError *CFErrorRef;\n" "typedef struct __attribute__((objc_bridge_mutable(NSError))) __Other *OtherRef;\n" "typedef struct __attribute__((objc_bridge(NSFoo))) __Foo *FooRef;\n" "void f15(void) __attribute__((swift_error));\n",
        "typedef struct __attribute__((objc_bridge(NSError))) __CFError *CFErrorRef;\n" "typedef struct __attribute__((objc_bridge_mutable(NSError))) __Other *OtherRef;\n" "typedef struct __attribute__((objc_bridge(NSFoo))) __Foo *FooRef;\n" "void (*fp)(CFErrorRef *) __attribute__((swift_error(nonnull_error)));\n",
        "typedef struct __attribute__((objc_bridge(NSError))) __CFError *CFErrorRef;\n" "typedef struct __attribute__((objc_bridge_mutable(NSError))) __Other *OtherRef;\n" "typedef struct __attribute__((objc_bridge(NSFoo))) __Foo *FooRef;\n" "typedef struct __attribute__((objc_bridge(NSError))) __Two *TwoRef;\n" "void f(TwoRef *e) __attribute__((swift_error(nonnull_error)));\n",
        "typedef struct __attribute__((objc_bridge_mutable(NSError))) __Other *OtherRef;\n" "void f(OtherRef *e) __attribute__((swift_error(nonnull_error)));\n",
        "typedef struct __attribute__((objc_bridge_mutable(NSError))) __Other *OtherRef;\n" "void f(OtherRef *e) __attribute__((swift_error(nonnull_error)));\n" "typedef struct __attribute__((objc_bridge(NSError))) __CF *CFRef;\n" "void g(CFRef *e) __attribute__((swift_error(nonnull_error)));\n",
        "struct __attribute__((objc_bridge(NSError))) E;\n" "void f(struct E **e) __attribute__((swift_error(null_result)));\n",
        "struct __attribute__((objc_bridge(NSError))) E;\n" "void *f(struct E **e) __attribute__((swift_error(null_result)));\n",
        "struct __attribute__((objc_bridge(NSError))) E;\n" "void *f(struct E *const *e) __attribute__((swift_error(nonzero_result)));\n",
        "void f(void) __attribute__((swift_error(__none__)));\n",
        "void f(int a) __attribute__((swift_name(\"A.(_:)\")));\n",
        "void f(int a) __attribute__((swift_name(\"A..b(_:)\")));\n",
        "int x __attribute__((swift_name(\"A..b\")));\n",
        "void f(int a) __attribute__((swift_name(\"A.b.c(_:)\")));\n",
        "void f(int a) __attribute__((swift_name(\"A.subscript)\")));\n",
        "void f(int a) __attribute__((swift_name(\"1.b.c(_:)\")));\n",
        "void f(int a) __attribute__((swift_name(\"A.1.c(_:)\")));\n",
        "int x __attribute__((swift_name(\"A.b.c\")));\n",
        "int x __attribute__((swift_name(\"A.1.c\")));\n",
        "int x __attribute__((swift_name(\"A..c\")));\n",
    };
    static const char* const standards[] = { "-std=gnu17", "-std=gnu23" };
    enum { LINES = 16384 };
    char* expected_lines = malloc(LINES);
    char* actual_lines = malloc(LINES);
    char source[PATH_MAX];
    snprintf(source, sizeof(source), "%s/swift-names.c", directory);
    const uint32_t total = BUSTER_ARRAY_LENGTH(units) * BUSTER_ARRAY_LENGTH(standards);
    for (uint32_t unit = 0; expected_lines != NULL && actual_lines != NULL && unit < total; unit += 1)
    {
        const char* text = units[unit % BUSTER_ARRAY_LENGTH(units)];
        char* standard = (char*)standards[unit / BUSTER_ARRAY_LENGTH(units)];
        // swift_error on a function of no prototype, as C17 has f14 (see above): clang's answer is undefined.
        const bool defined = strcmp(standard, "-std=gnu17") != 0 || strstr(text, "f14()") == NULL;
        if (defined)
        {
            const bool made = parse_test_write(source, text, strlen(text));
            char* arguments[] = { (char*)clang, standard, "-fsyntax-only", "-fno-caret-diagnostics", source, NULL, NULL };
            char* expected = NULL;
            const int oracle = made ? check_test_driver_spawn(arguments, &expected) : -1;
            const bool lines = expected != NULL && check_test_diagnostic_lines(expected, expected_lines, LINES);
            char description[4096];
            snprintf(description, sizeof(description), "swift names %u: clang's lines read", unit);
            check_test_expect(test, made && lines, description);
            for (uint32_t scalar = 0; lines && scalar < 2; scalar += 1)
            {
                arguments[0] = (char*)executable;
                arguments[5] = scalar != 0 ? "--scalar" : NULL;
                char* actual = NULL;
                const int status = check_test_driver_spawn(arguments, &actual);
                const bool same = actual != NULL && check_test_diagnostic_lines(actual, actual_lines, LINES) && strcmp(expected_lines, actual_lines) == 0;
                snprintf(description, sizeof(description), "swift names %u (%s %s), %s: status %d (clang %d), lines\n%s(clang\n%s)", unit, standard, text,
                         scalar != 0 ? "scalar" : "vector", status, oracle, actual_lines, expected_lines);
                check_test_expect(test, same && (status != 0) == (oracle != 0), description);
                free(actual);
            }
            free(expected);
        }
    }
    free(expected_lines);
    free(actual_lines);
    unlink(source);
}
