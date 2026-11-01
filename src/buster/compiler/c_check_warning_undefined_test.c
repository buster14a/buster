// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original source-reference controls, checked independently with Clang 23. Runtime selection and emitted liveness
// are deliberately unsuitable oracles: the source retains discarded references but excludes language queries.
// Include after c_checker_test.c's helpers and c_check_warning_undefined.c.

typedef struct CheckTestUndefinedFixture CheckTestUndefinedFixture;
struct CheckTestUndefinedFixture
{
    const char* source;
    const char* names[2];
    uint32_t uses[2];
    uint32_t declarations[2];
};

static const CheckTestUndefinedFixture check_test_undefined_fixtures[] =
{
    { "int value;\n" },
    { "static int missing(void);\n" },
    { "static int missing(void){return 1;}int f(void){return missing();}\n" },
    { "extern int missing(void);int f(void){return missing();}\n" },
    { "static int missing(void);int f(void){return missing();}\n", { "missing" } },
    { "static int missing(void);int f(void){return missing()+missing();}\n", { "missing" } },
    { "static int missing(void);static int missing(void);int f(void){return missing();}\n", { "missing" }, { 2 } },
    { "static int missing(void);int f(void){return missing();}static int missing(void){return 1;}\n" },
    { "static int missing(void);int f(void){extern int missing(void);return missing();}\n", { "missing" }, { 2 } },
    { "static int missing(void);int f(int(*missing)(void)){return missing();}\n" },
    { "static int missing(void);int f(void){return 0&&missing();}\n", { "missing" } },
    { "static int missing(void);int f(void){if(0)return missing();return 0;}\n", { "missing" } },
    { "static int missing(void);static int unused(void){return missing();}\n", { "missing" } },
    { "static int missing(void);int f(void){return _Generic(0,int:1,default:missing());}\n", { "missing" } },
    { "static int missing(void);int f(void){return __builtin_choose_expr(1,1,missing());}\n", { "missing" } },
    { "static int missing(void);int(*p[1])(void)={[0]=missing,[0]=0};\n", { "missing" } },
    { "static int missing(void);void f(void){(void)(&missing,0);}\n", { "missing" } },
    { "static int missing(void);void f(void){(void)(missing,0);}\n", { "missing" } },
    { "static int missing(void);int(*p)(void)=missing;int f(void){return missing();}\n", { "missing" } },
    { "static int missing(void);unsigned long n=sizeof(&missing);\n" },
    { "static int missing(void);__typeof__(missing()) value;\n" },
    { "static int missing(void);int f(void){return _Generic(missing(),int:1,default:0);}\n" },
    { "static int missing(void);unsigned long n=sizeof(&missing);__typeof__(missing()) value;"
      "int f(void){return _Generic(0,int:1,default:missing());}int g(void){return missing();}\n", { "missing" }, { 3 } },
    { "static int missing(void)__asm__(\"renamed\");int f(void){return missing();}\n", { "missing" } },
    { "static int missing(void);static int missing(void)__asm__(\"renamed\");int f(void){return missing();}\n", { "missing" }, { 2 } },
    { "static int missing(void)__attribute__((alias(\"target\")));int f(void){return missing();}\n" },
    { "static int missing(void)__attribute__((ifunc(\"target\")));int f(void){return missing();}\n" },
    { "static int missing(void);static int missing(void)__attribute__((alias(\"target\")));int f(void){return missing();}\n" },
    { "static int missing(void)__attribute__((weakref(\"target\")));int f(void){return missing();}\n" },
    { "static int missing(void)__attribute__((weakref));int f(void){return missing();}\n" },
    { "static int missing(void);static int other(void);int f(void){return other()+missing();}\n", { "other", "missing" } },
    { "static int missing(void);int f(void){return __alignof__(int[missing()]);}\n", { "missing" } },
    { "static int missing(void);int f(void){return sizeof(int[missing()]);}\n", { "missing" } },
    { "static int missing(void);int f(void){return sizeof(int[0&&missing()]);}\n", { "missing" } },
    { "static int missing(void);int f(void){return sizeof(int[sizeof(&missing)]);}\n" },
    { "static int missing(void);void f(void){__typeof__(int[missing()])*p=0;(void)p;}\n", { "missing" } },
    { "static int missing(void);int f(void){return __builtin_constant_p(missing());}\n", { "missing" } },
    { "static int missing(void);unsigned long f(void){return __builtin_object_size((void*)(long)missing(),0);}\n", { "missing" } },
    // Clang also counts classify_type operands. That builtin remains an unsupported source-checker boundary;
    // the independent external oracle retains it, while constant_p and object_size exercise the live seam here.
    { "static int missing(void);int f(void){return __alignof__(missing());}\n" },
    { "static int missing(void);int f(int n){return sizeof(int[(missing(),n)]);}\n", { "missing" } },
    { "static int missing(void);void f(int n){int a[(missing(),n)];(void)a;}\n", { "missing" } },
    { "static int missing(void);int a[sizeof missing];\n" },
    { "static int missing(void);int a[sizeof &missing];\n" },
    { "static int missing(void);int f(void){return sizeof(int[sizeof missing]);}\n" },
    { "static int missing(int)__attribute__((overloadable));static int missing(double)__attribute__((overloadable));"
      "int f(void){return missing(1);}\n", { "missing" }, { 2 } },
    { "static int missing(int)__attribute__((overloadable));static int missing(double x)__attribute__((overloadable)){return 1;}"
      "int f(void){return missing(1);}\n", { "missing" }, { 2 } },
    { "static int missing(int x)__attribute__((overloadable)){return 1;}static int missing(double)__attribute__((overloadable));"
      "int f(void){return missing(1);}\n" },
    { "static int missing(int)__attribute__((overloadable));static int missing(double)__attribute__((overloadable));"
      "int(*p)(int)=missing;\n", { "missing" }, { 2 } },
    { "static int missing(int)__attribute__((overloadable));static int missing(double)__attribute__((overloadable));"
      "struct S{int(*p)(int);};struct S s={missing};\n", { "missing" }, { 2 } },
    { "static int missing(int)__attribute__((overloadable));static int missing(double)__attribute__((overloadable));"
      "int(*p)(int)=(int(*)(int))missing;\n", { "missing" }, { 2 } },
    { "static int missing(int)__attribute__((overloadable));static int missing(double)__attribute__((overloadable));"
      "void f(void){int(*p)(int);p=missing;}\n", { "missing" }, { 2 } },
    { "static int missing(int)__attribute__((overloadable));static int missing(double)__attribute__((overloadable));"
      "void use(int(*)(int));void f(void){use(missing);}\n", { "missing" }, { 2 } },
    { "static int missing(int)__attribute__((overloadable));static int missing(double)__attribute__((overloadable));"
      "int(*f(void))(int){return missing;}\n", { "missing" }, { 2 } },
    { "static int missing(int)__attribute__((overloadable));static int missing(double)__attribute__((overloadable));"
      "int f(void){return sizeof(int[0&&missing(1)]);}\n", { "missing" }, { 2 } },
    { "static int missing(int)__attribute__((overloadable));static int missing(double)__attribute__((overloadable));"
      "int f(int n){return sizeof(int[(missing(1),n)]);}\n", { "missing" }, { 2 } },
    { "static int missing(int)__attribute__((overloadable));static int missing(double)__attribute__((overloadable));"
      "struct S{int(*p)(int);};struct S s[1]={[0]={missing},[0]={0}};\n", { "missing" }, { 2 } },
    { "static int missing(int)__attribute__((overloadable));static int missing(double)__attribute__((overloadable));"
      "int f(void){return missing(1.0)+missing(1);}\n", { "missing", "missing" }, { 2, 3 }, { 1, 0 } },
    { "static int missing(int)__attribute__((overloadable));static int missing(double)__attribute__((overloadable));"
      "int f(void){return missing(missing(1.0));}\n", { "missing", "missing" }, { 3, 2 }, { 1, 0 } },
    { "static int missing(int)__attribute__((overloadable));static int missing(double)__attribute__((overloadable));"
      "int f(void){return sizeof(int[0&&missing(missing(1.0))]);}\n", { "missing", "missing" }, { 3, 2 }, { 1, 0 } },
    { "static int missing(int)__attribute__((overloadable));int f(void){return missing(missing(1));}\n", { "missing" } },
    { "static int outer(int);static int missing(int)__attribute__((overloadable));static int missing(double)__attribute__((overloadable));"
      "int f(void){return outer(missing(1));}\n", { "outer", "missing" }, { 1, 2 } },
    { "static int outer(int)__attribute__((overloadable));static int outer(double)__attribute__((overloadable));static int missing(void);"
      "int f(void){return outer(missing());}\n", { "missing", "outer" }, { 1, 2 } },
    { "static int missing(int)__attribute__((overloadable));static long missing(double)__attribute__((overloadable));"
      "int(*p)(int)=_Generic(missing(1.0),long:missing,default:missing(1));\n", { "missing" }, { 4 } },
    { "static int missing(int)__attribute__((overloadable));static long missing(double)__attribute__((overloadable));"
      "int(*p)(int)=_Generic(0,float:missing(1.0),int:missing);\n", { "missing", "missing" }, { 2, 3 }, { 1, 0 } },
    { "static int missing(int)__attribute__((overloadable));static long missing(double)__attribute__((overloadable));"
      "int(*p)(int)=(_Generic(missing(1.0),long:(((missing))),default:missing(1)));\n", { "missing" }, { 4 } },
    { "static int missing(int)__attribute__((overloadable));static long missing(double)__attribute__((overloadable));"
      "int(*p)(int)=_Generic(missing(1.0),long:_Generic(0,int:missing,default:missing(1.0)),default:missing(1));\n",
      { "missing", "missing" }, { 4, 5 }, { 1, 0 } },
};

