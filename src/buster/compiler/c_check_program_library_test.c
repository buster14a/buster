// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

//
// #148's library builtins (c_builtin_library.c, c_check_builtin_library.c): the lists against the pinned clang 23.1.4's table, the
// folds' constant contexts against clang's acceptance, and programs of each family against clang -O0's build, run
// natively by the harness (check_test_program_source), by both paths. c_checker_test.c includes it after the program
// harness's sections; test-checker runs it, and `test-checker --program-library` alone. test-checker-driver has the
// driver's side: implicit declarations' diagnostics, -fno-builtin and the refusals by name.

#include "buster/compiler/test_paths.h"
#ifndef BUSTER_C_CHECK_PROGRAM_LIBRARY_TEST_INCLUDED
#define BUSTER_C_CHECK_PROGRAM_LIBRARY_TEST_INCLUDED

// Appends a name to a bounded list of the first few that a check found, for its failure's description.
static void check_test_library_note(char* list, size_t capacity, uint32_t* count, const char* name)
{
    const size_t used = strlen(list);
    if (*count < 6 && used + strlen(name) + 2 < capacity)
    {
        snprintf(list + used, capacity - used, "%s%s", used != 0 ? " " : "", name);
    }
    *count += 1;
}

// The lists against clang's table: every listed library builtin is a row with attribute F of all of clang's languages
// without features, its name __builtin_ and its function's; every plain one a row with attribute f, GNU's alone exactly
// where the list says, without custom checking; every fold's prototype its row's encoding, of a row with attribute E;
// each list in its names' order, within its bound, and each lookup finding exactly the listed names, at an unreadable
// page too. Then every row of C's languages: an F row of all languages without features is one __has_builtin advertises
// (the catalog's, the fortified builtins', the list's, powi's), or one whose prototype the checker doesn't decode
// (custom checking's);
// and an f row is listed.
static __attribute__((noinline)) void check_test_library_tables(CheckTest* test)
{
    bool ordered = true;
    bool rows = true;
    for (uint32_t l = 0; l < CBUILTIN_LIBRARY_COUNT; l += 1)
    {
        char name[64];
        const int length = snprintf(name, sizeof(name), "__builtin_%s", cbuiltin_library_names[l]);
        const BuiltinIntrinsicInfo* info = builtin_intrinsic_lookup(name, (uint32_t)length);
        rows &= info != NULL && memchr(info->attributes, 'F', info->attributes_length) != NULL && strcmp(info->language_token, "ALL_LANGUAGES") == 0 &&
                info->features_length == 0 && !info->custom_typechecking && cbuiltin_library_lengths[l] == strlen(cbuiltin_library_names[l]);
        rows &= (uint32_t)length <= BUSTER_CBUILTIN_LIBRARY_MAX_NAME && cbuiltin_library_id(name, (uint32_t)length) == (CBuiltinLibraryId)l &&
                cbuiltin_library_source((const uint8_t*)name, (uint32_t)length);
        ordered &= l == 0 || strcmp(cbuiltin_library_names[l - 1], cbuiltin_library_names[l]) < 0;
    }
    check_test_expect(test, rows && ordered, "library builtins: each listed call is clang's F row of its name, in order and within the bound");
    bool plains = true;
    for (uint32_t p = 0; p < BUSTER_CBUILTIN_LIBRARY_PLAIN_COUNT; p += 1)
    {
        const CBuiltinLibraryPlain* plain = cbuiltin_library_plains + p;
        const BuiltinIntrinsicInfo* info = builtin_intrinsic_lookup(plain->name, plain->length);
        plains &= info != NULL && memchr(info->attributes, 'f', info->attributes_length) != NULL && !info->custom_typechecking &&
                  info->features_length == 0 &&
                  strcmp(info->language_token, cbuiltin_library_plain_languages(plain)) == 0;
        plains &= plain->length == strlen(plain->name) && plain->length <= BUSTER_CBUILTIN_LIBRARY_PLAIN_MAX_NAME &&
                  cbuiltin_library_plain(plain->name, plain->length) == plain && (p == 0 || strcmp(cbuiltin_library_plains[p - 1].name, plain->name) < 0);
    }
    check_test_expect(test, plains, "library builtins: each listed plain name is clang's f row of its languages, in order and within the bound");
    bool folds = true;
    for (uint32_t f = 0; f < BUSTER_CBUILTIN_LIBRARY_PLAIN_FOLDS; f += 1)
    {
        const CBuiltinLibraryPlainFold* fold = cbuiltin_library_plain_folds + f;
        const uint32_t length = (uint32_t)strlen(fold->name);
        const BuiltinIntrinsicInfo* info = builtin_intrinsic_lookup(fold->name, length);
        // A fold's row has attribute E; a computed one's (#967, #1373) none, or C++'s constant evaluation's alone (memcpy's and
        // memmove's), and its library builtin is its __builtin_ form's where the list has that (__builtin_fmax's,
        // __builtin_abs's), none where that form is another family's (__builtin_fabs's, __builtin_memcpy's).
        const bool inline_call = fold->fold == CBUILTIN_LIBRARY_FOLD_NONE;
        const bool cplusplus = fold->computed == CBUILTIN_LIBRARY_COMPUTED_COPY || fold->computed == CBUILTIN_LIBRARY_COMPUTED_MOVE;
        char builtin[64];
        const int builtin_length = snprintf(builtin, sizeof(builtin), "__builtin_%s", fold->name);
        const CBuiltinLibraryId own = cbuiltin_library_id(builtin, (uint32_t)builtin_length);
        folds &= info != NULL && strcmp(info->encoding, fold->prototype) == 0 &&
                 (inline_call ? !info->constant_evaluable_attribute || cplusplus : info->constant_evaluable_attribute) &&
                 cbuiltin_library_plain(fold->name, length) != NULL && cbuiltin_library_plain_fold(fold->name, length) == f &&
                 (inline_call ? fold->computed != CBUILTIN_LIBRARY_COMPUTED_NONE && fold->library == own :
                                fold->library == CBUILTIN_LIBRARY_COUNT && fold->computed == CBUILTIN_LIBRARY_COMPUTED_NONE);
    }
    // Every fold of a listed call is of a row with attribute E, which clang's evaluator folds.
    for (uint32_t l = 0; l < CBUILTIN_LIBRARY_COUNT; l += 1)
    {
        bool wide = false;
        char name[64];
        const int length = snprintf(name, sizeof(name), "__builtin_%s", cbuiltin_library_names[l]);
        const BuiltinIntrinsicInfo* info = builtin_intrinsic_lookup(name, (uint32_t)length);
        folds &= cbuiltin_library_fold((CBuiltinLibraryId)l, &wide) == CBUILTIN_LIBRARY_FOLD_NONE || (info != NULL && info->constant_evaluable_attribute);
    }
    check_test_expect(test, folds, "library builtins: each fold is of a constexpr row, the plain ones' prototypes their rows' encodings");
    // Every row of C's languages.
    const PPOptions gnu = {
        .language = {
            .standard = 2017,
            .gnu = true,
        },
    };
    uint32_t advertised = 0;
    uint32_t undecoded = 0;
    uint32_t unaccounted = 0;
    uint32_t unlisted = 0;
    uint32_t plain_unaccounted = 0;
    char missing[160] = { 0 };
    char missing_plain[160] = { 0 };
    for (uint32_t i = 0; i < BUSTER_BUILTIN_INTRINSIC_COUNT; i += 1)
    {
        const BuiltinIntrinsicInfo* info = builtin_intrinsic_at(i);
        const bool all = strcmp(info->language_token, "ALL_LANGUAGES") == 0;
        const bool c = all || strcmp(info->language_token, "ALL_GNU_LANGUAGES") == 0;
        if (all && info->features_length == 0 && memchr(info->attributes, 'F', info->attributes_length) != NULL)
        {
            const bool query = pp_implemented_builtin_query(&gnu, (const uint8_t*)info->name, info->name_length) != 0;
            const bool half = memchr(info->encoding, 'h', info->encoding_length) != NULL;
            const bool file = memchr(info->encoding, 'P', info->encoding_length) != NULL;
            advertised += query;
            undecoded += !query && (half || file || info->custom_typechecking);
            if (!query && !half && !file && !info->custom_typechecking)
            {
                check_test_library_note(missing, sizeof(missing), &unaccounted, info->name);
            }
        }
        if ((c || strcmp(info->language_token, "C23_LANG") == 0) && memchr(info->attributes, 'f', info->attributes_length) != NULL &&
            cbuiltin_library_plain(info->name, info->name_length) == NULL)
        {
            unlisted += 1;
            check_test_library_note(missing_plain, sizeof(missing_plain), &plain_unaccounted, info->name);
        }
    }
    char description[384];
    snprintf(description, sizeof(description),
             "library builtins: clang's F rows are %u advertised and %u of prototypes that don't decode, with %u left (%s)",
             advertised, undecoded, unaccounted, missing);
    check_test_expect(test, unaccounted == 0, description);
    snprintf(description, sizeof(description), "library builtins: clang's f rows of C are %u listed and %u not, %u of them without a reason (%s)",
             BUSTER_CBUILTIN_LIBRARY_PLAIN_COUNT, unlisted, plain_unaccounted, missing_plain);
    check_test_expect(test, plain_unaccounted == 0, description);
    // Lookups of near misses and of bounded names at an unreadable page find nothing and read nothing past their slices.
    static const char* const misses[] = { "", "__builtin_", "__builtin_si", "__builtin_sinx", "__builtin_SIN", "_builtin_sin", "sin", "__builtin_powix", "__builtin_alloca" };
    bool missed = cbuiltin_library_id(NULL, 4) == CBUILTIN_LIBRARY_COUNT && !cbuiltin_library_source(NULL, 4) && cbuiltin_library_plain(NULL, 3) == NULL &&
                  cbuiltin_library_plain_fold(NULL, 6) == BUSTER_CBUILTIN_LIBRARY_PLAIN_FOLDS;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(misses); i += 1)
    {
        const uint32_t length = (uint32_t)strlen(misses[i]);
        missed &= cbuiltin_library_id(misses[i], length) == CBUILTIN_LIBRARY_COUNT && !cbuiltin_library_source((const uint8_t*)misses[i], length) &&
                  cbuiltin_library_libgcc(misses[i], length) == BUSTER_CBUILTIN_LIBRARY_LIBGCC;
    }
    missed &= cbuiltin_library_plain("sinx", 4) == NULL && cbuiltin_library_plain("si", 2) == NULL && cbuiltin_library_plain("va_starts", 9) == NULL && cbuiltin_library_plain("va_arg", 6) == NULL &&
              cbuiltin_library_plain("", 0) == NULL;
    const long page = sysconf(_SC_PAGESIZE);
    uint8_t* mapping = page > 0 ? mmap(NULL, (size_t)page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) : MAP_FAILED;
    const bool guarded = mapping != MAP_FAILED && mprotect(mapping + page, (size_t)page, PROT_NONE) == 0;
    for (uint32_t l = 0; guarded && l < CBUILTIN_LIBRARY_COUNT; l += 1)
    {
        char name[64];
        const uint32_t length = (uint32_t)snprintf(name, sizeof(name), "__builtin_%s", cbuiltin_library_names[l]);
        uint8_t* end = mapping + page;
        memcpy(end - length, name, length);
        missed &= cbuiltin_library_id((const char*)end - length, length) == (CBuiltinLibraryId)l && cbuiltin_library_source(end - length, length);
        missed &= cbuiltin_library_id((const char*)end - length, UINT32_MAX) == CBUILTIN_LIBRARY_COUNT && !cbuiltin_library_source(end - length, UINT32_MAX);
    }
    for (uint32_t p = 0; guarded && p < BUSTER_CBUILTIN_LIBRARY_PLAIN_COUNT; p += 1)
    {
        const CBuiltinLibraryPlain* plain = cbuiltin_library_plains + p;
        uint8_t* end = mapping + page;
        memcpy(end - plain->length, plain->name, plain->length);
        missed &= cbuiltin_library_plain((const char*)end - plain->length, plain->length) == plain;
    }
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, (size_t)page * 2);
    }
    check_test_expect(test, guarded && missed, "library builtins: near misses find nothing, and bounded names at an unreadable page their entries");
    // The prototypes of the implicit declarations (cbuiltin_library_spell), as clang spells them; test-checker-driver
    // compares every plain name's message with clang's own.
    static const struct
    {
        const char* name;
        bool c99;
        const char* sugared;
        const char* plain;
    } spellings[] =
    {
        { "sin", true, "double (double)", "double (double)" },
        { "strlen", true, "__size_t (const char *)", "unsigned long (const char *)" },
        { "printf", true, "int (const char *, ...)", "int (const char *, ...)" },
        { "snprintf", true, "int (char *restrict, __size_t, const char *restrict, ...)", "int (char *restrict, unsigned long, const char *restrict, ...)" },
        { "snprintf", false, "int (char *__restrict, __size_t, const char *__restrict, ...)", "int (char *__restrict, unsigned long, const char *__restrict, ...)" },
        { "abort", true, "void (void) __attribute__((noreturn))", "void (void) __attribute__((noreturn))" },
        { "vprintf", true, "int (const char *restrict, struct __va_list_tag *)", "int (const char *restrict, struct __va_list_tag *)" },
        { "cexpl", true, "_Complex long double (_Complex long double)", "_Complex long double (_Complex long double)" },
        { "strtod", true, "double (const char *, char **)", "double (const char *, char **)" },
        { "wcslen", true, "__size_t (const int *)", "unsigned long (const int *)" },
        { "_Block_object_dispose", true, "void (const void *, const int)", "void (const void *, const int)" },
        { "stdc_leading_zeros_us", true, "unsigned int (unsigned short)", "unsigned int (unsigned short)" },
        { "stdc_has_single_bit_ull", true, "bool (unsigned long long)", "bool (unsigned long long)" },
    };
    bool spelled = true;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(spellings); i += 1)
    {
        const BuiltinIntrinsicInfo* info = builtin_intrinsic_lookup(spellings[i].name, (uint32_t)strlen(spellings[i].name));
        char sugared[256];
        char plain[256];
        char tiny[8];
        bool sized = false;
        bool unsized = false;
        bool small = false;
        spelled &= info != NULL &&
                   cbuiltin_library_spell(info->encoding, info->encoding_length, info->attributes, info->attributes_length, true, spellings[i].c99, NULL, sugared,
                                          sizeof(sugared), &sized) &&
                   cbuiltin_library_spell(info->encoding, info->encoding_length, info->attributes, info->attributes_length, false, spellings[i].c99, NULL, plain,
                                          sizeof(plain), &unsized) &&
                   strcmp(sugared, spellings[i].sugared) == 0 && strcmp(plain, spellings[i].plain) == 0 && sized == (strstr(sugared, "__size_t") != NULL);
        small = info != NULL && !cbuiltin_library_spell(info->encoding, info->encoding_length, info->attributes, info->attributes_length, true, true, NULL, tiny,
                                                        sizeof(tiny), &sized);
        spelled &= small;
    }
    check_test_expect(test, spelled, "library builtins: implicit declarations' prototypes spelled as clang spells them, and refused past their room");
}

