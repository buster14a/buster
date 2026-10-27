// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #597: tiny C aggregate assignments must have bounded typed streams and machine code as the object grows.
// Include after c_check_program_test.c's source/execution helpers. Clang is an external semantic oracle.

static void check_test_program_copy_shape(CheckTest* test, uint32_t size, bool volatile_copy)
{
    char source[512];
    snprintf(source, sizeof(source),
             "struct S{unsigned char bytes[%u];};"
             "void copy(%sstruct S*d,const %sstruct S*s){*d=*s;}"
             "%sunsigned char* kept(%sstruct S*d,const %sstruct S*s){return (*d=*s).bytes;}"
             "void same(%sstruct S*d){*d=*d;}", size, volatile_copy ? "volatile " : "", volatile_copy ? "volatile " : "",
             volatile_copy ? "volatile " : "", volatile_copy ? "volatile " : "", volatile_copy ? "volatile " : "", volatile_copy ? "volatile " : "");
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    const PPOptions preprocessing = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true };
    const bool preprocessed = intern_initialize(&identifiers) && pp_initialize(&pp, &preprocessing, &identifiers) &&
                              pp_run_source(&pp, "aggregate-copy-shape", (const uint8_t*)source, (uint32_t)strlen(source));
    check_test_expect(test, preprocessed, "aggregate copy size probe preprocesses");
    uint64_t counts[2][3] = { 0 };
    for (uint32_t path = 0; preprocessed && path < 2; path += 1)
    {
        CheckTestTypes types = { 0 };
        Arena input_arena = arena_reserve(UINT64_C(1) << 26);
        Arena output = arena_reserve(UINT64_C(1) << 26);
        Arena bytes = arena_reserve(UINT64_C(1) << 22);
        Arena scratch = arena_reserve(UINT64_C(1) << 26);
        const ParseOptions parsing = { .scalar = path == 0 };
        const CheckOptions checking = { .scalar = path == 0 };
        CheckInput input = { 0 };
        CheckStream stream = { 0 };
        CheckInitializers plans = { 0 };
        CheckAsms asms = { 0 };
        CheckProgram program = { 0 };
        CheckProgramCode code = { 0 };
        bool ready = input_arena.base != 0 && output.base != 0 && bytes.base != 0 && scratch.base != 0 && check_test_types_open(&types) &&
                     parse_initialize(&parser, &parsing, preprocessing.language) && parse_translation_unit(&parser, &pp) && parser.errors == 0 &&
                     check_input(&input, &parser, &pp, &types.table, &input_arena, &scratch) &&
                     check_graph_run(&types.table, &input.graph, &input_arena, &scratch, &checking, &stream) && stream.report_count == 0 &&
                     check_initializers_run(&plans, &input, &stream, &parser, &types.table, &input_arena, &scratch) &&
                     check_asms_run(&asms, &input, &stream, &types.table, &input_arena, &scratch);
        const CheckProgramOptions options = { .initializers = &plans, .asms = &asms, .identifiers = &identifiers, .scalar = path == 0 };
        ready = ready && check_program(&program, &parser, &input, &stream, &types.table, options, &input_arena, &scratch) &&
                check_program_emit(&code, &program, &parser, &identifiers, (LinkCapacity){ 0 }, path == 0,
                                   &output, &bytes, &scratch, NULL, NULL, false, true);
        char description[160];
        snprintf(description, sizeof(description), "%u-byte copies lower, validate and encode by the %s path", size, path == 0 ? "scalar" : "vector");
        check_test_expect(test, ready && code.lowered.count == 3, description);
        for (uint32_t b = 0; ready && b < code.lowered.count; b += 1)
        {
            const Tier0Function* function = &code.lowered.functions[b];
            counts[path][0] += function->count;
            check_test_expect(test, function->count <= (volatile_copy ? 64u : 24u), "one nonvolatile aggregate assignment has a bounded typed stream");
        }
        counts[path][1] = code.records;
        counts[path][2] = code.code_bytes;
        check_test_expect(test, ready && code.records <= (volatile_copy ? 256u : 160u) && code.code_bytes <= (volatile_copy ? 1536u : 768u),
                          "discarded, consumed and self copies have bounded machine records and code bytes");
        check_test_expect(test, ready && code.lowered.runtime_symbol_count == (uint32_t)(size > BUSTER_TIER0_MAX_COPY_BYTES && !volatile_copy),
                          "small copies stay inline and large copies share one runtime symbol");
        parse_release(&parser);
        check_test_types_close(&types);
        arena_release(&scratch);
        arena_release(&bytes);
        arena_release(&output);
        arena_release(&input_arena);
    }
    check_test_expect(test, memcmp(counts[0], counts[1], sizeof(counts[0])) == 0, "copy typed entries, machine records and code bytes agree across paths");
    pp_release(&pp);
    intern_release(&identifiers);
}

