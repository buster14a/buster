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
                       check_test_program_column(a->argument_classes, b->argument_classes, a->argument_count) &&
                       a->x87_constant_count == b->x87_constant_count && a->x87 == b->x87 &&
                       check_test_program_column(a->x87_constants, b->x87_constants, (uint64_t)a->x87_constant_count * 2 * sizeof(uint64_t));
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
        "#include <signal.h>\n#include <sys/resource.h>\n#include <sys/wait.h>\n#include <unistd.h>\n#include <stdarg.h>\n"
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
        // A result through a hidden pointer, which clang takes as aligned to the union's 16: w is the pointer's bytes
        // (clang -O0 returns in place), and a misaligned one shows in the first byte.
        "union external_wide{long double x;char c[40];};union external_wide external_wide(int n){union external_wide w;"
        "for(int i=0;i<40;i++)w.c[i]=(char)(n+i);if((unsigned long)&w%16)w.c[0]=99;return w;}\n"
        // Structs of a complex long double, which return in st0 and st1: one returned to the program, and the program's
        // own called through a pointer.
        "struct external_cx{_Complex long double z;};struct external_cx external_complex(long double a,long double b){"
        "struct external_cx c;__real__ c.z=a;__imag__ c.z=b;return c;}long double external_complex_apply("
        "struct external_cx(*f)(long double,long double),long double a){struct external_cx c=f(a,-a);return __real__ c.z*4+__imag__ c.z;}\n"
        // Variadic functions that a program calls through pointers with values past Tier 0's depth tables (#434's
        // review): their arguments as clang's callee reads them, folded into one value. external_variadic's doubles come
        // in xmm0 to xmm7, which its prologue saves for va_arg only when al isn't 0, then on the stack, and its longs in
        // the five registers after n's, then on the stack; external_floats' f8 and f9 come on the stack as floats, after
        // xmm0 to xmm7, and its unnamed longs and doubles all on the stack.
        "static unsigned long external_fold(unsigned long h,unsigned long v){return (h^v)*1099511628211UL;}\n"
        "unsigned long external_variadic(int n,...){va_list a;va_start(a,n);unsigned long h=(unsigned long)n;for(int i=0;i<20;i++){"
        "long l=va_arg(a,long);double d=va_arg(a,double);unsigned long b;__builtin_memcpy(&b,&d,8);h=external_fold(external_fold(h,(unsigned long)l),b);}"
        "va_end(a);return h;}\n"
        "unsigned long external_floats(int n,float f0,float f1,float f2,float f3,float f4,float f5,float f6,float f7,float f8,float f9,int m,...){"
        "const float f[10]={f0,f1,f2,f3,f4,f5,f6,f7,f8,f9};va_list a;va_start(a,m);unsigned long h=(unsigned long)n*31+(unsigned long)m;"
        "for(int i=0;i<10;i++){unsigned b;__builtin_memcpy(&b,&f[i],4);h=external_fold(h,b);}for(int i=0;i<10;i++){long l=va_arg(a,long);"
        "double d=va_arg(a,double);unsigned long b;__builtin_memcpy(&b,&d,8);h=external_fold(external_fold(h,(unsigned long)l),b);}va_end(a);return h;}\n"
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
        bool lowered = ready && check_program_lower(lower, &program, true, false, &output, &scratch) && check_program_lower(lower + 1, &program, false, false, &output, &scratch);
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
        const bool emitted = lowered && check_program_emit(&code, &program, &parser, &identifiers, data.capacity, path == 0, &output, &bytes, &scratch, NULL, NULL, true) &&
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
                const bool lowered_ok = check_program_lower(&lowered, &program, path == 0, false, &output, &scratch);
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
                    const bool failed_lower_ok = check_program_lower(&failed_lower, &program, path == 0, false, &empty, &scratch);
                    check_test_expect(test, !failed_lower_ok && failed_lower.status == CHECK_PROGRAM_RESOURCE && failed_lower.functions == 0 &&
                                           empty.length == 17 && scratch.length == 0, "unit ABI/lowering exhaustion is atomic");
                    CheckProgramCode failed_code = { 0 };
                    const uint64_t emission_mark = output.length;
                    const bool failed_code_ok = check_program_emit(&failed_code, &program, &parser, &identifiers, (LinkCapacity){ 0 }, path == 0,
                                                                   &output, &empty, &scratch, NULL, NULL, true);
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

