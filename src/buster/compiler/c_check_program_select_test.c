// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original end-to-end demand fixtures. Default/lazy checking and scalar/vector schedules must emit byte-identical
// ELF objects after semantic selection. The fixture counts are stated independently of the reachability solver.
// Every object emitted is also compared with clang -O0 -c's of the same source (#129): the same functions, objects
// and string literals (c_lowering_symbols.c), which no fixture defeats with a constant condition's dead arm. The
// constant ifs' fixtures (check_test_program_folded_ifs) select their dead arms' callees, as Tier 0 lowers them, and
// their objects are compared under the model of clang's code instead (CheckProgramOptions.folded_ifs).

// Whether clang -O0 -c's object of `source` defines what `bytes` does (lowering_symbols_compare and _strings, with the
// source's `names`); if not, the differences are printed under `name`. Then each pair of `patches` (a list of strings
// ending in null, if not null itself), a name in the object's string table and one no longer, must make the two
// differ when the second replaces the first, as a wrong name would: the comparison's own check.
static bool check_test_program_symbols(const uint8_t* bytes, uint64_t size, const char* const* patches, const char* source, const char* name,
                                       const LoweringNames* names, const char* clang, Arena* scratch)
{
    const uint64_t mark = scratch->length;
    char directory[] = "/tmp/buster-check-symbols-XXXXXX";
    const bool created = mkdtemp(directory) != 0;
    char path[160];
    char object[160];
    char out[160];
    char err[160];
    snprintf(path, sizeof(path), "%s/fixture.c", directory);
    snprintf(object, sizeof(object), "%s/clang.o", directory);
    snprintf(out, sizeof(out), "%s/out", directory);
    snprintf(err, sizeof(err), "%s/err", directory);
    char* compile[] = { (char*)clang, "-std=gnu17", "-O0", "-c", "-w", "-o", object, path, 0 };
    LoweringSymbols ours = { 0 };
    LoweringSymbols theirs = { 0 };
    bool valid = created && parse_test_write(path, source, strlen(source)) && object_test_run(compile, out, err) == 0;
    valid = valid && lowering_symbols_read(&ours, (String8){ .pointer = (char*)bytes, .length = size }, names, scratch) &&
            lowering_symbols_read_file(&theirs, object, names, scratch);
    char report[2048];
    const uint32_t differences = valid ? lowering_symbols_compare(&ours, "ours", &theirs, "clang", NULL, 0, report, sizeof(report)) : 0;
    char strings[2048];
    const uint32_t string_differences = valid ? lowering_symbols_compare_strings(&ours, "ours", &theirs, "clang", strings, sizeof(strings)) : 0;
    if (!valid || differences != 0 || string_differences != 0)
    {
        fprintf(stderr, "%s: against clang -O0 -c (%s): %s%s", name, valid ? "compared" : "not compared", valid ? report : "", valid ? strings : "\n");
    }
    bool caught = true;
    for (uint32_t p = 0; valid && patches != 0 && patches[p] != 0; p += 2)
    {
        const uint64_t from_length = strlen(patches[p]) + 1;
        const uint64_t to_length = strlen(patches[p + 1]);
        uint8_t* copy = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, size);
        const uint8_t* found = memmem(bytes, size, patches[p], from_length);
        const bool once = found != 0 && memmem(found + 1, size - (uint64_t)(found + 1 - bytes), patches[p], from_length) == 0;
        LoweringSymbols patched = { 0 };
        bool differs = false;
        if (copy != 0 && once && to_length < from_length)
        {
            memcpy(copy, bytes, size);
            memset(copy + (found - bytes), 0, from_length);
            memcpy(copy + (found - bytes), patches[p + 1], to_length);
            differs = lowering_symbols_read(&patched, (String8){ .pointer = (char*)copy, .length = size }, names, scratch) &&
                      lowering_symbols_compare(&patched, "patched", &theirs, "clang", NULL, 0, NULL, 0) != 0;
        }
        if (!differs)
        {
            fprintf(stderr, "%s: %s in place of %s made no difference from clang -O0 -c%s\n", name, patches[p + 1], patches[p], once ? "" : " (not found once)");
        }
        caught &= differs;
    }
    if (created)
    {
        unlink(path);
        unlink(object);
        unlink(out);
        unlink(err);
        rmdir(directory);
    }
    arena_pop_to(scratch, mark);
    return valid && differences == 0 && string_differences == 0 && caught;
}

