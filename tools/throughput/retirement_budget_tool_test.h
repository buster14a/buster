/* The budget writer (retirement_budget_tool.h, the service's
 * `retirement-records budget-encode|budget-preflight`): the reviewed input
 * becomes exactly tp_retirement_budget_encode's record, which decodes and
 * preflights through the real codec; every rule refuses with nothing on
 * stdout. All nanosecond values are fixtures, never measurements. */

/* The fixture's reviewed input, in a scrambled but legal order with
 * provenance comments; `drop`/`replace` edit one line (by prefix). */
static char const* const test_budget_input_lines[] = {
    "# fixture values only; production bounds need 9700X measurements (#422)",
    "metrics-input-bytes=16384",
    "reviewed-ns=36000000000000",
    "reservation-ns=1000000000",
    "materialization-ns=1000000000",
    "baseline-build-ns=1000000000",
    "",
    "candidate-build-ns=1000000000",
    "correctness-ns=1000000000",
    "settling-per-stage-ns=1000000000",
    "aa-qualification-ns=1000000000",
    "aa-receipt-sealing-ns=1000000000",
    "sample-export-per-stage-ns=1000000000",
    "final-statistics-ns=1000000000",
    "final-sealing-ns=1000000000",
    "cleanup-ns=1000000000",
    "runtime-process-ns=50000000",
    "metrics-header-bytes=4096",
    "# AA_MEASURED re-read: 8 ms/MiB covers the container measurement of",
    "# bq_retirement_coordinator_aa_attest (7.44 ms/MiB worst, fixture shards)",
    "aa-attestation-ns-per-mib=8000000",
    "batch=4:100000000",
    "untimed-batch=4:150000000",
    "batch=1024:2000000000",
    "singleton=link:90000000",
    "singleton=self-host-stage1:900000000",
    "untimed-batch=1024:3000000000",
    "untimed-singleton=link:120000000",
    "untimed-singleton=self-host-stage1:1200000000",
};

static TpRetirementCampaignBudget test_budget_expected(void)
{
    TpRetirementCampaignBudget budget = {.reviewed_ns = UINT64_C(36000000000000),
        .reservation_ns = 1000000000, .materialization_ns = 1000000000, .baseline_build_ns = 1000000000,
        .candidate_build_ns = 1000000000, .correctness_ns = 1000000000, .settling_per_stage_ns = 1000000000,
        .aa_qualification_ns = 1000000000, .aa_receipt_sealing_ns = 1000000000,
        .sample_export_per_stage_ns = 1000000000, .final_statistics_ns = 1000000000,
        .final_sealing_ns = 1000000000, .cleanup_ns = 1000000000, .runtime_process_ns = 50000000,
        .metrics_header_bytes = 4096, .metrics_input_bytes = 16384, .aa_attestation_ns_per_mib = 8000000,
        .timed = {2, {{4, 100000000}, {TP_RETIREMENT_BATCH_INPUTS, 2000000000}},
                  {[TP_RETIREMENT_BUDGET_STAGE_LINK] = 90000000, [TP_RETIREMENT_BUDGET_STAGE_SELF_HOST] = 900000000}},
        .untimed = {2, {{4, 150000000}, {TP_RETIREMENT_BATCH_INPUTS, 3000000000}},
                    {[TP_RETIREMENT_BUDGET_STAGE_LINK] = 120000000,
                     [TP_RETIREMENT_BUDGET_STAGE_SELF_HOST] = 1200000000}}};
    return budget;
}

/* One edit of the fixture input: the line starting with `drop` becomes
 * `replace` (NULL removes it; it may hold several LF-separated lines). */
typedef struct TestBudgetEdit
{
    char const* drop;
    char const* replace;
} TestBudgetEdit;

