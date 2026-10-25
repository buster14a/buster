// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original end-to-end demand fixtures. Default/lazy checking and scalar/vector schedules must emit byte-identical
// ELF objects after semantic selection. The fixture counts are stated independently of the reachability solver.

static void check_test_program_selection_run(CheckTest* test, const char* source, const char* name, uint32_t functions, uint32_t objects,
                                             CheckProgramStatus emission_status, const char* clang)
{
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    const PPOptions preprocessing = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true };
    const ParseOptions parsing = { .extension_metadata = true };
    Arena expected_storage = arena_reserve(1u << 20);
    uint8_t* expected = 0;
    uint64_t expected_size = 0;
    const bool parsed = intern_initialize(&identifiers) && pp_initialize(&pp, &preprocessing, &identifiers) &&
                        parse_initialize(&parser, &parsing, preprocessing.language) &&
                        pp_run_source(&pp, name, (const uint8_t*)source, strlen(source)) && parse_translation_unit(&parser, &pp);
    char description[256];
    snprintf(description, sizeof(description), "%s: emission-selection source parses", name);
    check_test_expect(test, parsed && expected_storage.base != 0, description);
    for (uint32_t path = 0; parsed && path < 2; path += 1)
    {
        parser.options.scalar = path == 0;
        for (uint32_t mode = 0; mode < 2; mode += 1)
        {
            CheckTestTypes types = { 0 };
            Arena retained = arena_reserve(1u << 26);
            Arena output = arena_reserve(1u << 26);
            Arena bytes = arena_reserve(1u << 22);
            Arena scratch = arena_reserve(1u << 26);
            CheckLazy lazy = { 0 };
            CheckInput input = { 0 };
            CheckStream stream = { 0 };
            CheckInitializers initializers = { 0 };
            CheckAsms asms = { 0 };
            CheckDataPrepared data = { 0 };
            CheckProgram full = { 0 };
            CheckProgramSelection selection = { 0 };
            const CheckOptions checking = { .scalar = path == 0 };
            bool ready = retained.base != 0 && output.base != 0 && bytes.base != 0 && scratch.base != 0 && check_test_types_open(&types) &&
                         check_lazy_plan(&lazy, &parser, &pp, &retained, &scratch, path == 0);
            ready = ready && (mode == 0 ? check_input(&input, &parser, &pp, &types.table, &retained, &scratch) :
                                        check_input_selected(&input, &parser, &pp, &types.table, &retained, &scratch, lazy.deferred_tokens));
            ready = ready && check_graph_run(&types.table, &input.graph, &retained, &scratch, &checking, &stream) && stream.report_count == 0 &&
                    check_initializers_run(&initializers, &input, &stream, &parser, &types.table, &retained, &scratch) &&
                    check_asms_run(&asms, &input, &stream, &types.table, &retained, &scratch) &&
                    check_data_prepare(&data, &input, &stream, &initializers, &types.table, &retained, &scratch);
            const CheckProgramOptions options = { .initializers = &initializers, .asms = &asms, .identifiers = &identifiers,
                                                 .initializer_references = data.references, .initializer_reference_count = data.reference_count, .scalar = path == 0 };
            ready = ready && check_program(&full, &parser, &input, &stream, &types.table, options, &retained, &scratch) &&
                    check_program_select(&selection, &full, &data, path == 0, &retained, &scratch);
            snprintf(description, sizeof(description), "%s: checked semantic dependency/root selection succeeds", name);
            check_test_expect(test, ready, description);
            if (!ready)
            {
                fprintf(stderr, "selection input=%u graph=%u init=%u data=%u program=%u selected=%u token=%u\n", input.report_count, stream.report_count,
                        initializers.status, data.status, full.status, selection.status, selection.token);
            }
            if (ready)
            {
                snprintf(description, sizeof(description), "%s: independently stated code/data emission counts", name);
                check_test_expect(test, selection.selected_functions == functions && selection.selected_objects == objects &&
                                       selection.program.body_count == functions && selection.data.count == objects, description);
                check_test_expect(test, full.body_count == (mode == 0 ? lazy.body_count : lazy.selected_count),
                                  "emission selection leaves the complete semantic checking result unchanged");
                for (uint32_t i = 1; i < selection.program.symbol_count; i += 1)
                {
                    check_test_expect(test, selection.program.symbol_objects[i - 1] < selection.program.symbol_objects[i],
                                      "selected code/data/external symbols retain deterministic canonical source-object order");
                }
                Arena exhausted = arena_reserve(32);
                exhausted.length = 17;
                exhausted.capacity = 17;
                CheckProgramSelection failed = { 0 };
                check_test_expect(test, !check_program_select(&failed, &full, &data, path == 0, &exhausted, &scratch) &&
                                       failed.status == CHECK_PROGRAM_RESOURCE && failed.program.bodies == 0 && failed.data.objects == 0 &&
                                       exhausted.length == 17 && scratch.length == 0,
                                  "selected emission view rolls back atomically without destroying checked semantic input");
                arena_release(&exhausted);
            }
            CheckProgramCode code = { 0 };
            CheckDataResult emitted_data = { 0 };
            const CheckProgram* program = &selection.program;
            bool emitted = ready && check_program_emit(&code, program, &parser, &identifiers, selection.data.capacity, path == 0, &output, &bytes, &scratch, NULL, NULL, true) &&
                           check_data_publish(&selection.data, &types.table, &stream, program->object_symbols, program->object_count, &code.link, 0,
                                              &bytes, &scratch, &emitted_data) && link_check(&code.link) == LINK_ERROR_NONE;
            snprintf(description, sizeof(description), "%s: selected publication matches its supported object obligations", name);
            const bool publication = emission_status == CHECK_PROGRAM_OK ? emitted : ready && !emitted && code.status == emission_status &&
                                  code.link.symbols.count == 0 && code.lowered.functions == 0 && output.length == 0 && bytes.length == 0 && scratch.length == 0;
            check_test_expect(test, publication, description);
            if (ready && !publication)
            {
                fprintf(stderr, "selection emission=%u token=%u backend=%u data=%u\n", code.status, code.token, code.backend_error, emitted_data.status);
            }
            for (uint32_t token = 0; emitted && token < input.token_count; token += 1)
            {
                const CheckInputAttributes* attributes = input.attributes[token];
                const uint32_t object = input.objects[token];
                const SymbolId symbol = object < program->object_count ? program->object_symbols[object] : 0;
                for (uint32_t a = 0; symbol != 0 && attributes != 0 && a < attributes->count; a += 1)
                {
                    const CheckInputAttribute* attribute = attributes->records + a;
                    if (attribute->kind == CHECK_ATTRIBUTE_ASM_LABEL && !attribute->ignored && !input.registers[token])
                    {
                        const NameId renamed = code.link.symbols.name[symbol];
                        check_test_expect(test, code.link.names.length[renamed] == attribute->text.length &&
                                               memcmp(code.link.names.bytes[renamed], attribute->text.pointer, attribute->text.length) == 0,
                                          "decoded asm label survives canonical redeclarations into the actual object symbol");
                    }
                    else if (attribute->kind == CHECK_ATTRIBUTE_ASM_LABEL && attribute->ignored && !input.registers[token])
                    {
                        bool explicit_name = false;
                        for (uint32_t other = 0; other < input.token_count; other += 1)
                        {
                            const CheckInputAttributes* retained = input.attributes[other];
                            for (uint32_t j = 0; input.objects[other] == object && retained != 0 && j < retained->count; j += 1)
                            {
                                explicit_name |= retained->records[j].kind == CHECK_ATTRIBUTE_ASM_LABEL && !retained->records[j].ignored && !input.registers[other];
                            }
                        }
                        if (!explicit_name)
                        {
                            const uint32_t declaration = program->object_declarations[object];
                            const IdentId original = parser.tokens.names[declaration];
                            const NameId published = code.link.symbols.name[symbol];
                            check_test_expect(test, code.link.names.length[published] == intern_length(&identifiers, original) &&
                                                   memcmp(code.link.names.bytes[published], intern_bytes(&identifiers, original), code.link.names.length[published]) == 0,
                                              "ignored late asm label preserves the independently stated original C symbol spelling");
                        }
                    }
                }
            }
            ObjectUnits units = { 0 };
            ObjectSymbols symbols = { 0 };
            emitted = emitted && object_prepare(&units, &code.link, 1, &scratch) && object_map_symbols(&symbols, &code.link, &scratch);
            if (emitted)
            {
                const Object object = object_build(&code.link, &units, &symbols, 0, (ObjectOptions){ 0 }, BUSTER_S8("selected-source.c"), &scratch);
                bool equal = object.error == OBJECT_ERROR_NONE;
                if (equal && expected == 0)
                {
                    expected = BUSTER_ARENA_PUSH_ARRAY(&expected_storage, uint8_t, object.size);
                    expected_size = object.size;
                    equal = expected != 0;
                    if (equal)
                    {
                        memcpy(expected, object.bytes, object.size);
                    }
                }
                else if (equal)
                {
                    equal = expected_size == object.size && memcmp(expected, object.bytes, object.size) == 0;
                }
                snprintf(description, sizeof(description), "%s: default/lazy and both kernels emit byte-identical ELF", name);
                check_test_expect(test, equal, description);
                arena_pop_to(&scratch, 0);
                if (clang != 0)
                {
                    check_test_expect(test, check_test_program_execute(&code.link, source, clang, &scratch),
                                      "selected real program agrees with independent Clang execution");
                }
            }
            arena_release(&scratch);
            arena_release(&bytes);
            arena_release(&output);
            arena_release(&retained);
            check_test_types_close(&types);
        }
    }
    arena_release(&expected_storage);
    parse_release(&parser);
    pp_release(&pp);
    intern_release(&identifiers);
}

