// QuickJS builds its property tables with `JS_PROP_DOUBLE_DEF("NaN", NAN, 0)`
// and `JS_PROP_DOUBLE_DEF("Infinity", 1.0 / 0.0, 0)`.  Hosted <math.h> spells
// NAN as `(__builtin_nanf(""))` and INFINITY as `(__builtin_inff())`, so a
// static initializer holding either is a constant expression the constant
// evaluator has to fold -- and an IEEE divide by zero is a value, not the
// "unknown" an integer divide by zero has to answer.  The spellings are
// written out here rather than included so the fixture needs no SDK.
static const double constants[] = {
    (__builtin_nanf("")),
    (__builtin_inff()),
    -(__builtin_inff()),
    __builtin_huge_val(),
    1.0 / 0.0,
    -1.0 / 0.0,
    0.0 / 0.0,
    1.5,
};

struct entry
{
    const char *name;
    union
    {
        double number;
        int integer;
    } value;
};

static const struct entry table[] = {
    {"NaN", .value = {.number = (__builtin_nanf(""))}},
    {"Infinity", .value = {.number = 1.0 / 0.0}},
    {"answer", .value = {.integer = 42}},
};

static int is_nan(double value)
{
    return value != value;
}

// Distinct rounding points: unsigned and wide integer conversion, integer
// ties/sticky bits, f32 arithmetic, and a subsequent widening conversion.
static const double rounded[] = {
    (double)0x8000000000000000ULL,
    (double)0xffffffffffffffffULL,
    (double)((unsigned __int128)1 << 100),
    (double)(-((__int128)1 << 100)),
    (double)(((unsigned __int128)1 << 100) | 0xffffffffffffffffULL),
    (double)(float)0x8000008000000001ULL,
    (double)(float)(((unsigned __int128)1 << 100) + ((unsigned __int128)1 << 76) + 1),
    (double)(16777216.0f + 1.0f),
    (double)((16777216.0f + 1.0f) - 16777216.0f),
    (double)(1.0f / 3.0f),
    (double)((1.0f + 0x1p-24f) * 2.0f),
    (double)((1.0f + 0x1p-23f) * (1.0f + 0x1p-23f)),
};
static const double *observed_rounded = rounded;
static int check_constant_rounding(void)
{
    int failures = 0;
    const double expected[] = {
        0x1p63, 0x1p64, 0x1p100, -0x1p100,
        0x1.000000001p100, 0x1.000002p63, 0x1.000002p100,
        16777216.0, 0.0, 0x1.555556p-2, 2.0, 0x1.000004p0,
    };
    for (unsigned i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i)
        failures += observed_rounded[i] != expected[i];
    return failures;
}

int main(void)
{
    if (check_constant_rounding()) return 14;
    if (!is_nan(constants[0])) return 1;
    if (constants[1] <= 1.0e308 || is_nan(constants[1])) return 2;
    if (constants[2] >= -1.0e308 || is_nan(constants[2])) return 3;
    if (constants[3] != constants[1]) return 4;
    if (constants[4] != constants[1]) return 5;
    if (constants[5] != constants[2]) return 6;
    if (!is_nan(constants[6])) return 7;
    if (constants[7] != 1.5) return 8;
    if (!is_nan(table[0].value.number)) return 9;
    if (table[1].value.number != constants[1]) return 10;
    if (table[2].value.integer != 42) return 11;
    // The created NaN's sign is canonical, so a folded 0.0/0.0 has the same
    // bytes the reference compiler writes.
    if (__builtin_signbit(constants[6])) return 12;
    if (__builtin_signbit(constants[0])) return 13;
    return 0;
}
