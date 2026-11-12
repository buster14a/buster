// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original -Wshift-sign-overflow sources (#2264, c_check_common_warnings.c's check_common_shift_sign_overflow). Clang
// supplies the external diagnostic oracle. Include after c_check_warning_driver_test.c, whose fixture runner compares
// complete no-caret diagnostics through both kernels and lane requests, under the default, -Werror and -w policies, with
// -fsyntax-only and -c.

static const CheckDriverDefaultWarningFixture check_driver_shift_sign_overflow_fixtures[] =
{
    {
        // A nonnegative signed left operand shifted exactly into its promoted type's sign bit, at file scope, in a body,
        // in an enumerator, a bound, an unevaluated operand and unreachable code; a typedef's spelling; not an unsigned
        // shift, one short of the sign bit, a negative left operand's or an over-wide count's.
        .name = "shifts into the sign bit",
        .source = "typedef int I;\n"
                  "int a = 1 << 31;\n"
                  "enum { E = 1 << 31 };\n"
                  "int b[(1 << 30 << 1) < 0 ? 1 : 2];\n"
                  "int f(int x)\n"
                  "{\n"
                  "    long c = 1L << 63;\n"
                  "    int d = 2 << 30, e = (I)3 << 30, g = (short)1 << 31, h = (char)64 << 25;\n"
                  "    unsigned u = 1u << 31;\n"
                  "    int v = 3 << 29, w = -1 << 3, y = 1 << 32, z = 0 << 31;\n"
                  "    unsigned long s = sizeof(1 << 31);\n"
                  "    __int128 q = (__int128)1 << 127;\n"
                  "    if (x)\n"
                  "        return 1 << 31;\n"
                  "    return (int)(x + c + d + e + g + h + u + v + w + y + z + s + q) + (1 << x);\n"
                  "}\n",
        .options = { "-Wshift-sign-overflow" },
        .warnings = 13,
    },
    {
        // The constant reader's shifts, which no graph node holds: enumerators, file-scope and member bounds, a static
        // assertion's, a block's enumerator and bound; a cast's typedef spelling; a 128-bit and a long shift.
        .name = "shifts into the sign bit in constants",
        .source = "typedef int I;\n"
                  "enum { E = (I)3 << 30, F = 1 << 30, G = (short)1 << 31 };\n"
                  "int b[((__int128)1 << 127) < 0 ? 1 : 2];\n"
                  "int c[((long)1 << 63) < 0 ? 1 : 2];\n"
                  "_Static_assert((1 << 31) < 0, \"\");\n"
                  "struct S { int x[(1 << 31) < 0]; };\n"
                  "void f(void) { enum { H = 2 << 30 }; int y[(1 << 31) < 0 ? 1 : 2]; (void)y; }\n",
        .options = { "-Wshift-sign-overflow" },
        .warnings = 8,
    },
    {
        // Off by default, and a pragma's; none under -fwrapv.
        .name = "shift sign overflow policies",
        .source = "int a = 1 << 31;\n"
                  "#pragma clang diagnostic warning \"-Wshift-sign-overflow\"\n"
                  "int b = 1 << 31;\n"
                  "#pragma clang diagnostic ignored \"-Wshift-sign-overflow\"\n"
                  "int c = 1 << 31;\n",
        .warnings = 1,
    },
    {
        // A bit-precise type's own width: its sign bit, not a byte's.
        .name = "shift sign overflow of bit-precise types",
        .source = "_BitInt(9) a(void) { return (_BitInt(9))1 << 8; }\n"
                  "_BitInt(9) b(void) { return (_BitInt(9))1 << 7; }\n"
                  "_BitInt(9) c(void) { return (_BitInt(9))3 << 7; }\n"
                  "_BitInt(65) d(void) { return (_BitInt(65))1 << 64; }\n",
        .options = { "-Weverything", "-Wno-missing-prototypes", "-Wno-pre-c23-compat" },
        .c23 = true,
        .warnings = 3,
    },
    {
        .name = "shift sign overflow under -fwrapv",
        .source = "int a = 1 << 31;\n",
        .options = { "-Wshift-sign-overflow", "-fwrapv" },
    },
};

static void check_test_driver_shift_sign_overflow(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_fixtures(test, clang, executable, directory, check_driver_shift_sign_overflow_fixtures,
                                      BUSTER_ARRAY_LENGTH(check_driver_shift_sign_overflow_fixtures));
}
