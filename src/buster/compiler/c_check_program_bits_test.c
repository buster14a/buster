// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #148's bit builtins (c_check_builtin_bits.c, c_builtin_bits_lower.c) in test-checker: source fixtures of their result
// types, folds and refusals, each checked by both paths against clang -fsyntax-only, then programs run natively against
// clang -O0's build of the same source on both paths: every generic builtin of the standard unsigned types and bool of
// boundary values, with clzg's and ctzg's results for zero, the fixed ones, _BitInt, enumeration and bit-field operands,
// rotations by counts of eight types, the carry chains of every width, folds beside operands evaluated once, in order,
// and 128-bit and wide _BitInt operands, results and rotation counts. Include after c_check_musttail_test.c.
#ifndef BUSTER_C_CHECK_PROGRAM_BITS_TEST_INCLUDED
#define BUSTER_C_CHECK_PROGRAM_BITS_TEST_INCLUDED

static __attribute__((noinline)) void check_test_bits_sources(CheckTest* test, const char* clang)
{
    static const CheckSourceFixture fixtures[] =
    {
        { .name = "bits: result types", .source =
          "#define T(e, t) _Static_assert(_Generic((e), t: 1, default: 0), #e);\n"
          "enum U { UA = 1 };\n"
          "struct B { unsigned x : 3; int z : 5; };\n"
          "void f(unsigned char c, unsigned short s, _Bool b, enum U e, unsigned _BitInt(37) w, _BitInt(48) v, const unsigned k, struct B r)\n"
          "{\n"
          "T(__builtin_bitreverse8(c), unsigned char) T(__builtin_bitreverse64(1), unsigned long) T(__builtin_clrsbl(1), int) T(__builtin_ctzs(s), int)\n"
          "T(__builtin_popcountg(b), int) T(__builtin_popcountg(e), int) T(__builtin_clzg(w, (short)1), int) T(__builtin_ctzg(r.x, r.z), int)\n"
          "T(__builtin_bitreverseg(v), _BitInt(48)) T(__builtin_bitreverseg(k), unsigned int) T(__builtin_bswapg(e), enum U) T(__builtin_bitreverseg(r.z), int)\n"
          "T(__builtin_stdc_leading_zeros(c), unsigned int) T(__builtin_stdc_has_single_bit(w), _Bool) T(__builtin_stdc_bit_floor(s), unsigned short)\n"
          "T(__builtin_stdc_bit_ceil(w), unsigned _BitInt(37)) T(__builtin_stdc_rotate_left(b, -1), _Bool) T(__builtin_stdc_rotate_right(e, v), enum U)\n"
          "T(__builtin_addcb(c, c, c, &c), unsigned char) T(__builtin_subcll(1, 2, 0, (unsigned long long *)0), unsigned long long)\n"
          "}\n" },
        { .name = "bits: folds in constant contexts", .source =
          "_Static_assert(__builtin_popcountg(7u) == 3 && __builtin_clzg(1u, 5) == 31 && __builtin_clzg(0u, 5) == 5, \"\");\n"
          "_Static_assert(__builtin_ctzg((unsigned char)0, -1) == -1 && __builtin_stdc_bit_ceil(0x80000001u) == 0 && __builtin_stdc_bit_ceil(0u) == 1, \"\");\n"
          "_Static_assert(__builtin_stdc_rotate_left((unsigned char)1, -1) == 0x80 && __builtin_stdc_rotate_right(1u, 33) == 0x80000000u, \"\");\n"
          "_Static_assert(__builtin_bitreverse8(1) == 0x80 && __builtin_clrsb(-1) == 31 && __builtin_clrsbll(0) == 63, \"\");\n"
          "_Static_assert(__builtin_bswapg((unsigned short)0x1234) == 0x3412 && __builtin_bitreverseg((signed char)1) == -128, \"\");\n"
          "_Static_assert(__builtin_stdc_first_leading_zero((unsigned char)0xff) == 0 && __builtin_stdc_leading_ones((unsigned char)0xf0) == 4, \"\");\n"
          "_Static_assert(__builtin_popcountg((unsigned __int128)-1) == 128 && __builtin_clzg((unsigned _BitInt(100))1) == 99, \"\");\n"
          "int a[__builtin_clzs(1)];\n"
          "enum { E1 = __builtin_ctzs(8), E2 = __builtin_stdc_bit_width(5u) };\n"
          "static int s = __builtin_stdc_count_zeros((unsigned short)0x0f0f) + __builtin_ctzg(0u, 9);\n" },
        // Past 128 bits (#1447): the generic and C23 builtins of constants of a _BitInt past 128 bits fold by its limbs
        // (c_check_wide.c), rotations by narrow and wide counts, and a narrow value's rotation by a wide count.
        { .name = "bits: folds past 128 bits", .standard = 2023, .source =
          "typedef unsigned _BitInt(200) U;\ntypedef unsigned _BitInt(256) V;\n"
          "_Static_assert(__builtin_popcountg((U)-1) == 200 && __builtin_clzg((U)1) == 199 && __builtin_ctzg((U)1 << 150) == 150, \"\");\n"
          "_Static_assert(__builtin_clzg((U)0, 7) == 7 && __builtin_ctzg((U)0, -1) == -1 && __builtin_bitreverseg((U)1) == (U)1 << 199, \"\");\n"
          "_Static_assert(__builtin_bswapg((V)0xff) == (V)0xff << 248 && __builtin_stdc_rotate_left((U)1, 201) == 2, \"\");\n"
          "_Static_assert(__builtin_stdc_rotate_right((U)1, 1) == (U)1 << 199 && __builtin_stdc_rotate_left((U)3, (_BitInt(300))-1) == ((U)1 << 199 | 1), \"\");\n"
          "_Static_assert(__builtin_stdc_rotate_left((unsigned)1, (_BitInt(300))33) == 2, \"\");\n"
          "_Static_assert(__builtin_stdc_bit_floor((U)1 << 170 | 5) == (U)1 << 170 && __builtin_stdc_bit_ceil((U)1 << 170 | 5) == (U)1 << 171, \"\");\n"
          "_Static_assert(__builtin_stdc_bit_ceil((U)1 << 199 | 1) == 0 && __builtin_stdc_bit_ceil((U)0) == 1 && __builtin_stdc_bit_ceil((U)1 << 150) == (U)1 << 150, \"\");\n"
          "_Static_assert(__builtin_stdc_bit_width((U)1 << 150) == 151 && __builtin_stdc_has_single_bit((U)1 << 150), \"\");\n"
          "_Static_assert(__builtin_stdc_leading_ones(~(U)0 >> 3 ^ ~(U)0) == 3 && __builtin_stdc_trailing_ones((U)0x7f) == 7, \"\");\n"
          "_Static_assert(__builtin_stdc_first_leading_zero(~(U)0 >> 3 ^ ~(U)0) == 4 && __builtin_stdc_first_trailing_one((U)1 << 150) == 151, \"\");\n"
          "_Static_assert(__builtin_stdc_count_zeros((U)7) == 197 && __builtin_stdc_first_leading_one((U)0) == 0, \"\");\n"
          "int a[__builtin_popcountg((V)-1)];\nstatic U s = __builtin_bitreverseg((U)0xf0);\n" },
        { .name = "bits: clzg of a zero past 128 bits without a fallback is no constant", .standard = 2023,
          .source = "int z =\n__builtin_clzg((unsigned _BitInt(200))0);\n", .rejected = true },
        { .name = "bits: folds through const objects (#990)", .source =
          "static const unsigned ku=7u;\n"
          "static const unsigned _BitInt(37) kw=37;\n"
          "static const unsigned _BitInt(16) kw16=0x1234;\n"
          "static const unsigned short ks=0x1234;\n"
          "static const unsigned char kc=0xf0;\n"
          "_Static_assert(__builtin_popcountg(ku)==3 && __builtin_clzg(ku,99)==29 && __builtin_ctzg(ku,99)==0, \"\");\n"
          "_Static_assert(__builtin_bswapg(ks)==0x3412 && __builtin_bitreverseg((unsigned char)ku)==0xe0, \"\");\n"
          "_Static_assert(__builtin_stdc_count_ones(ku)==3 && __builtin_stdc_bit_width(ku)==3 && __builtin_stdc_bit_ceil(ku)==8, \"\");\n"
          "_Static_assert(__builtin_popcountg(kw)==3 && __builtin_bswapg(kw16)==0x3412 && __builtin_stdc_rotate_left(kc,4)==0x0f, \"\");\n"
          "static int gs = __builtin_popcountg(ku);\n"
          "int ga[__builtin_popcountg(ku)];\n"
          "enum { GE=__builtin_stdc_bit_floor(ku) };\n"
          "struct GF { unsigned field:__builtin_stdc_bit_width(ku); };\n"
          "unsigned f(unsigned n) { switch (n) { case __builtin_stdc_count_ones(ku): return gs; } return 0; }\n" },
        { .name = "bits: fold of a volatile object is no constant", .source =
          "static const volatile unsigned kv=7u;\n"
          "int gv[__builtin_popcountg(kv)];\n", .rejected = true },
        { .name = "bits: fold of a mutable object is no constant", .source =
          "static unsigned kn=7u;\n"
          "_Static_assert(__builtin_clzg(kn,0)==29, \"\");\n", .rejected = true },
        { .name = "bits: clzg of zero without a fallback is no constant", .source = "int z =\n__builtin_clzg(0u);\n", .rejected = true },
        { .name = "bits: popcountg of an int", .source = "int f(int i)\n{\nreturn __builtin_popcountg(i);\n}\n", .rejected = true },
        { .name = "bits: popcountg of plain char where it is signed", .source = "int f(char c)\n{\nreturn __builtin_popcountg(c);\n}\n", .rejected = true },
        { .name = "bits: popcountg of plain char where it is unsigned", .source = "int f(char c)\n{\nreturn __builtin_popcountg(c);\n}\n", .char_unsigned = true },
        { .name = "bits: a stdc builtin of a bool", .source = "unsigned f(_Bool b)\n{\nreturn __builtin_stdc_count_ones(b);\n}\n", .rejected = true },
        { .name = "bits: a stdc builtin of an enumeration", .source = "enum U { UA = 1 };\nunsigned f(enum U e)\n{\nreturn __builtin_stdc_leading_zeros(e);\n}\n",
          .rejected = true },
        { .name = "bits: clzg's fallback of a long", .source = "int f(unsigned u, long l)\n{\nreturn __builtin_clzg(u, l);\n}\n", .rejected = true },
        { .name = "bits: ctzg's fallback of an unsigned int", .source = "int f(unsigned u, unsigned v)\n{\nreturn __builtin_ctzg(u, v);\n}\n", .rejected = true },
        { .name = "bits: clzg's fallback of a 31-bit unsigned bit-field", .source = "struct B { unsigned x : 31; };\nint f(unsigned u, struct B b)\n{\nreturn __builtin_clzg(u, b.x);\n}\n" },
        { .name = "bits: clzg's fallback of a 32-bit unsigned bit-field", .source = "struct B { unsigned x : 32; };\nint f(unsigned u, struct B b)\n{\nreturn __builtin_clzg(u, b.x);\n}\n",
          .rejected = true },
        { .name = "bits: clzg's fallback of an enumeration of int", .source = "enum S { SA = -1 };\nint f(unsigned u, enum S e)\n{\nreturn __builtin_clzg(u, e);\n}\n" },
        { .name = "bits: a rotation of an int", .source = "unsigned f(int u)\n{\nreturn __builtin_stdc_rotate_left(u, 1);\n}\n", .rejected = true },
        { .name = "bits: a rotation by a float", .source = "unsigned f(unsigned u, float c)\n{\nreturn __builtin_stdc_rotate_right(u, c);\n}\n", .rejected = true },
        { .name = "bits: bswapg of a 24-bit _BitInt", .source = "_BitInt(24) f(_BitInt(24) x)\n{\nreturn __builtin_bswapg(x);\n}\n", .rejected = true },
        { .name = "bits: bitreverseg of a float", .source = "float f(float x)\n{\nreturn __builtin_bitreverseg(x);\n}\n", .rejected = true },
        { .name = "bits: a carry of the other signedness", .source = "unsigned f(unsigned u, int *q)\n{\nreturn __builtin_addc(u, u, 0, q);\n}\n" },
        { .name = "bits: a carry of a const pointee", .source = "unsigned f(unsigned u, const unsigned *p)\n{\nreturn __builtin_subc(u, u, 1, p);\n}\n" },
        { .name = "bits: a carry of another width", .source = "unsigned f(unsigned u, unsigned long *p)\n{\nreturn __builtin_addc(u, u, 0, p);\n}\n", .rejected = true },
        { .name = "bits: a carry of an int", .source = "unsigned f(unsigned u, int x)\n{\nreturn __builtin_addc(u, u, 0, x);\n}\n", .rejected = true },
        { .name = "bits: a carry chain of three arguments", .source = "unsigned f(unsigned u)\n{\nreturn __builtin_addcl(u, u, 0);\n}\n", .rejected = true },
        { .name = "bits: bitreverse32 of a pointer", .source = "unsigned f(int *p)\n{\nreturn __builtin_bitreverse32(p);\n}\n", .rejected = true },
        { .name = "bits: bitreverse32 of a double", .source = "unsigned f(double d)\n{\nreturn __builtin_bitreverse32(d);\n}\n" },
        { .name = "bits: clzg of three arguments", .source = "int f(unsigned u)\n{\nreturn __builtin_clzg(u, 1, 2);\n}\n", .rejected = true },
    };
    enum { FILE_COUNT = BUSTER_ARRAY_LENGTH(fixtures) };
    char directory[] = "/tmp/buster-checker-bits-XXXXXX";
    bool ready = mkdtemp(directory) != 0;
    char paths[FILE_COUNT][160];
    for (uint32_t i = 0; ready && i < FILE_COUNT; i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        ready = parse_test_write(paths[i], fixtures[i].source, strlen(fixtures[i].source));
    }
    // Plain char's signedness is an option: clang checks the fixtures of each in a run of its own.
    char* pointers[2][FILE_COUNT];
    uint32_t indices[2][FILE_COUNT];
    uint32_t counts[2] = { 0, 0 };
    for (uint32_t i = 0; ready && i < FILE_COUNT; i += 1)
    {
        const uint32_t group = fixtures[i].char_unsigned ? 1 : 0;
        pointers[group][counts[group]] = paths[i];
        indices[group][counts[group]] = i;
        counts[group] += 1;
    }
    const char* signed_arguments[] = { "-std=gnu17" };
    const char* unsigned_arguments[] = { "-std=gnu17", "-funsigned-char" };
    for (uint32_t group = 0; ready && group < 2; group += 1)
    {
        ParseTestClang outcomes[FILE_COUNT];
        const bool ran = counts[group] == 0 ||
                         (group == 0 ? parse_test_clang(clang, signed_arguments, BUSTER_ARRAY_LENGTH(signed_arguments), pointers[group], counts[group], outcomes) :
                                       parse_test_clang(clang, unsigned_arguments, BUSTER_ARRAY_LENGTH(unsigned_arguments), pointers[group], counts[group], outcomes));
        check_test_expect(test, ran, "the bit builtins' Clang oracle ran");
        for (uint32_t k = 0; ran && k < counts[group]; k += 1)
        {
            const CheckSourceFixture* fixture = fixtures + indices[group][k];
            char description[192];
            snprintf(description, sizeof(description), "%s: expectation differs from Clang", fixture->name);
            check_test_expect(test, outcomes[k].rejected == (fixture->rejected && (!fixture->unsupported || fixture->undecided)), description);
            check_test_source(test, fixture, paths[indices[group][k]], &outcomes[k]);
        }
    }
    for (uint32_t i = 0; i < FILE_COUNT; i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        unlink(paths[i]);
    }
    if (directory[0] != 0)
    {
        rmdir(directory);
    }
}

