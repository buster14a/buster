// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Real-source binding handoff checks. Both source paths must retain the same declaration for every token, and
// independent occurrence maps pin the declarations selected by shadowing, restoration and redeclaration. Include
// after the CheckTest helpers in c_checker_test.c; check_test_bindings is called by the checker test command.
#ifndef BUSTER_C_CHECK_BINDING_TEST_INCLUDED
#define BUSTER_C_CHECK_BINDING_TEST_INCLUDED

static void check_test_binding_source(CheckTest* test, const char* source, const char* name, const uint32_t* expected, uint32_t expected_count, bool accepted, bool fallback)
{
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parsers[2];
    const PPOptions pp_options = {
        .language = {
            .standard = 2017,
            .gnu = true,
        },
        .no_standard_includes = true,
    };
    CheckTestTypes types[2] = { 0 };
    Arena storage[2] = { 0 };
    Arena scratch[2] = { 0 };
    CheckInput inputs[2] = { 0 };
    bool adapted[2] = { 0 };
    bool ready = intern_initialize(&identifiers) && pp_initialize(&pp, &pp_options, &identifiers);
    for (uint32_t path = 0; path < 2; path += 1)
    {
        const ParseOptions options = { .scalar = path == 0 };
        ready = ready && parse_initialize(&parsers[path], &options, pp_options.language) && check_test_types_open(&types[path]);
        storage[path] = arena_reserve(UINT64_C(1) << 24);
        scratch[path] = arena_reserve(UINT64_C(1) << 24);
        ready = ready && storage[path].base != 0 && scratch[path].base != 0;
    }
    ready = ready && pp_run_source(&pp, "checker-binding.c", (const uint8_t*)source, (uint32_t)strlen(source));
    check_test_expect(test, ready, "source binding fixture initialization");
    const IdentId identifier = ready ? intern_find(&identifiers, (const uint8_t*)name, (uint32_t)strlen(name)) : 0;
    for (uint32_t path = 0; ready && path < 2; path += 1)
    {
        const bool parsed = parse_translation_unit(&parsers[path], &pp) && parsers[path].errors == 0;
        check_test_expect(test, parsed, "source binding fixture must parse");
        if (parsed)
        {
            const uint64_t mark = scratch[path].length;
            adapted[path] = check_input(&inputs[path], &parsers[path], &pp, &types[path].table, &storage[path], &scratch[path]);
            check_test_expect(test, !inputs[path].resource && adapted[path] == accepted, "source binding fixture acceptance");
            check_test_expect(test, scratch[path].length == mark, "source binding fixture retained scratch");
            uint32_t occurrences[128];
            uint32_t count = 0;
            for (uint32_t token = 0; token < parsers[path].tokens.count; token += 1)
            {
                if (identifier != 0 && parsers[path].tokens.names[token] == identifier)
                {
                    if (count < BUSTER_ARRAY_LENGTH(occurrences))
                    {
                        occurrences[count] = token;
                    }
                    count += 1;
                }
            }
            check_test_expect(test, count == expected_count && count <= BUSTER_ARRAY_LENGTH(occurrences), "source binding occurrence count");
            for (uint32_t i = 0; count == expected_count && count <= BUSTER_ARRAY_LENGTH(occurrences) && i < count; i += 1)
            {
                const uint32_t declaration = expected[i] == BUSTER_CHECK_NONE ? BUSTER_CHECK_NONE : occurrences[expected[i]];
                check_test_expect(test, inputs[path].declarations[occurrences[i]] == declaration, "source use bound to the wrong declaration");
                if (declaration == BUSTER_CHECK_NONE)
                {
                    check_test_expect(test, inputs[path].declared_types[occurrences[i]] == 0, "unresolved source use acquired a type");
                    bool reported = false;
                    for (uint32_t report = 0; report < inputs[path].report_count; report += 1)
                    {
                        reported |= inputs[path].reports[report].token == occurrences[i] && inputs[path].reports[report].message == CHECK_INPUT_UNDECLARED;
                    }
                    check_test_expect(test, reported, "unresolved source use lost its diagnostic behind a later declaration failure");
                }
                else
                {
                    char description[256];
                    snprintf(description, sizeof(description), "source binding type at token %u: %.180s", occurrences[i], source);
                    check_test_expect(test, inputs[path].declared_types[occurrences[i]] == inputs[path].declared_types[declaration], description);
                }
            }
            if (path == 1 && fallback)
            {
                check_test_expect(test, inputs[path].binding_statistics.walked_uses != 0 && inputs[path].binding_statistics.fallback_uses != 0,
                                  "closed sibling scopes did not exercise binding fallback");
            }
        }
    }
    if (ready && inputs[0].declarations != 0 && inputs[1].declarations != 0)
    {
        const uint32_t count = parsers[0].tokens.count;
        check_test_expect(test, count == parsers[1].tokens.count && adapted[0] == adapted[1], "binding source paths disagree on parser or checker status");
        if (count == parsers[1].tokens.count)
        {
            for (uint32_t token = 0; token <= count; token += 1)
            {
                check_test_expect(test, inputs[0].declarations[token] == inputs[1].declarations[token], "complete source declaration columns differ");
                check_test_expect(test, inputs[0].declared_types[token] == inputs[1].declared_types[token], "complete source type columns differ");
            }
        }
        check_test_expect(test, inputs[0].binding_event_count == inputs[1].binding_event_count &&
                               inputs[0].binding_statistics.identifiers == inputs[1].binding_statistics.identifiers &&
                               inputs[0].binding_statistics.declarations == inputs[1].binding_statistics.declarations,
                          "source paths recorded different binding events");
        check_test_expect(test, inputs[0].report_count == inputs[1].report_count, "binding source diagnostic counts differ");
        for (uint32_t i = 0; inputs[0].report_count == inputs[1].report_count && i < inputs[0].report_count; i += 1)
        {
            check_test_expect(test, inputs[0].reports[i].token == inputs[1].reports[i].token && inputs[0].reports[i].message == inputs[1].reports[i].message,
                              "binding source diagnostic identities differ");
        }
    }
    for (uint32_t path = 0; path < 2; path += 1)
    {
        arena_release(&scratch[path]);
        arena_release(&storage[path]);
        check_test_types_close(&types[path]);
        parse_release(&parsers[path]);
    }
    pp_release(&pp);
    intern_release(&identifiers);
}

