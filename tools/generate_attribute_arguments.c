// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Generates c_check_attribute_arguments.inc (#147): how clang's parser reads each attribute's arguments, from clang's
// generated parser switches, `clang-tblgen --gen-clang-attr-parser-string-switches` of its Attr.td. Each list there is
// `#if defined(CLANG_ATTR_X)`, `.Case("name", VALUE)` lines and `#endif`, a name without a keyword's underscores (the
// parser normalizes the spelling before it asks). One row per name that any list has, in byte order:
//
//   BUSTER_CHECK_ARGUMENTS("name", flags, strings)
//
// `flags` are the lists that name it (ARGUMENTS_* below); `strings` is CLANG_ATTR_STRING_LITERAL_ARG_LIST's mask, the
// arguments the parser reads as unevaluated string literals by place (all bits for a variadic one). A VALUE is `true`, a
// number, or a condition on the syntax, the scope and the target, which is evaluated for GNU's and C23's syntax on
// x86-64: the conditions this reads mention other targets (`T.getArch() == llvm::Triple::arm` and the like), the pragma
// syntax or C++'s `omp` scope alone, so they are 0 here; any other is a failure.
//
//   cc -std=c17 -O2 -Wall -Wextra -Werror tools/generate_attribute_arguments.c -o build/generate_attribute_arguments
//   build/generate_attribute_arguments --self-test
//   build/generate_attribute_arguments switches.inc "SOURCE DESCRIPTION" > src/buster/compiler/c_check_attribute_arguments.inc
//
// It succeeds only when it has read the whole input, evaluated every value and written and flushed the whole table.

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// attributeHasIdentifierArg: an identifier first argument is an identifier, not an expression.
#define ARGUMENTS_IDENTIFIER 0x1u
// attributeHasVariadicIdentifierArg and attributeHasStrictIdentifierArgs: every identifier argument is one.
#define ARGUMENTS_VARIADIC_IDENTIFIER 0x2u
#define ARGUMENTS_STRICT_IDENTIFIER 0x4u
// attributeIsTypeArgAttr: its argument is a type name.
#define ARGUMENTS_TYPE 0x8u
// attributeParsedArgsUnevaluated: its expressions are unevaluated.
#define ARGUMENTS_UNEVALUATED 0x10u
// IsAttributeArgsParsedInFunctionScope, IsAttributeLateParsedStandard and IsAttributeLateParsedExperimentalExt: a
// function declarator's parameters are names in its expressions, and it is parsed after its declaration.
#define ARGUMENTS_FUNCTION_SCOPE 0x20u
#define ARGUMENTS_LATE 0x40u
#define ARGUMENTS_LATE_EXPERIMENTAL 0x80u

typedef struct List List;
struct List
{
    const char* macro;
    uint32_t flag;
};

// The lists this reads; CLANG_ATTR_STRING_LITERAL_ARG_LIST's values are masks, the others' booleans. C++'s keyword `this`
// and parameter packs (CLANG_ATTR_THIS_ISA_IDENTIFIER_ARG_LIST, CLANG_ATTR_ACCEPTS_EXPR_PACK) aren't C's.
static const List lists[] = {
    { "CLANG_ATTR_IDENTIFIER_ARG_LIST", ARGUMENTS_IDENTIFIER },
    { "CLANG_ATTR_VARIADIC_IDENTIFIER_ARG_LIST", ARGUMENTS_VARIADIC_IDENTIFIER },
    { "CLANG_ATTR_STRICT_IDENTIFIER_ARG_LIST", ARGUMENTS_STRICT_IDENTIFIER },
    { "CLANG_ATTR_TYPE_ARG_LIST", ARGUMENTS_TYPE },
    { "CLANG_ATTR_ARG_CONTEXT_LIST", ARGUMENTS_UNEVALUATED },
    { "CLANG_ATTR_PARSE_ARGS_IN_FUNCTION_SCOPE_LIST", ARGUMENTS_FUNCTION_SCOPE },
    { "CLANG_ATTR_LATE_PARSED_LIST", ARGUMENTS_LATE },
    { "CLANG_ATTR_LATE_PARSED_EXPERIMENTAL_EXT_LIST", ARGUMENTS_LATE_EXPERIMENTAL },
    { "CLANG_ATTR_STRING_LITERAL_ARG_LIST", 0 },
};

typedef struct Row Row;
struct Row
{
    char name[96];
    uint32_t flags;
    uint32_t strings;
};

enum { ROWS = 1024 };
static Row rows[ROWS];
static uint32_t row_count;

static char* read_all(const char* path, size_t* length)
{
    char* result = NULL;
    FILE* file = fopen(path, "rb");
    bool valid = file != NULL && fseek(file, 0, SEEK_END) == 0;
    const long size = valid ? ftell(file) : -1;
    valid = valid && size >= 0 && fseek(file, 0, SEEK_SET) == 0;
    result = valid ? malloc((size_t)size + 1) : NULL;
    valid = result != NULL && fread(result, 1, (size_t)size, file) == (size_t)size;
    if (valid)
    {
        result[size] = 0;
        *length = (size_t)size;
    }
    else
    {
        free(result);
        result = NULL;
    }
    if (file != NULL)
    {
        fclose(file);
    }
    return result;
}

