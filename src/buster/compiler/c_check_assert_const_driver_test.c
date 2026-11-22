// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original sources of a failed static assertion's note on a const object's value (#2214, c_check_semantic_diagnostic.c's
// check_semantic_operand_value). Clang supplies the external diagnostic oracle. Include after c_check_warning_driver_test.c,
// whose fixture runner compares complete no-caret diagnostics through both kernels and lane requests, under the default,
// -Werror and -w policies.

#include "c_check_assert_requirement_fallback_test.c"

static const CheckDriverDefaultWarningFixture check_driver_assert_const_fixtures[] =
{
    {
        // "expression evaluates to" with a const object's value its initializer gives: a scalar's, in an operand's
        // arithmetic, a member's, an element's, a block's static's and a char's (as a character).
        .name = "static assertions of const objects",
        .source = "const int k = 0;\n"
                  "_Static_assert(k == 45, \"\");\n"
                  "const long m = 7;\n"
                  "_Static_assert(m + 1 == 3, \"\");\n"
                  "struct P { int a; }; const struct P p = { 4 };\n"
                  "_Static_assert(p.a == 5, \"\");\n"
                  "const int arr[2] = { 1, 2 };\n"
                  "_Static_assert(arr[1] == 5, \"\");\n"
                  "void f(void) { static const int s = 3; _Static_assert(s == 4, \"\"); }\n"
                  "const char c = 'x';\n"
                  "_Static_assert(c == 'y', \"\");\n",
        .errors = 6,
        .notes = 6,
        .syntax_only = true,
    },
    {
        // The requirement as clang's printer prints it (#2482): a compound literal's completed array bound, a _Generic's
        // `int: 1` associations, and no note where a selection's chosen operand is a literal; a record or enumeration a
        // type name defines by its name, or `(unnamed at f.c:1:2)`, without its body.
        .name = "static assertion requirements of selections, compound literals and defined tags",
        .source = "int x;\n"
                  "_Static_assert(sizeof((int[]){1, 2}) == 1, \"a\");\n"
                  "_Static_assert(sizeof((int[][2]){{1, 2}, {3}}) == 1, \"b\");\n"
                  "_Static_assert(_Generic(x, int: 1, default: 2) == 2, \"c\");\n"
                  "_Static_assert(_Generic(x, int: 1, default: 2) + 1 == 3, \"d\");\n"
                  "_Static_assert(__builtin_choose_expr(1, 3, 4) == 4, \"e\");\n"
                  "_Static_assert(__builtin_offsetof(struct { int a, b; }, b) == 1, \"f\");\n"
                  "_Static_assert(__builtin_offsetof(struct T { int a, b; }, b) == 1, \"g\");\n"
                  "_Static_assert(__builtin_offsetof(union { int a, b; }, b) == 1, \"h\");\n"
                  "_Static_assert(sizeof(enum E { A }) == 1, \"i\");\n"
                  "_Static_assert(sizeof(enum { B }) == 1, \"j\");\n",
        .errors = 10,
        .notes = 8,
        .syntax_only = true,
    },
};

static void check_test_driver_assert_const(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_fixtures(test, clang, executable, directory, check_driver_assert_const_fixtures,
                                      BUSTER_ARRAY_LENGTH(check_driver_assert_const_fixtures));
    static const struct
    {
        const char* path;
        CheckDriverDefaultWarningFixture fixture;
    } files[] =
    {
        {
            .path = "tests/driver/assert_requirement_selection.c",
            .fixture =
            {
                .name = "selected literal and conjunction requirements",
                .errors = 12,
                .notes = 6,
            },
        },
        {
            .path = "tests/driver/assert_requirement_types.c",
            .fixture =
            {
                .name = "canonical attributed type requirements",
                .errors = 13,
            },
        },
        {
            .path = "tests/driver/assert_requirement_ignored.c",
            .fixture =
            {
                .name = "ignored attributes disappear from requirements",
                // Existing duplicate warnings of these families are independent of requirement spelling.
                .options = { "-Wno-ignored-attributes", "-Wno-unknown-attributes" },
                .errors = 2,
            },
        },
        {
            .path = "tests/driver/assert_requirement_controls.c",
            .fixture =
            {
                .name = "passing selected and attributed assertions",
            },
        },
        {
            .path = "tests/driver/assert_member_extension.c",
            .fixture =
            {
                .name = "extension-prefixed member assertion recovery",
                .errors = 12,
            },
        },
        {
            .path = "tests/driver/assert_member_extension.c",
            .fixture =
            {
                .name = "member assertion recovery under the error limit",
                .options = { "-ferror-limit=1" },
                .errors = 2,
            },
        },
        {
            .path = "tests/driver/assert_member_extension.c",
            .fixture =
            {
                .name = "member assertion recovery under fatal errors",
                .options = { "-Wfatal-errors" },
                .errors = 1,
            },
        },
        {
            .path = "tests/driver/assert_member_extension_macro.c",
            .fixture =
            {
                .name = "member assertion recovery retains macro notes",
                .errors = 6,
                .notes = 5,
            },
        },
        {
            .path = "tests/driver/assert_member_extension_arguments.c",
            .fixture =
            {
                .name = "member assertion recovery retains argument origins",
                .errors = 9,
                .notes = 8,
            },
        },
        {
            .path = "tests/driver/assert_member_extension_after.c",
            .fixture =
            {
                .name = "member assertion recovery continues after the rejected record",
                .errors = 4,
            },
        },
        {
            .path = "tests/driver/assert_member_extension_controls.c",
            .fixture =
            {
                .name = "accepted member and extension assertion controls",
            },
        },
    };
    for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(files); f += 1)
    {
        const int file = open(files[f].path, O_RDONLY | O_CLOEXEC);
        char* source = NULL;
        const bool read = file >= 0 && check_test_driver_read_file(file, &source);
        check_test_expect(test, read, "assertion diagnostic runtime fixture is read (run from the repository root)");
        for (uint32_t language = 0; read && language < 2; language += 1)
        {
            CheckDriverDefaultWarningFixture fixture = files[f].fixture;
            fixture.source = source;
            fixture.c23 = language != 0;
            check_test_driver_warning_fixtures(test, clang, executable, directory, &fixture, 1);
        }
        if (file >= 0)
        {
            close(file);
        }
        free(source);
    }
    check_test_driver_assert_requirement_fallback(test, clang, executable, directory);
}
