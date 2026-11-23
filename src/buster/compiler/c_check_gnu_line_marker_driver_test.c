// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original -Wgnu-line-marker sources (#2222, c_preprocessor_impl.c's pp_gnu_line_marker). Clang supplies the external
// diagnostic oracle. Include after c_check_warning_driver_test.c, whose fixture runner compares complete no-caret
// diagnostics through both kernels and lane requests, under the default, -Werror and -w policies, with -fsyntax-only
// and -c.

static const CheckDriverDefaultWarningFixture check_driver_gnu_line_marker_fixtures[] =
{
    {
        // At the name, a macro's (with its note) or written past an empty one, and at a line's end without one, each
        // numbered as before its own marker, the first after a flag 1 with its include stack; a pragma ignores, promotes
        // (by -Wgnu) and demotes (by -Wpedantic) it; a #line is none.
        .name = "gnu line markers' places, macros and pragmas",
        .source = "#define NAME \"named.c\"\n"
                  "#define FLAG 1\n"
                  "#define EMPTY\n"
                  "# 10 NAME\n"
                  "# 20 \"f.c\" FLAG\n"
                  "# 30 \"main.c\" 2\n"
                  "#pragma clang diagnostic push\n"
                  "#pragma clang diagnostic ignored \"-Wgnu-line-marker\"\n"
                  "# 40 \"main.c\"\n"
                  "#pragma clang diagnostic pop\n"
                  "# 50 EMPTY \"main.c\" EMPTY\n"
                  "#pragma clang diagnostic error \"-Wgnu\"\n"
                  "# 60 \"main.c\"\n"
                  "#pragma clang diagnostic warning \"-Wpedantic\"\n"
                  "# 70 \"main.c\"\n"
                  "#line 80 \"main.c\"\n"
                  "# 90 \"main.c\" // comment\n"
                  "#\\\n"
                  " 100 \"main.c\"\n"
                  "# 110\n"
                  "int x;\n",
        .options = { "-pedantic" },
        .warnings = 8,
        .errors = 1,
        .notes = 1,
    },
    {
        // Without a name: at the line's end, past a comment and blanks, or at the file's end without a newline; and not
        // after a marker that makes a system header.
        .name = "gnu line markers without names",
        .source = "# 5\n"
                  "# 7 /* comment */  \n"
                  "# 12 \"main.c\" 1\n"
                  "# 14 \"main.c\" 2\n"
                  "# 20 \"s.c\" 3\n"
                  "# 30 \"main.c\"\n"
                  "int x;\n"
                  "# 40",
        .options = { "-Wgnu" },
        .warnings = 6,
    },
    {
        // Off by default, and ignored by a pragma of -Wgnu.
        .name = "gnu line markers off by default",
        .source = "#include <stddef.h>\n"
                  "# 1 \"user.c\"\n"
                  "# 2 \"user.c\"\n"
                  "int x;\n"
                  "#pragma clang diagnostic ignored \"-Wgnu\"\n"
                  "# 3 \"user.c\"\n",
    },
    {
        .name = "gnu line markers under -Wgnu-line-marker",
        .source = "#include <stddef.h>\n"
                  "# 1 \"user.c\"\n"
                  "# 2 \"user.c\"\n"
                  "int x;\n"
                  "#pragma clang diagnostic ignored \"-Wgnu\"\n"
                  "# 3 \"user.c\"\n",
        .options = { "-Wgnu-line-marker" },
        .warnings = 2,
    },
};

static void check_test_driver_gnu_line_marker(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_fixtures(test, clang, executable, directory, check_driver_gnu_line_marker_fixtures,
                                      BUSTER_ARRAY_LENGTH(check_driver_gnu_line_marker_fixtures));
}
