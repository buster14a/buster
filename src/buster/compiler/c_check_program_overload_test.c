// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright 2026 Buster contributors.
//
// #821: ABI names, selected calls and contextual addresses. Names are independently stated Itanium encodings;
// emitted fixtures also compare symbols and execution with Clang. Both kernels and default/lazy emission run.
#ifndef BUSTER_C_CHECK_PROGRAM_OVERLOAD_TEST_INCLUDED
#define BUSTER_C_CHECK_PROGRAM_OVERLOAD_TEST_INCLUDED

static void check_test_mangling(CheckTest* test)
{
    static const char source[] =
        "#define O __attribute__((overloadable))\n"
        "struct S{int n;};enum E{e};typedef int aligned __attribute__((aligned(16)));"
        "typedef int v4 __attribute__((vector_size(16)));typedef int x4 __attribute__((ext_vector_type(4)));"
        "void a(_Complex double,_Complex double) O;"
        "void b(const int*,const int*,int*) O;"
        "void c(int(*)(int),int(*)(int)) O;"
        "void d(v4,v4,x4,x4) O;"
        "void e1(_Atomic(int)*,_Atomic(int)*) O;"
        "void f(_BitInt(17),_BitInt(17),unsigned _BitInt(17)) O;"
        "void g(struct S,struct S,enum E,enum E) O;"
        "void h(const int(*)[2],const int(*)[2],const int*) O;"
        "void i(aligned*,int*) O;"
        "void j(__fp16*,_Float16*,__bf16*,__float128*) O;"
        "void k(int(*)(void),int(*)()) O;"
        "void l(int(__attribute__((ms_abi))*)(int),int(__attribute__((ms_abi))*)(int)) O;"
        "void variadic(int,...) O;void none(void) O;"
        "void quals(int *const*,int**,int *const*) O;"
        "void scalars(_Bool,char,signed char,unsigned char,short,unsigned short,int,unsigned int,"
        "long,unsigned long,long long,unsigned long long,__int128,unsigned __int128,float,double,long double) O;"
        "void complex_types(float _Complex,double _Complex,long double _Complex) O;"
        "void cv(const volatile int*,int *restrict*) O;"
        "void arrays(int(*)[],int(*)[3],int(*)[]) O;";
    static const char* const expected[] = {
        "_Z1aCdS_",
        "_Z1bPKiS0_Pi",
        "_Z1cPFiiES0_",
        "_Z1dDv4_iS_Dv4_iS0_",
        "_Z2e1PU7_AtomiciS0_",
        "_Z1fDB17_S_DU17_",
        "_Z1g1SS_1ES0_",
        "_Z1hPA2_KiS1_PS_",
        "_Z1iPiS_",
        "_Z1jPDhPDF16_PDF16bPg",
        "_Z1kPFivEPFiE",
        "_Z1lPU6ms_abiFiiES0_",
        "_Z8variadiciz",
        "_Z4nonev",
        "_Z5qualsPKPiPS_S1_",
        "_Z7scalarsbcahstijlmxynofde",
        "_Z13complex_typesCfCdCe",
        "_Z2cvPVKiPrPi",
        "_Z6arraysPA_iPA3_iS0_",
    };
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    const PPOptions preprocessing = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true };
    const ParseOptions parsing = { .extension_metadata = true };
    bool ready = intern_initialize(&identifiers) && pp_initialize(&pp, &preprocessing, &identifiers) &&
                 parse_initialize(&parser, &parsing, preprocessing.language) &&
                 pp_run_source(&pp, "overload-mangling.c", (const uint8_t*)source, sizeof(source) - 1) && parse_translation_unit(&parser, &pp);
    check_test_expect(test, ready, "overload mangling signatures parse");
    for (uint32_t path = 0; ready && path < 2; path += 1)
    {
        CheckTestTypes types = { 0 };
        Arena retained = arena_reserve(1u << 24);
        Arena output = arena_reserve(1u << 20);
        Arena scratch = arena_reserve(1u << 24);
        CheckInput input = { 0 };
        parser.options.scalar = path == 0;
        bool checked = check_test_types_open(&types) && retained.base != NULL && output.base != NULL && scratch.base != NULL &&
                       check_input(&input, &parser, &pp, &types.table, &retained, &scratch) && input.report_count == 0;
        check_test_expect(test, checked && input.graph.overload_candidate_count == BUSTER_ARRAY_LENGTH(expected), "overload mangling signature identities are retained");
        CheckMangleSource mangling = {
            .input = &input,
            .parser = &parser,
            .identifiers = &identifiers,
            .types = &types.table,
            .output = &output,
            .scalar = path == 0,
        };
        for (uint32_t i = 0; checked && i < input.graph.overload_candidate_count && i < BUSTER_ARRAY_LENGTH(expected); i += 1)
        {
            const CheckOverloadCandidate* candidate = input.graph.overload_candidates + i;
            String8 name = { 0 };
            const CheckMangleStatus status = check_mangle_name(&mangling, candidate->type, parser.tokens.names[candidate->declaration], &scratch, &name);
            const bool matches = status == CHECK_MANGLE_OK && name.length == strlen(expected[i]) && memcmp(name.pointer, expected[i], name.length) == 0;
            char description[256];
            snprintf(description, sizeof(description), "overload ABI name %s on %s path (got %.*s, status %u)", expected[i], path == 0 ? "scalar" : "vector",
                     (int)name.length, name.pointer != NULL ? name.pointer : "", status);
            check_test_expect(test, matches && scratch.length == 0, description);
        }
        if (checked && input.graph.overload_candidate_count != 0)
        {
            const CheckOverloadCandidate* candidate = input.graph.overload_candidates;
            const uint32_t identifier = parser.tokens.names[candidate->declaration];
            TypeId pointer = BUSTER_TYPE_INT;
            for (uint32_t i = 0; i < 1024; i += 1)
            {
                pointer = type_pointer(&types.table, pointer);
            }
            const TypeId deep = type_function(&types.table, BUSTER_TYPE_VOID, &pointer, 1, BUSTER_TYPE_FLAG_PROTOTYPE);
            String8 name = { 0 };
            const CheckMangleStatus deep_status = check_mangle_name(&mangling, deep, identifier, &scratch, &name);
            bool matches = deep_status == CHECK_MANGLE_OK && name.length == 1029 && memcmp(name.pointer, "_Z1a", 4) == 0 && name.pointer[1028] == 'i';
            for (uint32_t i = 4; matches && i < 1028; i += 1)
            {
                matches = name.pointer[i] == 'P';
            }
            check_test_expect(test, matches && scratch.length == 0, "deep overload signature uses a bounded explicit stack");
            TypeId parameters[41];
            pointer = BUSTER_TYPE_INT;
            for (uint32_t i = 0; i < 40; i += 1)
            {
                pointer = type_pointer(&types.table, pointer);
                parameters[i] = pointer;
            }
            parameters[40] = pointer;
            const TypeId repeated = type_function(&types.table, BUSTER_TYPE_VOID, parameters, BUSTER_ARRAY_LENGTH(parameters), BUSTER_TYPE_FLAG_PROTOTYPE);
            const CheckMangleStatus repeated_status = check_mangle_name(&mangling, repeated, identifier, &scratch, &name);
            check_test_expect(test, repeated_status == CHECK_MANGLE_OK && name.length >= 4 && memcmp(name.pointer + name.length - 4, "S12_", 4) == 0,
                              "overload substitution ids pass multiple vector groups and use base 36");
            Arena exhausted = arena_reserve(32);
            exhausted.length = 17;
            exhausted.capacity = 17;
            const uint64_t mark = output.length;
            const CheckMangleStatus no_work = check_mangle_name(&mangling, deep, identifier, &exhausted, &name);
            check_test_expect(test, no_work == CHECK_MANGLE_RESOURCE && name.length == 0 && exhausted.length == 17 && output.length == mark,
                              "overload mangling worklist exhaustion publishes no partial name");
            mangling.output = &exhausted;
            const CheckMangleStatus no_name = check_mangle_name(&mangling, deep, identifier, &scratch, &name);
            check_test_expect(test, no_name == CHECK_MANGLE_RESOURCE && name.length == 0 && exhausted.length == 17 && scratch.length == 0,
                              "overload name allocation exhaustion restores scratch");
            arena_release(&exhausted);
        }
        check_test_types_close(&types);
        arena_release(&scratch);
        arena_release(&output);
        arena_release(&retained);
    }
    parse_release(&parser);
    pp_release(&pp);
    intern_release(&identifiers);
}

