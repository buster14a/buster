// Pure bounded native replay of the frozen full compiler throughput corpus.
// Complete regression/inconclusive outcomes are data; only exact normal exit 1
// with a positive, consistent regression count is an accepted report-only exit.
#ifndef BUSTER_COMPILER_CORPUS_CONTRACT_C
#define BUSTER_COMPILER_CORPUS_CONTRACT_C
#include "ci_metrics_json.h"
#define BUSTER_COMPILER_CORPUS_JSON_LIMIT (8ull << 20)

BUSTER_GLOBAL_LOCAL bool compiler_closure_corpus_kind(const CmJson* json, unsigned node, char kind)
{
    bool result = json->valid && node && node <= json->count && json->tokens[node].kind == kind;
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_closure_corpus_text(const CmJson* json, unsigned object,
    const char* key, const char* expected)
{
    unsigned node = cm_member(json, object, key);
    bool result = compiler_closure_corpus_kind(json, node, 's') && cm_equal(cm_value(json, node), expected);
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_closure_corpus_uint(const CmJson* json, unsigned object,
    const char* key, u64 expected)
{
    unsigned node = cm_member(json, object, key);
    uint64_t value = 0;
    bool result = compiler_closure_corpus_kind(json, node, 'v') &&
        cm_unsigned(cm_value(json, node), &value) && value == expected;
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_closure_corpus_true(const CmJson* json, unsigned object, const char* key)
{
    unsigned node = cm_member(json, object, key);
    bool result = compiler_closure_corpus_kind(json, node, 'v') && cm_equal(cm_value(json, node), "true");
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_closure_corpus_sha(String8 value)
{
    bool result = value.pointer && value.length == 64;
    for (u64 index = 0; result && index < value.length; index += 1)
    {
        char8 byte = value.pointer[index];
        result = (byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'f');
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_closure_corpus_binding(const CmJson* json, unsigned object, String8 expected)
{
    unsigned node = cm_member(json, object, "sha256");
    const char* text = cm_value(json, node);
    bool result = compiler_closure_corpus_kind(json, object, 'o') &&
        compiler_closure_corpus_kind(json, node, 's') && compiler_closure_corpus_sha(expected) &&
        strlen(text) == expected.length && memcmp(text, expected.pointer, (size_t)expected.length) == 0;
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_closure_corpus_validate(Arena* arena, String8 summary_bytes,
    String8 metadata_bytes, String8 baseline_sha256, String8 candidate_sha256, u64 raw_posix_status)
{
    (void)arena;
    const char* workloads[] = {"tiny_startup", "large_function", "many_functions", "symbol_table",
        "control_flow", "backend_pressure"};
    const char* modes[] = {"fast", "quality"};
    bool result = summary_bytes.pointer && metadata_bytes.pointer &&
        summary_bytes.length && metadata_bytes.length &&
        summary_bytes.length <= BUSTER_COMPILER_CORPUS_JSON_LIMIT &&
        metadata_bytes.length <= BUSTER_COMPILER_CORPUS_JSON_LIMIT &&
        compiler_closure_corpus_sha(baseline_sha256) && compiler_closure_corpus_sha(candidate_sha256) &&
        (raw_posix_status == 0 || raw_posix_status == 256);
    CmJson summary = {0};
    CmJson metadata = {0};
    if (result)
    {
        summary = cm_json_parse((const char*)summary_bytes.pointer, (size_t)summary_bytes.length);
        metadata = cm_json_parse((const char*)metadata_bytes.pointer, (size_t)metadata_bytes.length);
        result = compiler_closure_corpus_kind(&summary, 1, 'o') &&
            compiler_closure_corpus_kind(&metadata, 1, 'o') &&
            compiler_closure_corpus_uint(&summary, 1, "schema", 2) &&
            compiler_closure_corpus_true(&summary, 1, "valid") &&
            compiler_closure_corpus_true(&summary, 1, "guard_enabled") &&
            compiler_closure_corpus_uint(&metadata, 1, "schema", 2) &&
            compiler_closure_corpus_text(&metadata, 1, "profile", "ci") &&
            compiler_closure_corpus_uint(&metadata, 1, "pairs_per_round", 20) &&
            compiler_closure_corpus_uint(&metadata, 1, "rounds", 2) &&
            compiler_closure_corpus_uint(&metadata, 1, "warmups", 2) &&
            compiler_closure_corpus_uint(&metadata, 1, "cpu", 2);
    }
    unsigned names = cm_member(&metadata, 1, "workloads");
    result = result && compiler_closure_corpus_kind(&metadata, names, 'a');
    u64 name_count = 0;
    for (unsigned node = result ? metadata.tokens[names].child : 0; result && node;
         node = metadata.tokens[node].next)
    {
        result = name_count < BUSTER_ARRAY_LENGTH(workloads) &&
            compiler_closure_corpus_kind(&metadata, node, 's') &&
            cm_equal(cm_value(&metadata, node), workloads[name_count]);
        name_count += 1;
    }
    result = result && name_count == BUSTER_ARRAY_LENGTH(workloads);
    unsigned provenance = cm_member(&metadata, 1, "compiler_provenance");
    result = result && compiler_closure_corpus_kind(&metadata, provenance, 'a');
    u64 compiler_count = 0;
    for (unsigned node = result ? metadata.tokens[provenance].child : 0; result && node;
         node = metadata.tokens[node].next)
    {
        result = compiler_count < 2 && compiler_closure_corpus_binding(&metadata, node,
            compiler_count == 0 ? baseline_sha256 : candidate_sha256);
        compiler_count += 1;
    }
    result = result && compiler_count == 2;
    unsigned comparisons = cm_member(&summary, 1, "comparisons");
    result = result && compiler_closure_corpus_kind(&summary, comparisons, 'a');
    u64 cells = 0, seen = 0, regressions = 0, inconclusive = 0;
    for (unsigned row = result ? summary.tokens[comparisons].child : 0; result && row;
         row = summary.tokens[row].next)
    {
        unsigned name_node = cm_member(&summary, row, "name");
        result = cells < 12 && compiler_closure_corpus_kind(&summary, row, 'o') &&
            compiler_closure_corpus_kind(&summary, name_node, 's');
        u64 matched = 12;
        const char* name = cm_value(&summary, name_node);
        for (u64 workload = 0; result && workload < BUSTER_ARRAY_LENGTH(workloads); workload += 1)
        {
            for (u64 mode = 0; mode < BUSTER_ARRAY_LENGTH(modes); mode += 1)
            {
                char expected[64] = {0};
                int written = snprintf(expected, sizeof(expected), "%s/%s", workloads[workload], modes[mode]);
                if (written > 0 && (size_t)written < sizeof(expected) && cm_equal(name, expected))
                {
                    matched = workload * BUSTER_ARRAY_LENGTH(modes) + mode;
                }
            }
        }
        result = result && matched < 12 && !(seen & (1ull << matched));
        if (result) { seen |= 1ull << matched; }
        unsigned decision_node = cm_member(&summary, row, "decision");
        const char* decision = cm_value(&summary, decision_node);
        bool regression = cm_equal(decision, "regression");
        bool uncertain = cm_equal(decision, "inconclusive");
        result = result && compiler_closure_corpus_kind(&summary, decision_node, 's') &&
            (regression || uncertain || cm_equal(decision, "no substantial regression detected")) &&
            compiler_closure_corpus_kind(&summary, cm_member(&summary, row, "medians"), 'o');
        if (result && regression) { regressions += 1; }
        if (result && uncertain) { inconclusive += 1; }
        unsigned tests = cm_member(&summary, row, "tests");
        result = result && compiler_closure_corpus_kind(&summary, tests, 'a');
        u64 test_count = 0, tested = 0;
        for (unsigned test = result ? summary.tokens[tests].child : 0; result && test;
             test = summary.tokens[test].next)
        {
            unsigned metric_node = cm_member(&summary, test, "metric");
            const char* metric = cm_value(&summary, metric_node);
            u64 metric_index = cm_equal(metric, "wall_seconds") ? 0 :
                cm_equal(metric, "peak_rss_bytes") ? 1 : 2;
            unsigned round_node = cm_member(&summary, test, "round");
            uint64_t round = 2;
            result = test_count < 4 && compiler_closure_corpus_kind(&summary, test, 'o') &&
                compiler_closure_corpus_kind(&summary, metric_node, 's') && metric_index < 2 &&
                compiler_closure_corpus_kind(&summary, round_node, 'v') &&
                cm_unsigned(cm_value(&summary, round_node), &round) && round < 2;
            u64 slot = metric_index * 2 + round;
            result = result && slot < 4 && !(tested & (1ull << slot));
            if (result) { tested |= 1ull << slot; }
            test_count += 1;
        }
        result = result && test_count == 4 && tested == 15;
        cells += 1;
    }
    result = result && cells == 12 && seen == 4095 &&
        compiler_closure_corpus_uint(&summary, 1, "confirmed_regressions", regressions) &&
        compiler_closure_corpus_uint(&summary, 1, "inconclusive_cases", inconclusive) &&
        (raw_posix_status != 256 || regressions > 0);
    cm_json_free(&summary);
    cm_json_free(&metadata);
    return result;
}
#endif
