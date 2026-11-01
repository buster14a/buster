// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original sparse-renderer protocol controls. Facts are supplied independently of any warning producer so this
// shared contract remains qualified without depending on a particular follow-up's language extensions.

static void check_test_warning_render(CheckTest* test)
{
    static const char source[] =
        "#pragma clang diagnostic error \"-Wnon-literal-null-conversion\"\n"
        "int first;\n"
        "#pragma clang diagnostic ignored \"-Wnon-literal-null-conversion\"\n"
        "int second;\n"
        "#warning later_preprocessing\n";
    for (uint32_t mode = 0; mode < 4; mode += 1)
    {
        static InternTable identifiers;
        static Preprocessor pp;
        static Parser parser;
        CheckTestTypes types = { 0 };
        Arena retained = arena_reserve(UINT64_C(1) << 26);
        Arena scratch = arena_reserve(UINT64_C(1) << 26);
        char* text = NULL;
        size_t size = 0;
        FILE* output = open_memstream(&text, &size);
        const PPOptions preprocessing = {
            .language = { .standard = 2017, .gnu = true },
            .semantic_pragmas = true,
            .no_standard_includes = true,
            .collect_diagnostics = mode != 1,
            .suppress_warnings = mode == 2,
            .diagnostics = output,
        };
        const ParseOptions parsing = { .extension_metadata = true };
        CheckInput input = { 0 };
        bool ready = output != NULL && retained.base != NULL && scratch.base != NULL && check_test_types_open(&types) && intern_initialize(&identifiers) &&
                     pp_initialize(&pp, &preprocessing, &identifiers) && parse_initialize(&parser, &parsing, preprocessing.language) &&
                     pp_run_source(&pp, "shared-warnings.c", (const uint8_t*)source, sizeof(source) - 1) && parse_translation_unit(&parser, &pp) &&
                     check_input(&input, &parser, &pp, &types.table, &retained, &scratch);
        check_test_expect(test, ready && parser.tokens.count == 6, "shared sparse renderer fixture has independent source and policy locations");
        if (ready)
        {
            const TypeId pointer = type_pointer(&types.table, BUSTER_TYPE_INT);
            const CheckWarningEvent primary = {
                .id = CHECK_WARNING_ID_NON_LITERAL_NULL,
                .kind = CHECK_WARNING_EVENT_NON_LITERAL_NULL,
                .token = 1,
                .source = BUSTER_CHECK_WARNING_NO_SOURCE,
                .completion_kind = CHECK_WARNING_COMPLETE_TU_END,
                .completion = BUSTER_CHECK_WARNING_NO_SOURCE,
                .payload = { .first_type = pointer, .token = BUSTER_CHECK_WARNING_NO_SOURCE },
            };
            const CheckWarningNote note = {
                .kind = CHECK_WARNING_NOTE_PRIOR_INITIALIZATION,
                .token = 4,
                .source = BUSTER_CHECK_WARNING_NO_SOURCE,
                .payload = { .token = BUSTER_CHECK_WARNING_NO_SOURCE },
            };
            CheckWarningBuffer facts = { .arena = &retained };
            CheckWarningEvent ignored = primary;
            ignored.token = 4;
            ready = pointer != 0 && check_warning_append(&facts, &primary, &note, 1) && check_warning_append(&facts, &ignored, &note, 1);
            CheckWarningEvent completion = primary;
            completion.kind = CHECK_WARNING_EVENT_MESSAGE;
            completion.id = CHECK_WARNING_ID_INTEGER_OVERFLOW;
            completion.payload.data = CHECK_WARNING_INTEGER_OVERFLOW;
            ready = ready && check_warning_append(&facts, &completion, NULL, 0);
            const CheckWarningEvents view = check_warning_view(&facts);
            const uint64_t mark = scratch.length;
            const bool promoted = check_driver_warning_promoted(&pp, &input, &view, NULL);
            ready = ready && check_driver_warning_events(&parser, &pp, &input, &types.table, &view, NULL, &scratch);
            check_test_expect(test, ready && scratch.length == mark && promoted == (mode != 2),
                              "source mappings and -w govern promotion while sparse rendering releases temporary storage");
            check_test_expect(test, !pp.options.collect_diagnostics || mode == 2 ||
                                   (pp.diagnostic_last != NULL && pp.diagnostic_last->severity == PP_SEVERITY_MAPPED_ERROR),
                              "a promoted sparse warning retains warning-origin class for later body analysis");
            if (mode != 1 && mode != 2)
            {
                const PPDiagnosticRecord* mapped = pp.diagnostic_last;
                check_test_expect(test, mapped != NULL && mapped->severity == PP_SEVERITY_MAPPED_ERROR &&
                                       mapped->position == ((uint64_t)(pp.output.count + 1u) << 2 | PP_DIAGNOSTIC_SEMANTIC),
                                  "a promoted sparse warning retains its mapped class and raw completion becomes the semantic phase once");
            }
            pp_diagnostic_publish(&pp);
            fflush(output);
            const char* diagnostic = text != NULL ? strstr(text, "error: expression which evaluates to zero treated as a null pointer constant of type 'int *' [-Werror,-Wnon-literal-null-conversion]") : NULL;
            const char* attached = text != NULL ? strstr(text, "shared-warnings.c:4:5: note: previous initialization is here") : NULL;
            const char* preprocessing_warning = text != NULL ? strstr(text, "later_preprocessing") : NULL;
            check_test_expect(test, mode == 2 ? pp.errors == 0 && pp.warnings == 0 && diagnostic == NULL && attached == NULL :
                                   pp.errors == 1 && pp.warnings == 1 && diagnostic != NULL && attached != NULL && preprocessing_warning != NULL &&
                                   preprocessing_warning < diagnostic && diagnostic < attached,
                              "completion orders a source group after EOF preprocessing and accepted primary owns its only attached note");
            check_test_expect(test, text == NULL || (strstr(text, "semantic warning") == NULL && strstr(text, "overflows its type") == NULL),
                              "legacy MESSAGE completion metadata never creates another warning group");
            CheckWarningEvent suppressed = primary;
            suppressed.flags = BUSTER_CHECK_WARNING_SUPPRESSED;
            CheckWarningBuffer suppressed_facts = { .arena = &retained };
            const uint32_t before_errors = pp.errors;
            const bool suppressed_ready = check_warning_append(&suppressed_facts, &suppressed, &note, 1);
            const CheckWarningEvents suppressed_view = check_warning_view(&suppressed_facts);
            check_test_expect(test, suppressed_ready && check_driver_warning_severity(&pp, &input, &suppressed) == CHECK_WARNING_IGNORE &&
                                   !check_driver_warning_promoted(&pp, &input, &suppressed_view, NULL) &&
                                   check_driver_warning_events(&parser, &pp, &input, &types.table, &suppressed_view, NULL, &scratch) && pp.errors == before_errors,
                              "semantic suppression survives a source error mapping without reviving its primary or attached note");
            if (mode == 3)
            {
                uint8_t headers[6] = { 0, 1, 0, 0, 0, 0 };
                input.graph.system_tokens = headers;
                input.graph.system_token_count = 6;
                check_test_expect(test, check_driver_warning_severity(&pp, &input, &primary) == CHECK_WARNING_IGNORE,
                                  "system-header suppression precedes source warning promotion");
                const char* options[] = { "-Wsystem-headers" };
                pp.options.semantic_warning_options = options;
                pp.options.semantic_warning_option_count = 1;
                check_test_expect(test, check_driver_warning_severity(&pp, &input, &primary) == CHECK_WARNING_ERROR,
                                  "an explicit system-header option restores the original source mapping");
                input.graph.system_tokens = NULL;
                input.graph.system_token_count = 0;
                Arena limited = scratch;
                limited.capacity = limited.length;
                CheckWarningEvent simple = primary;
                simple.note_first = 0;
                simple.note_count = 0;
                const CheckWarningEvents one = { .events = &simple, .count = 1 };
                const uint32_t errors = pp.errors;
                check_test_expect(test, !check_driver_warning_events(&parser, &pp, &input, &types.table, &one, NULL, &limited) &&
                                       limited.length == scratch.length && pp.errors == errors,
                                  "renderer allocation failure publishes no partial primary and releases all temporary text");
            }
        }
        if (output != NULL)
        {
            fclose(output);
        }
        free(text);
        parse_release(&parser);
        pp_release(&pp);
        intern_release(&identifiers);
        check_test_types_close(&types);
        arena_release(&scratch);
        arena_release(&retained);
    }
    static const char prefix_source[] = "int first;int second;\n#warning later_prefix_control\n";
    for (uint32_t scalar = 0; scalar < 2; scalar += 1)
    {
        static InternTable identifiers;
        static Preprocessor pp;
        static Parser parser;
        CheckTestTypes types = { 0 };
        Arena retained = arena_reserve(UINT64_C(1) << 26);
        Arena scratch = arena_reserve(UINT64_C(1) << 26);
        char* text = NULL;
        size_t size = 0;
        FILE* output = open_memstream(&text, &size);
        const PPOptions preprocessing = {
            .language = { .standard = 2017, .gnu = true },
            .collect_diagnostics = true,
            .diagnostics = output,
        };
        const ParseOptions parsing = { .extension_metadata = true, .scalar = scalar != 0 };
        CheckInput input = { 0 };
        bool ready = output != NULL && retained.base != NULL && scratch.base != NULL && check_test_types_open(&types) &&
                     intern_initialize(&identifiers) && pp_initialize(&pp, &preprocessing, &identifiers) &&
                     parse_initialize(&parser, &parsing, preprocessing.language) &&
                     pp_run_source(&pp, "sparse-prefix.c", (const uint8_t*)prefix_source, sizeof(prefix_source) - 1) &&
                     parse_translation_unit(&parser, &pp) && check_input(&input, &parser, &pp, &types.table, &retained, &scratch);
        if (ready && parser.tokens.count == 6)
        {
            Parser prefix = parser;
            prefix.tokens.count = 3;
            const CheckWarningEvent event = {
                .id = CHECK_WARNING_ID_MISSING_DECLARATIONS,
                .kind = CHECK_WARNING_EVENT_EMPTY_DECLARATION,
                .token = 1,
                .source = BUSTER_CHECK_WARNING_NO_SOURCE,
                .completion_kind = CHECK_WARNING_COMPLETE_AT_TOKEN,
                .completion = 3,
                .payload = { .token = BUSTER_CHECK_WARNING_NO_SOURCE },
            };
            const CheckWarningEvents view = { .events = &event, .count = 1 };
            ready = check_driver_warning_events_at(&prefix, &pp, &input, &types.table, &view, NULL, parser.tokens.sources[3], &scratch);
        }
        else
        {
            ready = false;
        }
        pp_diagnostic_publish(&pp);
        if (output != NULL)
        {
            fclose(output);
        }
        const char* semantic = text != NULL ? strstr(text, "warning: declaration does not declare anything") : NULL;
        const char* later = text != NULL ? strstr(text, "later_prefix_control") : NULL;
        check_test_expect(test, ready && semantic != NULL && later != NULL && semantic < later && pp.warnings == 2 && pp.errors == 0,
                          "a sparse fact at a retained prefix end uses original lookahead before later preprocessing groups");
        free(text);
        parse_release(&parser);
        pp_release(&pp);
        intern_release(&identifiers);
        check_test_types_close(&types);
        arena_release(&scratch);
        arena_release(&retained);
    }
    // #992's rendered spellings against clang's exact text: a kept qualifier, typedef sugar and its
    // `(aka '...')`, an unnamed tag's declaration place — its expansion's for a macro body's tag — and the
    // `__size_t` sentinel for a sizeof/alignof operand's own type.
    static const char spelling_source[] =
        "typedef struct { int x; } T;\n"
        "typedef struct S { int x; } S;\n"
        "typedef unsigned long size_t;\n"
        "T *tp;\n"
        "struct { int y; } *up;\n"
        "S *sp;\n"
        "S sv;\n"
        "size_t *zp;\n"
        "int *const cp;\n"
        "#define D struct { int y; }\n"
        "D *mp;\n";
    {
        static InternTable identifiers;
        static Preprocessor pp;
        static Parser parser;
        CheckTestTypes types = { 0 };
        Arena retained = arena_reserve(UINT64_C(1) << 26);
        Arena scratch = arena_reserve(UINT64_C(1) << 26);
        char* text = NULL;
        size_t size = 0;
        FILE* output = open_memstream(&text, &size);
        const PPOptions preprocessing = {
            .language = { .standard = 2017, .gnu = true },
            .semantic_pragmas = true,
            .no_standard_includes = true,
            .collect_diagnostics = true,
            .diagnostics = output,
        };
        const ParseOptions parsing = { .extension_metadata = true };
        CheckInput input = { 0 };
        bool ready = output != NULL && retained.base != NULL && scratch.base != NULL && check_test_types_open(&types) && intern_initialize(&identifiers) &&
                     pp_initialize(&pp, &preprocessing, &identifiers) && parse_initialize(&parser, &parsing, preprocessing.language) &&
                     pp_run_source(&pp, "spellings.c", (const uint8_t*)spelling_source, sizeof(spelling_source) - 1) &&
                     parse_translation_unit(&parser, &pp) && check_input(&input, &parser, &pp, &types.table, &retained, &scratch);
        // Each declarator's name token, for its declared type and its typedef-specifier spelling's scan.
        static const char* const names[] = { "tp", "up", "sp", "sv", "zp", "cp", "mp", "size_t" };
        uint32_t declaration[BUSTER_ARRAY_LENGTH(names)] = { 0 };
        for (uint32_t n = 0; ready && n < BUSTER_ARRAY_LENGTH(names); n += 1)
        {
            const uint32_t identifier = intern_find(&identifiers, (const uint8_t*)names[n], (uint32_t)strlen(names[n]));
            for (uint32_t i = 0; declaration[n] == 0 && i < parser.tokens.count; i += 1)
            {
                declaration[n] = parser.tokens.names[i] == identifier && input.declarations != 0 && input.declarations[i] == i ? i + 1 : 0;
            }
            ready &= declaration[n] != 0;
            declaration[n] -= 1;
        }
        check_test_expect(test, ready, "spelling fixture's declarations and typedefs resolve their own tokens");
        if (ready)
        {
            CheckWarningBuffer facts = { .arena = &retained };
            CheckWarningEvent event = {
                .id = CHECK_WARNING_ID_NON_LITERAL_NULL,
                .kind = CHECK_WARNING_EVENT_NON_LITERAL_NULL,
                .source = BUSTER_CHECK_WARNING_NO_SOURCE,
                .completion_kind = CHECK_WARNING_COMPLETE_TU_END,
                .completion = BUSTER_CHECK_WARNING_NO_SOURCE,
                .payload = { .token = BUSTER_CHECK_WARNING_NO_SOURCE },
            };
            // `int *const cp`: the declared type's qualifier stays, no sugar.
            event.token = declaration[5];
            event.payload.first_type = input.declared_types[declaration[5]];
            event.payload.data = check_input_typedef_spelling(&input, &parser, declaration[5]);
            ready = check_warning_append(&facts, &event, NULL, 0);
            // `T *tp`: the typedef is the anonymous record's own name, so clang prints no aka for it.
            event.token = declaration[0];
            event.payload.first_type = input.declared_types[declaration[0]];
            event.payload.data = check_input_typedef_spelling(&input, &parser, declaration[0]);
            ready = ready && check_warning_append(&facts, &event, NULL, 0);
            // `struct { int y; } *up`: no typedef names the tag; its own declaration's place does.
            event.token = declaration[1];
            event.payload.first_type = input.declared_types[declaration[1]];
            event.payload.data = check_input_typedef_spelling(&input, &parser, declaration[1]);
            ready = ready && check_warning_append(&facts, &event, NULL, 0);
            // `S *sp` and `S sv`: the typedef's sugar covers `struct S` wherever it appears in the type.
            event.token = declaration[2];
            event.payload.first_type = input.declared_types[declaration[2]];
            event.payload.data = check_input_typedef_spelling(&input, &parser, declaration[2]);
            ready = ready && check_warning_append(&facts, &event, NULL, 0);
            event.token = declaration[3];
            event.payload.first_type = input.declared_types[declaration[3]];
            event.payload.data = check_input_typedef_spelling(&input, &parser, declaration[3]);
            ready = ready && check_warning_append(&facts, &event, NULL, 0);
            // `size_t *zp`: an ordinary typedef's sugar, expanded in the aka.
            event.token = declaration[4];
            event.payload.first_type = input.declared_types[declaration[4]];
            event.payload.data = check_input_typedef_spelling(&input, &parser, declaration[4]);
            ready = ready && check_warning_append(&facts, &event, NULL, 0);
            // `D *mp`: the tag's declaration was written inside `D`'s body; clang's place is the expansion's.
            event.token = declaration[6];
            event.payload.first_type = input.declared_types[declaration[6]];
            event.payload.data = check_input_typedef_spelling(&input, &parser, declaration[6]);
            ready = ready && check_warning_append(&facts, &event, NULL, 0);
            // A sizeof/alignof operand's own type is `__size_t`; `size_t`'s typedef gives the canonical aka.
            event.token = declaration[7];
            event.payload.first_type = input.declared_types[declaration[7]];
            event.payload.data = BUSTER_CHECK_NONE;
            ready = ready && check_warning_append(&facts, &event, NULL, 0);
            // The ordered warning spells both operands; the sentinel covers the second here too.
            const CheckWarningEvent ordered = {
                .id = CHECK_WARNING_ID_ORDERED_POINTER_INTEGER,
                .kind = CHECK_WARNING_EVENT_ORDERED_POINTER_INTEGER,
                .token = declaration[1],
                .source = BUSTER_CHECK_WARNING_NO_SOURCE,
                .completion_kind = CHECK_WARNING_COMPLETE_TU_END,
                .completion = BUSTER_CHECK_WARNING_NO_SOURCE,
                .payload = { .first_type = input.declared_types[declaration[1]], .second_type = input.declared_types[declaration[7]],
                             .name = 0, .data = BUSTER_CHECK_NONE, .token = BUSTER_CHECK_WARNING_NO_SOURCE },
            };
            ready = ready && check_warning_append(&facts, &ordered, NULL, 0);
            const CheckWarningEvents view = check_warning_view(&facts);
            const uint64_t mark = scratch.length;
            ready = ready && check_driver_warning_events(&parser, &pp, &input, &types.table, &view, NULL, &scratch) && scratch.length == mark;
            pp_diagnostic_publish(&pp);
            if (output != NULL)
            {
                fflush(output);
            }
            check_test_expect(test, ready && pp.warnings == 9 && pp.errors == 0,
                              "every fabricated spelling fact publishes its own warning group");
            const char* expected[] = {
                "of type 'int *const' [-Wnon-literal-null-conversion]",
                "of type 'T *' [-Wnon-literal-null-conversion]",
                "of type 'struct (unnamed at spellings.c:5:1) *' [-Wnon-literal-null-conversion]",
                "of type 'S *' (aka 'struct S *') [-Wnon-literal-null-conversion]",
                "of type 'S' (aka 'struct S') [-Wnon-literal-null-conversion]",
                "of type 'size_t *' (aka 'unsigned long *') [-Wnon-literal-null-conversion]",
                "of type 'struct (unnamed at spellings.c:11:1) *' [-Wnon-literal-null-conversion]",
                "of type '__size_t' (aka 'unsigned long') [-Wnon-literal-null-conversion]",
                "ordered comparison between pointer and integer ('struct (unnamed at spellings.c:5:1) *' and '__size_t' (aka 'unsigned long'))",
            };
            bool found = true;
            for (uint32_t e = 0; e < BUSTER_ARRAY_LENGTH(expected); e += 1)
            {
                found &= text != NULL && strstr(text, expected[e]) != NULL;
            }
            check_test_expect(test, found, "qualifiers, typedef sugar and (aka '...'), unnamed-tag places and __size_t match clang's text");
        }
        if (output != NULL)
        {
            fclose(output);
        }
        free(text);
        parse_release(&parser);
        pp_release(&pp);
        intern_release(&identifiers);
        check_test_types_close(&types);
        arena_release(&scratch);
        arena_release(&retained);
    }
}
