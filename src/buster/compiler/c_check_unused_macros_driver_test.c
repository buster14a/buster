// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original -Wunused-macros sources (#2222, c_preprocessor_impl.c's pp_unused_macro_track). Clang supplies the external
// diagnostic oracle. Include after c_check_warning_driver_test.c, whose fixture runner compares complete no-caret
// diagnostics through both kernels and lane requests, under the default, -Werror and -w policies, with -fsyntax-only
// and -c.

static const CheckDriverDefaultWarningFixture check_driver_unused_macros_fixtures[] =
{
    {
        // Used: expanded (in a #if line too), named by #ifdef or a live `defined`. Unused: named only in an unexpanded body or
        // a dead `defined`, a function-like name without its (, a definition #undef or a redefinition replaces.
        .name = "basic unused macros",
        .source = "#define USED 1\n"
                  "#define UNUSED 2\n"
                  "#define ONLY_IF 3\n"
                  "#define IN_BODY 4\n"
                  "#define OUTER IN_BODY\n"
                  "#define FN(x) (x)\n"
                  "#define TESTED 6\n"
                  "#define LIVE 7\n"
                  "#define DEAD 8\n"
                  "#define BEFORE 9\n"
                  "#undef BEFORE\n"
                  "#define TWICE 10\n"
                  "#define TWICE 10\n"
                  "#define AGAIN 11\n"
                  "#undef AGAIN\n"
                  "#define AGAIN 12\n"
                  "#if ONLY_IF\n"
                  "#endif\n"
                  "#ifdef TESTED\n"
                  "#endif\n"
                  "#if defined(LIVE) || defined(DEAD)\n"
                  "#endif\n"
                  "#if 0 && defined(DEAD)\n"
                  "#endif\n"
                  "int FN;\n"
                  "int x = USED + FN(1) + AGAIN;\n",
        .options = { "-Wunused-macros" },
        .warnings = 8,
    },
    {
        // The main file's own definitions only, not those after a line marker that enters a file or makes a system header;
        // and an #undef there of a main file's macro, which warns, and its redefinition, which takes it out.
        .name = "sys unused macros",
        .source = "#include <stddef.h>\n"
                  "#define MAIN 1\n"
                  "# 1 \"entered.h\" 1\n"
                  "#define ENTERED 1\n"
                  "# 4 \"basic.c\" 2\n"
                  "#define BACK 1\n"
                  "# 1 \"system.h\" 3\n"
                  "#define SYSTEM 1\n"
                  "#undef MAIN\n"
                  "#define BACK 2\n"
                  "# 8 \"basic.c\"\n"
                  "#define LATER 1\n"
                  "int z;\n",
        .options = { "-Wunused-macros" },
        .warnings = 2,
    },
    {
        // Off by default: a pragma enables, promotes and ignores it, read at each definition's place.
        .name = "prag unused macros",
        .source = "#define OFF 1\n"
                  "#pragma clang diagnostic push\n"
                  "#pragma clang diagnostic warning \"-Wunused-macros\"\n"
                  "#define ON 1\n"
                  "#pragma clang diagnostic error \"-Wunused-macros\"\n"
                  "#define AS_ERROR 1\n"
                  "#pragma clang diagnostic pop\n"
                  "#define OFF_AGAIN 1\n"
                  "#pragma clang diagnostic warning \"-Wunused-macros\"\n"
                  "#define ENABLED 1\n"
                  "#pragma clang diagnostic ignored \"-Wunused-macros\"\n"
                  "int z;\n",
        .warnings = 2,
        .errors = 1,
    },
    {
        // The warnings left at the main file's end, in the order of clang's set of places, through its growth and #undefs.
        .name = "order unused macros",
        .source = "#define M0 0\n"
                  "#define M1 1\n"
                  "#define M2 2\n"
                  "#define M3 3\n"
                  "#define M4 4\n"
                  "#define M5 5\n"
                  "#define M6 6\n"
                  "#define M7 7\n"
                  "#define M8 8\n"
                  "#define M9 9\n"
                  "#define M10 10\n"
                  "#define M11 11\n"
                  "#define M12 12\n"
                  "#define M13 13\n"
                  "#define M14 14\n"
                  "#define M15 15\n"
                  "#define M16 16\n"
                  "#define M17 17\n"
                  "#define M18 18\n"
                  "#define M19 19\n"
                  "#define M20 20\n"
                  "#define M21 21\n"
                  "#define M22 22\n"
                  "#define M23 23\n"
                  "#define M24 24\n"
                  "#define M25 25\n"
                  "#define M26 26\n"
                  "#define M27 27\n"
                  "#define M28 28\n"
                  "#define M29 29\n"
                  "#define M30 30\n"
                  "#define M31 31\n"
                  "#define M32 32\n"
                  "#define M33 33\n"
                  "#define M34 34\n"
                  "#define M35 35\n"
                  "#define M36 36\n"
                  "#define M37 37\n"
                  "#define M38 38\n"
                  "#define M39 39\n"
                  "#undef M0\n"
                  "#undef M3\n"
                  "#undef M6\n"
                  "#undef M9\n"
                  "#undef M12\n"
                  "#undef M15\n"
                  "#undef M18\n"
                  "#undef M21\n"
                  "#undef M24\n"
                  "#undef M27\n"
                  "#undef M30\n"
                  "#undef M33\n"
                  "#undef M36\n"
                  "#undef M39\n"
                  "int v1 = M1;\n"
                  "int v11 = M11;\n"
                  "int v16 = M16;\n"
                  "int v26 = M26;\n"
                  "int v31 = M31;\n"
                  "#define M40(a) a\n"
                  "#define M41(a) a\n"
                  "#define M42(a) a\n"
                  "#define M43(a) a\n"
                  "#define M44(a) a\n"
                  "#define M45(a) a\n"
                  "#define M46(a) a\n"
                  "#define M47(a) a\n"
                  "#define M48(a) a\n"
                  "#define M49(a) a\n"
                  "#define M50(a) a\n"
                  "#define M51(a) a\n"
                  "int z;\n",
        .options = { "-Wunused-macros" },
        .warnings = 47,
    },
    {
        // #pragma pop_macro takes the replaced definition's place out silently, and an #undef after a pop warns again.
        .name = "push unused macros",
        .source = "#define P 1\n"
                  "#pragma push_macro(\"P\")\n"
                  "#undef P\n"
                  "#pragma pop_macro(\"P\")\n"
                  "#undef P\n"
                  "#define Q 1\n"
                  "#pragma push_macro(\"Q\")\n"
                  "#define Q 2\n"
                  "#pragma pop_macro(\"Q\")\n"
                  "#define V 1\n"
                  "#pragma push_macro(\"V\")\n"
                  "#pragma pop_macro(\"V\")\n"
                  "int z;\n",
        .options = { "-Wunused-macros" },
        .warnings = 3,
    },
    {
        // Not in -Wall.
        .name = "unused macros under -Wall",
        .source = "#define UNUSED 1\n"
                  "int z;\n",
        .options = { "-Wall" },
    },
};

static void check_test_driver_unused_macros(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_fixtures(test, clang, executable, directory, check_driver_unused_macros_fixtures,
                                      BUSTER_ARRAY_LENGTH(check_driver_unused_macros_fixtures));
}
