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
 * The gate is the per-campaign in-job A/A alone (#881): no separately pinned
 * #426 eligibility policy is read or required. Its values are fixed here and
 * in retirement_unit_campaign.h: P = 60 pairs per round
 * (BQ_RETIREMENT_AA_PAIRS, which the frozen plan must carry), runtime rows
 * U = R (every runtime-eligible timed row of the frozen layout, and only
 * those, carries runtime pairs in the A/A spool) and the equivalence band
 * [BQ_RETIREMENT_UNIT_CAMPAIGN_AA_BAND_LOWER, _UPPER] = [0.98, 1.02]
 * (BQ_RETIREMENT_AA_LOWER_BOUND and _UPPER_BOUND as doubles).
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
 * ([0, inf)) is unbounded; either refuses. Only an admission renders the v3
 * receipt (BQ_RETIREMENT_UNIT_CAMPAIGN_AA_SCHEMA): the v1 identities from the
 * pinned binding context, the fixed band strings, the decision and the digest
 * of the coordinator's published AA_MEASURED receipt (phase_receipt_sha256).
 *
 * The fixture build (BQ_RETIREMENT_UNIT_CAMPAIGN_FIXTURE_AA) compiles none of
 * this; it presents bq_retirement_worker_campaign_fixture_receipt instead.
 *
 * Authority: the band decision is the unit's, over rows the service
 * attested. An admission enters A/B only through the campaign's production
 * transition over that attested digest (bq_retirement_unit_campaign_admit,
 * tp_retirement_campaign_admit_aa), and the coordinator's finalization
 * requires the admission receipt to name its own AA_MEASURED receipt
 * (bq_retirement_coordinator_finalize).
 *
 * Map: BQ_RETIREMENT_AA_PAIRS and the band bounds, bq_retirement_aa_ratio
 * and bq_retirement_aa_ratios (the A/A spool as the family's per-metric
 * cell-major ratios), BqRetirementAaBand, bq_retirement_aa_inside and
 * bq_retirement_aa_band_check, BqRetirementAaIdentities,
 * bq_retirement_aa_identity and bq_retirement_aa_identities,
 * bq_retirement_aa_receipt_render, bq_retirement_aa_admission_decide.
 */

/* The functional fixture build admits through its stand-in receipt instead
 * (BQ_RETIREMENT_UNIT_CAMPAIGN_FIXTURE_AA), so it does not compile this. */
#if !defined(BQ_RETIREMENT_UNIT_CAMPAIGN_FIXTURE_AA)
/* The in-job A/A gate's fixed values (#881, decided on #36 for the current
 * job): P pairs per round and the equivalence band's bounds, the nearest
 * doubles of retirement_unit_campaign.h's band strings. */
#define BQ_RETIREMENT_AA_PAIRS 60u
#define BQ_RETIREMENT_AA_LOWER_BOUND 0.98
#define BQ_RETIREMENT_AA_UPPER_BOUND 1.02
/* A receipt identity string (machine, profile, service): printable ASCII
 * without quote or backslash, so it renders without escapes. */
#define BQ_RETIREMENT_AA_IDENTITY_BYTES 128u
#define BQ_RETIREMENT_AA_COMMIT_HEX 40u
/* The #619 scopes the band applies to: both rounds and the pooled analysis. */
#define BQ_RETIREMENT_AA_SCOPES (TP_RETIREMENT_ROUNDS + 1u)
#define BQ_RETIREMENT_AA_LEASE_PROTOCOL "server-authoritative-supervisor-lease-v1"
#define BQ_RETIREMENT_AA_NATIVE_TARGET "x86_64-unknown-linux-gnu"

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
 * be the family's, in order, with P = BQ_RETIREMENT_AA_PAIRS pairs per round
 * and runtime rows U = R: the rows flagged for runtime pairs are exactly the
 * family's runtime-eligible rows, so their count is its runtime cell count.
 * Every record is re-encoded with lane D's own
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
    bool ok = samples && layout && arena && ratios && pairs == BQ_RETIREMENT_AA_PAIRS && attested &&
              tp_retirement_digest(attested) &&
              !samples->failed && samples->finished && samples->rows && samples->groups &&
              samples->row_count == layout->row_count && samples->group_count == layout->group_count &&
              samples->object_count == family->object_count &&
              samples->row_records == (u64)samples->row_count * per_unit &&
              samples->expected == samples->row_records + (u64)samples->object_count * per_unit;
    u32 runtime_rows = 0;
    for (u32 row = 0; ok && row < layout->row_count; row += 1)
    {
        bool runtime = (samples->rows[row].metrics & TP_RETIREMENT_SAMPLE_RUNTIME) != 0;
        ok = samples->rows[row].id == layout->rows[row].id && runtime == (layout->rows[row].runtime != 0);
        runtime_rows += runtime ? 1u : 0u;
    }
    ok = ok && runtime_rows == family->runtime_count &&
         runtime_rows == family->cells[TP_RETIREMENT_GENERATED_RUNTIME];
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

/* Both bounds bounded and inside the fixed band [0.98, 1.02]. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_aa_inside(TpRetirementBound const* bound)
{
    bool inside = isfinite(bound->lower) && isfinite(bound->upper) && bound->lower > 0.0 &&
                  bound->lower >= BQ_RETIREMENT_AA_LOWER_BOUND && bound->upper <= BQ_RETIREMENT_AA_UPPER_BOUND;
    return inside;
}

/* Every family member, in canonical order, assessed once by
 * tp_retirement_assess over the frozen plan: true only when each is valid and
 * its two round bounds and pooled bound are inside the band. The plan must be
 * the family's (member counts) and the gate's (P = BQ_RETIREMENT_AA_PAIRS
 * pairs per round). Assessment
 * stops at the first member outside the band. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_aa_band_check(TpRetirementPlan const* plan,
    TpRetirementComposeFamily const* family, double* const ratios[TP_RETIREMENT_COMPOSE_METRICS], Arena* arena,
    BqRetirementAaBand* band)
{
    u64 per_unit = plan ? (u64)TP_RETIREMENT_ROUNDS * plan->pairs_per_round : 0;
    unsigned widest = 0;
    for (u32 metric = 0; family && metric < TP_RETIREMENT_COMPOSE_METRICS; metric += 1)
        if (family->cells[metric] > widest) widest = family->cells[metric];
    bool ok = plan && family && ratios && arena && band && family->count && widest &&
              plan->pairs_per_round == BQ_RETIREMENT_AA_PAIRS &&
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
            BQ_RETIREMENT_AA_UPPER_BOUND};
        TpRetirementResult assessed = ok ? tp_retirement_assess(plan, &series, member->kind ? NULL : workspace,
                                                                member->kind ? 0 : plan->resamples) :
                                      (TpRetirementResult){0};
        ok = ok && assessed.valid;
        TpRetirementBound const scopes[BQ_RETIREMENT_AA_SCOPES] = {assessed.round[0], assessed.round[1],
                                                                   assessed.pooled};
        bool inside = ok;
        for (u32 scope = 0; inside && scope < BQ_RETIREMENT_AA_SCOPES; scope += 1)
            inside = bq_retirement_aa_inside(&scopes[scope]);
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

/* The v3 receipt, in canonical key order (json.dumps sort_keys, compact),
 * and its digest. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_aa_receipt_render(BqRetirementAaIdentities const* identities,
    int logical_cpu, char const family_sha256[SHA256_HEX_CAPACITY],
    char const phase_receipt_sha256[SHA256_HEX_CAPACITY], char receipt[BQ_RETIREMENT_UNIT_CAMPAIGN_AA_RECEIPT_BYTES_MAX],
    u32* length, char digest[SHA256_HEX_CAPACITY])
{
    int written = identities && family_sha256 && logical_cpu >= 0 && phase_receipt_sha256 &&
                  tp_retirement_digest(phase_receipt_sha256) ?
        snprintf(receipt, BQ_RETIREMENT_UNIT_CAMPAIGN_AA_RECEIPT_BYTES_MAX,
            "{\"aa_decision\":\"" BQ_RETIREMENT_UNIT_CAMPAIGN_AA_DECISION "\","
            "\"admitted\":true,\"baseline_source_commit\":\"%s\",\"baseline_source_tree\":\"%s\","
            "\"equivalence_band\":" BQ_RETIREMENT_UNIT_CAMPAIGN_AA_BAND ",\"family_sha256\":\"%s\","
            "\"lease_protocol\":\"" BQ_RETIREMENT_AA_LEASE_PROTOCOL "\",\"logical_cpu\":%d,\"machine_id\":\"%s\","
            "\"native_only\":true,\"native_target\":\"" BQ_RETIREMENT_AA_NATIVE_TARGET "\",\"phase_receipt_sha256\":\"%s\","
            "\"profile_id\":\"%s\","
            "\"profile_version\":\"%s\",\"schema\":\"" BQ_RETIREMENT_UNIT_CAMPAIGN_AA_SCHEMA "\",\"service_id\":\"%s\","
            "\"version\":" BQ_RETIREMENT_UNIT_CAMPAIGN_AA_VERSION "}",
            identities->baseline_commit, identities->baseline_tree, family_sha256, logical_cpu, identities->machine_id, phase_receipt_sha256, identities->profile_id,
            identities->profile_version,
            identities->service_id) : -1;
    bool ok = written > 0 && written < (int)BQ_RETIREMENT_UNIT_CAMPAIGN_AA_RECEIPT_BYTES_MAX;
    if (ok) bq_digest(receipt, (u32)written, (char8*)digest);
    *length = ok ? (u32)written : 0;
    if (!ok) digest[0] = 0;
    return ok;
}

/* After the A/A stage: the family over the frozen layout, this job's attested
 * A/A ratios against the fixed band (P = 60, U = R, per round and pooled),
 * and on admission the receipt over the pinned binding context's identities.
 * BQ_OK with the receipt, or BQ_RECIPE_MISMATCH (family, rows or band),
 * BQ_CONFIGURATION_MISMATCH (installed binding context) and no receipt. */
BUSTER_GLOBAL_LOCAL BqError bq_retirement_aa_admission_decide(BqRetirementWorkerCampaign* campaign,
    BqRetirementCampaignUnitStore const* unit, char const phase_receipt_sha256[SHA256_HEX_CAPACITY],
    char receipt[BQ_RETIREMENT_UNIT_CAMPAIGN_AA_RECEIPT_BYTES_MAX], u32* receipt_bytes,
    char receipt_sha256[SHA256_HEX_CAPACITY])
{
    TpRetirementComposeFamily family = {0};
    double* ratios[TP_RETIREMENT_COMPOSE_METRICS] = {0};
    BqRetirementAaBand band = {0};
    BqRetirementWorkerBindingContext context = {0};
    BqRetirementAaIdentities identities = {0};
    *receipt_bytes = 0;
    receipt_sha256[0] = 0;
    BqError result = campaign && unit && campaign->arena ? BQ_OK : BQ_IO;
    if (result == BQ_OK &&
        !(tp_retirement_compose_family(&campaign->compose_layout, campaign->arena, &family) &&
          bq_retirement_aa_ratios(&campaign->stages[0].samples, &family, campaign->plan.pairs_per_round,
                                  campaign->driver.aa_attested_sha256, campaign->arena, ratios) &&
          bq_retirement_aa_band_check(&campaign->plan, &family, ratios, campaign->arena, &band)))
        result = BQ_RECIPE_MISMATCH;
    if (result == BQ_OK) result = bq_retirement_worker_binding_import(campaign->arena, unit->installed, unit->profile,
                                                                      &context);
    if (result == BQ_OK &&
        !(bq_retirement_aa_identities(&context, campaign->driver.family_sha256, &identities) &&
          bq_retirement_aa_receipt_render(&identities, campaign->campaign.cpu, campaign->driver.family_sha256,
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