// The folds' constant contexts, each checked by both paths against clang's acceptance (check_test_source): a static
// initializer, _Static_assert, an array's size, an enumerator and a case label of each kind of fold; a const array's
// string, a string at an offset and a plain declaration's call; and their refusals: a builtin clang doesn't fold, a
// mutable array, a declaration of another prototype, and a static declaration, which isn't the builtin.

static __attribute__((noinline)) void check_test_library_sources(CheckTest* test, const char* clang)
{
    // Its source fixtures are runtime data (#2053): tests/checker/library, which check_test_suite reads and runs.
    check_test_suite(test, clang, "checker/library", "library folds: the clang oracle is available");
}

// The programs' fold of a value's bits into a hash, which a program returns in place of the values (a NaN's bits too).
#define BUSTER_CHECK_TEST_LIBRARY_FOLD \
    "static unsigned long hash(unsigned long h, const void* p, unsigned n){const unsigned char* b=p;for(unsigned i=0;i<n;i++)h=(h^b[i])*1099511628211UL;" \
    "return h;}static unsigned long hd(unsigned long h, double d){return hash(h,&d,8);}static unsigned long hf(unsigned long h, float f){return hash(h,&f,4);}" \
    "static unsigned long hl(unsigned long h, long double l){return hash(h,&l,10);}static int out(unsigned long h){return (int)(h^(h>>32));}"

