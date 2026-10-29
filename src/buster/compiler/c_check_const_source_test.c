// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original const-object sources, with outcomes established by the issue's Clang 23.1.4 survey.
// Include after c_check_program_test.c; both source and native tests reuse the existing test machinery.
#ifndef BUSTER_C_CHECK_CONST_SOURCE_TEST_INCLUDED
#define BUSTER_C_CHECK_CONST_SOURCE_TEST_INCLUDED

static const CheckSourceFixture check_const_source_fixtures[] =
{
    {
        .name = "a whole replacement discards an effectful aggregate rvalue retaining an object ID",
        .source = "struct S{int x,y;};struct P{struct S s;};const struct S base={8,9};int touch(void);int f(void){const struct P p={.s=(touch(),base),.s={1,2}};enum{E=p.s.x};_Static_assert(E==1,\"rvalue replacement\");return E;}\n",
    },
    {
        .name = "a whole aggregate replacement makes an earlier aggregate call dead",
        .source = "struct S{int x,y;};struct P{struct S s;};struct S make(void);int f(void){const struct P p={.s=make(),.s={1,2}};enum{E=p.s.x};_Static_assert(E==1,\"whole replacement\");return E;}\n",
    },
    {
        .name = "sizeof a VLA type does not make a later const object constant",
        .source = "int f(void){const int k=3;const int n=sizeof(int[k]);enum{E=n};return E;}\n",
        .rejected = true,
    },
    {
        .name = "a const bound in a sizeof type-name keeps its variable array type",
        .source = "const int k=3;enum{E=sizeof(int[k])};\n",
        .rejected = true,
    },
    {
        .name = "static initialization cannot consume sizeof a variable array type",
        .source = "const int k=3;const int n=sizeof(int[k]);enum{E=n};\n",
        .rejected = true,
    },
    {
        .name = "overridden file-scope nonconstant writes do not poison final const value",
        .source = "int get(void);struct P{int x;};const struct P p={.x=get(),.x=3};enum{E=p.x};_Static_assert(E==3,\"file override\");\n",
    },
    {
        .name = "scalar field coverage does not kill an aggregate-copy base expression",
        .source = "struct S{int x,y;};struct P{struct S s;};struct S make(void);void f(void){const struct P p={.s=make(),.s.x=1,.s.y=2};enum{E=p.s.x};}\n",
        .rejected = true,
    },
    {
        .name = "an equal-sized nested child is not a whole replacement of its parent",
        .source = "struct I{int x;};struct O{struct I i;};struct P{struct O o;};struct O make(void);void f(void){const struct P p={.o=make(),.o.i={1}};enum{E=p.o.i.x};}\n",
        .rejected = true,
    },
    {
        .name = "fully overridden scalar GNU range expressions are dead",
        .source = "int get(void);int f(void){const int a[3]={[0 ... 2]=get(),[0]=1,[1]=2,[2]=3};enum{E=a[0]};_Static_assert(E==1,\"range\");return E;}\n",
    },
    {
        .name = "an automatic address can be consumed by a direct constant read",
        .source = "int f(void){const int k=3;enum{E=*(&k)};_Static_assert(E==3,\"direct address\");return E;}\n",
    },
    {
        .name = "automatic array addresses can be consumed without publishing a pointer value",
        .source = "int f(void){const int a[2]={2,5};enum{E=(&a[0])[1]};_Static_assert(E==5,\"array address\");return E;}\n",
    },
    {
        .name = "a const pointer initialized by automatic array decay is not constant evidence",
        .source = "int f(void){const int a[2]={2,5};const int* const p=a;enum{E=p[1]};_Static_assert(E==5,\"array pointer\");return E;}\n",
        .rejected = true,
    },
    {
        .name = "overridden effectful initializer is absent from the final value",
        .source = "int get(void);struct P{int x;};int f(void){const struct P p={.x=get(),.x=3};enum{E=p.x};_Static_assert(E==3,\"override\");return E;}\n",
    },
    {
        .name = "overridden effects do not poison another surviving aggregate member",
        .source = "int get(void);struct P{int x,y;};int f(void){const struct P p={.x=get(),.y=4,.x=3};enum{E=p.y};_Static_assert(E==4,\"other member\");return E;}\n",
    },
    {
        .name = "aggregate subobject copies clip implicit zero coverage",
        .source = "struct I{int x,y;};struct O{struct I a,b;};const struct O p={{1},{2}};const struct I q=p.b;_Static_assert(q.x==2 && q.y==0,\"copy\");\n",
    },
    {
        .name = "sparse const arrays do not materialize their declared extent",
        .source = "const int a[268435456]={[134217728]=7};enum{E=a[134217728],Z=a[268435455]};_Static_assert(E==7 && Z==0,\"sparse\");\n",
    },
    {
        .name = "automatic const array retains a source-only address path",
        .source = "int f(void){const int a[2]={2,5};enum{E=a[1]};_Static_assert(E==5,\"local array\");return E;}\n",
    },
    {
        .name = "automatic object addresses do not make a const pointer initializer constant",
        .source = "int f(void){const int k=3;const int* const p=&k;enum{E=*p};_Static_assert(E==3,\"local pointer\");return E;}\n",
        .rejected = true,
    },
    {
        .name = "builtin expect retains its own ICE contract after a const read",
        .source = "const int k=3;enum{E=__builtin_expect(k,0)};_Static_assert(E==3,\"expect\");\n",
    },
    {
        .name = "floating comparison builtin inherits GNU const-read evidence",
        .source = "const double k=3.5;enum{E=__builtin_isgreater(k,0.0)};_Static_assert(E==1,\"compare\");\n",
    },
    {
        .name = "choose expression retains its stricter ICE requirement",
        .source = "const int k=3;enum{E=__builtin_choose_expr(k,4,5)};\n",
        .rejected = true,
    },
    {
        .name = "const scalar becomes a GNU enumerator value",
        .source = "const int k=3;\nenum { E=k };\n_Static_assert(E==3,\"enumerator value\");\n",
    },
    {
        .name = "const scalar folds a file-scope array bound",
        .source = "const int k=3;\nint a[k];\n_Static_assert(sizeof(a)==3*sizeof(int),\"folded array extent\");\n",
    },
    {
        .name = "automatic const scalar folds a case label",
        .source = "int f(int n){const int k=3;switch(n){case k:return 1;default:return 0;}}\n",
    },
    {
        .name = "const unsigned scalar folds a bit-field width",
        .source = "const unsigned k=3;\nstruct S{unsigned b:k;};\n_Static_assert(sizeof(struct S)==sizeof(unsigned),\"bit-field storage\");\n",
    },
    {
        .name = "const scalar folds a static assertion",
        .source = "const int k=3;\n_Static_assert(k==3,\"value\");\n",
    },
    {
        .name = "const scalar supplies a static initializer",
        .source = "const int k=3;\nint g=k;\n",
    },
    {
        .name = "const record member folds an enumerator",
        .source = "struct P{int x;};const struct P p={4};\nenum { F=p.x };\n_Static_assert(F==4,\"record member value\");\n",
    },
    {
        .name = "completed const initializer chains retain converted values",
        .source = "const int a=3;const int b=a+2;\nenum { E=b };\n_Static_assert(E==5,\"initializer chain\");\n",
    },
    {
        .name = "const nested array member folds a selected element",
        .source = "struct P{int a[2];};const struct P p={{2,5}};\nenum { E=p.a[1] };\n_Static_assert(E==5,\"nested array element\");\n",
    },
    {
        .name = "const union folds its active member",
        .source = "union U{int x;float f;};const union U u={.x=7};\nenum { E=u.x };\n_Static_assert(E==7,\"active union value\");\n",
    },
    {
        .name = "const union inactive member cannot borrow active bytes",
        .source = "union U{int x;float f;};const union U u={.f=1.0f};\nenum { E=u.x };\n",
        .rejected = true,
    },
    {
        .name = "const floating object converts in a static initializer",
        .source = "const double k=3.5;\nint g=(int)k;\n",
    },
    {
        .name = "const pointer object retains a symbolic target in a static initializer",
        .source = "int object;int* const p=&object;\nint* q=p;\n",
    },
    {
        .name = "const pointer object can designate another completed const object",
        .source = "const int k=3;const int* const p=&k;\nenum { E=*p };\n_Static_assert(E==3,\"const pointer dereference\");\n",
    },
    {
        .name = "const pointer retains string-literal bytes for a constant element read",
        .source = "const char* const p=\"abc\";\nenum { E=p[1] };\n_Static_assert(E=='b',\"literal string element\");\n",
    },
    {
        .name = "a folded const-object zero is not an ICE-form null pointer constant",
        .source = "const int z=0;\nint* p=z;\n",
        .rejected = true,
    },
    {
        .name = "mutable scalar initialization is not const-object evidence",
        .source = "int k=3;\nenum { E=k };\n",
        .rejected = true,
    },
    {
        .name = "volatile const scalar cannot supply a constant value",
        .source = "const volatile int k=3;\nenum { E=k };\n",
        .rejected = true,
    },
    {
        .name = "a future initializer cannot satisfy an earlier const-object read",
        .source = "extern const int k;\nenum { E=k };const int k=3;\n",
        .rejected = true,
    },
    {
        .name = "const initializer cycles remain nonconstant",
        .source = "extern const int a,b;const int a=b;\nconst int b=a;enum { E=a };\n",
        .rejected = true,
    },
    {
        .name = "a mutable local shadow does not reuse the outer const receipt",
        .source = "const int k=3;\nint f(void){int k=2;enum { E=k };return E;}\n",
        .rejected = true,
    },
    {
        .name = "uninitialized automatic const object has no retained value",
        .source = "int f(void){const int k;enum { E=k };return E;}\n",
        .rejected = true,
    },
    {
        .name = "an evaluated side effect prevents folding a const-object expression",
        .source = "int n;const int k=3;\nenum { E=(++n,k) };\n",
        .rejected = true,
    },
    {
        .name = "a runtime const initializer does not become a constant receipt",
        .source = "int get(void);int f(void){const int k=get();enum { E=k };return E;}\n",
        .rejected = true,
    },
    {
        .name = "a mutable array index cannot select a constant element",
        .source = "const int a[2]={1,2};int n;\nenum { E=a[n] };\n",
        .rejected = true,
    },
    {
        .name = "a one-past const array element is not a readable constant subobject",
        .source = "const int a[2]={1,2};\nenum { E=a[2] };\n",
        .rejected = true,
    },
    {
        .name = "another effectful aggregate member prevents a partial constant receipt",
        .source = "int get(void);struct P{int a,b;};\nint f(void){const struct P p={3,get()};enum { E=p.a };return E;}\n",
        .rejected = true,
    },
    {
        .name = "a mutable pointer object cannot supply a constant dereference",
        .source = "const int k=3;const int* p=&k;\nenum { E=*p };\n",
        .rejected = true,
    },
    {
        .name = "const atomic scalar may be read during explicit constant evaluation",
        .source = "const _Atomic(int) az=0;\nenum { E=az };\n_Static_assert(E==0,\"const atomic value\");\n",
    },
    {
        .name = "const record atomic member may be read during explicit constant evaluation",
        .source = "struct P{_Atomic(int) x;};const struct P p={0};\nenum { E=p.x };\n_Static_assert(E==0,\"const atomic member value\");\n",
    },
    {
        .name = "an enumerator resets const-object provenance to an ordinary ICE zero",
        .source = "const int z=0;enum { G=z };\nint* q=G;\n_Static_assert(G==0,\"enumerator zero\");\n",
    },
    {
        .name = "a const member does not make its mutable defining record constant",
        .source = "struct P{const int x;int y;};struct P p={3,4};\nenum { E=p.x };\n",
        .rejected = true,
    },
    {
        .name = "a const member does not make its mutable defining union constant",
        .source = "union U{const int x;int y;};union U u={.x=3};\nenum { E=u.x };\n",
        .rejected = true,
    },
    {
        .name = "const atomic scalar supplies a static initializer without becoming a runtime load",
        .source = "const _Atomic(int) az=3;\nint g=az;\n",
    },
    {
        .name = "implicit const record members retain initialization zeros",
        .source = "struct P{int x,y;};const struct P p={4};\n_Static_assert(p.y==0,\"zero\");\n",
    },
    {
        .name = "implicit const array elements retain initialization zeros",
        .source = "const int a[2]={1};\n_Static_assert(a[1]==0,\"zero\");\n",
    },
    {
        .name = "const record value copy preserves explicit and implicit subobjects",
        .source = "struct P{int x,y;};const struct P p={4};const struct P q=p;\n_Static_assert(q.x==4 && q.y==0,\"copy\");\n",
    },
    {
        .name = "an inactive union member's address remains a valid symbolic pointer",
        .source = "union U{int x,y;};const union U u={.x=3};const int* const p=&u.y;\nconst int* q=p;\n",
    },
    {
        .name = "dereferencing a pointer to an inactive union member remains nonconstant",
        .source = "union U{int x,y;};const union U u={.x=3};const int* const p=&u.y;\nenum{E=*p};\n",
        .rejected = true,
    },
    {
        .name = "same-type inactive union members cannot share constant-read proof",
        .source = "union U{int x,y;};const union U u={.x=3};\nenum{E=u.y};\n",
        .rejected = true,
    },
    {
        .name = "const nonbyte bit-field values retain their stored bits",
        .source = "struct P{unsigned a:3,b:3;};const struct P p={1,5};\n_Static_assert(p.b==5,\"bits\");\n",
    },
    {
        .name = "a block-scope const bound remains a variable-length array type",
        .source = "int f(void){const int k=3;int a[k];_Static_assert(sizeof a==3*sizeof(int),\"bound\");return sizeof a;}\n",
        .rejected = true,
    },
};

