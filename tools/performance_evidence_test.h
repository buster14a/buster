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
        pe_remove_tree(root);
    }
    free(r);
    printf("PERFORMANCE_EVIDENCE_SELF_TEST %s checks=%u failures=%u\n", failures ? "FAIL" : "PASS", checks, failures);
    int result = failures ? 1 : 0;
    return result;
}

#endif
