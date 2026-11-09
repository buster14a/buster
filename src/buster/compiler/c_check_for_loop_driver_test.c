// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original sources for clang's -Wfor-loop-analysis (#1447, CheckForLoopConditionalStatement and
// CheckForRedundantIteration): a for loop's condition variables its loop never changes, and its variable both its
// header and its body's last statement step, under -Wloop-analysis, -Wmost and -Wall. The shared warning harness compares
// the pinned Clang's complete diagnostics through both kernels and requested lanes.

static const char check_driver_for_loop_forms_source[] =
    "struct S { int x; } gs; int g(int); extern int ext; enum { K = 3 };\n"
                  "typedef int T; volatile int vg;\n"
                  "void f(int n, int *p, int a[4], struct S s, struct S *ps) {\n"
                  "    int i = 0, j = 0, k = 0, m = 0, q = 0; static int st; volatile int vo = 0; int arr[4] = {0};\n"
                  "    for (; i < n; ) { i; }\n"
                  "    for (; i < n; ) { (void)i; }\n"
                  "    for (; i < n; ) { g(i); }\n"
                  "    for (; i < n; ) { int *pi = &i; (void)pi; }\n"
                  "    for (; i < n; ) { break; }\n"
                  "    for (; i < n; ) { for (;;) { break; } }\n"
                  "    for (; i < n; ) { if (g(0)) return; }\n"
                  "    for (; i < n; ) { continue; }\n"
                  "    for (; i < n && j < k && m > q; ) { }\n"
                  "    for (; i + j + k + m + q; ) { }\n"
                  "    for (; i < st; ) { }\n"
                  "    for (; i < vo; ) { }\n"
                  "    for (; i < ext; ) { }\n"
                  "    for (; i < K; ) { }\n"
                  "    for (; i < (T)n; ) { }\n"
                  "    for (; i < sizeof(n); ) { }\n"
                  "    for (; p; ) { p[0] = 1; }\n"
                  "    for (; p; ) { *p = 1; }\n"
                  "    for (; i < s.x; ) { }\n"
                  "    for (; ps; ) { ps->x = 1; }\n"
                  "    for (; i < n; ) { s.x = i; }\n"
                  "    for (; -i < ~n; ) { }\n"
                  "    for (; i ? j : k; ) { }\n"
                  "    for (; i < n; i++) { }\n"
                  "    for (; i < n; j++) { }\n"
                  "    for (; i < n; j++) { i++; }\n"
                  "    for (; i < n; i++) { i++; }\n"
                  "    for (; i < n; ++i) { --i; }\n"
                  "    for (; i < n; i--) { j++; i--; }\n"
                  "    for (; i < n; i++) { if (j) continue; i++; }\n"
                  "    for (; i < n; i++) { while (j) continue; i++; }\n"
                  "    for (; i < n; i++) { switch (j) { case 1: continue; } i++; }\n"
                  "    for (; i < n; i++) i++;\n"
                  "    for (; i < n; (i++)) { i++; }\n"
                  "    for (; i < n; i++) { (i++); }\n"
                  "    for (; i < n; i++) { i += 1; }\n"
                  "    for (; a; ) { }\n"
                  "    for (; i < n; ) { goto out; }\n"
                  "    for (; i < n; ) { ({ 0; }); }\n"
                  "    for (; i < 1.5; ) { }\n"
                  "    for (; i < 'c'; ) { }\n"
                  "    for (; (i) < (n); ) { }\n"
                  "    out: ;\n"
                  "}\n";

static const char check_driver_for_loop_order_source[] =
    "#define LOOP(v, n) for (int v = 0; v < (n); ) { }\n"
                  "#define STEP(v) v++\n"
                  "int g(int);\n"
                  "int f(int n, unsigned u) {\n"
                  "    int r = 0;\n"
                  "    for (int i = 0; i < n; ) {\n"
                  "        int unused;\n"
                  "        for (int j = 0; j < n; j++) { r += j < u; j++; }\n"
                  "        r == 1;\n"
                  "    }\n"
                  "    for (int i = 0; i < n; STEP(i)) { STEP(i); }\n"
                  "    for (int i = 0; i < n; i++) { r += g(i); i++; }\n"
                  "    for (int i = 0; i < n; ) {\n"
                  "#pragma clang diagnostic push\n"
                  "#pragma clang diagnostic ignored \"-Wfor-loop-analysis\"\n"
                  "        r++;\n"
                  "#pragma clang diagnostic pop\n"
                  "    }\n"
                  "    for (int i = 0; i < n; ) { for (int j = 0; j < i; ) { } }\n"
                  "    return r;\n"
                  "}\n";

