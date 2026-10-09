// Pure synthetic native corpus-contract fixtures. DIAGNOSTIC-UNQUALIFIED.
// This file never launches a child, samples hardware or changes a source tree.
#ifndef BUSTER_COMPILER_CORPUS_CONTRACT_TEST_C
#define BUSTER_COMPILER_CORPUS_CONTRACT_TEST_C

enum CompilerCorpusFixtureFlag
{
    COMPILER_CORPUS_FIXTURE_PARTIAL = 1ull << 0,
    COMPILER_CORPUS_FIXTURE_DUPLICATE_CELL = 1ull << 1,
    COMPILER_CORPUS_FIXTURE_DUPLICATE_KEY = 1ull << 2,
    COMPILER_CORPUS_FIXTURE_DUPLICATE_TEST = 1ull << 3,
    COMPILER_CORPUS_FIXTURE_FOREIGN_CELL = 1ull << 4,
    COMPILER_CORPUS_FIXTURE_WRONG_COUNT = 1ull << 5,
    COMPILER_CORPUS_FIXTURE_BOOL_SCHEMA = 1ull << 6,
    COMPILER_CORPUS_FIXTURE_STRING_GUARD = 1ull << 7,
    COMPILER_CORPUS_FIXTURE_BAD_MEDIANS = 1ull << 8,
    COMPILER_CORPUS_FIXTURE_BOOL_ROUND = 1ull << 9,
    COMPILER_CORPUS_FIXTURE_STRING_COUNT = 1ull << 10,
    COMPILER_CORPUS_FIXTURE_FOREIGN_WORKLOAD = 1ull << 11,
    COMPILER_CORPUS_FIXTURE_BAD_WARMUPS = 1ull << 12,
};

BUSTER_GLOBAL_LOCAL String8 compiler_closure_corpus_fixture_summary(Arena* arena, u64 regressions,
    u64 inconclusive, u64 flags)
{
    String8List parts = {0};
    String8 names[] = {S8("tiny_startup"), S8("large_function"), S8("many_functions"), S8("symbol_table"),
        S8("control_flow"), S8("backend_pressure")};
    String8 modes[] = {S8("fast"), S8("quality")};
    String8 schema = flags & COMPILER_CORPUS_FIXTURE_BOOL_SCHEMA ? S8("true") : S8("2");
    String8 guard = flags & COMPILER_CORPUS_FIXTURE_STRING_GUARD ? S8("\"true\"") : S8("true");
    u64 claimed = flags & COMPILER_CORPUS_FIXTURE_WRONG_COUNT ? regressions + 1 : regressions;
    String8 count = flags & COMPILER_CORPUS_FIXTURE_STRING_COUNT ?
        string_format(arena, S8("\"{u64}\""), claimed) : string_format(arena, S8("{u64}"), claimed);
    string8_list_push(arena, &parts, string_format(arena, S8("{{\"schema\":{S8},\"valid\":true,"
        "\"guard_enabled\":{S8},\"confirmed_regressions\":{S8},\"inconclusive_cases\":{u64},"),
        schema, guard, count, inconclusive));
    if (flags & COMPILER_CORPUS_FIXTURE_DUPLICATE_KEY)
    {
        string8_list_push(arena, &parts, S8("\"valid\":true,"));
    }
    string8_list_push(arena, &parts, S8("\"comparisons\":["));
    u64 cells = flags & COMPILER_CORPUS_FIXTURE_PARTIAL ? 11 : 12;
    for (u64 cell = 0; cell < cells; cell += 1)
    {
        if (cell) { string8_list_push(arena, &parts, S8(",")); }
        u64 selected = (flags & COMPILER_CORPUS_FIXTURE_DUPLICATE_CELL) && cell == 11 ? 0 : cell;
        String8 name = (flags & COMPILER_CORPUS_FIXTURE_FOREIGN_CELL) && cell == 11 ? S8("outside") : names[selected / 2];
        String8 decision = cell < regressions ? S8("regression") :
            cell < regressions + inconclusive ? S8("inconclusive") : S8("no substantial regression detected");
        String8 medians = flags & COMPILER_CORPUS_FIXTURE_BAD_MEDIANS ? S8("[]") : S8("{}");
        string8_list_push(arena, &parts, string_format(arena,
            S8("{{\"name\":\"{S8}/{S8}\",\"decision\":\"{S8}\",\"medians\":{S8},\"tests\":["),
            name, modes[selected % 2], decision, medians));
        for (u64 test = 0; test < 4; test += 1)
        {
            if (test) { string8_list_push(arena, &parts, S8(",")); }
            u64 selected_test = (flags & COMPILER_CORPUS_FIXTURE_DUPLICATE_TEST) && test == 3 ? 0 : test;
            String8 metric = selected_test / 2 == 0 ? S8("wall_seconds") : S8("peak_rss_bytes");
            String8 round = flags & COMPILER_CORPUS_FIXTURE_BOOL_ROUND ? S8("true") :
                string_format(arena, S8("{u64}"), selected_test % 2);
            string8_list_push(arena, &parts, string_format(arena,
                S8("{{\"metric\":\"{S8}\",\"round\":{S8}}"), metric, round));
        }
        string8_list_push(arena, &parts, S8("]}"));
    }
    string8_list_push(arena, &parts, S8("]}"));
    String8 result = string_join_arena(arena, string8_list_to_slice(arena, parts), false);
    return result;
}

