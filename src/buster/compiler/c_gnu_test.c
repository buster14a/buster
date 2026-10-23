// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// test-gnu: retained GNU syntax from original C fixtures, by both parser paths and both metadata collectors. The
// expected names, argument spellings and group memberships are independent of the collector. Guards remove the
// parser's padding from every input column and put every output array against an inaccessible page. Capacity and
// malformed-slice checks require empty results; arena-build failures must restore the caller's prior allocation.
// main.c includes this file after c_parser_test.c, whose file writer and optional Clang oracle are reused.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

typedef struct GnuTest GnuTest;
struct GnuTest
{
    const char* fixture;
    uint32_t checks;
    uint32_t failures;
};

static void gnu_test_expect(GnuTest* test, bool condition, const char* detail)
{
    test->checks += 1;
    if (!condition)
    {
        if (test->failures < 30)
        {
            printf("gnu: %s: %s\n", test->fixture, detail);
        }
        test->failures += 1;
    }
}

typedef struct GnuTestAttribute GnuTestAttribute;
struct GnuTestAttribute
{
    const char* name;
    // Null means a bare name; an empty string means an explicitly empty argument list.
    const char* arguments;
    uint32_t group;
};

typedef struct GnuTestFixture GnuTestFixture;
struct GnuTestFixture
{
    const char* name;
    const char* source;
    const char* standard;
    uint32_t groups;
    uint32_t attribute_count;
    GnuTestAttribute attributes[8];
    uint32_t label_count;
    const char* labels[4];
    bool arena_limits;
};

