// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original packing fixtures compare record sizes and diagnostics with the installed Clang. Separate direct
// metadata checks exercise named-stack restoration, exact positions, warning-only no-ops and arena exhaustion.

#define BUSTER_CHECK_PACK_TEST_IMPLEMENTATION 1

typedef struct CheckPackFixture CheckPackFixture;
struct CheckPackFixture
{
    const char* name;
    const char* source;
    uint32_t records;
    uint32_t depth;
    uint8_t final_log2;
    bool rejected;
};

static void check_test_pack_storage(CheckTest* test, const Parser* parser, Preprocessor* pp)
{
    Arena arena = arena_reserve(UINT64_C(1) << 16);
    Arena scratch = arena_reserve(UINT64_C(1) << 16);
    const bool ready = arena.base != 0 && scratch.base != 0 && parser->tokens.pragma_count < 64;
    check_test_expect(test, ready, "pack exhaustion storage initializes");
    if (ready)
    {
        memset(arena_push(&arena, arena.capacity, 1), 0xa5, arena.capacity);
        memset(arena_push(&scratch, scratch.capacity, 1), 0x5a, scratch.capacity);
        const uint64_t output_capacity = arena.capacity;
        const uint64_t scratch_capacity = scratch.capacity;
        uint64_t required = 0;
        bool recovered = false;
        for (uint32_t pass = 0; pass < 3; pass += 1)
        {
            const uint64_t maximum = pass == 0 ? 1 : pass == 1 ? required - 31 : scratch_capacity;
            for (uint64_t limit = 0; limit < maximum && !(pass == 2 && recovered); limit += pass == 2 ? 13 : 1)
            {
                arena.length = 31;
                arena.capacity = pass == 1 ? 31 + limit : output_capacity;
                scratch.length = 29;
                scratch.capacity = pass == 2 ? 29 + limit : scratch_capacity;
                uint8_t handled[64] = { 0 };
                CheckInput output = { 0 };
                CheckInputReader reader =
                {
                    .output = &output,
                    .parser = parser,
                    .pp = pp,
                    .arena = &arena,
                    .scratch = &scratch,
                };
                const bool valid = check_input_pack_pragmas(&reader, handled);
                if (valid)
                {
                    check_test_expect(test, !output.resource && output.pack_pragmas != 0 && output.pack_pragmas->count == 11 &&
                                      output.pack_pragmas->final_log2 == 0 && output.pack_log2 != 0 && scratch.length == 29,
                                      "pack state initializes poisoned output and restores scratch");
                    required = arena.length;
                    recovered |= pass == 2;
                }
                else
                {
                    bool untouched = true;
                    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(handled); i += 1)
                    {
                        untouched &= handled[i] == 0;
                    }
                    check_test_expect(test, pass != 0 && output.resource && output.pack_pragmas == 0 && output.pack_log2 == 0 &&
                                      untouched && arena.length == 31 && scratch.length == 29,
                                      "pack exhaustion rolls back history, token state and shared handled mask");
                }
            }
        }
        check_test_expect(test, recovered, "pack helper recovers when bounded stacks fit scratch");
        arena.capacity = output_capacity;
        scratch.capacity = scratch_capacity;
    }
    arena_release(&scratch);
    arena_release(&arena);
}

