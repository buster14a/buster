// Synthetic controls for performance_evidence.c (#4013). Parsers, verdicts,
// refusals and the summary contract run on fixed text; the end-to-end cases
// only execute the system `true`/`false` utilities under the `time` tool.
// Map: pe_test_check, pe_test_metric, pe_test_series, pe_test_compare, pe_self_test.
#ifndef BUSTER_PERFORMANCE_EVIDENCE_TEST_H
#define BUSTER_PERFORMANCE_EVIDENCE_TEST_H

#define PE_CHECK(condition, label) pe_test_check(&failures, &checks, (condition), (label))

BUSTER_GLOBAL_LOCAL void pe_test_check(unsigned *failures, unsigned *checks, int condition, const char *label)
{
    ++*checks;
    if (!condition)
    {
        ++*failures;
        fprintf(stderr, "PERFORMANCE_EVIDENCE_SELF_TEST_FAIL %s\n", label);
    }
}

BUSTER_GLOBAL_LOCAL const PeMetric *pe_test_metric(const PeReport *r, const char *name)
{
    const PeMetric *result = NULL;
    for (unsigned i = 0; i < r->metric_count && !result; ++i) if (cm_equal(r->metrics[i].name, name)) result = &r->metrics[i];
    return result;
}

// A report with one metric whose arms hold the given series.
BUSTER_GLOBAL_LOCAL void pe_test_series(PeReport *r, PeKind kind, const double *baseline, const double *candidate, unsigned n)
{
    memset(r, 0, sizeof(*r));
    r->complete = 1;
    r->runs = n;
    r->effect = PE_DEFAULT_EFFECT;
    r->claim = pe_claim(kind == PE_KIND_COUNT ? "work-reduction" : "native-runtime");
    r->tool = kind == PE_KIND_COUNT ? PE_TOOL_CALLGRIND : PE_TOOL_TIME;
    for (unsigned i = 0; i < n; ++i)
    {
        pe_record(r, 0, kind == PE_KIND_COUNT ? "Ir" : "wall_seconds", "x", kind, 1, baseline[i]);
        pe_record(r, 1, kind == PE_KIND_COUNT ? "Ir" : "wall_seconds", "x", kind, 1, candidate[i]);
    }
    pe_verdicts(r);
    pe_result(r);
}

// Run `compare` in-process with a fresh output directory below `root`.
BUSTER_GLOBAL_LOCAL int pe_test_compare(const char *root, const char *name, const char *claim, const char *tool, const char *binary,
    const char *runs, int reuse)
{
    char out[PE_PATH];
    snprintf(out, sizeof(out), "%s/%s", root, name);
    if (reuse) mkdir(out, 0755);
    char *argv[] = {"performance-evidence", "compare", "--claim", (char *)claim, "--tool", (char *)tool, "--baseline",
        (char *)binary, "--candidate", (char *)binary, "--out", out, "--runs", (char *)runs, "--warmup", "0", "--", NULL};
    int result = pe_compare((int)BUSTER_ARRAY_LENGTH(argv) - 1, argv);
    return result;
}