static const GnuTestFixture gnu_test_fixtures[] =
{
    {
        .name = "empty translation unit",
        .source = "",
    },
    {
        .name = "ordinary syntax",
        .source = "int x; int f(int x) { return x + 1; }\n",
    },
    {
        .name = "empty attribute lists",
        .source = "int a __attribute__(()); int b __attribute__((,,));\n",
        .groups = 2,
    },
    {
        .name = "bare, unknown and empty arguments",
        .source = "int a __attribute__((unused, mystery(), __aligned__(8),));\n",
        .groups = 1,
        .attribute_count = 3,
        .attributes =
        {
            { .name = "unused", .group = 0 },
            { .name = "mystery", .arguments = "", .group = 0 },
            { .name = "__aligned__", .arguments = "8", .group = 0 },
        },
    },
    {
        .name = "separate declaration groups",
        .source = "__attribute__((used)) int x __attribute__((section(\".data.x\"))) __attribute__((aligned(16)));\n",
        .groups = 3,
        .attribute_count = 3,
        .attributes =
        {
            { .name = "used", .group = 0 },
            { .name = "section", .arguments = "\".data.x\"", .group = 1 },
            { .name = "aligned", .arguments = "16", .group = 2 },
        },
    },
    {
        .name = "typedef, record, member and parameter",
        .source = "typedef int __attribute__((mode(QI))) Byte; struct __attribute__((packed)) Record { int x __attribute__((aligned(2))); }; void f(int x __attribute__((unused)));\n",
        .groups = 4,
        .attribute_count = 4,
        .attributes =
        {
            { .name = "mode", .arguments = "QI", .group = 0 },
            { .name = "packed", .group = 1 },
            { .name = "aligned", .arguments = "2", .group = 2 },
            { .name = "unused", .group = 3 },
        },
    },
    {
        .name = "pointer and declaration specifier",
        .source = "int * __attribute__((aligned(8))) p; int __attribute__((aligned(16))) x;\n",
        .groups = 2,
        .attribute_count = 2,
        .attributes =
        {
            { .name = "aligned", .arguments = "8", .group = 0 },
            { .name = "aligned", .arguments = "16", .group = 1 },
        },
    },
    {
        .name = "asm labels and assembly statements",
        .source = "int f(void) __asm__(\"re\" \"named\") __attribute__((nothrow)); extern int data __asm__(\"target\"); asm(\"\"); void g(int x) { asm volatile(\"\" : : \"r\"(x) : \"memory\"); }\n",
        .groups = 1,
        .attribute_count = 1,
        .attributes =
        {
            { .name = "nothrow", .group = 0 },
        },
        .label_count = 2,
        .labels =
        {
            "\"re\" \"named\"",
            "\"target\"",
        },
        .arena_limits = true,
    },
    {
        .name = "nested groups retain contiguous outer attributes",
        .source = "int x __attribute__((aligned(sizeof(int __attribute__((mode(QI))))), unused));\n",
        .groups = 2,
        .attribute_count = 3,
        .attributes =
        {
            { .name = "aligned", .arguments = "sizeof ( int __attribute__ ( ( mode ( QI ) ) ) )", .group = 0 },
            { .name = "unused", .group = 0 },
            { .name = "mode", .arguments = "QI", .group = 1 },
        },
    },
    {
        .name = "attribute in type operand",
        .source = "int f(void) { return sizeof(int __attribute__((aligned(8)))); }\n",
        .groups = 1,
        .attribute_count = 1,
        .attributes =
        {
            { .name = "aligned", .arguments = "8", .group = 0 },
        },
    },
    {
        .name = "keywords used as attribute names",
        .source = "int x __attribute__((__attribute__, __asm__, const));\n",
        .groups = 1,
        .attribute_count = 3,
        .attributes =
        {
            { .name = "__attribute__", .group = 0 },
            { .name = "__asm__", .group = 0 },
            { .name = "const", .group = 0 },
        },
    },
    {
        .name = "attribute from macro expansion",
        .source = "#define ATTR(n) __attribute__((n))\nATTR(used) int v;\n",
        .groups = 1,
        .attribute_count = 1,
        .attributes =
        {
            { .name = "used", .group = 0 },
        },
    },
    {
        .name = "candidate keywords with argument groups",
        .source = "int x __attribute__((__attribute__(sizeof(int __attribute__((unused)))), __asm__(\"ignored\"), const));\n",
        .groups = 2,
        .attribute_count = 4,
        .attributes =
        {
            { .name = "__attribute__", .arguments = "sizeof ( int __attribute__ ( ( unused ) ) )", .group = 0 },
            { .name = "__asm__", .arguments = "\"ignored\"", .group = 0 },
            { .name = "const", .group = 0 },
            { .name = "unused", .group = 1 },
        },
    },
    {
        .name = "label and statement attributes",
        .source = "void f(void) { L: __attribute__((cold)) __attribute__((unused)) f(); if (1) __attribute__((noinline)) f(); }\n",
        .groups = 3,
        .attribute_count = 3,
        .attributes =
        {
            { .name = "cold", .group = 0 },
            { .name = "unused", .group = 1 },
            { .name = "noinline", .group = 2 },
        },
    },
    {
        .name = "C23 attributes remain separate",
        .source = "[[gnu::unused]] int x; int y __attribute__((unused));\n",
        .standard = "c23",
        .groups = 1,
        .attribute_count = 1,
        .attributes =
        {
            { .name = "unused", .group = 0 },
        },
    },
    {
        .name = "local register asm declarator",
        .source = "int f(void) { register int x __asm__(\"r12\") = 0; __asm__(\"\" : \"+r\"(x)); return x; }\n",
        .label_count = 1,
        .labels =
        {
            "\"r12\"",
        },
    },
};

static bool gnu_test_zero(const GnuSyntax* syntax)
{
    return syntax->group_count == 0 && syntax->attribute_count == 0 && syntax->asm_label_count == 0;
}

static bool gnu_test_empty(const GnuSyntax* syntax)
{
    return gnu_test_zero(syntax) && syntax->groups == 0 && syntax->attributes == 0 && syntax->asm_labels == 0 &&
           syntax->group_capacity == 0 && syntax->attribute_capacity == 0 && syntax->asm_label_capacity == 0;
}

