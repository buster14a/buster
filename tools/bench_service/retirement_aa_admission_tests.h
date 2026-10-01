/* Production A/A admission (#426 plan step 6): retirement_aa_admission.c's
 * layers over synthetic inputs. Included by tests.c, which builds the
 * installed service (no fixture admission), after its fixtures.
 *
 * Covered: the policy check against the profile's aa-policy-sha256= pin
 * (bq_test_aa_policy: refusing bytes of another digest, a missing, repeated
 * or malformed pin, the compiled blocked profile, a status other than
 * eligible, ab_authorized true, another current-job P or U rule, a malformed
 * or out-of-order band and a document without its line feed, and accepting
 * an eligible synthetic policy in zen5_aa_evaluator.py's canonical form);
 * the band over the derived family's A/A intervals from a synthetic A/A
 * spool (bq_test_aa_band: rows inside the band admitted with every member
 * assessed, rows whose canonical lines are not the attested AA_MEASURED
 * digest refused (#1021), an exact cell outside the band and a bootstrap aggregate whose
 * interval crosses it refused, a plan of another P refused, and unbounded or crossing bounds
 * outside bq_retirement_aa_inside); and the v2 receipt from the binding
 * context's identities (bq_test_aa_receipt: exact canonical bytes and
 * digest, no receipt without a phase receipt digest, a context of another family or an identity needing escapes
 * refused).
 *
 * Map: bq_test_aa_policy_bytes (the synthetic policy document),
 * bq_test_aa_profile, bq_test_aa_policy, BqTestAaSpool,
 * bq_test_aa_spool_fill, bq_test_aa_band, bq_test_aa_receipt,
 * bq_test_retirement_aa_admission.
 */
#if defined(__linux__) && !defined(BQ_RETIREMENT_UNIT_CAMPAIGN_FIXTURE_AA)
#define BQ_TEST_AA_POLICY_BYTES 4096u
#define BQ_TEST_AA_ARENA_BYTES (UINT64_C(1) << 30)
#define BQ_TEST_AA_FAMILY "5555555555555555555555555555555555555555555555555555555555555555"
#define BQ_TEST_AA_PHASE "9999999999999999999999999999999999999999999999999999999999999999"

/* A buster-zen5-aa-policy-v1 document as the evaluator's canonical_bytes
 * writes it (sorted keys, compact separators, ASCII, one line feed), with
 * the top-level fields the admission reads set by the caller. The embedded
 * protocol repeats its own current_job, as build_policy's does. */
BUSTER_GLOBAL_LOCAL u32 bq_test_aa_policy_bytes(char output[BQ_TEST_AA_POLICY_BYTES], char const* status,
    char const* authorized, unsigned pairs, char const* runtime, char const* lower, char const* upper)
{
    int written = snprintf(output, BQ_TEST_AA_POLICY_BYTES,
        "{\"ab_authorized\":%s,\"attempts\":[],\"candidate_decision\":\"not-evaluated\","
        "\"current_job_aa\":{\"equivalence_band\":{\"lower\":\"%s\",\"upper\":\"%s\"},\"pairs_per_round\":%u,"
        "\"runtime_rows\":\"%s\"},\"evaluator\":{\"sources\":{}},\"evidence\":{\"confirmatory_declared\":64,"
        "\"confirmatory_listed\":64,\"confirmatory_valid\":64,\"minimum_for_finite_bound\":64,\"pilot_listed\":5,"
        "\"pilot_valid\":5,\"sufficient\":true,\"window_jobs\":[]},\"family\":{},\"family_withheld\":null,"
        "\"method\":{\"family_alpha\":\"0.05\",\"family_size\":42,\"optional_stopping\":false,"
        "\"outlier_deletion\":false,\"quantile\":\"0.90\"},\"pilot_summary\":{},"
        "\"policy_id\":\"buster-zen5-aa-eligibility-v1\",\"protocol\":{\"current_job\":{\"equivalence_band\":"
        "{\"lower\":\"%s\",\"upper\":\"%s\"},\"pairs_per_round\":%u,\"runtime_rows\":\"%s\"},"
        "\"schema\":\"buster-zen5-aa-protocol-v1\",\"version\":1},\"protocol_sha256\":"
        "\"6666666666666666666666666666666666666666666666666666666666666666\",\"reasons\":[],"
        "\"schema\":\"buster-zen5-aa-policy-v1\",\"status\":\"%s\",\"version\":1}\n",
        authorized, lower, upper, pairs, runtime, lower, upper, pairs, runtime, status);
    u32 length = written > 0 && written < (int)BQ_TEST_AA_POLICY_BYTES ? (u32)written : 0;
    return length;
}

