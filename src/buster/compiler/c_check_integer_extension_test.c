// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original GNU folding fixtures and generated target-bit assertions. Clang is an external oracle only. Tests
// keep the strict fault result and the explicit extension witness separate, including across 128-bit counts.
#ifndef BUSTER_C_CHECK_INTEGER_EXTENSION_TEST_INCLUDED
#define BUSTER_C_CHECK_INTEGER_EXTENSION_TEST_INCLUDED

typedef struct CheckIntegerExtensionFixture CheckIntegerExtensionFixture;
struct CheckIntegerExtensionFixture
{
    CheckOp op;
    uint64_t left;
    uint64_t right;
    uint64_t answer;
    CheckFault fault;
    const char* source;
};

static bool check_test_integer_extension_bits(FILE* source, unsigned __int128 value)
{
    return fprintf(source, "((((unsigned __int128)0x%016llxULL)<<64)|0x%016llxULL)",
                   (unsigned long long)(value >> 64), (unsigned long long)value) > 0;
}

static bool check_test_integer_extension_assertion(FILE* source, CheckIntegerFold fold, unsigned __int128 expected, uint32_t index)
{
    const bool unary = fold.op == CHECK_OP_NEGATIVE;
    const char* operation = "";
    switch (fold.op)
    {
        case CHECK_OP_ADD: operation = "+"; break;
        case CHECK_OP_SUBTRACT: operation = "-"; break;
        case CHECK_OP_MULTIPLY: operation = "*"; break;
        case CHECK_OP_DIVIDE: operation = "/"; break;
        case CHECK_OP_REMAINDER: operation = "%"; break;
        case CHECK_OP_SHIFT_LEFT: operation = "<<"; break;
        case CHECK_OP_SHIFT_RIGHT: operation = ">>"; break;
        default: break;
    }
    bool written = fprintf(source, "_Static_assert((((unsigned __int128)(%s(%s _BitInt(%u))", unary ? "-" : "", fold.is_signed ? "signed" : "unsigned", fold.width) > 0;
    written = check_test_integer_extension_bits(source, fold.left) && written;
    if (!unary)
    {
        written = fprintf(source, "%s(%s _BitInt(%u))", operation, fold.right_signed ? "signed" : "unsigned", fold.right_width) > 0 && written;
        written = check_test_integer_extension_bits(source, fold.right) && written;
    }
    written = fprintf(source, "))&") > 0 && written;
    written = check_test_integer_extension_bits(source, check_action_mask(fold.width)) && written;
    written = fprintf(source, ")==") > 0 && written;
    written = check_test_integer_extension_bits(source, expected) && written;
    written = fprintf(source, ",\"integer extension %u\");\n", index) > 0 && written;
    return written;
}