static bool contains(const char* begin, const char* end, const char* text)
{
    const size_t length = strlen(text);
    bool found = false;
    for (const char* at = begin; !found && at + length <= end; at += 1)
    {
        found = memcmp(at, text, length) == 0;
    }
    return found;
}

// A VALUE (from `begin` to `end`) for GNU's and C23's syntax on x86-64: `true` or a number; a condition that mentions
// another target's architecture, the pragma syntax or the `omp` scope alone, and no x86 one, is 0.
static bool evaluate(const char* begin, const char* end, uint32_t* value)
{
    const size_t length = (size_t)(end - begin);
    bool valid = true;
    if (length == 4 && memcmp(begin, "true", 4) == 0)
    {
        *value = 1;
    }
    else if (length != 0 && begin[0] >= '0' && begin[0] <= '9')
    {
        char* stop = NULL;
        char text[32] = { 0 };
        valid = length < sizeof(text);
        if (valid)
        {
            memcpy(text, begin, length);
            const unsigned long long number = strtoull(text, &stop, 10);
            valid = stop == text + length && number <= UINT32_MAX;
            *value = (uint32_t)number;
        }
    }
    else
    {
        const bool other_target = contains(begin, end, "T.getArch()") && !contains(begin, end, "x86");
        const bool pragma = contains(begin, end, "AS_Pragma") && !contains(begin, end, "AS_GNU") && !contains(begin, end, "AS_C23");
        const bool omp = contains(begin, end, "getName()==\"omp\"") && !contains(begin, end, "AS_GNU") && !contains(begin, end, "AS_C23");
        valid = other_target || pragma || omp;
        *value = 0;
    }
    return valid;
}

static Row* row_of(const char* name, size_t length)
{
    Row* result = NULL;
    for (uint32_t i = 0; result == NULL && i < row_count; i += 1)
    {
        result = strlen(rows[i].name) == length && memcmp(rows[i].name, name, length) == 0 ? rows + i : NULL;
    }
    if (result == NULL && row_count < ROWS && length < sizeof(rows[0].name))
    {
        result = rows + row_count;
        row_count += 1;
        memset(result, 0, sizeof(*result));
        memcpy(result->name, name, length);
    }
    return result;
}

// Each list's lines: `.Case("name", VALUE)`, VALUE to the line's last `)`.
static bool read_list(const char* text, size_t text_length, const List* list)
{
    char opening[128];
    char closing[128];
    snprintf(opening, sizeof(opening), "#if defined(%s)\n", list->macro);
    snprintf(closing, sizeof(closing), "#endif // %s\n", list->macro);
    const char* end_of_text = text + text_length;
    const char* begin = strstr(text, opening);
    const char* end = begin != NULL ? strstr(begin, closing) : NULL;
    bool valid = begin != NULL && end != NULL;
    const char* line = valid ? begin + strlen(opening) : end;
    while (valid && line < end)
    {
        const char* next = memchr(line, '\n', (size_t)(end_of_text - line));
        valid = next != NULL && next <= end && strncmp(line, ".Case(\"", 7) == 0 && next - line > 10 && next[-1] == ')';
        const char* name = valid ? line + 7 : NULL;
        const char* quote = valid ? memchr(name, '"', (size_t)(next - name)) : NULL;
        valid = quote != NULL && quote[1] == ',' && quote[2] == ' ';
        uint32_t value = 0;
        valid = valid && evaluate(quote + 3, next - 1, &value);
        Row* row = valid ? row_of(name, (size_t)(quote - name)) : NULL;
        valid = row != NULL;
        if (valid && list->flag == 0)
        {
            row->strings = value;
        }
        else if (valid && value != 0)
        {
            row->flags |= list->flag;
        }
        line = valid ? next + 1 : end;
    }
    return valid;
}

static int row_order(const void* a, const void* b)
{
    return strcmp(((const Row*)a)->name, ((const Row*)b)->name);
}

static bool write_table(FILE* output, const char* source)
{
    bool valid = fprintf(output,
                         "// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception\n"
                         "// Generated by tools/generate_attribute_arguments.c; do not edit. Derived LLVM data, part of the LLVM Project, under the\n"
                         "// Apache License v2.0 with LLVM Exceptions (LICENSES/LLVM.txt): how clang's parser reads each attribute's arguments, from\n"
                         "// clang-tblgen --gen-clang-attr-parser-string-switches of\n"
                         "// %s.\n"
                         "// BUSTER_CHECK_ARGUMENTS rows: a name without a keyword's underscores, the lists that name it (BUSTER_CHECK_ARGUMENTS_*,\n"
                         "// c_check_attribute.c) and the arguments its parser reads as string literals, by place.\n",
                         source) > 0;
    for (uint32_t i = 0; valid && i < row_count; i += 1)
    {
        valid = fprintf(output, "BUSTER_CHECK_ARGUMENTS(\"%s\", 0x%xu, 0x%xu)\n", rows[i].name, rows[i].flags, rows[i].strings) > 0;
    }
    return valid && fflush(output) == 0;
}