static void check_test_binding_failures(CheckTest* test)
{
    Arena storage = arena_reserve(1u << 16);
    Arena scratch = arena_reserve(1u << 16);
    CBindings bindings;
    bool ready = storage.base != 0 && scratch.base != 0 && c_bindings_initialize(&bindings, &storage, 4, 8);
    if (ready)
    {
        ready = c_bindings_declare(&bindings, C_BINDING_ORDINARY, 1, C_BINDING_OBJECT, 0, 0, 0) != 0 &&
                c_bindings_use(&bindings, C_BINDING_ORDINARY, 1, 1, 1) != 0 && c_bindings_restore(&bindings, 0, 2) &&
                c_bindings_use(&bindings, C_BINDING_ORDINARY, 1, 3, 3) != 0;
    }
    check_test_expect(test, ready, "binding handoff failure fixture allocation");
    static uint8_t roles[] = {
        PARSE_ROLE_LEAF_RIGHT,
        PARSE_ROLE_OPERAND,
        PARSE_ROLE_TERMINATOR,
        PARSE_ROLE_OPERAND,
    };
    static const Parser parser = {
        .tokens = { .count = 4 },
        .tree = { .roles = roles },
    };
    for (uint32_t path = 0; ready && path < 2; path += 1)
    {
        uint32_t declarations[4] = { 0 };
        TypeId types[] = {
            BUSTER_TYPE_INT,
            BUSTER_TYPE_LONG,
            0,
            BUSTER_TYPE_LONG,
        };
        CheckInputReport reports[4];
        CheckInput input = {
            .declarations = declarations,
            .declared_types = types,
            .reports = reports,
            .report_capacity = BUSTER_ARRAY_LENGTH(reports),
        };
        CheckInputReader reader = {
            .output = &input,
            .parser = &parser,
            .scratch = &scratch,
            .bindings = bindings,
            .declared_types = types,
        };
        const uint64_t mark = scratch.length;
        bool valid = check_input_bindings(&reader, path == 0);
        check_test_expect(test, !valid && !input.resource && input.report_count == 1 && reports[0].token == 3 && reports[0].message == CHECK_INPUT_UNDECLARED,
                          "unresolved binding did not remain a source diagnostic");
        check_test_expect(test, declarations[1] == 0 && types[1] == BUSTER_TYPE_INT && declarations[3] == BUSTER_CHECK_NONE && types[3] == 0,
                          "binding handoff retained lookup guesses instead of resolved declarations");
        check_test_expect(test, input.binding_event_count == 4 && input.binding_statistics.identifiers == 3 && input.binding_statistics.declarations == 1,
                          "binding handoff event statistics");
        check_test_expect(test, scratch.length == mark, "binding handoff retained temporary results");
        Arena exhausted = { 0 };
        reader.scratch = &exhausted;
        input.report_count = 0;
        declarations[1] = BUSTER_CHECK_NONE;
        valid = check_input_bindings(&reader, path == 0);
        check_test_expect(test, !valid && input.resource && exhausted.length == 0 && declarations[1] == BUSTER_CHECK_NONE,
                          "binding handoff scratch failure published partial results");
        input.resource = false;
        reader.bindings.valid = false;
        valid = check_input_bindings(&reader, path == 0);
        check_test_expect(test, !valid && input.resource && exhausted.length == 0, "invalid binding events escaped failure propagation");
        input.resource = false;
        reader.bindings = (CBindings){ .valid = true };
        valid = check_input_bindings(&reader, path == 0);
        check_test_expect(test, valid && !input.resource && input.binding_event_count == 0 && exhausted.length == 0,
                          "empty binding handoff required scratch storage");
    }
    arena_release(&scratch);
    arena_release(&storage);
}

