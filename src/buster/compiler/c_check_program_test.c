// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Real C source -> checked bodies -> unit ABI -> Tier0 -> shared code/data Link -> ELF. The independent Clang
// build of the same source is linked beside our object and executed on the same bounded inputs. Both source and
// backend schedules are exercised, and full Tier0 columns (including ABI locations) agree before code emission.
// Include after c_checker_test.c's shared helpers and c_check_program.c.
#ifndef BUSTER_C_CHECK_PROGRAM_TEST_INCLUDED
#define BUSTER_C_CHECK_PROGRAM_TEST_INCLUDED

#include "c_check_program_helpers_test.c"
#include "c_check_program_diagnostic_test.c"

static bool check_test_program_column(const void* a, const void* b, uint64_t size)
{
    return size == 0 || (a != 0 && b != 0 && memcmp(a, b, size) == 0);
}

static bool check_test_program_function(const Tier0Function* a, const Tier0Function* b)
{
    const bool equal = a->count == b->count && a->local_count == b->local_count && a->parameter_count == b->parameter_count &&
                       a->call_count == b->call_count && a->argument_count == b->argument_count &&
                       check_test_program_column(a->kinds, b->kinds, a->count) &&
                       check_test_program_column(a->classes, b->classes, a->count) &&
                       check_test_program_column(a->values, b->values, (uint64_t)a->count * sizeof(uint64_t)) &&
                       check_test_program_column(a->local_classes, b->local_classes, a->local_count) &&
                       check_test_program_column(a->local_flags, b->local_flags, a->local_count) &&
                       check_test_program_column(a->parameters, b->parameters, (uint64_t)a->parameter_count * sizeof(AbiLocation)) &&
                       memcmp(&a->result, &b->result, sizeof(AbiReturn)) == 0 &&
                       check_test_program_column(a->call_targets, b->call_targets, (uint64_t)a->call_count * sizeof(SymbolId)) &&
                       check_test_program_column(a->call_first, b->call_first, (uint64_t)a->call_count * sizeof(uint32_t)) &&
                       check_test_program_column(a->call_argument_counts, b->call_argument_counts, (uint64_t)a->call_count * sizeof(uint32_t)) &&
                       check_test_program_column(a->call_variadic, b->call_variadic, a->call_count) &&
                       check_test_program_column(a->calls, b->calls, (uint64_t)a->call_count * sizeof(AbiCall)) &&
                       check_test_program_column(a->argument_locations, b->argument_locations, (uint64_t)a->argument_count * sizeof(AbiLocation)) &&
                       check_test_program_column(a->argument_classes, b->argument_classes, a->argument_count);
    return equal;
}

static bool check_test_program_object(Link* link, const char* path, Arena* scratch)
{
    const uint64_t mark = scratch->length;
    ObjectUnits units = { 0 };
    ObjectSymbols symbols = { 0 };
    bool valid = link_check(link) == LINK_ERROR_NONE && object_prepare(&units, link, 1, scratch) && object_map_symbols(&symbols, link, scratch);
    if (valid)
    {
        const Object object = object_build(link, &units, &symbols, 0, (ObjectOptions){ 0 }, BUSTER_S8("checked-source.c"), scratch);
        valid = object.error == OBJECT_ERROR_NONE && object_write_file(path, object.bytes, object.size) == OBJECT_ERROR_NONE;
    }
    arena_pop_to(scratch, mark);
    return valid;
}

static bool check_test_program_oracle(const Link* link, const char* source, const char* path)
{
    FILE* file = fopen(path, "wb");
    bool valid = file != 0;
    for (SymbolId symbol = 1; valid && symbol < link->symbols.count; symbol += 1)
    {
        const uint8_t flags = link->symbols.flags[symbol];
        if ((flags & BUSTER_LINK_SYMBOL_BINDING) == BUSTER_LINK_SYMBOL_GLOBAL && link->symbols.atom[symbol] != 0)
        {
            const NameId name = link->symbols.name[symbol];
            valid = fprintf(file, "#define %.*s oracle_%.*s\n", (int)link->names.length[name], link->names.bytes[name],
                            (int)link->names.length[name], link->names.bytes[name]) > 0;
        }
    }
    if (valid)
    {
        valid = fwrite(source, 1, strlen(source), file) == strlen(source);
    }
    if (file != 0)
    {
        valid = fclose(file) == 0 && valid;
    }
    return valid;
}

static bool check_test_program_execute(Link* link, const char* source, const char* clang, Arena* scratch)
{
    static const char harness[] =
        "#include <stdio.h>\n"
        "#include <fenv.h>\n"
        "#include <errno.h>\n"
        "#include <xmmintrin.h>\n"
        "#include <signal.h>\n#include <sys/resource.h>\n#include <sys/wait.h>\n#include <unistd.h>\n"
        "int tested(int); int oracle_tested(int);\n"
        "void terminal_probe(void) __attribute__((weak));\n"
        "int external_data=17; int external_helper(int n){return n*3;}\n"
        "int external_apply(int(*function)(int),int n){return function(n);}\n"
        "int change_rounding(int n){const int modes[]={FE_TONEAREST,FE_TOWARDZERO,FE_UPWARD,FE_DOWNWARD};return fesetround(modes[(unsigned)n&3]);}\n"
        "int clear_fp(void){return feclearexcept(FE_ALL_EXCEPT);}\n"
        "int fp_state(void){int f=fetestexcept(FE_ALL_EXCEPT);return !!(f&FE_INVALID)+2*!!(f&FE_DIVBYZERO)+4*!!(f&FE_OVERFLOW)+8*!!(f&FE_UNDERFLOW)+16*!!(f&FE_INEXACT);}\n"
        "int change_daz(int n){unsigned old=_mm_getcsr();_mm_setcsr((old&~64u)|((unsigned)n&1u)*64u);return (old>>6)&1;}\n"
        "double fp_special(int n){const unsigned long b[]={0,0x8000000000000000UL,0x7ff0000000000000UL,0xfff0000000000000UL,"
        "0x7ff8000000000001UL,0x7ff0000000000001UL,1,0x8000000000000001UL,0x3ff8000000000000UL};"
        "union{unsigned long bits;double value;}x={b[(unsigned)n%9]};return x.value;}\n"
        "float fp_specialf(int n){const unsigned b[]={0,0x80000000u,0x7f800000u,0xff800000u,0x7fc00001u,0x7f800001u,1,0x80000001u,0x3fc00000u};"
        "union{unsigned bits;float value;}x={b[(unsigned)n%9]};return x.value;}\n"
        "unsigned long fp_bits(double d){union{double d;unsigned long u;}x={d};return x.u;}\n"
        "unsigned fp_bitsf(float f){union{float f;unsigned u;}x={f};return x.u;}\n"
        "int set_math_errno(int n){errno=n;return 0;}int get_math_errno(void){return errno;}\n"
        "int main(void) { for (int repeat=0;repeat<3;repeat++) for (int n=-3;n<=19;n++) {\n"
        "int a=tested(n), b=oracle_tested(n); if(a!=b) { fprintf(stderr,\"n=%d repeat=%d ours=%d oracle=%d\\n\",n,repeat,a,b); return 1; } }\n"
        "if(terminal_probe){int p=fork();if(p<0)return 2;if(!p){struct rlimit r={0,0};setrlimit(RLIMIT_CORE,&r);terminal_probe();_exit(91);}\n"
        "int s=0;if(waitpid(p,&s,0)!=p||!WIFSIGNALED(s)||WTERMSIG(s)!=SIGILL)return 3;}return 0;}\n";
    char directory[] = "/tmp/buster-check-program-XXXXXX";
    const bool created = mkdtemp(directory) != 0;
    char object[160];
    char oracle[160];
    char driver[160];
    char executable[160];
    char out[160];
    char err[160];
    snprintf(object, sizeof(object), "%s/checked.o", directory);
    snprintf(oracle, sizeof(oracle), "%s/oracle.c", directory);
    snprintf(driver, sizeof(driver), "%s/driver.c", directory);
    snprintf(executable, sizeof(executable), "%s/run", directory);
    snprintf(out, sizeof(out), "%s/out", directory);
    snprintf(err, sizeof(err), "%s/err", directory);
    bool valid = created && check_test_program_object(link, object, scratch) && check_test_program_oracle(link, source, oracle) &&
                 parse_test_write(driver, harness, sizeof(harness) - 1);
    char* compile[] = {
        (char*)clang,
        "-std=gnu17",
        "-O0",
        "-march=x86-64",
        "-fno-common",
        "-funsigned-char",
        "-Wno-unused-value",
        "-no-pie",
        driver,
        oracle,
        object,
        "-lm",
        "-o",
        executable,
        0,
    };
    char* run[] = {
        executable,
        0,
    };
    valid = valid && object_test_run(compile, out, err) == 0 && object_test_run(run, out, err) == 0;
    if (!valid && created)
    {
        fprintf(stderr, "test-checker: program oracle files retained in %s\n", directory);
        FILE* diagnostics = fopen(err, "rb");
        if (diagnostics != 0)
        {
            char line[256];
            while (fgets(line, sizeof(line), diagnostics) != 0)
            {
                fputs(line, stderr);
            }
            fclose(diagnostics);
        }
    }
    if (valid && created)
    {
        unlink(object);
        unlink(oracle);
        unlink(driver);
        unlink(executable);
        unlink(out);
        unlink(err);
        rmdir(directory);
    }
    return valid;
}

