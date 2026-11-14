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
static const char check_test_program_musttail_shapes[] =
    BUSTER_CHECK_TEST_MUSTTAIL_NEAR
    "static int odd(unsigned n);static int even(unsigned n){if(n==0)return near();__attribute__((musttail)) return odd(n-1);}"
    "static int odd(unsigned n){if(n==0)return 2*near();__attribute__((musttail)) return even(n-1);}"
    "typedef long step_fn(long n,long acc);static long step_a(long n,long acc);static long step_b(long n,long acc);"
    "static step_fn*const steps[2]={step_a,step_b};"
    "static long step_a(long n,long acc){if(n==0)return near()?acc:-1;__attribute__((musttail)) return steps[n&1](n-1,acc+1);}"
    "static long step_b(long n,long acc){if(n==0)return near()?acc:-1;[[clang::musttail]] return steps[n&1](n-1,acc+3);}"
    "static long many(long a,long b,long c,long d,long e,long f,long g,long h){"
    "if(a==0)return near()?b+c+d+e+f+g*3+h*5:-1;__attribute__((musttail)) return many(a-1,b+1,c,d,e,f,h,g+1);}"
    "struct pair{long a,b;};struct mixed{double d;long l;};"
    "static struct pair pairs(long n,struct pair p,struct mixed m){if(n==0)return (struct pair){near()?p.a+(long)m.d:-1,p.b+m.l};"
    "__attribute__((musttail)) return pairs(n-1,(struct pair){p.b,p.a+1},(struct mixed){m.d+0.5,m.l-1});}"
    "struct big{long a,b,c,d;};"
    "static struct big bigs(long n,struct big x){if(n==0){x.a=near()?x.a:-1;return x;}"
    "__attribute__((musttail)) return bigs(n-1,(struct big){x.b,x.c,x.d,x.a+1});}"
    "static struct big bigmany(long n,long b,long c,long d,long e,long f,long g,struct big x,long h){"
    "if(n==0)return (struct big){near()?x.a+b+c:-1,x.b+d+e,x.c+f+g,x.d+h};"
    "__attribute__((musttail)) return bigmany(n-1,c,d,e,f,g,b+1,(struct big){x.d,x.a,x.b,x.c+1},h^n);}"
    "static long double ld(long n,long double acc,long double step){if(n==0)return near()?acc:-1;"
    "__attribute__((musttail)) return ld(n-1,acc+step,step);}"
    "static unsigned __int128 wide(long n,unsigned __int128 acc,unsigned __int128 k){if(n==0)return near()?acc:0;"
    "__attribute__((musttail)) return wide(n-1,acc*3+k,k^(acc>>7));}"
    "static _Complex double cplx(long n,_Complex double z,_Complex double w){if(n==0)return near()?z:-1;"
    "__attribute__((musttail)) return cplx(n-1,z+w,w*0.5);}"
    "static long vla(long n,long acc){long buf[(n&3)+1];for(long i=0;i<=(n&3);i+=1)buf[i]=n+i;if(n==0)return near()?acc:-1;"
    "__attribute__((musttail)) return vla(n-1,acc+buf[n&3]);}"
    "static long aligned(long n,long acc){_Alignas(64) char buf[64];for(int i=0;i<64;i+=1)buf[i]=(char)(n&0x7f);"
    "if(((unsigned long)buf&63)!=0)return -2;if(n==0)return near()?acc:-1;__attribute__((musttail)) return aligned(n-1,acc+buf[n&63]);}"
    "static long helper(long x){return x*2+1;}"
    "static long saved(long n,long acc){long k=helper(n);if(n==0)return near()?acc+k:-1;__attribute__((musttail)) return saved(n-1,acc^k);}"
    "static double dbl(long n,double a,double b,double c,double d,double e,double f,double g,double h,double i,double j){"
    "if(n==0)return near()?a+b*2+c*3+d*4+e*5+f*6+g*7+h*8+i*9+j*10:-1;__attribute__((musttail)) return dbl(n-1,b,c,d,e,f,g,h,i,j,a+0.25);}"
    "static long sixteen(long a0,long a1,long a2,long a3,long a4,long a5,long a6,long a7,long a8,long a9,long a10,long a11,long a12,long a13,long a14,long a15){"
    "if(a0==0)return near()?a1+a2*2+a3*3+a4*4+a5*5+a6*6+a7*7+a8*8+a9*9+a10*10+a11*11+a12*12+a13*13+a14*14+a15*15:-1;"
    "__attribute__((musttail)) return sixteen(a0-1,(a2+(a3^(a4+(a5^(a6+(a7^(a8+(a9^(a10+(a11^(a12+(a13^(a14+(a15^a1))))))))))))))&0xffff,"
    "a4,a5,a6,a7,a8,a9,a10,a11,a12,a13,a14,a15,a1+1,a0&7);}"
    "struct cont;typedef long cont_fn(long n,long acc,const struct cont*k);struct cont{cont_fn*next;long add;};"
    "static long cont_a(long n,long acc,const struct cont*k){if(n==0)return near()?acc:-1;__attribute__((musttail)) return k->next(n-1,acc+k->add,k);}"
    "static void counter(long n,long*out){*out+=n;if(n==0){*out=near()?*out:-1;return;}__attribute__((musttail)) return counter(n-1,out);}"
    "static unsigned cleaned;static void clean(long*p){cleaned=cleaned*31u+(unsigned)*p;}"
    "static long scoped(long n,long acc){{long c __attribute__((cleanup(clean)))=n;acc+=c&1;}if(n==0)return near()?acc:-1;"
    "__attribute__((musttail)) return scoped(n-1,acc);}"
    "static long looped(long n,long acc){for(;;){switch(n&3){case 0:if(n==0)return near()?acc:-1;"
    "__attribute__((musttail)) return looped(n-1,acc+2);default:break;}{__attribute__((musttail)) return looped(n-1,acc+(n&3));}}}"
    "static long stmt_below(long n,long acc){if(n==0)return near()?acc:-1;return acc+({__attribute__((musttail)) return stmt_below(n-1,acc+5);0L;});}"
    "static long stmt_value(long n,long acc){if(n==0)return near()?acc:-1;return ({__attribute__((musttail)) return stmt_value(n-1,acc+7);0L;})+1;}"
    "int tested(int n){char mark;top=&mark;long d=20000+(n&15);unsigned long h=(unsigned long)(even((unsigned)d)+odd((unsigned)d)*3);"
    "h=fold(h,(unsigned long)step_a(d,n));h=fold(h,(unsigned long)many(d,1,2,3,4,n,6,7));"
    "struct pair p=pairs(d,(struct pair){1,n},(struct mixed){0.25,5});h=fold(h,(unsigned long)(p.a*3+p.b));"
    "struct big b=bigs(d,(struct big){1,2,n,4});h=fold(h,(unsigned long)(b.a+b.b*3+b.c*5+b.d*7));"
    "b=bigmany(d,1,2,3,4,5,n,(struct big){7,8,9,10},11);h=fold(h,(unsigned long)(b.a+b.b*3+b.c*5+b.d*7));"
    "h=fold(h,(unsigned long)(long)(ld(d,n,0.125L)*8));unsigned __int128 w=wide(d,1,(unsigned)n+7);h=fold(h,(unsigned long)(w>>64));h=fold(h,(unsigned long)w);"
    "_Complex double z=cplx(d,1.0+n,2.0);h=fold(h,(unsigned long)(long)(__real__ z*1024));h=fold(h,(unsigned long)(long)(__imag__ z*1024));"
    "h=fold(h,(unsigned long)vla(d,n));h=fold(h,(unsigned long)aligned(d,n));h=fold(h,(unsigned long)saved(d,n));"
    "h=fold(h,(unsigned long)(long)(dbl(d,1,2,3,4,5,6,7,8,9,n)*4));h=fold(h,(unsigned long)sixteen(d,1,2,3,4,5,6,7,8,9,10,11,12,13,14,n));"
    "struct cont k={cont_a,3};h=fold(h,(unsigned long)cont_a(d,n,&k));long total=n;counter(d,&total);h=fold(h,(unsigned long)total);"
    "cleaned=0;h=fold(h,(unsigned long)scoped(d,n));h=fold(h,cleaned);h=fold(h,(unsigned long)looped(d,n));"
    "h=fold(h,(unsigned long)stmt_below(d,n));h=fold(h,(unsigned long)stmt_value(d,n));return (int)(h%1000003);}";

// Tail calls of other compilers' code: the harness's (clang's external_helper, also through external_apply's pointer to
// this program's function, so that the tail call returns into clang's caller), and the C library's isatty, through
// its PLT entry.
static const char check_test_program_musttail_external[] =
    "int external_helper(int n);int external_apply(int(*function)(int),int n);int isatty(int fd);"
    "static int hop(int n){__attribute__((musttail)) return external_helper(n+1);}"
    "int mine(int n){if(n>3)return hop(n);__attribute__((musttail)) return external_helper(n);}"
    "static int terminal(int fd){__attribute__((musttail)) return isatty(fd);}"
    "int tested(int n){return hop(n)+mine(n)*7+external_apply(mine,n)*31+terminal(n&3)*1000;}";

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
    check_test_program_source(test, check_test_program_musttail_shapes, "musttail-shapes", clang);
    check_test_program_source(test, check_test_program_musttail_external, "musttail-external-callees", clang);
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
        const CheckOptions checking = { .scalar = path == 0 };
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