// Compare fields, not padding, and ignore ownership/capacity differences between a build and caller-owned output.
static bool gnu_test_equal(const GnuSyntax* left, const GnuSyntax* right)
{
    bool result = left->group_count == right->group_count && left->attribute_count == right->attribute_count && left->asm_label_count == right->asm_label_count;
    for (uint32_t i = 0; result && i < left->group_count; i += 1)
    {
        const GnuAttributeGroup* a = &left->groups[i];
        const GnuAttributeGroup* b = &right->groups[i];
        result = a->keyword == b->keyword && a->close == b->close && a->parent == b->parent && a->attribute_begin == b->attribute_begin &&
                 a->attribute_count == b->attribute_count && a->role == b->role;
    }
    for (uint32_t i = 0; result && i < left->attribute_count; i += 1)
    {
        const GnuAttribute* a = &left->attributes[i];
        const GnuAttribute* b = &right->attributes[i];
        result = a->name == b->name && a->argument_begin == b->argument_begin && a->argument_end == b->argument_end && a->group == b->group;
    }
    for (uint32_t i = 0; result && i < left->asm_label_count; i += 1)
    {
        const GnuAsmLabel* a = &left->asm_labels[i];
        const GnuAsmLabel* b = &right->asm_labels[i];
        result = a->keyword == b->keyword && a->string_begin == b->string_begin && a->string_end == b->string_end && a->parent == b->parent;
    }
    return result;
}

// Fixtures have short tokens; spelling each through the preprocessor also checks macro-expanded source identities.
static bool gnu_test_slice_is(Preprocessor* pp, const ParseTokens* tokens, uint32_t begin, uint32_t end, const char* expected)
{
    bool result = begin <= end && end <= tokens->count;
    char text[1024];
    uint32_t length = 0;
    for (uint32_t i = begin; result && i < end; i += 1)
    {
        const uint32_t source = tokens->sources[i];
        uint8_t buffer[1024];
        const uint8_t* spelling;
        const uint32_t bytes = pp_spelling(pp, pp->output.kinds[source], pp->output.values[source], pp->output.locations[source], buffer, &spelling);
        const uint32_t space = i != begin;
        result = bytes + space < sizeof(text) - length;
        if (result)
        {
            if (space != 0)
            {
                text[length++] = ' ';
            }
            memcpy(text + length, spelling, bytes);
            length += bytes;
        }
    }
    text[length] = 0;
    return result && strcmp(text, expected) == 0;
}

static void gnu_test_expected(GnuTest* test, const GnuTestFixture* fixture, Preprocessor* pp, const Parser* parser, const GnuSyntax* syntax)
{
    const bool counts = syntax->group_count == fixture->groups && syntax->attribute_count == fixture->attribute_count && syntax->asm_label_count == fixture->label_count;
    gnu_test_expect(test, counts, "exact group, attribute and asm-label counts");
    for (uint32_t g = 0; counts && g < syntax->group_count; g += 1)
    {
        const GnuAttributeGroup* group = &syntax->groups[g];
        uint32_t expected_count = 0;
        uint32_t expected_begin = 0;
        for (uint32_t a = 0; a < fixture->attribute_count; a += 1)
        {
            expected_count += fixture->attributes[a].group == g;
            expected_begin += fixture->attributes[a].group < g;
        }
        gnu_test_expect(test, group->attribute_begin == expected_begin && group->attribute_count == expected_count, "group attributes are contiguous and ordered");
        const bool bounds = group->keyword < group->close && group->close < parser->tokens.count;
        gnu_test_expect(test, bounds, "attribute group bounds");
        if (bounds)
        {
            gnu_test_expect(test, parser->tokens.kinds[group->keyword] == PARSE_KIND_ATTRIBUTE && parser->tokens.kinds[group->close] == PARSE_KIND_R_PAREN,
                            "attribute keyword and closing token");
            gnu_test_expect(test, group->parent == parser->tree.parents[group->close] && group->role == parser->tree.roles[group->close], "syntactic attachment retained");
        }
        gnu_test_expect(test, g == 0 || syntax->groups[g - 1].keyword < group->keyword, "groups are in source order");
    }
    for (uint32_t a = 0; counts && a < syntax->attribute_count; a += 1)
    {
        const GnuAttribute* attribute = &syntax->attributes[a];
        const GnuTestAttribute* expected = &fixture->attributes[a];
        const bool name = attribute->name < parser->tokens.count && gnu_test_slice_is(pp, &parser->tokens, attribute->name, attribute->name + 1, expected->name);
        gnu_test_expect(test, name && attribute->group == expected->group, "attribute name and owning group");
        const bool arguments = expected->arguments == 0 ? attribute->argument_begin == BUSTER_PARSE_NONE && attribute->argument_end == BUSTER_PARSE_NONE :
                               gnu_test_slice_is(pp, &parser->tokens, attribute->argument_begin, attribute->argument_end, expected->arguments);
        gnu_test_expect(test, arguments, "exact argument slice, distinguishing absent and empty lists");
    }
    for (uint32_t a = 0; counts && a < syntax->asm_label_count; a += 1)
    {
        const GnuAsmLabel* label = &syntax->asm_labels[a];
        const bool bounds = label->keyword < label->string_begin && label->string_begin < label->string_end && label->string_end < parser->tokens.count;
        gnu_test_expect(test, bounds, "asm label bounds");
        if (bounds)
        {
            gnu_test_expect(test, gnu_test_slice_is(pp, &parser->tokens, label->string_begin, label->string_end, fixture->labels[a]), "asm string spelling and concatenation");
            gnu_test_expect(test, label->parent == parser->tree.parents[label->string_end], "asm syntactic attachment retained");
        }
    }
}