// A fixture's selection, emitted, with `functions` and `objects` selected, by every path; with `folded`, of clang's model
// (CheckProgramOptions.unselected_operands and folded_ifs).
static void check_test_program_selection_run(CheckTest* test, const char* source, const char* name, uint32_t functions, uint32_t objects,
                                             CheckProgramStatus emission_status, const char* clang, const char* symbols_clang, const char* const* patches,
                                             bool folded)
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
                    check_data_prepare(&data, &input, &stream, &initializers, &types.table, path == 0, &retained, &scratch);
            const CheckProgramOptions options = { .initializers = &initializers, .asms = &asms, .identifiers = &identifiers,
                                                 .initializer_references = data.references, .initializer_reference_count = data.reference_count, .scalar = path == 0,
                                                 .unselected_operands = folded, .folded_ifs = folded };
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
            bool emitted = ready && check_program_emit(&code, program, &parser, &identifiers, selection.data.capacity, path == 0, &output, &bytes, &scratch, NULL, NULL, false, true) &&
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
                    LoweringNames names = { 0 };
                    if (equal && symbols_clang != 0)
                    {
                        // The other paths' objects must be these bytes, so one comparison covers them all.
                        snprintf(description, sizeof(description), "%s: the object defines clang -O0 -c's functions, objects and literals", name);
                        check_test_expect(test, lowering_names_of_input(&names, &input, &parser, &identifiers, &scratch) &&
                                               check_test_program_symbols(object.bytes, object.size, patches, source, name, &names, symbols_clang, &scratch),
                                          description);
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
                    check_test_expect(test, check_test_program_execute(&code.link, source, clang, false, &scratch),
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

// A fixture that must be emitted: its object compared with clang's, and with `execute`, run against clang's build.
static void check_test_program_selection_case(CheckTest* test, const char* source, const char* name, uint32_t functions, uint32_t objects,
                                              const char* clang, bool execute)
{
    check_test_program_selection_run(test, source, name, functions, objects, CHECK_PROGRAM_OK, execute ? clang : 0, clang, 0, false);
}

// #418's CI: a constant if's dead arm. Each fixture's selection selects the dead arms' callees, as Tier 0 lowers them,
// and clang's model selects what clang -O0 -c's object defines, which its emitted object must define too (and run as
// clang's build runs). An arm that a label or an outer case enters is kept, an arm whose cases are its own switch's
// is not, and conditions of a floating or pointer type or with a side effect keep both arms, as clang keeps them.
static void check_test_program_folded_ifs(CheckTest* test, const char* clang)
{
    typedef struct CheckFoldedIf CheckFoldedIf;
    struct CheckFoldedIf
    {
        const char* name;
        const char* source;
        uint32_t functions;
        uint32_t kept;
        uint32_t objects;
    };
    static const CheckFoldedIf fixtures[] = {
        { "folded-if-sizeof-chain",
          "static inline int narrow(int n){return n+1;}static inline int wide(int n){return n+2;}static inline int widest(int n){return n+3;}"
          "int tested(int n){long x=n;int r;if(sizeof(x)==sizeof(int))r=narrow(n);else if(sizeof(x)==sizeof(long))r=wide(n);else r=widest(n);"
          "return r+(int)x-n;}", 4, 2, 0 },
        { "folded-if-dead-arm-with-a-label",
          "static int entered(int n){return n+1;}int tested(int n){if(0){again:n=entered(n);}if(n<3)goto again;return n;}", 2, 2, 0 },
        { "folded-if-dead-arm-with-an-outer-case",
          "static int entered(int n){return n+1;}int tested(int n){switch(n){case 0:if(0){case 1:n=entered(n);}break;}return n;}", 2, 2, 0 },
        { "folded-if-dead-arm-with-its-own-switch",
          "static int inner(int n){return n+1;}int tested(int n){if(0){switch(n){case 1:n=inner(n);}}return n;}", 2, 1, 0 },
        { "folded-if-true-with-an-else",
          "static int then_arm(int n){return n+1;}static int else_arm(int n){return n+2;}int tested(int n){if(1)n=then_arm(n);else n=else_arm(n);return n;}",
          3, 2, 0 },
        { "folded-if-floating-and-pointer-conditions",
          "static int a(int n){return n+1;}static int b(int n){return n+2;}static int c(int n){return n+3;}static int d(int n){return n+4;}static int x;"
          "int tested(int n){if(0.0)n=a(n);else n=b(n);if(&x)n=c(n);else n=d(n);return n+x;}", 5, 5, 1 },
        { "folded-if-condition-with-a-side-effect",
          "static int a(int n){return n+1;}int tested(int n){if((n++,0))n=a(n);return n;}", 2, 2, 0 },
        { "folded-if-false-else-if-chain",
          "static int a(int n){return n+1;}static int b(int n){return n+2;}int tested(int n){if(0){n=a(n);}else if(0){n=b(n);}return n;}", 3, 1, 0 },
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(fixtures); i += 1)
    {
        const CheckFoldedIf* fixture = fixtures + i;
        char name[160];
        check_test_program_selection_run(test, fixture->source, fixture->name, fixture->functions, fixture->objects, CHECK_PROGRAM_OK, clang, 0, 0, false);
        snprintf(name, sizeof(name), "%s-in-clangs-model", fixture->name);
        check_test_program_selection_run(test, fixture->source, name, fixture->kept, fixture->objects, CHECK_PROGRAM_OK, clang, clang, 0, true);
    }
}

static void check_test_program_selection(CheckTest* test, const char* clang)
{
    check_test_program_selection_case(test, "", "empty-selected-unit", 0, 0, clang, false);
    check_test_program_selection_case(test, "static int dead=3;int kept=9;", "selected-data-only-unit", 0, 1, clang, false);
    check_test_program_selection_case(test,
        "int pick(int x){return x+5;}double pick(double) __attribute__((overloadable));int tested(int n){return pick(n);}",
        "selected-unmarked-overload-identity-excludes-lexical-placeholder", 2, 0, clang, true);
    check_test_program_selection_case(test,
        "int pick(int x){return x+5;}double pick(double) __attribute__((overloadable));int external_apply(int(*)(int),int);"
        "int tested(int n){int(*p)(int)=pick;int(*q)(int);q=&pick;return external_apply(p,n)+external_apply(q,n)+"
        "external_apply((int(*)(int))pick,n)+external_apply(pick,n)+external_apply(_Generic(0,int:pick,default:0),n);}",
        "contextual-overload-local-initializer-assignment-cast-and-argument", 2, 0, clang, true);
    check_test_program_selection_case(test,
        "int pick(int x){return x+5;}double pick(double) __attribute__((overloadable));int external_apply(int(*)(int),int);"
        "int(*saved)(int)=pick;int(*give(void))(int){return pick;}int tested(int n){return external_apply(saved,n)+external_apply(give(),n);}",
        "contextual-overload-static-initializer-and-return", 3, 1, clang, true);
    check_test_program_selection_run(test,
        "int pick(int x){return x+5;}double pick(double) __attribute__((overloadable));double take(double(*)(double));"
        "int tested(int n){return (int)take(pick)+n;}",
        "active-contextual-overload-address-needs-mangled-publication", 2, 0, CHECK_PROGRAM_UNSUPPORTED, 0, 0, 0, false);
    check_test_program_selection_case(test,
        "static int dead(int x) __attribute__((overloadable)){return x+1;}int tested(int n){return n;}",
        "dropped-overload-definition-needs-no-mangled-publication", 1, 0, clang, true);
    check_test_program_selection_run(test,
        "int pick(int x) __attribute__((overloadable)){return x+1;}int tested(int n){return n;}",
        "active-overload-definition-requires-mangled-publication", 2, 0, CHECK_PROGRAM_UNSUPPORTED, 0, 0, 0, false);
    check_test_program_selection_case(test,
        "# 1 \"selection-header.h\" 1 3\n"
        "static int leaf(int n){return n+3;}static int middle(int n){return leaf(n);}"
        "static __int128 dead(__int128 n){return n+1;}static int discarded=99;\n"
        "# 1 \"selection-user.c\" 2\n"
        "static int(*callback)(int)=middle;int tested(int n){return callback!=0?middle(n):0;}\n",
        "source-body-address-and-initializer-chain", 3, 1, clang, true);
    check_test_program_selection_case(test,
        "# 1 \"selection-header.h\" 1 3\n"
        "static int a(void);static int b(void){return a();}static int a(void){return b();}"
        "static int*cycle;static int**other=&cycle;static int*cycle=(int*)&other;\n"
        "# 1 \"selection-user.c\" 2\n"
        "int tested(int n){return n+5;}\n",
        "unreachable-code-and-data-cycles", 1, 0, clang, true);
    check_test_program_selection_case(test,
        "# 1 \"selection-header.h\" 1 3\n"
        "static int kept __attribute__((used))=3;static int dead=5;static int unused(void){return dead;}\n"
        "# 1 \"selection-user.c\" 2\n"
        "int tested(int n){return n+(\"abc\"[1]);}\n",
        "retained-data-and-live-string-pool", 1, 2, clang, true);
    check_test_program_selection_case(test,
        "# 1 \"selection-header.h\" 1 3\n"
        "inline int external_only(int n){return n+2;}extern inline int emitted(int n){return n+3;}"
        "static int unused(void){return 1;}\n"
        "# 1 \"selection-user.c\" 2\n"
        "int tested(int n){return emitted(n);}\n",
        "c99-inline-only-and-external-definition", 2, 0, clang, true);
    check_test_program_selection_case(test,
        "# 1 \"selection-header.h\" 1 3\n"
        "static int leaf(int) __asm__(\"renamed_\" \"leaf\");"
        "static int leaf(int n){static int saved __asm__(\"renamed_data\")=3;return n+saved;}\n"
        "# 1 \"selection-user.c\" 2\n"
        "int tested(int n){return leaf(n);}\n",
        "canonical-function-and-local-static-asm-labels", 2, 1, clang, true);
    check_test_program_selection_case(test,
        "int x=1;extern int x __asm__(\"late_x\");static int leaf(int n){return n+x;}"
        "static int leaf(int) __asm__(\"late_leaf\");int tested(int n){return leaf(n);}",
        "ignored-late-object-and-function-asm-labels", 2, 1, clang, true);
    check_test_program_selection_case(test,
        "# 1 \"selection-header.h\" 1 3\nstatic int dead(void){\n#pragma pack(push,1)\nreturn 99;}\n"
        "struct S{char c;int x;};\n#pragma pack(pop)\n# 1 \"selection-user.c\" 2\nint tested(int n){return n+sizeof(struct S);}\n",
        "deferred-body-retains-later-packing-history", 1, 0, clang, true);
    check_test_program_selection_case(test,
        "extern int unused(int) __attribute__((weak));int tested(int n){return n+sizeof(&unused);}",
        "unevaluated-weak-reference-has-no-publication-obligation", 1, 0, clang, true);
    check_test_program_selection_case(test,
        "static int dropped __attribute__((section(\".dropped\")))=3;int tested(int n){return n;}",
        "dropped-private-section-has-no-publication-obligation", 1, 0, clang, true);
    check_test_program_selection_case(test,
        "int kept __attribute__((nocommon));int tested(int n) __attribute__((visibility(\"default\")));int tested(int n){return n+kept;}",
        "default-visibility-and-nocommon-publication", 1, 1, clang, true);
    check_test_program_selection_run(test,
        "extern int other(int) __attribute__((weak));int tested(int n){return other?other(n):7;}",
        "active-weak-function-truth-requires-weak-publication", 1, 0, CHECK_PROGRAM_UNSUPPORTED, 0, 0, 0, false);
    check_test_program_selection_run(test,
        "extern int other __attribute__((weak));int tested(int n){return &other?n:7;}",
        "active-weak-object-truth-requires-weak-publication", 1, 0, CHECK_PROGRAM_UNSUPPORTED, 0, 0, 0, false);
    check_test_program_selection_run(test,
        "static int kept __attribute__((retain))=3;int tested(int n){return n;}",
        "retention-root-requires-object-section-retention", 1, 1, CHECK_PROGRAM_UNSUPPORTED, 0, 0, 0, false);
    check_test_program_selection_run(test,
        "int tested(int n) __attribute__((visibility(\"hidden\")));int tested(int n){return n;}",
        "hidden-symbol-visibility-publication", 1, 0, CHECK_PROGRAM_OK, 0, 0, 0, false);
    check_test_program_selection_run(test,
        "int kept __attribute__((common));int tested(int n){return n;}",
        "explicit-common-requires-common-symbol-publication", 1, 1, CHECK_PROGRAM_UNSUPPORTED, 0, 0, 0, false);
    // A section attribute's definition emits as any other's: check_program_sections gives its atom its section (#619).
    check_test_program_selection_run(test,
        "static int kept __attribute__((section(\".kept\")))=3;int tested(int n){return n+kept;}",
        "active-section-emits-for-named-section-publication", 1, 1, CHECK_PROGRAM_OK, 0, 0, 0, false);
    // #129's kinds of definition, each emitted exactly where clang -O0 -c emits it.
    check_test_program_selection_case(test,
        "static int used(int n){return n+1;}static int unused(int n){return n+2;}static int only_from_dead(int n){return n+3;}"
        "static int dead(int n){return only_from_dead(n)+unused(n);}int tested(int n){return used(n);}",
        "static-referenced-unreferenced-and-only-from-unreferenced", 2, 0, clang, true);
    check_test_program_selection_case(test,
        "inline int inline_only(int n){return n+1;}inline int made_external(int n){return n+2;}extern int made_external(int);"
        "extern inline int extern_inline(int n){return n+3;}static inline int static_used(int n){return n+4;}"
        "static inline int static_unused(int n){return n+5;}int tested(int n){return made_external(n)+extern_inline(n)+static_used(n);}",
        "inline-static-inline-and-extern-inline", 4, 0, clang, true);
    // The call of an inline definition goes to its external symbol, which this unit doesn't define.
    check_test_program_selection_case(test,
        "inline int inline_only(int n){return n+1;}int tested(int n){return inline_only(n);}",
        "inline-definition-called-through-its-external-symbol", 1, 0, clang, false);
    check_test_program_selection_case(test,
        "extern inline __attribute__((gnu_inline)) int gnu_extern(int n){return n+1;}"
        "inline __attribute__((gnu_inline)) int gnu_plain(int n){return n+2;}int tested(int n){return gnu_plain(n);}",
        "gnu-inline-forms", 2, 0, clang, true);
    check_test_program_selection_case(test,
        "static int by_address(int n){return n+1;}static int by_global(int n){return n+2;}static int by_static(int n){return n+3;}"
        "static int by_dead(int n){return n+4;}int(*exported)(int)=by_global;static int(*table[])(int)={by_static};"
        "static int(*dead_table[])(int)={by_dead};int tested(int n){int(*p)(int)=by_address;return p(n)+table[0](n)+exported(n);}",
        "address-taken-and-referenced-only-from-initializers", 4, 2, clang, true);
    check_test_program_selection_case(test,
        "static int used_static=3;static int unused_static=4;static int only_dead=5;static int dead(void){return only_dead+unused_static;}"
        "int global_unused=6;int tentative;static int static_tentative;static int*chain=&used_static;static int**chain_head=&chain;"
        "int tested(int n){return n+*chain+tentative+static_tentative;}",
        "objects-referenced-unreferenced-tentative-and-chained", 1, 5, clang, true);
    check_test_program_selection_case(test,
        "static int dead(void){static int in_dead=1;return in_dead;}"
        "int tested(int n){static int counter;static int unused_local=7;{static int counter=2;n+=counter;}return n+ ++counter;}",
        "block-scope-statics-of-emitted-and-unreferenced-functions", 1, 3, clang, true);
    check_test_program_selection_case(test,
        "static const char*dead(void){return \"only in dead\";}static const char*text=\"in a static initializer\";"
        "static const char*dead_text=\"in an unreferenced initializer\";int tested(int n){const char*s=\"in tested\";return n+s[1]+text[2];}",
        "string-literals-of-emitted-and-unreferenced-definitions", 1, 3, clang, true);
    check_test_program_selection_case(test,
        "static int kept(int n) __attribute__((used));static int kept(int n){return n+1;}static int kept_data __attribute__((used))=3;"
        "int tested(int n){return n;}",
        "used-function-and-object-roots", 2, 1, clang, true);
    // Asm labels that end as a made name's suffix does, beside two statics of one name: each label is exact, the
    // second static's suffix past the one a label spells, and a wrong label, or a made name without its suffix,
    // must differ from clang's object.
    static const char* const label_patches[] = { "tag.1", "tag.2", "tested.x.2", "tested.x", "tested.x.1", "tested.x.3", 0 };
    check_test_program_selection_run(test,
        "static int tagged __asm__(\"tag.1\") __attribute__((used))=1;static int spelled __asm__(\"tested.x.1\") __attribute__((used))=2;"
        "int tested(int n){{static int x=3;n+=x;}{static int x=4;n+=x;}return n+tagged+spelled;}",
        "numeric-suffix-asm-labels-beside-made-names", 1, 4, CHECK_PROGRAM_OK, clang, clang, label_patches, false);
    check_test_program_folded_ifs(test, clang);
}