static void check_test_bits_rotate_counts(CheckTest* test, const char* clang)
{
    CBuiltinLowerRecipe recipe = { 0 };
    cbuiltin_bits_amount(&recipe, 1, 0, 64 | BUSTER_CBUILTIN_SIGNED, 37);
    uint32_t remainders = 0;
    uint32_t signed_remainders = 0;
    for (uint32_t i = 0; i < recipe.count; i += 1)
    {
        if (recipe.kinds[i] == BUSTER_TYPED_MOD)
        {
            remainders += 1;
            signed_remainders += recipe.classes[i] == BUSTER_TYPED_CLASS_I64;
        }
    }
    check_test_expect(test, remainders == 1 && signed_remainders == 1, "an odd-width signed rotate count takes one signed remainder");

    // The expected distance of each count moves a single set bit around a 37-bit ring.
    check_test_program_against(test,
        "int tested(int n){static const struct {long long count; unsigned distance;} cases[]={"
        "{0,0},{37,0},{-37,0},{38,1},{-38,36},{0x7fffffffffffffffLL,5},{-0x7fffffffffffffffLL-1,31}};"
        "unsigned bit=(unsigned)n%37; unsigned _BitInt(37) x=(unsigned _BitInt(37))1<<bit;"
        "for(unsigned i=0;i<sizeof cases/sizeof cases[0];i++){long long k=cases[i].count;unsigned d=cases[i].distance;"
        "if(__builtin_stdc_rotate_left(x,k)!=(1ULL<<((bit+d)%37)))return 1;"
        "if(__builtin_stdc_rotate_right(x,k)!=(1ULL<<((bit+37-d)%37)))return 2;}return 0;}",
        "int tested(int n){return 0;}", false, "bits-signed-rotate-count-boundaries", clang);
}