static void check_const_source_atomic_runtime(CheckTest* test)
{
    static const char source[] =
        "const _Atomic(int) az=3;struct P{_Atomic(int) x;};const struct P p={4};\n"
        "enum{A=az,B=p.x};int g=az;int h=p.x;\n"
        "_Static_assert(az==3 && p.x==4,\"source constant atomic reads\");\n"
        "int f(void){return az;}int m(void){return p.x;}\n";
    for (uint32_t path = 0; path < 2; path += 1)
    {
        static InternTable identifiers;
        static Preprocessor pp;
        static Parser parser;
        const PPOptions pp_options = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true };
        const ParseOptions parse_options = { .scalar = path == 0 };
        const CheckOptions check_options = { .scalar = path == 0, .verify = true };
        CheckTestTypes types = { 0 };
        Arena input_storage = arena_reserve(UINT64_C(1) << 24);
        Arena output_storage = arena_reserve(UINT64_C(1) << 24);
        Arena scratch = arena_reserve(UINT64_C(1) << 24);
        bool ready = input_storage.base != NULL && output_storage.base != NULL && scratch.base != NULL &&
                     intern_initialize(&identifiers) && pp_initialize(&pp, &pp_options, &identifiers) &&
                     parse_initialize(&parser, &parse_options, pp_options.language) && check_test_types_open(&types) &&
                     pp_run_source(&pp, "const-atomic-runtime.c", (const uint8_t*)source, sizeof(source) - 1) &&
                     parse_translation_unit(&parser, &pp);
        CheckInput input = { 0 };
        CheckStream stream = { 0 };
        CheckInitializers initializers = { 0 };
        ready = ready && check_input(&input, &parser, &pp, &types.table, &input_storage, &scratch) &&
                check_graph_run(&types.table, &input.graph, &output_storage, &scratch, &check_options, &stream) && stream.report_count == 0 &&
                check_initializers_run(&initializers, &input, &stream, &parser, &types.table, &output_storage, &scratch);
        check_test_expect(test, ready && !input.resource && stream.constant_evaluated != NULL,
                          "const atomic source contexts accept while retaining an explicit graph evaluation closure");
        uint32_t returns = 0;
        for (uint32_t node = 0; ready && node < stream.count; node += 1)
        {
            const uint32_t token = input.graph.tokens[node];
            if (input.graph.ops[node] == CHECK_OP_CONVERT && token < parser.tokens.count && parser.tokens.kinds[token] == PARSE_KIND_RETURN)
            {
                const uint32_t operand = input.graph.operand0[node];
                const bool runtime = stream.constant_evaluated != NULL && stream.constant_evaluated[node] == 0 &&
                                     operand < node && stream.constant_evaluated[operand] == 0;
                check_test_expect(test, runtime && (stream.conversions[node].operands[0].flags & BUSTER_CHECK_CONVERT_LVALUE) != 0 &&
                                       (stream.values[node].flags & BUSTER_CHECK_SIDE_EFFECT) != 0 &&
                                       stream.values[node].constant_kind == CHECK_CONSTANT_NONE,
                                  "ordinary const atomic scalar/member returns retain observable lvalue loads outside the constant closure");
                returns += 1;
            }
        }
        check_test_expect(test, ready && returns == 2, "atomic runtime control exercises both a scalar root and a const record member");
        arena_release(&scratch);
        arena_release(&output_storage);
        arena_release(&input_storage);
        check_test_types_close(&types);
        parse_release(&parser);
        pp_release(&pp);
        intern_release(&identifiers);
    }
}

