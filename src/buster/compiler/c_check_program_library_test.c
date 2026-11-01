// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #148's library builtins (c_builtin_library.c, c_check_builtin_library.c): the lists against clang 23.1.1's table, the
// folds' constant contexts against clang's acceptance, and programs of each family against clang -O0's build, run
// natively by the harness (check_test_program_source), by both paths. c_checker_test.c includes it after the program
// harness's sections; test-checker runs it, and `test-checker --program-library` alone. test-checker-driver has the
// driver's side: implicit declarations' diagnostics, -fno-builtin and the refusals by name.
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
// (the catalog's, the fortified builtins', the list's, powi's), one the lowering refuses by name
// (cbuiltin_library_refusal), or one whose prototype the checker doesn't decode (__fp16's, custom checking's, FILE's);
// and an f row is listed, or has no prototype to check (attribute T, or none), or needs FILE or va_list's reference.
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
                cbuiltin_library_source((const uint8_t*)name, (uint32_t)length) && cbuiltin_library_refusal(name, (uint32_t)length) == NULL;
        ordered &= l == 0 || strcmp(cbuiltin_library_names[l - 1], cbuiltin_library_names[l]) < 0;
    }
    check_test_expect(test, rows && ordered, "library builtins: each listed call is clang's F row of its name, in order and within the bound");
    bool plains = true;
    for (uint32_t p = 0; p < BUSTER_CBUILTIN_LIBRARY_PLAIN_COUNT; p += 1)
    {
        const CBuiltinLibraryPlain* plain = cbuiltin_library_plains + p;
        const BuiltinIntrinsicInfo* info = builtin_intrinsic_lookup(plain->name, plain->length);
        plains &= info != NULL && memchr(info->attributes, 'f', info->attributes_length) != NULL && memchr(info->attributes, 'T', info->attributes_length) == NULL &&
                  !info->custom_typechecking && info->encoding_length != 0 && memchr(info->encoding, 'P', info->encoding_length) == NULL &&
                  memchr(info->encoding, 'J', info->encoding_length) == NULL && memchr(info->encoding, 'K', info->encoding_length) == NULL &&
                  memchr(info->encoding, 'A', info->encoding_length) == NULL && info->features_length == 0 &&
                  strcmp(info->language_token, plain->gnu ? "ALL_GNU_LANGUAGES" : "ALL_LANGUAGES") == 0;
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
        folds &= info != NULL && strcmp(info->encoding, fold->prototype) == 0 && info->constant_evaluable_attribute &&
                 cbuiltin_library_plain(fold->name, length) != NULL && cbuiltin_library_plain_fold(fold->name, length) == f &&
                 fold->fold != CBUILTIN_LIBRARY_FOLD_NONE;
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
    uint32_t refused = 0;
    uint32_t undecoded = 0;
    uint32_t unaccounted = 0;
    uint32_t refused_advertised = 0;
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
            const bool named = cbuiltin_library_refusal(info->name, info->name_length) != NULL;
            const bool half = memchr(info->encoding, 'h', info->encoding_length) != NULL;
            const bool file = memchr(info->encoding, 'P', info->encoding_length) != NULL;
            advertised += query;
            refused += !query && named;
            undecoded += !query && !named && (half || file || info->custom_typechecking);
            refused_advertised += query && named;
            if (!query && !named && !half && !file && !info->custom_typechecking)
            {
                check_test_library_note(missing, sizeof(missing), &unaccounted, info->name);
            }
        }
        if (c && memchr(info->attributes, 'f', info->attributes_length) != NULL && cbuiltin_library_plain(info->name, info->name_length) == NULL)
        {
            const bool unchecked = memchr(info->attributes, 'T', info->attributes_length) != NULL || info->encoding_length == 0;
            const bool reference = memchr(info->encoding, 'P', info->encoding_length) != NULL || memchr(info->encoding, 'A', info->encoding_length) != NULL;
            unlisted += 1;
            if (!unchecked && !reference)
            {
                check_test_library_note(missing_plain, sizeof(missing_plain), &plain_unaccounted, info->name);
            }
        }
    }
    char description[384];
    snprintf(description, sizeof(description),
             "library builtins: clang's F rows are %u advertised, %u refused by name and %u of prototypes that don't decode, with %u left (%s) and %u "
             "refused but advertised",
             advertised, refused, undecoded, unaccounted, missing, refused_advertised);
    check_test_expect(test, unaccounted == 0 && refused_advertised == 0 && refused != 0 && undecoded != 0, description);
    snprintf(description, sizeof(description), "library builtins: clang's f rows of C are %u listed and %u not, %u of them without a reason (%s)",
             BUSTER_CBUILTIN_LIBRARY_PLAIN_COUNT, unlisted, plain_unaccounted, missing_plain);
    check_test_expect(test, plain_unaccounted == 0, description);
    // Lookups of near misses and of bounded names at an unreadable page find nothing and read nothing past their slices.
    static const char* const misses[] = { "", "__builtin_", "__builtin_si", "__builtin_sinx", "__builtin_SIN", "_builtin_sin", "sin", "__builtin_powix", "__builtin_alloca" };
    bool missed = cbuiltin_library_id(NULL, 4) == CBUILTIN_LIBRARY_COUNT && !cbuiltin_library_source(NULL, 4) && cbuiltin_library_plain(NULL, 3) == NULL &&
                  cbuiltin_library_plain_fold(NULL, 6) == BUSTER_CBUILTIN_LIBRARY_PLAIN_FOLDS && cbuiltin_library_refusal(NULL, 4) == NULL;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(misses); i += 1)
    {
        const uint32_t length = (uint32_t)strlen(misses[i]);
        missed &= cbuiltin_library_id(misses[i], length) == CBUILTIN_LIBRARY_COUNT && !cbuiltin_library_source((const uint8_t*)misses[i], length) &&
                  cbuiltin_library_libgcc(misses[i], length) == BUSTER_CBUILTIN_LIBRARY_LIBGCC;
    }
    missed &= cbuiltin_library_plain("sinx", 4) == NULL && cbuiltin_library_plain("si", 2) == NULL && cbuiltin_library_plain("setjmp", 6) == NULL &&
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
                   cbuiltin_library_spell(info->encoding, info->encoding_length, info->attributes, info->attributes_length, true, spellings[i].c99, sugared,
                                          sizeof(sugared), &sized) &&
                   cbuiltin_library_spell(info->encoding, info->encoding_length, info->attributes, info->attributes_length, false, spellings[i].c99, plain,
                                          sizeof(plain), &unsized) &&
                   strcmp(sugared, spellings[i].sugared) == 0 && strcmp(plain, spellings[i].plain) == 0 && sized == (strstr(sugared, "__size_t") != NULL);
        small = info != NULL && !cbuiltin_library_spell(info->encoding, info->encoding_length, info->attributes, info->attributes_length, true, true, tiny,
                                                        sizeof(tiny), &sized);
        spelled &= small;
    }
    check_test_expect(test, spelled, "library builtins: implicit declarations' prototypes spelled as clang spells them, and refused past their room");
}