static void check_test_pack_history(CheckTest* test, const CheckPackFixture* fixture, bool first, bool deep)
{
    for (uint32_t variant = 0; variant < 2; variant += 1)
    {
        InternTable identifiers = { 0 };
        Preprocessor pp = { 0 };
        Parser parser = { 0 };
        Arena arena = arena_reserve(UINT64_C(1) << 20);
        Arena scratch = arena_reserve(UINT64_C(1) << 20);
        const PPOptions options =
        {
            .language = { .standard = 2017, .gnu = true },
            .no_standard_includes = true,
        };
        const ParseOptions parse_options = { .scalar = variant == 0 };
        bool ready = arena.base != 0 && scratch.base != 0 && intern_initialize(&identifiers) && pp_initialize(&pp, &options, &identifiers) &&
                     parse_initialize(&parser, &parse_options, options.language);
        ready = ready && pp_run_source(&pp, "packing.c", (const uint8_t*)fixture->source, (uint32_t)strlen(fixture->source)) && parse_translation_unit(&parser, &pp);
        check_test_expect(test, ready, "packing source preprocesses and parses independently of retained directives");
        if (ready)
        {
            uint8_t* handled = arena_push_array(&scratch, (uint64_t)parser.tokens.pragma_count + 1, 1, 1);
            memset(handled, 0, (uint64_t)parser.tokens.pragma_count + 1);
            const uint64_t mark = scratch.length;
            CheckInput output = { 0 };
            CheckInputReader reader =
            {
                .output = &output,
                .parser = &parser,
                .pp = &pp,
                .arena = &arena,
                .scratch = &scratch,
            };
            const bool valid = check_input_pack_pragmas(&reader, handled);
            const CheckInputPackPragmas* history = output.pack_pragmas;
            check_test_expect(test, valid && !output.resource && scratch.length == mark && history != 0 &&
                              history->count == fixture->records && history->final_log2 == fixture->final_log2 &&
                              history->unmatched_pushes == fixture->depth && output.pack_log2 != 0,
                              fixture->name);
            for (uint32_t i = 0; history != 0 && i < history->count; i += 1)
            {
                const CheckInputPackPragma* record = &history->records[i];
                check_test_expect(test, record->pragma < parser.tokens.pragma_count && record->source_begin == parser.tokens.pragma_sources[record->pragma] &&
                                  record->source_end <= pp.output.count && pp.output.kinds[record->source_end - 1] == BUSTER_PP_KIND_PRAGMA_END &&
                                  record->token == parser.tokens.pragma_positions[record->pragma] && record->token <= parser.tokens.count &&
                                  handled[record->pragma] == record->handled && record->handled != 0,
                                  "pack records retain original directive ranges and parser coordinates");
                const uint32_t end = i + 1 < history->count ? history->records[i + 1].token : parser.tokens.count + 1;
                bool column = true;
                for (uint32_t token = record->token; token < end; token += 1)
                {
                    column &= output.pack_log2[token] == record->pack_after;
                }
                check_test_expect(test, column, "token packing state includes every directive before its position");
            }
            if (first && history != 0 && history->count == 11)
            {
                const uint8_t before[] = { 0, 1, 2, 3, 2, 0, 1, 1, 0, 2, 2 };
                const uint8_t after[] = { 1, 2, 3, 2, 0, 1, 1, 0, 2, 2, 0 };
                const uint32_t depths[] = { 1, 2, 3, 2, 0, 0, 1, 1, 0, 0, 0 };
                for (uint32_t i = 0; i < history->count; i += 1)
                {
                    const CheckInputPackPragma* record = &history->records[i];
                    check_test_expect(test, record->pack_before == before[i] && record->pack_after == after[i] && record->depth_after == depths[i],
                                      "named pushes, repeated labels, resets and explicit pop alignment match Clang state");
                }
                check_test_expect(test, history->maximum_depth == 3 && history->records[3].matching_push == 2 &&
                                  history->records[4].matching_push == 0 && history->records[8].matching_push == 6 &&
                                  history->records[5].warnings == CHECK_PACK_WARNING_EMPTY &&
                                  history->records[8].warnings == CHECK_PACK_WARNING_NAMED_ALIGNMENT &&
                                  history->records[9].warnings == CHECK_PACK_WARNING_SHOW,
                                  "pack history retains stack matches and warning-only effects");
                check_test_pack_storage(test, &parser, &pp);
            }
            if (deep && history != 0)
            {
                check_test_expect(test, history->maximum_depth == 128 && history->unmatched_pushes == 0 && history->records[255].depth_after == 128,
                                  "deep named pushes and repeated unmatched pops retain the exact bounded stack");
            }
        }
        arena_release(&scratch);
        arena_release(&arena);
        parse_release(&parser);
        pp_release(&pp);
        intern_release(&identifiers);
    }
}

