/* Native-retirement performance statistics. This is the versioned,
 * machine-replayable method for native-retirement-performance-v1 only.
 *
 * A family member is an overall aggregate, a required slice, or a cell for
 * one variable metric. Its input is cell-major [cell][round][pair] paired
 * candidate/baseline ratios. Within a two-pair block the statistic is the
 * equal-weight geometric mean over both ratios and every included cell.
 * Round and pooled point estimates are medians of those block statistics.
 *
 * Adjacent pairs form one indivisible AB/BA block. Bootstrap draws select
 * whole blocks with replacement, use the same draw for every cell, and never
 * trim or discard a ratio. Round analyses draw only from their round. The
 * pooled analysis draws within each round independently and combines the two
 * equally sized round strata. Every resampled analysis uses at least 100,000
 * draws. A cell instead uses the exact distribution-free block-median interval.
 *
 * Both one-sided directions and all three scopes share family alpha 0.05.
 * Half is reserved for aggregate/slice bootstrap members and half for exact
 * cells. Each tail receives 0.05 / (12 * members_in_its_partition). Bootstrap
 * quantiles use inverse empirical CDF/type 1: sorted[ceil(p * B) - 1], clamped.
 * SplitMix64, the seed-domain mapping, rejection sampling, and Fisher-Yates
 * below completely define bootstrap replay and experiment-order shuffles.
 */
#ifndef BUSTER_THROUGHPUT_RETIREMENT_STATS_H
#define BUSTER_THROUGHPUT_RETIREMENT_STATS_H

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#define TP_RETIREMENT_STATISTICS_VERSION 1u
#define TP_RETIREMENT_ROUNDS 2u
#define TP_RETIREMENT_MIN_PAIRS_PER_ROUND 60u
#define TP_RETIREMENT_MAX_PAIRS_PER_ROUND 256u
#define TP_RETIREMENT_MIN_RESAMPLES 100000u
#define TP_RETIREMENT_MAX_RESAMPLES 1000000u
#define TP_RETIREMENT_MAX_CELLS 100000u
#define TP_RETIREMENT_MAX_BOOTSTRAP_MEMBERS_PER_SCOPE 80u
#define TP_RETIREMENT_MAX_CELL_MEMBERS_PER_SCOPE 300000u
#define TP_RETIREMENT_FAMILY_ALPHA 0.05
#define TP_RETIREMENT_STATISTICAL_SCOPES 3u
#define TP_RETIREMENT_ALPHA_PARTITIONS 2u
#define TP_RETIREMENT_MIN_EXPECTED_TAIL_DRAWS 5.0
#define TP_RETIREMENT_SEED_DOMAIN_SCHEDULE 1u
#define TP_RETIREMENT_SEED_DOMAIN_BOOTSTRAP 2u

typedef enum TpRetirementOrder
{
    TP_RETIREMENT_AB,
    TP_RETIREMENT_BA
} TpRetirementOrder;

typedef enum TpRetirementOutcome
{
    TP_RETIREMENT_INVALID,
    TP_RETIREMENT_PASS,
    TP_RETIREMENT_REGRESSION,
    TP_RETIREMENT_INCONCLUSIVE
} TpRetirementOutcome;

typedef enum TpRetirementMetric
{
    TP_RETIREMENT_WALL_TIME,
    TP_RETIREMENT_PEAK_RSS,
    TP_RETIREMENT_GENERATED_RUNTIME,
    TP_RETIREMENT_VARIABLE_METRICS
} TpRetirementMetric;

typedef enum TpRetirementMemberKind
{
    TP_RETIREMENT_BOOTSTRAP_MEMBER,
    TP_RETIREMENT_EXACT_CELL_MEMBER,
    TP_RETIREMENT_MEMBER_KINDS
} TpRetirementMemberKind;

typedef struct TpRetirementPlan
{
    uint64_t seed;
    unsigned version;
    unsigned bootstrap_members_per_scope;
    unsigned cell_members_per_scope;
    unsigned pairs_per_round;
    unsigned resamples;
    int frozen_before_samples;
} TpRetirementPlan;