static void check_test_integer_source_warnings(CheckTest* test, const char* clang, const char* source_path, const char* output_path, const char* error_path)
{
    const char* sources[] =
    {
        "enum { N=2147483647+1 };\n",
        "enum { N=(-2147483647-1)/-1 };\n",
        "enum { N=1<<32 };\n",
        "enum { N=-1<<1 };\n",
        "enum { N=1073741824<<2 };\n",
        "enum { N=(int)(1.0+2.0) };\n",
        "enum { N=(1,3) };\n",
        "int a[(2147483647+1)<0];\n",
        "int a[(1<<32)<0];\n",
        "int a[(int)(1.0+2.0)];\n",
        "void f(void){int a[(int)(1.0+2.0)];}\n",
        "void f(void){int a[(2147483647+1)<0];}\n",
        "struct S {int a[(int)(1.0+2.0)];};\n",
        "typedef int T [[gnu::aligned((0 ? (2147483647+1) : 16))]];\n",
        "typedef int T [[gnu::aligned(sizeof(2147483647+1))]];\n",
        "typedef int T [[gnu::aligned(((1<<33)<0)?16:8)]];\n",
        "enum { N=sizeof(2147483647+1) };\n",
        "enum { N=1||(2147483647+1) };\n",
        "enum { N=0 ? sizeof(int[(2147483647+1)<0]) : 1 };\n",
        "enum { N=0 ? sizeof(int[(1<<32)<0]) : 1 };\n",
        "int f(void){return __builtin_choose_expr(1,0,(2147483647+1));}\n",
        "int f(void){return __builtin_choose_expr(1,0,(1<<32));}\n",
        "int f(void){return _Generic(1,int:0,default:(2147483647+1));}\n",
        "int f(void){return _Generic(1,int:0,default:(1<<32));}\n",
        "int f(void){return _Generic((1<<32),int:0,default:1);}\n",
        "int f(void){return _Generic((1073741824<<2),int:0,default:1);}\n",
        "int f(void){return __builtin_choose_expr((1<<32)<0,1,2);}\n",
        "enum { N=__builtin_choose_expr((1<<32)<0,1,2) };\n",
        "typedef __typeof__(1<<32) T;\n",
        "typedef __typeof__(1073741824<<2) T;\n",
        "typedef _BitInt((8>>-1)+1) T;\n",
        "enum { N=__builtin_constant_p((int)1.0e100+1) };\n",
        "enum { N=__builtin_constant_p((__int128)1.0e100+1) };\n",
        "typedef int T [[gnu::aligned(__builtin_constant_p((int)1.0e100+1)*16)]];\n",
        "int f(void){return __builtin_constant_p((int)1.0e100+1);}\n",
        "enum { N=__builtin_constant_p((int)1.0e100) };\n",
        "enum { N=__builtin_constant_p((unsigned)-1.0e100+1) };\n",
    };
    const struct
    {
        const char* group;
        uint32_t warning;
    } groups[] =
    {
        { "[-Wgnu-folding-constant]", CHECK_WARNING_GNU_CONSTANT },
        { "[-Winteger-overflow]", CHECK_WARNING_INTEGER_OVERFLOW },
        { "[-Wshift-count-negative]", CHECK_WARNING_SHIFT_COUNT_NEGATIVE },
        { "[-Wshift-count-overflow]", CHECK_WARNING_SHIFT_COUNT_OVERFLOW },
        { "[-Wshift-negative-value]", CHECK_WARNING_SHIFT_NEGATIVE_VALUE },
        { "[-Wshift-overflow]", CHECK_WARNING_SHIFT_OVERFLOW },
    };
    Arena diagnostics = arena_reserve(65536);
    check_test_expect(test, diagnostics.base != NULL, "source warning oracle diagnostic allocation");
    for (uint32_t fixture = 0; diagnostics.base != NULL && fixture < BUSTER_ARRAY_LENGTH(sources); fixture += 1)
    {
        const char* source = sources[fixture];
        uint32_t oracle_warnings[2] = { 0 };
        bool oracle = parse_test_write(source_path, source, strlen(source));
        for (uint32_t enabled = 0; oracle && enabled < 2; enabled += 1)
        {
            char* arguments[] = { (char*)clang, "-std=gnu17", "-fsyntax-only", "-fno-color-diagnostics",
                                  enabled ? "-Wgnu-folding-constant" : "-Wno-unused-value", (char*)source_path, NULL };
            oracle = object_test_run(arguments, output_path, error_path) == 0;
            const String8 text = oracle ? object_test_read_file(&diagnostics, error_path) : (String8){ 0 };
            for (uint32_t group = 0; group < BUSTER_ARRAY_LENGTH(groups); group += 1)
            {
                if (text.pointer != NULL && memmem(text.pointer, text.length, groups[group].group, strlen(groups[group].group)) != NULL)
                {
                    oracle_warnings[enabled] |= groups[group].warning;
                }
            }
            arena_pop_to(&diagnostics, 0);
        }
        check_test_expect(test, oracle, "source warning contexts compile in independent Clang oracle");
        for (uint32_t path = 0; oracle && path < 2; path += 1)
        {
            static InternTable identifiers;
            static Preprocessor pp;
            static Parser parser;
            const PPOptions options = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true };
            const ParseOptions parse_options = { .scalar = path == 0 };
            CheckTestTypes types = { 0 };
            Arena storage = arena_reserve(UINT64_C(1) << 24);
            Arena scratch = arena_reserve(UINT64_C(1) << 24);
            Arena output = arena_reserve(UINT64_C(1) << 24);
            bool ready = storage.base != NULL && scratch.base != NULL && output.base != NULL && intern_initialize(&identifiers) &&
                         pp_initialize(&pp, &options, &identifiers) && parse_initialize(&parser, &parse_options, options.language) && check_test_types_open(&types) &&
                         pp_run_source(&pp, "integer-warning-source.c", (const uint8_t*)source, strlen(source)) && parse_translation_unit(&parser, &pp);
            CheckInput input = { 0 };
            CheckStream stream = { 0 };
            const CheckOptions check_options = { .scalar = path == 0 };
            ready = ready && parser.errors == 0 && check_input(&input, &parser, &pp, &types.table, &storage, &scratch) &&
                    check_graph_run(&types.table, &input.graph, &output, &scratch, &check_options, &stream) && stream.report_count == 0;
            uint32_t all = 0;
            uint32_t visible = 0;
            for (uint32_t token = 0; ready && token < parser.tokens.count; token += 1)
            {
                all |= input.constant_warnings[token];
                visible |= input.constant_warnings[token] & ~input.constant_warning_ignored[token];
            }
            for (uint32_t node = 0; ready && node < input.graph.count; node += 1)
            {
                all |= stream.warnings[node];
                visible |= stream.warnings[node] & ~stream.warning_ignored[node];
            }
            char description[512];
            snprintf(description, sizeof(description), "source warning facts fixture %u path %u: ready=%u all=%02x/%02x default=%02x/%02x %.300s",
                     fixture, path, ready, all, oracle_warnings[1], visible, oracle_warnings[0], source);
            check_test_expect(test, ready && all == oracle_warnings[1] && visible == oracle_warnings[0], description);
            check_test_types_close(&types);
            arena_release(&storage);
            arena_release(&scratch);
            arena_release(&output);
            parse_release(&parser);
            pp_release(&pp);
            intern_release(&identifiers);
        }
    }
    arena_release(&diagnostics);
}