typedef struct GnuTestGuard GnuTestGuard;
struct GnuTestGuard
{
    uint8_t* mapping;
    uint8_t* data;
    uint64_t mapping_bytes;
};

// Both sides are inaccessible, and the logical end is exactly the upper page boundary, with no readable padding.
static GnuTestGuard gnu_test_guard(uint64_t bytes)
{
    GnuTestGuard result = { 0 };
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    const uint64_t rounded = bytes == 0 ? page : (bytes + page - 1) & ~(page - 1);
    const uint64_t total = rounded + 2 * page;
    uint8_t* mapping = mmap(0, total, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mapping != MAP_FAILED)
    {
        if (mprotect(mapping + page, rounded, PROT_READ | PROT_WRITE) == 0)
        {
            result.mapping = mapping;
            result.mapping_bytes = total;
            result.data = mapping + page + rounded - bytes;
        }
        else
        {
            munmap(mapping, total);
        }
    }
    return result;
}

static void gnu_test_guard_release(GnuTestGuard* guard)
{
    if (guard->mapping != 0)
    {
        munmap(guard->mapping, guard->mapping_bytes);
    }
    *guard = (GnuTestGuard){ 0 };
}

static void gnu_test_candidates(GnuTest* test)
{
    test->fixture = "candidate masks and legal batch partitions";
    for (uint32_t count = 0; count <= 64; count += 1)
    {
        GnuTestGuard guard = gnu_test_guard(count);
        gnu_test_expect(test, guard.data != 0, "guard allocation");
        if (guard.data != 0)
        {
            for (uint32_t first = 0; first <= count; first += 1)
            {
                memset(guard.data, PARSE_KIND_IDENTIFIER, count);
                uint64_t expected = 0;
                if (first < count)
                {
                    guard.data[first] = PARSE_KIND_ATTRIBUTE;
                    expected |= 1ull << first;
                }
                if (first + 1 < count)
                {
                    guard.data[first + 1] = PARSE_KIND_ASM;
                    expected |= 1ull << (first + 1);
                }
                gnu_test_expect(test, gnu_syntax_candidates(guard.data, count) == expected, "masked vector candidate positions");
                gnu_test_expect(test, gnu_syntax_candidates_scalar(guard.data, count) == expected, "scalar candidate positions");
                static const uint32_t widths[] = { 1, 3, 16, 31, 64 };
                for (uint32_t w = 0; w < BUSTER_ARRAY_LENGTH(widths); w += 1)
                {
                    uint64_t joined = 0;
                    for (uint32_t begin = 0; begin < count; begin += widths[w])
                    {
                        const uint32_t length = count - begin < widths[w] ? count - begin : widths[w];
                        joined |= gnu_syntax_candidates(guard.data + begin, length) << begin;
                    }
                    gnu_test_expect(test, joined == expected, "candidate mask independent of legal batch partition");
                }
            }
        }
        gnu_test_guard_release(&guard);
    }
}

