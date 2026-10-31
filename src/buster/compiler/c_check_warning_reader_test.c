// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original reader controls, including source-spelling and missing-name insertion points that a resolved TypeId
// cannot establish. The independent Clang 23.1.4 witnesses have these counts; scalar/vector are additional checks.

static void check_test_warning_reader(CheckTest* test)
{
    static const struct
    {
        const char* source;
        uint32_t id;
        uint32_t count;
        uint32_t notes;
        uint32_t standard;
        uint32_t suppressed;
    } cases[] = {
        { "unsigned;\n", CHECK_WARNING_ID_MISSING_DECLARATIONS, 1 },
        { "struct {int x;};\n", CHECK_WARNING_ID_MISSING_DECLARATIONS, 1 },
        { "struct S; struct S {int x;}; enum {A};\n", CHECK_WARNING_ID_MISSING_DECLARATIONS, 0 },
        { "typedef struct S {int x;} T; T;\n", CHECK_WARNING_ID_MISSING_DECLARATIONS, 1 },
        { "typedef struct {int x;} T; struct S {T; int y;};\n", CHECK_WARNING_ID_MISSING_DECLARATIONS, 1 },
        { "struct S {int; struct Named{int x;}; int y;};\n", CHECK_WARNING_ID_MISSING_DECLARATIONS, 2 },
        { "struct S {struct {int x;}; int y;};\n", CHECK_WARNING_ID_MISSING_DECLARATIONS, 0 },
        { "struct S {enum {E};};\n", CHECK_WARNING_ID_MISSING_DECLARATIONS, 0 },
        { "union U {enum {A};};\n", CHECK_WARNING_ID_MISSING_DECLARATIONS, 0 },
        { "struct T {struct {enum {B};};};\n", CHECK_WARNING_ID_MISSING_DECLARATIONS, 0 },
        { "struct V {union {int x;};};\n", CHECK_WARNING_ID_MISSING_DECLARATIONS, 0 },
        { "struct W {enum E {A};};\n", CHECK_WARNING_ID_MISSING_DECLARATIONS, 1 },
        { "typedef int;\n", CHECK_WARNING_ID_MISSING_DECLARATIONS, 1 },
        { "typedef struct S {int x;};\n", CHECK_WARNING_ID_MISSING_DECLARATIONS, 1 },
        { "typedef enum E {A};\n", CHECK_WARNING_ID_MISSING_DECLARATIONS, 1 },
        { "extern struct S;\n", CHECK_WARNING_ID_MISSING_DECLARATIONS, 1 },
        { "static union U;\n", CHECK_WARNING_ID_MISSING_DECLARATIONS, 1 },
        { "const struct S;\n", CHECK_WARNING_ID_MISSING_DECLARATIONS, 1 },
        { "const int;\n", CHECK_WARNING_ID_MISSING_DECLARATIONS, 1 },
        { "_Complex z;\n", CHECK_WARNING_ID_PLAIN_COMPLEX, 1 },
        { "float _Complex a; double _Complex b; long double _Complex c;\n", CHECK_WARNING_ID_PLAIN_COMPLEX, 0 },
        { "struct F {int n;int a[];}; struct S {struct F f;int n;};\n", CHECK_WARNING_ID_VARIABLE_SIZED_MEMBER, 1 },
        { "struct F {int n;int a[];}; struct A {struct F f;}; union U {struct A a;int n;}; struct S {union U u;int n;};\n", CHECK_WARNING_ID_VARIABLE_SIZED_MEMBER, 1 },
        { "struct F {int n;int a[];}; struct A {struct F f;int n;}; struct S {struct A a;int n;};\n", CHECK_WARNING_ID_VARIABLE_SIZED_MEMBER, 2 },
        { "struct F {int n;int a[];}; struct A {struct F f[1];int n;}; struct S {struct A a;int n;};\n", CHECK_WARNING_ID_VARIABLE_SIZED_MEMBER, 0 },
        { "struct F {int n;int a[0];}; struct S {struct F f;int n;};\n", CHECK_WARNING_ID_VARIABLE_SIZED_MEMBER, 0 },
        { "inline int f(void){static int x;return x;}\n", CHECK_WARNING_ID_STATIC_LOCAL_INLINE, 1, 1 },
        { "static inline int f(void){static int x;return x;}\n", CHECK_WARNING_ID_STATIC_LOCAL_INLINE, 0 },
        { "extern inline int f(void){static int x;return x;}\n", CHECK_WARNING_ID_STATIC_LOCAL_INLINE, 0 },
        { "extern int f(void);inline int f(void){static int x;return x;}\n", CHECK_WARNING_ID_STATIC_LOCAL_INLINE, 0 },
        { "extern int f(void);inline int f(void);inline int f(void){static int x;return x;}\n", CHECK_WARNING_ID_STATIC_LOCAL_INLINE, 0 },
        { "inline int f(void){static int x;return x;}extern int f(void);\n", CHECK_WARNING_ID_STATIC_LOCAL_INLINE, 1, 1 },
        { "inline int f(void){static const int x=2;static const int a[]={2};static int *const p=0;return x+a[0]+(p!=0);}\n", CHECK_WARNING_ID_STATIC_LOCAL_INLINE, 0 },
        { "inline int f(void){static const int *p;return p!=0;}\n", CHECK_WARNING_ID_STATIC_LOCAL_INLINE, 1, 1 },
        { "__attribute__((gnu_inline)) inline int f(void){static int x;return x;}\n", CHECK_WARNING_ID_STATIC_LOCAL_INLINE, 0 },
        { "__attribute__((gnu_inline)) extern inline int f(void){static int x;return x;}\n", CHECK_WARNING_ID_STATIC_LOCAL_INLINE, 1, 0 },
        { "int f(long){return 1;}\n", CHECK_WARNING_ID_C23_PARAMETER, 1 },
        { "int f(int *,int (*)(int),int [2]){return 1;}\n", CHECK_WARNING_ID_C23_PARAMETER, 3 },
        { "int f(long);int g(int (*p)(int)){return p(1);}\n", CHECK_WARNING_ID_C23_PARAMETER, 0 },
        { "int f(void){return 1;}\n", CHECK_WARNING_ID_C23_PARAMETER, 0 },
        { "int f(long){return 1;}\n", CHECK_WARNING_ID_C23_PARAMETER, 0, 0, 2023 },
        { "int*p;int f(void){return sizeof(p=1-1);}\n", CHECK_WARNING_ID_NON_LITERAL_NULL, 0, 0, 0, 1 },
        { "int*p;int f(void){return __alignof__(p=1-1);}\n", CHECK_WARNING_ID_NON_LITERAL_NULL, 0 },
        { "int*p;__typeof__(p=1-1) q;\n", CHECK_WARNING_ID_NON_LITERAL_NULL, 0, 0, 0, 1 },
        { "int*p;int f(void){return _Generic((p=1-1),int*:1,default:0);}\n", CHECK_WARNING_ID_NON_LITERAL_NULL, 0, 0, 0, 1 },
        { "int*p;int f(int n){return sizeof(int[((p=1-1)!=0)+n]);}\n", CHECK_WARNING_ID_NON_LITERAL_NULL, 1 },
        { "int*p;int f(int n){return __alignof__(int[((p=1-1)!=0)+n]);}\n", CHECK_WARNING_ID_NON_LITERAL_NULL, 1 },
        { "int*p;int f(int n){return sizeof(int[sizeof(p=1-1)+n]);}\n", CHECK_WARNING_ID_NON_LITERAL_NULL, 0, 0, 0, 1 },
        { "int*p;int f(void){return sizeof(int[0&&(p=1-1)]);}\n", CHECK_WARNING_ID_NON_LITERAL_NULL, 1 },
        { "int*p;typedef __typeof__(p<2) T;T x;\n", CHECK_WARNING_ID_ORDERED_POINTER_INTEGER, 1 },
        { "int*p;int f(void){return sizeof(p<2);}\n", CHECK_WARNING_ID_ORDERED_POINTER_INTEGER, 1 },
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
            Arena scratch = arena_reserve(UINT64_C(1) << 26);
            const PPOptions preprocessing = {
                .language = { .standard = cases[c].standard != 0 ? cases[c].standard : 2017, .gnu = true },
                .semantic_pragmas = true,
                .no_standard_includes = true,
                .quiet = true,
            };
            const ParseOptions parsing = { .extension_metadata = true, .scalar = scalar != 0 };
            CheckInput input = { 0 };
            const bool ready = retained.base != NULL && scratch.base != NULL && check_test_types_open(&types) && intern_initialize(&identifiers) &&
                               pp_initialize(&pp, &preprocessing, &identifiers) && parse_initialize(&parser, &parsing, preprocessing.language) &&
                               pp_run_source(&pp, "reader-warnings.c", (const uint8_t*)cases[c].source, strlen(cases[c].source)) &&
                               parse_translation_unit(&parser, &pp) && check_input(&input, &parser, &pp, &types.table, &retained, &scratch);
            uint32_t count = 0;
            uint32_t notes = 0;
            uint32_t suppressed = 0;
            for (uint32_t i = 0; ready && i < input.warning_events.count; i += 1)
            {
                const CheckWarningEvent* event = input.warning_events.events + i;
                if (event->id == cases[c].id)
                {
                    count += (event->flags & BUSTER_CHECK_WARNING_SUPPRESSED) == 0;
                    notes += event->note_count;
                    suppressed += (event->flags & BUSTER_CHECK_WARNING_SUPPRESSED) != 0;
                }
            }
            char message[160];
            snprintf(message, sizeof(message), "reader warning fixture %u (%s) has its independent source count and attached-note count", c, scalar ? "scalar" : "vector");
            check_test_expect(test, ready && !input.resource && count == cases[c].count && notes == cases[c].notes && suppressed == cases[c].suppressed, message);
            parse_release(&parser);
            pp_release(&pp);
            intern_release(&identifiers);
            check_test_types_close(&types);
            arena_release(&scratch);
            arena_release(&retained);
        }
    }
}
