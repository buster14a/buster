// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original initializer conversion/list controls. Semantic facts live in the retained arena, after each walk's
// final contiguous part append; the expression and parts arenas remain independent.

static void check_test_warning_initializers(CheckTest* test)
{
    static const struct
    {
        const char* source;
        uint32_t nulls;
        uint32_t braces;
        uint32_t overrides;
        uint32_t partial;
        uint32_t effects;
        bool rejected;
        const char* sequence;
    } cases[] = {
        { "int x=1;\n" },
        { "int x={1};\n" },
        { "int x={{1}};\n", 0, 1 },
        { "int x={{{1}}};\n", 0, 2 },
        { "struct S{int x;};struct S s={{1}};\n" },
        { "struct S{int x;};struct S s={{{1}}};\n", 0, 1 },
        { "int a[2]={{1},{2}};\n" },
        { "int a[2]={{{1}},{{2}}};\n", 0, 2 },
        { "int*p=1-1;\n", 1 },
        { "int*p={1-1};\n", 1 },
        { "int*p={{1-1}};\n", 1, 1 },
        { "int*p[2]={1-1,2-2};\n", 2 },
        { "struct S{int*p,*q;};struct S s={1-1,2-2};\n", 2 },
        { "struct S{int*p;};struct S s={.p=(1-1)};\n", 1 },
        { "struct S{int*p;};struct S s={.p=0};\n" },
        { "struct S{int*p;};struct S s={.p=_Generic(1,int:0)};\n" },
        { "struct S{int*p;};struct S s={.p=_Generic(1,int:2-2)};\n", 1 },
        { "int f(void){int*p[2]={1-1,2-2};return p[0]==p[1];}\n", 2 },
        { "struct S{int x;};struct S s={.x=1,.x=2};\n", 0, 0, 1 },
        { "int a[2]={[0]=1,[0]=2};\n", 0, 0, 1 },
        { "union U{int x,y;};union U u={.x=1,.y=2};\n", 0, 0, 1 },
        { "union U{int x,y;};union U u={.x=1,.x=2};\n", 0, 0, 1 },
        { "struct S{int a[2];};struct S s={.a[0]=1,.a={2,3}};\n", 0, 0, 1 },
        { "struct S{int a[2];};struct S s={.a={2,3},.a[0]=1};\n", 0, 0, 1 },
        { "struct S{int a[2];};struct S s={.a={1},.a[1]=2};\n" },
        { "struct S{char a[8];};struct S s={.a=\"a\",.a[7]='x'};\n", 0, 0, 1, 1 },
        { "int a[4]={[0 ... 3]=1,[1 ... 2]=2};\n", 0, 0, 2 },
        { "int a[3]={[0 ... 2]=1,[0]=2,[1]=3,[2]=4};\n", 0, 0, 3 },
        { "int next(void);void f(void){int a[1]={[0]=next(),[0]=2};}\n", 0, 0, 1, 0, 1 },
        { "struct S{int x,y;};struct P{struct S s;};struct S make(void);void f(void){struct P p={.s=make(),.s.x=1};}\n", 0, 0, 1, 1 },
        { "struct S{int x,y;};struct P{struct S s;};struct S make(void);void f(void){struct P p={.s=make(),.s={1,2}};}\n", 0, 0, 1, 0, 1 },
        { "struct I{int x;};struct O{struct I i;};struct P{struct O o;};struct O make(void);void f(void){struct P p={.o=make(),.o.i={1}};}\n", 0, 0, 1, 1 },
        { "struct S{int x,y;};struct P{struct S s;};struct S make(void);void f(void){struct P p={.s=make(),.s.x=1,.s.y=2,.s.x=3};}\n", 0, 0, 4, 3 },
        { "struct S{int x,y;};union U{struct S s;int other;};struct S make(void);void f(void){union U u={.s=make(),.s.x=1,.other=2};}\n", 0, 0, 2, 1, 1 },
        { "struct S{int x;};struct S s={.x={{1}},.x=2};\n", 0, 1, 1 },
        { "struct S{int x;};struct S s={.x=1,.x={{2}}};\n", 0, 1, 1 },
        { "struct S{char a[4];};struct S s={.a={\"abc\"},.a[1]='x'};\n", 0, 0, 1, 1 },
        { "struct S{char a[4];};struct S s={.a={\"abc\"},.a={1,2}};\n", 0, 0, 1 },
        { "struct S{char a[4];};struct S s={.a=\"old\",.a={\"abc\"}};\n", 0, 0, 1 },
        { "struct S{int x;};struct P{struct S s;};struct P p={.s=(struct S){.x=1,.x=2}};\n", 0, 0, 1 },
        { "struct S{int x;};struct P{struct S s;};struct P p={.s={1},.s=(struct S){.x=2,.x=3}};\n", 0, 0, 2 },
        { "struct S{int*p;};struct P{struct S s;};struct P p={.s=(struct S){.p=1-1}};\n", 1 },
        { "struct S{int x;};struct P{struct S s;};struct P p={.s=(struct S){.x={{1}}}};\n", 0, 1 },
        { "struct S{int x;};struct S a[2]={[0 ... 1]=(struct S){.x=1,.x=2}};\n", 0, 0, 1 },
        { "int next(void);int a[1]={[0]=1,[0]=2};static int b=next();\n", 0, 0, 1, 0, 0, true },
        { "int next(void);static int a[2]={[0]=1,[0]=2,[1]=next()};\n", 0, 0, 1, 0, 0, true },
        { "int next(void);static int a[1]={[0]=1,[0]=next()};\n", 0, 0, 1, 0, 0, true },
        { "int next(void);int a[4]={[0 ... 3]=1,[1 ... 2]=2};static int b=next();\n", 0, 0, 2, 0, 0, true },
        { "int a[1]={[0]=1,[0]=2,3};\n", 0, 0, 1, 0, 0, false, "OX" },
        { "int a[1]={1,2,[0]=3};\n", 0, 0, 0, 0, 0, false, "X" },
        { "int a[1]={1,2,[0]={{3}}};\n", 0, 0, 0, 0, 0, false, "X" },
        { "struct S{int a[1];};struct S s={.a={1},.a={2,3}};\n", 0, 0, 1, 0, 0, false, "XO" },
        { "struct S{int x;};struct S s={.x=1,.x={{2}}};\n", 0, 1, 1, 0, 0, false, "BO" },
        { "int*p[1]={[0]=0,[0]=1-1};\n", 1, 0, 1, 0, 0, false, "NO" },
        { "volatile int source;void f(void){int a[1]={[0]=source,[0]=2};}\n", 0, 0, 1, 0, 1 },
        { "int next(void);struct S{int x;};struct P{struct S s;};void f(void){struct P p={.s=(struct S){next()},.s={1}};}\n", 0, 0, 1, 0, 1 },
    };
    for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(cases); c += 1)
    {
        for (uint32_t scalar = 0; scalar < 2; scalar += 1)
        {
            static InternTable identifiers;
            static Preprocessor pp;
            static Parser parser;
            CheckTestTypes types = { 0 };
            Arena retained = arena_reserve(UINT64_C(1) << 26);
            Arena output = arena_reserve(UINT64_C(1) << 26);
            Arena scratch = arena_reserve(UINT64_C(1) << 26);
            const PPOptions preprocessing = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true, .quiet = true };
            const ParseOptions parsing = { .extension_metadata = true, .scalar = scalar != 0 };
            const CheckOptions checking = { .verify = true, .scalar = scalar != 0 };
            CheckInput input = { 0 };
            CheckStream stream = { 0 };
            CheckInitializers initializers = { 0 };
            CheckWarningBuffer warnings = { .arena = &retained };
            CheckWarningSet keys = { .arena = &retained };
            bool ready = retained.base != NULL && output.base != NULL && scratch.base != NULL && check_test_types_open(&types) &&
                               intern_initialize(&identifiers) && pp_initialize(&pp, &preprocessing, &identifiers) &&
                               parse_initialize(&parser, &parsing, preprocessing.language) &&
                               pp_run_source(&pp, "initializer-warnings.c", (const uint8_t*)cases[c].source, strlen(cases[c].source)) &&
                               parse_translation_unit(&parser, &pp) && check_input(&input, &parser, &pp, &types.table, &retained, &scratch) &&
                               check_graph_run(&types.table, &input.graph, &output, &scratch, &checking, &stream) && stream.report_count == 0;
            if (ready && cases[c].rejected)
            {
                const CheckWarningEvent prefix = {
                    .id = CHECK_WARNING_ID_INTEGER_OVERFLOW,
                    .kind = CHECK_WARNING_EVENT_MESSAGE,
                    .source = BUSTER_CHECK_WARNING_NO_SOURCE,
                    .payload = { .token = BUSTER_CHECK_WARNING_NO_SOURCE, .data = 0x6217 },
                };
                ready = check_warning_append(&warnings, &prefix, NULL, 0) && check_warning_set_add(&keys, UINT64_C(0x123400000001));
            }
            const CheckWarningEvent* prefix_events = warnings.events;
            const uint64_t* prefix_keys = keys.keys;
            const uint64_t warning_mark = retained.length;
            const bool accepted = ready && check_initializers_run_events(&initializers, &input, &stream, &parser, &types.table,
                                                                           &output, &scratch, &warnings, &keys);
            if (ready && cases[c].rejected)
            {
                uint32_t excess[256];
                CheckInitWarningSink sink = { .tokens = excess, .capacity = BUSTER_ARRAY_LENGTH(excess), .events = &warnings, .event_keys = &keys };
                CheckInitDiagnostic diagnostic = { 0 };
                const bool rolled_back = !accepted && initializers.status == CHECK_INIT_NON_CONSTANT && warnings.count == 1 &&
                                         warnings.events == prefix_events && warnings.events[0].payload.data == 0x6217 && warnings.note_count == 0 &&
                                         keys.keys == prefix_keys && keys.count == 1 && check_warning_set_contains(&keys, UINT64_C(0x123400000001)) &&
                                         retained.length == warning_mark;
                ready = rolled_back && check_initializers_diagnose_warnings(&diagnostic, &input, &stream, &parser, &types.table,
                                                                               BUSTER_CHECK_NONE, &output, &scratch, &sink) &&
                        diagnostic.status == CHECK_INIT_NON_CONSTANT && diagnostic.completion > diagnostic.token && diagnostic.completion <= parser.tokens.count;
            }
            else
            {
                ready &= accepted;
            }
            ready = ready && check_initializers_warning_context(&warnings, &input, &parser, &types.table, &scratch);
            uint32_t nulls = 0;
            uint32_t braces = 0;
            uint32_t overrides = 0;
            uint32_t partial = 0;
            uint32_t effects = 0;
            char sequence[128] = { 0 };
            uint32_t sequence_count = 0;
            for (uint32_t i = 0; ready && i < warnings.count; i += 1)
            {
                const CheckWarningEvent* event = warnings.events + i;
                if ((event->flags & BUSTER_CHECK_WARNING_SUPPRESSED) == 0)
                {
                    nulls += event->kind == CHECK_WARNING_EVENT_NON_LITERAL_NULL;
                    braces += event->kind == CHECK_WARNING_EVENT_MANY_SCALAR_BRACES;
                    overrides += event->kind == CHECK_WARNING_EVENT_INITIALIZER_OVERRIDES;
                    partial += event->kind == CHECK_WARNING_EVENT_INITIALIZER_OVERRIDES && event->payload.data != 0;
                    const char item = event->kind == CHECK_WARNING_EVENT_NON_LITERAL_NULL ? 'N' :
                                      event->kind == CHECK_WARNING_EVENT_MANY_SCALAR_BRACES ? 'B' :
                                      event->kind == CHECK_WARNING_EVENT_INITIALIZER_OVERRIDES ? 'O' :
                                      event->kind == CHECK_WARNING_EVENT_MESSAGE && event->id == CHECK_WARNING_ID_EXCESS_INITIALIZERS ? 'X' : 0;
                    if (item != 0 && sequence_count + 1 < sizeof(sequence))
                    {
                        sequence[sequence_count++] = item;
                    }
                }
            }
            for (uint32_t i = 0; ready && i < warnings.note_count; i += 1)
            {
                effects += warnings.notes[i].kind == CHECK_WARNING_NOTE_PRIOR_INITIALIZATION && warnings.notes[i].payload.data != 0;
            }
            char message[256];
            snprintf(message, sizeof(message), "initializer warning source %u (%s) got null/brace/override/partial/effects %u/%u/%u/%u/%u; expected %u/%u/%u/%u/%u; sequence '%s'/%s",
                     c, scalar ? "scalar" : "vector", nulls, braces, overrides, partial, effects,
                     cases[c].nulls, cases[c].braces, cases[c].overrides, cases[c].partial, cases[c].effects, sequence,
                     cases[c].sequence != NULL ? cases[c].sequence : "any");
            check_test_expect(test, ready && nulls == cases[c].nulls && braces == cases[c].braces && overrides == cases[c].overrides &&
                                    partial == cases[c].partial && effects == cases[c].effects &&
                                    (cases[c].sequence == NULL || strcmp(sequence, cases[c].sequence) == 0), message);
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
