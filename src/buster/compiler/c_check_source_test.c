// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original declaration/statement fixtures run through both production parser/checker schedules and external Clang.
// Include after check_test_source; no Clang implementation or external fixture is incorporated.

static void check_test_source_fp_history(CheckTest* test, bool complex)
{
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    const char* source = complex ?
        "#pragma STDC CX_LIMITED_RANGE ON\ndouble file_on;double f(void){\n"
        "#pragma STDC CX_LIMITED_RANGE OFF\ndouble local_off=0;{\n"
        "#pragma STDC CX_LIMITED_RANGE ON\ndouble inner_on=0;}double restored_off=0;return 0;}double restored_on;\n"
        "#pragma STDC CX_LIMITED_RANGE DEFAULT\ndouble file_default;\n" :
        "#pragma STDC FENV_ACCESS ON\ndouble file_on;double f(void){\n"
        "#pragma STDC FENV_ACCESS OFF\ndouble local_off=0;{\n"
        "#pragma STDC FENV_ACCESS ON\ndouble inner_on=0;}double restored_off=0;return 0;}double restored_on;\n"
        "#pragma STDC FENV_ACCESS DEFAULT\ndouble file_default;\n";
    const PPOptions options = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true };
    bool ready = intern_initialize(&identifiers) && pp_initialize(&pp, &options, &identifiers);
    ready = ready && pp_run_source(&pp, "fp-history.c", (const uint8_t*)source, strlen(source));
    Arena arena = arena_reserve(UINT64_C(1) << 16);
    Arena scratch = arena_reserve(UINT64_C(1) << 16);
    ready = ready && arena.base != 0 && scratch.base != 0;
    check_test_expect(test, ready, "floating environment history fixture setup");
    for (uint32_t scalar = 0; ready && scalar < 2; scalar += 1)
    {
        const ParseOptions parser_options = { .scalar = scalar != 0 };
        ready = parse_initialize(&parser, &parser_options, options.language) && parse_translation_unit(&parser, &pp);
        check_test_expect(test, ready && parser.tokens.pragma_count == 4, "floating environment history fixture parses");
        const uint64_t arena_capacity = arena.capacity;
        const uint64_t scratch_capacity = scratch.capacity;
        uint64_t required = 0;
        if (ready)
        {
            arena.length = 0;
            memset(arena_push(&arena, arena.capacity, 1), 0xa5, arena.capacity);
            scratch.length = 0;
            memset(arena_push(&scratch, scratch.capacity, 1), 0x5a, scratch.capacity);
        }
        for (uint32_t pass = 0; ready && pass < 3; pass += 1)
        {
            const uint64_t attempts = pass == 1 ? required - 31 : 1;
            for (uint64_t limit = 0; limit < attempts; limit += 1)
            {
                arena.length = 31;
                arena.capacity = pass == 1 ? 31 + limit : arena_capacity;
                scratch.length = 29;
                scratch.capacity = pass == 2 ? 29 : scratch_capacity;
                uint8_t handled[4] = { 0 };
                CheckInput input = { .graph = { .capacity = parser.tokens.count * 2 } };
                CheckInputReader reader = { .output = &input, .parser = &parser, .pp = &pp, .arena = &arena, .scratch = &scratch };
                const bool valid = check_input_fp_pragmas(&reader, handled);
                if (pass == 0)
                {
                    required = arena.length;
                    check_test_expect(test, valid && !input.resource && input.fp_pragma_count == 4 &&
                                      input.fp_pragmas != 0 && input.fp_environments != 0 && input.cx_limited_ranges != 0 && input.graph.fp_environments != 0 &&
                                      handled[0] == 2 && handled[1] == 2 && handled[2] == 2 && handled[3] == 2 && scratch.length == 29,
                                      "floating environment history publishes initialized retained columns");
                    const char* names[] = { "file_on", "local_off", "inner_on", "restored_off", "restored_on", "file_default" };
                    const uint8_t expected[] = { 1, 0, 1, 0, 1, 0 };
                    uint32_t found = 0;
                    for (uint32_t token = 0; valid && token < parser.tokens.count; token += 1)
                    {
                        const uint32_t name = parser.tokens.names[token];
                        for (uint32_t item = 0; name != 0 && item < BUSTER_ARRAY_LENGTH(names); item += 1)
                        {
                            if (intern_length(&identifiers, name) == strlen(names[item]) && memcmp(intern_bytes(&identifiers, name), names[item], strlen(names[item])) == 0)
                            {
                                found += 1;
                                check_test_expect(test, input.fp_environments[token] == (complex ? 0 : expected[item]) &&
                                                  input.cx_limited_ranges[token] == (complex ? expected[item] : 0),
                                                  "floating policy restores its enclosing scope without changing the other policy");
                            }
                        }
                    }
                    check_test_expect(test, found == BUSTER_ARRAY_LENGTH(names), "floating environment metadata fixture visits every marker");
                    for (uint32_t pragma = 0; valid && pragma < input.fp_pragma_count; pragma += 1)
                    {
                        check_test_expect(test, input.fp_pragmas[pragma].kind == (complex ? CHECK_FP_PRAGMA_CX_LIMITED_RANGE : CHECK_FP_PRAGMA_FENV_ACCESS) &&
                                          input.fp_pragmas[pragma].warnings == 0 && input.fp_pragmas[pragma].valid,
                                          "floating policy records retain the exact directive kind");
                    }
                    ready = valid && required > 31;
                }
                else
                {
                    check_test_expect(test, !valid && input.resource && input.fp_pragmas == 0 && input.fp_environments == 0 &&
                                      input.cx_limited_ranges == 0 && input.graph.fp_environments == 0 && input.fp_pragma_count == 0 && arena.length == 31 && scratch.length == 29 &&
                                      handled[0] == 0 && handled[1] == 0 && handled[2] == 0 && handled[3] == 0,
                                      "floating environment exhaustion rolls back columns and handled state");
                }
            }
        }
        arena.capacity = arena_capacity;
        scratch.capacity = scratch_capacity;
        parse_release(&parser);
    }
    arena_release(&scratch);
    arena_release(&arena);
    pp_release(&pp);
    intern_release(&identifiers);
}

