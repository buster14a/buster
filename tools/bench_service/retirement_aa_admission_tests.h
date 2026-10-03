/* Production A/A admission (#881 in-job gate): retirement_aa_admission.c's
 * layers over synthetic inputs. Included by tests.c, which builds the
 * installed service (no fixture admission), after its fixtures.
 *
 * Covered: the gate's fixed values (bq_test_aa_gate: P = 60 and band strings
 * whose nearest doubles are the bounds the check uses); the band over the
 * derived family's A/A intervals from a synthetic A/A spool
 * (bq_test_aa_band): rows inside the band admitted with every member
 * assessed; an exact cell outside the band, a bootstrap aggregate whose
 * interval crosses the upper bound and a cell whose second round lies below
 * the lower bound refused; a consistent spool and plan of P = 62 refused at
 * both the rows and the band; runtime rows other than U = R (one runtime row
 * dropped or one added, attested as such) refused; rows whose canonical
 * lines are not the attested AA_MEASURED digest refused (#1021); and
 * unbounded or crossing bounds outside bq_retirement_aa_inside; and the v3
 * receipt from the binding context's identities (bq_test_aa_receipt: exact
 * canonical bytes and digest with the fixed band and no policy digest, no
 * receipt without a phase receipt digest, a context of another family or an
 * identity needing escapes refused).
 *
 * Map: bq_test_aa_gate, BqTestAaSpool, bq_test_aa_spool_fill,
 * bq_test_aa_spool_digest, bq_test_aa_band, bq_test_aa_receipt,
 * bq_test_retirement_aa_admission.
 */
#if defined(__linux__) && !defined(BQ_RETIREMENT_UNIT_CAMPAIGN_FIXTURE_AA)
#define BQ_TEST_AA_ARENA_BYTES (UINT64_C(1) << 30)
#define BQ_TEST_AA_FAMILY "5555555555555555555555555555555555555555555555555555555555555555"
#define BQ_TEST_AA_PHASE "9999999999999999999999999999999999999999999999999999999999999999"

/* The gate's fixed values: P = 60, and the band strings the receipt records
 * read (strtod, the service's C locale) as exactly the bounds the check
 * compares against, inside 0 < lower < 1 < upper. */
BUSTER_GLOBAL_LOCAL void bq_test_aa_gate(void)
{
    double lower = strtod(BQ_RETIREMENT_UNIT_CAMPAIGN_AA_BAND_LOWER, NULL);
    double upper = strtod(BQ_RETIREMENT_UNIT_CAMPAIGN_AA_BAND_UPPER, NULL);
    BQ_CHECK(BQ_RETIREMENT_AA_PAIRS == 60u && lower == BQ_RETIREMENT_AA_LOWER_BOUND &&
             upper == BQ_RETIREMENT_AA_UPPER_BOUND && lower == 0.98 && upper == 1.02 && 0.0 < lower &&
             lower < 1.0 && 1.0 < upper &&
             !strcmp(BQ_RETIREMENT_UNIT_CAMPAIGN_AA_BAND, "{\"lower\":\"0.98\",\"upper\":\"1.02\"}"));
}

/* A synthetic finished A/A spool over the test layout: three timed rows (row
 * 0 a runtime singleton, rows 1 and 2 one object group), two rounds of
 * `pairs` pairs. Wall and runtime ratios stay within 0.1% of 1; peak memory
 * and batch RSS are equal except, in `memory_mode` 1, row 1's peak memory (5%
 * above: an exact cell outside the band), in mode 2, the object group's
 * batch RSS in every other block (4% above: a bootstrap interval crossing
 * the upper bound) and, in mode 3, row 2's peak memory in the second round
 * only (5% below: that round's interval under the lower bound). The rows in
 * `runtime_rows` (a bit per row; the layout's own is row 0 alone) carry
 * runtime pairs and their flag. */
typedef struct BqTestAaSpool
{
    TpRetirementSamples samples;
    TpRetirementSampleRow rows[3];
    TpRetirementSampleGroup groups[2];
    unsigned pairs;
} BqTestAaSpool;