static __attribute__((noinline)) void check_test_bits_programs(CheckTest* test, const char* clang)
{
    check_test_bits_rotate_counts(test, clang);
    check_test_program_suite(test, "checker/programs/bits-programs", clang);
    // clang's code evaluates bitreverseg's argument twice, and bswapg's but of 1 or 8 bits (EmitScalarExpr, then
    // emitBuiltinWithOneOverloadedType again), repeating its effects; this compiler evaluates it once, as a call does:
    // its program agrees with clang's build of a model that evaluates each argument first, and differs from clang's build
    // of the program itself.
    static const char evaluated[] =
        "static int calls;static unsigned next(unsigned v){calls++;return v;}static unsigned short half(unsigned short v){calls++;return v;}"
        "int tested(int n){calls=0;unsigned a=__builtin_bitreverseg(next((unsigned)n));unsigned short b=__builtin_bswapg(half((unsigned short)(n*3)));"
        "_Bool c=__builtin_bitreverseg((_Bool)(next((unsigned)n)&1));unsigned char d=__builtin_bswapg((unsigned char)next((unsigned)n));"
        "return (int)(a>>16)+b+c+d+calls*100000;}";
    static const char model[] =
        "static int calls;static unsigned next(unsigned v){calls++;return v;}static unsigned short half(unsigned short v){calls++;return v;}"
        "int tested(int n){calls=0;unsigned x=next((unsigned)n);unsigned a=__builtin_bitreverseg(x);unsigned short y=half((unsigned short)(n*3));"
        "unsigned short b=__builtin_bswapg(y);_Bool z=(_Bool)(next((unsigned)n)&1);_Bool c=__builtin_bitreverseg(z);"
        "unsigned char w=(unsigned char)next((unsigned)n);unsigned char d=__builtin_bswapg(w);return (int)(a>>16)+b+c+d+calls*100000;}";
    check_test_program_against(test, evaluated, model, false, "bits-reversal-and-swap-arguments-evaluated-once", clang);
    check_test_program_against(test, evaluated, evaluated, true, "bits-clang-evaluates-reversal-and-swap-arguments-twice", clang);
}