static void check_test_source_builtin_effects(CheckTest* test)
{
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    const char* source =
        "int side(void);int f(int x){return __builtin_constant_p(1?1.0/3.0:side())+"
        "__builtin_constant_p(0&&side())+__builtin_constant_p(sizeof(side()))+"
        "__builtin_constant_p(_Generic(side(),int:1.0/3.0))+"
        "__builtin_constant_p(__builtin_choose_expr(1,1.0/3.0,side()))+"
        "__builtin_constant_p(__builtin_constant_p(side()))+__builtin_constant_p(x++ + 1.0/3.0);}";
    const bool expected[] = { true, true, false, false, false, true, false, true };
    const PPOptions options = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true };
    bool ready = intern_initialize(&identifiers) && pp_initialize(&pp, &options, &identifiers);
    ready = ready && pp_run_source(&pp, "constant-p-syntax.c", (const uint8_t*)source, strlen(source));
    check_test_expect(test, ready, "builtin syntax-effect fixture preprocessing");
    for (uint32_t scalar = 0; ready && scalar < 2; scalar += 1)
    {
        CheckTestTypes types = { 0 };
        Arena arena = arena_reserve(UINT64_C(1) << 24);
        Arena scratch = arena_reserve(UINT64_C(1) << 24);
        const ParseOptions parse_options = { .scalar = scalar != 0 };
        CheckInput input = { 0 };
        const bool parsed = check_test_types_open(&types) && arena.base != 0 && scratch.base != 0 &&
                            parse_initialize(&parser, &parse_options, options.language) && parse_translation_unit(&parser, &pp);
        const bool adapted = parsed && check_input(&input, &parser, &pp, &types.table, &arena, &scratch);
        check_test_expect(test, adapted && !input.resource, "builtin syntax-effect source adaptation");
        uint32_t found = 0;
        uint32_t first = BUSTER_CHECK_NONE;
        for (uint32_t call = 0; adapted && call < input.graph.builtin_count; call += 1)
        {
            const CheckBuiltinCall* evidence = input.graph.builtin_calls + call;
            const uint32_t node = evidence->node;
            if (input.graph.ops[node] == CHECK_OP_BUILTIN_QUERY && input.graph.members[node] == CBUILTIN_CONSTANT_P)
            {
                check_test_expect(test, found < BUSTER_ARRAY_LENGTH(expected) && evidence->constant_p_side_effects == expected[found],
                                  "constant_p syntax keeps conditional calls but ignores truly unevaluated operands");
                first = first == BUSTER_CHECK_NONE ? input.graph.argument_nodes[input.graph.argument_offsets[node]] : first;
                found += 1;
            }
        }
        check_test_expect(test, found == BUSTER_ARRAY_LENGTH(expected), "builtin syntax-effect fixture visits all nested calls");
        if (adapted && first != BUSTER_CHECK_NONE)
        {
            CheckInput limited = { .graph = input.graph };
            CheckInputReader reader = { .output = &limited, .scratch = &scratch };
            const uint64_t capacity = scratch.capacity;
            scratch.capacity = scratch.length;
            const bool effects = check_input_builtin_syntax_effects(&reader, first);
            check_test_expect(test, !effects && limited.resource && reader.builtin_syntax_count == 0 && reader.builtin_syntax_effects == 0,
                              "builtin syntax-effect exhaustion cannot fabricate a pure-argument witness");
            scratch.capacity = capacity;
        }
        parse_release(&parser);
        check_test_types_close(&types);
        arena_release(&scratch);
        arena_release(&arena);
    }
    pp_release(&pp);
    intern_release(&identifiers);
}

static void check_test_source_transparent_history(CheckTest* test)
{
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    const char* source = "union U{int i;unsigned u;};void take(union U);void before(void){take(1);}"
                         "typedef union U T __attribute__((transparent_union));void after(void){take(1);}";
    const PPOptions options = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true };
    bool ready = intern_initialize(&identifiers) && pp_initialize(&pp, &options, &identifiers);
    ready = ready && pp_run_source(&pp, "transparent-history.c", (const uint8_t*)source, strlen(source));
    check_test_expect(test, ready, "transparent union history fixture preprocessing");
    for (uint32_t scalar = 0; ready && scalar < 2; scalar += 1)
    {
        CheckTestTypes types = { 0 };
        Arena arena = arena_reserve(UINT64_C(1) << 24);
        Arena output = arena_reserve(UINT64_C(1) << 24);
        Arena scratch = arena_reserve(UINT64_C(1) << 24);
        const ParseOptions parse_options = { .scalar = scalar != 0 };
        CheckInput input = { 0 };
        const bool parsed = check_test_types_open(&types) && arena.base != 0 && output.base != 0 && scratch.base != 0 &&
                            parse_initialize(&parser, &parse_options, options.language) && parse_translation_unit(&parser, &pp);
        const bool adapted = parsed && check_input(&input, &parser, &pp, &types.table, &arena, &scratch);
        const uint32_t name = intern_find(&identifiers, (const uint8_t*)"T", 1);
        uint32_t tag = BUSTER_CHECK_NONE;
        for (uint32_t token = 0; adapted && token < parser.tokens.count; token += 1)
        {
            if (parser.tokens.names[token] == name && input.declaration_kinds[token] == C_BINDING_TYPEDEF)
            {
                tag = types.table.types.index[type_index(input.declared_types[token])];
            }
        }
        check_test_expect(test, adapted && !input.resource && input.graph.transparent_unions != 0 && tag < input.graph.tag_completion_count,
                          "transparent union history retains the exact typedef's nominal tag");
        uint32_t calls[2] = { BUSTER_CHECK_NONE, BUSTER_CHECK_NONE };
        uint32_t found = 0;
        for (uint32_t node = 0; adapted && node < input.graph.count; node += 1)
        {
            if (input.graph.ops[node] == CHECK_OP_CALL)
            {
                if (found < BUSTER_ARRAY_LENGTH(calls))
                {
                    calls[found] = node;
                }
                found += 1;
            }
        }
        if (adapted && found == BUSTER_ARRAY_LENGTH(calls) && input.graph.transparent_unions != 0 && tag < input.graph.tag_completion_count)
        {
            const uint32_t activation = input.graph.transparent_unions[tag];
            check_test_expect(test, input.graph.tag_completions[tag] < activation && input.graph.source_positions[calls[0]] < activation &&
                              activation <= input.graph.source_positions[calls[1]],
                              "transparent attribute activation is distinct from the earlier record completion");
            memset(scratch.base + scratch.length, 0xa5, scratch.capacity - scratch.length);
            CheckStream stream = { 0 };
            const CheckOptions check_options = { .scalar = scalar != 0 };
            const bool checked = check_graph_run(&types.table, &input.graph, &output, &scratch, &check_options, &stream);
            check_test_expect(test, checked && stream.report_count == 1 && stream.reports[0].node == calls[0] &&
                              input.graph.transparent_unions[tag] == activation,
                              "recycled scratch cannot retroactively allow the call before the transparent attribute");
        }
        else
        {
            check_test_expect(test, false, "transparent union fixture retains both original call nodes");
        }
        parse_release(&parser);
        check_test_types_close(&types);
        arena_release(&scratch);
        arena_release(&output);
        arena_release(&arena);
    }
    pp_release(&pp);
    intern_release(&identifiers);
}