BUSTER_GLOBAL_LOCAL bool bq_test_aa_spool_fill(BqTestAaSpool* spool, FILE* file, unsigned pairs, unsigned memory_mode,
    unsigned runtime_rows)
{
    u64 per_unit = (u64)TP_RETIREMENT_ROUNDS * pairs;
    *spool = (BqTestAaSpool){0};
    spool->pairs = pairs;
    for (u32 row = 0; row < 3; row += 1)
    {
        spool->rows[row].id = row;
        spool->rows[row].metrics = (runtime_rows >> row) & 1u ? TP_RETIREMENT_SAMPLE_RUNTIME : 0;
    }
    spool->groups[0].object = TP_RETIREMENT_NONE;
    spool->groups[1].object = 0;
    spool->samples = (TpRetirementSamples){.spool = file, .rows = spool->rows, .groups = spool->groups,
        .expected = 4u * per_unit, .row_records = 3u * per_unit, .row_count = 3, .group_count = 2,
        .object_count = 1, .finished = 1};
    bool ok = file != NULL && pairs;
    for (u64 ordinal = 0; ok && ordinal < 4u * per_unit; ordinal += 1)
    {
        u64 unit = ordinal / per_unit, pair = ordinal % pairs, round = ordinal / pairs % 2u;
        u64 wall = UINT64_C(1000000000), memory = UINT64_C(4096000);
        u64 values[TP_RETIREMENT_SAMPLE_VALUES] = {wall, wall - 1000000u + (ordinal * 7919u) % 2000001u, memory,
                                                   memory, 0, 0, 0};
        if (unit == 1 && memory_mode == 1) values[3] = memory / 100u * 105u;
        if (unit == 3 && memory_mode == 2 && (pair / 2u) % 2u) values[3] = memory / 100u * 104u;
        if (unit == 2 && memory_mode == 3 && round == 1) values[3] = memory / 100u * 95u;
        if (unit < 3 && (runtime_rows >> unit) & 1u)
        {
            values[4] = UINT64_C(20000000);
            values[5] = values[4] - 20000u + (ordinal * 104729u) % 40001u;
        }
        unsigned char bytes[TP_RETIREMENT_SAMPLE_RECORD_BYTES];
        for (u32 index = 0; index < TP_RETIREMENT_SAMPLE_VALUES; index += 1)
            tp_retirement_sample_pack(bytes + index * 8u, values[index]);
        ok = fseeko(file, (off_t)(ordinal * sizeof(bytes)), SEEK_SET) == 0 &&
             fwrite(bytes, 1, sizeof(bytes), file) == sizeof(bytes);
    }
    ok = ok && fflush(file) == 0;
    return ok;
}

/* The spool's canonical A/A row digest as lane D exports it (the AA_MEASURED
 * digest): every row record under the spool's current runtime flags, then
 * the batch record of group 1 (object 0), through lane D's encoders. */
BUSTER_GLOBAL_LOCAL bool bq_test_aa_spool_digest(BqTestAaSpool* spool, char digest[SHA256_HEX_CAPACITY])
{
    u64 per_unit = (u64)TP_RETIREMENT_ROUNDS * spool->pairs;
    Sha256 hash;
    sha256_init(&hash);
    bool ok = spool->pairs != 0;
    for (u64 ordinal = 0; ok && ordinal < 4u * per_unit; ordinal += 1)
    {
        uint64_t values[TP_RETIREMENT_SAMPLE_VALUES];
        char line[TP_RETIREMENT_SAMPLE_LINE_CAP];
        u32 unit = (u32)(ordinal / per_unit), round = (u32)(ordinal / spool->pairs % 2u);
        u32 pair = (u32)(ordinal % spool->pairs);
        ok = tp_retirement_sample_read(&spool->samples, ordinal, values);
        size_t length = !ok ? 0 : unit == 3 ? tp_retirement_batch_record(line, sizeof(line), 1, round, pair, values) :
            tp_retirement_sample_record(line, sizeof(line), unit, round, pair, spool->rows[unit].metrics, values);
        ok = ok && length;
        if (ok) sha256_add(&hash, line, (u64)length);
    }
    if (ok) sha256_finish_hex(&hash, (char8*)digest);
    return ok;
}

/* (U = R) A spool whose rows carrying runtime pairs are not the family's
 * runtime-eligible rows (row 0) refuses even when the attested digest is
 * that spool's own canonical lines: row 0's runtime pairs dropped, or row 1's
 * added. The layout's own runtime rows are admitted. */
BUSTER_GLOBAL_LOCAL void bq_test_aa_runtime_rows(TpRetirementComposeFamily const* family, Arena* arena)
{
    unsigned const masks[3] = {0u, 3u, 1u};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(masks); index += 1)
    {
        BqTestAaSpool spool;
        FILE* file = tmpfile();
        char attested[SHA256_HEX_CAPACITY] = {0};
        double* unused[TP_RETIREMENT_COMPOSE_METRICS] = {0};
        bool ready = bq_test_aa_spool_fill(&spool, file, 60, 0, masks[index]) &&
                     bq_test_aa_spool_digest(&spool, attested);
        bool admitted = ready && bq_retirement_aa_ratios(&spool.samples, family, 60, attested, arena, unused);
        BQ_CHECK(ready && admitted == (masks[index] == 1u));
        if (file) fclose(file);
    }
}

