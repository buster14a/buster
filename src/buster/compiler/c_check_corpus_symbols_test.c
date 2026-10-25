// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// test-checker-corpus --symbols' reasons (#129, and #418's CI) on an original fixture, through check_corpus_measure as
// the command runs it, against clang -O0 -c: the callees of a constant if's dead arms (dropped), a const scalar that
// clang reads as a constant (folded) and a dead ?: arm in a branch's condition (unselected) must each be explained,
// and nothing else may differ. Then the same with clang alone given `-Dwide=renamed`: a function that clang's model
// selects, under a name clang's object doesn't have, and the name it has instead, must stay unexplained. Include after
// c_check_corpus.c; test-checker calls this through its forward declaration.
#ifndef BUSTER_C_CHECK_CORPUS_SYMBOLS_TEST_INCLUDED
#define BUSTER_C_CHECK_CORPUS_SYMBOLS_TEST_INCLUDED

static __attribute__((noinline)) void check_test_corpus_symbol_reasons(CheckTest* test, const char* clang)
{
    static const char source[] =
        "static inline int narrow(int n){return n+1;}static inline int wide(int n){return n+2;}static inline int widest(int n){return n+3;}\n"
        "int dropped(int n){long x=n;int r;if(sizeof(x)==sizeof(int))r=narrow(n);else if(sizeof(x)==sizeof(long))r=wide(n);else r=widest(n);return r;}\n"
        "static const int limit=5;int folded(int n){return n<limit;}\n"
        "static int unselected_call(void){return 1;}int unselected(int n){if(sizeof(n)==4?n:unselected_call())return 1;return 0;}\n";
    char directory[] = "/tmp/buster-symbol-reasons-XXXXXX";
    char path[160];
    char objects[160];
    bool ready = mkdtemp(directory) != 0;
    snprintf(path, sizeof(path), "%s/reasons.c", directory);
    snprintf(objects, sizeof(objects), "%s/objects", directory);
    ready = ready && parse_test_write(path, source, sizeof(source) - 1) && mkdir(objects, 0700) == 0;
    Arena option_arena = arena_reserve(UINT64_C(1) << 24);
    char* text[] = { "-std=gnu17", path };
    PPOptions options;
    const char* input = 0;
    const char* output = 0;
    bool markers = false;
    ready = ready && pp_parse_options(2, text, &options, &option_arena, &input, &output, &markers);
    options.semantic_pragmas = true;
    check_test_expect(test, ready, "--symbols' reasons: the fixture is written and its options read");
    static const char* const same[] = { "-std=gnu17" };
    static const char* const renamed[] = { "-std=gnu17", "-Dwide=renamed" };
    for (uint32_t run = 0; ready && run < 2; run += 1)
    {
        CheckCorpusSymbols symbols = {
            .clang = clang,
            .arguments = run == 0 ? same : renamed,
            .argument_count = run == 0 ? BUSTER_ARRAY_LENGTH(same) : BUSTER_ARRAY_LENGTH(renamed),
            .directory = objects,
        };
        check_corpus_symbols = &symbols;
        CheckCorpusOutcome outcome;
        check_corpus_measure(path, &options, false, true, &outcome);
        check_corpus_symbols = NULL;
        const bool explained = symbols.compared == 1 && symbols.failures == 0 && symbols.documented[CHECK_CORPUS_SYMBOL_DROPPED] == 2 &&
                               symbols.documented[CHECK_CORPUS_SYMBOL_FOLDED] == 1 && symbols.documented[CHECK_CORPUS_SYMBOL_UNSELECTED] == 1 &&
                               symbols.unexplained == (run == 0 ? 0 : 2) && symbols.different == (run == 0 ? 0 : 1);
        check_test_expect(test, explained, run == 0 ? "--symbols explains a dead if arm's callees, a folded constant and a dead ?: arm's call" :
                                                      "--symbols leaves a function clang's object names otherwise unexplained");
        if (!explained)
        {
            printf("--symbols' reasons, run %u: compared=%" PRIu64 " failures=%" PRIu64 " dropped=%" PRIu64 " folded=%" PRIu64 " unselected=%" PRIu64
                   " unexplained=%" PRIu64 " different=%" PRIu64 "\n", run, symbols.compared, symbols.failures, symbols.documented[CHECK_CORPUS_SYMBOL_DROPPED],
                   symbols.documented[CHECK_CORPUS_SYMBOL_FOLDED], symbols.documented[CHECK_CORPUS_SYMBOL_UNSELECTED], symbols.unexplained,
                   symbols.different);
        }
    }
    arena_release(&option_arena);
    unlink(path);
    rmdir(objects);
    rmdir(directory);
}

#endif