// fmax, fmin and their _num kin of the harness's special values (zeros of both signs, infinities, quiet and signaling
// NaNs, subnormals), of float and double, as __builtin_X's and plain calls of the unit's declarations, which clang -O0's
// code computes inline as llvm.maxnum, minnum, maximumnum and minimumnum (#967), as it computes the elementwise builtins
// of those intrinsics' names: of two zeros maxnum's and minnum's first operand, of a signaling NaN beside a number the
// number, and maximumnum's and minimumnum's signaling first operand, unquieted, beside another NaN, bit for bit; a
// musttail return of one too, which clang's code computes inline. The long double ones call fmaxl and fminl, as clang's
// code does.
static const char check_test_library_extremes_source[] =
    BUSTER_CHECK_TEST_LIBRARY_FOLD
    "double fp_special(int);float fp_specialf(int);"
    "double fmax(double,double);double fmin(double,double);float fmaxf(float,float);float fminf(float,float);"
    "double fmaximum_num(double,double);double fminimum_num(double,double);float fmaximum_numf(float,float);float fminimum_numf(float,float);"
    "static double tail(double a,double b){__attribute__((musttail)) return fmax(a,b);}"
    "int tested(int n){unsigned long h=14695981039346656037UL;for(int i=0;i<9;i++){double a=fp_special(n),b=fp_special(i);float f=fp_specialf(n),g=fp_specialf(i);"
    "h=hd(h,tail(b,a));"
    "h=hd(h,__builtin_fmax(a,b));h=hd(h,__builtin_fmin(a,b));h=hd(h,__builtin_fmaximum_num(a,b));h=hd(h,__builtin_fminimum_num(a,b));"
    "h=hf(h,__builtin_fmaxf(f,g));h=hf(h,__builtin_fminf(f,g));h=hf(h,__builtin_fmaximum_numf(f,g));h=hf(h,__builtin_fminimum_numf(f,g));"
    "h=hd(h,fmax(b,a));h=hd(h,fmin(b,a));h=hd(h,fmaximum_num(b,a));h=hd(h,fminimum_num(b,a));"
    "h=hf(h,fmaxf(g,f));h=hf(h,fminf(g,f));h=hf(h,fmaximum_numf(g,f));h=hf(h,fminimum_numf(g,f));"
    "h=hd(h,fmax(a,i)+fmaxf(f,1.5f));h=hd(h,__builtin_fmaximum_num(fmin(a,b),fminimum_num(b,-a)));"
    "h=hd(h,__builtin_elementwise_maxnum(a,b));h=hf(h,__builtin_elementwise_minnum(f,g));h=hd(h,__builtin_elementwise_minimumnum(b,a));"
    "h=hf(h,__builtin_elementwise_maximumnum(g,f));"
    "h=hl(h,__builtin_fmaxl(a,b));h=hl(h,__builtin_fminl(a,b));h=hd(h,__builtin_round(a));h=hd(h,__builtin_roundeven(a));h=hl(h,__builtin_roundl(a));"
    "h=hl(h,__builtin_truncl(a));h=hl(h,__builtin_floorl(a));h=hl(h,__builtin_ceill(a));int e=0;h=hd(h,__builtin_frexp(a,&e));h+=e;}return out(h);}";