static void check_test_program_source(CheckTest* test, const char* source, const char* name, const char* clang)
{
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    const PPOptions pp_options = {
        .language = { .standard = 2017, .gnu = true },
        .no_standard_includes = true,
    };
    bool preprocessed = intern_initialize(&identifiers) && pp_initialize(&pp, &pp_options, &identifiers);
    preprocessed = preprocessed && pp_run_source(&pp, name, (const uint8_t*)source, (uint32_t)strlen(source));
    check_test_expect(test, preprocessed, "executable source preprocessing");
    for (uint32_t path = 0; preprocessed && path < 2; path += 1)
    {
        CheckTestTypes types = { 0 };
        Arena input_arena = arena_reserve(UINT64_C(1) << 26);
        Arena output = arena_reserve(UINT64_C(1) << 26);
        Arena bytes = arena_reserve(UINT64_C(1) << 22);
        Arena scratch = arena_reserve(UINT64_C(1) << 26);
        const ParseOptions parse_options = { .scalar = path == 0 };
        const CheckOptions options = { .scalar = path == 0 };
        bool ready = input_arena.base != 0 && output.base != 0 && bytes.base != 0 && scratch.base != 0 && check_test_types_open(&types) &&
                     parse_initialize(&parser, &parse_options, pp_options.language) && parse_translation_unit(&parser, &pp) && parser.errors == 0;
        CheckInput input = { 0 };
        CheckStream stream = { 0 };
        CheckInitializers initializers = { 0 };
        CheckAsms asms = { 0 };
        CheckDataPrepared data = { 0 };
        CheckProgram program = { 0 };
        ready = ready && check_input(&input, &parser, &pp, &types.table, &input_arena, &scratch) &&
                check_graph_run(&types.table, &input.graph, &input_arena, &scratch, &options, &stream) && stream.report_count == 0 &&
                check_initializers_run(&initializers, &input, &stream, &parser, &types.table, &input_arena, &scratch) &&
                check_asms_run(&asms, &input, &stream, &types.table, &input_arena, &scratch) &&
                check_data_prepare(&data, &input, &stream, &initializers, &types.table, &input_arena, &scratch);
        const CheckProgramOptions program_options = {
            .initializers = &initializers,
            .asms = &asms,
            .identifiers = &identifiers,
            .initializer_references = data.references,
            .initializer_reference_count = data.reference_count,
            .scalar = path == 0,
        };
        ready = ready && check_program(&program, &parser, &input, &stream, &types.table, program_options, &input_arena, &scratch);
        char description[256];
        snprintf(description, sizeof(description), "%s: %s source produces checked executable control and static-data plans", name, path == 0 ? "scalar" : "vector");
        check_test_expect(test, ready, description);
        if (!ready)
        {
            fprintf(stderr, "test-checker: program source input=%u stream=%u init=%u data=%u program=%u token=%u\n",
                    input.report_count, stream.report_count, initializers.status, data.status, program.status, program.token);
        }
        CheckProgramLowered lower[2] = { 0 };
        bool lowered = ready && check_program_lower(lower, &program, true, &output, &scratch) && check_program_lower(lower + 1, &program, false, &output, &scratch);
        snprintf(description, sizeof(description), "%s: both unit ABI batches lower to valid Tier0 functions", name);
        check_test_expect(test, lowered, description);
        if (ready && !lowered)
        {
            fprintf(stderr, "test-checker: program lower status=%u/%u token=%u/%u backend=%u/%u\n", lower[0].status, lower[1].status,
                    lower[0].token, lower[1].token, lower[0].backend_node, lower[1].backend_node);
        }
        if (lowered)
        {
            check_test_expect(test, lower[0].errno_symbol == lower[1].errno_symbol && lower[0].errno_pointer_type == lower[1].errno_pointer_type &&
                                   lower[0].runtime_symbol_count == lower[1].runtime_symbol_count,
                              "runtime accessor identity and genuine pointer ABI agree across unit batches");
            if (lower[0].errno_symbol != 0)
            {
                const bool fresh = lower[0].runtime_symbol_count == 1;
                check_test_expect(test, lower[0].runtime_symbol_count <= 1 &&
                                       type_kind(program.types, lower[0].errno_pointer_type) == BUSTER_TYPE_KIND_POINTER &&
                                       type_element(program.types, lower[0].errno_pointer_type) == BUSTER_TYPE_INT &&
                                       (fresh ? lower[0].errno_symbol == program.symbol_count + lower[0].helpers.count + 1 :
                                                lower[0].errno_symbol <= program.symbol_count),
                                  "errno accessor uses one reserved runtime symbol or an existing compatible global");
            }
        }
        for (uint32_t b = 0; lowered && b < program.body_count; b += 1)
        {
            check_test_expect(test, check_test_program_function(lower[0].functions + b, lower[1].functions + b),
                              "complete typed function and ABI locations agree across unit batches");
        }
        CheckProgramCode code = { 0 };
        CheckDataResult emission = { 0 };
        const bool emitted = lowered && check_program_emit(&code, &program, &parser, &identifiers, data.capacity, path == 0, &output, &bytes, &scratch, NULL, true) &&
                             check_data_publish(&data, &types.table, &stream, program.object_symbols, program.object_count, &code.link, 0, &bytes, &scratch, &emission);
        snprintf(description, sizeof(description), "%s: shared code/data Link emits through real ABI/Tier0/encoder consumers", name);
        check_test_expect(test, emitted, description);
        if (lowered && !emitted)
        {
            fprintf(stderr, "test-checker: program emission status=%u token=%u backend=%u node=%u data=%u\n", code.status, code.token, code.backend_error, code.backend_node, emission.status);
        }
        if (emitted)
        {
            snprintf(description, sizeof(description), "%s: generated executable agrees with %s on all repeated inputs", name, clang);
            check_test_expect(test, check_test_program_execute(&code.link, source, clang, &scratch), description);
        }
        check_test_expect(test, scratch.length == 0, "program preparation and all consumers restore scratch lifetime");
        parse_release(&parser);
        check_test_types_close(&types);
        arena_release(&scratch);
        arena_release(&bytes);
        arena_release(&output);
        arena_release(&input_arena);
    }
    pp_release(&pp);
    intern_release(&identifiers);
}

