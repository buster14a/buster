// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #110's cache regressions, included by c_preprocessor_test.c after pp_test_case. Every case uses that harness's
// cache-on/off comparison in both kernel modes, including token flags, spelling locations and expansion starts.
// The independent #109 generator is reused unchanged; this checks cache transparency, not its Clang qualification.

static const PPTestCase pp_cache_cases[] =
{
    {
        .name = "cache-definition-epochs",
        .source = "#define A word\nA A\n#define word 7\nA A\n#undef word\nA A\n#define word 8\nA A\n",
        .standard = "gnu17",
        .expected = "word word 7 7 word word 8 8",
    },
    {
        .name = "cache-push-pop",
        .source = "#define A word\nA A\n#pragma push_macro(\"word\")\n#define word 7\nA A\n#pragma push_macro(\"word\")\n"
                  "#undef word\nA A\n#pragma pop_macro(\"word\")\nA A\n#pragma pop_macro(\"word\")\nA A\n",
        .standard = "gnu17",
        .expected = "word word 7 7 word word 7 7 word word",
    },
    {
        .name = "cache-pragma-operator",
        .source = "#define A word\n#define RESTORE _Pragma(\"pop_macro(\\\"word\\\")\")\n"
                  "#pragma push_macro(\"word\")\n#define word 7\nA A\nRESTORE\nA A\n",
        .standard = "gnu17",
        .expected = "7 7 word word",
    },
    {
        .name = "cache-context-sensitive",
        .source = "#define L __LINE__\n#define C __COUNTER__\nL L\nL\nC C C\n#line 80 \"renamed.c\"\nL L\n"
                  "#define F(x) x\n#define A F\nA(7) A A(8)\n",
        .standard = "gnu17",
        .expected = "3 3 4 0 1 2 80 80 7 F 8",
    },
    {
        .name = "cache-disabled-and-pastes",
        .source = "#define A B\n#define B A\nA A B B\n#define P x##y\nP P\n#define xy 9\nP P\n#undef xy\nP P\n",
        .standard = "gnu17",
        .expected = "A A B B xy xy 9 9 xy xy",
    },
    {
        .name = "cache-empty-spacing",
        .source = "#define E\n#define TEXT one + two\n#define S0(x) #x\n#define S(x) S0(x)\n"
                  "S(E) S(E) S(a E+b) S(a E+b) S(TEXT) S(TEXT)\n",
        .standard = "gnu17",
        .expected = "\"\" \"\" \"a +b\" \"a +b\" \"one + two\" \"one + two\"",
    },
};

static bool pp_test_cache_guards(void)
{
    const long page = sysconf(_SC_PAGESIZE);
    bool result = page >= 4096;
    void* kind_pages = result ? mmap(0, (size_t)page * 3, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) : MAP_FAILED;
    void* value_pages = result ? mmap(0, (size_t)page * 3, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) : MAP_FAILED;
    result = result && kind_pages != MAP_FAILED && value_pages != MAP_FAILED;
    if (result)
    {
        result = mprotect((uint8_t*)kind_pages + page, (size_t)page, PROT_READ | PROT_WRITE) == 0 &&
                 mprotect((uint8_t*)value_pages + page, (size_t)page, PROT_READ | PROT_WRITE) == 0;
    }
    static Preprocessor pp;
    uint64_t is_macro = 1ull << 7;
    pp.is_macro = &is_macro;
    for (uint32_t count = 0; result && count <= 129; count += 1)
    {
        uint8_t* kinds = (uint8_t*)kind_pages + page * 2 - count;
        uint32_t* values = (uint32_t*)((uint8_t*)value_pages + page * 2) - count;
        const PPFileData data = { .kinds = kinds, .values = values };
        for (uint32_t at = 0; at < count; at += 1)
        {
            kinds[at] = C_TOKEN_RAW_IDENTIFIER | BUSTER_C_TOKEN_START_OF_LINE;
            values[at] = 6;
        }
        for (uint32_t macro = 0; result && macro <= count; macro += 1)
        {
            if (macro < count)
            {
                values[macro] = 7;
            }
            uint64_t a = 0;
            uint64_t b = 0;
            const bool vector = pp_direct_body_vector(&pp, &data, 0, count, &a);
            const bool scalar = pp_direct_body_scalar(&pp, &data, 0, count, &b);
            result = vector == scalar && vector == (macro == count) && a <= count && b <= count;
            if (macro < count)
            {
                values[macro] = 6;
            }
        }
    }
    PPMacro macros[3] = { 0 };
    macros[1].direct_epoch = 1;
    macros[2].direct_epoch = UINT64_MAX;
    pp.macros = macros;
    pp.macro_count = 3;
    pp.definition_epoch = UINT64_MAX;
    pp_next_definition_epoch(&pp);
    result = result && pp.definition_epoch == 1 && macros[1].direct_epoch == 0 && macros[2].direct_epoch == 0;
    if (kind_pages != MAP_FAILED)
    {
        munmap(kind_pages, (size_t)page * 3);
    }
    if (value_pages != MAP_FAILED)
    {
        munmap(value_pages, (size_t)page * 3);
    }
    pp = (Preprocessor){ 0 };
    printf("cache guards and epoch wrap: %s\n", result ? "pass" : "FAIL");
    return result;
}

