// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original successful-source conversion controls. Expected counts distinguish implicit sinks from comparison /
// conditional composition, actual zero literals from other ICEs, and semantic queries from runtime deadness.

static void check_test_warning_graph(CheckTest* test)
{
    static const struct
    {
        const char* source;
        uint32_t nulls;
        uint32_t ordered;
        uint32_t column;
    } cases[] = {
        { "int*p=1-1;\n", 1, 0, 8 },
        { "int*p=(0);\n" },
        { "int*p=((0));\n" },
        { "enum{ZERO=0};int*p=ZERO;\n", 1 },
        { "int*p='\\0';\n", 1 },
        { "int*p=(int)0.0;\n", 1, 0, 7 },
        { "int*p=((1-1));\n", 1, 0, 7 },
        { "int*p=1?0:0;\n", 1, 0, 7 },
        { "int*p=+0;\n", 1, 0, 7 },
        { "int*p=__builtin_choose_expr(1,0,1);\n" },
        { "int*p=_Generic(1,int:0);\n" },
        { "int*p=__builtin_choose_expr(1,2-2,1);\n", 1, 0, 7 },
        { "int*p=_Generic(1,int:3-3);\n", 1, 0, 7 },
        { "void f(int*);void g(void){f(5-5);}\n", 1 },
        { "void f(int*,int*);void g(void){f(5-5,8-8);}\n", 2 },
        { "int*f(void){return 1-1;}\n", 1 },
        { "void f(int**p){*p=1-1;}\n", 1 },
        { "int f(int*p){return p==(1-1);}\n" },
        { "int*f(int c,int*p){return c?p:1-1;}\n" },
        { "int f(int*p){return 0&&(p=1-1);}\n", 1 },
        { "int f(void){int*p;if(0)p=1-1;return 0;}\n", 1 },
        { "int*p=(int*)(1-1);\n" },
        { "int f(int*p,int n){return p<n;}\n", 0, 1 },
        { "int f(int*p){return p<0;}\n" },
        { "int f(int*p){return p<(1-1);}\n" },
        { "enum{ZERO=0,ONE=1};int f(int*p){return p<ONE || p<ZERO;}\n", 0, 1 },
        { "int f(int*p,int n){return sizeof(p<n);}\n", 0, 1 },
        { "int f(int*p){return sizeof(p<0);}\n" },
        { "int*p;int f(void){return sizeof(p=1-1);}\n" },
        { "int*p;int f(void){return __alignof__(p=1-1);}\n" },
        { "int*p;__typeof__(p=1-1) q;\n" },
        { "int*p;int f(void){return _Generic((p=1-1),int*:1,default:0);}\n" },
        { "int*p;int f(int n){return sizeof(int[((p=1-1)!=0)+n]);}\n", 1 },
        { "int*p;int f(int n){return sizeof(int[sizeof(p=1-1)+n]);}\n" },
        { "int*p;int f(void){return sizeof(int[0&&(p=1-1)]);}\n", 1 },
        { "int*p;int f(void){return _Generic(1,int:1,default:(p=1-1)!=0);}\n", 1 },
        { "int*p;int f(void){return __builtin_choose_expr(0,(p=1-1)!=0,1);}\n", 1 },
        { "int f(int*);int a[0&&f(1-1)];\n", 1 },
        { "int f(int*);int a[sizeof(f(1-1))];\n" },
        { "int f(int*);int g(void){return sizeof(int[0&&f(1-1)]);}\n", 1 },
        { "int f(int*);int a[__builtin_choose_expr(1,1,f(1-1))];\n", 1 },
    };
    for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(cases); c += 1)
    {
        for (uint32_t variant = 0; variant < 4; variant += 1)
        {
            const uint32_t scalar = variant & 1;
            static InternTable identifiers;
            static Preprocessor pp;
            static Parser parser;
            CheckTestTypes types = { 0 };
            Arena retained = arena_reserve(UINT64_C(1) << 26);
            Arena output = arena_reserve(UINT64_C(1) << 26);
            Arena scratch = arena_reserve(UINT64_C(1) << 26);
            const PPOptions preprocessing = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true, .quiet = true };
            const ParseOptions parsing = { .extension_metadata = true, .scalar = scalar != 0, .tree_only = variant >= 2 };
            const CheckOptions checking = { .verify = true, .scalar = scalar != 0 };
            CheckInput input = { 0 };
            CheckStream stream = { 0 };
            const bool ready = retained.base != NULL && output.base != NULL && scratch.base != NULL && check_test_types_open(&types) &&
                               intern_initialize(&identifiers) && pp_initialize(&pp, &preprocessing, &identifiers) &&
                               parse_initialize(&parser, &parsing, preprocessing.language) &&
                               pp_run_source(&pp, "graph-warnings.c", (const uint8_t*)cases[c].source, strlen(cases[c].source)) &&
                               parse_translation_unit(&parser, &pp) && check_input(&input, &parser, &pp, &types.table, &retained, &scratch) &&
                               check_graph_run(&types.table, &input.graph, &output, &scratch, &checking, &stream) && stream.report_count == 0 &&
                               check_input_warning_graph(&input, &parser, &types.table, &stream, &output, &scratch);
            uint32_t nulls = 0;
            uint32_t ordered = 0;
            uint32_t actual_column = 0;
            bool located = true;
            for (uint32_t i = 0; ready && i < input.warning_events.count; i += 1)
            {
                const CheckWarningEvent* event = input.warning_events.events + i;
                if ((event->flags & BUSTER_CHECK_WARNING_SUPPRESSED) == 0)
                {
                    nulls += event->kind == CHECK_WARNING_EVENT_NON_LITERAL_NULL;
                    ordered += event->kind == CHECK_WARNING_EVENT_ORDERED_POINTER_INTEGER;
                    if (event->kind == CHECK_WARNING_EVENT_NON_LITERAL_NULL && cases[c].column != 0)
                    {
                        const uint32_t location = pp.output.locations[parser.tokens.sources[event->token]];
                        const uint32_t range = pp_find_range(&pp, location);
                        uint32_t column = 0;
                        if (range < pp.range_count)
                        {
                            pp_line_of(&pp, range, location, &column);
                        }
                        located &= column == cases[c].column;
                        actual_column = column;
                    }
                }
            }
            char message[192];
            snprintf(message, sizeof(message), "graph warning source %u (%s, %s) got null/comparison %u/%u at column %u; expected %u/%u at %u",
                     c, scalar ? "scalar" : "vector", variant >= 2 ? "tree" : "derived", nulls, ordered, actual_column, cases[c].nulls, cases[c].ordered, cases[c].column);
            check_test_expect(test, ready && !input.resource && located && nulls == cases[c].nulls && ordered == cases[c].ordered, message);
            parse_release(&parser);
            pp_release(&pp);
            intern_release(&identifiers);
            check_test_types_close(&types);
            arena_release(&scratch);
            arena_release(&output);
            arena_release(&retained);
        }
    }
}
