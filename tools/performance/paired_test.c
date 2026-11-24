/* Exercise the reader and the history writer through v1 manifests and versioned time-runs resources.
 * Synthetic roles, constant absolute times and the six frozen Williams rows
 * make the effects, boundary decisions and observed control errors explicit.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
typedef struct PairFixture PairFixture;
struct PairFixture
{
    unsigned base;
    unsigned head;
    double ratio;
    double control;
    double noise;
    const char *status;
    bool missing;
    bool broken_control;
    bool corrupt;
    bool reorder;
    unsigned host;
    double wall;
    size_t zero_system_rounds;
    const char *last_warmups;
    bool legacy;
    bool legacy_receipts;
    bool imported;
};

static void paired_fixture_session(FILE *file, const PairPlan *p, PairFixture f, size_t session)
{
    static const unsigned order[6][3] = {
        {
            0,
            1,
            2
        },
        {
            1,
            2,
            0
        },
        {
            2,
            0,
            1
        },
        {
            2,
            1,
            0
        },
        {
            0,
            2,
            1
        },
        {
            1,
            0,
            2
        }
    };
    if (session + 1 == p->sessions && f.last_warmups)
    {
        fputs(f.last_warmups, file);
    }
    else if (!f.legacy || f.legacy_receipts)
    {
        fprintf(file, "time-runs: completed warmups: rounds=%zu slots=%zu\n", p->warmups, 3 * p->layouts * p->copies);
    }
    for (size_t r = 0; r < p->rounds; ++r)
    {
        static const unsigned six_slots[] = {
            0,
            1,
            5,
            2,
            4,
            3
        };
        for (size_t j = 0; j < 3 * p->layouts; ++j)
        {
            size_t slot = p->layouts == 2 ? (six_slots[j] + r) % 6 : order[r % 6][j];
            size_t role = slot % 3;
            char placement[32] = "";
            if (p->layouts == 2)
            {
                snprintf(placement, sizeof(placement), " placement %zu", slot / 3 + 1);
            }
            if (f.reorder && r == 1)
            {
                role = order[0][j];
            }
            double gain[] = {
                1,
                f.ratio + (r / p->block_rounds % 2 ? f.noise : -f.noise),
                f.control
            };
            if (!f.missing || r != p->rounds - 1 || j != 2)
            {
                double wall = f.wall > 0 ? f.wall : 100;
                fprintf(file, "run %zu list 1 %s%s copy 1: %.3f ms wall, %.3f ms user, %u.000 ms system, 1 KiB wait4 peak, 0 minor faults, 0 major, 0 user cycles, 0 instructions, status 0\n",
                        r + 1, paired_roles[role], placement, wall * gain[role], role == 1 ? 100.0 : 10.0, (unsigned)(r >= f.zero_system_rounds));
            }
        }
    }
    fputs("time-runs: done, 0 failed processes\n", file);
}

static void paired_fixture(const char *manifest, const PairPlan *p, PairFixture f)
{
    char raw[128];
    snprintf(raw, sizeof(raw), "%s.log", manifest);
    FILE *file = fopen(raw, "w");
    bool valid = file != NULL;
    if (valid)
    {
        for (size_t session = 0; session < p->sessions; ++session)
        {
            paired_fixture_session(file, p, f, session);
        }
        valid = fclose(file) == 0;
    }
    PerfBuffer bytes = {0};
    char digest[65];
    valid = valid && perf_read_file(raw, &bytes) && perf_sha256(perf_buffer_bytes(&bytes), digest);
    FILE *meta = valid ? fopen(manifest, "w") : NULL;
    valid = meta != NULL;
    if (valid)
    {
        fprintf(meta, "{\"format\":\"buster-benchmark\",\"version\":1,\"id\":\"fixture\",\"kind\":\"%s\",\"status\":\"%s\",\"recorded_at\":\"2026-01-01T00:00:00Z\","
                      "\"producer\":{\"name\":\"fixture\",\"version\":\"1\"},\"limitations\":[],\"resources\":[", f.imported ? "imported" : "measured", f.status);
        if (strcmp(f.status, "planned") != 0)
        {
            const char *name = strrchr(raw, '/');
            fprintf(meta, "{\"id\":\"observations\",\"format\":\"time-runs-v%u\",\"path\":\"%s\",\"bytes\":\"%zu\",\"sha256\":\"%s\"}", f.legacy ? 1u : 2u, name + 1, bytes.size, digest);
        }
        fprintf(meta, "],\"extensions\":{\"paired\":{\"plan_sha256\":\"%s\",\"host\":\"%064x\",\"corpus\":\"%064x\",\"toolchain\":\"%064x\",\"recipe\":\"%064x\",\"harness\":\"%064x\"",
                p->digest, f.host + 1, 2, 3, 4, 5);
        for (size_t s = 0; s < 3; ++s)
        {
            unsigned rev = s == 1 ? f.head : f.base;
            fprintf(meta, ",\"%s\":{\"source\":\"%040x\",\"binaries\":[", paired_roles[s], rev);
            for (size_t layout = 0; layout < p->layouts; ++layout)
            {
                fprintf(meta, "%s\"%064x\"", layout ? "," : "", rev + 16 * (unsigned)layout + (s == 2 && f.broken_control));
            }
            fputs("]}", meta);
        }
        fputs("}}}", meta);
        valid = fclose(meta) == 0;
    }
    if (valid && f.corrupt)
    {
        FILE *bad = fopen(raw, "a");
        valid = bad && fputs("extra\n", bad) >= 0 && fclose(bad) == 0;
    }
    free(bytes.data);
    perf_expect(valid, "write paired fixture");
}

static bool paired_test_report(const PairPlan *plan, const char *manifest, PairStatus expected, size_t units, bool false_alert)
{
    PairReport report = {0};
    char *text = NULL;
    size_t size = 0;
    FILE *report_file = open_memstream(&text, &size);
    paired_report(report_file, plan, manifest, &report);
    fclose(report_file);
    PerfJson json = {0};
    PerfBytes bytes = {
        .data = (unsigned char *)text,
        .size = size
    };
    perf_expect(perf_json_parse(&json, bytes), "machine-readable verdict");
    perf_expect(report.status == expected && report.complete_units == units, "verdict and independent population");
    if (expected != PAIR_INVALID && expected != PAIR_NONE)
    {
        perf_expect(perf_json_is(&json, perf_json_find(&json, 0, "aa_false_alerts"), false_alert ? "1" : "0"), "observed A/A false alerts");
    }
    free(text);
    perf_json_free(&json);
    perf_dataset_free(&report.dataset);
    return report.promotable;
}

static bool paired_self_test(void)
{
    char tmp[] = "/tmp/buster-paired-XXXXXX";
    bool valid = mkdtemp(tmp) != NULL;
    char path[120];
    char a[120];
    char b[120];
    char ledger[120];
    snprintf(path, sizeof(path), "%s/plan", tmp);
    snprintf(a, sizeof(a), "%s/a", tmp);
    snprintf(b, sizeof(b), "%s/b", tmp);
    snprintf(ledger, sizeof(ledger), "%s/history", tmp);
    FILE *file = valid ? fopen(path, "w") : NULL;
    valid = file && mkdir(ledger, 0700) == 0;
    PairPlan p = {0};
    if (valid)
    {
        fputs("{\"version\":1,\"case\":\"fixture\",\"unit\":\"block\",\"layouts\":[\"single\"],\"sessions\":1,\"rounds\":12,\"rounds_per_block\":2,\"copies\":1,\"warmups\":1,"
              "\"metrics\":[{\"name\":\"wall_ms\",\"role\":\"primary\",\"budget_ppm\":20000},{\"name\":\"user_ms\",\"role\":\"diagnostic\",\"budget_ppm\":10000}]}", file);
        valid = fclose(file) == 0 && paired_plan(path, &p);
    }
    if (valid)
    {
        PairFixture f = {
            .base = 1,
            .head = 2,
            .ratio = 1,
            .control = 1,
            .status = "complete"
        };
        const double ratios[] = {
            1,
            0.90,
            1.02,
            1.04
        };
        const PairStatus expected[] = {
            PAIR_PASS,
            PAIR_PASS,
            PAIR_PASS,
            PAIR_REGRESSION
        };
        for (size_t i = 0; i < 4; ++i)
        {
            f.ratio = ratios[i];
            paired_fixture(a, &p, f);
            paired_test_report(&p, a, expected[i], 6, false);
        }
        f.ratio = 1;
        f.legacy = true;
        paired_fixture(a, &p, f);
        perf_expect(!paired_test_report(&p, a, PAIR_INVALID, 6, false), "legacy measurement cannot promote");
        f.imported = true;
        paired_fixture(a, &p, f);
        perf_expect(!paired_test_report(&p, a, PAIR_PASS, 6, false), "imported v1 replays a pass without promotion");
        f.imported = false;
        f.legacy = false;
        f.control = 1.04;
        paired_fixture(a, &p, f);
        paired_test_report(&p, a, PAIR_UNCERTAIN, 6, true);
        f.control = 1;
        f.missing = true;
        paired_fixture(a, &p, f);
        paired_test_report(&p, a, PAIR_INVALID, 5, false);
        f.missing = false;
        f.reorder = true;
        paired_fixture(a, &p, f);
        paired_test_report(&p, a, PAIR_INVALID, 0, false);
        f.reorder = false;
        f.broken_control = true;
        paired_fixture(a, &p, f);
        paired_test_report(&p, a, PAIR_INVALID, 0, false);
        f.broken_control = false;
        f.corrupt = true;
        paired_fixture(a, &p, f);
        paired_test_report(&p, a, PAIR_INVALID, 0, false);
        f.corrupt = false;
        f.status = "planned";
        paired_fixture(a, &p, f);
        paired_test_report(&p, a, PAIR_NONE, 0, false);
        f.status = "complete";
        f.ratio = 1.02;
        f.noise = 0.08;
        paired_fixture(a, &p, f);
        paired_test_report(&p, a, PAIR_UNCERTAIN, 6, false);
        f.noise = 0;
        // First seed. The second revision costs 1.5%, within 2%. Another
        // 1.5% passes against the latest but exceeds 2% against the anchor.
        const unsigned heads[] = {
            1,
            2,
            3,
            3,
            4,
            5,
            5
        };
        const unsigned bases[] = {
            1,
            1,
            2,
            2,
            2,
            1,
            2
        };
        const double latest_ratio[] = {
            1,
            1.015,
            1.015,
            1,
            1,
            1,
            1
        };
        const double anchor_ratio[] = {
            1,
            1.015,
            1.030225,
            1,
            1,
            1,
            1
        };
        char first_anchor[65] = "";
        char accepted[65] = "";
        const size_t attempts = sizeof(heads) / sizeof(*heads);
        for (size_t i = 0; i < attempts; ++i)
        {
            f.missing = false;
            f.base = bases[i];
            f.head = heads[i];
            f.ratio = latest_ratio[i];
            paired_fixture(a, &p, f);
            f.base = 1;
            f.ratio = anchor_ratio[i];
            f.missing = i == 4;
            paired_fixture(b, &p, f);
            char source[41];
            char event_path[160];
            snprintf(source, sizeof(source), "%040x", heads[i]);
            perf_expect(paired_record(&p, ledger, source, a, b), "append baseline decision");
            perf_expect(paired_record(&p, ledger, source, a, b), "idempotent replay");
            snprintf(event_path, sizeof(event_path), "%s/%08zu.json", ledger, i + 1);
            PerfJson event = {0};
            bool read = paired_load_json(event_path, &event, NULL);
            perf_expect(read && perf_json_is(&event, perf_json_find(&event, 0, "promoted"), (i < 2 || i == 6) ? "true" : "false"), "calibrated complete coverage against both references");
            if (i == 0)
            {
                paired_saved_hash(&event, "anchor", first_anchor);
            }
            if (i == 1)
            {
                paired_saved_hash(&event, "latest", accepted);
            }
            perf_expect(perf_json_is(&event, perf_json_find(&event, 0, "anchor"), first_anchor), "immutable anchor");
            perf_expect(i < 2 || i == 6 || perf_json_is(&event, perf_json_find(&event, 0, "latest"), accepted), "adverse attempts cannot advance baseline");
            if (i == 5)
            {
                perf_expect(perf_json_is(&event, perf_json_find(&event, 0, "decision"), "stale latest reference") &&
                            perf_json_is(&event, perf_json_find(&event, 0, "adverse"), "false"), "stale passing comparison retains a retryable decision");
            }
            if (i == 6)
            {
                perf_expect(perf_json_is(&event, perf_json_find(&event, 0, "prior_attempt"), "6"), "fresh accepted comparison links to stale attempt");
            }
            perf_json_free(&event);
        }
        for (size_t i = 1; i <= attempts; ++i)
        {
            char event[160];
            snprintf(event, sizeof(event), "%s/%08zu.json", ledger, i);
            unlink(event);
        }
    }
    if (valid)
    {
        FILE *layout_plan = fopen(path, "w");
        valid = layout_plan != NULL;
        if (valid)
        {
            fputs("{\"version\":1,\"case\":\"layout-test\",\"unit\":\"relative-layout\",\"layouts\":[\"normal\",\"shifted\"],\"sessions\":2,\"rounds\":6,\"rounds_per_block\":1,\"copies\":1,\"warmups\":1,"
                  "\"metrics\":[{\"name\":\"wall_ms\",\"role\":\"primary\",\"budget_ppm\":20000},{\"name\":\"system_ms\",\"role\":\"primary\",\"budget_ppm\":10000}]}", layout_plan);
            valid = fclose(layout_plan) == 0;
        }
        PairPlan layout = {0};
        valid = valid && paired_plan(path, &layout) && layout.primary_count == 2;
        if (valid)
        {
            PairFixture f = {
                .base = 1,
                .head = 2,
                .ratio = 1,
                .control = 1,
                .status = "complete"
            };
            paired_fixture(a, &layout, f);
            paired_test_report(&layout, a, PAIR_PASS, 2, false);
            f.zero_system_rounds = 1;
            paired_fixture(a, &layout, f);
            paired_test_report(&layout, a, PAIR_PASS, 2, false);
            f.zero_system_rounds = 4;
            paired_fixture(a, &layout, f);
            paired_test_report(&layout, a, PAIR_INVALID, 2, false);
            f.zero_system_rounds = 0;
            const char *bad_warmups[] = {
                "",
                "time-runs: completed warmups: rounds=4 slots=6\n",
                "time-runs: completed warmups: rounds=1 slots=5\n",
                "time-runs: completed warmups: rounds=1 slots=6\ntime-runs: completed warmups: rounds=1 slots=6\n"
            };
            for (size_t i = 0; i < sizeof(bad_warmups) / sizeof(*bad_warmups); ++i)
            {
                f.last_warmups = bad_warmups[i];
                paired_fixture(a, &layout, f);
                paired_test_report(&layout, a, PAIR_INVALID, 0, false);
            }
            f.last_warmups = NULL;
            paired_fixture(a, &layout, f);
            FILE *sink = tmpfile();
            perf_expect(sink != NULL, "overlap report destination");
            if (sink)
            {
                f.host = 1;
                paired_fixture(b, &layout, f);
                perf_expect(paired_overlap(sink, &layout, a, b), "matched common builds qualify overlap");
                f.legacy = true;
                paired_fixture(b, &layout, f);
                char *bridge = NULL;
                size_t size = 0;
                FILE *stream = open_memstream(&bridge, &size);
                perf_expect(!paired_overlap(stream, &layout, b, a), "unverified v1 warmups limit the bridge");
                fclose(stream);
                PerfJson overlap = {0};
                PerfBytes output = {
                    .data = (unsigned char *)bridge,
                    .size = size
                };
                bool parsed = perf_json_parse(&overlap, output);
                perf_expect(parsed && perf_json_count(&overlap, perf_json_find(&overlap, 0, "between_series")) == 4,
                            "legacy bridge retains absolute comparisons of both common builds");
                perf_json_free(&overlap);
                free(bridge);
                f.legacy_receipts = true;
                paired_fixture(b, &layout, f);
                perf_expect(paired_overlap(sink, &layout, b, a), "verified warmups allow a cross-format overlap");
                perf_expect(!paired_test_report(&layout, b, PAIR_PASS, 2, false), "overlap does not promote an older format");
                f.legacy = false;
                f.wall = 110;
                paired_fixture(b, &layout, f);
                perf_expect(!paired_overlap(sink, &layout, a, b), "within-series passes cannot hide an environment shift");
                fclose(sink);
            }
        }
        perf_json_free(&layout.json);
    }
    perf_json_free(&p.json);
    char raw[128];
    snprintf(raw, sizeof(raw), "%s.log", a);
    unlink(raw);
    snprintf(raw, sizeof(raw), "%s.log", b);
    unlink(raw);
    unlink(a);
    unlink(b);
    unlink(path);
    rmdir(ledger);
    rmdir(tmp);
    fprintf(stderr, "paired checks: %u, failures: %u\n", perf_checks, perf_failures);
    return valid && perf_failures == 0;
}