// The folds' constant contexts, each checked by both paths against clang's acceptance (check_test_source): a static
// initializer, _Static_assert, an array's size, an enumerator and a case label of each kind of fold; a const array's
// string, a string at an offset and a plain declaration's call; and their refusals: a builtin clang doesn't fold, a
// mutable array, a declaration of another prototype, and a static declaration, which isn't the builtin.
static const CheckSourceFixture check_library_source_fixtures[] =
{
    {
        .name = "library folds: comparisons and lengths in constant contexts",
        .source = "static const unsigned long a = __builtin_strlen(\"abcd\");static const int b = __builtin_strcmp(\"a\", \"b\");\n"
                  "_Static_assert(__builtin_strcmp(\"a\", \"b\") < 0 && __builtin_strcmp(\"b\", \"b\") == 0, \"strcmp\");\n"
                  "_Static_assert(__builtin_strncmp(\"abc\", \"abd\", 2) == 0 && __builtin_strncmp(\"ab\", \"abc\", 5) == -1, \"strncmp\");\n"
                  "_Static_assert(__builtin_memcmp(\"ab\\0c\", \"ab\\0d\", 4) == -1 && __builtin_bcmp(\"ab\", \"ab\", 2) == 0, \"memcmp\");\n"
                  "_Static_assert(__builtin_strcmp(\"\\x80\", \"\\x01\") == 1, \"unsigned bytes\");\n"
                  "_Static_assert(__builtin_wcslen(L\"wide\") == 4 && __builtin_wcscmp(L\"\\x80000000\", L\"\\x01\") == -1, \"wide\");\n"
                  "_Static_assert(__builtin_wcsncmp(L\"ab\", L\"ac\", 1) == 0 && __builtin_wmemcmp(L\"ab\", L\"ac\", 2) == -1, \"wide bounded\");\n"
                  "int c[__builtin_strlen(\"abc\")];enum { E = __builtin_strlen(\"xy\") };\n"
                  "int f(int x){switch(x){case __builtin_strlen(\"ab\"):return 1;case __builtin_wcslen(L\"abc\"):return 2;}return 0;}\n",
        .rejected = false,
    },
    {
        .name = "library folds: strings at offsets and in const arrays",
        .source = "static const char s[] = \"abc\";static const unsigned long n = __builtin_strlen(s);\n"
                  "_Static_assert(__builtin_strlen(\"abc\" + 1) == 2 && __builtin_strcmp(&\"abc\"[1], \"bc\") == 0, \"offsets\");\n"
                  "int f(void){static const char t[] = \"xyz\";return (int)__builtin_strlen(t);}\n",
        .rejected = false,
    },
    {
        .name = "library folds: integers and floating values",
        .source = "static int a = __builtin_abs(-3);static long b = __builtin_labs(-4L);static long long c = __builtin_llabs(-5LL);\n"
                  "_Static_assert(__builtin_abs(-7) == 7, \"abs\");\n"
                  "static const double d = __builtin_fmax(1.0, 2.0);static const float e = __builtin_fminf(1.0f, -2.0f);\n"
                  "static const long double f = __builtin_fmaxl(1.0L, __builtin_nanl(\"\"));static const double g = __builtin_fmaximum_num(-0.0, 0.0);\n"
                  "static const long double h = __builtin_copysignl(2.0L, -1.0L);\n",
        .rejected = false,
    },
    {
        .name = "library folds: plain declarations of the builtins",
        .source = "unsigned long strlen(const char*);int strcmp(const char*, const char*);int memcmp(const void*, const void*, unsigned long);\n"
                  "static const unsigned long n = strlen(\"abcd\");_Static_assert(strcmp(\"a\", \"b\") < 0 && memcmp(\"ab\", \"ac\", 2) == -1, \"plain\");\n",
        .rejected = false,
    },
    {
        .name = "library folds: a builtin clang doesn't fold is no constant",
        .source = "static const double d = __builtin_sin(0.0);\n",
        .rejected = true,
    },
    {
        .name = "library folds: a mutable array's string is no constant",
        .source = "static char m[] = \"abc\";static const unsigned long n = __builtin_strlen(m);\n",
        .rejected = true,
    },
    {
        .name = "library folds: a declaration of another prototype isn't the builtin",
        .source = "int strlen(const char*);static const int n = strlen(\"abcd\");\n",
        .rejected = true,
    },
    {
        .name = "library folds: a static declaration isn't the builtin",
        .source = "static unsigned long strlen(const char* s){return 0;}static const unsigned long n = strlen(\"abcd\");\n",
        .rejected = true,
    },
};

