// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Real C source -> checked bodies -> unit ABI -> Tier0 -> shared code/data Link -> ELF. The independent Clang
// build of the same source is linked beside our object and executed on the same bounded inputs. Both source and
// backend schedules are exercised, and full Tier0 columns (including ABI locations) agree before code emission.
// Include after c_checker_test.c's shared helpers and c_check_program.c.
#ifndef BUSTER_C_CHECK_PROGRAM_TEST_INCLUDED
#define BUSTER_C_CHECK_PROGRAM_TEST_INCLUDED

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
        "int tested(int); int oracle_tested(int);\n"
        "int external_data=17; int external_helper(int n){return n*3;}\n"
        "int main(void) { for (int repeat=0;repeat<3;repeat++) for (int n=-3;n<=19;n++) {\n"
        "int a=tested(n), b=oracle_tested(n); if(a!=b) { fprintf(stderr,\"n=%d repeat=%d ours=%d oracle=%d\\n\",n,repeat,a,b); return 1; } } return 0; }\n";
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
        CheckDataPrepared data = { 0 };
        CheckProgram program = { 0 };
        ready = ready && check_input(&input, &parser, &pp, &types.table, &input_arena, &scratch) &&
                check_graph_run(&types.table, &input.graph, &input_arena, &scratch, &options, &stream) && stream.report_count == 0 &&
                check_initializers_run(&initializers, &input, &stream, &parser, &types.table, &input_arena, &scratch) &&
                check_data_prepare(&data, &input, &stream, &initializers, &types.table, &input_arena, &scratch);
        const CheckProgramOptions program_options = {
            .initializers = &initializers,
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
        for (uint32_t b = 0; lowered && b < program.body_count; b += 1)
        {
            check_test_expect(test, check_test_program_function(lower[0].functions + b, lower[1].functions + b),
                              "complete typed function and ABI locations agree across unit batches");
        }
        CheckProgramCode code = { 0 };
        CheckDataResult emission = { 0 };
        const bool emitted = lowered && check_program_emit(&code, &program, &parser, &identifiers, data.capacity, path == 0, &output, &bytes, &scratch) &&
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
        bool ready = input_arena.base != 0 && output.base != 0 && scratch.base != 0 && check_test_types_open(&types) &&
                     parse_initialize(&parser, &parsing, pp_options.language) && parse_translation_unit(&parser, &pp) && parser.errors == 0 &&
                     check_input(&input, &parser, &pp, &types.table, &input_arena, &scratch) &&
                     check_graph_run(&types.table, &input.graph, &input_arena, &scratch, &checking, &stream) && stream.report_count == 0 &&
                     check_initializers_run(&plans, &input, &stream, &parser, &types.table, &input_arena, &scratch);
        char description[256];
        snprintf(description, sizeof(description), "%s: source and typed expression/initializer input is ready", name);
        check_test_expect(test, ready, description);
        if (!ready)
        {
            fprintf(stderr, "test-checker: %s input=%u stream=%u plans=%u token=%u\n", name, input.report_count, stream.report_count, plans.status, plans.token);
        }
        if (ready)
        {
            const CheckProgramOptions options = { .initializers = &plans, .identifiers = &identifiers, .scalar = path == 0 };
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
                                                                   &output, &empty, &scratch);
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

static void check_test_programs(CheckTest* test, const char* clang)
{
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
        "int tested(int n){int x=0;int r=4+({int y=3+({if(n<0)goto finish;n;});x=y;finish:x+=2;x+1;});return r+x;}",
        "statement-region-inner-goto-outer-region", clang);
    check_test_program_source(test,
        "int missing(void);int tested(int n){int x=0;int y=(n&&({x++;n+2;}))||({x+=2;0;});"
        "return x*100+y+(int)sizeof(({missing();n;}));}", "statement-region-selected-and-unevaluated", clang);
    check_test_program_source(test,
        "int tested(int n){int x=0;({int y=n;if(y)x++;});__typeof__(({x+=50;n;})) t=1;return x+t;}", "statement-region-void-and-typeof", clang);
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
    check_test_program_boundary(test, "__int128 wide(__int128 x){return x+1;}", "wide-primary-program", true, false, 0, 0);
    check_test_program_boundary(test,
        "extern int missing;int tested(int n,...){__builtin_va_list ap;__builtin_va_start(ap,(missing++,n));"
        "int x=__builtin_va_arg(ap,int);__builtin_va_end(ap);return x;}", "varargs-unevaluated-final-parameter", true, false, 0, 1);
    check_test_program_boundary(test, "_Thread_local int g;int tested(int n){return g+n;}", "executable-tls-boundary", true, false, 1, 0);
    check_test_program_boundary(test, "int tested(int n){_Alignas(32) int x=n;return x;}", "automatic-alignment-boundary", true, false, 0, 1);
    check_test_program_boundary(test, "int global;struct S{int x;int*p;};int tested(int n){struct S s={n,&global};return s.x;}",
                                "aggregate-primary-initialization", true, false, 1, 1);
    check_test_program_boundary(test, "int tested(unsigned n){switch(n){case -1:return 1;case 4294967295U:return 2;}return 0;}",
                                "case-conversion-collision", false, false, 0, 0);
    check_test_program_boundary(test, "int tested(int n){switch(n){case 1 ... 5:return 1;case 4 ... 7:return 2;}return 0;}",
                                "overlapping-case-ranges", false, false, 0, 0);
    check_test_program_boundary(test, "int tested(int n){switch(n){default:return 1;default:return 2;}}", "duplicate-default", false, false, 0, 0);
    check_test_program_boundary(test, "int tested(int n){switch((double)n){case 1:return 1;}return 0;}", "noninteger-switch", false, false, 0, 0);
}

#endif
