/* Production A/A admission of the retirement producer (#426 plan step 6,
 * #1021, #881).
 *
 * Ownership: the producer's decision between lane D's A/A stage and the
 * driver's admission. retirement_worker_campaign.c calls
 * bq_retirement_aa_admission_decide (declared there) outside the fixture
 * build. This file is compiled only into the service translation unit
 * (main.c), after retirement_worker_compose.c (the pinned binding context and
 * its JSON queries) and before retirement_worker_unit.c.
 *
 * The #426 decision is the A/A policy document tools/zen5_aa_evaluator.py
 * writes (`buster-zen5-aa-policy-v1`, canonical JSON with one trailing line
 * feed), installed read-only as recipes/BQ_RETIREMENT_AA_POLICY_NAME. The
 * recipe profile's one `aa-policy-sha256=` line is the only source of the
 * expected digest. Each of these refuses with BQ_RECIPE_MISMATCH: a missing
 * or repeated pin line, bytes of another digest, a status other than
 * `eligible`, `ab_authorized` other than false, or a `current_job_aa` other
 * than P = 60 pairs per round, runtime rows `U=R` and an equivalence band of
 * decimals with 0 < lower < 1 < upper. An unreadable or unowned file refuses
 * with BQ_CONFIGURATION_MISMATCH.
 *
 * The rows are the A/A stage's finished spool, and only rows whose canonical
 * lines (lane D's encoders, in export order) hash to the AA_MEASURED digest
 * the coordinator attested against the published A/A sample shards and
 * acknowledged (#1021, bq_retirement_unit_campaign_aa_attest) are used.
 * The band applies to this job's A/A rows as #1188 specifies: every member of
 * the derived #619 family (tp_retirement_compose_family, the composer's own),
 * in each round and pooled, through tp_retirement_assess with the frozen
 * campaign plan (aggregates and slices bootstrapped with the pinned seed and
 * resamples, cells exact). Both bounds must lie in [lower, upper]. A crossing
 * interval is inconclusive and an interval no order statistic bounds
 * ([0, inf)) is unbounded; either refuses. The band's decimals are read as
 * their nearest doubles (strtod in the service's C locale). Only an
 * admission renders the v2 receipt (BQ_RETIREMENT_UNIT_CAMPAIGN_AA_SCHEMA):
 * the v1 identities from the pinned binding context, the policy digest, the
 * band strings as the policy records them, the decision and the digest of the
 * coordinator's published AA_MEASURED receipt (phase_receipt_sha256).
 *
 * The fixture build (BQ_RETIREMENT_UNIT_CAMPAIGN_FIXTURE_AA) compiles none of
 * this; it presents bq_retirement_worker_campaign_fixture_receipt instead.
 *
 * Authority: the policy decision is the unit's, over rows the service
 * attested. An admission enters A/B only through the campaign's production
 * transition over that attested digest (bq_retirement_unit_campaign_admit,
 * tp_retirement_campaign_admit_aa), and the coordinator's finalization
 * requires the admission receipt to name its own AA_MEASURED receipt
 * (bq_retirement_coordinator_finalize).
 *
 * Map: BqRetirementAaPolicy, bq_retirement_aa_decimal,
 * bq_retirement_aa_policy_check (pin, digest and fields over given bytes),
 * bq_retirement_aa_policy_load (the installed file), bq_retirement_aa_ratio
 * and bq_retirement_aa_ratios (the A/A spool as the family's per-metric
 * cell-major ratios), BqRetirementAaBand, bq_retirement_aa_inside and
 * bq_retirement_aa_band_check, BqRetirementAaIdentities,
 * bq_retirement_aa_identity and bq_retirement_aa_identities,
 * bq_retirement_aa_receipt_render, bq_retirement_aa_admission_decide.
 */

/* The functional fixture build admits through its stand-in receipt instead
 * (BQ_RETIREMENT_UNIT_CAMPAIGN_FIXTURE_AA), so it does not compile this. */
#if !defined(BQ_RETIREMENT_UNIT_CAMPAIGN_FIXTURE_AA)
/* The installed #426 policy and the profile key that pins it. */
#define BQ_RETIREMENT_AA_POLICY_NAME "native-retirement-performance-v1.aa-policy"
#define BQ_RETIREMENT_AA_POLICY_PIN "aa-policy-sha256="
/* zen5_aa_evaluator.py's POLICY_SCHEMA, POLICY_ID, the eligible status and
 * the current-job values decided on #36 (PAIRS_PER_ROUND, RUNTIME_ROWS). */
