// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original literal symbol-pragma fixtures from #1135, retained against the expanded #1447 handler: metadata across
// parser schedules, conservative refusal boundaries, and transactional preparation with poisoned arenas. Cases that
// now have implemented Clang warning/no-op semantics remain fixtures with their new expected metadata.

static void check_test_symbol_pragma_history(CheckTest* test)
{
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    const char* source = "#pragma weak f\n#pragma redefine_extname f exported_f\nint f(void);\n#pragma weak x\nint x;\n";
    const PPOptions options = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true };
    Arena arena = arena_reserve(UINT64_C(1) << 16);
    Arena scratch = arena_reserve(UINT64_C(1) << 16);
    bool ready = intern_initialize(&identifiers) && pp_initialize(&pp, &options, &identifiers);
    ready = ready && arena.base != NULL && scratch.base != NULL && pp_run_source(&pp, "symbol-history.c", (const uint8_t*)source, strlen(source));
    check_test_expect(test, ready, "symbol pragma history fixture initializes");
    for (uint32_t schedule = 0; ready && schedule < 4; schedule += 1)
    {
        const ParseOptions parse_options = { .scalar = (schedule & 1) != 0, .tree_only = (schedule & 2) != 0 };
        const bool parsed = parse_initialize(&parser, &parse_options, options.language) && parse_translation_unit(&parser, &pp);
        check_test_expect(test, parsed && parser.tokens.pragma_count == 3, "symbol history retains all source coordinates");
        const uint64_t arena_capacity = arena.capacity;
        const uint64_t scratch_capacity = scratch.capacity;
        uint64_t required = 31;
        for (uint32_t pass = 0; parsed && pass < 3; pass += 1)
        {
            const uint64_t attempts = pass == 1 ? required - 31 : 1;
            for (uint64_t limit = 0; limit < attempts; limit += 1)
            {
                arena.capacity = arena_capacity;
                scratch.capacity = scratch_capacity;
                arena.length = 0;
                scratch.length = 0;
                memset(arena_push(&arena, arena.capacity, 1), 0xa5, arena.capacity);
                memset(arena_push(&scratch, scratch.capacity, 1), 0x5a, scratch.capacity);
                arena.length = 31;
                scratch.length = 29;
                arena.capacity = pass == 1 ? 31 + limit : arena_capacity;
                scratch.capacity = pass == 2 ? 29 : scratch_capacity;
                uint8_t handled[3] = { 0 };
                CheckInput input = { 0 };
                CheckInputReader reader = { .output = &input, .parser = &parser, .pp = &pp, .arena = &arena, .scratch = &scratch };
                const bool valid = check_input_symbol_pragmas(&reader, handled, true);
                if (pass == 0)
                {
                    required = arena.length;
                    const bool published = valid && !input.resource && input.symbol_pragmas != NULL && input.symbol_pragmas->count == 3;
                    check_test_expect(test, published && scratch.length == 29, "symbol preparation restores scratch and publishes initialized history");
                    for (uint32_t i = 0; published && i < 3; i += 1)
                    {
                        const CheckInputSymbolPragma* record = input.symbol_pragmas->records + i;
                        check_test_expect(test, handled[i] == 1 && record->pragma == i && record->token == parser.tokens.pragma_positions[i] &&
                                               record->name != 0 && ((record->target != 0) == (i == 1)), "symbol history preserves kind, name and source ordering");
                    }
                }
                else
                {
                    check_test_expect(test, !valid && input.symbol_pragmas == NULL && arena.length == 31 && scratch.length == 29 &&
                                           handled[0] == 0 && handled[1] == 0 && handled[2] == 0, "failed symbol preparation publishes neither ownership nor partial history");
                }
            }
        }
        arena.capacity = arena_capacity;
        scratch.capacity = scratch_capacity;
        parse_release(&parser);
    }
    pp_release(&pp);
    intern_release(&identifiers);
    arena_release(&scratch);
    arena_release(&arena);
}

