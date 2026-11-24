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
    CheckTestFixtures catalog;
    const uint32_t file_count = check_test_private_catalog(test, &catalog, "checker/bit-builtins", NULL, 0, CHECK_TEST_CATALOG_GNU23 | CHECK_TEST_CATALOG_UNSIGNED_CHAR);
    const CheckSourceFixture* fixtures = catalog.cases;
    CheckTestOracleBatch batch;
    const bool allocated = check_test_oracle_batch(test, &batch, &catalog.storage, file_count);
    char directory[] = "/tmp/buster-checker-bits-XXXXXX";
    const bool created = allocated && mkdtemp(directory) != NULL;
    bool ready = created;
    for (uint32_t i = 0; ready && i < file_count; i += 1)
    {
        snprintf(batch.paths[i], sizeof(batch.paths[i]), "%s/%u.c", directory, i);
        ready = parse_test_write(batch.paths[i], fixtures[i].source, strlen(fixtures[i].source));
    }
    // Clang accepts the wide GNU23 builtin cases as GNU17 extensions. Keep the original GNU17 oracle commands,
    // grouped by plain char signedness, while check_test_source applies each case's checker standard.
    const char* signed_arguments[] = { "-std=gnu17" };
    const char* unsigned_arguments[] = { "-std=gnu17", "-funsigned-char" };
    for (uint32_t group = 0; ready && group < 2; group += 1)
    {
        uint32_t count = 0;
        for (uint32_t i = 0; i < file_count; i += 1)
        {
            if (fixtures[i].char_unsigned == (group != 0))
            {
                batch.pointers[count] = batch.paths[i];
                batch.indices[count++] = i;
            }
        }
        const bool ran = count == 0 ||
                         (group == 0 ? parse_test_clang(clang, signed_arguments, BUSTER_ARRAY_LENGTH(signed_arguments), batch.pointers, count, batch.outcomes) :
                                       parse_test_clang(clang, unsigned_arguments, BUSTER_ARRAY_LENGTH(unsigned_arguments), batch.pointers, count, batch.outcomes));
        check_test_expect(test, ran, "the bit builtins' Clang oracle ran");
        for (uint32_t k = 0; ran && k < count; k += 1)
        {
            const CheckSourceFixture* fixture = fixtures + batch.indices[k];
            char description[192];
            snprintf(description, sizeof(description), "%s: expectation differs from Clang", fixture->name);
            check_test_expect(test, batch.outcomes[k].rejected == (fixture->rejected && (!fixture->unsupported || fixture->undecided)), description);
            check_test_source(test, fixture, batch.paths[batch.indices[k]], &batch.outcomes[k]);
        }
    }
    for (uint32_t i = 0; created && i < file_count; i += 1)
    {
        snprintf(batch.paths[i], sizeof(batch.paths[i]), "%s/%u.c", directory, i);
        unlink(batch.paths[i]);
    }
    if (created)
    {
        rmdir(directory);
    }
    arena_release(&catalog.storage);
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

// C23's <stdbit.h> functions as clang's library builtins (cbuiltin_stdc_library): a call of the unit's declaration of
// one, external and of the row's prototype, is the generic builtin's of its parameter's type, in C23 alone. Each fixture
// is checked by both paths against clang -fsyntax-only in its mode: the results' types, folds in every constant context
// of arguments converted to the parameters' types; a pointer's and a struct's argument, two arguments, a declaration of
// another prototype or of internal linkage, and C17, where the call is the declaration's, no constant.
static __attribute__((noinline)) void check_test_bits_stdc_library_sources(CheckTest* test, const char* clang)
{
    CheckTestFixtures catalog;
    const uint32_t file_count = check_test_private_catalog(test, &catalog, "checker/stdc-bit-builtins", NULL, 0, CHECK_TEST_CATALOG_GNU23);
    const CheckSourceFixture* fixtures = catalog.cases;
    CheckTestOracleBatch batch;
    const bool allocated = check_test_oracle_batch(test, &batch, &catalog.storage, file_count);
    char directory[] = "/tmp/buster-checker-stdc-XXXXXX";
    const bool created = allocated && mkdtemp(directory) != NULL;
    bool ready = created;
    for (uint32_t i = 0; ready && i < file_count; i += 1)
    {
        snprintf(batch.paths[i], sizeof(batch.paths[i]), "%s/%u.c", directory, i);
        ready = parse_test_write(batch.paths[i], fixtures[i].source, strlen(fixtures[i].source));
    }
    // Each standard's fixtures in a run of clang's own.
    for (uint32_t group = 0; ready && group < 2; group += 1)
    {
        uint32_t count = 0;
        for (uint32_t i = 0; i < file_count; i += 1)
        {
            if ((fixtures[i].standard == 2023) == (group == 1))
            {
                batch.pointers[count] = batch.paths[i];
                batch.indices[count] = i;
                count += 1;
            }
        }
        const char* arguments[] = { group == 1 ? "-std=gnu23" : "-std=gnu17" };
        const bool ran = count == 0 || parse_test_clang(clang, arguments, BUSTER_ARRAY_LENGTH(arguments), batch.pointers, count, batch.outcomes);
        check_test_expect(test, ran, "the stdc library builtins' Clang oracle ran");
        for (uint32_t k = 0; ran && k < count; k += 1)
        {
            const CheckSourceFixture* fixture = fixtures + batch.indices[k];
            char description[192];
            snprintf(description, sizeof(description), "%s: expectation differs from Clang", fixture->name);
            check_test_expect(test, batch.outcomes[k].rejected == fixture->rejected, description);
            check_test_source(test, fixture, batch.paths[batch.indices[k]], &batch.outcomes[k]);
        }
    }
    for (uint32_t i = 0; created && i < file_count; i += 1)
    {
        snprintf(batch.paths[i], sizeof(batch.paths[i]), "%s/%u.c", directory, i);
        unlink(batch.paths[i]);
    }
    if (created)
    {
        rmdir(directory);
    }
    arena_release(&catalog.storage);
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