// #1821: loops whose variables only statement expressions in unevaluated operands write.
static const char check_driver_for_loop_unevaluated_source[] =
    "void f(int i, int n) {\n"
    "    for (; i < n;) { (void)__builtin_constant_p(({ i = 8; 1; })); }\n"
    "    for (; i < n + __builtin_constant_p(({ i = 6; 1; })); ) { }\n"
    "    for (; i < n;) { (void)sizeof(({ i = 9; 1; })); }\n"
    "    for (; i < n;) { (void)_Alignof(({ i = 1; 1; })); }\n"
    "    for (; i < n;) { typeof(({ i = 1; 1; })) t = 0; (void)t; }\n"
    "    for (; i < n;) { (void)({ i = 1; 1; }); }\n"
    "    for (; i < n;) { (void)__builtin_constant_p(({ ({ i = 2; 1; }); })); }\n"
    "    for (; i < n;) { (void)sizeof(({ (void)({ i = 2; 1; }); 1; })); }\n"
    "    for (; i < n;) { (void)_Generic(1, int: ({ i = 4; 1; }), long: 2); }\n"
    "    for (; i < n;) { (void)_Generic(1, long: ({ i = 4; 1; }), int: 2); }\n"
    "    for (; i < n;) { (void)__builtin_choose_expr(1, ({ i = 4; 1; }), 2); }\n"
    "    for (; i < n;) { (void)__builtin_choose_expr(0, ({ i = 4; 1; }), 2); }\n"
    "    for (; i < n;) { (void)__builtin_object_size(({ i = 5; &i; }), 0); }\n"
    "    for (; i < n;) { (void)_Generic(1, long: ({ (void)({ i = 4; 1; }); 1; }), int: 2); }\n"
    "    for (; i < n;) { (void)_Generic(1, int: ({ (void)({ i = 4; 1; }); 1; }), long: 2); }\n"
    "    for (; i < n;) { (void)_Generic(1, int: ({ (void)sizeof(({ i = 4; 1; })); 1; }), long: 2); }\n"
    "}\n"
    "void g(int i, int n) {\n"
    "    for (; i < n;) { (void)sizeof(({ return; 1; })); }\n"
    "    for (; i < n;) { (void)sizeof(({ break; 1; })); }\n"
    "    for (; i < n;) { (void)({ break; 1; }); }\n"
    "    for (; i < n;) { (void)_Generic(1, long: ({ goto out; 1; }), int: 2); }\n"
    "    for (; i < n + sizeof(({ i = 2; 1; }));) { }\n"
    "    for (; i < n; (void)sizeof(({ i = 2; 1; }))) { }\n"
    "    for (; i < n; (void)__builtin_constant_p(({ (void)({ i++; 1; }); 1; }))) { }\n"
    "    for (; i < n;) { (void)sizeof(int[({ i = 1; n; })]); }\n"
    "    for (; i < n;) { (void)sizeof(int[i++]); }\n"
    "    for (; i < n;) { (void)sizeof(int (*)[i++]); }\n"
    "    for (; i < n;) { int a[n]; (void)sizeof(a[({ i = 3; 1; })]); }\n"
    "    for (; i < n;) { int a[({ i = 3; n; })]; (void)a; }\n"
    "    for (; i < n;) { int a = ({ i = 3; 8; }); (void)a; }\n"
    "    (void)sizeof(({ for (; i < n;) { i++; } 1; }));\n"
    "out:;\n"
    "}\n";