// A program the checker refused, which may have no stream or input: lowering refuses it by its status, on both paths,
// before reading either, and writes nothing.
static void check_test_program_refused(CheckTest* test)
{
    Arena output = arena_reserve(1ull << 16);
    Arena scratch = arena_reserve(1ull << 16);
    for (uint32_t path = 0; path < 2; path += 1)
    {
        const CheckProgram program = { .status = CHECK_PROGRAM_INVALID };
        CheckProgramLowered lowered = { 0 };
        const bool ready = output.base != 0 && scratch.base != 0;
        const bool lowered_ok = ready && check_program_lower(&lowered, &program, path == 0, false, &output, &scratch);
        check_test_expect(test, ready && !lowered_ok && lowered.status == CHECK_PROGRAM_INVALID && lowered.functions == 0 && output.length == 0 && scratch.length == 0,
                          "a refused program without its stream or input lowers to nothing");
    }
    arena_release(&output);
    arena_release(&scratch);
}

static __attribute__((noinline)) void check_test_programs(CheckTest* test, const char* clang)
{
    check_test_program_refused(test);
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
        "_Thread_local int counter=9;_Thread_local long wide[4];int tested(int n){counter+=n;wide[n&3]+=counter;int*p=&counter;"
        "return *p*2+(int)wide[(n+1)&3];}", "thread-local-references", clang);
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
    // A discarded comma discards its right operand too: musl's vfwprintf.c copies a union with a long double so, and
    // a folded comma's constant is nothing (#440).
    check_test_program_source(test,
        "union A{unsigned long i;long double f;void*p;};"
        "int tested(int n){union A arg,list[4];int type[4]={0};for(int i=0;i<4;i++)list[i].f=n*0.5L+i;"
        "int k=n&3;arg.f=0;if(n>=0)type[k]=n,arg=list[k];else (k,5);return (int)(arg.f*4)+type[k]*100;}", "comma-discards-aggregate-copy", clang);
    // An aggregate assignment's value, the destination's address, read after its copy by another copy, a load at
    // offset 0, a return, a conditional's arm, and after the pieces of a copy past 256 bytes (#440).
    check_test_program_source(test,
        "struct S{long a,b;};struct F{long double v;int k;};struct L{long a;unsigned char c[300];};"
        "struct S pick(struct S*t,int n){struct S r;return r=t[n&1];}"
        "int tested(int n){struct S s,t,w[2]={{n,2},{3,n}};struct F f,g[2]={{n*0.25L,1},{2.5L,n}};struct L x,y,z;"
        "s=(t=w[n&1]);long a=(t=w[1]).a;long double v=(f=g[n&1]).v;struct S r=pick(w,n);struct S c=n>3?(t=w[0]):w[1];"
        "for(int i=0;i<300;i++)z.c[i]=(unsigned char)(i^n);z.a=n;x=(y=z);"
        "return (int)(s.a+s.b*3+t.b*5+a*7+r.a*11+c.b*13+x.a*17+f.k*19)+(int)(v*8)+x.c[n&255]+y.c[299];}",
        "aggregate-assignment-values", clang);
    check_test_program_source(test,
        "int add(int a,int b){return a+b;}int mul(int a,int b){return a*b;}int(*pick(int n))(int,int){return n&1?add:mul;}"
        "int apply(int(*f)(int,int),int x){return (*f)(x,x);}"
        "int tested(int n){int(*f)(int,int)=n<0?add:mul;int(*t[2])(int,int)={add,mul};"
        "return f(n,3)+(*t[n&1])(n,4)+pick(n)(2,n)+apply(t[0],n)+apply(&mul,2);}", "indirect-calls", clang);
    // A statement region makes the lowering track the stack, where an indirect call pops the address it calls too:
    // returns and joins after one must find nothing left behind (#377's review).
    check_test_program_source(test,
        "int inc(int x){return x+1;}int dec(int x){return x-1;}"
        "int tested(int n){int(*f)(int)=n&1?inc:dec;int y=({int t=f(n);t*2;});if(n>5)return f(y);int z=n>2?f(n):({f(y)+1;});return y+z;}",
        "indirect-calls-tracked", clang);
    check_test_program_source(test,
        "int tested(int n){char c=(char)n;unsigned char u=(unsigned char)(n*16);short s=(short)(n*4096);_Bool b=c;_Bool d=s;"
        "return !c*1000+!u*100+!s*10+b+d*2+(c&&u)*4+(s||u)*8+(c?16:0);}", "narrow-truth-values", clang);
    // Bit-fields (#145's 00218): reads sign- or zero-extend, stores truncate and keep the neighbors' bits, whose
    // bytes (3, 5 and 8 of them here) are accessed as pieces; an assignment's value is the field's as it now reads.
    check_test_program_source(test,
        "struct B{unsigned a:3;int b:5;unsigned c:20;char d;long long e:40;unsigned long long f:64;_Bool g:1;};"
        "int tested(int n){struct B s;s.d=99;s.a=(unsigned)n;s.b=n*3;s.c=(unsigned)(n*100003);s.e=(long long)n*-1000000007LL;"
        "s.f=(unsigned long long)n*0x9e3779b97f4a7c15ULL;s.g=n&2;int v=(s.b=n*7);int w=(s.a=n+9);"
        "return s.a+s.b*3+(int)(s.c%1009)+s.d+(int)(s.e%997)+(int)(s.f>>58)+s.g*5+v*11+w*13;}", "bit-field-reads-and-stores", clang);
    check_test_program_source(test,
        "struct C{unsigned x:7;unsigned y:9;int z:13;int w:3;};"
        "int tested(int n){struct C c={1,2,3,-1};struct C*p=&c;c.x++;++c.y;p->z-=n*50;p->w+=n;int a=c.x++;int b=--p->y;int d=(p->z*=3);"
        "int e=(c.w<<=1);c.y+=n*37;c.x^=n;p->z/=n|1;c.w%=3;return c.x+c.y*3+c.z*5+c.w*7+a*11+b*13+d*17+e*19;}", "bit-field-updates", clang);
    check_test_program_source(test,
        "struct D{unsigned char before;unsigned v:20;unsigned char after;unsigned long long big:40;unsigned char tail;};"
        "union U{struct{unsigned lo:4,hi:4;}s;unsigned char b;};"
        "int tested(int n){struct D d={0x5a,(unsigned)n*4099,0xa5,(unsigned long long)n<<30,7};struct D e={.v=3};d.v=(unsigned)n;d.big=~0ULL;"
        "union U u={.s={.lo=n,.hi=(unsigned)n>>4}};u.s.hi=9;struct D f=(struct D){.big=(unsigned long long)n,.after=1};"
        "return d.before+d.after*3+(int)d.v*5+(int)(d.big>>20)+d.tail+e.v*7+e.before+e.after+u.b+(int)f.big+f.after+f.v;}",
        "bit-field-initializers-and-neighbors", clang);
    check_test_program_source(test,
        "enum E{E0=0,E1=200,E2};enum F{F0=-3,F1=3};"
        "struct G{enum E e:8;enum F f:3;unsigned char c:3;signed char s:4;unsigned short h:10;_Bool b:1;};"
        "int tested(int n){struct G g={E2,F0,5,-2,1000,1};g.e=(enum E)(E1+n);g.f=n&1?F0:F1;g.s=(signed char)n;"
        "_Generic(0,int:g.h)=(unsigned short)(n*77);g.c++;int t=(g.b?g.s:0)+g.c*2;"
        "return (int)g.e+(int)g.f*3+g.c+g.s*5+g.h+g.b*7+t*11+(g.e>E1)*13;}", "bit-field-signedness-and-selection", clang);
    check_test_program_source(test,
        "struct __attribute__((packed)) P6{unsigned char k:4;unsigned long long x:44;};"
        "struct __attribute__((packed)) P7{unsigned char k:4;long long y:52;};struct __attribute__((packed)) P3{unsigned char k:4;int z:20;};"
        "int tested(int n){struct P6 a={1,2};struct P7 b={3,-4};struct P3 c[2]={{5,-6},{7,8}};a.x=(unsigned long long)n*0x123456789ULL;"
        "b.y=(long long)n*-0x1234567891LL;c[1].z=n*-30001;a.x+=7;b.y--;int t=(int)(c[1].z++);c[0].z^=n;"
        "return (int)(a.x%1013)+(int)(b.y%1019)+c[1].z+c[0].z*3+a.k+b.k*5+c[0].k+c[1].k*7+t*11;}", "bit-field-packed-spans", clang);
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
    // Variable-length arrays' objects: their strides and sizes from the extents their declarations captured, and the
    // stack they take released at their scopes' ends, by continue, break and goto out of them, and by a goto back before
    // their declaration. Each loop takes 20,000 arrays of about 4 KB or 10,000 of 8 KB: 80 MB, past the 8 MB stack, if
    // one release is missing.
    check_test_program_source(test,
        "int tested(int n){int m=(n&3)+2;int a[m][m+1];for(int i=0;i<m;i++)for(int j=0;j<=m;j++)a[i][j]=i*10+j;"
        "int(*p)[m+1]=a;p++;int(*q)[m+1]=a+m-1;q-=1;typedef char T[m*3];m=99;T t;t[0]=(char)(n&63);"
        "return (int)sizeof a+(int)sizeof a[0]*10+(*p)[1]*100+q[1][m-97]+(int)(q-p)*1000+(int)sizeof t+t[0];}",
        "vla-objects-strides-and-sizes", clang);
    check_test_program_source(test,
        "long sum(int n,int m,int a[n][m]){long s=0;for(int i=0;i<n;i++)for(int j=0;j<m;j++)s+=a[i][j]*(i+1);return s+(long)sizeof(a[0]);}"
        "int tested(int n){int r=(n&3)+1,c=(n&7)+2;int a[r][c];for(int i=0;i<r;i++)for(int j=0;j<c;j++)a[i][j]=i*c+j-n;"
        "int x=({int y=0;{int b[c];b[c-1]=n;y=b[c-1]*2;}y;});return (int)sum(r,c,a)+x;}",
        "vla-parameter-strides", clang);
    check_test_program_source(test,
        "int tested(int n){long s=0;for(int i=0;i<20000;i++){int a[1000+(n&7)];a[0]=i;a[999]=n;if(i%3==0)continue;"
        "s+=a[0]+a[999];if(i==19990)break;}int k=0;while(k<20000){{char c[4000+(n&3)];c[0]=(char)(k&63);k++;if(k&1)goto next;"
        "s+=c[0];}next:;}do{short d[2000+(n&1)];d[1]=(short)k;s+=d[1];if(--k&1)continue;}while(k>0);"
        "for(int i=0;i<20000;i++)switch((i+n)&1){case 0:{long e[60];long f[500+(n&3)];e[0]=i;f[0]=n;s+=f[0]+e[0];break;}default:s+=1;}"
        "return (int)(s%1000003);}",
        "vla-scope-releases", clang);
    check_test_program_source(test,
        "int tested(int n){int k=0;long s=0;again:;int a[2000+(n&3)];a[1999]=k;s+=a[1999];if(++k<10000)goto again;"
        "return (int)(s%1000003)+(int)sizeof a;}",
        "vla-backward-goto-releases", clang);
    check_test_program_source(test,
        "int tested(int n){long s=0;for(int i=0;i<20000;i++){int g[1000+(n&3)];g[0]=i;"
        "switch((i+n)&3){case 0:continue;case 1:{int h[(n&7)+1];h[0]=i;s+=h[0];break;}default:break;}s+=g[0];}"
        "return (int)(s%1000003);}",
        "vla-continue-through-switch", clang);
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
    // Long double (#136, #137): its locals and parameters frame objects, its values on the x87 through their slots, its
    // constants in each function's table, compares, every conversion (to and from unsigned long past 2^63 too), calls
    // passing it on the stack and returning it in st0 (a struct of one too), va_arg of it, and updates of it in memory.
    check_test_program_source(test,
        "long double scale(long double a,int i,double d){return a*i+d;}"
        "int tested(int n){long double x=n/3.0L;long double y=scale(x,n,0.5);long double z=-y;z+=x;z*=2;z-=n;z/=7;"
        "int c=(x<y)+2*(x<=y)+4*(x>y)+8*(x>=y)+16*(x==y)+32*(x!=y)+64*!z+128*(x&&y)+256*(z||0.0L);"
        "long long l=(long long)(y*1000);unsigned long u=(unsigned long)(x*x*1e15L+1e19L);unsigned char b=(unsigned char)(x+100);"
        "_Bool t=(_Bool)z;float f=(float)x;double d=(double)y;"
        "return c+(int)(l%100000)+(int)(u%9973)+(int)z+b+t+(int)(f*64)+(int)(d*16)+(int)((long double)u/1e18L);}",
        "long-double-arithmetic-and-conversions", clang);
    check_test_program_source(test,
        "long double g=2.5L;long double table[3]={1.5L,-0.25L,3.0L};struct P{long double a;int t;long double b;};struct One{long double x;};"
        "struct One third(long double a){struct One r={a/3};return r;}"
        "struct P make(long double a,int t){struct P p={a,t,a*t};return p;}"
        "long double sum(int count,...){__builtin_va_list ap;__builtin_va_start(ap,count);long double s=0;"
        "for(int i=0;i<count;i++){s=s*2+__builtin_va_arg(ap,long double);s+=__builtin_va_arg(ap,int);}__builtin_va_end(ap);return s;}"
        "int tested(int n){long double v=n;long double old=v++;long double pre=++v;long double down=v--;--v;"
        "table[(unsigned)n%3]+=v;g+=old;struct P p=make(pre,n);struct One o=third(p.b);"
        "long double s=sum(3,v,n,down,2,o.x,-n);long double*q=&table[1];*q-=s/64;long double r=n&1?s:*q;"
        "return (int)(s*16)^(int)(r*8)^(int)(g*2)^(int)(table[(unsigned)n%3]*4)^p.t^(int)(p.a*3);}",
        "long-double-memory-and-calls", clang);
    // A call's result object, which a hidden pointer passes, aligned to the result's canonical type (16 for a union of a
    // long double) though an aligned typedef says 1: va_arg's 8-byte object between the calls' objects puts one of them 8
    // bytes off a multiple of 16 if their alignment were the typedef's. (Clang passes a variable of the typedef's type as
    // the pointer itself, at the typedef's alignment, so the variables here are of the union's.)
    check_test_program_source(test,
        "union wide{long double x;signed char c[40];};typedef union wide W __attribute__((aligned(1)));struct eight{signed char c[8];};W external_wide(int n);"
        "int shifted(int n,...){__builtin_va_list ap;__builtin_va_start(ap,n);union wide a=external_wide(n);struct eight e=__builtin_va_arg(ap,struct eight);"
        "union wide b=external_wide(n+1);int c=external_wide(n+2).c[0];__builtin_va_end(ap);return a.c[0]+b.c[0]*3+c*5+a.c[39]+e.c[7];}"
        "int tested(int n){struct eight e={{1,2,3,4,5,6,7,(char)n}};return shifted(n,e);}",
        "hidden-result-canonical-alignment", clang);
    check_test_program_source(test,
        "struct cx{_Complex long double z;};struct cx external_complex(long double a,long double b);"
        "long double external_complex_apply(struct cx(*f)(long double,long double),long double a);"
        "struct cx swap(long double a,long double b){struct cx c=external_complex(b,a);return c;}"
        "int tested(int n){struct cx c=external_complex(n,n*0.5L);long double r=__real__ c.z,i=__imag__ c.z;"
        "return (int)(r*8+i*2)+(int)external_complex_apply(swap,n+0.25L);}",
        "long-double-complex-struct-results", clang);
    check_test_program_source(test,
        "long double half();long double twice(long double a){return a*2;}"
        "long double apply(long double(*f)(long double),long double a){return f(a);}"
        "int tested(int n){long double a[4]={n,n+0.5L,-n,0};long double*p=a;p+=2;"
        "long double k=apply(twice,*p)+half(a[1]);a[3]=k;"
        "return (int)(a[3]*4)+(int)(p[-1]*2)+(int)sizeof(long double)+(int)_Alignof(long double)+(int)sizeof a;}"
        "long double half(long double a){return a/2;}",
        "long-double-indirect-and-unprototyped-calls", clang);
    // Conversions between unsigned long and float or double (Tier 0's tier0_emit_u64_convert): to them near 2^53, past
    // 2^63 where the halving's kept bit rounds, and near 2^64; back from values up to 2^64 less 4096, every one in range.
    check_test_program_source(test,
        "unsigned long big(int n){return 0xfffffffffffffc00ul-(unsigned long)(n+3)*0x400ul;}"
        "int tested(int n){unsigned long u=big(n),w=(1ul<<63)+(unsigned long)(n+3)*1024u+1,t=(1ul<<53)+(unsigned long)(n+3);"
        "double d=u,e=t;float f=w,g=t;"
        "unsigned long r=(unsigned long)(d/2)+(unsigned long)(f/4)+(unsigned long)e+(unsigned long)g+(unsigned long)f+(unsigned long)(d-4096.0);"
        "unsigned long hi=(unsigned long)(double)w;return (int)(r%9973)^(int)(hi%10007)^(int)(u%1000)^(int)(t%977);}",
        "unsigned-long-floating-conversions", clang);
    {
        // Values past Tier 0's depth tables (tier0_select_deep): a call of 40 unsigned long and 24 double arguments, the
        // most a call takes (BUSTER_TIER0_MAX_CALL_ARGUMENTS), each an expression evaluated over the arguments before it,
        // with ?:, a division, a shift and a call among the later ones; a call through a pointer of 18; and a product
        // nested 24 deep. Run against clang's.
        char source[8192];
        uint32_t length = 0;
        length += (uint32_t)snprintf(source + length, sizeof(source) - length, "unsigned long many(");
        for (uint32_t i = 0; i < 40; i += 1)
        {
            length += (uint32_t)snprintf(source + length, sizeof(source) - length, "unsigned long a%u,", i);
        }
        for (uint32_t i = 0; i < 24; i += 1)
        {
            length += (uint32_t)snprintf(source + length, sizeof(source) - length, "double d%u%s", i, i + 1 < 24 ? "," : "){unsigned long s=0;double t=0;");
        }
        for (uint32_t i = 0; i < 40; i += 1)
        {
            length += (uint32_t)snprintf(source + length, sizeof(source) - length, "s=s*31+a%u;", i);
        }
        for (uint32_t i = 0; i < 24; i += 1)
        {
            length += (uint32_t)snprintf(source + length, sizeof(source) - length, "t=t*1.5+d%u;", i);
        }
        length += (uint32_t)snprintf(source + length, sizeof(source) - length, "return s^(unsigned long)(long)(t*16);}unsigned long eighteen(");
        for (uint32_t i = 0; i < 18; i += 1)
        {
            length += (uint32_t)snprintf(source + length, sizeof(source) - length, "unsigned long b%u%s", i, i + 1 < 18 ? "," : "){return 0");
        }
        for (uint32_t i = 0; i < 18; i += 1)
        {
            length += (uint32_t)snprintf(source + length, sizeof(source) - length, "+b%u*%u", i, 2 * i + 1);
        }
        length += (uint32_t)snprintf(source + length, sizeof(source) - length, ";}int tested(int n){unsigned long x=(unsigned long)n+3;unsigned long r=many(");
        for (uint32_t i = 0; i < 40; i += 1)
        {
            static const char* const shapes[4] = { "x+%u", "(x&1?x*%u:x-%u)", "x/%u+(x<<%u%%7)", "eighteen(x,%u,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x)" };
            char argument[96];
            snprintf(argument, sizeof(argument), shapes[i % 4], i + 2, i + 1);
            length += (uint32_t)snprintf(source + length, sizeof(source) - length, "%s,", argument);
        }
        for (uint32_t i = 0; i < 24; i += 1)
        {
            length += (uint32_t)snprintf(source + length, sizeof(source) - length, "(double)x*%u.5-%u%s", i, i, i + 1 < 24 ? "," : ");");
        }
        length += (uint32_t)snprintf(source + length, sizeof(source) - length, "unsigned long(*f)(");
        for (uint32_t i = 0; i < 18; i += 1)
        {
            length += (uint32_t)snprintf(source + length, sizeof(source) - length, "unsigned long%s", i + 1 < 18 ? "," : ")=eighteen;unsigned long q=f(");
        }
        for (uint32_t i = 0; i < 18; i += 1)
        {
            length += (uint32_t)snprintf(source + length, sizeof(source) - length, "x*%u+(x>>%u)%s", i + 1, i % 3, i + 1 < 18 ? "," : ");unsigned long deep=");
        }
        for (uint32_t i = 0; i < 24; i += 1)
        {
            length += (uint32_t)snprintf(source + length, sizeof(source) - length, "(x+%u)*(", i);
        }
        length += (uint32_t)snprintf(source + length, sizeof(source) - length, "x");
        for (uint32_t i = 0; i < 24; i += 1)
        {
            length += (uint32_t)snprintf(source + length, sizeof(source) - length, ")");
        }
        snprintf(source + length, sizeof(source) - length, ";return (int)((r^q^deep)%%100003);}");
        check_test_program_source(test, source, "values-past-the-depth-tables", clang);
    }
    {
        // Indirect variadic calls past the depth tables (#434's review), to the harness's functions, which clang compiles:
        // after a variable-length array, which gives the function its frame pointer (an integer table of 14) and moves
        // rsp, one of 20 unsigned long and 20 double arguments in turn, past both tables, whose al the callee's prologue
        // reads; and one of 10 floats, the last two on the stack, an int and 10 more of each, past the floating table.
        char source[4096];
        uint32_t length = 0;
        length += (uint32_t)snprintf(source + length, sizeof(source) - length,
                                     "unsigned long external_variadic(int n,...);unsigned long external_floats(int n,float,float,float,float,float,float,float,"
                                     "float,float,float,int m,...);int tested(int n){unsigned long x=(unsigned long)n+3;unsigned long buffer[(x&7)+1];"
                                     "for(unsigned long i=0;i<=(x&7);i++)buffer[i]=x*i+1;unsigned long(*v)(int,...)=external_variadic;"
                                     "unsigned long(*f)(int,float,float,float,float,float,float,float,float,float,float,int,...)=external_floats;"
                                     "unsigned long a=v(n");
        for (uint32_t i = 0; i < 20; i += 1)
        {
            length += (uint32_t)snprintf(source + length, sizeof(source) - length, ",x*%u+%u,(double)x*0.5-%u", i + 1, i, i);
        }
        length += (uint32_t)snprintf(source + length, sizeof(source) - length, ");unsigned long b=f(n");
        for (uint32_t i = 0; i < 10; i += 1)
        {
            length += (uint32_t)snprintf(source + length, sizeof(source) - length, ",(float)x*0.25f+%u", i);
        }
        length += (uint32_t)snprintf(source + length, sizeof(source) - length, ",n+7");
        for (uint32_t i = 0; i < 10; i += 1)
        {
            length += (uint32_t)snprintf(source + length, sizeof(source) - length, ",x*3+%u,(double)x/4+%u", i, i);
        }
        snprintf(source + length, sizeof(source) - length, ");return (int)((a^b^buffer[x&7])%%100003);}");
        check_test_program_source(test, source, "variadic-calls-past-the-depth-tables", clang);
    }
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
    check_test_program_boundary(test, "int tested(int n){int values[n+10];return sizeof values;}", "variable-length-array-program", true, true, 0, 0);
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
        "int x=__builtin_va_arg(ap,int);__builtin_va_end(ap);return x;}", "varargs-unevaluated-final-parameter", true, true, 0, 1);
    check_test_program_boundary(test, "_Thread_local int g;int tested(int n){return g+n;}", "executable-tls-reference", true, true, 1, 0);
    check_test_program_boundary(test, "int tested(int n){_Alignas(32) int x=n;return x;}", "automatic-alignment-boundary", true, false, 0, 1);
    check_test_program_boundary(test, "int global;struct S{int x;int*p;};int tested(int n){struct S s={n,&global};return s.x;}",
                                "aggregate-primary-initialization", true, true, 1, 1);
    check_test_program_boundary(test, "struct S{int x;};struct S make(int n);int tested(int n){return make(n).x;}",
                                "aggregate-result-lowering", true, true, 1, 0);
    check_test_program_boundary(test, "struct S{int x;};int take(struct S);int tested(int n){struct S s={n};return take(s);}",
                                "aggregate-argument-lowering", true, true, 1, 1);
    check_test_program_boundary(test, "struct B{unsigned x:3;};int tested(int n){struct B b;b.x=n;return b.x;}",
                                "bit-field-lowering", true, true, 0, 0);
    // Long double: lowered, with its constants' table, and what would keep one of its values in a temporary refused.
    check_test_program_boundary(test, "long double g;int tested(int n){long double x=n+0.5L;g=x;return g>1;}", "long-double-lowering", true, true, 1, 1);
    check_test_program_boundary(test, "long double tested(int n){return ({long double t=n;t;});}", "long-double-statement-region-value", true, false, 0, 1);
    check_test_program_boundary(test, "int tested(int n){long double x=n;return (x?:2.0L)>1;}", "long-double-omitted-conditional-operand", true, false, 0, 1);
    check_test_program_boundary(test, "int tested(int n){long double x=n;return __builtin_isnan(x);}", "long-double-floating-builtin", true, false, 0, 1);
    check_test_program_boundary(test, "int tested(int n){_Complex long double z=n;return __real__ z>1;}", "complex-long-double", true, false, 0, 1);
    check_test_program_boundary(test,
                                "struct __attribute__((packed)) P{unsigned char c:4;unsigned long long x:64;};"
                                "int tested(int n){struct P p;p.x=(unsigned long long)n;return (int)p.x;}",
                                "bit-field-nine-byte-machine-boundary", true, false, 0, 0);
    check_test_program_boundary(test, "int tested(unsigned n){switch(n){case -1:return 1;case 4294967295U:return 2;}return 0;}",
                                "case-conversion-collision", false, false, 0, 0);
    check_test_program_boundary(test, "int tested(int n){switch(n){case 1 ... 5:return 1;case 4 ... 7:return 2;}return 0;}",
                                "overlapping-case-ranges", false, false, 0, 0);
    check_test_program_boundary(test, "int tested(int n){switch(n){default:return 1;default:return 2;}}", "duplicate-default", false, false, 0, 0);
    check_test_program_boundary(test, "int tested(int n){switch((double)n){case 1:return 1;}return 0;}", "noninteger-switch", false, false, 0, 0);
}

#endif