/* A profile pinning `bytes` (or `pin` when given) under other lines. */
BUSTER_GLOBAL_LOCAL String8 bq_test_aa_profile(char output[512], u8 const* bytes, u32 length, char const* pin,
    bool twice)
{
    char digest[SHA256_HEX_CAPACITY] = {0};
    if (!pin) bq_digest(bytes, length, (char8*)digest);
    char const* value = pin ? pin : digest;
    int written = snprintf(output, 512, "status=admitted\naa-policy-sha256=%s\ncampaign-pairs=60\n%s%s%s", value,
                           twice ? "aa-policy-sha256=" : "", twice ? value : "", twice ? "\n" : "");
    String8 result = written > 0 && written < 512 ? (String8){(char8*)output, (u64)written} : (String8){0};
    return result;
}

BUSTER_GLOBAL_LOCAL void bq_test_aa_policy(Arena* arena)
{
    char document[BQ_TEST_AA_POLICY_BYTES], profile_text[512];
    BqRetirementAaPolicy policy = {0};
    u32 length = bq_test_aa_policy_bytes(document, "eligible", "false", 60, "U=R", "0.98", "1.02");
    String8 profile = bq_test_aa_profile(profile_text, (u8 const*)document, length, NULL, false);
    char digest[SHA256_HEX_CAPACITY] = {0};
    bq_digest(document, length, (char8*)digest);
    /* Eligible, pinned: admitted with the band exactly as recorded. */
    BQ_CHECK(length && bq_retirement_aa_policy_check(profile, (u8 const*)document, length, arena, &policy) == BQ_OK &&
             !strcmp(policy.sha256, digest) && !strcmp(policy.lower, "0.98") && !strcmp(policy.upper, "1.02") &&
             policy.lower_bound == 0.98 && policy.upper_bound == 1.02 && policy.pairs_per_round == 60);
    /* Bytes other than the pinned ones: one changed byte, or the pin of
     * another document. */
    char changed[BQ_TEST_AA_POLICY_BYTES];
    memcpy(changed, document, length);
    changed[length - 3] = '2';
    BQ_CHECK(bq_retirement_aa_policy_check(profile, (u8 const*)changed, length, arena, &policy) ==
             BQ_RECIPE_MISMATCH && !policy.sha256[0]);
    char other[BQ_TEST_AA_POLICY_BYTES], other_profile[512];
    u32 other_length = bq_test_aa_policy_bytes(other, "eligible", "false", 60, "U=R", "0.97", "1.03");
    String8 pinned_other = bq_test_aa_profile(other_profile, (u8 const*)other, other_length, NULL, false);
    BQ_CHECK(bq_retirement_aa_policy_check(pinned_other, (u8 const*)document, length, arena, &policy) ==
             BQ_RECIPE_MISMATCH);
    /* No pin (the compiled blocked profile pins none), a repeated pin and a
     * pin that is not 64 lowercase hex digits refuse before any field. */
    String8 none = S8("status=admitted\ncampaign-pairs=60\n");
    BQ_CHECK(bq_retirement_aa_policy_check(none, (u8 const*)document, length, arena, &policy) == BQ_RECIPE_MISMATCH);
    BQ_CHECK(bq_retirement_aa_policy_check(bq_recipe_profile(BQ_RECIPE_NATIVE_RETIREMENT_BLOCKED),
             (u8 const*)document, length, arena, &policy) == BQ_RECIPE_MISMATCH);
    char twice_text[512], upper_text[512], upper_pin[SHA256_HEX_CAPACITY];
    String8 twice = bq_test_aa_profile(twice_text, (u8 const*)document, length, NULL, true);
    BQ_CHECK(bq_retirement_aa_policy_check(twice, (u8 const*)document, length, arena, &policy) == BQ_RECIPE_MISMATCH);
    for (u32 index = 0; index < SHA256_HEX_CAPACITY; index += 1)
        upper_pin[index] = digest[index] >= 'a' && digest[index] <= 'f' ? (char)(digest[index] - 'a' + 'A') :
                           digest[index];
    String8 shouting = bq_test_aa_profile(upper_text, NULL, 0, upper_pin, false);
    BQ_CHECK(strcmp(upper_pin, digest) &&
             bq_retirement_aa_policy_check(shouting, (u8 const*)document, length, arena, &policy) ==
             BQ_RECIPE_MISMATCH);
    /* Pinned but not admissible: each document below is pinned exactly. */
    struct
    {
        char const* status;
        char const* authorized;
        unsigned pairs;
        char const* runtime;
        char const* lower;
        char const* upper;
    } const refused[] = {
        {"inconclusive", "false", 60, "U=R", "0.98", "1.02"},
        {"unavailable", "false", 60, "U=R", "0.98", "1.02"},
        {"invalid", "false", 60, "U=R", "0.98", "1.02"},
        {"Eligible", "false", 60, "U=R", "0.98", "1.02"},
        {"eligible", "true", 60, "U=R", "0.98", "1.02"},
        {"eligible", "null", 60, "U=R", "0.98", "1.02"},
        {"eligible", "false", 62, "U=R", "0.98", "1.02"},
        {"eligible", "false", 60, "U<R", "0.98", "1.02"},
        {"eligible", "false", 60, "U=R", "1.01", "1.02"},
        {"eligible", "false", 60, "U=R", "0.98", "0.99"},
        {"eligible", "false", 60, "U=R", "0", "1.02"},
        {"eligible", "false", 60, "U=R", "00.98", "1.02"},
        {"eligible", "false", 60, "U=R", ".98", "1.02"},
        {"eligible", "false", 60, "U=R", "0.98", "1.02e0"},
        {"eligible", "false", 60, "U=R", "0.9800000000001", "1.02"},
        {"eligible", "false", 60, "U=R", "0.98", "1000000000"},
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(refused); index += 1)
    {
        char bad[BQ_TEST_AA_POLICY_BYTES], bad_profile[512];
        u32 bad_length = bq_test_aa_policy_bytes(bad, refused[index].status, refused[index].authorized,
            refused[index].pairs, refused[index].runtime, refused[index].lower, refused[index].upper);
        String8 pinned = bq_test_aa_profile(bad_profile, (u8 const*)bad, bad_length, NULL, false);
        BQ_CHECK(bad_length && bq_retirement_aa_policy_check(pinned, (u8 const*)bad, bad_length, arena, &policy) ==
                 BQ_RECIPE_MISMATCH && !policy.sha256[0]);
    }
    /* The canonical document ends with its line feed; without it the pin of
     * the shorter bytes still cannot admit it. */
    char cut_profile[512];
    String8 cut = bq_test_aa_profile(cut_profile, (u8 const*)document, length - 1u, NULL, false);
    BQ_CHECK(bq_retirement_aa_policy_check(cut, (u8 const*)document, length - 1u, arena, &policy) ==
             BQ_RECIPE_MISMATCH);
}

