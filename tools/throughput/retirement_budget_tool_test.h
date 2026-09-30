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
        .metrics_header_bytes = 4096, .metrics_input_bytes = 16384,
        .timed = {2, {{4, 100000000}, {TP_RETIREMENT_BATCH_INPUTS, 2000000000}},
                  {[TP_RETIREMENT_BUDGET_STAGE_LINK] = 90000000, [TP_RETIREMENT_BUDGET_STAGE_SELF_HOST] = 900000000}},
        .untimed = {2, {{4, 150000000}, {TP_RETIREMENT_BATCH_INPUTS, 3000000000}},
                    {[TP_RETIREMENT_BUDGET_STAGE_LINK] = 120000000,
                     [TP_RETIREMENT_BUDGET_STAGE_SELF_HOST] = 1200000000}}};
    return budget;
}

/* Writes the input with line `drop` (a prefix, or NULL) replaced by
 * `replace` (NULL removes it) and `extra` appended. */
static int test_budget_input(char const* root, char const* name, char const* drop, char const* replace,
    char const* extra)
{
    char text[TP_RETIREMENT_BUDGET_INPUT_BYTES];
    size_t used = (size_t)snprintf(text, sizeof(text), "schema=%s\n", TP_RETIREMENT_BUDGET_INPUT_SCHEMA);
    for (unsigned i = 0; i < BUSTER_ARRAY_LENGTH(test_budget_input_lines); ++i)
    {
        char const* line = test_budget_input_lines[i];
        if (drop && !strncmp(line, drop, strlen(drop))) line = replace;
        if (line) used += (size_t)snprintf(text + used, sizeof(text) - used, "%s\n", line);
    }
    if (extra) used += (size_t)snprintf(text + used, sizeof(text) - used, "%s\n", extra);
    return used < sizeof(text) && test_text(root, name, text);
}

/* Three timed groups (416 inputs, a link and a self-host singleton), two
 * runtime rows, 60 pairs and two untimed groups. */
static char const test_budget_counts[] = "schema=" TP_RETIREMENT_BUDGET_COUNTS_SCHEMA "\npairs=60\nruntime-rows=2\n"
    "timed-groups=3\ntimed=object 416\ntimed=link 1\ntimed=self-host-stage1 1\n"
    "untimed-groups=2\nuntimed=object 3\nuntimed=link 1\n";

/* Runs the CLI with its stdout in `out` (truncated first); returns its exit
 * status and leaves the bytes written in text. */
static int test_budget_run(char const* root, char const* command, char const* first, char const* second,
    char* text, size_t capacity, size_t* size)
{
    char paths[3][TP_PATH_CAP];
    int ok = tp_path(paths[0], root, first) && tp_path(paths[1], root, second) && tp_path(paths[2], root, "budget.out");
    FILE* out = ok ? fopen(paths[2], "w+b") : NULL;
    FILE* sink = fopen(
#ifdef _WIN32
        "NUL",
#else
        "/dev/null",
#endif
        "w");
    char* argv[] = {(char*)command, paths[0], paths[1]};
    int result = out && sink ? tp_retirement_budget_cli(3, argv, out, sink) : -1;
    *size = 0;
    if (out && fseek(out, 0, SEEK_SET) == 0) *size = fread(text, 1, capacity - 1, out);
    text[*size] = 0;
    if (out) fclose(out);
    if (sink) fclose(sink);
    return result;
}

