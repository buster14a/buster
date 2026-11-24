// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// clang's musttail attribute (#147, c_check_musttail.c), on original fixtures. Its rules against clang's, where each
// source is first rejected: the attribute's place and arguments, the call it needs, the prototypes, the calling
// conventions, variadic functions, the noreturn callees, the signatures as similar types, and the cleanup variables'
// scopes a tail call can't leave; and what it takes (parentheses and selections around the call, the statements and
// blocks it may be in, a statement expression's return). Then programs run natively against clang -O0's build of the
// same source on both paths, whose tail calls recurse 20,000 times and more, each checking at its deepest call that
// its frame is where its first was (within 8 KB, which 20,000 calls' frames without tail calls are far past): direct
// and mutual recursion, indirect calls through tables, parameters and members, stack arguments, aggregates in
// registers and in memory, results through a hidden pointer, long double, __int128, complex, vectors in each kind of
// register, variable-length arrays (a frame pointer), realigned frames, saved registers, values past the depth
// tables, void functions, statement expressions, and calls of the harness's clang code and of the C library.
// Include after c_check_cleanup_test.c.
#ifndef BUSTER_C_CHECK_MUSTTAIL_TEST_INCLUDED
#define BUSTER_C_CHECK_MUSTTAIL_TEST_INCLUDED

static __attribute__((noinline)) void check_test_musttail_sources(CheckTest* test, const char* clang)
{
    // Its source fixtures are runtime data (#2053): tests/checker/musttail, which check_test_suite reads and runs.
    check_test_suite(test, clang, "checker/musttail", "musttail Clang oracle could not run");
}

// Each program's recursions check at their deepest call that its frame is near tested's (`near`): within 8 KB, where
// 20,000 frames without tail calls would be at least 320 KB down.
#define BUSTER_CHECK_TEST_MUSTTAIL_NEAR \
    "static char*top;static int near(void){char here;long d=top-&here;return d<8192&&d>-8192;}" \
    "static unsigned long fold(unsigned long h,unsigned long v){return (h^v)*1099511628211UL;}"

// Every shape of tail call the default target compiles: mutual recursion, a table of pointers, stack arguments, structs
// in registers, structs on the stack and a result through a hidden pointer (with stack arguments too), long double,
// __int128, complex, a variable-length array, a realigned frame, saved registers, ten doubles, sixteen longs computed past
// the depth tables, a member's pointer, a void function, a cleanup's scope ended before the call, the call in a loop and
// a switch, and statement expressions with values below them and as the return's value.

// Tail calls of other compilers' code: the harness's (clang's external_helper, also through external_apply's pointer to
// this program's function, so that the tail call returns into clang's caller), and the C library's isatty, through
// its PLT entry.

// Vectors in each kind of register, and past the eight vector registers on the stack, with x86-64-v4's features.
static const char check_test_program_musttail_vectors[] =
    BUSTER_CHECK_TEST_MUSTTAIL_NEAR
    "typedef float v4f __attribute__((vector_size(16)));typedef double v4d __attribute__((vector_size(32)));"
    "typedef int v16i __attribute__((vector_size(64)));"
    "static v4f vf(long n,v4f acc,v4f step){if(n==0)return near()?acc:(v4f){-1,-1,-1,-1};"
    "__attribute__((musttail)) return vf(n-1,acc+step,step*0.5f+1.0f);}"
    "static v4d vd(long n,v4d acc,v4d step){if(n==0)return near()?acc:(v4d){-1,-1,-1,-1};"
    "__attribute__((musttail)) return vd(n-1,acc+step,step*0.25+2.0);}"
    "static v16i vi(long n,v16i acc,v16i step){if(n==0)return near()?acc:(v16i){-1};"
    "__attribute__((musttail)) return vi(n-1,acc+step,(step^acc)&0xffff);}"
    "static v4f nine(long n,v4f a,v4f b,v4f c,v4f d,v4f e,v4f f,v4f g,v4f h,v4f i,v4f j){"
    "if(n==0)return near()?a+b*2+c*3+d*4+e*5+f*6+g*7+h*8+i*9+j*10:(v4f){-1,-1,-1,-1};"
    "__attribute__((musttail)) return nine(n-1,b,c,d,e,f,g,h,i,j,a+1.0f);}"
    "int tested(int n){char mark;top=&mark;long d=20000+(n&15);unsigned long h=0;"
    "v4f a=vf(d,(v4f){1,2,3,(float)n},(v4f){0.5f,0.25f,0.125f,1});for(int i=0;i<4;i++)h=fold(h,(unsigned long)(long)(a[i]*8));"
    "v4d b=vd(d,(v4d){1,2,3,n},(v4d){0.5,0.25,0.125,1});for(int i=0;i<4;i++)h=fold(h,(unsigned long)(long)(b[i]*8));"
    "v16i c=vi(d,(v16i){1,2,3,n},(v16i){5,6,7,8,9,10,11,12});for(int i=0;i<16;i++)h=fold(h,(unsigned long)c[i]);"
    "v4f e=nine(d,(v4f){1},(v4f){2},(v4f){3},(v4f){4},(v4f){5},(v4f){6},(v4f){7},(v4f){8},(v4f){9},(v4f){(float)n});"
    "for(int i=0;i<4;i++)h=fold(h,(unsigned long)(long)e[i]);return (int)(h%1000003);}";