// Review on #1896: static selections nested in statement expressions nested in static selections, each arm chosen or
// not, with writes and jumps; and __builtin_choose_expr's.
static const char check_driver_for_loop_nested_selections_source[] =
    "void f1(int i, int n) {\n"
    "    for (; i < n;) {\n"
    "        (void)({ (void)_Generic(1, long: ({ i++; 1; }), int: 2); 1; });\n"
    "    }\n"
    "}\n"
    "void f2(int i, int n) {\n"
    "    for (; i < n;) {\n"
    "        (void)({ (void)_Generic(1, int: ({ i++; 1; }), long: 2); 1; });\n"
    "    }\n"
    "}\n"
    "void f3(int i, int n) {\n"
    "    for (; i < n;) {\n"
    "        (void)({ (void)_Generic(1, long: ({ break; 1; }), int: 2); 1; });\n"
    "    }\n"
    "}\n"
    "void f4(int i, int n) {\n"
    "    for (; i < n;) {\n"
    "        (void)({ (void)_Generic(1, int: ({ break; 1; }), long: 2); 1; });\n"
    "    }\n"
    "}\n"
    "void f5(int i, int n) {\n"
    "    for (; i < n;) {\n"
    "        (void)_Generic(1, int: ({ (void)_Generic(1, long: ({ i++; 1; }), int: 2); 1; }), long: 2);\n"
    "    }\n"
    "}\n"
    "void f6(int i, int n) {\n"
    "    for (; i < n;) {\n"
    "        (void)_Generic(1, int: ({ (void)__builtin_choose_expr(1, ({ i++; 1; }), 2); 1; }), long: 2);\n"
    "    }\n"
    "}\n"
    "void f7(int i, int n) {\n"
    "    for (; i < n;) {\n"
    "        (void)({ (void)__builtin_choose_expr(0, ({ i++; 1; }), 2); 1; });\n"
    "    }\n"
    "}\n"
    "void f8(int i, int n) {\n"
    "    for (; i < n;) {\n"
    "        (void)({ (void)sizeof(_Generic(1, int: ({ i++; 1; }), long: 2)); 1; });\n"
    "    }\n"
    "}\n"
    "void f9(int i, int n) {\n"
    "    for (; i < n;) {\n"
    "        (void)({ (void)_Generic(1, long: ({ goto out; 1; }), int: 2); 1; });\n"
    "    }\n"
    "out:;\n"
    "}\n"
    "void f11(int i, int n) {\n"
    "    for (; i < n;) {\n"
    "        (void)_Generic(1, long: 0, int: ({ (void)_Generic(1, int: ({ (void)({ i++; 1; }); 1; }), long: 2); 1; }));\n"
    "    }\n"
    "}\n"
    "void f12(int i, int n) {\n"
    "    for (; i < n;) {\n"
    "        (void)_Generic(1, long: 0, int: ({ (void)_Generic(1, long: ({ (void)({ return; 1; }); 1; }), int: 2); 1; }));\n"
    "    }\n"
    "}\n";