static void check_test_bindings(CheckTest* test)
{
    check_test_binding_source(test, "", "x", 0, 0, true, false);
    const uint32_t shadow[] = {
        0,
        1,
        2,
        2,
        2,
        2,
        1,
    };
    check_test_binding_source(test, "int x; int f(int x) { { long x = x; x = x + 1; } return x; }", "x", shadow, BUSTER_ARRAY_LENGTH(shadow), true, false);
    const uint32_t siblings[] = {
        0,
        1,
        1,
        1,
        4,
        4,
        4,
        0,
    };
    check_test_binding_source(test, "int x; int f(void) { { int x = 1; x = x + 1; } { int x = 2; x = x + 1; } return x; }", "x", siblings, BUSTER_ARRAY_LENGTH(siblings), true, false);
    const uint32_t iteration[] = {
        0,
        1,
        1,
        1,
        1,
        1,
        1,
        0,
    };
    check_test_binding_source(test, "int x; int f(void) { for (int x = 0; x < 3; x = x + 1) { x = x + 1; } return x; }", "x", iteration, BUSTER_ARRAY_LENGTH(iteration), true, false);
    const uint32_t redeclaration[] = {
        0,
        1,
        1,
    };
    check_test_binding_source(test, "extern int x; int x; int f(void) { return x; }", "x", redeclaration, BUSTER_ARRAY_LENGTH(redeclaration), true, false);
    const uint32_t self[] = {
        0,
        1,
        1,
        1,
    };
    check_test_binding_source(test, "int x; int f(void) { int x = x; return x; }", "x", self, BUSTER_ARRAY_LENGTH(self), true, false);
    const uint32_t multiple[] = {
        0,
        0,
        2,
        2,
    };
    check_test_binding_source(test, "int x; int f(void) { int y = x, x = y; return x; }", "x", multiple, BUSTER_ARRAY_LENGTH(multiple), true, false);
    const uint32_t prototype[] = {
        0,
        1,
        0,
        3,
        3,
    };
    check_test_binding_source(test, "int x; int f(int x); int g(void) { return x; } int f(int x) { return x; }", "x", prototype, BUSTER_ARRAY_LENGTH(prototype), true, false);
    const uint32_t nested_prototype[] = {
        0,
        1,
        2,
        2,
        0,
    };
    check_test_binding_source(test, "int x; int f(int (*cb)(int x), int x) { return x; } int g(void) { return x; }", "x", nested_prototype, BUSTER_ARRAY_LENGTH(nested_prototype), true, false);
    const uint32_t returned_declarator[] = {
        0,
        1,
        0,
    };
    check_test_binding_source(test, "typedef int T; int (*f(int T))(T) { return 0; }", "T", returned_declarator, BUSTER_ARRAY_LENGTH(returned_declarator), true, false);
    const uint32_t typedef_shadow[] = {
        0,
        1,
        1,
        1,
        0,
    };
    check_test_binding_source(test, "typedef int T; int f(int x) { { long T = x; T = T + 1; } T y = x; return y; }", "T", typedef_shadow, BUSTER_ARRAY_LENGTH(typedef_shadow), true, false);
    const uint32_t namespaces[] = {
        0,
        1,
        0,
        1,
    };
    check_test_binding_source(test, "struct S { int value; }; int S; struct S *p; int f(void) { return S; }", "S", namespaces, BUSTER_ARRAY_LENGTH(namespaces), true, false);
    const uint32_t tags[] = {
        0,
        1,
        1,
        0,
    };
    check_test_binding_source(test, "struct T { int a; }; int f(void) { { struct T { long b; }; struct T *p; } struct T *q; return 0; }", "T", tags, BUSTER_ARRAY_LENGTH(tags), true, false);
    const uint32_t forward[] = {
        0,
        0,
        0,
    };
    check_test_binding_source(test, "struct S; struct S { int value; }; struct S *p;", "S", forward, BUSTER_ARRAY_LENGTH(forward), true, false);
    const uint32_t enumeration[] = {
        0,
        0,
        0,
        3,
        3,
        3,
        0,
    };
    check_test_binding_source(test, "enum E { x = 1, y = (x + x) }; int f(void) { { int x = 3; x = x + 1; } return x; }", "x", enumeration, BUSTER_ARRAY_LENGTH(enumeration), true, false);
    const uint32_t expired[] = {
        0,
        BUSTER_CHECK_NONE,
    };
    check_test_binding_source(test, "int f(void) { { int x = 1; } return x; }", "x", expired, BUSTER_ARRAY_LENGTH(expired), false, false);
    check_test_binding_source(test, "int f(int x); int g(void) { return x; }", "x", expired, BUSTER_ARRAY_LENGTH(expired), false, false);
    const uint32_t absent[] = {
        BUSTER_CHECK_NONE,
    };
    check_test_binding_source(test, "int f(void) { return x; }", "x", absent, BUSTER_ARRAY_LENGTH(absent), false, false);
    check_test_binding_source(test, "int f(void) { x; int y; int y; }", "x", absent, BUSTER_ARRAY_LENGTH(absent), false, false);
    const uint32_t widths[] = {
        0,
        1,
        15,
        16,
        17,
        31,
        32,
        33,
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(widths); i += 1)
    {
        char source[2048];
        uint32_t expected[35];
        uint32_t used = (uint32_t)snprintf(source, sizeof(source), "int x; int f(void) { ");
        expected[0] = 0;
        for (uint32_t sibling = 0; sibling < widths[i]; sibling += 1)
        {
            used += (uint32_t)snprintf(source + used, sizeof(source) - used, "{ int x = 1; } ");
            expected[sibling + 1] = sibling + 1;
        }
        snprintf(source + used, sizeof(source) - used, "return x; }");
        expected[widths[i] + 1] = 0;
        check_test_binding_source(test, source, "x", expected, widths[i] + 2, true, widths[i] > BUSTER_C_BINDING_WALK_LIMIT);
    }
    check_test_binding_failures(test);
}

#endif