static void check_test_program_copies(CheckTest* test, const char* clang)
{
    static const uint32_t sizes[] = { 1, 15, 16, 255, 256, 257, 1024, 4096, 16384, 65536, 262144 };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(sizes); i += 1)
    {
        check_test_program_copy_shape(test, sizes[i], false);
    }
    check_test_program_copy_shape(test, 513, true);
    static const uint32_t execution_sizes[] = { 255, 256, 257, 4097, 16384, 262145 };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(execution_sizes); i += 1)
    {
        char source[2048];
        char name[64];
        snprintf(name, sizeof(name), "aggregate-copy-runtime-%u", execution_sizes[i]);
        snprintf(source, sizeof(source),
                 "struct S{unsigned char c[%u];};static struct S a,b,c;static int ds,ss;"
                 "struct S*dest(void){ds++;return &b;}const struct S*src(void){ss++;return &a;}"
                 "int tested(int n){ds=ss=0;for(unsigned i=0;i<sizeof(a.c);i++)a.c[i]=(unsigned char)(i*37+n);"
                 "long live=n*17L;double floating=n*0.25;"
                 "long value=live+(*dest()=*src()).c[(unsigned)n%%sizeof(a.c)];floating+=(b=a).c[0];"
                 "c=(b=a);b=b;value+=(c=a).c[sizeof(a.c)-1];"
                 "for(unsigned i=0;i<sizeof(a.c);i++)if(b.c[i]!=a.c[i]||c.c[i]!=a.c[i])return -1002;"
                 "return (int)(value+floating*4)+ds*100+ss*1000;}", execution_sizes[i]);
        check_test_program_source(test, source, name, clang);
    }
    check_test_program_source(test,
        "struct S{unsigned char c[513];};static volatile struct S a,b;"
        "int tested(int n){for(int i=0;i<513;i++)a.c[i]=(unsigned char)(n+i);b=a;b=b;"
        "struct S r=a;b=r;return (b=a).c[512]+r.c[0];}", "aggregate-copy-volatile-accesses", clang);
    check_test_program_source(test,
        "extern void* memmove(void*,const void*,unsigned long);struct S{char c[513];};static struct S a,b;"
        "int tested(int n){for(int i=0;i<513;i++)a.c[i]=(char)(n+i);b=a;return (b=a).c[512]+b.c[0];}",
        "aggregate-copy-declared-runtime", clang);
    check_test_program_source(test,
        "struct S{char c[513];};static struct S a,b;int tested(int n){a.c[512]=(char)n;b=a;"
        "return __builtin_isnan(__builtin_sqrt((double)n))+b.c[512];}",
        "aggregate-copy-shares-runtime-publication", clang);
    check_test_program_source(test,
        "struct S{char c[1025];};struct S pass(struct S s){struct S r=s;return r;}"
        "int tested(int n){struct S a;for(int i=0;i<1025;i++)a.c[i]=(char)(n+i);struct S b=pass(a);"
        "for(int i=0;i<1025;i++)if(a.c[i]!=b.c[i])return -1003;return b.c[1024];}",
        "aggregate-copy-argument-initializer-return", clang);
}
