// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The corpus consumer must check the same resumed source as the driver. These original fixtures inspect the retained
// view, not just its first parse error (which an unresumed prefix could also report), and compare that error with Clang.

static void check_test_corpus_resumed_declarations(CheckTest* test, const char* clang, const char* directory)
{
    static const struct
    {
        const char* source;
        uint32_t insertions;
        bool whole;
        uint32_t warning_line;
    } fixtures[] =
    {
        { "int a\nint b;\nint f(void){return b;}\n", 1, true, 0 },
        { "int a\nint b __attribute__((unknown));\n", 1, true, 2 },
        { "void f(void){int a const; int b volatile;}\n", 2, true, 0 },
        { "int a\nint b;\nvoid f(void){int c const;}\n", 2, true, 0 },
        { "int a\nint b;\nint f(void){return 1 +;}\n", 1, false, 0 },
        { "int a __attribute__((aligned(3)));\nint b\nint c;\n", 1, true, 0 },
        { "int a\n#pragma clang diagnostic ignored \"-Wunknown-attributes\"\nint b __attribute__((unknown));\n", 1, true, 0 },
        { "int a\n#pragma pack(1)\nconst;\n", 0, false, 0 },
        { "int a;\nint f(void){return 1 +;}\n", 0, false, 0 },
    };
    const PPOptions options =
    {
        .language = { .standard = 2017, .gnu = true },
        .no_standard_includes = true,
        .quiet = true,
        .clang_features = true,
        .semantic_pragmas = true,
    };
    const char* arguments[] = { "--no-default-config", "--target=x86_64-linux-gnu", "-std=gnu17", "-nostdinc" };
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(fixtures); f += 1)
    {
        char source[PATH_MAX];
        snprintf(source, sizeof(source), "%s/resumed-corpus-%u.c", directory, f);
        CheckCorpusOracle oracle = { 0 };
        const bool written = check_corpus_write(source, fixtures[f].source, strlen(fixtures[f].source));
        const bool expected = written && check_corpus_clang_file(clang, arguments, BUSTER_ARRAY_LENGTH(arguments), source, &oracle);
        check_test_expect(test, expected && oracle.rejected, "Clang rejects the original corpus recovery fixture");
        for (uint32_t scalar = 0; expected && scalar < 2; scalar += 1)
        {
            CheckCorpusPath path = { 0 };
            const bool ready = intern_initialize(&identifiers) && pp_initialize(&pp, &options, &identifiers) && pp_run(&pp, source);
            const bool accepted = ready && check_corpus_check_policy(&path, &parser, &pp, scalar != 0, false);
            const bool resumed = path.resumed != NULL && path.resumed->status == CHECK_PREFIX_READY;
            const bool shape = fixtures[f].insertions != 0 ? resumed && path.resumed->inserted == fixtures[f].insertions &&
                               (path.resumed->parser.errors == 0) == fixtures[f].whole : !resumed;
            const bool all_tokens = !fixtures[f].whole || (path.input.token_count == parser.tokens.count + fixtures[f].insertions);
            const bool warning = fixtures[f].warning_line == 0 || path.first_warning_line == fixtures[f].warning_line;
            char description[1024];
            snprintf(description, sizeof(description), "corpus recovery fixture %u path %u: Clang %u:%u %s, checker %u:%u %s, inserted %u",
                     f, scalar, oracle.line, oracle.column, oracle.message, path.line, path.column, path.message,
                     resumed ? path.resumed->inserted : 0);
            check_test_expect(test, ready && !accepted && !path.parsed && !path.resource && shape && all_tokens && warning &&
                                   path.line == oracle.line && path.column == oracle.column && strcmp(path.message, oracle.message) == 0, description);
            check_corpus_close(&path);
            parse_release(&parser);
            pp_release(&pp);
            intern_release(&identifiers);
        }
        if (expected)
        {
            CheckCorpusOutcome outcome;
            check_corpus_measure(source, &options, false, true, &outcome);
            check_test_expect(test, outcome.prepared && outcome.equal && !outcome.resource && !outcome.accepted[0] && !outcome.accepted[1],
                              "corpus recovery compares complete scalar and vector views");
        }
        check_test_expect(test, !written || unlink(source) == 0, "corpus recovery fixture removed");
    }
}