typedef struct TpRetirementSeries
{
    double const* ratios;
    size_t ratio_count;
    unsigned cell_count;
    unsigned observed_pairs[TP_RETIREMENT_ROUNDS];
    unsigned member_kind;
    unsigned family_index;
    unsigned metric_index;
    double limit;
} TpRetirementSeries;

typedef struct TpRetirementBound
{
    double estimate;
    double lower;
    double upper;
} TpRetirementBound;

typedef struct TpRetirementResult
{
    TpRetirementOutcome outcome;
    TpRetirementBound round[TP_RETIREMENT_ROUNDS];
    TpRetirementBound pooled;
    double tail_alpha;
    unsigned resamples;
    int resampled;
    int valid;
} TpRetirementResult;

typedef struct TpRetirementRandom
{
    uint64_t state;
} TpRetirementRandom;

static inline uint64_t tp_retirement_mix64(uint64_t value)
{
    value = (value ^ (value >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    value = (value ^ (value >> 27)) * UINT64_C(0x94d049bb133111eb);
    uint64_t result = value ^ (value >> 31);
    return result;
}

/* The four coordinates are stable evidence fields, not array addresses or
 * iteration history. Domain 1 is schedule order; domain 2 is bootstrap.
 */
static inline uint64_t tp_retirement_seed(uint64_t seed, unsigned domain, unsigned metric,
                                          unsigned family_member, unsigned analysis)
{
    uint64_t value = seed ^ UINT64_C(0x6e61746976652d31);
    value ^= tp_retirement_mix64((uint64_t)domain + UINT64_C(0x100000001b3));
    value ^= tp_retirement_mix64((uint64_t)metric + UINT64_C(0x9e3779b97f4a7c15));
    value ^= tp_retirement_mix64((uint64_t)family_member + UINT64_C(0xd1b54a32d192ed03));
    value ^= tp_retirement_mix64((uint64_t)analysis + UINT64_C(0x94d049bb133111eb));
    uint64_t result = tp_retirement_mix64(value);
    return result;
}

static inline uint64_t tp_retirement_random_next(TpRetirementRandom* random)
{
    random->state += UINT64_C(0x9e3779b97f4a7c15);
    uint64_t result = tp_retirement_mix64(random->state);
    return result;
}

/* Rejection avoids modulo bias. bound is validated nonzero by callers. */
static inline uint64_t tp_retirement_random_bounded(TpRetirementRandom* random, uint64_t bound)
{
    uint64_t threshold = (uint64_t)(0 - bound) % bound;
    uint64_t result;
    do result = tp_retirement_random_next(random); while (result < threshold);
    result %= bound;
    return result;
}

/* Defines per-cell AB/BA orientation and a separate cell shuffle for each pair
 * in the block. The second pair reverses first_orders[cell].
 */
static inline int tp_retirement_block_schedule(uint64_t seed, unsigned round, unsigned block,
                                               unsigned cell_count, unsigned* first_orders,
                                               unsigned* first_cells, unsigned* second_cells,
                                               unsigned cell_capacity)
{
    int result = cell_count > 0 && cell_count <= TP_RETIREMENT_MAX_CELLS &&
                 cell_capacity == cell_count && first_orders && first_cells && second_cells &&
                 round < TP_RETIREMENT_ROUNDS;
    if (result)
    {
        TpRetirementRandom random = {
            tp_retirement_seed(seed, TP_RETIREMENT_SEED_DOMAIN_SCHEDULE,
                               round, block, cell_count)};
        for (unsigned i = 0; i < cell_count; ++i)
        {
            first_orders[i] = (unsigned)tp_retirement_random_bounded(&random, 2);
            first_cells[i] = second_cells[i] = i;
        }
        for (unsigned remaining = cell_count; remaining > 1; --remaining)
        {
            unsigned other = (unsigned)tp_retirement_random_bounded(&random, remaining);
            unsigned value = first_cells[remaining - 1];
            first_cells[remaining - 1] = first_cells[other];
            first_cells[other] = value;
        }
        for (unsigned remaining = cell_count; remaining > 1; --remaining)
        {
            unsigned other = (unsigned)tp_retirement_random_bounded(&random, remaining);
            unsigned value = second_cells[remaining - 1];
            second_cells[remaining - 1] = second_cells[other];
            second_cells[other] = value;
        }
    }
    return result;
}

static inline double tp_retirement_select(double* values, unsigned count, unsigned rank)
{
    ptrdiff_t left = 0, right = (ptrdiff_t)count - 1;
    while (left < right)
    {
        double pivot = values[left + (right - left) / 2];
        ptrdiff_t i = left, j = right;
        while (i <= j)
        {
            while (values[i] < pivot) ++i;
            while (values[j] > pivot) --j;
            if (i <= j)
            {
                double value = values[i];
                values[i] = values[j];
                values[j] = value;
                ++i;
                --j;
            }
        }
        if ((ptrdiff_t)rank <= j) right = j;
        else if ((ptrdiff_t)rank >= i) left = i;
        else left = right;
    }
    double result = values[rank];
    return result;
}

static inline double tp_retirement_median(double* values, unsigned count)
{
    double result = tp_retirement_select(values, count, count / 2);
    if (!(count & 1))
    {
        double lower = tp_retirement_select(values, count, count / 2 - 1);
        result = lower * 0.5 + result * 0.5;
    }
    return result;
}

static inline void tp_retirement_sift(double* values, size_t root, size_t count)
{
    int done = 0;
    while (!done)
    {
        size_t child = root * 2 + 1;
        if (child >= count) done = 1;
        else
        {
            if (child + 1 < count && values[child] < values[child + 1]) ++child;
            if (values[root] >= values[child]) done = 1;
            else
            {
                double value = values[root];
                values[root] = values[child];
                values[child] = value;
                root = child;
            }
        }
    }
}

/* Callback-free heapsort keeps 100,000+ resample quantiles O(B log B). */
static inline void tp_retirement_sort(double* values, size_t count)
{
    for (size_t start = count / 2; start > 0; --start) tp_retirement_sift(values, start - 1, count);
    for (size_t end = count; end > 1; --end)
    {
        double value = values[0];
        values[0] = values[end - 1];
        values[end - 1] = value;
        tp_retirement_sift(values, 0, end - 1);
    }
}

static inline size_t tp_retirement_quantile_rank(size_t count, double probability)
{
    double product = ceil(probability * (double)count);
    size_t result = product > 1.0 ? (size_t)product - 1 : 0;
    if (result >= count) result = count - 1;
    return result;
}

static inline size_t tp_retirement_ratio_index(TpRetirementPlan const* plan, unsigned cell,
                                               unsigned round, unsigned pair)
{
    size_t result = ((size_t)cell * TP_RETIREMENT_ROUNDS + round) * plan->pairs_per_round + pair;
    return result;
}

static inline int tp_retirement_block_values(TpRetirementPlan const* plan,
                                             TpRetirementSeries const* series,
                                             double* values)
{
    unsigned blocks = plan->pairs_per_round / 2;
    int result = 1;
    for (unsigned round = 0; round < TP_RETIREMENT_ROUNDS && result; ++round)
    {
        for (unsigned block = 0; block < blocks && result; ++block)
        {
            double mean_log = 0.0;
            unsigned count = 0;
            for (unsigned cell = 0; cell < series->cell_count; ++cell)
            {
                for (unsigned within = 0; within < 2; ++within)
                {
                    double ratio = series->ratios[tp_retirement_ratio_index(plan, cell, round, block * 2 + within)];
                    double ratio_log = log(ratio);
                    ++count;
                    mean_log += (ratio_log - mean_log) / (double)count;
                }
            }
            values[round * blocks + block] = exp(mean_log);
            result = isfinite(values[round * blocks + block]) && values[round * blocks + block] > 0.0;
        }
    }
    return result;
}

static inline double tp_retirement_analysis_median(double const* block_values, unsigned blocks,
                                                   unsigned analysis, double* sample)
{
    unsigned count = analysis < TP_RETIREMENT_ROUNDS ? blocks : blocks * TP_RETIREMENT_ROUNDS;
    unsigned offset = analysis < TP_RETIREMENT_ROUNDS ? analysis * blocks : 0;
    for (unsigned i = 0; i < count; ++i) sample[i] = block_values[offset + i];
    double result = tp_retirement_median(sample, count);
    return result;
}

static inline double tp_retirement_bootstrap_sample(TpRetirementRandom* random,
                                                    double const* block_values, unsigned blocks,
                                                    unsigned analysis, double* sample)
{
    unsigned count = analysis < TP_RETIREMENT_ROUNDS ? blocks : blocks * TP_RETIREMENT_ROUNDS;
    for (unsigned i = 0; i < count; ++i)
    {
        unsigned round = analysis < TP_RETIREMENT_ROUNDS ? analysis : i / blocks;
        unsigned block = (unsigned)tp_retirement_random_bounded(random, blocks);
        sample[i] = block_values[round * blocks + block];
    }
    double result = tp_retirement_median(sample, count);
    return result;
}

static inline double tp_retirement_sign_tail(unsigned count, unsigned successes)
{
    double term = ldexp(1.0, -(int)count);
    double result = 0.0;
    for (unsigned k = 0; k <= count; ++k)
    {
        if (k >= successes) result += term;
        term *= (double)(count - k) / (double)(k + 1);
    }
    return result;
}

/* Exact order-statistic interval for the median of independent block
 * statistics. This is stats.h's binomial construction with one-sided tail
 * alpha, applied to the declared two-pair observational unit.
 */
static inline void tp_retirement_exact_interval(double* values, unsigned count,
                                                double tail_alpha, TpRetirementBound* bound)
{
    tp_retirement_sort(values, count);
    bound->estimate = values[count / 2];
    if (!(count & 1)) bound->estimate = values[count / 2 - 1] * 0.5 + bound->estimate * 0.5;
    bound->lower = 0.0;
    bound->upper = INFINITY;
    for (unsigned k = 0; k < count / 2; ++k)
    {
        if (tp_retirement_sign_tail(count, count - k) <= tail_alpha)
        {
            bound->lower = values[k];
            bound->upper = values[count - k - 1];
        }
    }
}

static inline int tp_retirement_bootstrap_bound(TpRetirementPlan const* plan,
                                                TpRetirementSeries const* series,
                                                unsigned analysis, double tail_alpha,
                                                double const* block_values, double* workspace,
                                                TpRetirementBound* bound)
{
    unsigned blocks = plan->pairs_per_round / 2;
    double sample[TP_RETIREMENT_MAX_PAIRS_PER_ROUND] = {0};
    TpRetirementRandom random = {
        tp_retirement_seed(plan->seed, TP_RETIREMENT_SEED_DOMAIN_BOOTSTRAP,
                           series->metric_index, series->family_index, analysis)};
    bound->estimate = tp_retirement_analysis_median(block_values, blocks, analysis, sample);
    int result = isfinite(bound->estimate) && bound->estimate > 0.0;
    for (unsigned resample = 0; resample < plan->resamples && result; ++resample)
    {
        workspace[resample] = tp_retirement_bootstrap_sample(&random, block_values, blocks,
                                                             analysis, sample);
        result = isfinite(workspace[resample]) && workspace[resample] > 0.0;
    }
    if (result)
    {
        tp_retirement_sort(workspace, plan->resamples);
        bound->lower = workspace[tp_retirement_quantile_rank(plan->resamples, tail_alpha)];
        bound->upper = workspace[tp_retirement_quantile_rank(plan->resamples, 1.0 - tail_alpha)];
    }
    return result;
}

static inline int tp_retirement_exact_bound(TpRetirementPlan const* plan, unsigned analysis,
                                            double tail_alpha, double const* block_values,
                                            TpRetirementBound* bound)
{
    unsigned blocks = plan->pairs_per_round / 2;
    unsigned count = analysis < TP_RETIREMENT_ROUNDS ? blocks : blocks * TP_RETIREMENT_ROUNDS;
    unsigned offset = analysis < TP_RETIREMENT_ROUNDS ? analysis * blocks : 0;
    double values[TP_RETIREMENT_MAX_PAIRS_PER_ROUND] = {0};
    for (unsigned i = 0; i < count; ++i) values[i] = block_values[offset + i];
    tp_retirement_exact_interval(values, count, tail_alpha, bound);
    int result = isfinite(bound->estimate) && bound->estimate > 0.0 &&
                 !isnan(bound->lower) && bound->lower >= 0.0 &&
                 !isnan(bound->upper) && bound->upper > 0.0;
    return result;
}

/* The pooled point estimate is the median of the two equal round strata. For
 * distribution-free coverage, do not assume those strata have one identical
 * distribution: build each round's exact interval with half of the pooled tail
 * allocation, then envelope their population medians. The median of an equal
 * mixture lies between its component medians, and the inner union bound costs
 * no more than the pooled tail allocation in either direction.
 */
static inline int tp_retirement_exact_pooled_bound(TpRetirementPlan const* plan,
                                                   double tail_alpha,
                                                   double const* block_values,
                                                   TpRetirementBound* bound)
{
    unsigned blocks = plan->pairs_per_round / 2;
    double pooled_values[TP_RETIREMENT_MAX_PAIRS_PER_ROUND] = {0};
    double round_values[TP_RETIREMENT_MAX_PAIRS_PER_ROUND / 2] = {0};
    TpRetirementBound component[TP_RETIREMENT_ROUNDS] = {0};
    for (unsigned i = 0; i < blocks * TP_RETIREMENT_ROUNDS; ++i)
        pooled_values[i] = block_values[i];
    bound->estimate = tp_retirement_median(pooled_values, blocks * TP_RETIREMENT_ROUNDS);
    for (unsigned round = 0; round < TP_RETIREMENT_ROUNDS; ++round)
    {
        for (unsigned block = 0; block < blocks; ++block)
            round_values[block] = block_values[round * blocks + block];
        tp_retirement_exact_interval(round_values, blocks, tail_alpha * 0.5,
                                     &component[round]);
    }
    bound->lower = fmin(component[0].lower, component[1].lower);
    bound->upper = fmax(component[0].upper, component[1].upper);
    int result = isfinite(bound->estimate) && bound->estimate > 0.0 &&
                 !isnan(bound->lower) && bound->lower >= 0.0 &&
                 !isnan(bound->upper) && bound->upper > 0.0;
    return result;
}

static inline int tp_retirement_validate(TpRetirementPlan const* plan, TpRetirementSeries const* series,
                                         double* workspace, size_t workspace_count)
{
    size_t expected = 0;
    int result = plan && series && plan->version == TP_RETIREMENT_STATISTICS_VERSION &&
                 plan->frozen_before_samples == 1 && plan->bootstrap_members_per_scope > 0 &&
                 plan->bootstrap_members_per_scope <= TP_RETIREMENT_MAX_BOOTSTRAP_MEMBERS_PER_SCOPE &&
                 plan->cell_members_per_scope > 0 &&
                 plan->cell_members_per_scope <= TP_RETIREMENT_MAX_CELL_MEMBERS_PER_SCOPE &&
                 plan->pairs_per_round >= TP_RETIREMENT_MIN_PAIRS_PER_ROUND &&
                 plan->pairs_per_round <= TP_RETIREMENT_MAX_PAIRS_PER_ROUND && !(plan->pairs_per_round & 1) &&
                 plan->resamples >= TP_RETIREMENT_MIN_RESAMPLES &&
                 plan->resamples <= TP_RETIREMENT_MAX_RESAMPLES &&
                 series->ratios && series->cell_count > 0 && series->cell_count <= TP_RETIREMENT_MAX_CELLS &&
                 series->member_kind < TP_RETIREMENT_MEMBER_KINDS &&
                 series->metric_index < TP_RETIREMENT_VARIABLE_METRICS &&
                 isfinite(series->limit) && series->limit > 0.0;
    if (result && series->member_kind == TP_RETIREMENT_BOOTSTRAP_MEMBER)
        result = workspace && workspace_count == plan->resamples &&
                 series->family_index < plan->bootstrap_members_per_scope &&
                 (double)plan->resamples * TP_RETIREMENT_FAMILY_ALPHA /
                 (2.0 * TP_RETIREMENT_STATISTICAL_SCOPES * TP_RETIREMENT_ALPHA_PARTITIONS *
                  (double)plan->bootstrap_members_per_scope) >= TP_RETIREMENT_MIN_EXPECTED_TAIL_DRAWS;
    else if (result)
        result = !workspace && workspace_count == 0 && series->cell_count == 1 &&
                 series->family_index < plan->cell_members_per_scope;
    if (result)
    {
        result = series->cell_count <= SIZE_MAX / (TP_RETIREMENT_ROUNDS * (size_t)plan->pairs_per_round);
        if (result) expected = (size_t)series->cell_count * TP_RETIREMENT_ROUNDS * plan->pairs_per_round;
        result = result && series->ratio_count == expected;
    }
    for (unsigned round = 0; round < TP_RETIREMENT_ROUNDS && result; ++round)
        result = series->observed_pairs[round] == plan->pairs_per_round;
    for (size_t i = 0; i < expected && result; ++i)
        result = isfinite(series->ratios[i]) && series->ratios[i] > 0.0;
    return result;
}

static inline TpRetirementResult tp_retirement_assess(TpRetirementPlan const* plan,
                                                       TpRetirementSeries const* series,
                                                       double* workspace, size_t workspace_count)
{
    TpRetirementResult result = {0};
    double block_values[TP_RETIREMENT_MAX_PAIRS_PER_ROUND];
    result.outcome = TP_RETIREMENT_INVALID;
    result.valid = tp_retirement_validate(plan, series, workspace, workspace_count);
    if (result.valid)
    {
        unsigned members = series->member_kind == TP_RETIREMENT_BOOTSTRAP_MEMBER ?
                           plan->bootstrap_members_per_scope : plan->cell_members_per_scope;
        result.resampled = series->member_kind == TP_RETIREMENT_BOOTSTRAP_MEMBER;
        result.resamples = result.resampled ? plan->resamples : 0;
        result.tail_alpha = TP_RETIREMENT_FAMILY_ALPHA /
                            (2.0 * TP_RETIREMENT_STATISTICAL_SCOPES *
                             TP_RETIREMENT_ALPHA_PARTITIONS * (double)members);
        result.valid = tp_retirement_block_values(plan, series, block_values);
        for (unsigned analysis = 0; analysis < TP_RETIREMENT_ROUNDS && result.valid; ++analysis)
            result.valid = result.resampled ?
                           tp_retirement_bootstrap_bound(plan, series, analysis, result.tail_alpha,
                                                         block_values, workspace, &result.round[analysis]) :
                           tp_retirement_exact_bound(plan, analysis, result.tail_alpha,
                                                     block_values, &result.round[analysis]);
        if (result.valid)
            result.valid = result.resampled ?
                           tp_retirement_bootstrap_bound(plan, series, TP_RETIREMENT_ROUNDS,
                                                         result.tail_alpha, block_values,
                                                         workspace, &result.pooled) :
                           tp_retirement_exact_pooled_bound(plan, result.tail_alpha,
                                                            block_values, &result.pooled);
    }
    if (result.valid)
    {
        int pass = result.pooled.upper <= series->limit;
        int regression = result.pooled.lower > series->limit;
        for (unsigned round = 0; round < TP_RETIREMENT_ROUNDS; ++round)
        {
            pass = pass && result.round[round].upper <= series->limit;
            regression = regression && result.round[round].lower > series->limit;
        }
        if (pass) result.outcome = TP_RETIREMENT_PASS;
        else if (regression) result.outcome = TP_RETIREMENT_REGRESSION;
        else result.outcome = TP_RETIREMENT_INCONCLUSIVE;
    }
    return result;
}

#endif