static void test_retirement_budget_tool(char const* root)
{
    char budget_root[TP_PATH_CAP], text[8192], again[8192], expected[TP_RETIREMENT_BUDGET_BYTES];
    size_t size = 0, again_size = 0;
    TpRetirementCampaignBudget reference = test_budget_expected(), decoded = {0};
    size_t expected_size = tp_retirement_budget_encode(&reference, expected, sizeof(expected));
    CHECK(expected_size && tp_path(budget_root, root, "retirement-budget") && tp_mkdirs(budget_root));
    CHECK(test_text(budget_root, "counts", test_budget_counts) && test_budget_input(budget_root, "input", NULL, NULL, NULL));

    /* The counts codec is strict and round-trips. */
    TpRetirementBudgetOwnedCounts counts = {0};
    CHECK(tp_retirement_budget_counts_parse(test_budget_counts, strlen(test_budget_counts), &counts) &&
          counts.counts.timed.count == 3 && counts.counts.untimed.count == 2 && counts.counts.pairs == 60 &&
          counts.counts.runtime_rows == 2 && counts.counts.timed.inputs[0] == 416 &&
          counts.counts.timed.kinds[0] == TP_RETIREMENT_GROUP_OBJECT &&
          counts.counts.timed.stages[2] == TP_RETIREMENT_BUDGET_STAGE_SELF_HOST &&
          counts.counts.untimed.stages[1] == TP_RETIREMENT_BUDGET_STAGE_LINK);
    char encoded[512];
    CHECK(tp_retirement_budget_counts_encode(&counts.counts, encoded, sizeof(encoded)) == strlen(test_budget_counts) &&
          !memcmp(encoded, test_budget_counts, strlen(test_budget_counts)));
    tp_retirement_budget_counts_release(&counts);
    static char const* const bad_counts[] = {
        "schema=" TP_RETIREMENT_BUDGET_COUNTS_SCHEMA "\npairs=60\nruntime-rows=0\ntimed-groups=1\ntimed=link 2\n"
        "untimed-groups=0\n",
        "schema=" TP_RETIREMENT_BUDGET_COUNTS_SCHEMA "\npairs=60\nruntime-rows=0\ntimed-groups=1\ntimed=object 01\n"
        "untimed-groups=0\n",
        "schema=" TP_RETIREMENT_BUDGET_COUNTS_SCHEMA "\npairs=60\nruntime-rows=0\ntimed-groups=2\ntimed=object 1\n"
        "untimed-groups=0\n",
        "schema=" TP_RETIREMENT_BUDGET_COUNTS_SCHEMA "\npairs=60\nruntime-rows=0\ntimed-groups=1\ntimed=object 1\n"
        "untimed-groups=0\nextra\n",
        "schema=" TP_RETIREMENT_BUDGET_COUNTS_SCHEMA "\npairs=60\nruntime-rows=0\ntimed-groups=1\ntimed=object 1025\n"
        "untimed-groups=0\n"};
    for (unsigned i = 0; i < BUSTER_ARRAY_LENGTH(bad_counts); ++i)
    {
        CHECK(!tp_retirement_budget_counts_parse(bad_counts[i], strlen(bad_counts[i]), &counts) && !counts.storage);
    }

    /* encode writes exactly the canonical record, which decodes, and is
     * deterministic across runs and input orderings. */
    CHECK(test_budget_run(budget_root, "encode", "input", "counts", text, sizeof(text), &size) == 0 &&
          size == expected_size && !memcmp(text, expected, size) &&
          tp_retirement_budget_decode(text, size, &decoded) && !memcmp(&decoded, &reference, sizeof(decoded)));
    CHECK(test_budget_run(budget_root, "encode", "input", "counts", again, sizeof(again), &again_size) == 0 &&
          again_size == size && !memcmp(again, text, size));
    CHECK(test_budget_input(budget_root, "reordered", "metrics-input-bytes=", NULL,
                            "# moved last\nmetrics-input-bytes=16384") &&
          test_budget_run(budget_root, "encode", "reordered", "counts", again, sizeof(again), &again_size) == 0 &&
          again_size == size && !memcmp(again, text, size));
    CHECK(test_text(budget_root, "budget", text));

    /* preflight reports the derivation for the same record. */
    TpRetirementBudgetOwnedCounts parsed = {0};
    TpRetirementBudgetPreflight result = {0};
    char digest[65], line[128];
    CHECK(tp_retirement_budget_counts_parse(test_budget_counts, strlen(test_budget_counts), &parsed) &&
          tp_retirement_budget_preflight(&reference, &parsed.counts, &result) && result.fits &&
          tp_retirement_budget_digest(&reference, digest));
    tp_retirement_budget_counts_release(&parsed);
    CHECK(test_budget_run(budget_root, "preflight", "budget", "counts", again, sizeof(again), &again_size) == 0);
    snprintf(line, sizeof(line), "budget-sha256=%s\nfits=1\nrequired-ns=%" PRIu64 "\n", digest, result.required_ns);
    CHECK(!strncmp(again, line, strlen(line)) && strstr(again, "\nuntimed-batches=8\n"));

    /* Every rule refuses with nothing on stdout. */
    struct
    {
        char const* drop;
        char const* replace;
        char const* extra;
        char const* counts;
    } const refused[] = {
        {"batch=1024:", "batch=3:2000000000", NULL, NULL},                  /* classes out of order */
        {"batch=1024:", "batch=1024:10", NULL, NULL},                       /* a bound that shrinks */
        {"batch=1024:", "batch=1025:2000000000", NULL, NULL},               /* more than 1024 inputs */
        {"untimed-singleton=link", NULL, NULL, NULL},                       /* a missing singleton */
        {NULL, NULL, "singleton=link:90000000", NULL},                      /* a repeated singleton */
        {"reviewed-ns=", "reviewed-ns=59999999999", NULL, NULL},            /* ceiling below 60 s */
        {"reviewed-ns=", "reviewed-ns=259201000000000", NULL, NULL},        /* ceiling above 72 h */
        {"cleanup-ns=", "cleanup-ns=40000000000000", NULL, NULL},           /* the fixed phases do not fit */
        {"reviewed-ns=", "reviewed-ns=200000000000", NULL, NULL},           /* the derivation does not fit */
        {"cleanup-ns=", NULL, NULL, NULL},                                  /* a missing scalar */
        {NULL, NULL, "cleanup-ns=1000000000", NULL},                        /* a repeated scalar */
        {"cleanup-ns=", "cleanup-ns=0", NULL, NULL},                        /* a zero bound */
        {"cleanup-ns=", "cleanup-ns=01", NULL, NULL},                       /* a non-canonical decimal */
        {NULL, NULL, "unknown-ns=1", NULL},                                 /* an unknown key */
        {"batch=1024:", "batch=400:2000000000", NULL, NULL},                /* the 416-input group uncovered */
        {"untimed-batch=4:", "untimed-batch=2:150000000", NULL,
         "schema=" TP_RETIREMENT_BUDGET_COUNTS_SCHEMA "\npairs=60\nruntime-rows=0\ntimed-groups=1\ntimed=object 4\n"
         "untimed-groups=1\nuntimed=object 1025\n"},                        /* malformed counts */
        {"metrics-input-bytes=", "metrics-input-bytes=67108864", NULL, NULL}, /* the metrics cap */
        {NULL, NULL, NULL,
         "schema=" TP_RETIREMENT_BUDGET_COUNTS_SCHEMA "\npairs=59\nruntime-rows=0\ntimed-groups=1\ntimed=object 4\n"
         "untimed-groups=0\n"},                                             /* an odd pair count */
    };
    for (unsigned i = 0; i < BUSTER_ARRAY_LENGTH(refused); ++i)
    {
        CHECK(test_budget_input(budget_root, "refused", refused[i].drop, refused[i].replace, refused[i].extra) &&
              test_text(budget_root, "refused-counts", refused[i].counts ? refused[i].counts : test_budget_counts));
        int status = test_budget_run(budget_root, "encode", "refused", "refused-counts", again, sizeof(again),
                                     &again_size);
        CHECK(status == 2 && again_size == 0);
        if (status != 2 || again_size) fprintf(stderr, "TEST budget refusal %u was accepted\n", i);
    }
    /* preflight refuses a record that is not canonical or does not fit. */
    char changed[TP_RETIREMENT_BUDGET_BYTES + 16];
    snprintf(changed, sizeof(changed), "%s", text);
    char* ceiling = strstr(changed, "reviewed-ns=36");
    if (ceiling) ceiling[12] = '0';
    CHECK(ceiling && test_text(budget_root, "changed", changed) &&
          test_budget_run(budget_root, "preflight", "changed", "counts", again, sizeof(again), &again_size) == 2 &&
          again_size == 0);
    TpRetirementCampaignBudget tight = reference;
    tight.reviewed_ns = UINT64_C(200000000000);
    size_t tight_size = tp_retirement_budget_encode(&tight, changed, TP_RETIREMENT_BUDGET_BYTES);
    changed[tight_size] = 0;
    CHECK(tight_size && test_text(budget_root, "tight", changed) &&
          test_budget_run(budget_root, "preflight", "tight", "counts", again, sizeof(again), &again_size) == 2 &&
          again_size == 0);
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
