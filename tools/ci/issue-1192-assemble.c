// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Temporary, exact-input edit adapter for the connector-only #1192 session. Not part of the published patch.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int edit(const char* path, const char* from, const char* until, const char* to)
{
    FILE* file = fopen(path, "rb");
    int ok = file != NULL && fseek(file, 0, SEEK_END) == 0;
    const long size = ok ? ftell(file) : -1;
    char* bytes = size >= 0 && size < 16777216 ? malloc((size_t)size + 1) : NULL;
    ok = ok && bytes != NULL && fseek(file, 0, SEEK_SET) == 0 && fread(bytes, 1, (size_t)size, file) == (size_t)size;
    if (file != NULL)
    {
        ok = fclose(file) == 0 && ok;
    }
    char* first = NULL;
    char* last = NULL;
    if (ok)
    {
        bytes[size] = 0;
        first = strstr(bytes, from);
        ok = first != NULL && strstr(first + strlen(from), from) == NULL;
        last = ok ? (until != NULL ? strstr(first + strlen(from), until) : first + strlen(from)) : NULL;
        ok = ok && last != NULL;
    }
    if (ok)
    {
        file = fopen(path, "wb");
        const size_t before = (size_t)(first - bytes);
        const size_t after = (size_t)size - (size_t)(last - bytes);
        ok = file != NULL && fwrite(bytes, 1, before, file) == before && fwrite(to, 1, strlen(to), file) == strlen(to) && fwrite(last, 1, after, file) == after;
        if (file != NULL)
        {
            ok = fclose(file) == 0 && ok;
        }
    }
    if (!ok)
    {
        fprintf(stderr, "exact-input edit failed: %s: %s\n", path, from);
    }
    free(bytes);
    return ok;
}

int main(void)
{
    const char* program = "src/buster/compiler/c_check_program.c";
    const char* tests = "src/buster/compiler/c_check_program_select_test.c";
    int ok = edit(program, "    uint32_t* fold_stack;\n", NULL,
                  "    // Temporary storage for a known constant switch's nonrecursive source collector.\n    Arena* switch_scratch;\n    uint32_t* fold_stack;\n");
    ok = ok && edit(program, "        .fold_stack = stack,\n", NULL, "        .switch_scratch = scratch,\n        .fold_stack = stack,\n");
    ok = ok && edit(program, "// Appends a source label's number", NULL, "#include \"c_check_program_switch.c\"\n\n// Appends a source label's number");
    ok = ok && edit(program,
        "        else if (parser->tokens.kinds[keyword] == PARSE_KIND_SWITCH)\n        {\n            writer->stops[token] = end;\n",
        "        else\n        {\n            check_program_fail(writer->program, CHECK_PROGRAM_UNSUPPORTED, keyword);",
        "        else if (parser->tokens.kinds[keyword] == PARSE_KIND_SWITCH)\n        {\n            check_control_switch(writer, keyword, token, body, condition, end, task.repeat);\n        }\n");
    ok = ok && edit(tests, "// #418's CI and #1108:", NULL, "#include \"c_check_program_switch_test.c\"\n\n// #418's CI and #1108:");
    ok = ok && edit(tests, "static void check_test_program_folded_ifs(CheckTest* test, const char* clang)\n{\n", NULL,
        "static void check_test_program_folded_ifs(CheckTest* test, const char* clang)\n{\n    check_test_program_folded_switches(test, clang);\n");
    ok = ok && edit("docs/c-lowering.md",
        "A constant\n  `switch`'s cases that clang skips (`CodeGenFunction::EmitSwitchStmt`'s\n  `FindCaseStatementsForValue`) are still written and selected.", NULL,
        "Constant\n  `switch` statements use the same dropped ranges (#1192), before reference\n  selection: a foldable selector keeps the matching case or default and its\n  fallthrough statements through the first direct break, or nothing without a\n  match/default. The nonrecursive collector in `c_check_program_switch.c`\n  retains the whole switch when skipped labels or declarations, an escaping\n  inner break, a partially entered scope's fallthrough declarations, or a case\n  range encountered before the reverse-order match prevent Clang's\n  `FindCaseStatementsForValue` shortcut. Nonconstant/effectful selectors and\n  the outside-constant-context refusals (`__builtin_constant_p(n)`, `(n,1)`)\n  keep normal dispatch. Complete retained blocks keep their scope exits;\n  terminal breaks use the ordinary cleanup, VLA-restore and lifetime machinery.\n  Dropped regions and block-scope statics introduce no selection roots.");
    ok = ok && edit("docs/commands/checker.md", "  `--program-selection` selects the emission-selection and constant-if regressions alone, including #1194's\n", NULL,
        "  `--program-selection` selects the emission-selection, constant-if and constant-switch regressions alone.\n  The switch fixtures (#1192, `c_check_program_switch_test.c`) cover matching/default/no-match dispatch,\n  fallthrough, converted values, range ordering, label/declaration/inner-break fallbacks, nonfoldable selectors,\n  nested scopes, cleanups, VLAs and dropped statics. The constant-if fixtures include #1194's\n");
    return ok ? 0 : 1;
}
