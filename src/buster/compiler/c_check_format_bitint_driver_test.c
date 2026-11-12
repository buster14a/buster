// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original _BitInt format-argument sources (#1392), and _Atomic ones (#2234), with independent default-warning counts. The shared harness
// compares complete Clang diagnostics, -Werror/output preservation and -w, both kernels and requested lane counts.

static const CheckDriverDefaultWarningFixture check_driver_format_bitint_fixtures[] =
{
    {
        .name = "narrow signed bit-precise data is not an int format argument",
        .source = "int printf(const char*,...);void f(_BitInt(8) x){printf(\"%d\",x);}\n",
        .warnings = 1,
        .c23 = true,
    },
    {
        .name = "narrow unsigned bit-precise data is not an unsigned int format argument",
        .source = "int printf(const char*,...);void f(unsigned _BitInt(24) x){printf(\"%u\",x);}\n",
        .warnings = 1,
        .c23 = true,
    },
    {
        .name = "one-bit unsigned data is not an int format argument",
        .source = "int printf(const char*,...);void f(unsigned _BitInt(1) x){printf(\"%d\",x);}\n",
        .warnings = 1,
        .c23 = true,
    },
    {
        .name = "equal-width bit-precise data is distinct from int",
        .source = "int printf(const char*,...);void f(_BitInt(32) x){printf(\"%d\",x);}\n",
        .warnings = 1,
        .c23 = true,
    },
    {
        .name = "character conversions require ordinary integer classes",
        .source = "int printf(const char*,...);void f(_BitInt(8) x,_BitInt(32) y){printf(\"%c %lc\",x,y);}\n",
        .warnings = 2,
        .c23 = true,
    },
    {
        .name = "count pointers require the ordinary pointed-to class",
        .source = "int printf(const char*,...);void f(_BitInt(8)*x,_BitInt(16)*y,_BitInt(32)*z){printf(\"%hhn %hn %n\",x,y,z);}\n",
        .warnings = 3,
        .c23 = true,
    },
    {
        .name = "field width does not promote a bit-precise integer to int",
        .source = "int printf(const char*,...);void f(_BitInt(8) x){printf(\"%*d\",x,1);}\n",
        .warnings = 1,
        .c23 = true,
    },
    {
        .name = "field precision does not promote a bit-precise integer to int",
        .source = "int printf(const char*,...);void f(unsigned _BitInt(24) x){printf(\"%.*s\",x,\"x\");}\n",
        .warnings = 1,
        .c23 = true,
    },
    {
        .name = "C23 exact-width modifiers still require their ordinary integer types",
        .source = "int printf(const char*,...);void f(_BitInt(8) x,_BitInt(32) y){printf(\"%w8d %w32d\",x,y);}\n",
        .warnings = 2,
        .c23 = true,
    },
    {
        .name = "explicit integer casts retain valid data width and precision",
        .source = "int printf(const char*,...);void f(_BitInt(8) x){printf(\"%d %c %*d %.*s\",(int)x,(int)x,(int)x,1,(int)x,\"x\");}\n",
        .c23 = true,
    },
    {
        .name = "ordinary arithmetic promotes the narrow bit-precise operand",
        .source = "int printf(const char*,...);void f(_BitInt(8) x){printf(\"%d\",x+0);}\n",
        .c23 = true,
    },
    {
        .name = "fixed int parameter converts a bit-precise source before format checking",
        .source = "void log_value(const char*,int) __attribute__((format(printf,1,2)));void f(_BitInt(8) x){log_value(\"%d\",x);}\n",
        .options = { "-Wno-gcc-compat" },
        .c23 = true,
    },
    {
        .name = "fixed long parameter conversion retains its valid format",
        .source = "void log_value(const char*,long) __attribute__((format(printf,1,2)));void f(_BitInt(8) x){log_value(\"%ld\",x);}\n",
        .options = { "-Wno-gcc-compat" },
        .c23 = true,
    },
    {
        .name = "fixed long parameter mismatch reports the converted type",
        .source = "void log_value(const char*,long) __attribute__((format(printf,1,2)));void f(_BitInt(8) x){log_value(\"%d\",x);}\n",
        .options = { "-Wno-gcc-compat" },
        .warnings = 1,
        .c23 = true,
    },
    {
        .name = "fixed bit-precise parameter retains its distinct type",
        .source = "void log_value(const char*,_BitInt(32)) __attribute__((format(printf,1,2)));void f(_BitInt(8) x){log_value(\"%d\",x);}\n",
        .options = { "-Wno-gcc-compat" },
        .warnings = 1,
        .c23 = true,
    },
    {
        .name = "ordinary narrow integer and count-pointer classes remain valid",
        .source = "int printf(const char*,...);void f(signed char x,short y,int z,signed char*a,short*b,int*c){printf(\"%hhd %hd %d %hhn %hn %n\",x,y,z,a,b,c);}\n",
        .c23 = true,
    },
    {
        .name = "wide bit-precise data remains distinct from equal-width long",
        .source = "int printf(const char*,...);void f(_BitInt(64) x){printf(\"%ld\",x);}\n",
        .warnings = 1,
        .c23 = true,
    },
    {
        .name = "ordinary boolean character warning is retained",
        .source = "int printf(const char*,...);void f(_Bool x){printf(\"%c\",x);}\n",
        .warnings = 1,
        .c23 = true,
    },
    {
        .name = "string conversions require character rather than bit-precise pointees",
        .source = "int printf(const char*,...);void f(_BitInt(8)*x,_BitInt(32)*y){printf(\"%s %ls\",x,y);}\n",
        .warnings = 2,
        .c23 = true,
    },
    {
        .name = "explicit string-pointer casts and generic pointer formats remain valid",
        .source = "int printf(const char*,...);void f(_BitInt(8)*x,_BitInt(32)*y){printf(\"%s %ls %p\",(char*)x,(int*)y,x);}\n",
        .c23 = true,
    },
    {
        .name = "fixed dynamic width and precision use their int parameter conversions",
        .source = "void log_value(const char*,int,int,const char*) __attribute__((format(printf,1,2)));void f(_BitInt(8) x){log_value(\"%*.*s\",x,x,\"x\");}\n",
        .options = { "-Wno-gcc-compat" },
        .c23 = true,
    },
    {
        .name = "fixed void pointer mismatch reports its converted count-pointer type",
        .source = "void log_value(const char*,void*) __attribute__((format(printf,1,2)));void f(_BitInt(32)*x){log_value(\"%n\",x);}\n",
        .options = { "-Wno-gcc-compat" },
        .warnings = 1,
        .c23 = true,
    },
    {
        .name = "fixed bit-precise data parameter converts an ordinary int before checking",
        .source = "void log_value(const char*,_BitInt(32)) __attribute__((format(printf,1,2)));void f(int x){log_value(\"%d\",x);}\n",
        .options = { "-Wno-gcc-compat" },
        .warnings = 1,
        .c23 = true,
    },
    {
        .name = "fixed bit-precise width parameter converts an ordinary int before checking",
        .source = "void log_value(const char*,_BitInt(32),int) __attribute__((format(printf,1,2)));void f(int x){log_value(\"%*d\",x,1);}\n",
        .options = { "-Wno-gcc-compat" },
        .warnings = 1,
        .c23 = true,
    },
    {
        .name = "fixed bit-precise pointer parameter converts void pointer before checking",
        .source = "void log_value(const char*,_BitInt(32)*) __attribute__((format(printf,1,2)));void f(void*x){log_value(\"%n\",x);}\n",
        .options = { "-Wno-gcc-compat" },
        .warnings = 1,
        .c23 = true,
    },
    // _Atomic arguments are their value types (#2234): clang's lvalue conversion drops _Atomic, so a matching one gives no
    // warning and a mismatched one names its value type.
    {
        .name = "atomic format arguments by their value types",
        .source = "int printf(const char*, ...);\n"
                  "static _Atomic int a = 5;\n"
                  "static _Atomic long l = 6;\n"
                  "static _Atomic double d = 6;\n"
                  "static _Atomic short s = 6;\n"
                  "static _Atomic char c = 6;\n"
                  "int main(void) { printf(\"%d %ld %f %hd %c\\n\", a, l, d, s, c); return 0; }\n",
        .warnings = 0,
    },
    {
        .name = "atomic format arguments that don't match, by their value types",
        .source = "int printf(const char*, ...);\n"
                  "typedef _Atomic long AL;\n"
                  "typedef _Atomic(int) AI;\n"
                  "static AL al = 1;\n"
                  "static AI ai = 2;\n"
                  "static _Atomic unsigned char uc = 3;\n"
                  "static _Atomic(int *) ap;\n"
                  "static _Atomic(float) af;\n"
                  "struct S { _Atomic int m; } s;\n"
                  "int main(void) { printf(\"%d %d %s %d %p %f %x %*d\\n\", al, ai, uc, s.m, ap, af, ai, ai, 1); printf(\"%s %c\\n\", ap, ai); return 0; }\n",
        .warnings = 3,
    },
};

static void check_test_driver_format_bitints(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_fixtures(test, clang, executable, directory, check_driver_format_bitint_fixtures, BUSTER_ARRAY_LENGTH(check_driver_format_bitint_fixtures));
}