static uint32_t check_test_undefined_token(const Parser* parser, const Preprocessor* pp, const char* name, uint32_t occurrence)
{
    uint32_t result = BUSTER_CHECK_NONE;
    for (uint32_t token = 0; result == BUSTER_CHECK_NONE && token < parser->tokens.count; token += 1)
    {
        const uint32_t id = parser->tokens.names[token];
        if (parser->tokens.kinds[token] == PARSE_KIND_IDENTIFIER && id < pp->table->count &&
            intern_length(pp->table, id) == strlen(name) && memcmp(intern_bytes(pp->table, id), name, strlen(name)) == 0)
        {
            if (occurrence == 0)
            {
                result = token;
            }
            else
            {
                occurrence -= 1;
            }
        }
    }
    return result;
}

static void check_test_warning_undefined(CheckTest* test)
{
    for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(check_test_undefined_fixtures); c += 1)
    {
        const CheckTestUndefinedFixture* fixture = check_test_undefined_fixtures + c;
        const uint32_t wanted = (fixture->names[0] != NULL) + (fixture->names[1] != NULL);
        for (uint32_t mode = 0; mode < 4; mode += 1)
        {
            const uint32_t scalar = mode & 1;
            static InternTable identifiers;
            static Preprocessor pp;
            static Parser parser;
            CheckTestTypes types = { 0 };
            Arena retained = arena_reserve(UINT64_C(1) << 26);
            Arena output = arena_reserve(UINT64_C(1) << 26);
            Arena semantic = arena_reserve(UINT64_C(1) << 26);
            Arena scratch = arena_reserve(UINT64_C(1) << 26);
            const PPOptions preprocessing = { .language = { .standard = 2017, .gnu = true }, .semantic_pragmas = true,
                                              .no_standard_includes = true, .quiet = true };
            const ParseOptions parsing = { .extension_metadata = true, .scalar = scalar != 0, .tree_only = mode >= 2 };
            const CheckOptions checking = { .verify = true, .scalar = scalar != 0 };
            CheckInput input = { 0 };
            CheckStream stream = { 0 };
            CheckInitializers initializers = { 0 };
            CheckWarningBuffer initializer_events = { .arena = &semantic };
            bool ready = retained.base != NULL && output.base != NULL && semantic.base != NULL && scratch.base != NULL && check_test_types_open(&types) &&
                         intern_initialize(&identifiers) && pp_initialize(&pp, &preprocessing, &identifiers) &&
                         parse_initialize(&parser, &parsing, preprocessing.language) &&
                         pp_run_source(&pp, "undefined-warnings.c", (const uint8_t*)fixture->source, strlen(fixture->source)) &&
                         parse_translation_unit(&parser, &pp) && check_input(&input, &parser, &pp, &types.table, &retained, &scratch) &&
                         check_graph_run(&types.table, &input.graph, &output, &scratch, &checking, &stream) && stream.report_count == 0 &&
                         check_initializers_run_events(&initializers, &input, &stream, &parser, &types.table, &output, &scratch, &initializer_events, NULL);
            const CheckWarningEvents initializer_view = check_warning_view(&initializer_events);
            CheckInput uses = { 0 };
            ready = ready && check_initializers_warning_uses(&uses, &input, &initializer_view, &semantic);
            CheckWarningBuffer facts = { .arena = &output };
            const CheckWarningEvent sentinel = {
                .id = CHECK_WARNING_ID_LITERAL_RANGE,
                .kind = CHECK_WARNING_EVENT_MESSAGE,
                .token = 0,
                .source = BUSTER_CHECK_WARNING_NO_SOURCE,
                .completion_kind = CHECK_WARNING_COMPLETE_TU_END,
                .completion = BUSTER_CHECK_WARNING_NO_SOURCE,
                .payload = { .token = BUSTER_CHECK_WARNING_NO_SOURCE },
            };
            ready = ready && check_warning_append(&facts, &sentinel, NULL, 0);
            const CheckWarningBuffer before = facts;
            const uint64_t mark = scratch.length;
            ready = ready && check_warning_undefined_internal(&facts, &uses, &parser, &types.table, &stream, &scratch);
            bool exact = ready && facts.count == 1 + wanted && facts.note_count == wanted && scratch.length == mark &&
                         before.events[0].kind == CHECK_WARNING_EVENT_MESSAGE && before.events[0].note_count == 0;
            // A source the reader rejects (a weakref without its required alias) has no analysis to compare: its
            // expected facts are none, and the rejection itself is tested elsewhere.
            exact |= !ready && wanted == 0 && input.report_count != 0 && facts.count == 0;
            for (uint32_t i = 0; exact && i < wanted; i += 1)
            {
                const CheckWarningEvent* event = facts.events + 1 + i;
                const CheckWarningNote* note = facts.notes + i;
                const uint32_t declaration = check_test_undefined_token(&parser, &pp, fixture->names[i], fixture->declarations[i]);
                const uint32_t use = check_test_undefined_token(&parser, &pp, fixture->names[i], fixture->uses[i] != 0 ? fixture->uses[i] : 1);
                exact = event->id == CHECK_WARNING_ID_UNDEFINED_INTERNAL && event->kind == CHECK_WARNING_EVENT_UNDEFINED_INTERNAL &&
                        event->token == declaration && event->source == BUSTER_CHECK_WARNING_NO_SOURCE && event->flags == 0 &&
                        event->completion_kind == CHECK_WARNING_COMPLETE_TU_END && event->completion == BUSTER_CHECK_WARNING_NO_SOURCE &&
                        event->payload.name == parser.tokens.names[declaration] && event->note_first == i && event->note_count == 1 &&
                        note->kind == CHECK_WARNING_NOTE_USED_HERE && note->token == use && note->source == BUSTER_CHECK_WARNING_NO_SOURCE;
            }
            char description[192];
            snprintf(description, sizeof(description), "undefined source %u (mode %u): first declaration/use and TU completion (%u facts, wanted %u; input %u, stream %u/%u)",
                     c, mode, facts.count, 1 + wanted, input.report_count, stream.status, stream.report_count);
            check_test_expect(test, exact, description);
            if (ready && strstr(fixture->source, "a[sizeof missing]") != NULL)
            {
                bool proven = false;
                for (uint32_t i = 0; i < input.warning_events.count; i += 1)
                {
                    const CheckWarningEvent* event = input.warning_events.events + i;
                    proven |= event->id == CHECK_WARNING_ID_UNDEFINED_INTERNAL && event->kind == CHECK_WARNING_EVENT_MESSAGE &&
                              (event->flags & BUSTER_CHECK_WARNING_SUPPRESSED) != 0;
                }
                check_test_expect(test, proven, "the source reader retains an authoritative sizeof proof before derived operand ends disappear");
            }
            if (exact && c == 4)
            {
                CheckWarningBuffer proofs = { .arena = &semantic };
                const uint32_t use = facts.notes[0].token;
                CheckWarningEvent proof = {
                    .id = CHECK_WARNING_ID_UNDEFINED_INTERNAL,
                    .kind = CHECK_WARNING_EVENT_MESSAGE,
                    .token = use,
                    .source = BUSTER_CHECK_WARNING_NO_SOURCE,
                    .flags = BUSTER_CHECK_WARNING_SUPPRESSED,
                    .completion_kind = CHECK_WARNING_COMPLETE_AT_TOKEN,
                    .completion = use,
                    .payload = { .token = BUSTER_CHECK_WARNING_NO_SOURCE, .data = input.objects[facts.events[1].token] },
                };
                bool appended = check_warning_append(&proofs, &proof, NULL, 0);
                proof.flags = 0;
                proof.payload.first_type = input.declared_types[facts.events[1].token];
                appended = appended && check_warning_append(&proofs, &proof, NULL, 0);
                CheckInput proven = uses;
                proven.warning_events = check_warning_view(&proofs);
                CheckWarningBuffer quiet = before;
                check_test_expect(test, appended && check_warning_undefined_internal(&quiet, &proven, &parser, &types.table, &stream, &scratch) &&
                                       quiet.count == before.count && scratch.length == mark,
                                  "an authoritative source unevaluation proof survives a later unflagged selected-identity witness");
            }
            if (ready && c == 2)
            {
                Arena empty = scratch;
                empty.capacity = empty.length;
                CheckWarningBuffer quiet = before;
                check_test_expect(test, check_warning_undefined_internal(&quiet, &input, &parser, &types.table, &stream, &empty) &&
                                       quiet.count == before.count && empty.length == scratch.length,
                                  "a translation unit with only supplied internal functions needs no warning scratch or query index");
                CheckInput malformed = input;
                malformed.objects = NULL;
                check_test_expect(test, !check_warning_undefined_internal(&quiet, &malformed, &parser, &types.table, &stream, &scratch) &&
                                       quiet.count == before.count && scratch.length == mark &&
                                       !check_warning_undefined_internal(NULL, &input, &parser, &types.table, &stream, &scratch),
                                  "missing required input columns or output owners fail without dereferencing them or publishing a batch");
            }
            if (exact && wanted == 2)
            {
                Arena limited = output;
                const uint64_t failed_mark = limited.length;
                limited.capacity = failed_mark + 2 * sizeof(CheckWarningEvent) + 16 * sizeof(CheckWarningNote);
                CheckWarningBuffer failed = before;
                failed.arena = &limited;
                const CheckWarningBuffer old = failed;
                const bool appended = check_warning_undefined_internal(&failed, &uses, &parser, &types.table, &stream, &scratch);
                check_test_expect(test, !appended && memcmp(&failed, &old, sizeof(old)) == 0 && limited.length == failed_mark &&
                                       scratch.length == mark && before.events[0].kind == CHECK_WARNING_EVENT_MESSAGE,
                                  "failure after a staged first warning cannot publish any partial EOF batch or alter its prefix");
                if (strcmp(fixture->names[0], fixture->names[1]) != 0)
                {
                    CheckWarningBuffer conflicting = { .arena = &semantic };
                    const uint32_t use = facts.notes[0].token;
                    CheckWarningEvent selected = {
                        .id = CHECK_WARNING_ID_UNDEFINED_INTERNAL,
                        .kind = CHECK_WARNING_EVENT_MESSAGE,
                        .token = use,
                        .source = BUSTER_CHECK_WARNING_NO_SOURCE,
                        .completion_kind = CHECK_WARNING_COMPLETE_AT_TOKEN,
                        .completion = use,
                        .payload = { .first_type = input.declared_types[facts.events[1].token], .token = BUSTER_CHECK_WARNING_NO_SOURCE,
                                     .data = input.objects[facts.events[1].token] },
                    };
                    bool supplied = check_warning_append(&conflicting, &selected, NULL, 0);
                    selected.payload.data = input.objects[facts.events[2].token];
                    supplied = supplied && check_warning_append(&conflicting, &selected, NULL, 0);
                    CheckInput inconsistent = uses;
                    inconsistent.warning_events = check_warning_view(&conflicting);
                    CheckWarningBuffer rejected = before;
                    const CheckWarningBuffer unchanged = rejected;
                    const uint64_t output_mark = output.length;
                    check_test_expect(test, supplied && !check_warning_undefined_internal(&rejected, &inconsistent, &parser, &types.table, &stream, &scratch) &&
                                           memcmp(&rejected, &unchanged, sizeof(unchanged)) == 0 && output.length == output_mark && scratch.length == mark,
                                      "contradictory selected identities at one source use reject the whole batch and retain its published prefix");
                }
            }
            parse_release(&parser);
            pp_release(&pp);
            intern_release(&identifiers);
            check_test_types_close(&types);
            arena_release(&scratch);
            arena_release(&semantic);
            arena_release(&output);
            arena_release(&retained);
        }
    }
}
