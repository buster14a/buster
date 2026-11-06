// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original source-point pragma fixtures. The ordinary -E cases compare with Clang; semantic output has explicit
// expected expanded tokens, checked against both producer schedules and their complete expansion histories.

static bool pp_test_eval_method_uses(const char* directory, bool with_clang)
{
    static const struct
    {
        const char* source;
        const char* output;
        uint32_t count;
        uint32_t positions[3];
        uint32_t pragmas[3];
        uint32_t macro_uses;
    } cases[] =
    {
        {
            .source = "int a=__FLT_EVAL_METHOD__;\n",
            .output = "int a=0;\n",
            .count = 1,
            .positions = { 3 },
        },
        {
            .source = "#if __FLT_EVAL_METHOD__ == 0\nint before;\n#endif\n#pragma clang fp eval_method(source)\n"
                      "#if __FLT_EVAL_METHOD__ == 0\n#endif\n#pragma clang fp eval_method(double)\nint after=__FLT_EVAL_METHOD__;\n",
            .output = "int before;\n#pragma clang fp eval_method(source)\n#pragma clang fp eval_method(double)\nint after=0;\n",
            .count = 3,
            .positions = { 0, 12, 24 },
            .pragmas = { 0, 1, 2 },
        },
        {
            .source = "#define EVAL __FLT_EVAL_METHOD__\n#define CALL() EVAL\n#if CALL() == 0\n#endif\nint a=CALL();\n",
            .output = "int a=0;\n",
            .count = 2,
            .positions = { 0, 3 },
            .macro_uses = 3,
        },
        {
            .source = "#define MODE _Pragma(\"clang fp eval_method(double)\")\n#if MODE __FLT_EVAL_METHOD__\n#endif\n",
            .output = "#pragma clang fp eval_method(double)\n",
            .count = 1,
            .pragmas = { 1 },
        },
        {
            .source = "#if 0\n__FLT_EVAL_METHOD__\n#endif\n#if defined(__FLT_EVAL_METHOD__)\n#endif\n"
                      "#define S(x) #x\nchar* a=S(__FLT_EVAL_METHOD__);\n",
            .output = "char* a=\"__FLT_EVAL_METHOD__\";\n",
        },
    };
    bool passed = true;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(cases); i += 1)
    {
        const PPTestCase fixture =
        {
            .name = "floating evaluation macro preprocessing control",
            .source = cases[i].source,
            .standard = "gnu17",
            .expected = cases[i].output,
        };
        bool valid = pp_test_case(&fixture, directory, with_clang);
        char path[PATH_MAX];
        valid = pp_test_write_case(&fixture, directory, path, sizeof(path)) && valid;
        static InternTable tables[4];
        static Preprocessor runs[4];
        for (uint32_t mode = 0; valid && mode < 4; mode += 1)
        {
            const PPOptions options =
            {
                .language = { .standard = 2017, .gnu = true },
                .no_standard_includes = true,
                .semantic_pragmas = mode != 3,
                .scalar = mode == 0,
                .defer_history = mode == 2,
            };
            valid = pp_test_run(&runs[mode], &tables[mode], &options, path) && valid;
            valid = valid && runs[mode].eval_method_use_count == (mode != 3 ? cases[i].count : 0);
            if (valid && mode == 2)
            {
                valid = pp_history_ensure(&runs[mode]) && !runs[mode].fatal;
            }
            for (uint32_t use = 0; valid && mode != 3 && use < cases[i].count; use += 1)
            {
                const PPEvalMethodUse* record = runs[mode].eval_method_uses + use;
                valid = record->position == cases[i].positions[use] && record->pragmas == cases[i].pragmas[use] &&
                        (record->origin != 0) == ((cases[i].macro_uses & (1u << use)) != 0);
            }
            if (valid && mode != 0)
            {
                valid = pp_same_streams(&runs[0], &runs[mode], "floating evaluation macro use timeline") &&
                        (mode == 3 || pp_eval_method_uses_same(&runs[0], &runs[mode]));
            }
        }
        if (!valid)
        {
            fprintf(stderr, "floating evaluation macro use fixture %u failed\n", i);
        }
        passed &= valid;
        for (uint32_t mode = 0; mode < 4; mode += 1)
        {
            pp_release(&runs[mode]);
            intern_release(&tables[mode]);
        }
    }
    printf("floating evaluation macro uses: %u source-point cases, %s\n", (uint32_t)BUSTER_ARRAY_LENGTH(cases), passed ? "pass" : "fail");
    return passed;
}

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
        {
            "#define SOURCE value\n#define DEST first\n#pragma weak SOURCE\n#pragma redefine_extname SOURCE DEST\n#undef DEST\n#define DEST second\n_Pragma(\"redefine_extname SOURCE DEST\")\n",
            "#pragma weak SOURCE\n#pragma redefine_extname SOURCE DEST\n#pragma redefine_extname SOURCE DEST\n",
            "#pragma weak value\n#pragma redefine_extname value first\n#pragma redefine_extname value second\n",
        },
        {
            "#define fp unused\n#define MODE(x) x\n#define STATE strict\n#pragma clang fp exceptions(MODE(STATE))\n#undef STATE\n#define STATE ignore\n_Pragma(\"clang fp exceptions(STATE)\")\n",
            "#pragma clang fp exceptions(MODE(STATE))\n#pragma clang fp exceptions(STATE)\n",
            "#pragma clang fp exceptions(strict)\n#pragma clang fp exceptions(ignore)\n",
        },
        {
            "#define float_control unused\n#define KIND except\n#define STATE on\n#pragma float_control(KIND,STATE,push)\n#undef STATE\n#define STATE off\n#pragma float_control(KIND,STATE)\n#pragma float_control(pop)\n",
            "#pragma float_control(KIND,STATE,push)\n#pragma float_control(KIND,STATE)\n#pragma float_control(pop)\n",
            "#pragma float_control(except,on,push)\n#pragma float_control(except,off)\n#pragma float_control(pop)\n",
        },
        {
            "#define MODE on\n#pragma ms_struct MODE\n#undef MODE\n#define MODE reset\n#pragma ms_struct MODE\n",
            "#pragma ms_struct MODE\n#pragma ms_struct MODE\n",
            "#pragma ms_struct on\n#pragma ms_struct reset\n",
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
        // Every fixture expands at least one pragma operand. Equality alone would accept two streams that both
        // lost the capture's origins, leaving semantic warnings at macro definitions with no expansion note.
        bool origin = false;
        for (uint32_t token = 0; valid && token < runs[0].output.count; token += 1)
        {
            origin |= pp_history_at(&runs[0], token) != 0;
        }
        valid = valid && origin;
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
    passed &= pp_test_eval_method_uses(directory, with_clang);
    printf("semantic pragmas: %u source-point cases, %s\n", (uint32_t)BUSTER_ARRAY_LENGTH(cases), passed ? "pass" : "fail");
    return passed;
}