static bool read_switches(const char* text, size_t length)
{
    bool valid = true;
    for (uint32_t l = 0; valid && l < sizeof(lists) / sizeof(lists[0]); l += 1)
    {
        valid = read_list(text, length, lists + l);
    }
    qsort(rows, row_count, sizeof(rows[0]), row_order);
    return valid;
}

static bool self_test(void)
{
    static const char sample[] =
        "#if defined(CLANG_ATTR_IDENTIFIER_ARG_LIST)\n.Case(\"format\", true)\n#endif // CLANG_ATTR_IDENTIFIER_ARG_LIST\n"
        "#if defined(CLANG_ATTR_VARIADIC_IDENTIFIER_ARG_LIST)\n#endif // CLANG_ATTR_VARIADIC_IDENTIFIER_ARG_LIST\n"
        "#if defined(CLANG_ATTR_STRICT_IDENTIFIER_ARG_LIST)\n#endif // CLANG_ATTR_STRICT_IDENTIFIER_ARG_LIST\n"
        "#if defined(CLANG_ATTR_TYPE_ARG_LIST)\n.Case(\"preferred_type\", true)\n#endif // CLANG_ATTR_TYPE_ARG_LIST\n"
        "#if defined(CLANG_ATTR_ARG_CONTEXT_LIST)\n#endif // CLANG_ATTR_ARG_CONTEXT_LIST\n"
        "#if defined(CLANG_ATTR_PARSE_ARGS_IN_FUNCTION_SCOPE_LIST)\n.Case(\"enable_if\", 1)\n#endif // CLANG_ATTR_PARSE_ARGS_IN_FUNCTION_SCOPE_LIST\n"
        "#if defined(CLANG_ATTR_LATE_PARSED_LIST)\n#endif // CLANG_ATTR_LATE_PARSED_LIST\n"
        "#if defined(CLANG_ATTR_LATE_PARSED_EXPERIMENTAL_EXT_LIST)\n#endif // CLANG_ATTR_LATE_PARSED_EXPERIMENTAL_EXT_LIST\n"
        "#if defined(CLANG_ATTR_STRING_LITERAL_ARG_LIST)\n.Case(\"deprecated\", 3)\n.Case(\"enable_if\", 2)\n"
        ".Case(\"interrupt\", (Syntax==AttributeCommonInfo::AS_GNU && !ScopeName && (T.getArch() == llvm::Triple::arm)) ? 1 : 0)\n"
        "#endif // CLANG_ATTR_STRING_LITERAL_ARG_LIST\n";
    row_count = 0;
    bool valid = read_switches(sample, sizeof(sample) - 1) && row_count == 5;
    valid = valid && strcmp(rows[0].name, "deprecated") == 0 && rows[0].strings == 3 && rows[0].flags == 0;
    valid = valid && strcmp(rows[1].name, "enable_if") == 0 && rows[1].strings == 2 && rows[1].flags == ARGUMENTS_FUNCTION_SCOPE;
    valid = valid && strcmp(rows[2].name, "format") == 0 && rows[2].flags == ARGUMENTS_IDENTIFIER;
    valid = valid && strcmp(rows[3].name, "interrupt") == 0 && rows[3].strings == 0;
    valid = valid && strcmp(rows[4].name, "preferred_type") == 0 && rows[4].flags == ARGUMENTS_TYPE;
    // A condition of an x86 target is no shape this knows.
    static const char unknown[] =
        "#if defined(CLANG_ATTR_STRING_LITERAL_ARG_LIST)\n.Case(\"x\", (T.getArch() == llvm::Triple::x86_64) ? 1 : 0)\n"
        "#endif // CLANG_ATTR_STRING_LITERAL_ARG_LIST\n";
    row_count = 0;
    valid = valid && !read_list(unknown, sizeof(unknown) - 1, lists + sizeof(lists) / sizeof(lists[0]) - 1);
    row_count = 0;
    return valid;
}

int main(int argc, char** argv)
{
    int status = 1;
    if (argc == 2 && strcmp(argv[1], "--self-test") == 0)
    {
        const bool valid = self_test();
        fprintf(stderr, "generate_attribute_arguments: self-test %s\n", valid ? "passed" : "failed");
        status = valid ? 0 : 1;
    }
    else if (argc == 3)
    {
        size_t length = 0;
        char* text = read_all(argv[1], &length);
        const bool valid = text != NULL && read_switches(text, length) && write_table(stdout, argv[2]);
        if (!valid)
        {
            fprintf(stderr, "generate_attribute_arguments: failed to read %s or write the table\n", argv[1]);
        }
        free(text);
        status = valid ? 0 : 1;
    }
    else
    {
        fprintf(stderr, "usage: %s --self-test | %s switches.inc \"SOURCE DESCRIPTION\"\n", argv[0], argv[0]);
    }
    return status;
}