/* Writes the input with up to two edits and `extra` appended. */
static int test_budget_input_edits(char const* root, char const* name, TestBudgetEdit const* edits, unsigned count,
    char const* extra)
{
    char text[TP_RETIREMENT_BUDGET_INPUT_BYTES];
    size_t used = (size_t)snprintf(text, sizeof(text), "schema=%s\n", TP_RETIREMENT_BUDGET_INPUT_SCHEMA);
    for (unsigned i = 0; i < BUSTER_ARRAY_LENGTH(test_budget_input_lines); ++i)
    {
        char const* line = test_budget_input_lines[i];
        for (unsigned e = 0; e < count; ++e)
            if (edits[e].drop && !strncmp(test_budget_input_lines[i], edits[e].drop, strlen(edits[e].drop)))
                line = edits[e].replace;
        if (line && used < sizeof(text)) used += (size_t)snprintf(text + used, sizeof(text) - used, "%s\n", line);
    }
    if (extra && used < sizeof(text)) used += (size_t)snprintf(text + used, sizeof(text) - used, "%s\n", extra);
    return used < sizeof(text) && test_text(root, name, text);
}

static int test_budget_input(char const* root, char const* name, char const* drop, char const* replace,
    char const* extra)
{
    TestBudgetEdit edit = {drop, replace};
    return test_budget_input_edits(root, name, &edit, 1, extra);
}

#define TEST_BUDGET_DIGESTS "population=1111111111111111111111111111111111111111111111111111111111111111\n" \
    "declaration=2222222222222222222222222222222222222222222222222222222222222222\n"
/* Three timed groups (416 inputs, a link and a self-host singleton), two
 * runtime rows, 60 pairs and two untimed groups. */
static char const test_budget_counts[] = "schema=" TP_RETIREMENT_BUDGET_COUNTS_SCHEMA "\n" TEST_BUDGET_DIGESTS
    "pairs=60\nruntime-rows=2\ntimed-groups=3\ntimed=object 416\ntimed=link 1\ntimed=self-host-stage1 1\n"
    "untimed-groups=2\nuntimed=object 3\nuntimed=link 1\n";
/* One four-input object group, nothing else: fixed 14 s plus 488 batches of
 * 0.1 s, 62.8 s, plus (v3) its AA_MEASURED re-read, 120 * (4 * 330 + 266) =
 * 190,320 bytes at 8 ms/MiB, 1,452,027 ns: 62,801,452,027 ns in all. */
static char const test_budget_tiny[] = "schema=" TP_RETIREMENT_BUDGET_COUNTS_SCHEMA "\n" TEST_BUDGET_DIGESTS
    "pairs=60\nruntime-rows=0\ntimed-groups=1\ntimed=object 4\nuntimed-groups=0\n";

/* Runs the CLI with its stdout in `text` and its diagnostic line in
 * `diagnostic`; returns its exit status. */
static int test_budget_run(char const* root, char const* command, char const* first, char const* second,
    char* text, size_t capacity, size_t* size, char diagnostic[TP_RETIREMENT_BUDGET_DIAGNOSTIC + 64])
{
    char paths[4][TP_PATH_CAP];
    int ok = tp_path(paths[0], root, first) && tp_path(paths[1], root, second) &&
        tp_path(paths[2], root, "budget.out") && tp_path(paths[3], root, "budget.err");
    FILE* out = ok ? fopen(paths[2], "w+b") : NULL;
    FILE* err = ok ? fopen(paths[3], "w+b") : NULL;
    char* argv[] = {(char*)command, paths[0], paths[1]};
    int result = out && err ? tp_retirement_budget_cli(3, argv, out, err) : -1;
    *size = 0;
    if (out && fseek(out, 0, SEEK_SET) == 0) *size = fread(text, 1, capacity - 1, out);
    text[*size] = 0;
    size_t length = err && fseek(err, 0, SEEK_SET) == 0 ? fread(diagnostic, 1, TP_RETIREMENT_BUDGET_DIAGNOSTIC + 63, err) :
        0;
    diagnostic[length] = 0;
    if (out) fclose(out);
    if (err) fclose(err);
    return result;
}

