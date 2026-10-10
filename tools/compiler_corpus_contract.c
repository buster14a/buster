// Pure bounded native replay of the frozen full compiler throughput corpus.
// Complete regression/inconclusive outcomes are data; only exact normal exit 1
// with a positive, consistent regression count is an accepted report-only exit.
#ifndef BUSTER_COMPILER_CORPUS_CONTRACT_C
#define BUSTER_COMPILER_CORPUS_CONTRACT_C
#if BUSTER_LINUX
#include "ci_metrics_json.h"
#include <math.h>
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


BUSTER_GLOBAL_LOCAL bool compiler_closure_corpus_number(const CmJson* json, unsigned node, double* value)
{
    const char* text = cm_value(json, node);
    char* end = 0;
    errno = 0;
    double parsed = compiler_closure_corpus_kind(json, node, 'v') ? strtod(text, &end) : 0.0;
    bool result = compiler_closure_corpus_kind(json, node, 'v') && end && end != text && !*end &&
        !errno && isfinite(parsed);
    if (result) { *value = parsed; }
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_closure_corpus_numeric(const CmJson* json, unsigned object,
    const char* key, double minimum, double maximum, double* value)
{
    double parsed = 0.0;
    bool result = compiler_closure_corpus_number(json, cm_member(json, object, key), &parsed) &&
        parsed >= minimum && parsed <= maximum;
    if (result) { *value = parsed; }
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_closure_corpus_boolean(const CmJson* json, unsigned object,
    const char* key, bool* value)
{
    unsigned node = cm_member(json, object, key);
    const char* text = cm_value(json, node);
    bool result = compiler_closure_corpus_kind(json, node, 'v') &&
        (cm_equal(text, "true") || cm_equal(text, "false"));
    if (result) { *value = cm_equal(text, "true"); }
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_closure_corpus_bounded_uint(const CmJson* json, unsigned object,
    const char* key, u64 minimum, u64 maximum)
{
    unsigned node = cm_member(json, object, key);
    uint64_t value = 0;
    bool result = compiler_closure_corpus_kind(json, node, 'v') &&
        cm_unsigned(cm_value(json, node), &value) && value >= minimum && value <= maximum;
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_closure_corpus_sha_member(const CmJson* json, unsigned object, const char* key)
{
    unsigned node = cm_member(json, object, key);
    const char* text = cm_value(json, node);
    String8 value = {.pointer = (char8*)text, .length = (u64)strlen(text)};
    bool result = compiler_closure_corpus_kind(json, node, 's') && compiler_closure_corpus_sha(value);
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_closure_corpus_medians(const CmJson* json, unsigned object, const char* key)
{
    unsigned series = cm_member(json, object, key);
    bool result = compiler_closure_corpus_kind(json, series, 'a');
    u64 count = 0;
    for (unsigned node = result ? json->tokens[series].child : 0; result && node; node = json->tokens[node].next)
    {
        double value = 0.0;
        result = count < 2 && compiler_closure_corpus_number(json, node, &value) && value > 0.0;
        count += 1;
    }
    result = result && count == 2;
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
            compiler_closure_corpus_uint(&metadata, 1, "cpu", 2) &&
            compiler_closure_corpus_uint(&metadata, 1, "seed", 20260907) &&
            compiler_closure_corpus_uint(&metadata, 1, "scale", 1) &&
            compiler_closure_corpus_uint(&metadata, 1, "input_schema", 1) &&
            compiler_closure_corpus_text(&metadata, 1, "cache_policy", "warm-filesystem-new-process") &&
            compiler_closure_corpus_text(&metadata, 1, "clock", "monotonic");
        double family_alpha = 0.0, per_test_alpha = 0.0;
        result = result && compiler_closure_corpus_numeric(&summary, 1, "family_alpha", 0.0, 1.0, &family_alpha) &&
            compiler_closure_corpus_numeric(&summary, 1, "per_test_alpha", 0.0, 1.0, &per_test_alpha) &&
            family_alpha == 0.01 && per_test_alpha == 0.01 / 24.0;
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
            compiler_count == 0 ? baseline_sha256 : candidate_sha256) &&
            compiler_closure_corpus_bounded_uint(&metadata, node, "bytes", 1, 8ull << 30);
        compiler_count += 1;
    }
    result = result && compiler_count == 2;
    unsigned jobs = cm_member(&metadata, 1, "jobs");
    result = result && compiler_closure_corpus_kind(&metadata, jobs, 'a');
    u64 job_count = 0;
    for (unsigned node = result ? metadata.tokens[jobs].child : 0; result && node;
         node = metadata.tokens[node].next)
    {
        unsigned source_node = cm_member(&metadata, node, "source");
        const char* source = cm_value(&metadata, source_node);
        result = job_count < 12 && compiler_closure_corpus_kind(&metadata, node, 'o') &&
            compiler_closure_corpus_uint(&metadata, node, "job", job_count) &&
            compiler_closure_corpus_text(&metadata, node, "name", workloads[job_count / 2]) &&
            compiler_closure_corpus_text(&metadata, node, "mode", modes[job_count % 2]) &&
            compiler_closure_corpus_text(&metadata, node, "artifact", "object") &&
            compiler_closure_corpus_kind(&metadata, source_node, 's') && source[0] == '/' && strlen(source) <= 4096 &&
            compiler_closure_corpus_sha_member(&metadata, node, "sha256") &&
            compiler_closure_corpus_bounded_uint(&metadata, node, "bytes", 1, 8ull << 30) &&
            compiler_closure_corpus_bounded_uint(&metadata, node, "physical_lines", 1, 8ull << 30) &&
            compiler_closure_corpus_bounded_uint(&metadata, node, "defined_functions", 1, 8ull << 30);
        job_count += 1;
    }
    result = result && job_count == 12;
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
        unsigned medians = cm_member(&summary, row, "medians");
        result = result && compiler_closure_corpus_medians(&summary, medians, "wall_seconds") &&
            compiler_closure_corpus_medians(&summary, medians, "peak_rss_bytes");
        if (result && regression) { regressions += 1; }
        if (result && uncertain) { inconclusive += 1; }
        unsigned tests = cm_member(&summary, row, "tests");
        result = result && compiler_closure_corpus_kind(&summary, tests, 'a');
        u64 test_count = 0, tested = 0, regression_rounds[2] = {0};
        bool uncertain_high = false;
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
            double ratio = 0.0, low = 0.0, high = 0.0, baseline_mad = 0.0, candidate_mad = 0.0, p_value = 0.0;
            bool test_regression = false;
            result = result && compiler_closure_corpus_uint(&summary, test, "pairs", 20) &&
                compiler_closure_corpus_numeric(&summary, test, "median_ratio", 0.0, HUGE_VAL, &ratio) &&
                compiler_closure_corpus_numeric(&summary, test, "ci_low", 0.0, HUGE_VAL, &low) &&
                compiler_closure_corpus_numeric(&summary, test, "ci_high", 0.0, HUGE_VAL, &high) &&
                ratio > 0.0 && low > 0.0 && high > 0.0 && low <= ratio && ratio <= high &&
                compiler_closure_corpus_numeric(&summary, test, "baseline_relative_mad", 0.0, HUGE_VAL, &baseline_mad) &&
                compiler_closure_corpus_numeric(&summary, test, "candidate_relative_mad", 0.0, HUGE_VAL, &candidate_mad) &&
                compiler_closure_corpus_bounded_uint(&summary, test, "margin_exceedances", 0, 20) &&
                compiler_closure_corpus_numeric(&summary, test, "p_value", 0.0, 1.0, &p_value) &&
                compiler_closure_corpus_boolean(&summary, test, "regression", &test_regression) &&
                test_regression == (p_value <= 0.01 / 24.0);
            if (result)
            {
                tested |= 1ull << slot;
                if (test_regression) { regression_rounds[metric_index] |= 1ull << round; }
                if (!test_regression && high > (metric_index ? 1.20 : 1.15)) { uncertain_high = true; }
            }
            test_count += 1;
        }
        bool confirmed = regression_rounds[0] == 3 || regression_rounds[1] == 3;
        bool producer_uncertain = !confirmed && (regression_rounds[0] || regression_rounds[1] || uncertain_high);
        result = result && test_count == 4 && tested == 15 &&
            regression == confirmed && uncertain == producer_uncertain;
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
#endif // BUSTER_LINUX
#endif