// The declarations of C23's <stdbit.h> functions that the stdc library fixtures call, as clang's table has them.
#define BUSTER_CHECK_TEST_STDC_DECLARATIONS \
    "unsigned int stdc_leading_zeros_uc(unsigned char);\nunsigned int stdc_count_ones_us(unsigned short);\n" \
    "unsigned int stdc_bit_width_ui(unsigned int);\nunsigned long stdc_bit_floor_ul(unsigned long);\n" \
    "_Bool stdc_has_single_bit_ull(unsigned long long);\nunsigned char stdc_bit_ceil_uc(unsigned char);\n" \
    "unsigned int stdc_trailing_zeros_ull(unsigned long long);\nunsigned int stdc_first_leading_one_ul(unsigned long);\n"

// C23's <stdbit.h> functions as clang's library builtins (cbuiltin_stdc_library): a call of the unit's declaration of
// one, external and of the row's prototype, is the generic builtin's of its parameter's type, in C23 alone. Each fixture
// is checked by both paths against clang -fsyntax-only in its mode: the results' types, folds in every constant context
// of arguments converted to the parameters' types; a pointer's and a struct's argument, two arguments, a declaration of
// another prototype or of internal linkage, and C17, where the call is the declaration's, no constant.
static __attribute__((noinline)) void check_test_bits_stdc_library_sources(CheckTest* test, const char* clang)
{
    static const CheckSourceFixture fixtures[] =
    {
        { .name = "stdc library: result types and folds", .standard = 2023, .source =
          BUSTER_CHECK_TEST_STDC_DECLARATIONS
          "#define T(e, t) _Static_assert(_Generic((e), t: 1, default: 0), #e);\n"
          "void f(unsigned char c, int i)\n{\nT(stdc_leading_zeros_uc(c), unsigned int) T(stdc_bit_floor_ul(i), unsigned long)\n"
          "T(stdc_has_single_bit_ull(i), _Bool) T(stdc_bit_ceil_uc(i), unsigned char)\n}\n"
          "_Static_assert(stdc_leading_zeros_uc(1) == 7 && stdc_count_ones_us(-1) == 16 && stdc_bit_width_ui(5) == 3 && stdc_bit_floor_ul(100) == 64, \"\");\n"
          "_Static_assert(stdc_has_single_bit_ull(1ull << 63) && !stdc_has_single_bit_ull(3) && stdc_bit_ceil_uc(200) == 0 && stdc_bit_ceil_uc(0) == 1, \"\");\n"
          "_Static_assert(stdc_leading_zeros_uc(-1) == 0 && stdc_count_ones_us(0x10000) == 0 && stdc_leading_zeros_uc(256) == 8, \"\");\n"
          "enum { E = stdc_trailing_zeros_ull(1ull << 40) };\n"
          "static int s = stdc_first_leading_one_ul(1);\n"
          "int a[stdc_bit_ceil_uc(3)];\n"
          "_Static_assert(E == 40 && sizeof a == 4 * sizeof(int), \"\");\n" },
        { .name = "stdc library: a block's declaration and a const parameter", .standard = 2023, .source =
          "unsigned stdc_leading_zeros_ui(const unsigned);\n_Static_assert(stdc_leading_zeros_ui(1) == 31, \"\");\n"
          "unsigned f(double d)\n{\nextern unsigned stdc_count_ones_ui(unsigned);\nreturn stdc_leading_zeros_ui(d) + stdc_count_ones_ui(1.5);\n}\n" },
        { .name = "stdc library: a pointer's argument", .standard = 2023, .rejected = true, .source =
          BUSTER_CHECK_TEST_STDC_DECLARATIONS "unsigned f(int *p)\n{\nreturn stdc_bit_width_ui(p);\n}\n" },
        { .name = "stdc library: a struct's argument", .standard = 2023, .rejected = true, .source =
          BUSTER_CHECK_TEST_STDC_DECLARATIONS "struct S { int x; };\nunsigned f(struct S s)\n{\nreturn stdc_bit_width_ui(s);\n}\n" },
        { .name = "stdc library: two arguments", .standard = 2023, .rejected = true, .source =
          BUSTER_CHECK_TEST_STDC_DECLARATIONS "unsigned f(unsigned x)\n{\nreturn stdc_bit_width_ui(x, 1);\n}\n" },
        { .name = "stdc library: another prototype's declaration is no constant", .standard = 2023, .rejected = true, .source =
          "int stdc_leading_zeros_ui(int);\n_Static_assert(stdc_leading_zeros_ui(1) == 31, \"\");\n" },
        { .name = "stdc library: another prototype's declaration is called", .standard = 2023, .source =
          "int stdc_leading_zeros_ui(int);\nint f(int x)\n{\nreturn stdc_leading_zeros_ui(x);\n}\n" },
        { .name = "stdc library: a static function is no constant", .standard = 2023, .rejected = true, .source =
          "static unsigned stdc_leading_zeros_ui(unsigned x)\n{\nreturn x;\n}\n_Static_assert(stdc_leading_zeros_ui(1) == 31, \"\");\n" },
        { .name = "stdc library: C17's declaration is no constant", .rejected = true, .source =
          "unsigned stdc_leading_zeros_ui(unsigned);\n_Static_assert(stdc_leading_zeros_ui(1) == 31, \"\");\n" },
        { .name = "stdc library: C17's declaration is called", .source =
          BUSTER_CHECK_TEST_STDC_DECLARATIONS "unsigned f(unsigned x)\n{\nreturn stdc_bit_width_ui(x) + stdc_bit_ceil_uc(x);\n}\n" },
    };
    enum { FILE_COUNT = BUSTER_ARRAY_LENGTH(fixtures) };
    char directory[] = "/tmp/buster-checker-stdc-XXXXXX";
    bool ready = mkdtemp(directory) != 0;
    char paths[FILE_COUNT][160];
    for (uint32_t i = 0; ready && i < FILE_COUNT; i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        ready = parse_test_write(paths[i], fixtures[i].source, strlen(fixtures[i].source));
    }
    // Each standard's fixtures in a run of clang's own.
    for (uint32_t group = 0; ready && group < 2; group += 1)
    {
        char* pointers[FILE_COUNT];
        uint32_t indices[FILE_COUNT];
        uint32_t count = 0;
        for (uint32_t i = 0; i < FILE_COUNT; i += 1)
        {
            if ((fixtures[i].standard == 2023) == (group == 1))
            {
                pointers[count] = paths[i];
                indices[count] = i;
                count += 1;
            }
        }
        const char* arguments[] = { group == 1 ? "-std=gnu23" : "-std=gnu17" };
        ParseTestClang outcomes[FILE_COUNT];
        const bool ran = parse_test_clang(clang, arguments, BUSTER_ARRAY_LENGTH(arguments), pointers, count, outcomes);
        check_test_expect(test, ran, "the stdc library builtins' Clang oracle ran");
        for (uint32_t k = 0; ran && k < count; k += 1)
        {
            const CheckSourceFixture* fixture = fixtures + indices[k];
            char description[192];
            snprintf(description, sizeof(description), "%s: expectation differs from Clang", fixture->name);
            check_test_expect(test, outcomes[k].rejected == fixture->rejected, description);
            check_test_source(test, fixture, paths[indices[k]], &outcomes[k]);
        }
    }
    for (uint32_t i = 0; i < FILE_COUNT; i += 1)
    {
        unlink(paths[i]);
    }
    if (directory[0] != 0)
    {
        rmdir(directory);
    }
}

