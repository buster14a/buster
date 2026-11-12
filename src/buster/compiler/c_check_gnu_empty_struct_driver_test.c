// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original -Wgnu-empty-struct sources (#2409, c_check_input.c's CHECK_WARNING_EVENT_GNU_EMPTY_STRUCT). Clang supplies the
// external diagnostic oracle. Include after c_check_warning_driver_test.c, whose fixture runner compares complete
// no-caret diagnostics through both kernels and lane requests, under the default, -Werror and -w policies, with
// -fsyntax-only and -c.

static const CheckDriverDefaultWarningFixture check_driver_gnu_empty_struct_fixtures[] =
{
    {
        // Empty structs and unions, and those of unnamed bit-fields alone, at their keyword: a typedef's, a nested one's
        // before its enclosing record's, a block's and a member's; an anonymous member names none but counts.
        .name = "records without named members",
        .source = "struct s {};\n"
                  "typedef struct {} T;\n"
                  "  union u {};\n"
                  "struct b { int : 3; };\n"
                  "struct n { struct {}; };\n"
                  "void f(void) { struct l {} x; (void)x; }\n"
                  "struct o { struct m {} q; };\n"
                  "struct c { int : 0; };\n"
                  "struct d { int a : 2; };\n",
        .options = { "-pedantic" },
        .warnings = 8,
        .compare_warning_text = true,
    },
    {
        // -Wgnu-empty-struct and -Wgnu give them; they're ignored by default.
        .name = "records without named members under -Wgnu",
        .source = "struct s {};\nunion u { int : 1; };\n",
        .options = { "-Wgnu" },
        .warnings = 2,
        .compare_warning_text = true,
    },
    {
        .name = "records without named members by default",
        .source = "struct s {};\nunion u { int : 1; };\n",
        .compare_warning_text = true,
    },
};

static void check_test_driver_gnu_empty_struct(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_fixtures(test, clang, executable, directory, check_driver_gnu_empty_struct_fixtures,
                                      BUSTER_ARRAY_LENGTH(check_driver_gnu_empty_struct_fixtures));
}
