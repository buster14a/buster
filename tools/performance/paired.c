/* Paired verdicts for complete #509 time-runs resources in a v1 manifest.
 * Each cell contains repetitions of one build in one independent unit.
 * Only the resulting paired unit effects enter the interval.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
#include <math.h>
#include <sys/file.h>
#define main buster_manifest_cli
#include "main.c"
#undef main
#include "../../src/buster/compiler/profile_order.h"

enum
{
    PAIRED_METRICS = 3,
    PAIRED_ROLES = 3,
    PAIRED_MAX_ROWS = 65536
};
static const char *const paired_metrics[] = {
    "wall_ms",
    "user_ms",
    "system_ms"
};
static const char *const paired_roles[] = {
    "main",
    "head",
    "main-again"
};
static const char *const paired_status[] = {
    "not_measured",
    "pass",
    "inconclusive",
    "regression",
    "invalid_incomplete"
};
typedef enum
{
    PAIR_NONE,
    PAIR_PASS,
    PAIR_UNCERTAIN,
    PAIR_REGRESSION,
    PAIR_INVALID
} PairStatus;

typedef struct PairPlan PairPlan;
struct PairPlan
{
    PerfJson json;
    char digest[65];
    size_t layouts;
    size_t copies;
    size_t rounds;
    size_t warmups;
    size_t sessions;
    size_t block_rounds;
    size_t units;
    size_t inner;
    size_t primary_count;
    bool layout_unit;
    bool included[PAIRED_METRICS];
    bool primary[PAIRED_METRICS];
    double budget[PAIRED_METRICS];
};

typedef struct PairSamples PairSamples;
struct PairSamples
{
    double value[PAIRED_METRICS];
};

typedef struct PairReport PairReport;
struct PairReport
{
    PerfDataset dataset;
    uint32_t identity;
    PairStatus status;
    const char *reason;
    char series[65];
    char builds[PAIRED_ROLES][65];
    char sources[PAIRED_ROLES][41];
    size_t complete_units;
    bool measured_pass;
    bool warmups_verified;
    bool promotable;
    PairSamples *unit_medians;
};

static PerfBytes paired_field(const PerfJson *json, uint32_t parent, const char *name)
{
    return perf_json_text(json, perf_json_find(json, parent, name));
}

static bool paired_hex(PerfBytes bytes, size_t length)
{
    bool valid = bytes.size == length;
    bool nonzero = false;
    for (size_t i = 0; valid && i < length; ++i)
    {
        unsigned char c = bytes.data[i];
        nonzero |= c != '0';
        valid = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    }
    return valid && nonzero;
}

static bool paired_load_json(const char *path, PerfJson *json, char *digest)
{
    PerfBuffer bytes = {0};
    bool valid = perf_read_file(path, &bytes) && perf_json_parse(json, perf_buffer_bytes(&bytes));
    valid = valid && (digest == NULL || perf_sha256(perf_buffer_bytes(&bytes), digest));
    if (!valid)
    {
        perf_json_free(json);
    }
    free(bytes.data);
    return valid;
}

static bool paired_count(const PerfJson *json, uint32_t parent, const char *name, size_t maximum, size_t *count)
{
    uint32_t node = perf_json_find(json, parent, name);
    uint64_t n = 0;
    bool valid = node < json->node_count && json->nodes[node].kind == PERF_JSON_NUMBER && perf_uint64(perf_json_text(json, node), &n) && n > 0 && n <= maximum;
    *count = valid ? n : 0;
    return valid;
}

static bool paired_plan(const char *path, PairPlan *plan)
{
    PerfJson *json = &plan->json;
    bool valid = paired_load_json(path, json, plan->digest);
    uint32_t unit = perf_json_find(json, 0, "unit");
    plan->layout_unit = perf_json_is(json, unit, "relative-layout");
    uint32_t layouts = perf_json_find(json, 0, "layouts");
    plan->layouts = perf_json_count(json, layouts);
    size_t version = 0;
    valid = valid && paired_count(json, 0, "version", 1, &version) && perf_identifier(paired_field(json, 0, "case")) &&
            (plan->layout_unit || perf_json_is(json, unit, "block")) && plan->layouts > 0 && plan->layouts <= 128 &&
            json->nodes[layouts].kind == PERF_JSON_ARRAY && paired_count(json, 0, "copies", 16, &plan->copies) &&
            paired_count(json, 0, "rounds", 1000, &plan->rounds) && paired_count(json, 0, "sessions", 64, &plan->sessions) &&
            paired_count(json, 0, "warmups", 1000, &plan->warmups) && paired_count(json, 0, "rounds_per_block", 1000, &plan->block_rounds);
    for (size_t i = 0; valid && i < plan->layouts; ++i)
    {
        uint32_t id = layouts + 1 + i;
        valid = json->nodes[id].kind == PERF_JSON_STRING && perf_identifier(perf_json_text(json, id));
        for (size_t j = 0; valid && j < i; ++j)
        {
            valid = !perf_bytes_equal(perf_json_text(json, id), perf_json_text(json, layouts + 1 + j));
        }
    }
    valid = valid && (plan->layout_unit ? plan->block_rounds == 1 : plan->layouts == 1 && plan->rounds % plan->block_rounds == 0);
    if (valid)
    {
        plan->units = plan->layout_unit ? plan->layouts : plan->sessions * plan->rounds / plan->block_rounds;
        plan->inner = plan->copies * (plan->layout_unit ? plan->sessions * plan->rounds : plan->block_rounds);
        valid = plan->units <= 2048 && (plan->warmups + plan->rounds) * plan->copies * plan->layouts * plan->sessions * PAIRED_ROLES <= PAIRED_MAX_ROWS;
    }
    uint32_t metrics = perf_json_find(json, 0, "metrics");
    valid = valid && metrics < json->node_count && json->nodes[metrics].kind == PERF_JSON_ARRAY;
    for (uint32_t m = perf_json_first(json, metrics); valid && m != UINT32_MAX; m = perf_json_after(json, metrics, m))
    {
        size_t k = 0;
        while (k < PAIRED_METRICS && !perf_json_is(json, perf_json_find(json, m, "name"), paired_metrics[k]))
        {
            ++k;
        }
        uint32_t role = perf_json_find(json, m, "role");
        size_t ppm = 0;
        valid = k < PAIRED_METRICS && !plan->included[k] && (perf_json_is(json, role, "primary") || perf_json_is(json, role, "diagnostic")) &&
                paired_count(json, m, "budget_ppm", 1000000, &ppm);
        if (valid)
        {
            plan->included[k] = true;
            plan->primary[k] = perf_json_is(json, role, "primary");
            plan->primary_count += plan->primary[k];
            plan->budget[k] = (double)ppm / 1000000.0;
        }
    }
    return valid && plan->primary_count > 0;
}

// Hash length-independent fixed-size digests in schema order. JSON whitespace
// and key order cannot split a series or make two builds appear different.
static bool paired_identity(const PairPlan *p, PairReport *r)
{
    PerfJson *json = &r->dataset.document;
    const uint32_t id = perf_json_find(json, perf_json_find(json, 0, "extensions"), "paired");
    r->identity = id;
    PerfSha256 series = perf_sha256_start();
    uint32_t plan_node = perf_json_find(json, id, "plan_sha256");
    bool valid = plan_node < json->node_count && json->nodes[plan_node].kind == PERF_JSON_STRING && perf_json_is(json, plan_node, p->digest) && perf_sha256_add(&series, perf_literal(p->digest));
    const char *fields[] = {
        "toolchain",
        "corpus",
        "host",
        "recipe",
        "harness"
    };
    for (size_t i = 0; valid && i < sizeof(fields) / sizeof(*fields); ++i)
    {
        uint32_t node = perf_json_find(json, id, fields[i]);
        PerfBytes field = perf_json_text(json, node);
        valid = node < json->node_count && json->nodes[node].kind == PERF_JSON_STRING && paired_hex(field, 64) && perf_sha256_add(&series, field);
    }
    if (valid)
    {
        perf_sha256_finish(&series, r->series);
    }
    for (size_t side = 0; valid && side < PAIRED_ROLES; ++side)
    {
        uint32_t build = perf_json_find(json, id, paired_roles[side]);
        uint32_t source_node = perf_json_find(json, build, "source");
        PerfBytes source = perf_json_text(json, source_node);
        uint32_t bins = perf_json_find(json, build, "binaries");
        PerfSha256 hash = perf_sha256_start();
        valid = source_node < json->node_count && json->nodes[source_node].kind == PERF_JSON_STRING && paired_hex(source, 40) && bins < json->node_count && json->nodes[bins].kind == PERF_JSON_ARRAY &&
                perf_json_count(json, bins) == p->layouts && perf_sha256_add(&hash, source);
        if (valid)
        {
            memcpy(r->sources[side], source.data, 40);
        }
        for (size_t i = 0; valid && i < p->layouts; ++i)
        {
            PerfBytes binary = perf_json_text(json, bins + 1 + i);
            valid = json->nodes[bins + 1 + i].kind == PERF_JSON_STRING && paired_hex(binary, 64) && perf_sha256_add(&hash, binary);
            for (size_t j = 0; valid && j < i; ++j)
            {
                valid = !perf_bytes_equal(binary, perf_json_text(json, bins + 1 + j));
            }
        }
        if (valid)
        {
            perf_sha256_finish(&hash, r->builds[side]);
        }
    }
    return valid && strcmp(r->builds[0], r->builds[2]) == 0;
}

static bool paired_take(PerfBytes *line, const char *literal)
{
    const size_t n = strlen(literal);
    bool matched = line->size >= n && memcmp(line->data, literal, n) == 0;
    if (matched)
    {
        line->data += n;
        line->size -= n;
    }
    return matched;
}

static bool paired_value(PerfBytes *line, bool milliseconds, const char *suffix, double *value)
{
    size_t n = 0;
    while (n < line->size && line->data[n] >= '0' && line->data[n] <= '9')
    {
        ++n;
    }
    uint64_t whole = 0;
    PerfBytes digits = {
        .data = line->data,
        .size = n
    };
    bool valid = perf_uint64(digits, &whole);
    line->data += n;
    line->size -= n;
    *value = (double)whole;
    if (milliseconds && valid)
    {
        valid = paired_take(line, ".") && line->size >= 3;
        unsigned fraction = 0;
        for (size_t i = 0; valid && i < 3; ++i)
        {
            unsigned digit = (unsigned)line->data[i] - '0';
            valid = digit < 10;
            fraction = fraction * 10 + digit;
        }
        if (valid)
        {
            *value += fraction / 1000.0;
            line->data += 3;
            line->size -= 3;
        }
    }
    return valid && paired_take(line, suffix);
}

static bool paired_row(const PairPlan *p, size_t session, size_t ordinal, PerfBytes line, PairSamples *samples, size_t *counts)
{
    const size_t slots = p->layouts * p->copies * PAIRED_ROLES;
    const size_t round = ordinal / slots;
    const size_t slot = profile_ab_tree(round, ordinal % slots, slots);
    const size_t layout = slot / PAIRED_ROLES % p->layouts;
    const size_t role = slot % PAIRED_ROLES;
    const size_t copy = slot / (PAIRED_ROLES * p->layouts);
    const size_t unit = p->layout_unit ? layout : (session * p->rounds + round) / p->block_rounds;
    char prefix[100];
    snprintf(prefix, sizeof(prefix), "%zu list 1 %s", round + 1, paired_roles[role]);
    bool valid = paired_take(&line, prefix);
    if (p->layouts > 1)
    {
        snprintf(prefix, sizeof(prefix), " placement %zu", layout + 1);
        valid = valid && paired_take(&line, prefix);
    }
    snprintf(prefix, sizeof(prefix), " copy %zu: ", copy + 1);
    valid = valid && paired_take(&line, prefix);
    PairSamples value = {0};
    const char *times[] = {
        " ms wall, ",
        " ms user, ",
        " ms system, "
    };
    for (size_t i = 0; valid && i < PAIRED_METRICS; ++i)
    {
        valid = paired_value(&line, true, times[i], &value.value[i]);
    }
    const char *counters[] = {
        " KiB wait4 peak, ",
        " minor faults, ",
        " major, ",
        " user cycles, ",
        " instructions, status 0"
    };
    double ignored;
    for (size_t i = 0; valid && i < sizeof(counters) / sizeof(*counters); ++i)
    {
        valid = paired_value(&line, false, counters[i], &ignored);
    }
    valid = valid && line.size == 0;
    if (valid)
    {
        const size_t cell = unit * PAIRED_ROLES + role;
        samples[cell * p->inner + counts[cell]++] = value;
    }
    return valid;
}

static bool paired_rows(const PairPlan *p, PerfBytes bytes, bool require_warmups, PairSamples *samples, size_t *counts, bool *verified)
{
    const size_t rows = p->layouts * p->copies * p->rounds * PAIRED_ROLES;
    size_t ordinal = 0;
    size_t sessions = 0;
    size_t receipts = 0;
    bool warmed = false;
    char warmups[80];
    snprintf(warmups, sizeof(warmups), "rounds=%zu slots=%zu", p->warmups, p->layouts * p->copies * PAIRED_ROLES);
    bool valid = true;
    while (valid && bytes.size > 0)
    {
        const unsigned char *end = memchr(bytes.data, '\n', bytes.size);
        if (end == NULL)
        {
            valid = false;
            break;
        }
        PerfBytes line = {
            .data = bytes.data,
            .size = (size_t)(end - bytes.data)
        };
        bytes.data = end + 1;
        bytes.size -= line.size + 1;
        if (paired_take(&line, "time-runs: completed warmups: "))
        {
            valid = !warmed && ordinal == 0 && sessions < p->sessions && perf_bytes_equal(line, perf_literal(warmups));
            warmed = valid;
        }
        else if (paired_take(&line, "run "))
        {
            valid = (warmed || !require_warmups) && ordinal < rows && sessions < p->sessions && paired_row(p, sessions, ordinal, line, samples, counts);
            ++ordinal;
        }
        else if (paired_take(&line, "time-runs: done, "))
        {
            valid = sessions < p->sessions && ordinal == rows && perf_bytes_equal(line, perf_literal("0 failed processes"));
            receipts += warmed;
            ++sessions;
            ordinal = 0;
            warmed = false;
        }
        else if (paired_take(&line, "warm-up "))
        {
            valid = false;
        }
        else if (paired_take(&line, "ab: done, "))
        {
            valid = perf_bytes_equal(line, perf_literal("0 failed runs"));
        }
    }
    valid = valid && ordinal == 0 && sessions == p->sessions;
    *verified = valid && receipts == p->sessions;
    return valid;
}

static int paired_number_order(const void *a, const void *b)
{
    double x = *(const double *)a;
    double y = *(const double *)b;
    return (x > y) - (x < y);
}

typedef struct PairInterval PairInterval;
struct PairInterval
{
    double mean;
    double squares;
    double lower;
    double upper;
    PairStatus status;
};

typedef struct PairCritical PairCritical;
struct PairCritical
{
    size_t df;
    double t[PAIRED_METRICS];
};

// Two-sided Student quantiles t(1 - .05/(4*m), df), rounded UP to
// six decimals. Bonferroni covers m primary metrics and two references.
// Rounding degrees of freedom DOWN is conservative between table entries.
static double paired_critical(size_t units, size_t primary)
{
    static const PairCritical table[] = {
        {
            .df = 1,
            .t = {
                25.451700,
                50.923037,
                76.390010
            }
        },
        {
            .df = 2,
            .t = {
                6.205347,
                8.860201,
                10.885867
            }
        },
        {
            .df = 3,
            .t = {
                4.176535,
                5.391950,
                6.231544
            }
        },
        {
            .df = 4,
            .t = {
                3.495406,
                4.314657,
                4.851009
            }
        },
        {
            .df = 5,
            .t = {
                3.163382,
                3.810005,
                4.219310
            }
        },
        {
            .df = 7,
            .t = {
                2.841245,
                3.335295,
                3.635808
            }
        },
        {
            .df = 10,
            .t = {
                2.633767,
                3.038244,
                3.276842
            }
        },
        {
            .df = 15,
            .t = {
                2.489880,
                2.836628,
                3.036284
            }
        },
        {
            .df = 30,
            .t = {
                2.359563,
                2.657356,
                2.824696
            }
        },
        {
            .df = 60,
            .t = {
                2.299046,
                2.575235,
                2.728553
            }
        },
        {
            .df = 120,
            .t = {
                2.269875,
                2.535915,
                2.682700
            }
        }
    };
    size_t row = 0;
    while (row + 1 < sizeof(table) / sizeof(*table) && table[row + 1].df < units)
    {
        ++row;
    }
    return table[row].t[primary - 1];
}

static void paired_add_effect(PairInterval *effect, size_t unit, double ratio)
{
    double log_ratio = log(ratio);
    double delta = log_ratio - effect->mean;
    effect->mean += delta / (double)(unit + 1);
    effect->squares += delta * (log_ratio - effect->mean);
}

static void paired_interval(FILE *out, PairInterval *effect, size_t units, size_t primary, double budget)
{
    effect->status = PAIR_UNCERTAIN;
    fprintf(out, "{\"mean_log_ratio\":%.17g,\"effect_pct\":%.10g", effect->mean, 100 * expm1(effect->mean));
    if (units > 1)
    {
        double radius = paired_critical(units, primary) * sqrt(effect->squares / ((double)units * (units - 1)));
        effect->lower = effect->mean - radius;
        effect->upper = effect->mean + radius;
        // Compare on the ratio scale. Log-of-ratio subtraction at exactly the
        // declared boundary would otherwise sometimes reverse the inequality.
        double limit = 1 + budget;
        if (exp(effect->lower) > limit)
        {
            effect->status = PAIR_REGRESSION;
        }
        else if (exp(effect->upper) <= limit)
        {
            effect->status = PAIR_PASS;
        }
        fprintf(out, ",\"interval_log\":[%.17g,%.17g]", effect->lower, effect->upper);
    }
    else
    {
        fputs(",\"interval_log\":null", out);
    }
    fprintf(out, ",\"verdict\":\"%s\"}", paired_status[effect->status]);
}

static void paired_statistics(FILE *out, const PairPlan *p, PairReport *r, const PairSamples *samples, double *scratch)
{
    r->status = PAIR_PASS;
    bool calibration_pass = true;
    bool false_alert = false;
    bool calibrated = true;
    bool comma = false;
    fputs("\"metrics\":[", out);
    for (size_t m = 0; m < PAIRED_METRICS; ++m)
    {
        if (!p->included[m])
        {
            continue;
        }
        fprintf(out, "%s{\"name\":\"%s\",\"primary\":%s,\"budget_pct\":%.7g,\"units\":[", comma ? "," : "", paired_metrics[m], p->primary[m] ? "true" : "false", 100 * p->budget[m]);
        comma = true;
        PairInterval effects[2] = {0};
        bool positive = true;
        for (size_t u = 0; u < p->units; ++u)
        {
            double median[PAIRED_ROLES];
            for (size_t side = 0; side < PAIRED_ROLES; ++side)
            {
                size_t start = (u * PAIRED_ROLES + side) * p->inner;
                for (size_t j = 0; j < p->inner; ++j)
                {
                    scratch[j] = samples[start + j].value[m];
                }
                qsort(scratch, p->inner, sizeof(*scratch), paired_number_order);
                median[side] = (scratch[(p->inner - 1) / 2] + scratch[p->inner / 2]) / 2;
                positive = positive && median[side] > 0;
                if (r->unit_medians)
                {
                    r->unit_medians[u * PAIRED_ROLES + side].value[m] = median[side];
                }
            }
            fprintf(out, "%s[%.12g,%.12g,%.12g]", u ? "," : "", median[0], median[1], median[2]);
            for (size_t side = 1; positive && side < PAIRED_ROLES; ++side)
            {
                paired_add_effect(&effects[side - 1], u, median[side] / median[0]);
            }
        }
        fputs("]", out);
        if (positive)
        {
            fputs(",\"candidate\":", out);
            paired_interval(out, &effects[0], p->units, p->primary_count, p->budget[m]);
            fputs(",\"aa\":", out);
            paired_interval(out, &effects[1], p->units, p->primary_count, p->budget[m]);
        }
        else
        {
            fputs(",\"unavailable\":\"zero unit median has no log ratio\"", out);
        }
        fputs("}", out);
        if (p->primary[m])
        {
            PairStatus status = positive ? effects[0].status : PAIR_INVALID;
            if (status > r->status)
            {
                r->status = status;
            }
            calibrated = calibrated && positive;
            false_alert = false_alert || (positive && effects[1].status == PAIR_REGRESSION);
            calibration_pass = calibration_pass && positive && effects[1].status == PAIR_PASS && exp(-effects[1].lower) <= 1 + p->budget[m];
        }
    }
    fprintf(out, "],\"aa_campaigns\":%d,\"aa_false_alerts\":%d,\"aa_within_budget\":%s,", calibrated, false_alert && calibrated, calibration_pass ? "true" : "false");
    if (!calibration_pass && r->status != PAIR_INVALID)
    {
        r->status = PAIR_UNCERTAIN;
        r->reason = "A/A interval outside or crossing the symmetric budget";
    }
}

static void paired_json_value(FILE *out, const PerfJson *json, uint32_t node)
{
    if (node < json->node_count)
    {
        PerfJsonNode value = json->nodes[node];
        fwrite(json->source.data + value.raw_offset, 1, value.raw_size, out);
    }
    else
    {
        fputs("null", out);
    }
}

static void paired_report(FILE *out, const PairPlan *p, const char *manifest, PairReport *r)
{
    bool loaded = perf_dataset_load(&r->dataset, manifest, "docs/performance/buster-manifest-v1.schema.json");
    const PerfJson *json = &r->dataset.document;
    bool identified = r->dataset.manifest_validated && paired_identity(p, r);
    if (!r->dataset.manifest_validated)
    {
        perf_json_free(&r->dataset.document);
        r->identity = UINT32_MAX;
    }
    r->status = PAIR_INVALID;
    r->reason = "invalid resource, plan, identity or coverage";
    fputs("{\"plan\":", out);
    paired_json_value(out, &p->json, 0);
    fputs(",\"identity\":", out);
    paired_json_value(out, json, r->identity);
    fputs(",\"estimator\":\"mean paired log ratio of within-unit medians\",\"family_confidence\":0.95,\"reference_count\":2", out);
    fprintf(out, ",\"series\":\"%s\",\"dataset\":", r->series);
    paired_json_value(out, json, perf_json_find(json, 0, "id"));
    uint32_t kind = perf_json_find(json, 0, "kind");
    fputs(",\"kind\":", out);
    paired_json_value(out, json, kind);
    fputs(",\"limitations\":", out);
    paired_json_value(out, json, perf_json_find(json, 0, "limitations"));
    fputs(",\"observations_sha256\":", out);
    PerfResource *raw = loaded ? perf_resource_find(&r->dataset, perf_literal("observations")) : NULL;
    paired_json_value(out, json, raw ? perf_json_find(json, raw->definition, "sha256") : UINT32_MAX);
    fputs(",", out);
    uint32_t status = perf_json_find(json, 0, "status");
    uint32_t format = raw ? perf_json_find(json, raw->definition, "format") : UINT32_MAX;
    bool v2 = perf_json_is(json, format, "time-runs-v2");
    bool legacy = perf_json_is(json, format, "time-runs-v1");
    bool measured = perf_json_is(json, kind, "measured");
    bool imported = perf_json_is(json, kind, "imported");
    if (loaded && identified && raw == NULL && perf_json_is(json, status, "planned"))
    {
        r->status = PAIR_NONE;
        r->reason = "no observations";
    }
    else if (loaded && identified && (v2 || legacy))
    {
        PairSamples *samples = calloc(p->units * p->inner * PAIRED_ROLES, sizeof(*samples));
        size_t *counts = calloc(p->units * PAIRED_ROLES, sizeof(*counts));
        double *scratch = calloc(p->inner, sizeof(*scratch));
        bool valid = samples && counts && scratch && paired_rows(p, perf_buffer_bytes(&raw->bytes), v2, samples, counts, &r->warmups_verified);
        for (size_t u = 0; counts && u < p->units; ++u)
        {
            size_t *cell = &counts[u * PAIRED_ROLES];
            r->complete_units += cell[0] == p->inner && cell[1] == p->inner && cell[2] == p->inner;
        }
        if (valid && r->complete_units == p->units && perf_json_is(json, status, "complete"))
        {
            r->reason = "fixed sampling plan complete";
            paired_statistics(out, p, r, samples, scratch);
            r->measured_pass = r->status == PAIR_PASS && measured;
            r->promotable = v2 && r->measured_pass;
            if (legacy && !imported && !r->warmups_verified)
            {
                r->status = PAIR_INVALID;
                r->reason = "legacy capture does not verify completed warmups";
            }
        }
        free(samples);
        free(counts);
        free(scratch);
    }
    if (measured)
    {
        fputs("\"observations_format\":", out);
        paired_json_value(out, json, format);
        fprintf(out, ",\"promotable\":%s,", r->promotable ? "true" : "false");
    }
    fprintf(out, "\"independent_units\":%zu,\"planned_units\":%zu,\"verdict\":\"%s\",\"reason\":\"%s\"}", r->complete_units, p->units, paired_status[r->status], r->reason);
}

// Bridge only matched relative layouts of the same source pair. Wall times can
// shift even when the within-series candidate and A/A ratios stay unchanged.
static bool paired_overlap(FILE *out, const PairPlan *p, const char *old_path, const char *new_path)
{
    PairReport old = {.unit_medians = calloc(p->units * PAIRED_ROLES, sizeof(PairSamples))};
    PairReport next = {.unit_medians = calloc(p->units * PAIRED_ROLES, sizeof(PairSamples))};
    fputs("{\"old\":", out);
    paired_report(out, p, old_path, &old);
    fputs(",\"new\":", out);
    paired_report(out, p, new_path, &next);
    bool compatible = old.unit_medians && next.unit_medians && p->layout_unit && p->units > 1 && old.measured_pass && next.measured_pass &&
                      strcmp(old.series, next.series) != 0 && strcmp(old.sources[0], next.sources[0]) == 0 && strcmp(old.sources[1], next.sources[1]) == 0;
    bool warmups_verified = old.warmups_verified && next.warmups_verified;
    bool qualified = compatible && warmups_verified;
    fputs(",\"between_series\":[", out);
    size_t count = 0;
    for (size_t m = 0; compatible && m < PAIRED_METRICS; ++m)
    {
        for (size_t side = 0; p->primary[m] && side < 2; ++side)
        {
            PairInterval effect = {0};
            for (size_t u = 0; u < p->units; ++u)
            {
                size_t i = u * PAIRED_ROLES + side;
                paired_add_effect(&effect, u, next.unit_medians[i].value[m] / old.unit_medians[i].value[m]);
            }
            fprintf(out, "%s{\"metric\":\"%s\",\"build\":\"%s\",\"comparison\":", count ? "," : "", paired_metrics[m], paired_roles[side]);
            paired_interval(out, &effect, p->units, p->primary_count, p->budget[m]);
            fputs("}", out);
            qualified = qualified && effect.status == PAIR_PASS && exp(-effect.lower) <= 1 + p->budget[m];
            ++count;
        }
    }
    fprintf(out, "],\"warmups_verified\":%s,\"overlap_qualified\":%s}\n", warmups_verified ? "true" : "false", qualified ? "true" : "false");
    perf_dataset_free(&old.dataset);
    perf_dataset_free(&next.dataset);
    free(old.unit_medians);
    free(next.unit_medians);
    return qualified;
}

#include "paired_history.c"
#include "paired_test.c"

int main(int argc, char **argv)
{
    PairPlan plan = {0};
    bool valid = argc >= 4 && paired_plan(argv[2], &plan);
    if (argc == 2 && strcmp(argv[1], "--self-test") == 0)
    {
        valid = paired_self_test();
    }
    else if (valid && (argc == 4 || (argc == 6 && strcmp(argv[4], "--evidence-root") == 0)) && strcmp(argv[1], "report") == 0)
    {
        PairReport report = {0};
        report.dataset.evidence_root = argc == 6 ? argv[5] : NULL;
        paired_report(stdout, &plan, argv[3], &report);
        putchar('\n');
        valid = report.status != PAIR_INVALID && !ferror(stdout);
        perf_dataset_free(&report.dataset);
    }
    else if (valid && argc == 5 && strcmp(argv[1], "overlap") == 0)
    {
        valid = paired_overlap(stdout, &plan, argv[3], argv[4]);
    }
    else if (valid && argc == 7 && strcmp(argv[1], "record") == 0)
    {
        valid = paired_record(&plan, argv[3], argv[4], argv[5], argv[6]);
    }
    else
    {
        fputs("usage: paired --self-test | paired report PLAN MANIFEST [--evidence-root DIR] | paired overlap PLAN OLD_MANIFEST NEW_MANIFEST | paired record PLAN DIRECTORY CANDIDATE_SOURCE LATEST_MANIFEST ANCHOR_MANIFEST\n", stderr);
        valid = false;
    }
    perf_json_free(&plan.json);
    return valid ? 0 : 1;
}