#define BQ_RETIREMENT_AA_POLICY_SCHEMA "buster-zen5-aa-policy-v1"
#define BQ_RETIREMENT_AA_POLICY_ID "buster-zen5-aa-eligibility-v1"
#define BQ_RETIREMENT_AA_POLICY_ELIGIBLE "eligible"
#define BQ_RETIREMENT_AA_POLICY_PAIRS 60u
#define BQ_RETIREMENT_AA_POLICY_RUNTIME_ROWS "U=R"
/* A storage bound on the policy document (it embeds the protocol, every
 * attempt and the family statistics), not a measurement value. */
#define BQ_RETIREMENT_AA_POLICY_BYTES_CAP (UINT32_C(16) << 20)
/* The evaluator's DECIMAL_RE: at most nine integer and twelve fraction
 * digits, plus the point and a NUL. */
#define BQ_RETIREMENT_AA_DECIMAL_INTEGER_DIGITS 9u
#define BQ_RETIREMENT_AA_DECIMAL_FRACTION_DIGITS 12u
#define BQ_RETIREMENT_AA_DECIMAL_BYTES 24u
/* A receipt identity string (machine, profile, service): printable ASCII
 * without quote or backslash, so it renders without escapes. */
#define BQ_RETIREMENT_AA_IDENTITY_BYTES 128u
#define BQ_RETIREMENT_AA_COMMIT_HEX 40u
/* The #619 scopes the band applies to: both rounds and the pooled analysis. */
#define BQ_RETIREMENT_AA_SCOPES (TP_RETIREMENT_ROUNDS + 1u)
#define BQ_RETIREMENT_AA_LEASE_PROTOCOL "server-authoritative-supervisor-lease-v1"
#define BQ_RETIREMENT_AA_NATIVE_TARGET "x86_64-unknown-linux-gnu"

typedef struct BqRetirementAaPolicy
{
    char sha256[SHA256_HEX_CAPACITY];
    char lower[BQ_RETIREMENT_AA_DECIMAL_BYTES], upper[BQ_RETIREMENT_AA_DECIMAL_BYTES];
    double lower_bound, upper_bound;
    unsigned pairs_per_round;
} BqRetirementAaPolicy;

/* A band decimal as the evaluator admits it (DECIMAL_RE): `0` or one to nine
 * digits without a leading zero, then optionally a point and one to twelve
 * digits; copied with its NUL and read as its nearest double. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_aa_decimal(TpComposeJson const* json, unsigned node,
    char output[BQ_RETIREMENT_AA_DECIMAL_BYTES], double* value)
{
    TpComposeJsonNode const* item = node < json->count ? json->nodes + node : NULL;
    u32 length = item && item->kind == TP_COMPOSE_JSON_STRING ? item->length : 0;
    char const* text = item ? item->text : NULL;
    u32 integer = 0, fraction = 0;
    while (integer < length && text[integer] >= '0' && text[integer] <= '9') integer += 1;
    bool point = integer < length && text[integer] == '.';
    while (point && integer + 1u + fraction < length && text[integer + 1u + fraction] >= '0' &&
           text[integer + 1u + fraction] <= '9')
        fraction += 1;
    bool ok = length && length < BQ_RETIREMENT_AA_DECIMAL_BYTES && integer &&
              integer <= BQ_RETIREMENT_AA_DECIMAL_INTEGER_DIGITS && (integer == 1 || text[0] != '0') &&
              (point ? fraction && fraction <= BQ_RETIREMENT_AA_DECIMAL_FRACTION_DIGITS &&
                       integer + 1u + fraction == length : integer == length);
    if (ok)
    {
        memcpy(output, text, length);
        output[length] = 0;
        *value = strtod(output, NULL);
        ok = isfinite(*value);
    }
    return ok;
}

/* The policy `bytes` against the profile's pin and the admission fields
 * (see the header): BQ_OK with `policy` filled, or BQ_RECIPE_MISMATCH. */