static void check_test_integer_extensions(CheckTest* test, const char* clang)
{
    const CheckIntegerExtensionFixture fixtures[] = {
        { CHECK_OP_NEGATIVE, 0x80000000, 0, 0x80000000, CHECK_FAULT_SIGNED_OVERFLOW, "-(-2147483647-1)==(-2147483647-1)" },
        { CHECK_OP_ADD, 0x7fffffff, 1, 0x80000000, CHECK_FAULT_SIGNED_OVERFLOW, "2147483647+1==(-2147483647-1)" },
        { CHECK_OP_SUBTRACT, 0x80000000, 1, 0x7fffffff, CHECK_FAULT_SIGNED_OVERFLOW, "(-2147483647-1)-1==2147483647" },
        { CHECK_OP_MULTIPLY, 50000, 50000, 2500000000u, CHECK_FAULT_SIGNED_OVERFLOW, "(unsigned)(50000*50000)==2500000000u" },
        { CHECK_OP_DIVIDE, 0x80000000, 0xffffffff, 0x80000000, CHECK_FAULT_SIGNED_OVERFLOW, "(-2147483647-1)/-1==(-2147483647-1)" },
        { CHECK_OP_REMAINDER, 0x80000000, 0xffffffff, 0, CHECK_FAULT_SIGNED_OVERFLOW, "(-2147483647-1)%-1==0" },
        { CHECK_OP_SHIFT_LEFT, 1, 32, 0x80000000, CHECK_FAULT_SHIFT_COUNT, "(1<<32)==(-2147483647-1)" },
        { CHECK_OP_SHIFT_LEFT, 1, 33, 0x80000000, CHECK_FAULT_SHIFT_COUNT, "(1<<33)==(-2147483647-1)" },
        { CHECK_OP_SHIFT_LEFT, 1, 0xffffffff, 0, CHECK_FAULT_SHIFT_COUNT, "(1<<-1)==0" },
        { CHECK_OP_SHIFT_RIGHT, 8, 32, 0, CHECK_FAULT_SHIFT_COUNT, "(8>>32)==0" },
        { CHECK_OP_SHIFT_RIGHT, 8, 0xffffffff, 16, CHECK_FAULT_SHIFT_COUNT, "(8>>-1)==16" },
        { CHECK_OP_SHIFT_LEFT, 0xfffffff8, 0xffffffff, 0xfffffffc, CHECK_FAULT_SHIFT_COUNT, "(-8<<-1)==-4" },
        { CHECK_OP_SHIFT_LEFT, 0xfffffff8, 1, 0xfffffff0, CHECK_FAULT_SHIFT_VALUE, "(-8<<1)==-16" },
        { CHECK_OP_SHIFT_LEFT, 0x40000000, 2, 0, CHECK_FAULT_SHIFT_VALUE, "(1073741824<<2)==0" },
        { CHECK_OP_SHIFT_RIGHT, 8, 0x80000000, 0, CHECK_FAULT_SHIFT_COUNT, "(8>>(-2147483647-1))==0" },
    };
    char directory[] = "/tmp/buster-checker-integer-extension-XXXXXX";
    const bool directory_ready = mkdtemp(directory) != NULL;
    char source_path[160];
    char output_path[160];
    char error_path[160];
    snprintf(source_path, sizeof(source_path), "%s/oracle.c", directory);
    snprintf(output_path, sizeof(output_path), "%s/out", directory);
    snprintf(error_path, sizeof(error_path), "%s/err", directory);
    FILE* source = directory_ready ? fopen(source_path, "w") : NULL;
    bool written = source != NULL;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(fixtures); i += 1)
    {
        const CheckIntegerExtensionFixture fixture = fixtures[i];
        const CheckIntegerFold fold = {
            .active = true,
            .op = fixture.op,
            .left = fixture.left,
            .right = fixture.right,
            .width = 32,
            .right_width = 32,
            .is_signed = true,
            .right_signed = true,
            .node = i + 1,
        };
        CheckValue strict = { .flags = BUSTER_CHECK_ICE };
        check_action_fold_integer(fold, &strict);
        const CheckIntegerExtension extension = check_integer_extension(fold, strict.fault);
        check_test_expect(test, strict.fault == fixture.fault && strict.fault_node == fold.node && strict.constant_kind == CHECK_CONSTANT_NONE,
                          "strict integer folding preserves its original fault");
        check_test_expect(test, extension.applied && extension.fault == fixture.fault && extension.integer == fixture.answer,
                          "GNU integer extension has independent target bits and a fault witness");
        if (written)
        {
            written = fprintf(source, "_Static_assert(%s,\"handwritten integer extension %u\");\n", fixture.source, i) > 0;
        }
    }
    const CheckIntegerFold no_division = { .active = true, .op = CHECK_OP_DIVIDE, .left = 1, .width = 128, .right_width = 128, .is_signed = true, .right_signed = true };
    for (uint32_t fault = CHECK_FAULT_NONE; fault <= CHECK_FAULT_UNSUPPORTED_CONSTANT; fault += 1)
    {
        const CheckIntegerExtension extension = check_integer_extension(no_division, (CheckFault)fault);
        check_test_expect(test, !extension.applied, "GNU extension never invents a value for division by zero or unrelated faults");
    }
    const uint32_t widths[] = { 8, 32, 64, 65, 128 };
    const uint32_t right_widths[] = { 32, 64, 128 };
    uint32_t index = 0;
    uint32_t extended = 0;
    for (uint32_t w = 0; w < BUSTER_ARRAY_LENGTH(widths); w += 1)
    {
        const uint32_t width = widths[w];
        const unsigned __int128 sign = (unsigned __int128)1 << (width - 1);
        const unsigned __int128 mask = check_action_mask(width);
        const unsigned __int128 left_values[] = { 1, 8, sign, mask };
        for (uint32_t r = 0; r < BUSTER_ARRAY_LENGTH(right_widths); r += 1)
        {
            const uint32_t right_width = right_widths[r];
            const unsigned __int128 right_values[] = { width - 1, width, width + 1, check_action_mask(right_width), (unsigned __int128)1 << (right_width - 1) };
            for (uint32_t a = 0; a < BUSTER_ARRAY_LENGTH(left_values); a += 1)
            {
                for (uint32_t b = 0; b < BUSTER_ARRAY_LENGTH(right_values); b += 1)
                {
                    for (uint32_t flags = 0; flags < 8; flags += 1)
                    {
                        const CheckIntegerFold fold = {
                            .active = true,
                            .op = (flags & 1) != 0 ? CHECK_OP_SHIFT_LEFT : CHECK_OP_SHIFT_RIGHT,
                            .left = left_values[a],
                            .right = right_values[b],
                            .width = width,
                            .right_width = right_width,
                            .is_signed = (flags & 2) != 0,
                            .right_signed = (flags & 4) != 0,
                            .node = index,
                        };
                        CheckValue strict = { .flags = BUSTER_CHECK_ICE };
                        check_action_fold_integer(fold, &strict);
                        const CheckIntegerExtension extension = check_integer_extension(fold, strict.fault);
                        const bool required = strict.fault != CHECK_FAULT_NONE;
                        check_test_expect(test, extension.applied == required && extension.fault == strict.fault,
                                          "generated integer shifts retain strict versus extension provenance");
                        const unsigned __int128 answer = required ? extension.integer : strict.integer;
                        check_test_expect(test, (answer & ~mask) == 0, "generated extension result fits its target width");
                        extended += extension.applied;
                        if (written)
                        {
                            written = check_test_integer_extension_assertion(source, fold, answer, index);
                        }
                        index += 1;
                    }
                }
            }
        }
    }
    check_test_expect(test, index == 2400 && extended > 1000, "integer oracle covers signedness, target widths, reversed counts and saturation");
    const uint32_t arithmetic_widths[] = { 64, 65, 127, 128 };
    const CheckOp operations[] = { CHECK_OP_NEGATIVE, CHECK_OP_ADD, CHECK_OP_SUBTRACT, CHECK_OP_MULTIPLY, CHECK_OP_DIVIDE, CHECK_OP_REMAINDER };
    for (uint32_t w = 0; w < BUSTER_ARRAY_LENGTH(arithmetic_widths); w += 1)
    {
        const uint32_t width = arithmetic_widths[w];
        const unsigned __int128 sign = (unsigned __int128)1 << (width - 1);
        const unsigned __int128 mask = check_action_mask(width);
        const unsigned __int128 answers[] = { sign, sign, sign - 1, mask - 1, sign, 0 };
        for (uint32_t o = 0; o < BUSTER_ARRAY_LENGTH(operations); o += 1)
        {
            const CheckIntegerFold fold = {
                .active = true,
                .op = operations[o],
                .left = o == 1 || o == 3 ? sign - 1 : sign,
                .right = o >= 4 ? mask : o == 3 ? 2 : 1,
                .width = width,
                .right_width = width,
                .is_signed = true,
                .right_signed = true,
                .node = index,
            };
            CheckValue strict = { 0 };
            check_action_fold_integer(fold, &strict);
            const CheckIntegerExtension extension = check_integer_extension(fold, strict.fault);
            check_test_expect(test, strict.fault == CHECK_FAULT_SIGNED_OVERFLOW && strict.constant_kind == CHECK_CONSTANT_NONE &&
                              extension.applied && extension.fault == CHECK_FAULT_SIGNED_OVERFLOW && extension.integer == answers[o],
                              "wide overflow folds use independent modulo answers and retain the extension witness");
            if (written)
            {
                written = check_test_integer_extension_assertion(source, fold, answers[o], index);
            }
            index += 1;
        }
    }
    if (source != NULL)
    {
        written = fclose(source) == 0 && written;
    }
    char* arguments[] = { (char*)clang, "-std=gnu17", "-fsyntax-only", "-Wno-everything", source_path, NULL };
    check_test_expect(test, written && object_test_run(arguments, output_path, error_path) == 0,
                      "Clang independently confirms all generated GNU integer extension values");
    source = directory_ready ? fopen(source_path, "w") : NULL;
    written = source != NULL;
    if (written)
    {
        written = fprintf(source, "_Static_assert((2147483647+1)==(-2147483647-1),\"overflow\");\n_Static_assert((1<<32)==(-2147483647-1),\"shift\");\n") > 0;
        written = fclose(source) == 0 && written;
    }
    arguments[3] = "-Werror=gnu-folding-constant";
    const bool diagnosed = written && object_test_run(arguments, output_path, error_path) != 0;
    Arena oracle_arena = arena_reserve(32768);
    const String8 diagnostics = diagnosed ? object_test_read_file(&oracle_arena, error_path) : (String8){ 0 };
    check_test_expect(test, diagnosed && diagnostics.pointer != NULL &&
                      memmem(diagnostics.pointer, diagnostics.length, "GNU extension", sizeof("GNU extension") - 1) != NULL,
                      "Clang's explicit GNU-folding diagnostic preserves the strict-policy boundary");
    arena_release(&oracle_arena);
    const CheckSourceFixture source_fixtures[] = {
        {
            .name = "GNU overflow enumeration retains target arithmetic",
            .source = "enum { A=2147483647+1, B=(-2147483647-1)/-1, C=(-2147483647-1)%-1, D=-(-2147483647-1), E=-8<<1 }; _Static_assert(A==B && B==D && C==0 && E==-16,\"wrapped integers\");\n",
        },
        {
            .name = "GNU constant array bound and raw attribute",
            .source = "int a[((1<<32)==(-2147483647-1))*3]; typedef int I [[gnu::aligned(((1<<33)==(-2147483647-1))*16)]]; _Static_assert(sizeof a==12 && _Alignof(I)==16,\"folded bound and attribute\");\n",
        },
        {
            .name = "GNU negative shifts and bit-precise type width",
            .source = "typedef _BitInt((8>>-1)+1) I; enum { A=1<<-1, B=8>>32, C=-8<<-1 }; _Static_assert(sizeof(I)==4 && A==0 && B==0 && C==-4,\"reversed shifts\");\n",
        },
        {
            .name = "GNU full-width overflow and shift constants",
            .source = "_Static_assert((((__int128)1<<126)*2)==((__int128)1<<127),\"wide multiply\"); _Static_assert((-((__int128)1<<127))==((__int128)1<<127),\"wide negate\"); _Static_assert((((__int128)1<<127)/-1)==((__int128)1<<127),\"wide divide\"); _Static_assert((((__int128)1<<127)%-1)==0,\"wide remainder\"); _Static_assert(((unsigned __int128)1<<128)==((unsigned __int128)1<<127),\"wide saturation\");\n",
        },
        {
            .name = "GNU complex integer product wraps its components",
            .source = "static _Complex int value=(50000+0i)*(50000+0i); _Static_assert(__real__ ((50000+0i)*(50000+0i))==-1794967296,\"wrapped real component\"); _Static_assert(__imag__ ((50000+0i)*(50000+0i))==0,\"imaginary component\");\n",
        },
        {
            .name = "GNU wrapped ICE zero remains a null pointer constant",
            .source = "int *p=(1<<-1); int *q=(1<<-1)==0 ? (1<<-1) : (2<<-2); int f(void){int *r=(int)(1<<-1); return p==q && q==r;}\n",
        },
        {
            .name = "GNU folded comma zero is not a null pointer constant",
            .source = "int *p=(0,0);\n",
            .rejected = true,
        },
        {
            .name = "GNU computed floating zero is not a null pointer constant",
            .source = "int *p=(int)(0.5-0.5);\n",
            .rejected = true,
        },
        {
            .name = "GNU statement expression zero is not a null pointer constant",
            .source = "int f(void){int *p=({0;});return p==0;}\n",
            .rejected = true,
        },
        {
            .name = "GNU folding still rejects an evaluated zero divisor",
            .source = "enum { A=1/0 };\n",
            .rejected = true,
        },
    };
    char* paths[] = { source_path };
    const char* source_arguments[] = { "-std=gnu17" };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(source_fixtures); i += 1)
    {
        const CheckSourceFixture* fixture = source_fixtures + i;
        ParseTestClang outcome = { 0 };
        const bool oracle = directory_ready && parse_test_write(source_path, fixture->source, strlen(fixture->source)) &&
                            parse_test_clang(clang, source_arguments, BUSTER_ARRAY_LENGTH(source_arguments), paths, 1, &outcome);
        check_test_expect(test, oracle && outcome.rejected == fixture->rejected, fixture->name);
        if (oracle)
        {
            check_test_source(test, fixture, source_path, &outcome);
        }
    }
    if (directory_ready)
    {
        check_test_integer_source_warnings(test, clang, source_path, output_path, error_path);
        unlink(source_path);
        unlink(output_path);
        unlink(error_path);
        rmdir(directory);
    }
}

#endif