static void check_test_source_intrinsic_range(CheckTest* test)
{
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    const char* source = "typedef int V __attribute__((vector_size(64)));V f(V x){return __builtin_ia32_pshufd512(x,256);}";
    const PPOptions options = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true };
    bool ready = intern_initialize(&identifiers) && pp_initialize(&pp, &options, &identifiers);
    ready = ready && pp_run_source(&pp, "intrinsic-range.c", (const uint8_t*)source, strlen(source));
    check_test_expect(test, ready, "intrinsic range provenance fixture preprocesses");
    for (uint32_t scalar = 0; ready && scalar < 2; scalar += 1)
    {
        CheckTestTypes types = { 0 };
        Arena arena = arena_reserve(UINT64_C(1) << 24);
        Arena output = arena_reserve(UINT64_C(1) << 24);
        Arena scratch = arena_reserve(UINT64_C(1) << 24);
        const ParseOptions parse_options = { .scalar = scalar != 0 };
        const CheckOptions check_options = { .scalar = scalar != 0 };
        CheckInput input = { 0 };
        CheckStream stream = { 0 };
        const bool parsed = check_test_types_open(&types) && arena.base != 0 && output.base != 0 && scratch.base != 0 &&
                            parse_initialize(&parser, &parse_options, options.language) && parse_translation_unit(&parser, &pp);
        const bool adapted = parsed && check_input(&input, &parser, &pp, &types.table, &arena, &scratch);
        const bool checked = adapted && check_graph_run(&types.table, &input.graph, &output, &scratch, &check_options, &stream);
        check_test_expect(test, checked && !input.resource && stream.report_count == 0 && stream.x86_count == 1 && input.graph.x86_count == 1,
                          "suppressible intrinsic range diagnostics remain retained after successful typing");
        if (checked && stream.x86_count == 1 && input.graph.x86_count == 1)
        {
            const CheckBuiltinX86Effect* effect = stream.x86;
            const CheckBuiltinX86RangeEvent* event = effect->range_events;
            check_test_expect(test, effect->range_event_count == 1 && event->node == effect->node && event->argument == 1 &&
                              event->low == 0 && event->high == 255 && event->value.constant_kind == CHECK_CONSTANT_INTEGER &&
                              event->value.integer == 256 && event->default_error && event->disposition == CHECK_BUILTIN_X86_DEFERRED &&
                              (effect->checked_ice_arguments & 2) != 0 && (input.graph.x86_calls[0].ice_arguments & 2) != 0,
                              "intrinsic range event preserves source ICE, exact bounds and default-error policy for the diagnostic consumer");
        }
        parse_release(&parser);
        check_test_types_close(&types);
        arena_release(&scratch);
        arena_release(&output);
        arena_release(&arena);
    }
    pp_release(&pp);
    intern_release(&identifiers);
}

