// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original source schedule fixtures. Source-proof expectations are stated independently; the ordinary generated
// program is also compared with Clang O0 for value, floating exceptions and call counts on both kernel paths.

static void check_test_region_proof(CheckTest* test, const char* source, const char* name, bool constant)
{
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    const PPOptions options = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true };
    const ParseOptions parsing = { .extension_metadata = true };
    bool parsed = intern_initialize(&identifiers) && pp_initialize(&pp, &options, &identifiers) && parse_initialize(&parser, &parsing, options.language) &&
                  pp_run_source(&pp, name, (const uint8_t*)source, strlen(source)) && parse_translation_unit(&parser, &pp);
    check_test_expect(test, parsed, "region proof fixture parses through the ordinary frontend");
    for (uint32_t path = 0; parsed && path < 2; path += 1)
    {
        CheckTestTypes types = { 0 };
        Arena output = arena_reserve(1u << 26);
        Arena scratch = arena_reserve(1u << 26);
        CheckInput input = { 0 };
        CheckStream stream = { 0 };
        parser.options.scalar = path == 0;
        const CheckOptions checking = { .scalar = path == 0 };
        bool ready = output.base != NULL && scratch.base != NULL && check_test_types_open(&types) &&
                     check_input(&input, &parser, &pp, &types.table, &output, &scratch) &&
                     check_graph_run(&types.table, &input.graph, &output, &scratch, &checking, &stream) && stream.report_count == 0;
        char description[256];
        snprintf(description, sizeof(description), "%s: original source produces complete region proof metadata", name);
        check_test_expect(test, ready && input.region_count != 0 && input.graph.region_proof_count == input.region_count, description);
        if (ready)
        {
            bool described = stream.query_count == 1;
            for (uint32_t i = 0; i < input.graph.region_proof_count; i += 1)
            {
                const CheckRegionProof* proof = input.graph.region_proofs + i;
                described &= proof->flags == 0 && proof->count != 0;
            }
            snprintf(description, sizeof(description), "%s: independent Clang source proof is retained separately from runtime", name);
            const uint32_t node = stream.query_count == 1 ? stream.queries[0].node : BUSTER_CHECK_NONE;
            check_test_expect(test, described && node < stream.count && check_stream_constant_kind(&stream, node) == CHECK_CONSTANT_INTEGER &&
                                   check_stream_value(&stream, node).integer == (unsigned)constant, description);
        }
        check_test_expect(test, scratch.length == 0, "region source production and proof replay restore scratch lifetime");
        check_test_types_close(&types);
        arena_release(&scratch);
        arena_release(&output);
    }
    parse_release(&parser);
    pp_release(&pp);
    intern_release(&identifiers);
}

static void check_test_program_region_proofs(CheckTest* test, const char* clang)
{
    const struct
    {
        const char* name;
        const char* expression;
        bool constant;
    } fixtures[] = {
        { "integer-prefix", "({1;2;})", true },
        { "inexact-prefix", "({1.0/3.0;2;})", true },
        { "unknown-prefix", "({n;2;})", false },
        { "known-if", "({if(1)1.0/2.0;else bump();2;})", true },
        { "inexact-if", "({if(1)1.0/3.0;else 1;2;})", true },
        { "false-while", "({while(0)bump();2;})", true },
        { "break-while", "({while(1){1;break;}2;})", true },
        { "false-do", "({do{1;}while(0);2;})", true },
        { "false-for", "({for(;0;)bump();2;})", true },
        { "unused-local", "({int x=1;2;})", true },
        { "used-local", "({int x=1;x;})", false },
        { "unknown-initializer", "({int x=n;2;})", false },
        { "volatile-initializer", "({volatile int x=1;2;})", true },
        { "known-switch", "({switch(2){case 1:bump();break;case 2:1;break;default:bump();}3;})", true },
        { "case-conversion", "({switch(4294967295u){case -1:1;break;default:bump();}3;})", true },
        { "prefix-call", "({bump();2;})", false },
        { "nested-prefix", "({({1;1.0/3.0;});2;})", true },
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(fixtures); i += 1)
    {
        char source[2048];
        char name[96];
        snprintf(name, sizeof(name), "region-proof-%s", fixtures[i].name);
        snprintf(source, sizeof(source), "#pragma STDC FENV_ACCESS ON\n"
                 "int visits;int bump(void){visits++;return 1;}int clear_fp(void);int fp_state(void);"
                 "int tested(int n){visits=0;clear_fp();int answer=__builtin_constant_p(%s);int flags=fp_state();return answer*100+flags+visits*1000;}",
                 fixtures[i].expression);
        check_test_region_proof(test, source, name, fixtures[i].constant);
        check_test_program_source(test, source, name, clang);
    }
    const char* truth[] = { "!x", "x?1:2", "__builtin_isinf_sign(x)", "__builtin_fpclassify(0,1,2,3,4,x)" };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(truth); i += 1)
    {
        char source[1024];
        char name[80];
        snprintf(name, sizeof(name), "strict-query-signaling-nan-%u", i);
        snprintf(source, sizeof(source), "#pragma STDC FENV_ACCESS ON\n"
                 "double fp_special(int);int clear_fp(void);int fp_state(void);int tested(int n){double x=fp_special(n);clear_fp();"
                 "int answer=__builtin_constant_p(%s)+1;int flags=fp_state();return answer*100+flags;}", truth[i]);
        check_test_program_source(test, source, name, clang);
    }
}