static void check_test_const_sources(CheckTest* test, const char* clang)
{
    check_const_source_atomic_runtime(test);
    enum { FILE_COUNT = BUSTER_ARRAY_LENGTH(check_const_source_fixtures) };
    char directory[] = "/tmp/buster-checker-const-XXXXXX";
    const bool created = mkdtemp(directory) != NULL;
    bool ready = created;
    char paths[FILE_COUNT][160];
    char* pointers[FILE_COUNT];
    ParseTestClang outcomes[FILE_COUNT];
    for (uint32_t i = 0; ready && i < FILE_COUNT; i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        pointers[i] = paths[i];
        ready = parse_test_write(paths[i], check_const_source_fixtures[i].source, strlen(check_const_source_fixtures[i].source));
    }
    const char* arguments[] = { "-std=gnu17" };
    ready = ready && parse_test_clang(clang, arguments, BUSTER_ARRAY_LENGTH(arguments), pointers, FILE_COUNT, outcomes);
    check_test_expect(test, ready, "const-object source Clang oracle is available");
    for (uint32_t i = 0; ready && i < FILE_COUNT; i += 1)
    {
        char description[224];
        snprintf(description, sizeof(description), "%s: recorded GNU17 acceptance agrees with Clang", check_const_source_fixtures[i].name);
        check_test_expect(test, outcomes[i].rejected == check_const_source_fixtures[i].rejected, description);
        check_test_source(test, check_const_source_fixtures + i, paths[i], outcomes + i);
    }
    for (uint32_t i = 0; created && i < FILE_COUNT; i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        unlink(paths[i]);
    }
    if (created)
    {
        rmdir(directory);
    }
}