// The classic bit builtins (#1374, check_lower_bits_row: __builtin_bswap16 to __builtin_ffsll) of boundary values and their
// shifts, ffs's of 0 among them, at the targets their rows' forms depend on: x86-64's bsf, bsr and bswap beside the
// popcount recipe, x86-64-v2's popcnt, x86-64-v3's tzcnt and lzcnt, and znver5's. Each runs natively against clang -O0's
// build at the same -march where the host has the target's features, by both paths, and its lowering's rows are the
// target's forms (check_test_program_source_native_bounds' "bits-classic" check). The generic/C23 fixture (#1652)
// checks the same rows after its width, zero and fallback corrections, including both-zero and one-zero wide limbs.
static __attribute__((noinline)) void check_test_bits_classic(CheckTest* test, const char* clang)
{
    static const char* const marches[] = { "-march=x86-64", "-march=x86-64-v2", "-march=x86-64-v3", "-march=znver5" };
    static const char* const source =
        "static const unsigned long long values[]={0,1,2,3,0x7f,0x80,0xff,0x100,0x7fff,0x8000,0xffff,0x12345678,0x7fffffff,0x80000000,0xffffffff,"
        "0x100000000ull,0x123456789abcdef0ull,0x7fffffffffffffffull,0x8000000000000000ull,0xffffffffffffffffull,0x5555555555555555ull,37};"
        "static unsigned long long h;static void mix(unsigned long long v){h=h*31+v;}"
        "int tested(int n){h=(unsigned)n;for(unsigned i=0;i<sizeof values/sizeof values[0];i++)for(unsigned s=0;s<64;s+=7){"
        "unsigned long long v=values[i]^(values[(i+(unsigned)n)%22]<<s);unsigned u=(unsigned)v;unsigned long l=v;unsigned short w=(unsigned short)v;"
        "mix(__builtin_bswap16(w));mix(__builtin_bswap32(u));mix(__builtin_bswap64(l));"
        "if(u){mix(__builtin_clz(u));mix(__builtin_ctz(u));}"
        "if(l){mix(__builtin_clzl(l));mix(__builtin_ctzl(l));mix(__builtin_clzll(l));mix(__builtin_ctzll(l));}"
        "mix(__builtin_popcount(u));mix(__builtin_popcountl(l));mix(__builtin_popcountll(l));"
        "mix(__builtin_parity(u));mix(__builtin_parityl(l));mix(__builtin_parityll(l));"
        "mix(__builtin_ffs((int)u));mix(__builtin_ffsl((long)l));mix(__builtin_ffsll((long long)l));mix((unsigned)__builtin_ffs((int)(u&0xff00)));"
        "mix((unsigned char)__builtin_clz(u|1)+__builtin_ctzll(l|0x8000000000000000ull)+__builtin_popcount((unsigned)__builtin_bswap16(w)));}"
        "return (int)(h^(h>>32));}";
    static const char* const generic_source =
        "static unsigned long h;\n"
        "#define M(x) h=h*31+(unsigned long)(x)\n"
        "#define COUNTS(T,name) static void name(T x,int f){"
        "M(__builtin_popcountg(x));M(__builtin_clzg(x,f));M(__builtin_ctzg(x,f));"
        "M(__builtin_stdc_leading_zeros(x));M(__builtin_stdc_leading_ones(x));"
        "M(__builtin_stdc_trailing_zeros(x));M(__builtin_stdc_trailing_ones(x));"
        "M(__builtin_stdc_first_leading_zero(x));M(__builtin_stdc_first_leading_one(x));"
        "M(__builtin_stdc_first_trailing_zero(x));M(__builtin_stdc_first_trailing_one(x));"
        "M(__builtin_stdc_count_zeros(x));M(__builtin_stdc_count_ones(x));M(__builtin_stdc_has_single_bit(x));"
        "M(__builtin_stdc_bit_width(x));M(__builtin_stdc_bit_floor(x));M(__builtin_stdc_bit_ceil(x));"
        "if(x){M(__builtin_clzg(x));M(__builtin_ctzg(x));}}\n"
        "COUNTS(unsigned char,c8) COUNTS(unsigned short,c16) COUNTS(unsigned,c32) COUNTS(unsigned long,c64)\n"
        "COUNTS(unsigned _BitInt(5),c5) COUNTS(unsigned _BitInt(31),c31) COUNTS(unsigned _BitInt(33),c33)\n"
        "COUNTS(unsigned _BitInt(65),c65) COUNTS(unsigned _BitInt(96),c96) COUNTS(unsigned _BitInt(100),c100)\n"
        "COUNTS(unsigned _BitInt(127),c127) COUNTS(unsigned __int128,c128)\n"
        "enum U{U0,U1};struct B{unsigned x:5;unsigned long y:37;};\n"
        "static const unsigned long values[]={0,1,2,3,0x80,0xff,0x100,0x8000,0xffff,0x80000000,0xffffffff,"
        "0x100000000ul,0x8000000000000000ul,0xfffffffffffffffful,0x0123456789abcdeful};"
        "int tested(int n){h=(unsigned)n;for(unsigned i=0;i<sizeof values/sizeof values[0];i++){"
        "unsigned long v=values[i];int f=(n&1)?(-2147483647-1):2147483647;unsigned short s=v;_Bool b=v&1;enum U e=(enum U)(unsigned)v;struct B fields={(unsigned)v,v};"
        "c8(v,f);c16(v,f);c32(v,f);c64(v,f);c5(v,f);c31(v,f);c33(v,f);"
        "M(__builtin_popcountg(b));M(__builtin_clzg(b,f));M(__builtin_ctzg(b,f));"
        "M(__builtin_popcountg(e));M(__builtin_clzg(e,f));M(__builtin_ctzg(fields.x,f));M(__builtin_clzg(fields.y,f));"
        "M(__builtin_clrsb((int)v));M(__builtin_clrsbl((long)v));M(__builtin_clrsbll((long long)v));"
        "if(s){M(__builtin_clzs(s));M(__builtin_ctzs(s));}"
        "M(__builtin_bswapg((unsigned short)v));M(__builtin_bswapg((unsigned)v));M(__builtin_bswapg(v));"
        "M((long)__builtin_bswapg((_BitInt(48))v));"
        "for(unsigned part=0;part<3;part++){unsigned __int128 x=part==0?v:((unsigned __int128)v<<64)|(part==2?v:0);"
        "c65(x,f);c96(x,f);c100(x,f);c127(x,f);c128(x,f);"
        "unsigned __int128 swap=__builtin_bswapg(x);M(swap);M(swap>>64);"
        "swap=(unsigned __int128)__builtin_bswapg((unsigned _BitInt(80))x);M(swap);M(swap>>64);"
        "swap=__builtin_stdc_bit_floor(x);M(swap);M(swap>>64);"
        "swap=__builtin_stdc_bit_ceil(x);M(swap);M(swap>>64);}}"
        "return (int)(h^(h>>32));}";
    const X86Target native = x86_target_native();
    for (uint32_t t = 0; t < BUSTER_ARRAY_LENGTH(marches); t += 1)
    {
        const char* name = marches[t] + strlen("-march=");
        X86Target target = { 0 };
        const bool parsed = x86_target_parse(name, (uint32_t)strlen(name), &target);
        check_test_expect(test, parsed, "a classic bit builtins program's target");
        if (parsed)
        {
            const bool run_native = (native.features & target.features) == target.features;
            if (!run_native)
            {
                printf("  classic bit builtins for %s: native execution skipped (the host lacks the target's features); the programs compile and link\n", name);
            }
            check_test_program_build_release();
            check_test_program_march = marches[t];
            check_test_program_source_native_limits(test, source, "bits-classic-boundary-values", clang, false, -1, 0, &target, run_native);
            check_test_program_build_release();
            check_test_program_source_native_limits(test, generic_source, "bits-generic-boundary-values", clang, false, -1, 0, &target, run_native);
            check_test_program_build_release();
            check_test_program_march = "-march=x86-64";
        }
    }
}

static __attribute__((noinline)) void check_test_bits(CheckTest* test, const char* clang)
{
    check_test_bits_sources(test, clang);
    check_test_bits_stdc_library_sources(test, clang);
    check_test_bits_programs(test, clang);
    check_test_bits_classic(test, clang);
}

#endif