// The same program at the targets whose code differs (#967): x86-64-v2's (still SSE2's sequences), x86-64-v3's (AVX's
// VBLENDVPD) and x86-64-v4's (AVX-512's masked moves), each run where the host has the target's features.
static __attribute__((noinline)) void check_test_library_extremes_targets(CheckTest* test, const char* clang)
{
    static const char* const marches[] = { "-march=x86-64-v2", "-march=x86-64-v3", "-march=x86-64-v4" };
    const X86Target native = x86_target_native();
    for (uint32_t t = 0; t < BUSTER_ARRAY_LENGTH(marches); t += 1)
    {
        const char* name = marches[t] + strlen("-march=");
        X86Target target = { 0 };
        const bool parsed = x86_target_parse(name, (uint32_t)strlen(name), &target);
        check_test_expect(test, parsed, "the library extremes program's target");
        if (parsed)
        {
            const bool run_native = (native.features & target.features) == target.features;
            if (!run_native)
            {
                printf("  library extremes for %s: native execution skipped (the host lacks the target's features); the program compiles and links\n", name);
            }
            check_test_program_build_release();
            check_test_program_march = marches[t];
            check_test_program_source_native_limits(test, check_test_library_extremes_source, "library-extremes-of-special-values", clang, false, -1, 0, &target,
                                                    run_native);
            check_test_program_build_release();
            check_test_program_march = "-march=x86-64";
        }
    }
}

// Programs of each family of library builtins against clang -O0's build (check_test_program_source, by both paths): the
// library's functions' results, bit for bit, and what each changes (errno, the buffers it writes), on the harness's
// inputs; the folds' -1, 0 or 1 in code where the library's functions return differences; and powi's libgcc.
static __attribute__((noinline)) void check_test_library_programs(CheckTest* test, const char* clang)
{
    check_test_program_suite(test, "checker/programs/library-math-double", clang);
    check_test_program_source(test, check_test_library_extremes_source, "library-extremes-of-special-values", clang);
    // errno, which the library's functions set: log of a negative number and of zero, and pow's overflow.
    check_test_program_suite(test, "checker/programs/library-errno", clang);
}

static void check_test_library(CheckTest* test, const char* clang)
{
    check_test_library_tables(test);
    check_test_library_sources(test, clang);
    check_test_library_programs(test, clang);
    check_test_library_extremes_targets(test, clang);
}

#endif