static __attribute__((noinline)) void check_test_source_extensions(CheckTest* test, const char* clang)
{
    const CheckSourceFixture fixtures[] =
    {
        {
            "generic controls use intrinsic result types",
            "_Static_assert(_Generic(__builtin_ia32_rdpid(),unsigned:1,default:0),\"rdpid type\");\n"
            "int g(unsigned char x){return _Generic(__builtin_rotateleft8(x,1),unsigned char:1,default:0);}\n",
            false,
        },
        {
            "generic intrinsic controls retain narrow types and nested calls",
            "#define T(e,t) _Static_assert(_Generic((e),t:1,default:0),\"intrinsic type\")\n"
            "T(__builtin_rotateright8(1,1),unsigned char);T(__builtin_rotateleft16(1,1),unsigned short);\n"
            "T(__builtin_rotateright32(1,1),unsigned);T(__builtin_rotateleft64(1,1),unsigned long);\n"
            "T(__builtin_rotateleft8(__builtin_rotateright16(1,1),1),unsigned char);\n"
            "T(+__builtin_rotateleft8(1,1),int);\n",
            false,
        },
        {
            "generic intrinsic controls select vector and void results",
            "typedef int V __attribute__((vector_size(64)));\n"
            "int f(V x){return _Generic(__builtin_ia32_pshufd512(x,255),V:1,default:0)+"
            "_Generic(__builtin_ia32_pause(),int:0,default:1);}\n",
            false,
        },
        {
            "intrinsic generic types are available in declaration bounds",
            "enum { N=_Generic(__builtin_ia32_rdpid(),unsigned:3,default:1) };\n"
            "int a[_Generic(__builtin_rotateleft8(1,1),unsigned char:4,default:1)];\n"
            "_Static_assert(N==3 && sizeof(a)==4*sizeof(int),\"declaration bounds\");\n",
            false,
        },
        {
            "intrinsic generic types are available in raw attribute queries",
            "typedef int I __attribute__((aligned(_Generic(__builtin_ia32_rdpid(),unsigned:8,default:4))));\n"
            "_Static_assert(_Alignof(I)==8,\"attribute query\");\n",
            false,
        },
        {
            "intrinsic generic types respect an ordinary shadowing declaration",
            "int f(void){double (*__builtin_rotateleft8)(int,int);return _Generic(__builtin_rotateleft8(1,1),double:1,default:0);}\n",
            false,
        },
        {
            "generic intrinsic control rejects an extra argument",
            "int f(void){return _Generic(__builtin_ia32_rdpid(1),unsigned:1,default:0);}\n",
            true,
        },
        {
            "generic intrinsic control rejects a missing argument",
            "int f(void){return _Generic(__builtin_rotateleft8(1),unsigned char:1,default:0);}\n",
            true,
        },
        {
            "generic intrinsic control rejects an incompatible argument",
            "struct S {int x;};int f(struct S s){return _Generic(__builtin_rotateleft8(s,1),unsigned char:1,default:0);}\n",
            true,
        },
        {
            "generic intrinsic control retains immediate constraints",
            "typedef int V __attribute__((vector_size(64)));int f(V x,int n){return _Generic(__builtin_ia32_pshufd512(x,n),V:1,default:0);}\n",
            true,
        },
        {
            "generic declaration bound retains intrinsic immediate constraints",
            "typedef int V __attribute__((vector_size(64)));int f(V x,int n){int a[_Generic(__builtin_ia32_pshufd512(x,n),V:2,default:1)];return sizeof(a);}\n",
            true,
        },
        {
            "generic intrinsic control retains hard SAE constraints",
            "typedef float V __attribute__((vector_size(64)));int f(V x){return _Generic(__builtin_ia32_rndscaleps_mask(x,0,x,65535,5),V:1,default:0);}\n",
            true,
        },
        {
            "exact intrinsic prototypes preserve distinct result and argument types",
            "void f(void){__builtin_ia32_clui();}unsigned long long g(unsigned long long a){return __builtin_ia32_urdmsr(a);}\n",
            false,
        },
        {
            "exact intrinsic prototype rejects an extra argument",
            "void f(void){__builtin_ia32_clui(1);}\n",
            true,
        },
        {
            "intrinsic immediate boundary accepts its maximum",
            "typedef int V __attribute__((vector_size(64)));V f(V x){return __builtin_ia32_pshufd512(x,255);}\n",
            false,
        },
        {
            "intrinsic immediate range error remains a suppressible deferred diagnostic",
            "typedef int V __attribute__((vector_size(64)));V f(V x){return __builtin_ia32_pshufd512(x,256);}\n",
            false,
        },
        {
            "intrinsic immediate rejects a runtime integer",
            "typedef int V __attribute__((vector_size(64)));V f(V x,int n){return __builtin_ia32_pshufd512(x,n);}\n",
            true,
        },
        {
            "intrinsic immediate retains its source constant evaluation proof",
            "typedef int V __attribute__((vector_size(64)));int side(void);V f(V x){return __builtin_ia32_pshufd512(x,__builtin_expect(1,side()));}\n",
            false,
        },
        {
            "intrinsic rounding argument accepts its defined encoding",
            "typedef float V __attribute__((vector_size(64)));V f(V x){return __builtin_ia32_rndscaleps_mask(x,0,x,65535,4);}\n",
            false,
        },
        {
            "intrinsic rounding argument rejects an undefined encoding",
            "typedef float V __attribute__((vector_size(64)));V f(V x){return __builtin_ia32_rndscaleps_mask(x,0,x,65535,5);}\n",
            true,
        },
        {
            "void intrinsic result can be returned from a void function",
            "void f(unsigned long long a,unsigned long long b){return __builtin_ia32_uwrmsr(a,b);}\n",
            false,
        },
        {
            "void function cannot return a nonvoid intrinsic result",
            "void f(unsigned long long a){return __builtin_ia32_urdmsr(a);}\n",
            true,
        },
        {
            "transparent union converts scalar arguments through its first member",
            "typedef union {int i;float f;} U __attribute__((transparent_union));void take(U);void f(short x){take(x);take(1.5f);}\n",
            false,
        },
        {
            "record transparent union annotation accepts each compatible pointer member",
            "union __attribute__((transparent_union)) U{int *i;double *d;};void take(union U);void f(int *i,double *d){take(i);take(d);take(0);}\n",
            false,
        },
        {
            "transparent union remains a distinct ordinary type",
            "typedef union {int *i;double *d;} U __attribute__((transparent_union));_Static_assert(!__builtin_types_compatible_p(U,int*),\"\");_Static_assert(__builtin_types_compatible_p(void(*)(U),void(*)(int*)),\"\");_Static_assert(__builtin_types_compatible_p(void(*)(double*),void(*)(U)),\"\");\n",
            false,
        },
        {
            "transparent union composite declaration preserves the visible pointer parameter",
            "typedef union {int *i;double *d;} U __attribute__((transparent_union));void take(U);void take(int*);void f(int *p){take(p);}\n",
            false,
        },
        {
            "transparent union composite in reverse order preserves the visible pointer parameter",
            "typedef union {int *i;double *d;} U __attribute__((transparent_union));void take(int*);void take(U);void f(int *p){take(p);}\n",
            false,
        },
        {
            "transparent union definition retains its written local parameter type",
            "typedef union {int *i;double *d;} U __attribute__((transparent_union));int take(int*);int take(U x){return *x.i;}\n",
            false,
        },
        {
            "transparent union composite narrows subsequent calls",
            "typedef union {int *i;double *d;} U __attribute__((transparent_union));void take(U);void take(int*);\nvoid f(double *p){take(p);}\n",
            true,
        },
        {
            "transparent union pointer selection does not discard qualifiers",
            "typedef union {void *v;int *i;} U __attribute__((transparent_union));void take(U);\nvoid f(const int *p){take(p);}\n",
            true,
        },
        {
            "later transparent union attribute does not change an earlier call",
            "union U{int i;unsigned u;};void take(union U);\nvoid before(void){take(1);}\ntypedef union U T __attribute__((transparent_union));\nvoid after(void){take(1);}\n",
            true,
        },
        {
            "completed typedef transparent union attribute permits later calls",
            "union U{int i;unsigned u;};void take(union U);typedef union U T __attribute__((transparent_union));void after(void){take(1);}\n",
            false,
        },
        {
            "later transparent union attribute does not change an earlier function pointer initializer",
            "union U{int *i;double *d;};void take(union U);\nvoid(*before)(int*)=take;\ntypedef union U T __attribute__((transparent_union));\n",
            true,
        },
        {
            "transparent union history permits a later compatible function pointer initializer",
            "union U{int *i;double *d;};void take(union U);typedef union U T __attribute__((transparent_union));void(*after)(int*)=take;\n",
            false,
        },
        {
            "incomplete transparent union attribute remains ignored after completion",
            "typedef union U U __attribute__((transparent_union));union U{int i;unsigned u;};void take(U);\nvoid f(void){take(1);}\n",
            true,
        },
        {
            "floating first member prevents transparent union argument conversion",
            "typedef union {float f;int i;} U __attribute__((transparent_union));void take(U);\nvoid f(void){take(1);}\n",
            true,
        },
        {
            "unequal member layout prevents transparent union argument conversion",
            "typedef union {int *p;int i;} U __attribute__((transparent_union));void take(U);\nvoid f(int *p){take(p);}\n",
            true,
        },
        {
            "complex range permission restores lexical state",
            "#pragma STDC CX_LIMITED_RANGE ON\n_Complex double f(_Complex double z){\n#pragma STDC CX_LIMITED_RANGE OFF\n{\n#pragma STDC CX_LIMITED_RANGE ON\nz=z*z;}return z;}\n#pragma STDC CX_LIMITED_RANGE DEFAULT\n",
            false,
        },
        {
            "unknown floating pragma states warn and leave the setting unchanged",
            "#pragma STDC FENV_ACCESS UNKNOWN\n#pragma STDC CX_LIMITED_RANGE UNKNOWN\nint f(void){int x=1;\n#pragma STDC FENV_ACCESS BAD\n#pragma STDC CX_LIMITED_RANGE BAD\nreturn x;}\n",
            false,
        },
        {
            "complex range late block setting is rejected",
            "int f(void){int x=0;\n#pragma STDC CX_LIMITED_RANGE ON\nreturn x;}\n",
            true,
        },
        {
            "complex range setting cannot split a declaration",
            "int\n#pragma STDC CX_LIMITED_RANGE ON\nx;\n",
            true,
        },
        {
            "recognized floating setting with trailing token is rejected",
            "#pragma STDC FENV_ACCESS ON extra\nint x;\n",
            true,
        },
        {
            "recognized complex range setting with trailing token is rejected",
            "#pragma STDC CX_LIMITED_RANGE ON extra\nint x;\n",
            true,
        },
        {
            "typedef redefinition requires identical enum type",
            "typedef enum { X } E;\ntypedef unsigned E;\n",
            true,
        },
        {
            "typedef pointer redefinition keeps enum identity",
            "typedef enum { X } E;typedef E *P;\ntypedef unsigned *P;\n",
            true,
        },
        {
            "typedef array redefinition cannot supply a different bound",
            "typedef int A[];\ntypedef int A[3];\n",
            true,
        },
        {
            "typedef function redefinition cannot add a prototype",
            "typedef int F();\ntypedef int F(int);\n",
            true,
        },
        {
            "typedef canonical identity ignores nested alignment attributes",
            "typedef int I __attribute__((aligned(8)));typedef int I;typedef I *P;typedef int *P;P p;\n",
            false,
        },
        {
            "floating environment lexical scopes",
            "#pragma STDC FENV_ACCESS ON\nfloat x=1.0f/3.0f;float f(float a){\n#pragma STDC FENV_ACCESS OFF\n{\n#pragma STDC FENV_ACCESS ON\na+=1.0f/3.0f;}return a+1.0f/3.0f;}\n#pragma STDC FENV_ACCESS DEFAULT\nfloat g(void){return 1.0f/3.0f;}\n",
            false,
            false
        },
        {
            "floating environment pragma requires compound beginning",
            "int f(void){int x=0;_Pragma(\"STDC FENV_ACCESS ON\")return x;}\n",
            true,
            false
        },
        {
            "floating environment pragma cannot split file declaration",
            "int\n#pragma STDC FENV_ACCESS ON\nx;\n",
            true,
            false
        },
        {
            "floating environment pragma cannot split file initializer",
            "double x=\n#pragma STDC FENV_ACCESS ON\n1.0;\n",
            true,
            false
        },
        {
            "floating environment pragma cannot split parameter declaration",
            "int f(int\n#pragma STDC FENV_ACCESS ON\nx);\n",
            true,
            false
        },
        {
            "empty record member declarations retain layout",
            "struct S{;char a[63];;unsigned long n;;;};_Static_assert(sizeof(struct S)==72,\"layout\");\n",
            false,
            false
        },
        {
            "incomplete record member reports its own declaration",
            "struct S {\nint a;\nstruct Missing m;\n};\n",
            true,
            false
        },
        {
            "invalid bit-field width reports its own declaration",
            "struct S {\nint a;\nint v:99;\n};\n",
            true,
            false
        },
        {
            "nonfinal flexible member reports its own declaration",
            "struct S {\nint a[];\nint b;\n};\n",
            true,
            false
        },
        {
            "expanded anonymous member collision reports the later member",
            "struct S {\nstruct {int x;};\nstruct {int x;};\n};\n",
            true,
            false
        },
        {
            "duplicate declaration modifiers retain warning-only semantics",
            "typedef typedef unsigned unsigned I; extern extern signed signed int x; static static short short y; _Thread_local _Thread_local int z; _Complex _Complex double c; _Atomic _Atomic int a; I f(I n){return n+(unsigned unsigned)3+y;}\n",
            false,
            false
        },
        {
            "conflicting sign specifiers remain invalid",
            "signed unsigned int x;\n",
            true,
            false
        },
        {
            "invalid third long reports its specifier line",
            "long long long\nf(void){return 0;}\n",
            true,
            false
        },
        {
            "incompatible floating specifier reports its own line",
            "unsigned float\nf(void){return 0;}\n",
            true,
            false
        },
        {
            "GNU extension prefix preserves operand category",
            "_Static_assert(__extension__ 3==3,\"value\"); int f(int x){(__extension__ x)=3;return __extension__ ({int y=x;y+1;});}\n",
            false,
            false
        },
        {
            "predefined function names retain static array types",
            "_Static_assert(sizeof __func__==1,\"file\"); _Static_assert(sizeof __PRETTY_FUNCTION__==10,\"top level\"); int long_name(int n){_Static_assert(sizeof __func__==10,\"name\");static const char *p=__func__;return p[n]+__FUNCTION__[0]+__PRETTY_FUNCTION__[0];}\n",
            false,
            false
        },
        {
            "predefined function names are const arrays",
            "int f(void){__func__[0]='x';return 0;}\n",
            true,
            false
        },
        {
            "predefined function name is not an array initializer literal",
            "int f(void){char name[]=__func__;return sizeof name;}\n",
            true,
            false
        },
        {
            "typeof statement expression local declarations",
            "int f(int n){__typeof__(({int x=n; x;})) t=1; return t;} int g(int n){const __typeof__(({long x=n; __typeof__(({short y=2;y;})) z=3; x+z;})) t=1; return t;}\n",
            false,
            false
        },
        {
            "typeof statement expression retains scope",
            "int f(int n){__typeof__(({int x=n; x;})) t=1; return x+t;}\n",
            true,
            false
        },
        {
            "typeof statement expression final declaration is void",
            "int f(void){__typeof__(({int x=1;})) t;return 0;}\n",
            true,
            false
        },
        {
            "string subobject bounds are source constants",
            "_Static_assert(__builtin_object_size(\"abc\",1)==4,\"max\"); _Static_assert(__builtin_object_size(\"abc\",3)==4,\"min\"); _Static_assert(!__builtin_constant_p(__builtin_object_size(\"abc\",0)),\"deferred\"); _Static_assert(__builtin_constant_p(1.0+2.0i),\"complex\");\n",
            false,
            false
        },
        {
            "string whole object maximum is not a source constant",
            "_Static_assert(__builtin_object_size(\"abc\",0)==4,\"whole\");\n",
            true,
            false
        },
        {
            "string whole object minimum is not a source constant",
            "_Static_assert(__builtin_object_size(\"abc\",2)==4,\"whole\");\n",
            true,
            false
        },
        {
            "builtin expect source constant evaluation",
            "int side(void); enum { N=__builtin_expect(3,side())+2 }; int a[N]; _Static_assert(N==5,\"enum\"); _Static_assert(__builtin_expect(3,side())+2==5,\"assert\"); static long x=__builtin_expect(7,side())+1; static int b[]={__builtin_expect(9,side())}; long f(long n){return __builtin_expect(n,side());}\n",
            false,
            false
        },
        {
            "builtin string and object witnesses",
            "int a[7]; _Static_assert(__builtin_constant_p(\"abc\"),\"literal\"); _Static_assert(__builtin_constant_p((unsigned long)\"abc\"),\"cast\"); _Static_assert(!__builtin_constant_p(\"abc\"+1),\"interior\"); _Static_assert(!__builtin_constant_p(&a),\"object\"); _Static_assert(__builtin_object_size(a,0)==sizeof a,\"whole max\"); _Static_assert(__builtin_object_size(a,1)==sizeof a,\"sub max\"); _Static_assert(__builtin_object_size(a,2)==sizeof a,\"whole min\"); _Static_assert(__builtin_object_size(a,3)==sizeof a,\"sub min\"); unsigned long f(void){return __builtin_object_size(\"abc\",0);}\n",
            false,
            false
        },
        {
            "floating builtin constants and classify",
            "static double inf=__builtin_huge_val(); static float nan=__builtin_nanf(\"0x123\"); _Static_assert(__builtin_isinf(__builtin_inf()),\"inf\"); _Static_assert(__builtin_isnan(__builtin_nan(\"\")),\"nan\"); _Static_assert(__builtin_fpclassify(1,2,3,4,5,0.0)==5,\"zero\");\n",
            false,
            false
        },
        {
            "builtin expect rejects wrong arity",
            "long f(void){return __builtin_expect(1);}\n",
            true,
            false
        },
        {
            "builtin expect still checks discarded constant argument type",
            "struct S{int x;}; _Static_assert(__builtin_expect(1,(struct S){0}),\"type\");\n",
            true,
            false
        },
        {
            "object size rejects invalid mode",
            "int a[4]; unsigned long n=__builtin_object_size(a,4);\n",
            true,
            false
        },
        {
            "pointer array strings infer object count",
            "const char *items[]={\"label\",\"x\"}; _Static_assert(sizeof items==2*sizeof(char*),\"count\");\n",
            false,
            false
        },
        {
            "calling convention between pointer and name",
            "void *__cdecl f(void *p){return p;} int (__cdecl *callback)(int);\n",
            false,
            false
        },
        {
            "conditional middle operand comma",
            "int put(int*,int); int f(int *p,int n){return (unsigned char)((n<128)?(*p=n),1:put(p,n));}\n",
            false,
            false
        },
        {
            "goto cannot enter statement expression",
            "int f(void){goto entered;return ({entered: 3;});}\n",
            true,
            false
        },
        {
            "switch cannot enter statement expression",
            "int f(int n){switch(n){return ({case 0: 3;});}return 0;}\n",
            true,
            false
        },
        {
            "statement expression pure folded assertion",
            "int f(void){_Static_assert(({1;}),\"fold\");return ({3;});}\n",
            false,
            false
        },
        {
            "condition statement expression cannot break its pending loop",
            "int f(void){while(({break;1;})){}return 0;}\n",
            true,
            false
        },
        {
            "varargs list and type operands",
            "typedef __builtin_va_list V; int f(int n,...){V x,y;__builtin_va_start(x,n);__builtin_va_copy(y,x);int r=__builtin_va_arg(y,int);__builtin_va_end(y);__builtin_va_end(x);return r;}\n",
            false,
            false
        },
        {
            "varargs start requires variadic function",
            "typedef __builtin_va_list V; void f(int n){V x;__builtin_va_start(x,n);}\n",
            true,
            false
        },
        {
            "varargs rejects unrelated list",
            "int f(int n,...){int x;__builtin_va_start(x,n);return 0;}\n",
            true,
            false
        },
        {
            "parameter array pointer qualifiers",
            "void f(int x[const 5]); void f(int *const x); void g(int x[static restrict 5]); void g(int *restrict x); void h(int x[const *]); void h(int *const x); void grouped(int (x[const 3]));\n",
            false,
            false
        },
        {
            "parameter array qualifier applies to local pointer",
            "void f(int x[const 5]){x=0;}\n",
            true,
            false
        },
        {
            "parameter array star cannot define function",
            "void f(int x[const *]){x[0]=1;}\n",
            true,
            false
        },
        {
            "inner array qualifier is invalid",
            "void f(int x[3][const 4]);\n",
            true,
            false
        },
        {
            "attribute prefixed cast type",
            "int f(void *p){return ((__attribute__((noinline)) int(*)(void))p)();}\n",
            false,
            false
        },
        {
            "attribute inside grouped abstract declarator",
            "int f(void *p){return ((int(__attribute__((noinline)) *)(void))p)();}\n",
            false,
            false
        },
        {
            "final record member without semicolon",
            "struct S{int member}; struct S s; _Static_assert(sizeof s==sizeof(int),\"member\");\n",
            false,
            false
        },
        {
            "thread local declaration identity",
            "extern _Thread_local int x; _Thread_local int x=3; int f(void){static _Thread_local int y; return x+y;}\n",
            false,
            false
        },
        {
            "thread local redeclaration mismatch",
            "extern _Thread_local int x; int x;\n",
            true,
            false
        },
        {
            "block thread local requires storage",
            "void f(void){_Thread_local int x;}\n",
            true,
            false
        },
        {
            "concatenated and wide strings",
            "char a[]=\"ab\" \"cd\"; unsigned short u[]=u\"x\"; _Static_assert(sizeof a==5,\"a\"); _Static_assert(sizeof u==4,\"u\");\n",
            false,
            false
        },
        {
            "sparse local inferred array",
            "int f(void){int a[]={[3]=7,9}; _Static_assert(sizeof a==20,\"extent\"); return a[4];}\n",
            false,
            false
        },
        {
            "anonymous member lookup",
            "struct S{union{int x;long y;}; int z;}; int f(struct S *p){return p->x+p->z;}\n",
            false,
            false
        },
        {
            "anonymous member duplicate",
            "struct S{struct{int x;}; int x;};\n",
            true,
            false
        },
        {
            "anonymous member supports flexible successor",
            "struct S{struct{int x;}; int values[];}; _Static_assert(sizeof(struct S)==4,\"flexible\");\n",
            false,
            false
        },
        {
            "compound scalar lvalue",
            "int f(void){int *p=&(int){7}; return *p;}\n",
            false,
            false
        },
        {
            "compound array extent",
            "int f(void){return sizeof((int[]){1,2,3});}\n",
            false,
            false
        },
        {
            "function labels separate from objects",
            "int f(int n){goto done; done: return n;} int g(void){int done=3; done: return done;}\n",
            false,
            false
        },
        {
            "duplicate function label",
            "int f(void){x:; {x:;} return 0;}\n",
            true,
            false
        },
        {
            "missing function label",
            "int f(void){goto absent; return 0;}\n",
            true,
            false
        },
        {
            "nested switch does not leak expression values",
            "int f(int x){switch(x){case 1: return 2; default: switch(x+1){case 2:return 3; default:return 4;}}}\n",
            false,
            false
        },
        {
            "typeof preserves pointed qualifiers",
            "const int x=1; __typeof__(&x) p=&x; int f(void){return *p;}\n",
            false,
            false
        },
        {
            "unsigned bit precise declaration",
            "unsigned _BitInt(17) value; _Static_assert(sizeof value==4,\"bits\");\n",
            false,
            false
        },
        {
            "callback parameter",
            "int f(int (*cb)(int), int x) { return cb(x); }\n",
            false,
            false
        },
        {
            "function parameter adjustment",
            "int f(int cb(int), int x) { return cb(x); }\n",
            false,
            false
        },
        {
            "function returning callback",
            "int (*f(int (*cb)(int)))(int) { return cb; }\n",
            false,
            false
        },
        {
            "array of callbacks",
            "int (*callbacks[2])(int); int f(void) { return callbacks[1](3); }\n",
            false,
            false
        },
        {
            "callback static initializer",
            "int f(int x) { return x; } int (*p)(int) = f;\n",
            false,
            false
        },
        {
            "parenthesized declarator",
            "int (((x))) = 7;\n",
            false,
            false
        },
        {
            "abstract callback array",
            "_Static_assert(sizeof(int (*[2])(int)) == 16, \"callbacks\");\n",
            false,
            false
        },
        {
            "array pointer binding",
            "int a[2][3]; int (*p)[3] = &a[1]; _Static_assert(sizeof(*p) == 12, \"row\");\n",
            false,
            false
        },
        {
            "ordered typedef declarators",
            "typedef int I, F(I); F f; int g(void) { return f(3); }\n",
            false,
            false
        },
        {
            "ordered initializer declarators",
            "int f(void) { int x=1, (*p)(int)=0, y=x; return y; }\n",
            false,
            false
        },
        {
            "compatible block function redeclaration",
            "int g(int); int f(void) { int g(int); return g(3); }\n",
            false,
            false
        },
        {
            "builtin System V variadic list type",
            "typedef __builtin_va_list V; _Static_assert(sizeof(V)==24, \"va_list\"); void f(V p) { (void)p; }\n",
            false,
            false
        },
        {
            "builtin Microsoft variadic list type",
            "typedef __builtin_ms_va_list V; _Static_assert(sizeof(V)==8, \"ms_va_list\");\n",
            false,
            false
        },
        {
            "builtin integer typedefs",
            "__int128_t x; __uint128_t y; _Static_assert(sizeof(x)==16 && sizeof(y)==16, \"wide\");\n",
            false,
            false
        },
        {
            "conflicting builtin typedef",
            "typedef long __int128_t;\n",
            true,
            false
        },
        {
            "block extern uses file object",
            "int x=3; int f(void) { extern int x; return x; }\n",
            false,
            false
        },
        {
            "block extern before file definition",
            "int f(void) { extern int x; return x; } int x=3;\n",
            false,
            false
        },
        {
            "repeated block extern",
            "int f(void) { extern int x; extern int x; return x; }\n",
            false,
            false
        },
        {
            "block extern conflicting linked type",
            "int x; int f(void) { extern long x; return 0; }\n",
            true,
            false
        },
        {
            "block extern initializer rejected",
            "int f(void) { extern int x=1; return x; }\n",
            true,
            false
        },
        {
            "conflicting block function redeclaration",
            "int g(int); int f(void) { int g(double); return 0; }\n",
            true,
            false
        },
        {
            "linked object and block function conflict",
            "int f; int g(void) { int f(void); return 0; }\n",
            true,
            false
        },
        {
            "compatible disjoint block prototypes",
            "int g(void) { { int f(int); } { int f(int); return f(3); } }\n",
            false,
            false
        },
        {
            "incompatible disjoint block prototypes",
            "int g(void) { { int f(int); } { int f(double); } return 0; }\n",
            true,
            false
        },
        {
            "block prototype before conflicting file declaration",
            "int g(void) { int f(int); return 0; } int f(double);\n",
            true,
            false
        },
        {
            "conflicting prototypes in different functions",
            "int g(void) { int f(int); return 0; } int h(void) { int f(double); return 0; }\n",
            true,
            false
        },
        {
            "local shadow preserves linked conflict",
            "int f(int); int g(void) { int f; { int f(double); } return 0; }\n",
            true,
            false
        },
        {
            "expired prototype does not strengthen later declaration",
            "int g(void) { { int f(int); } return 0; } int f(); int h(void) { return f(); }\n",
            false,
            false
        },
        {
            "inner prototype does not strengthen outer declaration",
            "int f(); int g(void) { { int f(int); } return f(); }\n",
            false,
            false
        },
        {
            "hidden prototype conflicts with parameterless definition",
            "int g(void) { { int f(int); } return 0; } int f() { return 0; }\n",
            true,
            false
        },
        {
            "GNU17 prototype after parameterless definition",
            "int f() { return 0; } int g(void) { int f(int); return 0; }\n",
            false,
            false
        },
        {
            "nested duplicate parameter",
            "int f(int x, int (*cb)(int x), int x);\n",
            true,
            false
        },
        {
            "const callback parameter",
            "int f(int (*const cb)(int)) { cb=0; return 0; }\n",
            true,
            false
        },
        {
            "invalid callback argument",
            "int f(int (*cb)(int)) { return cb(); }\n",
            true,
            false
        },
        {
            "function cannot return array",
            "int f(void)[3];\n",
            true,
            false
        },
        {
            "declaration ICE bounds",
            "int a[(2+3)*4]; _Static_assert(sizeof(a)==80, \"bounds\");\n",
            false,
            false
        },
        {
            "declaration ICE unevaluated fault",
            "int a[1 || (1/0)]; _Static_assert(sizeof(a)==4, \"bound\");\n",
            false,
            false
        },
        {
            "invalid negative bound",
            "int a[-1];\n",
            true,
            false
        },
        {
            "enum arithmetic and bounds",
            "enum E { A=-2, B=A+4, C, N=1<<3 }; int a[N+1]; _Static_assert(sizeof(a)==36 && B==2 && C==3, \"enum\");\n",
            false,
            false
        },
        {
            "enum scope restoration",
            "enum E { A=1 }; int f(void) { { enum F { A=2 }; } return A; }\n",
            false,
            false
        },
        {
            "duplicate enumerator",
            "enum E { A=1, A=2 };\n",
            true,
            false
        },
        {
            "enum is not assignable",
            "enum E { A=1 }; int f(void) { A=2; return A; }\n",
            true,
            false
        },
        {
            "record member access",
            "struct S { int x; long y; }; int f(struct S *p) { return p->x+p->y; }\n",
            false,
            false
        },
        {
            "record forward completion",
            "struct S; struct S { int x; }; struct S x; _Static_assert(sizeof(x)==4, \"record\");\n",
            false,
            false
        },
        {
            "record recursion",
            "struct S { int x; struct S *next; }; _Static_assert(sizeof(struct S)==16, \"record\");\n",
            false,
            false
        },
        {
            "nested record layout",
            "struct O { char c; struct I { long x; } i; unsigned b:3; }; _Static_assert(sizeof(struct O)==24, \"layout\"); int f(struct O *p) { return p->i.x; }\n",
            false,
            false
        },
        {
            "union layout",
            "union U { int i; long l; }; _Static_assert(sizeof(union U)==8, \"union\");\n",
            false,
            false
        },
        {
            "record assignment and return",
            "struct S { int x; }; struct S f(struct S x) { struct S y=x; y=x; return y; }\n",
            false,
            false
        },
        {
            "record tag shadow",
            "struct S { int x; }; int f(void) { struct S { long x; }; struct S s; return sizeof(s); }\n",
            false,
            false
        },
        {
            "duplicate member across nested tag",
            "struct O { int x; struct I { int x; } i; int x; };\n",
            true,
            false
        },
        {
            "tag kind mismatch",
            "struct S; union S *p;\n",
            true,
            false
        },
        {
            "missing record member",
            "struct S { int x; }; int f(struct S *p) { return p->y; }\n",
            true,
            false
        },
        {
            "record const member assignment",
            "struct S { const int x; }; void f(struct S a, struct S b) { a=b; }\n",
            true,
            false
        },
        {
            "incomplete by-value member",
            "struct S { struct S x; };\n",
            true,
            false
        },
        {
            "incomplete tentative object",
            "struct S; struct S x;\n",
            true,
            false
        },
        {
            "sizeof before completion",
            "struct S; unsigned long x=sizeof(struct S); struct S { int x; };\n",
            true,
            false
        },
        {
            "pointer comparison before record completion",
            "struct S; struct S *p; int f(void) { return p!=0; } struct S { int x; };\n",
            false,
            false
        },
        {
            "flexible member",
            "struct S { int n; int data[]; }; _Static_assert(sizeof(struct S)==4, \"flexible\");\n",
            false,
            false
        },
        {
            // A GNU extension clang takes (a warning under -pedantic alone), with clang's layouts: unnamed bit-fields
            // before it are no named member, and so it is the struct's only one.
            "sole flexible member GNU extension",
            "struct S { int data[]; };\n"
            "_Static_assert(sizeof(struct S) == 0 && _Alignof(struct S) == 4, \"S\");\n",
            false,
            false
        },
        {
            "unnamed zero-width field before a sole flexible member",
            "struct T { int :0; int data[]; };\n"
            "_Static_assert(sizeof(struct T) == 0 && _Alignof(struct T) == 4, \"T\");\n",
            false,
            false
        },
        {
            "unnamed bitfield before a sole flexible member",
            "struct U { int :1; int data[]; };\n"
            "_Static_assert(sizeof(struct U) == 4 && _Alignof(struct U) == 4 && __builtin_offsetof(struct U, data) == 4, \"U\");\n",
            false,
            false
        },
        {
            "flexible member after named field and unnamed padding",
            "struct S { int n; int :0; int data[]; }; _Static_assert(sizeof(struct S)==4, \"flexible padding\");\n",
            false,
            false
        },
        {
            "union flexible member GNU extension rejected",
            "union U { int n; int data[]; };\n",
            true,
            false,
            true
        },
        {
            "flexible member must be last",
            "struct S { int n; int data[]; int tail; };\n",
            true,
            false
        },
        {
            "named zero-width bitfield",
            "struct S { int x:0; };\n",
            true,
            false
        },
        {
            "bitfield update",
            "struct S { unsigned x:3; }; int f(struct S *p) { p->x+=8; return p->x++; }\n",
            false,
            false
        },
        {
            "bitfield has no address",
            "struct S { unsigned x:3; }; void f(struct S *p) { &p->x; }\n",
            true,
            false
        },
        {
            "all compound operators",
            "int f(int x,int y) { x+=y; x-=y; x*=y; x/=y; x%=y; x<<=y; x>>=y; x&=y; x^=y; x|=y; return x; }\n",
            false,
            false
        },
        {
            "prefix and postfix updates",
            "int f(int x) { ++x; --x; x++; x--; return x; }\n",
            false,
            false
        },
        {
            "mixed compound assignment",
            "int f(int x, double y) { x+=y; return x; }\n",
            false,
            false
        },
        {
            "narrow compound assignment",
            "unsigned char f(unsigned char x) { x+=256; return x; }\n",
            false,
            false
        },
        {
            "pointer updates",
            "int *f(int *p) { ++p; p--; p-=2; return p; }\n",
            false,
            false
        },
        {
            "invalid pointer compound difference",
            "void f(int *p,int *q) { p-=q; }\n",
            true,
            false
        },
        {
            "invalid const update",
            "int f(const int x) { return x++; }\n",
            true,
            false
        },
        {
            "invalid rvalue update",
            "int f(void) { return ++1; }\n",
            true,
            false
        },
        {
            "invalid array update",
            "int f(void) { int a[2]; a++; return 0; }\n",
            true,
            false
        },
        {
            "for declaration scope",
            "int f(int n) { int x=0; for(int i=0;i<n;i++) { if(i) continue; x+=i; if(x) break; } return x; }\n",
            false,
            false
        },
        {
            "empty for condition",
            "void f(void) { for(;;) break; }\n",
            false,
            false
        },
        {
            "comma for condition",
            "int f(int x) { for(;x=0,x;) x++; return x; }\n",
            false,
            false
        },
        {
            "for scope expires",
            "int f(void) { for(int i=0;i<1;i++) {} return i; }\n",
            true,
            false
        },
        {
            "record condition rejected",
            "struct S { int x; }; void f(struct S x) { for(;x;) {} }\n",
            true,
            false
        },
        {
            "while and do controls",
            "int f(int x) { while(x) { if(x) break; continue; } do { x--; if(x) continue; break; } while(x); return x; }\n",
            false,
            false
        },
        {
            "earlier binding error survives a later retained pragma",
            "int f(void){return missing;}\n#pragma pack(1)\nstruct S{char c;int x;};\n",
            true,
            false
        },
        {
            "retained pragma follows prepared attributed declarations",
            "int f(int *) __attribute__((__nonnull__(1)));\nint g(int *p){return f(p);}\n#pragma pack(1)\nstruct S{char c;int x;};\n_Static_assert(sizeof(struct S)==5,\"\");\n",
            false,
            false
        },
        {
            "record captures packing at its opening brace",
            "struct S{char c;\n#pragma pack(1)\nint x;};\nstruct T{char c;int x;};\n_Static_assert(sizeof(struct S)==8,\"\");\n_Static_assert(sizeof(struct T)==5,\"\");\n",
            false,
            false
        },
        {
            "retained packing pragma changes record layout",
            "#pragma pack(1)\nstruct S { char c; int x; };\n_Static_assert(sizeof(struct S)==5,\"\");\n",
            false,
            false
        },
    };
    enum { FILE_COUNT = BUSTER_ARRAY_LENGTH(fixtures) };
    char directory[] = "/tmp/buster-checker-source-XXXXXX";
    bool ready = mkdtemp(directory) != 0;
    char paths[FILE_COUNT][160];
    char* pointers[FILE_COUNT];
    ParseTestClang outcomes[FILE_COUNT];
    for (uint32_t i = 0; ready && i < FILE_COUNT; i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        pointers[i] = paths[i];
        ready = parse_test_write(paths[i], fixtures[i].source, strlen(fixtures[i].source));
    }
    const char* arguments[] = { "-std=gnu17" };
    ready = ready && parse_test_clang(clang, arguments, BUSTER_ARRAY_LENGTH(arguments), pointers, FILE_COUNT, outcomes);
    check_test_expect(test, ready, "extended source clang oracle could not run");
    for (uint32_t i = 0; ready && i < FILE_COUNT; i += 1)
    {
        char description[192];
        snprintf(description, sizeof(description), "%s: extended fixture differs from clang", fixtures[i].name);
        check_test_expect(test, outcomes[i].rejected == (fixtures[i].rejected && !fixtures[i].unsupported && !fixtures[i].extension), description);
        check_test_source(test, &fixtures[i], paths[i], &outcomes[i]);
    }
    for (uint32_t i = 0; i < FILE_COUNT; i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        unlink(paths[i]);
    }
    if (directory[0] != 0)
    {
        rmdir(directory);
    }
    check_test_source_fp_history(test, false);
    check_test_source_fp_history(test, true);
    check_test_source_builtin_effects(test);
    check_test_source_transparent_history(test);
    check_test_source_intrinsic_range(test);
}