BUSTER_GLOBAL_LOCAL BqError bq_retirement_aa_policy_check(String8 profile, u8 const* bytes, u32 length, Arena* arena,
    BqRetirementAaPolicy* policy)
{
    static char const* const schema[] = {"schema"};
    static char const* const version[] = {"version"};
    static char const* const policy_id[] = {"policy_id"};
    static char const* const status[] = {"status"};
    static char const* const candidate[] = {"candidate_decision"};
    static char const* const authorized[] = {"ab_authorized"};
    static char const* const job_path[] = {"current_job_aa"};
    static char const* const job_keys[] = {"equivalence_band", "pairs_per_round", "runtime_rows"};
    static char const* const pairs[] = {"pairs_per_round"};
    static char const* const runtime[] = {"runtime_rows"};
    static char const* const band_path[] = {"equivalence_band"};
    static char const* const band_keys[] = {"lower", "upper"};
    char pin[SHA256_HEX_CAPACITY] = {0}, digest[SHA256_HEX_CAPACITY] = {0};
    TpComposeJson json = {0};
    if (policy) *policy = (BqRetirementAaPolicy){0};
    BqError result = policy && arena && bq_retirement_profile_sha(profile, S8(BQ_RETIREMENT_AA_POLICY_PIN), pin) &&
                     bytes && length > 1 && length <= BQ_RETIREMENT_AA_POLICY_BYTES_CAP ? BQ_OK : BQ_RECIPE_MISMATCH;
    if (result == BQ_OK)
    {
        bq_digest(bytes, length, (char8*)digest);
        if (memcmp(digest, pin, SHA256_HEX_CAPACITY)) result = BQ_RECIPE_MISMATCH;
    }
    /* The evaluator's canonical bytes end with exactly one line feed. */
    if (result == BQ_OK &&
        !(bytes[length - 1] == '\n' && tp_retirement_compose_json_parse(bytes, length - 1u, arena, &json) &&
          json.count && json.nodes[0].kind == TP_COMPOSE_JSON_OBJECT))
        result = BQ_RECIPE_MISMATCH;
    unsigned authorized_node = result == BQ_OK ? bq_retirement_worker_json_node(&json, 0, authorized, 1) :
                               TP_COMPOSE_JSON_NONE;
    unsigned job = result == BQ_OK ? bq_retirement_worker_json_node(&json, 0, job_path, 1) : TP_COMPOSE_JSON_NONE;
    unsigned band = job != TP_COMPOSE_JSON_NONE ? bq_retirement_worker_json_node(&json, job, band_path, 1) :
                    TP_COMPOSE_JSON_NONE;
    if (result == BQ_OK &&
        !(bq_retirement_worker_json_text(&json, 0, schema, 1, BQ_RETIREMENT_AA_POLICY_SCHEMA) &&
          bq_retirement_worker_json_integer(&json, 0, version, 1, 1) &&
          bq_retirement_worker_json_text(&json, 0, policy_id, 1, BQ_RETIREMENT_AA_POLICY_ID) &&
          bq_retirement_worker_json_text(&json, 0, status, 1, BQ_RETIREMENT_AA_POLICY_ELIGIBLE) &&
          bq_retirement_worker_json_text(&json, 0, candidate, 1, "not-evaluated") &&
          authorized_node != TP_COMPOSE_JSON_NONE && json.nodes[authorized_node].kind == TP_COMPOSE_JSON_FALSE &&
          tp_retirement_compose_json_keys(&json, job, job_keys, BUSTER_ARRAY_LENGTH(job_keys)) &&
          bq_retirement_worker_json_integer(&json, job, pairs, 1, BQ_RETIREMENT_AA_POLICY_PAIRS) &&
          bq_retirement_worker_json_text(&json, job, runtime, 1, BQ_RETIREMENT_AA_POLICY_RUNTIME_ROWS) &&
          tp_retirement_compose_json_keys(&json, band, band_keys, BUSTER_ARRAY_LENGTH(band_keys)) &&
          bq_retirement_aa_decimal(&json, tp_retirement_compose_json_member(&json, band, "lower"), policy->lower,
                                   &policy->lower_bound) &&
          bq_retirement_aa_decimal(&json, tp_retirement_compose_json_member(&json, band, "upper"), policy->upper,
                                   &policy->upper_bound) &&
          policy->lower_bound > 0.0 && policy->lower_bound < 1.0 && policy->upper_bound > 1.0))
        result = BQ_RECIPE_MISMATCH;
    if (result == BQ_OK)
    {
        memcpy(policy->sha256, digest, SHA256_HEX_CAPACITY);
        policy->pairs_per_round = BQ_RETIREMENT_AA_POLICY_PAIRS;
    }
    else if (policy) *policy = (BqRetirementAaPolicy){0};
    return result;
}