static void test_retirement_budget_tool(char const* root)
{
    char budget_root[TP_PATH_CAP], text[8192], again[8192], expected[TP_RETIREMENT_BUDGET_BYTES];
    char diagnostic[TP_RETIREMENT_BUDGET_DIAGNOSTIC + 64];
    size_t size = 0, again_size = 0;
    TpRetirementCampaignBudget reference = test_budget_expected(), decoded = {0};
    size_t expected_size = tp_retirement_budget_encode(&reference, expected, sizeof(expected));
    CHECK(expected_size && tp_path(budget_root, root, "retirement-budget") && tp_mkdirs(budget_root));
    CHECK(test_text(budget_root, "counts", test_budget_counts) && test_text(budget_root, "tiny", test_budget_tiny) &&
          test_budget_input(budget_root, "input", NULL, NULL, NULL));

    /* The counts codec is strict and round-trips, digests included. */
    TpRetirementBudgetOwnedCounts counts = {0};
    CHECK(tp_retirement_budget_counts_parse(test_budget_counts, strlen(test_budget_counts), &counts) &&
          counts.counts.timed.count == 3 && counts.counts.untimed.count == 2 && counts.counts.pairs == 60 &&
          counts.counts.runtime_rows == 2 && counts.counts.timed.inputs[0] == 416 &&
          counts.counts.timed.kinds[0] == TP_RETIREMENT_GROUP_OBJECT &&
          counts.counts.timed.stages[2] == TP_RETIREMENT_BUDGET_STAGE_SELF_HOST &&
          counts.counts.untimed.stages[1] == TP_RETIREMENT_BUDGET_STAGE_LINK &&
          counts.population_sha256[0] == '1' && counts.declaration_sha256[63] == '2');
    char encoded[512];
    CHECK(tp_retirement_budget_counts_encode(&counts.counts, counts.population_sha256, counts.declaration_sha256,
                                             encoded, sizeof(encoded)) == strlen(test_budget_counts) &&
          !memcmp(encoded, test_budget_counts, strlen(test_budget_counts)));
    char short_digest[65] = "short";
    CHECK(!tp_retirement_budget_counts_encode(&counts.counts, short_digest, counts.declaration_sha256, NULL, 0));
    tp_retirement_budget_counts_release(&counts);
    static char const* const bad_counts[] = {
        "schema=" TP_RETIREMENT_BUDGET_COUNTS_SCHEMA "\n" TEST_BUDGET_DIGESTS
        "pairs=60\nruntime-rows=0\ntimed-groups=1\ntimed=link 2\nuntimed-groups=0\n",
        "schema=" TP_RETIREMENT_BUDGET_COUNTS_SCHEMA "\n" TEST_BUDGET_DIGESTS
        "pairs=60\nruntime-rows=0\ntimed-groups=1\ntimed=object 01\nuntimed-groups=0\n",
        "schema=" TP_RETIREMENT_BUDGET_COUNTS_SCHEMA "\n" TEST_BUDGET_DIGESTS
        "pairs=60\nruntime-rows=0\ntimed-groups=2\ntimed=object 1\nuntimed-groups=0\n",
        "schema=" TP_RETIREMENT_BUDGET_COUNTS_SCHEMA "\n" TEST_BUDGET_DIGESTS
        "pairs=60\nruntime-rows=0\ntimed-groups=1\ntimed=object 1\nuntimed-groups=0\nextra\n",
        "schema=" TP_RETIREMENT_BUDGET_COUNTS_SCHEMA "\n" TEST_BUDGET_DIGESTS
        "pairs=60\nruntime-rows=0\ntimed-groups=1\ntimed=object 1025\nuntimed-groups=0\n",
        /* No digests, an upper-case digest, digests out of order. */
        "schema=" TP_RETIREMENT_BUDGET_COUNTS_SCHEMA "\npairs=60\nruntime-rows=0\ntimed-groups=1\ntimed=object 1\n"
        "untimed-groups=0\n",
        "schema=" TP_RETIREMENT_BUDGET_COUNTS_SCHEMA "\n"
        "population=A111111111111111111111111111111111111111111111111111111111111111\n"
        "declaration=2222222222222222222222222222222222222222222222222222222222222222\n"
        "pairs=60\nruntime-rows=0\ntimed-groups=1\ntimed=object 1\nuntimed-groups=0\n",
        "schema=" TP_RETIREMENT_BUDGET_COUNTS_SCHEMA "\n"
        "declaration=2222222222222222222222222222222222222222222222222222222222222222\n"
        "population=1111111111111111111111111111111111111111111111111111111111111111\n"
        "pairs=60\nruntime-rows=0\ntimed-groups=1\ntimed=object 1\nuntimed-groups=0\n"};
    for (unsigned i = 0; i < BUSTER_ARRAY_LENGTH(bad_counts); ++i)
    {
        CHECK(!tp_retirement_budget_counts_parse(bad_counts[i], strlen(bad_counts[i]), &counts) && !counts.storage);
    }

    /* encode writes exactly the canonical record, which decodes, and is
     * deterministic across runs and input orderings. */
    CHECK(test_budget_run(budget_root, "encode", "input", "counts", text, sizeof(text), &size, diagnostic) == 0 &&
          size == expected_size && !memcmp(text, expected, size) && !diagnostic[0] &&
          tp_retirement_budget_decode(text, size, &decoded) && !memcmp(&decoded, &reference, sizeof(decoded)));
    CHECK(test_budget_run(budget_root, "encode", "input", "counts", again, sizeof(again), &again_size, diagnostic) == 0 &&
          again_size == size && !memcmp(again, text, size));
    CHECK(test_budget_input(budget_root, "reordered", "metrics-input-bytes=", NULL,
                            "# moved last\nmetrics-input-bytes=16384") &&
          test_budget_run(budget_root, "encode", "reordered", "counts", again, sizeof(again), &again_size,
                          diagnostic) == 0 &&
          again_size == size && !memcmp(again, text, size));
    CHECK(test_text(budget_root, "budget", text));

    /* preflight reports the derivation for the same record; remaining is
     * measured from the enforced whole-second ceiling. */
    TpRetirementBudgetOwnedCounts parsed = {0};
    TpRetirementBudgetPreflight result = {0};
    char digest[65], line[160];
    CHECK(tp_retirement_budget_counts_parse(test_budget_counts, strlen(test_budget_counts), &parsed) &&
          tp_retirement_budget_preflight(&reference, &parsed.counts, &result) && result.fits &&
          tp_retirement_budget_digest(&reference, digest));
    tp_retirement_budget_counts_release(&parsed);
    CHECK(test_budget_run(budget_root, "preflight", "budget", "counts", again, sizeof(again), &again_size,
                          diagnostic) == 0);
    snprintf(line, sizeof(line), "budget-sha256=%s\nfits=1\nrequired-ns=%" PRIu64 "\nremaining-ns=%" PRIu64 "\n",
             digest, result.required_ns, result.remaining_ns);
    CHECK(!strncmp(again, line, strlen(line)) && strstr(again, "\nuntimed-batches=8\n"));

    /* The ceiling floor: the derivation needs 62.801452027 s; 62.999999999 s
     * floors to the enforced 62 s and refuses, 63 s holds it with 0.198547973 s
     * left. */
    CHECK(test_budget_input(budget_root, "floor", "reviewed-ns=", "reviewed-ns=62999999999", NULL) &&
          test_budget_run(budget_root, "encode", "floor", "tiny", again, sizeof(again), &again_size, diagnostic) == 2 &&
          again_size == 0 && strstr(diagnostic, "requires 62801452027 ns, above the enforced whole-second ceiling "
                                                "62000000000 ns"));
    CHECK(test_budget_input(budget_root, "floor", "reviewed-ns=", "reviewed-ns=63000000000", NULL) &&
          test_budget_run(budget_root, "encode", "floor", "tiny", text, sizeof(text), &size, diagnostic) == 0 &&
          test_text(budget_root, "floor-budget", text) &&
          test_budget_run(budget_root, "preflight", "floor-budget", "tiny", again, sizeof(again), &again_size,
                          diagnostic) == 0 &&
          strstr(again, "\nrequired-ns=62801452027\nremaining-ns=198547973\n") &&
          strstr(again, "\naa-attestation-ns=1452027\naa-attestation-bytes=190320\n"));

    /* Every rule refuses with nothing on stdout and its own diagnostic; each
     * case breaks only the rule it names (covering tables, fitting ceilings). */
    struct
    {
        TestBudgetEdit edits[2];
        char const* extra;
        char const* counts;
        char const* expect;
    } const refused[] = {
        {{{"batch=4:", "batch=4:100000000\nbatch=2:100000000"}}, NULL, NULL, "timed class 1: classes must ascend"},
        {{{"batch=1024:", "batch=1024:10"}}, NULL, NULL, "timed class 1: a bound may not decrease"},
        {{{"batch=1024:", "batch=1025:2000000000"}}, NULL, NULL, "malformed table line"},
        {{{"untimed-singleton=link", NULL}}, NULL, NULL, "untimed table misses singleton=link"},
        {{{NULL, NULL}}, "singleton=link:90000000", NULL, "repeated singleton"},
        {{{"reviewed-ns=", "reviewed-ns=59999999999"}}, NULL, test_budget_tiny, "floors to 59 s, outside [60, 259200]"},
        {{{"reviewed-ns=", "reviewed-ns=259201000000000"}}, NULL, NULL, "floors to 259201 s"},
        /* 60.2 s of fixed phases: within reviewed-ns 60.5 s, above its 60 s floor. */
        {{{"reviewed-ns=", "reviewed-ns=60500000000"}, {"cleanup-ns=", "cleanup-ns=47200000000"}}, NULL, test_budget_tiny,
         "the fixed phases (60200000000 ns) exceed the enforced whole-second ceiling (60000000000 ns)"},
        {{{"reviewed-ns=", "reviewed-ns=200000000000"}}, NULL, NULL, "the derivation requires"},
        /* (v3) The AA_MEASURED re-read is required, nonzero, and counted:
         * at 1.2 s/MiB the tiny campaign's 190,320 bytes cost 0.218 s, more
         * than the 0.199 s the 63 s ceiling leaves. */
        {{{"aa-attestation-ns-per-mib=", NULL}}, NULL, NULL, "missing aa-attestation-ns-per-mib"},
        {{{"aa-attestation-ns-per-mib=", "aa-attestation-ns-per-mib=0"}}, NULL, NULL,
         "aa-attestation-ns-per-mib must be nonzero"},
        {{{"reviewed-ns=", "reviewed-ns=63000000000"}, {"aa-attestation-ns-per-mib=",
          "aa-attestation-ns-per-mib=1200000000"}}, NULL, test_budget_tiny, "the derivation requires 63017803956 ns"},
        {{{"cleanup-ns=", NULL}}, NULL, NULL, "missing cleanup-ns"},
        {{{NULL, NULL}}, "cleanup-ns=1000000000", NULL, "cleanup-ns repeated"},
        {{{"cleanup-ns=", "cleanup-ns=0"}}, NULL, NULL, "cleanup-ns must be nonzero"},
        {{{"cleanup-ns=", "cleanup-ns=01"}}, NULL, NULL, "not a canonical decimal"},
        {{{NULL, NULL}}, "unknown-ns=1", NULL, "unknown key"},
        {{{"batch=1024:", "batch=400:2000000000"}}, NULL, NULL,
         "timed group 0 (object, 416 inputs) is not covered by the timed table"},
        /* Covered by the 1024 class, but 4096 + 416 * 200000 exceeds 64 MiB. */
        {{{"metrics-input-bytes=", "metrics-input-bytes=200000"}}, NULL, NULL,
         "timed group 0: 416 inputs exceed the metrics artifact cap"},
        {{{"metrics-input-bytes=", "metrics-input-bytes=67108864"}}, NULL, NULL,
         "metrics-header-bytes plus one metrics-input-bytes exceed"},
        {{{NULL, NULL}}, NULL,
         "schema=" TP_RETIREMENT_BUDGET_COUNTS_SCHEMA "\n" TEST_BUDGET_DIGESTS
         "pairs=60\nruntime-rows=0\ntimed-groups=1\ntimed=object 4\nuntimed-groups=1\nuntimed=object 1025\n",
         "the counts are not canonical"},
        {{{NULL, NULL}}, NULL,
         "schema=" TP_RETIREMENT_BUDGET_COUNTS_SCHEMA "\n" TEST_BUDGET_DIGESTS
         "pairs=59\nruntime-rows=0\ntimed-groups=1\ntimed=object 4\nuntimed-groups=0\n",
         "the counts are outside the derivation"},
    };
    for (unsigned i = 0; i < BUSTER_ARRAY_LENGTH(refused); ++i)
    {
        CHECK(test_budget_input_edits(budget_root, "refused", refused[i].edits, 2, refused[i].extra) &&
              test_text(budget_root, "refused-counts", refused[i].counts ? refused[i].counts : test_budget_counts));
        int status = test_budget_run(budget_root, "encode", "refused", "refused-counts", again, sizeof(again),
                                     &again_size, diagnostic);
        int described = strstr(diagnostic, refused[i].expect) != NULL;
        CHECK(status == 2 && again_size == 0 && described);
        if (status != 2 || again_size || !described)
            fprintf(stderr, "TEST budget refusal %u: status %d, diagnostic %s", i, status, diagnostic);
    }
    /* preflight refuses a record that is not canonical or does not fit. */
    char changed[TP_RETIREMENT_BUDGET_BYTES + 16];
    snprintf(changed, sizeof(changed), "%s", expected);
    char* ceiling = strstr(changed, "reviewed-ns=36");
    if (ceiling) ceiling[12] = '0';
    CHECK(ceiling && test_text(budget_root, "changed", changed) &&
          test_budget_run(budget_root, "preflight", "changed", "counts", again, sizeof(again), &again_size,
                          diagnostic) == 2 &&
          again_size == 0 && strstr(diagnostic, "not a canonical"));
    TpRetirementCampaignBudget tight = reference;
    tight.reviewed_ns = UINT64_C(200000000000);
    size_t tight_size = tp_retirement_budget_encode(&tight, changed, TP_RETIREMENT_BUDGET_BYTES);
    changed[tight_size] = 0;
    CHECK(tight_size && test_text(budget_root, "tight", changed) &&
          test_budget_run(budget_root, "preflight", "tight", "counts", again, sizeof(again), &again_size,
                          diagnostic) == 2 &&
          again_size == 0 && strstr(diagnostic, "the derivation requires"));
    CHECK(test_budget_run(budget_root, "encode", "missing", "counts", again, sizeof(again), &again_size,
                          diagnostic) == 2 && strstr(diagnostic, "cannot read"));
    char* usage[] = {(char*)"encode", (char*)"only-one"};
    FILE* sink = fopen(
#ifdef _WIN32
        "NUL",
#else
        "/dev/null",
#endif
        "w");
    CHECK(sink && tp_retirement_budget_cli(2, usage, sink, sink) == 2);
    if (sink) fclose(sink);
}