static void check_test_program_boundary(CheckTest* test, const char* source, const char* name, bool accepted, bool machine, uint32_t references, uint32_t initializations)
{
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    const PPOptions pp_options = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true };
    bool preprocessed = intern_initialize(&identifiers) && pp_initialize(&pp, &pp_options, &identifiers) &&
                        pp_run_source(&pp, name, (const uint8_t*)source, (uint32_t)strlen(source));
    check_test_expect(test, preprocessed, "program boundary source preprocessing");
    for (uint32_t path = 0; preprocessed && path < 2; path += 1)
    {
        CheckTestTypes types = { 0 };
        Arena input_arena = arena_reserve(UINT64_C(1) << 26);
        Arena output = arena_reserve(UINT64_C(1) << 26);
        Arena scratch = arena_reserve(UINT64_C(1) << 26);
        const ParseOptions parsing = { .scalar = path == 0 };
        const CheckOptions checking = { .scalar = path == 0 };
        CheckInput input = { 0 };
        CheckStream stream = { 0 };
        CheckInitializers plans = { 0 };
        CheckAsms asms = { 0 };
        bool ready = input_arena.base != 0 && output.base != 0 && scratch.base != 0 && check_test_types_open(&types) &&
                     parse_initialize(&parser, &parsing, pp_options.language) && parse_translation_unit(&parser, &pp) && parser.errors == 0 &&
                     check_input(&input, &parser, &pp, &types.table, &input_arena, &scratch) &&
                     check_graph_run(&types.table, &input.graph, &input_arena, &scratch, &checking, &stream) && stream.report_count == 0 &&
                     check_initializers_run(&plans, &input, &stream, &parser, &types.table, &input_arena, &scratch) &&
                     check_asms_run(&asms, &input, &stream, &types.table, &input_arena, &scratch);
        char description[256];
        snprintf(description, sizeof(description), "%s: source and typed expression/initializer input is ready", name);
        check_test_expect(test, ready, description);
        if (!ready)
        {
            fprintf(stderr, "test-checker: %s input=%u stream=%u plans=%u token=%u\n", name, input.report_count, stream.report_count, plans.status, plans.token);
        }
        if (ready)
        {
            const CheckProgramOptions options = { .initializers = &plans, .asms = &asms, .identifiers = &identifiers, .scalar = path == 0 };
            CheckProgram program = { 0 };
            const bool built = check_program(&program, &parser, &input, &stream, &types.table, options, &output, &scratch);
            snprintf(description, sizeof(description), "%s: checked control acceptance matches the independent C constraint", name);
            check_test_expect(test, built == accepted && program.status == (accepted ? CHECK_PROGRAM_OK : CHECK_PROGRAM_INVALID), description);
            if (built)
            {
                uint32_t writes = 0;
                for (uint32_t i = 0; i < program.control_count; i += 1)
                {
                    writes += program.controls[i].kind == CHECK_CONTROL_INITIALIZE;
                }
                check_test_expect(test, program.reference_count == references && writes == initializations,
                                  "checked body preserves exact live reference and ordered initializer counts");
                CheckProgramLowered lowered = { 0 };
                const uint64_t mark = output.length;
                const CheckControl* controls = program.controls;
                const bool lowered_ok = check_program_lower(&lowered, &program, path == 0, &output, &scratch);
                snprintf(description, sizeof(description), "%s: typed program support is independent of current Tier0 machine classes", name);
                check_test_expect(test, lowered_ok == machine && lowered.status == (machine ? CHECK_PROGRAM_OK : CHECK_PROGRAM_UNSUPPORTED), description);
                if (!lowered_ok)
                {
                    check_test_expect(test, output.length == mark && lowered.functions == 0 && lowered.count == 0 && program.controls == controls && program.status == CHECK_PROGRAM_OK,
                                      "machine refusal rolls back only its output and retains the complete checked program");
                }
                if (machine && program.body_count != 0)
                {
                    Arena empty = arena_reserve(4096);
                    empty.length = 17;
                    empty.capacity = 17;
                    CheckProgram failed = { 0 };
                    const bool failed_ok = check_program(&failed, &parser, &input, &stream, &types.table, options, &empty, &scratch);
                    check_test_expect(test, !failed_ok && failed.status == CHECK_PROGRAM_RESOURCE && failed.controls == 0 && failed.bodies == 0 &&
                                           empty.length == 17 && scratch.length == 0, "program output exhaustion is atomic and preserves live prefixes");
                    CheckProgramLowered failed_lower = { 0 };
                    const bool failed_lower_ok = check_program_lower(&failed_lower, &program, path == 0, &empty, &scratch);
                    check_test_expect(test, !failed_lower_ok && failed_lower.status == CHECK_PROGRAM_RESOURCE && failed_lower.functions == 0 &&
                                           empty.length == 17 && scratch.length == 0, "unit ABI/lowering exhaustion is atomic");
                    CheckProgramCode failed_code = { 0 };
                    const uint64_t emission_mark = output.length;
                    const bool failed_code_ok = check_program_emit(&failed_code, &program, &parser, &identifiers, (LinkCapacity){ 0 }, path == 0,
                                                                   &output, &empty, &scratch, NULL, true);
                    check_test_expect(test, !failed_code_ok && failed_code.status == CHECK_PROGRAM_RESOURCE && failed_code.link.symbols.count == 0 &&
                                           failed_code.lowered.functions == 0 && output.length == emission_mark && empty.length == 17 && scratch.length == 0,
                                      "code byte exhaustion rolls back Link, ABI, atoms and bytes together");
                    empty.capacity = 4096;
                    arena_release(&empty);
                }
            }
            else
            {
                check_test_expect(test, output.length == 0 && program.controls == 0 && program.cases == 0 && program.bodies == 0,
                                  "invalid control constraints publish no partial program");
            }
        }
        check_test_expect(test, scratch.length == 0, "program boundary checks restore scratch");
        parse_release(&parser);
        check_test_types_close(&types);
        arena_release(&scratch);
        arena_release(&output);
        arena_release(&input_arena);
    }
    pp_release(&pp);
    intern_release(&identifiers);
}

#include "c_check_program_select_test.c"
#include "c_check_program_query_test.c"
#include "c_check_region_input_test.c"