static void check_test_program_selection_case(CheckTest* test, const char* source, const char* name, uint32_t functions, uint32_t objects, const char* clang)
{
    check_test_program_selection_run(test, source, name, functions, objects, CHECK_PROGRAM_OK, clang);
}

static void check_test_program_selection(CheckTest* test, const char* clang)
{
    check_test_program_selection_case(test, "", "empty-selected-unit", 0, 0, 0);
    check_test_program_selection_case(test, "static int dead=3;int kept=9;", "selected-data-only-unit", 0, 1, 0);
    check_test_program_selection_case(test,
        "int pick(int x){return x+5;}double pick(double) __attribute__((overloadable));int tested(int n){return pick(n);}",
        "selected-unmarked-overload-identity-excludes-lexical-placeholder", 2, 0, clang);
    check_test_program_selection_case(test,
        "int pick(int x){return x+5;}double pick(double) __attribute__((overloadable));int external_apply(int(*)(int),int);"
        "int tested(int n){int(*p)(int)=pick;int(*q)(int);q=&pick;return external_apply(p,n)+external_apply(q,n)+"
        "external_apply((int(*)(int))pick,n)+external_apply(pick,n)+external_apply(_Generic(0,int:pick,default:0),n);}",
        "contextual-overload-local-initializer-assignment-cast-and-argument", 2, 0, clang);
    check_test_program_selection_case(test,
        "int pick(int x){return x+5;}double pick(double) __attribute__((overloadable));int external_apply(int(*)(int),int);"
        "int(*saved)(int)=pick;int(*give(void))(int){return pick;}int tested(int n){return external_apply(saved,n)+external_apply(give(),n);}",
        "contextual-overload-static-initializer-and-return", 3, 1, clang);
    check_test_program_selection_run(test,
        "int pick(int x){return x+5;}double pick(double) __attribute__((overloadable));double take(double(*)(double));"
        "int tested(int n){return (int)take(pick)+n;}",
        "active-contextual-overload-address-needs-mangled-publication", 2, 0, CHECK_PROGRAM_UNSUPPORTED, 0);
    check_test_program_selection_case(test,
        "static int dead(int x) __attribute__((overloadable)){return x+1;}int tested(int n){return n;}",
        "dropped-overload-definition-needs-no-mangled-publication", 1, 0, clang);
    check_test_program_selection_run(test,
        "int pick(int x) __attribute__((overloadable)){return x+1;}int tested(int n){return n;}",
        "active-overload-definition-requires-mangled-publication", 2, 0, CHECK_PROGRAM_UNSUPPORTED, 0);
    check_test_program_selection_case(test,
        "# 1 \"selection-header.h\" 1 3\n"
        "static int leaf(int n){return n+3;}static int middle(int n){return leaf(n);}"
        "static __int128 dead(__int128 n){return n+1;}static int discarded=99;\n"
        "# 1 \"selection-user.c\" 2\n"
        "static int(*callback)(int)=middle;int tested(int n){return callback!=0?middle(n):0;}\n",
        "source-body-address-and-initializer-chain", 3, 1, clang);
    check_test_program_selection_case(test,
        "# 1 \"selection-header.h\" 1 3\n"
        "static int a(void);static int b(void){return a();}static int a(void){return b();}"
        "static int*cycle;static int**other=&cycle;static int*cycle=(int*)&other;\n"
        "# 1 \"selection-user.c\" 2\n"
        "int tested(int n){return n+5;}\n",
        "unreachable-code-and-data-cycles", 1, 0, clang);
    check_test_program_selection_case(test,
        "# 1 \"selection-header.h\" 1 3\n"
        "static int kept __attribute__((used))=3;static int dead=5;static int unused(void){return dead;}\n"
        "# 1 \"selection-user.c\" 2\n"
        "int tested(int n){return n+(\"abc\"[1]);}\n",
        "retained-data-and-live-string-pool", 1, 2, clang);
    check_test_program_selection_case(test,
        "# 1 \"selection-header.h\" 1 3\n"
        "inline int external_only(int n){return n+2;}extern inline int emitted(int n){return n+3;}"
        "static int unused(void){return 1;}\n"
        "# 1 \"selection-user.c\" 2\n"
        "int tested(int n){return emitted(n);}\n",
        "c99-inline-only-and-external-definition", 2, 0, clang);
    check_test_program_selection_case(test,
        "# 1 \"selection-header.h\" 1 3\n"
        "static int leaf(int) __asm__(\"renamed_\" \"leaf\");"
        "static int leaf(int n){static int saved __asm__(\"renamed_data\")=3;return n+saved;}\n"
        "# 1 \"selection-user.c\" 2\n"
        "int tested(int n){return leaf(n);}\n",
        "canonical-function-and-local-static-asm-labels", 2, 1, clang);
    check_test_program_selection_case(test,
        "int x=1;extern int x __asm__(\"late_x\");static int leaf(int n){return n+x;}"
        "static int leaf(int) __asm__(\"late_leaf\");int tested(int n){return leaf(n);}",
        "ignored-late-object-and-function-asm-labels", 2, 1, clang);
    check_test_program_selection_case(test,
        "# 1 \"selection-header.h\" 1 3\nstatic int dead(void){\n#pragma pack(push,1)\nreturn 99;}\n"
        "struct S{char c;int x;};\n#pragma pack(pop)\n# 1 \"selection-user.c\" 2\nint tested(int n){return n+sizeof(struct S);}\n",
        "deferred-body-retains-later-packing-history", 1, 0, clang);
    check_test_program_selection_case(test,
        "extern int unused(int) __attribute__((weak));int tested(int n){return n+sizeof(&unused);}",
        "unevaluated-weak-reference-has-no-publication-obligation", 1, 0, clang);
    check_test_program_selection_case(test,
        "static int dropped __attribute__((section(\".dropped\")))=3;int tested(int n){return n;}",
        "dropped-private-section-has-no-publication-obligation", 1, 0, clang);
    check_test_program_selection_case(test,
        "int kept __attribute__((nocommon));int tested(int n) __attribute__((visibility(\"default\")));int tested(int n){return n+kept;}",
        "default-visibility-and-nocommon-publication", 1, 1, clang);
    check_test_program_selection_run(test,
        "extern int other(int) __attribute__((weak));int tested(int n){return other?other(n):7;}",
        "active-weak-function-truth-requires-weak-publication", 1, 0, CHECK_PROGRAM_UNSUPPORTED, 0);
    check_test_program_selection_run(test,
        "extern int other __attribute__((weak));int tested(int n){return &other?n:7;}",
        "active-weak-object-truth-requires-weak-publication", 1, 0, CHECK_PROGRAM_UNSUPPORTED, 0);
    check_test_program_selection_run(test,
        "static int kept __attribute__((retain))=3;int tested(int n){return n;}",
        "retention-root-requires-object-section-retention", 1, 1, CHECK_PROGRAM_UNSUPPORTED, 0);
    check_test_program_selection_run(test,
        "int tested(int n) __attribute__((visibility(\"hidden\")));int tested(int n){return n;}",
        "hidden-symbol-requires-visibility-publication", 1, 0, CHECK_PROGRAM_UNSUPPORTED, 0);
    check_test_program_selection_run(test,
        "int kept __attribute__((common));int tested(int n){return n;}",
        "explicit-common-requires-common-symbol-publication", 1, 1, CHECK_PROGRAM_UNSUPPORTED, 0);
    check_test_program_selection_run(test,
        "static int kept __attribute__((section(\".kept\")))=3;int tested(int n){return n+kept;}",
        "active-section-requires-named-section-publication", 1, 1, CHECK_PROGRAM_UNSUPPORTED, 0);
}
