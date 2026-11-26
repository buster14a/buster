// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original -Wconstant-logical-operand sources (#2900), with independently counted warnings, errors and notes.
// Clang 23.1.4 is an external behavior oracle. Include after c_check_warning_driver_test.c: its runner compares
// complete diagnostics and status through both readers and one/three requested lanes, in syntax/object modes,
// with the default, -Werror and -w policies. Rejected object runs must preserve an existing output file.

typedef struct CheckDriverConstantLogicalFile CheckDriverConstantLogicalFile;
struct CheckDriverConstantLogicalFile
{
    const char* path;
    CheckDriverDefaultWarningFixture fixture;
};

static void check_test_driver_constant_logical(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const CheckDriverConstantLogicalFile files[] =
    {
        {
            .path = "driver/constant_logical_values.c",
            .fixture =
            {
                .name = "logical constants retain integer values and source operand distinctions",
                .warnings = 18,
                .notes = 28,
            },
        },
        {
            .path = "driver/constant_logical_quiet.c",
            .fixture =
            {
                .name = "logical constants leave boolean, enum and nonconstant controls quiet",
            },
        },
        {
            .path = "driver/constant_logical_quiet.c",
            .fixture =
            {
                .name = "logical constant quiet controls keep C23 behavior",
                .c23 = true,
            },
        },
        {
            .path = "driver/constant_logical_eval.c",
            .fixture =
            {
                .name = "logical constants distinguish foldable object expressions from runtime reads and effects",
                .warnings = 17,
                .notes = 22,
            },
        },
        {
            .path = "driver/constant_logical_eval.c",
            .fixture =
            {
                .name = "logical constant object evaluation keeps C23 behavior",
                .warnings = 17,
                .notes = 22,
                .c23 = true,
            },
        },
        {
            .path = "driver/constant_logical_erased.c",
            .fixture =
            {
                .name = "logical constants retain builtin and complex const values in erased operands",
                .warnings = 11,
                .notes = 16,
            },
        },
        {
            .path = "driver/constant_logical_erased.c",
            .fixture =
            {
                .name = "C23 logical constants retain erased const values and reject runtime builtin operands",
                .warnings = 11,
                .notes = 16,
                .c23 = true,
            },
        },
        {
            .path = "driver/constant_logical_wide.c",
            .fixture =
            {
                .name = "logical constants inspect every wide integer limb before the boolean exception",
                .warnings = 4,
                .notes = 7,
            },
        },
        {
            .path = "driver/constant_logical_wide.c",
            .fixture =
            {
                .name = "logical wide integer constants keep C23 behavior",
                .warnings = 4,
                .notes = 7,
                .c23 = true,
            },
        },
        {
            .path = "driver/constant_logical_invalid.c",
            .fixture =
            {
                .name = "invalid arithmetic is not a logical constant and valid sign-bit shifts retain diagnostic order",
                .warnings = 20,
                .notes = 2,
            },
        },
        {
            .path = "driver/constant_logical_invalid.c",
            .fixture =
            {
                .name = "C23 logical warning evaluation rejects invalid arithmetic in ordinary and query operands",
                .warnings = 20,
                .notes = 2,
                .c23 = true,
            },
        },
        {
            .path = "driver/constant_logical_receipts.c",
            .fixture =
            {
                // Existing runtime arithmetic warnings do not fold const-object receipts. Compare the logical
                // family here; the invalid-expression fixture separately pins arithmetic diagnostics and ordering.
                .name = "logical constants retain GNU const values, valid builtin folds and complex components",
                .options =
                {
                    "-Wno-integer-overflow",
                    "-Wno-shift-count-overflow",
                },
                .warnings = 14,
                .notes = 22,
            },
        },
        {
            .path = "driver/constant_logical_receipts.c",
            .fixture =
            {
                // C23 rejects invalid const receipts with static storage, including valid members of an invalid
                // aggregate. Invalid automatic const receipts still warn, as do the valid builtin/component folds.
                .name = "C23 logical constants distinguish automatic and static invalid const receipts",
                .options =
                {
                    "-Wno-integer-overflow",
                    "-Wno-shift-count-overflow",
                },
                .warnings = 6,
                .notes = 10,
                .c23 = true,
                // Pinned Clang accepts these declarations for syntax checking but its C23 backend rejects the
                // overflowing static initializer. This row compares the semantic warning contract alone.
                .syntax_only = true,
            },
        },
        {
            .path = "driver/constant_logical_macros.c",
            .fixture =
            {
                .name = "logical constants distinguish macro operators from macro operands",
                .warnings = 5,
                .notes = 8,
            },
        },
        {
            .path = "driver/constant_logical_stress.c",
            .fixture =
            {
                .name = "logical constants retain long dependency chains and shared conditional values",
                .warnings = 4,
                .notes = 6,
            },
        },
        {
            .path = "driver/constant_logical_stress.c",
            .fixture =
            {
                .name = "C23 logical constants retain long and shared dependency paths",
                .warnings = 4,
                .notes = 6,
                .c23 = true,
            },
        },
        {
            .path = "driver/constant_logical_system.c",
            .fixture =
            {
                .name = "logical warnings ignore system headers and resume in user source",
                .options =
                {
                    "-Itests/driver",
                },
                .warnings = 1,
                .notes = 2,
            },
        },
        {
            .path = "driver/constant_logical_system.c",
            .fixture =
            {
                .name = "system header opt in retains logical warnings and notes",
                .options =
                {
                    "-Itests/driver",
                    "-Wsystem-headers",
                },
                .warnings = 4,
                .notes = 7,
            },
        },
        {
            .path = "driver/constant_logical_system.c",
            .fixture =
            {
                .name = "system header warning suppression obeys the last mapping",
                .options =
                {
                    "-Itests/driver",
                    "-Wsystem-headers",
                    "-Wno-system-headers",
                },
                .warnings = 1,
                .notes = 2,
            },
        },
        {
            .path = "driver/constant_logical_system.c",
            .fixture =
            {
                .name = "C23 system header opt in retains logical warnings and notes",
                .options =
                {
                    "-Itests/driver",
                    "-Wsystem-headers",
                },
                .warnings = 4,
                .notes = 7,
                .c23 = true,
            },
        },
        {
            .path = "driver/constant_logical_order.c",
            .fixture =
            {
                .name = "logical warnings retain operand construction and enclosing diagnostic order",
                .warnings = 11,
                .notes = 17,
            },
        },
        {
            .path = "driver/constant_logical_assert.c",
            .fixture =
            {
                .name = "logical warnings precede failed assertions in every scope",
                .warnings = 9,
                .errors = 5,
                .notes = 16,
            },
        },
        {
            .path = "driver/constant_logical_assert.c",
            .fixture =
            {
                .name = "logical assertion warnings retain C23 ordering",
                .warnings = 9,
                .errors = 5,
                .notes = 16,
                .c23 = true,
            },
        },
        {
            .path = "driver/constant_logical_assert.c",
            .fixture =
            {
                .name = "logical assertion notes precede the error limit",
                .options =
                {
                    "-ferror-limit=1",
                },
                .warnings = 3,
                .errors = 2,
                .notes = 6,
            },
        },
        {
            .path = "driver/constant_logical_assert.c",
            .fixture =
            {
                .name = "logical assertion notes precede fatal errors",
                .options =
                {
                    "-Wfatal-errors",
                },
                .warnings = 2,
                .errors = 1,
                .notes = 4,
            },
        },
        {
            .path = "driver/constant_logical_assert.c",
            .fixture =
            {
                .name = "disabled logical warnings leave assertion failures and no orphan notes",
                .options =
                {
                    "-Wno-constant-logical-operand",
                },
                .errors = 5,
            },
        },
        {
            .path = "driver/constant_logical_policy.c",
            .fixture =
            {
                .name = "logical warnings obey source push, pop, suppression and promotion",
                .warnings = 3,
                .errors = 1,
                .notes = 6,
            },
        },
        {
            .path = "driver/constant_logical_policy.c",
            .fixture =
            {
                .name = "source pragmas reenable a disabled logical warning",
                .options =
                {
                    "-Wno-constant-logical-operand",
                },
                .warnings = 1,
                .errors = 1,
                .notes = 3,
            },
        },
        {
            .path = "driver/constant_logical_contexts.c",
            .fixture =
            {
                .name = "logical warnings survive unevaluated and unselected operands",
                .warnings = 12,
                .errors = 1,
                .notes = 20,
            },
        },
        {
            .path = "driver/constant_logical_contexts.c",
            .fixture =
            {
                .name = "C23 logical warnings survive unevaluated and unselected operands",
                .warnings = 12,
                .errors = 1,
                .notes = 20,
                .c23 = true,
            },
        },
        {
            .path = "driver/constant_logical_recovery.c",
            .fixture =
            {
                .name = "logical warnings survive later file and block semantic errors",
                .warnings = 4,
                .errors = 3,
                .notes = 6,
            },
        },
        {
            .path = "driver/constant_logical_recovery.c",
            .fixture =
            {
                .name = "C23 logical warnings retain semantic recovery order",
                .warnings = 4,
                .errors = 3,
                .notes = 6,
                .c23 = true,
            },
        },
        {
            .path = "driver/constant_logical_prefix.c",
            .fixture =
            {
                .name = "logical warnings in a complete prefix survive a later structural parse error",
                .warnings = 3,
                .errors = 1,
                .notes = 5,
            },
        },
        {
            .path = "driver/constant_logical_c23.c",
            .fixture =
            {
                .name = "C23 logical warnings distinguish constexpr integers and booleans",
                .warnings = 4,
                .errors = 1,
                .notes = 7,
                .c23 = true,
            },
        },
        {
            .path = "driver/constant_logical_group.c",
            .fixture =
            {
                .name = "logical constant warnings are enabled by default in enum initializers",
                .warnings = 2,
                .notes = 3,
            },
        },
        {
            .path = "driver/constant_logical_group.c",
            .fixture =
            {
                .name = "common warning groups do not suppress the independent logical family",
                .options =
                {
                    "-Wno-all",
                    "-Wno-most",
                    "-Wno-extra",
                    "-Wno-parentheses",
                },
                .warnings = 2,
                .notes = 3,
            },
        },
        {
            .path = "driver/constant_logical_group.c",
            .fixture =
            {
                .name = "conversion warnings do not control the logical family",
                .options =
                {
                    "-Wno-conversion",
                },
                .warnings = 2,
                .notes = 3,
            },
        },
        {
            .path = "driver/constant_logical_group.c",
            .fixture =
            {
                .name = "common warning groups do not enable a disabled logical family",
                .options =
                {
                    "-Wno-everything",
                    "-Wall",
                    "-Wextra",
                    "-Wmost",
                },
            },
        },
        {
            .path = "driver/constant_logical_group.c",
            .fixture =
            {
                .name = "everything does not override explicit logical warning suppression",
                .options =
                {
                    "-Wno-constant-logical-operand",
                    "-Weverything",
                },
            },
        },
        {
            .path = "driver/constant_logical_group.c",
            .fixture =
            {
                .name = "explicit logical warning selection reenables everything suppression",
                .options =
                {
                    "-Wno-everything",
                    "-Wconstant-logical-operand",
                },
                .warnings = 2,
                .notes = 3,
            },
        },
        {
            .path = "driver/constant_logical_group.c",
            .fixture =
            {
                .name = "logical warning options keep the last exact mapping",
                .options =
                {
                    "-Wno-constant-logical-operand",
                    "-Wconstant-logical-operand",
                },
                .warnings = 2,
                .notes = 3,
            },
        },
        {
            .path = "driver/constant_logical_group.c",
            .fixture =
            {
                .name = "logical warning promotion retains both operators' notes",
                .options =
                {
                    "-Werror=constant-logical-operand",
                },
                .errors = 2,
                .notes = 3,
            },
        },
        {
            .path = "driver/constant_logical_group.c",
            .fixture =
            {
                .name = "logical warning demotion survives a later global Werror",
                .options =
                {
                    "-Wno-error=constant-logical-operand",
                },
                .warnings = 2,
                .notes = 3,
            },
        },
    };
    for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(files); f += 1)
    {
        char path[PATH_MAX];
        const int file = check_test_fixture_path(path, sizeof(path), files[f].path) ? open(path, O_RDONLY | O_CLOEXEC) : -1;
        char* source = NULL;
        const bool read = file >= 0 && check_test_driver_read_file(file, &source);
        check_test_expect(test, read, "constant logical operand runtime fixture is read");
        if (read)
        {
            CheckDriverDefaultWarningFixture fixture = files[f].fixture;
            fixture.source = source;
            check_test_driver_warning_fixtures(test, clang, executable, directory, &fixture, 1);
        }
        if (file >= 0)
        {
            close(file);
        }
        free(source);
    }
}