static __attribute__((noinline)) void check_test_musttail_programs(CheckTest* test, const char* clang)
{
    check_test_program_suite(test, "checker/programs/musttail-programs", clang);
    X86Target target = { 0 };
    const bool parsed = x86_target_parse("x86-64-v4", strlen("x86-64-v4"), &target);
    check_test_expect(test, parsed, "the musttail vector programs' target, x86-64-v4");
    if (parsed)
    {
        const X86Target native = x86_target_native();
        const bool run_native = (native.features & target.features) == target.features;
        check_test_program_build_release();
        check_test_program_march = "-march=x86-64-v4";
        check_test_program_source_native_limits(test, check_test_program_musttail_vectors, "musttail-vectors", clang, false, -1, 0, &target, run_native);
        check_test_program_build_release();
        check_test_program_march = "-march=x86-64";
    }
}

// The errors' count past its limit (#936's review: a scope's error for each active cleanup at each return can pass a
// uint32_t's): two returns in three cleanups' scopes, six errors, checked by both paths with a limit of 5, which must fail
// for want of room with neither arena changed, then of 6, which must give all six.
static __attribute__((noinline)) void check_test_musttail_limit(CheckTest* test)
{
    static const char source[] = "void c(int *);\nint f(int);\nint g(int x)\n{\nint a __attribute__((cleanup(c))) = x;\nint b __attribute__((cleanup(c))) = x;\n"
                                 "int d __attribute__((cleanup(c))) = x;\nif (x)\n__attribute__((musttail)) return f(x);\n__attribute__((musttail)) return f(x + 1);\n}\n";
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    const PPOptions pp_options = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true };
    bool preprocessed = intern_initialize(&identifiers) && pp_initialize(&pp, &pp_options, &identifiers) &&
                        pp_run_source(&pp, "musttail-limit", (const uint8_t*)source, (uint32_t)strlen(source));
    check_test_expect(test, preprocessed, "musttail limit: source preprocessing");
    for (uint32_t path = 0; preprocessed && path < 2; path += 1)
    {
        CheckTestTypes types = { 0 };
        Arena input_arena = arena_reserve(UINT64_C(1) << 24);
        Arena output = arena_reserve(UINT64_C(1) << 24);
        Arena scratch = arena_reserve(UINT64_C(1) << 24);
        const ParseOptions parsing = { .scalar = path == 0 };
        const CheckOptions checking = { 0 };
        CheckInput input = { 0 };
        CheckStream stream = { 0 };
        const bool ready = input_arena.base != 0 && output.base != 0 && scratch.base != 0 && check_test_types_open(&types) &&
                           parse_initialize(&parser, &parsing, pp_options.language) && parse_translation_unit(&parser, &pp) && parser.errors == 0 &&
                           check_input(&input, &parser, &pp, &types.table, &input_arena, &scratch) &&
                           check_graph_run(&types.table, &input.graph, &input_arena, &scratch, &checking, &stream) && stream.report_count == 0;
        check_test_expect(test, ready, "musttail limit: the source is checked");
        const uint64_t output_mark = output.length;
        const uint64_t scratch_mark = scratch.length;
        CheckMusttails short_of = { 0 };
        const bool refused = ready && !check_musttail_run_limited(&short_of, &input, &parser, &stream, &types.table, &output, &scratch, 5);
        check_test_expect(test, refused && short_of.resource && short_of.count == 0 && short_of.diagnostics == NULL && output.length == output_mark &&
                                    scratch.length == scratch_mark,
                          "musttail limit: six errors past a limit of 5 fail for want of room, with neither arena changed");
        CheckMusttails enough = { 0 };
        const bool taken = ready && check_musttail_run_limited(&enough, &input, &parser, &stream, &types.table, &output, &scratch, 6);
        bool scopes = taken && enough.count == 6;
        for (uint32_t i = 0; scopes && i < enough.count; i += 1)
        {
            scopes = enough.diagnostics[i].error == CHECK_MUSTTAIL_SCOPE;
        }
        check_test_expect(test, scopes && scratch.length == scratch_mark, "musttail limit: a limit of 6 gives the six scopes' errors");
        parse_release(&parser);
        check_test_types_close(&types);
        arena_release(&scratch);
        arena_release(&output);
        arena_release(&input_arena);
    }
    pp_release(&pp);
    intern_release(&identifiers);
}

static __attribute__((noinline)) void check_test_musttails(CheckTest* test, const char* clang)
{
    check_test_musttail_sources(test, clang);
    check_test_musttail_limit(test);
    check_test_musttail_programs(test, clang);
}

#endif