static void gnu_test_guarded(GnuTest* test, const Parser* parser, const GnuSyntax* expected)
{
    const uint32_t count = parser->tokens.count;
    GnuTestGuard guards[] =
    {
        gnu_test_guard(count),
        gnu_test_guard(count * sizeof(uint32_t)),
        gnu_test_guard(count),
        gnu_test_guard(count * sizeof(uint32_t)),
        gnu_test_guard(expected->group_count * sizeof(GnuAttributeGroup)),
        gnu_test_guard(expected->attribute_count * sizeof(GnuAttribute)),
        gnu_test_guard(expected->asm_label_count * sizeof(GnuAsmLabel)),
    };
    bool ready = true;
    for (uint32_t g = 0; g < BUSTER_ARRAY_LENGTH(guards); g += 1)
    {
        ready &= guards[g].data != 0;
    }
    gnu_test_expect(test, ready, "guarded input and output allocation");
    if (ready)
    {
        if (count != 0)
        {
            memcpy(guards[0].data, parser->tokens.kinds, count);
            memcpy(guards[1].data, parser->tokens.names, count * sizeof(uint32_t));
            memcpy(guards[2].data, parser->tree.roles, count);
            memcpy(guards[3].data, parser->tree.parents, count * sizeof(uint32_t));
        }
        ParseTokens tokens = parser->tokens;
        ParseTree tree = parser->tree;
        tokens.kinds = guards[0].data;
        tokens.names = (uint32_t*)guards[1].data;
        tree.roles = guards[2].data;
        tree.parents = (uint32_t*)guards[3].data;
        GnuSyntax output =
        {
            .groups = (GnuAttributeGroup*)guards[4].data,
            .attributes = (GnuAttribute*)guards[5].data,
            .asm_labels = (GnuAsmLabel*)guards[6].data,
            .group_capacity = expected->group_count,
            .attribute_capacity = expected->attribute_count,
            .asm_label_capacity = expected->asm_label_count,
        };
        for (uint32_t scalar = 0; scalar < 2; scalar += 1)
        {
            GnuSyntaxStatus status = gnu_syntax_collect(&tokens, &tree, &output, scalar != 0, false);
            gnu_test_expect(test, status == GNU_SYNTAX_OK && gnu_test_equal(&output, expected), "guarded columns and exact output capacities");
            for (uint32_t column = 0; column < 3; column += 1)
            {
                GnuSyntax short_output = output;
                uint32_t* capacity = column == 0 ? &short_output.group_capacity : column == 1 ? &short_output.attribute_capacity : &short_output.asm_label_capacity;
                if (*capacity != 0)
                {
                    *capacity -= 1;
                    status = gnu_syntax_collect(&tokens, &tree, &short_output, scalar != 0, false);
                    gnu_test_expect(test, status == GNU_SYNTAX_CAPACITY && gnu_test_zero(&short_output), "short output capacity leaves no valid rows");
                }
            }
            GnuSyntax measured = { .group_capacity = UINT32_MAX, .attribute_capacity = UINT32_MAX, .asm_label_capacity = UINT32_MAX };
            status = gnu_syntax_collect(&tokens, &tree, &measured, scalar != 0, true);
            gnu_test_expect(test, status == GNU_SYNTAX_OK && measured.group_count == expected->group_count && measured.attribute_count == expected->attribute_count &&
                            measured.asm_label_count == expected->asm_label_count, "measurement ignores output arrays and capacities");
            GnuSyntax absent = { .group_capacity = 1 };
            status = gnu_syntax_collect(&tokens, &tree, &absent, scalar != 0, false);
            gnu_test_expect(test, status == GNU_SYNTAX_CAPACITY && gnu_test_zero(&absent), "missing output array rejected");
            if (expected->group_count != 0)
            {
                const uint32_t open = expected->groups[0].keyword + 1;
                const uint32_t saved = tree.parents[open];
                tree.parents[open] = count;
                status = gnu_syntax_collect(&tokens, &tree, &output, scalar != 0, false);
                gnu_test_expect(test, status == GNU_SYNTAX_INVALID_TREE && gnu_test_zero(&output), "out-of-bounds group closer rejected");
                tree.parents[open] = saved;
            }
            if (expected->attribute_count != 0)
            {
                const uint32_t name = expected->attributes[0].name;
                const uint32_t saved = tokens.names[name];
                tokens.names[name] = 0;
                status = gnu_syntax_collect(&tokens, &tree, &output, scalar != 0, false);
                gnu_test_expect(test, status == GNU_SYNTAX_INVALID_TREE && gnu_test_zero(&output), "missing attribute name rejected");
                tokens.names[name] = saved;
            }
            if (expected->asm_label_count != 0)
            {
                const uint32_t string = expected->asm_labels[0].string_begin;
                const uint8_t saved = tokens.kinds[string];
                tokens.kinds[string] = PARSE_KIND_NUMBER;
                status = gnu_syntax_collect(&tokens, &tree, &output, scalar != 0, false);
                gnu_test_expect(test, status == GNU_SYNTAX_INVALID_TREE && gnu_test_zero(&output), "non-string asm label rejected");
                tokens.kinds[string] = saved;
            }
            status = gnu_syntax_collect(&tokens, &tree, &output, scalar != 0, false);
            gnu_test_expect(test, status == GNU_SYNTAX_OK && gnu_test_equal(&output, expected), "collection recovers after failed calls");
        }
    }
    for (uint32_t g = 0; g < BUSTER_ARRAY_LENGTH(guards); g += 1)
    {
        gnu_test_guard_release(&guards[g]);
    }
}

