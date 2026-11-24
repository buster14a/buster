// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original #1394 expression sources. The pinned Clang oracle checks complete no-caret diagnostics, including notes,
// evaluated contexts, both kernels, lane requests, -Werror and -w. Include after c_check_warning_driver_test.c.

static const CheckDriverDefaultWarningFixture check_driver_expression_warning_fixtures[] =
{
    {
        .name = "default and Wall warning families reported together in issue 1394",
        .source = "int __attribute__((warn_unused_result)) wu(void);\n"
                  "[[nodiscard]] int nd(void);\n"
                  "void nn(int*) __attribute__((nonnull(1)));\n"
                  "typedef __SIZE_TYPE__ size_t;\n"
                  "void *memcpy(void *, const void *, size_t);\n"
                  "char *strcpy(char *, const char *);\n"
                  "void *memset(void *, int, size_t);\n"
                  "int abs(int);\n"
                  "void w(void){wu();nd();nn(0);}\n"
                  "void buffers(void){char a[4],b[8];memcpy(a,\"123456789\",9);strcpy(b,\"123456789\");}\n"
                  "int literals(void){int i=1.5; char c=1000.0; return i+c;}\n"
                  "int shifts(int x,long y){return (x<<40)+(x>>32)+(y<<64)+(x<<-1);}\n"
                  "int self(int x){return (x<x)+(x!=x);}\n"
                  "int range(unsigned char c){return c>300;}\n"
                  "int pointer(int *p){return &p==0;}\n"
                  "int function(void){if(w)return 1;return 0;}\n"
                  "int equality(int x){if ((x==1))return 1;return 0;}\n"
                  "int logical(int x){return !x&1;}\n"
                  "int string(const char *s){return s==\"x\";}\n"
                  "int sw(int x){switch(x==1){default:return 0;}}\n"
                  "int arr(int a[10]){return sizeof(a);}\n"
                  "void mem(int *p){memset(p,0,sizeof(p));}\n"
                  "int absolute(double d){return abs(d);}\n"
                  "char *cast1(int x){return (char*)x;}\n"
                  "int cast2(void *p){return (int)p;}\n"
                  "int extras(int*a,char c){label:return a[c];}\n",
        .options = { "-Wall" },
        .c23 = true,
        .warnings = 27,
        .notes = 9,
        .compare_node_window = true,
    },
    {
        .name = "structural self comparisons",
        .source = "int fn(void);\n"
                  "_Bool bf(void);\n"
                  "void vf(void);\n"
                  "int pf(int);\n"
                  "int *ptrfn(void);\n"
                  "int weakfn(void) __attribute__((weak));\n"
                  "extern int array[3];\n"
                  "extern int weakarray[3] __attribute__((weak));\n"
                  "struct Pair { int x; int a[3]; int *p; };\n"
                  "#define SELF(x) ((x) == (x))\n"
                  "#define FUN fn\n"
                  "#define BOOL(x) (!!(x))\n"
                  "#define CMP(x, y) ((x) == (y))\n"
                  "int comparisons(int x, volatile int v, _Atomic int at, double d, int *p, int i, struct Pair *s, struct Pair t) {\n"
                  "    int r = x == x;\n"
                  "    r += x != x;\n"
                  "    r += x < x;\n"
                  "    r += x >= x;\n"
                  "    r += (x) <= (x);\n"
                  "    r += (int)x == (int)x;\n"
                  "    r += v == v;\n"
                  "    r += at == at;\n"
                  "    r += d == d;\n"
                  "    r += d != d;\n"
                  "    r += p == p;\n"
                  "    r += *p == *p;\n"
                  "    r += s->x == s->x;\n"
                  "    r += t.x == t.x;\n"
                  "    r += s->x == t.x;\n"
                  "    r += p[0] == p[0];\n"
                  "    r += p[i] == p[i];\n"
                  "    r += p[i+1] == p[i+1];\n"
                  "    r += array == array;\n"
                  "    r += array == weakarray;\n"
                  "    r += fn == fn;\n"
                  "    r += fn() == fn();\n"
                  "    r += SELF(x);\n"
                  "    r += CMP(x,x);\n"
                  "    if (0) r += x == x;\n"
                  "    r += 0 && (x == x);\n"
                  "    r += sizeof(x == x);\n"
                  "    int b[(x == x) + 2];\n"
                  "    (void)b;\n"
                  "    r += _Generic(x, int: x == x, default: x == x);\n"
                  "    r += __builtin_choose_expr(1, x == x, x == x);\n"
                  "    return r;\n"
                  "    r += x == x;\n"
                  "}\n",
        .warnings = 21,
        .notes = 0,
        .syntax_only = true,
    },
    {
        .name = "strong pointer conversions and comparisons",
        .source = "int fn(void);\n"
                  "_Bool bf(void);\n"
                  "void vf(void);\n"
                  "int pf(int);\n"
                  "int *ptrfn(void);\n"
                  "int weakfn(void) __attribute__((weak));\n"
                  "extern int array[3];\n"
                  "extern int weakarray[3] __attribute__((weak));\n"
                  "struct Pair { int x; int a[3]; int *p; };\n"
                  "#define SELF(x) ((x) == (x))\n"
                  "#define FUN fn\n"
                  "#define BOOL(x) (!!(x))\n"
                  "#define CMP(x, y) ((x) == (y))\n"
                  "int pointers(int x, int *p, struct Pair *s, struct Pair t) {\n"
                  "    int r = 0;\n"
                  "    if (fn) r++;\n"
                  "    if (bf) r++;\n"
                  "    if (vf) r++;\n"
                  "    if (pf) r++;\n"
                  "    if (weakfn) r++;\n"
                  "    if (&fn) r++;\n"
                  "    if ((fn)) r++;\n"
                  "    if (array) r++;\n"
                  "    if (weakarray) r++;\n"
                  "    if (&p) r++;\n"
                  "    if (&(p)) r++;\n"
                  "    if ((&p)) r++;\n"
                  "    if (&s->x) r++;\n"
                  "    if (s->a) r++;\n"
                  "    if (t.a) r++;\n"
                  "    r += !fn;\n"
                  "    r += !(&p);\n"
                  "    r += fn && bf;\n"
                  "    r += array ? x : 0;\n"
                  "    _Bool b = fn;\n"
                  "    b = fn;\n"
                  "    b = (_Bool)fn;\n"
                  "    b = (_Bool)&p;\n"
                  "    (void)b;\n"
                  "    r += fn == 0;\n"
                  "    r += 0 != fn;\n"
                  "    r += ptrfn == 0;\n"
                  "    r += fn == (void *)0;\n"
                  "    r += &fn == 0;\n"
                  "    r += (fn) == 0;\n"
                  "    r += &p == 0;\n"
                  "    r += 0 != &p;\n"
                  "    r += (&p) == 0;\n"
                  "    r += &(p) == 0;\n"
                  "    r += array == 0;\n"
                  "    r += weakarray == 0;\n"
                  "    r += &s->x == 0;\n"
                  "    r += s->a == 0;\n"
                  "    if (FUN) r++;\n"
                  "    r += BOOL(fn);\n"
                  "    r += CMP(fn, 0);\n"
                  "    r += sizeof(fn == 0);\n"
                  "    r += sizeof(!fn);\n"
                  "    if (0) r += fn == 0;\n"
                  "    if (0) { if (fn) r++; }\n"
                  "    return r;\n"
                  "    if (fn) r++;\n"
                  "}\n",
        .warnings = 32,
        .notes = 26,
        .syntax_only = true,
    },
    {
        .name = "string comparison contexts",
        .source = "int strings(const char *s, int x) {\n"
                  "    int r = s == \"x\";\n"
                  "    r += \"x\" != s;\n"
                  "    r += s < \"x\";\n"
                  "    r += (char *)\"x\" == s;\n"
                  "    r += (\"x\") == s;\n"
                  "    r += \"x\" == \"x\";\n"
                  "    r += \"x\" == \"y\";\n"
                  "    r += \"x\" == 0;\n"
                  "    r += \"x\" != (void *)0;\n"
                  "    r += \"x\" == (char *)0;\n"
                  "    r += \"x\" == (1 - 1);\n"
                  "    r += sizeof(s == \"x\");\n"
                  "    if (0) r += s == \"x\";\n"
                  "    r += 0 && (s == \"x\");\n"
                  "    r += _Generic(x, int: s == \"x\", default: s == \"x\");\n"
                  "    return r;\n"
                  "    r += s == \"x\";\n"
                  "}\n",
        .warnings = 12,
        .notes = 0,
    },
    {
        .name = "address family suppression",
        .source = "int f(void);\nint g(int x, const char *p) {\n  int r = x == x;\n  r += p == \"x\";\n  if (f) r++;\n  r += f == 0;\n  return r;\n}\n",
        .options = { "-Wno-address" },
        .warnings = 1,
        .notes = 0,
    },
    {
        .name = "tautological family suppression",
        .source = "int f(void);\nint g(int x, const char *p) {\n  int r = x == x;\n  r += p == \"x\";\n  if (f) r++;\n  r += f == 0;\n  return r;\n}\n",
        .options = { "-Wno-tautological-compare" },
        .warnings = 2,
        .notes = 1,
    },
    {
        .name = "bool conversion family suppression",
        .source = "int f(void);\nint g(int x, const char *p) {\n  int r = x == x;\n  r += p == \"x\";\n  if (f) r++;\n  r += f == 0;\n  return r;\n}\n",
        .options = { "-Wno-bool-conversion" },
        .warnings = 3,
        .notes = 2,
    },
    {
        .name = "address family enables descendants",
        .source = "int f(void);\nint g(int x, const char *p) {\n  int r = x == x;\n  r += p == \"x\";\n  if (f) r++;\n  r += f == 0;\n  return r;\n}\n",
        .options = { "-Wno-everything", "-Waddress" },
        .warnings = 3,
        .notes = 3,
    },
};

static void check_test_driver_expression_warnings(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_fixtures_dealt(test, clang, executable, directory, check_driver_expression_warning_fixtures,
                                      BUSTER_ARRAY_LENGTH(check_driver_expression_warning_fixtures), true);
}