/* A synthetic finished A/A spool over the test layout: three timed rows (row
 * 0 a runtime singleton, rows 1 and 2 one object group), two rounds of 60
 * pairs. Wall and runtime ratios stay within 0.1% of 1; peak memory and
 * batch RSS are equal except, in `memory_mode` 1, row 1's peak memory (5%
 * above: an exact cell outside the band) and, in mode 2, the object group's
 * batch RSS in every other block (4% above: a bootstrap interval crossing
 * the upper bound). */
typedef struct BqTestAaSpool
{
    TpRetirementSamples samples;
    TpRetirementSampleRow rows[3];
    TpRetirementSampleGroup groups[2];
} BqTestAaSpool;

BUSTER_GLOBAL_LOCAL bool bq_test_aa_spool_fill(BqTestAaSpool* spool, FILE* file, unsigned memory_mode)
{
    u64 per_unit = (u64)TP_RETIREMENT_ROUNDS * 60u;
    *spool = (BqTestAaSpool){0};
    for (u32 row = 0; row < 3; row += 1) spool->rows[row].id = row;
    spool->rows[0].metrics = TP_RETIREMENT_SAMPLE_RUNTIME;
    spool->groups[0].object = TP_RETIREMENT_NONE;
    spool->groups[1].object = 0;
    spool->samples = (TpRetirementSamples){.spool = file, .rows = spool->rows, .groups = spool->groups,
        .expected = 4u * per_unit, .row_records = 3u * per_unit, .row_count = 3, .group_count = 2,
        .object_count = 1, .finished = 1};
    bool ok = file != NULL;
    for (u64 ordinal = 0; ok && ordinal < 4u * per_unit; ordinal += 1)
    {
        u64 unit = ordinal / per_unit, pair = ordinal % 60u;
        u64 wall = UINT64_C(1000000000), memory = UINT64_C(4096000);
        u64 values[TP_RETIREMENT_SAMPLE_VALUES] = {wall, wall - 1000000u + (ordinal * 7919u) % 2000001u, memory,
                                                   memory, 0, 0, 0};
        if (unit == 1 && memory_mode == 1) values[3] = memory / 100u * 105u;
        if (unit == 3 && memory_mode == 2 && (pair / 2u) % 2u) values[3] = memory / 100u * 104u;
        if (unit == 0)
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
 * digest): every row record, then the batch record of group 1 (object 0),
 * through lane D's encoders. */
BUSTER_GLOBAL_LOCAL bool bq_test_aa_spool_digest(BqTestAaSpool* spool, char digest[SHA256_HEX_CAPACITY])
{
    u64 per_unit = (u64)TP_RETIREMENT_ROUNDS * 60u;
    Sha256 hash;
    sha256_init(&hash);
    bool ok = true;
    for (u64 ordinal = 0; ok && ordinal < 4u * per_unit; ordinal += 1)
    {
        uint64_t values[TP_RETIREMENT_SAMPLE_VALUES];
        char line[TP_RETIREMENT_SAMPLE_LINE_CAP];
        u32 unit = (u32)(ordinal / per_unit), round = (u32)(ordinal / 60u % 2u), pair = (u32)(ordinal % 60u);
        ok = tp_retirement_sample_read(&spool->samples, ordinal, values);
        size_t length = !ok ? 0 : unit == 3 ? tp_retirement_batch_record(line, sizeof(line), 1, round, pair, values) :
            tp_retirement_sample_record(line, sizeof(line), unit, round, pair, spool->rows[unit].metrics, values);
        ok = ok && length;
        if (ok) sha256_add(&hash, line, (u64)length);
    }
    if (ok) sha256_finish_hex(&hash, (char8*)digest);
    return ok;
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
    BqRetirementAaPolicy policy = {.lower = "0.98", .upper = "1.02", .lower_bound = 0.98, .upper_bound = 1.02,
                                   .pairs_per_round = 60};
    for (unsigned mode = 0; mode < 3; mode += 1)
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
        BQ_CHECK(bq_test_aa_spool_fill(&spool, file, mode) && bq_test_aa_spool_digest(&spool, attested) &&
                 bq_retirement_aa_ratios(&spool.samples, &family, 60, attested, arena, ratios));
        TpRetirementPlan plan = {4260881u, TP_RETIREMENT_STATISTICS_VERSION, family.bootstrap, family.cells_total, 60,
                                 TP_RETIREMENT_MIN_RESAMPLES, 1};
        bool admitted = built && ratios[0] && bq_retirement_aa_band_check(&plan, &family, ratios, &policy, arena, &band);
        if (mode == 0)
        {
            BQ_CHECK(admitted && band.assessed == family.count && band.refused == TP_RETIREMENT_COMPOSE_NONE);
            /* The same rows under a plan of another P refuse. */
            TpRetirementPlan other = plan;
            other.pairs_per_round = 62;
            BQ_CHECK(!bq_retirement_aa_band_check(&other, &family, ratios, &policy, arena, &band));
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
            /* A spool that is not finished, of another row or another runtime
             * flag refuses. */
            spool.samples.finished = 0;
            BQ_CHECK(!bq_retirement_aa_ratios(&spool.samples, &family, 60, attested, arena, unused));
            spool.samples.finished = 1;
            spool.rows[0].metrics = 0;
            BQ_CHECK(!bq_retirement_aa_ratios(&spool.samples, &family, 60, attested, arena, unused));
            spool.rows[0].metrics = TP_RETIREMENT_SAMPLE_RUNTIME;
            spool.rows[2].id = 7;
            BQ_CHECK(!bq_retirement_aa_ratios(&spool.samples, &family, 60, attested, arena, unused));
        }
        else
        {
            /* The refused member: peak memory's row-1 cell, outside the
             * band (mode 1), or the batch RSS aggregate, the first member in
             * canonical order, crossing its upper bound (mode 2). */
            TpRetirementComposeMember const* member = built && band.refused < family.count ?
                family.members + family.order[band.refused] : NULL;
            BQ_CHECK(!admitted && member && band.assessed == band.refused + 1u && band.bounds[2].upper > 1.02 &&
                     (mode == 1 ? !strcmp(member->name, "compiler_peak_memory/cell/row=1") &&
                                  band.bounds[2].lower > 1.02 :
                                  !strcmp(member->name, "compiler_batch_peak_rss/aggregate") && !band.refused &&
                                  band.bounds[2].lower < 1.02));
        }
        if (file) fclose(file);
        if (arena) arena_destroy(arena, 1);
    }
    /* Unbounded and crossing bounds are outside; a bounded interval inside
     * the band is inside, its endpoints included. */
    TpRetirementBound unbounded = {1.0, 0.0, INFINITY}, crossing = {1.0, 0.97, 1.0}, inside = {1.0, 0.98, 1.02};
    BQ_CHECK(!bq_retirement_aa_inside(&unbounded, &policy) && !bq_retirement_aa_inside(&crossing, &policy) &&
             bq_retirement_aa_inside(&inside, &policy));
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
    BqRetirementAaPolicy policy = {.sha256 = "7777777777777777777777777777777777777777777777777777777777777777",
                                   .lower = "0.98", .upper = "1.02", .lower_bound = 0.98, .upper_bound = 1.02,
                                   .pairs_per_round = 60};
    BqRetirementAaIdentities identities = {0};
    char receipt[BQ_RETIREMENT_UNIT_CAMPAIGN_AA_RECEIPT_BYTES_MAX], digest[SHA256_HEX_CAPACITY] = {0};
    char expected_digest[SHA256_HEX_CAPACITY] = {0};
    u32 length = 0;
    char const* expected = "{\"aa_decision\":\"admitted\",\"aa_policy_sha256\":"
        "\"7777777777777777777777777777777777777777777777777777777777777777\",\"admitted\":true,"
        "\"baseline_source_commit\":\"3333333333333333333333333333333333333333\","
        "\"baseline_source_tree\":\"4444444444444444444444444444444444444444\","
        "\"equivalence_band\":{\"lower\":\"0.98\",\"upper\":\"1.02\"},\"family_sha256\":\"" BQ_TEST_AA_FAMILY "\","
        "\"lease_protocol\":\"server-authoritative-supervisor-lease-v1\",\"logical_cpu\":3,"
        "\"machine_id\":\"zen5-9700x-01\",\"native_only\":true,\"native_target\":\"x86_64-unknown-linux-gnu\","
        "\"phase_receipt_sha256\":\"" BQ_TEST_AA_PHASE "\","
        "\"profile_id\":\"zen5-9700x-native\",\"profile_version\":\"profile-v1\","
        "\"schema\":\"buster-native-retirement-aa-admission-v2\",\"service_id\":\"retirement-9700x\",\"version\":2}";
    bq_digest(expected, (u32)strlen(expected), (char8*)expected_digest);
    BQ_CHECK(parsed && bq_retirement_aa_identities(&context, BQ_TEST_AA_FAMILY, &identities) &&
             bq_retirement_aa_receipt_render(&policy, &identities, 3, BQ_TEST_AA_FAMILY, BQ_TEST_AA_PHASE, receipt,
                                             &length, digest) &&
             length == strlen(expected) && !memcmp(receipt, expected, length) && !strcmp(digest, expected_digest));
    /* (#1021) No phase receipt digest, no receipt. */
    BQ_CHECK(!bq_retirement_aa_receipt_render(&policy, &identities, 3, BQ_TEST_AA_FAMILY, NULL, receipt, &length,
                                              digest) && !length && !digest[0] &&
             !bq_retirement_aa_receipt_render(&policy, &identities, 3, BQ_TEST_AA_FAMILY, "short", receipt, &length,
                                              digest) && !length);
    /* A context binding another family, or an identity that would need
     * escaping, renders nothing. */
    BQ_CHECK(!bq_retirement_aa_identities(&context, "8888888888888888888888888888888888888888888888888888888888888888",
                                          &identities) && !identities.machine_id[0]);
    char const* quoted = "{\"host\":{\"machine_id\":\"zen5\\\"01\"},\"profile\":{\"id\":\"p\",\"version\":\"v\"},"
                         "\"service\":{\"id\":\"s\"}}";
    BQ_CHECK(tp_retirement_compose_json_parse((unsigned char const*)quoted, strlen(quoted), arena,
                                              &context.json[BQ_RETIREMENT_WORKER_BINDING_EXECUTION]) &&
             !bq_retirement_aa_identities(&context, BQ_TEST_AA_FAMILY, &identities));
    /* No policy digest, no receipt. */
    BqRetirementAaPolicy unpinned = policy;
    unpinned.sha256[0] = 0;
    BQ_CHECK(!bq_retirement_aa_receipt_render(&unpinned, &identities, 3, BQ_TEST_AA_FAMILY, BQ_TEST_AA_PHASE, receipt,
                                              &length, digest) && !length && !digest[0]);
}

BUSTER_GLOBAL_LOCAL void bq_test_retirement_aa_admission(void)
{
    Arena* arena = arena_create((ArenaCreation){.reserved_size = BQ_TEST_AA_ARENA_BYTES, .flags = {.no_pool = 1}});
    BQ_CHECK(arena != NULL);
    if (arena)
    {
        bq_test_aa_policy(arena);
        bq_test_aa_receipt(arena);
        arena_destroy(arena, 1);
    }
    bq_test_aa_band();
}
#endif