/* The installed policy, read-only under <installed>/recipes/, checked
 * against the profile (bq_retirement_aa_policy_check). The pin is required
 * before the file is opened. */
BUSTER_GLOBAL_LOCAL BqError bq_retirement_aa_policy_load(int installed, String8 profile, Arena* arena,
    BqRetirementAaPolicy* policy)
{
    char pin[SHA256_HEX_CAPACITY] = {0}, digest[SHA256_HEX_CAPACITY] = {0};
    BqError result = installed >= 0 && bq_retirement_profile_sha(profile, S8(BQ_RETIREMENT_AA_POLICY_PIN), pin) ?
                     BQ_OK : BQ_RECIPE_MISMATCH;
    int recipes = result == BQ_OK ? openat(installed, "recipes", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    u8* bytes = NULL;
    u32 length = 0;
    if (result == BQ_OK)
        result = recipes >= 0 && bq_owned_directory(recipes, false, true) &&
                 bq_retirement_reference_read_installed(recipes, BQ_RETIREMENT_AA_POLICY_NAME,
                     BQ_RETIREMENT_AA_POLICY_BYTES_CAP, &bytes, &length, digest, NULL) ?
                 BQ_OK : BQ_CONFIGURATION_MISMATCH;
    if (result == BQ_OK) result = bq_retirement_aa_policy_check(profile, bytes, length, arena, policy);
    free(bytes);
    if (recipes >= 0 && close(recipes) != 0 && result == BQ_OK) result = BQ_CONFIGURATION_MISMATCH;
    return result;
}

/* One paired ratio, candidate slot over baseline slot: both observed and the
 * quotient finite and positive. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_aa_ratio(u64 candidate, u64 baseline, double* output)
{
    double ratio = candidate && baseline ? (double)candidate / (double)baseline : 0.0;
    bool ok = isfinite(ratio) && ratio > 0.0;
    *output = ok ? ratio : 0.0;
    return ok;
}

/* The A/A stage's finished spool as the family's ratios: per metric,
 * cell-major [cell][round][pair] (tp_retirement_ratio_index's order). Lane
 * D's row record holds the row's wall, peak-memory and runtime pairs and an
 * object group's batch record (after every row record) its process wall and
 * RSS pairs; the spool's rows, their runtime flags and its object groups must
 * be the family's, in order. Every record is re-encoded with lane D's own
 * canonical encoders (tp_retirement_sample_record, tp_retirement_batch_record)
 * in export order, and the lines must hash to `attested`, the AA_MEASURED
 * digest the coordinator attested against the published A/A shards (#1021):
 * the ratios come only from values whose canonical lines are those rows. A
 * zero or non-finite ratio refuses. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_aa_ratios(TpRetirementSamples* samples, TpRetirementComposeFamily const* family,
    unsigned pairs, char const attested[SHA256_HEX_CAPACITY], Arena* arena,
    double* ratios[TP_RETIREMENT_COMPOSE_METRICS])
{
    u64 per_unit = (u64)TP_RETIREMENT_ROUNDS * pairs;
    TpRetirementComposeLayout const* layout = family ? family->layout : NULL;
    bool ok = samples && layout && arena && ratios && pairs && attested && tp_retirement_digest(attested) &&
              !samples->failed && samples->finished && samples->rows && samples->groups &&
              samples->row_count == layout->row_count && samples->group_count == layout->group_count &&
              samples->object_count == family->object_count &&
              samples->row_records == (u64)samples->row_count * per_unit &&
              samples->expected == samples->row_records + (u64)samples->object_count * per_unit;
    for (u32 row = 0; ok && row < layout->row_count; row += 1)
        ok = samples->rows[row].id == layout->rows[row].id &&
             ((samples->rows[row].metrics & TP_RETIREMENT_SAMPLE_RUNTIME) != 0) == (layout->rows[row].runtime != 0);
    for (u32 group = 0; ok && group < layout->group_count; group += 1)
        ok = samples->groups[group].object == (family->group_object[group] == TP_RETIREMENT_COMPOSE_NONE ?
                                               TP_RETIREMENT_NONE : family->group_object[group]);
    for (u32 metric = 0; metric < TP_RETIREMENT_COMPOSE_METRICS; metric += 1)
    {
        if (ratios) ratios[metric] = ok ? bq_retirement_worker_allocate(arena, (u64)family->cells[metric] * per_unit,
                                                                        sizeof(double)) : NULL;
        ok = ok && ratios[metric];
    }
    Sha256 hash;
    sha256_init(&hash);
    u32 units = ok ? samples->row_count + samples->object_count : 0;
    for (u32 unit = 0; ok && unit < units; unit += 1)
    {
        bool batch = unit >= samples->row_count;
        u32 cell = batch ? unit - samples->row_count : unit;
        u32 runtime = batch ? TP_RETIREMENT_COMPOSE_NONE : family->runtime_index[cell];
        u64 base = batch ? samples->row_records + (u64)cell * per_unit : (u64)cell * per_unit;
        for (u64 index = 0; ok && index < per_unit; index += 1)
        {
            uint64_t values[TP_RETIREMENT_SAMPLE_VALUES] = {0};
            char line[TP_RETIREMENT_SAMPLE_LINE_CAP];
            unsigned round = (unsigned)(index / pairs), pair = (unsigned)(index % pairs);
            u64 slot = (u64)cell * per_unit + index;
            ok = tp_retirement_sample_read(samples, base + index, values);
            size_t length = !ok ? 0 : batch ?
                tp_retirement_batch_record(line, sizeof(line), family->object_groups[cell], round, pair, values) :
                tp_retirement_sample_record(line, sizeof(line), samples->rows[cell].id, round, pair,
                                            samples->rows[cell].metrics, values);
            ok = ok && length;
            if (ok) sha256_add(&hash, line, (u64)length);
            ok = ok &&
                 bq_retirement_aa_ratio(values[1], values[0], &ratios[batch ? TP_RETIREMENT_BATCH_WALL_TIME :
                                                                          TP_RETIREMENT_WALL_TIME][slot]) &&
                 bq_retirement_aa_ratio(values[3], values[2], &ratios[batch ? TP_RETIREMENT_BATCH_PEAK_RSS :
                                                                          TP_RETIREMENT_PEAK_MEMORY][slot]);
            if (ok && runtime != TP_RETIREMENT_COMPOSE_NONE)
                ok = bq_retirement_aa_ratio(values[5], values[4],
                                            &ratios[TP_RETIREMENT_GENERATED_RUNTIME][(u64)runtime * per_unit + index]);
        }
    }
    char computed[SHA256_HEX_CAPACITY] = {0};
    if (ok)
    {
        sha256_finish_hex(&hash, (char8*)computed);
        ok = !strcmp(computed, attested);
    }
    return ok;
}

/* The band verdict: members assessed and, on a refusal, the canonical
 * ordinal of the first member outside the band with its round and pooled
 * bounds (TP_RETIREMENT_COMPOSE_NONE when every member is inside). */
typedef struct BqRetirementAaBand
{
    unsigned assessed, refused;
    TpRetirementBound bounds[BQ_RETIREMENT_AA_SCOPES];
} BqRetirementAaBand;

/* Both bounds bounded and inside [lower, upper]. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_aa_inside(TpRetirementBound const* bound, BqRetirementAaPolicy const* policy)
{
    bool inside = isfinite(bound->lower) && isfinite(bound->upper) && bound->lower > 0.0 &&
                  bound->lower >= policy->lower_bound && bound->upper <= policy->upper_bound;
    return inside;
}

/* Every family member, in canonical order, assessed once by
 * tp_retirement_assess over the frozen plan: true only when each is valid and
 * its two round bounds and pooled bound are inside the band. The plan must be
 * the family's (member counts) and the policy's (pairs per round). Assessment
 * stops at the first member outside the band. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_aa_band_check(TpRetirementPlan const* plan,
    TpRetirementComposeFamily const* family, double* const ratios[TP_RETIREMENT_COMPOSE_METRICS],
    BqRetirementAaPolicy const* policy, Arena* arena, BqRetirementAaBand* band)
{
    u64 per_unit = plan ? (u64)TP_RETIREMENT_ROUNDS * plan->pairs_per_round : 0;
    unsigned widest = 0;
    for (u32 metric = 0; family && metric < TP_RETIREMENT_COMPOSE_METRICS; metric += 1)
        if (family->cells[metric] > widest) widest = family->cells[metric];
    bool ok = plan && family && ratios && policy && arena && band && family->count && widest &&
              plan->pairs_per_round == policy->pairs_per_round &&
              plan->bootstrap_members_per_scope == family->bootstrap &&
              plan->cell_members_per_scope == family->cells_total;
    for (u32 metric = 0; ok && metric < TP_RETIREMENT_COMPOSE_METRICS; metric += 1) ok = ratios[metric] != NULL;
    if (band) *band = (BqRetirementAaBand){.refused = TP_RETIREMENT_COMPOSE_NONE};
    double* workspace = ok ? bq_retirement_worker_allocate(arena, plan->resamples, sizeof(double)) : NULL;
    double* gathered = ok ? bq_retirement_worker_allocate(arena, (u64)widest * per_unit, sizeof(double)) : NULL;
    ok = ok && workspace && gathered;
    bool admitted = ok;
    for (u32 index = 0; ok && admitted && index < family->count; index += 1)
    {
        TpRetirementComposeMember const* member = family->members + family->order[index];
        unsigned metric = member->metric, cells = family->cells[metric];
        /* An aggregate's and a cell's ratios are contiguous; a slice's
         * selected cells are gathered in cell order. */
        double const* series_ratios = member->kind ? ratios[metric] + (u64)member->unit * per_unit :
                                      member->dimension == TP_RETIREMENT_COMPOSE_NONE ? ratios[metric] : gathered;
        unsigned selected = 0;
        for (u32 cell = 0; cell < cells; cell += 1)
        {
            if (tp_retirement_compose_member_selects(family, member, cell))
            {
                if (series_ratios == gathered)
                    memcpy(gathered + (u64)selected * per_unit, ratios[metric] + (u64)cell * per_unit,
                           (size_t)per_unit * sizeof(double));
                selected += 1;
            }
        }
        ok = selected == member->cells && selected && (member->kind ? member->unit < cells : 1);
        TpRetirementSeries series = {series_ratios, (size_t)selected * per_unit, selected,
            {plan->pairs_per_round, plan->pairs_per_round}, member->kind, member->family, metric,
            policy->upper_bound};
        TpRetirementResult assessed = ok ? tp_retirement_assess(plan, &series, member->kind ? NULL : workspace,
                                                                member->kind ? 0 : plan->resamples) :
                                      (TpRetirementResult){0};
        ok = ok && assessed.valid;
        TpRetirementBound const scopes[BQ_RETIREMENT_AA_SCOPES] = {assessed.round[0], assessed.round[1],
                                                                   assessed.pooled};
        bool inside = ok;
        for (u32 scope = 0; inside && scope < BQ_RETIREMENT_AA_SCOPES; scope += 1)
            inside = bq_retirement_aa_inside(&scopes[scope], policy);
        if (ok) band->assessed += 1;
        if (ok && !inside)
        {
            admitted = false;
            band->refused = index;
            memcpy(band->bounds, scopes, sizeof(scopes));
        }
    }
    return ok && admitted;
}

