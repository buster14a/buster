/* Test-only exact dyadic -> binary16 oracle. No production helpers or FP math.
 * Scope: finite magnitudes <= 65504, round-to-nearest/ties-to-even.
 * Map: compare_dyadic / half_units -> rounded_half; make_cases -> emit/check.
 * Seeded faults in self_test are detector controls, not production findings.
 */
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct ExactCase
{
    uint64_t numerator;
    int exponent;
    unsigned negative;
    unsigned control;
} ExactCase;

enum { MAX_CASES = 128, MAX_FINITE = 0x7bff };

static unsigned width(uint64_t value)
{
    unsigned result = 0;
    while (value != 0) { value >>= 1; ++result; }
    return result;
}

/* Both coefficients are at most 64 bits. Equal leading exponents let us
 * align to the larger coefficient width without overflowing that width. */
static int compare_dyadic(uint64_t a, int ae, uint64_t b, int be)
{
    int result;
    if (a == 0 || b == 0)
    {
        result = (a != 0) - (b != 0);
    }
    else
    {
        unsigned aw = width(a), bw = width(b);
        int atop = (int)aw + ae, btop = (int)bw + be;
        if (atop != btop) { result = atop < btop ? -1 : 1; }
        else
        {
            if (aw < bw) { a <<= bw - aw; }
            if (bw < aw) { b <<= aw - bw; }
            result = (a > b) - (a < b);
        }
    }
    return result;
}

/* All positive finite halves are integers in units of 2^-24. */
static uint64_t half_units(unsigned encoding)
{
    unsigned exponent = encoding >> 10;
    uint64_t result;
    if (exponent == 0) { result = encoding; }
    else { result = (uint64_t)(1024 + (encoding & 1023)) << (exponent - 1); }
    return result;
}

static uint16_t rounded_half(ExactCase value)
{
    unsigned lo = 0, hi = MAX_FINITE + 1;
    uint16_t result;
    if (compare_dyadic(value.numerator, value.exponent,
                       half_units(MAX_FINITE), -24) > 0)
    {
        result = UINT16_MAX; /* Deliberately unsupported: no overflow oracle. */
    }
    else
    {
        while (lo + 1 < hi)
        {
            unsigned middle = lo + (hi - lo) / 2;
            if (compare_dyadic(value.numerator, value.exponent,
                              half_units(middle), -24) >= 0) { lo = middle; }
            else { hi = middle; }
        }
        if (lo != MAX_FINITE)
        {
            uint64_t midpoint_coefficient = half_units(lo) + half_units(lo + 1);
            int side = compare_dyadic(value.numerator, value.exponent,
                                     midpoint_coefficient, -25);
            if (side > 0 || (side == 0 && (lo & 1) != 0)) { ++lo; }
        }
        result = (uint16_t)(lo | (value.negative ? 0x8000 : 0));
    }
    return result;
}

/* Test-only injected information-loss fault: discard exact bits at a binary64
 * boundary before using the otherwise-correct half oracle. This is a model,
 * not a copy of Buster's parser and not evidence Buster executes this path. */
static ExactCase seed_binary64_intermediate(ExactCase value)
{
    unsigned bits = width(value.numerator);
    if (bits > 53)
    {
        unsigned drop = bits - 53;
        uint64_t q = value.numerator >> drop;
        uint64_t remainder = value.numerator & ((UINT64_C(1) << drop) - 1);
        uint64_t midpoint = UINT64_C(1) << (drop - 1);
        q += remainder > midpoint || (remainder == midpoint && (q & 1) != 0);
        value.numerator = q;
        value.exponent += (int)drop;
    }
    return value;
}

static unsigned make_cases(const char* family, ExactCase cases[MAX_CASES])
{
    unsigned count = 0;
    if (strcmp(family, "design") == 0)
    {
        static const ExactCase controls[] = {
            {0, 0, 0, 1}, {0, 0, 1, 1}, {1, 0, 0, 1}, {1, 0, 1, 1},
            {1, -24, 0, 1}, {1, -24, 1, 1}, {1023, -24, 0, 1},
            {1024, -24, 0, 1}, {2047, -24, 0, 1}, {1, -25, 0, 1},
            {3, -25, 0, 1}, {65504, 0, 0, 1}, {65504, 0, 1, 1}
        };
        static const unsigned neighbors[] = {0, 1, 31, 511};
        static const int binades[] = {-10, 0, 8};
        memcpy(cases, controls, sizeof(controls));
        count = (unsigned)(sizeof(controls) / sizeof(controls[0]));
        for (unsigned e = 0; e < 3; ++e)
        {
            for (unsigned k = 0; k < 4; ++k)
            {
                for (int delta = -1; delta <= 1; ++delta)
                {
                    for (unsigned sign = 0; sign < 2; ++sign)
                    {
                        uint64_t n = (uint64_t)(2049 + 2 * neighbors[k]) << 45;
                        n = delta < 0 ? n - 1 : n + (unsigned)delta;
                        cases[count++] = (ExactCase){n, binades[e] - 56, sign, delta == 0};
                    }
                }
            }
        }
    }
    else if (strcmp(family, "heldout") == 0)
    {
        /* Freeze this subnormal family before interpreting design results. */
        static const unsigned neighbors[] = {0, 1, 7, 31};
        for (unsigned k = 0; k < 4; ++k)
        {
            for (int delta = -1; delta <= 1; ++delta)
            {
                for (unsigned sign = 0; sign < 2; ++sign)
                {
                    uint64_t n = (uint64_t)(2 * neighbors[k] + 1) << 55;
                    n = delta < 0 ? n - 1 : n + (unsigned)delta;
                    cases[count++] = (ExactCase){n, -80, sign, delta == 0};
                }
            }
        }
    }
    return count;
}