BUSTER_GLOBAL_LOCAL int pe_self_test(void)
{
    unsigned failures = 0, checks = 0;
    char hex[65];
    pe_sha_hex((const uint8_t *)"abc", 3, NULL, hex);
    PE_CHECK(cm_equal(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"), "sha256 of abc");
    pe_sha_hex((const uint8_t *)"", 0, NULL, hex);
    PE_CHECK(cm_equal(hex, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"), "sha256 of the empty string");
    pe_sha_hex((const uint8_t *)"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56, NULL, hex);
    PE_CHECK(cm_equal(hex, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"), "sha256 across a padding block");

    // Claim routing: alternatives never serve a Zen 5 or uarch claim.
    PE_CHECK(pe_claim_allows(pe_claim("work-reduction"), PE_TOOL_CALLGRIND), "work reduction accepts callgrind");
    PE_CHECK(pe_claim_allows(pe_claim("work-reduction"), PE_TOOL_CACHEGRIND), "work reduction accepts cachegrind");
    PE_CHECK(!pe_claim_allows(pe_claim("work-reduction"), PE_TOOL_TIME), "elapsed time is not a work count");
    PE_CHECK(pe_claim_allows(pe_claim("native-runtime"), PE_TOOL_TIME), "native runtime accepts uninstrumented time");
    PE_CHECK(!pe_claim_allows(pe_claim("native-runtime"), PE_TOOL_CALLGRIND), "an instrumented count is not native runtime");
    for (unsigned tool = 1; tool < PE_TOOL_COUNT; ++tool)
    {
        PE_CHECK(!pe_claim_allows(pe_claim("uarch"), (PeTool)tool), "a uarch claim accepts no alternative tool");
        PE_CHECK(!pe_claim_allows(pe_claim("zen5"), (PeTool)tool), "a Zen 5 claim accepts no alternative tool");
    }
    PE_CHECK(cm_equal(pe_claim("uarch")->evidence_class, "zen5-native"), "uarch escalates to the physical class");
    PE_CHECK(pe_claim("mixed") == NULL, "a mixed claim must be split");

    // Profile parsing: callgrind totals, cachegrind summary and model lines.
    char names[PE_METRICS][48], model[PE_MODEL_LINES][128];
    double values[PE_METRICS];
    unsigned models = 0;
    const char *callgrind = "version: 1\ncreator: callgrind-3.22.0\npid: 7\ncmd: ./x\npart: 1\n\nevents: Ir\n"
        "summary: 1234\n\nfl=(1) x.c\nfn=(1) main\n3 1234\n\ntotals: 1234\n";
    PE_CHECK(pe_parse_profile(callgrind, names, values, PE_METRICS, model, &models) == 1 && cm_equal(names[0], "Ir") &&
        values[0] == 1234, "callgrind totals");
    const char *cachegrind = "desc: I1 cache:         32768 B, 64 B, 8-way associative\ndesc: D1 cache: 32768 B, 64 B, 8-way associative\n"
        "cmd: ./x\nevents: Ir I1mr ILmr Dr D1mr DLmr Dw D1mw DLmw\nfl=x.c\nfn=main\n3 100 1 1 50 2 1 20 1 0\n"
        "summary: 100 1 1 50 2 1 20 1 0\n";
    models = 0;
    PE_CHECK(pe_parse_profile(cachegrind, names, values, PE_METRICS, model, &models) == 9 && cm_equal(names[4], "D1mr") &&
        values[4] == 2 && models == 2 && cm_equal(model[0], "I1 cache: 32768 B, 64 B, 8-way associative"), "cachegrind summary and model");
    PE_CHECK(pe_parse_profile("events: Ir Dr\nsummary: 5\n", names, values, PE_METRICS, NULL, &models) == 0,
        "an event/value count mismatch is malformed");
    PE_CHECK(pe_parse_profile("events: Ir\nsummary: -5\n", names, values, PE_METRICS, NULL, &models) == 0,
        "a non-numeric total is malformed");
    PE_CHECK(pe_parse_profile("events: Ir\n", names, values, PE_METRICS, NULL, &models) == 0, "a truncated profile is malformed");

    // perf stat CSV: unsupported, under-run and absent events stay missing.
    PeReport *r = calloc(1, sizeof(PeReport));
    r->runs = 1;
    r->complete = 1;
    r->effect = PE_DEFAULT_EFFECT;
    r->claim = pe_claim("native-runtime");
    r->tool = PE_TOOL_PERF_STAT;
    pe_parse_perf(r, 0, "# started on Tue\n\n12.34,msec,task-clock:u,12340000,100.00,0.9,CPUs utilized\n"
        "1000000,,instructions:u,5000,75.00,,\n<not supported>,,cycles:u,0,100.00,,\n2000,,context-switches,5000,40.00,,\n",
        PE_DEFAULT_EVENTS);
    PE_CHECK(pe_test_metric(r, "task-clock") && pe_test_metric(r, "task-clock")->value[0][0] == 12.34, "perf task-clock");
    PE_CHECK(pe_test_metric(r, "instructions") && pe_test_metric(r, "instructions")->count[0] == 1 &&
        pe_test_metric(r, "instructions")->multiplexed, "a multiplexed counter is kept and marked");
    PE_CHECK(pe_test_metric(r, "cycles") && strstr(pe_test_metric(r, "cycles")->missing, "not supported"),
        "an unsupported counter is missing, never zero");
    PE_CHECK(pe_test_metric(r, "context-switches") && strstr(pe_test_metric(r, "context-switches")->missing, "insufficient"),
        "insufficient counter running time is refused");
    PE_CHECK(pe_test_metric(r, "page-faults") && strstr(pe_test_metric(r, "page-faults")->missing, "absent"),
        "an absent event is missing");
    pe_parse_perf(r, 1, "12.00,msec,task-clock:u,12000000,100.00,,\n", PE_DEFAULT_EVENTS);
    pe_verdicts(r);
    PE_CHECK(!r->complete && strstr(r->problems, "cycles"), "a missing required counter leaves the comparison incomplete");

    // Scoped events retain their requested identity and actual perf scope.
    const char *scope_cases[][4] = {
        {"instructions", "instructions", "unqualified", ""},
        {"instructions", "instructions:u", "u", ""},
        {"instructions", "instructions:k", "k", ""},
        {"instructions:u", "instructions:u", "u", ""},
        {"instructions:k", "instructions:k", "k", ""},
        {"instructions:u", "instructions:k", "", "scope"},
        {"instructions:k", "instructions:u", "", "scope"},
        {"instructions:u", "instructions", "", "scope"},
        {"instructions:u", "cycles:u", "", "absent"},
        {"instructions", "instructions:uk", "", "unsupported"},
    };
    for (unsigned i = 0; i < BUSTER_ARRAY_LENGTH(scope_cases); ++i)
    {
        memset(r, 0, sizeof(*r));
        char row[128];
        snprintf(row, sizeof(row), "123,,%s,5000,100.00,,\n", scope_cases[i][1]);
        pe_parse_perf(r, 0, row, scope_cases[i][0]);
        const PeMetric *m = pe_test_metric(r, scope_cases[i][0]);
        PE_CHECK(m && (scope_cases[i][3][0] ? m->count[0] == 0 && strstr(m->missing, scope_cases[i][3]) != NULL :
            m->count[0] == 1 && m->value[0][0] == 123 && cm_equal(m->scope, scope_cases[i][2])), "perf scoped event matching");
    }
    memset(r, 0, sizeof(*r));
    pe_parse_perf(r, 0, "1,,instructions:k,1,100,,\n2,,instructions:u,1,100,,\n", "instructions:u,instructions:k");
    PE_CHECK(pe_test_metric(r, "instructions:u")->value[0][0] == 2 && pe_test_metric(r, "instructions:k")->value[0][0] == 1,
        "distinct explicit scopes select their own counters regardless of row order");
    memset(r, 0, sizeof(*r));
    pe_parse_perf(r, 0, "1,,instructions:k,1,100,,\n2,,instructions:u,1,100,,\n", "instructions");
    PE_CHECK(r->metrics[0].count[0] == 0 && strstr(r->metrics[0].missing, "multiple"), "an ambiguous unqualified event is missing");
    memset(r, 0, sizeof(*r));
    pe_parse_perf(r, 0, "1,,instructions:u,1,100,,\n", "instructions");
    pe_parse_perf(r, 1, "2,,instructions:k,1,100,,\n", "instructions");
    PE_CHECK(r->metrics[0].count[1] == 0 && strstr(r->metrics[0].missing, "changed"), "scope cannot change across comparison arms");
    PE_CHECK(pe_perf_events_valid("task-clock,instructions:u,cycles:k"), "supported perf event spellings are accepted");
    const char *invalid_events[] = {"", "instructions:", "instructions:uk", "instructions:uu", "instructions:p", "instructions:u:k",
        "instructions,", ",instructions", "instructions,,cycles", "instructions:u ", "cpu/instructions/", "sched:sched_switch"};
    for (unsigned i = 0; i < BUSTER_ARRAY_LENGTH(invalid_events); ++i)
        PE_CHECK(!pe_perf_events_valid(invalid_events[i]), "unsupported perf event syntax is refused");

    PE_CHECK(pe_valgrind_problem("vex amd64->IR: unhandled instruction bytes: 0x62 0xF1", NULL) != NULL,
        "valgrind EVEX failure is an explicit ISA problem");
    PE_CHECK(pe_valgrind_problem("==1== Process terminating with default action of signal 4 (SIGILL)", NULL) != NULL,
        "SIGILL under valgrind is an explicit ISA problem");
    PE_CHECK(pe_valgrind_problem("==1== ERROR SUMMARY: 0 errors", NULL) == NULL, "a clean valgrind log has no problem");
    PE_CHECK(pe_perf_problem("Error:\nAccess to performance monitoring and observability operations is limited.") != NULL,
        "denied perf events are explicit");
    PE_CHECK(pe_perf_problem(" Performance counter stats for 'true':") == NULL, "a clean perf log has no problem");

    // Verdicts: separated repeatable counts, nonrepeatable counts, paired sign test.
    double base[10] = {1000, 1000}, less[10] = {900, 900}, noisy[10] = {900, 950}, near[10] = {995, 995};
    pe_test_series(r, PE_KIND_COUNT, base, less, 2);
    PE_CHECK(r->complete && cm_equal(r->metrics[0].verdict, "improvement") && strstr(r->result, "work reduction validated") &&
        strstr(r->result, "native runtime not measured"), "a repeatable separated count reduction");
    pe_test_series(r, PE_KIND_COUNT, less, base, 2);
    PE_CHECK(cm_equal(r->metrics[0].verdict, "regression") && strstr(r->result, "no work-reduction claim"), "a count increase");
    pe_test_series(r, PE_KIND_COUNT, base, noisy, 2);
    PE_CHECK(cm_equal(r->metrics[0].verdict, "nonrepeatable"), "nonrepeatable counts establish nothing");
    pe_test_series(r, PE_KIND_COUNT, base, near, 2);
    PE_CHECK(cm_equal(r->metrics[0].verdict, "within-effect"), "a change below the minimum effect");
    double wall[10] = {1.0, 1.1, 0.9, 1.0, 1.05, 0.95, 1.0, 1.02, 0.98, 1.0}, faster[10], mixed[10];
    for (unsigned i = 0; i < 10; ++i) { faster[i] = wall[i] * 0.9; mixed[i] = wall[i] * (i % 2 ? 0.8 : 1.2); }
    pe_test_series(r, PE_KIND_TIMED, wall, faster, 10);
    PE_CHECK(cm_equal(r->metrics[0].verdict, "improvement") && r->metrics[0].p_value < 0.002 &&
        strstr(r->result, "not transferable to Zen 5"), "a consistent paired native improvement");
    pe_test_series(r, PE_KIND_TIMED, wall, mixed, 10);
    PE_CHECK(cm_equal(r->metrics[0].verdict, "inconclusive"), "a mixed paired result is inconclusive");
    pe_test_series(r, PE_KIND_TIMED, wall, faster, 5);
    PE_CHECK(cm_equal(r->metrics[0].verdict, "inconclusive"), "five pairs cannot reach significance");
    PE_CHECK(fabs(pe_sign_test(0, 10) - 2.0 / 1024.0) < 1e-12 && fabs(pe_sign_test(0, 5) - 0.0625) < 1e-12 &&
        pe_sign_test(5, 10) == 1.0, "exact sign test");

    // The summary is valid JSON and never claims Zen 5 coverage.
    pe_test_series(r, PE_KIND_COUNT, base, less, 2);
    const char *identity[6] = {"a", "00", "", "b", "11", ""};
    char *args[] = {"x.c"};
    FILE *f = tmpfile();
    pe_write_summary(f, r, identity, args, 1);
    rewind(f);
    char *text = calloc(1, 65536);
    size_t n = fread(text, 1, 65535, f);
    fclose(f);
    CmJson json = cm_json_parse(text, n);
    PE_CHECK(json.valid && cm_equal(cm_get(&json, 1, "schema"), PE_SCHEMA) && cm_equal(cm_get(&json, 1, "zen5"), PE_NOT_ZEN5) &&
        cm_equal(cm_get(&json, 1, "evidence_class"), "work-count"), "the summary is a valid non-Zen-5 receipt");
    PE_CHECK(!strstr(text, "\"covered\""), "the summary never writes covered");
    cm_json_free(&json);
    free(text);

    memset(r, 0, sizeof(*r));
    r->complete = 1;
    r->tool = PE_TOOL_PERF_STAT;
    r->claim = pe_claim("work-reduction");
    pe_parse_perf(r, 0, "1,,instructions:k,1,100,,\n2,,instructions:u,1,100,,\n", "instructions:u,instructions:k");
    pe_result(r);
    PE_CHECK(!r->complete && strstr(r->problems, "multiple primary") && strstr(r->result, "incomplete"),
        "multiple primary scopes cannot establish a work reduction by selecting one counter");

    // The scoped primary drives the result, and summaries bind both scopes.
    const char *scoped_primary[] = {"instructions:u", "task-clock:k"};
    for (unsigned i = 0; i < BUSTER_ARRAY_LENGTH(scoped_primary); ++i)
    {
        memset(r, 0, sizeof(*r));
        r->complete = 1;
        r->runs = 6;
        r->effect = PE_DEFAULT_EFFECT;
        r->tool = PE_TOOL_PERF_STAT;
        r->claim = pe_claim(i ? "native-runtime" : "work-reduction");
        char row[128];
        for (unsigned k = 0; k < r->runs; ++k)
        {
            snprintf(row, sizeof(row), "1000,,%s,1,100,,\n", scoped_primary[i]);
            pe_parse_perf(r, 0, row, scoped_primary[i]);
            snprintf(row, sizeof(row), "900,,%s,1,100,,\n", scoped_primary[i]);
            pe_parse_perf(r, 1, row, scoped_primary[i]);
        }
        pe_verdicts(r);
        pe_result(r);
        PE_CHECK(r->complete && strstr(r->result, "validated") && strstr(r->result, scoped_primary[i]),
            "a scoped primary establishes its own comparison result");
        PE_CHECK(cm_equal(r->metrics[0].unit, i ? "msec" : "count"), "the scoped event keeps its base unit");
        f = tmpfile();
        pe_write_summary(f, r, identity, args, 1);
        rewind(f);
        text = calloc(1, 65536);
        n = fread(text, 1, 65535, f);
        fclose(f);
        json = cm_json_parse(text, n);
        PE_CHECK(json.valid && strstr(text, scoped_primary[i]) && strstr(text, i ? "\"scope\": \"k\"" : "\"scope\": \"u\""),
            "the perf summary retains requested and observed scope");
        cm_json_free(&json);
        free(text);
    }

    // Missing claim primaries must fail the same finalizer that writes CLI receipts and returns its status.
    const char *missing_primary[][3] = {
        {"work-reduction", "cycles", "instructions"},
        {"native-runtime", "instructions", "task-clock"},
    };
    for (unsigned i = 0; i < BUSTER_ARRAY_LENGTH(missing_primary); ++i)
    {
        memset(r, 0, sizeof(*r));
        r->complete = 1;
        r->a_a = 1;
        r->runs = 6;
        r->effect = PE_DEFAULT_EFFECT;
        r->tool = PE_TOOL_PERF_STAT;
        r->claim = pe_claim(missing_primary[i][0]);
        char row[128];
        snprintf(row, sizeof(row), "1000,,%s,1,100,,\n", missing_primary[i][1]);
        for (unsigned k = 0; k < r->runs; ++k)
        {
            pe_parse_perf(r, 0, row, missing_primary[i][1]);
            pe_parse_perf(r, 1, row, missing_primary[i][1]);
        }
        PE_CHECK(r->complete && r->metric_count == 1 && r->metrics[0].count[0] == 6 && r->metrics[0].count[1] == 6,
            "the supplied non-primary perf counter is fully observed");
        f = tmpfile();
        PE_CHECK(pe_finish(r, f, identity, args, 1) == 2 && !r->complete &&
            strstr(r->problems, "missing required primary metric: ") && strstr(r->problems, missing_primary[i][2]) &&
            strstr(r->result, "incomplete"), "missing primary fails completeness, result and CLI exit status");
        rewind(f);
        text = calloc(1, 65536);
        n = fread(text, 1, 65535, f);
        fclose(f);
        json = cm_json_parse(text, n);
        PE_CHECK(json.valid && cm_equal(cm_get(&json, 1, "complete"), "false") &&
            strstr(cm_get(&json, 1, "problems"), "missing required primary metric: ") &&
            strstr(cm_get(&json, 1, "problems"), missing_primary[i][2]) &&
            strstr(cm_get(&json, 1, "result"), "incomplete"), "missing primary JSON agrees with result and failure status");
        cm_json_free(&json);
        free(text);
    }

    // Cleanup joins accept the last fitting byte and clear refusals before any path operation.
    char joined[PE_PATH], long_parent[PE_PATH];
    PE_CHECK(pe_path_join(joined, 10, "root", "leaf") && cm_equal(joined, "root/leaf"),
        "cleanup join fits exactly including the terminator");
    joined[0] = 'x';
    PE_CHECK(!pe_path_join(joined, 9, "root", "leaf") && !joined[0], "cleanup join refuses one byte beyond capacity");
    joined[0] = 'x';
    PE_CHECK(!pe_path_join(joined, 0, "root", "leaf") && joined[0] == 'x', "zero-capacity join writes nothing");
    PE_CHECK(!pe_path_join(joined, 1, "", "") && !joined[0], "cleanup join needs room for slash and terminator");
    memset(long_parent, 'a', sizeof(long_parent) - 1);
    long_parent[PE_PATH - 3] = 0;
    PE_CHECK(pe_path_join(joined, sizeof(joined), long_parent, "b") && strlen(joined) == PE_PATH - 1,
        "cleanup join accepts the full path capacity");
    long_parent[PE_PATH - 3] = 'a';
    long_parent[PE_PATH - 2] = 0;
    PE_CHECK(!pe_path_join(joined, sizeof(joined), long_parent, "b") && !joined[0],
        "cleanup join rejects an overlong parent and child");
    long_parent[PE_PATH - 2] = 'a';
    long_parent[PE_PATH - 1] = 0;
    PE_CHECK(!pe_path_join(joined, sizeof(joined), "a", long_parent) && !joined[0],
        "cleanup join rejects an overlong child");

    // End to end with the time tool: an A/A control, refusals and an unexpected exit.
    char root[] = "/tmp/performance-evidence-self-test-XXXXXX", truth[PE_PATH], falsity[PE_PATH];
    int have = mkdtemp(root) != NULL && pe_which("true", truth, sizeof(truth)) && pe_which("false", falsity, sizeof(falsity));
    PE_CHECK(have, "the self-test needs a scratch directory and the true/false utilities");
    if (have)
    {
        char summary[PE_PATH];
        size_t length = 0;
        PE_CHECK(pe_test_compare(root, "aa", "native-runtime", "time", truth, "6", 0) == 0, "an A/A time comparison completes");
        snprintf(summary, sizeof(summary), "%s/aa/summary.json", root);
        char *written = cm_read(summary, PE_LOG_BYTES, &length);
        PE_CHECK(written && strstr(written, "\"a_a_control\": true") && strstr(written, "A/A noise control"),
            "identical binaries are reported as an A/A control");
        free(written);
        PE_CHECK(pe_test_compare(root, "wrong-tool", "native-runtime", "callgrind", truth, "2", 0) == 2,
            "an instrumented count is refused for a native-runtime claim");
        PE_CHECK(pe_test_compare(root, "uarch", "uarch", "time", truth, "6", 0) == 2, "a uarch claim is refused off the 9700X");
        PE_CHECK(pe_test_compare(root, "few", "native-runtime", "time", truth, "3", 0) == 2, "too few pairs are refused");
        PE_CHECK(pe_test_compare(root, "stale", "native-runtime", "time", truth, "6", 1) == 2, "a reused output directory is refused");
        PE_CHECK(pe_test_compare(root, "failing", "native-runtime", "time", falsity, "6", 0) == 2,
            "an unexpected exit status leaves the comparison incomplete");
        PE_CHECK(pe_test_compare(root, "absent", "native-runtime", "time", "/nonexistent/binary", "6", 0) == 2,
            "an unreadable binary leaves the comparison incomplete");
        char refused_out[PE_PATH];
        snprintf(refused_out, sizeof(refused_out), "%s/unsupported-events", root);
        char *perf_argv[] = {"performance-evidence", "compare", "--claim", "work-reduction", "--tool", "perf-stat", "--baseline",
            truth, "--candidate", truth, "--out", refused_out, "--runs", "6", "--events", "instructions:uk", "--", NULL};
        PE_CHECK(pe_compare((int)BUSTER_ARRAY_LENGTH(perf_argv) - 1, perf_argv) == 2 && access(refused_out, F_OK) != 0,
            "unsupported modifiers are refused before creating output or running perf");
        PE_CHECK(pe_remove_tree(root) && access(root, F_OK) != 0, "scratch cleanup removes root and nested comparison files");
    }
    free(r);
    printf("PERFORMANCE_EVIDENCE_SELF_TEST %s checks=%u failures=%u\n", failures ? "FAIL" : "PASS", checks, failures);
    int result = failures ? 1 : 0;
    return result;
}

#endif