// Exercise actual driver inputs, not only the in-process fixtures: both directions of Clang/Buster linkage,
// Clang's installed tgmath header, scalar/default object identity, and the direct executable path.
static void check_test_overload_driver(CheckTest* test, const char* clang)
{
    static const char provider[] =
        "#define O __attribute__((overloadable))\n"
        "int choose(int x) O{return x+5;}float choose(float x) O{return x+1;}double choose(double x) O{return x+2;}"
        "long double choose(long double x) O{return x+3;}double _Complex choose(double _Complex x) O{return x+4+2.0i;}"
        "int apply(int(*a)(int),int(*b)(int),int x) O{return a(x)+b(x);}"
        "int ptr(const int*a,const int*b) O{return *a+*b;}";
    static const char client[] =
        "#define O __attribute__((overloadable))\n"
        "int choose(int) O;float choose(float) O;double choose(double) O;long double choose(long double) O;"
        "double _Complex choose(double _Complex) O;int apply(int(*)(int),int(*)(int),int) O;int ptr(const int*,const int*) O;"
        "float(*saved)(float)=choose;static int plus(int x){return x+9;}"
        "int main(void){int n=3;double _Complex z=choose(1.0+2.0i);"
        "return !(choose(2)==7&&saved(1.0f)==2.0f&&choose(1.0)==3.0&&choose(1.0L)==4.0L&&"
        "__real__ z==5.0&&__imag__ z==4.0&&apply(plus,plus,1)==20&&ptr(&n,&n)==6);}";
    static const char tgmath[] =
        "#include <stdio.h>\n#include <tgmath.h>\n"
        "int main(void){float f=2.0f;double d=2.0;long double ld=2.0L;double complex z=1.0+1.0*I;"
        "printf(\"%g %g %Lg %g\\n\",sqrt(f),sqrt(d),sqrt(ld),creal(sqrt(z)));"
        "printf(\"%zu %zu %zu\\n\",sizeof(sqrt(f)),sizeof(sqrt(d)),sizeof(sqrt(ld)));"
        "printf(\"%g %g %g\\n\",pow(f,2),fabs(-d),exp(1));return 0;}";
    char directory[] = "/tmp/buster-overload-driver-XXXXXX";
    const bool created = mkdtemp(directory) != NULL;
    char sources[3][160];
    char objects[4][160];
    char executable[160];
    char out[160];
    char err[160];
    snprintf(executable, sizeof(executable), "%s/program", directory);
    snprintf(out, sizeof(out), "%s/out", directory);
    snprintf(err, sizeof(err), "%s/err", directory);
    const char* texts[] = { provider, client, tgmath };
    bool valid = created && clang != NULL;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(sources); i += 1)
    {
        snprintf(sources[i], sizeof(sources[i]), "%s/input%u.c", directory, i);
        valid = valid && parse_test_write(sources[i], texts[i], strlen(texts[i]));
    }
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(objects); i += 1)
    {
        snprintf(objects[i], sizeof(objects[i]), "%s/object%u.o", directory, i);
        char* compile[] = { i < 2 ? "/proc/self/exe" : (char*)clang, "-std=gnu17", "-c", sources[i % 2], "-o", objects[i], NULL };
        valid = valid && object_test_run(compile, out, err) == 0;
    }
    for (uint32_t i = 0; valid && i < 2; i += 1)
    {
        char* link[] = { (char*)clang, objects[i == 0 ? 0 : 2], objects[i == 0 ? 3 : 1], "-o", executable, NULL };
        char* run[] = { executable, NULL };
        valid = object_test_run(link, out, err) == 0 && object_test_run(run, out, err) == 0;
    }
    check_test_expect(test, valid, "external overloads link and execute in both Clang/Buster directions");
    Arena arena = arena_reserve(1u << 24);
    char* reference[] = { (char*)clang, "-std=gnu17", sources[2], "-lm", "-o", executable, NULL };
    char* run[] = { executable, NULL };
    valid = valid && arena.base != NULL && object_test_run(reference, out, err) == 0 && object_test_run(run, out, err) == 0;
    const String8 expected = valid ? object_test_read_file(&arena, out) : (String8){ 0 };
    valid = valid && expected.pointer != NULL && expected.length != 0;
    String8 original = { 0 };
    for (uint32_t path = 0; valid && path < 3; path += 1)
    {
        char* object[] = { "/proc/self/exe", "-std=gnu17", "-c", sources[2], "-o", objects[0], path == 1 ? "--scalar" : NULL, NULL };
        char* link[] = { (char*)clang, objects[0], "-lm", "-o", executable, NULL };
        char* direct[] = { "/proc/self/exe", "-std=gnu17", sources[2], "-lm", "-o", executable, NULL };
        if (path != 2)
        {
            valid = object_test_run(object, out, err) == 0;
            const String8 bytes = valid ? object_test_read_file(&arena, objects[0]) : (String8){ 0 };
            valid = valid && bytes.pointer != NULL && bytes.length != 0;
            if (path == 0)
            {
                original = bytes;
            }
            else
            {
                check_test_expect(test, valid && bytes.length == original.length && memcmp(bytes.pointer, original.pointer, bytes.length) == 0,
                                  "tgmath scalar/default driver objects are byte-identical");
            }
            valid = valid && object_test_run(link, out, err) == 0;
        }
        else
        {
            valid = object_test_run(direct, out, err) == 0;
        }
        valid = valid && object_test_run(run, out, err) == 0;
        const String8 result = valid ? object_test_read_file(&arena, out) : (String8){ 0 };
        valid = valid && result.pointer != NULL && result.length == expected.length && memcmp(result.pointer, expected.pointer, expected.length) == 0;
        check_test_expect(test, valid, path != 2 ? "installed tgmath macros compile, link and run as Clang does" : "tgmath direct executable runs as Clang does");
    }
    if (!valid && created)
    {
        const String8 diagnostics = object_test_read_file(&arena, err);
        fprintf(stderr, "test-checker: overload driver files retained in %s\n%.*s", directory, (int)diagnostics.length,
                diagnostics.pointer != NULL ? diagnostics.pointer : "");
    }
    if (valid && created)
    {
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(sources); i += 1)
        {
            unlink(sources[i]);
        }
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(objects); i += 1)
        {
            unlink(objects[i]);
        }
        unlink(executable);
        unlink(out);
        unlink(err);
        rmdir(directory);
    }
    arena_release(&arena);
}