static void check_test_pack_pragmas(CheckTest* test, const char* clang)
{
    char deep_source[32768];
    uint32_t deep_length = 0;
    for (uint32_t i = 0; i < 128; i += 1)
    {
        deep_length += (uint32_t)snprintf(deep_source + deep_length, sizeof(deep_source) - deep_length, "#pragma pack(push,same,1)\n");
    }
    for (uint32_t i = 0; i < 128; i += 1)
    {
        deep_length += (uint32_t)snprintf(deep_source + deep_length, sizeof(deep_source) - deep_length, "#pragma pack(pop,missing)\n");
    }
    for (uint32_t i = 0; i < 128; i += 1)
    {
        deep_length += (uint32_t)snprintf(deep_source + deep_length, sizeof(deep_source) - deep_length, "#pragma pack(pop,same)\n");
    }
    snprintf(deep_source + deep_length, sizeof(deep_source) - deep_length, "struct S{char c;int x;};_Static_assert(sizeof(struct S)==8,\"restore\");\n");
    const CheckPackFixture fixtures[] =
    {
        {
            .name = "named pack stack restores exact layouts",
            .source = "#pragma pack(push,outer,1)\nstruct A{char c;int x;};_Static_assert(sizeof(struct A)==5,\"A\");\n#pragma pack(push,inner,2)\nstruct B{char c;int x;};_Static_assert(sizeof(struct B)==6,\"B\");\n#pragma pack(push,outer,4)\nstruct C{char c;int x;};_Static_assert(sizeof(struct C)==8,\"C\");\n#pragma pack(pop,outer)\nstruct D{char c;int x;};_Static_assert(sizeof(struct D)==6,\"D\");\n#pragma pack(pop,outer)\nstruct E{char c;int x;};_Static_assert(sizeof(struct E)==8,\"E\");\n#pragma pack(pop,1)\nstruct F{char c;int x;};_Static_assert(sizeof(struct F)==5,\"F\");\n#pragma pack(push,tag)\n#pragma pack()\nstruct G{char c;int x;};_Static_assert(sizeof(struct G)==8,\"G\");\n#pragma pack(pop,tag,2)\nstruct H{char c;int x;};_Static_assert(sizeof(struct H)==6,\"H\");\n#pragma pack(show)\n#pragma pack(0)\n",
            .records = 11,
        },
        {
            .name = "invalid pack values and trailing tokens preserve prior alignment",
            .source = "#pragma pack(1)\n#pragma pack(3)\n#pragma pack(32)\n#pragma pack(-1)\n#pragma pack(1+1)\n#pragma pack((2))\n#pragma pack(2) extra\n#pragma pack(show,1)\n#pragma pack(push,int,2)\nstruct S{char c;int x;};_Static_assert(sizeof(struct S)==5,\"unchanged\");\n",
            .records = 9,
            .final_log2 = 1,
        },
        {
            .name = "unmatched named pop leaves stack but explicit alignment still applies",
            .source = "#pragma pack(push,a,1)\n#pragma pack(pop,missing,2)\nstruct S{char c;int x;};_Static_assert(sizeof(struct S)==6,\"set\");\n#pragma pack(pop,a)\nstruct T{char c;int x;};_Static_assert(sizeof(struct T)==8,\"saved\");\n",
            .records = 3,
        },
        {
            .name = "record opening samples pack before nested directive",
            .source = "#pragma pack(1)\nstruct Outer{char c;\n#pragma pack(2)\nstruct Inner{char c;int x;} inner;int x;};\n_Static_assert(sizeof(struct Inner)==6,\"inner\");_Static_assert(sizeof(struct Outer)==11,\"outer\");_Static_assert(_Alignof(struct Outer)==1,\"align\");\n",
            .records = 2,
            .final_log2 = 2,
        },
        {
            .name = "forward declaration does not sample later definition packing",
            .source = "struct S;\n#pragma pack(2)\nstruct S{char c;int x;};_Static_assert(sizeof(struct S)==6,\"definition\");\n",
            .records = 1,
            .final_log2 = 2,
        },
        {
            .name = "explicit record alignment composes with packing",
            .source = "#pragma pack(1)\nstruct __attribute__((aligned(16))) S{char c;int x;};_Static_assert(sizeof(struct S)==16,\"size\");_Static_assert(_Alignof(struct S)==16,\"align\");_Static_assert(__builtin_offsetof(struct S,x)==1,\"offset\");\n",
            .records = 1,
            .final_log2 = 1,
        },
        {
            .name = "union packing does not change enum alignment",
            .source = "#pragma pack(2)\nunion U{char c;double x;};enum E{A};_Static_assert(sizeof(union U)==8,\"size\");_Static_assert(_Alignof(union U)==2,\"align\");_Static_assert(_Alignof(enum E)==4,\"enum\");\n",
            .records = 1,
            .final_log2 = 2,
        },
        {
            .name = "packing outlives C function scope",
            .source = "int f(void){\n#pragma pack(1)\nstruct S{char c;int x;};return sizeof(struct S);}\nstruct T{char c;int x;};_Static_assert(sizeof(struct T)==5,\"global state\");\n",
            .records = 1,
            .final_log2 = 1,
        },
        {
            .name = "pragma operator packing retains expansion coordinates",
            .source = "#define PACK _Pragma(\"pack(push,1)\")\nPACK\nstruct S{char c;int x;};_Static_assert(sizeof(struct S)==5,\"packed\");\n_Pragma(\"pack(pop)\")\n",
            .records = 2,
        },
        {
            .name = "integer bases and suffixes are literal pack operands",
            .source = "#pragma pack(0x2u)\nstruct S{char c;int x;};_Static_assert(sizeof(struct S)==6,\"hex\");\n#pragma pack(010ULL)\nstruct T{char c;double x;};_Static_assert(sizeof(struct T)==16,\"octal\");\n#pragma pack(0b1)\n",
            .records = 3,
            .final_log2 = 1,
        },
        {
            .name = "floating pack operands are warning-only no-ops",
            .source = "#pragma pack(1)\n#pragma pack(2.0)\n#pragma pack(0x1p1)\nstruct S{char c;int x;};_Static_assert(sizeof(struct S)==5,\"float ignored\");\n",
            .records = 3,
            .final_log2 = 1,
        },
        {
            .name = "invalid numeric pack token retains hard error",
            .source = "#pragma pack(08)\nstruct S{char c;int x;};\n",
            .records = 1,
            .rejected = true,
        },
        {
            .name = "overflowing numeric pack token retains hard error",
            .source = "#pragma pack(18446744073709551616)\nstruct S{char c;int x;};\n",
            .records = 1,
            .rejected = true,
        },
        {
            .name = "pack between tag name and opening brace is invalid placement",
            .source = "struct S\n#pragma pack(1)\n{char c;int x;};\n",
            .records = 1,
            .final_log2 = 1,
            .rejected = true,
        },
        {
            .name = "pack between declaration specifier and name is accepted",
            .source = "int\n#pragma pack(1)\nx;\n",
            .records = 1,
            .final_log2 = 1,
        },
        {
            .name = "pack between parameter specifier and name is accepted",
            .source = "int f(int\n#pragma pack(1)\nx);\n",
            .records = 1,
            .final_log2 = 1,
        },
        {
            .name = "pack inside an expression is invalid placement",
            .source = "int f(void){return 1+\n#pragma pack(1)\n2;}\n",
            .records = 1,
            .final_log2 = 1,
            .rejected = true,
        },
        {
            .name = "deep named stack and repeated unmatched pop remain bounded",
            .source = deep_source,
            .records = 384,
        },
        { "pack at parameter start", "int f(\n#pragma pack(1)\nint x);\n", 1, 0, 1, false },
        { "pack after parameter name", "int f(int x\n#pragma pack(1)\n,int y);\n", 1, 0, 1, true },
        { "pack after pointer star", "int *\n#pragma pack(1)\nx;\n", 1, 0, 1, true },
        { "pack after global declarator", "int x\n#pragma pack(1)\n;\n", 1, 0, 1, true },
        { "pack before function body", "int f(void)\n#pragma pack(1)\n{return 0;}\n", 1, 0, 1, true },
        { "pack before function parameters", "int f\n#pragma pack(1)\n(void);\n", 1, 0, 1, true },
        { "pack after function parameters", "int f(void)\n#pragma pack(1)\n;\n", 1, 0, 1, true },
        { "pack before first enumerator", "enum E{\n#pragma pack(1)\nA};\n", 1, 0, 1, true },
        { "pack before later enumerator", "enum E{A,\n#pragma pack(1)\nB};\n", 1, 0, 1, true },
        { "pack inside array dimension", "int a[\n#pragma pack(1)\n2];\n", 1, 0, 1, true },
        { "pack after array declarator", "int a[2]\n#pragma pack(1)\n;\n", 1, 0, 1, true },
        { "pack before for declaration", "void f(void){for(\n#pragma pack(1)\nint x=0;x<1;x++){} }\n", 1, 0, 1, true },
        { "pack after for declaration type", "void f(void){for(int\n#pragma pack(1)\nx=0;x<1;x++){} }\n", 1, 0, 1, false },
        { "pack before cast type", "int f(int x){return (\n#pragma pack(1)\nint)x;}\n", 1, 0, 1, true },
        { "pack after sizeof type", "int x=sizeof(int\n#pragma pack(1)\n);\n", 1, 0, 1, false },
        { "pack after typeof type", "typeof(int\n#pragma pack(1)\n) x;\n", 1, 0, 1, false },
        { "pack after atomic type", "_Atomic(int\n#pragma pack(1)\n) x;\n", 1, 0, 1, false },
        { "pack before empty return terminator", "void f(void){return\n#pragma pack(1)\n;}\n", 1, 0, 1, true },
        { "pack inside initializer", "int x[]={1,\n#pragma pack(1)\n2};\n", 1, 0, 1, true },
        { "pack after record type", "struct S{int x;}\n#pragma pack(1)\ns;\n", 1, 0, 1, false },
        { "pack after tag reference", "struct S{int x;};struct S\n#pragma pack(1)\ns;\n", 1, 0, 1, false },
        { "pack after declaration comma", "int x,\n#pragma pack(1)\ny;\n", 1, 0, 1, true },
        { "pack before record member", "struct S{int x;\n#pragma pack(1)\nint y;};\n", 1, 0, 1, false },
        { "pack before record close", "struct S{int x;\n#pragma pack(1)\n};\n", 1, 0, 1, false },
        { "invalid alignment still creates parser annotation", "int f(void){return 1+\n#pragma pack(3)\n2;}\n", 1, 0, 0, true },
        { "malformed pack creates no parser annotation", "int f(void){return 1+\n#pragma pack(1,2)\n2;}\n", 1, 0, 0, false },
        { "pack interrupts pending else", "int f(int x){if(x)x++;\n#pragma pack(1)\nelse x--;return x;}\n", 1, 0, 1, true },
        { "pack interrupts pending do while", "int f(int x){do{x++;}\n#pragma pack(1)\nwhile(x<2);return x;}\n", 1, 0, 1, true },
        { "pack interrupts do while terminator", "int f(int x){do{x++;}while(x<2)\n#pragma pack(1)\n;return x;}\n", 1, 0, 1, true },
        { "pack after typedef type", "typedef int T;T\n#pragma pack(1)\nx;\n", 1, 0, 1, false },
        { "pack after declaration qualifier", "int const\n#pragma pack(1)\nx=1;\n", 1, 0, 1, false },
        { "pack after pointer qualifier", "int *const\n#pragma pack(1)\nx;\n", 1, 0, 1, true },
        { "pack before pointer declarator", "int\n#pragma pack(1)\n*x;\n", 1, 0, 1, false },
        { "pack interrupts named struct keyword", "struct\n#pragma pack(1)\nS{int x;};\n", 1, 0, 1, true },
        { "pack interrupts anonymous struct keyword", "struct\n#pragma pack(1)\n{int x;}s;\n", 1, 0, 1, true },
        { "pack after prefix attribute", "__attribute__((unused))\n#pragma pack(1)\nint x;\n", 1, 0, 1, false },
        { "pack after postfix attribute", "int x __attribute__((unused))\n#pragma pack(1)\n;\n", 1, 0, 1, true },
        { "pack after parameter comma", "int f(int x,\n#pragma pack(1)\nint y);\n", 1, 0, 1, false },
        { "pack after expression argument comma", "int f(int,int);int x;int g(void){return f(x,\n#pragma pack(1)\nx);}\n", 1, 0, 1, true },
        { "pack after forward tag", "struct S\n#pragma pack(1)\n;\n", 1, 0, 1, false },
        { "pack after local declarator", "void f(void){int x\n#pragma pack(1)\n;}\n", 1, 0, 1, true },
        { "pack after member declarator", "struct S{int x\n#pragma pack(1)\n;};\n", 1, 0, 1, true },
        { "pack after scalar initializer", "int x=1\n#pragma pack(1)\n;\n", 1, 0, 1, true },
        { "pack after sizeof keyword", "int x=sizeof\n#pragma pack(1)\n(int);\n", 1, 0, 1, true },
        { "pack after typeof specifier", "typeof(int)\n#pragma pack(1)\nx;\n", 1, 0, 1, false },
        { "pack after alignment specifier", "_Alignas(8)\n#pragma pack(1)\nint x;\n", 1, 0, 1, false },
        { "pack after generic association type", "int x=_Generic(1,int\n#pragma pack(1)\n:1);\n", 1, 0, 1, false },
        { "pack after bitfield width", "struct S{int x:1\n#pragma pack(1)\n;};\n", 1, 0, 1, true },
        { "pack after top level asm", "asm(\"\")\n#pragma pack(1)\n;\n", 1, 0, 1, true },
    };
    enum { FILE_COUNT = BUSTER_ARRAY_LENGTH(fixtures) };
    char directory[] = "/tmp/buster-packing-XXXXXX";
    char paths[FILE_COUNT][256];
    char* pointers[FILE_COUNT];
    ParseTestClang outcomes[FILE_COUNT];
    bool ready = mkdtemp(directory) != 0;
    for (uint32_t i = 0; ready && i < FILE_COUNT; i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        pointers[i] = paths[i];
        ready = parse_test_write(paths[i], fixtures[i].source, (uint32_t)strlen(fixtures[i].source));
    }
    const char* arguments[] = { "-std=gnu17", "-fsyntax-only", "-w" };
    ready = ready && parse_test_clang(clang, arguments, BUSTER_ARRAY_LENGTH(arguments), pointers, FILE_COUNT, outcomes);
    check_test_expect(test, ready, "packing Clang source oracle runs");
    for (uint32_t i = 0; ready && i < FILE_COUNT; i += 1)
    {
        const CheckPackFixture* fixture = &fixtures[i];
        check_test_expect(test, outcomes[i].rejected == fixture->rejected, fixture->name);
        check_test_pack_history(test, fixture, i == 0, fixture->records == 384);
        const CheckSourceFixture semantic =
        {
            .name = fixture->name,
            .source = fixture->source,
            .rejected = fixture->rejected,
        };
        check_test_source(test, &semantic, paths[i], &outcomes[i]);
    }
    for (uint32_t i = 0; i < FILE_COUNT; i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        unlink(paths[i]);
    }
    rmdir(directory);
}