/* The receipt's identities, which the #511 validator requires to equal the
 * binding's: the host's machine, the host profile's id and version, the
 * service's id and the baseline subject's source commit and tree. */
typedef struct BqRetirementAaIdentities
{
    char machine_id[BQ_RETIREMENT_AA_IDENTITY_BYTES];
    char profile_id[BQ_RETIREMENT_AA_IDENTITY_BYTES];
    char profile_version[BQ_RETIREMENT_AA_IDENTITY_BYTES];
    char service_id[BQ_RETIREMENT_AA_IDENTITY_BYTES];
    char baseline_commit[BQ_RETIREMENT_AA_COMMIT_HEX + 1u];
    char baseline_tree[BQ_RETIREMENT_AA_COMMIT_HEX + 1u];
} BqRetirementAaIdentities;

/* The string at `path`: `hex` lowercase hex digits exactly, or (hex 0)
 * printable ASCII without quote or backslash that fits `capacity`. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_aa_identity(TpComposeJson const* json, char const* const* path, u32 depth,
    u32 hex, char* output, u32 capacity)
{
    unsigned node = bq_retirement_worker_json_node(json, 0, path, depth);
    TpComposeJsonNode const* item = node != TP_COMPOSE_JSON_NONE ? json->nodes + node : NULL;
    u32 length = item && item->kind == TP_COMPOSE_JSON_STRING ? item->length : 0;
    bool ok = length && length < capacity && (!hex || length == hex);
    for (u32 index = 0; ok && index < length; index += 1)
    {
        u8 c = (u8)item->text[index];
        ok = hex ? (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') : c >= 0x20 && c < 0x7f && c != '"' && c != '\\';
    }
    if (ok)
    {
        memcpy(output, item->text, length);
        output[length] = 0;
    }
    else if (output && capacity) output[0] = 0;
    return ok;
}

/* The pinned binding context's identities, and its statistical family, which
 * must be the one lane D derived. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_aa_identities(BqRetirementWorkerBindingContext const* context,
    char const family_sha256[SHA256_HEX_CAPACITY], BqRetirementAaIdentities* identities)
{
    static char const* const machine[] = {"host", "machine_id"};
    static char const* const profile_id[] = {"profile", "id"};
    static char const* const profile_version[] = {"profile", "version"};
    static char const* const service[] = {"service", "id"};
    static char const* const commit[] = {"baseline", "source_commit"};
    static char const* const tree[] = {"baseline", "source_tree"};
    static char const* const family[] = {"statistical_family", "sha256"};
    TpComposeJson const* execution = context ? &context->json[BQ_RETIREMENT_WORKER_BINDING_EXECUTION] : NULL;
    TpComposeJson const* subjects = context ? &context->json[BQ_RETIREMENT_WORKER_BINDING_SUBJECTS] : NULL;
    TpComposeJson const* population = context ? &context->json[BQ_RETIREMENT_WORKER_BINDING_POPULATION] : NULL;
    bool ok = context && family_sha256 && identities && execution->count && subjects->count && population->count;
    if (identities) *identities = (BqRetirementAaIdentities){0};
    ok = ok && bq_retirement_aa_identity(execution, machine, 2, 0, identities->machine_id, BQ_RETIREMENT_AA_IDENTITY_BYTES) &&
         bq_retirement_aa_identity(execution, profile_id, 2, 0, identities->profile_id, BQ_RETIREMENT_AA_IDENTITY_BYTES) &&
         bq_retirement_aa_identity(execution, profile_version, 2, 0, identities->profile_version,
                                   BQ_RETIREMENT_AA_IDENTITY_BYTES) &&
         bq_retirement_aa_identity(execution, service, 2, 0, identities->service_id, BQ_RETIREMENT_AA_IDENTITY_BYTES) &&
         bq_retirement_aa_identity(subjects, commit, 2, BQ_RETIREMENT_AA_COMMIT_HEX, identities->baseline_commit,
                                   BQ_RETIREMENT_AA_COMMIT_HEX + 1u) &&
         bq_retirement_aa_identity(subjects, tree, 2, BQ_RETIREMENT_AA_COMMIT_HEX, identities->baseline_tree,
                                   BQ_RETIREMENT_AA_COMMIT_HEX + 1u) &&
         bq_retirement_worker_json_text(population, 0, family, 2, family_sha256);
    if (!ok && identities) *identities = (BqRetirementAaIdentities){0};
    return ok;
}

/* The v2 receipt, in canonical key order (json.dumps sort_keys, compact),
 * and its digest. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_aa_receipt_render(BqRetirementAaPolicy const* policy,
    BqRetirementAaIdentities const* identities, int logical_cpu, char const family_sha256[SHA256_HEX_CAPACITY],
    char const phase_receipt_sha256[SHA256_HEX_CAPACITY], char receipt[BQ_RETIREMENT_UNIT_CAMPAIGN_AA_RECEIPT_BYTES_MAX],
    u32* length, char digest[SHA256_HEX_CAPACITY])
{
    int written = policy && identities && family_sha256 && logical_cpu >= 0 && policy->sha256[0] &&
                  phase_receipt_sha256 && tp_retirement_digest(phase_receipt_sha256) ?
        snprintf(receipt, BQ_RETIREMENT_UNIT_CAMPAIGN_AA_RECEIPT_BYTES_MAX,
            "{\"aa_decision\":\"" BQ_RETIREMENT_UNIT_CAMPAIGN_AA_DECISION "\",\"aa_policy_sha256\":\"%s\","
            "\"admitted\":true,\"baseline_source_commit\":\"%s\",\"baseline_source_tree\":\"%s\","
            "\"equivalence_band\":{\"lower\":\"%s\",\"upper\":\"%s\"},\"family_sha256\":\"%s\","
            "\"lease_protocol\":\"" BQ_RETIREMENT_AA_LEASE_PROTOCOL "\",\"logical_cpu\":%d,\"machine_id\":\"%s\","
            "\"native_only\":true,\"native_target\":\"" BQ_RETIREMENT_AA_NATIVE_TARGET "\",\"phase_receipt_sha256\":\"%s\","
            "\"profile_id\":\"%s\","
            "\"profile_version\":\"%s\",\"schema\":\"" BQ_RETIREMENT_UNIT_CAMPAIGN_AA_SCHEMA "\",\"service_id\":\"%s\","
            "\"version\":" BQ_RETIREMENT_UNIT_CAMPAIGN_AA_VERSION "}",
            policy->sha256, identities->baseline_commit, identities->baseline_tree, policy->lower, policy->upper,
            family_sha256, logical_cpu, identities->machine_id, phase_receipt_sha256, identities->profile_id,
            identities->profile_version,
            identities->service_id) : -1;
    bool ok = written > 0 && written < (int)BQ_RETIREMENT_UNIT_CAMPAIGN_AA_RECEIPT_BYTES_MAX;
    if (ok) bq_digest(receipt, (u32)written, (char8*)digest);
    *length = ok ? (u32)written : 0;
    if (!ok) digest[0] = 0;
    return ok;
}

/* After the A/A stage: the pinned policy, the family over the frozen layout,
 * this job's A/A ratios against the band, and on admission the receipt over
 * the pinned binding context's identities. BQ_OK with the receipt, or
 * BQ_RECIPE_MISMATCH (policy, family, rows or band), BQ_CONFIGURATION_MISMATCH
 * (installed files) and no receipt. */