static __attribute__((noinline)) void check_test_library_sources(CheckTest* test, const char* clang)
{
    enum { FILE_COUNT = BUSTER_ARRAY_LENGTH(check_library_source_fixtures) };
    char directory[4096];
    const char* temporary = getenv("TMPDIR");
    snprintf(directory, sizeof(directory), "%s/buster-checker-library-XXXXXX", temporary != NULL ? temporary : "/tmp");
    const bool created = mkdtemp(directory) != NULL;
    bool ready = created;
    static char paths[FILE_COUNT][4200];
    char* pointers[FILE_COUNT];
    ParseTestClang outcomes[FILE_COUNT];
    for (uint32_t i = 0; ready && i < FILE_COUNT; i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        pointers[i] = paths[i];
        ready = parse_test_write(paths[i], check_library_source_fixtures[i].source, strlen(check_library_source_fixtures[i].source));
    }
    const char* arguments[] = { "-std=gnu17" };
    ready = ready && parse_test_clang(clang, arguments, BUSTER_ARRAY_LENGTH(arguments), pointers, FILE_COUNT, outcomes);
    check_test_expect(test, ready, "library folds: the clang oracle is available");
    for (uint32_t i = 0; ready && i < FILE_COUNT; i += 1)
    {
        char description[224];
        snprintf(description, sizeof(description), "%s: recorded GNU17 acceptance agrees with Clang", check_library_source_fixtures[i].name);
        check_test_expect(test, outcomes[i].rejected == check_library_source_fixtures[i].rejected, description);
        check_test_source(test, check_library_source_fixtures + i, paths[i], outcomes + i);
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

// The programs' fold of a value's bits into a hash, which a program returns in place of the values (a NaN's bits too).
#define BUSTER_CHECK_TEST_LIBRARY_FOLD \
    "static unsigned long hash(unsigned long h, const void* p, unsigned n){const unsigned char* b=p;for(unsigned i=0;i<n;i++)h=(h^b[i])*1099511628211UL;" \
    "return h;}static unsigned long hd(unsigned long h, double d){return hash(h,&d,8);}static unsigned long hf(unsigned long h, float f){return hash(h,&f,4);}" \
    "static unsigned long hl(unsigned long h, long double l){return hash(h,&l,10);}static int out(unsigned long h){return (int)(h^(h>>32));}"

// Programs of each family of library builtins against clang -O0's build (check_test_program_source, by both paths): the
// library's functions' results, bit for bit, and what each changes (errno, the buffers it writes), on the harness's
// inputs; the folds' -1, 0 or 1 in code where the library's functions return differences; and powi's libgcc.
static __attribute__((noinline)) void check_test_library_programs(CheckTest* test, const char* clang)
{
    check_test_program_source(test,
        BUSTER_CHECK_TEST_LIBRARY_FOLD
        "int tested(int n){double x=n*0.37+0.11;unsigned long h=14695981039346656037UL;"
        "h=hd(h,__builtin_sin(x));h=hd(h,__builtin_cos(x));h=hd(h,__builtin_tan(x));h=hd(h,__builtin_exp(x));h=hd(h,__builtin_exp2(x));"
        "h=hd(h,__builtin_expm1(x));h=hd(h,__builtin_exp10(x/4));h=hd(h,__builtin_log(x+5));h=hd(h,__builtin_log2(x+5));h=hd(h,__builtin_log10(x+5));"
        "h=hd(h,__builtin_log1p(x+5));h=hd(h,__builtin_cbrt(x));h=hd(h,__builtin_atan2(x,1.5));h=hd(h,__builtin_hypot(x,2));h=hd(h,__builtin_pow(x+5,0.75));"
        "h=hd(h,__builtin_fmod(x,0.7));h=hd(h,__builtin_remainder(x,0.7));h=hd(h,__builtin_asin(x/20));h=hd(h,__builtin_acos(x/20));h=hd(h,__builtin_atan(x));"
        "h=hd(h,__builtin_sinh(x/4));h=hd(h,__builtin_cosh(x/4));h=hd(h,__builtin_tanh(x));h=hd(h,__builtin_asinh(x));h=hd(h,__builtin_acosh(x*x+1));"
        "h=hd(h,__builtin_atanh(x/20));h=hd(h,__builtin_erf(x/4));h=hd(h,__builtin_erfc(x/4));h=hd(h,__builtin_tgamma(x/4+1));h=hd(h,__builtin_lgamma(x/4+1));"
        "h=hd(h,__builtin_round(x));h=hd(h,__builtin_roundeven(x*2));h=hd(h,__builtin_fdim(x,1));h=hd(h,__builtin_fma(x,x,0.5));h=hd(h,__builtin_fmax(x,1));"
        "h=hd(h,__builtin_fmin(x,1));h=hd(h,__builtin_logb(x+5));h=hd(h,__builtin_nextafter(x,0));h=hd(h,__builtin_nexttoward(x,0.0L));h=hd(h,__builtin_scalbn(x,n));"
        "h=hd(h,__builtin_scalbln(x,n));h=hd(h,__builtin_ldexp(x,3));int e;h=hd(h,__builtin_frexp(x,&e));double ip;h=hd(h,__builtin_modf(x,&ip));h=hd(h,ip);"
        "int q;h=hd(h,__builtin_remquo(x,0.7,&q));double s,c;__builtin_sincos(x,&s,&c);h=hd(hd(h,s),c);"
        "return out(h)+e*3+q*5+__builtin_ilogb(x+5)*7+(int)__builtin_lround(x)+(int)__builtin_llrint(x*3)+(int)__builtin_lrint(x*5)+(int)__builtin_llround(x*7);}",
        "library-math-double", clang);
    check_test_program_source(test,
        BUSTER_CHECK_TEST_LIBRARY_FOLD
        "int tested(int n){float y=n*0.37f+0.11f;long double z=n*0.37L+0.11L;unsigned long h=14695981039346656037UL;"
        "h=hf(h,__builtin_sinf(y));h=hf(h,__builtin_expf(y));h=hf(h,__builtin_logf(y+5));h=hf(h,__builtin_powf(y+5,1.5f));h=hf(h,__builtin_hypotf(y,2));"
        "h=hf(h,__builtin_fmaxf(y,1));h=hf(h,__builtin_fminf(y,1));h=hf(h,__builtin_roundf(y));h=hf(h,__builtin_fmaf(y,y,0.5f));h=hf(h,__builtin_ldexpf(y,n));"
        "int e;h=hf(h,__builtin_frexpf(y,&e));float fi;h=hf(h,__builtin_modff(y,&fi));h=hf(h,fi);h=hf(h,__builtin_powif(y,n));"
        "h=hl(h,__builtin_sinl(z));h=hl(h,__builtin_expl(z));h=hl(h,__builtin_logl(z+5));h=hl(h,__builtin_powl(z+5,1.5L));h=hl(h,__builtin_sqrtl(z+5));"
        "h=hl(h,__builtin_floorl(z));h=hl(h,__builtin_ceill(z));h=hl(h,__builtin_truncl(z));h=hl(h,__builtin_roundl(z));h=hl(h,__builtin_rintl(z*3));"
        "h=hl(h,__builtin_nearbyintl(z*3));h=hl(h,__builtin_copysignl(z,-1.0L));h=hl(h,__builtin_fmaxl(z,1));h=hl(h,__builtin_fminl(z,1));h=hl(h,__builtin_fmal(z,z,0.5L));"
        "int f;h=hl(h,__builtin_frexpl(z,&f));long double li;h=hl(h,__builtin_modfl(z,&li));h=hl(h,li);h=hl(h,__builtin_ldexpl(z,n));h=hl(h,__builtin_powil(z,n));"
        "h=hl(h,__builtin_fmaximum_numl(z,-z));h=hl(h,__builtin_fminimum_numl(z,-z));h=hd(h,__builtin_powi(n*0.37+0.11,n));"
        "return out(h)+e*3+f*5;}",
        "library-math-float-long-double-powi", clang);
    check_test_program_source(test,
        BUSTER_CHECK_TEST_LIBRARY_FOLD
        "static unsigned long hq(unsigned long h, __float128 q){return hash(h,&q,16);}"
        "int tested(int n){__float128 q=(__float128)(n*0.37+0.11);unsigned long h=14695981039346656037UL;"
        "h=hq(h,__builtin_sinf128(q));h=hq(h,__builtin_sqrtf128(q+5));h=hq(h,__builtin_expf128(q));h=hq(h,__builtin_logf128(q+5));h=hq(h,__builtin_powf128(q+5,q));"
        "h=hq(h,__builtin_floorf128(q));h=hq(h,__builtin_ceilf128(q));h=hq(h,__builtin_truncf128(q));h=hq(h,__builtin_roundf128(q));h=hq(h,__builtin_rintf128(q*3));"
        "h=hq(h,__builtin_fabsf128(-q));h=hq(h,__builtin_copysignf128(q,-q));h=hq(h,__builtin_fmaxf128(q,1));h=hq(h,__builtin_fminf128(q,1));"
        "h=hq(h,__builtin_fmaf128(q,q,q));int e;h=hq(h,__builtin_frexpf128(q,&e));h=hq(h,__builtin_ldexpf128(q,n));h=hq(h,__builtin_logbf128(q+5));"
        "return out(h)+e*3+__builtin_ilogbf128(q+5);}",
        "library-math-float128", clang);
    check_test_program_source(test,
        BUSTER_CHECK_TEST_LIBRARY_FOLD
        "int tested(int n){_Complex double c;__real__ c=n*0.37+0.11;__imag__ c=1.5-n*0.25;_Complex float g;__real__ g=n*0.5f;__imag__ g=0.75f;"
        "_Complex long double l;__real__ l=n*0.25L;__imag__ l=-0.5L;unsigned long h=14695981039346656037UL;"
        "_Complex double r[]={__builtin_cexp(c),__builtin_clog(c),__builtin_csqrt(c),__builtin_cpow(c,c),__builtin_csin(c),__builtin_ccos(c),__builtin_ctan(c),"
        "__builtin_casin(c),__builtin_cacos(c),__builtin_catan(c),__builtin_csinh(c),__builtin_ccosh(c),__builtin_ctanh(c),__builtin_casinh(c),__builtin_cacosh(c),"
        "__builtin_catanh(c),__builtin_cproj(c),__builtin_conj(c)};h=hash(h,r,sizeof(r));h=hd(hd(h,__builtin_cabs(c)),__builtin_carg(c));"
        "_Complex float rf[]={__builtin_cexpf(g),__builtin_csqrtf(g),__builtin_conjf(g),__builtin_cpowf(g,g)};h=hash(h,rf,sizeof(rf));"
        "h=hd(hd(h,__builtin_cabsf(g)),__builtin_cargf(g));"
        "_Complex long double rl[]={__builtin_cexpl(l),__builtin_csqrtl(l),__builtin_conjl(l)};for(int i=0;i<3;i++)h=hl(hl(h,__real__ rl[i]),__imag__ rl[i]);"
        "h=hl(hl(h,__builtin_cabsl(l)),__builtin_cargl(l));return out(h);}",
        "library-complex", clang);
    // Each pointer a call returns, as its offset in the array it points into, or -1 for none: the arrays' addresses
    // differ between the program and its oracle.
    check_test_program_source(test,
        "static int at(const char*p,const char*a){return p?(int)(p-a):-1;}"
        "int tested(int n){char a[32],b[32];int k=n&7;__builtin_strcpy(a,\"hello world\");a[k]=(char)('A'+k);__builtin_strcpy(b,a);b[(k+3)&7]='z';"
        "int r=__builtin_strcmp(a,b)*3+__builtin_strncmp(a,b,(unsigned)k)*5;r+=at(__builtin_strchr(a,'o'),a)+at(__builtin_strrchr(a,'o'),a)*7;"
        "r+=at(__builtin_strstr(a,\"wor\"),a)+(int)__builtin_strspn(a,\"hel\")+(int)__builtin_strcspn(a,\" \")*11;r+=at(__builtin_strpbrk(a,\"wd\"),a)*13;"
        "char c[64];__builtin_strcpy(c,a);__builtin_strcat(c,\"!\");__builtin_strncat(c,b,3);r+=(int)__builtin_strlen(c)*17;"
        "r+=__builtin_bcmp(a,b,12)!=0;r+=at(__builtin_memchr(a,'w',12),a);char d[32];r+=at(__builtin_mempcpy(d,a,5),d);__builtin_bzero(d,4);"
        "r+=d[0]+d[4];__builtin_bcopy(a,d,6);r+=d[5];r+=at(__builtin_stpcpy(d,\"xy\"),d);__builtin_strncpy(d,\"abc\",8);r+=d[7]+d[1];"
        "r+=at(__builtin_stpncpy(d,\"ab\",4),d);char*dup=__builtin_strdup(a);r+=dup[k];__builtin_free(dup);"
        "char*ndup=__builtin_strndup(a,3);r+=(int)__builtin_strlen(ndup)*19;__builtin_free(ndup);r+=__builtin_strcasecmp(\"HeLLo\",a)<0;"
        "r+=__builtin_strncasecmp(\"HELLO\",a,3)==0;r+=at(__builtin_index(a,'l'),a)*23+at(__builtin_rindex(a,'l'),a)*29;return r;}",
        "library-strings", clang);
    // The wide characters stay positive: glibc's x86 wcscmp variants disagree on whether a negative wchar_t is less
    // than a positive one, and which variant runs depends on the strings' addresses, so a negative one's comparison
    // changed sign from run to run (the 9700X job's n=-3 with ASLR).
    check_test_program_source(test,
        "typedef int wchar;static int at(const wchar*p,const wchar*a){return p?(int)(p-a):-1;}"
        "int tested(int n){wchar a[16]={'w','i','d','e',0},b[16]={'w','i','d','f',0};a[n&3]=(wchar)((n+4)*0x10001);"
        "int r=(int)__builtin_wcslen(a)+__builtin_wcscmp(a,b)*3+__builtin_wcsncmp(a,b,(unsigned)(n&3))*5+__builtin_wmemcmp(a,b,4)*7;"
        "r+=at(__builtin_wcschr(a,'d'),a)*11+at(__builtin_wmemchr(a,'e',4),a)*13;wchar d[16];"
        "r+=at(__builtin_wmemcpy(d,a,5),d);r+=d[3];__builtin_wmemmove(d+1,d,4);return r+d[1]*17+d[4];}",
        "library-wide", clang);
    check_test_program_source(test,
        "static int v(char*b,const char*f,...){__builtin_va_list a;__builtin_va_start(a,f);int r=__builtin_vsnprintf(b,64,f,a);__builtin_va_end(a);return r;}"
        "static int w(char*b,const char*f,...){__builtin_va_list a;__builtin_va_start(a,f);int r=__builtin_vsprintf(b,f,a);__builtin_va_end(a);return r;}"
        "static int s(const char*b,const char*f,...){__builtin_va_list a;__builtin_va_start(a,f);int r=__builtin_vsscanf(b,f,a);__builtin_va_end(a);return r;}"
        "int tested(int n){char b[64];int r=__builtin_snprintf(b,sizeof(b),\"%d:%s:%.3f:%Lg\",n,\"x\",n*0.5,(long double)n/3);int x=0;char t[8];"
        "r+=__builtin_sscanf(b,\"%d:%7[^:]\",&x,t)*100+x*3+t[0];r+=v(b,\"%c%d\",'q',n)*7+b[0]+b[1];r+=w(b,\"%05d\",n)*11+b[3];"
        "int y=0;r+=s(\"17 23\",\"%d %d\",&x,&y)*13+x+y;return r+__builtin_sprintf(b,\"%x\",n*1000)+b[0];}",
        "library-formatted", clang);
    check_test_program_source(test,
        "int tested(int n){int*p=__builtin_malloc(16*sizeof(int));for(int i=0;i<16;i++)p[i]=i*n;p=__builtin_realloc(p,64*sizeof(int));int r=p[15];"
        "__builtin_free(p);long*q=__builtin_calloc(8,sizeof(long));r+=(int)q[7];__builtin_free(q);"
        "return r+__builtin_abs(n-7)*3+(int)__builtin_labs(-(long)n)*5+(int)__builtin_llabs((long long)n*-3)*7;}",
        "library-memory-and-absolutes", clang);
    // The folds in code: clang's -1, 0 or 1 of constant strings, where the library's functions return the differences
    // (glibc's strcmp("a", "c") is -2), through __builtin_X and the plain declarations; a const array's string; and a
    // call with a side effect, which neither folds.
    check_test_program_source(test,
        "int strcmp(const char*,const char*);int strncmp(const char*,const char*,unsigned long);int memcmp(const void*,const void*,unsigned long);"
        "unsigned long strlen(const char*);static const char k[]=\"azz\";int calls;static const char*touch(void){calls+=1;return \"abd\";}"
        "int tested(int n){int r=__builtin_strcmp(\"a\",\"c\")*1000+__builtin_strncmp(\"abc\",\"abz\",3)*100+__builtin_memcmp(\"ab\",\"ad\",2)*10;"
        "r+=__builtin_wcscmp(L\"a\",L\"c\")+__builtin_bcmp(\"ab\",\"ad\",2)*20000+(int)__builtin_strlen(\"hello\")*30000+(int)__builtin_wcslen(L\"xyz\")*40000;"
        "r+=strcmp(\"a\",\"c\")*50000+strncmp(\"abc\",\"abz\",3)*60000+memcmp(\"ab\",\"ad\",2)*70000+(int)strlen(k)*80000+__builtin_strcmp(k,\"aa\")*90000;"
        "r+=__builtin_strcmp(touch(),\"abc\")*100000+calls;return r+n;}",
        "library-folds-in-code", clang);
    // fmax, fmin and their _num kin of the harness's special values (zeros, infinities, quiet and signaling NaNs,
    // subnormals), each of a call of the library's function against clang's llvm.maxnum and llvm.maximumnum. clang's
    // x86 code of llvm.maxnum and llvm.minnum (maxsd or minsd beside a NaN's select) gives its first operand of two
    // zeros and the other operand of a signaling NaN, where glibc's fmax and fmin give the second zero and a quiet NaN,
    // and its code of llvm.maximumnum and minimumnum a signaling first operand unquieted where the other is a NaN too,
    // where glibc's give a quiet NaN, which C leaves open: those pairs are left out of the float and double ones
    // (docs/c-builtins-integration.md).
    check_test_program_source(test,
        BUSTER_CHECK_TEST_LIBRARY_FOLD
        "double fp_special(int);float fp_specialf(int);"
        "static int signalingd(double d){union{double d;unsigned long u;}x={d};return (x.u&0x7ff8000000000000UL)==0x7ff0000000000000UL&&(x.u&0xfffffffffffffUL)!=0;}"
        "static int signalingf(float f){union{float f;unsigned u;}x={f};return (x.u&0x7fc00000u)==0x7f800000u&&(x.u&0x7fffffu)!=0;}"
        "int tested(int n){unsigned long h=14695981039346656037UL;for(int i=0;i<9;i++){double a=fp_special(n),b=fp_special(i);float f=fp_specialf(n),g=fp_specialf(i);"
        "if(!signalingd(a)&&!signalingd(b)&&!(a==0&&b==0)){h=hd(h,__builtin_fmax(a,b));h=hd(h,__builtin_fmin(a,b));}"
        "if(!(signalingd(a)&&b!=b)){h=hd(h,__builtin_fmaximum_num(a,b));h=hd(h,__builtin_fminimum_num(a,b));}"
        "if(!signalingf(f)&&!signalingf(g)&&!(f==0&&g==0)){h=hf(h,__builtin_fmaxf(f,g));h=hf(h,__builtin_fminf(f,g));}"
        "if(!(signalingf(f)&&g!=g)){h=hf(h,__builtin_fmaximum_numf(f,g));h=hf(h,__builtin_fminimum_numf(f,g));}"
        "h=hl(h,__builtin_fmaxl(a,b));h=hl(h,__builtin_fminl(a,b));h=hd(h,__builtin_round(a));h=hd(h,__builtin_roundeven(a));h=hl(h,__builtin_roundl(a));"
        "h=hl(h,__builtin_truncl(a));h=hl(h,__builtin_floorl(a));h=hl(h,__builtin_ceill(a));int e=0;h=hd(h,__builtin_frexp(a,&e));h+=e;}return out(h);}",
        "library-extremes-of-special-values", clang);
    // errno, which the library's functions set: log of a negative number and of zero, and pow's overflow.
    check_test_program_source(test,
        "int set_math_errno(int);int get_math_errno(void);"
        "int tested(int n){set_math_errno(0);double x=__builtin_log(n<0?-1.0:n==0?0.0:n);int a=get_math_errno();set_math_errno(0);"
        "double y=__builtin_pow(10.0,n*40.0);int b=get_math_errno();return a*100+b*7+(x>0)+(y>1e300);}",
        "library-errno", clang);
}

static void check_test_library(CheckTest* test, const char* clang)
{
    check_test_library_tables(test);
    check_test_library_sources(test, clang);
    check_test_library_programs(test, clang);
}

#endif
