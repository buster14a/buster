// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original sources for clang's casts from a pointer to a narrower integer type (#1943, CheckCStyleCast,
// c_check_warning_graph.c's check_warning_graph_pointer_cast): -Wpointer-to-int-cast, -Wvoid-pointer-to-int-cast,
// -Wpointer-to-enum-cast and -Wvoid-pointer-to-enum-cast, on by default. The shared warning harness compares the pinned
// Clang's complete diagnostics through both kernels and requested lanes. Include after c_check_warning_driver_test.c
// (check_test_driver_warning_fixtures).

// Every source a pointer has (an object, an array and a function that decay, a string literal, an address, a pointer to
// void through a typedef), every integer target (enumerations, typedefs, _BitInt, and _Bool, long and __int128, which
// don't warn), in unevaluated operands, a macro's body, a file-scope sizeof and a _Static_assert, and before a shift's
// own warning.
static const char check_driver_pointer_cast_source[] =
    "#define TOI(p) ((int)(p))\n"
    "typedef void *VP; typedef unsigned U32; typedef enum E { E0 } EE;\n"
    "void *q;\n"
    "int z = sizeof((char)&q);\n"
    "_Static_assert(sizeof((short)&q) == 2, \"\");\n"
    "void f(int *p, VP v, int a[2], const char *s, _BitInt(32) *bp, int n) {\n"
    "    int x = (int)p << 40;\n"
    "    int y = TOI(v);\n"
    "    U32 u = (U32)v + (U32)a + (U32)\"lit\" + (unsigned char)s + (U32)&x;\n"
    "    EE e = (EE)v; enum E e2 = (enum E)p;\n"
    "    _Bool b = (_Bool)p; long l = (long)p; unsigned long long m = (unsigned long long)p;\n"
    "    _BitInt(32) w = (_BitInt(32))p; __int128 h = (__int128)p; int c = (int)(int[2]){ 0 }; int d = (int)*&p;\n"
    "    int vla[n]; int g = (char)vla + (short)bp;\n"
    "    (void)x; (void)y; (void)u; (void)e; (void)e2; (void)b; (void)l; (void)m; (void)w; (void)h; (void)c; (void)d; (void)g;\n"
    "    (void)sizeof((short)p); (void)_Generic((char)v, char: 0);\n"
    "}\n";

static const CheckDriverDefaultWarningFixture check_driver_pointer_cast_fixtures[] =
{
    {
        .name = "casts from pointers to narrower integers",
        .source = check_driver_pointer_cast_source,
        .warnings = 19,
        .notes = 1,
    },
    {
        .name = "casts from pointers to narrower integers in C23 under -Wall -Wextra",
        .source = check_driver_pointer_cast_source,
        .options = { "-Wall", "-Wextra" },
        .warnings = 19,
        .notes = 1,
        .c23 = true,
    },
    {
        .name = "-Wno-pointer-to-int-cast holds the enumerations' and void's",
        .source = check_driver_pointer_cast_source,
        .options = { "-Wno-pointer-to-int-cast" },
        .warnings = 1,
    },
    {
        .name = "-Wno-void-pointer-to-int-cast holds -Wvoid-pointer-to-enum-cast",
        .source = check_driver_pointer_cast_source,
        .options = { "-Wno-void-pointer-to-int-cast" },
        .warnings = 15,
    },
    {
        .name = "-Wno-pointer-to-enum-cast holds -Wvoid-pointer-to-enum-cast",
        .source = check_driver_pointer_cast_source,
        .options = { "-Wno-pointer-to-enum-cast" },
        .warnings = 17,
        .notes = 1,
    },
    {
        .name = "-Werror=pointer-to-int-cast with -Wno-everything -Wvoid-pointer-to-enum-cast",
        .source = check_driver_pointer_cast_source,
        .options = { "-Wno-everything", "-Wvoid-pointer-to-enum-cast", "-Werror=pointer-to-int-cast" },
        .errors = 18,
        .notes = 1,
    },
};

static void check_test_driver_pointer_casts(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_fixtures(test, clang, executable, directory, check_driver_pointer_cast_fixtures,
                                      BUSTER_ARRAY_LENGTH(check_driver_pointer_cast_fixtures));
}