static void gnu_test_build_limits(GnuTest* test, const Parser* parser, const GnuSyntax* expected)
{
    _Alignas(64) uint8_t storage[4096];
    memset(storage, 0x5a, sizeof(storage));
    const uint64_t mark = 13;
    Arena full = { .base = storage, .length = mark, .capacity = sizeof(storage) };
    GnuSyntax built;
    GnuSyntaxStatus status = gnu_syntax_build(&parser->tokens, &parser->tree, &full, &built, false);
    gnu_test_expect(test, status == GNU_SYNTAX_OK && gnu_test_equal(&built, expected), "arena build in existing caller allocation");
    const uint64_t required = full.length;
    for (uint32_t scalar = 0; scalar < 2; scalar += 1)
    {
        for (uint64_t capacity = mark; capacity < required; capacity += 1)
        {
            Arena limited = { .base = storage, .length = mark, .capacity = capacity };
            status = gnu_syntax_build(&parser->tokens, &parser->tree, &limited, &built, scalar != 0);
            gnu_test_expect(test, status == GNU_SYNTAX_MEMORY && limited.length == mark && gnu_test_empty(&built), "every insufficient arena capacity rolls back");
            bool preserved = true;
            for (uint32_t i = 0; i < mark; i += 1)
            {
                preserved &= storage[i] == 0x5a;
            }
            gnu_test_expect(test, preserved, "caller bytes before arena checkpoint preserved");
        }
        Arena exact = { .base = storage, .length = mark, .capacity = required };
        status = gnu_syntax_build(&parser->tokens, &parser->tree, &exact, &built, scalar != 0);
        gnu_test_expect(test, status == GNU_SYNTAX_OK && exact.length == required && gnu_test_equal(&built, expected), "exact arena capacity succeeds");
        ParseTokens invalid = parser->tokens;
        invalid.kinds = 0;
        Arena unchanged = { .base = storage, .length = mark, .capacity = sizeof(storage) };
        status = gnu_syntax_build(&invalid, &parser->tree, &unchanged, &built, scalar != 0);
        gnu_test_expect(test, status == GNU_SYNTAX_INVALID_TREE && unchanged.length == mark && gnu_test_empty(&built), "invalid input rolls back build and result");
    }
}

