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
#include "c_check_control_input_test.c"

static void check_test_program_addressed(CheckTest* test)
{
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    uint8_t* mappings[3] = { 0 };
    bool ready = true;
    for (uint32_t column = 0; column < 3; column += 1)
    {
        mappings[column] = mmap(0, page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        ready = mappings[column] != MAP_FAILED && ready;
        if (mappings[column] != MAP_FAILED)
        {
            ready = mprotect(mappings[column] + page, page, PROT_NONE) == 0 && ready;
        }
    }
    check_test_expect(test, ready, "label address columns have protected tails");
    for (uint32_t count = 0; ready && count <= 129; count += 1)
    {
        uint8_t* kinds = mappings[0] + page - count;
        uint8_t* roles = mappings[1] + page - count;
        uint32_t* targets = (uint32_t*)(mappings[2] + page - (uint64_t)count * sizeof(uint32_t));
        uint8_t expected[130] = { 0 };
        uint8_t scalar[130];
        uint8_t vector[130];
        for (uint32_t token = 0; token < count; token += 1)
        {
            kinds[token] = token % 2 == 0 ? PARSE_KIND_AMPAMP : PARSE_KIND_IDENTIFIER;
            roles[token] = token % 3 == 0 ? PARSE_ROLE_PREFIX : PARSE_ROLE_LOGICAL_AND;
            targets[token] = token % 5 == 0 ? count : (token * 7) % count;
            if (token % 6 == 0 && token % 5 != 0)
            {
                expected[targets[token]] = 1;
            }
        }
        Parser parser = { 0 };
        parser.tokens.kinds = kinds;
        parser.tree.roles = roles;
        const CheckInput input = { .token_count = count, .label_targets = targets };
        check_program_addressed(&parser, &input, scalar, true);
        check_program_addressed(&parser, &input, vector, false);
        check_test_expect(test, memcmp(scalar, expected, count + 1) == 0 && memcmp(vector, expected, count + 1) == 0,
                          "label addresses exclude logical && and non-prefix tokens, including every guarded vector tail");
    }
    for (uint32_t column = 0; column < 3; column += 1)
    {
        if (mappings[column] != MAP_FAILED)
        {
            munmap(mappings[column], page * 2);
        }
    }
}

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

// Writes the link's object to `path`, and a copy of its bytes to `bytes` and `size` (the caller frees them).
static bool check_test_program_object(Link* link, const char* path, Arena* scratch, uint8_t** bytes, uint64_t* size)
{
    *bytes = 0;
    *size = 0;
    const uint64_t mark = scratch->length;
    ObjectUnits units = { 0 };
    ObjectSymbols symbols = { 0 };
    bool valid = link_check(link) == LINK_ERROR_NONE && object_prepare(&units, link, 1, scratch) && object_map_symbols(&symbols, link, scratch);
    if (valid)
    {
        const Object object = object_build(link, &units, &symbols, 0, (ObjectOptions){ 0 }, BUSTER_S8("checked-source.c"), scratch);
        valid = object.error == OBJECT_ERROR_NONE && object_write_file(path, object.bytes, object.size) == OBJECT_ERROR_NONE;
        *bytes = valid ? malloc(object.size + 1) : 0;
        valid = valid && *bytes != 0;
        if (valid)
        {
            memcpy(*bytes, object.bytes, object.size);
            *size = object.size;
        }
    }
    arena_pop_to(scratch, mark);
    return valid;
}

// The oracle's source: each global the checked object defines renamed oracle_NAME, then the program, in `text`.
static bool check_test_program_oracle(const Link* link, const char* source, char** text, size_t* length)
{
    *text = 0;
    *length = 0;
    FILE* file = open_memstream(text, length);
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

// The program every checked object and its oracle are linked with: it runs both on the same inputs.
static const char check_test_program_harness[] =
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
    // Structs aligned to 32 by value (MEMORY, on a stack aligned to 32 at the call), which clang's callee takes in place:
    // an outgoing area off its alignment shows in their addresses.
    "struct external_aligned{_Alignas(32) long a;int b;};int external_aligned_by_value(int k,struct external_aligned x,struct external_aligned y){"
    "return (int)(x.a+x.b+y.a*3+y.b)+k+(int)((((unsigned long)&x)|((unsigned long)&y))&31)*1000;}\n"
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

// What every program's build shares (#465): the harness, the same for every program, compiled once per run for each
// signedness of plain char that the programs are read with, and the last oracle, which the scalar and vector paths' programs of one source share, compiled once per distinct text. Each
// compiles with -c and the flags of the one-step build, which gives the code that build's compiles give, so a program
// links the same harness, oracle and checked object as before. The oracle's key is its text and plain char's
// signedness. And the last program that linked and ran as the oracle
// did: its checked object's bytes. A program whose checked object and oracle are that one's links the same files into
// the same executable, which runs alike, so it takes that run's result (the vector path's program, mostly, after the
// scalar path's).
typedef struct CheckTestProgramBuild CheckTestProgramBuild;
struct CheckTestProgramBuild
{
    char directory[40];
    bool harness[2];
    char* oracle;
    size_t oracle_length;
    bool oracle_unsigned;
    uint8_t* object;
    uint64_t object_size;
};

static CheckTestProgramBuild check_test_program_build;

// The flags of every compile and link of a program, with plain char as the checker read it (`char_unsigned`), for the
// oracle and the harness alike.
#define BUSTER_CHECK_TEST_PROGRAM_FLAGS(char_unsigned) \
    "-std=gnu17", \
    "-O0", \
    "-march=x86-64", \
    "-fno-common", \
    (char_unsigned) ? "-funsigned-char" : "-fsigned-char", \
    "-Wno-unused-value", \
    "-no-pie"

// The harness's name in the build's directory for plain char's signedness.
static const char* check_test_program_harness_name(bool char_unsigned)
{
    return char_unsigned ? "harness-unsigned" : "harness-signed";
}

static void check_test_program_build_path(char* path, size_t size, const char* name)
{
    snprintf(path, size, "%s/%s", check_test_program_build.directory, name);
}

// Compiles `name`.c, holding `text`, to `name`.o in the build's directory, its diagnostics in `out` and `err`.
static bool check_test_program_compile(const char* clang, const char* name, const char* text, size_t length, bool char_unsigned, const char* out,
                                       const char* err)
{
    char source[96];
    char object[96];
    char file[32];
    snprintf(file, sizeof(file), "%s.c", name);
    check_test_program_build_path(source, sizeof(source), file);
    snprintf(file, sizeof(file), "%s.o", name);
    check_test_program_build_path(object, sizeof(object), file);
    char* compile[] = {
        (char*)clang,
        BUSTER_CHECK_TEST_PROGRAM_FLAGS(char_unsigned),
        "-Wno-unused-command-line-argument",
        "-c",
        source,
        "-o",
        object,
        0,
    };
    return parse_test_write(source, text, length) && object_test_run(compile, out, err) == 0;
}

// The build's directory, made on first use, with the harness of plain char's signedness compiled into it, and the
// oracle of `text` compiled unless it is the last one's, with the same signedness (`*same_oracle`).
static bool check_test_program_prepare(const char* clang, const char* text, size_t length, bool char_unsigned, const char* out, const char* err,
                                       bool* same_oracle)
{
    CheckTestProgramBuild* build = &check_test_program_build;
    if (build->directory[0] == 0)
    {
        snprintf(build->directory, sizeof(build->directory), "/tmp/buster-check-build-XXXXXX");
        if (mkdtemp(build->directory) == 0)
        {
            build->directory[0] = 0;
        }
    }
    bool valid = build->directory[0] != 0;
    if (valid && !build->harness[char_unsigned])
    {
        build->harness[char_unsigned] = check_test_program_compile(clang, check_test_program_harness_name(char_unsigned), check_test_program_harness,
                                                                   sizeof(check_test_program_harness) - 1, char_unsigned, out, err);
        valid = build->harness[char_unsigned];
    }
    const bool same = build->oracle != 0 && build->oracle_unsigned == char_unsigned && build->oracle_length == length &&
                      memcmp(build->oracle, text, length) == 0;
    *same_oracle = same;
    if (valid && !same)
    {
        free(build->oracle);
        build->oracle = 0;
        valid = check_test_program_compile(clang, "oracle", text, length, char_unsigned, out, err);
        build->oracle = valid ? malloc(length) : 0;
        valid = valid && build->oracle != 0;
        if (valid)
        {
            memcpy(build->oracle, text, length);
            build->oracle_length = length;
            build->oracle_unsigned = char_unsigned;
        }
    }
    return valid;
}

// Removes the build's directory and forgets what it held: run after the last program of a section.
static void check_test_program_build_release(void)
{
    CheckTestProgramBuild* build = &check_test_program_build;
    if (build->directory[0] != 0)
    {
        static const char* const names[] = { "harness-signed.c", "harness-signed.o", "harness-unsigned.c", "harness-unsigned.o", "oracle.c", "oracle.o" };
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(names); i += 1)
        {
            char path[96];
            check_test_program_build_path(path, sizeof(path), names[i]);
            unlink(path);
        }
        rmdir(build->directory);
    }
    free(build->oracle);
    free(build->object);
    *build = (CheckTestProgramBuild){ 0 };
}

static bool check_test_program_execute(Link* link, const char* source, const char* clang, bool char_unsigned, Arena* scratch)
{
    char directory[] = "/tmp/buster-check-program-XXXXXX";
    const bool created = mkdtemp(directory) != 0;
    char object[160];
    char executable[160];
    char out[160];
    char err[160];
    char harness[96];
    char oracle[96];
    snprintf(object, sizeof(object), "%s/checked.o", directory);
    snprintf(executable, sizeof(executable), "%s/run", directory);
    snprintf(out, sizeof(out), "%s/out", directory);
    snprintf(err, sizeof(err), "%s/err", directory);
    char* text = 0;
    size_t length = 0;
    uint8_t* bytes = 0;
    uint64_t size = 0;
    bool same_oracle = false;
    bool valid = created && check_test_program_object(link, object, scratch, &bytes, &size) && check_test_program_oracle(link, source, &text, &length);
    valid = valid && check_test_program_prepare(clang, text, length, char_unsigned, out, err, &same_oracle);
    CheckTestProgramBuild* build = &check_test_program_build;
    const bool same = valid && same_oracle && build->object != 0 && build->object_size == size && memcmp(build->object, bytes, size) == 0;
    char harness_file[32];
    snprintf(harness_file, sizeof(harness_file), "%s.o", check_test_program_harness_name(char_unsigned));
    check_test_program_build_path(harness, sizeof(harness), harness_file);
    check_test_program_build_path(oracle, sizeof(oracle), "oracle.o");
    char* link_command[] = {
        (char*)clang,
        BUSTER_CHECK_TEST_PROGRAM_FLAGS(char_unsigned),
        harness,
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
    valid = valid && (same || (object_test_run(link_command, out, err) == 0 && object_test_run(run, out, err) == 0));
    if (!same)
    {
        free(build->object);
        build->object = valid ? bytes : 0;
        build->object_size = valid ? size : 0;
        bytes = valid ? 0 : bytes;
    }
    if (!valid && created)
    {
        // The sources go beside the object, as the one-step build had them.
        char path[160];
        snprintf(path, sizeof(path), "%s/driver.c", directory);
        parse_test_write(path, check_test_program_harness, sizeof(check_test_program_harness) - 1);
        snprintf(path, sizeof(path), "%s/oracle.c", directory);
        if (text != 0)
        {
            parse_test_write(path, text, length);
        }
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
    free(text);
    free(bytes);
    if (valid && created)
    {
        unlink(object);
        unlink(executable);
        unlink(out);
        unlink(err);
        rmdir(directory);
    }
    return valid;
}

// A program fixture, checked and compiled with plain char signed or unsigned (-funsigned-char), against clang's build of
// it with the same.
static void check_test_program_source_char(CheckTest* test, const char* source, const char* name, const char* clang, bool char_unsigned)
{
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    const PPOptions pp_options = {
        .language = { .standard = 2017, .gnu = true },
        .no_standard_includes = true,
        .predefines.char_unsigned = char_unsigned,
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
        const bool emitted = lowered && check_program_emit(&code, &program, &parser, &identifiers, data.capacity, path == 0, &output, &bytes, &scratch, NULL, NULL, false, true) &&
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
            check_test_expect(test, check_test_program_execute(&code.link, source, clang, char_unsigned, &scratch), description);
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

// A program fixture with plain char signed, x86-64's.
static void check_test_program_source(CheckTest* test, const char* source, const char* name, const char* clang)
{
    check_test_program_source_char(test, source, name, clang, false);
}

// A program fixture checked and lowered by both paths: accepted or not, lowered or refused, with the references and
// initializations it keeps; and, for a refusal with `reason`, the feature the refusal names
// (CheckProgramLowered.reason, which the driver prints as "unsupported: FEATURE").
static void check_test_program_boundary_reason(CheckTest* test, const char* source, const char* name, bool accepted, bool machine, uint32_t references,
                                               uint32_t initializations, const char* reason)
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
                snprintf(description, sizeof(description), "%s: the refusal names its feature", name);
                check_test_expect(test, reason == NULL || (!lowered_ok && lowered.reason != NULL && strcmp(lowered.reason, reason) == 0), description);
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
                                                                   &output, &empty, &scratch, NULL, NULL, false, true);
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

static void check_test_program_boundary(CheckTest* test, const char* source, const char* name, bool accepted, bool machine, uint32_t references, uint32_t initializations)
{
    check_test_program_boundary_reason(test, source, name, accepted, machine, references, initializations, NULL);
}

// The jump tables the lowering makes of `source`'s switches (#136's item 7), by both paths: `tables` of them, with
// `entries` entries in all, as clang -O0's rule gives them (check_lower_switch_table). The program must list exactly
// its SWITCH controls with four CASE controls or more after them, in the controls' order (CheckProgram.switches), as a
// scan of its controls finds them.
static void check_test_program_tables(CheckTest* test, const char* source, const char* name, uint32_t tables, uint32_t entries)
{
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    const PPOptions pp_options = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true };
    const bool preprocessed = intern_initialize(&identifiers) && pp_initialize(&pp, &pp_options, &identifiers) &&
                              pp_run_source(&pp, name, (const uint8_t*)source, (uint32_t)strlen(source));
    check_test_expect(test, preprocessed, "jump table source preprocessing");
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
        CheckProgram program = { 0 };
        CheckProgramLowered lowered = { 0 };
        const bool ready = input_arena.base != 0 && output.base != 0 && scratch.base != 0 && check_test_types_open(&types) &&
                           parse_initialize(&parser, &parsing, pp_options.language) && parse_translation_unit(&parser, &pp) && parser.errors == 0 &&
                           check_input(&input, &parser, &pp, &types.table, &input_arena, &scratch) &&
                           check_graph_run(&types.table, &input.graph, &input_arena, &scratch, &checking, &stream) && stream.report_count == 0 &&
                           check_initializers_run(&plans, &input, &stream, &parser, &types.table, &input_arena, &scratch) &&
                           check_asms_run(&asms, &input, &stream, &types.table, &input_arena, &scratch);
        const CheckProgramOptions options = { .initializers = &plans, .asms = &asms, .identifiers = &identifiers, .scalar = path == 0 };
        const bool lowered_ok = ready && check_program(&program, &parser, &input, &stream, &types.table, options, &output, &scratch) &&
                                check_program_lower(&lowered, &program, path == 0, &output, &scratch);
        uint32_t total = 0;
        for (uint32_t t = 0; lowered_ok && t < lowered.table_count; t += 1)
        {
            total += lowered.tables[t].count;
        }
        char description[256];
        snprintf(description, sizeof(description), "%s: %u jump tables of %u entries by the %s path (clang -O0's rule: %u of %u)", name,
                 lowered_ok ? lowered.table_count : 0, total, path == 0 ? "scalar" : "vector", tables, entries);
        check_test_expect(test, lowered_ok && lowered.table_count == tables && total == entries, description);
        uint32_t listed = 0;
        bool switches = lowered_ok;
        for (uint32_t c = 0; switches && c < program.control_count; c += 1)
        {
            const bool control_switch = program.controls[c].kind == CHECK_CONTROL_SWITCH;
            uint32_t cases = 0;
            while (control_switch && c + 1 + cases < program.control_count && program.controls[c + 1 + cases].kind == CHECK_CONTROL_CASE)
            {
                cases += 1;
            }
            switches = !control_switch || cases < 4 || (listed < program.switch_count && program.switches[listed] == c);
            listed += control_switch && cases >= 4;
        }
        snprintf(description, sizeof(description), "%s: the %s path's program lists %u switches of four cases or more, as its controls have them", name,
                 path == 0 ? "scalar" : "vector", lowered_ok ? program.switch_count : 0);
        check_test_expect(test, switches && listed == program.switch_count, description);
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
#include "c_check_program_atomic_test.c"
#include "c_check_program_asm_test.c"

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
        const bool lowered_ok = ready && check_program_lower(&lowered, &program, path == 0, &output, &scratch);
        check_test_expect(test, ready && !lowered_ok && lowered.status == CHECK_PROGRAM_INVALID && lowered.functions == 0 && output.length == 0 && scratch.length == 0,
                          "a refused program without its stream or input lowers to nothing");
    }
    arena_release(&output);
    arena_release(&scratch);
}

// The table of every type's class and facts that the bodies read (check_lower_classify_types) against its twin's, on the
// types of random worlds of every kind (the type table's tests' world: pointers, arrays, functions, vectors, atomic
// types, aligned typedefs, complex types of _BitInts, enums and records; and the calling convention's tests' world:
// complex types of every floating type among them): of the whole world and of every count of its types to 300; then with
// the kinds' column and both tables ending at a PROT_NONE page, for every count to 200, so that a read or a write past the
// last type faults. And a function's memory classes from its entries' (check_lower_entry_classes), at the page's end too.
static __attribute__((noinline)) void check_test_program_type_facts(CheckTest* test)
{
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    uint8_t* mappings[3] = { 0 };
    bool guarded = true;
    for (uint32_t i = 0; i < 3; i += 1)
    {
        mappings[i] = mmap(0, 2 * page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        guarded = guarded && mappings[i] != MAP_FAILED && mprotect(mappings[i] + page, page, PROT_NONE) == 0;
    }
    check_test_expect(test, guarded, "the guarded columns of the types' classes");
    for (uint32_t round = 0; round < 2; round += 1)
    {
        AbiTestWorld world;
        const bool opened = abi_test_world_open(&world, 8192);
        check_test_expect(test, opened, "a world of types to classify for the lowering");
        if (opened)
        {
            if (round == 0)
            {
                type_test_world_build(&world.types, 531, 1000);
            }
            else
            {
                abi_test_world_build(&world, 531, 1000);
            }
            const TypeTable* table = &world.types.test.table;
            const uint32_t count = table->types.count;
            uint8_t* columns = malloc(4 * ((uint64_t)count + 1));
            check_test_expect(test, columns != 0 && count > 300, "the world's types and their columns");
            for (uint32_t prefix = 0; columns != 0 && prefix <= count; prefix = prefix < 300 ? prefix + 1 : prefix < count ? count : prefix + 1)
            {
                uint8_t* scalar_classes = columns;
                uint8_t* scalar_facts = columns + count + 1;
                uint8_t* vector_classes = columns + 2 * ((uint64_t)count + 1);
                uint8_t* vector_facts = columns + 3 * ((uint64_t)count + 1);
                memset(vector_classes, 0xa5, prefix + 1);
                memset(vector_facts, 0xa5, prefix + 1);
                check_lower_classify_types_scalar(table, prefix, scalar_classes, scalar_facts);
                check_lower_classify_types(table, prefix, vector_classes, vector_facts);
                const bool same = memcmp(scalar_classes, vector_classes, prefix) == 0 && memcmp(scalar_facts, vector_facts, prefix) == 0 &&
                                  vector_classes[prefix] == 0xa5 && vector_facts[prefix] == 0xa5;
                char description[160];
                snprintf(description, sizeof(description), "the first %u of world %u's %u types: their classes and facts by both paths", prefix, round, count);
                check_test_expect(test, same, description);
            }
            for (uint32_t prefix = 0; columns != 0 && guarded && prefix <= 200 && prefix <= count; prefix += 1)
            {
                // The kinds copied to end at the guard page, read through a copy of the table that has them.
                TypeTable copy = *table;
                uint8_t* kinds = mappings[0] + page - prefix;
                memcpy(kinds, table->types.kind, prefix);
                copy.types.kind = kinds;
                uint8_t* classes = mappings[1] + page - prefix;
                uint8_t* facts = mappings[2] + page - prefix;
                check_lower_classify_types_scalar(table, prefix, columns, columns + count + 1);
                check_lower_classify_types(&copy, prefix, classes, facts);
                char description[160];
                snprintf(description, sizeof(description), "world %u's first %u types' classes and facts, at a page's end", round, prefix);
                check_test_expect(test, memcmp(columns, classes, prefix) == 0 && memcmp(columns + count + 1, facts, prefix) == 0, description);
            }
            free(columns);
            abi_test_world_close(&world);
        }
    }
    // A function's memory classes from its entries' classes (check_lower_entry_classes) against its twin's: columns of
    // every count to 200 ending at the guard page, of random classes but F80, I128 and U128, with one of those at a random
    // place in one column of four, and none in the others.
    for (uint32_t count = 0; guarded && count <= 200; count += 1)
    {
        for (uint32_t round = 0; round < 8; round += 1)
        {
            uint8_t* classes = mappings[0] + page - count;
            for (uint32_t i = 0; i < count; i += 1)
            {
                classes[i] = (uint8_t)(check_test_random(test) % BUSTER_TYPED_CLASS_F80);
            }
            const uint8_t memory[3] = { BUSTER_TYPED_CLASS_F80, BUSTER_TYPED_CLASS_I128, BUSTER_TYPED_CLASS_U128 };
            if (count != 0 && round % 4 != 0)
            {
                classes[check_test_random(test) % count] = memory[round % 3];
            }
            const uint32_t vector = check_lower_entry_classes(classes, count);
            const uint32_t scalar = check_lower_entry_classes_scalar(classes, count);
            const uint32_t expected = count == 0 || round % 4 == 0 ? 0 : round % 3 == 0 ? BUSTER_CHECK_LOWER_ENTRIES_X87 : BUSTER_CHECK_LOWER_ENTRIES_WIDE;
            char description[160];
            snprintf(description, sizeof(description), "the memory classes of %u entries' classes, round %u: %u by the vector path, %u by the scalar one, for %u", count,
                     round, vector, scalar, expected);
            check_test_expect(test, vector == expected && scalar == expected, description);
        }
    }
    for (uint32_t i = 0; i < 3; i += 1)
    {
        if (mappings[i] != MAP_FAILED && mappings[i] != 0)
        {
            munmap(mappings[i], 2 * page);
        }
    }
}

// The x87's encodings, from n: zero, negative zero, a denormal, two pseudo-denormals, one, an unnormal, a pseudo-zero,
// the infinities, a pseudo-infinity, quiet and signaling NaNs, a pseudo-NaN, the largest value and the largest unnormal.
#define BUSTER_CHECK_TEST_X87_ENCODINGS \
    "typedef union{struct{unsigned long m;unsigned short e;}b;long double x;}L;" \
    "static const unsigned long ms[16]={0,0,1,0x8000000000000000UL,0x8000000000000001UL,0x8000000000000000UL,0x4000000000000000UL,0," \
    "0x8000000000000000UL,0x8000000000000000UL,0,0xc000000000000000UL,0x8000000000000001UL,0x4000000000000000UL,0xffffffffffffffffUL," \
    "0x7fffffffffffffffUL};" \
    "static const unsigned short es[16]={0,0x8000,0,0,0x8000,0x3fff,0x3fff,0x3fff,0x7fff,0xffff,0x7fff,0x7fff,0x7fff,0x7fff,0x7ffe,0x7ffe};" \
    "long double make(int n){L l;l.b.m=ms[(unsigned)n%16];l.b.e=es[(unsigned)n%16];return l.x;}"

// The builtins of long double that this compiler's own main.c calls (#148, #129): clang -O0 classifies an x87 value by
// the x87's compares or by its bits, by builtin, which the encodings tell apart. The fpclassify's normal and subnormal
// selectors are constants, since clang evaluates the two in an order its own build decides (check_lower_floating_prepare).
static __attribute__((noinline)) void check_test_program_long_double_builtins(CheckTest* test, const char* clang)
{
    check_test_program_source(test,
        BUSTER_CHECK_TEST_X87_ENCODINGS
        "int tested(int n){long double x=make(n);return __builtin_isnan(x)+2*__builtin_isinf(x)+4*__builtin_isfinite(x)+8*__builtin_signbit(x)+"
        "16*__builtin_fpclassify(0,1,2,3,4,x)+128*(__builtin_isinf_sign(x)+1);}", "long-double-classification-encodings", clang);
    check_test_program_source(test,
        BUSTER_CHECK_TEST_X87_ENCODINGS
        "int seen;int mark(int v){seen=seen*10+v;return v;}"
        "int tested(int n){seen=0;long double x=make(n);L l;l.x=__builtin_fabsl(x);int r=__builtin_fpclassify(mark(1),mark(2),3,4,mark(5),x);"
        "return (int)l.b.e+(int)(l.b.m>>48)*3+r*100000+seen+__builtin_signbit(x)*500000;}", "long-double-fabsl-and-fpclassify-selectors", clang);
    check_test_program_source(test,
        "#pragma STDC FENV_ACCESS ON\n"
        "int clear_fp(void);int fp_state(void);"
        BUSTER_CHECK_TEST_X87_ENCODINGS
        "int tested(int n){long double x=make(n);clear_fp();int a=__builtin_fpclassify(11,22,33,44,55,x);int f=fp_state();"
        "clear_fp();int b=__builtin_isinf_sign(x);int g=fp_state();clear_fp();int c=__builtin_isnan(x);int h=fp_state();"
        "return a*10000+f*1000+(b+1)*100+g*10+h+c*2;}", "long-double-fenv-classification-and-exceptions", clang);
}

static __attribute__((noinline)) void check_test_programs(CheckTest* test, const char* clang)
{
    check_test_program_long_double_builtins(test, clang);
    check_test_control_masks(test);
    check_test_control_walks(test);
    check_test_program_addressed(test);
    check_test_program_refused(test);
    check_test_program_type_facts(test);
    check_test_program_selection(test, clang);
    check_test_program_queries(test, clang);
    check_test_program_region_proofs(test, clang);
    check_test_program_helpers(test);
    check_test_program_atomics(test, clang);
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
    // An address constant cast to an integer type, of a class other than a pointer's (musl's syscalls take one).
    check_test_program_source(test,
        "volatile int lock;long sys(long n,long a){return n*3+(a&7);}long base(void){return (long)&lock;}"
        "int tested(int n){long a=(long)&lock;long b=(long)(&lock+2);int c=(int)(long)&lock;return (int)(b-a)+(c==(int)a)+(base()==a)+(int)sys(n,(long)&lock)-(int)((long)&lock&7);}",
        "address-constants-as-integers", clang);
    check_test_program_source(test,
        "int tested(int n){signed char a=(signed char)(n+125);unsigned char b=(unsigned char)(n+250);"
        "_Bool c=(_Bool)(n*256);return (int)a+(int)b*3+c*700;}", "narrow-values-and-bool", clang);
    // Plain char unsigned (-funsigned-char) and signed, from one source: stores and loads of values past 0x7f,
    // conversions, division, shifts and compares, a switch, character constants, and a char parameter and result.
    static const char plain_char[] =
        "static char table[]={'\\x80','\\xff','a','\\x7f',0};static char pass(char c){return c;}static int widen(char c){return c;}"
        "int tested(int n){char c=(char)(n*37+128);int r=c/3+(c>>1)+(c>100)+widen(pass(c))+'\\xff'+(char)-1;"
        "r+=table[(unsigned)n%5]+(table[(unsigned)n%5]<0)*1000;switch(c){case '\\xff':r+=7;break;case '\\x80':r+=11;break;default:break;}"
        "table[(unsigned)n%5]=c;return r;}";
    check_test_program_source_char(test, plain_char, "plain-char-unsigned", clang, true);
    check_test_program_source_char(test, plain_char, "plain-char-signed", clang, false);
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
    // Jump tables (#136's item 7), each switch swept past its cases on both sides: holes, defaults in the middle and
    // none, fallthrough, GNU ranges, negative and unsigned values at their types' ends, nested tables, and in loops.
    check_test_program_source(test,
        "static int dense(int x){switch(x){case 0:return 3;case 1:return 5;case 2:return 7;case 3:return 11;case 5:return 13;case 7:return 17;default:return -1;}}"
        "static int nodefault(int x){int r=7;switch(x){case 1:r=1;break;case 2:r=2;break;case 3:r=3;break;case 4:r=4;break;}return r;}"
        "int tested(int n){unsigned s=0;for(int i=-6;i<14;i++){s=s*3+(unsigned)dense(i+n);s=s*5+(unsigned)nodefault(i-n);}return (int)s;}",
        "switch-table-dense", clang);
    check_test_program_source(test,
        "static int ranged(int x){int r=0;switch(x){case -7 ... -5:r+=1;case -4:r+=2;break;default:r+=100;case -1:r+=4;break;"
        "case 0 ... 2:r+=8;break;case 4:r+=16;case 5:r+=32;break;}return r;}"
        "int tested(int n){unsigned s=0;for(int i=-10;i<10;i++)s=s*7+(unsigned)ranged(i+n-5);return (int)s;}",
        "switch-table-ranges", clang);
    check_test_program_source(test,
        "static int top(unsigned x){switch(x){case 4294967290u:return 1;case 4294967291u:return 2;case 4294967292u:return 3;"
        "case 4294967294u:return 4;case 4294967295u:return 5;default:return 0;}}"
        "static int utop(unsigned long long x){switch(x){case 18446744073709551610ull:return 1;case 18446744073709551611ull:return 2;"
        "case 18446744073709551613ull:return 3;case 18446744073709551614ull:return 4;case 18446744073709551615ull:return 5;default:return 0;}}"
        "int tested(int n){unsigned s=0;for(int i=-8;i<8;i++){s=s*5+(unsigned)top((unsigned)(i+n-12));s=s*7+(unsigned)utop((unsigned long long)(long long)(i+n-12));}"
        "return (int)s;}",
        "switch-table-unsigned-ends", clang);
    check_test_program_source(test,
        "static int wide(long x){switch(x){case -9223372036854775807L-1:return 1;case -9223372036854775807L:return 2;case -9223372036854775806L:return 3;"
        "case -9223372036854775805L:return 4;case -9223372036854775803L:return 5;default:return 6;}}"
        "static int high(long x){switch(x){case 9223372036854775803L:return 1;case 9223372036854775804L:return 2;case 9223372036854775805L:return 3;"
        "case 9223372036854775806L:return 4;case 9223372036854775807L:return 5;default:return 6;}}"
        "int tested(int n){unsigned s=0;for(int i=0;i<10;i++){s=s*7+(unsigned)wide((-9223372036854775807L-1)+(long)(i+n+3));"
        "s=s*7+(unsigned)high(9223372036854775807L-(long)(i+n+3));s=s*7+(unsigned)wide((long)(i+n));}return (int)s;}",
        "switch-table-long-ends", clang);
    check_test_program_source(test,
        "static int nested(int a,int b){int r=0;switch(a){case 0:switch(b){case 0:r=1;break;case 1:r=2;break;case 2:r=3;break;case 3:r=4;break;default:r=5;}break;"
        "case 1:r=10;break;case 2:r=20;case 3:r+=30;break;default:r=-1;}return r;}"
        "int tested(int n){unsigned s=0;for(int i=0;i<30;i++){if(i%7==6)continue;switch(i%5){case 0:s+=1;break;case 1:s*=3;"
        "case 2:s+=(unsigned)nested(i%4,(i+n)%6);break;case 3:if(i>20)break;s^=0x55;break;case 4:s+=(unsigned)n;break;}}return (int)s;}",
        "switch-table-nested-loop", clang);
    check_test_program_source(test,
        "enum color{red,green,blue,cyan,magenta,yellow};"
        "static int named(enum color c){switch(c){case red:return 1;case green:return 2;case blue:return 3;case cyan:return 4;case yellow:return 6;default:return 0;}}"
        "static int chars(char c){switch(c){case 'a':return 1;case 'b':return 2;case 'c':return 3;case 'e':return 5;case 'f':return 6;default:return 0;}}"
        "int tested(int n){unsigned s=0;for(int i=-3;i<9;i++)s=s*11+(unsigned)named((enum color)(i+n))+(unsigned)chars((char)('a'+i+n));return (int)s;}",
        "switch-table-enum-char", clang);
    // A table of 300 values with holes, every seventh value the default's.
    char big[16384];
    int length = snprintf(big, sizeof(big), "static int big(int x){switch(x){");
    for (int k = 0; k < 300 && length > 0 && (size_t)length < sizeof(big); k += 1)
    {
        length += k % 7 == 3 ? 0 : snprintf(big + length, sizeof(big) - (size_t)length, "case %d:return %d;", k, (k * k) % 1000 + 1);
    }
    length += length > 0 && (size_t)length < sizeof(big) ?
              snprintf(big + length, sizeof(big) - (size_t)length,
                       "default:return -7;}}int tested(int n){unsigned s=0;for(int i=-5;i<305;i++)s=s*31+(unsigned)big(i+n);return (int)s;}") : 0;
    check_test_expect(test, length > 0 && (size_t)length < sizeof(big), "the 300-value switch's source fits");
    check_test_program_source(test, big, "switch-table-300", clang);
    check_test_program_tables(test, big, "switch-table-300-tables", 1, 300);
    // The rule's edges: four clusters or more, a tenth of the range, cases at one place one cluster (`case 1: case 2:`, as
    // LLVM's one block), classes of 32 and 64 bits, ranges of BUSTER_CHECK_LOWER_MAX_TABLE values at most.
    check_test_program_tables(test, "int f(int x){switch(x){case 0:return 3;case 1:return 5;case 2:return 7;case 3:return 11;case 5:return 13;"
                                    "case 7:return 17;default:return -1;}}", "switch-tables-dense", 1, 8);
    check_test_program_tables(test, "int f(int x){switch(x){case 0:return 3;case 1:return 5;case 2:return 7;default:return -1;}}", "switch-tables-three", 0, 0);
    check_test_program_tables(test, "int f(int x){switch(x){case 0:return 3;case 13:return 5;case 26:return 7;case 39:return 9;default:return -1;}}",
                              "switch-tables-tenth", 1, 40);
    check_test_program_tables(test, "int f(int x){switch(x){case 0:return 3;case 14:return 5;case 28:return 7;case 42:return 9;default:return -1;}}",
                              "switch-tables-sparse", 0, 0);
    check_test_program_tables(test, "int f(int x){switch(x){case 1:case 2:case 3:case 4:return 1;default:return 0;}}", "switch-tables-one-place", 0, 0);
    check_test_program_tables(test, "int f(int x){switch(x){case 1:case 2:return 1;case 3:case 4:return 2;case 5:return 3;case 6:return 4;case 7:return 5;default:return 0;}}",
                              "switch-tables-shared-places", 1, 7);
    check_test_program_tables(test, "int f(__int128 x){switch(x){case 0:return 3;case 1:return 5;case 2:return 7;case 3:return 11;default:return -1;}}",
                              "switch-tables-int128", 0, 0);
    check_test_program_tables(test, "int f(int x){switch(x){case 0 ... 70000:return 1;case 70001:return 2;case 70002:return 3;case 70003:return 4;default:return 0;}}",
                              "switch-tables-past-the-most", 0, 0);
    check_test_program_tables(test, "int f(int x){switch(x){case 0 ... 65531:return 1;case 65532:return 2;case 65533:return 3;case 65534:return 4;"
                                    "case 65535:return 5;default:return 0;}}", "switch-tables-the-most", 1, 65536);
    check_test_program_tables(test, "int f(int a,int b){int r=0;switch(a){case 0:switch(b){case 0:r=1;break;case 1:r=2;break;case 2:r=3;break;case 3:r=4;break;"
                                    "default:r=5;}break;case 1:r=10;break;case 2:r=20;case 3:r+=30;break;default:r=-1;}return r;}", "switch-tables-nested", 2, 8);
    // Empty GNU ranges (#536's review), which match nothing and which clang drops before LLVM sees the switch: last by
    // their lower values, where one sized the table short of the case below it and its entry was written past the table's
    // end; first, below the table's lowest value; as the fourth case, which they don't make; and an unsigned one past
    // INT32_MAX. Then the same switches swept past their cases on both sides against clang's build.
    check_test_program_tables(test, "int f(int x){switch(x){case 0:return 3;case 1:return 5;case 2:return 7;case 4:return 9;case 5 ... 3:return 11;default:return -1;}}",
                              "switch-tables-empty-range-last", 1, 5);
    check_test_program_tables(test, "int f(int x){switch(x){case -5 ... -9:return 1;case 0:return 3;case 1:return 5;case 2:return 7;case 3:return 9;default:return -1;}}",
                              "switch-tables-empty-range-first", 1, 4);
    check_test_program_tables(test, "int f(int x){switch(x){case 0:return 3;case 1:return 5;case 2:return 7;case 9 ... 4:return 9;default:return -1;}}",
                              "switch-tables-empty-range-fourth", 0, 0);
    check_test_program_tables(test, "int f(unsigned x){switch(x){case 4294967295u ... 0u:return 1;case 7:return 3;case 8:return 5;case 9:return 7;case 11:return 9;"
                                    "default:return -1;}}", "switch-tables-empty-range-unsigned", 1, 5);
    check_test_program_source(test,
        "static int last(int x){switch(x){case 0:return 3;case 1:return 5;case 2:return 7;case 4:return 9;case 5 ... 3:return 11;default:return -1;}}"
        "static int first(int x){switch(x){case -5 ... -9:return 1;case 0:return 3;case 1:return 5;case 2:return 7;case 3:return 9;default:return -1;}}"
        "static int fourth(int x){switch(x){case 0:return 3;case 1:return 5;case 2:return 7;case 9 ... 4:return 9;default:return -1;}}"
        "static int high(unsigned x){switch(x){case 4294967295u ... 0u:return 1;case 7:return 3;case 8:return 5;case 9:return 7;case 11:return 9;default:return -1;}}"
        "int tested(int n){unsigned s=0;for(int i=-12;i<14;i++){s=s*7+(unsigned)last(i+n);s=s*5+(unsigned)first(i-n);s=s*3+(unsigned)fourth(i+n);"
        "s=s*11+(unsigned)high((unsigned)(i+n));}return (int)s;}",
        "switch-table-empty-ranges", clang);
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
    // The function names' arrays (__func__, __FUNCTION__, __PRETTY_FUNCTION__), private constants as a literal's are:
    // returned, indexed, taken whole, in a static's initializer and at file scope (an empty one). In static functions,
    // whose names the oracle's source keeps (it renames the global ones).
    check_test_program_source(test,
        "static unsigned fold(const char*s){unsigned h=0;while(*s)h=h*31+(unsigned char)*s++;return h;}"
        "static const char*top=__func__;static const char*named(void){return __func__;}"
        "static const char*pretty(int k,const char*(*f)(void)){static const char*own=__FUNCTION__;return k&1?__PRETTY_FUNCTION__:k&2?own:f();}"
        "static int sized(int n){const char(*p)[6]=&__func__;return (int)sizeof(__PRETTY_FUNCTION__)*100+(*p)[n&3]+__func__[4];}"
        "int tested(int n){return (int)(fold(top)^fold(named())*3^fold(pretty(n,named))*5)+sized(n)+n;}",
        "function-name-arrays", clang);
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
    // Objects aligned past 16 bytes, in realigned frames (Tier0Frame.alignment_log2): arrays, structs, a long double and
    // scalars (values aligned past 8 bytes are objects too), their addresses' low bits, which must be 0, in the result.
    check_test_program_source(test,
        "typedef struct{_Alignas(64) int a;long b;}S64;int tested(int n){_Alignas(32) int x=n;_Alignas(4096) char page[100];S64 s;"
        "_Alignas(16) double d=n*0.5;_Alignas(32) long double e=n+0.25L;page[0]=(char)n;page[99]=(char)(n+1);s.a=n*3;s.b=n*5;"
        "unsigned long mask=((unsigned long)&x&31)|((unsigned long)page&4095)|((unsigned long)&s&63)|((unsigned long)&d&15)|((unsigned long)&e&31);"
        "x+=7;d*=3;return x+page[0]+page[99]+s.a+(int)s.b+(int)d+(int)e+(int)mask*1000;}",
        "objects-aligned-past-16", clang);
    // Variable-length arrays aligned past 16 bytes (their storage rounded up within a larger allocation), beside a fixed
    // object aligned to 128 (a realigned frame with a base, r15, since the arrays move rsp), across calls with stack
    // arguments whose outgoing area the allocations keep aligned.
    check_test_program_source(test,
        "long sum8(long a,long b,long c,long d,long e,long f,long g,long h){return a+b+c+d+e+f+g+h;}"
        "typedef struct{_Alignas(32) char c;}T;int tested(int n){_Alignas(128) long fixed[4]={n,n+1,n+2,n+3};long total=0;"
        "for(int round=0;round<3;round++){int count=(n&7)+round+1;_Alignas(64) int v[count];T w[count+1];"
        "for(int i=0;i<count;i++)v[i]=i*n;for(int i=0;i<=count;i++)w[i].c=(char)i;long sum=0;"
        "for(int i=0;i<count;i++)sum+=v[i];for(int i=0;i<=count;i++)sum+=w[i].c;"
        "unsigned long mask=((unsigned long)v&63)|((unsigned long)w&31)|((unsigned long)fixed&127);"
        "total+=sum8(sum,fixed[0],fixed[1],fixed[2],fixed[3],round,(long)sizeof(w),(long)mask*1000);}return (int)(total%1000003);}",
        "vlas-aligned-past-16", clang);
    // A variable-length array aligned to 64 allocated below a guard array, while calls with two stack arguments put the
    // outgoing area 16 bytes past the realigned frame's alignment: its storage, rounded up from the allocation's
    // address, must leave the guard's bytes as they were (the allocation's room, its alignment less 16 bytes more).
    check_test_program_source(test,
        "long sum8(long a,long b,long c,long d,long e,long f,long g,long h){return a+b+c+d+e+f+g+h;}"
        "int tested(int n){long total=0;for(int k=1;k<=4;k++){char guard[16*k];for(int i=0;i<16*k;i++)guard[i]=(char)(i+n);"
        "_Alignas(64) char v[64*k];for(int i=0;i<64*k;i++)v[i]=(char)0x5a;long bad=0;"
        "for(int i=0;i<16*k;i++)bad+=guard[i]!=(char)(i+n);"
        "total+=sum8(bad*100000,v[0],v[64*k-1],k,n,0,0,(long)((unsigned long)v&63)*1000);}return (int)(total%1000003);}",
        "vlas-aligned-past-16-room", clang);
    // Structs aligned to 32 and 64 passed by value (on a stack aligned to them at the call, which realigns the caller's
    // frame) and returned (through a hidden pointer), the parameters' addresses aligned.
    check_test_program_source(test,
        "typedef struct{_Alignas(32) long a;int b;}A32;typedef struct{_Alignas(64) char c;double d;}A64;"
        "A32 make(int n){A32 r={n*2,n+1};return r;}"
        "long take(A32 x,int k,A64 y,A32 z){return x.a+x.b+k+y.c+(long)y.d+z.a*3+z.b+"
        "(long)(((unsigned long)&x&31)|((unsigned long)&y&63)|((unsigned long)&z&31))*1000;}"
        "A64 pass(A64 y,int k){y.c+=(char)k;y.d*=2;return y;}"
        "int tested(int n){A32 p=make(n);A64 q={(char)n,n*0.25};A32 r=make(n+5);A64 t=pass(q,n);return (int)take(p,n,t,r)+(int)t.d;}",
        "aggregates-aligned-past-16-by-value", clang);
    // A variadic definition whose named argument is aligned to 32 (on its caller's stack, aligned to 32 at the call): its
    // incoming area so aligned, and the unnamed arguments after it, each read at its own alignment.
    check_test_program_source(test,
        "typedef struct{_Alignas(32) long a;int b;}A32;typedef struct{_Alignas(64) char c;long d;}A64;"
        "long sum(int count,A32 first,...){__builtin_va_list ap;__builtin_va_start(ap,first);long s=first.a+first.b+(long)((unsigned long)&first&31)*1000;"
        "for(int i=0;i<count;i++){A64 x=__builtin_va_arg(ap,A64);s+=x.c+x.d*(i+1);}long last=__builtin_va_arg(ap,long);__builtin_va_end(ap);return s+last;}"
        "int tested(int n){A32 f={n,n+1};A64 x={(char)n,n*3L};A64 y={(char)(n+1),n*5L};return (int)(sum(2,f,x,y,(long)n*7)%1000003);}",
        "variadic-named-argument-aligned-past-16", clang);
    // Calls whose outgoing areas are aligned to 32 (of clang's callee in the harness, which takes its arguments in place),
    // from functions with no object aligned past 16, whose frames the calls alone realign: two of frames 16 bytes apart,
    // so that a frame left unrealigned would put one of their areas off 32. (After a variable-length array, clang's code
    // rounds the allocation to 16 alone and its call's area may be off 32, where Tier 0's keeps the frame's alignment: a
    // test of its own, tier0_deep_test.c's base, checks that.)
    check_test_program_source(test,
        "struct external_aligned{_Alignas(32) long a;int b;};int external_aligned_by_value(int,struct external_aligned,struct external_aligned);"
        "struct external_aligned gx,gy;int inner(int n){return external_aligned_by_value(n,gx,gy);}"
        "int padded(int n){volatile long pad[2]={n,n};return external_aligned_by_value(n+(int)(pad[1]-pad[0]),gx,gy);}"
        "int tested(int n){gx.a=n;gx.b=n+1;gy.a=n*2;gy.b=n-1;return inner(n)+padded(n+1)*3;}",
        "calls-aligned-past-16", clang);
    // Parameters of types aligned past 8 bytes: objects of their alignment, from a register's part or an 8-byte stack slot
    // (an aligned typedef passes as its base, so a slot is aligned to 8 alone), called at two stack depths 16 bytes apart.
    check_test_program_source(test,
        "typedef int aligned_int __attribute__((aligned(32)));typedef double aligned_double __attribute__((aligned(16)));"
        "long check(aligned_int a,aligned_double b,int c,aligned_int d,long e,long f,long g,aligned_int h){a+=1;h*=2;"
        "return a+(long)b+c+d+e+f+g+h+(long)(((unsigned long)&a&31)|((unsigned long)&b&15)|((unsigned long)&d&31)|((unsigned long)&h&31))*1000;}"
        "int tested(int n){int r=(int)check(n,n*2.5,3,n+1,5,6,7,n*9);{char pad[16+(n&1)*16];pad[0]=0;r+=(int)check(n+pad[0],1.5,n,2,3,4,5,n);}return r;}",
        "parameters-aligned-past-8", clang);
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
    // The 128-bit integers (#133, #136): Tier 0's I128 and U128, their locals and parameters frame objects, every operation
    // and conversion (to and from every integer class and floating type, the floating ones and division as libgcc's calls),
    // constants of every shape (one CONST of either extension, and halves), shifts by counts of every class, updates of
    // locals, globals and memory, calls passing them in register pairs and on the stack and returning them, va_arg of them
    // from registers and the overflow area, and temporaries of them (statement expressions, ?: without its middle operand,
    // a switch on one).
    check_test_program_source(test,
        "typedef __int128 i128;typedef unsigned __int128 u128;static int fold(u128 v){return (int)(unsigned)(v^(v>>29)^(v>>64)^(v>>101));}"
        "int tested(int n){u128 a=((u128)0x0123456789abcdefULL<<64)|(unsigned long long)(n+11)*0x9e3779b97f4a7c15ULL;"
        "i128 b=-(i128)(n+5)*1000000007LL;u128 c=((u128)0xfedcba9876543210ULL<<64)|0xfedcba9876543210ULL;i128 m=-1;"
        "i128 h=(i128)0x8000000000000000ULL;u128 k=(u128)-5;int s=(n&7)*9+5;u128 r=a+c;r^=a*c;r+=a-c;r|=(u128)b&c;"
        "r+=a/(c>>(n&63|1)|1);r^=a%((u128)(n+20)*3|1);i128 q=b/(i128)(n|1)+b%(i128)(n-30|1);r+=(u128)q+(u128)h+k;"
        "r+=a<<s;r^=c>>s;r+=(u128)((i128)c>>s);r+=a<<(i128)s;r+=(u128)1<<(unsigned char)s;r+=~a;r-=-c;"
        "int cmp=(a<c)+2*(b<0)+4*(a>=c)+8*((i128)a>b)+16*(a==a)+32*(b!=m)+64*!b+128*(a&&b)+256*(_Bool)(c-c)+512*(h>m);"
        "double d=(double)a+(double)b;float f=(float)c;long double ld=(long double)b;"
        "r+=(u128)(d*1e-20)+(u128)(f*1e-30f)+(u128)(-ld);i128 back=(i128)(d/3)-(i128)(ld*7)+(i128)(-f*1e-31f);"
        "r+=(u128)back+(u128)(long double)(r>>70);long long l=(long long)b;unsigned u=(unsigned)c;signed char sc=(signed char)a;"
        "unsigned short us=(unsigned short)(a>>3);r+=l+u+sc+us;i128 z=b;z+=n;z-=7;z*=3;z/=-5;z%=1000003;z>>=1;z&=-1;z|=4;z^=2;"
        "u128 w=a;w++;++w;w--;--w;w<<=s;w>>=3;w*=w;w/=(u128)n*n+1;w%=0xfffffffffffffull;"
        "return fold(r)^fold((u128)z)^fold(w)^cmp^fold((u128)back)^(int)(float)(r>>100)^(int)(d*1e-33);}",
        "int128-arithmetic-and-conversions", clang);
    check_test_program_source(test,
        "typedef __int128 i128;typedef unsigned __int128 u128;static int fold(u128 v){return (int)(unsigned)(v^(v>>29)^(v>>64)^(v>>101));}"
        "i128 gi=-12345;u128 gu[3]={1,(u128)1<<100,~(u128)0};struct S{int tag;i128 x;long t;u128 y;};struct S gs={7,-99,9,5};"
        "i128 muladd(i128 a,u128 b,long c){return a*(i128)(b&0xffff)+c;}"
        "u128 stacked(long a,long b,long c,long d,long e,u128 x){return x*7-(u128)(a+2*b+3*c+4*d+5*e);}"
        "u128 sum(int n,...){__builtin_va_list ap;__builtin_va_start(ap,n);u128 r=0;"
        "for(int i=0;i<n;i++){r=r*3+__builtin_va_arg(ap,u128);r+=__builtin_va_arg(ap,int);}__builtin_va_end(ap);return r;}"
        "struct S make(i128 x,int t){struct S s={t,x,t*2,(u128)x*x};return s;}"
        "u128 apply(u128(*f)(long,long,long,long,long,u128),u128 x){return f(1,2,3,4,5,x);}"
        "int tested(int n){u128 v=gu[(unsigned)n%3];gi=gi*7%1000003-n;gu[1]>>=n&7;struct S s=make(gi,n);gs.x+=s.x;gs.y^=s.y;"
        "u128*p=&gu[2];*p-=n;(*p)++;u128 old=(*p)--;i128*q=&gs.x;*q/=3;"
        "u128 r=muladd(gi,v,n)+stacked(n,n,n,n,n,v)+sum(4,v,1,(u128)gi,2,old,3,*p,4)+apply(stacked,gu[1]);"
        "return fold(r)^fold((u128)gs.x)^fold(gs.y)^fold(gu[1])^s.tag^(int)s.t;}",
        "int128-memory-and-calls", clang);
    check_test_program_source(test,
        "typedef __int128 i128;typedef unsigned __int128 u128;static int fold(u128 v){return (int)(unsigned)(v^(v>>29)^(v>>64)^(v>>101));}"
        "u128 pick(u128 a,u128 b){return a?:b;}"
        "int tested(int n){u128 a=(u128)(n+7)<<70;i128 b=-(i128)n*99;u128 x=({u128 t=a;t+=b;t*3;})+({i128 s=b;s-=1;(u128)s;});"
        "i128 y=n>3?b:(i128)a;u128 z=pick(0,a)+pick((u128)n,7);int sw=0;"
        "switch(b){case 0:sw=1;break;case -99:sw=2;break;case -990 ... -100:sw=3;break;default:sw=4;}"
        "u128 c=(u128)(n&1?b:(i128)a)*(n>5?a:(u128)b);u128 e=a*({u128 t=c;t>>=3;t;});"
        "return fold(x)^fold((u128)y)^fold(z)^sw*7^fold(c)^fold(e);}",
        "int128-temporaries", clang);
    // Bit-fields of the 128-bit types, whose bits stay within 8 bytes: read, stored, updated and initialized as others are.
    check_test_program_source(test,
        "typedef __int128 i128;typedef unsigned __int128 u128;struct B{u128 a:20;i128 b:40;int k;u128 c:3;};struct B gb={5,-7,1,2};"
        "int tested(int n){struct B b={(u128)n,(i128)n*-1000,n,1};b.a+=7;b.b*=3;b.c=n;u128 old=b.a++;i128 x=--b.b;gb.b-=n;gb.a^=(u128)n<<5;"
        "return (int)(b.a+old)^(int)(x>>3)^(int)b.c^(int)gb.b^(int)gb.a^b.k;}",
        "int128-bit-fields", clang);
    // Long double values kept in temporaries, which frame objects of their own now hold (as the 128-bit integers').
    check_test_program_source(test,
        "long double pick(long double a,long double b){return a?:b;}"
        "int tested(int n){long double a=n*0.75L;long double x=({long double t=a;t*=3;t;})+({long double s=-a;s+1;});"
        "long double y=pick(0.0L,a)+pick(n,2.5L);long double w=a*({long double q=x;q/=2;q;});"
        "return (int)(x*16)^(int)(y*8)^(int)(w*4);}",
        "long-double-temporaries", clang);
    // Complex values (#133): double's sums, differences, products and quotients (libgcc's __muldc3 and __divdc3 for two
    // complex operands, a real one kept real), negation, the conjugate and equality, with infinities, NaNs and a zero
    // divisor; (inf + inf i) times 1, whose inline product's two NaNs __muldc3 recovers as infinities. Each part's bits
    // folded, so that signed zeros count.
    check_test_program_source(test,
        "typedef _Complex double cd;typedef _Complex float cf;typedef _Complex long double cl;"
        "static unsigned fold(double d){union{double d;unsigned long u;}x;x.d=d;return (unsigned)(x.u^(x.u>>29));}"
        "static unsigned foldz(cd z){return fold(__real__ z)*3u^fold(__imag__ z);}"
        "static unsigned foldf(cf z){return fold(__real__ z)*5u^fold(__imag__ z);}"
        "static unsigned foldl(cl z){return fold((double)__real__ z)*7u^fold((double)__imag__ z)^fold((double)(__real__ z-(double)__real__ z))*11u;}"
        "int tested(int n){volatile double x=n*0.75,zero=0.0;double inf=1.0/zero,nan=zero/zero;"
        "cd a=x+(n-2)*1.0i,b=(n%5-2)+0.5i*n,c=inf+x*1.0i,d=nan+1.0i;"
        "cd s=a+b,t=a-b,u=a*b,v=a/b,w=x+a,y=a-x,o=x-b,p=x*b,q=b/x,r=x/b,m=-a,k=~b;"
        "cd e=c*a,f=a/c,g=d*a,h=c*c,j=a/(b-b),ii=a,kk;__real__ ii=inf;__imag__ ii=inf;kk=ii*(cd)1.0;"
        "unsigned acc=foldz(s)^foldz(t)*5^foldz(u)*7^foldz(v)*11^foldz(w)*13^foldz(y)*17^foldz(p)*19^foldz(q)*23^foldz(r)*29^foldz(m)*31^foldz(k)*37;"
        "acc^=foldz(e)*41^foldz(f)*43^foldz(g)*47^foldz(h)*53^foldz(j)*59^foldz(o)*61^foldz(kk)*67;"
        "acc^=(a==a)+2*(a==b)+4*(a!=b)+8*(d!=d)+16*(x==a)+32*(c==c);"
        "return (int)acc;}",
        "complex-arithmetic", clang);
    // Complex float's and long double's, and mixed: __mulsc3, __divsc3, __mulxc3 and __divxc3, each part's bits folded.
    check_test_program_source(test,
        "typedef _Complex double cd;typedef _Complex float cf;typedef _Complex long double cl;"
        "static unsigned fold(double d){union{double d;unsigned long u;}x;x.d=d;return (unsigned)(x.u^(x.u>>29));}"
        "static unsigned foldz(cd z){return fold(__real__ z)*3u^fold(__imag__ z);}"
        "static unsigned foldf(cf z){return fold(__real__ z)*5u^fold(__imag__ z);}"
        "static unsigned foldl(cl z){return fold((double)__real__ z)*7u^fold((double)__imag__ z)^fold((double)(__real__ z-(double)__real__ z))*11u;}"
        "int tested(int n){volatile float xf=n*0.5f;volatile long double xl=n*0.25L;"
        "cf a=xf+2.0fi,b=(n-3)-1.5fi;cl c=xl+3.0Li,d=1.0L-n*1.0Li;"
        "cf s=a*b,t=a/b,u=a+b*xf,w2=-a;cl v=c*d,w=c/d,y=c-d+xl,z=xl/d;cl mixed=a*c;cd half=b/(cd)c;"
        "return (int)(foldf(s)^foldf(t)*3^foldf(u)*5^foldl(v)*7^foldl(w)*11^foldl(y)*13^foldl(z)*17^foldl(mixed)*19^foldz(half)*23^foldf(w2)*29^(a==b)^(c!=d)*2);}",
        "complex-float-and-long-double", clang);
    // Complex values passed and returned (in xmm registers, in st0 and st1, on the stack), in structs, read by va_arg,
    // through a function pointer, and in arrays.
    check_test_program_source(test,
        "typedef _Complex double cd;typedef _Complex float cf;typedef _Complex long double cl;"
        "static unsigned fold(double d){union{double d;unsigned long u;}x;x.d=d;return (unsigned)(x.u^(x.u>>29));}"
        "static unsigned foldz(cd z){return fold(__real__ z)*3u^fold(__imag__ z);}"
        "static unsigned foldf(cf z){return fold(__real__ z)*5u^fold(__imag__ z);}"
        "static unsigned foldl(cl z){return fold((double)__real__ z)*7u^fold((double)__imag__ z)^fold((double)(__real__ z-(double)__real__ z))*11u;}"
        "struct holder{int tag;cd z;cf w;cl v;};"
        "static cd scale(cd z,double k){return z*k;}"
        "static cf twistf(cf z){return ~z*2.0f;}"
        "static cl twistl(cl z,cl w){return z*w;}"
        "static struct holder make(cd z,int t){struct holder h={t,z,(cf)z,(cl)z*2};return h;}"
        "static cd many(cd a,cd b,cd c,cd d,cd e,cd f,double g,cd h,cf i,cl j){return a+b*c-d/e+f*g+h+i+j;}"
        "static cd sum(int n,...){__builtin_va_list ap;__builtin_va_start(ap,n);cd r=0;for(int i=0;i<n;i++){r+=__builtin_va_arg(ap,cd);r*=1.5;}__builtin_va_end(ap);return r;}"
        "static cl suml(int n,...){__builtin_va_list ap;__builtin_va_start(ap,n);cl r=0;for(int i=0;i<n;i++)r+=__builtin_va_arg(ap,cl);__builtin_va_end(ap);return r;}"
        "static cf sumf(int n,...){__builtin_va_list ap;__builtin_va_start(ap,n);cf r=0;for(int i=0;i<n;i++)r=r*2.0f+__builtin_va_arg(ap,cf);__builtin_va_end(ap);return r;}"
        "static cd mul(cd a,cd b){return a*b;}"
        "static cd quo(cd a,cd b){return a/b;}"
        "static cd(*pick(int k))(cd,cd){return k&1?mul:quo;}"
        "int tested(int n){cd a=n+0.5i,b=1.0-n*1.0i;struct holder h=make(a,n);cd arr[3]={a,b,a*b};arr[n&1]+=arr[2];"
        "unsigned acc=foldz(scale(a,n))^foldf(twistf((cf)b))*3^foldl(twistl(a,b))*5^foldz(h.z)*7^foldf(h.w)*11^foldl(h.v)*13^h.tag;"
        "acc^=foldz(many(a,b,a,b,a,b,n,a,(cf)b,(cl)a))*17^foldz(sum(3,a,b,(cd)n))*19^foldl(suml(2,(cl)a,(cl)b))*23^foldf(sumf(3,(cf)a,(cf)b,(cf)n))*29;"
        "acc^=foldz(pick(n)(a,b))*31^foldz(arr[0])*37^foldz(arr[1])*41;"
        "return (int)acc;}",
        "complex-calls-and-varargs", clang);
    // Compound assignments into complex, double, int, _Bool and long long destinations, conversions between the complex
    // types, ?:, and truth tests in conditions.
    check_test_program_source(test,
        "typedef _Complex double cd;typedef _Complex float cf;typedef _Complex long double cl;"
        "static unsigned fold(double d){union{double d;unsigned long u;}x;x.d=d;return (unsigned)(x.u^(x.u>>29));}"
        "static unsigned foldz(cd z){return fold(__real__ z)*3u^fold(__imag__ z);}"
        "static unsigned foldf(cf z){return fold(__real__ z)*5u^fold(__imag__ z);}"
        "static unsigned foldl(cl z){return fold((double)__real__ z)*7u^fold((double)__imag__ z)^fold((double)(__real__ z-(double)__real__ z))*11u;}"
        "int tested(int n){cd a=n-4.0+(n%3)*1.0i;cf f=a;cl l=a;double d=n;int i=n;_Bool b=0;long long ll=n;"
        "a+=f;a*=l;f/=a;l-=a*1.0i;d*=a;i+=a;b+=a*0.0;ll-=a;"
        "cd z=n>3?a:(cd)f;"
        "int t=0;if(a)t|=1;if(!f)t|=2;if(a&&l)t|=4;if(f||0)t|=8;t|=16*(_Bool)(l-l);while(z){z-=z;t+=32;}"
        "return (int)(foldz(a)^foldf(f)*3^foldl(l)*5^fold(d)*7^i*11^b*13^(unsigned)ll*17^foldz(z)*19^t);}",
        "complex-updates-and-truth", clang);
    // Complex globals, a static local and an array's elements, from static data, updated each call; a compound
    // literal, and folded constants.
    check_test_program_source(test,
        "typedef _Complex double cd;typedef _Complex float cf;typedef _Complex long double cl;"
        "static unsigned fold(double d){union{double d;unsigned long u;}x;x.d=d;return (unsigned)(x.u^(x.u>>29));}"
        "static unsigned foldz(cd z){return fold(__real__ z)*3u^fold(__imag__ z);}"
        "static unsigned foldf(cf z){return fold(__real__ z)*5u^fold(__imag__ z);}"
        "static unsigned foldl(cl z){return fold((double)__real__ z)*7u^fold((double)__imag__ z)^fold((double)(__real__ z-(double)__real__ z))*11u;}"
        "cd ga=1.5+2.5i,garr[2]={1.0i,3.0};static cf gf=-1.0f+0.5fi;cl gl=2.0L-3.0Li;"
        "int tested(int n){static cd counter;counter+=n*1.0i;ga*=1.0+n*0.5i;garr[n&1]+=ga;gf+=n;gl/=1.0L+n;"
        "cd local=(cd){0};local=2.0i*2.0i;cd folded=(1.0+2.0i)*(3.0-1.0i);"
        "return (int)(foldz(ga)^foldz(garr[0])*3^foldz(garr[1])*5^foldf(gf)*7^foldl(gl)*11^foldz(counter)*13^foldz(local)*17^foldz(folded)*19);}",
        "complex-static-data-and-constants", clang);
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
    // GNU C's labels as values (#133): an interpreter's dispatch through a static table of labels' addresses, a computed goto
    // through a local and through ?:, the addresses compared, and a computed goto out of a variable-length array's block.
    check_test_program_source(test,
        "static int run(const unsigned char*c,int n){static const void*const t[]={&&add,&&mul,&&jump,&&end,&&dup};"
        "int acc=n;int pc=0;void*last=&&end;goto*t[c[pc]];"
        "add:acc+=c[pc+1];pc+=2;goto*t[c[pc]];"
        "mul:acc*=c[pc+1];pc+=2;goto*t[c[pc]];"
        "jump:pc=c[pc+1];goto*(pc>8?&&end:t[c[pc]]);"
        "dup:acc+=acc;pc+=1;last=&&dup;goto*t[c[pc]];"
        "end:return last==&&dup?acc+1000:acc;}"
        "int tested(int n){static const unsigned char p[]={0,3,1,5,4,2,9,0,100,3};static const unsigned char q[]={1,2,0,7,3};"
        "void*out=&&done;int r=run(p,n)*31+run(q,n);"
        "for(int i=0;i<3;i++){int v[(n&3)+1];v[0]=i;r+=v[0];if(i==1)goto*out;}r=-1;"
        "done:return r;}",
        "labels-as-values-dispatch", clang);
    // A computed goto's operand is the whole expression after its `*` (#431's review): through ?: of an int's condition,
    // a void *'s sum, a comma and an assignment, each where `*` taken as the operand's prefix would read another value.
    check_test_program_source(test,
        "int tested(int n){void*t[2]={&&even,&&odd};void*p=t[n&1];int r=0,k=0;goto*p+0;"
        "even:r+=2;k+=1;goto*k<3?t[0]:&&odd;"
        "odd:r+=3;k+=1;goto*k<5?t[1]+0:&&last;"
        "last:if(r>12)goto*r++,&&done;goto*p=&&done;"
        "done:return r*100+k*10+(p==&&done);}",
        "labels-as-values-whole-operand", clang);
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
    check_test_program_boundary(test, "__int128 wide(__int128 x){return x+1;}", "wide-primary-program", true, true, 0, 0);
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
                                "typed-asm-operands-and-machine-boundary", true, true, 1, 0);
    check_test_program_boundary_reason(test, "int tested(int n){asm goto(\"\"::::target);return n;target:return n+1;}",
                                       "typed-asm-goto-target", true, false, 0, 0, "asm goto");
    check_test_program_boundary(test, "asm(\".byte 0\");int tested(int n){return n;}",
                                "file-assembly-machine-boundary", true, false, 0, 0);
    check_test_program_boundary(test,
        "extern int missing;int tested(int n,...){__builtin_va_list ap;__builtin_va_start(ap,(missing++,n));"
        "int x=__builtin_va_arg(ap,int);__builtin_va_end(ap);return x;}", "varargs-unevaluated-final-parameter", true, true, 0, 1);
    check_test_program_boundary(test, "_Thread_local int g;int tested(int n){return g+n;}", "executable-tls-reference", true, true, 1, 0);
    check_test_program_boundary(test, "int tested(int n){_Alignas(32) int x=n;return x;}", "automatic-alignment-boundary", true, true, 0, 1);

    check_test_program_boundary(test, "int global;struct S{int x;int*p;};int tested(int n){struct S s={n,&global};return s.x;}",
                                "aggregate-primary-initialization", true, true, 1, 1);
    check_test_program_boundary(test, "struct S{int x;};struct S make(int n);int tested(int n){return make(n).x;}",
                                "aggregate-result-lowering", true, true, 1, 0);
    check_test_program_boundary(test, "struct S{int x;};int take(struct S);int tested(int n){struct S s={n};return take(s);}",
                                "aggregate-argument-lowering", true, true, 1, 1);
    check_test_program_boundary(test, "struct B{unsigned x:3;};int tested(int n){struct B b;b.x=n;return b.x;}",
                                "bit-field-lowering", true, true, 0, 0);
    // Long double: lowered, with its constants' table, its values in temporaries (frame objects of their own) too.
    check_test_program_boundary(test, "long double g;int tested(int n){long double x=n+0.5L;g=x;return g>1;}", "long-double-lowering", true, true, 1, 1);
    check_test_program_boundary(test, "long double tested(int n){return ({long double t=n;t;});}", "long-double-statement-region-value", true, true, 0, 1);
    check_test_program_boundary(test, "int tested(int n){long double x=n;return (x?:2.0L)>1;}", "long-double-omitted-conditional-operand", true, true, 0, 1);
    check_test_program_boundary(test, "int tested(int n){long double x=n;return __builtin_isnan(x);}", "long-double-floating-builtin", true, true, 0, 1);
    check_test_program_boundary(test, "int tested(int n){_Complex long double z=n;return __real__ z>1;}", "complex-long-double", true, true, 0, 1);
    // The complex types check_lower_complex doesn't take are refused by what they need: GNU's complex integer types,
    // and complex types of the floating types other than float, double and long double (clang 23 has no _Float128 in
    // C; __float128 is its spelling).
    check_test_program_boundary_reason(test, "int tested(int n){_Complex int z=n;return __real__ z>1;}", "complex-int", true, false, 0, 1, "complex integer types");
    check_test_program_boundary_reason(test, "int tested(int n){_Complex __float128 z=n;return __real__ z>1;}", "complex-float128", true, false, 0, 1,
                                       "complex types other than _Complex float, double and long double");
    check_test_program_boundary_reason(test, "int tested(int n){_Complex _Float16 z=n;return __real__ z>1;}", "complex-float16", true, false, 0, 1,
                                       "complex types other than _Complex float, double and long double");
    // The 128-bit integers: lowered, but _BitInt(128) and a bit-field of them spread over more than 8 bytes.
    check_test_program_boundary(test, "__int128 g;int tested(int n){__int128 x=n;g=x*x;return g>1;}", "int128-lowering", true, true, 1, 1);
    check_test_program_boundary(test, "int tested(int n){_BitInt(128) x=n;return x>1;}", "bit-int-128", true, false, 0, 1);
    check_test_program_boundary(test, "struct W{unsigned __int128 x:100;};int tested(int n){struct W w;w.x=n;return w.x>1;}", "int128-bit-field-past-8-bytes", true,
                                false, 0, 0);
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
    check_test_program_asm(test, clang);
    check_test_program_build_release();
}

#endif