static void check_test_const_programs(CheckTest* test, const char* clang)
{
    check_test_program_source(test,
        "int calls;int touch(void){calls+=1;return 9;}struct S{int x,y;};struct P{struct S s;};const struct S base={8,9};\n"
        "int tested(int n){const struct P p={.s=(touch(),base),.s={1,2}};enum{E=p.s.x};return E+p.s.y+calls+n;}\n",
        "const-object-whole-replacement-drops-effectful-aggregate-rvalue", clang);
    check_test_program_source(test,
        "int calls;struct S{int x,y;};struct P{struct S s;};struct S make(int n){calls+=1;return (struct S){n,n+1};}\n"
        "int tested(int n){const struct P p={.s=make(n),.s={1,2}};enum{E=p.s.x};return E+p.s.y+calls+n;}\n",
        "const-object-whole-aggregate-replacement-drops-runtime-effect", clang);
    check_test_program_source(test,
        "int calls;int get(void){calls+=1;return 9;}\n"
        "int tested(int n){const int a[3]={[0 ... 2]=get(),[0]=1,[1]=2,[2]=3};enum{E=a[0]};return E+a[1]+a[2]+calls+n;}\n",
        "const-object-overridden-range-effect-is-not-executed", clang);
    check_test_program_source(test,
        "int calls;struct S{int x,y;};struct P{struct S s;};struct S make(int n){calls+=1;return (struct S){n,n+1};}\n"
        "int tested(int n){const struct P p={.s=make(n),.s.x=1,.s.y=2};return p.s.x+p.s.y+calls+n;}\n",
        "const-object-partial-aggregate-base-retains-runtime-effect", clang);
    check_test_program_source(test,
        "int calls;int get(void){calls+=1;return 9;}struct P{int x;};\n"
        "int tested(int n){const struct P p={.x=get(),.x=3};enum{E=p.x};return E+calls+n;}\n",
        "const-object-dead-initializer-effect-is-not-executed", clang);
    check_test_program_source(test,
        "const int k=3;int a[k];struct P{int x;};const struct P p={4};enum{E=p.x};int g=k;\n"
        "int tested(int n){const int local=3;switch(n){case local:return g+E+(int)sizeof(a);default:return n+g;}}\n",
        "const-object-case-label-and-static-initializer-values", clang);
    check_test_program_source(test,
        "int object=9;int* const p=&object;int* q=p;const int k=3;const int* const r=&k;int g=*r;\n"
        "const double d=3.5;int h=(int)d;\nint tested(int n){return *q+g+h+n;}\n",
        "const-pointer-target-and-converted-static-values", clang);
}

#endif