static void gnu_test_fixture(GnuTest* test, const GnuTestFixture* fixture, const char* path, const char* clang)
{
    test->fixture = fixture->name;
    const ParseTestSnippet snippet = { .source = fixture->source, .error = "", .std = fixture->standard };
    const PPOptions options = parse_test_snippet_options(&snippet);
    static InternTable table;
    static Preprocessor pp;
    static Parser parsers[2];
    Arena arenas[2] = { arena_reserve(1ull << 20), arena_reserve(1ull << 20) };
    GnuSyntax syntax[2] = { 0 };
    const bool prepared = parse_test_write(path, fixture->source, strlen(fixture->source)) && intern_initialize(&table) && pp_initialize(&pp, &options, &table) && pp_run(&pp, path);
    gnu_test_expect(test, prepared, "fixture preprocessing");
    for (uint32_t scalar = 0; prepared && scalar < 2; scalar += 1)
    {
        Parser* parser = &parsers[scalar];
        const ParseOptions parse_options = { .scalar = scalar != 0, .extension_metadata = true };
        const bool parsed = parse_initialize(parser, &parse_options, options.language) && parse_translation_unit(parser, &pp) && parser->errors == 0;
        gnu_test_expect(test, parsed, "fixture parsing");
        if (parsed)
        {
            GnuSyntaxStatus status = gnu_syntax_build(&parser->tokens, &parser->tree, &arenas[scalar], &syntax[scalar], scalar != 0);
            gnu_test_expect(test, status == GNU_SYNTAX_OK, "metadata build succeeds");
            if (status == GNU_SYNTAX_OK)
            {
                gnu_test_expected(test, fixture, &pp, parser, &syntax[scalar]);
                gnu_test_expect(test, gnu_test_equal(&parser->gnu, &syntax[scalar]), "parser opt-in matches explicit metadata build");
                gnu_test_guarded(test, parser, &syntax[scalar]);
                if (fixture->arena_limits)
                {
                    gnu_test_build_limits(test, parser, &syntax[scalar]);
                }
            }
        }
    }
    if (prepared)
    {
        gnu_test_expect(test, gnu_test_equal(&syntax[0], &syntax[1]), "scalar and vector parser metadata agree");
    }
    if (prepared && clang != 0)
    {
        char standard[32];
        snprintf(standard, sizeof(standard), "-std=%s", fixture->standard != 0 ? fixture->standard : "gnu17");
        const char* arguments[] = { standard };
        char* paths[] = { (char*)path };
        ParseTestClang theirs;
        const bool accepted = parse_test_clang(clang, arguments, 1, paths, 1, &theirs) && !theirs.rejected;
        gnu_test_expect(test, accepted, "original fixture accepted by Clang");
    }
    for (uint32_t scalar = 0; scalar < 2; scalar += 1)
    {
        parse_release(&parsers[scalar]);
        gnu_test_expect(test, gnu_test_empty(&parsers[scalar].gnu), "parser release clears retained metadata");
        arena_release(&arenas[scalar]);
    }
    pp_release(&pp);
    intern_release(&table);
    remove(path);
}

static void gnu_test_boundaries(GnuTest* test, const char* path)
{
    static const char body[] = "int x __asm__(\"target\") __attribute__((used, aligned(8)));\n";
    char source[512];
    for (uint32_t shift = 0; shift < 128; shift += 1)
    {
        for (uint32_t i = 0; i < shift; i += 1)
        {
            source[i * 2] = ';';
            source[i * 2 + 1] = ' ';
        }
        memcpy(source + 2 * shift, body, sizeof(body));
        const GnuTestFixture fixture =
        {
            .name = "metadata groups across every 64-token boundary",
            .source = source,
            .groups = 1,
            .attribute_count = 2,
            .attributes =
            {
                { .name = "used", .group = 0 },
                { .name = "aligned", .arguments = "8", .group = 0 },
            },
            .label_count = 1,
            .labels =
            {
                "\"target\"",
            },
        };
        gnu_test_fixture(test, &fixture, path, 0);
    }
}

static void gnu_test_invalid_inputs(GnuTest* test)
{
    test->fixture = "empty and invalid inputs";
    const ParseTree tree = { 0 };
    for (uint32_t scalar = 0; scalar < 2; scalar += 1)
    {
        ParseTokens tokens = { 0 };
        GnuSyntax syntax = { .group_count = 17, .attribute_count = 19, .asm_label_count = 23 };
        GnuSyntaxStatus status = gnu_syntax_collect(&tokens, &tree, &syntax, scalar != 0, false);
        gnu_test_expect(test, status == GNU_SYNTAX_OK && gnu_test_empty(&syntax), "empty input needs no arrays");
        Arena arena = { 0 };
        status = gnu_syntax_build(&tokens, &tree, &arena, &syntax, scalar != 0);
        gnu_test_expect(test, status == GNU_SYNTAX_OK && gnu_test_empty(&syntax) && arena.length == 0, "empty build needs no arena allocation");
        tokens.count = 1;
        status = gnu_syntax_collect(&tokens, &tree, &syntax, scalar != 0, true);
        gnu_test_expect(test, status == GNU_SYNTAX_INVALID_TREE && gnu_test_zero(&syntax), "missing nonempty token columns rejected");
        tokens.count = BUSTER_PARSE_MAX_TOKENS + 1;
        status = gnu_syntax_collect(&tokens, &tree, &syntax, scalar != 0, true);
        gnu_test_expect(test, status == GNU_SYNTAX_INVALID_TREE && gnu_test_zero(&syntax), "oversized token count rejected before reading");
    }
}