static int self_test(void)
{
    static const struct { ExactCase value; uint16_t expected; } calibration[] = {
        {{0,0,0,1},0x0000}, {{0,0,1,1},0x8000},
        {{1,0,0,1},0x3c00}, {{1,0,1,1},0xbc00},
        {{1,-24,0,1},0x0001}, {{1,-25,0,1},0x0000}, {{3,-25,0,1},0x0002},
        {{2049,-11,0,1},0x3c00}, {{2051,-11,0,1},0x3c02},
        {{(UINT64_C(2049)<<45)+1,-56,0,0},0x3c01},
        {{(UINT64_C(2049)<<45)-1,-56,0,0},0x3c00},
        {{(UINT64_C(2049)<<45)+1,-56,1,0},0xbc01},
        {{65504,0,0,1},0x7bff}
    };
    unsigned failures = 0, sticky_detected = 0, tie_detected = 0;
    unsigned sign_detected = 0, flush_detected = 0, control_errors = 0;
    for (unsigned i = 0; i < sizeof(calibration)/sizeof(calibration[0]); ++i)
    {
        ExactCase value = calibration[i].value;
        uint16_t expected = calibration[i].expected;
        uint16_t observed = rounded_half(value);
        failures += observed != expected;
        uint16_t sticky = rounded_half(seed_binary64_intermediate(value));
        sticky_detected += sticky != expected;
        control_errors += value.control && sticky != expected;
        /* Bounded corrupted-observation controls; never patch production. */
        uint16_t sign = observed & UINT16_C(0x7fff);
        uint16_t flush = (observed & 0x7c00) == 0 ? observed & 0x8000 : observed;
        uint16_t tie = i == 7 ? UINT16_C(0x3c01) : observed;
        sign_detected += sign != expected;
        flush_detected += flush != expected;
        tie_detected += tie != expected;
    }
    printf("calibration_failures=%u negative_control_errors=%u "
           "seed_binary64_detected=%u seed_ties_away_detected=%u "
           "seed_sign_loss_detected=%u seed_flush_detected=%u\n",
           failures, control_errors, sticky_detected, tie_detected, sign_detected, flush_detected);
    int result = failures != 0 || control_errors != 0 || sticky_detected == 0 ||
                 tie_detected == 0 || sign_detected == 0 || flush_detected == 0;
    return result;
}

static int emit(const char* path, const ExactCase* cases, unsigned count)
{
    FILE* file = fopen(path, "w");
    int result = 2;
    if (file != NULL)
    {
        fprintf(file, "/* Exact hexadecimal tokens: no host floating formatting. */\n");
        fprintf(file, "const unsigned oracle_count = %u;\nconst _Float16 oracle_values[] = {\n", count);
        for (unsigned i = 0; i < count; ++i)
        {
            fprintf(file, "    %s0x%" PRIx64 "p%+dF16,\n",
                    cases[i].negative ? "-" : "", cases[i].numerator, cases[i].exponent);
        }
        fprintf(file, "};\n");
        result = ferror(file) != 0 ? 2 : 0;
        if (fclose(file) != 0) { result = 2; }
    }
    return result;
}

static int check(const char* family, const char* path, const ExactCase* cases, unsigned count)
{
    FILE* file = fopen(path, "r");
    int result = 2;
    if (file != NULL)
    {
        unsigned mismatches = 0, controls = 0, control_errors = 0, read_count = 0;
        int valid = 1;
        puts("family,index,control,numerator_hex,exponent,negative,expected,observed,result");
        for (unsigned i = 0; i < count && valid; ++i)
        {
            unsigned index = 0, observed = 0;
            valid = fscanf(file, "%u %x", &index, &observed) == 2 && index == i && observed <= 0xffff;
            if (valid)
            {
                uint16_t expected = rounded_half(cases[i]);
                unsigned mismatch = expected != observed;
                mismatches += mismatch;
                controls += cases[i].control;
                control_errors += cases[i].control && mismatch;
                ++read_count;
                printf("%s,%u,%u,%" PRIx64 ",%d,%u,%04x,%04x,%s\n",
                       family, i, cases[i].control, cases[i].numerator, cases[i].exponent,
                       cases[i].negative, (unsigned)expected, observed, mismatch ? "FAIL" : "PASS");
            }
        }
        char extra[2];
        if (valid) { valid = fscanf(file, "%1s", extra) == EOF && !ferror(file); }
        if (fclose(file) != 0) { valid = 0; }
        fprintf(stderr, "family=%s rows=%u/%u mismatches=%u controls=%u control_errors=%u valid_input=%d\n",
                family, read_count, count, mismatches, controls, control_errors, valid);
        result = !valid ? 2 : mismatches != 0;
    }
    return result;
}

int main(int argc, char** argv)
{
    int result = 2;
    if (argc == 2 && strcmp(argv[1], "self-test") == 0) { result = self_test(); }
    else if (argc == 4)
    {
        ExactCase cases[MAX_CASES];
        unsigned count = make_cases(argv[2], cases);
        if (count != 0 && strcmp(argv[1], "emit") == 0) { result = emit(argv[3], cases, count); }
        else if (count != 0 && strcmp(argv[1], "check") == 0) { result = check(argv[2], argv[3], cases, count); }
    }
    if (result == 2) { fputs("Usage: half_oracle self-test | (emit|check) (design|heldout) FILE\n", stderr); }
    return result;
}