/* (P = 60) A consistent spool of 62 pairs per round, attested as such,
 * refuses at the rows, and read as 60-pair rows it cannot admit either. (A
 * plan of 62 pairs over admissible rows refuses at the band: bq_test_aa_band.) */
BUSTER_GLOBAL_LOCAL void bq_test_aa_pairs(TpRetirementComposeFamily const* family, Arena* arena)
{
    BqTestAaSpool spool;
    FILE* file = tmpfile();
    char attested[SHA256_HEX_CAPACITY] = {0};
    double* unused[TP_RETIREMENT_COMPOSE_METRICS] = {0};
    BQ_CHECK(bq_test_aa_spool_fill(&spool, file, 62, 0, 1u) && bq_test_aa_spool_digest(&spool, attested) &&
             !bq_retirement_aa_ratios(&spool.samples, family, 62, attested, arena, unused) &&
             !bq_retirement_aa_ratios(&spool.samples, family, 60, attested, arena, unused));
    if (file) fclose(file);
}

BUSTER_GLOBAL_LOCAL void bq_test_aa_band(void)
{
    static char const* const lead[TP_RETIREMENT_COMPOSE_DIMENSIONS] = {"x86_64-unknown-linux-gnu", "znver5", "arena",
        "c", "pie", "link"};
    static char const* const object[TP_RETIREMENT_COMPOSE_DIMENSIONS] = {"x86_64-unknown-linux-gnu", "znver5",
        "arena", "c", "pie", "object"};
    TpRetirementComposeRow rows[3] = {{0, 0, 1, {0}}, {1, 1, 0, {0}}, {2, 1, 0, {0}}};
    for (u32 dimension = 0; dimension < TP_RETIREMENT_COMPOSE_DIMENSIONS; dimension += 1)
    {
        rows[0].dimensions[dimension] = lead[dimension];
        rows[1].dimensions[dimension] = rows[2].dimensions[dimension] = object[dimension];
    }
    unsigned const kinds[2] = {TP_RETIREMENT_GROUP_SINGLETON, TP_RETIREMENT_GROUP_OBJECT};
    TpRetirementComposeLayout layout = {rows, kinds, 3, 2, 3, 0};
    for (unsigned mode = 0; mode < 4; mode += 1)
    {
        Arena* arena = arena_create((ArenaCreation){.reserved_size = BQ_TEST_AA_ARENA_BYTES, .flags = {.no_pool = 1}});
        TpRetirementComposeFamily family = {0};
        BqTestAaSpool spool;
        FILE* file = tmpfile();
        double* ratios[TP_RETIREMENT_COMPOSE_METRICS] = {0};
        BqRetirementAaBand band = {0};
        bool built = arena && tp_retirement_compose_family(&layout, arena, &family);
        /* Wall and memory over three rows, runtime over row 0, the batch
         * pair over the object group: nine exact cells. */
        BQ_CHECK(built && family.cells_total == 9 && family.bootstrap > 0 && family.runtime_count == 1 &&
                 family.object_count == 1);
        char attested[SHA256_HEX_CAPACITY] = {0};
        BQ_CHECK(bq_test_aa_spool_fill(&spool, file, 60, mode, 1u) && bq_test_aa_spool_digest(&spool, attested) &&
                 bq_retirement_aa_ratios(&spool.samples, &family, 60, attested, arena, ratios));
        TpRetirementPlan plan = {4260881u, TP_RETIREMENT_STATISTICS_VERSION, family.bootstrap, family.cells_total, 60,
                                 TP_RETIREMENT_MIN_RESAMPLES, 1};
        bool admitted = built && ratios[0] && bq_retirement_aa_band_check(&plan, &family, ratios, arena, &band);
        if (mode == 0)
        {
            /* In band: every member assessed and admitted. */
            BQ_CHECK(admitted && band.assessed == family.count && band.refused == TP_RETIREMENT_COMPOSE_NONE);
            /* The same rows under a plan of another P refuse. */
            TpRetirementPlan other = plan;
            other.pairs_per_round = 62;
            BQ_CHECK(!bq_retirement_aa_band_check(&other, &family, ratios, arena, &band));
            if (built) bq_test_aa_pairs(&family, arena);
            /* (#1021) Rows whose canonical lines are not the attested ones
             * refuse: another attested digest, or one record changed after
             * the attestation. */
            double* unused[TP_RETIREMENT_COMPOSE_METRICS] = {0};
            char forged[SHA256_HEX_CAPACITY];
            memcpy(forged, attested, sizeof(forged));
            forged[0] = forged[0] == 'a' ? 'b' : 'a';
            BQ_CHECK(!bq_retirement_aa_ratios(&spool.samples, &family, 60, forged, arena, unused));
            uint64_t values[TP_RETIREMENT_SAMPLE_VALUES];
            unsigned char bytes[TP_RETIREMENT_SAMPLE_RECORD_BYTES];
            bool read = tp_retirement_sample_read(&spool.samples, 130, values);
            values[1] += 1;
            for (u32 index = 0; index < TP_RETIREMENT_SAMPLE_VALUES; index += 1)
                tp_retirement_sample_pack(bytes + index * 8u, values[index]);
            BQ_CHECK(read && fseeko(file, (off_t)(130u * sizeof(bytes)), SEEK_SET) == 0 &&
                     fwrite(bytes, 1, sizeof(bytes), file) == sizeof(bytes) && fflush(file) == 0 &&
                     !bq_retirement_aa_ratios(&spool.samples, &family, 60, attested, arena, unused));
            values[1] -= 1;
            for (u32 index = 0; index < TP_RETIREMENT_SAMPLE_VALUES; index += 1)
                tp_retirement_sample_pack(bytes + index * 8u, values[index]);
            BQ_CHECK(fseeko(file, (off_t)(130u * sizeof(bytes)), SEEK_SET) == 0 &&
                     fwrite(bytes, 1, sizeof(bytes), file) == sizeof(bytes) && fflush(file) == 0 &&
                     bq_retirement_aa_ratios(&spool.samples, &family, 60, attested, arena, unused));
            /* A spool that is not finished or of another row refuses. */
            spool.samples.finished = 0;
            BQ_CHECK(!bq_retirement_aa_ratios(&spool.samples, &family, 60, attested, arena, unused));
            spool.samples.finished = 1;
            spool.rows[2].id = 7;
            BQ_CHECK(!bq_retirement_aa_ratios(&spool.samples, &family, 60, attested, arena, unused));
            spool.rows[2].id = 2;
            if (built) bq_test_aa_runtime_rows(&family, arena);
        }
        else
        {
            /* The refused member: peak memory's row-1 cell, outside the
             * band (mode 1), the batch RSS aggregate, the first member in
             * canonical order, crossing its upper bound (mode 2), or row 2's
             * peak memory with its second round under the lower bound while
             * its first round is inside (mode 3). */
            TpRetirementComposeMember const* member = built && band.refused < family.count ?
                family.members + family.order[band.refused] : NULL;
            BQ_CHECK(!admitted && member && band.assessed == band.refused + 1u);
            if (mode == 1)
                BQ_CHECK(member && !strcmp(member->name, "compiler_peak_memory/cell/row=1") &&
                         band.bounds[2].lower > 1.02);
            if (mode == 2)
                BQ_CHECK(member && !strcmp(member->name, "compiler_batch_peak_rss/aggregate") && !band.refused &&
                         band.bounds[2].upper > 1.02 && band.bounds[2].lower < 1.02);
            if (mode == 3)
                BQ_CHECK(member && member->metric == TP_RETIREMENT_PEAK_MEMORY &&
                         bq_retirement_aa_inside(&band.bounds[0]) && !bq_retirement_aa_inside(&band.bounds[1]) &&
                         band.bounds[1].lower < 0.98);
        }
        if (file) fclose(file);
        if (arena) arena_destroy(arena, 1);
    }
    /* Unbounded and crossing bounds are outside; a bounded interval inside
     * the band is inside, its endpoints included; one just past either
     * endpoint is outside. */
    TpRetirementBound unbounded = {1.0, 0.0, INFINITY}, crossing = {1.0, 0.97, 1.0}, inside = {1.0, 0.98, 1.02};
    TpRetirementBound below = {1.0, 0.979, 1.0}, above = {1.0, 1.0, 1.021};
    BQ_CHECK(!bq_retirement_aa_inside(&unbounded) && !bq_retirement_aa_inside(&crossing) &&
             bq_retirement_aa_inside(&inside) && !bq_retirement_aa_inside(&below) && !bq_retirement_aa_inside(&above));
}