BUSTER_GLOBAL_LOCAL BqError bq_retirement_aa_admission_decide(BqRetirementWorkerCampaign* campaign,
    BqRetirementCampaignUnitStore const* unit, char const phase_receipt_sha256[SHA256_HEX_CAPACITY],
    char receipt[BQ_RETIREMENT_UNIT_CAMPAIGN_AA_RECEIPT_BYTES_MAX], u32* receipt_bytes,
    char receipt_sha256[SHA256_HEX_CAPACITY])
{
    BqRetirementAaPolicy policy = {0};
    TpRetirementComposeFamily family = {0};
    double* ratios[TP_RETIREMENT_COMPOSE_METRICS] = {0};
    BqRetirementAaBand band = {0};
    BqRetirementWorkerBindingContext context = {0};
    BqRetirementAaIdentities identities = {0};
    *receipt_bytes = 0;
    receipt_sha256[0] = 0;
    BqError result = campaign && unit && campaign->arena ? BQ_OK : BQ_IO;
    if (result == BQ_OK) result = bq_retirement_aa_policy_load(unit->installed, unit->profile, campaign->arena, &policy);
    if (result == BQ_OK &&
        !(tp_retirement_compose_family(&campaign->compose_layout, campaign->arena, &family) &&
          bq_retirement_aa_ratios(&campaign->stages[0].samples, &family, campaign->plan.pairs_per_round,
                                  campaign->driver.aa_attested_sha256, campaign->arena, ratios) &&
          bq_retirement_aa_band_check(&campaign->plan, &family, ratios, &policy, campaign->arena, &band)))
        result = BQ_RECIPE_MISMATCH;
    if (result == BQ_OK) result = bq_retirement_worker_binding_import(campaign->arena, unit->installed, unit->profile,
                                                                      &context);
    if (result == BQ_OK &&
        !(bq_retirement_aa_identities(&context, campaign->driver.family_sha256, &identities) &&
          bq_retirement_aa_receipt_render(&policy, &identities, campaign->campaign.cpu, campaign->driver.family_sha256,
                                          phase_receipt_sha256, receipt, receipt_bytes, receipt_sha256)))
        result = BQ_RECIPE_MISMATCH;
    bq_retirement_worker_binding_release(&context);
    if (result != BQ_OK)
    {
        *receipt_bytes = 0;
        receipt_sha256[0] = 0;
    }
    return result;
}
#endif
