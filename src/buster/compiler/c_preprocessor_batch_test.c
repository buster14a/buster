// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Macro batches through the real preprocessor and its scalar twin.
// Included by c_preprocessor_test.c after its fixture helpers. Expected spellings come from the generated source's
// simple macro definitions; stream comparisons also check flags, locations and complete expansion history.

static bool pp_test_macro_batches(const char* directory, bool with_clang)
{
    Scratch scratch = scratch_begin(0);
    const uint64_t capacity = 2 << 20;
    char* source = arena_push(scratch.arena, capacity, 1);
    char* expected = arena_push(scratch.arena, capacity, 1);
    size_t source_length = 0;
    size_t expected_length = 0;
    bool result = source != 0 && expected != 0;
    result = result && pp_test_append(source, capacity, &source_length,
        "#define M(x) x\n#define W(x) M(x)\n#define V(x,...) x __VA_ARGS__\n#define P(a,b) a | b\n");
    uint32_t calls = 0;
    // Each closing-parenthesis lane, with flat and nested arguments spanning up to three 64-token blocks. W forces
    // argument prescan and buffer contexts; V leaves commas in its variadic argument; newlines exercise flag changes.
    for (uint32_t count = 0; result && count <= 129; count += 1)
    {
        for (uint32_t shape = 0; result && shape < 4; shape += 1)
        {
            const char* start = shape == 0 ? "M(" : shape == 1 ? "W(" : shape == 2 ? "M((" : "V(";
            result = pp_test_append(source, capacity, &source_length, "%s", start) &&
                     pp_test_append(expected, capacity, &expected_length, "%s", shape == 2 ? "(" : "");
            for (uint32_t i = 0; result && i < count; i += 1)
            {
                result = pp_test_append(source, capacity, &source_length, "v%s", shape == 1 && i % 3 == 0 ? "\n" : " ") &&
                         pp_test_append(expected, capacity, &expected_length, "v ");
            }
            result = result && pp_test_append(source, capacity, &source_length, "%s\n", shape == 2 ? "))" : shape == 3 ? ", left, right)" : ")") &&
                     pp_test_append(expected, capacity, &expected_length, "%s\n", shape == 2 ? ")" : shape == 3 ? "left, right" : "");
            calls += 1;
        }
    }
    // Only parentheses protect a macro-argument comma; brackets and braces do not.
    result = result && pp_test_append(source, capacity, &source_length, "P({ a, b }) P([ c, d ])\n") &&
             pp_test_append(expected, capacity, &expected_length, "{ a | b } [ c | d ]\n");
    PPTestCase test =
    {
        .name = "macro batch boundaries",
        .source = source,
        .standard = "gnu17",
        .expected = expected,
    };
    result = result && pp_test_case(&test, directory, with_clang);
    // Runs on both sides of __VA_OPT__ exceed the 16-bit run-length field. Its omitted closing token must separate
    // its content from the following literal run when both are present; the second invocation leaves the content out.
    source_length = 0;
    expected_length = 0;
    result = result && pp_test_append(source, capacity, &source_length, "#define LONG(x,...) ");
    const uint32_t run = 65537;
    for (uint32_t part = 0; result && part < 3; part += 1)
    {
        result = pp_test_append(source, capacity, &source_length, "%s", part == 0 ? "" : part == 1 ? "x __VA_OPT__(" : ") ");
        const uint32_t tokens = part == 1 ? 65 : run;
        for (uint32_t i = 0; result && i < tokens; i += 1)
        {
            result = pp_test_append(source, capacity, &source_length, "v ");
        }
    }
    result = result && pp_test_append(source, capacity, &source_length, "\nLONG(z, 1)\nLONG(y)\n");
    for (uint32_t call = 0; result && call < 2; call += 1)
    {
        for (uint32_t part = 0; result && part < 3; part += 1)
        {
            if (part == 1)
            {
                result = pp_test_append(expected, capacity, &expected_length, "%s ", call == 0 ? "z" : "y");
            }
            const uint32_t tokens = part == 1 ? (call == 0 ? 65 : 0) : run;
            for (uint32_t i = 0; result && i < tokens; i += 1)
            {
                result = pp_test_append(expected, capacity, &expected_length, "v ");
            }
        }
    }
    test = (PPTestCase)
    {
        .name = "long literal macro batches",
        .source = source,
        .standard = "gnu17",
        .expected = expected,
    };
    result = result && pp_test_case(&test, directory, with_clang);
    scratch_end(scratch);
    printf("macro batches: %u boundary calls and long literal/optional runs: %s\n", calls + 2, result ? "agree" : "FAIL");
    return result;
}