static void check_test_program_overloads(CheckTest* test, const char* clang)
{
    check_test_mangling(test);
    check_test_overload_driver(test, clang);
    check_test_program_selection_case(test,
        "#define O __attribute__((overloadable))\n"
        "static int pick(int) O;static double pick(double) O;"
        "static int pick(int n) O{return n+3;}static double pick(double n) O{return n+7;}"
        "int tested(int n){return pick(n)+(int)pick((double)n);}",
        "overload-prototypes-definitions-direct-calls", 3, 0, clang, true);
    check_test_program_selection_case(test,
        "#define O __attribute__((overloadable))\n"
        "static int pick(int n) O{return n+3;}static double pick(double n) O{return n+7;}"
        "static int(*saved)(int)=pick;static double(*other)(double)=pick;"
        "static int(*give(void))(int){return pick;}int external_apply(int(*)(int),int);"
        "int tested(int n){int(*p)(int)=pick;int(*q)(int);q=&pick;struct S{int(*f)(int);}s={pick};"
        "return saved(n)+(int)other(n)+give()(n)+p(n)+q(n)+s.f(n)+external_apply(pick,n)+((int(*)(int))pick)(n);}",
        "overload-addresses-static-local-aggregate-cast-return-argument", 4, 2, clang, true);
    check_test_program_selection_case(test,
        "#define O __attribute__((overloadable))\n"
        "static int pick(int) __asm__(\"renamed_pick\") O;static int pick(int n) O{static int offset=3;return n+offset;}"
        "static double pick(double n) O{static int offset=7;return n+offset;}"
        "int tested(int n){return pick(n)+(int)pick((double)n);}",
        "overload-asm-label-and-distinct-static-local-names", 3, 2, clang, true);
    check_test_program_selection_case(test,
        "#define O __attribute__((overloadable))\n"
        "static inline float tg(float x) O{return x+1;}static inline double tg(double x) O{return x+2;}"
        "static inline long double tg(long double x) O{return x+3;}"
        "static inline double _Complex tg(double _Complex x) O{return x+(4.0+2.0i);}"
        "int tested(int n){float f=tg((float)n);double d=tg((double)n);long double l=tg((long double)n);"
        "double _Complex z=tg((double _Complex)n);return (int)(f+d+l+__real__ z+__imag__ z)+sizeof(tg(f))+sizeof(tg(d))+sizeof(tg(l));}",
        "tgmath-shaped-real-and-complex-overloads", 5, 0, clang, true);
    check_test_program_selection_case(test,
        "#define O __attribute__((overloadable))\n"
        "struct S{int n;};static int pick(struct S*s,const int*p,const int*q) O{return s->n+*p+*q;}"
        "static int pick(int(*f)(int),int n) O{return f(n);}static int plus(int n){return n+9;}"
        "int tested(int n){struct S s={n};int p=3;return pick(&s,&p,&p)+pick(plus,n);}",
        "overload-nominal-pointer-and-function-pointer-signatures", 4, 0, clang, true);
    check_test_program_selection_run(test,
        "typedef struct{int n;} S;int f(S*) __attribute__((overloadable));int tested(int n){return f((S*)0)+n;}",
        "overload-anonymous-tag-fails-closed", 1, 0, CHECK_PROGRAM_UNSUPPORTED, NULL, NULL, NULL, false);
    check_test_program_selection_run(test,
        "int tested(int n){struct S{int n;};int f(struct S*) __attribute__((overloadable));return f((struct S*)0)+n;}",
        "overload-local-tag-fails-closed", 1, 0, CHECK_PROGRAM_UNSUPPORTED, NULL, NULL, NULL, false);
    check_test_program_selection_case(test,
        "typedef struct{int n;} S;int f(S*) __asm__(\"external_named\") __attribute__((overloadable));"
        "int tested(int n){return f((S*)0)+n;}",
        "overload-asm-label-bypasses-unsupported-signature-mangling", 1, 0, clang, false);
}

#endif