static void gnu_test_parser_lifetime(GnuTest* test, const char* path)
{
    test->fixture = "parser metadata lifetime and opt-in";
    const PPOptions options = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true };
    static const char source[] = "int x __attribute__((unused));\n";
    static const char invalid[] = "int x __attribute__((unused)); int y = ;\n";
    for (uint32_t scalar = 0; scalar < 2; scalar += 1)
    {
        static InternTable table;
        static Preprocessor pp;
        static Parser parser;
        const ParseOptions parse_options = { .scalar = scalar != 0, .tree_only = true, .extension_metadata = true };
        bool prepared = parse_test_write(path, source, sizeof(source) - 1) && intern_initialize(&table) && pp_initialize(&pp, &options, &table) &&
                        pp_run(&pp, path) && parse_initialize(&parser, &parse_options, options.language);
        gnu_test_expect(test, prepared, "lifetime fixture initialization");
        if (prepared)
        {
            bool parsed = parse_translation_unit(&parser, &pp);
            gnu_test_expect(test, parsed && parser.gnu.group_count == 1 && parser.gnu.attribute_count == 1 && parser.postorder == 0 && parser.sizes == 0,
                            "tree-only parse retains metadata without a postorder");
            parser.options.extension_metadata = false;
            parsed = parse_translation_unit(&parser, &pp);
            gnu_test_expect(test, parsed && gnu_test_empty(&parser.gnu) && parser.gnu_arena.base == 0, "ordinary syntax-only reparse clears metadata and its arena");
            parser.options.extension_metadata = true;
            parsed = parse_translation_unit(&parser, &pp);
            gnu_test_expect(test, parsed && parser.gnu.group_count == 1 && parser.gnu.attribute_count == 1, "metadata can be enabled again on a reused parser");
            pp_release(&pp);
            prepared = parse_test_write(path, invalid, sizeof(invalid) - 1) && pp_initialize(&pp, &options, &table) && pp_run(&pp, path);
            gnu_test_expect(test, prepared, "invalid lifetime fixture preprocessing");
            if (prepared)
            {
                parsed = parse_translation_unit(&parser, &pp);
                gnu_test_expect(test, !parsed && parser.errors != 0 && gnu_test_empty(&parser.gnu) && parser.gnu_arena.base == 0,
                                "failed reparse never exposes stale metadata");
            }
        }
        parse_release(&parser);
        pp_release(&pp);
        intern_release(&table);
    }
    remove(path);
}

static int run_gnu_tests(int argument_count, char** arguments)
{
    const bool valid = argument_count == 0 || ((argument_count == 1 || argument_count == 2) && strcmp(arguments[0], "--clang") == 0);
    const char* clang = argument_count == 2 ? arguments[1] : argument_count == 1 ? "clang" : 0;
    GnuTest test = { .fixture = "test setup" };
    char directory[4096];
    char path[4200];
    const char* temporary = getenv("TMPDIR");
    snprintf(directory, sizeof(directory), "%s/buster-test-gnu-XXXXXX", temporary != 0 ? temporary : "/tmp");
    if (!valid)
    {
        printf("usage: test-gnu [--clang [PATH]]\n");
        test.failures += 1;
    }
    else if (mkdtemp(directory) == 0)
    {
        printf("test-gnu: cannot create temporary directory\n");
        test.failures += 1;
    }
    else
    {
        snprintf(path, sizeof(path), "%s/fixture.c", directory);
        gnu_test_candidates(&test);
        gnu_test_invalid_inputs(&test);
        gnu_test_parser_lifetime(&test, path);
        for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(gnu_test_fixtures); f += 1)
        {
            gnu_test_fixture(&test, &gnu_test_fixtures[f], path, clang);
        }
        gnu_test_boundaries(&test, path);
        rmdir(directory);
    }
    printf("test-gnu: %u checks, %u failures%s\n", test.checks, test.failures, clang != 0 ? " (with Clang)" : "");
    return test.failures == 0 ? 0 : 1;
}