static void check_test_symbol_pragmas(CheckTest* test)
{
    static const struct
    {
        const char* source;
        bool accepted;
        bool weak;
        const char* renamed;
    } fixtures[] =
    {
        { "#pragma weak f\nint f(void){return 7;}\n", true, true, NULL },
        { "int f(void);\n#pragma weak f\nint f(void){return 7;}\n", true, true, NULL },
        { "#pragma weak f\n#pragma weak f\nint f(void);\n", true, true, NULL },
        { "#pragma weak f\nint f=7;\n", true, true, NULL },
        { "int f;\n#pragma weak f\n", true, true, NULL },
        { "extern int f;\n#pragma weak f\nint g(void){return f;}\n", true, true, NULL },
        { "#pragma weak f\nint f __attribute__((aligned(16)));\n", true, true, NULL },
        { "#pragma redefine_extname f exported_f\nint f(void){return 7;}\n", true, false, "exported_f" },
        { "int f(void);\n#pragma redefine_extname f exported_f\nint g(void){return f();}\n", true, false, "exported_f" },
        { "#pragma redefine_extname f exported_f\nextern int f;\nint g(void){return f;}\n", true, false, "exported_f" },
        { "#pragma redefine_extname f exported_f\nint f;\nint f;\n", true, false, "exported_f" },
        { "#pragma redefine_extname f exported_f\n#pragma weak f\nint f(void){return 7;}\n", true, true, "exported_f" },
        { "#pragma weak f\n#pragma redefine_extname f exported_f\nint f(void){return 7;}\n", true, true, "exported_f" },
        { "#pragma redefine_extname f exported_f\n#pragma redefine_extname f ignored_f\nint f(void){return 7;}\n", true, false, "exported_f" },
        { "#pragma redefine_extname missing exported_f\nint f;\n", true, false, NULL },
        { "_Pragma(\"weak f\")\nint f;\n", true, true, NULL },
        // clang's clone of g's declaration under f (#1849), which the emission makes: f has no declaration of its own.
        { "#pragma weak f = g\nint g(void){return 7;}\n", true, false, NULL },
        { "#pragma weak f = g\nint g(void);\n", false, false, NULL },
        { "#pragma weak missing\nint f;\n", true, false, NULL },
        { "#pragma weak f\nstatic int f;\n", true, false, NULL },
        { "#pragma redefine_extname f exported_f\nstatic int f;\n", true, false, NULL },
        { "int f(void){return 7;}\n#pragma weak f\n", true, false, NULL },
        { "int f=7;\n#pragma redefine_extname f exported_f\n", true, false, NULL },
        { "extern int f;\nint g(void){return f;}\n#pragma weak f\n", true, true, NULL },
        // The reader retains the rename; emission decides whether an earlier eager reference kept the original name.
        { "int f(void);\nint g(void){return f();}\n#pragma redefine_extname f exported_f\n", true, false, "exported_f" },
        { "#pragma redefine_extname f exported_f\nint f __asm__(\"other_f\");\n", true, false, "other_f" },
        { "#define f g\n#pragma weak f\n#undef f\nint f;\n", false, false, NULL },
        { "#define exported_f g\n#pragma redefine_extname f exported_f\nint f;\n", false, false, NULL },
        { "int g(void){\n#pragma weak f\nreturn 0;}\nint f;\n", true, true, NULL },
        { "struct S{\n#pragma weak f\nint member;};\nint f;\n", false, false, NULL },
        { "int\n#pragma weak f\nf;\n", false, false, NULL },
        { "#pragma weak f trailing\nint f;\n", true, false, NULL },
        { "#pragma redefine_extname f exported_f trailing\nint f;\n", true, false, NULL },
    };
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    const PPOptions options = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true };
    for (uint32_t fixture = 0; fixture < BUSTER_ARRAY_LENGTH(fixtures); fixture += 1)
    {
        bool ready = intern_initialize(&identifiers) && pp_initialize(&pp, &options, &identifiers);
        ready = ready && pp_run_source(&pp, "symbol-pragma.c", (const uint8_t*)fixtures[fixture].source, strlen(fixtures[fixture].source));
        check_test_expect(test, ready, "symbol pragma source preprocesses");
        for (uint32_t schedule = 0; ready && schedule < 4; schedule += 1)
        {
            CheckTestTypes types = { 0 };
            Arena arena = arena_reserve(UINT64_C(1) << 24);
            Arena scratch = arena_reserve(UINT64_C(1) << 24);
            const ParseOptions parse_options = { .scalar = (schedule & 1) != 0, .tree_only = (schedule & 2) != 0 };
            const bool parsed = check_test_types_open(&types) && arena.base != NULL && scratch.base != NULL &&
                                parse_initialize(&parser, &parse_options, options.language) && parse_translation_unit(&parser, &pp);
            CheckInput input = { 0 };
            const bool adapted = parsed && check_input(&input, &parser, &pp, &types.table, &arena, &scratch);
            const bool accepted = parsed && adapted && !input.resource && input.report_count == 0;
            const bool rejected = parsed && !input.resource && input.report_count != 0 && input.reports[0].message == CHECK_INPUT_PRAGMA_SEMANTICS && input.reports[0].pragma != 0;
            check_test_expect(test, fixtures[fixture].accepted ? accepted : rejected, fixtures[fixture].source);
            bool weak = false;
            const CheckInputAttribute* label = NULL;
            for (uint32_t token = 0; accepted && token < parser.tokens.count; token += 1)
            {
                const IdentId name = parser.tokens.names[token];
                if (input.declarations[token] == token && name != 0 && intern_length(&identifiers, name) == 1 && intern_bytes(&identifiers, name)[0] == 'f')
                {
                    const CheckInputAttributes* attributes = check_input_attributes_at(&input, token);
                    weak |= check_input_attribute_of(attributes, CHECK_ATTRIBUTE_WEAK) != NULL;
                    const CheckInputAttribute* own = check_input_attribute_of(attributes, CHECK_ATTRIBUTE_ASM_LABEL);
                    label = own != NULL ? own : label;
                }
            }
            if (accepted)
            {
                check_test_expect(test, weak == fixtures[fixture].weak, "weak pragma publishes real weak metadata");
                const char* expected = fixtures[fixture].renamed;
                check_test_expect(test, expected != NULL ? label != NULL && label->text.length == strlen(expected) && memcmp(label->text.pointer, expected, label->text.length) == 0 : label == NULL,
                                  "external-name pragma publishes the canonical emitted name");
            }
            parse_release(&parser);
            check_test_types_close(&types);
            arena_release(&scratch);
            arena_release(&arena);
        }
        pp_release(&pp);
        intern_release(&identifiers);
    }
    check_test_symbol_pragma_history(test);
}