static __attribute__((noinline)) void check_test_programs(CheckTest* test, const char* clang)
{
    check_test_program_selection(test, clang);
    check_test_program_queries(test, clang);
    check_test_program_region_proofs(test, clang);
    check_test_program_helpers(test);
    check_test_program_diagnostics(test, clang);
    check_test_program_source(test,
        "int tested(int n){int x=0,y=1;if(n){x=2;}else x=3;while(n){if(x)break;n--;}do x++;while(x<4);"
        "for(int i=0;i<n;i++){if(i==1)continue;x+=i;}return x+y;}", "structured-loops", clang);
    check_test_program_source(test,
        "int tested(int n){int x=2;int y=(n>0&&++x);int z=(n<0||++x);int q=n?x++:++x;"
        "return x*1000+y*100+z*10+q;}", "short-circuit-updates", clang);
    check_test_program_source(test,
        "int helper(int a,int b,int c,int d,int e,int f,int g,int h){return a+b*2+c*3+d*4+e*5+f*6+g*7+h*8;}"
        "int tested(int n){return helper(n,1,2,3,4,5,6,7)+helper(1,2,3,4,5,6,7,n);}", "stack-argument-calls", clang);
    check_test_program_source(test,
        "double helper(double a,float b,int c){return a*2.0+b+c;}"
        "int tested(int n){double d=helper(n+0.25,1.5f,n);return (d?7:3)+(int)d;}", "floating-call-conversions", clang);
    check_test_program_source(test,
        "int snprintf(char*,unsigned long,const char*,...);char text[64];int tested(int n){"
        "int length=snprintf(text,64,\"%d %.1f\",n,(float)n+0.5f);return length*100+text[0];}", "external-variadic-call-abi", clang);
    check_test_program_source(test,
        "double helper(double a,double b,double c,double d,double e,double f,double g,double h,double i,double j)"
        "{return a+b+c+d+e+f+g+h+i+j;}int tested(int n){return (int)helper(n,1,2,3,4,5,6,7,8,9);}", "floating-stack-arguments", clang);
    check_test_program_source(test,
        "int tested(int n){int x=n;int*p=&x;int a=(*p)++;int b=++*p;*p+=3;return a+2*b+4*x;}", "addressed-local-updates", clang);
    check_test_program_source(test,
        "int tested(int n){signed char a=(signed char)(n+125);unsigned char b=(unsigned char)(n+250);"
        "_Bool c=(_Bool)(n*256);return (int)a+(int)b*3+c*700;}", "narrow-values-and-bool", clang);
    check_test_program_source(test,
        "int global=7;int helper(int x){global+=x;return global;}"
        "int tested(int n){int a=helper(n);global+=2;return a*3+global;}", "shared-code-and-data", clang);
    check_test_program_source(test,
        "extern int external_data;int external_helper(int);"
        "int tested(int n){return external_helper(n)+external_data;}", "external-code-and-data", clang);
    check_test_program_source(test,
        "_Thread_local int saved=9;int tested(int n){return n+1;}", "unreferenced-tls-definition", clang);
    check_test_program_source(test,
        "int missing(void);int tested(int n){return (0&&missing())+(1? n+2:missing())+(int)sizeof(missing());}", "unevaluated-references", clang);
    check_test_program_source(test,
        "int tested(int n){void*p=(void*)100;p++;p+=3;return (char*)p-(char*)100+n;}", "gnu-byte-pointer-stride", clang);
    check_test_program_source(test,
        "int tested(int n){int x=7;switch(n++){case -2 ... 0:x=1;break;case 1:x=2;case 2:x+=4;break;default:x=9;}return x*10+n;}",
        "switch-range-and-fallthrough", clang);
    check_test_program_source(test,
        "int tested(int n){int x=1;switch(n){case 3 ... 1:x=7;break;case 2:x=5;break;default:x=9;}return x;}",
        "empty-gnu-case-range-is-warning-only", clang);
    check_test_program_source(test,
        "int tested(int n){int x=0;for(int i=0;i<n;i++){switch(i%4){case 0:continue;case 1:x+=3;break;default:switch(i%2){case 0:x++;break;default:x+=7;}}x+=2;}return x;}",
        "nested-switch-continue", clang);
    check_test_program_source(test,
        "int tested(int n){int x=0;if(n<0)n=-n;again:x+=n;if(--n>0)goto again;goto done;x=999;done:return x;}", "forward-and-backward-goto", clang);
    check_test_program_source(test,
        "int helper(int n){return n+2;}int(*saved)(int)=helper;int tested(int n){return (saved==helper)+n;}", "function-address-data-reference", clang);
    check_test_program_source(test,
        "char*text=\"abc\";int tested(int n){return text[1]+n;}", "body-used-string-object", clang);
    check_test_program_source(test,
        "int tested(int n){int x=0;int y=n?(int){++x}:(int){x+=3};return x*10+y+(int)sizeof((int){++x});}", "scalar-compound-evaluation", clang);
    check_test_program_source(test,
        "int values[4]={3,7,11,19};struct S{int x;int*p;};struct S saved={5,values};"
        "int tested(volatile int n){struct S*p=&saved;return p->x+saved.p[(unsigned)n%4]+values[2];}", "aggregate-data-body-addresses", clang);
    // Frame objects: local arrays, structs and unions (#145's c-testsuite programs). dirty() first leaves nonzero bytes
    // where inner's frame goes, so that an initializer's implicit zeros must be written.
    check_test_program_source(test,
        "int tested(int n){int a[8];for(int i=0;i<8;i++)a[i]=i*n;short s[3]={1,2};char c[5]=\"ab\";int m[2][3]={{1},{4,5,6}};"
        "return a[n&7]+s[2]+s[1]*10+c[1]+c[3]*7+(int)sizeof a+m[0][1]+m[1][2]*3;}", "local-arrays", clang);
    check_test_program_source(test,
        "struct P{int x;long y;char z[3];struct{short h;}w;};int dirty(int n){volatile char b[512];for(int i=0;i<512;i++)b[i]=(char)(i|n|1);return b[n&511];}"
        "int inner(int n){struct P p={.y=n};struct P q={n,2,{3},{4}};char big[300]={'a'};int r[40]={[20]=n};"
        "return p.x+(int)p.y+p.z[2]+p.w.h+q.z[0]*10+q.z[1]+q.w.h+(int)q.y+big[299]+big[0]+r[19]+r[20]+r[39];}"
        "int tested(int n){return dirty(n)*0+inner(n);}", "local-initializer-zeros", clang);
    check_test_program_source(test,
        "struct A{int v[3];};struct B{unsigned char c[300];};union U{int i;unsigned char c[6];};"
        "int tested(int n){struct A a={{n,2,3}},b;b=a;a.v[0]=9;struct B x,y;for(int i=0;i<300;i++)x.c[i]=(char)(i+n);y=x;"
        "union U u,w;u.i=n;w=u;struct A c=b;struct A d;int v=(d=c).v[1];return b.v[0]+b.v[2]+a.v[0]+y.c[299]+y.c[n&255]+w.c[0]+c.v[0]+d.v[2]+v;}",
        "aggregate-assignment-copies", clang);
    check_test_program_source(test,
        "struct S{int a,b;};struct T{struct S s;int c;};int sum(struct S*q){return q->a*10+q->b;}"
        "int tested(int n){int t=0;for(int i=0;i<3;i++)t+=sum(&(struct S){i,n});struct S s={n,2};struct T u={s,3};struct S arr[3]={{1,2},{3}};"
        "struct S z=n&1?s:arr[0];struct S e=({struct S w={n,n+1};w;});"
        "return t+((int[]){5,6,7})[n&1]+u.s.a*7+u.c+arr[1].a+arr[1].b+arr[2].a+z.a+e.b;}", "aggregate-values-and-compounds", clang);
    check_test_program_source(test,
        "int add(int a,int b){return a+b;}int mul(int a,int b){return a*b;}int(*pick(int n))(int,int){return n&1?add:mul;}"
        "int apply(int(*f)(int,int),int x){return (*f)(x,x);}"
        "int tested(int n){int(*f)(int,int)=n<0?add:mul;int(*t[2])(int,int)={add,mul};"
        "return f(n,3)+(*t[n&1])(n,4)+pick(n)(2,n)+apply(t[0],n)+apply(&mul,2);}", "indirect-calls", clang);
    check_test_program_source(test,
        "int tested(int n){char c=(char)n;unsigned char u=(unsigned char)(n*16);short s=(short)(n*4096);_Bool b=c;_Bool d=s;"
        "return !c*1000+!u*100+!s*10+b+d*2+(c&&u)*4+(s||u)*8+(c?16:0);}", "narrow-truth-values", clang);
    check_test_program_source(test,
        "int tested(int n){unsigned x=(unsigned)n|1;return __builtin_popcount(x)+__builtin_clz(x)+__builtin_ctz(x)+"
        "__builtin_parity(x)+__builtin_ffs((int)x)+(int)(__builtin_bswap16((unsigned short)x)%101);}", "integer-builtin-runtime", clang);
    check_test_program_source(test,
        "int tested(int n){int x=0;int a=__builtin_add_overflow(n,2147483647,&x);unsigned char y=0;"
        "int b=__builtin_mul_overflow((long)n,111ULL,&y);return a*1000+b*100+y+(int)((unsigned)x%97);}", "overflow-runtime-widths", clang);
    check_test_program_source(test,
        "int saved;int visits;int*slot(void){visits++;return &saved;}int tested(int n){visits=0;"
        "int a=__builtin_sub_overflow(n,7,slot());int b=__builtin_add_overflow(7,8,&saved);return visits*100+a*10+b+saved;}",
        "overflow-destination-once-and-folded-store", clang);
    check_test_program_source(test,
        "int tested(int n){int x=9;int a=0&&__builtin_add_overflow(1,2,(int*)0);"
        "int b=n?__builtin_add_overflow(n,7,&x):__builtin_mul_overflow(3,4,&x);"
        "return a+b+x+(int)sizeof(__builtin_add_overflow(7,8,&x));}", "overflow-selected-effects", clang);
    check_test_program_source(test,
        "int tested(int n){n+=10;typedef int A[n++];int x=n;n+=3;return (int)sizeof(A)+(int)sizeof(A)+n+x;}",
        "vla-typedef-captures-bound-once", clang);
    check_test_program_source(test,
        "int tested(int n){n+=10;int(*p)[++n],x=(n*=2),(*q)[++n];return (int)sizeof(*p)+(int)sizeof(*q)+n+x;}",
        "vla-declarator-and-initializer-order", clang);
    check_test_program_source(test,
        "int size(int n,int(*p)[n]){n+=7;return (int)sizeof(*p)+n;}int tested(int n){return size(n+10,0);}",
        "vla-parameter-bound-capture", clang);
    check_test_program_source(test,
        "int tested(int n){n+=10;int x=sizeof(int[n++]);return x+(int)sizeof(int[n++])+n;}",
        "vla-inline-typename-captures", clang);
    check_test_program_source(test,
        "int tested(int n){int sum=0;for(int i=1;i<4;i++){typedef int A[n+10+i];sum+=sizeof(A);sum+=sizeof(A);}return sum;}",
        "vla-loop-lifetime-recaptures", clang);
    check_test_program_source(test,
        "char a[24]=\"abcdefghijklmnop\";char b[24];int tested(int n){unsigned k=(unsigned)n%12;"
        "__builtin_memset(b,0,24);__builtin_memcpy(b,a,k);__builtin_memmove(b+2,b,k);"
        "return (int)__builtin_strlen(b)+(__builtin_memcmp(a,b,k)>0)-(__builtin_memcmp(a,b,k)<0)+b[2];}",
        "memory-builtin-runtime-loops", clang);
    check_test_program_source(test,
        "int tested(int n){int x=0;__builtin_memset((void*)0,7,0);__builtin_memcpy((void*)0,(void*)0,0);"
        "long y=__builtin_expect((x++,n),(x++,1));__builtin_prefetch(&x);"
        "return (int)y+x+__builtin_constant_p(n++);}", "builtin-hints-and-unevaluated-query", clang);
    check_test_program_source(test,
        "int tested(int n){double x=n<0?__builtin_nan(\"\"):n==0?0.0:n==1?__builtin_inf():n==2?0x1p-1074:(double)n;"
        "return __builtin_isnan(x)*100+__builtin_isinf(x)*10+__builtin_isfinite(x)+__builtin_signbit(-x)*2;}",
        "floating-builtin-runtime-classification", clang);
    check_test_program_source(test,
        "int seen;int mark(int n){seen=seen*10+n;return n;}int tested(int n){seen=0;"
        "double x=n<0?__builtin_nan(\"\"):n==0?0.0:n==1?__builtin_inf():n==2?0x1p-1074:(double)n;"
        "int r=__builtin_fpclassify(mark(1),mark(2),mark(3),mark(4),mark(5),x);return seen*10+r;}",
        "fpclassify-conditional-selector-effects", clang);
    check_test_program_source(test,
        "int seen;int mark(int n){seen=seen*10+n;return n;}int tested(int n){seen=0;"
        "int r=__builtin_fpclassify(mark(1),mark(2),mark(3),mark(4),mark(5),(seen++,1.0));return seen*10+r+n;}",
        "fpclassify-known-category-effects", clang);
    check_test_program_source(test,
        "int calls;double make(int n){calls++;return n<0?-__builtin_inf():n==0?-0.0:(double)(-n)-0.25;}int tested(int n){calls=0;"
        "double d=__builtin_fabs(make(n));float f=__builtin_fabsf((float)n-7.5f);return __builtin_isinf_sign(make(n))*1000+"
        "(__builtin_isinf(d)?31:(int)(d*4))+(int)(f*2)+calls*100;}", "absolute-and-signed-infinity-runtime", clang);
    check_test_program_source(test,
        "int change_rounding(int);int tested(int n){change_rounding(n);int a=__builtin_flt_rounds();"
        "change_rounding(n+1);int b=__builtin_flt_rounds();change_rounding(0);return a*10+b;}", "floating-environment-helper-observes-each-call", clang);
    check_test_program_source(test,
        "#pragma STDC FENV_ACCESS ON\n"
        "int change_rounding(int);int clear_fp(void);int fp_state(void);int tested(int n){change_rounding(n);clear_fp();"
        "double x=1.0+0x1p-53;int a=x>1.0;int f=fp_state();change_rounding(0);return a*100+f;}",
        "fenv-source-constant-runtime-rounding", clang);
    check_test_program_source(test,
        "#pragma STDC FENV_ACCESS ON\n"
        "int change_rounding(int);int clear_fp(void);int fp_state(void);int tested(int n){change_rounding(n);clear_fp();"
        "1.0/3.0;int f=fp_state();clear_fp();int a=(1.0+0x1p-53>1.0)?7:3;"
        "int b=(1.0+0x1p-53>1.0)&&++a;int c=(1.0+0x1p-53>1.0)||++a;"
        "change_rounding(0);return f*1000+a*100+b*10+c;}", "fenv-discarded-effects-and-runtime-branches", clang);
    check_test_program_source(test,
        "#pragma STDC FENV_ACCESS ON\n"
        "int change_rounding(int);int clear_fp(void);int fp_state(void);int tested(int n){change_rounding(n);clear_fp();"
        "float x=1.0+0x1p-24;int a=x>1.0f;int f=fp_state();change_rounding(0);return a*100+f;}",
        "fenv-implicit-narrowing-conversion", clang);
    check_test_program_source(test,
        "#pragma STDC FENV_ACCESS ON\n"
        "int clear_fp(void);int fp_state(void);int change_daz(int);double fp_special(int);int tested(int n){"
        "int old=change_daz(n);double x=fp_special(n);clear_fp();int a=__builtin_fpclassify(11,22,33,44,55,x);int f=fp_state();"
        "clear_fp();int b=__builtin_isinf_sign(x);int g=fp_state();change_daz(old);return a*1000+f*100+b*10+g;}",
        "fenv-strict-classification-exceptions-and-daz", clang);
    check_test_program_source(test,
        "#pragma STDC FENV_ACCESS ON\n"
        "_Static_assert(__builtin_constant_p(1.0/3.0)==1,\"source proof\");"
        "int clear_fp(void);int fp_state(void);int tested(int n){clear_fp();"
        "int a=__builtin_constant_p(1.0/3.0);int f=fp_state();return (a+1)*100+f+n;}",
        "fenv-query-source-proof-and-runtime-value", clang);
    check_test_program_source(test,
        "#pragma STDC FENV_ACCESS ON\n"
        "int absent(void);int clear_fp(void);int fp_state(void);int tested(int n){clear_fp();"
        "int a=__builtin_constant_p(1.0/3.0)?absent():n;int f=fp_state();return a+f;}",
        "fenv-query-runtime-truth-keeps-effects-and-selected-references", clang);
    check_test_program_source(test,
        "#pragma STDC FENV_ACCESS ON\n"
        "int visits;double absent(void);double called(void){visits++;return 1.0/3.0;}int clear_fp(void);int fp_state(void);"
        "int tested(int n){int x=n;visits=0;clear_fp();int a=__builtin_constant_p((x++,1.0/3.0));"
        "int b=__builtin_constant_p(called()+1.0);int c=__builtin_constant_p(absent()+1.0);int f=fp_state();"
        "return a+b+c+x+visits*100+f*1000;}", "fenv-query-nonconstant-arguments-remain-unevaluated", clang);
    check_test_program_source(test,
        "#pragma STDC FENV_ACCESS ON\n"
        "int clear_fp(void);int fp_state(void);int tested(int n){clear_fp();int a=__builtin_constant_p(1.0/2.0);int f=fp_state();"
        "clear_fp();int b=__builtin_constant_p(1.0/3.0);int g=fp_state();clear_fp();int c=__builtin_constant_p(n+1.0/3.0);int h=fp_state();"
        "return a*100000+b*10000+c*1000+f*100+g*10+h+n;}", "fenv-query-exact-inexact-and-dynamic-operands", clang);
    check_test_program_source(test,
        "#pragma STDC FENV_ACCESS ON\n"
        "int visits;int next(void){visits++;return 7;}int clear_fp(void);int fp_state(void);int tested(int n){visits=0;clear_fp();"
        "int a=__builtin_constant_p(1?1.0/3.0:next());int b=__builtin_constant_p((0&&next())+1.0/3.0);"
        "int c=__builtin_constant_p(1?1.0/2.0:next());int d=__builtin_constant_p(1?1.0/3.0:n++);int f=fp_state();"
        "return a*100000+b*10000+c*1000+d*100+visits*10+f+n;}", "fenv-query-syntactic-effects-and-exact-selected-proof", clang);
    check_test_program_source(test,
        "#pragma STDC FENV_ACCESS ON\n"
        "int clear_fp(void);int fp_state(void);int tested(int n){clear_fp();int a=__builtin_constant_p(__builtin_constant_p(1.0/3.0));int f=fp_state();"
        "clear_fp();int b=__builtin_constant_p(__builtin_constant_p(1.0/3.0)+1);int g=fp_state();"
        "clear_fp();int c=__builtin_constant_p((n,1.0/2.0));int d=__builtin_constant_p(__imag__ n);int h=fp_state();"
        "return a*100000+b*10000+c*1000+d*100+f*10+g+h+n;}", "fenv-query-nested-runtime-proofs-comma-and-imaginary", clang);
    check_test_program_source(test,
        "#pragma STDC FENV_ACCESS ON\n"
        "int change_rounding(int);int clear_fp(void);int fp_state(void);int external_helper(int);extern int external_data;"
        "int tested(int n){static double fixed=1.0+0x1p-53;change_rounding(n);clear_fp();double a=1.0+0x1p-53;int f=fp_state();"
        "double off;change_rounding(0);{\n#pragma STDC FENV_ACCESS OFF\n off=1.0+0x1p-53;}change_rounding(n);double b=1.0+0x1p-53;"
        "int selected=(1.0+0x1p-53>1.0)?external_helper(n):external_data;change_rounding(0);"
        "return (a>fixed)*1000+(b>off)*100+f+selected;}", "fenv-static-proof-scoped-policy-and-live-references", clang);
    check_test_program_source(test,
        "int tested(int n){double d=(double)n+0.625;float f=(float)n-0.375f;"
        "return (int)__builtin_ceil(d)+(int)__builtin_floor(d)*3+(int)__builtin_trunc(d)*7+"
        "(int)__builtin_ceilf(f)*11+(int)__builtin_floorf(f)*13+(int)__builtin_truncf(f)*17;}",
        "math-fixed-rounding-raw-float-and-double-abi", clang);
    check_test_program_source(test,
        "double fp_special(int);float fp_specialf(int);unsigned long fp_bits(double);unsigned fp_bitsf(float);"
        "int tested(int n){double x=fp_special(n);float y=fp_specialf(n);"
        "return (int)(fp_bits(__builtin_copysign(x,(double)n))%997)+(int)(fp_bitsf(__builtin_copysignf(y,(float)n))%991);}",
        "math-copysign-preserves-raw-representation", clang);
    check_test_program_source(test,
        "#pragma STDC FENV_ACCESS ON\n"
        "double fp_special(int);float fp_specialf(int);int set_math_errno(int);int get_math_errno(void);int clear_fp(void);int fp_state(void);"
        "int tested(int n){double x=fp_special(n);float y=fp_specialf(n);set_math_errno(7);clear_fp();double a=__builtin_sqrt(x);"
        "int e=get_math_errno(),f=fp_state();set_math_errno(7);clear_fp();float b=__builtin_sqrtf(y);int g=get_math_errno(),h=fp_state();"
        "return e*100000+f*10000+g*100+h+__builtin_isnan(a)*3+__builtin_isinf(a)*5+__builtin_signbit(a)*7+"
        "__builtin_isnan(b)*11+__builtin_isinf(b)*13+__builtin_signbit(b)*17;}",
        "math-sqrt-real-errno-address-and-exceptions", clang);
    check_test_program_source(test,
        "#pragma STDC FENV_ACCESS ON\n"
        "int change_rounding(int);int clear_fp(void);int fp_state(void);int tested(int n){change_rounding(n);"
        "double d=(double)n+0.625;float f=(float)n-0.375f;clear_fp();double a=__builtin_nearbyint(d);int ea=fp_state();"
        "clear_fp();double b=__builtin_rint(d);int eb=fp_state();clear_fp();float c=__builtin_nearbyintf(f);int ec=fp_state();"
        "clear_fp();float e=__builtin_rintf(f);int ed=fp_state();change_rounding(0);"
        "return (int)a+(int)b*3+(int)c*7+(int)e*11+ea*1000+eb*100+ec*10000+ed*100000;}",
        "math-current-rounding-nearbyint-and-rint-effects", clang);
    check_test_program_source(test,
        "#pragma STDC FENV_ACCESS ON\n"
        "int change_rounding(int);int clear_fp(void);int fp_state(void);int tested(int n){change_rounding(n);clear_fp();"
        "float x=__builtin_copysignf(1.0+0x1p-24,-1.0);int a=x<-1.0f;int f=fp_state();change_rounding(0);return a*100+f;}",
        "math-copysign-runtime-prototype-narrowing", clang);
    check_test_program_source(test,
        "int*__errno_location(void);int tested(int n){*__errno_location()=7;double x=__builtin_sqrt((double)n);"
        "return *__errno_location()+(__builtin_isnan(x)?100:(int)x);}", "math-reuses-existing-errno-accessor", clang);
    check_test_program_source(test,
        "double sqrt(double x){return __builtin_sqrt(x);}double ceil(double x){return __builtin_ceil(x);}"
        "int tested(int n){return (int)(sqrt((double)(n+4))+ceil((double)n+0.25));}",
        "math-source-definitions-do-not-recurse-through-runtime-helpers", clang);
    check_test_program_source(test,
        "int visits;double next(int n){visits++;return (double)(n+4);}"
        "double mix(double a,int b,float c,double d){return a+b+c+d;}int tested(int n){visits=0;"
        "double a=mix(0.5,2,__builtin_copysignf((float)n,-1.0f),__builtin_sqrt(next(n)));"
        "double b=__builtin_sqrt(__builtin_sqrt((double)(n+4)*16.0));return (int)(a+b)+visits*100;}",
        "math-nested-accessor-calls-preserve-live-arguments", clang);
    check_test_program_source(test,
        "int tested(int n){unsigned long a=(unsigned long)n,b=3,out=0;int overflow=__builtin_umull_overflow(a++,b++,&out);"
        "return (int)(out%97)+overflow*100+(int)(a%13)+(int)b;}", "typed-overflow-prototype-conversions-and-effects", clang);
    check_test_program_source(test,
        "int absent(void);int tested(int n){unsigned long a=__builtin_object_size(\"abc\",0);"
        "unsigned long b=__builtin_object_size(\"abc\",2);int ignored=__builtin_object_size((n++,(char*)0),0)==~0UL;"
        "return n+(int)(a*10+b)+ignored+__builtin_constant_p(absent());}", "deferred-query-runtime-values-remain-unevaluated", clang);
    check_test_program_source(test,
        "signed char a;unsigned short b;int c;unsigned long d;int tested(int n){int order=0;"
        "__atomic_store_n(&a,n-120,order++);__atomic_store_n(&b,n+65530,0);__atomic_store_n(&c,n*7,5);__atomic_store_n(&d,(unsigned long)n+9,3);"
        "__atomic_thread_fence(5);__atomic_signal_fence(5);__sync_synchronize();"
        "return __atomic_load_n(&a,0)+__atomic_load_n(&b,2)+__atomic_load_n(&c,5)+(int)__atomic_load_n(&d,0)+order;}",
        "atomic-shared-helper-widths-orders-and-sign", clang);
    check_test_program_source(test,
        "void terminal_probe(void){__builtin_trap();}"
        "int tested(int n){if(n==999)__builtin_trap();if(n==998)return 7+(__builtin_unreachable(),1);return n+3;}",
        "terminal-helper-has-no-continuation", clang);
    check_test_program_source(test,
        "int visits;int next(int n){visits++;return n;}int tested(int n){visits=0;int x=n;"
        "double a=x++?:2.5;int b=next(n)?:next(7);int c=(visits++,3)?:next(9);return (int)(a*4)+b+c+x+visits*100;}",
        "gnu-omitted-middle-evaluates-once", clang);
    check_test_program_source(test,
        "int visits;int next(int n){visits++;return n;}int tested(int n){visits=0;volatile int x=n;"
        "int a=__imag__(next(n));int b=__imag__(x++);__real__ x+=3;return a+b+x+visits*100;}",
        "scalar-real-imaginary-preserve-evaluation", clang);
    check_test_program_source(test,
        "int g;int bump(void){g++;return 9;}int tested(int n){g=0;int x={n,bump()};return x+g;}",
        "ignored-excess-initializer-does-not-execute", clang);
    check_test_program_source(test,
        "int tested(int n){int x=0;int y=5+({int z=n;x++;z+x;});return x*100+y;}", "statement-region-value-and-effects", clang);
    check_test_program_source(test,
        "int helper(int a,int b,int c){return a*100+b*10+c;}int tested(int n){int x=0;"
        "int z=helper(n,({x++;n+2;}),({int y=n*2;y+3;}));return z+x;}", "statement-region-pending-call-values", clang);
    check_test_program_source(test,
        "int tested(int n){return 7+({if(n<0)return 17;int x=n;x+2;});}", "statement-region-return-escape", clang);
    check_test_program_source(test,
        "int tested(int n){int x=0;int y=7+({x++;if(n<0)goto done;x+2;});x+=y;done:return x+n;}", "statement-region-goto-escape", clang);
    check_test_program_source(test,
        "int tested(int n){int x=0;for(int i=0;i<n+6;i++){x+=7+({if(i==2)continue;if(i==5)break;i;});}return x;}",
        "statement-region-loop-escape", clang);
    // Clang 23 accepts this outer-loop context but its generated break targets the inner while (10/10/14).
    // GNU's specified outer-loop target and GCC give 4/4/6 for n=-1/0/1; keep this focused independent oracle.
    check_test_program_source(test,
        "int tested(int n){int x=0;for(int i=0;i<5;i++){while(({if(i==2)break;n>0;})){x++;break;}x+=2;}return x;}",
        "statement-region-loop-condition-escape-gcc", "gcc");
    check_test_program_source(test,
        "int tested(int n){int x=0;int y=3+({int z=4+({x++;n+1;});for(int i=0;i<3;i++)z+=i;z;});return x*100+y;}",
        "nested-statement-regions-and-loop", clang);
    check_test_program_source(test,
        "int tested(int n){int x=0;double z=0.5+({double y=n+0.25;x++;y;});return (int)(z*4)+x;}", "statement-region-floating-pending-value", clang);
    check_test_program_source(test,
        "int sum(int a,double b,int c,double d,int e){return a+(int)(b*2)+c+(int)(d*4)+e;}"
        "int tested(int n){return sum(n,n+0.5,n+1,n+0.25,({int x=n;x+3;}));}",
        "statement-region-mixed-pending-stack-classes", clang);
    check_test_program_source(test,
        "int tested(int n){int x=0;int r=4+({int y=3+({if(n<0)goto finish;n;});x=y;finish:x+=2;x+1;});return r+x;}",
        "statement-region-inner-goto-outer-region", clang);
    check_test_program_source(test,
        "int missing(void);int tested(int n){int x=0;int y=(n&&({x++;n+2;}))||({x+=2;0;});"
        "return x*100+y+(int)sizeof(({missing();n;}));}", "statement-region-selected-and-unevaluated", clang);
    check_test_program_source(test,
        "int tested(int n){int x=0;({int y=n;if(y)x++;});__typeof__(({x+=50;n;})) t=1;return x+t;}", "statement-region-void-and-typeof", clang);
    check_test_program_source(test,
        "int tested(int n){int x=n;__typeof__(({int y=++x;y;})) t=2;return x+t;}", "statement-region-typeof-local-declarations-unevaluated", clang);
    check_test_program_source(test,
        "int tested(int n){int x=0;({int y=n;if(y)x++;});return x;}", "statement-region-void-prefix", clang);
    check_test_program_source(test,
        "int missing(void);int tested(int n){int x=0;int y=__builtin_choose_expr(0,({missing();n;}),({x++;n+1;}));return x*100+y;}",
        "statement-region-static-selection", clang);
    {
        char source[4096];
        uint32_t length = 0;
        for (uint32_t i = 0; i < 33; i += 1)
        {
            length += (uint32_t)snprintf(source + length, sizeof(source) - length, "int global%u=%u;", i, i + 1);
        }
        length += (uint32_t)snprintf(source + length, sizeof(source) - length, "int tested(int n){return n");
        for (uint32_t i = 33; i-- > 0;)
        {
            length += (uint32_t)snprintf(source + length, sizeof(source) - length, "+global%u", i);
        }
        snprintf(source + length, sizeof(source) - length, ";}");
        check_test_program_source(test, source, "vector-reference-batch", clang);
    }
    for (uint32_t fixture = 0; fixture < 4; fixture += 1)
    {
        char source[1024];
        char name[64];
        const uint32_t multiplier = 2 + check_test_random(test) % 7;
        const uint32_t modulus = 3 + check_test_random(test) % 11;
        const uint32_t bias = 1 + check_test_random(test) % 31;
        snprintf(source, sizeof(source), "int tested(int n){int x=%u;for(int i=0;i<n+4;i++){if(i%%3==1)continue;x=(x*%u+i)%%%u;}return n?x+n:x-%u;}",
                 bias, multiplier, modulus, bias);
        snprintf(name, sizeof(name), "generated-control-%u", fixture);
        check_test_program_source(test, source, name, clang);
    }
    check_test_program_boundary(test, "", "empty-program", true, true, 0, 0);
    check_test_program_boundary(test, "int main(void){}", "implicit-main-return", true, true, 0, 0);
    check_test_program_boundary(test, "int tested(int n){int x=n+1;return x*2;}", "program-rollback", true, true, 0, 1);
    check_test_program_boundary(test, "int tested(int n){return (int)__builtin_sqrt(n+4);}", "math-runtime-symbol-rollback", true, true, 0, 0);
    check_test_program_boundary(test, "double __errno_location(void);int tested(int n){return (int)__builtin_sqrt(n+4);}",
                                "incompatible-errno-accessor-function", true, false, 0, 0);
    check_test_program_boundary(test, "extern int __errno_location;int tested(int n){return (int)__builtin_sqrt(n+4);}",
                                "incompatible-errno-accessor-object", true, false, 0, 0);
    check_test_program_boundary(test, "extern int __errno_location;int tested(int n){return n+(0&&__builtin_sqrt(-1.0));}",
                                "inactive-math-has-no-runtime-accessor", true, true, 0, 0);
    check_test_program_boundary(test, "__int128 wide(__int128 x){return x+1;}", "wide-primary-program", true, false, 0, 0);
    check_test_program_boundary(test, "extern int(*callback)(int);int tested(int n){return callback(n);}",
                                "indirect-function-pointer-call", true, true, 1, 0);
    check_test_program_boundary(test,
        "typedef union __attribute__((transparent_union)){int value;unsigned bits;} U;int consume(U);int tested(int n){return consume(n);}",
        "transparent-union-call-machine-boundary", true, false, 1, 0);
    check_test_program_boundary(test,
        "typedef int V __attribute__((vector_size(16)));extern V global;int tested(int n){return __builtin_reduce_add(global);}",
        "vector-builtin-live-reference-machine-boundary", true, false, 1, 0);
    check_test_program_boundary(test,
        "typedef int V __attribute__((vector_size(16)));extern V global;int tested(int n){return __builtin_reduce_add(__builtin_nondeterministic_value(global));}",
        "vector-builtin-unevaluated-type-argument", true, false, 0, 0);
    check_test_program_boundary(test,
        "typedef int V __attribute__((vector_size(16)));extern V global;int tested(int n){return __builtin_reduce_add(__builtin_shufflevector(global,global,0,1,2,3));}",
        "vector-builtin-shuffle-prefix-reference-mask", true, false, 1, 0);
    check_test_program_boundary(test, "int tested(int n){int values[n+10];return sizeof values;}", "dynamic-frame-machine-boundary", true, false, 0, 0);
    check_test_program_boundary(test, "int extent;int tested(int n){typedef int A[extent+10];return sizeof(A);}",
                                "vla-bound-reference", true, true, 1, 0);
    check_test_program_boundary(test, "int global;int tested(int n){asm volatile(\"\":\"=r\"(global):\"r\"(n):\"memory\");return global;}",
                                "typed-asm-operands-and-machine-boundary", true, false, 1, 0);
    check_test_program_boundary(test, "int tested(int n){asm goto(\"\"::::target);return n;target:return n+1;}",
                                "typed-asm-goto-target", true, false, 0, 0);
    check_test_program_boundary(test, "asm(\".byte 0\");int tested(int n){return n;}",
                                "file-assembly-machine-boundary", true, false, 0, 0);
    check_test_program_boundary(test,
        "extern int missing;int tested(int n,...){__builtin_va_list ap;__builtin_va_start(ap,(missing++,n));"
        "int x=__builtin_va_arg(ap,int);__builtin_va_end(ap);return x;}", "varargs-unevaluated-final-parameter", true, false, 0, 1);
    check_test_program_boundary(test, "_Thread_local int g;int tested(int n){return g+n;}", "executable-tls-boundary", true, false, 1, 0);
    check_test_program_boundary(test, "int tested(int n){_Alignas(32) int x=n;return x;}", "automatic-alignment-boundary", true, false, 0, 1);
    check_test_program_boundary(test, "int global;struct S{int x;int*p;};int tested(int n){struct S s={n,&global};return s.x;}",
                                "aggregate-primary-initialization", true, true, 1, 1);
    check_test_program_boundary(test, "struct S{int x;};struct S make(int n);int tested(int n){return make(n).x;}",
                                "aggregate-result-machine-boundary", true, false, 1, 0);
    check_test_program_boundary(test, "struct S{int x;};int take(struct S);int tested(int n){struct S s={n};return take(s);}",
                                "aggregate-argument-machine-boundary", true, false, 1, 1);
    check_test_program_boundary(test, "struct B{unsigned x:3;};int tested(int n){struct B b;b.x=n;return b.x;}",
                                "bit-field-machine-boundary", true, false, 0, 0);
    check_test_program_boundary(test, "int tested(unsigned n){switch(n){case -1:return 1;case 4294967295U:return 2;}return 0;}",
                                "case-conversion-collision", false, false, 0, 0);
    check_test_program_boundary(test, "int tested(int n){switch(n){case 1 ... 5:return 1;case 4 ... 7:return 2;}return 0;}",
                                "overlapping-case-ranges", false, false, 0, 0);
    check_test_program_boundary(test, "int tested(int n){switch(n){default:return 1;default:return 2;}}", "duplicate-default", false, false, 0, 0);
    check_test_program_boundary(test, "int tested(int n){switch((double)n){case 1:return 1;}return 0;}", "noninteger-switch", false, false, 0, 0);
}

#endif