static const CheckDriverDefaultWarningFixture check_driver_for_loop_fixtures[] =
{
    {
        .name = "condition variables a for loop never changes, and steps its header and its body repeat",
        .source = check_driver_for_loop_forms_source,
        .options = { "-Wfor-loop-analysis" },
        .warnings = 25,
        .notes = 3,
    },
    {
        .name = "-Wloop-analysis holds -Wfor-loop-analysis",
        .source = check_driver_for_loop_forms_source,
        .options = { "-Wloop-analysis" },
        .warnings = 25,
        .notes = 3,
    },
    {
        .name = "-Wfor-loop-analysis is off by default",
        .source = check_driver_for_loop_forms_source,
        .warnings = 2,
    },
    {
        .name = "-Wall's for loop warnings follow their bodies' own, nested loops' first",
        .source = check_driver_for_loop_order_source,
        .options = { "-Wall" },
        .warnings = 9,
        .notes = 4,
    },
    {
        // Review on #1811: statement expressions, static selections and the builtins clang leaves unvisited in a condition.
        .name = "conditions with statement expressions, selections and unvisited builtins, and selections in the body",
        .source = "struct S { int a, b; };\n"
                  "void f(int i, int n, int j, int *p) {\n"
                  "    for (; ({ i < n; 1; }); ) { }\n"
                  "    for (; (int)({ i < n; 0; }); ) { }\n"
                  "    for (; _Generic(i, int: j < n, default: 0); ) { }\n"
                  "    for (; _Generic(0, int: i < n, default: 0); ) { }\n"
                  "    for (; i < __builtin_offsetof(struct S, b); ) { }\n"
                  "    for (; i < n + __builtin_constant_p(j); ) { }\n"
                  "    for (; i < __builtin_object_size(p, 0); ) { }\n"
                  "    for (; i < __builtin_classify_type(j); ) { }\n"
                  "    for (; __builtin_choose_expr(1, i < n, j); ) { }\n"
                  "    for (; i < n; ) { (void)__builtin_choose_expr(0, i++, 0); }\n"
                  "    for (; i < n; ) { (void)_Generic(0, int: 0, default: i++); }\n"
                  "    for (; i < n; ) { (void)_Generic(0, int: i++, default: 0); }\n"
                  "}\n",
        .options = { "-Wfor-loop-analysis" },
        .warnings = 11,
    },
    {
        // Review on #1811: the condition's and the increment's first tokens' pragmas, and many variables and nodes.
        .name = "pragmas at the condition and the increment, and long conditions",
        .source = "void f(int i, int n, int a, int b, int c, int d, int e, int g, int h, int k, int l, int m, int o, int p, int q, int r, int s, int t) {\n"
                  "#pragma clang diagnostic push\n"
                  "#pragma clang diagnostic ignored \"-Wfor-loop-analysis\"\n"
                  "    for (; i < n;\n"
                  "#pragma clang diagnostic pop\n"
                  "         i++) { i++; }\n"
                  "#pragma clang diagnostic push\n"
                  "#pragma clang diagnostic ignored \"-Wfor-loop-analysis\"\n"
                  "    for (; \n"
                  "#pragma clang diagnostic pop\n"
                  "        i < n; ) { }\n"
                  "    for (; a + b + c + d + e + g + h + k + l + m + o + p + q + r + s + t + i + n < 0; ) { }\n"
                  "    for (; i + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 < n; ) { }\n"
                  "}\n",
        .options = { "-Wfor-loop-analysis" },
        .warnings = 4,
        .notes = 1,
    },
    {
        // Second review on #1811: the unevaluated-argument builtins' operands in the body and the increment, and loops nested
        // in a statement expression that leads a condition.
        .name = "unvisited builtins' operands after the condition, and loops in a condition's statement expression",
        .source = "void f1(int n) { int i = 0; for (; i < n; ) { (void)__builtin_constant_p(i = 6); } }\n"
                  "void f2(int n) { int i = 0; for (; i < n; ) { (void)__builtin_object_size(&i, 0); } }\n"
                  "void f5(int n) { int i = 0; for (; i < n; ) { (void)__builtin_dynamic_object_size(&i, 0); } }\n"
                  "void f6(int n) { int i = 0; for (; i < n; ) { (void)__builtin_classify_type(i = 6); } }\n"
                  "void f7(int n) { int i = 0; for (; i < n; __builtin_constant_p(i = 6)) { } }\n"
                  "void f8(int n) { int i = 0; for (; i < n; ) { (void)__builtin_constant_p(0 ? 0 : (i = 6)); } }\n"
                  "void g(int n) {\n"
                  "    int i = 0;\n"
                  "    for (; ({ for (int x = 0; x < 2; ) { } 1; }); ) { }\n"
                  "    for (; ({ do { } while (i < 2); 1; }); ) { }\n"
                  "    for (; ({ int y = 0; for (; y < n; ) { } 1; }) && i < n; ) { }\n"
                  "    for (; i < ({ for (int z = 0; z < 2; ) { } 1; }); ) { }\n"
                  "    for (; (struct { int a; }){ i }.a < n; ) { }\n"
                  "}\n",
        .options = { "-Wfor-loop-analysis" },
        .warnings = 10,
    },
    {
        // #1821: a write in a GNU statement expression clang's visitor never reaches changes no loop variable: one in an
        // unevaluated operand (sizeof, _Alignof, typeof, an unevaluated-argument builtin's call, a sizeof's type name's
        // bounds), nested or not, or in a static selection's other arm, as a return, a break or a goto there is none.
        // One in a selected arm, in a bound or in an initializer is.
        .name = "statement expressions in unevaluated operands",
        .source = check_driver_for_loop_unevaluated_source,
        .options = { "-Wfor-loop-analysis", "-Wno-unevaluated-expression" },
        .warnings = 23,
    },
    {
        .name = "statement expressions in unevaluated operands in C23",
        .source = check_driver_for_loop_unevaluated_source,
        .options = { "-Wfor-loop-analysis", "-Wno-unevaluated-expression" },
        .warnings = 23,
        .c23 = true,
    },
    {
        .name = "statement expressions in unevaluated operands under -Wall",
        .source = check_driver_for_loop_unevaluated_source,
        .options = { "-Wall", "-Wno-unevaluated-expression" },
        .warnings = 23,
    },
    {
        .name = "statement expressions in unevaluated operands under -Wall -Wextra",
        .source = check_driver_for_loop_unevaluated_source,
        .options = { "-Wall", "-Wextra", "-Wno-unevaluated-expression" },
        .warnings = 24,
    },
    {
        .name = "nested selections in statement expressions",
        .source = check_driver_for_loop_nested_selections_source,
        .options = { "-Wfor-loop-analysis", "-Wno-unevaluated-expression" },
        .warnings = 7,
    },
    {
        .name = "nested selections in statement expressions in C23",
        .source = check_driver_for_loop_nested_selections_source,
        .options = { "-Wfor-loop-analysis", "-Wno-unevaluated-expression" },
        .warnings = 7,
        .c23 = true,
    },
    {
        .name = "nested selections in statement expressions under -Wall -Wextra",
        .source = check_driver_for_loop_nested_selections_source,
        .options = { "-Wall", "-Wextra", "-Wno-unevaluated-expression" },
        .warnings = 7,
    },
    {
        .name = "-Werror=for-loop-analysis and a pragma",
        .source = check_driver_for_loop_order_source,
        .options = { "-Werror=for-loop-analysis", "-Wall" },
        .warnings = 2,
        .errors = 7,
        .notes = 4,
    },
};

static void check_test_driver_for_loops(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_fixtures(test, clang, executable, directory, check_driver_for_loop_fixtures,
                                      BUSTER_ARRAY_LENGTH(check_driver_for_loop_fixtures));
}