BUSTER_GLOBAL_LOCAL String8 compiler_closure_corpus_fixture_metadata(Arena* arena, String8 baseline,
    String8 candidate, u64 flags)
{
    String8 last = flags & COMPILER_CORPUS_FIXTURE_FOREIGN_WORKLOAD ? S8("tiny_startup") : S8("backend_pressure");
    u64 warmups = flags & COMPILER_CORPUS_FIXTURE_BAD_WARMUPS ? 3 : 2;
    String8 result = string_format(arena, S8("{{\"schema\":2,\"profile\":\"ci\",\"pairs_per_round\":20,"
        "\"rounds\":2,\"warmups\":{u64},\"cpu\":2,\"workloads\":[\"tiny_startup\",\"large_function\","
        "\"many_functions\",\"symbol_table\",\"control_flow\",\"{S8}\"],"
        "\"compiler_provenance\":[{{\"sha256\":\"{S8}\"},{{\"sha256\":\"{S8}\"}]}"),
        warmups, last, baseline, candidate);
    return result;
}

BUSTER_GLOBAL_LOCAL ProcessResult compiler_closure_corpus_contract_self_test(Arena* arena)
{
    String8 baseline = S8("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    String8 candidate = S8("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    String8 wrong = S8("cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc");
    String8 metadata = compiler_closure_corpus_fixture_metadata(arena, baseline, candidate, 0);
    String8 regression = compiler_closure_corpus_fixture_summary(arena, 12, 0, 0);
    String8 zero = compiler_closure_corpus_fixture_summary(arena, 0, 0, 0);
    String8 uncertain = compiler_closure_corpus_fixture_summary(arena, 0, 12, 0);
    String8 mixed = compiler_closure_corpus_fixture_summary(arena, 1, 1, 0);
    bool passed = compiler_closure_corpus_validate(arena, regression, metadata, baseline, candidate, 256) &&
        compiler_closure_corpus_validate(arena, zero, metadata, baseline, candidate, 0) &&
        compiler_closure_corpus_validate(arena, uncertain, metadata, baseline, candidate, 0) &&
        compiler_closure_corpus_validate(arena, mixed, metadata, baseline, candidate, 256);
    u64 refused = 0;
    String8 malformed[] = {(String8){0}, S8("{}"), S8("[]"), S8("{"),
        compiler_closure_corpus_fixture_summary(arena, 11, 0, COMPILER_CORPUS_FIXTURE_PARTIAL),
        compiler_closure_corpus_fixture_summary(arena, 12, 0, COMPILER_CORPUS_FIXTURE_DUPLICATE_CELL),
        compiler_closure_corpus_fixture_summary(arena, 12, 0, COMPILER_CORPUS_FIXTURE_DUPLICATE_KEY),
        compiler_closure_corpus_fixture_summary(arena, 12, 0, COMPILER_CORPUS_FIXTURE_DUPLICATE_TEST),
        compiler_closure_corpus_fixture_summary(arena, 12, 0, COMPILER_CORPUS_FIXTURE_FOREIGN_CELL),
        compiler_closure_corpus_fixture_summary(arena, 12, 0, COMPILER_CORPUS_FIXTURE_WRONG_COUNT),
        compiler_closure_corpus_fixture_summary(arena, 12, 0, COMPILER_CORPUS_FIXTURE_BOOL_SCHEMA),
        compiler_closure_corpus_fixture_summary(arena, 12, 0, COMPILER_CORPUS_FIXTURE_STRING_GUARD),
        compiler_closure_corpus_fixture_summary(arena, 12, 0, COMPILER_CORPUS_FIXTURE_BAD_MEDIANS),
        compiler_closure_corpus_fixture_summary(arena, 12, 0, COMPILER_CORPUS_FIXTURE_BOOL_ROUND),
        compiler_closure_corpus_fixture_summary(arena, 12, 0, COMPILER_CORPUS_FIXTURE_STRING_COUNT)};
    for (u64 index = 0; index < BUSTER_ARRAY_LENGTH(malformed); index += 1)
    {
        bool rejected = !compiler_closure_corpus_validate(arena, malformed[index], metadata, baseline, candidate, 256);
        if (rejected) { refused += 1; }
        passed = rejected && passed;
    }
    String8 metadata_cases[] = {(String8){0}, S8("{}"), S8("[]"), S8("{"),
        compiler_closure_corpus_fixture_metadata(arena, wrong, candidate, 0),
        compiler_closure_corpus_fixture_metadata(arena, baseline, wrong, 0),
        compiler_closure_corpus_fixture_metadata(arena, baseline, candidate, COMPILER_CORPUS_FIXTURE_FOREIGN_WORKLOAD),
        compiler_closure_corpus_fixture_metadata(arena, baseline, candidate, COMPILER_CORPUS_FIXTURE_BAD_WARMUPS)};
    for (u64 index = 0; index < BUSTER_ARRAY_LENGTH(metadata_cases); index += 1)
    {
        bool rejected = !compiler_closure_corpus_validate(arena, regression, metadata_cases[index], baseline, candidate, 256);
        if (rejected) { refused += 1; }
        passed = rejected && passed;
    }
    u64 statuses[] = {1, 2, 9, 512, 65536};
    for (u64 index = 0; index < BUSTER_ARRAY_LENGTH(statuses); index += 1)
    {
        bool rejected = !compiler_closure_corpus_validate(arena, regression, metadata, baseline, candidate, statuses[index]);
        if (rejected) { refused += 1; }
        passed = rejected && passed;
    }
    bool zero_refused = !compiler_closure_corpus_validate(arena, zero, metadata, baseline, candidate, 256);
    bool inconclusive_refused = !compiler_closure_corpus_validate(arena, uncertain, metadata, baseline, candidate, 256);
    passed = zero_refused && inconclusive_refused && passed;
    if (zero_refused) { refused += 1; }
    if (inconclusive_refused) { refused += 1; }
    string_print(S8("COMPILER_CORPUS_CONTRACT_SELF_TEST status={S8} positive=4 refused={u64} "
        "full_regression_exit1=1 missing=1 partial=1 duplicates=1 wrong_binary=1 abnormal_status=1 "
        "unexplained_exit1=1 diagnostic=1 qualified=0 activation=0\n"),
        passed ? S8("pass") : S8("fail"), refused);
    ProcessResult result = passed ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
    return result;
}
#endif
