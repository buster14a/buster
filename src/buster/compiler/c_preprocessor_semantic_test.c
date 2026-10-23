// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original source-point packing fixtures. The ordinary -E cases compare with Clang; semantic output has explicit
// expected expanded tokens, checked against both producer schedules and their complete expansion histories.

static bool pp_test_semantic_pragmas(const char* directory, bool with_clang)
{
    static const struct
    {
        const char* source;
        const char* raw;
        const char* expanded;
    } cases[] =
    {
        {
            "#define ACT push\n#define N 1\n#pragma pack(ACT,tag,N)\n#undef N\n#define N 8\n#pragma pack(pop,tag)\n",
            "#pragma pack(ACT,tag,N)\n#pragma pack(pop,tag)\n",
            "#pragma pack(push,tag,1)\n#pragma pack(pop,tag)\n",
        },
        {
            "#define N(x) x\n#pragma pack(push,N(2))\n#pragma pack(pop)\n",
            "#pragma pack(push,N(2))\n#pragma pack(pop)\n",
            "#pragma pack(push,2)\n#pragma pack(pop)\n",
        },
        {
            "#define LABEL kept\n#define N 4\n#pragma pack(push,LABEL,N)\n#undef LABEL\n#define LABEL other\n#pragma pack(pop,kept)\n",
            "#pragma pack(push,LABEL,N)\n#pragma pack(pop,kept)\n",
            "#pragma pack(push,kept,4)\n#pragma pack(pop,kept)\n",
        },
        {
            "#define ACT push\n#define N 1\n_Pragma(\"pack(ACT,tag,N)\")\n#pragma pack(pop,tag)\n",
            "#pragma pack(ACT,tag,N)\n#pragma pack(pop,tag)\n",
            "#pragma pack(push,tag,1)\n#pragma pack(pop,tag)\n",
        },
        {
            "#define ACT push\n#define N 2\n#define DO _Pragma(\"pack(ACT,tag,N)\")\nDO\n#undef N\n#define N 8\n#pragma pack(pop,tag)\n",
            "#pragma pack(ACT,tag,N)\n#pragma pack(pop,tag)\n",
            "#pragma pack(push,tag,2)\n#pragma pack(pop,tag)\n",
        },
        {
            "#define N 1\n#pragma pack(push,N)\n#undef N\n#define N 4\n#pragma pack(push,N)\n#pragma pack(pop)\n#pragma pack(pop)\n",
            "#pragma pack(push,N)\n#pragma pack(push,N)\n#pragma pack(pop)\n#pragma pack(pop)\n",
            "#pragma pack(push,1)\n#pragma pack(push,4)\n#pragma pack(pop)\n#pragma pack(pop)\n",
        },
    };
    bool passed = true;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(cases); i += 1)
    {
        const PPTestCase fixture =
        {
            .name = "raw semantic-pragma control",
            .source = cases[i].source,
            .standard = "gnu17",
            .expected = cases[i].raw,
        };
        bool valid = pp_test_case(&fixture, directory, with_clang);
        char path[PATH_MAX];
        valid = pp_test_write_case(&fixture, directory, path, sizeof(path)) && valid;
        static InternTable tables[2];
        static Preprocessor runs[2];
        bool ran[2] = { false, false };
        for (uint32_t mode = 0; valid && mode < 2; mode += 1)
        {
            const PPOptions options =
            {
                .language = { .standard = 2017, .gnu = true },
                .no_standard_includes = true,
                .semantic_pragmas = true,
                .scalar = mode == 0,
            };
            ran[mode] = pp_test_run(&runs[mode], &tables[mode], &options, path);
        }
        valid = valid && ran[0] && ran[1] && runs[0].errors == 0 && runs[1].errors == 0 &&
                pp_same_streams(&runs[0], &runs[1], "expanded semantic pragma");
        PPSpellings actual = { 0 };
        PPSpellings expected = { 0 };
        valid = valid && pp_stream_spellings(&runs[0], &actual) &&
                pp_text_spellings((const uint8_t*)cases[i].expanded, strlen(cases[i].expanded), runs[0].split_kinds,
                                  pp_test_lex_flags(&runs[0]), &expected) &&
                pp_compare_spellings(&runs[0], &actual, &expected, "expanded semantic pragma", "source-point expectation");
        if (!valid)
        {
            fprintf(stderr, "semantic pragma fixture %u failed\n", i);
        }
        passed &= valid;
        arena_release(&expected.arena);
        arena_release(&actual.arena);
        for (uint32_t mode = 0; mode < 2; mode += 1)
        {
            pp_release(&runs[mode]);
            intern_release(&tables[mode]);
        }
    }
    printf("semantic pragmas: %u source-point cases, %s\n", (uint32_t)BUSTER_ARRAY_LENGTH(cases), passed ? "pass" : "fail");
    return passed;
}
