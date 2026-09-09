/* Compiler-throughput statistics. No timing, I/O, or platform dependencies.
 * tp_assess: exact one-sided paired sign test of a practical-effect margin.
 * tp_interval: distribution-free order-statistic interval for the median.
 * Rows are NEVER winsorized, trimmed, or selected by their observed result.
 */
#ifndef BUSTER_THROUGHPUT_STATS_H
#define BUSTER_THROUGHPUT_STATS_H
#include <math.h>
#include <stddef.h>
#include <stdint.h>

#define TP_MAX_PAIRS 256

typedef struct TpAssessment
{
    double baseline_median;
    double candidate_median;
    double ratio_median;
    double ratio_low;
    double ratio_high;
    double baseline_relative_mad;
    double candidate_relative_mad;
    double p_value;
    unsigned wins;
    unsigned count;
    int valid;
    int regression;
} TpAssessment;

/* Bounded small sample arrays: insertion sort avoids callback dispatch. */
static void tp_sort(double* values, unsigned count)
{
    for (unsigned i = 1; i < count; ++i)
    {
        double value = values[i];
        unsigned j = i;
        while (j && values[j - 1] > value)
        {
            values[j] = values[j - 1];
            --j;
        }
        values[j] = value;
    }
}

static double tp_median_sorted(double const* values, unsigned count)
{
    double result = NAN;
    if (count)
    {
        result = values[count / 2];
        if (!(count & 1))
        {
            result = values[count / 2 - 1] * 0.5 + result * 0.5;
        }
    }
    return result;
}

/* P[Binomial(n, 1/2) >= successes], including ties as non-successes.
 * Powers of two are exact and n <= 256 keeps all terms representable.
 */
static double tp_sign_tail(unsigned n, unsigned successes)
{
    double term = ldexp(1.0, -(int)n);
    double result = 0.0;
    for (unsigned k = 0; k <= n; ++k)
    {
        if (k >= successes)
        {
            result += term;
        }
        term *= (double)(n - k) / (double)(k + 1);
    }
    return result;
}

/* A conservative two-sided (1-alpha) confidence interval for the population
 * median, under independent pairs. Infinity is honest when n is too small.
 * P[Bin(n,.5) <= k] <= alpha/2 selects [x[k], x[n-k-1]].
 */
static void tp_interval(double const* sorted, unsigned n, double alpha, double* low, double* high)
{
    *low = 0.0;
    *high = INFINITY;
    for (unsigned k = 0; k < n / 2; ++k)
    {
        if (tp_sign_tail(n, n - k) <= alpha * 0.5)
        {
            *low = sorted[k];
            *high = sorted[n - k - 1];
        }
    }
}

static double tp_relative_mad(double const* values, unsigned n, double median)
{
    double deviations[TP_MAX_PAIRS];
    for (unsigned i = 0; i < n; ++i)
    {
        deviations[i] = fabs(values[i] - median);
    }
    tp_sort(deviations, n);
    return tp_median_sorted(deviations, n) / median;
}

static TpAssessment tp_assess(double const* baseline, double const* candidate, unsigned n,
                              double relative_margin, double absolute_margin, double alpha)
{
    TpAssessment result = {0};
    double a[TP_MAX_PAIRS], b[TP_MAX_PAIRS], ratios[TP_MAX_PAIRS];
    result.valid = n >= 1 && n <= TP_MAX_PAIRS && relative_margin >= 0.0 &&
                   absolute_margin >= 0.0 && alpha > 0.0 && alpha < 1.0;
    if (result.valid)
    {
        for (unsigned i = 0; i < n; ++i)
        {
            if (!(isfinite(baseline[i]) && isfinite(candidate[i]) && baseline[i] > 0.0 && candidate[i] > 0.0))
            {
                result.valid = 0;
                break;
            }
            a[i] = baseline[i];
            b[i] = candidate[i];
            ratios[i] = b[i] / a[i];
            if (!isfinite(ratios[i]))
            {
                result.valid = 0;
                break;
            }
            /* Both thresholds must be crossed in this pair, not just the
             * overall medians. No effect/noise-dependent choice of a test. */
            if (b[i] > a[i] * (1.0 + relative_margin) && b[i] - a[i] > absolute_margin)
            {
                ++result.wins;
            }
        }
    }
    if (result.valid)
    {
        tp_sort(a, n);
        tp_sort(b, n);
        tp_sort(ratios, n);
        result.count = n;
        result.baseline_median = tp_median_sorted(a, n);
        result.candidate_median = tp_median_sorted(b, n);
        result.ratio_median = tp_median_sorted(ratios, n);
        result.baseline_relative_mad = tp_relative_mad(a, n, result.baseline_median);
        result.candidate_relative_mad = tp_relative_mad(b, n, result.candidate_median);
        tp_interval(ratios, n, alpha, &result.ratio_low, &result.ratio_high);
        result.p_value = tp_sign_tail(n, result.wins);
        result.regression = result.p_value <= alpha;
    }
    return result;
}
#endif