BUSTER_GLOBAL_LOCAL void bq_test_aa_receipt(Arena* arena)
{
    char const* const sections[3] = {
        "{\"host\":{\"machine_id\":\"zen5-9700x-01\"},\"profile\":{\"id\":\"zen5-9700x-native\","
        "\"version\":\"profile-v1\"},\"service\":{\"id\":\"retirement-9700x\"}}",
        "{\"baseline\":{\"source_commit\":\"3333333333333333333333333333333333333333\","
        "\"source_tree\":\"4444444444444444444444444444444444444444\"}}",
        "{\"statistical_family\":{\"sha256\":\"" BQ_TEST_AA_FAMILY "\"}}"};
    u32 const indexes[3] = {BQ_RETIREMENT_WORKER_BINDING_EXECUTION, BQ_RETIREMENT_WORKER_BINDING_SUBJECTS,
                            BQ_RETIREMENT_WORKER_BINDING_POPULATION};
    BqRetirementWorkerBindingContext context = {0};
    bool parsed = true;
    for (u32 index = 0; index < 3; index += 1)
        parsed = parsed && tp_retirement_compose_json_parse((unsigned char const*)sections[index],
                                                            strlen(sections[index]), arena,
                                                            &context.json[indexes[index]]);
    BqRetirementAaIdentities identities = {0};
    char receipt[BQ_RETIREMENT_UNIT_CAMPAIGN_AA_RECEIPT_BYTES_MAX], digest[SHA256_HEX_CAPACITY] = {0};
    char expected_digest[SHA256_HEX_CAPACITY] = {0};
    u32 length = 0;
    char const* expected = "{\"aa_decision\":\"admitted\",\"admitted\":true,"
        "\"baseline_source_commit\":\"3333333333333333333333333333333333333333\","
        "\"baseline_source_tree\":\"4444444444444444444444444444444444444444\","
        "\"equivalence_band\":{\"lower\":\"0.98\",\"upper\":\"1.02\"},\"family_sha256\":\"" BQ_TEST_AA_FAMILY "\","
        "\"lease_protocol\":\"server-authoritative-supervisor-lease-v1\",\"logical_cpu\":3,"
        "\"machine_id\":\"zen5-9700x-01\",\"native_only\":true,\"native_target\":\"x86_64-unknown-linux-gnu\","
        "\"phase_receipt_sha256\":\"" BQ_TEST_AA_PHASE "\","
        "\"profile_id\":\"zen5-9700x-native\",\"profile_version\":\"profile-v1\","
        "\"schema\":\"buster-native-retirement-aa-admission-v3\",\"service_id\":\"retirement-9700x\",\"version\":3}";
    bq_digest(expected, (u32)strlen(expected), (char8*)expected_digest);
    BQ_CHECK(parsed && bq_retirement_aa_identities(&context, BQ_TEST_AA_FAMILY, &identities) &&
             bq_retirement_aa_receipt_render(&identities, 3, BQ_TEST_AA_FAMILY, BQ_TEST_AA_PHASE, receipt, &length,
                                             digest) &&
             length == strlen(expected) && !memcmp(receipt, expected, length) && !strcmp(digest, expected_digest) &&
             !strstr(receipt, "aa_policy_sha256"));
    /* (#1021) No phase receipt digest, no receipt. */
    BQ_CHECK(!bq_retirement_aa_receipt_render(&identities, 3, BQ_TEST_AA_FAMILY, NULL, receipt, &length, digest) &&
             !length && !digest[0] &&
             !bq_retirement_aa_receipt_render(&identities, 3, BQ_TEST_AA_FAMILY, "short", receipt, &length, digest) &&
             !length);
    /* A context binding another family, or an identity that would need
     * escaping, renders nothing. */
    BQ_CHECK(!bq_retirement_aa_identities(&context, "8888888888888888888888888888888888888888888888888888888888888888",
                                          &identities) && !identities.machine_id[0]);
    char const* quoted = "{\"host\":{\"machine_id\":\"zen5\\\"01\"},\"profile\":{\"id\":\"p\",\"version\":\"v\"},"
                         "\"service\":{\"id\":\"s\"}}";
    BQ_CHECK(tp_retirement_compose_json_parse((unsigned char const*)quoted, strlen(quoted), arena,
                                              &context.json[BQ_RETIREMENT_WORKER_BINDING_EXECUTION]) &&
             !bq_retirement_aa_identities(&context, BQ_TEST_AA_FAMILY, &identities));
}

BUSTER_GLOBAL_LOCAL void bq_test_retirement_aa_admission(void)
{
    Arena* arena = arena_create((ArenaCreation){.reserved_size = BQ_TEST_AA_ARENA_BYTES, .flags = {.no_pool = 1}});
    BQ_CHECK(arena != NULL);
    bq_test_aa_gate();
    if (arena)
    {
        bq_test_aa_receipt(arena);
        arena_destroy(arena, 1);
    }
    bq_test_aa_band();
}
#endif
