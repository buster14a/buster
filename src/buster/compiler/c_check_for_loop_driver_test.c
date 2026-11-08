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