static bool pp_test_cache_fuzz(const char* directory)
{
    char path[4096];
    snprintf(path, sizeof(path), "%s/cache-fuzz.c", directory);
    Arena arena = arena_reserve(1ull << 24);
    bool result = arena.base != 0;
    const uint64_t seeds[] = { 1, 7, 99 };
    uint32_t cases = 0;
    const uint32_t before = pp_test_cache_mismatches;
    for (uint32_t mode = 0; result && mode < 2; mode += 1)
    {
        for (uint32_t seed = 0; result && seed < BUSTER_ARRAY_LENGTH(seeds); seed += 1)
        {
            for (uint32_t id = 0; result && id < 72; id += 1)
            {
                arena_pop_to(&arena, 0);
                PreprocessorFuzzCase test;
                result = preprocessor_fuzz_generate(&arena, seeds[seed], id, mode != 0, &test);
                FILE* file = result ? fopen(path, "wb") : 0;
                result = result && file != 0;
                if (file != 0)
                {
                    result = fwrite(test.source.pointer, 1, test.source.length, file) == test.source.length && result;
                    result = fclose(file) == 0 && result;
                }
                PPOptions options = {
                    .language = { .standard = mode != 0 ? 2023 : 2017 },
                    .no_standard_includes = true,
                };
                static Preprocessor runs[2];
                static InternTable tables[2];
                bool accepted[2] = { false, false };
                for (uint32_t k = 0; result && k < 2; k += 1)
                {
                    options.scalar = k != 0;
                    accepted[k] = pp_test_run(&runs[k], &tables[k], &options, path);
                }
                result = result && accepted[0] == accepted[1] && pp_same_streams(&runs[0], &runs[1], path) && pp_test_cache_mismatches == before;
                for (uint32_t k = 0; k < 2; k += 1)
                {
                    pp_release(&runs[k]);
                    intern_release(&tables[k]);
                }
                cases += 1;
                if (!result)
                {
                    printf("cache fuzz: seed %lu, case %u, c%u differs; input %s\n", (unsigned long)seeds[seed], id, options.language.standard, path);
                }
            }
        }
    }
    if (result)
    {
        unlink(path);
    }
    arena_release(&arena);
    printf("cache fuzz: %u generated cases, both modes, three seeds: %s\n", cases, result ? "pass" : "FAIL");
    return result;
}

static bool pp_test_cache_cases(const char* directory, bool with_clang)
{
    bool result = pp_test_cache_guards();
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(pp_cache_cases); i += 1)
    {
        result = pp_test_case(&pp_cache_cases[i], directory, with_clang) && result;
    }
    result = pp_test_cache_fuzz(directory) && result;
    printf("cache fixtures: %s\n", result ? "pass" : "FAIL");
    return result;
}
